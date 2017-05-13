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

typedef Glib::RefPtr<Gio::File> PGioFile;

namespace Gdk { class Pixbuf; }
typedef Glib::RefPtr<Gdk::Pixbuf> PPixbuf;

class FSContainer;
class ContentsLock;


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
    virtual void onItemAdded(PFSModelBase &pFS) = 0;
    virtual void onItemRemoved(PFSModelBase &pFS) = 0;
    virtual void onItemRenamed(PFSModelBase &pFS, const std::string &strOldName, const std::string &strNewName) = 0;

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


/***************************************************************************
 *
 *  FSModelBase
 *
 **************************************************************************/

enum class FSFlag : uint16_t
{
    POPULATED_WITH_DIRECTORIES =  (1 <<  0),        // only for dirs
    POPULATED_WITH_ALL         =  (1 <<  1),        // only for dirs
    POPULATING                 =  (1 <<  2),        // only for dirs
    IS_ROOT_DIRECTORY          =  (1 <<  3),        // only for dirs; strParticle is ""
    IS_CURRENT_DIRECTORY       =  (1 <<  4),        // only for dirs; strParticle is "."
    DIRTY                      =  (1 <<  5),        // only used during populate
    HIDDEN_CHECKED             =  (1 <<  6),
    HIDDEN                     =  (1 <<  7),
    THUMBNAILING               =  (1 <<  8),
};
// DEFINE_FLAGSET(FSFlag)

typedef FlagSet<FSFlag> FSFlagSet;

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

    PGioFile getGioFile()
    {
        return _pGioFile;
    }

    std::string getURI() const
    {
        return _pGioFile->get_uri();
    }

    const std::string& getBasename() const
    {
        return _strBasename;
    }

    uint64_t getFileSize() const
    {
        return _cbSize;
    }

    Glib::ustring getIcon() const;

    FSType getType() const
    {
        return _type;
    }

    virtual FSTypeResolved getResolvedType() = 0;

    bool hasFlag(FSFlag f) const
    {
        return _fl.test(f);
    }

    void setFlag(FSFlag f)
    {
        _fl.set(f);
    }

    void clearFlag(FSFlag f)
    {
        _fl.clear(f);
    }

    PFSFile getFile();

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

    std::string getPath() const;

    PFSModelBase getParent() const;

    bool isUnder(PFSDirectory pDir) const;

    const std::string& describeType() const;
    std::string describe(bool fLong = false) const;

    /*
     *  Public operation methods
     */

    void rename(const std::string &strNewName);

    void sendToTrash();

    void testFileOps();

protected:
    /*
     *  Protected methods
     */

    friend class FSContainer;

    static PFSModelBase MakeAwake(PGioFile pGioFile);
    FSModelBase(FSType type,
                PGioFile pGioFile,
                uint64_t cbSize);
    virtual ~FSModelBase() { };

    PFSModelBase getSharedFromThis()
    {
        return shared_from_this();
    }

    uint64_t                    _uID = 0;
    FSType                      _type = FSType::UNINITIALIZED;
    FSFlagSet                   _fl;
    PGioFile                    _pGioFile;
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

public:
    virtual FSTypeResolved getResolvedType() override
    {
        return FSTypeResolved::FILE;
    }

    void setThumbnail(uint32_t thumbsize, PPixbuf ppb);
    PPixbuf getThumbnail(uint32_t thumbsize) const;

protected:
    friend class FSContainer;

    static PFSFile Create(PGioFile pGioFile,
                          uint64_t cbSize);
    FSFile(PGioFile pGioFile,
           uint64_t cbSize);
    virtual ~FSFile();

    struct ThumbData;
    ThumbData   *_pThumbData = nullptr;
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

    PFSModelBase find(const std::string &strParticle);

    bool isPopulatedWithDirectories() const;

    bool isCompletelyPopulated() const;

    void unsetPopulated();

    size_t getContents(FSList &llFiles,
                       Get getContents,
                       FSList *pllFilesRemoved,
                       StopFlag *pStopFlag);

    PFSDirectory createSubdirectory(const std::string &strName);
    PFSFile createEmptyDocument(const std::string &strName);

    void notifyFileAdded(PFSModelBase pFS) const;
    void notifyFileRemoved(PFSModelBase pFS) const;
    void notifyFileRenamed(PFSModelBase pFS, const std::string &strOldName, const std::string &strNewName) const;

protected:
    friend class FSModelBase;
    friend class FSMonitorBase;
    friend class ContentsLock;

    FSContainer(FSModelBase &refBase);
    virtual ~FSContainer();

    PFSDirectory resolveDirectory();

    void addChild(ContentsLock &lock, PFSModelBase p);
    void removeChild(ContentsLock &lock, PFSModelBase p);
    PFSModelBase isAwake(ContentsLock &lock, const std::string &strParticle) const;

    struct Impl;
    Impl                *_pImpl;

    FSModelBase         &_refBase;
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
public:
    virtual FSTypeResolved getResolvedType() override
    {
        return FSTypeResolved::DIRECTORY;
    }

    static PFSDirectory GetHome();
    static PFSDirectory GetRoot();

protected:
    friend class FSModelBase;
    friend class FSContainer;

    FSDirectory(PGioFile pGioFile);
    static PFSDirectory Create(PGioFile pGioFile);
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
    FSSymlink(PGioFile pGioFile);
    static PFSSymlink Create(PGioFile pGioFile);

    enum class State
    {
        NOT_FOLLOWED_YET = 0,
        RESOLVING = 1,
        BROKEN = 2,
        TO_FILE = 3,
        TO_DIRECTORY = 4
    };
    State           _state;
    PFSModelBase    _pTarget;

private:
    State follow();
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
    FSSpecial(PGioFile pGioFile);
    static PFSSpecial Create(PGioFile pGioFile);

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
    FSMountable(PGioFile pGioFile);
    static PFSMountable Create(PGioFile pGioFile);

    virtual FSTypeResolved getResolvedType() override
    {
        return FSTypeResolved::MOUNTABLE;
    }
};

#endif // ELISSO_FSMODEL_H
