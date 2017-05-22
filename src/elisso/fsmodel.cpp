/*
 * elisso (C) 2016--2017 Baubadil GmbH.
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

/**
 *  Finds the FSModelBase for the given file system path, waking up all parent
 *  objects as necessary.
 *
 *  For example, if you call this on "/home/user/subdir/file.txt", this will
 *  wake up every one of the parent directories from left to right, if they
 *  have not been woken up yet, and finally file.txt, setting parent items
 *  as necessary.
 *
 *  This is the most common entry point into the file-system model. From here
 *  on up, you can iterate over folder contents or find more files.
 *
 *  This supports URI prefixes like "file://" or "trash://" as far as Gio::File
 *  recognizes them.
 *
 *  This is thread-safe. This throws FSException if the path is invalid.
 */
/* static */
PFSModelBase
FSModelBase::FindPath(const string &strPath0)
{
    Debug::Enter(FILE_LOW, __func__ + string("(" + strPath0 + ")"));

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
    if ((fAbsolute = (strPath[0] == '/')))
        strPathSplit = strPath.substr(1);
    else
        strPathSplit = strPath;

    StringVector aParticles = explodeVector(strPathSplit, "/");
    Debug::Log(FILE_LOW, to_string(aParticles.size()) + " particle(s) given");

    // Do not hold any locks in this method. We iterate over the path on the stack
    // and call into FSContainer::find(), which has proper locking.

    uint c = 0;
    PFSModelBase pCurrent;
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

                    Debug::Log(FILE_LOW, "Loop " + to_string(c) + ": collapsed \"" + pPrev->getPath() + "/" + strParticle + "\" to " + quote(pCurrent->getPath()));
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

    Debug::Leave("Result: " + (pCurrent ? pCurrent->describe() : "NULL"));

    return pCurrent;
}

/**
 *  Protected internal method to creates a new FSModelBase instance around the given Gio::File,
 *  picking the correct FSModelBase subclass depending on the file's type.
 *
 *  Note that creating a Gio::File never fails because there is no I/O involved, but this
 *  function does perform blocking I/O to test for the file's existence and dermine its type,
 *  so it can fail. If it does fail, e.g. because the file does not exist, then we throw FSException.
 *
 *  If this returns something, it is a dangling file object without an owner. You MUST call
 *  FSContainer::addChild() with the return value.
 *
 *  In any case, DO NOT CALL THIS FOR ROOT DIRECTORIES since this will mess up our internal
 *  management. Use RootDirectory::Get() instead.
 */
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
    catch(Gio::Error &e)
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
        Debug::Log(FILE_MID, "result for \"" + strPath + "\": " + pFS->describe());
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

/**
 *  Returns true if the file-system object has the "hidden" attribute, according to however Gio defines it.
 *
 *  Overridden for symlinks!
 */
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

/*
 *  Expands the path without resorting to realpath(), which would hit the disk.
 */
string
FSModelBase::getPath() const
{
    string strFullpath;

    if (!_pParent)
    {
//         if (_fl.test(FSFlag::IS_ROOT_DIRECTORY))
//             strFullpath = (static_cast<const RootDirectory*>(this))->getURIScheme() + "://";
    }
    else
    {
        // If we have a parent, recurse FIRST.
        strFullpath = _pParent->getPath();
        if (strFullpath != "/")
            strFullpath += '/';
    }

    strFullpath += getBasename();

    return strFullpath;
}

Glib::ustring
FSModelBase::getIcon() const
{
    return _pIcon->to_string();
}

/**
 *  Returns an FSFile if *this is a file or a symlink to a file; otherwise, this returns
 *  nullptr.
 */
PFSFile
FSModelBase::getFile()
{
    if (_type == FSType::FILE)
        return static_pointer_cast<FSFile>(shared_from_this());
    if (getResolvedType() == FSTypeResolved::SYMLINK_TO_FILE)
        return static_pointer_cast<FSFile>((static_cast<FSSymlink*>(this))->getTarget());

    return nullptr;
}

/**
 *  Returns the parent directory of this filesystem object. Note that this
 *  can be either a FSDirectory or a FSSymlink that points to one.
 */
PFSModelBase
FSModelBase::getParent() const
{
    if (_pParent)
        ;
    else if (_fl.test(FSFlag::IS_ROOT_DIRECTORY))
        ;       // return NULL;

    return _pParent;
}

/**
 *  Returns true if this is located under the given directory, either directly
 *  or somewhere more deeply.
 *
 *  Does NOT return true if this and pDir are the same; you need to test for that
 *  manually.
 */
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

/**
 *  Returns the FSContainer component of this directory or symlink.
 *
 *  We use C++ multiple inheritance to be able to store directory contents for
 *  both real directories and symlinks to real directories. In both cases,
 *  this returns a pointer to the FSContainer class of this instance. Otherwise
 *  (including for non-directory symlinks), this returns nullptr.
 *
 *  This allows you to call container methods like find() and getContents() for
 *  both directories and directory symlinks without losing path information.
 */
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

/**
 *  Renames this file to the given new name.
 *
 *  This does not notify file monitors since we can't be sure which thread we're running on. Call
 *  FSContainer::notifyFileRenamed() either afterwards if you call this on thread one, or have a
 *  GUI dispatcher which calls it instead.
 */
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
        catch(Gio::Error &e)
        {
            throw FSException(e.what());
        }
}

/**
 *  Attempts to send the file (or directory) to the desktop's trash can via the Gio methods.
 *
 *  Throws an exception if that fails, for example, if the underlying file system has no trash
 *  support, or if the object's permissions are insufficient.
 *
 *  This does not notify file monitors since we can't be sure which thread we're running on. Call
 *  FSContainer::notifyFileRemoved() either afterwards if you call this on thread one, or have a
 *  GUI dispatcher which calls it instead.
 */
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
    catch(Gio::Error &e)
    {
        throw FSException(e.what());
    }
}

/**
 *  Moves *this to the given target directory. pTarget mus either be a directory
 *  or a symlink to one.
 *
 *  This does not notify file monitors since we can't be sure which thread we're running on. Call
 *  FSContainer::notifyFileRemoved() and FSContainer::notifyFileAdded() either afterwards if you
 *  call this on thread one, or have a GUI dispatcher which calls it instead.

 */
void
FSModelBase::moveTo(PFSModelBase pTarget)
{
    Debug::Enter(FILE_HIGH, __func__);
    try
    {
        auto pParent = getParent();
        if (!pParent)
            throw FSException("cannot get parent for moving");

        auto pParentCnr = pParent->getContainer();
        if (!pParentCnr)
            throw FSException("cannot get parent container for moving");

        auto pTargetCnr = pTarget->getContainer();
        if (!pTargetCnr)
            throw FSException("cannot get target container for moving");

        {
            ContentsLock cLock(*pParentCnr);
            pParentCnr->removeChild(cLock, shared_from_this());
        }

        auto pGioFile = getGioFile();
        pGioFile->move(pTarget->getGioFile(),
                       Gio::FileCopyFlags::FILE_COPY_NOFOLLOW_SYMLINKS /*| Gio::FileCopyFlags::FILE_COPY_NO_FALLBACK_FOR_MOVE */);

        {
            ContentsLock cLock(*pTargetCnr);
            pParentCnr->addChild(cLock, shared_from_this());
        }
    }
    catch(Gio::Error &e)
    {
        Debug::Leave("Caught Gio::Error: " + e.what());
        throw FSException(e.what());
    }
    Debug::Leave();
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

/**
 *  Factory method to create an instance and return a shared_ptr to it.
 *  This normally gets called by FSModelBase::MakeAwake() only. This
 *  does not add the new object to a container; you must call setParent()
 *  on the result.
 */
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

FSFile::FSFile(Glib::RefPtr<Gio::File> pGioFile,
               uint64_t cbSize)
    : FSModelBase(FSType::FILE,
                  pGioFile,
                  cbSize)
{
}

/* virtual */
FSFile::~FSFile()
{
    FSLock lock;
    if (_pThumbData)
        delete _pThumbData;
}

/**
 *  Caches the given pixbuf for *this and the given thumbnail size.
 */
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

/**
 *  Returns a pixbuf for *this and the given thumbnail size if
 *  setThumbnail() gave us one fore.
 */
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

/**
 *  Constructor. This is protected because an FSContainer only ever gets created through
 *  multiple inheritance.
 */
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

/**
 *  Protected method to add the given child to this container's list of children, under
 *  this container's ContentsLock. Also updates the parent pointer in the child.
 *
 *  This must be called after an object has been instantiated with FSModelBase::MakeAwake().
 */
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

/*
 *  Inversely to addChild(), removes the given child from this container's list of
 *  children.
 */
void
FSContainer::removeChild(ContentsLock &lock, PFSModelBase p)
{
    auto it = _pImpl->mapContents.find(p->getBasename());
    if (it == _pImpl->mapContents.end())
        throw FSException("internal: cannot find myself in parent");

    _pImpl->removeImpl(lock, it);
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

/**
 *  Attempts to find a file-system object in the current container (directory or symlink
 *  to a directory). This first calls isAwake() to check if the object has already been
 *  instantiated in memory; if not, we try to find it on disk and instantiate it.
 *  Returns nullptr if the file definitely doesn't exist or cannot be read.
 *
 *  This calls into the Gio backend and may throw.
 */
PFSModelBase
FSContainer::find(const string &strParticle)
{
    PFSModelBase pReturn;
    ContentsLock cLock(*this);
    if ((pReturn = _pImpl->isAwake(cLock, strParticle, nullptr)))
        Debug::Log(FILE_MID, "Directory::find(" + quote(strParticle) + ") => already awake " + pReturn->describe());
    else
    {
        string strPath(_refBase.getPath() + "/" + strParticle);
        Debug::Enter(FILE_MID, "Directory::find(" + quote(strParticle) + "): looking up " + quote(strPath));

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

        Debug::Leave();
    }

    return pReturn;
}

/**
 *  Returns true if the container has the "populated with directories" flag set.
 *  See getContents() for what that means.
 */
bool
FSContainer::isPopulatedWithDirectories() const
{
    FSLock lock;
    return _refBase._fl.test(FSFlag::POPULATED_WITH_DIRECTORIES);
}

/**
 *  Returns true if the container has the "populated with all" flag set.
 *  See getContents() for what that means.
 */
bool
FSContainer::isCompletelyPopulated() const
{
    FSLock lock;
    return _refBase._fl.test(FSFlag::POPULATED_WITH_ALL);
}

/**
 *  Unsets both the "populated with all" and "populated with directories" flags for this
 *  directory, which will cause getContents() to refresh the contents list from disk on
 *  the next call.
 */
void
FSContainer::unsetPopulated()
{
    FSLock lock;
    _refBase._fl.clear(FSFlag::POPULATED_WITH_ALL);
    _refBase._fl.clear(FSFlag::POPULATED_WITH_DIRECTORIES);
}

condition_variable_any g_condFolderPopulated;

/**
 *  Returns the container's contents by copying them into the given list.
 *
 *  This will perform blocking I/O and can take several seconds to complete, depending on
 *  the size of the directory contents and the medium, unless this is not the first
 *  call on this container and the container contents have been cached before. Caching
 *  depends on the given getContents flag:
 *
 *   -- If getContents == Get::ALL, this will return all files and direct subdirectories
 *      of the container, and all these objects will be cached, and the "populated with all"
 *      flag is set on the container. When called for a second time on the same container,
 *      contents can then be returned without blocking I/O again regardless of the getContents
 *      flag, since all objects are already awake.
 *
 *   -- If getContents == Get::FOLDERS_ONLY, this only wakes up directories and symlinks
 *      to directories in the container, which might be slightly faster. (Probably not
 *      a lot since we still have to enumerate the entire directory contents and wake
 *      up all symlinks to test for whether they point to subdirectories.) This will
 *      only copy directories and symlinks to directories to the given list and then set
 *      the "populated with directories" flag on the container. If this mode is called
 *      on a container with the "populated with all" or "populated with directories"
 *      flag already set, this can return without blocking disk I/O.
 *
 *   -- If getContents == Get::FIRST_FOLDER_ONLY, this will only enumerate directory
 *      contents until the first directory or symlink to a directory is encountered.
 *      This has the potential to be a lot faster. This can be useful for a directory
 *      tree view where an expander ("+" sign) needs to be shown for folders that
 *      have at least one subfolder, but a full populate with folders only needs to
 *      happen once the folder is actually expanded. Note that this will not return the
 *      first folder in a strictly alphabetical sense, but simply the first directory
 *      or symlink pointing to a directory that happens to be returned by the Gio
 *      backend. Unfortunately even this cannot be faster than Get::FOLDERS_ONLY if
 *      the container happens to contain only files but no subdirectories, since in
 *      that case, the code will have to enumerate the entire directory contents.
 *
 *  For all modes, you can optionally pass the address of an atomic bool with pfStopFlag,
 *  which is useful if you run this on a secondary thread and you want this function to be
 *  interruptible: if pfStopFlag is not nullptr, it is checked periodically, and the functions
 *  returns early once the stop flag is set.
 *
 *  To clear all "populated" flags and force a refresh from disk, call unsetPopulated()
 *  before calling this. For that case, you can pass in two FSVectors with pvFilesAdded
 *  and pvFilesRemoved so you can call notifiers after the refresh.
 */
size_t
FSContainer::getContents(FSVector &vFiles,
                         Get getContents,
                         FSVector *pvFilesAdded,
                         FSVector *pvFilesRemoved,        //!< out: list of file-system object that have been removed, or nullptr (optional)
                         StopFlag *pStopFlag)
{
    Debug::Enter(FOLDER_POPULATE_HIGH, "FSContainer::getContents(\"" + _refBase.getPath() + "\")");

    size_t c = 0;

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
                                    // Always wake up directories.
                                    Debug::Enter(FILE_LOW, "Waking up directory " + strThis);
                                    pAddToContents = pTemp;
                                    Debug::Leave();
                                break;

                                case FSType::SYMLINK:
                                    // Need to wake up the symlink to figure out if it's a link to a dir.
                                    Debug::Enter(FILE_LOW, "Waking up symlink " + strThis);
                                    pAddToContents = pTemp;
                                    Debug::Leave();
                                break;

                                default:
                                    // Ordinary file:
                                    if (getContents == Get::ALL)
                                    {
                                        Debug::Enter(FILE_LOW, "Waking up plain file " + strThis);
                                        pAddToContents = pTemp;
                                        Debug::Leave();
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
    catch(Gio::Error &e)
    {
        throw FSException(e.what());
    }

    _refBase._fl.clear(FSFlag::POPULATING);
    g_condFolderPopulated.notify_all();

    Debug::Leave();

    return c;
}

/**
 *  Creates a new physical directory in this container (physical directory or symlink
 *  pointing to one), which is returned.
 *
 *  Throws an FSException on I/O errors.
 *
 *  This does not automatically call file-system monitors since this may not run on
 *  the GUI thread. Call notifyFileAdded() with the returned instance afterwards.
 */
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
        string strPath = pDirParent->getPath() + "/" + strName;
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
    catch(Gio::Error &e)
    {
        throw FSException(e.what());
    }

    return pDirReturn;
}

/**
 *  Creates a new physical directory in this container (physical directory or symlink
 *  pointing to one), which is returned.
 *
 *  Throws an FSException on I/O errors.
 *
 *  This does not automatically call file-system monitors since this may not run on
 *  the GUI thread. Call notifyFileAdded() with the returned instance afterwards.
 */
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
        string strPath = pDirParent->getPath() + "/" + strName;
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
    catch(Gio::Error &e)
    {
        throw FSException(e.what());
    }

    return pFileReturn;
}

/**
 *  Notifies all monitors attached to *this that a file has been added.
 *
 *  Call this on the GUI thread after calling FSContainer::create*().
 */
void
FSContainer::notifyFileAdded(PFSModelBase pFS) const
{
    Debug::Enter(FILEMONITORS, string(__func__) + "(" + pFS->getPath() + ")");
    for (auto &pMonitor : _pImpl->llMonitors)
        pMonitor->onItemAdded(pFS);
    Debug::Leave();
}

/**
 *  Notifies all monitors attached to *this that a file has been removed.
 *
 *  Call this on the GUI thread after calling FSModelBase::sendToTrash().
 *  Note that at this time, the file no longer exists on disk and is only
 *  an empty shell any more.
 */
void
FSContainer::notifyFileRemoved(PFSModelBase pFS) const
{
    Debug::Enter(FILEMONITORS, string(__func__) + "(" + pFS->getPath() + ")");
    for (auto &pMonitor : _pImpl->llMonitors)
        pMonitor->onItemRemoved(pFS);
    Debug::Leave();
}

void
FSContainer::notifyFileRenamed(PFSModelBase pFS, const string &strOldName, const string &strNewName) const
{
    Debug::Enter(FILEMONITORS, string(__func__) + "(" + strOldName + " -> " + strNewName + ")");
    for (auto &pMonitor : _pImpl->llMonitors)
        pMonitor->onItemRenamed(pFS, strOldName, strNewName);
    Debug::Leave();
}


/***************************************************************************
 *
 *  FSDirectory
 *
 **************************************************************************/

FSDirectory::FSDirectory(Glib::RefPtr<Gio::File> pGioFile)
    : FSModelBase(FSType::DIRECTORY, pGioFile, 0),
      FSContainer((FSModelBase&)*this)
{
}

/**
 *  Factory method to create an instance and return a shared_ptr to it.
 *  This normally gets called by FSModelBase::MakeAwake() only. This
 *  does not add the new object to a container; you must call setParent()
 *  on the result.
 */
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

/**
 *  Returns the user's home directory, or nullptr on errors.
 */
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

/**
 *  Returns the root directory for the given URI scheme, e.g. "file" or "trash" or
 *  "ftp". Throws on errors.
 */
/*static */
PRootDirectory
RootDirectory::Get(const string &strScheme)        //<! in: URI scheme (e.g. "file")
{
    PRootDirectory pReturn;

    static Mutex                                    s_mutexRootDirectories;
    static map<string, PRootDirectory>    s_mapRootDirectories;
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


//     if (!s_theRoot)
//     {
//         // Class has a private constructor, so make_shared doesn't work without this hackery which derives a class from it.
//         class Derived : public RootDirectory { };
//         s_theRoot = make_shared<Derived>();
//         Debug::Log(FILE_MID, "RootDirectory::Get(): instantiated the root");
//     }
//     else
//         Debug::Log(FILE_MID, "RootDirectory::Get(): subsequent call, returning theRoot");
//
//     return s_theRoot;
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

FSSymlink::FSSymlink(Glib::RefPtr<Gio::File> pGioFile)
    : FSModelBase(FSType::SYMLINK, pGioFile, 0),
      FSContainer((FSModelBase&)*this),
      _state(State::NOT_FOLLOWED_YET)
{
}

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

        case State::NOT_FOLLOWED_YET:
        case State::RESOLVING:
        break;
    }
    throw FSException("shouldn't happen");
}

/**
 *  Factory method to create an instance and return a shared_ptr to it.
 *  This normally gets called by FSModelBase::MakeAwake() only. This
 *  does not add the new object to a container; you must call setParent()
 *  on the result.
 */
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

/**
 *  Returns the symlink target, or nullptr if the symlink is broken. This is not const
 *  as the target may need to be resolved on the first call.
 */
PFSModelBase
FSSymlink::getTarget()
{
    follow();
    return _pTarget;
}

condition_variable_any g_condSymlinkResolved;

/**
 *  Atomically resolves the symlink and caches the result. This may need to
 *  to blocking disk I/O and may therefore not be quick.
 */
FSSymlink::State
FSSymlink::follow()
{
    Debug::Enter(FILE_LOW, "FSSymlink::follow(\"" + getPath() + "\"");

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

        string strParentDir = _pParent->getPath();
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
                    if (_pTarget->getType() == FSType::DIRECTORY)
                        _state = State::TO_DIRECTORY;
                    else
                        _state = State::TO_FILE;
                    Debug::Log(FILE_MID, "Woke up symlink target \"" + strTarget + "\", state: " + to_string((int)_state));
                }
            }
            catch(...)
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

    Debug::Leave();
    return _state;
}


/***************************************************************************
 *
 *  FSSpecial
 *
 **************************************************************************/

FSSpecial::FSSpecial(Glib::RefPtr<Gio::File> pGioFile)
    : FSModelBase(FSType::SPECIAL, pGioFile, 0)
{
}

/**
 *  Factory method to create an instance and return a shared_ptr to it.
 *  This normally gets called by FSModelBase::MakeAwake() only. This
 *  does not add the new object to a container; you must call setParent()
 *  on the result.
 */
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

FSMountable::FSMountable(Glib::RefPtr<Gio::File> pGioFile)
    : FSModelBase(FSType::MOUNTABLE, pGioFile, 0)
{
}

/**
 *  Factory method to create an instance and return a shared_ptr to it.
 *  This normally gets called by FSModelBase::MakeAwake() only. This
 *  does not add the new object to a container; you must call setParent()
 *  on the result.
 */
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
