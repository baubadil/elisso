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
#include "elisso/mainwindow.h"

ElissoApplicationWindow::ElissoApplicationWindow(Gtk::Application &app,
                                                 PFSDirectory pdirInitial)      //!< in: initial directory or nullptr for "home"
    : _app(app),
      _mainVBox(Gtk::ORIENTATION_VERTICAL),
      _vPaned(),
      _treeViewLeft(*this),
      _notebook()
{
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
        w.set_copyright("(C) 2017 Baubadil GmbH");
        w.set_comments("Soon to be the best file manager for Linux.");
        w.set_license("All rights reserved");
        w.set_transient_for(*this);
        w.run();
    });

//     this->set_border_width(10);
    this->set_default_size(1000, 600);

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

