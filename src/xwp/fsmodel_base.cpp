/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "xwp/fsmodel_base.h"

#include "xwp/debug.h"
#include "xwp/stringhelp.h"
#include "xwp/except.h"
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <set>
#include <map>
#include <list>

#include <unistd.h>


/***************************************************************************
 *
 *  Globals, static variable instantiations
 *
 **************************************************************************/

atomic<uint64_t>  g_uFSID(1);

FsImplBase* g_pFsImpl = nullptr;


/***************************************************************************
 *
 *  FsImplBase definition
 *
 **************************************************************************/

FsImplBase::FsImplBase()
{
    g_pFsImpl = this;
}


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

FSLock::FSLock()
    : Lock(g_mutexFiles)
{ }


/***************************************************************************
 *
 *  ContentsLock
 *
 **************************************************************************/

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

FSModelBase::FSModelBase(FSType type,
                         const string &strBasename,
                         uint64_t cbSize)
    : _uID(g_uFSID++),      // atomic
      _type(type),
      _strBasename(strBasename),
      _cbSize(cbSize)
{
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
    {
        // Update the contents map, which sorts by name.
        ContentsLock cLock(*pCnr);

        g_pFsImpl->rename(*this, strNewName);

        // First remove the old item; this calls getBasename(), which still has the old base name.
        pCnr->removeChild(cLock, shared_from_this());

        _strBasename = strNewName;
        pCnr->addChild(cLock, shared_from_this());
    }
}

void
FSModelBase::sendToTrash()
{
    auto pParent = getParent();
    if (!pParent)
        throw FSException("cannot get parent for trashing");
    auto pParentCnr = pParent->getContainer();
    if (!pParentCnr)
        throw FSException("cannot get parent container for trashing");

    ContentsLock cLock(*pParentCnr);

    g_pFsImpl->trash(*this);

    pParentCnr->removeChild(cLock, shared_from_this());
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

    string strTargetPath = pTarget->getPath();
    strTargetPath += "/" + this->getBasename();

    if (fIsCopy)
    {
        // This is a copy:

        g_pFsImpl->copy(d, *this, strTargetPath);

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

        g_pFsImpl->move(d, *this, strTargetPath);

        {
            ContentsLock cLock(*pTargetCnr);
            pTargetCnr->addChild(cLock, shared_from_this());

//                 pParentCnr->dumpContents("After copy/move, contents of ", cLock);
        }
    }

    return pReturn;
}

void
FSModelBase::testFileOps()
{
    XWP::Thread::Sleep(50);
}

/* static */
PFSModelBase
FSModelBase::FindPath(const string &strPath)
{
    return g_pFsImpl->findPath(strPath);
}

/* static */
PFSDirectory
FSModelBase::FindDirectory(const string &strPath)
{
    if (auto pFS = g_pFsImpl->findPath(strPath))
    {
        Debug::Log(FILE_MID, string(__func__) + "(" + quote(strPath) + ") => " + pFS->describe(true));
        if (pFS->getType() == FSType::DIRECTORY)
            return static_pointer_cast<FSDirectory>(pFS);
    }

    return nullptr;
}

/* static */
PFSDirectory
FSModelBase::GetHome()
{
    const char *p;
    if ((p = getenv("HOME")))
        return FindDirectory(p);
    return nullptr;
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
    Debug::Log(FILE_LOW, "storing " + quote(strBasename) + " in parent map");

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
//     if (g_flDebugSet & FOLDER_POPULATE_LOW)
//     {
//         Debug::Log(FOLDER_POPULATE_LOW, strIntro + _refBase.getPath() + ": ");
//         for (auto &p : _pImpl->mapContents)
//             Debug::Log(FOLDER_POPULATE_LOW, "  " + quote(p.second->getPath()));
//     }
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
        Debug d(FILE_MID, "Directory::find(" + quote(strParticle) + "): particle needs waking up");

        if ((pReturn = g_pFsImpl->makeAwake(_refBase.getPathImpl(),
                                            strParticle,
                                            _refBase.hasFlag(FSFlag::IS_LOCAL))))
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

            bool fIsLocal = _refBase.hasFlag(FSFlag::IS_LOCAL);

            string strThisPath = _refBase.getPathImpl();
            PFsDirEnumeratorBase pEnumerator = g_pFsImpl->beginEnumerateChildren(*this);
            string strBasename;
            while (g_pFsImpl->getNextChild(pEnumerator, strBasename))
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
                PFSModelBase pAwake = _pImpl->isAwake(cLock, strBasename, &it);
                // Also wake up a new object from the GioFile. This is necessary
                // so we can detect if the type of the file changed. This will not have the dirty flag set.
                PFSModelBase pTemp = g_pFsImpl->makeAwake(strThisPath,
                                                          strBasename,
                                                          fIsLocal);

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
                            Debug d(FILE_LOW, "Waking up directory " + strBasename);
                            pAddToContents = pTemp;
                        }
                        break;

                        case FSType::SYMLINK:
                        {
                            // Need to wake up the symlink to figure out if it's a link to a dir.
                            Debug d(FILE_LOW, "Waking up symlink " + strBasename);
                            pAddToContents = pTemp;
                        }
                        break;

                        default:
                            // Ordinary file:
                            if (getContents == Get::ALL)
                            {
                                Debug d(FILE_LOW, "Waking up plain file " + strBasename);
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
    catch (FSException &e)
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

    // This throws on errors.
    pDirReturn = g_pFsImpl->createSubdirectory(pDirParent->getPathImpl(), strName);

    ContentsLock cLock(*this);
    this->addChild(cLock, pDirReturn);

    return pDirReturn;
}

PFSFile
FSContainer::createEmptyDocument(const string &strName)
{
    PFSFile pFileReturn;

    // If this is a symlink, then create the directory in the symlink's target instead.
    PFSDirectory pDirParent = resolveDirectory();

    pFileReturn = g_pFsImpl->createEmptyDocument(pDirParent->getPathImpl(), strName);

    ContentsLock cLock(*this);
    this->addChild(cLock, pFileReturn);

    return pFileReturn;
}

void
FSContainer::notifyFileAdded(PFSModelBase pFS) const
{
    Debug d(FILEMONITORS, string(__func__) + "(" + pFS->getPath() + ")");
    FSLock lock;
    for (auto &pMonitor : _pImpl->llMonitors)
        pMonitor->onItemAdded(pFS);
}

void
FSContainer::notifyFileRemoved(PFSModelBase pFS) const
{
    Debug d(FILEMONITORS, string(__func__) + "(" + pFS->getPath() + ")");
    FSLock lock;
    for (auto &pMonitor : _pImpl->llMonitors)
        pMonitor->onItemRemoved(pFS);
}

void
FSContainer::notifyFileRenamed(PFSModelBase pFS, const string &strOldName, const string &strNewName) const
{
    Debug d(FILEMONITORS, string(__func__) + "(" + strOldName + " -> " + strNewName + ")");
    FSLock lock;
    for (auto &pMonitor : _pImpl->llMonitors)
        pMonitor->onItemRenamed(pFS, strOldName, strNewName);
}


/***************************************************************************
 *
 *  FSDirectory
 *
 **************************************************************************/

/* static */
PFSDirectory FSDirectory::GetCwdOrThrow()
{
    char cwd[FS_BUF_LEN];
    string strCWD = getcwd(cwd, sizeof(cwd));
    auto p = FSModelBase::FindDirectory(strCWD);
    if (!p)
        throw FSException("failed to find current directory " + quote(strCWD));

    return p;
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
FSSymlink::Create(const string &strBasename)
{
    /* This nasty trickery is necessary to make make_shared work with a protected constructor. */
    class Derived : public FSSymlink
    {
    public:
        Derived(const string &strBasename) : FSSymlink(strBasename) { }
    };

    return make_shared<Derived>(strBasename);
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

        string strThisPath = quote(this->getPath());

        try
        {
            string strContents = g_pFsImpl->getSymlinkContents(*this);

            if (strContents.empty())
            {
                Debug::Log(FILE_MID, "readlink(" + strThisPath + ") returned empty string -> BROKEN_SYMLINK");
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

                auto pTarget = g_pFsImpl->findPath(strTarget);
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
        }
        catch (...)
        {
            Debug::Log(FILE_HIGH, "Could not find symlink target of " + strThisPath + " --> BROKEN");
            lock.lock();
            _state = State::BROKEN;
        }

        // Post the condition variable so that other threads who may be blocked in this function
        // on this symlink will wake up.
        g_condSymlinkResolved.notify_all();
    }

    return _state;
}


