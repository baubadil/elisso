/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_FOLDERTREE_H
#define ELISSO_FOLDERTREE_H

#include <gtkmm.h>

#include "elisso/fsmodel.h"

class ElissoApplicationWindow;


/***************************************************************************
 *
 *  ElissoTreeView
 *
 **************************************************************************/

class ElissoTreeView : public Gtk::ScrolledWindow
{
public:
    ElissoTreeView(ElissoApplicationWindow &mainWindow);

    virtual ~ElissoTreeView();

    void addTreeRoot(const Glib::ustring &strName,
                     PFSDirectory pDir);

private:
    bool spawnPopulate(Gtk::TreeModel::iterator &it);
    void onPopulateDone();

    ElissoApplicationWindow     &_mainWindow;
    Gtk::TreeView               _treeView;

    struct Impl;
    Impl                        *_pImpl;
};

#endif // ELISSO_FOLDERTREE_H
