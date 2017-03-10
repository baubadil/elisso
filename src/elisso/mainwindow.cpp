/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/elisso.h"
#include "elisso/application.h"
#include "elisso/mainwindow.h"

ElissoApplicationWindow::ElissoApplicationWindow(ElissoApplication &app,
                                                 PFSDirectory pdirInitial)      //!< in: initial directory or nullptr for "home"
    : _app(app),
      _mainVBox(Gtk::ORIENTATION_VERTICAL),
      _vPaned(),
      _treeViewLeft(*this),
      _notebook()
{
    /*
     *  Connect to actions.
     */
    this->add_action(ACTION_FILE_QUIT, [this](){
        get_application()->quit();
    });

    this->add_action(ACTION_VIEW_ICONS, [this](){
        auto p = this->getActiveFolderView();
        if (p)
            p->setViewMode(FolderViewMode::ICONS);
    });

    this->add_action(ACTION_VIEW_LIST, [this](){
        auto p = this->getActiveFolderView();
        if (p)
            p->setViewMode(FolderViewMode::LIST);
    });

    this->add_action(ACTION_VIEW_COMPACT, [this](){
        auto p = this->getActiveFolderView();
        if (p)
            p->setViewMode(FolderViewMode::COMPACT);
    });

    _pActionGoBack = this->add_action(ACTION_GO_BACK, [this](){
        auto pFolderView = this->getActiveFolderView();
        if (pFolderView)
            pFolderView->goBack();
    });

    _pActionGoForward = this->add_action(ACTION_GO_FORWARD, [this](){
        auto pFolderView = this->getActiveFolderView();
        if (pFolderView)
            pFolderView->goForward();
    });

    _pActionGoParent = this->add_action(ACTION_GO_PARENT, [this](){
        auto pFolderView = this->getActiveFolderView();
        if (pFolderView)
        {
            PFSModelBase pDir;
            if ((pDir = pFolderView->getDirectory()))
                if ((pDir = pDir->getParent()))
                    pFolderView->setDirectory(pDir);
        }
    });

    this->add_action(ACTION_GO_HOME, [this](){
        auto p = this->getActiveFolderView();
        if (p)
        {
            FSLock lock;
            auto pHome = FSDirectory::GetHome(lock);
            if (pHome)
                p->setDirectory(pHome);
        }
    });

    this->add_action(ACTION_ABOUT, [this](){
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

    /*
     *  Window setup
     */
    this->setSizeAndPosition();

    this->set_icon(app.getIcon());


    /*
     *  Children setup
     */

    _vPaned.set_position(200);
    _vPaned.set_wide_handle(true);
    _vPaned.add1(_treeViewLeft);
    _vPaned.add2(_notebook);

    Gtk::Toolbar* pToolbar = new Gtk::Toolbar();
    _mainVBox.pack_start(*pToolbar, Gtk::PACK_SHRINK);

    _pButtonGoBack = makeToolButton("go-previous", _pActionGoBack);
    pToolbar->append(*_pButtonGoBack);
    _pButtonGoForward = makeToolButton("go-next", _pActionGoForward);
    pToolbar->append(*_pButtonGoForward);
    _pButtonGoParent = makeToolButton("go-up", _pActionGoParent);
    pToolbar->append(*_pButtonGoParent);

    this->signal_action_enabled_changed().connect([this](const Glib::ustring &strAction, bool fEnabled)
    {
        Debug::Log(DEBUG_ALWAYS, "enabled changed: " + strAction);
        if (strAction == ACTION_GO_BACK)
            _pButtonGoBack->set_sensitive(fEnabled);
        else if (strAction == ACTION_GO_FORWARD)
            _pButtonGoForward->set_sensitive(fEnabled);
        else if (strAction == ACTION_GO_PARENT)
            _pButtonGoParent->set_sensitive(fEnabled);
    });

    _mainVBox.pack_start(_vPaned);

//     auto pStatusBar = new Gtk::Statusbar();
//     _mainVBox.pack_start(*pStatusBar);
//     pStatusBar->show();

//     int context_id = pStatusBar->get_context_id("Statusbar example");
//     pStatusBar->push("Test", context_id);

    this->add(_mainVBox);

    this->show_all_children();

    this->addFolderTab(pdirInitial);
    _notebook.show_all();
}

void ElissoApplicationWindow::setSizeAndPosition()
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

Gtk::ToolButton* ElissoApplicationWindow::makeToolButton(const Glib::ustring &strIconName,
                                                         PSimpleAction pAction)
{
    Gtk::ToolButton *pButton = nullptr;
    if (pAction)
    {
        auto pImage = new Gtk::Image();
        pImage->set_from_icon_name(strIconName,
                                Gtk::BuiltinIconSize::ICON_SIZE_SMALL_TOOLBAR);
        pButton = Gtk::manage(new Gtk::ToolButton(*pImage));
        // Connect to "clicked" signal on button.
        pButton->signal_clicked().connect([this, pAction]()
        {
            this->activate_action(pAction->get_name());
        });
    }
    return pButton;
}

/* virtual */
void ElissoApplicationWindow::on_size_allocate(Gtk::Allocation& allocation) /* override */
{
    ApplicationWindow::on_size_allocate(allocation);

    if (!_fIsMaximized && !_fIsFullscreen)
    {
        this->get_position(_x, _y);
        this->get_size(_width, _height);
    }
}

/* virtual */
bool ElissoApplicationWindow::on_window_state_event(GdkEventWindowState *ev) /* override */
{
    ApplicationWindow::on_window_state_event(ev);

    _fIsMaximized = (ev->new_window_state & GDK_WINDOW_STATE_MAXIMIZED) != 0;
    _fIsFullscreen = (ev->new_window_state & GDK_WINDOW_STATE_FULLSCREEN) != 0;

    return false; // propagate
}

/* virtual */
bool ElissoApplicationWindow::on_delete_event(GdkEventAny *ev) /* override */
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
 *  Adds a new tab to the GTK notebook in the right pane with an ElissoFolderView
 *  for the given directory inside.
 *
 *  If pDir is nullptr, we retrieve the user's home directory from the FS backend.
 */
void ElissoApplicationWindow::addFolderTab(PFSDirectory pDir)       //!< in: directory to open, or nullptr for "home"
{
    ElissoFolderView *pFolderView = new ElissoFolderView(*this);
    _aFolderViews.push_back(PElissoFolderView(pFolderView));

    if (!pDir)
    {
        FSLock lock;
        pDir = FSDirectory::GetHome(lock);
    }

    _notebook.append_page(*pFolderView, pDir->getBasename());
    pFolderView->setDirectory(pDir);
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

PElissoFolderView ElissoApplicationWindow::getActiveFolderView() const
{
    auto i = _notebook.get_current_page();
    if (i != -1)
        return _aFolderViews[i];
    return PElissoFolderView(NULL);
}

void ElissoApplicationWindow::enableActions()
{
    PElissoFolderView pActive;
    if ((pActive = getActiveFolderView()))
    {
        _pActionGoBack->set_enabled(pActive->canGoBack());
        _pActionGoForward->set_enabled(pActive->canGoForward());
    }
}

