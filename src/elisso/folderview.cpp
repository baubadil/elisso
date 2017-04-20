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

#include <thread>
#include <iostream>

#include "xwp/except.h"

#include "elisso/elisso.h"
#include "elisso/mainwindow.h"
#include "elisso/textentrydialog.h"


/***************************************************************************
 *
 *  Globals, static variable instantiations
 *
 **************************************************************************/

std::atomic<std::uint64_t>  g_uViewID(1);


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
        add(_colFilename);
        add(_colSize);
        add(_colIconSmall);
        add(_colIconBig);
        add(_colPFile);
        add(_colTypeString);
    }

    Gtk::TreeModelColumn<Glib::ustring>     _colFilename;
    Gtk::TreeModelColumn<u_int64_t>         _colSize;
    Gtk::TreeModelColumn<PPixBuf>           _colIconSmall;
    Gtk::TreeModelColumn<PPixBuf>           _colIconBig;
    Gtk::TreeModelColumn<PFSModelBase>      _colPFile;
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

/**
 *  Temporary structure used to communicate with the populate thread.
 */
struct ElissoFolderView::PopulateData : public ProhibitCopy
{
    PFSModelBase        pDir;
    Glib::Dispatcher    &dispatch;
    FSList              &llFolderContents;
    Glib::ustring       strError;
    std::thread         *pThread = nullptr;
    StopFlag            stopFlag;
    bool                fClickFromTree = false;     // true if SetDirectoryFlags::CLICK_FROM_TREE was set.

    PopulateData(PFSModelBase &pDir_,
                 Glib::Dispatcher &dispatch_,
                 FSList &llFolderContents_)
        : pDir(pDir_),
          dispatch(dispatch_),
          llFolderContents(llFolderContents_)
    {
    }
};

typedef std::shared_ptr<ElissoFolderView::PopulateData> PPopulateData;

struct ElissoFolderView::Impl : public ProhibitCopy
{
    PPopulateData                   pPopulateData;      // only set while state == POPULATING

    // GUI thread dispatcher for when a folder populate is done.
    Glib::Dispatcher                dispatcherPopulateDone;

    Glib::RefPtr<Gtk::ListStore>    pListStore;
    FSList                          llFolderContents;
    Glib::RefPtr<Gtk::IconTheme>    pIconTheme;

    std::shared_ptr<Gtk::Menu>      pPopupMenu;

    Impl()
        : pIconTheme(Gtk::IconTheme::get_default())
    { }
};


/***************************************************************************
 *
 *  ElissoFolderView::Selection
 *
 **************************************************************************/

/**
 *  Lists the items that are currently selected in the tree view.
 *  If nothing is selected, all lists are empty.
 */
struct ElissoFolderView::Selection : public ProhibitCopy
{
    FSList llFolders;       // directories or symlinks to directories
    FSList llOthers;        // other files
};


/***************************************************************************
 *
 *  ElissoFolderView
 *
 **************************************************************************/

/**
 *
 */
ElissoFolderView::ElissoFolderView(ElissoApplicationWindow &mainWindow)
    : Gtk::ScrolledWindow(),
      _id(g_uViewID++),
      _mainWindow(mainWindow),
      _iconView(),
      _treeView(),
      _infoBarError(),
      _infoBarLabel(),
      _pImpl(new ElissoFolderView::Impl())
{
    _treeView.setParent(*this);

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

    _pImpl->pListStore->set_sort_func(cols._colFilename, [&cols](const Gtk::TreeModel::iterator &a,
                                                                 const Gtk::TreeModel::iterator &b) -> int
    {
        auto rowA = *a;
        auto rowB = *b;
        PFSModelBase pFileA = rowA[cols._colPFile];
        PFSModelBase pFileB = rowB[cols._colPFile];
        auto typeA = pFileA->getResolvedType();
        auto typeB = pFileB->getResolvedType();
        bool fAIsFolder = (typeA == FSTypeResolved::DIRECTORY) || (typeA == FSTypeResolved::SYMLINK_TO_DIRECTORY);
        bool fBIsFolder = (typeB == FSTypeResolved::DIRECTORY) || (typeB == FSTypeResolved::SYMLINK_TO_DIRECTORY);
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

    /* Set up the icon view (more in setViewMode()) */
    _iconView.signal_item_activated().connect([this](const Gtk::TreeModel::Path &path)
    {
        this->onPathActivated(path);
    });

    /* Set up the list view */
    setListViewColumns();

    _treeView.signal_row_activated().connect([this](const Gtk::TreeModel::Path &path,
                                                    Gtk::TreeViewColumn *pColumn)
    {
        this->onPathActivated(path);
    });

    this->setViewMode(FolderViewMode::LIST);

    _iconView.signal_selection_changed().connect([this]()
    {
        this->onSelectionChanged();
    });

    _treeView.get_selection()->signal_changed().connect([this]()
    {
        this->onSelectionChanged();
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
    PFSModelBase pDirPrevious = _pDir;

    switch (_state)
    {
        case ViewState::ERROR:
        case ViewState::UNDEFINED:
        case ViewState::POPULATED:
        break;

        case ViewState::POPULATING:
            // stop the populate thread
            if (_pImpl->pPopulateData)
            {
                Debug::Log(FOLDER_POPULATE_HIGH, "already populating, stopping other populate thread");
                _pImpl->pPopulateData->stopFlag.set();
                _pImpl->pPopulateData->pThread->join();
                Debug::Log(FOLDER_POPULATE_LOW, "OK, stopped");
            }
        break;
    }

    auto t = pDirOrSymlinkToDir->getResolvedType();

    switch (t)
    {
        case FSTypeResolved::DIRECTORY:
        case FSTypeResolved::SYMLINK_TO_DIRECTORY:
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

            FSContainer *pDir = this->_pDir->getContainer();
            if (!pDir)
                this->setError("Invalid directory");
            else
            {
                Debug::Log(FOLDER_POPULATE_HIGH, "POPULATING LIST \"" + _pDir->getRelativePath() + "\"");

                PPopulateData pPopulateData = std::make_shared<PopulateData>(this->_pDir,
                                                                             this->_pImpl->dispatcherPopulateDone,
                                                                             _pImpl->llFolderContents);
                this->_pImpl->pPopulateData = pPopulateData;

                if (fl.test(SetDirectoryFlags::CLICK_FROM_TREE))
                    pPopulateData->fClickFromTree = true;

                pPopulateData->pThread = new std::thread([pPopulateData]()
                {
                    try
                    {
                        FSContainer *pDir = pPopulateData->pDir->getContainer();
                        if (pDir)
                            pDir->getContents(pPopulateData->llFolderContents,
                                              FSDirectory::Get::ALL,
                                              &pPopulateData->stopFlag);
                    }
                    catch(exception &e)
                    {
                        pPopulateData->strError = e.what();
                    }

                    if (!pPopulateData->stopFlag)
                        // Trigger the dispatcher, which will call "populate done".
                        pPopulateData->dispatch.emit();
                });
            }

            dumpStack();
        }
        break;

        default:
        break;
    }

    _mainWindow.enableBackForwardActions();

    return rc;
}

/**
 *  Gets called when the populate thread within setDirectory() has finished. We must now
 *  inspect the PopulateData in the implementation struct for the results.
 */
void
ElissoFolderView::onPopulateDone()
{
    if (!_pImpl->pPopulateData)
        throw FSException("PopulateData is nullptr");

    if (_pImpl->pPopulateData->strError.size())
        this->setError(_pImpl->pPopulateData->strError);
    else
    {
        const FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();

        Debug::Log(FOLDER_POPULATE_LOW, "ElissoFolderView::onPopulateDone(\"" + _pDir->getRelativePath() + "\")");

        for (auto &pFS : _pImpl->llFolderContents)
            if (!pFS->isHidden())
            {
                auto row = *(_pImpl->pListStore->append());

//                 PPixBuf pb2 = _pImpl->pIconTheme->choose_icon(sv, 50).load_icon();
//                 row[cols._colIconBig] = pb2;

                row[cols._colFilename] = pFS->getBasename();
                row[cols._colSize] = pFS->getFileSize();
                row[cols._colPFile] = pFS;

                auto t = pFS->getResolvedType();
//                 row[cols._colTypeResolved] = t;

                const char *p = "Special";
                switch (t)
                {
                    case FSTypeResolved::FILE: p = "File"; break;
                    case FSTypeResolved::DIRECTORY: p = "Folder"; break;
                    case FSTypeResolved::SYMLINK_TO_FILE: p = "Link to file"; break;
                    case FSTypeResolved::SYMLINK_TO_DIRECTORY: p = "Link to folder"; break;
                    case FSTypeResolved::BROKEN_SYMLINK: p = "Broken link"; break;
                    default: break;
                }

                row[cols._colTypeString] = Glib::ustring(p);
            }

        this->setState(ViewState::POPULATED);

        // Release the populate data.
        this->_pImpl->pPopulateData = nullptr;
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
            {}))     // do not push to history
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
                Gtk::Spinner *pSpinner = new Gtk::Spinner();
                _mainWindow.getNotebook().set_tab_label(*this, *pSpinner);
                pSpinner->show();
                pSpinner->start();

                _mainWindow.onLoadingFolderView(*this);
            }
            break;

            case ViewState::POPULATED:
            {
                // Connect model again, set sort.
                this->connectModel(true);

                Glib::ustring strTitle("Error");
                if (_pDir)
                {
                    std::string str = _pDir->getRelativePath();
                    strTitle = str;
                    _mainWindow.getNotebook().set_tab_label_text(*this,
                                                                 _pDir->getBasename());
                }

                // Notify the tree that this folder has been populated.
                // Do not select the node in the tree if the click came from
                // the tree in the first place.
                PFSModelBase pDirSelect;
                if (    (!_pImpl->pPopulateData)
                     || (!_pImpl->pPopulateData->fClickFromTree)
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

            default:
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
                    _iconView.set_item_width(-1);
                    _iconView.set_margin(5);
                    _iconView.set_row_spacing(5);
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

            default:
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
    if (this->_mode == FolderViewMode::LIST)
        _treeView.get_selection()->select_all();
}

/**
 *  Returns the single selected folder (directory or symlink pointing to one),
 *  or nullptr if either nothing is selected or the selection is not a single
 *  such folder.
 */
PFSModelBase
ElissoFolderView::getSelectedFolder()
{
    Selection sel;
    size_t cTotal = getSelection(sel);
    if (cTotal == 1)
        if (sel.llFolders.size() == 1)
            return sel.llFolders.front();

    return nullptr;
}

/**
 *  Opens the given file-system object.
 *  As a special case, if pFS == nullptr, we try to get the current selection,
 *  but will take it only if exactly one object is selected.
 *
 *  If the object is a directory or a symlink to one, we call setDirectory().
 *  Otherwise we open the file with the default application as reported by
 *  Gio::AppInfo.
 */
void
ElissoFolderView::openFile(PFSModelBase pFS)
{
    if (!pFS)
    {
        Selection sel;
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
            std::string strPath = pFS->getRelativePath();
            char *pContentType = g_content_type_guess(strPath.c_str(),
                                                      nullptr,
                                                      0,
                                                      NULL);            // image/jpeg
            if (pContentType)
            {
                Glib::RefPtr<Gio::AppInfo> pAppInfo = Glib::wrap(g_app_info_get_default_for_type(pContentType, FALSE));
                g_free(pContentType);
                if (pAppInfo)
                    pAppInfo->launch(pFS->getGioFile());
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
 */
PFSDirectory
ElissoFolderView::createSubfolder()
{
    PFSDirectory pNew;

    TextEntryDialog dlg(this->_mainWindow,
                        "Create folder",
                        "Please enter the name of the new folder to be created in <b>" + this->_pDir->getBasename() + "</b>:",
                        "Create");
    if (dlg.run() == Gtk::RESPONSE_OK)
    {
        auto s = dlg.getText();
    }

    return pNew;
}

void
ForEachSubstring(const Glib::ustring &str,
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

    i = _treeView.append_column("Icon", cols._colIconSmall);
    if ((pColumn = _treeView.get_column(i - 1)))
    {
        pColumn->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
        pColumn->set_fixed_width(aSizes[i - 1]);
        pColumn->set_cell_data_func(_cellRendererIconSmall,
                                    [this, &cols](Gtk::CellRenderer*,
                                                  const Gtk::TreeModel::iterator& it)
        {
            Gtk::TreeModel::Row row = *it;

            PPixBuf pb1 = row[cols._colIconSmall];
            if (!pb1)
            {
                PFSModelBase pFS = row[cols._colPFile];

                Glib::ustring strIcons = pFS->getIcon();

                std::vector<Glib::ustring> sv;
                ForEachSubstring(   strIcons,
                                    " ",
                                    [&sv](const Glib::ustring &strParticle)
                                    {
                                        if (!strParticle.empty())
                                            sv.push_back(strParticle);
                                    });


                Gtk::IconInfo i = _pImpl->pIconTheme->choose_icon(sv, 16);
                if (i)
                    pb1 = i.load_icon();
    //             Gtk::CellRendererPixbuf* pPixbufRenderer = dynamic_cast<Gtk::CellRendererPixbuf*>(pRenderer);
                // _cellRendererIconSmall.property_pixbuf() = pb1;
                row[cols._colIconSmall] = pb1;
            }
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

    i = _treeView.append_column("Size", cols._colSize);
    if ((pColumn = _treeView.get_column(i - 1)))
    {
        pColumn->set_sizing(Gtk::TREE_VIEW_COLUMN_FIXED);
        pColumn->set_fixed_width(aSizes[i - 1]);
        pColumn->set_resizable(true);
        pColumn->set_sort_column(cols._colSize);
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

PFSModelBase
ElissoFolderView::getFSObject(Gtk::TreeModel::iterator &iter)
{
    if (iter)
    {
        Gtk::TreeModel::Row row = *iter;
        FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();
        std::string strBasename = Glib::filename_from_utf8(row[cols._colFilename]);

        std::string strParentDir = _pDir->getRelativePath();
        std::string strFullPath = makePath(strParentDir, strBasename);

        return FSModelBase::FindPath(strFullPath);
    }

    return nullptr;
}

size_t
ElissoFolderView::getSelection(Selection &sel)
{
    auto pSelection = _treeView.get_selection();
    if (pSelection)
    {
        auto v = pSelection->get_selected_rows();
        if (v.size())
            for (auto &path : v)
            {
                Gtk::TreeModel::iterator iter = _pImpl->pListStore->get_iter(path);
                if (iter)
                {
                    PFSModelBase pFS = getFSObject(iter);
                    if (pFS)
                    {
                        auto t = pFS->getResolvedType();
                        if (    (t == FSTypeResolved::DIRECTORY)
                             || (t == FSTypeResolved::SYMLINK_TO_DIRECTORY)
                           )
                            sel.llFolders.push_back(pFS);
                        else
                            sel.llOthers.push_back(pFS);
                    }
                }
            }
    }

    for (auto &p : sel.llFolders)
        Debug::Log(DEBUG_ALWAYS, "Selected folder: " + p->getRelativePath());
    for (auto &p : sel.llOthers)
        Debug::Log(DEBUG_ALWAYS, "Selected file: " + p->getRelativePath());

    return sel.llFolders.size() + sel.llOthers.size();
}

/**
 *  Shared event handler between the icon view and the tree view for double clicks.
 */
void
ElissoFolderView::onPathActivated(const Gtk::TreeModel::Path &path)
{
    Gtk::TreeModel::iterator iter = _pImpl->pListStore->get_iter(path);
    PFSModelBase pFS = getFSObject(iter);
    if (pFS)
    {
        Debug::Log(FOLDER_POPULATE_HIGH, string(__func__) + "(\"" + pFS->getRelativePath() + "\")");
        this->openFile(pFS);
    }
}

void
ElissoFolderView::onSelectionChanged()
{
    if (_state == ViewState::POPULATED)
    {
        Selection sel;
        this->getSelection(sel);
        _mainWindow.enableEditActions(sel.llFolders.size(), sel.llOthers.size());
    }
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

    switch (clickType)
    {
        case MouseButton3ClickType::SINGLE_ROW_SELECTED:
        case MouseButton3ClickType::MULTIPLE_ROWS_SELECTED:
        {
            Selection sel;
            /* size_t cTotal = */ this->getSelection(sel);

        //     if (cTotal == 1)
                app.addMenuItem(pMenu, "Open", ACTION_EDIT_OPEN_SELECTED);
        //     if (sel.llFolders.size())
                app.addMenuItem(pMenu, "Open in new tab", ACTION_EDIT_OPEN_SELECTED_IN_TAB);
                app.addMenuItem(pMenu, "Open in terminal", ACTION_EDIT_OPEN_SELECTED_IN_TERMINAL);

            auto pSubSection = app.addMenuSection(pMenu);
            app.addMenuItem(pSubSection, "Cut", ACTION_EDIT_CUT);
            app.addMenuItem(pSubSection, "Copy", ACTION_EDIT_COPY);

            pSubSection = app.addMenuSection(pMenu);
        //     if (cTotal == 1)
                app.addMenuItem(pSubSection, "Rename", ACTION_EDIT_RENAME);
            app.addMenuItem(pSubSection, "Move to trash", ACTION_EDIT_TRASH);

            pSubSection = app.addMenuSection(pMenu);
        //     if (cTotal == 1)
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
    _pImpl->pPopupMenu->attach_to_widget(*this);
    _pImpl->pPopupMenu->popup(pEvent->button, pEvent->time);
}
