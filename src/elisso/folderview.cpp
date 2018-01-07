/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
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
#include "elisso/populate.h"
#include "xwp/except.h"
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
 *  ElissoFolderView::Impl (private)
 *
 **************************************************************************/

struct ElissoFolderView::Impl : public ProhibitCopy
{
    PPopulateThread                 pPopulateThread;      // only set while state == POPULATING or REFRESHING
    uint                            idCurrentPopulateThread = 0;

    // GUI thread dispatcher for when a folder populate is done.
    PViewPopulatedWorker            pWorkerPopulated;
    sigc::connection                connWorker;

    PFSVector                       pllFolderContents;      // This includes hidden items.
    Glib::RefPtr<Gtk::ListStore>    pListStore;             // The model, without hidden items.
    size_t                          cFolders,
                                    cFiles,
                                    cImageFiles,
                                    cTotal;

    Glib::RefPtr<Gtk::IconTheme>    pIconTheme;

    PFolderViewMonitor              pMonitor;

    Thumbnailer                     thumbnailer;
    uint                            cToThumbnail;
    uint                            cThumbnailed;
    sigc::connection                connThumbnailProgressTimer;

    sigc::connection                connSelectionChanged;       // needs to be disconnected in destructor

    // This is a map which allows us to look up rows quickly for efficient removal of items by name.
    std::map<std::string, Gtk::TreeRowReference> mapRowReferences;

    // Clipboard buffer filled by copy/cut
    vector<Glib::ustring>           vURIs;

    Impl()
        : pWorkerPopulated(make_shared<ViewPopulatedWorker>()),
          pIconTheme(Gtk::IconTheme::get_default())
    { }

    ~Impl()
    {
        // Disconnect the populated worker in case something is still in the populate queue.
        connWorker.disconnect();
        // Just in case the thumbnailer is running.
        connThumbnailProgressTimer.disconnect();

        // Disconnect the "selection changed" signal or we might get crashes.
        connSelectionChanged.disconnect();
    }

    void clearModel()
    {
        pListStore->clear();
        mapRowReferences.clear();
        pllFolderContents = nullptr;
        cFolders = 0;
        cFiles = 0;
        cImageFiles = 0;
        cTotal = 0;
    }
};


/***************************************************************************
 *
 *  ElissoFolderView
 *
 **************************************************************************/

ElissoFolderView::ElissoFolderView(ElissoApplicationWindow &mainWindow, int &iPageInserted)
    : Gtk::Overlay(),
      _id(g_uViewID++),
      _mainWindow(mainWindow),
      _pImpl(new ElissoFolderView::Impl())
{
    auto &ntb = _mainWindow.getNotebook();
    iPageInserted = ntb.append_page(*this,
                                    _labelNotebookPage,
                                    _labelNotebookMenu);

    _treeView.setParent(mainWindow, TreeViewPlusMode::IS_FOLDER_CONTENTS_RIGHT);

    // Create the monitor. We need the this pointer so we can't do it in the Impl constructor.
    _pImpl->pMonitor = make_shared<FolderViewMonitor>(*this);

    // Allow multiple selections.
    _treeView.get_selection()->set_mode(Gtk::SELECTION_MULTIPLE);
    _iconView.property_selection_mode() = Gtk::SELECTION_MULTIPLE;

    // Connect the GUI thread dispatcher for when a folder populate is done.
    _pImpl->connWorker = _pImpl->pWorkerPopulated->connect([this]()
    {
        auto p = _pImpl->pWorkerPopulated->fetchResult();
        this->onPopulateDone(p);
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
    _pImpl->connSelectionChanged = _iconView.signal_selection_changed().connect([this]()
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

    Debug::Log(WINDOWHIERARCHY, "this->add(_scrolledWindow);");
    this->add(_scrolledWindow);
    _scrolledWindow.show();
}

/* virtual */
ElissoFolderView::~ElissoFolderView()
{
    Debug::Log(CMD_TOP, "~ElissoFolderView");
    delete _pImpl;
}

bool
ElissoFolderView::setDirectory(PFSModelBase pDirOrSymlinkToDir,
                               SetDirectoryFlagSet fl)
{
    bool rc = false;

    // Remember previous directory so we can try scrolling to and selecting it.
    PFSModelBase pDirSelectPrevious;
    if (fl.test(SetDirectoryFlag::SELECT_PREVIOUS))
        pDirSelectPrevious = _pDir;

    switch (_state)
    {
        case ViewState::ERROR:
        case ViewState::UNDEFINED:
        case ViewState::POPULATED:
        case ViewState::INSERTING:
        break;

        case ViewState::POPULATING:
        case ViewState::REFRESHING:
            // stop the populate thread
            if (_pImpl->pPopulateThread)
            {
                Debug::Log(FOLDER_POPULATE_HIGH, "already populating, stopping other populate thread");
                _pImpl->pPopulateThread->stop();
            }
        break;
    }

    // Container is only != NULL if this is a directory or a symlink to one.
    FSContainer *pContainer;
    if ((pContainer = pDirOrSymlinkToDir->getContainer()))
    {
        if (fl.test(SetDirectoryFlag::IS_REFRESH))
        {
            if (_pDir != pDirOrSymlinkToDir)
                throw FSException("Cannot change directory when refreshing");
        }
        else
            _pDir = pDirOrSymlinkToDir;

        // Push the new path into the history.
        if (fl.test(SetDirectoryFlag::PUSH_TO_HISTORY))
        {
            Debug::Log(FOLDER_STACK, string(__func__) + "(): SetDirectoryFlag::PUSH_TO_HISTORY is set: pushing new " + _pDir->getPath());
            if (_pDir)
            {
                auto strFull = _pDir->getPath();
                // Do not push if this is the same as the last item on the stack.
                if (    (!_aPathHistory.size())
                     || (_aPathHistory.back() != strFull)
                   )
                {
                    // Before pushing back, cut off the stack if the user has pressed "back" at least once.
                    if (_uPathHistoryOffset > 0)
                    {
                        Debug::Log(FOLDER_STACK, "  cutting off history before pushing new item, old:");
                        this->dumpStack();
                        auto itFirstToDelete = _aPathHistory.begin() + (_aPathHistory.size() - _uPathHistoryOffset);
                        _aPathHistory.erase(itFirstToDelete, _aPathHistory.end());
                        Debug::Log(FOLDER_STACK, "  new:");
                        this->dumpStack();
                    }

                    _aPathHistory.push_back(strFull);
                }

                _uPathHistoryOffset = 0;
            }
        }
        else
            Debug::Log(FOLDER_STACK, string(__func__) + "(): SetDirectoryFlag::PUSH_TO_HISTORY is NOT set");

        // Change view state early to avoid "selection changed" signals overflowing us.
        if (fl.test(SetDirectoryFlag::IS_REFRESH))
            this->setState(ViewState::REFRESHING);
        else
            this->setState(ViewState::POPULATING);

        // If we're currently displaying an error, remove it.
        if (_mode == FolderViewMode::ERROR)
            setViewMode(_modeBeforeError);

        // Remove all old data, if any.
        if (!(fl.test(SetDirectoryFlag::IS_REFRESH)))
            _pImpl->clearModel();

        _pImpl->thumbnailer.clearQueues();

        auto pWatching = _pImpl->pMonitor->isWatching();
        if (pWatching)
            _pImpl->pMonitor->stopWatching(*pWatching);

        Debug::Log(FOLDER_POPULATE_HIGH, "POPULATING LIST \"" + _pDir->getPath() + "\"");

        _pImpl->pPopulateThread = PopulateThread::Create(this->_pDir,
                                                         this->_pImpl->pWorkerPopulated,
                                                         fl.test(SetDirectoryFlag::CLICK_FROM_TREE),
                                                         pDirSelectPrevious);
        _pImpl->idCurrentPopulateThread = _pImpl->pPopulateThread->getID();

        rc = true;

        dumpStack();

        Glib::ustring strFree;
        PGioFile pGioFile;
        if (    (_pDir)
             && ((pGioFile = _pDir->getGioFile()))
           )
        {
            try
            {
                Glib::RefPtr<Gio::FileInfo> pInfo = pGioFile->query_filesystem_info("*");
                auto z = pInfo->get_attribute_uint64("filesystem::free");
                strFree = formatBytes(z) + " free";
            }
            catch (...)
            {
            }
        }
        _mainWindow.setStatusbarFree(strFree);
    }
    else
    {
        if (pDirOrSymlinkToDir)
            this->setError("The given file " + quote(pDirOrSymlinkToDir->getPath()) + " is not a folder");
        else
            this->setError("The given file does not exist");
    }

    _mainWindow.enableBackForwardActions();

    return rc;
}

void
ElissoFolderView::onPopulateDone(PViewPopulatedResult pResult)
{
    if (!pResult->strError.empty())
        this->setError(pResult->strError);
    else if (pResult->idPopulateThread != _pImpl->idCurrentPopulateThread)
        // When a populate thread gets stopped prematurely because a user clicked
        // on another folder while a populate was still going on, we can end up
        // with a populate result for the previous folder, which we should simply
        // discard.
        ;
    else
    {
        Debug::Log(FOLDER_POPULATE_LOW, "ElissoFolderView::onPopulateDone(\"" + _pDir->getPath() + "\", id=" + to_string(pResult->idPopulateThread) + ")");

        Gtk::ListStore::iterator itSelect;

        _pImpl->pllFolderContents = pResult->pvContents;

        bool fRefreshing = _state == ViewState::REFRESHING;

        // This sets the wait cursor.
        this->setState(ViewState::INSERTING);

        // Reset the thumbnailer count for the progress bar. insertFile() increments it for each image file.
        _pImpl->cToThumbnail = 0;
        _pImpl->cThumbnailed = 0;

        // If we're refreshing, we only insert newly added files to avoid duplicates.
        FSVector &vFiles = (fRefreshing) ? pResult->vAdded : *_pImpl->pllFolderContents;

        /*
         *  Insert all the files and collect some statistics.
         */
        for (auto &pFS : vFiles)
        {
            auto it = this->insertFile(pFS);
            if (it)
            {
                ++_pImpl->cTotal;

                if (pFS == pResult->pDirSelectPrevious)
                    itSelect = it;

                switch (pFS->getResolvedType())
                {
                    case FSTypeResolved::DIRECTORY:
                    case FSTypeResolved::SYMLINK_TO_DIRECTORY:
                        ++_pImpl->cFolders;
                    break;

                    case FSTypeResolved::FILE:
                    case FSTypeResolved::SYMLINK_TO_FILE:
                        ++_pImpl->cFiles;
                        if (_pImpl->thumbnailer.isImageFile(pFS))
                            ++_pImpl->cImageFiles;
                    break;

                    default:

                    break;
                }
            }
        }

        if (!fRefreshing)
        {
            // This does not yet connect the model, since we haven't set the state to populated yet.
            if (_pImpl->cImageFiles)
                this->setViewMode(FolderViewMode::ICONS);
            else
                this->setViewMode(FolderViewMode::LIST);
        }

        // This connects the model and also calls onFolderViewLoaded().
        this->setState(ViewState::POPULATED);

        _mainWindow.setWaitCursor(_iconView.get_window(), Cursor::DEFAULT);
        _mainWindow.setWaitCursor(_treeView.get_window(), Cursor::DEFAULT);

        if (!pResult->fClickFromTree)
            _mainWindow.selectInFolderTree(_pDir);

        // Focus the view (we may have switched views, and then the view
        // might still be hidden) and select and scroll an item in the
        // list if necessary.
        switch (_mode)
        {
            case FolderViewMode::ICONS:
            case FolderViewMode::COMPACT:
                if (itSelect)
                {
                    Gtk::TreeModel::Path path(itSelect);
                    _iconView.scroll_to_path(path, true, 0.5, 0.5);
                    _iconView.select_path(path);
                }
                // Grab focus (this is useful for "back" and "forward").
                if (!pResult->fClickFromTree)
                    _iconView.grab_focus();
            break;

            case FolderViewMode::LIST:
                if (itSelect)
                {
                    Gtk::TreeModel::Path path(itSelect);
                    _treeView.scroll_to_row(path, 0.5);
                    _treeView.get_selection()->select(path);
                }
                // Grab focus (this is useful for "back" and "forward").
                if (!pResult->fClickFromTree)
                    _treeView.grab_focus();
            break;

            case FolderViewMode::UNDEFINED:
            case FolderViewMode::ERROR:
            break;
        }

        // Release the populate data.
        this->_pImpl->pPopulateThread = nullptr;

        // Start watching.
        auto pCnr = _pDir->getContainer();
        if (pCnr)
        {
            // Make sure we have a monitor.
            auto pOther = _pImpl->pMonitor->isWatching();
            if (pOther)
                _pImpl->pMonitor->stopWatching(*pOther);
            _pImpl->pMonitor->startWatching(*pCnr);

            // Notify this and other monitors (tree view) of the items that have been removed
            // if this was a refresh.
            for (auto &pRemoved : pResult->vRemoved)
                pCnr->notifyFileRemoved(pRemoved);

        }

        // The folder view may have inserted a lot of icons and got the thumbnailer going.
        // If so, begin a timer to update the thumbnailer progress bar until it's done.
        Debug::Log(THUMBNAILER, "cToThumbnail: " + to_string(_pImpl->cToThumbnail));
        if (_pImpl->cToThumbnail)
        {
            _mainWindow.setThumbnailerProgress(0, _pImpl->cToThumbnail, ShowHideOrNothing::SHOW);
            _pImpl->connThumbnailProgressTimer = Glib::signal_timeout().connect([this]() -> bool
            {
                Debug::Log(THUMBNAILER, "cThumbnailed: " + to_string(_pImpl->cThumbnailed));
                if (!_pImpl->thumbnailer.isBusy())
                {
                    _mainWindow.setThumbnailerProgress(_pImpl->cToThumbnail, _pImpl->cToThumbnail, ShowHideOrNothing::HIDE);
                    return false; // disconnect
                }

                _mainWindow.setThumbnailerProgress(_pImpl->cThumbnailed, _pImpl->cToThumbnail, ShowHideOrNothing::DO_NOTHING);
                return true; // keep going
            }, 300);
        }
    }
}

Gtk::ListStore::iterator
ElissoFolderView::insertFile(PFSModelBase pFS)
{
    Debug::Log(FOLDER_POPULATE_LOW, "ElissoFolderView::insertFile(" + quote(pFS->getPath()) + ")");

    Gtk::ListStore::iterator it;

    if (!pFS->isHidden())
    {
        const FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();

        it = _pImpl->pListStore->append();
        auto row = *it;

        // pFile must always be set first because the sort function relies on it;
        // the sort function gets triggered AS SOON AS cols._colFilename is set.
        const std::string &strBasename = pFS->getBasename();
        row[cols._colPFile] = pFS;
        row[cols._colFilename] = strBasename;
        row[cols._colSize] = pFS->getFileSize();
        bool fThumbnailing = false;
        row[cols._colIconSmall] = loadIcon(it, pFS, ICON_SIZE_SMALL, &fThumbnailing);
        row[cols._colIconBig] = loadIcon(it, pFS, ICON_SIZE_BIG, nullptr);

        if (fThumbnailing)
            ++_pImpl->cToThumbnail;

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
            case FSTypeResolved::SYMLINK_TO_OTHER: pstrType = &TYPE_LINK_TO_OTHER; break;
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
ElissoFolderView::removeFile(PFSModelBase pFS)
{
    string strBasename = pFS->getBasename();
    // Look up the row reference in the map to quickly get to the row.
    auto itSTL = _pImpl->mapRowReferences.find(strBasename);
    if (itSTL != _pImpl->mapRowReferences.end())
    {
        auto &rowref= itSTL->second;
        Gtk::TreePath path = rowref.get_path();
        if (path)
        {
            auto itModel = _pImpl->pListStore->get_iter(path);
            if (itModel)
                _pImpl->pListStore->erase(itModel);
        }
    }
}

void
ElissoFolderView::renameFile(PFSModelBase pFS, const std::string &strOldName, const std::string &strNewName)
{
    // Look up the row reference in the map to quickly get to the row.
    auto itSTL = _pImpl->mapRowReferences.find(strOldName);
    if (itSTL != _pImpl->mapRowReferences.end())
    {
        auto rowref= itSTL->second;
        Gtk::TreePath path = rowref.get_path();
        if (path)
        {
            auto itModel = _pImpl->pListStore->get_iter(path);
            auto row = *itModel;
            FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();
            row[cols._colFilename] = strNewName;
        }

        _pImpl->mapRowReferences.erase(itSTL);
        _pImpl->mapRowReferences[strNewName] = rowref;
    }
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
            this->setDirectory(_pDir, SetDirectoryFlag::IS_REFRESH);     // but do not push to history
        }
    }
}

bool
ElissoFolderView::canGoBack()
{
    return (_uPathHistoryOffset + 1 < _aPathHistory.size());
}

bool
ElissoFolderView::goBack()
{
    if (this->canGoBack())
    {
        ++_uPathHistoryOffset;
        std::string strPrevious = _aPathHistory[_aPathHistory.size() - _uPathHistoryOffset - 1];
        PFSModelBase pDir;
        if ((pDir = FSModelBase::FindPath(strPrevious)))
            if (this->setDirectory(pDir,
                                   SetDirectoryFlag::SELECT_PREVIOUS))     // but do not push to history
                return true;
    }

    return false;
}

bool
ElissoFolderView::canGoForward()
{
    return (_uPathHistoryOffset > 0);
}

bool
ElissoFolderView::goForward()
{
    if (this->canGoForward())
    {
        Debug::Log(FOLDER_STACK, string(__func__) + "(): _aPathHistory.size()=" + to_string(_aPathHistory.size()) + ", _uPathHistoryOffset=" + to_string(_uPathHistoryOffset));
        std::string strPrevious = _aPathHistory[_aPathHistory.size() - _uPathHistoryOffset--];
        Debug::Log(FOLDER_STACK, " --> " + strPrevious);
        PFSModelBase pDir;
        if ((pDir = FSModelBase::FindPath(strPrevious)))
            if (this->setDirectory(pDir, {}))     // do not push to history
                return true;
    }

    return false;
}

void ElissoFolderView::setNotebookTabTitle()
{
    Glib::ustring strTitle("Error");
    if (_pDir)
    {
        strTitle = _pDir->getBasename();
        // set_max_width_chars doesn't work with ELLIPSIZE_MIDDLE so do it like this.
        uint maxChars = strTitle.length();
        if (maxChars < 5)
            maxChars = 5;
        else if (maxChars > 50)
            maxChars = 50;
        _labelNotebookPage.set_width_chars(maxChars);
        _labelNotebookPage.set_ellipsize(Pango::EllipsizeMode::ELLIPSIZE_MIDDLE);
        _labelNotebookPage.set_text(strTitle);
        _mainWindow.getNotebook().set_tab_label(*this,
                                                _labelNotebookPage);
        _labelNotebookMenu.set_text(strTitle);
    }
}

/**
 *  Sets a new state for the view.
 */
void
ElissoFolderView::setState(ViewState s)
{
    if (s != _state)
    {
        if (    (_state == ViewState::POPULATING)
             || (_state == ViewState::REFRESHING)
           )
        {
            delete _pLoading;
            _pLoading = nullptr;
        }

        switch (s)
        {
            case ViewState::POPULATING:
                // Disconnect model, disable sorting to speed up inserting lots of rows.
                this->connectModel(false);
                // Fall though!

            case ViewState::REFRESHING:
            {
                this->setNotebookTabTitle();

                _pLoading = Gtk::manage(new Gtk::EventBox);
                auto pLabel = Gtk::manage(new Gtk::Label());
                pLabel->set_markup("<big><b>Loading" + HELLIP + "</b></big> ");
                auto pSpinner = Gtk::manage(new Gtk::Spinner);
                pSpinner->set_size_request(32, 32);
                auto pBox = Gtk::manage(new Gtk::Box());
                pBox->pack_start(*pLabel);
                pBox->pack_start(*pSpinner);
                _pLoading->add(*pBox);
                _pLoading->property_margin_left() = 30;
                _pLoading->property_margin_top() = 40;
                _pLoading->property_halign() = Gtk::Align::ALIGN_START;
                _pLoading->property_valign() = Gtk::Align::ALIGN_START;

                this->add_overlay(*_pLoading);
                _pLoading->show_all();
                pSpinner->start();

                this->setWaitCursor(Cursor::WAIT_PROGRESS);

                _mainWindow.onLoadingFolderView(*this);
                        // this sets the status bar text
            }
            break;

            case ViewState::INSERTING:
                this->setWaitCursor(Cursor::WAIT_BLOCKED);
            break;

            case ViewState::POPULATED:
            {
                // Connect model again, set sort.
                this->connectModel(true);

                this->setWaitCursor(Cursor::DEFAULT);

                _mainWindow.onFolderViewLoaded(*this);

                this->updateStatusbar(nullptr);
            }
            break;

            case ViewState::ERROR:
                _mainWindow.getNotebook().set_tab_label_text(*this, "Error");
                _mainWindow.onFolderViewLoaded(*this);
            break;

            case ViewState::UNDEFINED:
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
    Debug d(WINDOWHIERARCHY, string(__func__) + "(" + to_string((int)m) + ")");

    if (m != _mode)
    {
        Debug::Log(WINDOWHIERARCHY, "old mode=" + to_string((int)_mode));

        switch (_mode)
        {
            case FolderViewMode::ICONS:
            case FolderViewMode::COMPACT:
                _iconView.hide();
                _scrolledWindow.remove();       // Remove the one contained widget.
            break;

            case FolderViewMode::LIST:
                _treeView.hide();
                _scrolledWindow.remove();       // Remove the one contained widget.
            break;

//                 _compactView.hide();
//                 this->remove();
//             break;
//
            case FolderViewMode::ERROR:
                _infoBarError.hide();
                _scrolledWindow.remove();       // Remove the one contained widget.
            break;

            case FolderViewMode::UNDEFINED:
            break;
        }

        switch (m)
        {
            case FolderViewMode::ICONS:
            case FolderViewMode::COMPACT:
            {
                _scrolledWindow.add(_iconView);
                _iconView.show();

                FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();
                if (m == FolderViewMode::ICONS)
                {
                    // icon
                    _iconView.set_item_orientation(Gtk::Orientation::ORIENTATION_VERTICAL);
                    _iconView.set_pixbuf_column(cols._colIconBig);
                    _iconView.set_text_column(cols._colFilename);
                    _iconView.set_item_width(100);

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
                    _iconView.set_margin(0);
                    _iconView.set_row_spacing(1);
                    _iconView.set_column_spacing(5);
                    _iconView.set_item_padding(5);
                }
            }
            break;

            case FolderViewMode::LIST:
                _scrolledWindow.add(_treeView);
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
                static bool fAdded = false;
                if (!fAdded)
                {
                    auto pInfoBarContainer = dynamic_cast<Gtk::Container*>(_infoBarError.get_content_area());
                    if (pInfoBarContainer)
                        pInfoBarContainer->add(_infoBarLabel);
                    fAdded = true;
                }
                _scrolledWindow.add(_infoBarError);
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
//     Debug::Log(DEBUG_ALWAYS, std::string(__FUNCTION__) + "(): " + strError);
    _strError = strError;
    setState(ViewState::ERROR);
    setViewMode(FolderViewMode::ERROR);
}

void
ElissoFolderView::updateStatusbar(FileSelection *pSel)
{
    Glib::ustring str;
    if (_pImpl->pllFolderContents)
    {
        if (_pImpl->cTotal)
        {
            str = formatNumber(_pImpl->cTotal) + " items in folder";
            uint64_t z = 0;
            FSVector *pList = nullptr;

            if (pSel && pSel->vAll.size())
            {
                if (pSel->vAll.size() == 1)
                    str += ", " + quote(pSel->vAll.front()->getBasename()) + " selected";
                else
                    str += ", " + formatNumber(pSel->vAll.size()) + " selected";

                if (pSel->vOthers.size())
                    pList = &pSel->vOthers;
            }
            else if (_pImpl->pllFolderContents)
                pList = &(*_pImpl->pllFolderContents);

            if (pList)
            {
                PFSFile pFile;
                for (auto &pFS : *pList)
                {
                    if ((pFile = pFS->getFile()))
                        z += pFile->getFileSize();
                }
                str += " (" + formatBytes(z) + ")";
            }
        }
        else
            str = "Folder is empty";
    }

    _mainWindow.setStatusbarCurrent(str);
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
        if (sel.vFolders.size() == 1)
            return sel.vFolders.front();

    return nullptr;
}

/**
 *  Fills the given FileSelection object with the selected items from the
 *  active icon or tree view. Returns the total no. of item selected.
 */
size_t
ElissoFolderView::getSelection(FileSelection &sel)
{
    std::vector<Gtk::TreePath> vPaths;

    switch (_mode)
    {
        case FolderViewMode::LIST:
        {
            auto pSelection = _treeView.get_selection();
            if (pSelection)
                vPaths = pSelection->get_selected_rows();
        }
        break;

        case FolderViewMode::ICONS:
        case FolderViewMode::COMPACT:
            vPaths = _iconView.get_selected_items();
        break;

        case FolderViewMode::UNDEFINED:
        case FolderViewMode::ERROR:
        break;
    }

    if (vPaths.size())
    {
        FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();

        for (auto &path : vPaths)
        {
            Gtk::TreeModel::iterator iter = _pImpl->pListStore->get_iter(path);
            if (iter)
            {
                Gtk::TreeModel::Row row = *iter;
                PFSModelBase pFS = row[cols._colPFile];
                if (pFS)
                {
                    sel.vAll.push_back(pFS);
                    if (pFS->isDirectoryOrSymlinkToDirectory())
                        sel.vFolders.push_back(pFS);
                    else
                        sel.vOthers.push_back(pFS);
                }
            }
        }
    }

    return sel.vAll.size();
}

MouseButton3ClickType
ElissoFolderView::handleClick(GdkEventButton *pEvent,
                              Gtk::TreeModel::Path &path)
{
    MouseButton3ClickType clickType = MouseButton3ClickType::WHITESPACE;

    // Figure out if the click was on a row or whitespace, and which row if any.
    // There are two variants for this call -- but the more verbose one with a column returns
    // a column always even if the user clicks on the whitespace to the right of the column
    // so there's no point.
    if (this->getPathAtPos((int)pEvent->x,
                           (int)pEvent->y,
                           path))
    {
        // Click on a row with mouse button 3: in list mode, figure out if it's selected
        if (this->isSelected(path))
        {
            // Click on row that's selected: then show context even if it's whitespace.
//                             Debug::Log(DEBUG_ALWAYS, "row is selected");
            if (this->countSelectedItems() == 1)
                clickType = MouseButton3ClickType::SINGLE_ROW_SELECTED;
            else
                clickType = MouseButton3ClickType::MULTIPLE_ROWS_SELECTED;
        }
        else
        {
//                             Debug::Log(DEBUG_ALWAYS, "row is NOT selected");
            if (    (this->_mode != FolderViewMode::LIST)
                 || (!_treeView.is_blank_at_pos((int)pEvent->x, (int)pEvent->y))
               )
            {
                selectExactlyOne(path);
                clickType = MouseButton3ClickType::SINGLE_ROW_SELECTED;
            }
        }
    }

    return clickType;
}

/**
 *  Called from ElissoApplicationWindow::handleViewAction() to handle
 *  those actions that operate on the current folder view or the
 *  files therein.
 *
 *  Some of these are asynchronous, most are not.
 */
void
ElissoFolderView::handleAction(FolderAction action)
{
    try
    {
        switch (action)
        {
            case FolderAction::EDIT_COPY:
                clipboardCopyOrCutSelected(false);       // not cut
            break;

            case FolderAction::EDIT_CUT:
                clipboardCopyOrCutSelected(true);        // cut
            break;

            case FolderAction::EDIT_PASTE:
                clipboardPaste();        // cut
            break;

            case FolderAction::EDIT_SELECT_ALL:
                selectAll();
            break;

            case FolderAction::EDIT_OPEN_SELECTED:
                _mainWindow.openFile(nullptr, {});
            break;

            case FolderAction::FILE_CREATE_FOLDER:
                createSubfolderDialog();
            break;

            case FolderAction::FILE_CREATE_DOCUMENT:
                createEmptyFileDialog();
            break;

            case FolderAction::EDIT_RENAME:
                renameSelected();
            break;

            case FolderAction::EDIT_TRASH:
                trashSelected();
            break;

#ifdef USE_TESTFILEOPS
            case FolderAction::EDIT_TEST_FILEOPS:
                testFileopsSelected();
            break;
#endif

            case FolderAction::VIEW_ICONS:
                setViewMode(FolderViewMode::ICONS);
            break;

            case FolderAction::VIEW_LIST:
                setViewMode(FolderViewMode::LIST);
            break;

            case FolderAction::VIEW_COMPACT:
                setViewMode(FolderViewMode::COMPACT);
            break;

            case FolderAction::VIEW_REFRESH:
                refresh();
            break;

            case FolderAction::GO_BACK:
                goBack();
            break;

            case FolderAction::GO_FORWARD:
                goForward();
            break;

            case FolderAction::GO_PARENT:
            {
                PFSModelBase pDir = _pDir->getParent();
                if (pDir)
                {
                    setDirectory(pDir,  SetDirectoryFlag::SELECT_PREVIOUS | SetDirectoryFlag::PUSH_TO_HISTORY);
                }
            }
            break;

            case FolderAction::GO_HOME:
            {
                auto pHome = FSDirectory::GetHome();
                if (pHome)
                    setDirectory(pHome, SetDirectoryFlag::PUSH_TO_HISTORY);
            }
            break;

            case FolderAction::GO_COMPUTER:
            {
                auto pTrash = RootDirectory::Get("computer");
                if (pTrash)
                    setDirectory(pTrash, SetDirectoryFlag::PUSH_TO_HISTORY);
            }
            break;

            case FolderAction::GO_TRASH:
            {
                auto pTrash = RootDirectory::Get("trash");
                if (pTrash)
                    setDirectory(pTrash, SetDirectoryFlag::PUSH_TO_HISTORY);
            }
            break;
        }
    }
    catch (FSException &e)
    {
        _mainWindow.errorBox(e.what());
    }
}

void
ElissoFolderView::clipboardCopyOrCutSelected(bool fCut)
{
    FileSelection sel;
    if (getSelection(sel))
    {
        _pImpl->vURIs.clear();
        for (auto &pFS : sel.vAll)
            _pImpl->vURIs.push_back(pFS->getPath());

        vector<Gtk::TargetEntry> vTargets;
        vTargets.push_back(Gtk::TargetEntry(CLIPBOARD_TARGET_GNOME_COPIED_FILES));     // TODO others?
        vTargets.push_back(Gtk::TargetEntry(CLIPBOARD_TARGET_UTF8_STRING));

        auto pCB = Gtk::Clipboard::get();
        pCB->set(   vTargets,
                    [this, fCut](Gtk::SelectionData &selectionData, guint /* info */)
                    {
                        if (selectionData.get_target() == CLIPBOARD_TARGET_GNOME_COPIED_FILES)
                        {
                            // Format info courtesy of http://stackoverflow.com/questions/7339084/gtk-clipboard-copy-cut-paste-files
                            selectionData.set(CLIPBOARD_TARGET_GNOME_COPIED_FILES,
                                              Glib::ustring(fCut ? "cut" : "copy") + "\n" + implode("\n", _pImpl->vURIs));
                        }
                        else if (selectionData.get_target() == CLIPBOARD_TARGET_UTF8_STRING)
                            selectionData.set(CLIPBOARD_TARGET_UTF8_STRING, implode(" ", _pImpl->vURIs));
                    },
                    [](){

                    });

        if (sel.vAll.size() == 1)
        {
            Glib::ustring str = quote(sel.vAll.front()->getBasename());
            if (fCut)
                _mainWindow.setStatusbarCurrent(str + " will be moved if you select the " + quote("Paste") + " command");
            else
                _mainWindow.setStatusbarCurrent(str + " will be copied if you select the " + quote("Paste") + " command");
        }
        else
        {
            if (fCut)
                _mainWindow.setStatusbarCurrent(formatNumber(sel.vAll.size()) + " items will be moved if you select the " + quote("Paste") + " command");
            else
                _mainWindow.setStatusbarCurrent(formatNumber(sel.vAll.size()) + " items will be copied if you select the " + quote("Paste") + " command");
        }
    }
}

/**
 *  Called from handleAction() to handle the "Paste" action.
 */
void ElissoFolderView::clipboardPaste()
{
    Debug d(CMD_TOP, __func__);
    Glib::RefPtr<Gtk::Clipboard> pClip = Gtk::Clipboard::get();
    pClip->request_contents(CLIPBOARD_TARGET_GNOME_COPIED_FILES, [this](const Gtk::SelectionData &selectionData)
    {
        try
        {
            FileOperationType fopType(FileOperationType::TEST);

            Glib::ustring data = selectionData.get_data_as_string();
            StringVector lines = explodeVector(data, "\n");
            FSVector vFiles;
            if (lines.size())
            {
                auto it = lines.begin();
                auto cmd = *it;
                if (cmd == "copy")
                    fopType = FileOperationType::COPY;
                else if (cmd == "cut")
                    fopType = FileOperationType::MOVE;
                else
                    throw FSException("Invalid command " + quote(cmd) + " in clipboard");

                ++it;
                while (it != lines.end())
                {
                    auto line = *it;
                    char *pszUnescaped = g_uri_unescape_string(line.c_str(), NULL);
                    if (!pszUnescaped)
                        throw FSException("Invalid file name in clipboard");
                    string strUnescaped(pszUnescaped);
                    g_free(pszUnescaped);
                    Debug::Log(CLIPBOARD, "getting file for " + quote(strUnescaped));
                    // This will throw if the path is invalid:
                    auto pFS = FSModelBase::FindPath(strUnescaped);
                    vFiles.push_back(pFS);
                    ++it;
                }
            }

            if (!vFiles.size())
                throw FSException("Did not find any files to copy in clipboard");
            if (fopType == FileOperationType::TEST)
                throw FSException("Invalid file operation in clipboard");

            _mainWindow.addFileOperation(fopType,
                                         vFiles,
                                         _pDir);
        }
        catch (exception &e)
        {
            _mainWindow.errorBox(e.what());
        }
    });
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
            if ((pNew = pContainer->createSubdirectory(s)))
                pContainer->notifyFileAdded(pNew);
        }
    }

    return pNew;
}

PFSFile
ElissoFolderView::createEmptyFileDialog()
{
    PFSFile pNew;

    FSContainer *pContainer = this->_pDir->getContainer();
    if (pContainer)
    {
        TextEntryDialog dlg(this->_mainWindow,
                            "Create empty document",
                            "Please enter the name of the new document file to be created in <b>" + this->_pDir->getBasename() + "</b>:",
                            "Create");
        if (dlg.run() == Gtk::RESPONSE_OK)
        {
            auto s = dlg.getText();
            if ((pNew = pContainer->createEmptyDocument(s)))
                pContainer->notifyFileAdded(pNew);
        }
    }

    return pNew;
}

void
ElissoFolderView::renameSelected()
{
    FileSelection sel;
    if (    (getSelection(sel))
         && (sel.vAll.size() == 1)
       )
    {
        FSContainer *pContainer = this->_pDir->getContainer();
        if (pContainer)
        {
            PFSModelBase pFile = sel.vAll.front();
            Glib::ustring strOld(pFile->getBasename());
            TextEntryDialog dlg(this->_mainWindow,
                                "Rename file",
                                "Please enter the new name for <b>" + strOld + "</b>:",
                                "Rename");
            dlg.setText(strOld);

            auto pos = strOld.rfind('.');
            dlg.selectRegion(0, pos);
            if (dlg.run() == Gtk::RESPONSE_OK)
            {
                string strNew = dlg.getText();
                pFile->rename(strNew);
                pContainer->notifyFileRenamed(pFile, strOld, strNew);
            }
        }
    }
    else
        _mainWindow.errorBox("Bad selection");
}

void
ElissoFolderView::trashSelected()
{
    FileSelection sel;
    if (getSelection(sel))
        _mainWindow.addFileOperation(FileOperationType::TRASH,
                                     sel.vAll,
                                     nullptr);
}

#ifdef USE_TESTFILEOPS
void
ElissoFolderView::testFileopsSelected()
{
    FileSelection sel;
    if (getSelection(sel))
        FileOperation::Create(FileOperation::Type::TEST,
                              sel.vAll,
                              nullptr,      // pTargetContainer
                              _pImpl->llFileOperations,
                              &_pImpl->pProgressDialog,
                              &_mainWindow);
}
#endif

/**
 *  Protected method which sets the wait cursor on the member view windows.
 */
void
ElissoFolderView::setWaitCursor(Cursor cursor)
{
    _mainWindow.setWaitCursor(_iconView.get_window(), cursor);
    _mainWindow.setWaitCursor(_treeView.get_window(), cursor);
}

void
ElissoFolderView::dumpStack()
{
    Debug::Log(FOLDER_STACK, string(__func__) + "(): size=" + to_string(_aPathHistory.size()) + ", offset=" + to_string(_uPathHistoryOffset));
    uint i = 0;
    for (auto &s : _aPathHistory)
        Debug::Log(FOLDER_STACK, "  stack item " + to_string(i++) + ": " + s);
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
                // Scroll to the top. Otherwise we end up at a random scroll position if the
                // icon view has been used before.
                Glib::RefPtr<Gtk::Adjustment> pVAdj = _iconView.get_vadjustment();
                if (pVAdj)
                    pVAdj->set_value(0);
//       gtk_adjustment_set_value (icon_view->priv->hadjustment,
//                                 gtk_adjustment_get_value (icon_view->priv->hadjustment) + offset);

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
                           int size,
                           bool *pfThumbnailing)
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
        // the thumbnailer to process in the background. This will create both sizes.
        if (pFile)
            if (!pFile->hasFlag(FSFlag::THUMBNAILING))
            {
                auto pFormat = _pImpl->thumbnailer.isImageFile(pFile);
                if (pFormat)
                {
                    pFile->setFlag(FSFlag::THUMBNAILING);
                    _pImpl->thumbnailer.enqueue(pFile, pFormat);

                    if (pfThumbnailing)
                        *pfThumbnailing = true;
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

    const auto &strName = pThumbnail->pFile->getBasename();
    auto itSTL = _pImpl->mapRowReferences.find(strName);
    if (itSTL != _pImpl->mapRowReferences.end())
    {
        auto rowref= itSTL->second;
        Gtk::TreePath path = rowref.get_path();
        if (path)
        {
            auto it = _pImpl->pListStore->get_iter(path);
            if (it)
            {
                Gtk::TreeModel::Row row = *it;
                row[cols._colIconSmall] = pThumbnail->ppbIconSmall;
                row[cols._colIconBig] = pThumbnail->ppbIconBig;
            }
        }
    }

    ++_pImpl->cThumbnailed;
}

/**
 *  Calls either Gtk::TreeView::get_path_at_pos() or Gtk::IconView::get_item_at_pos(),
 *  depending on which view is active.
 */
bool
ElissoFolderView::getPathAtPos(int x,
                               int y,
                               Gtk::TreeModel::Path &path)
{
    switch (_mode)
    {
        case FolderViewMode::LIST:
        {
            auto pSel = _treeView.get_selection();
            if (pSel)
                return _treeView.get_path_at_pos(x, y, path);
        }
        break;

        case FolderViewMode::ICONS:
        case FolderViewMode::COMPACT:
            return _iconView.get_item_at_pos(x, y, path);
        break;

        case FolderViewMode::ERROR:
        case FolderViewMode::UNDEFINED:
        break;
    }

    return false;
}

bool
ElissoFolderView::isSelected(Gtk::TreeModel::Path &path)
{
    switch (_mode)
    {
        case FolderViewMode::LIST:
        {
            auto pSel = _treeView.get_selection();
            if (pSel)
                return pSel->is_selected(path);
        }
        break;

        case FolderViewMode::ICONS:
        case FolderViewMode::COMPACT:
            return _iconView.path_is_selected(path);
        break;

        case FolderViewMode::ERROR:
        case FolderViewMode::UNDEFINED:
        break;
    }

    return false;
}

int ElissoFolderView::countSelectedItems()
{
    switch (_mode)
    {
        case FolderViewMode::LIST:
        {
            auto pSel = _treeView.get_selection();
            if (pSel)
                return pSel->count_selected_rows();
        }
        break;

        case FolderViewMode::ICONS:
        case FolderViewMode::COMPACT:
        {
            auto v = _iconView.get_selected_items();
            return v.size();
        }
        break;

        case FolderViewMode::ERROR:
        case FolderViewMode::UNDEFINED:
        break;
    }

    return 0;
}

void ElissoFolderView::selectExactlyOne(Gtk::TreeModel::Path &path)
{
    switch (_mode)
    {
        case FolderViewMode::LIST:
        {
            auto pSel = _treeView.get_selection();
            if (pSel)
            {
                pSel->unselect_all();
                pSel->select(path);
            }
        }
        break;

        case FolderViewMode::ICONS:
        case FolderViewMode::COMPACT:
            _iconView.unselect_all();
            _iconView.select_path(path);
        break;

        case FolderViewMode::ERROR:
        case FolderViewMode::UNDEFINED:
        break;
    }
}

void
ElissoFolderView::setIconViewColumns()
{
    _iconView.signal_button_press_event().connect([this](_GdkEventButton *pEvent) -> bool
    {
        if (_mainWindow.onButtonPressedEvent(pEvent, TreeViewPlusMode::IS_FOLDER_CONTENTS_RIGHT))
            return true;

        return false;
    });

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
        Debug::Log(FOLDER_POPULATE_HIGH, string(__func__) + "(\"" + pFS->getPath() + "\")");
        _mainWindow.openFile(pFS, {});
    }
}

void
ElissoFolderView::onSelectionChanged()
{
    // Only get the folder selection if we're fully populated and no file operations are going on.
    if (    (_state == ViewState::POPULATED)
         && (!_mainWindow.areFileOperationsRunning())
       )
    {
        FileSelection sel;
        this->getSelection(sel);
        _mainWindow.enableEditActions(sel.vFolders.size(), sel.vOthers.size());

        updateStatusbar(&sel);
    }
}


/***************************************************************************
 *
 *  FolderViewMonitor
 *
 **************************************************************************/

/* virtual */
void
FolderViewMonitor::onItemAdded(PFSModelBase &pFS) /* override */
{
    Debug d(FILEMONITORS, string(__func__) + "(" + pFS->getPath() + ")");
    _view.insertFile(pFS);
}

/* virtual */
void
FolderViewMonitor::onItemRemoved(PFSModelBase &pFS) /* override */
{
    Debug d(FILEMONITORS, string(__func__) + "(" + pFS->getPath() + ")");
    _view.removeFile(pFS);
}

/* virtual */
void
FolderViewMonitor::onItemRenamed(PFSModelBase &pFS, const std::string &strOldName, const std::string &strNewName) /* override */
{
    Debug d(FILEMONITORS, string(__func__) + "(" + pFS->getPath() + ")");
    _view.renameFile(pFS, strOldName, strNewName);
}

