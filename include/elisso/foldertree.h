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
#include "elisso/treemodel.h"
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

/**
 *  The ElissoFolderTree occupies the left third or so of the folder window
 *  and contains the tree of folders. On the one hand, this reacts to folders
 *  clicked on in the view on the right; on the other hand, it sets the
 *  folder to view on the right when a folder is clicked on the left.
 *
 *  The folder tree can have a lot of background threads that insert subfolders
 *  into the tree when items get expanded, but this happens automatically.
 *  There are few public methods, everything else happens under the hood.
 */
class ElissoFolderTree : public Gtk::ScrolledWindow
{
public:
    ElissoFolderTree(ElissoApplicationWindow &mainWindow);

    virtual ~ElissoFolderTree();

    void addTreeRoot(const Glib::ustring &strName,
                     PFSDirectory pDir);

    void selectNode(PFSModelBase pDir);

private:
    friend class FolderTreeMonitor;

    void onNodeSelected();
    void onNodeExpanded(const Gtk::TreeModel::iterator &it,
                        const Gtk::TreeModel::Path &path);

    bool spawnPopulate(PFolderTreeModelRow pRow);
    void onPopulateDone();

    void addMonitor(PFolderTreeModelRow pRow);

    void spawnAddFirstSubfolders(PAddOneFirstsList pllToAddFirst);
    void onAddAnotherFirst();

    void updateCursor();

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
                      PFolderTreeModelRow &pRow)
        : FSMonitorBase(),
          _tree(tree),
          _pRow(pRow)
    { };

    virtual void onItemAdded(PFSModelBase &pFS) override;
    virtual void onItemRemoved(PFSModelBase &pFS) override;
    virtual void onItemRenamed(PFSModelBase &pFS, const std::string &strOldName, const std::string &strNewName) override;

private:
    Gtk::TreeModel::iterator findIterator(PFSModelBase &pFS);

    ElissoFolderTree &_tree;
    PFolderTreeModelRow _pRow;
};
typedef shared_ptr<FolderTreeMonitor> PFolderTreeMonitor;


#endif // ELISSO_FOLDERTREE_H
