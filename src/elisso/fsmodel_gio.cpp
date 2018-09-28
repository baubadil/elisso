/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */


#include "elisso/fsmodel_gio.h"

#include "elisso/thumbnailer.h"

#include "xwp/debug.h"
// #include "xwp/stringhelp.h"
#include "xwp/regex.h"
#include "xwp/except.h"

FsGioImpl *g_pFsGioImpl = nullptr;


/***************************************************************************
 *
 *  FsGioImpl
 *
 **************************************************************************/

/* virtual */
PFsObject
FsGioImpl::findPath(const string &strPath0) /* override */
{
    Debug d(FILE_LOW, __func__ + string("(" + quote(strPath0) + ")"));

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
    PFsObject pCurrent;
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
    // and call into FsContainer::find(), which has proper locking.

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
                pCurrent = FsDirectory::GetCwdOrThrow();
                break;
            }
        }
        else
        {
            FsContainer *pDir = nullptr;
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
                    pDir = FsDirectory::GetCwdOrThrow()->getContainer();
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
                    pCurrent = pCurrent->getParent();

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

/* virtual */
PFsObject
FsGioImpl::makeAwake(const string &strParentPath,
                     const string &strBasename,
                     bool fIsLocal) /* override */
{
    PFsObject pReturn;

    string strFullPath2 = strParentPath + "/" + strBasename;
    Debug d(FILE_LOW, "FsGioImpl::makeAwake(" + quote(strFullPath2) + ")");

    PGioFile pGioFile;
    if (fIsLocal)
        pGioFile = Gio::File::create_for_path(strFullPath2.substr(7));
    else
        pGioFile = Gio::File::create_for_uri(strFullPath2);

    static string star("*");

    try
    {
        auto pInfo = this->getFileInfo(pGioFile);

        switch (pInfo->get_file_type())
        {
            case Gio::FileType::FILE_TYPE_REGULAR:         // File handle represents a regular file.
            {
                FsCoreInfo info(pInfo->get_size(),
                                pInfo->get_attribute_string(G_FILE_ATTRIBUTE_OWNER_USER),
                                pInfo->get_attribute_string(G_FILE_ATTRIBUTE_OWNER_GROUP));
                pReturn = FsGioFile::Create(strBasename, info);
            }
            break;

            case Gio::FileType::FILE_TYPE_DIRECTORY:       // File handle represents a directory.
            {
                FsCoreInfo info(0,
                                pInfo->get_attribute_string(G_FILE_ATTRIBUTE_OWNER_USER),
                                pInfo->get_attribute_string(G_FILE_ATTRIBUTE_OWNER_GROUP));
                Debug::Log(FILE_LOW, "  creating FsGioDirectory for " + quote(strBasename));
                pReturn = FsGioDirectory::Create(strBasename, info);
            }
            break;

            case Gio::FileType::FILE_TYPE_SYMBOLIC_LINK:   // File handle represents a symbolic link (Unix systems).
            case Gio::FileType::FILE_TYPE_SHORTCUT:        // File is a shortcut (Windows systems).
                pReturn = FsSymlink::Create(strBasename);
            break;

            case Gio::FileType::FILE_TYPE_SPECIAL:         // File is a "special" file, such as a socket, fifo, block device, or character device.
                pReturn = FsGioSpecial::Create(strBasename);
            break;

            case Gio::FileType::FILE_TYPE_MOUNTABLE:       // File is a mountable location.
                Debug::Log(MOUNTS, "  creating FsGioMountable");
//                 pReturn = FsGioMountable::Create(strBasename);
            break;

            case Gio::FileType::FILE_TYPE_NOT_KNOWN:       // File's type is unknown. This is what we get if the file does not exist.
                Debug::Log(FILE_HIGH, "file type not known");
            break;      // return nullptr
        }
    }
    catch (Gio::Error &e)
    {
        Debug::Log(CMD_TOP, "FsGioImpl::makeAwake(): got Gio::Error: " + e.what());
        throw FSException(e.what());
    }

    if (!pReturn)
        throw FSException("Unknown error waking up file-system object");

    return pReturn;
}

class FsDirEnumeratorGio : public FsDirEnumeratorBase
{
public:
    Glib::RefPtr<Gio::FileEnumerator> en;
};

/* virtual */
PFsDirEnumeratorBase
FsGioImpl::beginEnumerateChildren(FsContainer &cnr)
{
    shared_ptr<FsDirEnumeratorGio> pEnum;

    try
    {
        auto pgioContainer = g_pFsGioImpl->getGioFile(cnr._refBase);

        pEnum = make_shared<FsDirEnumeratorGio>();

        if (!(pEnum->en = pgioContainer->enumerate_children("*",
                                                            Gio::FileQueryInfoFlags::FILE_QUERY_INFO_NOFOLLOW_SYMLINKS)))
            throw FSException("Error populating!");
    }
    catch (Gio::Error &e)
    {
        throw FSException(e.what());
    }

    return pEnum;
}

/* virtual */
bool
FsGioImpl::getNextChild(PFsDirEnumeratorBase pEnum, string &strBasename) /* override */
{
    FsDirEnumeratorGio *pEnum2 = static_cast<FsDirEnumeratorGio*>(&*pEnum);
    try
    {
        Glib::RefPtr<Gio::FileInfo> pInfo;

        while ((pInfo = pEnum2->en->next_file()))
        {
            auto pGioFileThis = pEnum2->en->get_child(pInfo);

            strBasename = pGioFileThis->get_basename();
            if (    (strBasename != ".")
                 && (strBasename != "..")
               )
                return true;
        }
    }
    catch (Gio::Error &e)
    {
        throw FSException(e.what());
    }

    return false;
}

/* virtual */
string FsGioImpl::getSymlinkContents(FsSymlink &ln) /* override */
{
    try
    {
        auto pGioFile = this->getGioFile(ln);
        Glib::RefPtr<Gio::FileInfo> pInfo = pGioFile->query_info(G_FILE_ATTRIBUTE_STANDARD_SYMLINK_TARGET,
                                                                 Gio::FileQueryInfoFlags::FILE_QUERY_INFO_NOFOLLOW_SYMLINKS);
        return pInfo->get_symlink_target();
    }
    catch (Gio::Error &e)
    {
        throw FSException(e.what());
    }
}

/* virtual */
void
FsGioImpl::rename(FsObject &fs,
                  const string &strNewName) /* override */
{
    try
    {
        auto pGioFile = getGioFile(fs);
        pGioFile->set_display_name(strNewName);
    }
    catch (Gio::Error &e)
    {
        throw FSException(e.what());
    }
}

/* virtual */
void
FsGioImpl::trash(FsObject &fs) /* override */
{
    try
    {
        auto pGioFile = getGioFile(fs);
        if (!pGioFile)
            throw FSException("cannot get GIO file");

        pGioFile->trash();
    }
    catch (Gio::Error &e)
    {
        throw FSException(e.what());
    }
}

/* virtual */
void
FsGioImpl::copy(Debug &d,
                FsObject &fs,
                const string &strTargetPath) /* override */
{
    try
    {
        auto pGioFile = getGioFile(fs);
        auto pTargetGioFile = Gio::File::create_for_uri(strTargetPath);

        Debug::Log(FILE_HIGH, "pGioFile->copy(" + quote(pGioFile->get_path()) + " to " + quote(pTargetGioFile->get_path()) + ")");

        pGioFile->copy(pTargetGioFile,
                       Gio::FileCopyFlags::FILE_COPY_NOFOLLOW_SYMLINKS      // copy symlinks as symlinks
            /*| Gio::FileCopyFlags::FILE_COPY_NO_FALLBACK_FOR_MOVE */);
    }
    catch (Gio::Error &e)
    {
        d.setExit("Caught Gio::Error: " + e.what());
        throw FSException(e.what());
    }
}

/* virtual */
void
FsGioImpl::move(Debug &d,
                FsObject &fs,
                const string &strTargetPath) /* override */
{
    try
    {
        auto pGioFile = getGioFile(fs);
        auto pTargetGioFile = Gio::File::create_for_uri(strTargetPath);

        Debug::Log(FILE_HIGH, "pGioFile->move(" + quote(pGioFile->get_path()) + " to " + quote(pTargetGioFile->get_path()) + ")");
        pGioFile->move(pTargetGioFile,
                       Gio::FileCopyFlags::FILE_COPY_NOFOLLOW_SYMLINKS      // copy symlinks as symlinks
                        /*| Gio::FileCopyFlags::FILE_COPY_NO_FALLBACK_FOR_MOVE */);
    }
    catch (Gio::Error &e)
    {
        d.setExit("Caught Gio::Error: " + e.what());
        throw FSException(e.what());
    }
}

/* virtual */
PFsDirectory
FsGioImpl::createSubdirectory(const string &strParentPath,
                              const string &strBasename) /* override */
{
    PFsDirectory pReturn;

    try
    {
        // To create a new subdirectory via Gio::File, create an empty Gio::File first
        // and then invoke make_directory on it.
        string strPath = strParentPath + "/" + strBasename;
        Debug::Log(FILE_HIGH, string(__func__) + ": creating directory \"" + strPath + "\"");

        // The follwing cannot fail.
        Glib::RefPtr<Gio::File> pGioFileNew = Gio::File::create_for_uri(strPath);
        // But the following can throw.
        pGioFileNew->make_directory();
        // If we got here, we have a directory.
        auto pGioInfo = this->getFileInfo(pGioFileNew);
        FsCoreInfo info(0,
                        pGioInfo->get_attribute_string(G_FILE_ATTRIBUTE_OWNER_USER),
                        pGioInfo->get_attribute_string(G_FILE_ATTRIBUTE_OWNER_GROUP));
        pReturn = FsGioDirectory::Create(strBasename, info);
    }
    catch (Gio::Error &e)
    {
        throw FSException(e.what());
    }

    return pReturn;
}

/* virtual */
PFSFile
FsGioImpl::createEmptyDocument(const string &strParentPath,
                               const string &strBasename) /* override */
{
    PFSFile pReturn;
    try
    {
        // To create a new subdirectory via Gio::File, create an empty Gio::File first
        // and then invoke make_directory on it.
        string strPath = strParentPath + "/" + strBasename;
        Debug::Log(FILE_HIGH, string(__func__) + ": creating directory \"" + strPath + "\"");

        // The follwing cannot fail.
        Glib::RefPtr<Gio::File> pGioFileNew = Gio::File::create_for_uri(strPath);
        // But the following can throw.
        auto pStream = pGioFileNew->create_file();
        pStream->close();

        // If we got here, we have a directory.
        auto pGioInfo = this->getFileInfo(pGioFileNew);
        FsCoreInfo info(0,
                        pGioInfo->get_attribute_string(G_FILE_ATTRIBUTE_OWNER_USER),
                        pGioInfo->get_attribute_string(G_FILE_ATTRIBUTE_OWNER_GROUP));
        pReturn = FsGioFile::Create(strBasename, info);
    }
    catch (Gio::Error &e)
    {
        throw FSException(e.what());
    }

    return pReturn;
}


PGioFile
FsGioImpl::getGioFile(FsObject &fs)
{
    auto strPath = fs.getPath();

    if (fs.hasFlag(FSFlag::IS_LOCAL))
        return Gio::File::create_for_path(strPath.substr(7));

    return Gio::File::create_for_uri(strPath);
}

PFsGioFile
FsGioImpl::getFile(PFsObject pFS, FSTypeResolved t)
{
    if (pFS)
    {
        if (t == FSTypeResolved::FILE)
            return static_pointer_cast<FsGioFile>(pFS);
        if (t == FSTypeResolved::SYMLINK_TO_FILE)
            return static_pointer_cast<FsGioFile>((static_cast<FsSymlink*>(&*pFS))->getTarget());
    }

    return nullptr;
}

Glib::RefPtr<Gio::FileInfo>
FsGioImpl::getFileInfo(PGioFile pGioFile)
{
    static string s_comma = string(",");
    static string s_attrs = string(G_FILE_ATTRIBUTE_STANDARD_TYPE)
                          + s_comma + string(G_FILE_ATTRIBUTE_STANDARD_SIZE)
                          + s_comma + string(G_FILE_ATTRIBUTE_OWNER_USER)
                          + s_comma + string(G_FILE_ATTRIBUTE_OWNER_GROUP);
    // The following can throw Gio::Error.
    auto pInfo = pGioFile->query_info(s_attrs,
                                      Gio::FileQueryInfoFlags::FILE_QUERY_INFO_NOFOLLOW_SYMLINKS);
    return pInfo;
}

/* static */
void
FsGioImpl::Init()
{
    g_pFsGioImpl = new FsGioImpl();
}


/***************************************************************************
 *
 *  FsGioFile
 *
 **************************************************************************/

uint64_t g_cbTotalPixbufs = 0;
Mutex    g_mutexPixbufsSize;

struct PixbufWithStats
{
public:
    PixbufWithStats(PPixbuf p)
        : _pPixbuf(p)
    {
        Lock l(g_mutexPixbufsSize);
        g_cbTotalPixbufs += _pPixbuf->get_byte_length();
    }

    ~PixbufWithStats()
    {
        Lock l(g_mutexPixbufsSize);
        g_cbTotalPixbufs -= _pPixbuf->get_byte_length();
    }

    PPixbuf getPixbuf()
    {
        return _pPixbuf;
    }

private:
    PPixbuf _pPixbuf;
};

typedef shared_ptr<PixbufWithStats> PPixbufWithStats;

struct FsGioFile::ThumbData
{
    map<uint32_t, PPixbufWithStats> mapThumbnails;
};

/* static */
PFsGioFile
FsGioFile::Create(const string &strBasename, const FsCoreInfo &info)
{
    /* This nasty trickery is necessary to make make_shared work with a protected constructor. */
    class Derived : public FsGioFile
    {
    public:
        Derived(const string &strBasename, const FsCoreInfo &info) : FsGioFile(strBasename, info) { }
    };

    return make_shared<Derived>(strBasename, info);
}

/* virtual */
FsGioFile::~FsGioFile()
{
    FsLock lock;
    if (_pThumbData)
        delete _pThumbData;
    if (_psvIcons)
        delete _psvIcons;
}


void
FsGioFile::setThumbnail(uint32_t thumbsize, PPixbuf ppb)
{
    FsLock lock;
    if (ppb)
    {
        if (!_pThumbData)
            _pThumbData = new ThumbData;

        _pThumbData->mapThumbnails[thumbsize] = make_shared<PixbufWithStats>(ppb);
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
FsGioFile::getThumbnail(uint32_t thumbsize) const
{
    PPixbuf ppb;

    FsLock lock;
    if (_pThumbData)
    {
        auto it = _pThumbData->mapThumbnails.find(thumbsize);
        if (it != _pThumbData->mapThumbnails.end())
            ppb = it->second->getPixbuf();
    }

    return ppb;
}

/* static */
uint64_t
FsGioFile::GetThumbnailCacheSize()
{
    Lock l(g_mutexPixbufsSize);
    return g_cbTotalPixbufs;
}


/***************************************************************************
 *
 *  FsGioDirectory
 *
 **************************************************************************/

/* static */
PFsGioDirectory
FsGioDirectory::Create(const string &strBasename, const FsCoreInfo &info)
{
    /* This nasty trickery is necessary to make make_shared work with a protected constructor. */
    class Derived : public FsGioDirectory
    {
    public:
        Derived(const string &strBasename, const FsCoreInfo &info) : FsGioDirectory(strBasename, info) { }
    };

    return make_shared<Derived>(strBasename, info);
}


/***************************************************************************
 *
 *  RootDirectory
 *
 **************************************************************************/

RootDirectory::RootDirectory(const string &strScheme, const FsCoreInfo &info)
    : FsDirectory(FSType::DIRECTORY,
                  strScheme + "://",
                  info),
      _strScheme(strScheme)
{
    _fl = FSFlag::IS_ROOT_DIRECTORY;
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
            Derived(const string &strScheme, const FsCoreInfo &info) : RootDirectory(strScheme, info) { }
        };

        FsCoreInfo info(0, "", "");
        pReturn = make_shared<Derived>(strScheme, info);
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


/***************************************************************************
 *
 *  FsGioSpecial
 *
 **************************************************************************/

/* static */
PFsGioSpecial
FsGioSpecial::Create(const string &strBasename)
{
    /* This nasty trickery is necessary to make make_shared work with a protected constructor. */
    class Derived : public FsGioSpecial
    {
    public:
        Derived(const string &strBasename) : FsGioSpecial(strBasename) { }
    };

    return make_shared<Derived>(strBasename);
}


/***************************************************************************
 *
 *  FSMountable
 *
 **************************************************************************/

/* static */
void
FsGioMountable::GetMountables(FsGioMountablesVector &llMountables)
{
    Debug d(MOUNTS, "FsGioMountable::GetMountables()");
    Glib::RefPtr<Gio::VolumeMonitor> pVolm = Gio::VolumeMonitor::get();
    if (pVolm)
    {
        /* The GIO docs say GDrive represent a piece of hardware connected to the machine. It's "generally" only created
         * for removable  hardware or hardware with removable media. GDrive is a container class for GVolume objects that
         * stem from the same piece of media. As such, GDrive abstracts a drive with (or without) removable media and
         * provides operations for querying whether media is available, determining whether media change is automatically
         * detected and ejecting the media.
         *
         * I get one for each of the physical drives in my system (an SSD containing several partitions, plus the 2TB
         * spinning hard disk), plus one for the CD/DVD writer, plus one each for the five SD card readers.
         */
        d.Log(MOUNTS, "Getting drives");
        std::list<Glib::RefPtr<Gio::Drive>> llDrives = pVolm->get_connected_drives();
        for (auto pDrive : llDrives)
        {
            Debug::Log(MOUNTS, "Drive: " + quote(pDrive->get_name()) + ", has volumes: " + string(pDrive->has_volumes() ? "yes" : "no"));

            for (auto strKind : pDrive->enumerate_identifiers())
            {
                string strId = pDrive->get_identifier(strKind);
                Debug::Log(MOUNTS, "  Identifier " + quote(strKind) + ": " + quote(strId));
            }
        }

        /* "Get volumes" returns a strange list of what GIO considers a volume.
         * From my testing:
         *
         *    Name            Type                             Returned by get_volumes()     Drive
         *     /              Root file system                 No
         *     /dev/sda3      My unmounted Windows NTFS C:     Yes                           Points to the SSD
         *     name@sld.tld   My OwnCloud "online account"     Yes                           No
         *     Windows shares                                  No, not even when mounted in Nautilus
         */
        d.Log(MOUNTS, "Getting volumes");
        std::list<Glib::RefPtr<Gio::Volume>> llVolumes = pVolm->get_volumes();
        for (auto pVolume : llVolumes)
        {
            Glib::ustring strDrive;
            Glib::RefPtr<Gio::Drive> pDrive = pVolume->get_drive();
            if (pDrive)
                strDrive = pDrive->get_name();

            Debug::Log(MOUNTS, "Volume: " + pVolume->get_name() + ", drive name: " + quote(strDrive));

            // This returns nullptr if the volume is not mounted.
            auto pMount = pVolume->get_mount();
            if (pMount)
            {
                string strMountedAt;
                auto pGioFile = pMount->get_root();
                if (pGioFile)
                {
                    strMountedAt = pGioFile->get_path();
                    auto pDir = FsObject::FindDirectory(strMountedAt);
                    if (pDir)
                    {
                        llMountables.push_back(Create(pMount->get_name(),
                                                      static_pointer_cast<FsGioDirectory>(pDir)));
                        Debug::Log(MOUNTS, "  Mount: " + quote(pMount->get_name()) + " mounted at: " + quote(strMountedAt));
                    }
                }
            }
        }
    }
}

/* static */
PFsGioMountable
FsGioMountable::Create(const string &strName,
                       PFsGioDirectory pRootDir)
{
    /* This nasty trickery is necessary to make make_shared work with a protected constructor. */
    class Derived : public FsGioMountable
    {
    public:
        Derived(const string &strName, PFsGioDirectory pRootDir) : FsGioMountable(strName, pRootDir) { }
    };

    return make_shared<Derived>(strName, pRootDir);
}


/***************************************************************************
 *
 *  FileContents
 *
 **************************************************************************/

FileContents::FileContents(FsGioFile &file)
    : _pData(nullptr), _size(0)
{
    try
    {
        auto pGioFile = g_pFsGioImpl->getGioFile(file);
        auto pStream = pGioFile->read();
        Glib::RefPtr<Gio::FileInfo> pInfo = pStream->query_info(G_FILE_ATTRIBUTE_STANDARD_SIZE);
        _size = pInfo->get_attribute_uint64(G_FILE_ATTRIBUTE_STANDARD_SIZE);

        if (!(_pData = (char*)malloc(_size)))
            throw FSException("Not enough memory");
        gsize zRead;
        pStream->read_all(_pData, _size, zRead);
        pStream->close();
    }
    catch (Gio::Error &e)
    {
        throw FSException(e.what());
    }
}

FileContents::~FileContents()
{
    if (_pData)
        free(_pData);
}

