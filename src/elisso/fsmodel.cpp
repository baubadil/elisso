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

#include <mutex>

#include <string.h>


/***************************************************************************
 *
 *  FSLockGuard
 *
 **************************************************************************/

std::mutex g_mutexFiles;

struct FSLock::Impl
{
    std::lock_guard<std::mutex> *pGuard;

    Impl()
    {
        pGuard = new std::lock_guard<std::mutex>(g_mutexFiles);
    }

    ~Impl()
    {
        delete pGuard;
    }
};

FSLock::FSLock()
    : _pImpl()
{
}

/* virtual */
FSLock::~FSLock()
{
    delete _pImpl;
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
PFSModelBase FSModelBase::FindPath(const std::string &strPath,
                                   FSLock &lock)
{
    auto pGioFile = Gio::File::create_for_path(strPath);
    return MakeAwake(pGioFile);
}

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
PFSDirectory FSModelBase::FindDirectory(const std::string &strPath,
                                        FSLock &lock)
{
    if (auto pFS = FindPath(strPath, lock))
        if (pFS->getType() == FSType::DIRECTORY)
            return std::static_pointer_cast<FSDirectory>(pFS);

    return nullptr;
}

FSModelBase::FSModelBase(FSType type, Glib::RefPtr<Gio::File> pGioFile)
    : _type(type),
      _pGioFile(pGioFile)
{
}

std::string FSModelBase::getBasename()
{
    return _pGioFile->get_basename();
}

uint64_t FSModelBase::getFileSize()
{
    return _pGioFile->query_info()->get_size();
}

std::string FSModelBase::getIcon()
{
    return _pGioFile->query_info()->get_icon()->to_string();
}

/* static */
std::string FSModelBase::GetDirname(const std::string& str)
{
    const char *p1 = str.c_str();
    const char *p2 = strrchr(p1, '/');
    Debug::Log(FILE_LOW, "dirname of " + string(p1));
    if (p2)
        return str.substr(0, p2 - p1);
    return "";
}

/* static */
std::string FSModelBase::GetBasename(const std::string &str)
{
    const char *p1 = str.c_str();
    const char *p2 = strrchr(p1, '/');
    if (p2)
        return p2 + 1;
    return str;
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
      _pop(FolderPopulated::NOT)
{
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

void FSDirectory::getContents(FSList &llFiles,
                              bool fDirsOnly)
{
    Debug::Enter(FILE_LOW, "Directory::getContents(\"" + getBasename() + "\")");
    if (    (fDirsOnly && !isPopulatedWithDirectories())
         || (!fDirsOnly && !isCompletelyPopulated())
       )
    {
        Glib::RefPtr<Gio::FileEnumerator> en = _pGioFile->enumerate_children("*",
                                                                               Gio::FileQueryInfoFlags::FILE_QUERY_INFO_NOFOLLOW_SYMLINKS);
        Glib::RefPtr<Gio::FileInfo> pInfo;
        while (pInfo = en->next_file())
        {
            auto p = en->get_child(pInfo);
            auto p2 = FSModelBase::MakeAwake(p);
            llFiles.push_back(p2);
        }

//         string strRealPath = getRelativePath();
//         const char *pcszPath = strRealPath.c_str();
//         DIR *pcdir;
//         struct dirent *ent;
//         if ((pcdir = opendir(pcszPath)) != NULL)
//         {
//             while ((ent = readdir(pcdir)) != NULL)
//             {
//                 string strThis = ent->d_name;
//                 if (    (strThis != ".")
//                      && (strThis != "..")
//                      && (!isAwake(strThis))
//                    )
//                 {
//                     PFileBase pSetParentFor, pNew;
//
//                     switch (ent->d_type)
//                     {
//                         case DT_UNKNOWN:
//                             // Not all file systems support reporting the type, so then we need to run stat().
//                             pNew = find(strThis);
//                                 // This sets the parent already, so set pNew instead of pSetParentFor.
//                         break;
//
//                         case DT_DIR:
//                             // Always wake up directories.
//                             Debug::Enter(FILE_LOW, "Waking up directory " + strThis);
//                             pSetParentFor = Directory::MakeShared(strThis);
//                             Debug::Leave();
//                         break;
//
//                         case DT_LNK:
//                             // Need to wake up the symlink to figure out if it's a link to a dir.
//                             Debug::Enter(FILE_LOW, "Waking up symlink " + strThis);
//                             pSetParentFor = Symlink::MakeShared(strThis);
//                             Debug::Leave();
//                         break;
//
//                         default:
//                             // Ordinary file:
//                             if (!fDirsOnly)
//                             {
//                                 Debug::Enter(FILE_LOW, "Waking up plain file " + strThis);
//                                 pSetParentFor = File::MakeShared(strThis);
//                                 Debug::Leave();
//                             }
//                         break;
//                     }
//
//                     if (pSetParentFor)
//                     {
//                         pSetParentFor->setParent(static_pointer_cast<Directory>(shared_from_this()));
//                         pNew = pSetParentFor;
//                     }
//
//                     if (pNew)
//                     {
//                         if (    (fDirsOnly)
//                              && (pNew->getResolvedType() == FSTypeResolved::SYMLINK_TO_DIRECTORY)
//                            )
//                         {
//                             Debug::Log(FILE_LOW, "discarding, not a directory");
//                             pNew.reset();
//                         }
//                     }
//                 }
//             }
//             closedir(pcdir);
//         }
//         else
//             throw FSException("failed to get contents of directory \"" + (string(pcszPath)) + "\": " + strerror(errno));
//
//         if (fDirsOnly)
//             _flFile |= FL_POPULATED_WITH_DIRECTORIES;
//         else
//             _flFile |= (FL_POPULATED_WITH_DIRECTORIES | FL_POPULATED_WITH_ALL);
    }

//     for (auto it : _mapContents)
//         // Leave out ".." in the list.
//         if (it.second != _pParent)
//             fslist.push_back(it.second);
//
    Debug::Leave();
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

/* virtual */
FSTypeResolved FSSymlink::getResolvedType() /* override */
{
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
