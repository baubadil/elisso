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
#include <atomic>

#include "xwp/except.h"

#include "elisso/elisso.h"
#include "elisso/mainwindow.h"


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
        add(_colTypeResolved);
        add(_colTypeString);
    }

    Gtk::TreeModelColumn<Glib::ustring>     _colFilename;
    Gtk::TreeModelColumn<u_int64_t>         _colSize;
    Gtk::TreeModelColumn<PPixBuf>           _colIconSmall;
    Gtk::TreeModelColumn<PPixBuf>           _colIconBig;
    Gtk::TreeModelColumn<FSTypeResolved>    _colTypeResolved;
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

struct ElissoFolderView::Impl
{
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

struct ElissoFolderView::Selection
{
    FSList llFolders;       // directories or symlinks to directories
    FSList llOthers;        // other files
};


/***************************************************************************
 *
 *  TreeViewWithPopup
 *
 **************************************************************************/

bool
TreeViewWithPopup::on_button_press_event(GdkEventButton* button_event) /* override */
{
    if  ((button_event->type == GDK_BUTTON_PRESS) && (button_event->button == 3))
    {
        if (this->_pView)
            this->_pView->onMouseButton3Pressed(button_event);

        return true;        // do not propagate
    }
    else
        return Gtk::TreeView::on_button_press_event(button_event);
}

/***************************************************************************
 *
 *  ElissoFolderView
 *
 **************************************************************************/

ElissoFolderView::ElissoFolderView(ElissoApplicationWindow &mainWindow)
    : Gtk::ScrolledWindow(),
      _id(g_uViewID++),
      _mainWindow(mainWindow),
      _iconView(),
      _treeView(),
//       _compactView(),
      _pImpl(new ElissoFolderView::Impl())
{
    _treeView.setParent(*this);

    // Allow multiple selections.
    auto pTreeSel = _treeView.get_selection();
    pTreeSel->set_mode(Gtk::SELECTION_MULTIPLE);
//     auto pIconSel = _iconView.get_selection();
//     pIconSel->set_mode(Gtk::SELECTION_MULTIPLE);

    // Connect the GUI thread dispatcher for when a folder populate is done.
    _pImpl->dispatcherPopulateDone.connect([this]()
    {
        this->onPopulateDone();
    });

    FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();
    _pImpl->pListStore = Gtk::ListStore::create(cols);

    _pImpl->pListStore->set_sort_func(cols._colFilename, [&cols](const Gtk::TreeModel::iterator &a,
                                                                 const Gtk::TreeModel::iterator &b) -> int
    {
        auto rowA = *a;
        auto rowB = *b;
        auto typeA = rowA[cols._colTypeResolved];
        auto typeB = rowB[cols._colTypeResolved];
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

//     _treeView.signal_button_press_event().connect_notify([this](GdkEventButton* button_event)
//     {
//         //Then do our custom stuff:
//         if  ((button_event->type == GDK_BUTTON_PRESS) && (button_event->button == 3))
//         {
//             // Mouse button 3 pressed: add an idle function to call onMouseButton3Pressed() once,
//             // to give GTK time to change the selection first.
//             Glib::signal_idle().connect([this]()
//             {
//                 this->onMouseButton3Pressed();
//                 return false;       // disconnect, do not call again
//             }, Glib::PRIORITY_DEFAULT_IDLE);
//         }
//     });
//
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
                               bool fPushToHistory /* = true */)
{
    bool rc = false;
    auto t = pDirOrSymlinkToDir->getResolvedType();

    switch (t)
    {
        case FSTypeResolved::DIRECTORY:
        case FSTypeResolved::SYMLINK_TO_DIRECTORY:
        {
            // If we have a directory already, push the path into the history.
            if (fPushToHistory)
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
            // Remove all old data, if any.
            _pImpl->pListStore->clear();
            _pImpl->llFolderContents.clear();
            rc = this->spawnPopulate();

            dumpStack();
        }
        break;

        default:
        break;
    }

    _mainWindow.enableBackForwardActions();

    return rc;
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
                                   false))      // do not push on history stack
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
            if (this->setDirectory(pDir,
                                   false))      // do not push on history stack
            {
                return true;
            }
    }

    return false;
}

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
                _mainWindow.onFolderViewReady(*this);
            }
            break;

            default:
            break;
        }

        _state = s;
    }
}

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
                    _iconView.set_row_spacing(5);
                }
                else
                {
                    // compact
                    _iconView.set_item_orientation(Gtk::Orientation::ORIENTATION_HORIZONTAL);
                    _iconView.set_pixbuf_column(cols._colIconSmall);
                    _iconView.set_text_column(cols._colFilename);
                    _iconView.set_item_width(200);
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

            default:
            break;
        }

        _mode = m;

        this->connectModel(_state == ViewState::POPULATED);
    }
}

/**
 *  As a special case, if pFS == nullptr, we try to get the current selection.
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
            this->setDirectory(pFS);
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

void
ElissoFolderView::openTerminalOnSelectedFolder()
{
    Selection sel;
    size_t cTotal = getSelection(sel);
    if (    (cTotal == 1)
         && (sel.llFolders.size() == 1)
       )
    {
        auto pFS = sel.llFolders.front();
        if (pFS)
        {
            auto strPath = pFS->getRelativePath();
            g_subprocess_new(G_SUBPROCESS_FLAGS_NONE,
                             NULL,
                             "open", "--screen", "auto", strPath.c_str(), nullptr);
        }
    }
}

/**
 *  Returns true if a populate thread was started, or false if the folder had already been populated.
 *
 *  Gets called only from setDirectory() at this time.
 */
bool
ElissoFolderView::spawnPopulate()
{
    bool rc = false;
    if (_state != ViewState::POPULATING)
    {
        this->setState(ViewState::POPULATING);

        Debug::Log(FOLDER_POPULATE, "POPULATING - ElissoFolderView::spawnPopulate(\"" + _pDir->getRelativePath() + "\")");

        new std::thread([this]()
        {
            try
            {
                PFSDirectory pDir = this->_pDir->resolveDirectory();
                if (pDir)
                    pDir->getContents(_pImpl->llFolderContents,
                                      FSDirectory::Get::ALL);
            }
            catch(exception &e)
            {
                this->setState(ViewState::POPULATE_ERROR);
            }
            // Trigger the dispatcher, which will call "populate done".
            this->_pImpl->dispatcherPopulateDone.emit();
        });

        rc = true;
    }

    return rc;
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
ElissoFolderView::onPopulateDone()
{
    const FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();

    Debug::Log(FOLDER_POPULATE, "ElissoFolderView::onPopulateDone(\"" + _pDir->getRelativePath() + "\")");

    for (auto &pFS : _pImpl->llFolderContents)
        if (!pFS->isHidden())
        {
            auto row = *(_pImpl->pListStore->append());

            Glib::ustring strIcons = pFS->getIcon();
            std::vector<Glib::ustring> sv;
            ForEachSubstring(   strIcons,
                                " ",
                                [&sv](const Glib::ustring &strParticle)
                                {
                                    if (!strParticle.empty())
                                        sv.push_back(strParticle);
                                });

            PPixBuf pb1 = _pImpl->pIconTheme->choose_icon(sv, 16).load_icon();
            row[cols._colIconSmall] = pb1;
            PPixBuf pb2 = _pImpl->pIconTheme->choose_icon(sv, 50).load_icon();
            row[cols._colIconBig] = pb2;

            row[cols._colFilename] = pFS->getBasename();
            row[cols._colSize] = pFS->getFileSize();

            auto t = pFS->getResolvedType();
            row[cols._colTypeResolved] = t;

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
}

void
ElissoFolderView::connectModel(bool fConnect)
{
    switch (_mode)
    {
        case FolderViewMode::ICONS:
        case FolderViewMode::COMPACT:
        {
            FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();
            if (fConnect)
            {
                _iconView.set_model(_pImpl->pListStore);
                 _pImpl->pListStore->set_sort_column(cols._colFilename, Gtk::SortType::SORT_ASCENDING);
            }
            else
            {
                _iconView.unset_model();
                 _pImpl->pListStore->set_sort_column(Gtk::TreeSortable::DEFAULT_UNSORTED_COLUMN_ID, Gtk::SortType::SORT_ASCENDING);
            }
        }
        break;

        case FolderViewMode::LIST:
            if (fConnect)
                _treeView.set_model(_pImpl->pListStore);
            else
                _treeView.unset_model();
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
        else
            sel.llFolders.push_back(_pDir);
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
        Debug::Log(FOLDER_POPULATE, string(__func__) + "(\"" + pFS->getRelativePath() + "\")");
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
ElissoFolderView::onMouseButton3Pressed(GdkEventButton* event)
{
    auto &app = _mainWindow.getApplication();

    Selection sel;
    /* size_t cTotal = */ this->getSelection(sel);

    auto pMenu = Gio::Menu::create();
//     if (cTotal == 1)
        app.addMenuItem(pMenu, "Open", ACTION_EDIT_OPEN);
//     if (sel.llFolders.size())
        app.addMenuItem(pMenu, "Open in terminal", ACTION_EDIT_TERMINAL);
    app.addMenuItem(pMenu, "Cut", ACTION_EDIT_CUT);
    app.addMenuItem(pMenu, "Copy", ACTION_EDIT_COPY);
    app.addMenuItem(pMenu, "Paste", ACTION_EDIT_PASTE);
//     if (cTotal == 1)
        app.addMenuItem(pMenu, "Rename", ACTION_EDIT_RENAME);
    app.addMenuItem(pMenu, "Move to trash", ACTION_EDIT_TRASH);
//     if (cTotal == 1)
        app.addMenuItem(pMenu, "Properties", ACTION_EDIT_PROPERTIES);

    _pImpl->pPopupMenu = std::make_shared<Gtk::Menu>(pMenu);
    _pImpl->pPopupMenu->attach_to_widget(*this);
    _pImpl->pPopupMenu->popup(event->button, event->time);
}
