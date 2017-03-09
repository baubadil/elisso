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
 *  TreeJob
 *
 **************************************************************************/

typedef size_t TreeJobID;

// class TreeJob;
// typedef std::shared_ptr<TreeJob>        PTreeJob;


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
class TreeJob
{
public:
    static TreeJob* Create(Glib::RefPtr<Gtk::TreeStore> pTreeStore,
                           PFSDirectory pDir,                       //!< in: actual directory (resolved if from symlink)
                           const Gtk::TreeModel::iterator it);      //!< in: tree iterator to insert children under

    TreeJob(Glib::RefPtr<Gtk::TreeStore> pTreeStore,
            PFSDirectory pDir);
    TreeJob(const TreeJob &) = delete;

    virtual ~TreeJob();

protected:
    Gtk::TreeModel::iterator getIterator(PRowReference &pRowRef);
    PRowReference getRowReference(Gtk::TreeModel::iterator &it);

    void spawnPopulate();

    void onPopulateDone();
    void spawnAddFirstFolders(PAddOneFirstsList pllToAddFirst);
    void onAddAnotherFirst();

    TreeJobID                               _id;
    std::string                             _strPath;       // for debugging
    Glib::RefPtr<Gtk::TreeStore>            _pTreeStore;
    PFSDirectory                            _pDir;
    std::shared_ptr<Gtk::TreeRowReference>  _pRowReference;
    FSList                                  _llContents;

    PWorkerThread                           _pPopulateThread;
    PWorkerThread                           _pAddFirstThread;

    bool                                    _fCleanedUp = false;
};


#endif // ELISSO_TREEJOB_H
