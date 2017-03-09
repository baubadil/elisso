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

// Instantiate the static member
JobsMap  TreeJob::s_mapJobs;


struct AddOneFirst
{
    PFSModelBase                _pDirOrSymlink;
    PFSModelBase                _pFirstSubfolder;
    Gtk::TreeModel::iterator    _gtkit;

    AddOneFirst(PFSModelBase pDirOrSymlink,
                Gtk::TreeModel::iterator gtkit)
        : _pDirOrSymlink(pDirOrSymlink),
            _gtkit(gtkit)
    { }
};


/***************************************************************************
 *
 *  JobsLock
 *
 **************************************************************************/

std::mutex g_mutexJobs;

class JobsLock
{
public:
    JobsLock()
        : g(g_mutexJobs)
    { }

private:
    std::lock_guard<std::mutex> g;
};


/***************************************************************************
 *
 *  TreeJob
 *
 **************************************************************************/

/* static */
PTreeJob TreeJob::Create(Glib::RefPtr<Gtk::TreeStore> &pTreeStore,
                         PFSDirectory &pDir,                       //!< in: actual directory (resolved if from symlink)
                         const Gtk::TreeModel::iterator it)       //!< in: tree iterator to insert children under
{
//     /* This nasty trickery is necessary to make std::make_shared work with a protected constructor. */
//     class Derived : public TreeJob
//     {
//     public:
//         Derived(Glib::RefPtr<Gtk::TreeStore> &pTreeStore, PFSDirectory &pDir) : TreeJob(pTreeStore, pDir) { }
//     };

    auto p = std::make_shared<TreeJob>(pTreeStore, pDir);
    JobsLock lock;
    s_mapJobs[p->_id] = p;

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
TreeJob::TreeJob(Glib::RefPtr<Gtk::TreeStore> &pTreeStore,
                 PFSDirectory &pDir)
  : _id(g_uJobID++),
    _pTreeStore(pTreeStore),
    _pDir(pDir)
{
    _strPath = _pDir->getRelativePath();

    // Connect the GUI thread dispatcher for when a folder populate is done.
    _dispatcherPopulateDone.connect([this]()
    {
        this->onPopulateDone();
    });
    // Connect the GUI thread dispatcher for when a folder populate is done.
    _dispatcherAddFirst.connect([this]()
    {
        Debug::Enter(FOLDER_POPULATE, "_dispatcherAddFirst");
        this->onAddAnotherFirst();
        Debug::Leave();
    });
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

    new std::thread([this, pDir]()
    {
        // Create an FSList on the thread's stack and have it filled by the back-end.
        PFSList pllContents = std::make_shared<FSList>();
        pDir->getContents(*pllContents,
                          FSDirectory::Get::FOLDERS_ONLY);

        // Hand the list over to the instance.
        JobsLock lock;
        this->_pllContents = pllContents;

        // Trigger the dispatcher, which will call "populate done".
        this->_dispatcherPopulateDone.emit();
    });
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
    Debug::Log(FOLDER_POPULATE, "TreeJob::onPopulateDone(\"" + _pDir->getRelativePath() + "\")");

    FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();

    // Now take the row reference we constructed in Create() and get a path
    // and iterator back from it.
    const Gtk::TreeModel::iterator itPopulating = getIterator();

    std::map<Glib::ustring, Gtk::TreeModel::iterator> mapChildren;
    const Gtk::TreeNodeChildren children = itPopulating->children();
    for (Gtk::TreeModel::iterator itChild = children.begin();
         itChild != children.end();
        ++itChild)
    {
        auto row = *itChild;
        mapChildren[row[cols._colIconAndName]] = itChild;
    }

    PAddOneFirstsList pllToAddFirst = std::make_shared<AddOneFirstsList>();
    if (_pllContents)
        for (auto &p : *_pllContents)
            if (!p->isHidden())
            {
                Gtk::TreeModel::iterator itChild = children.end();
                Glib::ustring strName = p->getBasename();
                auto itMap = mapChildren.find(strName);
                if (itMap != mapChildren.end())
                    itChild = itMap->second;
                else
                {
                    itChild = _pTreeStore->append(children);
                    (*itChild)[cols._colIconAndName] = strName;
                    (*itChild)[cols._colPDir] = p;
                    (*itChild)[cols._colState] = TreeNodeState::UNKNOWN;
                }
                pllToAddFirst->push_back(std::make_shared<AddOneFirst>(p, itChild));
            }

    if (pllToAddFirst->size())
        spawnAddFirstFolders(pllToAddFirst);
    else
        cleanUp();
}

void TreeJob::spawnAddFirstFolders(PAddOneFirstsList pllToAddFirst)
{
    Debug::Log(FOLDER_POPULATE, "TreeJob::spawnAddFirstFolders(" + to_string(pllToAddFirst->size()) + " items)");

    _pAddFirstThread = WorkerThread::Create([this, pllToAddFirst]()
    {
        for (PAddOneFirst pAddOneFirst : *pllToAddFirst)
        {
            PFSDirectory pDir = pAddOneFirst->_pDirOrSymlink->resolveDirectory();
            if (pDir)
            {
                FSList llFiles;
                pDir->getContents(llFiles, FSDirectory::Get::FIRST_FOLDER_ONLY);
                for (auto &pFS : llFiles)
                    if (!pFS->isHidden())
                    {
                        JobsLock lock;
//                             Debug::Log(FOLDER_POPULATE, " thread(\"" + pDir->getBasename() + "\": " + to_string(llFiles.size()) + " items)");
                        pAddOneFirst->_pFirstSubfolder = pFS;
                        this->_dequeAddFirst.push_back(pAddOneFirst);
                        this->_dispatcherAddFirst.emit();
                        break;
                    }
            }
        }

        // Trigger the dispatcher, which will call "addAnotherFirst".
        // Say "Done" by pushing a nullptr.
        JobsLock lock;
        this->_dequeAddFirst.push_back(nullptr);
        this->_dispatcherAddFirst.emit();
    });
}

/**
 *  Called when this->_dispatcherAddFirst was signalled, which means the add-first
 *  thread has pushed a new item onto the queue.
 */
void TreeJob::onAddAnotherFirst()
{
    bool fCleanup = false;
    JobsLock lock;
    if (this->_dequeAddFirst.size())
    {
        FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
        PAddOneFirst p = this->_dequeAddFirst.at(0);
        if (p)
        {
            PFSModelBase pFSChild = p->_pDirOrSymlink;
            PFSModelBase pFSGrandchild = p->_pFirstSubfolder;
            Debug::Log(FOLDER_POPULATE, "TreeJob::onAddAnotherFirst(): popped \"" + pFSChild->getBasename() + "\"");
            if (pFSGrandchild)
            {
                Gtk::TreeModel::iterator itGrandChild = _pTreeStore->append(p->_gtkit->children());
                (*itGrandChild)[cols._colIconAndName] = pFSGrandchild->getBasename();
                (*itGrandChild)[cols._colPDir] = pFSGrandchild;
                (*itGrandChild)[cols._colState] = TreeNodeState::UNKNOWN;
            }
            (*p->_gtkit)[cols._colState] = TreeNodeState::POPULATED_WITH_FIRST;
        }
        else
            fCleanup = true;

        this->_dequeAddFirst.pop_front();
    }

    Debug::Log(FOLDER_POPULATE, "TreeJob::onAddAnotherFirst(): leaving");

    if (fCleanup)
    {
        if (_pAddFirstThread)
            _pAddFirstThread->join();
        this->cleanUp();
    }
}

/**
 *  Removes this from the list of running jobs, which causes the refcount to drop to 0,
 *  and this will be deleted.
 */
void TreeJob::cleanUp()
{
    string str(_strPath);
    Debug::Log(FOLDER_POPULATE, "TreeJob::cleanUp(\"" + str + "\"): erasing from map");
    if (_fCleanedUp)
        throw FSException("cleanUp called twice");
    _fCleanedUp = true;
    // First mark the tree model node as "populated with folders".
    FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
    auto itNode = this->getIterator();
    (*itNode)[cols._colState] = TreeNodeState::POPULATED_WITH_FOLDERS;

    auto it = s_mapJobs.find(_id);
    if (it != s_mapJobs.end())
        s_mapJobs.erase(it);
    Debug::Log(FOLDER_POPULATE, "TreeJob::cleanUp(\"" + str + "\"): done erasing from map");
}
