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

FSLock::FSLock()
    : LockBase(g_mutexFiles)
{ }


/***************************************************************************
 *
 *  FSDirectory::Impl definition
 *
 **************************************************************************/

typedef map<const std::string, PFSModelBase> FilesMap;

struct FSDirectory::Impl
{
    FilesMap    mapContents;
};


/***************************************************************************
 *
 *  FSModelBase
 *
 **************************************************************************/

/**
 *  Returns nullptr if the path is invalid.
 */
/* static */
PFSModelBase FSModelBase::FindPath(const std::string &strPath, FSLock &lock)
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

    string strForStat;
    uint c = 0;
//     bool fPreviousWasNamed = false;
    PFSModelBase pCurrent;
    for (auto const &strParticle : aParticles)
    {
        if (strParticle == ".")
        {
            if (aParticles.size() > 1)
                Debug::Log(FILE_LOW, "Ignoring particle . in list");
            else
                return CurrentDirectory::GetImpl(lock);
        }
        else
        {
            PFSDirectory pDir;
            bool fCollapsing = false;
            if (!pCurrent)
            {
                if (fAbsolute)
                    // First item on an absolute path must be a child of the root directory.
                    pDir = RootDirectory::GetImpl(lock);
                else
                {
                    // First item on a relative path must be a child of the curdir.
                    pDir = CurrentDirectory::GetImpl(lock);
                    strForStat = ".";
                }
            }
            else
            {
                // Later particles:

                if (    (strParticle == "..")
//                      && (fPreviousWasNamed)
                   )
                {
                    // Avoid things like ./../subdir1/../subdir2/
                    //                                ^ we would create pChild here
                    //                        ^ this is pCurrent at this time
                    //           =>      ./../subdir2/
                    fCollapsing = true;
//                     fPreviousWasNamed = false;

                    auto pPrev = pCurrent;

                    // Go back to the parent and skip over the rest of this step.
                    pCurrent = pCurrent->_pParent;
                    strForStat = getDirnameString(strForStat);

                    Debug::Log(FILE_LOW, "Loop " + to_string(c) + ": collapsed \"" + pPrev->getRelativePath() + "/" + strParticle + "\" to " + quote(pCurrent->getRelativePath()));
                }
                else if (!(pDir = pCurrent->resolveDirectory(lock)))
                    throw FSException("path particle \"" + pCurrent->getBasename() + "\" is not a directory");
            }

            if (!fCollapsing)
            {
                strForStat += "/" + strParticle;
                PFSModelBase pChild;

                if ((pChild = pDir->isAwake(strParticle, lock)))
                    Debug::Log(FILE_LOW, "Loop " + to_string(c) + ": particle \"" + strParticle + "\" is already awake: " + pChild->describe());
                else
                {
                    Debug::Log(FILE_LOW, "Loop " + to_string(c) + ": particle \"" + strParticle + "\" => stat(\"" + strForStat + ")\"");
                    auto pGioFile = Gio::File::create_for_path(strForStat);
                    // The above never fails. To find out whether the path is valid we need to query the type, which does blocking I/O.
                    if (!(pChild = MakeAwake(pGioFile)))
                    {
                        Debug::Log(FILE_LOW, "  could not make awake");
                        pCurrent.reset();
                        break;
                    }

                    pChild->setParent(pDir, lock);

                    if (!pDir->isAwake(strParticle, lock))
                        throw FSException("error: particle \"" + strParticle + "\" is not in parent map");
                }

//                 if (strParticle != "..")
//                     fPreviousWasNamed = true;

                pCurrent = pChild;
            }
            ++c;
        }
    }

    Debug::Leave();

    Debug::Log(FILE_LOW, "Result: " + (pCurrent ? pCurrent->describe() : "NULL"));

    return pCurrent;
}

/**
 *  Returns this as a Directory instance, if this is a directory. If this is a symlink to a
 *  directory, the target is returned.
 *
 *  Returns NULL if this is a file or a symlink to something other than a directory.
 */
PFSDirectory FSModelBase::resolveDirectory(FSLock &lock)
{
    if (getType() == FSType::DIRECTORY)
    {
        Debug::Log(FILE_LOW, "FileBase::resolveDirectory(\"" + getRelativePath() + "\"): DIRECTORY, returning #" + to_string(_uID));
        return static_pointer_cast<FSDirectory>(shared_from_this());
    }

    if (getResolvedType(lock) == FSTypeResolved::SYMLINK_TO_DIRECTORY)
    {
        Debug::Log(FILE_LOW, "getting symlink target");
        FSSymlink *pSymlink = static_cast<FSSymlink*>(this);
        return static_pointer_cast<FSDirectory>(pSymlink->getTarget(lock));
    }

    return NULL;
}

/**
 *  Creates a new FSModelBase instance, which is not yet related to a folder. You must call setParent()
 *  on the return value.
 *
 *  Returns nullptr if the given file is for an invalid path. (GIO creates File instances without
 *  testing I/O, so the given pGioFile could be for a non-existing file. We do the testing here.)
 */
/* static */
PFSModelBase FSModelBase::MakeAwake(Glib::RefPtr<Gio::File> pGioFile)
{
    PFSModelBase pReturn = nullptr;
    auto type = pGioFile->query_file_type(Gio::FileQueryInfoFlags::FILE_QUERY_INFO_NOFOLLOW_SYMLINKS);

    switch (type)
    {
        case Gio::FileType::FILE_TYPE_NOT_KNOWN:       // File's type is unknown.
        break;      // return nullptr

        case Gio::FileType::FILE_TYPE_REGULAR:         // File handle represents a regular file.
            pReturn = FSFile::Create(pGioFile);
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

    return pReturn;
}

/* static */
PFSDirectory FSModelBase::FindDirectory(const std::string &strPath, FSLock &lock)
{
    if (auto pFS = FindPath(strPath, lock))
    {
        Debug::Log(FILE_MID, "result for \"" + strPath + "\": " + pFS->describe());
        if (pFS->getType() == FSType::DIRECTORY)
            return std::static_pointer_cast<FSDirectory>(pFS);
    }

    return nullptr;
}

FSModelBase::FSModelBase(FSType type, Glib::RefPtr<Gio::File> pGioFile)
    : _uID(g_uFSID++),      // atomic
      _type(type),
      _pGioFile(pGioFile)
{
}

void FSModelBase::setParent(PFSDirectory pParentDirectory, FSLock &lock)
{
    if (!pParentDirectory)
    {
        if (_pParent)
        {
            // Unsetting previous parent:
            auto it = _pParent->_pImpl->mapContents.find(getBasename());
            if (it == _pParent->_pImpl->mapContents.end())
                throw FSException("internal: cannot find myself in parent");

            _pParent->_pImpl->mapContents.erase(it);
            _pParent = nullptr;
        }
    }
    else
    {
        if (_pParent)
            throw FSException("setParent called twice");

        // Setting initial parent:
        _pParent = pParentDirectory;
        std::string strBasename(getBasename());
        Debug::Log(FILE_LOW, "storing \"" + strBasename + "\" in parent map");
        pParentDirectory->_pImpl->mapContents[strBasename] = shared_from_this();
    }
}

std::string FSModelBase::getBasename()
{
    return _pGioFile->get_basename();
}

/**
 *  Returns true if the file-system object has the "hidden" attribute, according to however Gio defines it.
 *
 *  Overridden for symlinks!
 */
/* virtual */
bool FSModelBase::isHidden(FSLock &lock)
{
    auto pInfo = _pGioFile->query_info(G_FILE_ATTRIBUTE_STANDARD_IS_HIDDEN,
                                       Gio::FileQueryInfoFlags::FILE_QUERY_INFO_NOFOLLOW_SYMLINKS);
    return pInfo->is_hidden();
}

/*
 *  Expands the path without resorting to realpath(), which would hit the disk.
 */
std::string FSModelBase::getRelativePath()
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

uint64_t FSModelBase::getFileSize()
{
    return _pGioFile->query_info()->get_size();
}

Glib::ustring FSModelBase::getIcon()
{
    return _pGioFile->query_info()->get_icon()->to_string();
}

PFSDirectory FSModelBase::getParent()
{
    if (_pParent)
        ;
    else if (_flFile & FL_IS_ROOT_DIRECTORY)
        ;       // return NULL;

    return _pParent;
}

const std::string g_strFile("file");
const std::string g_strDirectory("directory");
const std::string g_strSymlink("symlink");
const std::string g_strOther("other");

const std::string& FSModelBase::describeType()
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

std::string FSModelBase::describe(bool fLong /* = false */ )
{
    return  describeType() + " \"" + (fLong ? getRelativePath() : getBasename()) + "\" (#" + to_string(_uID) + ")";
}


/***************************************************************************
 *
 *  FSFile
 *
 **************************************************************************/

FSFile::FSFile(Glib::RefPtr<Gio::File> pGioFile)
    : FSModelBase(FSType::FILE, pGioFile)
{
}

/* static */
PFSFile FSFile::Create(Glib::RefPtr<Gio::File> pGioFile)
{
    /* This nasty trickery is necessary to make std::make_shared work with a protected constructor. */
    class Derived : public FSFile
    {
    public:
        Derived(Glib::RefPtr<Gio::File> pGioFile) : FSFile(pGioFile) { }
    };

    return std::make_shared<Derived>(pGioFile);
}


/***************************************************************************
 *
 *  FSDirectory
 *
 **************************************************************************/

FSDirectory::FSDirectory(Glib::RefPtr<Gio::File> pGioFile)
    : FSModelBase(FSType::DIRECTORY, pGioFile),
      _pImpl(new Impl)

{
}

/* virtual */
FSDirectory::~FSDirectory()
{
    delete _pImpl;
}

/* static */
PFSDirectory FSDirectory::Create(Glib::RefPtr<Gio::File> pGioFile)
{
    /* This nasty trickery is necessary to make std::make_shared work with a protected constructor. */
    class Derived : public FSDirectory
    {
    public:
        Derived(Glib::RefPtr<Gio::File> pGioFile) : FSDirectory(pGioFile) { }
    };

    return std::make_shared<Derived>(pGioFile);
}

PFSModelBase FSDirectory::isAwake(const string &strParticle,
                                  FSLock &lock)
{
    auto it = _pImpl->mapContents.find(strParticle);
    if (it != _pImpl->mapContents.end())
        return it->second;

    return nullptr;
}

size_t FSDirectory::getContents(FSList &llFiles,
                                Get getContents,
                                FSLock &lock)
{
    Debug::Enter(FILE_LOW, "Directory::getContents(\"" + getBasename() + "\")");
    if (    ((getContents == Get::ALL) && !isCompletelyPopulated())
         || ((getContents != Get::ALL) && !isPopulatedWithDirectories())
       )
    {
        PFSDirectory pSharedThis = static_pointer_cast<FSDirectory>(shared_from_this());

        Glib::RefPtr<Gio::FileEnumerator> en;
        if (!(en = _pGioFile->enumerate_children("*",
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
                     && (!isAwake(strThis, lock))
                   )
                {
                    PFSModelBase pKeep;

                    PFSModelBase pTemp = MakeAwake(pGioFile);

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
                        pKeep->setParent(pSharedThis, lock);

                        if (getContents == Get::FIRST_FOLDER_ONLY)
                            if (t == FSType::DIRECTORY)
                                break;      // we're done
                    }
                }
            }

            if (getContents == Get::FOLDERS_ONLY)
                _flFile |= FL_POPULATED_WITH_DIRECTORIES;
            else if (getContents == Get::ALL)
                _flFile |= (FL_POPULATED_WITH_DIRECTORIES | FL_POPULATED_WITH_ALL);
        }
    }

    size_t c = 0;
    for (auto it : _pImpl->mapContents)
    {
        auto &p = it.second;
        // Leave out ".." in the list.
        if (p != _pParent)
        {
            if (    (getContents == Get::ALL)
                 || (p->getType() == FSType::DIRECTORY)
                 || (    (getContents == Get::FOLDERS_ONLY)
                      && (p->getResolvedType(lock) == FSTypeResolved::SYMLINK_TO_DIRECTORY)
                    )
               )
            {
                llFiles.push_back(p);
                ++c;

                if (getContents == Get::FIRST_FOLDER_ONLY)
                    break;
            }
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
        if (p != _pParent)
            if (p->getResolvedType(lock) == FSTypeResolved::SYMLINK_TO_DIRECTORY)
            {
                llFiles.push_back(p);
                ++c;
                break;
            }
    }

    Debug::Leave();

    return c;
}

/*static */
PFSModelBase FSDirectory::find(const string &strParticle,
                               FSLock &lock)
{
//     AssertBasename(__func__, strParticle);
    PFSModelBase pReturn;
    if ((pReturn = isAwake(strParticle, lock)))
        Debug::Log(FILE_MID, "Directory::find(\"" + strParticle + "\") => already awake " + pReturn->describe());
    else
    {
        const string strPath(getRelativePath() + "/" + strParticle);
        Debug::Enter(FILE_MID, "Directory::find(\"" + strPath + "\")");

        auto pGioFile = Gio::File::create_for_path(strPath);
        // The above never fails. To find out whether the path is valid we need to query the type, which does blocking I/O.
        if (!(pReturn = MakeAwake(pGioFile)))
            Debug::Log(FILE_LOW, "  could not make awake");
        else
            pReturn->setParent(static_pointer_cast<FSDirectory>(shared_from_this()), lock);

        Debug::Leave();
    }

    return pReturn;
}

PFSDirectory FSDirectory::findSubdirectory(const std::string &strParticle,
                                           FSLock &lock)
{
    PFSModelBase p;
    if ((p = find(strParticle, lock)))
        return p->resolveDirectory(lock);

    return nullptr;
}

/**
 *  Returns the user's home directory, or nullptr on errors.
 */
/* static */
PFSDirectory FSDirectory::GetHome(FSLock &lock)
{
    const char *p;
    if ((p = getenv("HOME")))
        return FindDirectory(p, lock);
    return nullptr;
}

PFSDirectory FSDirectory::GetRoot(FSLock &lock)
{
    return RootDirectory::GetImpl(lock);
}

RootDirectory::RootDirectory()
    : FSDirectory(Gio::File::create_for_path("/"))
{
    _flFile = FL_IS_ROOT_DIRECTORY;
}

/*static */
PRootDirectory RootDirectory::GetImpl(FSLock &lock)
{
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
    _flFile = FL_IS_CURRENT_DIRECTORY;
}

/*static */
PCurrentDirectory CurrentDirectory::GetImpl(FSLock &lock)
{
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
    : FSModelBase(FSType::SYMLINK, pGioFile)
{
}

/* virtual */
FSTypeResolved FSSymlink::getResolvedType(FSLock &lock) /* override */
{
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

/* static */
PFSSymlink FSSymlink::Create(Glib::RefPtr<Gio::File> pGioFile)
{
    /* This nasty trickery is necessary to make std::make_shared work with a protected constructor. */
    class Derived : public FSSymlink
    {
    public:
        Derived(Glib::RefPtr<Gio::File> pGioFile) : FSSymlink(pGioFile) { }
    };

    return std::make_shared<Derived>(pGioFile);
}

void FSSymlink::follow(FSLock &lock)
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
            if ((_pTarget = FindPath(strTarget, lock)))
            {
                if (_pTarget->getType() == FSType::DIRECTORY)
                    _state = State::TO_DIRECTORY;
                else
                    _state = State::TO_FILE;
                Debug::Log(FILE_MID, "Woke up symlink target \"" + strTarget + "\", state: " + to_string(int(_state)));
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

/**
 *  Override the FSModelBase implementation to instead return the value for the target.
 */
/* virtual */
bool FSSymlink::isHidden(FSLock &lock) /* override */
{
    auto p = this->getTarget(lock);
    if (p)
        return p->isHidden(lock);

    return FSModelBase::isHidden(lock);
}


/***************************************************************************
 *
 *  FSSpecial
 *
 **************************************************************************/

FSSpecial::FSSpecial(Glib::RefPtr<Gio::File> pGioFile)
    : FSModelBase(FSType::SPECIAL, pGioFile)
{
}

/* static */
PFSSpecial FSSpecial::Create(Glib::RefPtr<Gio::File> pGioFile)
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
    : FSModelBase(FSType::MOUNTABLE, pGioFile)
{
}

/* static */
PFSMountable FSMountable::Create(Glib::RefPtr<Gio::File> pGioFile)
{
    /* This nasty trickery is necessary to make std::make_shared work with a protected constructor. */
    class Derived : public FSMountable
    {
    public:
        Derived(Glib::RefPtr<Gio::File> pGioFile) : FSMountable(pGioFile) { }
    };

    return std::make_shared<Derived>(pGioFile);
}
