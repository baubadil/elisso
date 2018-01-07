/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/fsmodel.h"

#include "elisso/thumbnailer.h"
#include "xwp/debug.h"
#include "xwp/stringhelp.h"
#include "xwp/except.h"
#include "xwp/regex.h"
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <set>


/***************************************************************************
 *
 *  Globals, static variable instantiations
 *
 **************************************************************************/

atomic<uint64_t>  g_uFSID(1);

PCurrentDirectory CurrentDirectory::s_theCWD = NULL;


/***************************************************************************
 *
 *  FSContainer::Impl definition
 *
 **************************************************************************/

typedef map<const string, PFSModelBase> FilesMap;
typedef list<PFSMonitorBase> FSMonitorsList;

struct FSContainer::Impl
{
    Mutex           contentsMutex;
    FilesMap        mapContents;
    FSMonitorsList  llMonitors;

    /**
     *  Tests if a file-system object with the given name has already been instantiated in this
     *  container. If so, it is returned. If this returns nullptr instead, that doesn't mean
     *  that the file doesn't exist becuase the container might not be fully populated, or
     *  directory contents may have changed since.
     */
    PFSModelBase
    isAwake(ContentsLock &lock,
            const string &strParticle,
            FilesMap::iterator *pIt)
    {
        FilesMap::iterator it = mapContents.find(strParticle);
        if (it != mapContents.end())
        {
            if (pIt)
                *pIt = it;
            return it->second;
        }

        return nullptr;
    }

    void removeImpl(ContentsLock &lock,
                    FilesMap::iterator it)
    {
        auto p = it->second;
        mapContents.erase(it);
        p->_pParent = nullptr;
        p->clearFlag(FSFlag::IS_LOCAL);
    }
};


/***************************************************************************
 *
 *  FSLock
 *
 **************************************************************************/

Mutex g_mutexFiles;

/**
 *  Global lock for the whole file-system model. This is used to protect
 *  instance data of FSModelBase and the derived classes. Since the
 *  file-system model is designed to be thread-safe, this lock must always
 *  be held when reading or modifying instance data.
 *
 *  This is a global lock so it must only ever be held for a very short
 *  amount of time, say, a few instructions.
 *
 *  The one exception is ContentsLock, see below.
 *
 *  To avoid deadlocks, never hold FSLock when requesting a ContentsLock.
 *
 *  Again, do not hold this for a long time since this will block all
 *  file operations. If you have something that takes longer but needs
 *  to be atomic, use a mechanism like in FSSymlink::follow(), which
 *  introduces a per-object state flag (which is protected by FSLock)
 *  together with a condition variable.
 */
class FSLock : public XWP::Lock
{
public:
    FSLock()
        : Lock(g_mutexFiles)
    { }
};

/**
 *  ContentsLock is a more specialized lock to protect the list of
 *  children in a FSContainer. Since iterating over the contents
 *  list can take longer than we want to hold FSLock, this is
 *  a) more specialized and b) not global, but an instance of this
 *  exists in every FSContainer::ContentsMap.
 *
 *  This must be held when reading or modifying the following two
 *  items:
 *
 *   a) anything in a container's ContentsMap;
 *
 *   b) the _pParent pointer of any child of a container.
 *
 *  To reiterate, the FSModelBase::_pParent pointer is NOT protected
 *  by FSLock, but by the parent's ContentsLock since the two always
 *  get modified together.
 *
 *  To avoid deadlocks, never hold FSLock when requesting a ContentsLock.
 */
class ContentsLock : public XWP::Lock
{
public:
    ContentsLock(FSContainer& cnr)
        : Lock(cnr._pImpl->contentsMutex)
    { }
};


/***************************************************************************
 *
 *  FSMonitorBase
 *
 **************************************************************************/

void
FSMonitorBase::startWatching(FSContainer &cnr)
{
    FSLock lock;
    if (_pContainer)
    {
        if (_pContainer != &cnr)
            throw FSException("Monitor is already busy with another container");
    }
    else
    {
        cnr._pImpl->llMonitors.push_back(shared_from_this());
        _pContainer = &cnr;
    }
}

void
FSMonitorBase::stopWatching(FSContainer &cnr)
{
    FSLock lock;
    if (_pContainer != &cnr)
        throw FSException("Cannot remove monitor as it's not active for this container");

    _pContainer = nullptr;
    cnr._pImpl->llMonitors.remove(shared_from_this());
}


/***************************************************************************
 *
 *  FSModelBase
 *
 **************************************************************************/

/* static */
PFSModelBase
FSModelBase::FindPath(const string &strPath0)
{
    Debug d(FILE_LOW, __func__ + string("(" + strPath0 + ")"));

    string strPath;
    static Regex s_reScheme(R"i____(^([-+a-z]+)://(.*))i____");
    RegexMatches aMatches;
    string strScheme;
    if (s_reScheme.matches(strPath0, aMatches))
    {
        strScheme = aMatches.get(1);
        strPath = aMatches.get(2);
        Debug::Log(FILE_LOW, "explicit scheme=" + quote(strScheme) + ", path=" + quote(strPath));
    }
    else
    {
        strScheme = "file";
        strPath = strPath0;
        Debug::Log(FILE_LOW, "implicit scheme=" + quote(strScheme) + ", path=" + quote(strPath));
    }

    string strPathSplit;
    bool fAbsolute;
    PFSModelBase pCurrent;
    if ((fAbsolute = (strPath[0] == '/')))
    {
        strPathSplit = strPath.substr(1);
        if (strPathSplit.empty())       // path == "/" case
            pCurrent = RootDirectory::Get(strScheme);       // And this will be returned, as aParticles will be empty.

    }
    else
        strPathSplit = strPath;

    StringVector aParticles = explodeVector(strPathSplit, "/");
    Debug::Log(FILE_LOW, to_string(aParticles.size()) + " particle(s) given");

    // Do not hold any locks in this method. We iterate over the path on the stack
    // and call into FSContainer::find(), which has proper locking.

    uint c = 0;
    for (auto const &strParticle : aParticles)
    {
        Debug::Log(FILE_LOW, "Particle: " + quote(strParticle));
        if (strParticle == ".")
        {
            if (aParticles.size() > 1)
                Debug::Log(FILE_LOW, "Ignoring particle . in list");
            else
            {
                pCurrent = CurrentDirectory::GetImpl();
                break;
            }
        }
        else
        {
            FSContainer *pDir = nullptr;
            bool fCollapsing = false;
            if (!pCurrent)
            {
                if (fAbsolute)
                {
                    // First item on an absolute path must be a child of the root directory.
                    auto pRoot = RootDirectory::Get(strScheme);     // This throws on errors.
                    pDir = pRoot->getContainer();
                    Debug::Log(FILE_LOW, "got root dir " + quote(pRoot->getBasename()));
                }
                else
                    // First item on a relative path must be a child of the curdir.
                    pDir = CurrentDirectory::GetImpl()->getContainer();
            }
            else
            {
                // Later particles:

                if (strParticle == "..")
                {
                    // Avoid things like ./../subdir1/../subdir2/
                    //                                ^ we would create pChild here
                    //                        ^ this is pCurrent at this time
                    //           =>      ./../subdir2/
                    fCollapsing = true;

                    auto pPrev = pCurrent;

                    // Go back to the parent and skip over the rest of this step.
                    pCurrent = pCurrent->_pParent;

                    Debug::Log(FILE_LOW, "Loop " + to_string(c) + ": collapsed " + quote(pPrev->getPath() + "/" + strParticle) + " to " + quote(pCurrent->getPath()));
                }
                else if (!(pDir = pCurrent->getContainer()))
                    // Particle is a broken symlink.
                    break;
//                     throw FSException("path particle \"" + pCurrent->getPath() + "\" cannot have contents");
            }

            if (!fCollapsing)
            {
                // The following can throw.
                if (!(pCurrent = pDir->find(strParticle)))
                {
                    Debug::Log(FILE_LOW, "Directory::find() returned nullptr");
                    break;
                }
            }
            ++c;
        }
    }

    d.setExit("Result: " + (pCurrent ? pCurrent->describe(true) : "NULL"));

    return pCurrent;
}

/* static */
PFSModelBase
FSModelBase::MakeAwake(Glib::RefPtr<Gio::File> pGioFile)
{
    PFSModelBase pReturn = nullptr;
    static string star("*");

    try
    {
        static string s_attrs = string(G_FILE_ATTRIBUTE_STANDARD_TYPE) + string(",") + string(G_FILE_ATTRIBUTE_STANDARD_SIZE);
        // The following can throw Gio::Error.
        auto pInfo = pGioFile->query_info(s_attrs,
                                          Gio::FileQueryInfoFlags::FILE_QUERY_INFO_NOFOLLOW_SYMLINKS);
//         auto type = pGioFile->query_file_type(Gio::FileQueryInfoFlags::FILE_QUERY_INFO_NOFOLLOW_SYMLINKS);

        switch (pInfo->get_file_type())
        {
            case Gio::FileType::FILE_TYPE_REGULAR:         // File handle represents a regular file.
                pReturn = FSFile::Create(pGioFile, pInfo->get_size());
            break;

            case Gio::FileType::FILE_TYPE_DIRECTORY:       // File handle represents a directory.
                Debug::Log(FILE_LOW, "  creating FSDirectory");
                pReturn = FSDirectory::Create(pGioFile);
            break;

            case Gio::FileType::FILE_TYPE_SYMBOLIC_LINK:   // File handle represents a symbolic link (Unix systems).
            case Gio::FileType::FILE_TYPE_SHORTCUT:        // File is a shortcut (Windows systems).
                pReturn = FSSymlink::Create(pGioFile);
            break;

            case Gio::FileType::FILE_TYPE_SPECIAL:         // File is a "special" file, such as a socket, fifo, block device, or character device.
                pReturn = FSSpecial::Create(pGioFile);
            break;

            case Gio::FileType::FILE_TYPE_MOUNTABLE:       // File is a mountable location.
                pReturn = FSMountable::Create(pGioFile);
            break;

            case Gio::FileType::FILE_TYPE_NOT_KNOWN:       // File's type is unknown. This is what we get if the file does not exist.
                Debug::Log(FILE_HIGH, "file type not known");
            break;      // return nullptr
        }
    }
    catch (Gio::Error &e)
    {
        Debug::Log(FILE_HIGH, "FSModelBase::MakeAwake(): got Gio::Error: " + e.what());
        throw FSException(e.what());
    }

    if (!pReturn)
        throw FSException("Unknown error waking up file-system object");

    return pReturn;
}

/* static */
PFSDirectory
FSModelBase::FindDirectory(const string &strPath)
{
    if (auto pFS = FindPath(strPath))
    {
        Debug::Log(FILE_MID, string(__func__) + "(" + quote(strPath) + ") => " + pFS->describe(true));
        if (pFS->getType() == FSType::DIRECTORY)
            return static_pointer_cast<FSDirectory>(pFS);
    }

    return nullptr;
}

FSModelBase::FSModelBase(FSType type,
                         Glib::RefPtr<Gio::File> pGioFile,
                         uint64_t cbSize)
    : _uID(g_uFSID++),      // atomic
      _type(type),
      _strBasename(pGioFile->get_basename()),
      _cbSize(cbSize)
{
    _pIcon = pGioFile->query_info()->get_icon();
}

PGioFile FSModelBase::getGioFile()
{
    auto strPath = getPath();

    if (hasFlag(FSFlag::IS_LOCAL))
        return Gio::File::create_for_path(strPath.substr(7));

    return Gio::File::create_for_uri(strPath);
}

bool
FSModelBase::isHidden()
{
    FSLock lock;
    if (!_fl.test(FSFlag::HIDDEN_CHECKED))
    {
        auto len = _strBasename.length();
        if (!len)
            _fl.set(FSFlag::HIDDEN);
        else
            if (    (_strBasename[0] == '.')
                 || (_strBasename[len - 1] == '~')
               )
                _fl.set(FSFlag::HIDDEN);

        _fl.set(FSFlag::HIDDEN_CHECKED);
    }

    return _fl.test(FSFlag::HIDDEN);

//     auto pInfo = _pGioFile->query_info(G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
//                                        Gio::FileQueryInfoFlags::FILE_QUERY_INFO_NOFOLLOW_SYMLINKS);
//     return pInfo->is_hidden();
}

string
FSModelBase::getPath() const
{
    if (_fl.test(FSFlag::IS_ROOT_DIRECTORY))
        return _strBasename + "/";

    return getPathImpl();
}

string
FSModelBase::getPathImpl() const
{
    string strFullpath;

    if (_pParent)
    {
        // If we have a parent, recurse FIRST.
        strFullpath = _pParent->getPathImpl();
        if (strFullpath != "/")
            strFullpath += '/';
    }

    return strFullpath + getBasename();
}

Glib::ustring
FSModelBase::getIcon() const
{
    return _pIcon->to_string();
}

PFSFile
FSModelBase::getFile()
{
    if (_type == FSType::FILE)
        return static_pointer_cast<FSFile>(shared_from_this());
    if (getResolvedType() == FSTypeResolved::SYMLINK_TO_FILE)
        return static_pointer_cast<FSFile>((static_cast<FSSymlink*>(this))->getTarget());

    return nullptr;
}

PFSModelBase
FSModelBase::getParent() const
{
    if (_pParent)
        ;
    else if (_fl.test(FSFlag::IS_ROOT_DIRECTORY))
        ;       // return NULL;

    return _pParent;
}

bool
FSModelBase::isUnder(PFSDirectory pDir) const
{
    auto p = getParent();
    while (p)
    {
        if (p == pDir)
            return true;
        p = p->getParent();
    }

    return false;
}

FSContainer*
FSModelBase::getContainer()
{
    if (getType() == FSType::DIRECTORY)
        return (static_cast<FSDirectory*>(this));

    if (getResolvedType() == FSTypeResolved::SYMLINK_TO_DIRECTORY)
        return (static_cast<FSSymlink*>(this));

    return nullptr;
}

const string g_strFile("file");
const string g_strDirectory("directory");
const string g_strSymlink("symlink");
const string g_strOther("other");

const string&
FSModelBase::describeType() const
{
    switch (_type)
    {
        case FSType::FILE:
            return g_strFile;

        case FSType::DIRECTORY:
            return g_strDirectory;

        case FSType::SYMLINK:
            return g_strSymlink;

        case FSType::UNINITIALIZED:
        case FSType::MOUNTABLE:
        case FSType::SPECIAL:
            break;
    }

    return g_strOther;
}

string
FSModelBase::describe(bool fLong /* = false */ ) const
{
    return  describeType() + " \"" + (fLong ? getPath() : getBasename()) + "\" (#" + to_string(_uID) + ")";
}

void
FSModelBase::rename(const string &strNewName)
{
    auto pCnr = _pParent->getContainer();
    if (pCnr)
        try
        {
            auto pGioFile = getGioFile();
            pGioFile->set_display_name(strNewName);

            // Update the contents map, which sorts by name.
            ContentsLock cLock(*pCnr);
            // First remove the old item; this calls getBasename(), which still has the old base name.
            pCnr->removeChild(cLock, shared_from_this());

            _strBasename = strNewName;
            pCnr->addChild(cLock, shared_from_this());
        }
        catch (Gio::Error &e)
        {
            throw FSException(e.what());
        }
}

void
FSModelBase::sendToTrash()
{
    try
    {
        auto pGioFile = getGioFile();
        if (!pGioFile)
            throw FSException("cannot get GIO file");

        auto pParent = getParent();
        if (!pParent)
            throw FSException("cannot get parent for trashing");
        auto pParentCnr = pParent->getContainer();
        if (!pParentCnr)
            throw FSException("cannot get parent container for trashing");

        {
            ContentsLock cLock(*pParentCnr);
            pParentCnr->removeChild(cLock, shared_from_this());
        }

        pGioFile->trash();
    }
    catch (Gio::Error &e)
    {
        throw FSException(e.what());
    }
}

void
FSModelBase::moveTo(PFSModelBase pTarget)
{
    copyOrMoveImpl(pTarget,
                   false);      // fIsCopy
}

PFSModelBase
FSModelBase::copyTo(PFSModelBase pTarget)
{
    return copyOrMoveImpl(pTarget,
                          true);      // fIsCopy
}

PFSModelBase
FSModelBase::copyOrMoveImpl(PFSModelBase pTarget,
                            bool fIsCopy)
{
    PFSModelBase pReturn;

    Debug d(FILE_HIGH, __func__, "(" + quote(getBasename()) + "), target=" + quote(pTarget->getBasename()));
    try
    {
        auto pParent = getParent();
        if (!pParent)
            throw FSException("cannot get parent for moving");

        auto pParentCnr = pParent->getContainer();
        if (!pParentCnr)
            throw FSException("cannot get parent container for moving");

//         {
//             ContentsLock cLock(*pParentCnr);
//             pParentCnr->dumpContents("Before copy/move, contents of ", cLock);
//         }

        auto pTargetCnr = pTarget->getContainer();
        if (!pTargetCnr)
            throw FSException("cannot get target container for moving");

        auto pGioFile = getGioFile();
        string strTargetPath = pTarget->getPath();
        strTargetPath += "/" + this->getBasename();
        auto pTargetGioFile = Gio::File::create_for_uri(strTargetPath);

        if (fIsCopy)
        {
            // This is a copy:

            Debug::Log(FILE_HIGH, "pGioFile->copy(" + quote(pGioFile->get_path()) + " to " + quote(pTargetGioFile->get_path()) + ")");

            pGioFile->copy(pTargetGioFile,
                           Gio::FileCopyFlags::FILE_COPY_NOFOLLOW_SYMLINKS      // copy symlinks as symlinks
                /*| Gio::FileCopyFlags::FILE_COPY_NO_FALLBACK_FOR_MOVE */);

            if (!(pReturn = pTargetCnr->find(this->getBasename())))
                throw FSException("Cannot find copied file in destination after copying");
        }
        else
        {
            // This is a move:
            {
                ContentsLock cLock(*pParentCnr);
                pParentCnr->removeChild(cLock, shared_from_this());
            }

            Debug::Log(FILE_HIGH, "pGioFile->move(" + quote(pGioFile->get_path()) + " to " + quote(pTargetGioFile->get_path()) + ")");
            pGioFile->move(pTargetGioFile,
                           Gio::FileCopyFlags::FILE_COPY_NOFOLLOW_SYMLINKS      // copy symlinks as symlinks
                            /*| Gio::FileCopyFlags::FILE_COPY_NO_FALLBACK_FOR_MOVE */);

            {
                ContentsLock cLock(*pTargetCnr);
                pParentCnr->addChild(cLock, shared_from_this());
            }
        }

    }
    catch (Gio::Error &e)
    {
        d.setExit("Caught Gio::Error: " + e.what());
        throw FSException(e.what());
    }

    return pReturn;
}

void
FSModelBase::testFileOps()
{
    XWP::Thread::Sleep(50);
}


/***************************************************************************
 *
 *  FSFile
 *
 **************************************************************************/

struct FSFile::ThumbData
{
    map<uint32_t, PPixbuf> mapThumbnails;
};

/* static */
PFSFile
FSFile::Create(Glib::RefPtr<Gio::File> pGioFile, uint64_t cbSize)
{
    /* This nasty trickery is necessary to make make_shared work with a protected constructor. */
    class Derived : public FSFile
    {
    public:
        Derived(Glib::RefPtr<Gio::File> pGioFile, uint64_t cbSize) : FSFile(pGioFile, cbSize) { }
    };

    return make_shared<Derived>(pGioFile, cbSize);
}

/* virtual */
FSFile::~FSFile()
{
    FSLock lock;
    if (_pThumbData)
        delete _pThumbData;
}

void
FSFile::setThumbnail(uint32_t thumbsize, PPixbuf ppb)
{
    FSLock lock;
    if (ppb)
    {
        if (!_pThumbData)
            _pThumbData = new ThumbData;

        _pThumbData->mapThumbnails[thumbsize] = ppb;
    }
    else
        // nullptr:
        if (_pThumbData)
        {
            auto it = _pThumbData->mapThumbnails.find(thumbsize);
            if (it != _pThumbData->mapThumbnails.end())
                _pThumbData->mapThumbnails.erase(it);
        }
}

PPixbuf
FSFile::getThumbnail(uint32_t thumbsize) const
{
    PPixbuf ppb;

    FSLock lock;
    if (_pThumbData)
    {
        auto it = _pThumbData->mapThumbnails.find(thumbsize);
        if (it != _pThumbData->mapThumbnails.end())
            ppb = it->second;
    }

    return ppb;
}


/***************************************************************************
 *
 *  FSContainer
 *
 **************************************************************************/

FSContainer::FSContainer(FSModelBase &refBase)
    : _pImpl(new Impl),
      _refBase(refBase)
{
}

/**
 *  Constructor. This is protected because an FSContainer only ever gets created through
 *  multiple inheritance.
 *
 *  We delete the implementation, but MUST clear the list first under the protection of
 *  the contents map.
 */
/* virtual */
FSContainer::~FSContainer()
{
    {
        ContentsLock lock(*this);
        _pImpl->mapContents.clear();
    }

    delete _pImpl;
}

void FSContainer::addChild(ContentsLock &lock, PFSModelBase p)
{
    const string &strBasename = p->getBasename();
    Debug::Log(FILE_LOW, "storing \"" + strBasename + "\" in parent map");

    if (p->_pParent)
        throw FSException("addChild() called for a child who already has a parent");

    _pImpl->mapContents[strBasename] = p;

    // Propagate DIR_IS_LOCAL from parents.
    if (_refBase.hasFlag(FSFlag::IS_LOCAL))
        p->setFlag(FSFlag::IS_LOCAL);

    p->_pParent = _refBase.getSharedFromThis();
}

void
FSContainer::removeChild(ContentsLock &lock, PFSModelBase p)
{
    auto it = _pImpl->mapContents.find(p->getBasename());
    if (it == _pImpl->mapContents.end())
        throw FSException("internal: cannot find myself in parent");

    _pImpl->removeImpl(lock, it);
}

void
FSContainer::dumpContents(const string &strIntro, ContentsLock &lock)
{
    if (g_flDebugSet & FOLDER_POPULATE_LOW)
    {
        Debug::Log(FOLDER_POPULATE_LOW, strIntro + _refBase.getPath() + ": ");
        for (auto &p : _pImpl->mapContents)
            Debug::Log(FOLDER_POPULATE_LOW, "  " + quote(p.second->getPath()));
    }
}

PFSDirectory
FSContainer::resolveDirectory()
{
    switch (_refBase.getResolvedType())
    {
        case FSTypeResolved::SYMLINK_TO_DIRECTORY:
        {
            FSSymlink *pSymlink = static_cast<FSSymlink*>(&_refBase);
            return static_pointer_cast<FSDirectory>(pSymlink->getTarget());
        }
        break;

        case FSTypeResolved::DIRECTORY:
            return static_pointer_cast<FSDirectory>(_refBase.getSharedFromThis());
        break;

        default:
        break;
    }

    throw FSException("Cannot create directory under " + _refBase.getPath());
}

PFSModelBase
FSContainer::find(const string &strParticle)
{
    PFSModelBase pReturn;
    ContentsLock cLock(*this);
    if ((pReturn = _pImpl->isAwake(cLock, strParticle, nullptr)))
        Debug::Log(FILE_MID, "Directory::find(" + quote(strParticle) + ") => already awake " + pReturn->describe());
    else
    {
        string strPath(_refBase.getPathImpl() + "/" + strParticle);
        Debug d(FILE_MID, "Directory::find(" + quote(strParticle) + "): looking up " + quote(strPath));

        PGioFile pGioFile;
        if (_refBase.hasFlag(FSFlag::IS_LOCAL))
            pGioFile = Gio::File::create_for_path(strPath.substr(7));
        else
            pGioFile = Gio::File::create_for_uri(strPath);
        // The above never fails. To find out whether the path is valid we need to query the type, which does blocking I/O.
        if (!(pReturn = FSModelBase::MakeAwake(pGioFile)))
            Debug::Log(FILE_LOW, "  could not make awake");
        else
            this->addChild(cLock, pReturn);
    }

    return pReturn;
}

bool
FSContainer::isPopulatedWithDirectories() const
{
    FSLock lock;
    return _refBase._fl.test(FSFlag::POPULATED_WITH_DIRECTORIES);
}

bool
FSContainer::isCompletelyPopulated() const
{
    FSLock lock;
    return _refBase._fl.test(FSFlag::POPULATED_WITH_ALL);
}

void
FSContainer::unsetPopulated()
{
    FSLock lock;
    _refBase._fl.clear(FSFlag::POPULATED_WITH_ALL);
    _refBase._fl.clear(FSFlag::POPULATED_WITH_DIRECTORIES);
}

condition_variable_any g_condFolderPopulated;

size_t
FSContainer::getContents(FSVector &vFiles,
                         Get getContents,
                         FSVector *pvFilesAdded,
                         FSVector *pvFilesRemoved,        //!< out: list of file-system object that have been removed, or nullptr (optional)
                         StopFlag *pStopFlag)
{
    Debug d(FILE_HIGH, "FSContainer::getContents(\"" + _refBase.getPath() + "\")");

    size_t c = 0;

    string strException;

    try
    {
        bool fStopped = false;

        // If this container is being populated on another thread, block on the global
        // condition variable until the other thread posts it (when populate is done).
        unique_lock<recursive_mutex> lock(g_mutexFiles);
        while (_refBase._fl.test(FSFlag::POPULATING))
            g_condFolderPopulated.wait(lock);
        // Lock is held again now. Folder state is now guaranteed to not be POPULATING
        // (either it's never been populated, or it was populated earlier, or the
        // other thread is done); now go test the state for good.

        if (    ((getContents == Get::ALL) && !isCompletelyPopulated())
             || ((getContents != Get::ALL) && !isPopulatedWithDirectories())
           )
        {
            // Folder needs populating: then set the flag so that only one thread populates
            // at a time.
            _refBase._fl.set(FSFlag::POPULATING);
            lock.unlock();

            PFSModelBase pSharedThis = _refBase.getSharedFromThis();

            /* The refresh algorithm is simple. For every file returned from the Gio backend,
             * we check if it's already in the contents map; if not, it is added. This adds
             * missing files. To remove awake files that have been removed on disk, every file
             * returned by the Gio backend that was either already awake or has been added in
             * the above loop is marked with  a "dirty" flag. A final loop then removes all
             * objects from the contents map that do not have the "dirty" flag set. */
            if (getContents == Get::ALL)
            {
                ContentsLock cLock(*this);
                FSLock lock2Temp;
                for (auto it : _pImpl->mapContents)
                {
                    auto &p = it.second;
                    p->_fl.set(FSFlag::DIRTY);
                }
            }

            auto pgioContainer = _refBase.getGioFile();
            Glib::RefPtr<Gio::FileEnumerator> en;
            if (!(en = pgioContainer->enumerate_children("*",
                                                         Gio::FileQueryInfoFlags::FILE_QUERY_INFO_NOFOLLOW_SYMLINKS)))
                throw FSException("Error populating!");
            else
            {
                Glib::RefPtr<Gio::FileInfo> pInfo;
                FSVector vSymlinksForFirstFolder;
                while ((pInfo = en->next_file()))
                {
                    auto pGioFileThis = en->get_child(pInfo);
                    string strThis = pGioFileThis->get_basename();
                    if (    (strThis != ".")
                         && (strThis != "..")
                       )
                    {
                        if (pStopFlag)
                            if (*pStopFlag)
                            {
                                fStopped = true;
                                break;
                            }

                        ContentsLock cLock(*this);

                        // Check if the object is already in this container.
                        FilesMap::iterator it;
                        PFSModelBase pAwake = _pImpl->isAwake(cLock, strThis, &it);
                        // Also wake up a new object from the GioFile. This is necessary
                        // so we can detect if the type of the file changed. This will not have the dirty flag set.
                        PFSModelBase pTemp = FSModelBase::MakeAwake(pGioFileThis);

                        if (    (pAwake)
                             && (pAwake->getType() == pTemp->getType())
                           )
                        {
                            FSLock lock2Temp;
                            // Clear the dirty flag.
                            pAwake->_fl.clear(FSFlag::DIRTY);
                        }
                        else
                        {
                            PFSModelBase pAddToContents;

                            if (pAwake)
                            {
                                // Type of file changed: then remove it from the folder before adding the new one.
                                if (pvFilesRemoved)
                                    pvFilesRemoved->push_back(pAwake);
                                _pImpl->removeImpl(cLock, it);
                            }

                            auto t = pTemp->getType();
                            switch (t)
                            {
                                case FSType::DIRECTORY:
                                {
                                    // Always wake up directories.
                                    Debug d(FILE_LOW, "Waking up directory " + strThis);
                                    pAddToContents = pTemp;
                                }
                                break;

                                case FSType::SYMLINK:
                                {
                                    // Need to wake up the symlink to figure out if it's a link to a dir.
                                    Debug d(FILE_LOW, "Waking up symlink " + strThis);
                                    pAddToContents = pTemp;
                                }
                                break;

                                default:
                                    // Ordinary file:
                                    if (getContents == Get::ALL)
                                    {
                                        Debug d(FILE_LOW, "Waking up plain file " + strThis);
                                        pAddToContents = pTemp;
                                    }
                                break;
                            }

                            if (pAddToContents)
                            {
                                this->addChild(cLock, pAddToContents);
                                if (pvFilesAdded)
                                    pvFilesAdded->push_back(pAddToContents);


                                if (    (getContents == Get::FIRST_FOLDER_ONLY)
                                     && (!pAddToContents->isHidden())
                                   )
                                {
                                    if (t == FSType::DIRECTORY)
                                        break;      // we're done
                                    else if (    (t == FSType::SYMLINK)
                                              && (pAddToContents->getResolvedType() == FSTypeResolved::SYMLINK_TO_DIRECTORY)
                                            )
                                        break;
                                }
                            }
                        }
                    }
                }
            }
        }

        if (!fStopped)
        {
            ContentsLock cLock(*this);
            for (auto it = _pImpl->mapContents.begin();
                 it != _pImpl->mapContents.end();
                )
            {
                auto &p = it->second;
                if (    (getContents == Get::ALL)
                     && (p->_fl.test(FSFlag::DIRTY))
                   )
                {
                    Debug::Log(FOLDER_POPULATE_HIGH, "Removing dirty file " + quote(p->getBasename()));
                    if (pvFilesRemoved)
                        pvFilesRemoved->push_back(p);
                    _pImpl->removeImpl(cLock, it);
                    // Note the post increment. http://stackoverflow.com/questions/180516/how-to-filter-items-from-a-stdmap/180616#180616
                    it++;
                }
                else
                {
                    // Leave out ".." in the list.
                    if (p != _refBase._pParent)
                    {
                        if (    (getContents == Get::ALL)
                             || (p->getType() == FSType::DIRECTORY)
                             || (p->getResolvedType() == FSTypeResolved::SYMLINK_TO_DIRECTORY)
                           )
                        {
                            if (    (getContents != Get::FIRST_FOLDER_ONLY)
                                 || (!p->isHidden())
                               )
                            {
                                vFiles.push_back(p);
                                ++c;

                                if (getContents == Get::FIRST_FOLDER_ONLY)
                                    break;
                            }
                        }
                    }

                    ++it;
                }
            }

            // This leaves one case: if we have found a real directory with Get::FIRST_FOLDER_ONLY,
            // then it's already in llFiles. But if there is no real directory, there might be a
            // symlink to one. All symlinks are in the folder contents, so we need to resolve all of
            // them to find a folder.
            if (    (getContents == Get::FIRST_FOLDER_ONLY)
                 && (!c)
               )
                for (auto it : _pImpl->mapContents)
                {
                    auto &p = it.second;
                    // Leave out ".." in the list.
                    if (p != _refBase._pParent)
                        if (p->getResolvedType() == FSTypeResolved::SYMLINK_TO_DIRECTORY)
                        {
                            vFiles.push_back(p);
                            ++c;
                            break;
                        }
                }

            FSLock lock2Temp;
            if (getContents == Get::FOLDERS_ONLY)
                _refBase._fl.set(FSFlag::POPULATED_WITH_DIRECTORIES);
            else if (getContents == Get::ALL)
            {
                _refBase._fl.set(FSFlag::POPULATED_WITH_DIRECTORIES);
                _refBase._fl.set(FSFlag::POPULATED_WITH_ALL);
            }
        } // if (!fStopped)
    }
    catch (Gio::Error &e)
    {
        strException = e.what();
    }

    _refBase._fl.clear(FSFlag::POPULATING);
    g_condFolderPopulated.notify_all();

    if (!strException.empty())
        throw FSException(strException);

    return c;
}

PFSDirectory
FSContainer::createSubdirectory(const string &strName)
{
    PFSDirectory pDirReturn;

    // If this is a symlink, then create the directory in the symlink's target instead.
    PFSDirectory pDirParent = resolveDirectory();

    try
    {
        // To create a new subdirectory via Gio::File, create an empty Gio::File first
        // and then invoke make_directory on it.
        string strPath = pDirParent->getPathImpl() + "/" + strName;
        Debug::Log(FILE_HIGH, string(__func__) + ": creating directory \"" + strPath + "\"");

        // The follwing cannot fail.
        Glib::RefPtr<Gio::File> pGioFileNew = Gio::File::create_for_uri(strPath);
        // But the following can throw.
        pGioFileNew->make_directory();

        // If we got here, we have a directory.
        pDirReturn = FSDirectory::Create(pGioFileNew);
        {
            ContentsLock cLock(*this);
            this->addChild(cLock, pDirReturn);
        }
    }
    catch (Gio::Error &e)
    {
        throw FSException(e.what());
    }

    return pDirReturn;
}

PFSFile
FSContainer::createEmptyDocument(const string &strName)
{
    PFSFile pFileReturn;

    // If this is a symlink, then create the directory in the symlink's target instead.
    PFSDirectory pDirParent = resolveDirectory();

    try
    {
        // To create a new subdirectory via Gio::File, create an empty Gio::File first
        // and then invoke make_directory on it.
        string strPath = pDirParent->getPathImpl() + "/" + strName;
        Debug::Log(FILE_HIGH, string(__func__) + ": creating directory \"" + strPath + "\"");

        // The follwing cannot fail.
        Glib::RefPtr<Gio::File> pGioFileNew = Gio::File::create_for_uri(strPath);
        // But the following can throw.
        auto pStream = pGioFileNew->create_file();
        pStream->close();

        // If we got here, we have a directory.
        pFileReturn = FSFile::Create(pGioFileNew, 0);
        {
            ContentsLock cLock(*this);
            this->addChild(cLock, pFileReturn);
        }
    }
    catch (Gio::Error &e)
    {
        throw FSException(e.what());
    }

    return pFileReturn;
}

void
FSContainer::notifyFileAdded(PFSModelBase pFS) const
{
    Debug d(FILEMONITORS, string(__func__) + "(" + pFS->getPath() + ")");
    for (auto &pMonitor : _pImpl->llMonitors)
        pMonitor->onItemAdded(pFS);
}

void
FSContainer::notifyFileRemoved(PFSModelBase pFS) const
{
    Debug d(FILEMONITORS, string(__func__) + "(" + pFS->getPath() + ")");
    for (auto &pMonitor : _pImpl->llMonitors)
        pMonitor->onItemRemoved(pFS);
}

void
FSContainer::notifyFileRenamed(PFSModelBase pFS, const string &strOldName, const string &strNewName) const
{
    Debug d(FILEMONITORS, string(__func__) + "(" + strOldName + " -> " + strNewName + ")");
    for (auto &pMonitor : _pImpl->llMonitors)
        pMonitor->onItemRenamed(pFS, strOldName, strNewName);
}


/***************************************************************************
 *
 *  FSDirectory
 *
 **************************************************************************/

/* static */
PFSDirectory
FSDirectory::Create(Glib::RefPtr<Gio::File> pGioFile)
{
    /* This nasty trickery is necessary to make make_shared work with a protected constructor. */
    class Derived : public FSDirectory
    {
    public:
        Derived(Glib::RefPtr<Gio::File> pGioFile) : FSDirectory(pGioFile) { }
    };

    return make_shared<Derived>(pGioFile);
}

/* static */
PFSDirectory
FSDirectory::GetHome()
{
    const char *p;
    if ((p = getenv("HOME")))
        return FindDirectory(p);
    return nullptr;
}

RootDirectory::RootDirectory(const string &strScheme, PGioFile pGioFile)
    : FSDirectory(pGioFile),
      _strScheme(strScheme)
{
    _fl = FSFlag::IS_ROOT_DIRECTORY;

    // Override the basename set in the FSModelBase constructor.
    _strBasename = strScheme + "://";
}

/*static */
PRootDirectory
RootDirectory::Get(const string &strScheme)        //<! in: URI scheme (e.g. "file")
{
    PRootDirectory pReturn;

    static Mutex                        s_mutexRootDirectories;
    static map<string, PRootDirectory>  s_mapRootDirectories;
    Lock rLock(s_mutexRootDirectories);

    auto it = s_mapRootDirectories.find(strScheme);
    if (it != s_mapRootDirectories.end())
        return it->second;

    try
    {
        string strPath = strScheme + ":///";
        auto pGioFile = Gio::File::create_for_uri(strPath);
        // The above never fails; the following checks for whether this exists. Shouldn't
        // be a problem for file:/// but who knows about webdav or trash or whatever else
        // is available.
        if (!pGioFile->query_exists())
            throw FSException("Cannot get root directory for URI scheme " + quote(strScheme));

        /* This nasty trickery is necessary to make make_shared work with a protected constructor. */
        class Derived : public RootDirectory
        {
        public:
            Derived(const string &strScheme, PGioFile pGioFile) : RootDirectory(strScheme, pGioFile) { }
        };

        pReturn = make_shared<Derived>(strScheme, pGioFile);
        s_mapRootDirectories[strScheme] = pReturn;

        // This will get propagated to all children.
        if (strScheme == "file")
            pReturn->setFlag(FSFlag::IS_LOCAL);
    }
    catch (Gio::Error &e)
    {
        throw FSException(e.what());
    }

    return pReturn;
}

CurrentDirectory::CurrentDirectory()
    : FSDirectory(Gio::File::create_for_path("."))
{
    _fl = FSFlag::IS_CURRENT_DIRECTORY;
}

/*static */
PCurrentDirectory
CurrentDirectory::GetImpl()
{
    FSLock lock;
    if (!s_theCWD)
    {
        // Class has a private constructor, so make_shared doesn't work without this hackery which derives a class from it.
        class Derived : public CurrentDirectory {  };
        s_theCWD = make_shared<Derived>();
        Debug::Log(FILE_MID, "CurrentDirectory::Get(): instantiated theCWD (#" + to_string(s_theCWD->_uID) + ")");
    }
    else
        Debug::Log(FILE_MID, "CurrentDirectory::Get(): subsequent call, returning theCWD (#" + to_string(s_theCWD->_uID) + ")");

    return s_theCWD;
}


/***************************************************************************
 *
 *  FSSymlink
 *
 **************************************************************************/

/* virtual */
FSTypeResolved
FSSymlink::getResolvedType() /* override */
{
    Debug::Log(FILE_LOW, "Symlink::getResolvedTypeImpl()");

    auto state = follow();

    switch (state)
    {
        case State::BROKEN:
            return FSTypeResolved::BROKEN_SYMLINK;

        case State::TO_FILE:
            return FSTypeResolved::SYMLINK_TO_FILE;

        case State::TO_DIRECTORY:
            return FSTypeResolved::SYMLINK_TO_DIRECTORY;

        case State::TO_OTHER:
            return FSTypeResolved::SYMLINK_TO_OTHER;

        case State::NOT_FOLLOWED_YET:
        case State::RESOLVING:
        break;
    }
    throw FSException("shouldn't happen");
}

/* static */
PFSSymlink
FSSymlink::Create(Glib::RefPtr<Gio::File> pGioFile)
{
    /* This nasty trickery is necessary to make make_shared work with a protected constructor. */
    class Derived : public FSSymlink
    {
    public:
        Derived(Glib::RefPtr<Gio::File> pGioFile) : FSSymlink(pGioFile) { }
    };

    return make_shared<Derived>(pGioFile);
}

PFSModelBase
FSSymlink::getTarget()
{
    follow();
    return _pTarget;
}

condition_variable_any g_condSymlinkResolved;

FSSymlink::State
FSSymlink::follow()
{
    Debug d(FILE_LOW, "FSSymlink::follow(\"" + getPath() + "\"");

    // Make sure that only one thread resolves this link at a time. We
    // set the link's state to State::RESOLVING below while we're following,
    // so if the link's state is State::RESOLVING now, it means another
    // thread is already in the process of resolving this link. In that
    // case, block on a condition variable which will get posted by the
    // other thread.
    unique_lock<recursive_mutex> lock(g_mutexFiles);
    while (_state == State::RESOLVING)
        g_condSymlinkResolved.wait(lock);

    // Lock is held again now.
    // Either we're the only thread, or the other thread is done following.
    // So now REALLY test the state.

    if (_state == State::NOT_FOLLOWED_YET)
    {
        if (!_pParent)
            throw FSException("symlink with no parent no good");

        _state = State::RESOLVING;
        lock.unlock();

        string strParentDir = _pParent->getPathImpl();
        Debug::Log(FILE_LOW, "parent = \"" + strParentDir + "\"");

        auto pGioFile = getGioFile();
        Glib::RefPtr<Gio::FileInfo> pInfo = pGioFile->query_info(G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET,
                                                                 Gio::FileQueryInfoFlags::FILE_QUERY_INFO_NOFOLLOW_SYMLINKS);
        string strContents = pInfo->get_symlink_target();
        if (strContents.empty())
        {
            Debug::Log(FILE_MID, "readlink(\"" + getPath() + "\") returned empty string -> BROKEN_SYMLINK");
            lock.lock();
            _state = State::BROKEN;
        }
        else
        {
            string strTarget = "";
            if (strContents[0] == '/')
                strTarget = strContents;
            else
            {
                // Make it relative to the symlink's directory.
                if (!strParentDir.empty())
                    strTarget = strParentDir + "/";
                strTarget += strContents;
            }

            try
            {
                auto pTarget = FindPath(strTarget);
                if (pTarget)
                {
                    lock.lock();
                    _pTarget = pTarget;

                    switch (_pTarget->getType())
                    {
                        case FSType::DIRECTORY:
                            _state = State::TO_DIRECTORY;
                        break;

                        case FSType::FILE:
                            _state = State::TO_FILE;
                        break;

                        case FSType::UNINITIALIZED:
                        case FSType::SYMLINK:
                        case FSType::SPECIAL:
                        case FSType::MOUNTABLE:
                            _state = State::TO_OTHER;
                        break;
                    }

                    Debug::Log(FILE_MID, "Woke up symlink target \"" + strTarget + "\", state: " + to_string((int)_state));
                }
            }
            catch (...)
            {
                Debug::Log(FILE_HIGH, "Could not find symlink target " + strTarget + " (from \"" + strContents + "\") --> BROKEN");
                lock.lock();
                _state = State::BROKEN;
            }
        }

        // Post the condition variable so that other threads who may be blocked in this function
        // on this symlink will wake up.
        g_condSymlinkResolved.notify_all();
    }

    return _state;
}


/***************************************************************************
 *
 *  FSSpecial
 *
 **************************************************************************/

/* static */
PFSSpecial
FSSpecial::Create(Glib::RefPtr<Gio::File> pGioFile)
{
    /* This nasty trickery is necessary to make make_shared work with a protected constructor. */
    class Derived : public FSSpecial
    {
    public:
        Derived(Glib::RefPtr<Gio::File> pGioFile) : FSSpecial(pGioFile) { }
    };

    return make_shared<Derived>(pGioFile);
}


/***************************************************************************
 *
 *  FSMountable
 *
 **************************************************************************/

/* static */
PFSMountable
FSMountable::Create(Glib::RefPtr<Gio::File> pGioFile)
{
    /* This nasty trickery is necessary to make make_shared work with a protected constructor. */
    class Derived : public FSMountable
    {
    public:
        Derived(Glib::RefPtr<Gio::File> pGioFile) : FSMountable(pGioFile) { }
    };

    return make_shared<Derived>(pGioFile);
}
