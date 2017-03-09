/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/elisso.h"
#include "elisso/treejob.h"
#include "xwp/except.h"

#include <atomic>
#include <mutex>
#include <thread>


/***************************************************************************
 *
 *  Globals
 *
 **************************************************************************/

std::atomic<std::uint64_t>  g_uJobID(1);




/***************************************************************************
 *
 *  TreeJob
 *
 **************************************************************************/

/* static */
TreeJob* TreeJob::Create(Glib::RefPtr<Gtk::TreeStore> pTreeStore,
                         PFSDirectory pDir,                       //!< in: actual directory (resolved if from symlink)
                         const Gtk::TreeModel::iterator it)       //!< in: tree iterator to insert children under
{
//     /* This nasty trickery is necessary to make std::make_shared work with a protected constructor. */
//     class Derived : public TreeJob
//     {
//     public:
//         Derived(Glib::RefPtr<Gtk::TreeStore> &pTreeStore, PFSDirectory &pDir) : TreeJob(pTreeStore, pDir) { }
//     };

    auto p = new TreeJob(pTreeStore, pDir);

    FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
    (*it)[cols._colState] = TreeNodeState::POPULATING;

    Debug::Log(FOLDER_POPULATE, "TreeJob::Create(\"" + p->_strPath + "\")");

    // Construct a path from the iterator, and then a row reference from the path because both the
    // path and the iterator can be invalidated if tree contents change while we're populating.
    Gtk::TreePath path(it);
    p->_pRowReference = std::make_shared<Gtk::TreeRowReference>(pTreeStore, path);

    p->spawnPopulate();

    return p;
}

/**
 *  Protected constructor, called only by the Create() factory.
 */
TreeJob::TreeJob(Glib::RefPtr<Gtk::TreeStore> pTreeStore,
                 PFSDirectory pDir)
  : _id(g_uJobID++),
    _pTreeStore(pTreeStore),
    _pDir(pDir)
{
    _strPath = _pDir->getRelativePath();

}

TreeJob::~TreeJob()
{
    Debug::Log(FOLDER_POPULATE, std::string(__func__) + "(\"" + _strPath + "\")");
}

/**
 *  Spawns the populate thread for this tree view. Called from the Create() factory
 *  after the instance has been created.
 */
void TreeJob::spawnPopulate()
{
    PFSDirectory    pDir = this->_pDir;

}

/**
 *  Takes the row reference created in Create() and returns a tree iterator for it.
 *  This is necessary because iterators are invalidated when items are added or removed.
 */
Gtk::TreeModel::iterator TreeJob::getIterator()
{
    Gtk::TreePath path = _pRowReference->get_path();
    return _pTreeStore->get_iter(path);
}

void TreeJob::onPopulateDone()
{
}

void TreeJob::spawnAddFirstFolders(PAddOneFirstsList pllToAddFirst)
{
    Debug::Log(FOLDER_POPULATE, "TreeJob::spawnAddFirstFolders(" + to_string(pllToAddFirst->size()) + " items)");

    _pAddFirstThread = WorkerThread::Create([this, pllToAddFirst]()
    {
        for (PAddOneFirst pAddOneFirst : *pllToAddFirst)
        {
            FSLock flock;
            PFSDirectory pDir = pAddOneFirst->_pDirOrSymlink->resolveDirectory(flock);
            if (pDir)
            {
                FSList llFiles;
                pDir->getContents(llFiles, FSDirectory::Get::FIRST_FOLDER_ONLY, flock);
                for (auto &pFS : llFiles)
                    if (!pFS->isHidden(flock))
                    {
                        pAddOneFirst->_pFirstSubfolder = pFS;
                        this->_dequeAddFirst.push_back(pAddOneFirst);

                        flock.release();
                        JobsLock jlock;
                        this->_dispatcherAddFirst.emit();
                        break;
                    }
            }
        }

        // Trigger the dispatcher, which will call "addAnotherFirst".
        // Say "Done" by pushing a nullptr.
        JobsLock jlock;
        this->_dequeAddFirst.push_back(nullptr);
        this->_dispatcherAddFirst.emit();
    });
}

