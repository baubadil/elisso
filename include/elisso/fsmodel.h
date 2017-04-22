/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_FSMODEL_H
#define ELISSO_FSMODEL_H

#include <memory>
#include <type_traits>

#include "glibmm.h"
#include "giomm.h"

#include "xwp/lock.h"
#include "xwp/flagset.h"

#define FS_BUF_LEN 1024

class FSLock;

enum class FSType
{
    UNINITIALIZED,
    FILE,
    DIRECTORY,
    SYMLINK,
    SPECIAL,
    MOUNTABLE
};

enum class FSTypeResolved
{
    FILE,
    DIRECTORY,
    SYMLINK_TO_FILE,
    SYMLINK_TO_DIRECTORY,
    BROKEN_SYMLINK,
    SPECIAL,
    MOUNTABLE
};

class FSModelBase;
typedef std::shared_ptr<FSModelBase> PFSModelBase;

class FSFile;
typedef std::shared_ptr<FSFile> PFSFile;

class FSDirectory;
typedef std::shared_ptr<FSDirectory> PFSDirectory;

class FSSymlink;
typedef std::shared_ptr<FSSymlink> PFSSymlink;

class FSSpecial;
typedef std::shared_ptr<FSSpecial> PFSSpecial;

class FSMountable;
typedef std::shared_ptr<FSMountable> PFSMountable;

typedef std::list<PFSModelBase> FSList;
typedef std::shared_ptr<FSList> PFSList;

class FSContainer;
struct ContentsMap;


/***************************************************************************
 *
 *  FSMonitorBase
 *
 **************************************************************************/

/**
 *  Monitor interface to allow clients to be notified when the contents of
 *  a directory change. This happens both on programmatic changes (e.g.
 *  createSubdirectory()) as well as background changes from file system
 *  watches (TODO).
 *
 *  To use:
 *
 *   1) Derive your own subclass of this and implement the required pure
 *      virtual methods.
 *
 *   2) Create a shared_ptr of this and store it in your GUI instance data.
 *
 *   3) Call startWatching() with a container. This stores the instance in
 *      the container (and thus increases the refcount of the shared_ptr).
 *      From now on the container will call the monitor methods when folder
 *      contents change.
 *
 *   4) When you're done monitoring, call stopWatching(). You can keep the
 *      instance around for another container.
 *
 *  There is an 1:N relation between containers and monitors: a monitor
 *  can watch at most one container, but a folder (container) can have
 *  multiple monitors.
 */
class FSMonitorBase : public ProhibitCopy, public enable_shared_from_this<FSMonitorBase>
{
    friend class FSContainer;

public:
    virtual void onItemRemoved(PFSModelBase &pFS) = 0;
    virtual void onDirectoryAdded(PFSDirectory &pDir) = 0;

    FSContainer* isWatching()
    {
        return _pContainer;
    }

    void startWatching(FSContainer &cnr);
    void stopWatching(FSContainer &cnr);

protected:
    virtual ~FSMonitorBase() {}

private:
    FSContainer *_pContainer = nullptr;
};

typedef std::shared_ptr<FSMonitorBase> PFSMonitorBase;
typedef std::list<PFSMonitorBase> FSMonitorsList;


/***************************************************************************
 *
 *  FSModelBase
 *
 **************************************************************************/

enum class FSFlags : uint8_t
{
    POPULATED_WITH_DIRECTORIES =  (1 <<  0),        // only for dirs
    POPULATED_WITH_ALL         =  (1 <<  1),        // only for dirs
    IS_ROOT_DIRECTORY          =  (1 <<  2),        // only for dirs; strParticle is ""
    IS_CURRENT_DIRECTORY       =  (1 <<  3),        // only for dirs; strParticle is "."
    DIRTY                      =  (1 <<  4),        // only used during populate
    HIDDEN_CHECKED             =  (1 <<  5),
    HIDDEN                     =  (1 <<  6),
};

typedef FlagSet<FSFlags> FSFlagSet;

/**
 *  Base class for all file-system objects (files, directories, symlinks, specials).
 *
 *  There are several main entry points to get objects for files and directories:
 *
 *   -- Most obviously, FSModelBase::FindPath() and FSModelBase::FindDirectory().
 *      They will do blocking I/O and build a hierarchy of objects for the given
 *      path (including all parent directories to the root).
 *
 *   -- To get objects for the contents of a folder (directory), use
 *      FSDirectory::getContents() and FSDirectory::find().
 *
 *  This code also handles symlinks and can treat symlinks to directories like
 *  directories; see FSSymlink for details.
 */
class FSModelBase : public std::enable_shared_from_this<FSModelBase>
{
public:
    static PFSModelBase FindPath(const std::string &strPath);
    static PFSDirectory FindDirectory(const std::string &strPath);

    /*
     *  Public Information methods
     */

    Glib::RefPtr<Gio::File> getGioFile()
    {
        return _pGioFile;
    }

    const std::string& getBasename()
    {
        return _strBasename;
    }

    uint64_t getFileSize()
    {
        return _cbSize;
    }

    Glib::ustring getIcon();

    FSType getType() const
    {
        return _type;
    }

    virtual FSTypeResolved getResolvedType() = 0;

    /**
     *  Returns true if this is a directory or a symlink to one. Can cause I/O if this
     *  is a symlink as this calls the virtual getResolvedType() method.
     */
    bool isDirectoryOrSymlinkToDirectory()
    {
        return (_type == FSType::DIRECTORY) || (getResolvedType() == FSTypeResolved::SYMLINK_TO_DIRECTORY);
    }

    FSContainer* getContainer();

    bool isHidden();

    std::string getRelativePath();
    std::string getAbsolutePath(bool fThrow = false);
    std::string getFormattedPath();

    PFSModelBase getParent();

    bool isUnder(PFSDirectory pDir);

    const std::string& describeType();
    std::string describe(bool fLong = false);

    /*
     *  Public operation methods
     */

    void sendToTrash();
    void testFileOps();

    void notifyFileRemoved();

protected:
    /*
     *  Protected methods
     */

    friend class FSContainer;

    static PFSModelBase MakeAwake(Glib::RefPtr<Gio::File> pGioFile);
    FSModelBase(FSType type,
                Glib::RefPtr<Gio::File> pGioFile,
                uint64_t cbSize);
    virtual ~FSModelBase() { };

    ContentsMap& getContentsMap();
    void setParent(PFSModelBase pNewParent);

    PFSModelBase getSharedFromThis()
    {
        return shared_from_this();
    }

    uint64_t                    _uID = 0;
    FSType                      _type = FSType::UNINITIALIZED;
    FSFlagSet                   _fl;
    Glib::RefPtr<Gio::File>     _pGioFile;
    std::string                 _strBasename;
    uint64_t                    _cbSize;
    Glib::RefPtr<Gio::Icon>     _pIcon;
    PFSModelBase                _pParent;
};


/***************************************************************************
 *
 *  FSFile
 *
 **************************************************************************/

/**
 *  FSModelBase subclass for ordinary files. Everything that is not a directory
 *  and not a symlink is instantiated as an instance of this.
 */
class FSFile : public FSModelBase
{
    friend class FSModelBase;

    virtual FSTypeResolved getResolvedType() override
    {
        return FSTypeResolved::FILE;
    }

protected:
    FSFile(Glib::RefPtr<Gio::File> pGioFile,
           uint64_t cbSize);
    static PFSFile Create(Glib::RefPtr<Gio::File> pGioFile,
                          uint64_t cbSize);
};


/***************************************************************************
 *
 *  FSContainer
 *
 **************************************************************************/

/**
 *  Helper class that implements directory contents. This is inherited via
 *  multiple inheritance by both FSContainer and FSSymlink and contains
 *  the public methods that both these classes should have to populate
 *  their contents.
 *
 *  This allows you to call methods like getContents() and find() on both
 *  directories and symlinks to directories while preserving path information,
 *  but without losing type safety.
 *
 *  As an example, if you have /dir/symlink/subdir but "symlink" is really
 *  a symlink pointing to /otherdir, FSModelBase::FindPath() will still
 *  build a path for /dir/symlink/subdir and the "symlink" particle will
 *  be an instance of FSSymlink with its own contents. However, you can
 *  call getTarget() on the symlink and receive a PFSDirectory for /otherdir.
 */
class FSContainer : public ProhibitCopy
{
public:
    enum class Get
    {
        ALL,
        FOLDERS_ONLY,
        FIRST_FOLDER_ONLY
    };

    PFSModelBase isAwake(const std::string &strParticle);

    PFSModelBase find(const std::string &strParticle);

    bool isPopulatedWithDirectories();

    bool isCompletelyPopulated();

    void unsetPopulated();

    size_t getContents(FSList &llFiles,
                       Get getContents,
                       StopFlag *pStopFlag);

    PFSDirectory createSubdirectory(const std::string &strName);

protected:
    friend class FSModelBase;
    friend class FSMonitorBase;

    FSContainer(FSModelBase &refBase);
    virtual ~FSContainer();

    ContentsMap         *_pMap;

    FSModelBase         &_refBase;
    FSMonitorsList      _llMonitors;
};

/***************************************************************************
 *
 *  FSDirectory
 *
 **************************************************************************/

/**
 *  FSModelBase subclass for physical directories. This multiply-inherits from FSContainer
 *  so the content methods work.
 *
 *  Note that symlinks to directories are instantiated as FSSymlink instances instead,
 *  like all symlinks.
 */
class FSDirectory : public FSModelBase, public FSContainer
{
    friend class FSModelBase;
    friend class FSContainer;

public:
    virtual FSTypeResolved getResolvedType() override
    {
        return FSTypeResolved::DIRECTORY;
    }

    static PFSDirectory GetHome();
    static PFSDirectory GetRoot();

protected:
    FSDirectory(Glib::RefPtr<Gio::File> pGioFile);
    static PFSDirectory Create(Glib::RefPtr<Gio::File> pGioFile);
};

class RootDirectory;
typedef std::shared_ptr<RootDirectory> PRootDirectory;

/**
 *  Specialization of the root directory, "/".
 */
class RootDirectory : public FSDirectory
{
    friend class FSModelBase;
    friend class FSDirectory;

private:
    RootDirectory();

    static PRootDirectory s_theRoot;

    static PRootDirectory GetImpl();
};

class CurrentDirectory;
typedef std::shared_ptr<CurrentDirectory> PCurrentDirectory;

/**
 *  Specialization of the current user's home directory.
 */
class CurrentDirectory : public FSDirectory
{
    friend class FSModelBase;

private:
    CurrentDirectory();

    static PCurrentDirectory s_theCWD;

    static PCurrentDirectory GetImpl();
};


/***************************************************************************
 *
 *  FSSymlink
 *
 **************************************************************************/

/**
 *  FSModelBase subclass for symbolic links. All symbolic links are instantiated
 *  as instances of this, even if the symlink's target is a directory.
 *
 *  To speed up things, we only read the symlink target lazily, on request.
 *  If you do getContents() and receive a list of symlinks, the symlink is not
 *  read until you call either getTarget() or getResolvedType(). Those two
 *  call the private follow() method, which does read the link and instantiates
 *  the target.
 *
 *  If the symlink is broken, then getTarget() will return nullptr, and
 *  getResolvedType() will return BROKEN_SYMLINK.
 *
 *  Otherwise getResolvedType() will return SYMLINK_TO_FILE or SYMLINK_TO_DIRECTORY.
 *
 *  Only if getResolvedType() returns SYMLINK_TO_DIRECTORY, you can follow up
 *  in two ways:
 *
 *   -- You can call getTarget(), which returns the target, which will really be
 *      an FSDirectory for the target. You can then populate that directory,
 *      but the path will be different (the symlink will have been followed).
 *
 *  --  You can call the FSContainer content methods directly on the symlink,
 *      since FSSymlink multiply-inherits from both FSModelBase and FSContainer.
 *      In that case the symlink behaves exactly like a directory, and the
 *      symlink is not followed.
 */
class FSSymlink : public FSModelBase, public FSContainer
{
    friend class FSModelBase;

public:
    virtual FSTypeResolved getResolvedType() override;

    PFSModelBase getTarget();

protected:
    FSSymlink(Glib::RefPtr<Gio::File> pGioFile);
    static PFSSymlink Create(Glib::RefPtr<Gio::File> pGioFile);

    enum class State
    {
        NOT_FOLLOWED_YET = 0,
        BROKEN = 1,
        TO_FILE = 2,
        TO_DIRECTORY = 3
    };
    State               _state;
    PFSModelBase        _pTarget;

private:
    void follow(FSLock &lock);
};


/***************************************************************************
 *
 *  FSSpecial
 *
 **************************************************************************/

class FSSpecial : public FSModelBase
{
    friend class FSModelBase;

protected:
    FSSpecial(Glib::RefPtr<Gio::File> pGioFile);
    static PFSSpecial Create(Glib::RefPtr<Gio::File> pGioFile);

    virtual FSTypeResolved getResolvedType() override
    {
        return FSTypeResolved::SPECIAL;
    }
};


/***************************************************************************
 *
 *  FSMountable
 *
 **************************************************************************/

class FSMountable : public FSModelBase
{
    friend class FSModelBase;

protected:
    FSMountable(Glib::RefPtr<Gio::File> pGioFile);
    static PFSMountable Create(Glib::RefPtr<Gio::File> pGioFile);

    virtual FSTypeResolved getResolvedType() override
    {
        return FSTypeResolved::MOUNTABLE;
    }
};

#endif // ELISSO_FSMODEL_H
