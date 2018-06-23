/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef XWP_FSMODEL_BASE_H
#define XWP_FSMODEL_BASE_H

#include <memory>
#include <type_traits>

#include "xwp/thread.h"
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
    SYMLINK_TO_OTHER,
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

typedef std::vector<PFSModelBase> FSVector;
typedef std::shared_ptr<FSVector> PFSVector;

class FSContainer;
class ContentsLock;

namespace XWP { class Debug; }


/***************************************************************************
 *
 *  FSLock
 *
 **************************************************************************/

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
    FSLock();
};


/***************************************************************************
 *
 *  FsImplBase
 *
 **************************************************************************/

/**
 *  Empty class to be subclassed by client code that subclasses FsImplBase
 *  as well. An instance of this is returned by FsImplBase::beginEnumerateChildren().
 */
class FsDirEnumeratorBase
{
};

typedef std::shared_ptr<FsDirEnumeratorBase> PFsDirEnumeratorBase;

/**
 *  Abstract base class that the using code must implement to actually connect
 *  the logic of this to the disk. This way the library can be used for both
 *  Glib/Gio and standard C POSIX calls.
 *
 *  Client code must implement all the methods herein.
 */
class FsImplBase
{
protected:
    FsImplBase();

public:
    virtual PFSModelBase findPath(const string &strPath) = 0;

    /**
     *  Instantiates the given file system object from the given path and returns it,
     *  but does NOT add it to the parent container. This gets called if no object
     *  for this path has been instantiated yet.
     *
     *  This never returns NULL, but throws FSExceptions instead, even for file-system
     *  errors.
     */
    virtual PFSModelBase makeAwake(const string &strParentPath,
                                   const string &strBasename,
                                   bool fIsLocal) = 0;

    /**
     *  The equivalent of opendir(). This returns a shared pointer to a buffer with
     *  implementation-defined data. Keep calling getNextChild() until that returns
     *  false.
     */
    virtual PFsDirEnumeratorBase beginEnumerateChildren(FSContainer &cnr) = 0;

    /**
     *  To be used with the buffer returned by beginEnumerateChildren(). If this
     *  returns true, then strBasename has been set to another directory entry;
     *  otherwise no other items have been found. This never returns the "." or
     *  ".." entries so it may return false even on the first call if the directory
     *  is empty.
     */
    virtual bool getNextChild(PFsDirEnumeratorBase pEnum, string &strBasename) = 0;

    /**
     *  The equivalent of readlink(). Returns the unprocessed contents of the given
     *  symlink.
     */
    virtual string getSymlinkContents(FSSymlink &ln) = 0;

    /**
     *  This renames a file. Throws an FSException on errors.
     */
    virtual void rename(FSModelBase &fs, const string &strNewName) = 0;

    /**
     *  This sends a file to the trash can. Throws an FSException on errors.
     */
    virtual void trash(FSModelBase &fs) = 0;

    /**
     *  This copies a file. Throws an FSException on errors.
     */
    virtual void copy(XWP::Debug &d, FSModelBase &fs, const string &strTargetPath) = 0;

    /**
     *  This moves a file. Throws an FSException on errors.
     */
    virtual void move(XWP::Debug &d, FSModelBase &fs, const string &strTargetPath) = 0;

    /**
     *  This creates a subdirectory in the given directory. Throws an FSException on errors.
     */
    virtual PFSDirectory createSubdirectory(const string &strParentPath,
                                            const string &strBasename) = 0;

    /**
     *  This creates an empty file in the given directory. Throws an FSException on errors.
     */
    virtual PFSFile createEmptyDocument(const string &strParentPath,
                                        const string &strBasename) = 0;
};


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

    /**************************************
     *
     *  Constructors / destructors
     *
     *************************************/

protected:
    virtual ~FSMonitorBase();


    /**************************************
     *
     *  Public methods
     *
     *************************************/

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


    /**************************************
     *
     *  Protected instance data
     *
     *************************************/

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
    IS_LOCAL                   =  (1 <<  9),        // path has file:/// URI

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
    friend class FSContainer;
    friend class FSSymlink;

    /**************************************
     *
     *  Constructors / destructors
     *
     *************************************/

protected:
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
//     static PFSModelBase MakeAwake(PGioFile pGioFile);

    FSModelBase(FSType type,
                const string &strBasename,
                uint64_t cbSize);
    virtual ~FSModelBase() { };


    /**************************************
     *
     *  Public instance methods
     *
     *************************************/
public:

    /**
     *  Returns a Gio::File for this instance. This creates a new instance on every call.
     */
//     PGioFile getGioFile();

    const std::string& getBasename() const
    {
        return _strBasename;
    }

    uint64_t getId() const
    {
        return _uID;
    }

    uint64_t getFileSize() const
    {
        return _cbSize;
    }

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

    /**
     *  Returns true if this is a directory or a symlink to one. Can cause I/O if this
     *  is a symlink as this calls the virtual getResolvedType() method.
     */
    bool isDirectoryOrSymlinkToDirectory(FSTypeResolved &t)
    {
        t = getResolvedType();
        return (t == FSTypeResolved::DIRECTORY) || (t == FSTypeResolved::SYMLINK_TO_DIRECTORY);
    }

    /**
     *  Returns true if the file-system object has the "hidden" attribute, according to however Gio defines it.
     *
     *  Overridden for symlinks!
     */
    bool isHidden();

    /**
     *  Expands the path without resorting to realpath(), which would hit the disk. This
     *  walks up the parents chain recursively and rebuilds the string on every call.
     */
    std::string getPath() const;

    /**
     *  Returns an FSFile if *this is a file or a symlink to a file; otherwise, this returns
     *  nullptr.
     */
//     PFSFile getFile();

    /**
     *  Returns the parent directory of this filesystem object. Note that this
     *  can be either a FSDirectory or a FSSymlink that points to one.
     */
    PFSModelBase getParent() const;

    /**
     *  Returns true if this is located under the given directory, either directly
     *  or somewhere more deeply.
     *
     *  Does NOT return true if this and pDir are the same; you need to test for that
     *  manually.
     */
    bool isUnder(PFSDirectory pDir) const;

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
    FSContainer* getContainer();

    const std::string& describeType() const;
    std::string describe(bool fLong = false) const;

    /*
     *  Public operation methods
     */

    /**
     *  Renames this file to the given new name.
     *
     *  This does not notify file monitors since we can't be sure which thread we're running on. Call
     *  FSContainer::notifyFileRenamed() either afterwards if you call this on thread one, or have a
     *  GUI dispatcher which calls it instead.
     */
    void rename(const std::string &strNewName);

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
    void sendToTrash();

    /**
     *  Moves *this to the given target directory. pTarget mus either be a directory
     *  or a symlink to one.
     *
     *  This does not notify file monitors since we can't be sure which thread we're running on. Call
     *  FSContainer::notifyFileRemoved() and FSContainer::notifyFileAdded() either afterwards if you
     *  call this on thread one, or have a GUI dispatcher which calls it instead.
     */
    void moveTo(PFSModelBase pTarget);

    PFSModelBase copyTo(PFSModelBase pTarget);

    void testFileOps();


    /**************************************
     *
     *  Public static methods
     *
     *************************************/

    /**
     *  This should return an FSModelBase instance for the given file system path,
     *  waking up all parent objects as necessary.
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
    static PFSModelBase FindPath(const string &strPath);

    /**
     *  Looks up the given full path and returns it as a directory, or nullptr
     *  if the path does not exist or is not a directory.
     */
    static PFSDirectory FindDirectory(const string &strPath);

    /**
     *  Returns the user's home directory, or nullptr on errors.
     */
    static PFSDirectory GetHome();


    /**************************************
     *
     *  Protected instance methods
     *
     *************************************/

protected:
    PFSModelBase getSharedFromThis()
    {
        return shared_from_this();
    }

    PFSModelBase copyOrMoveImpl(PFSModelBase pTarget, bool fIsCopy);

    /**
     *  Implementation for getPath(), which only gets called if *this is not a root directory. This
     *  is necessary to correctly return file:/// for the root but file:///dir for anything else
     *  without double slashes.
     */
    std::string getPathImpl() const;


    /**************************************
     *
     *  Protected data
     *
     *************************************/

    uint64_t                    _uID = 0;
    FSType                      _type = FSType::UNINITIALIZED;
    FSFlagSet                   _fl;
    std::string                 _strBasename;
    uint64_t                    _cbSize;
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
    friend class FSContainer;


    /**************************************
     *
     *  Constructors / destructors
     *
     *************************************/

protected:
    FSFile(const string &strBasename,
           uint64_t cbSize)
        : FSModelBase(FSType::FILE,
                      strBasename,
                      cbSize)
    { }

    virtual ~FSFile()
    { }


    /**************************************
     *
     *  Public instance methods
     *
     *************************************/

public:
    virtual FSTypeResolved getResolvedType() override
    {
        return FSTypeResolved::FILE;
    }

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
    friend class FSModelBase;
    friend class FsImplBase;
    friend class FSMonitorBase;
    friend class ContentsLock;

public:
    enum class Get
    {
        ALL,
        FOLDERS_ONLY,
        FIRST_FOLDER_ONLY
    };


    /**************************************
     *
     *  Constructors / destructors
     *
     *************************************/
protected:
    /**
     *  Constructor. This is protected because an FSContainer only ever gets created through
     *  multiple inheritance.
     */
    FSContainer(FSModelBase &refBase);

    virtual ~FSContainer();


    /**************************************
     *
     *  Public instance methods
     *
     *************************************/

public:
    /**
     *  Attempts to find a file-system object in the current container (directory or symlink
     *  to a directory). This first calls isAwake() to check if the object has already been
     *  instantiated in memory; if not, we try to find it on disk and instantiate it.
     *  Returns nullptr if the file definitely doesn't exist or cannot be accessed.
     *
     *  This calls into the backend and may throw FSException. It may return nullptr if
     *  no such file exists.
     */
    PFSModelBase find(const std::string &strParticle);

    /**
     *  Returns true if the container has the "populated with directories" flag set.
     *  See getContents() for what that means.
     */
    bool isPopulatedWithDirectories() const;

    /**
     *  Returns true if the container has the "populated with all" flag set.
     *  See getContents() for what that means.
     */
    bool isCompletelyPopulated() const;

    /**
     *  Unsets both the "populated with all" and "populated with directories" flags for this
     *  directory, which will cause getContents() to refresh the contents list from disk on
     *  the next call.
     */
    void unsetPopulated();

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
    size_t getContents(FSVector &vFiles,
                       Get getContents,
                       FSVector *pvFilesAdded,
                       FSVector *pvFilesRemoved,
                       StopFlag *pStopFlag,
                       bool fFollowSymlinks = false);       //!< in: whether to call follow() on each symlink

    /**
     *  Creates a new physical directory in this container (physical directory or symlink
     *  pointing to one), which is returned.
     *
     *  Throws an FSException on I/O errors.
     *
     *  This does not automatically call file-system monitors since this may not run on
     *  the GUI thread. Call notifyFileAdded() with the returned instance afterwards.
     */
    PFSDirectory createSubdirectory(const std::string &strName);

    /**
     *  Creates a new physical directory in this container (physical directory or symlink
     *  pointing to one), which is returned.
     *
     *  Throws an FSException on I/O errors.
     *
     *  This does not automatically call file-system monitors since this may not run on
     *  the GUI thread. Call notifyFileAdded() with the returned instance afterwards.
     */
    PFSFile createEmptyDocument(const std::string &strName);

    /**
     *  Notifies all monitors attached to *this that a file has been added.
     *
     *  Call this on the GUI thread after pFS has been added to a container
     *  (e.g. by a create or move or copy operation).
     */
    void notifyFileAdded(PFSModelBase pFS) const;

    /**
     *  Notifies all monitors attached to *this that a file has been removed.
     *
     *  Call this on the GUI thread after pFS has been removed from a container
     *  (e.g. after trashing).
     *  Note that at this time, the file may longer exist on disk and is only
     *  an empty shell any more.
     */
    void notifyFileRemoved(PFSModelBase pFS) const;

    /**
     *  Notifies all monitors attached to *this that a file has been renamed
     *  (without having changed containers).
     */
    void notifyFileRenamed(PFSModelBase pFS, const std::string &strOldName, const std::string &strNewName) const;


    /**************************************
     *
     *  Protected instance methods
     *
     *************************************/

protected:
    PFSDirectory resolveDirectory();

    /**
     *  Protected method to add the given child to this container's list of children, under
     *  this container's ContentsLock. Also updates the parent pointer in the child.
     *
     *  This must be called after an object has been instantiated with FSModelBase::MakeAwake().
     */
    void addChild(ContentsLock &lock, PFSModelBase p);

    /**
     *  Inversely to addChild(), removes the given child from this container's list of
     *  children.
     */
    void removeChild(ContentsLock &lock, PFSModelBase p);

    /**
     *  Debugging helper.
     */
    void dumpContents(const string &strIntro, ContentsLock &lock);


    /**************************************
     *
     *  Protected data
     *
     *************************************/

    struct Impl;
    Impl                *_pImpl;

public:
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
    friend class FSModelBase;
    friend class FSContainer;

    /**************************************
     *
     *  Constructors / destructors
     *
     *************************************/

protected:
    FSDirectory(const string &strBasename)
        : FSModelBase(FSType::DIRECTORY, strBasename, 0),
          FSContainer((FSModelBase&)*this)
    { }


    /**************************************
     *
     *  Public instance methods
     *
     *************************************/

public:
    virtual FSTypeResolved getResolvedType() override
    {
        return FSTypeResolved::DIRECTORY;
    }

    static PFSDirectory GetCwdOrThrow();
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
    friend class FsGioImpl;

    /**************************************
     *
     *  Constructors / destructors
     *
     *************************************/

protected:
    /**
     *  Factory method to create an instance and return a shared_ptr to it.
     *  This normally gets called by FSModelBase::MakeAwake() only. This
     *  does not add the new object to a container; you must call setParent()
     *  on the result.
     */
    static PFSSymlink Create(const string &strBasename);

    FSSymlink(const string &strBasename)
        : FSModelBase(FSType::SYMLINK, strBasename, 0),
          FSContainer((FSModelBase&)*this),
          _state(State::NOT_FOLLOWED_YET)
    { }


    /**************************************
     *
     *  Public instance methods
     *
     *************************************/

public:
    virtual FSTypeResolved getResolvedType() override;

    /**
     *  Returns the symlink target, or nullptr if the symlink is broken.
     *  This cannot be const as the target may need to be resolved on the first call.
     */
    PFSModelBase getTarget();


    /**************************************
     *
     *  Protected instance data
     *
     *************************************/

protected:
    enum class State
    {
        NOT_FOLLOWED_YET = 0,
        RESOLVING = 1,
        BROKEN = 2,
        TO_FILE = 3,
        TO_DIRECTORY = 4,
        TO_OTHER = 5
    };

    State           _state;
    PFSModelBase    _pTarget;
    Mutex           _mutexState;

    /**
     *  Atomically resolves the symlink and caches the result. This may need to
     *  do blocking disk I/O and may therefore not be quick.
     */
    State follow();
};


#endif // XWP_FSMODEL_BASE_H
