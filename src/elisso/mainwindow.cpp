/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/mainwindow.h"

#include "elisso/elisso.h"
#include "elisso/fileops.h"
#include "elisso/contenttype.h"
#include "xwp/except.h"

struct ElissoApplicationWindow::Impl
{
    PSimpleAction                   pActionEditOpenSelected;
    PSimpleAction                   pActionEditOpenSelectedInTab;
    PSimpleAction                   pActionEditOpenSelectedInTerminal;
    PSimpleAction                   pActionEditCopy;
    PSimpleAction                   pActionEditCut;
    PSimpleAction                   pActionEditPaste;
    PSimpleAction                   pActionEditSelectAll;
    PSimpleAction                   pActionEditRename;
    PSimpleAction                   pActionEditTrash;
    PSimpleAction                   pActionEditTestFileops;
    PSimpleAction                   pActionEditProperties;

    PSimpleAction                   pActionGoBack;
    Gtk::ToolButton                 *pButtonGoBack;
    PSimpleAction                   pActionGoForward;
    Gtk::ToolButton                 *pButtonGoForward;
    PSimpleAction                   pActionGoParent;
    Gtk::ToolButton                 *pButtonGoParent;
    PSimpleAction                   pActionGoHome;
    Gtk::ToolButton                 *pButtonGoHome;

    PSimpleAction                   pActionViewNextTab;
    PSimpleAction                   pActionViewPreviousTab;
    PSimpleAction                   pActionViewIcons;
    Gtk::ToolButton                 *pButtonViewIcons;
    PSimpleAction                   pActionViewList;
    Gtk::ToolButton                 *pButtonViewList;
    PSimpleAction                   pActionViewCompact;
//     Gtk::ToolButton                 *_pButtonViewCompact;
    PSimpleAction                   pActionViewRefresh;
    Gtk::ToolButton                 *pButtonViewRefresh;

    std::shared_ptr<Gtk::Menu>      pPopupMenu;

    FileOperationsList              llFileOperations;
    PProgressDialog                 pProgressDialog;
};

ElissoApplicationWindow::ElissoApplicationWindow(ElissoApplication &app)      //!< in: initial directory or nullptr for "home"
    : _app(app),
      _pImpl(new Impl),
      _mainVBox(Gtk::ORIENTATION_VERTICAL),
      _vPaned(),
      _folderTreeMgr(*this),
      _boxForNotebookAndStatusBar(Gtk::ORIENTATION_VERTICAL),
      _notebook()
{
    this->initActionHandlers();

    /*
     *  Window setup
     */
    this->setSizeAndPosition();

    this->set_icon(app.getIcon());

    /*
     *  Clipboard
     */

    Glib::RefPtr<Gtk::Clipboard> pClip = Gtk::Clipboard::get();

    pClip->signal_owner_change().connect([this](GdkEventOwnerChange*)
    {
        this->onClipboardChanged();
    });

//     std::vector<Gtk::TargetEntry> targets;
// //     targets.push_back( Gtk::TargetEntry("example_custom_target") );
//     targets.push_back( Gtk::TargetEntry("UTF8_STRING") );
//
//     pClip->set(targets,
//                sigc::mem_fun(*this, &ElissoApplicationWindow::onClipboardGet),
//                sigc::mem_fun(*this, &ElissoApplicationWindow::onClipboardClear));
//
    /*
     *  Toolbar
     */

    _toolbar.append(*(_pImpl->pButtonGoBack = makeToolButton("go-previous-symbolic", _pImpl->pActionGoBack)));
    _toolbar.append(*(_pImpl->pButtonGoForward = makeToolButton("go-next-symbolic", _pImpl->pActionGoForward)));
    _toolbar.append(*(_pImpl->pButtonGoParent = makeToolButton("go-up-symbolic", _pImpl->pActionGoParent)));
    _toolbar.append(*(_pImpl->pButtonGoHome = makeToolButton("go-home-symbolic", _pImpl->pActionGoHome)));

    auto pSeparator = new Gtk::SeparatorToolItem();
    pSeparator->set_expand(true);
    pSeparator->set_draw(false);
    _toolbar.append(*pSeparator);

    _toolbar.append(*(_pImpl->pButtonViewIcons = makeToolButton("view-grid-symbolic", _pImpl->pActionViewIcons, true)));
    _toolbar.append(*(_pImpl->pButtonViewList = makeToolButton("view-list-symbolic", _pImpl->pActionViewList)));
    _toolbar.append(*(_pImpl->pButtonViewRefresh = makeToolButton("view-refresh-symbolic", _pImpl->pActionViewRefresh)));

    this->signal_action_enabled_changed().connect([this](const Glib::ustring &strAction, bool fEnabled)
    {
//         Debug::Log(DEBUG_ALWAYS, "enabled changed: " + strAction);
        if (strAction == ACTION_GO_BACK)
            _pImpl->pButtonGoBack->set_sensitive(fEnabled);
        else if (strAction == ACTION_GO_FORWARD)
            _pImpl->pButtonGoForward->set_sensitive(fEnabled);
        else if (strAction == ACTION_GO_PARENT)
            _pImpl->pButtonGoParent->set_sensitive(fEnabled);
        else if (strAction == ACTION_VIEW_ICONS)
            _pImpl->pButtonViewIcons->set_sensitive(fEnabled);
        else if (strAction == ACTION_VIEW_LIST)
            _pImpl->pButtonViewList->set_sensitive(fEnabled);
//         else if (strAction == ACTION_VIEW_COMPACT)
        else if (strAction == ACTION_VIEW_REFRESH)
            _pImpl->pButtonViewRefresh->set_sensitive(fEnabled);
    });

    _statusbarCurrent.set_hexpand(true);

    _progressBarThumbnailer.set_valign(Gtk::Align::ALIGN_CENTER);
    _progressBarThumbnailer.set_size_request(50, -1);
    _gridThumbnailing.set_valign(Gtk::Align::ALIGN_CENTER);
    _statusbarThumbnailing.push("Thumbnailing:");
    _gridThumbnailing.add(_statusbarThumbnailing);
    _gridThumbnailing.add(_progressBarThumbnailer);

    _statusbarFree.set_halign(Gtk::Align::ALIGN_END);

    _gridStatusBar.add(_statusbarCurrent);
    _gridStatusBar.add(_gridThumbnailing);
    _gridStatusBar.add(_statusbarFree);

    _boxForNotebookAndStatusBar.pack_start(_notebook, true, true);
    _boxForNotebookAndStatusBar.pack_start(_gridStatusBar, false, false);

    _vPaned.set_position(200);
    _vPaned.set_wide_handle(true);
    _vPaned.add1(_folderTreeMgr);
    _vPaned.add2(_boxForNotebookAndStatusBar);

    _mainVBox.pack_start(_toolbar, Gtk::PACK_SHRINK);
    _mainVBox.pack_start(_vPaned);

//     auto pStatusBar = new Gtk::Statusbar();
//     _mainVBox.pack_start(*pStatusBar);
//     pStatusBar->show();

//     int context_id = pStatusBar->get_context_id("Statusbar example");
//     pStatusBar->push("Test", context_id);

    this->add(_mainVBox);

    this->show_all_children();

    _notebook.set_scrollable(true);
    _notebook.popup_enable();

    _boxForNotebookAndStatusBar.show_all();

    /*
     *  Children setup
     */

    _notebook.signal_switch_page().connect([this](Gtk::Widget *pw,
                                                  guint page_number)
    {
        auto p = static_cast<ElissoFolderView*>(pw);
        if (p)
            this->onNotebookTabChanged(*p);
    });


    Glib::ustring data = ".file-ops-success {background-image: radial-gradient(ellipse at center, green 0%, transparent 100%);}";
    auto css = Gtk::CssProvider::create();
    if (!css->load_from_data(data))
      throw FSException("CSS parsing error");
    auto screen = Gdk::Screen::get_default();
    auto ctx = this->get_style_context();
    ctx->add_provider_for_screen(screen, css, GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);

    // Finally, add the window to the application for "quit" management.
    app.add_window(*this);
}

/* virtual */
ElissoApplicationWindow::~ElissoApplicationWindow()
{
    delete _pImpl;
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
    // Add the first page in an idle loop so we have no delay in showing the window.
    Glib::signal_idle().connect([this, pDirOrSymlink]() -> bool
    {
        Debug d(CMD_TOP, "addFolderTab lambda");

        auto pView = this->doAddTab();

        auto p2 = pDirOrSymlink;
        if (!p2)
            p2 = FSModelBase::GetHome();
        pView->setDirectory(p2,
                            SetDirectoryFlag::PUSH_TO_HISTORY); // but not CLICK_FROM_TREE

        return false;       // disconnect, do not call again
    }, Glib::PRIORITY_LOW);
}

void
ElissoApplicationWindow::addFolderTab(const std::string &strError)
{
    Debug d(CMD_TOP, "addFolderTab with error: " + strError);
    auto pView = this->doAddTab();
    pView->setError(strError);
}

/* protected */
ElissoFolderView*
ElissoApplicationWindow::doAddTab()
{
    int iPageInserted;
    auto pView = new ElissoFolderView(*this, iPageInserted);
    pView->show();
    _notebook.set_current_page(iPageInserted);
    _notebook.set_tab_reorderable(*pView, true);
    return pView;
}

void
ElissoApplicationWindow::setWaitCursor(Glib::RefPtr<Gdk::Window> pWindow,
                                       Cursor cursor)
{
    if (pWindow)
    {
        static Glib::RefPtr<Gdk::Cursor> pDefaultCursor;
        static Glib::RefPtr<Gdk::Cursor> pWaitProgressCursor;
        static Glib::RefPtr<Gdk::Cursor> pWaitBlockedCursor;
        if (!pDefaultCursor)
        {
            static Glib::RefPtr<Gdk::Display> pDisplay;
            if (!pDisplay)
                pDisplay = Gdk::Display::get_default();
            if (pDisplay)
            {
                pDefaultCursor = Gdk::Cursor::create(pDisplay, "default");
                pWaitProgressCursor = Gdk::Cursor::create(pDisplay, "progress");
                pWaitBlockedCursor = Gdk::Cursor::create(pDisplay, "wait");
//                 Debug::Log(DEBUG_ALWAYS, "loaded cursors");
            }
        }

        if (pDefaultCursor)
        {
//             Debug::Log(DEBUG_ALWAYS, string(__func__) + ": fWait = " + to_string((int)cursor));
            switch (cursor)
            {
                case Cursor::DEFAULT:
                    pWindow->set_cursor(pDefaultCursor);
                break;
                case Cursor::WAIT_PROGRESS:
                    pWindow->set_cursor(pWaitProgressCursor);
                break;
                case Cursor::WAIT_BLOCKED:
                    pWindow->set_cursor(pWaitBlockedCursor);
                break;
            }

            if (cursor == Cursor::WAIT_BLOCKED)
                // Process remaining events, or the cursor won't change, since the caller plans
                // to block the GUI.
                while (gtk_events_pending()) gtk_main_iteration();
        }
    }
}

void
ElissoApplicationWindow::initActionHandlers()
{
    /*
     *  File menu
     */
    this->addActiveViewActionHandler(ACTION_FILE_NEW_TAB);
    this->addActiveViewActionHandler(ACTION_FILE_NEW_WINDOW);
    this->addActiveViewActionHandler(ACTION_FILE_OPEN_IN_TERMINAL);
    this->addActiveViewActionHandler(ACTION_FILE_CREATE_FOLDER);
    this->addActiveViewActionHandler(ACTION_FILE_CREATE_DOCUMENT);

    this->add_action(ACTION_FILE_QUIT, [this]()
    {
        getApplication().quit();
    });

    this->addActiveViewActionHandler(ACTION_FILE_CLOSE_TAB);

    /*
     *  Edit menu
     */

    _pImpl->pActionEditOpenSelected = this->addActiveViewActionHandler(ACTION_EDIT_OPEN_SELECTED);
    _pImpl->pActionEditOpenSelectedInTab = this->addActiveViewActionHandler(ACTION_EDIT_OPEN_SELECTED_IN_TAB);
    _pImpl->pActionEditOpenSelectedInTerminal = this->addActiveViewActionHandler(ACTION_EDIT_OPEN_SELECTED_IN_TERMINAL);
    _pImpl->pActionEditCopy = this->addActiveViewActionHandler(ACTION_EDIT_COPY);
    _pImpl->pActionEditCut = this->addActiveViewActionHandler(ACTION_EDIT_CUT);
    _pImpl->pActionEditPaste = this->addActiveViewActionHandler(ACTION_EDIT_PASTE);
    _pImpl->pActionEditSelectAll = this->addActiveViewActionHandler(ACTION_EDIT_SELECT_ALL);
    _pImpl->pActionEditRename = this->addActiveViewActionHandler(ACTION_EDIT_RENAME);
    _pImpl->pActionEditTrash = this->addActiveViewActionHandler(ACTION_EDIT_TRASH);

#ifdef USE_TESTFILEOPS
    _pActionEditTestFileops = this->addOneActionHandler(ACTION_EDIT_TEST_FILEOPS);
#endif

    _pImpl->pActionEditProperties = this->addActiveViewActionHandler(ACTION_EDIT_PROPERTIES);

    /*
     *  Tree popup menu items
     */
    this->addTreeActionHandler(ACTION_TREE_OPEN_SELECTED);
    this->addTreeActionHandler(ACTION_TREE_OPEN_SELECTED_IN_TAB);
    this->addTreeActionHandler(ACTION_TREE_OPEN_SELECTED_IN_TERMINAL);
    this->addTreeActionHandler(ACTION_TREE_TRASH_SELECTED);

    /*
     *  View menu
     */
    _pImpl->pActionViewNextTab = this->add_action(ACTION_VIEW_NEXT_TAB, [this]()
    {
        int i = _notebook.get_current_page();
        if (i < _notebook.get_n_pages() - 1)
            _notebook.set_current_page(i + 1);
    });

    _pImpl->pActionViewPreviousTab = this->add_action(ACTION_VIEW_PREVIOUS_TAB, [this]()
    {
        int i = _notebook.get_current_page();
        if (i > 0)
            _notebook.set_current_page(i - 1);
    });

    _pImpl->pActionViewIcons = this->addActiveViewActionHandler(ACTION_VIEW_ICONS);
    _pImpl->pActionViewList = this->addActiveViewActionHandler(ACTION_VIEW_LIST);
    _pImpl->pActionViewCompact = this->addActiveViewActionHandler(ACTION_VIEW_COMPACT);
    _pImpl->pActionViewRefresh = this->addActiveViewActionHandler(ACTION_VIEW_REFRESH);

    /*
     *  Go menu
     */
    _pImpl->pActionGoBack = this->addActiveViewActionHandler(ACTION_GO_BACK);
    _pImpl->pActionGoForward = this->addActiveViewActionHandler(ACTION_GO_FORWARD);
    _pImpl->pActionGoParent = this->addActiveViewActionHandler(ACTION_GO_PARENT);
    _pImpl->pActionGoHome = this->addActiveViewActionHandler(ACTION_GO_HOME);
    this->addActiveViewActionHandler(ACTION_GO_COMPUTER);
    this->addActiveViewActionHandler(ACTION_GO_TRASH);

    /*
     *  Help menu
     */
    this->add_action(ACTION_ABOUT, [this]()
    {
        auto w = Gtk::AboutDialog();
        w.set_version(ELISSO_VERSION);
        w.set_copyright("(C) 2017 Baubadil GmbH");
        w.set_website("http://www.baubadil.de");
        Glib::ustring strComments("Soon to be the best file manager for Linux.");
#ifdef USE_XICONVIEW
        strComments += "\nCompiled with XIconView.";
#endif
        w.set_comments(strComments);
        w.set_license_type(Gtk::License::LICENSE_CUSTOM);
        w.set_license("All rights reserved");
        w.set_logo(_app.getIcon());
        w.set_transient_for(*this);
        w.run();
    });
}

PSimpleAction
ElissoApplicationWindow::addActiveViewActionHandler(const string &strAction)
{
    PSimpleAction p = this->add_action(strAction, [this, &strAction]()
    {
        this->handleActiveViewAction(strAction);
    });

    return p;
}

PSimpleAction
ElissoApplicationWindow::addTreeActionHandler(const string &strAction)
{
    PSimpleAction p = this->add_action(strAction, [this, &strAction]()
    {
        auto &tmgr = this->getTreeMgr();
        tmgr.handleAction(strAction);
    });

    return p;
};

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
 *  Called once on window creation and then via a signal callback whenever the
 *  clipboard contents change. This updates whether the "paste" action is available.
 */
void ElissoApplicationWindow::onClipboardChanged()
{
    Glib::RefPtr<Gtk::Clipboard> pClip = Gtk::Clipboard::get();
    pClip->request_targets([this](const vector<Glib::ustring> &vTargets)
    {
        bool fPaste = false;
        for (auto &s : vTargets)
            if (s == CLIPBOARD_TARGET_GNOME_COPIED_FILES)
            {
                fPaste = true;
                break;
            }

        _pImpl->pActionEditPaste->set_enabled(fPaste);
    });
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
                Debug::Log(DEBUG_ALWAYS, "removing notebook page");
                _notebook.remove_page(*pPageWidget);
                delete pViewThis;
                break;
            }
        }
    }
    else
        this->close();
}

int ElissoApplicationWindow::errorBox(Glib::ustring strMessage)
{
    Gtk::MessageDialog dialog(*this,
                              strMessage,
                              false /* use_markup */,
                              Gtk::MESSAGE_QUESTION,
                              Gtk::BUTTONS_CANCEL);
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

void
ElissoApplicationWindow::enableEditActions(size_t cFolders, size_t cOtherFiles)
{
//     Debug::Log(DEBUG_ALWAYS, "cFolders: " + to_string(cFolders) + ", cOtherFiles: " + to_string(cOtherFiles));
    size_t cTotal = cFolders + cOtherFiles;
    bool fSingleFolder = (cTotal == 1) && (cFolders == 1);
    _pImpl->pActionEditOpenSelected->set_enabled(cTotal == 1);
    _pImpl->pActionEditOpenSelectedInTab->set_enabled(fSingleFolder);
    _pImpl->pActionEditOpenSelectedInTerminal->set_enabled(fSingleFolder);
    _pImpl->pActionEditCopy->set_enabled(cTotal > 0);
    _pImpl->pActionEditCut->set_enabled(cTotal > 0);
    _pImpl->pActionEditRename->set_enabled(cTotal == 1);
    _pImpl->pActionEditTrash->set_enabled(cTotal > 0);
    _pImpl->pActionEditProperties->set_enabled(cTotal == 1);
}

void
ElissoApplicationWindow::enableBackForwardActions()
{
    ElissoFolderView *pActive;
    bool fBack = false, fForward = false;
    if ((pActive = getActiveFolderView()))
    {
        fBack = pActive->canGoBack();
        fForward = pActive->canGoForward();
    }
    _pImpl->pActionGoBack->set_enabled(fBack);
    _pImpl->pActionGoForward->set_enabled(fForward);
}

void
ElissoApplicationWindow::onLoadingFolderView(ElissoFolderView &view)
{
    auto pCurrentViewPage = getActiveFolderView();
    if (pCurrentViewPage && (pCurrentViewPage == &view))
    {
        Glib::ustring strTitle = this->updateWindowTitle(view);

        this->setStatusbarCurrent("Loading " + quote(strTitle) + HELLIP);

        this->enableViewTypeActions(false);
        // Disable all edit actions; they will only get re-enabled when the "selected" signal comes in.
        this->enableEditActions(0, 0);
    }
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
ElissoApplicationWindow::onFolderViewLoaded(ElissoFolderView &view)
{
    this->enableViewTypeActions(true);
}

void
ElissoApplicationWindow::onNotebookTabChanged(ElissoFolderView &view)
{
    this->onFolderViewLoaded(view);
    this->updateWindowTitle(view);
    this->selectInFolderTree(view.getDirectory());
    this->enableViewTabActions();
}

/**
 *  Sets the main window title to the full path of the current folder view.
 *  This is in a separate method because it needs to be called both from
 *  onLoadingFolderView() and onNotebookTabChanged(). Returns the full path
 *  so the caller can use it elsewhere.
 */
Glib::ustring
ElissoApplicationWindow::updateWindowTitle(ElissoFolderView &view)
{
    PFSModelBase pDir = view.getDirectory();

    Glib::ustring strTitle = "?";
    if (pDir)
        strTitle = pDir->getPath();

    this->setWindowTitle(strTitle);

    return strTitle;
}

/**
 *  Updates the left status bar with the "current" text.
 *  This is shared between all folder views, so the folder view calls
 *  this after having composed a meaningful text.
 */
void
ElissoApplicationWindow::setStatusbarCurrent(const Glib::ustring &str)
{
    _statusbarCurrent.pop();       // Remove previous message, if any.
    _statusbarCurrent.push(str);
}

void
ElissoApplicationWindow::setThumbnailerProgress(uint current, uint max, ShowHideOrNothing shn)
{
    if (max && current < max)
        _progressBarThumbnailer.set_fraction((gdouble)current / (gdouble)max);
    else
    {
        _progressBarThumbnailer.set_fraction(1);
        shn = ShowHideOrNothing::HIDE;
    }

    switch (shn)
    {
        case ShowHideOrNothing::SHOW:
            _gridThumbnailing.show();
        break;

        case ShowHideOrNothing::HIDE:
            Glib::signal_timeout().connect([this]() -> bool
            {
                _gridThumbnailing.hide();
                return false; // disconnect
            }, 500);
        break;

        case ShowHideOrNothing::DO_NOTHING:
        break;
    }
}

/**
 *  Updates the left status bar with the "current" text.
 *  This is shared between all folder views, so the folder view calls
 *  this after having composed a meaningful text.
 */
void
ElissoApplicationWindow::setStatusbarFree(const Glib::ustring &str)
{
    _statusbarFree.pop();       // Remove previous message, if any.
    _statusbarFree.push(str);
}

void
ElissoApplicationWindow::selectInFolderTree(PFSModelBase pDir)
{
    if (pDir)
    {
        this->_folderTreeMgr.selectNode(pDir);
    }
}

/**
 *  Handler for the "button press event" signal for
 *
 *   -- the IconView when the folder contents is in icon mode;
 *
 *   -- the TreeViewPlus of the folder contents when in list mode;
 *
 *   -- the TreeViewPlus of the folder tree on the left.
 *
 *  If this returns true, then the event has been handled, and the parent handler should NOT
 *  be called.
 */
bool
ElissoApplicationWindow::onButtonPressedEvent(GdkEventButton *pEvent,
                                              TreeViewPlusMode mode)
{
    if (pEvent->type == GDK_BUTTON_PRESS)
    {
        switch (pEvent->button)
        {
            case 3:
            {
                MouseButton3ClickType clickType = MouseButton3ClickType::WHITESPACE;
                Gtk::TreeModel::Path path;

                if (mode == TreeViewPlusMode::IS_FOLDER_TREE_LEFT)
                {
                    auto &tmgr = this->getTreeMgr();
                    auto &twp = tmgr.getTreeViewPlus();
                    auto pSel = twp.get_selection();
                    if (pSel)
                    {
                        if (twp.get_path_at_pos((int)pEvent->x, (int)pEvent->y, path))
                        {
                            // Select, but without populating the contents.
                            tmgr.suppressSelectHandler(true);
                            pSel->select(path);
                            tmgr.suppressSelectHandler(false);

                            clickType = MouseButton3ClickType::TREE_ITEM_SELECTED;
                            // Call our handler for the popup menu below.
                        }
                        else
                            // Click on white space: do nothing.
                            return true;
                    }
                }
                else
                {
                    auto pActiveView = getActiveFolderView();
                    if (pActiveView)
                        clickType = pActiveView->handleClick(pEvent, path);
                }

                // Open a popup menu.
                this->onMouseButton3Pressed(pEvent, clickType);
                return true;        // do not propagate
            }
            break;

            // GTK+ routes mouse button 8 to the "back" event.
            case 8:
                this->activate_action(ACTION_GO_BACK);
                return true;
            break;

            // GTK+ routes mouse button 9 to the "forward" event.
            case 9:
                this->activate_action(ACTION_GO_FORWARD);
                return true;
            break;

            default:
            break;
        }
    }

    return false;
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
ElissoApplicationWindow::onMouseButton3Pressed(GdkEventButton *pEvent,
                                               MouseButton3ClickType clickType)
{
//     Debug::Log(DEBUG_ALWAYS, string(__FUNCTION__) + "(): clickType = " + to_string((int)clickType));

    auto pMenu = Gio::Menu::create();
    auto pActiveView = getActiveFolderView();
    Gtk::Widget* pAttachMenuTo = pActiveView;

    std::map<std::string, PAppInfo> mapAppInfosForTempMenuItems;

    switch (clickType)
    {
        case MouseButton3ClickType::TREE_ITEM_SELECTED:
        {
            _app.addMenuItem(pMenu, MENUITEM_OPEN, ACTION_TREE_OPEN_SELECTED);
            _app.addMenuItem(pMenu, MENUITEM_OPEN_IN_TAB, ACTION_TREE_OPEN_SELECTED_IN_TAB);
            _app.addMenuItem(pMenu, MENUITEM_OPEN_IN_TERMINAL, ACTION_TREE_OPEN_SELECTED_IN_TERMINAL);
            auto pSubSection = _app.addMenuSection(pMenu);
            _app.addMenuItem(pSubSection, MENUITEM_TRASH, ACTION_TREE_TRASH_SELECTED);
        }
        break;

        case MouseButton3ClickType::SINGLE_ROW_SELECTED:
        case MouseButton3ClickType::MULTIPLE_ROWS_SELECTED:
        {
            FileSelection sel;
            size_t cTotal = pActiveView->getSelection(sel);

            if (cTotal == 1)
            {
                if (sel.vFolders.size() == 1)
                {
                    _app.addMenuItem(pMenu, MENUITEM_OPEN, ACTION_EDIT_OPEN_SELECTED);
                    _app.addMenuItem(pMenu, MENUITEM_OPEN_IN_TAB, ACTION_EDIT_OPEN_SELECTED_IN_TAB);
                    _app.addMenuItem(pMenu, MENUITEM_OPEN_IN_TERMINAL, ACTION_EDIT_OPEN_SELECTED_IN_TERMINAL);
                }
                else
                {
                    PFSModelBase pFS = sel.vOthers.front();
                    if (pFS)
                    {
                        PFsGioFile pFile = g_pFsGioImpl->getFile(pFS);
                        if (pFile)
                        {
                            auto pContentType = ContentType::Guess(pFile);
                            if (pContentType)
                            {
                                auto pDefaultAppInfo = pContentType->getDefaultAppInfo();
                                if (pDefaultAppInfo)
                                {
                                    // The menu item for the default application is easy, it has a a compile-time action.
                                    _app.addMenuItem(pMenu, "Open with " + pDefaultAppInfo->get_name(), ACTION_EDIT_OPEN_SELECTED);

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

            auto pSubSection = _app.addMenuSection(pMenu);
            _app.addMenuItem(pSubSection, "Cut", ACTION_EDIT_CUT);
            _app.addMenuItem(pSubSection, "Copy", ACTION_EDIT_COPY);

            pSubSection = _app.addMenuSection(pMenu);
            if (cTotal == 1)
                _app.addMenuItem(pSubSection, "Rename", ACTION_EDIT_RENAME);
            _app.addMenuItem(pSubSection, MENUITEM_TRASH, ACTION_EDIT_TRASH);
#ifdef USE_TESTFILEOPS
            _app.addMenuItem(pSubSection, "TEST FILEOPS", ACTION_EDIT_TEST_FILEOPS);
#endif

            pSubSection = _app.addMenuSection(pMenu);
            if (cTotal == 1)
                _app.addMenuItem(pSubSection, "Properties", ACTION_EDIT_PROPERTIES);
        }
        break;

        case MouseButton3ClickType::WHITESPACE:
            _app.addMenuItem(pMenu, "Open in terminal", ACTION_FILE_OPEN_IN_TERMINAL);
            auto pSubSection = _app.addMenuSection(pMenu);
            _app.addMenuItem(pSubSection, "Create new folder", ACTION_FILE_CREATE_FOLDER);
            _app.addMenuItem(pSubSection, "Create empty document", ACTION_FILE_CREATE_DOCUMENT);
            _app.addMenuItem(pSubSection, "Paste", ACTION_EDIT_PASTE);
            pSubSection = _app.addMenuSection(pMenu);
            _app.addMenuItem(pSubSection, "Properties", ACTION_FILE_PROPERTIES);
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
                        this->openFile(nullptr, pAppInfo);
                    });
                }
            }
        }
    }

    _pImpl->pPopupMenu->attach_to_widget(*pAttachMenuTo);
    _pImpl->pPopupMenu->popup(pEvent->button, pEvent->time);
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
ElissoApplicationWindow::openFile(PFSModelBase pFS,        //!< in: file or folder to open; if nullptr, get single file from selection
                                  PAppInfo pAppInfo)       //!< in: application to open file with; if nullptr, use file type's default
{
    auto pActiveFolderView = getActiveFolderView();
    if (!pActiveFolderView)
        return;

    if (!pFS)
    {
        FileSelection sel;
        size_t cTotal = pActiveFolderView->getSelection(sel);
        if (cTotal == 1)
        {
            if (sel.vFolders.size() == 1)
                pFS = sel.vFolders.front();
            else
                pFS = sel.vOthers.front();
        }

        if (!pFS)
            return;
    }

    FSTypeResolved t = pFS->getResolvedType();

    switch (t)
    {
        case FSTypeResolved::DIRECTORY:
        case FSTypeResolved::SYMLINK_TO_DIRECTORY:
            pActiveFolderView->setDirectory(pFS, SetDirectoryFlag::PUSH_TO_HISTORY);
        break;

        case FSTypeResolved::FILE:
        case FSTypeResolved::SYMLINK_TO_FILE:
        {
            PFsGioFile pFile = g_pFsGioImpl->getFile(pFS);
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
                    pAppInfo2->launch(g_pFsGioImpl->getGioFile(*pFile));
                else
                    this->errorBox("Cannot determine default application for file \"" + pFS->getPath() + "\"");
            }
        }
        break;

        case FSTypeResolved::MOUNTABLE:
        {
            try
            {
                Glib::RefPtr<Gio::MountOperation> pMountOp;
                Glib::RefPtr<Gio::Cancellable> pCancellable;
                PGioFile pGioFile = g_pFsGioImpl->getGioFile(*pFS);
                pGioFile->mount_mountable( pMountOp,
                                                    [pGioFile, this](Glib::RefPtr<Gio::AsyncResult> &pResult)
                                                    {
                                                        try
                                                        {
                                                            pGioFile->mount_mountable_finish(pResult);
                                                            Debug::Log(DEBUG_ALWAYS, "mount success");
                                                        }
                                                        catch (Gio::Error &e)
                                                        {
                                                            this->errorBox(e.what());
                                                        }
                                                    },
                                                    pCancellable,
                                                    Gio::MOUNT_MOUNT_NONE);
            }
            catch (Gio::Error &e)
            {
                throw FSException(e.what());
            }
        }
        break;

        case FSTypeResolved::BROKEN_SYMLINK:
        case FSTypeResolved::SPECIAL:
        case FSTypeResolved::SYMLINK_TO_OTHER:
        break;
    }
}

void
ElissoApplicationWindow::openFolderInTerminal(PFSModelBase pFS)
{
    auto strPath = pFS->getPath();
    if (startsWith(strPath, "file:///"))
        strPath = strPath.substr(7);
    g_subprocess_new(G_SUBPROCESS_FLAGS_NONE,
                     NULL,
                     "open", "--screen", "auto", strPath.c_str(), nullptr);
}

void
ElissoApplicationWindow::addFileOperation(FileOperationType type,
                                          const FSVector &vFiles,
                                          PFSModelBase pTarget)
{
    FileOperation::Create(type,
                          vFiles,
                          pTarget,
                          _pImpl->llFileOperations,
                          &_pImpl->pProgressDialog,
                          this);
}

bool
ElissoApplicationWindow::areFileOperationsRunning() const
{
    return !!_pImpl->llFileOperations.size();
}

void
ElissoApplicationWindow::enableViewTabActions()
{
    int cCurrent = _notebook.get_current_page();
    _pImpl->pActionViewNextTab->set_enabled(cCurrent < _notebook.get_n_pages() - 1);
    _pImpl->pActionViewPreviousTab->set_enabled(cCurrent > 0);
}

void
ElissoApplicationWindow::enableViewTypeActions(bool f)
{
    _pImpl->pActionViewIcons->set_enabled(f);
    _pImpl->pActionViewList->set_enabled(f);
    _pImpl->pActionViewCompact->set_enabled(f);
    _pImpl->pActionViewRefresh->set_enabled(f);
}

void
ElissoApplicationWindow::handleActiveViewAction(const std::string &strAction)
{
    auto pView = this->getActiveFolderView();
    if (pView)
    {
        if (strAction == ACTION_FILE_NEW_TAB)
        {
            PFSModelBase pDir;
            if ((pDir = pView->getDirectory()))
                this->addFolderTab(pDir);
        }
        else if (strAction == ACTION_FILE_NEW_WINDOW)
        {
            auto p = new ElissoApplicationWindow(_app);
            p->addFolderTab(pView->getDirectory());
            p->present();
        }
        else if (strAction == ACTION_FILE_OPEN_IN_TERMINAL)
            this->openFolderInTerminal(pView->getDirectory());
        else if (strAction == ACTION_FILE_CLOSE_TAB)
            this->closeFolderTab(*pView);
        else if (strAction == ACTION_EDIT_OPEN_SELECTED_IN_TAB)
        {
            auto pFS = pView->getSelectedFolder();
            if (pFS)
                this->addFolderTab(pFS);
        }
        else if (strAction == ACTION_EDIT_OPEN_SELECTED_IN_TERMINAL)
        {
            auto pFS = pView->getSelectedFolder();
            if (pFS)
                this->openFolderInTerminal(pFS);
        }
        else
        {
            static std::map<string, FolderAction> mapActions =
            {
                { ACTION_EDIT_COPY, FolderAction::EDIT_COPY },
                { ACTION_EDIT_CUT, FolderAction::EDIT_CUT },
                { ACTION_EDIT_PASTE, FolderAction::EDIT_PASTE },
                { ACTION_EDIT_SELECT_ALL, FolderAction::EDIT_SELECT_ALL },
                { ACTION_EDIT_OPEN_SELECTED, FolderAction::EDIT_OPEN_SELECTED },
                { ACTION_FILE_CREATE_FOLDER, FolderAction::FILE_CREATE_FOLDER },
                { ACTION_FILE_CREATE_DOCUMENT, FolderAction::FILE_CREATE_DOCUMENT },
                { ACTION_EDIT_RENAME, FolderAction::EDIT_RENAME },
                { ACTION_EDIT_TRASH, FolderAction::EDIT_TRASH },
#ifdef USE_TESTFILEOPS
                { ACTION_EDIT_TEST_FILEOPS, FolderAction::EDIT_TEST_FILEOPS },
#endif
                { ACTION_VIEW_ICONS, FolderAction::VIEW_ICONS },
                { ACTION_VIEW_LIST, FolderAction::VIEW_LIST },
                { ACTION_VIEW_COMPACT, FolderAction::VIEW_COMPACT },
                { ACTION_VIEW_REFRESH, FolderAction::VIEW_REFRESH },
                { ACTION_GO_BACK, FolderAction::GO_BACK },
                { ACTION_GO_FORWARD, FolderAction::GO_FORWARD },
                { ACTION_GO_PARENT, FolderAction::GO_PARENT },
                { ACTION_GO_HOME, FolderAction::GO_HOME },
                { ACTION_GO_COMPUTER, FolderAction::GO_COMPUTER },
                { ACTION_GO_TRASH, FolderAction::GO_TRASH },
            };

            // Forward all others to currently active folder view.
            auto it = mapActions.find(strAction);
            if (it != mapActions.end())
                pView->handleAction(it->second);
            else
                this->errorBox("View action " + quote(strAction) + " not implemented yet");
        }
    }
}
