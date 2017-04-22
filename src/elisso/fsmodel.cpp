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
#include "xwp/debug.h"
#include "xwp/stringhelp.h"
#include "xwp/except.h"

#include <mutex>
#include <atomic>
#include <chrono>
#include <thread>

#include <string.h>


/***************************************************************************
 *
 *  Globals, static variable instantiations
 *
 **************************************************************************/

std::atomic<std::uint64_t>  g_uFSID(1);

PRootDirectory RootDirectory::s_theRoot = NULL;
PCurrentDirectory CurrentDirectory::s_theCWD = NULL;


/***************************************************************************
 *
 *  FSLock
 *
 **************************************************************************/

std::recursive_mutex g_mutexFiles;

class FSLock : public XWP::Lock
{
public:
    FSLock()
        : Lock(g_mutexFiles)
    { }
};


/***************************************************************************
 *
 *  FSContainer::Impl definition
 *
 **************************************************************************/

typedef map<const std::string, PFSModelBase> FilesMap;

struct ContentsMap
{
    FilesMap    m;
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
        throw FSException("Monitor is already busy with another container");

    cnr._llMonitors.push_back(shared_from_this());
    _pContainer = &cnr;
}

void
FSMonitorBase::stopWatching(FSContainer &cnr)
{
    FSLock lock;
    if (_pContainer != &cnr)
        throw FSException("Cannot remove monitor as it's not active for this container");

    size_t c = cnr._llMonitors.size();
    _pContainer = nullptr;
    cnr._llMonitors.remove(shared_from_this());
    if (cnr._llMonitors.size() != c - 1)
        throw FSException("Couln't find monitor to remove in container's list");
}


/***************************************************************************
 *
 *  FSModelBase
 *
 **************************************************************************/

/**
 *  Returns nullptr if the path is invalid.
 */
/* static */
PFSModelBase
FSModelBase::FindPath(const std::string &strPath)
{
    Debug::Enter(FILE_LOW, __func__ + string("(" + strPath + ")"));

    string strPathSplit;
    bool fAbsolute;
    if ((fAbsolute = (strPath[0] == '/')))
        strPathSplit = strPath.substr(1);
    else
        strPathSplit = strPath;

    StringVector aParticles = explodeVector(strPathSplit, "/");
    Debug::Log(FILE_LOW, to_string(aParticles.size()) + " particle(s) given");

    FSLock lock;

    string strForStat;
    uint c = 0;
    PFSModelBase pCurrent;
    for (auto const &strParticle : aParticles)
    {
        if (strParticle == ".")
        {
            if (aParticles.size() > 1)
                Debug::Log(FILE_LOW, "Ignoring particle . in list");
            else
                return CurrentDirectory::GetImpl();
        }
        else
        {
            FSContainer *pDir = nullptr;
            bool fCollapsing = false;
            if (!pCurrent)
            {
                if (fAbsolute)
                    // First item on an absolute path must be a child of the root directory.
                    pDir = RootDirectory::GetImpl()->getContainer();
                else
                {
                    // First item on a relative path must be a child of the curdir.
                    pDir = CurrentDirectory::GetImpl()->getContainer();
                    strForStat = ".";
                }
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
                    strForStat = getDirnameString(strForStat);

                    Debug::Log(FILE_LOW, "Loop " + to_string(c) + ": collapsed \"" + pPrev->getRelativePath() + "/" + strParticle + "\" to " + quote(pCurrent->getRelativePath()));
                }
                else if (!(pDir = pCurrent->getContainer()))
                    // Particle is a broken symlink.
                    break;
//                     throw FSException("path particle \"" + pCurrent->getRelativePath() + "\" cannot have contents");
            }

            if (!fCollapsing)
            {
                strForStat += "/" + strParticle;

                if (!(pCurrent = pDir->find(strParticle)))
                    break;
            }
            ++c;
        }
    }

    Debug::Leave();

    Debug::Log(FILE_LOW, "Result: " + (pCurrent ? pCurrent->describe() : "NULL"));

    return pCurrent;
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

/**
 *  Creates a new FSModelBase instance around the given Gio::File. Note that creating a Gio::File
 *  never fails because there is no I/O involved, but this function does perform blocking I/O
 *  to test for the file's existence and dermine its type, so it can fail. If so, we throw an
 *  FSException.
 *
 *  If this returns something, it is a dangling file object without an owner. You MUST call
 *  setParent() on the return value.
 */
/* static */
PFSModelBase
FSModelBase::MakeAwake(Glib::RefPtr<Gio::File> pGioFile)
{
    PFSModelBase pReturn = nullptr;

    try
    {
        auto type = pGioFile->query_file_type(Gio::FileQueryInfoFlags::FILE_QUERY_INFO_NOFOLLOW_SYMLINKS);

        switch (type)
        {
            case Gio::FileType::FILE_TYPE_NOT_KNOWN:       // File's type is unknown.
            break;      // return nullptr

            case Gio::FileType::FILE_TYPE_REGULAR:         // File handle represents a regular file.
                pReturn = FSFile::Create(pGioFile, pGioFile->query_info()->get_size());
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
        }
    }
    catch(Gio::Error &e)
    {
        throw FSException(e.what());
    }

    return pReturn;
}

/* static */
PFSDirectory
FSModelBase::FindDirectory(const std::string &strPath)
{
    if (auto pFS = FindPath(strPath))
    {
        Debug::Log(FILE_MID, "result for \"" + strPath + "\": " + pFS->describe());
        if (pFS->getType() == FSType::DIRECTORY)
            return std::static_pointer_cast<FSDirectory>(pFS);
    }

    return nullptr;
}

FSModelBase::FSModelBase(FSType type,
                         Glib::RefPtr<Gio::File> pGioFile,
                         uint64_t cbSize)
    : _uID(g_uFSID++),      // atomic
      _type(type),
      _pGioFile(pGioFile),
      _strBasename(_pGioFile->get_basename()),
      _cbSize(cbSize)
{
    _pIcon = _pGioFile->query_info()->get_icon();
}

/**
 *  Returns the private ContentsMap structure for both directories and
 *  symlinks to directories. Otherwise it throws.
 */
ContentsMap&
FSModelBase::getContentsMap()
{
    if (getType() == FSType::DIRECTORY)
        return *((static_cast<FSDirectory*>(this))->_pMap);

    if (getResolvedType() == FSTypeResolved::SYMLINK_TO_DIRECTORY)
        return *((static_cast<FSSymlink*>(this))->_pMap);

    throw FSException("Cannot get contents map");
}

/**
 *  Sets the parent container for this file-system object.
 *
 *  This is a protected internal method and must be called after an
 *  object was instantiated via MakeAwake() to add it to a container.
 *  This handles both directories and symlinks to directories correctly
 *  by using getContentsMap().
 */
void
FSModelBase::setParent(PFSModelBase pNewParent)
{
    FSLock lock;
    if (!pNewParent)
    {
        if (_pParent)
        {
            // Unsetting previous parent:
            ContentsMap &map = _pParent->getContentsMap();

            auto it = map.m.find(getBasename());
            if (it == map.m.end())
                throw FSException("internal: cannot find myself in parent");

            map.m.erase(it);
            _pParent = nullptr;
        }
    }
    else
    {
        if (_pParent)
            throw FSException("setParent called twice");

        // Setting initial parent:
        _pParent = pNewParent;
        std::string strBasename(getBasename());
        Debug::Log(FILE_LOW, "storing \"" + strBasename + "\" in parent map");

        ContentsMap &map = pNewParent->getContentsMap();
        map.m[strBasename] = shared_from_this();
    }
}

/**
 *  Returns true if the file-system object has the "hidden" attribute, according to however Gio defines it.
 *
 *  Overridden for symlinks!
 */
bool
FSModelBase::isHidden()
{
    if (!_fl.test(FSFlags::HIDDEN_CHECKED))
    {
        auto len = _strBasename.length();
        if (!len)
            _fl |= FSFlags::HIDDEN;
        else
            if (    (_strBasename[0] == '.')
                 || (_strBasename[len - 1] == '~')
               )
                _fl |= FSFlags::HIDDEN;

        _fl |= FSFlags::HIDDEN_CHECKED;
    }

    return _fl.test(FSFlags::HIDDEN);

//     auto pInfo = _pGioFile->query_info(G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
//                                        Gio::FileQueryInfoFlags::FILE_QUERY_INFO_NOFOLLOW_SYMLINKS);
//     return pInfo->is_hidden();
}

/*
 *  Expands the path without resorting to realpath(), which would hit the disk.
 */
std::string
FSModelBase::getRelativePath()
{
    std::string strFullpath;
    if (_pParent)
    {
        strFullpath = _pParent->getRelativePath();
        if (strFullpath != "/")
            strFullpath += '/';
        strFullpath += getBasename();
    }
    else
        /* This is a relative path: then we need the current working dir.
         * For example, if we're in /home/user and _strOriginal is ../user2. */
        strFullpath = getBasename();

    return strFullpath;
}

Glib::ustring
FSModelBase::getIcon()
{
    return _pIcon->to_string();
}

/**
 *  Returns the parent directory of this filesystem object. Note that this
 *  can be either a FSDirectory or a FSSymlink that points to one.
 */
PFSModelBase
FSModelBase::getParent()
{
    if (_pParent)
        ;
    else if (_fl.test(FSFlags::IS_ROOT_DIRECTORY))
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
FSModelBase::isUnder(PFSDirectory pDir)
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

const std::string g_strFile("file");
const std::string g_strDirectory("directory");
const std::string g_strSymlink("symlink");
const std::string g_strOther("other");

const std::string&
FSModelBase::describeType()
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

std::string
FSModelBase::describe(bool fLong /* = false */ )
{
    return  describeType() + " \"" + (fLong ? getRelativePath() : getBasename()) + "\" (#" + to_string(_uID) + ")";
}

/**
 *  Attempts to send the file (or directory) to the desktop's trash can via the Gio methods. Throws an exception
 *  if that fails, for example, if the underlying file system has no trash support, or if the object's
 *  permissions are insufficient.
 *
 *  This does not notify file monitors since we can't be sure which thread we're running on. Call this->notifyFileRemoved()
 *  either afterwards if you call this on thread one, or have a GUI dispatcher which calls it instead.
 */
void
FSModelBase::sendToTrash()
{
    try
    {
        _pGioFile->trash();
    }
    catch(Gio::Error &e)
    {
        throw FSException(e.what());
    }
}

void
FSModelBase::testFileOps()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

/**
 *  Notifies all monitors attached to this file's container that this file
 *  has been removed.
 *
 *  Call this on the GUI thread after calling sendToTrash().
 */
void
FSModelBase::notifyFileRemoved()
{
    auto pParent = getParent();
    if (pParent)
    {
        auto pCnr = pParent->getContainer();
        if (pCnr)
        {
            auto pThis = shared_from_this();
            for (auto &p : pCnr->_llMonitors)
                p->onItemRemoved(pThis);
        }
    }
}


/***************************************************************************
 *
 *  FSFile
 *
 **************************************************************************/

FSFile::FSFile(Glib::RefPtr<Gio::File> pGioFile,
               uint64_t cbSize)
    : FSModelBase(FSType::FILE,
                  pGioFile,
                  cbSize)
{
}

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
    /* This nasty trickery is necessary to make std::make_shared work with a protected constructor. */
    class Derived : public FSFile
    {
    public:
        Derived(Glib::RefPtr<Gio::File> pGioFile, uint64_t cbSize) : FSFile(pGioFile, cbSize) { }
    };

    return std::make_shared<Derived>(pGioFile, cbSize);
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
    : _pMap(new ContentsMap),
      _refBase(refBase)
{
}

/**
 *  Constructor. This is protected because an FSContainer only ever gets created through
 *  multiple inheritance.
 */
/* virtual */
FSContainer::~FSContainer()
{
    delete _pMap;
}

/**
 *  Tests if a file-system object with the given name has already been instantiated in this
 *  container. If so, it is returned. If this returns nullptr instead, that doesn't mean
 *  that the file doesn't exist: the container might not be fully populated, or directory
 *  contents may have changed since.
 */
PFSModelBase
FSContainer::isAwake(const string &strParticle)
{
    FSLock lock;
    auto it = _pMap->m.find(strParticle);
    if (it != _pMap->m.end())
        return it->second;

    return nullptr;
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
    FSLock lock;

    PFSModelBase pReturn;
    if ((pReturn = isAwake(strParticle)))
        Debug::Log(FILE_MID, "Directory::find(\"" + strParticle + "\") => already awake " + pReturn->describe());
    else
    {
        const string strPath(_refBase.getRelativePath() + "/" + strParticle);
        Debug::Enter(FILE_MID, "Directory::find(\"" + strPath + "\")");

        auto pGioFile = Gio::File::create_for_path(strPath);
        // The above never fails. To find out whether the path is valid we need to query the type, which does blocking I/O.
        if (!(pReturn = FSModelBase::MakeAwake(pGioFile)))
            Debug::Log(FILE_LOW, "  could not make awake");
        else
            pReturn->setParent(static_pointer_cast<FSDirectory>(_refBase.getSharedFromThis()));

        Debug::Leave();
    }

    return pReturn;
}

/**
 *  Returns true if the container has the "populated with directories" flag set.
 *  See getContents() for what that means.
 */
bool
FSContainer::isPopulatedWithDirectories()
{
    FSLock lock;
    return _refBase._fl.test(FSFlags::POPULATED_WITH_DIRECTORIES);
}

/**
 *  Returns true if the container has the "populated with all" flag set.
 *  See getContents() for what that means.
 */
bool
FSContainer::isCompletelyPopulated()
{
    FSLock lock;
    return _refBase._fl.test(FSFlags::POPULATED_WITH_ALL);
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
    _refBase._fl.reset(FSFlags::POPULATED_WITH_ALL);
    _refBase._fl.reset(FSFlags::POPULATED_WITH_DIRECTORIES);
}

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
 *  before calling this.
 */
size_t
FSContainer::getContents(FSList &llFiles,
                         Get getContents,
                         StopFlag *pStopFlag)
{
    Debug::Enter(FILE_LOW, "FSContainer::getContents(\"" + _refBase.getBasename() + "\")");

    size_t c = 0;

    try
    {
        bool fStopped = false;
        FSLock lock;
        if (    ((getContents == Get::ALL) && !isCompletelyPopulated())
             || ((getContents != Get::ALL) && !isPopulatedWithDirectories())
           )
        {
            PFSModelBase pSharedThis = _refBase.getSharedFromThis();

            /* The refresh algorithm is simple. For every file returned from the Gio backend,
             * we check if it's already in the contents map; if not, it is added. This adds
             * missing files. To remove awake files that have been removed on disk, every file
             * returned by the Gio backend that was either already awake or has been added in
             * the above loop is marked with  a "dirty" flag. A final loop then removes all
             * objects from the contents map that do not have the "dirty" flag set. */
            if (getContents == Get::ALL)
                for (auto it : _pMap->m)
                {
                    auto &p = it.second;
                    p->_fl |= FSFlags::DIRTY;
                }

            Glib::RefPtr<Gio::FileEnumerator> en;
            if (!(en = _refBase._pGioFile->enumerate_children("*",
                                                              Gio::FileQueryInfoFlags::FILE_QUERY_INFO_NOFOLLOW_SYMLINKS)))
                throw FSException("Error populating!");
            else
            {
                Glib::RefPtr<Gio::FileInfo> pInfo;
                FSList llSymlinksForFirstFolder;
                while ((pInfo = en->next_file()))
                {
                    auto pGioFile = en->get_child(pInfo);
                    std::string strThis = pGioFile->get_basename();
                    if (    (strThis != ".")
                         && (strThis != "..")
                       )
                    {
                        auto pAwake = isAwake(strThis);
                        if (pAwake)
                            // Clear the dirty flag.
                            pAwake->_fl.reset(FSFlags::DIRTY);
                        else
                        {
                            if (pStopFlag)
                                if (*pStopFlag)
                                {
                                    fStopped = true;
                                    break;
                                }

                            PFSModelBase pKeep;

                            // Wake up a new object. This will not have the dirty flag set.
                            PFSModelBase pTemp = FSModelBase::MakeAwake(pGioFile);

                            auto t = pTemp->getType();
                            switch (t)
                            {
                                case FSType::DIRECTORY:
                                    // Always wake up directories.
                                    Debug::Enter(FILE_LOW, "Waking up directory " + strThis);
                                    pKeep = pTemp;
                                    Debug::Leave();
                                break;

                                case FSType::SYMLINK:
                                    // Need to wake up the symlink to figure out if it's a link to a dir.
                                    Debug::Enter(FILE_LOW, "Waking up symlink " + strThis);
                                    pKeep = pTemp;
                                    Debug::Leave();
                                break;

                                default:
                                    // Ordinary file:
                                    if (getContents == Get::ALL)
                                    {
                                        Debug::Enter(FILE_LOW, "Waking up plain file " + strThis);
                                        pKeep = pTemp;
                                        Debug::Leave();
                                    }
                                break;
                            }

                            if (pKeep)
                            {
                                pKeep->setParent(pSharedThis);

                                if (    (getContents == Get::FIRST_FOLDER_ONLY)
                                     && (!pKeep->isHidden())
                                   )
                                {
                                    if (t == FSType::DIRECTORY)
                                        break;      // we're done
                                    else if (    (t == FSType::SYMLINK)
                                              && (pKeep->getResolvedType() == FSTypeResolved::SYMLINK_TO_DIRECTORY)
                                            )
                                        break;
                                }
                            }
                        }
                    }
                }

                if (!fStopped)
                {
                    if (getContents == Get::FOLDERS_ONLY)
                        _refBase._fl |= FSFlags::POPULATED_WITH_DIRECTORIES;
                    else if (getContents == Get::ALL)
                    {
                        _refBase._fl |= FSFlags::POPULATED_WITH_DIRECTORIES;
                        _refBase._fl |= FSFlags::POPULATED_WITH_ALL;
                    }
                }
            }
        }

        if (!fStopped)
        {
            for (auto it = _pMap->m.begin();
                 it != _pMap->m.end();
                )
            {
                auto &p = it->second;
                if (    (getContents == Get::ALL)
                     && (p->_fl & FSFlags::DIRTY)
                   )
                {
                    // Note the post increment. http://stackoverflow.com/questions/180516/how-to-filter-items-from-a-stdmap/180616#180616
                    _pMap->m.erase(it++);
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
                                llFiles.push_back(p);
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
            for (auto it : _pMap->m)
            {
                auto &p = it.second;
                // Leave out ".." in the list.
                if (p != _refBase._pParent)
                    if (p->getResolvedType() == FSTypeResolved::SYMLINK_TO_DIRECTORY)
                    {
                        llFiles.push_back(p);
                        ++c;
                        break;
                    }
            }
        }
    }
    catch(Gio::Error &e)
    {
        throw FSException(e.what());
    }

    Debug::Leave();

    return c;
}

/**
 *  Creates a new physical directory in this container (physical directory or symlink
 *  pointing to one), which is returned.
 *
 *  Throws an FSException on I/O errors.
 *
 *  Otherwise, this calls onDirectoryAdded() with the new FSDirectory instance (which
 *  is in the symlink target if the container is a symlink). The new instance is also
 *  returned.
 */
PFSDirectory FSContainer::createSubdirectory(const std::string &strName)
{
    PFSDirectory pDirReturn;

    PFSDirectory pDirParent;

    // If this is a symlink, then create the directory in the symlink's target instead.
    switch (_refBase.getResolvedType())
    {
        case FSTypeResolved::SYMLINK_TO_DIRECTORY:
        {
            FSSymlink *pSymlink = static_cast<FSSymlink*>(&_refBase);
            pDirParent = static_pointer_cast<FSDirectory>(pSymlink->getTarget());
        }
        break;

        case FSTypeResolved::DIRECTORY:
            pDirParent = static_pointer_cast<FSDirectory>(_refBase.getSharedFromThis());
        break;

        default:
        break;
    }

    if (!pDirParent)
        throw FSException("Cannot create directory under " + _refBase.getRelativePath());

    try
    {
        // To create a new subdirectory via Gio::File, create an empty Gio::File first
        // and then invoke make_directory on it.
        std::string strPath = pDirParent->getRelativePath() + "/" + strName;
        Debug::Log(FILE_HIGH, string(__func__) + ": creating directory \"" + strPath + "\"");

        // The follwing cannot fail.
        Glib::RefPtr<Gio::File> pGioFileNew = Gio::File::create_for_path(strPath);
        // But the following can throw.
        pGioFileNew->make_directory();

        // If we got here, we have a directory.
        pDirReturn = FSDirectory::Create(pGioFileNew);
        pDirReturn->setParent(_refBase.getSharedFromThis());

        // Notify the monitors.
        for (auto &p : _llMonitors)
            p->onDirectoryAdded(pDirReturn);
    }
    catch(Gio::Error &e)
    {
        throw FSException(e.what());
    }

    return pDirReturn;
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
    /* This nasty trickery is necessary to make std::make_shared work with a protected constructor. */
    class Derived : public FSDirectory
    {
    public:
        Derived(Glib::RefPtr<Gio::File> pGioFile) : FSDirectory(pGioFile) { }
    };

    return std::make_shared<Derived>(pGioFile);
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

PFSDirectory
FSDirectory::GetRoot()
{
    return RootDirectory::GetImpl();
}

RootDirectory::RootDirectory()
    : FSDirectory(Gio::File::create_for_path("/"))
{
    _fl = FSFlags::IS_ROOT_DIRECTORY;
}

/*static */
PRootDirectory
RootDirectory::GetImpl()
{
    FSLock lock;
    if (!s_theRoot)
    {
        // Class has a private constructor, so make_shared doesn't work without this hackery which derives a class from it.
        class Derived : public RootDirectory { };
        s_theRoot = make_shared<Derived>();
        Debug::Log(FILE_MID, "RootDirectory::Get(): instantiated the root");
    }
    else
        Debug::Log(FILE_MID, "RootDirectory::Get(): subsequent call, returning theRoot");

    return s_theRoot;
}

CurrentDirectory::CurrentDirectory()
    : FSDirectory(Gio::File::create_for_path("."))
{
    _fl = FSFlags::IS_CURRENT_DIRECTORY;
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
    FSLock lock;
    Debug::Log(FILE_LOW, "Symlink::getResolvedTypeImpl()");
    follow(lock);

    switch (_state)
    {
        case State::NOT_FOLLOWED_YET:
            throw FSException("shouldn't happen");

        case State::BROKEN:
            return FSTypeResolved::BROKEN_SYMLINK;

        case State::TO_FILE:
            return FSTypeResolved::SYMLINK_TO_FILE;

        case State::TO_DIRECTORY:
        break;
    }

    return FSTypeResolved::SYMLINK_TO_DIRECTORY;
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
    /* This nasty trickery is necessary to make std::make_shared work with a protected constructor. */
    class Derived : public FSSymlink
    {
    public:
        Derived(Glib::RefPtr<Gio::File> pGioFile) : FSSymlink(pGioFile) { }
    };

    return std::make_shared<Derived>(pGioFile);
}

PFSModelBase
FSSymlink::getTarget()
{
    FSLock lock;
    follow(lock);
    return _pTarget;
}

void
FSSymlink::follow(FSLock &lock)
{
    Debug::Enter(FILE_LOW, "FSSymlink::follow(\"" + getRelativePath() + "\"");
    if (_state == State::NOT_FOLLOWED_YET)
    {
        if (!_pParent)
            throw FSException("symlink with no parent no good");

        string strParentDir = _pParent->getRelativePath();
        Debug::Log(FILE_LOW, "parent = \"" + strParentDir + "\"");

        Glib::RefPtr<Gio::FileInfo> pInfo = _pGioFile->query_info(G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET,
                                                                  Gio::FileQueryInfoFlags::FILE_QUERY_INFO_NOFOLLOW_SYMLINKS);
        std::string strContents = pInfo->get_symlink_target();
        if (strContents.empty())
        {
            Debug::Log(FILE_MID, "readlink(\"" + getRelativePath() + "\") returned empty string -> BROKEN_SYMLINK");
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

            if ((_pTarget = FindPath(strTarget)))
            {
                if (_pTarget->getType() == FSType::DIRECTORY)
                    _state = State::TO_DIRECTORY;
                else
                    _state = State::TO_FILE;
                Debug::Log(FILE_MID, "Woke up symlink target \"" + strTarget + "\", state: " + to_string((int)_state));
            }
            else
            {
                Debug::Log(FILE_MID, "Could not find symlink target " + strTarget + " (from \"" + strContents + "\") --> BROKEN");
                _state = State::BROKEN;
            }
        }
    }

    Debug::Leave();
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
    /* This nasty trickery is necessary to make std::make_shared work with a protected constructor. */
    class Derived : public FSSpecial
    {
    public:
        Derived(Glib::RefPtr<Gio::File> pGioFile) : FSSpecial(pGioFile) { }
    };

    return std::make_shared<Derived>(pGioFile);
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
    /* This nasty trickery is necessary to make std::make_shared work with a protected constructor. */
    class Derived : public FSMountable
    {
    public:
        Derived(Glib::RefPtr<Gio::File> pGioFile) : FSMountable(pGioFile) { }
    };

    return std::make_shared<Derived>(pGioFile);
}
