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
 *  FsContainer::Impl definition
 *
 **************************************************************************/

typedef map<const string, PFsObject> FilesMap;
typedef list<PFsMonitorBase> FSMonitorsList;

struct FsContainer::Impl
{
    Mutex           mutexContents;
    Mutex           mutexFind;
    FilesMap        mapContents;
    FSMonitorsList  llMonitors;

    /**
     *  Tests if a file-system object with the given name has already been instantiated in this
     *  container. If so, it is returned. If this returns nullptr instead, that doesn't mean
     *  that the file doesn't exist becuase the container might not be fully populated, or
     *  directory contents may have changed since.
     */
    PFsObject
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
 *  FsLock
 *
 **************************************************************************/

Mutex g_mutexFiles2;

FsLock::FsLock()
    : Lock(g_mutexFiles2)
{ }


/***************************************************************************
 *
 *  ContentsLock
 *
 **************************************************************************/

/**
 *  ContentsLock is a more specialized lock to protect the list of
 *  children in a FsContainer. Since iterating over the contents
 *  list can take longer than we want to hold FsLock, this is
 *  a) more specialized and b) not global, but an instance of this
 *  exists in every FsContainer::ContentsMap.
 *
 *  This must be held when reading or modifying the following two
 *  items:
 *
 *   a) anything in a container's ContentsMap;
 *
 *   b) the _pParent pointer of any child of a container.
 *
 *  To reiterate, the FsObject::_pParent pointer is NOT protected
 *  by FsLock, but by the parent's ContentsLock since the two always
 *  get modified together.
 *
 *  To avoid deadlocks, never hold FsLock when requesting a ContentsLock.
 */
class ContentsLock : public XWP::Lock
{
public:
    ContentsLock(FsContainer& cnr)
        : Lock(cnr._pImpl->mutexContents)
    { }
};


/***************************************************************************
 *
 *  FsMonitorBase
 *
 **************************************************************************/

FsMonitorBase::~FsMonitorBase()
{
    // When the monitor is deleted (for example because the folder view is closed),
    // it must be removed.
    if (_pContainer)
        stopWatching(*_pContainer);
}

void
FsMonitorBase::startWatching(FsContainer &cnr)
{
    FsLock lock;
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
FsMonitorBase::stopWatching(FsContainer &cnr)
{
    FsLock lock;
    if (_pContainer != &cnr)
        throw FSException("Cannot remove monitor as it's not active for this container");

    _pContainer = nullptr;
    cnr._pImpl->llMonitors.remove(shared_from_this());
}


/***************************************************************************
 *
 *  FsObject
 *
 **************************************************************************/

FsObject::FsObject(FSType type,
                   const string &strBasename,
                   const FsCoreInfo &info)
    : _uID(g_uFSID++),      // atomic
      _type(type),
      _strBasename(strBasename),
      _cbSize(info._cbSize),
      _uLastModified(info._uLastModified),
      _strOwnerUser(info._strOwnerUser),
      _strOwnerGroup(info._strOwnerGroup)
{
}

bool
FsObject::isHidden()
{
    FsLock lock;
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
FsObject::makeOwnerString()
{
    return _strOwnerUser + ":" + _strOwnerGroup;
}

string
FsObject::getPath() const
{
    if (_fl.test(FSFlag::IS_ROOT_DIRECTORY))
        return _strBasename + "/";

    return getPathImpl();
}

string
FsObject::getPathImpl() const
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

PFsObject
FsObject::getParent() const
{
    if (_pParent)
        ;
    else if (_fl.test(FSFlag::IS_ROOT_DIRECTORY))
        ;       // return NULL;

    return _pParent;
}

bool
FsObject::isUnder(PFsDirectory pDir) const
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

FsContainer*
FsObject::getContainer()
{
    if (getType() == FSType::DIRECTORY)
        return (static_cast<FsDirectory*>(this));

    if (getResolvedType() == FSTypeResolved::SYMLINK_TO_DIRECTORY)
        return (static_cast<FsSymlink*>(this));

    return nullptr;
}

const string g_strFile("file");
const string g_strDirectory("directory");
const string g_strSymlink("symlink");
const string g_strOther("other");

const string&
FsObject::describeType() const
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
FsObject::describe(bool fLong /* = false */ ) const
{
    return  describeType() + " \"" + (fLong ? getPath() : getBasename()) + "\" (#" + to_string(_uID) + ")";
}

bool
FsObject::operator==(const FsObject &o) const
{
//     Debug::Log(DEBUG_ALWAYS, quote(_strBasename) + ": comparing " + to_string(this->_uLastModified) + " and " + to_string(this->_uLastModified));
    return     (this->_type == o._type)
            && (this->_cbSize == o._cbSize)
            && (this->_uLastModified == o._uLastModified)
            && (this->_strOwnerUser == o._strOwnerUser)
            && (this->_strOwnerGroup == o._strOwnerGroup)
            ;
}

void
FsObject::rename(const string &strNewName)
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
FsObject::sendToTrash()
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
FsObject::moveTo(PFsObject pTarget)
{
    copyOrMoveImpl(pTarget,
                   CopyOrMove::MOVE);      // fIsCopy
}

PFsObject
FsObject::copyTo(PFsObject pTarget)
{
    return copyOrMoveImpl(pTarget,
                          CopyOrMove::COPY);      // fIsCopy
}

PFsObject
FsObject::copyOrMoveImpl(PFsObject pTarget,
                         CopyOrMove copyOrMove)
{
    PFsObject pReturn;

    const string &strBasename = getBasename();
    if (strBasename.empty())
        throw FSException("cannot copy or move: basename is empty");

    Debug d(FILE_HIGH, string(__func__) + "(" + quote(strBasename) + ", target=" + quote(pTarget->getBasename()) + ")");

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

    if (copyOrMove == CopyOrMove::COPY)
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
FsObject::testFileOps()
{
    throw FSException("Test error");
}

/* static */
PFsObject
FsObject::FindPath(const string &strPath)
{
    return g_pFsImpl->findPath(strPath);
}

/* static */
PFsDirectory
FsObject::FindDirectory(const string &strPath)
{
    if (auto pFS = g_pFsImpl->findPath(strPath))
    {
        Debug::Log(FILE_MID, string(__func__) + "(" + quote(strPath) + ") => " + pFS->describe(true));
        if (pFS->getType() == FSType::DIRECTORY)
            return static_pointer_cast<FsDirectory>(pFS);
    }

    return nullptr;
}

/* static */
PFsDirectory
FsObject::GetHome()
{
    const char *p;
    if ((p = getenv("HOME")))
        return FindDirectory(p);
    return nullptr;
}


/***************************************************************************
 *
 *  FsContainer
 *
 **************************************************************************/

FsContainer::FsContainer(FsObject &refBase)
    : _pImpl(new Impl),
      _refBase(refBase)
{
}

/**
 *  Constructor. This is protected because an FsContainer only ever gets created through
 *  multiple inheritance.
 *
 *  We delete the implementation, but MUST clear the list first under the protection of
 *  the contents map.
 */
/* virtual */
FsContainer::~FsContainer()
{
    {
        ContentsLock lock(*this);
        _pImpl->mapContents.clear();
    }

    delete _pImpl;
}

void FsContainer::addChild(ContentsLock &lock,
                           PFsObject p)
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
FsContainer::removeChild(ContentsLock &lock,
                         PFsObject p)
{
    auto it = _pImpl->mapContents.find(p->getBasename());
    if (it == _pImpl->mapContents.end())
        throw FSException("internal: cannot find myself in parent");

    _pImpl->removeImpl(lock, it);
}

void
FsContainer::dumpContents(const string &strIntro,
                          ContentsLock &lock)
{
//     if (g_flDebugSet & FOLDER_POPULATE_LOW)
//     {
//         Debug::Log(FOLDER_POPULATE_LOW, strIntro + _refBase.getPath() + ": ");
//         for (auto &p : _pImpl->mapContents)
//             Debug::Log(FOLDER_POPULATE_LOW, "  " + quote(p.second->getPath()));
//     }
}

PFsDirectory
FsContainer::resolveDirectory()
{
    switch (_refBase.getResolvedType())
    {
        case FSTypeResolved::SYMLINK_TO_DIRECTORY:
        {
            FsSymlink *pSymlink = static_cast<FsSymlink*>(&_refBase);
            return static_pointer_cast<FsDirectory>(pSymlink->getTarget());
        }
        break;

        case FSTypeResolved::DIRECTORY:
            return static_pointer_cast<FsDirectory>(_refBase.getSharedFromThis());
        break;

        default:
        break;
    }

    throw FSException("Cannot create directory under " + _refBase.getPath());
}

PFsObject
FsContainer::find(const string &strParticle)
{
    PFsObject pReturn;
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
FsContainer::isPopulatedWithDirectories() const
{
    FsLock lock;
    return _refBase._fl.test(FSFlag::POPULATED_WITH_DIRECTORIES);
}

bool
FsContainer::isCompletelyPopulated() const
{
    FsLock lock;
    return _refBase._fl.test(FSFlag::POPULATED_WITH_ALL);
}

void
FsContainer::unsetPopulated()
{
    FsLock lock;
    _refBase._fl.clear(FSFlag::POPULATED_WITH_ALL);
    _refBase._fl.clear(FSFlag::POPULATED_WITH_DIRECTORIES);
}

condition_variable_any g_condFolderPopulated;

size_t
FsContainer::getContents(FsVector &vFiles,
                         Get getContents,
                         FsVector *pvFilesAdded,
                         FsVector *pvFilesRemoved,        //!< out: list of file-system object that have been removed, or nullptr (optional)
                         StopFlag *pStopFlag,
                         bool fFollowSymlinks /* = false */)
{
    Debug d(FILE_HIGH, "FsContainer::getContents(\"" + _refBase.getPath() + "\")");

    size_t c = 0;

    string strException;

    try
    {
        bool fStopped = false;

        // If this container is being populated on another thread, block on the global
        // condition variable until the other thread posts it (when populate is done).
        unique_lock<recursive_mutex> lock(_pImpl->mutexFind);
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

            PFsObject pSharedThis = _refBase.getSharedFromThis();

            /* The refresh algorithm is simple. For every file returned from the Gio backend,
             * we check if it's already in the contents map; if not, it is added. This adds
             * missing files. To remove awake files that have been removed on disk, every file
             * returned by the Gio backend that was either already awake or has been added in
             * the above loop is marked with  a "dirty" flag. A final loop then removes all
             * objects from the contents map that do not have the "dirty" flag set. */
            if (getContents == Get::ALL)
            {
                ContentsLock cLock(*this);
                FsLock lock2Temp;
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
                PFsObject pAwake = _pImpl->isAwake(cLock, strBasename, &it);
                // Also wake up a new object from the GioFile. This is necessary
                // so we can detect if the type of the file changed. This will not have the dirty flag set.
                PFsObject pTemp = g_pFsImpl->makeAwake(strThisPath,
                                                       strBasename,
                                                       fIsLocal);

                if (    (pAwake)
                    // Use our operator== to compare, which which check timestamps and size.
                     && (*pAwake == *pTemp)
                   )
                {
                    // Cached item valid: clear the dirty flag.
                    FsLock lock2Temp;
                    pAwake->_fl.clear(FSFlag::DIRTY);
                }
                else
                {
                    PFsObject pAddToContents;

                    if (pAwake)
                    {
                        // Type of file changed: then remove it from the folder before adding the new one.
                        if (pvFilesRemoved)
                            pvFilesRemoved->push_back(pAwake);
                        _pImpl->removeImpl(cLock, it);

                        pAwake = nullptr;
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

                        FSTypeResolved tr;

                        if (t == FSType::SYMLINK)
                            if (fFollowSymlinks)
                                tr = pAddToContents->getResolvedType();   // This calls follow() and we don't have to typecast here.

                        if (    (getContents == Get::FIRST_FOLDER_ONLY)
                             && (!pAddToContents->isHidden())
                           )
                        {
                            if (t == FSType::DIRECTORY)
                                break;      // we're done
                            else if (t == FSType::SYMLINK)
                            {
                                if (!fFollowSymlinks)
                                    // Not yet followed above:
                                    tr = pAddToContents->getResolvedType();
                                if (tr == FSTypeResolved::SYMLINK_TO_DIRECTORY)
                                    break;
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

            FsLock lock2Temp;
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

PFsDirectory
FsContainer::createSubdirectory(const string &strName)
{
    PFsDirectory pDirReturn;

    // If this is a symlink, then create the directory in the symlink's target instead.
    PFsDirectory pDirParent = resolveDirectory();

    // This throws on errors.
    pDirReturn = g_pFsImpl->createSubdirectory(pDirParent->getPathImpl(), strName);

    ContentsLock cLock(*this);
    this->addChild(cLock, pDirReturn);

    return pDirReturn;
}

PFsFile
FsContainer::createEmptyDocument(const string &strName)
{
    PFsFile pFileReturn;

    // If this is a symlink, then create the directory in the symlink's target instead.
    PFsDirectory pDirParent = resolveDirectory();

    pFileReturn = g_pFsImpl->createEmptyDocument(pDirParent->getPathImpl(), strName);

    ContentsLock cLock(*this);
    this->addChild(cLock, pFileReturn);

    return pFileReturn;
}

void
FsContainer::notifyFileAdded(PFsObject pFS) const
{
    Debug d(FILEMONITORS, string(__func__) + "(" + pFS->getPath() + ")");
    FsLock lock;
    for (auto &pMonitor : _pImpl->llMonitors)
        pMonitor->onItemAdded(pFS);
}

void
FsContainer::notifyFileRemoved(PFsObject pFS) const
{
    Debug d(FILEMONITORS, string(__func__) + "(" + pFS->getPath() + ")");
    FsLock lock;
    for (auto &pMonitor : _pImpl->llMonitors)
        pMonitor->onItemRemoved(pFS);
}

void
FsContainer::notifyFileRenamed(PFsObject pFS, const string &strOldName, const string &strNewName) const
{
    Debug d(FILEMONITORS, string(__func__) + "(" + strOldName + " -> " + strNewName + ")");
    FsLock lock;
    for (auto &pMonitor : _pImpl->llMonitors)
        pMonitor->onItemRenamed(pFS, strOldName, strNewName);
}


/***************************************************************************
 *
 *  FsDirectory
 *
 **************************************************************************/

/* static */
PFsDirectory FsDirectory::GetCwdOrThrow()
{
    char cwd[FS_BUF_LEN];
    string strCWD = getcwd(cwd, sizeof(cwd));
    auto p = FsObject::FindDirectory(strCWD);
    if (!p)
        throw FSException("failed to find current directory " + quote(strCWD));

    return p;
}


/***************************************************************************
 *
 *  FsSymlink
 *
 **************************************************************************/

/* virtual */
FSTypeResolved
FsSymlink::getResolvedType() /* override */
{
//     Debug::Log(FILE_LOW, "FsSymlink::getResolvedType()");

    auto state = follow();

    switch (state)
    {
        case State::BROKEN:
            return FSTypeResolved::BROKEN_SYMLINK;

        case State::RESOLVED_TO_FILE:
            return FSTypeResolved::SYMLINK_TO_FILE;

        case State::RESOLVED_TO_DIRECTORY:
            return FSTypeResolved::SYMLINK_TO_DIRECTORY;

        case State::RESOLVED_TO_OTHER:
            return FSTypeResolved::SYMLINK_TO_OTHER;

        case State::NOT_FOLLOWED_YET:
        case State::RESOLVING:
        break;
    }

    throw FSException("shouldn't happen");
}

/* static */
PFsSymlink
FsSymlink::Create(const string &strBasename,
                  uint64_t uLastModified)
{
    /* This nasty trickery is necessary to make make_shared work with a protected constructor. */
    class Derived : public FsSymlink
    {
    public:
        Derived(const string &strBasename, uint64_t uLastModified) : FsSymlink(strBasename, uLastModified) { }
    };

    return make_shared<Derived>(strBasename, uLastModified);
}

PFsObject
FsSymlink::getTarget()
{
    follow();
    return _pTarget;
}

condition_variable_any g_condSymlinkResolved;

FsSymlink::State
FsSymlink::follow()
{
    // Make sure that only one thread resolves this link at a time. We
    // set the link's state to State::RESOLVING below while we're following,
    // so if the link's state is State::RESOLVING now, it means another
    // thread is already in the process of resolving this link. In that
    // case, block on a condition variable which will get posted by the
    // other thread.
    unique_lock<recursive_mutex> lock(_mutexState);
    while (_state == State::RESOLVING)
        g_condSymlinkResolved.wait(lock);

    // Lock is held again now.
    // Either we're the only thread, or the other thread is done following.
    // So now REALLY test the state.

    if (_state == State::NOT_FOLLOWED_YET)
    {
        Debug d(FILE_MID, "FsSymlink::follow(" + quote(getPath()) + "): not followed yet, resolving");

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
                            _state = State::RESOLVED_TO_DIRECTORY;
                        break;

                        case FSType::FILE:
                            _state = State::RESOLVED_TO_FILE;
                        break;

                        case FSType::UNINITIALIZED:
                        case FSType::SYMLINK:
                        case FSType::SPECIAL:
                        case FSType::MOUNTABLE:
                            _state = State::RESOLVED_TO_OTHER;
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


