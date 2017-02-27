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

/***************************************************************************
 *
 *  ElissoApplicationWindow
 *
 **************************************************************************/

class ElissoApplicationWindow : public Gtk::ApplicationWindow
{
public:
    ElissoApplicationWindow(const Glib::ustring &strInitialPath);
    virtual ~ElissoApplicationWindow();

protected:
    Gtk::Paned          _vPaned;
    ElissoTreeView      _treeView;
    ElissoFolderView    _folderView;

};

#endif // ELISSO_MAINWINDOW_H
