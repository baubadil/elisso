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

typedef std::vector<PFsGioMountable> FsGioMountablesVector;
typedef std::shared_ptr<FsGioMountablesVector> PFsGioMountablesVector;

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
    virtual PFsObject findPath(const string &strPath) override;

    /**
     *  Instantiates the given file system object from the given path and returns it,
     *  but does NOT add it to the parent container.
     */
    virtual PFsObject makeAwake(const string &strParentPath,
                                const string &strBasename,
                                bool fIsLocal) override;

    virtual PFsDirEnumeratorBase beginEnumerateChildren(FsContainer &cnr) override;

    virtual bool getNextChild(PFsDirEnumeratorBase pEnum, string &strBasename) override;

    virtual string getSymlinkContents(FsSymlink &ln) override;

    virtual void rename(FsObject &fs, const string &strNewName) override;

    virtual void trash(FsObject &fs) override;

    virtual void copy(Debug &d, FsObject &fs, const string &strTargetPath) override;

    virtual void move(Debug &d, FsObject &fs, const string &strTargetPath) override;

    virtual PFsDirectory createSubdirectory(const string &strParentPath,
                                            const string &strBasename) override;

    virtual PFSFile createEmptyDocument(const string &strParentPath,
                                        const string &strBasename) override;

    PGioFile getGioFile(FsObject &fs);

    PFsGioFile getFile(PFsObject pFS, FSTypeResolved t);

    Glib::RefPtr<Gio::FileInfo> getFileInfo(PGioFile pGioFile);

    static void Init();
};

extern FsGioImpl *g_pFsGioImpl;


/***************************************************************************
 *
 *  FsGioFile
 *
 **************************************************************************/

/**
 *  Subclass of the base implementation's FsFile. All file instances are actually
 *  instantiated as instances of this subclass, adding Gio::File support to them.
 */
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
     *  This normally gets called by FsObject::MakeAwake() only. This
     *  does not add the new object to a container; you must call setParent()
     *  on the result.
     */
    static PFsGioFile Create(const string &strBasename, const FsCoreInfo &info);

    FsGioFile(const string &strBasename,
              const FsCoreInfo &info)
        : FSFile(strBasename, info)
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

    StringVector& getIcons();


    /**************************************
     *
     *  Public static methods
     *
     *************************************/

    static uint64_t GetThumbnailCacheSize();


    /**************************************
     *
     *  Protected data
     *
     *************************************/

protected:
    struct ThumbData;
    ThumbData       *_pThumbData = nullptr;
    StringVector    *_psvIcons = nullptr;
};


/***************************************************************************
 *
 *  FsGioDirectory
 *
 **************************************************************************/

class FsGioDirectory : public FsDirectory
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
     *  This normally gets called by FsObject::MakeAwake() only. This
     *  does not add the new object to a container; you must call setParent()
     *  on the result.
     */
    static PFsGioDirectory Create(const string &strBasename, const FsCoreInfo &info);

    FsGioDirectory(const string &strBasename,
                   const FsCoreInfo &info)
        : FsDirectory(FSType::DIRECTORY, strBasename, info)
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
class RootDirectory : public FsDirectory
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
    RootDirectory(const std::string &strScheme, const FsCoreInfo &info);

    std::string     _strScheme;
};


/***************************************************************************
 *
 *  FsGioSpecial
 *
 **************************************************************************/

class FsGioSpecial : public FsObject
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
     *  This normally gets called by FsObject::MakeAwake() only. This
     *  does not add the new object to a container; you must call setParent()
     *  on the result.
     */
    static PFsGioSpecial Create(const string &strBasename);

    FsGioSpecial(const string &strBasename)
        : FsObject(FSType::SPECIAL, strBasename, { 0, "", ""})
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

class FsGioMountable : public FsObject
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
     *  This normally gets called by FsObject::MakeAwake() only. This
     *  does not add the new object to a container; you must call setParent()
     *  on the result.
     */
    static PFsGioMountable Create(const string &strName,
                                  PFsGioDirectory pRootDir);

    FsGioMountable(const string &strName,
                   PFsGioDirectory pRootDir)
        : FsObject(FSType::MOUNTABLE, strName, { 0, "", ""}),
          _pRootDir(pRootDir)
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

    PFsGioDirectory getRootDirectory()
    {
        return _pRootDir;
    }

    /**************************************
     *
     *  Public static methods
     *
     *************************************/

    static void GetMountables(FsGioMountablesVector &llMountables);

private:
    PFsGioDirectory _pRootDir;
};


/***************************************************************************
 *
 *  FileContents
 *
 **************************************************************************/

/**
 *  Simple structure to temporarily hold the complete binary contents
 *  of a file. The constructor reads them from disk via fopen().
 */
struct FileContents
{
    FileContents(FsGioFile &file);
    ~FileContents();

    char *_pData;
    size_t _size;
};
typedef std::shared_ptr<FileContents> PFileContents;


#endif // ELISSO_FSMODEL_GIO_H
