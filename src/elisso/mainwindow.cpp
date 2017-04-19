/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/mainwindow.h"

#include "elisso/elisso.h"

ElissoApplicationWindow::ElissoApplicationWindow(ElissoApplication &app,
                                                 PFSModelBase pdirInitial)      //!< in: initial directory or nullptr for "home"
    : _app(app),
      _mainVBox(Gtk::ORIENTATION_VERTICAL),
      _vPaned(),
      _treeViewLeft(*this),
      _notebook()
{
    this->initActionHandlers();

    /*
     *  Window setup
     */
    this->setSizeAndPosition();

    this->set_icon(app.getIcon());

    /*
     *  Toolbar
     */

    Gtk::Toolbar* pToolbar = new Gtk::Toolbar();
    _mainVBox.pack_start(*pToolbar, Gtk::PACK_SHRINK);

    pToolbar->append(*(_pButtonGoBack = makeToolButton("go-previous-symbolic", _pActionGoBack)));
    pToolbar->append(*(_pButtonGoForward = makeToolButton("go-next-symbolic", _pActionGoForward)));
    pToolbar->append(*(_pButtonGoParent = makeToolButton("go-up-symbolic", _pActionGoParent)));
    pToolbar->append(*(_pButtonGoHome = makeToolButton("go-home-symbolic", _pActionGoHome)));

    auto pSeparator = new Gtk::SeparatorToolItem();
    pSeparator->set_expand(true);
    pSeparator->set_draw(false);
    pToolbar->append(*pSeparator);

    pToolbar->append(*(_pButtonViewIcons = makeToolButton("view-grid-symbolic", _pActionViewIcons, true)));
    pToolbar->append(*(_pButtonViewList = makeToolButton("view-list-symbolic", _pActionViewList)));
    pToolbar->append(*(_pButtonViewRefresh = makeToolButton("view-refresh-symbolic", _pActionViewRefresh)));

    this->signal_action_enabled_changed().connect([this](const Glib::ustring &strAction, bool fEnabled)
    {
//         Debug::Log(DEBUG_ALWAYS, "enabled changed: " + strAction);
        if (strAction == ACTION_GO_BACK)
            _pButtonGoBack->set_sensitive(fEnabled);
        else if (strAction == ACTION_GO_FORWARD)
            _pButtonGoForward->set_sensitive(fEnabled);
        else if (strAction == ACTION_GO_PARENT)
            _pButtonGoParent->set_sensitive(fEnabled);
        else if (strAction == ACTION_VIEW_ICONS)
            _pButtonViewIcons->set_sensitive(fEnabled);
        else if (strAction == ACTION_VIEW_LIST)
            _pButtonViewList->set_sensitive(fEnabled);
//         else if (strAction == ACTION_VIEW_COMPACT)
        else if (strAction == ACTION_VIEW_REFRESH)
            _pButtonViewRefresh->set_sensitive(fEnabled);
    });

    _vPaned.set_position(200);
    _vPaned.set_wide_handle(true);
    _vPaned.add1(_treeViewLeft);
    _vPaned.add2(_notebook);

    _mainVBox.pack_start(_vPaned);

//     auto pStatusBar = new Gtk::Statusbar();
//     _mainVBox.pack_start(*pStatusBar);
//     pStatusBar->show();

//     int context_id = pStatusBar->get_context_id("Statusbar example");
//     pStatusBar->push("Test", context_id);

    this->add(_mainVBox);

    this->show_all_children();

    // Add the first page in an idle loop so we have no delay in showing the window.
    Glib::signal_idle().connect([this, pdirInitial]() -> bool
    {
        this->addFolderTab(pdirInitial);
        return false;       // don't call again
    });

    _notebook.set_scrollable(true);
    _notebook.show_all();

    /*
     *  Children setup
     */

    _notebook.signal_switch_page().connect([this](Gtk::Widget *pw,
                                                  guint page_number)
    {
        auto p = static_cast<ElissoFolderView*>(pw);
        if (p)
        {
            PFSModelBase pDir = p->getDirectory();
            this->onFolderViewLoaded(*p, pDir);
        }
    });
}

/**
 *  Adds a new tab to the GTK notebook in the right pane with an ElissoFolderView
 *  for the given directory inside.
 *
 *  If pDir is nullptr, we retrieve the user's home directory from the FS backend.
 */
void
ElissoApplicationWindow::addFolderTab(PFSModelBase pDirOrSymlink)       //!< in: directory to open, or nullptr for "home"
{
    // Create a new view and add it to the notebook, which then owns it.
    auto pView = new ElissoFolderView(*this);
    auto p2 = pDirOrSymlink;

    if (!p2)
        p2 = FSDirectory::GetHome();

    int n = _notebook.append_page(*pView,
                                  p2->getBasename());
    pView->show();
    _notebook.set_current_page(n);
    _notebook.set_tab_reorderable(*pView, true);

    pView->setDirectory(p2,
                        {});     // do not push to history
}

void
ElissoApplicationWindow::initActionHandlers()
{
    /*
     *  File menu
     */
    this->add_action(ACTION_FILE_NEW_TAB, [this]()
    {
        ElissoFolderView *pView;
        if ((pView = getActiveFolderView()))
        {
            PFSModelBase pDir;
            if ((pDir = pView->getDirectory()))
            {
                this->addFolderTab(pDir);
            }
        }
    });

    this->add_action(ACTION_FILE_QUIT, [this]()
    {
        getApplication().quit();
    });

    this->add_action(ACTION_FILE_CLOSE_TAB, [this]()
    {
        this->handleViewAction(ACTION_FILE_CLOSE_TAB);
    });

    /*
     *  Edit menu
     */

    _pActionEditOpen = this->add_action(ACTION_EDIT_OPEN, [this]()
    {
        this->handleViewAction(ACTION_EDIT_OPEN);
    });

    _pActionEditOpenInTab = this->add_action(ACTION_EDIT_OPEN_IN_TAB, [this]()
    {
        this->handleViewAction(ACTION_EDIT_OPEN_IN_TAB);
    });

    _pActionEditOpenInTerminal = this->add_action(ACTION_EDIT_OPEN_IN_TERMINAL, [this]()
    {
        this->handleViewAction(ACTION_EDIT_OPEN_IN_TERMINAL);
    });

    _pActionEditCopy = this->add_action(ACTION_EDIT_COPY, [this]()
    {
    });

    _pActionEditCut = this->add_action(ACTION_EDIT_CUT, [this]()
    {
    });

    _pActionEditPaste = this->add_action(ACTION_EDIT_PASTE, [this]()
    {
    });

    _pActionEditRename = this->add_action(ACTION_EDIT_RENAME, [this]()
    {
    });

    _pActionEditTrash = this->add_action(ACTION_EDIT_TRASH, [this]()
    {
    });

    _pActionEditProperties = this->add_action(ACTION_EDIT_PROPERTIES, [this]()
    {
    });


    /*
     *  View menu
     */
    _pActionViewIcons = this->add_action(ACTION_VIEW_ICONS, [this]()
    {
        this->handleViewAction(ACTION_VIEW_ICONS);
    });

    _pActionViewList = this->add_action(ACTION_VIEW_LIST, [this]()
    {
        this->handleViewAction(ACTION_VIEW_LIST);
    });

    _pActionViewCompact = this->add_action(ACTION_VIEW_COMPACT, [this]()
    {
        this->handleViewAction(ACTION_VIEW_COMPACT);
    });

    _pActionViewRefresh = this->add_action(ACTION_VIEW_REFRESH, [this]()
    {
        this->handleViewAction(ACTION_VIEW_REFRESH);
    });

    /*
     *  Go menu
     */
    _pActionGoBack = this->add_action(ACTION_GO_BACK, [this]()
    {
        this->handleViewAction(ACTION_GO_BACK);
    });

    _pActionGoForward = this->add_action(ACTION_GO_FORWARD, [this]()
    {
        this->handleViewAction(ACTION_GO_FORWARD);
    });

    _pActionGoParent = this->add_action(ACTION_GO_PARENT, [this]()
    {
        this->handleViewAction(ACTION_GO_PARENT);
    });

    _pActionGoHome = this->add_action(ACTION_GO_HOME, [this]()
    {
        this->handleViewAction(ACTION_GO_HOME);
    });

    /*
     *  Help menu
     */
    this->add_action(ACTION_ABOUT, [this]()
    {
        auto w = Gtk::AboutDialog();
        w.set_version(ELISSO_VERSION);
        w.set_copyright("(C) 2017 Baubadil GmbH");
        w.set_website("http://www.baubadil.de");
        w.set_comments("Soon to be the best file manager for Linux.");
        w.set_license_type(Gtk::License::LICENSE_CUSTOM);
        w.set_license("All rights reserved");
        w.set_logo(_app.getIcon());
        w.set_transient_for(*this);
        w.run();
    });
}

void
ElissoApplicationWindow::setSizeAndPosition()
{
    auto strPos = _app.getSettingsString(SETTINGS_WINDOWPOS);     // x,x,1000,600
    auto v = explodeVector(strPos, ",");
    if (v.size() == 6)
    {
        _width = stoi(v[2]);
        _height = stoi(v[3]);

        bool fCenterX = (v[0] == "x");
        bool fCenterY = (v[1] == "x");

        Gdk::Rectangle rectCurrentMonitor;
        if (fCenterX ||  fCenterY)
        {
            // It's not enough to get the total screen size since there may be multiple monitors.
            auto pScreen = this->get_screen();
            int m = pScreen->get_monitor_at_window(pScreen->get_active_window());
            pScreen->get_monitor_geometry(m, rectCurrentMonitor);
        }

        if (fCenterX)
            _x = rectCurrentMonitor.get_x() + (rectCurrentMonitor.get_width() - _width) / 2;
        else
            _x = stoi(v[0]);
        if (fCenterY)
            _y = rectCurrentMonitor.get_y() + (rectCurrentMonitor.get_height() - _height) / 2;
        else
            _y = stoi(v[1]);

        _fIsMaximized = !!(stoi(v[4]));
        _fIsFullscreen = !!(stoi(v[5]));
    }

    this->set_default_size(_width, _height);
    this->move(_x, _y);

    if (_fIsMaximized)
        this->maximize();
    if (_fIsFullscreen)
        this->fullscreen();
}

void
ElissoApplicationWindow::setWindowTitle(Glib::ustring str)
{
    Glib::ustring strTitle(str);
    strTitle += " — " + APPLICATION_NAME;
    this->set_title(strTitle);

}

Gtk::ToolButton*
ElissoApplicationWindow::makeToolButton(const Glib::ustring &strIconName,
                                        PSimpleAction pAction,
                                        bool fAlignRight /* = false*/ )
{
    Gtk::ToolButton *pButton = nullptr;
    if (pAction)
    {
        auto pImage = new Gtk::Image();
        pImage->set_from_icon_name(strIconName,
                                Gtk::BuiltinIconSize::ICON_SIZE_SMALL_TOOLBAR);
        pButton = Gtk::manage(new Gtk::ToolButton(*pImage));
        if (fAlignRight)
            pButton->set_halign(Gtk::ALIGN_START);
        // Connect to "clicked" signal on button.
        pButton->signal_clicked().connect([this, pAction]()
        {
            this->activate_action(pAction->get_name());
        });
    }
    return pButton;
}

/* virtual */
void
ElissoApplicationWindow::on_size_allocate(Gtk::Allocation& allocation) /* override */
{
    ApplicationWindow::on_size_allocate(allocation);

    if (!_fIsMaximized && !_fIsFullscreen)
    {
        this->get_position(_x, _y);
        this->get_size(_width, _height);
    }
}

/* virtual */
bool
ElissoApplicationWindow::on_window_state_event(GdkEventWindowState *ev) /* override */
{
    ApplicationWindow::on_window_state_event(ev);

    _fIsMaximized = (ev->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
    _fIsFullscreen = (ev->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) != 0;

    return false; // propagate
}

/* virtual */
bool
ElissoApplicationWindow::on_delete_event(GdkEventAny *ev) /* override */
{
    ApplicationWindow::on_delete_event(ev);

    StringVector v;
    v.push_back(to_string(_x));
    v.push_back(to_string(_y));
    v.push_back(to_string(_width));
    v.push_back(to_string(_height));
    v.push_back((_fIsMaximized) ? "1" : "0");
    v.push_back((_fIsFullscreen) ? "1" : "0");
    if (v.size() == 6)
        _app.setSettingsString(SETTINGS_WINDOWPOS, implode(",", v));

    return false; // propagate
}

/**
 *  Closes the notebook tab for the given ElissoFolderView. If this is the last
 *  tab, it closes the entire ElissoApplicationWindow.
 */
void
ElissoApplicationWindow::closeFolderTab(ElissoFolderView &viewClose)
{
    int cPages = _notebook.get_n_pages();
    if (cPages > 1)
    {
        for (int i = 0; i < cPages; ++i)
        {
            auto pPageWidget = _notebook.get_nth_page(i);
            ElissoFolderView *pViewThis = static_cast<ElissoFolderView *>(pPageWidget);
            if (pViewThis->getID() == viewClose.getID())
            {
                _notebook.remove_page(*pPageWidget);
                break;
            }
        }
    }
    else
        this->close();
}

/* virtual */
ElissoApplicationWindow::~ElissoApplicationWindow()
{
}

int ElissoApplicationWindow::errorBox(Glib::ustring strMessage)
{
    Gtk::MessageDialog dialog(*this,
                              strMessage,
                              false /* use_markup */,
                              Gtk::MESSAGE_QUESTION,
                              Gtk::BUTTONS_CANCEL);
//     dialog.set_secondary_text("Launch \"" + pAppInfo->get_commandline() + "\"?");
    return dialog.run();
}

/**
 *  Returns the ElissoFolderView that is on the currently active notebook page.
 */
ElissoFolderView*
ElissoApplicationWindow::getActiveFolderView()
{
    auto i = _notebook.get_current_page();
    if (i != -1)
        return static_cast<ElissoFolderView*>(_notebook.get_nth_page(i));

    return nullptr;
}

/**
 *  Called from ElissoFolderView::onSelectionChanged() whenever the selection changes.
 */
void
ElissoApplicationWindow::enableEditActions(size_t cFolders, size_t cOtherFiles)
{
    Debug::Log(DEBUG_ALWAYS, "cFolders: " + to_string(cFolders) + ", cOtherFiles: " + to_string(cOtherFiles));
    size_t cTotal = cFolders + cOtherFiles;
    bool fSingleFolder = (cTotal == 1) && (cFolders == 1);
    _pActionEditOpen->set_enabled(cTotal == 1);
    _pActionEditOpenInTab->set_enabled(fSingleFolder);
    _pActionEditOpenInTerminal->set_enabled(fSingleFolder);
    _pActionEditCopy->set_enabled(cTotal > 0);
    _pActionEditCut->set_enabled(cTotal > 0);
    _pActionEditPaste->set_enabled(false);
    _pActionEditRename->set_enabled(cTotal == 1);
    _pActionEditTrash->set_enabled(cTotal > 0);
    _pActionEditProperties->set_enabled(cTotal == 1);
}

/**
 *  Called from ElissoFolderView::setDirectory() to enable back/forward actions.
 */
void
ElissoApplicationWindow::enableBackForwardActions()
{
    ElissoFolderView *pActive;
    if ((pActive = getActiveFolderView()))
    {
        _pActionGoBack->set_enabled(pActive->canGoBack());
        _pActionGoForward->set_enabled(pActive->canGoForward());
    }
}

void
ElissoApplicationWindow::onLoadingFolderView(ElissoFolderView &view)
{
    PFSModelBase pDir = view.getDirectory();

    this->setWindowTitle("Loading " + pDir->getBasename() + "...");
    this->enableViewActions(false);
}

/**
 *  Gets called when the main window needs updating as a result of the folder view changing. In particular:
 *
 *   -- when the notebook page changes;
 *
 *   -- when the state of the current notebook page changes.
 *
 *  In both cases we want to update the title bar, and we want to update the tree to point to the
 *  folder that's displaying on the right.
 */
void
ElissoApplicationWindow::onFolderViewLoaded(ElissoFolderView &view,
                                            PFSModelBase pDirSelect)
{
    PFSModelBase pDir = view.getDirectory();

    Glib::ustring strTitle = "?";
    if (pDir)
    {
        strTitle = pDir->getRelativePath();
        Debug::Log(FOLDER_POPULATE_HIGH, string(__func__) + "(\"" + pDir->getRelativePath() + "\")");
    }

    this->setWindowTitle(strTitle);
    this->enableViewActions(true);

    if (pDirSelect)
        _treeViewLeft.select(pDirSelect);
}

void
ElissoApplicationWindow::enableViewActions(bool f)
{
    _pActionViewIcons->set_enabled(f);
    _pActionViewList->set_enabled(f);
    _pActionViewCompact->set_enabled(f);
    _pActionViewRefresh->set_enabled(f);
}

void
ElissoApplicationWindow::handleViewAction(const std::string &strAction)
{
    auto p = this->getActiveFolderView();
    if (p)
    {
        if (strAction == ACTION_FILE_CLOSE_TAB)
            this->closeFolderTab(*p);
        else if (strAction == ACTION_EDIT_OPEN)
            p->openFile(nullptr);
        else if (strAction == ACTION_EDIT_OPEN_IN_TAB)
        {
            auto pFS = p->getSelectedFolder();
            if (pFS)
                this->addFolderTab(pFS);
        }
        else if (strAction == ACTION_EDIT_OPEN_IN_TERMINAL)
        {
            auto pFS = p->getSelectedFolder();
            if (pFS)
            {
                auto strPath = pFS->getRelativePath();
                g_subprocess_new(G_SUBPROCESS_FLAGS_NONE,
                                 NULL,
                                 "open", "--screen", "auto", strPath.c_str(), nullptr);
            }
        }
        else if (strAction == ACTION_VIEW_ICONS)
            p->setViewMode(FolderViewMode::ICONS);
        else if (strAction == ACTION_VIEW_LIST)
            p->setViewMode(FolderViewMode::LIST);
        else if (strAction == ACTION_VIEW_COMPACT)
            p->setViewMode(FolderViewMode::COMPACT);
        else if (strAction == ACTION_VIEW_REFRESH)
            p->refresh();
        else if (strAction == ACTION_GO_BACK)
            p->goBack();
        else if (strAction == ACTION_GO_FORWARD)
            p->goForward();
        else if (strAction == ACTION_GO_PARENT)
        {
            Debug::Log(DEBUG_ALWAYS, "go parent");
            PFSModelBase pDir;
            if ((pDir = p->getDirectory()))
                if ((pDir = pDir->getParent()))
                    p->setDirectory(pDir, SetDirectoryFlags::PUSH_TO_HISTORY);
        }
        else if (strAction == ACTION_GO_HOME)
        {
            auto pHome = FSDirectory::GetHome();
            if (pHome)
                p->setDirectory(pHome, SetDirectoryFlags::PUSH_TO_HISTORY);
        }
    }
}

