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
#include "elisso/foldertree.h"
#include "elisso/mainwindow.h"

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

FolderTreeModelColumns* FolderTreeModelColumns::s_p = nullptr;


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

typedef size_t TreeJobID;

class TreeJob;
typedef std::shared_ptr<TreeJob>        PTreeJob;
typedef std::map<TreeJobID, PTreeJob>   JobsMap;

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
                           PFSDirectory pDir,                       //!< in: actual directory (resolved if from symlink)
                           Gtk::TreeModel::iterator it)
    {
        /* This nasty trickery is necessary to make std::make_shared work with a protected constructor. */
        class Derived : public TreeJob
        {
        public:
            Derived(Glib::RefPtr<Gtk::TreeStore> &pTreeStore, PFSDirectory &pDir, Gtk::TreeModel::iterator &it) : TreeJob(pTreeStore, pDir, it) { }
        };

        auto p = std::make_shared<Derived>(pTreeStore, pDir, it);
        JobsLock lock;
        s_mapJobs[p->_id] = p;

        Debug::Log(FOLDER_POPULATE, "TreeJob::Create(\"" + pDir->getRelativePath() + "\")");

        p->spawnPopulate(pDir);

        return p;
    }

protected:
    TreeJob(Glib::RefPtr<Gtk::TreeStore> &pTreeStore,
            PFSDirectory &pDir,
            Gtk::TreeModel::iterator &it)
        : _id(g_uJobID++),
          _pTreeStore(pTreeStore),
          _pDir(pDir),
          _it(it)
    {
        FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
        (*_it)[cols._colState] = TreeNodeState::POPULATING;

        // Connect the GUI thread dispatcher for when a folder populate is done.
        _dispatcherPopulateDone.connect([this]()
        {
            this->onPopulateDone();
        });
        // Connect the GUI thread dispatcher for when a folder populate is done.
        _dispatcherAddFirst.connect([this]()
        {
            this->onAddAnotherFirst();
        });
    }

    ~TreeJob()
    {
        Debug::Log(FOLDER_POPULATE, __func__);
    }

    void spawnPopulate(PFSDirectory pDir)
    {
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
    typedef std::shared_ptr<AddOneFirst> PAddOneFirst;
    typedef std::list<PAddOneFirst> AddOneFirstsList;
    typedef std::shared_ptr<AddOneFirstsList> PAddOneFirstsList;

    /* This queue is used to communicate between the "add first" thread and the GUI thread.
     * For every item that's pushed to the back of the queue, _dispatcherAddFirst must be fired once.
     * To signal that the thread is done, it pushes a nullptr and fires _dispatcherAddFirst one last time.
     */
    std::deque<PAddOneFirst>    _dequeAddFirst;

    void onPopulateDone()
    {
        Debug::Log(FOLDER_POPULATE, "TreeJob::onPopulateDone(\"" + _pDir->getRelativePath() + "\")");

        FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();

        std::map<Glib::ustring, Gtk::TreeModel::iterator> mapChildren;
        const Gtk::TreeNodeChildren children = _it->children();
        for (Gtk::TreeModel::iterator it = children.begin();
             it != children.end();
             ++it)
        {
            auto row = *it;
            mapChildren[row[cols._colIconAndName]] = it;
        }

        PAddOneFirstsList pllToAddFirst = std::make_shared<AddOneFirstsList>();
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

    void spawnAddFirstFolders(PAddOneFirstsList pllToAddFirst)
    {
        Debug::Log(FOLDER_POPULATE, "TreeJob::spawnAddFirstFolders(" + to_string(pllToAddFirst->size()) + " items)");

        new std::thread([this, pllToAddFirst]()
        {
            for (PAddOneFirst pAddOneFirst : *pllToAddFirst)
            {
                PFSDirectory pDir = pAddOneFirst->_pDirOrSymlink->resolveDirectory();
                if (pDir)
                {
                    FSList llFiles;
                    pDir->getContents(llFiles, FSDirectory::Get::FIRST_FOLDER_ONLY);
                    for (auto &pFS : llFiles)
//                         if (!pFS->isHidden())
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
    void onAddAnotherFirst()
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
            this->cleanUp();
    }

    /**
     *  Removes this from the list of running jobs, which causes the refcount to drop to 0,
     *  and this will be deleted.
     */
    void cleanUp()
    {
        // First mark the tree model node as "populated with folders".
        FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
        (*_it)[cols._colState] = TreeNodeState::POPULATED_WITH_FOLDERS;
        Debug::Log(FOLDER_POPULATE, "TreeJob::cleanUp(\"" + _pDir->getRelativePath() + "\")");
        auto it = s_mapJobs.find(_id);
        if (it != s_mapJobs.end())
            s_mapJobs.erase(it);
    }

    TreeJobID                       _id;
    Glib::RefPtr<Gtk::TreeStore>    _pTreeStore;
    PFSDirectory                    _pDir;
    Gtk::TreeModel::iterator        _it;        // DO NOT TOUCH UNLESS FROM GUI THREAD
    PFSList                         _pllContents;

    // GUI thread dispatcher for when a folder populate is done.
    Glib::Dispatcher                _dispatcherPopulateDone;
    // GUI thread dispatcher for when the "add first" thread has finished some work.
    Glib::Dispatcher                _dispatcherAddFirst;

    static JobsMap      s_mapJobs;
};

// Instantiate the static member
JobsMap  TreeJob::s_mapJobs;


/***************************************************************************
 *
 *  ElissoTreeView::Impl (private)
 *
 **************************************************************************/

struct ElissoTreeView::Impl
{
    std::list<PFSDirectory>         llTreeRoots;

    Glib::RefPtr<Gtk::TreeStore>    pTreeStore;
};


/***************************************************************************
 *
 *  ElissoTreeView
 *
 **************************************************************************/

ElissoTreeView::ElissoTreeView(ElissoApplicationWindow &mainWindow)
    : Gtk::ScrolledWindow(),
      _mainWindow(mainWindow),
      _treeView(),
      _pImpl(new Impl)
{
    auto pTreeSelection = _treeView.get_selection();
    pTreeSelection->signal_changed().connect([this](){
        this->onNodeSelected();
    });

    _treeView.signal_row_activated().connect([](const Gtk::TreeModel::Path&,
                                                Gtk::TreeViewColumn*)
    {
        Debug::Log(FOLDER_POPULATE, "tree item activated");
    });

    _treeView.signal_row_expanded().connect([this](const Gtk::TreeModel::iterator &it,
                                                   const Gtk::TreeModel::Path &path)
    {
        this->onNodeExpanded(it, path);
    });

    FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
    _pImpl->pTreeStore = Gtk::TreeStore::create(cols);

    _treeView.set_enable_tree_lines(true);

    _treeView.set_model(_pImpl->pTreeStore);

    _treeView.set_headers_visible(false);
    _treeView.append_column("Name", cols._colIconAndName);

    this->add(_treeView);

    this->show_all_children();

    this->addTreeRoot("Home", FSDirectory::GetHome());
//     this->addTreeRoot("File system", FSDirectory::GetRoot());
}

/* virtual */
ElissoTreeView::~ElissoTreeView()
{
    delete _pImpl;
}

void ElissoTreeView::addTreeRoot(const Glib::ustring &strName,
                                 PFSDirectory pDir)
{
    const FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();

    Gtk::TreeModel::iterator itRoot;
    itRoot = _pImpl->pTreeStore->append();
    (*itRoot)[cols._colIconAndName] = strName;
    (*itRoot)[cols._colPDir] = pDir;
    (*itRoot)[cols._colState] = TreeNodeState::UNKNOWN;

    this->spawnPopulate(itRoot);

    Gtk::TreePath path = Gtk::TreePath(itRoot);

    _pImpl->llTreeRoots.push_back(pDir);
}

bool ElissoTreeView::spawnPopulate(const Gtk::TreeModel::iterator &it)
{
    bool rc = false;

    const FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
    if ((*it)[cols._colState] != TreeNodeState::POPULATED_WITH_FOLDERS)
    {
        PFSModelBase pDir = (*it)[cols._colPDir];
        PFSDirectory pDir2 = pDir->resolveDirectory();
        Debug::Log(FOLDER_POPULATE, "ElissoTreeView::spawnPopulate(\"" + ((pDir2) ? pDir2->getRelativePath() : "NULL") + "\")");
        if (pDir2)
        {
            auto pJob = TreeJob::Create(_pImpl->pTreeStore, pDir2, it);
            rc = true;
        }
    }

    return rc;
}

void ElissoTreeView::onPopulateDone()
{
//     Debug::Log(FOLDER_POPULATE, "ElissoTreeView::onPopulateDone(\"" + pDir->getRelativePath() + "\")");
}

void ElissoTreeView::onNodeSelected()
{
    auto pTreeSelection = _treeView.get_selection();
    Gtk::TreeModel::iterator it;
    if ((it = pTreeSelection->get_selected()))
    {
        const FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
        PFSModelBase pDir = (*it)[cols._colPDir];
        Debug::Log(FOLDER_POPULATE, "Selected: " + pDir->getRelativePath());
        if (pDir)
        {
            auto pActiveFolderView = _mainWindow.getActiveFolderView();
            if (pActiveFolderView)
                pActiveFolderView->setDirectory(pDir);
        }
    }
}

void ElissoTreeView::onNodeExpanded(const Gtk::TreeModel::iterator &it,
                                    const Gtk::TreeModel::Path &path)
{
    const FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
    PFSModelBase pDir = (*it)[cols._colPDir];
    Debug::Log(FOLDER_POPULATE, "Expanded: " + pDir->getRelativePath());

    switch ((*it)[cols._colState])
    {
        case TreeNodeState::UNKNOWN:
        case TreeNodeState::POPULATED_WITH_FIRST:
            this->spawnPopulate(it);
        break;

        case TreeNodeState::POPULATING:
        case TreeNodeState::POPULATED_WITH_FOLDERS:
        break;
    }
}
