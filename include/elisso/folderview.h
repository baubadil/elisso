/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_FOLDERVIEW_H
#define ELISSO_FOLDERVIEW_H

#include <gtkmm.h>

class FSLock;

/***************************************************************************
 *
 *  ElissoFolderView
 *
 **************************************************************************/

class ElissoFolderView : public Gtk::ScrolledWindow
{
public:
    ElissoFolderView();
    virtual ~ElissoFolderView();

    bool setPath(const std::string &strPath);

    bool spawnPopulate();

    void populate(FSLock &lock);

private:
    void onPopulateDone();

    Gtk::TreeView   treeview;
    std::string     _strPath;

    struct Impl;
    Impl            *_pImpl;
};

#endif // ELISSO_FOLDERVIEW_H
