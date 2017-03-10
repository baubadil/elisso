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

    ElissoApplication& getApplication()
    {
        return _app;
    }

    void setSizeAndPosition();

    int errorBox(Glib::ustring strMessage);

    Gtk::Notebook& getNotebook()
    {
        return _notebook;
    }

    PElissoFolderView getActiveFolderView() const;

    void enableActions();

protected:
    void addFolderTab(PFSDirectory pDir);

    Gtk::ToolButton* makeToolButton(const Glib::ustring &strIconName,
                                    PSimpleAction pAction);

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

    int                             _x = 0, _y = 0, _width = 100, _height = 100;
    bool                            _fIsMaximized = false,
                                    _fIsFullscreen = false;

    Gtk::Box                        _mainVBox;
    Gtk::Paned                      _vPaned;
    ElissoTreeView                  _treeViewLeft;
    Gtk::Notebook                   _notebook;

    std::vector<PElissoFolderView>  _aFolderViews;
};

#endif // ELISSO_MAINWINDOW_H
