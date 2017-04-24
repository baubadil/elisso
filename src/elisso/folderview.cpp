/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/folderview.h"

#include "elisso/elisso.h"
#include "elisso/fileops.h"
#include "elisso/mainwindow.h"
#include "elisso/textentrydialog.h"
#include "elisso/thumbnailer.h"
#include "elisso/contenttype.h"
#include "xwp/except.h"
#include <thread>
#include <iostream>
#include <iomanip>


/***************************************************************************
 *
 *  Globals, static variable instantiations
 *
 **************************************************************************/

std::atomic<std::uint64_t>  g_uViewID(1);

void
ForEachUString(const Glib::ustring &str,
               const Glib::ustring &strDelimiter,
               std::function<void (const Glib::ustring&)> fnParticle)
{
    size_t p1 = 0;
    size_t p2;
    while ((p2 = str.find(strDelimiter, p1)) != string::npos)
    {
        int len = p2 - p1;
        if (len > 0)
            fnParticle(str.substr(p1, len));
        p1 = p2 + 1;
    }

    fnParticle(str.substr(p1));
}


/***************************************************************************
 *
 *  FolderContentsModelColumns (private)
 *
 **************************************************************************/

class FolderContentsModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    FolderContentsModelColumns()
    {
        add(_colPFile);
        add(_colFilename);
        add(_colSize);
        add(_colIconSmall);
        add(_colIconBig);
        add(_colTypeString);
    }

    Gtk::TreeModelColumn<PFSModelBase>      _colPFile;
    Gtk::TreeModelColumn<Glib::ustring>     _colFilename;
    Gtk::TreeModelColumn<u_int64_t>         _colSize;
    Gtk::TreeModelColumn<PPixbuf>           _colIconSmall;
    Gtk::TreeModelColumn<PPixbuf>           _colIconBig;
    Gtk::TreeModelColumn<Glib::ustring>     _colTypeString;

    static FolderContentsModelColumns& Get()
    {
        if (!s_p)
            s_p = new FolderContentsModelColumns;
        return *s_p;
    }

private:
    static FolderContentsModelColumns *s_p;
};

FolderContentsModelColumns* FolderContentsModelColumns::s_p = nullptr;


/***************************************************************************
 *
 *  ElissoFolderView::PopulateThread
 *
 **************************************************************************/

typedef std::shared_ptr<ElissoFolderView::PopulateThread> PPopulateThread;

/**
 *  Populate thread implementation. This works as follows:
 *
 *   1) ElissoFolderView::setDirectory() calls Create(), which creates a shared_ptr
 *      to an instance and spawns the thread. The instance is also passed to the
 *      thread, which increases the refcount.
 *
 *   2) Create() takes a reference to a Glib::Dispatcher which gets fired when
 *      the populate thread ends. Create() also takes a reference to an FSList,
 *      which gets filled by the populate thread with the contents from
 *      FSContainer::getContents().
 *
 *   3) When the dispatcher then fires on the GUI thread, it should check
 *      getError() if an exception occured on the populate thread. If not,
 *      it can take the folder contents that was referenced by Create()
 *      and fill the folder view with it.
 *
 *   4) If, for any reason, the populate needs to be stopped early, the caller
 *      can call stopAndJoin() which will set the stop flag passed to
 *      FSContainer::getContents() and block until the populate thread has ended.
 */
class ElissoFolderView::PopulateThread : public ProhibitCopy
{
public:
    /**
     *  Creates an instance and returns a shared_ptr to it. Caller MUST store that shared_ptr
     *  in instance data until the thread ends.
     */
    static PPopulateThread Create(PFSModelBase &pDir,               //!< in: directory or symlink to directory to populate
                                  Glib::Dispatcher &dispatch,       //!< in: reference to dispatcher that gets fired when done
                                  FSList &llFolderContents,         //!< in: reference to list that should receive folder contents
                                  bool fClickFromTree,              //!< in: stored in instance data for dispatcher handler
                                  PFSModelBase pDirSelectPrevious)  //!< in: if set, select this item after populating
    {
        /* This nasty trickery is necessary to make std::make_shared work with a protected constructor. */
        class Derived : public PopulateThread
        {
        public:
            Derived(PFSModelBase &pDir, Glib::Dispatcher &dispatch, FSList &llFolderContents, bool fClickFromTree, PFSModelBase pDirSelectPrevious)
                : PopulateThread(pDir, dispatch, llFolderContents, fClickFromTree, pDirSelectPrevious) { }
        };

        auto p = std::make_shared<Derived>(pDir, dispatch, llFolderContents, fClickFromTree, pDirSelectPrevious);

        // We capture the shared_ptr "p" without &, meaning we create a copy, which increases the refcount
        // while the thread is running.
        p->_pThread = new std::thread([p]()
        {
            /*
             *  Thread function!
             */
            p->threadFunc();
        });

        return p;
    }

    void stopAndJoin()
    {
        _stopFlag.set();
        _pThread->join();
    }

    const Glib::ustring& getError() const
    {
        return _strError;
    }

    bool hadClickFromTree() const
    {
        return _fClickFromTree;
    }

    bool shouldBeSelected(PFSModelBase &pFS)
    {
        return (pFS == _pDirSelectPrevious);
    }

private:
    /**
     *  Constructor.
     */
    PopulateThread(PFSModelBase &pDir,
                   Glib::Dispatcher &dispatch,
                   FSList &llFolderContents,
                   bool fClickFromTree,
                   PFSModelBase pDirSelectPrevious)
        : _pDir(pDir),
          _refDispatch(dispatch),
          _refllFolderContents(llFolderContents),
          _fClickFromTree(fClickFromTree),
          _pDirSelectPrevious(pDirSelectPrevious)
    {
    }

    void threadFunc()
    {
        try
        {
            FSContainer *pCnr = _pDir->getContainer();
            if (pCnr)
                pCnr->getContents(_refllFolderContents,
                                  FSDirectory::Get::ALL,
                                  &_stopFlag);
        }
        catch(exception &e)
        {
            _strError = e.what();
        }

        if (!_stopFlag)
            // Trigger the dispatcher, which will call "populate done".
            _refDispatch.emit();
    }

    PFSModelBase        _pDir;
    Glib::Dispatcher    &_refDispatch;
    FSList              &_refllFolderContents;
    Glib::ustring       _strError;
    std::thread         *_pThread = nullptr;
    StopFlag            _stopFlag;
    bool                _fClickFromTree = false;     // true if SetDirectoryFlags::CLICK_FROM_TREE was set.
    PFSModelBase        _pDirSelectPrevious;
};


/***************************************************************************
 *
 *  ElissoFolderView::Impl (private)
 *
 **************************************************************************/

struct ElissoFolderView::Impl : public ProhibitCopy
{
    PPopulateThread                 pPopulateThread;      // only set while state == POPULATING

    // GUI thread dispatcher for when a folder populate is done.
    Glib::Dispatcher                dispatcherPopulateDone;

    Glib::RefPtr<Gtk::ListStore>    pListStore;
    FSList                          llFolderContents;
    Glib::RefPtr<Gtk::IconTheme>    pIconTheme;

    std::shared_ptr<Gtk::Menu>      pPopupMenu;

    PFolderViewMonitor              pMonitor;
    FileOperationsList              llFileOperations;
    PProgressDialog                 pProgressDialog;

    Thumbnailer                     thumbnailer;

    // This is a map which allows us to look up rows quickly for efficient removal of items by name.
    std::map<std::string, Gtk::TreeRowReference> mapRowReferences;

    Impl()
        : pIconTheme(Gtk::IconTheme::get_default())
    { }
};


/***************************************************************************
 *
 *  ElissoFolderView
 *
 **************************************************************************/

/**
 *
 */
ElissoFolderView::ElissoFolderView(ElissoApplicationWindow &mainWindow, int &iPageInserted)
    : Gtk::ScrolledWindow(),
      _id(g_uViewID++),
      _mainWindow(mainWindow),
      _iconView(),
      _treeView(),
      _infoBarError(),
      _infoBarLabel(),
      _pImpl(new ElissoFolderView::Impl())
{
    iPageInserted = _mainWindow.getNotebook().append_page(*this,
                                                          _labelNotebookPage,
                                                          _labelNotebookMenu);

    _treeView.setParent(*this);

    // Create the monitor. We need the this pointer so we can't do it in the Impl constructor.
    _pImpl->pMonitor = make_shared<FolderViewMonitor>(*this);

    // Allow multiple selections.
    _treeView.get_selection()->set_mode(Gtk::SELECTION_MULTIPLE);
    _iconView.property_selection_mode() = Gtk::SELECTION_MULTIPLE;

    // Connect the GUI thread dispatcher for when a folder populate is done.
    _pImpl->dispatcherPopulateDone.connect([this]()
    {
        this->onPopulateDone();
    });

    /*
     *  Set up the model for the list and icon views.
     */
    FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();
    _pImpl->pListStore = Gtk::ListStore::create(cols);

    // Model gets a custom sort function for "sort by filename", which sorts folders and symlinks to folders first.
    _pImpl->pListStore->set_sort_func(cols._colFilename, [&cols](const Gtk::TreeModel::iterator &a,
                                                                 const Gtk::TreeModel::iterator &b) -> int
    {
        auto rowA = *a;
        auto rowB = *b;
        PFSModelBase pFileA = rowA[cols._colPFile];
        PFSModelBase pFileB = rowB[cols._colPFile];
        bool fAIsFolder = pFileA->isDirectoryOrSymlinkToDirectory();
        bool fBIsFolder = pFileB->isDirectoryOrSymlinkToDirectory();
        if (fAIsFolder)
        {
            if (!fBIsFolder)
                return -1;
        }
        else
            if (fBIsFolder)
                return +1;

        const Glib::ustring &strA = rowA[cols._colFilename];
        const Glib::ustring &strB = rowB[cols._colFilename];
        return strA.compare(strB);
    });

    /*
     *  Set up the icon and list view.
     */
    setIconViewColumns();
    setListViewColumns();

    /*
     *  This has the config that can be also be changed by the user.
     */
    this->setViewMode(FolderViewMode::LIST);

    /*
     *  List and icon view signal handlers.
     */
    _iconView.signal_selection_changed().connect([this]()
    {
        this->onSelectionChanged();
    });

    _treeView.get_selection()->signal_changed().connect([this]()
    {
        this->onSelectionChanged();
    });

    _iconView.signal_item_activated().connect([this](const Gtk::TreeModel::Path &path)
    {
        this->onPathActivated(path);
    });

    _treeView.signal_row_activated().connect([this](const Gtk::TreeModel::Path &path,
                                                    Gtk::TreeViewColumn *pColumn)
    {
        this->onPathActivated(path);
    });

    /*
     *  Connect to the thumbnailer.
     */
    _pImpl->thumbnailer.connect([this]()
    {
        this->onThumbnailReady();
    });
}

/* virtual */
ElissoFolderView::~ElissoFolderView()
{
    delete _pImpl;
}

/**
 *  This gets called whenever the notebook tab on the right needs to be populated with the contents
 *  of another folder, in particular:
 *
 *   -- on startup, when the main window is constructed for the first time;
 *
 *   -- if the user double-clicks on another folder in the folder contents (via onFileActivated());
 *
 *   -- if the user single-clicks on a folder in the tree on the left.
 *
 *  Returns true if a populate thread was started, or false if the folder had already been populated.
 */
bool
ElissoFolderView::setDirectory(PFSModelBase pDirOrSymlinkToDir,
                               SetDirectoryFlagSet fl)
{
    bool rc = false;

    // Remember previous directory so we can try scrolling to and selecting it.
    PFSModelBase pDirSelectPrevious;
    if (fl & SetDirectoryFlags::SELECT_PREVIOUS)
        pDirSelectPrevious = _pDir;

    switch (_state)
    {
        case ViewState::ERROR:
        case ViewState::UNDEFINED:
        case ViewState::POPULATED:
        break;

        case ViewState::POPULATING:
            // stop the populate thread
            if (_pImpl->pPopulateThread)
            {
                Debug::Log(FOLDER_POPULATE_HIGH, "already populating, stopping other populate thread");
                _pImpl->pPopulateThread->stopAndJoin();
                Debug::Log(FOLDER_POPULATE_LOW, "OK, stopped");
            }
        break;
    }

    // Container is only != NULL if this is a directory or a symlink to onee.
    FSContainer *pContainer;
    if ((pContainer = pDirOrSymlinkToDir->getContainer()))
    {
        // If we have a directory already, push the path into the history.
        if (fl.test(SetDirectoryFlags::PUSH_TO_HISTORY))
            if (_pDir)
            {
                auto strFull = _pDir->getRelativePath();
                // Do not push if this is the same as the last item on the stack.
                if (    (!_aPathHistory.size())
                     || (_aPathHistory.back() != strFull)
                   )
                    _aPathHistory.push_back(strFull);

                _uPreviousOffset = 0;
            }

        _pDir = pDirOrSymlinkToDir;

        // Change view state early to avoid "selection changed" signals overflowing us.
        this->setState(ViewState::POPULATING);

        // If we're currently displaying an error, remove it.
        if (_mode == FolderViewMode::ERROR)
            setViewMode(_modeBeforeError);

        // Remove all old data, if any.
        _pImpl->pListStore->clear();
        _pImpl->llFolderContents.clear();

        _pImpl->thumbnailer.stop();

        auto pWatching = _pImpl->pMonitor->isWatching();
        if (pWatching)
            _pImpl->pMonitor->stopWatching(*pWatching);

        Debug::Log(FOLDER_POPULATE_HIGH, "POPULATING LIST \"" + _pDir->getRelativePath() + "\"");

        _pImpl->pPopulateThread = PopulateThread::Create(this->_pDir,
                                                         this->_pImpl->dispatcherPopulateDone,
                                                         _pImpl->llFolderContents,
                                                         fl.test(SetDirectoryFlags::CLICK_FROM_TREE),
                                                         pDirSelectPrevious);

        rc = true;

        dumpStack();
    }

    _mainWindow.enableBackForwardActions();

    return rc;
}

bool isImageFile(PFSModelBase pFile)
{
    const std::string &strBasename = pFile->getBasename();
    if (    endsWith(strBasename, ".jpg")
         || endsWith(strBasename, ".JPG")
         || endsWith(strBasename, ".png")
         || endsWith(strBasename, ".PNG")
       )
        return true;

    return false;
}

/**
 *  Gets called when the populate thread within setDirectory() has finished. We must now
 *  inspect the PopulateThread in the implementation struct for the results.
 */
void
ElissoFolderView::onPopulateDone()
{
    if (!_pImpl->pPopulateThread)
        return; // throw FSException("PopulateThread is nullptr");

    if (_pImpl->pPopulateThread->getError().size())
        this->setError(_pImpl->pPopulateThread->getError());
    else
    {
        Debug::Log(FOLDER_POPULATE_LOW, "ElissoFolderView::onPopulateDone(\"" + _pDir->getRelativePath() + "\")");

        Gtk::ListStore::iterator itSelect;

        size_t  cFolders = 0,
                cFiles = 0,
                cImageFiles = 0;

        /*
         * Insert all the files and collect some statistics.
         */
        for (auto &pFS : _pImpl->llFolderContents)
        {
            auto it = this->insertFile(pFS);

            if (_pImpl->pPopulateThread->shouldBeSelected(pFS))
                itSelect = it;

            switch (pFS->getResolvedType())
            {
                case FSTypeResolved::DIRECTORY:
                case FSTypeResolved::SYMLINK_TO_DIRECTORY:
                    ++cFolders;
                break;

                case FSTypeResolved::FILE:
                case FSTypeResolved::SYMLINK_TO_FILE:
                    ++cFiles;
                    if (isImageFile(pFS))
                        ++cImageFiles;
                break;

                default:

                break;
            }
        }

        // This does not yet connect the model, since we haven't set the state to populated yet.
        if (cImageFiles)
            this->setViewMode(FolderViewMode::ICONS);
        else
            this->setViewMode(FolderViewMode::LIST);

        // This connects the model and also calls onFolderViewLoaded(),
        // which in turn selects the item in the tree on the left.
        this->setState(ViewState::POPULATED);

        // Select and scroll an item in the list if necessary.
        if (itSelect)
        {
            Gtk::TreeModel::Path path(itSelect);
            switch (_mode)
            {
                case FolderViewMode::ICONS:
                case FolderViewMode::COMPACT:
                    _iconView.select_path(path);
                    _iconView.scroll_to_path(path, true, 0.5, 0.5);
                    // Grab focus (this is useful for "back" and "forward").
                    if (!_pImpl->pPopulateThread->hadClickFromTree())
                        _iconView.grab_focus();
                break;

                case FolderViewMode::LIST:
                    _treeView.get_selection()->select(path);
                    _treeView.scroll_to_row(path, 0.5);
                    // Grab focus (this is useful for "back" and "forward").
                    if (!_pImpl->pPopulateThread->hadClickFromTree())
                        _treeView.grab_focus();
                break;

                case FolderViewMode::UNDEFINED:
                case FolderViewMode::ERROR:
                break;
            }
        }

        // Release the populate data.
        this->_pImpl->pPopulateThread = nullptr;

        // Start watching.
        auto p = _pDir->getContainer();
        if (p)
            _pImpl->pMonitor->startWatching(*p);
    }
}

void
ElissoFolderView::removeFile(PFSModelBase pFS)
{
    string strBasename = pFS->getBasename();
    // Look up the row reference in the map to quickly get to the row.
    auto itSTL = _pImpl->mapRowReferences.find(strBasename);
    if (itSTL != _pImpl->mapRowReferences.end())
    {
        auto &rowref= itSTL->second;
        Gtk::TreePath path = rowref.get_path();
        auto itModel = _pImpl->pListStore->get_iter(path);
        _pImpl->pListStore->erase(itModel);
    }
}

Gtk::ListStore::iterator
ElissoFolderView::insertFile(PFSModelBase pFS)
{
    Gtk::ListStore::iterator it;

    if (!pFS->isHidden())
    {
        const FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();

        it = _pImpl->pListStore->append();
        auto row = *it;

        // pFile must always be set first because the sort function relies on it;
        // the sort function gets triggered AS SOON AS cols._colFilename is set.
        row[cols._colPFile] = pFS;
        const std::string &strBasename = pFS->getBasename();
        row[cols._colFilename] = strBasename;
        row[cols._colSize] = pFS->getFileSize();
        row[cols._colIconSmall] = loadIcon(it, pFS, ICON_SIZE_SMALL);
        row[cols._colIconBig] = loadIcon(it, pFS, ICON_SIZE_BIG);

        auto t = pFS->getResolvedType();
//                 row[cols._colTypeResolved] = t;

        std::string strType = "Error";
        const std::string *pstrType = &strType;
        switch (t)
        {
            case FSTypeResolved::FILE:
            case FSTypeResolved::SYMLINK_TO_FILE:
            {
                const ContentType *pType = nullptr;
                PFSFile pFile = pFS->getFile();
                if (pFile)
                    pType = ContentType::Guess(pFile);

                if (pType)
                {
                    if (t == FSTypeResolved::SYMLINK_TO_FILE)
                        strType = TYPE_LINK_TO + pType->getDescription();
                    else
                        strType = pType->getDescription();
                }
                else
                    pstrType = (t == FSTypeResolved::SYMLINK_TO_FILE) ? &TYPE_LINK_TO_FILE : &TYPE_FILE;
            }
            break;

            case FSTypeResolved::DIRECTORY: pstrType = &TYPE_FOLDER; break;
            case FSTypeResolved::SYMLINK_TO_DIRECTORY: pstrType = &TYPE_LINK_TO_FOLDER; break;
            case FSTypeResolved::BROKEN_SYMLINK: pstrType = &TYPE_BROKEN_LINK; break;

            case FSTypeResolved::SPECIAL: pstrType = &TYPE_SPECIAL; break;
            case FSTypeResolved::MOUNTABLE: pstrType = &TYPE_MOUNTABLE; break;
        }

        row[cols._colTypeString] = *pstrType;

        // Store this in the map by 1) creating a path from iterator and 2) then a row reference from the path.
        Gtk::TreePath path(it);
        Gtk::TreeRowReference rowref(_pImpl->pListStore, path);
        _pImpl->mapRowReferences[strBasename] = rowref;
    }

    return it;
}

void
ElissoFolderView::refresh()
{
    if (    (_state == ViewState::POPULATED)
         && (_pDir)
       )
    {
        FSContainer *pDir = _pDir->getContainer();
        if (pDir)
        {
            pDir->unsetPopulated();
            this->setDirectory(_pDir, {});     // do not push to history
        }
    }
}

bool
ElissoFolderView::canGoBack()
{
    return (_uPreviousOffset < _aPathHistory.size());
}

bool
ElissoFolderView::goBack()
{
    if (this->canGoBack())
    {
        ++_uPreviousOffset;
        std::string strPrevious = _aPathHistory[_aPathHistory.size() - _uPreviousOffset];
        PFSModelBase pDirOld = _pDir;
        PFSModelBase pDir;
        if ((pDir = FSModelBase::FindPath(strPrevious)))
            if (this->setDirectory(pDir,
                                   SetDirectoryFlags::SELECT_PREVIOUS))     // but do not push to history
            {
                std::vector<std::string>::iterator it = _aPathHistory.begin() + _uPreviousOffset;
                _aPathHistory.insert(it, pDirOld->getRelativePath());
                return true;
            }

    }

    return false;
}

bool
ElissoFolderView::canGoForward()
{
    return (_uPreviousOffset > _aPathHistory.size());
}

bool
ElissoFolderView::goForward()
{
    if (this->canGoForward())
    {
        std::string strPrevious = _aPathHistory[_aPathHistory.size() - --_uPreviousOffset];
        PFSDirectory pDir;
        if ((pDir = FSModelBase::FindDirectory(strPrevious)))
            if (this->setDirectory(pDir, {}))     // do not push to history
            {
                return true;
            }
    }

    return false;
}

/**
 *  The view state is one of the following:
 *
 *   -- POPULATING: setDirecotry() has been called, and a populate thread is running in
 *      the background. All calls are valid during this time, including another setDirectory()
 *      (which will kill the existing populate thread).
 *
 *   -- POPULATED: the populate thread was successful, and the contents of _pDir are being
 *      displayed.
 *
 *   -- ERROR: an error occured. This hides the tree or icon view containers and displays
 *      the error message instead. The only way to get out of this state is to call
 *      setDirectory() to try and display a directory again.
 *
 */
void
ElissoFolderView::setState(ViewState s)
{
    if (s != _state)
    {
        switch (s)
        {
            case ViewState::POPULATING:
            {
                // Disconnect model, disable sorting to speed up inserting lots of rows.
                this->connectModel(false);
                _mainWindow.getNotebook().set_tab_label(*this, _spinnerNotebookPage);
                _spinnerNotebookPage.show();
                _spinnerNotebookPage.start();

                _mainWindow.onLoadingFolderView(*this);
            }
            break;

            case ViewState::POPULATED:
            {
                // Connect model again, set sort.
                this->connectModel(true);

                _spinnerNotebookPage.stop();

                Glib::ustring strTitle("Error");
                if (_pDir)
                {
                    strTitle = _pDir->getBasename();
                    // set_max_width_chars doesn't work with ELLIPSIZE_MIDDLE so do it like this.
                    uint maxChars = strTitle.length();
                    if (maxChars < 20)
                        maxChars = 20;
                    else if (maxChars > 50)
                        maxChars = 50;
                    _labelNotebookPage.set_width_chars(maxChars);
                    _labelNotebookPage.set_ellipsize(Pango::EllipsizeMode::ELLIPSIZE_MIDDLE);
                    _labelNotebookPage.set_text(strTitle);
                    _mainWindow.getNotebook().set_tab_label(*this,
                                                            _labelNotebookPage);
                    _labelNotebookMenu.set_text(strTitle);
                }

                // Notify the tree that this folder has been populated.
                // Do not select the node in the tree if the click came from
                // the tree in the first place.
                PFSModelBase pDirSelect;
                if (    (!_pImpl->pPopulateThread)
                     || (!_pImpl->pPopulateThread->hadClickFromTree())
                   )
                    pDirSelect = _pDir;

                _mainWindow.onFolderViewLoaded(*this, pDirSelect);
            }
            break;

            case ViewState::ERROR:
                _mainWindow.getNotebook().set_tab_label_text(*this, "Error");
                _mainWindow.onFolderViewLoaded(*this, nullptr);
            break;

            default:
            break;
        }

        _state = s;
        if (s != ViewState::ERROR)
            _strError.clear();
    }
}

/**
 *  If the view's state is ERROR, the mode is ignored, and an error is displayed instead.
 */
void
ElissoFolderView::setViewMode(FolderViewMode m)
{
    if (m != _mode)
    {
        switch (_mode)
        {
            case FolderViewMode::ICONS:
            case FolderViewMode::COMPACT:
                _iconView.hide();
                this->remove();
            break;

            case FolderViewMode::LIST:
                _treeView.hide();
                this->remove();
            break;

//                 _compactView.hide();
//                 this->remove();
//             break;
//
            case FolderViewMode::ERROR:
                _infoBarError.hide();
                this->remove();
            break;

            case FolderViewMode::UNDEFINED:
            break;
        }

        switch (m)
        {
            case FolderViewMode::ICONS:
            case FolderViewMode::COMPACT:
            {
                this->add(_iconView);
                _iconView.show();

                FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();
                if (m == FolderViewMode::ICONS)
                {
                    // icon
                    _iconView.set_item_orientation(Gtk::Orientation::ORIENTATION_VERTICAL);
                    _iconView.set_pixbuf_column(cols._colIconBig);
                    _iconView.set_text_column(cols._colFilename);
                    _iconView.set_item_width(ICON_SIZE_BIG / 2);

                    // Margin around the entire view.
                    _iconView.set_margin(5);

                    // Spacings between items.
                    _iconView.set_row_spacing(5);
                    _iconView.set_column_spacing(5);

                    // Spacing between text and icon.
                    _iconView.set_spacing(0);
                    _iconView.set_item_padding(5);
                }
                else
                {
                    // compact
                    _iconView.set_item_orientation(Gtk::Orientation::ORIENTATION_HORIZONTAL);
                    _iconView.set_pixbuf_column(cols._colIconSmall);
                    _iconView.set_text_column(cols._colFilename);
                    _iconView.set_item_width(200);
                    _iconView.set_item_padding(0);
                    _iconView.set_margin(0);
                    _iconView.set_row_spacing(1);
                    _iconView.set_column_spacing(5);
                    _iconView.set_item_padding(5);
                }
            }

            case FolderViewMode::LIST:
                this->add(_treeView);
                _treeView.show();
            break;

//             case FolderViewMode::COMPACT:
//                 this->add(_compactView);
//                 _compactView.show();
//             break;

            case FolderViewMode::ERROR:
            {
                // Remember the old mode.
                _modeBeforeError = _mode;

                _infoBarLabel.set_markup("<span size=\"x-large\">" + Glib::Markup::escape_text(_strError) + "</span>");
                _infoBarLabel.show();
                _infoBarError.set_message_type(Gtk::MESSAGE_ERROR);
                auto pInfoBarContainer = dynamic_cast<Gtk::Container*>(_infoBarError.get_content_area());
                if (pInfoBarContainer)
                    pInfoBarContainer->add(_infoBarLabel);
                this->add(_infoBarError);
                _infoBarError.show();
            }
            break;

            case FolderViewMode::UNDEFINED:
            break;
        }

        _mode = m;

        this->connectModel(_state == ViewState::POPULATED);
    }
}

void
ElissoFolderView::setError(Glib::ustring strError)
{
    Debug::Log(DEBUG_ALWAYS, std::string(__FUNCTION__) + "(): " + strError);
    _strError = strError;
    setState(ViewState::ERROR);
    setViewMode(FolderViewMode::ERROR);
}

void
ElissoFolderView::selectAll()
{
    switch (this->_mode)
    {
        case FolderViewMode::ICONS:
        case FolderViewMode::COMPACT:
            _iconView.select_all();
        break;

        case FolderViewMode::LIST:
            _treeView.get_selection()->select_all();
        break;

        case FolderViewMode::ERROR:
        case FolderViewMode::UNDEFINED:
        break;
    }
}

/**
 *  Returns the single selected folder (directory or symlink pointing to one),
 *  or nullptr if either nothing is selected or the selection is not a single
 *  such folder.
 */
PFSModelBase
ElissoFolderView::getSelectedFolder()
{
    FileSelection sel;
    size_t cTotal = getSelection(sel);
    if (cTotal == 1)
        if (sel.llFolders.size() == 1)
            return sel.llFolders.front();

    return nullptr;
}

/**
 *  This is called by our subclass of the GTK TreeView, TreeViewPlus, which
 *  emulates the right-click and selection behavior of Nautilus/Nemo.
 *
 *  Types of context menus to be created:
 *
 *   -- One file selected.              Open, Copy/Cut/Paste, Rename, Delete
 *
 *   -- One folder selected, closed.    Open, Copy/Cut/Paste, Rename, Delete
 *
 *   -- One folder selected, opened.    Open, Copy/Cut/Paste, Rename, Delete
 *
 *   -- Multiple objects selected.      Copy/Cut/Paste, Delete
 */
void
ElissoFolderView::onMouseButton3Pressed(GdkEventButton *pEvent,
                                        MouseButton3ClickType clickType)
{
    auto &app = _mainWindow.getApplication();

    Debug::Log(DEBUG_ALWAYS, string(__FUNCTION__) + "(): clickType = " + to_string((int)clickType));

    auto pMenu = Gio::Menu::create();

    std::map<std::string, PAppInfo> mapAppInfosForTempMenuItems;

    switch (clickType)
    {
        case MouseButton3ClickType::SINGLE_ROW_SELECTED:
        case MouseButton3ClickType::MULTIPLE_ROWS_SELECTED:
        {
            FileSelection sel;
            size_t cTotal = this->getSelection(sel);

            if (cTotal == 1)
            {
                if (sel.llFolders.size() == 1)
                {
                    app.addMenuItem(pMenu, "Open", ACTION_EDIT_OPEN_SELECTED);
                    app.addMenuItem(pMenu, "Open in new tab", ACTION_EDIT_OPEN_SELECTED_IN_TAB);
                    app.addMenuItem(pMenu, "Open in terminal", ACTION_EDIT_OPEN_SELECTED_IN_TERMINAL);
                }
                else
                {
                    PFSModelBase pFS = sel.llOthers.front();
                    if (pFS)
                    {
                        PFSFile pFile = pFS->getFile();
                        if (pFile)
                        {
                            auto pContentType = ContentType::Guess(pFile);
                            if (pContentType)
                            {
                                auto pDefaultAppInfo = pContentType->getDefaultAppInfo();
                                if (pDefaultAppInfo)
                                {
                                    // The menu item for the default application is easy, it has a a compile-time action.
                                    app.addMenuItem(pMenu, "Open with " + pDefaultAppInfo->get_name(), ACTION_EDIT_OPEN_SELECTED);

                                    // The menu items for the non-default application items are difficult because they have
                                    // no compile-time actions, and we cannot add signals to the Gio::Menu items we are creating
                                    // here. So make a list of them and override the signals below when we create the Gtk::Menu.
                                    AppInfoList llInfos = pContentType->getAllAppInfos();
                                    if (llInfos.size() > 1)
                                        for (auto &pInfo : llInfos)
                                            if (pInfo->get_id() != pDefaultAppInfo->get_id())
                                            {
                                                std::string strLabel("Open with " + pInfo->get_name());
                                                auto pMenuItem = Gio::MenuItem::create(strLabel,
                                                                                       EMPTY_STRING);   // empty action
                                                // Remember the application info for the signal handlers below.
                                                mapAppInfosForTempMenuItems[strLabel] = pInfo;
                                                pMenu->append_item(pMenuItem);
                                            }
                                }
                            }
                        }
                    }
                }
            }

            auto pSubSection = app.addMenuSection(pMenu);
            app.addMenuItem(pSubSection, "Cut", ACTION_EDIT_CUT);
            app.addMenuItem(pSubSection, "Copy", ACTION_EDIT_COPY);

            pSubSection = app.addMenuSection(pMenu);
            if (cTotal == 1)
                app.addMenuItem(pSubSection, "Rename", ACTION_EDIT_RENAME);
            app.addMenuItem(pSubSection, "Move to trash", ACTION_EDIT_TRASH);
            app.addMenuItem(pSubSection, "TEST FILEOPS", ACTION_EDIT_TEST_FILEOPS);

            pSubSection = app.addMenuSection(pMenu);
            if (cTotal == 1)
                app.addMenuItem(pSubSection, "Properties", ACTION_EDIT_PROPERTIES);
        }
        break;

        case MouseButton3ClickType::WHITESPACE:
            app.addMenuItem(pMenu, "Open in terminal", ACTION_FILE_OPEN_IN_TERMINAL);
            auto pSubSection = app.addMenuSection(pMenu);
            app.addMenuItem(pSubSection, "Create new folder", ACTION_FILE_CREATE_FOLDER);
            app.addMenuItem(pSubSection, "Create empty document", ACTION_FILE_CREATE_DOCUMENT);
            app.addMenuItem(pSubSection, "Paste", ACTION_EDIT_PASTE);
            pSubSection = app.addMenuSection(pMenu);
            app.addMenuItem(pSubSection, "Properties", ACTION_FILE_PROPERTIES);
        break;
    }

    _pImpl->pPopupMenu = std::make_shared<Gtk::Menu>(pMenu);

    // Now fix up the menu items for non-default applications.
    for (Gtk::Widget *pChild : _pImpl->pPopupMenu->get_children())
    {
        // Skip the separators.
        Gtk::SeparatorMenuItem *pSep = dynamic_cast<Gtk::SeparatorMenuItem*>(pChild);
        if (!pSep)
        {
            Gtk::MenuItem *pMenuItem = dynamic_cast<Gtk::MenuItem*>(pChild);
            if (pMenuItem)
            {
                std::string strLabel(pMenuItem->get_label());
                auto it = mapAppInfosForTempMenuItems.find(strLabel);
                if (it != mapAppInfosForTempMenuItems.end())
                {
                    PAppInfo pAppInfo = it->second;
                    pMenuItem->signal_activate().connect([this, pMenuItem, pAppInfo]()
                    {
                        Debug::Log(DEBUG_ALWAYS, pMenuItem->get_label());
                        this->openFile(nullptr, pAppInfo);
                    });
                }
            }
        }
    }

    _pImpl->pPopupMenu->attach_to_widget(*this);
    _pImpl->pPopupMenu->popup(pEvent->button, pEvent->time);
}

/**
 *  Called from ElissoApplicationWindow::handleViewAction() to handle
 *  those actions that operate on the current folder view or the
 *  files therein.
 *
 *  Some of these are asynchronous, most are not.
 */
void
ElissoFolderView::handleAction(const std::string &strAction)
{
    try
    {
        if (strAction == ACTION_FILE_CREATE_FOLDER)
            createSubfolderDialog();
        else if (strAction == ACTION_EDIT_OPEN_SELECTED)
            openFile(nullptr, {});
        else if (strAction == ACTION_EDIT_SELECT_ALL)
            selectAll();
        else if (strAction == ACTION_EDIT_TRASH)
            trashSelected();
        else if (strAction == ACTION_EDIT_TEST_FILEOPS)
            testFileopsSelected();
        else if (strAction == ACTION_VIEW_ICONS)
            setViewMode(FolderViewMode::ICONS);
        else if (strAction == ACTION_VIEW_LIST)
            setViewMode(FolderViewMode::LIST);
        else if (strAction == ACTION_VIEW_COMPACT)
            setViewMode(FolderViewMode::COMPACT);
        else if (strAction == ACTION_VIEW_REFRESH)
            refresh();
        else if (strAction == ACTION_GO_BACK)
            goBack();
        else if (strAction == ACTION_GO_FORWARD)
            goForward();
        else if (strAction == ACTION_GO_PARENT)
        {
            PFSModelBase pDir = _pDir->getParent();
            if (pDir)
            {
                SetDirectoryFlagSet fl(SetDirectoryFlags::SELECT_PREVIOUS);
                fl |= SetDirectoryFlags::PUSH_TO_HISTORY;
                setDirectory(pDir,  fl);
            }
        }
        else if (strAction == ACTION_GO_HOME)
        {
            auto pHome = FSDirectory::GetHome();
            if (pHome)
                setDirectory(pHome, SetDirectoryFlags::PUSH_TO_HISTORY);
        }
    }
    catch(FSException &e)
    {
        _mainWindow.errorBox(e.what());
    }
}

/**
 *  Opens the given file-system object.
 *  As a special case, if pFS == nullptr, we try to get the current selection,
 *  but will take it only if exactly one object is selected.
 *
 *  If the object is a directory or a symlink to one, we call setDirectory().
 *  Otherwise we open the file with the
 */
void
ElissoFolderView::openFile(PFSModelBase pFS,        //!< in: file or folder to open; if nullptr, get single file from selection
                           PAppInfo pAppInfo)       //!< in: application to open file with; if nullptr, use file type's default
{
    if (!pFS)
    {
        FileSelection sel;
        size_t cTotal = getSelection(sel);
        if (cTotal == 1)
        {
            if (sel.llFolders.size() == 1)
                pFS = sel.llFolders.front();
            else
                pFS = sel.llOthers.front();
        }

        if (!pFS)
            return;
    }

    FSTypeResolved t = pFS->getResolvedType();

    switch (t)
    {
        case FSTypeResolved::DIRECTORY:
        case FSTypeResolved::SYMLINK_TO_DIRECTORY:
            this->setDirectory(pFS, SetDirectoryFlags::PUSH_TO_HISTORY);
        break;

        case FSTypeResolved::FILE:
        case FSTypeResolved::SYMLINK_TO_FILE:
        {
            PFSFile pFile = pFS->getFile();
            if (pFile)
            {
                PAppInfo pAppInfo2(pAppInfo);
                if (!pAppInfo2)
                {
                    const ContentType *pType = ContentType::Guess(pFile);
                    if (pType)
                        pAppInfo2 = pType->getDefaultAppInfo();
                }

                if (pAppInfo2)
                    pAppInfo2->launch(pFS->getGioFile());
                else
                    _mainWindow.errorBox("Cannot determine default application for file \"" + pFS->getRelativePath() + "\"");
            }
        }
        break;

        default:
        break;
    }
}

/**
 *  Prompts for a name and then creates a new subdirectory in the folder that is currently showing.
 *  Returns the directory object.
 *
 *  This may throw FSException.
 */
PFSDirectory
ElissoFolderView::createSubfolderDialog()
{
    PFSDirectory pNew;

    FSContainer *pContainer = this->_pDir->getContainer();

    if (pContainer)
    {
        TextEntryDialog dlg(this->_mainWindow,
                            "Create folder",
                            "Please enter the name of the new folder to be created in <b>" + this->_pDir->getBasename() + "</b>:",
                            "Create");
        if (dlg.run() == Gtk::RESPONSE_OK)
        {
            auto s = dlg.getText();
            pNew = pContainer->createSubdirectory(s);
        }
    }

    return pNew;
}

/**
 *  Trashes all files which are currently selected in the folder contents.
 */
void
ElissoFolderView::trashSelected()
{
    FileSelection sel;
    if (getSelection(sel))
        FileOperation::Create(FileOperation::Type::TRASH,
                              sel,
                              _pImpl->llFileOperations,
                              &_pImpl->pProgressDialog,
                              &_mainWindow);
}

void
ElissoFolderView::testFileopsSelected()
{
    FileSelection sel;
    if (getSelection(sel))
        FileOperation::Create(FileOperation::Type::TEST,
                              sel,
                              _pImpl->llFileOperations,
                              &_pImpl->pProgressDialog,
                              &_mainWindow);
}

void
ElissoFolderView::dumpStack()
{
    uint i = 0;
    for (auto &s : _aPathHistory)
        Debug::Log(FOLDER_STACK, "stack item " + to_string(i++) + ": " + s);
    Debug::Log(FOLDER_STACK, "offset: " + to_string(_uPreviousOffset));
}

void
ElissoFolderView::connectModel(bool fConnect)
{
    FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();

    switch (_mode)
    {
        case FolderViewMode::ICONS:
        case FolderViewMode::COMPACT:
            if (fConnect)
            {
                 _pImpl->pListStore->set_sort_column(cols._colFilename, Gtk::SortType::SORT_ASCENDING);
                _iconView.set_model(_pImpl->pListStore);
            }
            else
            {
                _iconView.unset_model();
                 _pImpl->pListStore->set_sort_column(Gtk::TreeSortable::DEFAULT_UNSORTED_COLUMN_ID, Gtk::SortType::SORT_ASCENDING);
            }
        break;

        case FolderViewMode::LIST:
            if (fConnect)
            {
                _pImpl->pListStore->set_sort_column(cols._colFilename, Gtk::SortType::SORT_ASCENDING);
                _treeView.set_model(_pImpl->pListStore);
            }
            else
            {
                _treeView.unset_model();
                _pImpl->pListStore->set_sort_column(Gtk::TreeSortable::DEFAULT_UNSORTED_COLUMN_ID, Gtk::SortType::SORT_ASCENDING);
            }
        break;

//             if (fConnect)
//             {
//                 for (auto &pFS : _pImpl->llFolderContents)
//                     if (!pFS->isHidden())
//                     {
//                         Glib::ustring strLabel = pFS->getBasename();
//                         auto p = Gtk::manage(new Gtk::Label(strLabel));
//                         _compactView.insert(*p, -1);
//                     }
//                 _compactView.show_all();
//             }
//             else
//             {
//                 auto childList = _compactView.get_children();
//                 for (auto &p : childList)
//                     _compactView.remove(*p);
//             }
//
        break;

        default:
        break;
    }
}

/**
 *  Part of the lazy-loading implementation.
 *
 *  If the given file should have a stock icon, then it is returned immediately
 *  and we're done.
 *
 *  If the given file has already been thumbnailed for the given size in this
 *  session, then its pixbuf is returned.
 *
 *  If the given file can be thumbnailed but has not yet been, then a stock icon
 *  is returned and it is handed over to the thumbnailer worker threads which
 *  will create a thumbnail in the background and call a dispatcher when the
 *  thumbnail is done.
 */
PPixbuf
ElissoFolderView::loadIcon(const Gtk::TreeModel::iterator& it,
                           PFSModelBase pFS,
                           int size)
{
    Glib::RefPtr<Gdk::Pixbuf> pReturn;

    // If this is a file for which we have previously set a thumbnail, then we're done.
    PFSFile pFile = pFS->getFile();
    if (pFile)
        pReturn = pFile->getThumbnail(size);

    // Not a file, or no thumbnail yet, then load default icon first.
    if (!pReturn)
    {
        Glib::ustring strIcons = pFS->getIcon();

        std::vector<Glib::ustring> sv;
        ForEachUString( strIcons,
                        " ",
                        [&sv](const Glib::ustring &strParticle)
                        {
                            if (!strParticle.empty())
                                sv.push_back(strParticle);

                        });

        Gtk::IconInfo i = _pImpl->pIconTheme->choose_icon(sv, size);
        if (i)
            pReturn = i.load_icon();

        // Again, if it's a file, and if it can be thumbnailed, then give it to
        // the thumbnailer to process in the background.
        if (pFile)
        {
            if (isImageFile(pFile))
            {
                Gtk::TreePath path(it);
                auto pRowRef = std::make_shared<Gtk::TreeRowReference>(_pImpl->pListStore, path);
                _pImpl->thumbnailer.enqueue(pFile, pRowRef);
            }
        }
    }

    return pReturn;
}

void
ElissoFolderView::onThumbnailReady()
{
    FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();

    auto pThumbnail = _pImpl->thumbnailer.fetchResult();
    Gtk::TreePath path = pThumbnail->pRowRef->get_path();
    auto it = _pImpl->pListStore->get_iter(path);
    Gtk::TreeModel::Row row = *it;
    row[cols._colIconSmall] = pThumbnail->ppbIconSmall;
    row[cols._colIconBig] = pThumbnail->ppbIconBig;

}

// PPixbuf
// ElissoFolderView::cellDataFuncIcon(const Gtk::TreeModel::iterator& it,
//                                    Gtk::TreeModelColumn<PPixbuf> &column,
//                                    int iconSize)
// {
//     FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();
//
//     Gtk::TreeModel::Row row = *it;
//     PPixbuf pb1 = row[column];
//     if (!pb1)
//     {
//         PFSModelBase pFS = row[cols._colPFile];
//         row[column] = loadIcon(it, pFS, iconSize);
//     }
//
//     return pb1;
// }

const std::string
    strSuffixEB = " EB",
    strSuffixPB = " PB",
    strSuffixTB = " TB",
    strSuffixGB = " GB",
    strSuffixMB = " MB",
    strSuffixKB = " KB",
    strSuffixB = " bytes";

Glib::ustring formatBytes(uint64_t u)
{
    const std::string *pSuffix = &strSuffixB;
    double readable;
    if (u >= 0x1000000000000000) // Exabyte
    {
        pSuffix = &strSuffixEB;
        readable = (u >> 50);
    }
    else if (u >= 0x4000000000000) // Petabyte
    {
        pSuffix = &strSuffixPB;
        readable = (u >> 40);
    }
    else if (u >= 0x10000000000) // Terabyte
    {
        pSuffix = &strSuffixTB;
        readable = (u >> 30);
    }
    else if (u >= 0x40000000) // Gigabyte
    {
        pSuffix = &strSuffixGB;
        readable = (u >> 20);
    }
    else if (u >= 0x100000) // Megabyte
    {
        pSuffix = &strSuffixMB;
        readable = (u >> 10);
    }
    else if (u >= 0x400) // Kilobyte
    {
        pSuffix = &strSuffixKB;
        readable = u;
    }
    else
    {
        return to_string(u) + strSuffixB;
    }
    // Divide by 1024 to get fractional value.
    readable = (readable / 1024);
    // Return formatted number with suffix
    return Glib::ustring::format(std::fixed, std::setprecision(2), readable) + *pSuffix;
}

void
ElissoFolderView::setIconViewColumns()
{
//     FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();

//     _iconView.set_cell_data_func(_cellRendererIconSmall,
//                                  [this, &cols](const Gtk::TreeModel::iterator& it)
//     {
//         this->cellDataFuncIcon(it, cols._colIconSmall, ICON_SIZE_SMALL);
//     });
//
//     _iconView.set_cell_data_func(_cellRendererIconBig,
//                                  [this, &cols](const Gtk::TreeModel::iterator& it)
//     {
//         this->cellDataFuncIcon(it, cols._colIconBig, ICON_SIZE_BIG);
//     });
}

void
ElissoFolderView::setListViewColumns()
{
    int i;
    Gtk::TreeView::Column* pColumn;

    int aSizes[4] = { 40, 40, 40, 40 };
    auto s = _mainWindow.getApplication().getSettingsString(SETTINGS_LIST_COLUMN_WIDTHS);
    StringVector sv = explodeVector(s, ",");
    if (sv.size() == 4)
    {
        int i = 0;
        for (auto &s : sv)
        {
            int v = stoi(s);
            if (v)
                aSizes[i] = v;
            i++;
        }
    }

    FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();

    // "Icon" column with our own cell renderer. Do not use the overload with a column
    // number since that would create a cell renderer automatically.
    i = _treeView.append_column("Icon", _cellRendererIconSmall);
//     i = _treeView.append_column("Icon", cols._colIconSmall);
    if ((pColumn = _treeView.get_column(i - 1)))
    {
        pColumn->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
        pColumn->set_fixed_width(aSizes[i - 1]);
        pColumn->set_cell_data_func(_cellRendererIconSmall,
                                    [this, &cols](Gtk::CellRenderer*,
                                                  const Gtk::TreeModel::iterator& it)
        {
            Gtk::TreeModel::Row row = *it;
            PPixbuf pb1 = row[cols._colIconSmall];
//             auto pPixbuf = this->cellDataFuncIcon(it, cols._colIconSmall, ICON_SIZE_SMALL);
            _cellRendererIconSmall.property_pixbuf() = pb1;
        });
    }

    i = _treeView.append_column("Name", cols._colFilename);
    if ((pColumn = _treeView.get_column(i - 1)))
    {
        pColumn->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
        pColumn->set_fixed_width(aSizes[i - 1]);
        pColumn->set_resizable(true);
        pColumn->set_sort_column(cols._colFilename);
    }

    // "Size" column with our own cell renderer.
    i = _treeView.append_column("Size", _cellRendererSize);
    if ((pColumn = _treeView.get_column(i - 1)))
    {
        pColumn->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
        pColumn->set_fixed_width(aSizes[i - 1]);
        pColumn->set_resizable(true);
        pColumn->set_sort_column(cols._colSize);
        pColumn->set_cell_data_func(_cellRendererSize,
                                    [this, &cols](Gtk::CellRenderer* pRend,
                                                  const Gtk::TreeModel::iterator& it)
        {
            Gtk::TreeModel::Row row = *it;
            PFSModelBase pFS = row[cols._colPFile];
            Glib::ustring str;
            if (pFS->getType() == FSType::FILE)
            {
                str = formatBytes(row[cols._colSize]);
                (static_cast<Gtk::CellRendererText*>(pRend))->property_xalign() = 1.0;
            }
            (static_cast<Gtk::CellRendererText*>(pRend))->property_text() = str;
        });
    }

    i = _treeView.append_column("Type", cols._colTypeString);
    if ((pColumn = _treeView.get_column(i - 1)))
    {
        pColumn->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
        pColumn->set_fixed_width(aSizes[i - 1]);
        pColumn->set_resizable(true);
        pColumn->set_sort_column(cols._colTypeString);
    }

    _treeView.set_fixed_height_mode(true);
}

size_t
ElissoFolderView::getSelection(FileSelection &sel)
{
    auto pSelection = _treeView.get_selection();
    if (pSelection)
    {
        auto v = pSelection->get_selected_rows();
        if (v.size())
        {
            FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();

            for (auto &path : v)
            {
                Gtk::TreeModel::iterator iter = _pImpl->pListStore->get_iter(path);
                if (iter)
                {
                    Gtk::TreeModel::Row row = *iter;
                    PFSModelBase pFS = row[cols._colPFile];
                    if (pFS)
                    {
                        sel.llAll.push_back(pFS);
                        if (pFS->isDirectoryOrSymlinkToDirectory())
                            sel.llFolders.push_back(pFS);
                        else
                            sel.llOthers.push_back(pFS);
                    }
                }
            }
        }
    }

//     for (auto &p : sel.llFolders)
//         Debug::Log(DEBUG_ALWAYS, "Selected folder: " + p->getRelativePath());
//     for (auto &p : sel.llOthers)
//         Debug::Log(DEBUG_ALWAYS, "Selected file: " + p->getRelativePath());

    return sel.llAll.size();
}

/**
 *  Shared event handler between the icon view and the tree view for double clicks.
 */
void
ElissoFolderView::onPathActivated(const Gtk::TreeModel::Path &path)
{
    Gtk::TreeModel::iterator iter = _pImpl->pListStore->get_iter(path);
    Gtk::TreeModel::Row row = *iter;
    FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();
    PFSModelBase pFS = row[cols._colPFile];
    if (pFS)
    {
        Debug::Log(FOLDER_POPULATE_HIGH, string(__func__) + "(\"" + pFS->getRelativePath() + "\")");
        this->openFile(pFS, {});
    }
}

void
ElissoFolderView::onSelectionChanged()
{
    // Only get the folder selection if we're fully populated and no file operations are going on.
    if (    (_state == ViewState::POPULATED)
         && (!_pImpl->llFileOperations.size())
       )
    {
        FileSelection sel;
        this->getSelection(sel);
        _mainWindow.enableEditActions(sel.llFolders.size(), sel.llOthers.size());
    }
}


/***************************************************************************
 *
 *  FolderViewMonitor
 *
 **************************************************************************/

/* virtual */
void
FolderViewMonitor::onItemRemoved(PFSModelBase &pFS) /* override; */
{
    _view.removeFile(pFS);
}

/* virtual */
void
FolderViewMonitor::onDirectoryAdded(PFSDirectory &pDir) /* override*/
{
    Debug::Log(FILE_HIGH,  "Directory added to " + _view._pDir->getRelativePath() + ": " + pDir->getRelativePath());

    _view.insertFile(pDir);
}
