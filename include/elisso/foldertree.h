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

#include "elisso/elisso.h"
#include "elisso/fsmodel.h"
#include "elisso/treeviewplus.h"

class ElissoApplicationWindow;
class ElissoFolderView;

struct AddOneFirst;
typedef std::shared_ptr<AddOneFirst> PAddOneFirst;
typedef std::list<PAddOneFirst> AddOneFirstsList;
typedef std::shared_ptr<AddOneFirstsList> PAddOneFirstsList;


/***************************************************************************
 *
 *  ElissoFolderTree
 *
 **************************************************************************/

class ElissoFolderTree : public Gtk::ScrolledWindow
{
public:
    ElissoFolderTree(ElissoApplicationWindow &mainWindow);

    virtual ~ElissoFolderTree();

    void addTreeRoot(const Glib::ustring &strName,
                     PFSDirectory pDir);

    void select(PFSModelBase pDir);

private:
    friend class FolderTreeMonitor;
    bool spawnPopulate(const Gtk::TreeModel::iterator &it);
    void onPopulateDone();

    Gtk::TreeModel::iterator insertNode(const Glib::ustring &strName,
                                        PFSModelBase pFS,
                                        const Gtk::TreeNodeChildren &children);
    void addMonitor(Gtk::TreeModel::iterator it);

    void spawnAddFirstSubfolders(PAddOneFirstsList pllToAddFirst);
    void onAddAnotherFirst();

    void onNodeSelected();
    void onNodeExpanded(const Gtk::TreeModel::iterator &it,
                        const Gtk::TreeModel::Path &path);

    Gtk::TreeModel::iterator getIterator(const PRowReference &pRowRef);
    PRowReference getRowReference(const Gtk::TreeModel::iterator &it);

    ElissoApplicationWindow     &_mainWindow;
    TreeViewPlus                _treeView;

    struct Impl;
    Impl                        *_pImpl;
};


/***************************************************************************
 *
 *  FolderTreeMonitor
 *
 **************************************************************************/

/**
 *  FSMonitorBase subclass tailored to the folder tree on the left of the screen.
 *
 *  Whenever we populate a folder in the tree, either completely with folders because
 *  it has been expanded or with a first folder to add the expander icon, we create
 *  a monitor for the directory that was populated so we can remove subfolders
 *  if necessary.
 */
class FolderTreeMonitor : public FSMonitorBase
{
public:
    FolderTreeMonitor(ElissoFolderTree &tree,
                      PRowReference pRowRefDirWatching,
                      PFSModelBase pDirWatching)
        : FSMonitorBase(),
          _tree(tree),
          _pRowRefDirWatching(pRowRefDirWatching),
          _pDirWatching(pDirWatching)
    { };

    virtual void onItemAdded(PFSModelBase &pFS) override;
    virtual void onItemRemoved(PFSModelBase &pFS) override;

private:
    ElissoFolderTree &_tree;
    PRowReference _pRowRefDirWatching;
    PFSModelBase _pDirWatching;
};
typedef shared_ptr<FolderTreeMonitor> PFolderTreeMonitor;

#endif // ELISSO_FOLDERTREE_H
