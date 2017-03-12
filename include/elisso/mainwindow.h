/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_MAINWINDOW_H
#define ELISSO_MAINWINDOW_H

#include "elisso/application.h"
#include "elisso/folderview.h"
#include "elisso/foldertree.h"

class ElissoAction;
typedef Glib::RefPtr<Gio::SimpleAction> PSimpleAction;

class ElissoApplication;


/***************************************************************************
 *
 *  ElissoApplicationWindow
 *
 **************************************************************************/

/**
 *  Window hierarchy:
 *
 *  ElissoApplicationWindow
 *   |
 *   +- GtkMenuBar
 *   |
 *   +- GtkBox
 *       |
 *       +- GtkToolbar
 *       |
 *       +- GtkPaned: to split between tree (left) and notebook (right)
 *           |
 *           +- ElissoTreeView (subclass of GtkScrolledWindow)
 *           |
 *           +- GtkNotebook
 *               |
 *               +- ElissoFolderView (subclass of GtkScrolledWindow)
 *               |   |
 *               |   +- GtkTreeView (list view)
 *               |
 *               +- ElissoFolderView (subclass of GtkScrolledWindow)
 *                   |
 *                   +- GtkTreeView (list view)
 *
 *
 *
 *
 */
class ElissoApplicationWindow : public Gtk::ApplicationWindow
{
public:
    ElissoApplicationWindow(ElissoApplication &app,
                            PFSDirectory pdirInitial);
    virtual ~ElissoApplicationWindow();

    /**
     *  Returns the ElissoApplication that created us.
     */
    ElissoApplication& getApplication()
    {
        return _app;
    }

    int errorBox(Glib::ustring strMessage);

    /**
     *  Returns the ElissoTreeView that makes up the left of the member Gtk::Paned.
     */
    ElissoTreeView& getTreeView()
    {
        return _treeViewLeft;
    }

    /**
     *  Returns the notebook that makes up the right of the member Gtk::Paned, which in turns
     *  has ElissoFolderView widgets as notebook pages.
     */
    Gtk::Notebook& getNotebook()
    {
        return _notebook;
    }

    ElissoFolderView* getActiveFolderView();

    void enableActions();

    void onLoadingFolderView(ElissoFolderView &view);
    void onFolderViewReady(ElissoFolderView &view);

protected:
    void initActionHandlers();
    void setSizeAndPosition();

    void addFolderTab(PFSModelBase pDirOrSymlink);

    void closeFolderTab(ElissoFolderView &viewClose);

    Gtk::ToolButton* makeToolButton(const Glib::ustring &strIconName,
                                    PSimpleAction pAction,
                                    bool fAlignRight = false);

    // Override window signal handlers
    virtual void on_size_allocate(Gtk::Allocation& allocation) override;
    virtual bool on_window_state_event(GdkEventWindowState *) override;
    virtual bool on_delete_event(GdkEventAny *) override;

    ElissoApplication               &_app;

    PSimpleAction                   _pActionGoBack;
    Gtk::ToolButton                 *_pButtonGoBack;
    PSimpleAction                   _pActionGoForward;
    Gtk::ToolButton                 *_pButtonGoForward;
    PSimpleAction                   _pActionGoParent;
    Gtk::ToolButton                 *_pButtonGoParent;
    PSimpleAction                   _pActionGoHome;
    Gtk::ToolButton                 *_pButtonGoHome;
    PSimpleAction                   _pActionViewIcons;
    Gtk::ToolButton                 *_pButtonViewIcons;
    PSimpleAction                   _pActionViewList;
    Gtk::ToolButton                 *_pButtonViewList;
    PSimpleAction                   _pActionViewRefresh;
    Gtk::ToolButton                 *_pButtonViewRefresh;

    int                             _x = 0, _y = 0, _width = 100, _height = 100;
    bool                            _fIsMaximized = false,
                                    _fIsFullscreen = false;

    Gtk::Box                        _mainVBox;
    Gtk::Paned                      _vPaned;
    ElissoTreeView                  _treeViewLeft;
    Gtk::Notebook                   _notebook;
};

#endif // ELISSO_MAINWINDOW_H
