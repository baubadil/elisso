/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_FSMODEL_GIO_H
#define ELISSO_FSMODEL_GIO_H

#include "glibmm.h"
#include "giomm.h"

#include "xwp/fsmodel_base.h"

typedef Glib::RefPtr<Gio::File> PGioFile;

namespace Gdk { class Pixbuf; }
typedef Glib::RefPtr<Gdk::Pixbuf> PPixbuf;

class FsGioFile;
typedef std::shared_ptr<FsGioFile> PFsGioFile;

class FsGioDirectory;
typedef std::shared_ptr<FsGioDirectory> PFsGioDirectory;

class FsGioSpecial;
typedef std::shared_ptr<FsGioSpecial> PFsGioSpecial;

class FsGioMountable;
typedef std::shared_ptr<FsGioMountable> PFsGioMountable;

class CurrentDirectory;
typedef std::shared_ptr<CurrentDirectory> PCurrentDirectory;


/***************************************************************************
 *
 *  FsGioImpl
 *
 **************************************************************************/

class FsGioImpl : public FsImplBase
{
public:
    virtual PFSModelBase findPath(const string &strPath) override;

    /**
     *  Instantiates the given file system object from the given path and returns it,
     *  but does NOT add it to the parent container.
     */
    virtual PFSModelBase makeAwake(const string &strParentPath,
                                   const string &strBasename,
                                   bool fIsLocal) override;

    virtual PFsDirEnumeratorBase beginEnumerateChildren(FSContainer &cnr) override;

    virtual bool getNextChild(PFsDirEnumeratorBase pEnum, string &strBasename) override;

    virtual string getSymlinkContents(FSSymlink &ln) override;

    virtual void rename(FSModelBase &fs, const string &strNewName) override;

    virtual void trash(FSModelBase &fs) override;

    virtual void copy(Debug &d, FSModelBase &fs, const string &strTargetPath) override;

    virtual void move(Debug &d, FSModelBase &fs, const string &strTargetPath) override;

    virtual PFSDirectory createSubdirectory(const string &strParentPath,
                                            const string &strBasename) override;

    virtual PFSFile createEmptyDocument(const string &strParentPath,
                                        const string &strBasename) override;

    PGioFile getGioFile(FSModelBase &fs);

    Glib::ustring getIcon(FSModelBase &fs);

    PFsGioFile getFile(PFSModelBase pFS);

    static void Init();
};

extern FsGioImpl *g_pFsGioImpl;


/***************************************************************************
 *
 *  FsGioFile
 *
 **************************************************************************/

class FsGioFile : public FSFile
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
    static PFsGioFile Create(const string &strBasename, uint64_t cbSize);

    FsGioFile(const string &strBasename, uint64_t cbSize)
        : FSFile(strBasename, cbSize)
    { }

    virtual ~FsGioFile();


    /**************************************
     *
     *  Public instance methods
     *
     *************************************/

public:
    /**
     *  Caches the given pixbuf for *this and the given thumbnail size.
     */
    void setThumbnail(uint32_t thumbsize, PPixbuf ppb);

    /**
     *  Returns a pixbuf for *this and the given thumbnail size if
     *  setThumbnail() gave us one before.
     */
    PPixbuf getThumbnail(uint32_t thumbsize) const;


    /**************************************
     *
     *  Protected data
     *
     *************************************/

protected:
    struct ThumbData;
    ThumbData   *_pThumbData = nullptr;
};


/***************************************************************************
 *
 *  FsGioDirectory
 *
 **************************************************************************/

class FsGioDirectory : public FSDirectory
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
    static PFsGioDirectory Create(const string &strBasename);

    FsGioDirectory(const string &strBasename)
        : FSDirectory(strBasename)
    { }
};


/***************************************************************************
 *
 *  RootDirectory
 *
 **************************************************************************/

class RootDirectory;
typedef std::shared_ptr<RootDirectory> PRootDirectory;

/**
 *  Specialization of root directories ("/") with a URI scheme.
 *  Most commonly, "/" for the "file" scheme is the root of the
 *  local file system, but we can instantiate all of the schemes
 *  supported by Gio::File.
 */
class RootDirectory : public FSDirectory
{

    /**************************************
     *
     *  Constructors / destructors
     *
     *************************************/

public:
    const std::string& getURIScheme() const
    {
        return _strScheme;
    }

    /**
     *  Returns the root directory for the given URI scheme, e.g. "file" or "trash" or
     *  "ftp". Throws on errors.
     */
    static PRootDirectory Get(const std::string &strScheme);

private:
    RootDirectory(const std::string &strScheme);

    std::string     _strScheme;
};


/***************************************************************************
 *
 *  FsGioSpecial
 *
 **************************************************************************/

class FsGioSpecial : public FSModelBase
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
    static PFsGioSpecial Create(const string &strBasename);

    FsGioSpecial(const string &strBasename)
        : FSModelBase(FSType::SPECIAL, strBasename, 0)
    { }


    /**************************************
     *
     *  Public methods
     *
     *************************************/

public:
    virtual FSTypeResolved getResolvedType() override
    {
        return FSTypeResolved::SPECIAL;
    }
};


/***************************************************************************
 *
 *  FsGioMountable
 *
 **************************************************************************/

class FsGioMountable : public FSModelBase
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
    static PFsGioMountable Create(const string &strBasename);

    FsGioMountable(const string &strBasename)
        : FSModelBase(FSType::MOUNTABLE, strBasename, 0)
    { }


    /**************************************
     *
     *  Public instance methods
     *
     *************************************/

public:
    virtual FSTypeResolved getResolvedType() override
    {
        return FSTypeResolved::MOUNTABLE;
    }

    /**************************************
     *
     *  Public static methods
     *
     *************************************/

public:
    static void GetMountables();

};


#endif // ELISSO_FSMODEL_GIO_H
