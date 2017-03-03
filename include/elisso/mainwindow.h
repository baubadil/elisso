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

#include <gtkmm.h>

#include "elisso/folderview.h"

class ElissoAction;
typedef Glib::RefPtr<Gio::SimpleAction> PSimpleAction;


/***************************************************************************
 *
 *  ElissoApplicationWindow
 *
 **************************************************************************/

class ElissoApplicationWindow : public Gtk::ApplicationWindow
{
public:
    ElissoApplicationWindow(Gtk::Application &app,
                            const std::string &strInitialPath);
    virtual ~ElissoApplicationWindow();

    int errorBox(Glib::ustring strMessage);

protected:
    friend class ElissoFolderView;

    Gtk::Application    &_app;
    Gtk::Box            _mainVBox;
    Gtk::Paned          _vPaned;
    ElissoTreeView      _treeViewLeft;
    Gtk::Notebook       _notebook;

    PSimpleAction       _pActionGoBack;
    Gtk::ToolButton     *_pButtonGoBack;
    PSimpleAction       _pActionGoForward;
    Gtk::ToolButton     *_pButtonGoForward;
    PSimpleAction       _pActionGoParent;
    Gtk::ToolButton     *_pButtonGoParent;

    void addTab(PFSDirectory pDir);
    PElissoFolderView getActiveFolderView() const;

    Gtk::Notebook& getNotebook()
    {
        return _notebook;
    }

    Gtk::ToolButton* makeToolButton(const Glib::ustring &strIconName,
                                    PSimpleAction pAction);

    std::vector<PElissoFolderView> _aFolderViews;
};

#endif // ELISSO_MAINWINDOW_H
