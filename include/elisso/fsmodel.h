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

enum class FSType
{
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

enum class FolderPopulated
{
    NOT,
    WITH_FOLDERS,
    COMPLETELY
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


/***************************************************************************
 *
 *  FSLockGuard
 *
 **************************************************************************/

class FSLock
{
public:
    FSLock();
    virtual ~FSLock();

private:
    struct Impl;
    Impl *_pImpl;
};


/***************************************************************************
 *
 *  FSModelBase
 *
 **************************************************************************/

class FSModelBase
{
public:
    static PFSModelBase FindPath(const std::string &strPath, FSLock &lock);
    static PFSDirectory FindDirectory(const std::string &strPath, FSLock &lock);

    std::string getBasename();
    uint64_t getFileSize();
    std::string getIcon();

    FSType getType() const
    {
        return _type;
    }

    virtual FSTypeResolved getResolvedType() = 0;

    static std::string GetDirname(const std::string& str);
    static std::string GetBasename(const std::string &str);

protected:
    static PFSModelBase MakeAwake(Glib::RefPtr<Gio::File> pGioFile);
    FSModelBase(FSType type, Glib::RefPtr<Gio::File> pGioFile);

    FSType                  _type;
    Glib::RefPtr<Gio::File> _pGioFile;
};


/***************************************************************************
 *
 *  FSFile
 *
 **************************************************************************/

class FSFile : public FSModelBase
{
    friend class FSModelBase;

public:
    virtual FSTypeResolved getResolvedType() override
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
    virtual FSTypeResolved getResolvedType() override
    {
        return FSTypeResolved::DIRECTORY;
    }

    void getContents(FSList &llFiles, bool fDirsOnly);

    bool isPopulatedWithDirectories()
    {
        return (_pop != FolderPopulated::NOT);
    }

    bool isCompletelyPopulated()
    {
        return (_pop == FolderPopulated::COMPLETELY);
    }

protected:
    FolderPopulated     _pop;

    FSDirectory(Glib::RefPtr<Gio::File> pGioFile);
    static PFSDirectory Create(Glib::RefPtr<Gio::File> pGioFile);
};


/***************************************************************************
 *
 *  FSSymlink
 *
 **************************************************************************/

class FSSymlink : public FSModelBase
{
    friend class FSModelBase;

public:
    virtual FSTypeResolved getResolvedType() override;

protected:
    FSSymlink(Glib::RefPtr<Gio::File> pGioFile);
    static PFSSymlink Create(Glib::RefPtr<Gio::File> pGioFile);
};


/***************************************************************************
 *
 *  FSSpecial
 *
 **************************************************************************/

class FSSpecial : public FSModelBase
{
    friend class FSModelBase;

public:
    virtual FSTypeResolved getResolvedType() override
    {
        return FSTypeResolved::SPECIAL;
    }

protected:
    FSSpecial(Glib::RefPtr<Gio::File> pGioFile);
    static PFSSpecial Create(Glib::RefPtr<Gio::File> pGioFile);
};


/***************************************************************************
 *
 *  FSMountable
 *
 **************************************************************************/

class FSMountable : public FSModelBase
{
    friend class FSModelBase;

public:
    virtual FSTypeResolved getResolvedType() override
    {
        return FSTypeResolved::MOUNTABLE;
    }

protected:
    FSMountable(Glib::RefPtr<Gio::File> pGioFile);
    static PFSMountable Create(Glib::RefPtr<Gio::File> pGioFile);
};

#endif // ELISSO_FSMODEL_H
