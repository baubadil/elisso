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
#include "glibmm.h"
#include "giomm.h"

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


/***************************************************************************
 *
 *  FSLock
 *
 **************************************************************************/

class LockBase
{
public:
    LockBase(std::recursive_mutex &m)
        : _m(m)
    {
        _m.lock();
        _fLocked = true;
    }

    ~LockBase()
    {
        release();
    }

    void release()
    {
        if (_fLocked)
            _m.unlock();
        _fLocked = false;
    }

protected:
    std::recursive_mutex    &_m;
    bool                    _fLocked = false;
};

class FSLock : public LockBase
{
public:
    FSLock();
};


/***************************************************************************
 *
 *  FSModelBase
 *
 **************************************************************************/

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
 */
class FSModelBase : public std::enable_shared_from_this<FSModelBase>
{
public:
    static PFSModelBase FindPath(const std::string &strPath, FSLock &lock);
    static PFSDirectory FindDirectory(const std::string &strPath, FSLock &lock);

    Glib::RefPtr<Gio::File> getGioFile()
    {
        return _pGioFile;
    }

    std::string getBasename();
    uint64_t getFileSize();
    Glib::ustring getIcon();

    FSType getType() const
    {
        return _type;
    }

    virtual FSTypeResolved getResolvedType(FSLock &lock) = 0;
    PFSDirectory resolveDirectory(FSLock &lock);

    virtual bool isHidden(FSLock &lock);

    std::string getRelativePath();
    std::string getAbsolutePath(bool fThrow = false);
    std::string getFormattedPath();

    PFSDirectory getParent();

    const std::string& describeType();
    std::string describe(bool fLong = false);

protected:
    friend class FSDirectory;

    static PFSModelBase MakeAwake(Glib::RefPtr<Gio::File> pGioFile);
    FSModelBase(FSType type, Glib::RefPtr<Gio::File> pGioFile);
    virtual ~FSModelBase() { };

    void setParent(PFSDirectory pParentDirectory, FSLock &lock);

    uint64_t                    _uID = 0;
    FSType                      _type = FSType::UNINITIALIZED;
    uint8_t                     _flFile = 0;
    Glib::RefPtr<Gio::File>     _pGioFile;
    PFSDirectory                _pParent;
};

const uint8_t  FL_POPULATED_WITH_DIRECTORIES =  (1 <<  0);        // only for dirs
const uint8_t  FL_POPULATED_WITH_ALL         =  (1 <<  1);        // only for dirs
const uint8_t  FL_IS_ROOT_DIRECTORY          =  (1 <<  2);        // only for dirs; strParticle is ""
const uint8_t  FL_IS_CURRENT_DIRECTORY       =  (1 <<  3);        // only for dirs; strParticle is "."
const uint8_t  FL_IS_RELATIVE_PARENT         =  (1 <<  4);        // only for dirs; strParticle is ".."
const uint8_t  FL_DELETED                    =  (1 <<  5);        // After Unlink() has been called.


/***************************************************************************
 *
 *  FSFile
 *
 **************************************************************************/

class FSFile : public FSModelBase
{
    friend class FSModelBase;

    virtual FSTypeResolved getResolvedType(FSLock &lock) override
    {
        return FSTypeResolved::FILE;
    }

protected:
    FSFile(Glib::RefPtr<Gio::File> pGioFile);
    static PFSFile Create(Glib::RefPtr<Gio::File> pGioFile);
};


/***************************************************************************
 *
 *  FSDirectory
 *
 **************************************************************************/

class FSDirectory : public FSModelBase
{
    friend class FSModelBase;

public:
    enum class Get
    {
        ALL,
        FOLDERS_ONLY,
        FIRST_FOLDER_ONLY
    };

    virtual FSTypeResolved getResolvedType(FSLock &lock) override
    {
        return FSTypeResolved::DIRECTORY;
    }

    size_t getContents(FSList &llFiles, Get getContents, FSLock &lock);

    bool isPopulatedWithDirectories()
    {
        return !!(_flFile & FL_POPULATED_WITH_DIRECTORIES);
    }

    bool isCompletelyPopulated()
    {
        return !!(_flFile & FL_POPULATED_WITH_ALL);
    }

    PFSModelBase find(const std::string &strParticle, FSLock &lock);

    PFSDirectory findSubdirectory(const std::string &strParticle, FSLock &lock);

    PFSModelBase isAwake(const std::string &strParticle, FSLock &lock);

    static PFSDirectory GetHome(FSLock &lock);
    static PFSDirectory GetRoot(FSLock &lock);

protected:
    struct Impl;
    Impl                *_pImpl;

    FSDirectory(Glib::RefPtr<Gio::File> pGioFile);
    virtual ~FSDirectory();
    static PFSDirectory Create(Glib::RefPtr<Gio::File> pGioFile);
};

class RootDirectory;
typedef std::shared_ptr<RootDirectory> PRootDirectory;

class RootDirectory : public FSDirectory
{
    friend class FSModelBase;
    friend class FSDirectory;

private:
    RootDirectory();

    static PRootDirectory s_theRoot;

    static PRootDirectory GetImpl(FSLock &lock);
};

class CurrentDirectory;
typedef std::shared_ptr<CurrentDirectory> PCurrentDirectory;

class CurrentDirectory : public FSDirectory
{
    friend class FSModelBase;

private:
    CurrentDirectory();

    static PCurrentDirectory s_theCWD;

    static PCurrentDirectory GetImpl(FSLock &lock);
};


/***************************************************************************
 *
 *  FSSymlink
 *
 **************************************************************************/

class FSSymlink : public FSModelBase
{
    friend class FSModelBase;

    virtual FSTypeResolved getResolvedType(FSLock &lock) override;

protected:
    FSSymlink(Glib::RefPtr<Gio::File> pGioFile);
    static PFSSymlink Create(Glib::RefPtr<Gio::File> pGioFile);

    PFSModelBase getTarget(FSLock &lock)
    {
        follow(lock);
        return _pTarget;
    }

    virtual bool isHidden(FSLock &lock) override;

    enum class State
    {
        NOT_FOLLOWED_YET = 0,
        BROKEN = 1,
        TO_FILE = 2,
        TO_DIRECTORY = 3
    };
    State           _state = State::NOT_FOLLOWED_YET;
    PFSModelBase    _pTarget;

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

    virtual FSTypeResolved getResolvedType(FSLock &lock) override
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

    virtual FSTypeResolved getResolvedType(FSLock &lock) override
    {
        return FSTypeResolved::MOUNTABLE;
    }
};

#endif // ELISSO_FSMODEL_H
