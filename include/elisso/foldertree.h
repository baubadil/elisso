/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
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
 *  ElissoFolderTreeMgr
 *
 **************************************************************************/

/**
 *  The ElissoFolderTreeMgr occupies the left third or so of the folder window
 *  and contains the tree of folders. On the one hand, this reacts to folders
 *  clicked on in the view on the right; on the other hand, it sets the
 *  folder to view on the right when a folder is clicked on the left.
 *
 *  The folder tree can have a lot of background threads that insert subfolders
 *  into the tree when items get expanded, but this happens automatically.
 *  There are few public methods, everything else happens under the hood.
 */
class ElissoFolderTreeMgr : public Gtk::ScrolledWindow
{
public:
    ElissoFolderTreeMgr(ElissoApplicationWindow &mainWindow);

    virtual ~ElissoFolderTreeMgr();

    /**
     *  Adds a new tree root (toplevel folder) to the tree. This gets called
     *  on setup to insert the "Home" and "File system" trees under which
     *  additional items get inserted over time depending on what is selected.
     */
    void addTreeRoot(const Glib::ustring &strName,
                     PFsDirectory pDir);

    /**
     *  Called from ElissoApplicationWindow::selectInFolderTree() after the notebook page on the
     *  right has finished populating to select the node in the tree that corresponds to the folder
     *  contents being displayed.
     *
     *  If the given directory is already inserted into the tree, for example on startup because it's
     *  the user's home directory, it simply gets selected and then expanded. However, if it is not
     *  yet inserted, this needs to spawn a populate thread first for the parent. If the parent hasn't
     *  been inserted either, that needs to be populated first. So this goes through the path of
     *  the given directory and checks each component; if it is inserted already, it is expanded;
     *  if it is not yet inserted, it is populated and then expanded. This can spawn many threads
     *  as a result.
     *
     *  Example: if $(HOME)/subdir is showing on the right, we expand the $(HOME) item in the tree
     *  and select the "subdir" node under it.
     */
    void selectNode(PFsObject pDir);

    /**
     *  Can be called to temporarily suppress the "on select" handler which
     *  populates the folder contents on the right.
     */
    void suppressSelectHandler(bool fSuppress);

    TreeViewPlus& getTreeViewPlus()
    {
        return _treeView;
    }

    /**
     *  Called from the main window when an action is invoked from the tree view's
     *  popup menu.
     */
    void handleAction(const string &strAction);

private:
    friend class FolderTreeMonitor;

    /**
     *  Spawns a thread to add or refresh mounts (volumes) to the tree view.
     *  Gets called by the constructor.
     */
    void spawnGetMountables();

    void onGetMountablesDone();

    /**
     *  Returns the currently selected folder (or symlink to one) from the
     *  folder tree. Returns nullptr if nothing is selected or the selection
     *  is not a folder.
     */
    PFsObject getSelectedFolder();

    /**
     *  Protected handler called from the lambda for the 'changed' signal. This gets called
     *  every time the selected node in the tree changes. We then want to set the same
     *  directory for the folder list.
     */
    void onNodeSelected();

    /**
     *  Protected handler called from the lambda for the 'row-expanded' signal. This gets
     *  called every time a node gets expanded in the tree, which happens in two situations:
     *
     *   -- The user actually clicked on the expander icon.
     *
     *   -- Indirectly from the selectNode() method, which expands every parent directory
     *      of the node being selected before actually selecting the node. So that can
     *      trigger several signals in quick succession.
     *
     *  For every such signal, we spawn a populate thread via spawnPopulate() if the node's
     *  folder has not yet been been populated.
     */
    void onNodeExpanded(const Gtk::TreeModel::iterator &it,
                        const Gtk::TreeModel::Path &path);

    /**
     *  Spawns a thread to populate the tree node represented by the given iterator
     *  with subfolders.
     *
     *  This gets called from
     *
     *   -- selectNode() for the tree root;
     *
     *   -- onNodeExpanded() when a node gets expanded (either by the user or automatically
     *      as a result of selectNode() expanding subnodes).
     *
     *  Returns true if the thread was actually started. This is will not happen if the
     *  folder was already populated with subfolders or if the directory is invalid.
     */
    bool spawnPopulate(PFolderTreeModelRow pRow);

    /**
     *  Called on the GUI thread by the dispatcher when the populate thread started by
     *  spawnPopulate() is done.
     */
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
 *  FsMonitorBase subclass tailored to the folder tree on the left of the screen.
 *
 *  Whenever we populate a folder in the tree, either completely with folders because
 *  it has been expanded or with a first folder to add the expander icon, we create
 *  a monitor for the directory that was populated so we can remove subfolders
 *  if necessary.
 */
class FolderTreeMonitor : public FsMonitorBase
{
public:
    FolderTreeMonitor(ElissoFolderTreeMgr &tree,
                      PFolderTreeModelRow &pRow)
        : FsMonitorBase(),
          _tree(tree),
          _pRowWatching(pRow)
    { };

    virtual void onItemAdded(PFsObject &pFS) override;
    virtual void onItemRemoved(PFsObject &pFS) override;
    virtual void onItemRenamed(PFsObject &pFS, const std::string &strOldName, const std::string &strNewName) override;

private:
    ElissoFolderTreeMgr &_tree;
    PFolderTreeModelRow _pRowWatching;
};
typedef shared_ptr<FolderTreeMonitor> PFolderTreeMonitor;


#endif // ELISSO_FOLDERTREE_H
