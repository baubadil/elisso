/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/foldertree.h"

#include "elisso/mainwindow.h"
#include "elisso/worker.h"
#include "elisso/fileops.h"


/***************************************************************************
 *
 *  ElissoFolderTreeMgr::Impl (private)
 *
 **************************************************************************/

struct ResultBase : public ProhibitCopy
{
    PFolderTreeModelRow        _pRow;

protected:
    std::string             *_pstrError = nullptr;

    ResultBase(const PFolderTreeModelRow &pRow)
      : _pRow(pRow)
    { }

    virtual ~ResultBase()
    {
        if (_pstrError)
            delete _pstrError;
    }

public:
    void setError(exception &e)
    {
        _pstrError = new std::string(e.what());
    }

    bool fHasError()
    {
        return !!_pstrError;
    }

    std::string getError()
    {
        return (_pstrError) ? *_pstrError : "";
    }

};

struct SubtreePopulated : ResultBase
{
    FSVector                  _vContents;

    SubtreePopulated(const PFolderTreeModelRow &pRow)
      : ResultBase(pRow)
    { }
};

typedef std::shared_ptr<SubtreePopulated> PSubtreePopulated;

struct AddOneFirst : ResultBase
{
    PFSModelBase            _pFirstSubfolder;

    AddOneFirst(const PFolderTreeModelRow &pRow)
      : ResultBase(pRow)
    { }
};

struct ElissoFolderTreeMgr::Impl : public ProhibitCopy
{
    std::vector<std::pair<PFSDirectory, PFolderTreeModelRow>>  vTreeRoots;

    Glib::RefPtr<FolderTreeModel>           pModel;

    WorkerResultQueue<PFsGioMountablesVector> workerAddMounts;
    WorkerResultQueue<PSubtreePopulated>    workerSubtreePopulated;
    WorkerResultQueue<PAddOneFirst>         workerAddOneFirst;

    std::atomic<uint>                       cThreadsRunning;

    // The following is true while we're in select(); we don't want to process
    // the "node selected" signal then and recurse infinitely.
    bool                                    fSuppressSelectHandler = false;

    // This gets set by selectNode so that a previously selected node gets
    // scrolled back into view every time a subtree has been expanded.
    PFolderTreeModelRow                     pScrollToAfterExpand;

    Impl()
        : cThreadsRunning(0)
    { }
};


/***************************************************************************
 *
 *  JobsLock
 *
 **************************************************************************/

Mutex g_mutexJobs;

class JobsLock : public Lock
{
public:
    JobsLock() : Lock(g_mutexJobs) { };
};


/***************************************************************************
 *
 *  ElissoFolderTree
 *
 **************************************************************************/

ElissoFolderTreeMgr::ElissoFolderTreeMgr(ElissoApplicationWindow &mainWindow)
    : Gtk::ScrolledWindow(),
      _mainWindow(mainWindow),
      _treeView(),
      _pImpl(new Impl)
{
    FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
    _pImpl->pModel = FolderTreeModel::create();

    _treeView.set_enable_tree_lines(true);

    _treeView.set_model(_pImpl->pModel);

    _treeView.set_headers_visible(false);
    _treeView.append_column("Name", cols._colIconAndName);

    _treeView.setParent(mainWindow, TreeViewPlusMode::IS_FOLDER_TREE_LEFT);

    // Connect the GUI thread dispatcher for when "add mounts" is done.
    _pImpl->workerAddMounts.connect([this]()
    {
        Debug d(MOUNTS, "workerAddMounts.dispatcher");
        this->onGetMountablesDone();
    });

    // Connect the GUI thread dispatcher for when a folder populate is done.
    _pImpl->workerSubtreePopulated.connect([this]()
    {
        Debug d(FOLDER_POPULATE_LOW, "workerSubtreePopulated.dispatcher");
        this->onPopulateDone();
    });

    // Connect the GUI thread dispatcher for when a folder populate is done.
    _pImpl->workerAddOneFirst.connect([this]()
    {
        Debug d(FOLDER_POPULATE_LOW, "workerAddOneFirst.dispatcher");
        this->onAddAnotherFirst();
    });

    auto pTreeSelection = _treeView.get_selection();
    pTreeSelection->signal_changed().connect([this](){
        this->onNodeSelected();
    });

    _treeView.signal_row_activated().connect([this](const Gtk::TreeModel::Path &path,
                                                    Gtk::TreeViewColumn*)
    {
        if (!_treeView.row_expanded(path))
            _treeView.expand_row(path, false);
        else
            _treeView.collapse_row(path);
    });

    _treeView.signal_row_expanded().connect([this](const Gtk::TreeModel::iterator &it,
                                                   const Gtk::TreeModel::Path &path)
    {
        this->onNodeExpanded(it, path);
    });

    this->add(_treeView);

    this->show_all_children();

    this->addTreeRoot("Home", FSModelBase::GetHome());
    this->addTreeRoot("File system", FSModelBase::FindDirectory("/"));
    this->spawnGetMountables();
}

/* virtual */
ElissoFolderTreeMgr::~ElissoFolderTreeMgr()
{
    delete _pImpl;
}

uint8_t g_cTreeRootItems = 0;

void
ElissoFolderTreeMgr::addTreeRoot(const Glib::ustring &strName,
                                 PFSDirectory pDir)
{
     // Add the first page in an idle loop so we have no delay in showing the window.
    Glib::signal_idle().connect([this, strName, pDir]() -> bool
    {
        Debug d(FOLDER_POPULATE_HIGH, "addTreeRoot lambda for " + quote(pDir->getPath()));

        auto pRowRoot = _pImpl->pModel->append(nullptr,
                                               g_cTreeRootItems++,
                                               pDir,
                                               strName);

        _pImpl->vTreeRoots.push_back({pDir, pRowRoot});

        // Do not populate just yet, we'll populate the root node in selectNode().
//         spawnPopulate(pRowRoot);

        return false;
    });
}

void
ElissoFolderTreeMgr::selectNode(PFSModelBase pDir)
{
    if (!pDir)
        return;

    Debug d(FOLDER_POPULATE_HIGH, string(__func__) + "(" + quote(pDir->getPath()) + ")");
    PFSModelBase pSelectRoot;
    PFolderTreeModelRow pRootRow;

    for (auto &pair : _pImpl->vTreeRoots)
    {
        auto &pRootThis = pair.first;
        if (    (pRootThis == pDir)
             || (pDir->isUnder(pRootThis))
           )
        {
            pSelectRoot = pRootThis;
            pRootRow = pair.second;
            break;
        }
    }

    if (pRootRow)
    {
        PFolderTreeModelRow pRowSelect;

        // Now pSelectRoot points to the FS object of the tree root (e.g. $(HOME), and itRoot has its tree model iterator.
        Gtk::TreePath path(_pImpl->pModel->getPath(pRootRow));
        _treeView.expand_row(path, false);

        // Now follow the path components of pDir until we reach pDir. For example, if pDir == $(HOME)/dir1/dir2/dir3
        // we will need to expand dir1 and dir2 and select the dir3 node. For each of the nodes, we need to insert
        // an item into the tree if it's not there yet; the "expanded" signal that gets fired will then populate the
        // tree nodes with the remaining items and "add first" subfolders as if they had been expanded manually.

        std::string strDir = pDir->getPath();                   // $(HOME)/dir1/dir2/dir3
        std::string strRoot = pSelectRoot->getPath();           // $(HOME)
        if (strDir.length() <= strRoot.length())
        {
            // Final node:
            pRowSelect = pRootRow;
            // Make sure the root node is populated too. This means lazy populating until
            // the root node is actually used.
            this->spawnPopulate(pRootRow);
        }
        else
        {
            std::string strRestOfDir = strDir.substr(strRoot.length()); //         dir1/dir2/dir3
            if (startsWith(strRestOfDir, "/"))
                strRestOfDir = strRestOfDir.substr(1);

            Debug::Log(FOLDER_POPULATE_HIGH, "exploding rest of root " + quote(strRestOfDir));

            auto svParticles = explodeVector(strRestOfDir, "/");

            // We have FSModel methods below, which can throw.
            try
            {
                size_t c = 1;
                // Wee keep strParticle and itParticle and pFSParticle in sync.
                PFolderTreeModelRow pParticleRow = pRootRow;
                auto pFSParticle = pSelectRoot;
                for (auto &strParticle : svParticles)
                {
                    Debug::Log(FOLDER_POPULATE_HIGH, "  looking for " + strParticle);

                    auto pChildRow = _pImpl->pModel->findRow(pParticleRow, strParticle);
                    if (pChildRow)
                    {
                        // Particle already in tree: if this is the final particle, select it.
                        if (c == svParticles.size())
                        {
                            Debug::Log(FOLDER_POPULATE_HIGH, "    found final node " + quote(strParticle) + ", selecting");
                            pRowSelect = pChildRow;
                        }
                        else
                        {
                            // Otherwise expand it. This will trigger a populate via the signal handler.
                            Debug::Log(FOLDER_POPULATE_HIGH, "    found intermediate node " + quote(strParticle) + ", expanding");
                            path = _pImpl->pModel->getPath(pChildRow);
                            _treeView.expand_row(path, false);
                        }
                        pParticleRow = pChildRow;
                        pFSParticle = pChildRow->pDir;
    //                     break;
                    }
                    else
                    {
                        // Insert a node for the child.
                        FSContainer *pDir2 = pFSParticle->getContainer();
                        if (    pDir2
                             && ((pFSParticle = pDir2->find(strParticle)))
                           )
                        {
                            auto pParent = pParticleRow;
                            Debug::Log(FOLDER_POPULATE_HIGH,
                                       "    node " + quote(strParticle) + " is not yet in tree, inserting under " + quote(pParent->name));
                            pParticleRow = _pImpl->pModel->append(pParent,
                                                                  0,        // overrideSort
                                                                  pFSParticle,
                                                                  pFSParticle->getBasename());
                            pRowSelect = pParticleRow;

                            path = _pImpl->pModel->getPath(pParent);
                            _treeView.expand_row(path, false);
                        }
                        else
                        {
                            Debug::Log(FOLDER_POPULATE_HIGH, "    node " + quote(strParticle) + " DOES NOT EXIST");
                            break;
                        }
                    }
                    ++c;
                } // end for (auto &strParticle : svParticles)
            }
            catch (...)
            {
            }
        }

        if (pRowSelect)
        {
            // Disable signal processing, or else we'll recurse infinitely and crash.
            _pImpl->fSuppressSelectHandler = true;
            path = _pImpl->pModel->getPath(pRowSelect);
            _treeView.get_selection()->select(path);
            _treeView.scroll_to_row(path);
            _pImpl->fSuppressSelectHandler = false;

            _pImpl->pScrollToAfterExpand = pRowSelect;
        }
    }
}

void
ElissoFolderTreeMgr::suppressSelectHandler(bool fSuppress)
{
    _pImpl->fSuppressSelectHandler = fSuppress;
}

void
ElissoFolderTreeMgr::handleAction(const string &strAction)
{
    auto pDir = this->getSelectedFolder();
    if (pDir)
    {
        if (strAction == ACTION_TREE_OPEN_SELECTED)
            onNodeSelected();       // duplicates work but less code
        else if (strAction == ACTION_TREE_OPEN_SELECTED_IN_TAB)
            _mainWindow.addFolderTab(pDir);
        else if (strAction == ACTION_TREE_OPEN_SELECTED_IN_TERMINAL)
            _mainWindow.openFolderInTerminal(pDir);
        else if (strAction == ACTION_TREE_TRASH_SELECTED)
            _mainWindow.addFileOperation(FileOperationType::TRASH,
                                         { pDir },
                                         nullptr);
    }
}

void
ElissoFolderTreeMgr::spawnGetMountables()
{
    /*
     * Launch the thread!
     */
    XWP::Thread::Create([this]()
    {
        ++_pImpl->cThreadsRunning;
        // Create an FSList on the thread's stack and have it filled by the back-end.
        auto pllMountables = make_shared<FsGioMountablesVector>();

        try
        {
            FsGioMountable::GetMountables(*pllMountables);
        }
        catch (exception &e)
        {
        }

        --_pImpl->cThreadsRunning;

        // Hand the results over to the instance: add it to the queue, signal the dispatcher.
        this->_pImpl->workerAddMounts.postResultToGui(pllMountables);
        // This triggers onGetMountablesDone().
    });

    this->updateCursor();

    Debug::Log(MOUNTS, "spawned");
}

void ElissoFolderTreeMgr::onGetMountablesDone()
{
    PFsGioMountablesVector pllMountables = this->_pImpl->workerAddMounts.fetchResult();

    if (pllMountables)
        for (auto pMountable : *pllMountables)
            Debug::Log(MOUNTS, "Got mountable " + pMountable->getBasename());
}

PFSModelBase
ElissoFolderTreeMgr::getSelectedFolder()
{
    PFSModelBase pDir;
    auto pTreeSelection = _treeView.get_selection();
    Gtk::TreeModel::iterator it;
    if ((it = pTreeSelection->get_selected()))
    {
        PFolderTreeModelRow pRow = _pImpl->pModel->findRow(it);
        if (pRow)
            pDir = pRow->pDir;
    }

    return pDir;
}

void
ElissoFolderTreeMgr::onNodeSelected()
{
    if (!_pImpl->fSuppressSelectHandler)
    {
        auto pDir = this->getSelectedFolder();
        if (pDir)
        {
            Debug::Log(FOLDER_POPULATE_LOW, "Selected: " + pDir->getPath());
            auto pActiveFolderView = _mainWindow.getActiveFolderView();
            if (pActiveFolderView)
                pActiveFolderView->setDirectory(pDir,
                                                SetDirectoryFlag::PUSH_TO_HISTORY | SetDirectoryFlag::CLICK_FROM_TREE);
                            // SetDirectoryFlag::CLICK_FROM_TREE prevents the callback to select the node in the
                            // tree again, which is already selected, since we're in the "selected" signal handler.
        }
    }
}

void
ElissoFolderTreeMgr::onNodeExpanded(const Gtk::TreeModel::iterator &it,
                                 const Gtk::TreeModel::Path &path)
{
    PFolderTreeModelRow pRow = _pImpl->pModel->findRow(it);
    PFSModelBase pDir;
    if (    (pRow)
         && ((pDir = pRow->pDir))
       )
    {
        Debug::Log(FOLDER_POPULATE_HIGH, "Expanded: " + pDir->getPath());

        switch (pRow->state)
        {
            case TreeNodeState::UNKNOWN:
            case TreeNodeState::POPULATED_WITH_FIRST:
                this->spawnPopulate(pRow);
            break;

            case TreeNodeState::POPULATING:
            case TreeNodeState::POPULATED_WITH_FOLDERS:
            case TreeNodeState::POPULATE_ERROR:
            break;
        }
    }
}

bool
ElissoFolderTreeMgr::spawnPopulate(PFolderTreeModelRow pRow)
{
    bool rc = false;

    if (pRow->state != TreeNodeState::POPULATED_WITH_FOLDERS)
    {
        FSContainer *pDir2 = pRow->pDir->getContainer();
        if (pDir2)
        {
            Debug::Log(FOLDER_POPULATE_HIGH, "POPULATING TREE \"" + ((pDir2) ? pRow->pDir->getPath() : "NULL") + "\"");

            pRow->state = TreeNodeState::POPULATING;

            /*
             * Launch the thread!
             */
            XWP::Thread::Create([this, pDir2, pRow]()
            {
                ++_pImpl->cThreadsRunning;
                // Create an FSList on the thread's stack and have it filled by the back-end.
                PSubtreePopulated pResult = std::make_shared<SubtreePopulated>(pRow);

                try
                {
                    pDir2->getContents(pResult->_vContents,
                                       FSDirectory::Get::FOLDERS_ONLY,
                                       nullptr,
                                       nullptr,
                                       nullptr);        // ptr to stop flag
                }
                catch (exception &e)
                {
                }

                --_pImpl->cThreadsRunning;

                // Hand the results over to the instance: add it to the queue, signal the dispatcher.
                this->_pImpl->workerSubtreePopulated.postResultToGui(pResult);
                // This triggers onPopulateDone().
            });

            this->updateCursor();

            Debug::Log(FOLDER_POPULATE_LOW, "spawned");

            rc = true;
        }
    }

    return rc;
}

void
ElissoFolderTreeMgr::onPopulateDone()
{
    // Fetch the SubtreePopulated result from the queue.
    PSubtreePopulated pSubtreePopulated = this->_pImpl->workerSubtreePopulated.fetchResult();

    Debug::Log(FOLDER_POPULATE_HIGH, "ElissoFolderTree::onPopulateDone(" + quote(pSubtreePopulated->_pRow->pDir->getPath()) + ")");

    PAddOneFirstsList pllToAddFirst = std::make_shared<AddOneFirstsList>();

    for (auto &pFS : pSubtreePopulated->_vContents)
        if (!pFS->isHidden())
        {
            auto pChildRow = _pImpl->pModel->append(pSubtreePopulated->_pRow,
                                                    0,       // overrideSort
                                                    pFS,
                                                    pFS->getBasename());

            pllToAddFirst->push_back(std::make_shared<AddOneFirst>(pChildRow));
        }

    // Now sort!
    Debug::Log(FOLDER_POPULATE_HIGH, "  sorting " + quote(pSubtreePopulated->_pRow->name));
    _pImpl->pModel->sort(pSubtreePopulated->_pRow);

    // Add a monitor for the parent folder.
    this->addMonitor(pSubtreePopulated->_pRow);

    if (pllToAddFirst->size())
        this->spawnAddFirstSubfolders(pllToAddFirst);

    if (_pImpl->pScrollToAfterExpand)
    {
        auto path = _pImpl->pModel->getPath(_pImpl->pScrollToAfterExpand);
        _treeView.scroll_to_row(path);
    }

    this->updateCursor();
}

/**
 *  Adds a monitor for the file-system object behind the given tree iterator.
 */
void
ElissoFolderTreeMgr::addMonitor(PFolderTreeModelRow pRow)
{
    if (pRow->pDir)
    {
        if (pRow->pMonitor)
            Debug::Log(FILEMONITORS, string(__func__) + ": " + pRow->pDir->getPath() + " already has a monitor");
        else
        {
            Debug::Log(FILEMONITORS, string(__func__) + ": adding for " + pRow->pDir->getPath());
            auto pContainer = pRow->pDir->getContainer();
            if (pContainer)
            {
                auto pMonitor = std::make_shared<FolderTreeMonitor>(*this,
                                                                    pRow);
                pMonitor->startWatching(*pContainer);

                pRow->pMonitor = pMonitor;
            }
        }
    }
}

void
ElissoFolderTreeMgr::spawnAddFirstSubfolders(PAddOneFirstsList pllToAddFirst)
{
    /*
     * Launch the thread!
     */
    XWP::Thread::Create([this, pllToAddFirst]()
    {
        ++_pImpl->cThreadsRunning;
        for (PAddOneFirst pAddOneFirst : *pllToAddFirst)
        {
            try
            {
                FSContainer *pCnr = pAddOneFirst->_pRow->pDir->getContainer();
                if (pCnr)
                {
                    FSVector vFiles;
                    pCnr->getContents(vFiles,
                                      FSDirectory::Get::FIRST_FOLDER_ONLY,
                                      nullptr,
                                      nullptr,
                                      nullptr);
                    for (auto &pFS : vFiles)
                        if (!pFS->isHidden())
                        {
                            pAddOneFirst->_pFirstSubfolder = pFS;
                            _pImpl->workerAddOneFirst.postResultToGui(pAddOneFirst);
                            break;
                        }
                }
            }
            catch (exception &e)
            {
                pAddOneFirst->setError(e);
                _pImpl->workerAddOneFirst.postResultToGui(pAddOneFirst);
            }
        }

        --_pImpl->cThreadsRunning;
        _pImpl->workerAddOneFirst.postResultToGui(nullptr);
    });

    this->updateCursor();
}

/**
 *  Called when this->_dispatcherAddFirst was signalled, which means the add-first
 *  thread has pushed a new item onto the queue.
 */
void
ElissoFolderTreeMgr::onAddAnotherFirst()
{
    auto pAddOneFirst = this->_pImpl->workerAddOneFirst.fetchResult();
    if (pAddOneFirst)
    {
        PFSModelBase pFSChild = pAddOneFirst->_pRow->pDir;
        Debug::Log(FOLDER_POPULATE_LOW, "TreeJob::onAddAnotherFirst(): popped \"" + pFSChild->getBasename() + "\"");

        PFSModelBase pFSGrandchild = pAddOneFirst->_pFirstSubfolder;
        if (pFSGrandchild)
        {
            _pImpl->pModel->append(pAddOneFirst->_pRow,
                                   0,       // overrideSort
                                   pFSGrandchild,
                                   pFSGrandchild->getBasename());

            // Add a monitor for the parent folder.
            this->addMonitor(pAddOneFirst->_pRow);
        }

        pAddOneFirst->_pRow->state = TreeNodeState::POPULATED_WITH_FIRST;
    }

    Debug::Log(FOLDER_POPULATE_LOW, "TreeJob::onAddAnotherFirst(): leaving");

    this->updateCursor();
}

void
ElissoFolderTreeMgr::updateCursor()
{
    _mainWindow.setWaitCursor(_treeView.get_window(),
                              (_pImpl->cThreadsRunning > 0) ? Cursor::WAIT_PROGRESS : Cursor::DEFAULT);
}


/***************************************************************************
 *
 *  FolderTreeMonitor
 *
 **************************************************************************/

/* virtual */
void
FolderTreeMonitor::onItemAdded(PFSModelBase &pFS) /* override */
{
}

/* virtual */
void
FolderTreeMonitor::onItemRemoved(PFSModelBase &pFS) /* override */
{
    auto pRow = _tree._pImpl->pModel->findRow(_pRowWatching, pFS->getBasename());
    if (pRow)
        _tree._pImpl->pModel->remove(_pRowWatching, pRow);
}

/* virtual */
void
FolderTreeMonitor::onItemRenamed(PFSModelBase &pFS,
                                 const std::string &strOldName,
                                 const std::string &strNewName) /* override */
{
    auto pRow = _tree._pImpl->pModel->findRow(_pRowWatching, strOldName);
    if (pRow)
        _tree._pImpl->pModel->rename(pRow, strNewName);
}
