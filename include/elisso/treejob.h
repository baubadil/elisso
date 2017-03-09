/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_TREEJOB_H
#define ELISSO_TREEJOB_H

#include <deque>

#include <gtkmm.h>

#include "elisso/fsmodel.h"
#include "xwp/worker.h"


/***************************************************************************
 *
 *  FolderTreeModelColumns (private)
 *
 **************************************************************************/

enum class TreeNodeState
{
    UNKNOWN,
    POPULATING,
    POPULATED_WITH_FIRST,
    POPULATED_WITH_FOLDERS
};

class FolderTreeModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    FolderTreeModelColumns()
    {
        add(_colIconAndName);
        add(_colPDir);
        add(_colState);
    }

    Gtk::TreeModelColumn<Glib::ustring>             _colIconAndName;
    Gtk::TreeModelColumn<PFSModelBase>              _colPDir;
    Gtk::TreeModelColumn<TreeNodeState>             _colState;

    static FolderTreeModelColumns& Get()
    {
        if (!s_p)
            s_p = new FolderTreeModelColumns;
        return *s_p;
    }

private:
    static FolderTreeModelColumns *s_p;
};


/***************************************************************************
 *
 *  TreeJob
 *
 **************************************************************************/

typedef size_t TreeJobID;

class TreeJob;
typedef std::shared_ptr<TreeJob>        PTreeJob;
typedef std::map<TreeJobID, PTreeJob>   JobsMap;

struct AddOneFirst;
typedef std::shared_ptr<AddOneFirst> PAddOneFirst;
typedef std::list<PAddOneFirst> AddOneFirstsList;
typedef std::shared_ptr<AddOneFirstsList> PAddOneFirstsList;

/**
 *  A tree job gets called when a folder node in the tree needs to be filled with
 *  subfolders, especially when the folder node was just expanded (the "+"
 *  sign was clicked on).
 *
 *  It does two things:
 *
 *   1) First it populates the node itself with folders and inserts all
 *      subfolders under the node that was expanded.
 *
 *   2) It then does a "populate with first subfolder" for every sub-node
 *      to be able to add a "+" sign next to the sub-nodes that have
 *      subfolders.
 */
class TreeJob : public std::enable_shared_from_this<TreeJob>
{
public:
    static PTreeJob Create(Glib::RefPtr<Gtk::TreeStore> &pTreeStore,
                           PFSDirectory &pDir,                       //!< in: actual directory (resolved if from symlink)
                           const Gtk::TreeModel::iterator it);      //!< in: tree iterator to insert children under

    TreeJob(Glib::RefPtr<Gtk::TreeStore> &pTreeStore,
            PFSDirectory &pDir);
    TreeJob(const TreeJob &) = delete;

    virtual ~TreeJob();

protected:
    Gtk::TreeModel::iterator getIterator();

    void spawnPopulate();

    void onPopulateDone();
    void spawnAddFirstFolders(PAddOneFirstsList pllToAddFirst);
    void onAddAnotherFirst();
    void cleanUp();

    TreeJobID                               _id;
    std::string                             _strPath;       // for debugging
    Glib::RefPtr<Gtk::TreeStore>            _pTreeStore;
    PFSDirectory                            _pDir;
    std::shared_ptr<Gtk::TreeRowReference>  _pRowReference;
    PFSList                                 _pllContents;

    // GUI thread dispatcher for when a folder populate is done.
    Glib::Dispatcher                        _dispatcherPopulateDone;
    // GUI thread dispatcher for when the "add first" thread has finished some work.
    Glib::Dispatcher                        _dispatcherAddFirst;

    /* This queue is used to communicate between the "add first" thread and the GUI thread.
     * For every item that's pushed to the back of the queue, _dispatcherAddFirst must be fired once.
     * To signal that the thread is done, it pushes a nullptr and fires _dispatcherAddFirst one last time.
     */
    std::deque<PAddOneFirst>                _dequeAddFirst;
    PWorkerThread                           _pAddFirstThread;

    bool                                    _fCleanedUp = false;

    static JobsMap                          s_mapJobs;
};


#endif // ELISSO_TREEJOB_H
