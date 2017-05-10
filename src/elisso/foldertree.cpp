/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/foldertree.h"

#include <thread>

#include "elisso/mainwindow.h"
#include "elisso/worker.h"


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
    POPULATED_WITH_FOLDERS,
    POPULATE_ERROR
};

class FolderTreeModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    FolderTreeModelColumns()
    {
        add(_colMajorSort);
        add(_colIconAndName);
        add(_colPDir);
        add(_colState);
        add(_colPMonitor);
    }

    Gtk::TreeModelColumn<uint8_t>                   _colMajorSort;         // To keep "home" sorted before "file system" etc.
    Gtk::TreeModelColumn<Glib::ustring>             _colIconAndName;
    Gtk::TreeModelColumn<PFSModelBase>              _colPDir;
    Gtk::TreeModelColumn<TreeNodeState>             _colState;
    Gtk::TreeModelColumn<PFolderTreeMonitor>        _colPMonitor;

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
 *  ElissoFolderTree::Impl (private)
 *
 **************************************************************************/

struct ResultBase : public ProhibitCopy
{
    PFSModelBase            _pDirOrSymlink;
    PRowReference           _pRowRef;

protected:
    std::string             *_pstrError = nullptr;

    ResultBase(PFSModelBase pDirOrSymlink,
               const PRowReference &pRowRef)
      : _pDirOrSymlink(pDirOrSymlink),
        _pRowRef(pRowRef)
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
    FSList                  _llContents;
    FSList                  _llRemoved;

    SubtreePopulated(PFSModelBase pDirOrSymlink,
              const PRowReference &pRowRef)
      : ResultBase(pDirOrSymlink, pRowRef)
    { }
};

typedef std::shared_ptr<SubtreePopulated> PSubtreePopulated;

struct AddOneFirst : ResultBase
{
    PFSModelBase            _pFirstSubfolder;

    AddOneFirst(PFSModelBase pDirOrSymlink,
                const PRowReference &pRowRef)
      : ResultBase(pDirOrSymlink, pRowRef)
    { }
};

struct ElissoFolderTree::Impl : public ProhibitCopy
{
    std::vector<std::pair<PFSDirectory, Gtk::TreeModel::iterator>>  vTreeRoots;

    Glib::RefPtr<Gtk::TreeStore>            pTreeStore;

    WorkerResultQueue<PSubtreePopulated>    workerSubtreePopulated;
    WorkerResultQueue<PAddOneFirst>         workerAddOneFirst;

    std::atomic<uint>                       cThreadsRunning;

    // The following is true while we're in select(); we don't want to process
    // the "node selected" signal then and recurse infinitely.
    bool                                    fInExplicitSelect = false;

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

ElissoFolderTree::ElissoFolderTree(ElissoApplicationWindow &mainWindow)
    : Gtk::ScrolledWindow(),
      _mainWindow(mainWindow),
      _treeView(),
      _pImpl(new Impl)
{
    FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
    _pImpl->pTreeStore = Gtk::TreeStore::create(cols);

    _treeView.set_enable_tree_lines(true);

    _treeView.set_model(_pImpl->pTreeStore);

    _treeView.set_headers_visible(false);
    _treeView.append_column("Name", cols._colIconAndName);

    _pImpl->pTreeStore->set_sort_func(cols._colIconAndName, [&cols](const Gtk::TreeModel::iterator &a,
                                                                    const Gtk::TreeModel::iterator &b) -> int
    {
        auto rowA = *a;
        auto rowB = *b;
        if (rowA[cols._colMajorSort] < rowB[cols._colMajorSort])
            return -1;
        if (rowA[cols._colMajorSort] > rowB[cols._colMajorSort])
            return +1;
        const Glib::ustring &strA = rowA[cols._colIconAndName];
        const Glib::ustring &strB = rowB[cols._colIconAndName];
        return strA.compare(strB);
    });

    // Connect the GUI thread dispatcher for when a folder populate is done.
    _pImpl->workerSubtreePopulated.connect([this]()
    {
        Debug::Enter(FOLDER_POPULATE_LOW, "workerSubtreePopulated.dispatcher");
        this->onPopulateDone();
        Debug::Leave();
    });
    // Connect the GUI thread dispatcher for when a folder populate is done.
    _pImpl->workerAddOneFirst.connect([this]()
    {
        Debug::Enter(FOLDER_POPULATE_LOW, "workerAddOneFirst.dispatcher");
        this->onAddAnotherFirst();
        Debug::Leave();
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

    this->addTreeRoot("Home", FSDirectory::GetHome());
//     this->addTreeRoot("File system", FSDirectory::GetRoot());
//     this->addTreeRoot("File system", FSDirectory::GetRoot());
}

/* virtual */
ElissoFolderTree::~ElissoFolderTree()
{
    delete _pImpl;
}

uint8_t g_cTreeRootItems = 0;

/**
 *  Adds a new tree root (toplevel folder) to the tree. This gets called
 *  on setup to insert the "Home" and "File system" trees under which
 *  additional items get inserted over time depending on what is selected.
 */
void
ElissoFolderTree::addTreeRoot(const Glib::ustring &strName,
                              PFSDirectory pDir)
{
     // Add the first page in an idle loop so we have no delay in showing the window.
    Glib::signal_idle().connect([this, strName, pDir]() -> bool
    {
        const FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();

        Gtk::TreeModel::iterator itRoot;
        itRoot = _pImpl->pTreeStore->append();
        (*itRoot)[cols._colMajorSort] = g_cTreeRootItems++;
        (*itRoot)[cols._colIconAndName] = strName;
        (*itRoot)[cols._colPDir] = pDir;
        (*itRoot)[cols._colState] = TreeNodeState::UNKNOWN;

//         this->spawnPopulate(itRoot);

        Gtk::TreePath path = Gtk::TreePath(itRoot);
        _pImpl->vTreeRoots.push_back({pDir, itRoot});

        return false;
    });
}

/**
 *  Called from the main window after the notebook page on the right has finished populating
 *  to select the node in the tree that corresponds to the folder contents being displayed.
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
void
ElissoFolderTree::select(PFSModelBase pDir)
{
    if (!pDir)
        return;

    Debug::Enter(FOLDER_POPULATE_HIGH, string(__func__) + "(" + pDir->getRelativePath() + ")");
    PFSModelBase pSelectRoot;
    Gtk::TreeModel::iterator itRoot;

    for (auto &pair : _pImpl->vTreeRoots)
    {
        auto &pRootThis = pair.first;
        if (    (pRootThis == pDir)
             || (pDir->isUnder(pRootThis))
           )
        {
            pSelectRoot = pRootThis;
            itRoot = pair.second;
            break;
        }
    }

    if (itRoot)
    {
        Gtk::TreeModel::iterator itSelect;      // If set, we'll select this item below.

        // Now pSelectRoot points to the FS object of the tree root (e.g. $(HOME), and itRoot has its tree model iterator.
        Gtk::TreePath path(itRoot);
        _treeView.expand_row(path, false);

        // Now follow the path components of pDir until we reach pDir. For example, if pDir == $(HOME)/dir1/dir2/dir3
        // we will need to expand dir1 and dir2 and select the dir3 node. For each of the nodes, we need to insert
        // an item into the tree if it's not there yet; the "expanded" signal that gets fired will then populate the
        // tree nodes with the remaining items and "add first" subfolders as if they had been expanded manually.

        std::string strDir = pDir->getRelativePath();                   // $(HOME)/dir1/dir2/dir3
        std::string strRoot = pSelectRoot->getRelativePath();           // $(HOME)
        if (strDir.length() <= strRoot.length())
        {
            itSelect = itRoot;
            // Make sure it's populated.
            this->spawnPopulate(itSelect);
        }
        else
        {
            std::string strRestOfDir = strDir.substr(strRoot.length() + 1); //         dir1/dir2/dir3

            Debug::Log(FOLDER_POPULATE_HIGH, "ElissoFolderTree: selected " + pDir->getRelativePath() + ", rest of root: " + strRestOfDir);

            FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();

            auto svParticles = explodeVector(strRestOfDir, "/");

            size_t c = 1;
            // Wee keep strParticle and itParticle and pFSParticle in sync.
            Gtk::TreeModel::iterator itParticle = itRoot;
            auto pFSParticle = pSelectRoot;
            for (auto &strParticle : svParticles)
            {
                Debug::Log(FOLDER_POPULATE_HIGH, "  looking for " + strParticle);

                bool fFound = false;
                auto children = itParticle->children();
                for (auto itChild : children)
                {
                    auto row = *itChild;
                    if (row[cols._colIconAndName] == strParticle)
                    {
                        // Particle already in tree: if this is the final particle, select it.
                        if (c == svParticles.size())
                        {
                            Debug::Log(FOLDER_POPULATE_HIGH, "    selecting " + strParticle);
                            itSelect = itChild;
                        }
                        else
                        {
                            // Otherwise expand it. This will trigger a populate via the signal handler.
                            Debug::Log(FOLDER_POPULATE_HIGH, "    expanding " + strParticle);
                            path = itChild;
                            _treeView.expand_row(path, false);
                        }
                        fFound = true;
                        itParticle = itChild;
                        pFSParticle = row[cols._colPDir];
                        break;
                    }
                }
                // If we couldn't find a node and the folder has not been fully populated,
                // insert a node for the child.
                if (!fFound)
                {
//                     auto row = *itParticle;
//                     if (    (row[cols._colState] != TreeNodeState::POPULATED_WITH_FOLDERS)
//                          && (row[cols._colState] != TreeNodeState::POPULATING)
//                        )
                    {
                        FSContainer *pDir2 = pFSParticle->getContainer();
                        if (    pDir2
                             && ((pFSParticle = pDir2->find(strParticle)))
                           )
                        {
                            Debug::Log(FOLDER_POPULATE_HIGH, "    inserted and selecting " + strParticle);
                            itParticle = this->insertNode(strParticle,
                                                          pFSParticle,
                                                          children);
                            itSelect = itParticle;
                        }
                        else
                            break;
                    }
                }
                ++c;
            }
        }

        if (itSelect)
        {
            // Disable signal processing, or else we'll recurse infinitely and crash.
            _pImpl->fInExplicitSelect = true;
            path = itSelect;
            _treeView.get_selection()->select(path);
            _treeView.scroll_to_row(path);
            _pImpl->fInExplicitSelect = false;
        }
    }

    Debug::Leave();
}

/**
 *  Spawns a TreeJob to populate the tree node represented by the given iterator.
 */
bool
ElissoFolderTree::spawnPopulate(const Gtk::TreeModel::iterator &it)
{
    bool rc = false;

    const FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
    if ((*it)[cols._colState] != TreeNodeState::POPULATED_WITH_FOLDERS)
    {
        PFSModelBase pDir = (*it)[cols._colPDir];
        FSContainer *pDir2 = pDir->getContainer();
        if (pDir2)
        {
            Debug::Log(FOLDER_POPULATE_HIGH, "POPULATING TREE \"" + ((pDir2) ? pDir->getRelativePath() : "NULL") + "\"");

            (*it)[cols._colState] = TreeNodeState::POPULATING;

            auto pRowRefPopulating = this->getRowReference(it);

            /*
             * Launch the thread!
             */
            ++_pImpl->cThreadsRunning;
            auto pThread = new std::thread([this, pDir, pDir2, pRowRefPopulating]()
            {
                // Create an FSList on the thread's stack and have it filled by the back-end.
                PSubtreePopulated pResult = std::make_shared<SubtreePopulated>(pDir, pRowRefPopulating);

                try
                {
                    pDir2->getContents(pResult->_llContents,
                                       FSDirectory::Get::FOLDERS_ONLY,
                                       &pResult->_llRemoved,
                                       nullptr);        // ptr to stop flag
                }
                catch(exception &e)
                {
                }

                // Hand the results over to the instance: add it to the queue, signal the dispatcher.
                this->_pImpl->workerSubtreePopulated.postResultToGUI(pResult);
                // This triggers onPopulateDone().

                --_pImpl->cThreadsRunning;
            });
            pThread->detach();

            this->updateCursor();

            Debug::Log(FOLDER_POPULATE_LOW, "spawned");

            rc = true;
        }
    }

    return rc;
}

void
ElissoFolderTree::onPopulateDone()
{
    FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();

    // Fetch the SubtreePopulated result from the queue.
    PSubtreePopulated pSubtreePopulated = this->_pImpl->workerSubtreePopulated.fetchResult();

    Debug::Log(FOLDER_POPULATE_HIGH, "ElissoFolderTree::onPopulateDone(\"" + pSubtreePopulated->_pDirOrSymlink->getRelativePath() + "\")");

    // Turn off sorting before inserting a lot of items. This can really slow things down exponentially otherwise.
    _pImpl->pTreeStore->set_sort_column(Gtk::TreeSortable::DEFAULT_UNSORTED_COLUMN_ID,
                                        Gtk::SortType::SORT_ASCENDING);

    auto itPopulating = this->getIterator(pSubtreePopulated->_pRowRef);

    PAddOneFirstsList pllToAddFirst = std::make_shared<AddOneFirstsList>();

    // Build a map of tree iterators sorted by file name so we can look up existing nodes quickly.
    // We don't want to insert duplicates (some items might have been inserted already by a previous
    // "add first").
    std::map<Glib::ustring, Gtk::TreeModel::iterator> mapChildren;
    const Gtk::TreeNodeChildren children = itPopulating->children();
    for (Gtk::TreeModel::iterator itChild = children.begin();
         itChild != children.end();
         ++itChild)
    {
        auto row = *itChild;
        mapChildren[row[cols._colIconAndName]] = itChild;
    }

    for (auto &pFS : pSubtreePopulated->_llContents)
        if (!pFS->isHidden())
        {
            Gtk::TreeModel::iterator itChild = children.end();
            Glib::ustring strName = pFS->getBasename();
            auto itMap = mapChildren.find(strName);
            if (itMap != mapChildren.end())
                itChild = itMap->second;
            else
                itChild = this->insertNode(strName, pFS, children);

            auto pRowRef = this->getRowReference(itChild);
            pllToAddFirst->push_back(std::make_shared<AddOneFirst>(pFS, pRowRef));
        }

    // Now sort again.
    _pImpl->pTreeStore->set_sort_column(cols._colIconAndName, Gtk::SortType::SORT_ASCENDING);

    // Add a monitor for the parent folder.
    this->addMonitor(itPopulating);

    if (pllToAddFirst->size())
        this->spawnAddFirstSubfolders(pllToAddFirst);

    this->updateCursor();
}

Gtk::TreeModel::iterator
ElissoFolderTree::insertNode(const Glib::ustring &strName,
                             PFSModelBase pFS,
                             const Gtk::TreeNodeChildren &children)
{
    FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
    auto itChild = _pImpl->pTreeStore->append(children);
    (*itChild)[cols._colMajorSort] = 0;
    (*itChild)[cols._colIconAndName] = strName;
    (*itChild)[cols._colPDir] = pFS;
    (*itChild)[cols._colState] = TreeNodeState::UNKNOWN;

    return itChild;
}

/**
 *  Adds a monitor for the file-system object behind the given tree iterator.
 */
void
ElissoFolderTree::addMonitor(Gtk::TreeModel::iterator it)
{
    FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
    auto row = *it;
    PFSModelBase pFSMonitored = row[cols._colPDir];
    if (pFSMonitored)
    {
        PFolderTreeMonitor pMonitor = row[cols._colPMonitor];
        if (pMonitor)
            Debug::Log(FILEMONITORS, string(__func__) + ": " + pFSMonitored->getRelativePath() + " already has a monitor");
        else
        {
            Debug::Log(FILEMONITORS, string(__func__) + ": adding for " + pFSMonitored->getRelativePath());
            auto pContainer = pFSMonitored->getContainer();
            if (pContainer)
            {
                Gtk::TreePath path(it);
                auto pRowRef = std::make_shared<Gtk::TreeRowReference>(_pImpl->pTreeStore, path);
                pMonitor = std::make_shared<FolderTreeMonitor>(*this,
                                                               pRowRef,
                                                               pFSMonitored);
                pMonitor->startWatching(*pContainer);

                row[cols._colPMonitor] = pMonitor;
            }
        }
    }
}

void
ElissoFolderTree::spawnAddFirstSubfolders(PAddOneFirstsList pllToAddFirst)
{
    /*
     * Launch the thread!
     */
    ++_pImpl->cThreadsRunning;
    auto pThread = new std::thread([this, pllToAddFirst]()
    {
        for (PAddOneFirst pAddOneFirst : *pllToAddFirst)
        {
            try
            {
                FSContainer *pDir = pAddOneFirst->_pDirOrSymlink->getContainer();
                if (pDir)
                {
                    FSList llFiles;
                    pDir->getContents(llFiles,
                                      FSDirectory::Get::FIRST_FOLDER_ONLY,
                                      nullptr,
                                      nullptr);
                    for (auto &pFS : llFiles)
                        if (!pFS->isHidden())
                        {
                            pAddOneFirst->_pFirstSubfolder = pFS;
                            _pImpl->workerAddOneFirst.postResultToGUI(pAddOneFirst);
                            break;
                        }
                }
            }
            catch (exception &e)
            {
                pAddOneFirst->setError(e);
                _pImpl->workerAddOneFirst.postResultToGUI(pAddOneFirst);
            }
        }
        --_pImpl->cThreadsRunning;
    });
    pThread->detach();

    this->updateCursor();
}

/**
 *  Called when this->_dispatcherAddFirst was signalled, which means the add-first
 *  thread has pushed a new item onto the queue.
 */
void
ElissoFolderTree::onAddAnotherFirst()
{
    auto pAddOneFirst = this->_pImpl->workerAddOneFirst.fetchResult();
    if (pAddOneFirst)
    {
        FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
        PFSModelBase pFSChild = pAddOneFirst->_pDirOrSymlink;
        PFSModelBase pFSGrandchild = pAddOneFirst->_pFirstSubfolder;
        Debug::Log(FOLDER_POPULATE_LOW, "TreeJob::onAddAnotherFirst(): popped \"" + pFSChild->getBasename() + "\"");
        Gtk::TreePath pathChild = pAddOneFirst->_pRowRef->get_path();
        auto itChild = _pImpl->pTreeStore->get_iter(pathChild);
        if (pFSGrandchild)
        {
            this->insertNode(pFSGrandchild->getBasename(),
                             pFSGrandchild,
                             itChild->children());

            // Add a monitor for the parent folder.
            this->addMonitor(itChild);
        }

        (*itChild)[cols._colState] = TreeNodeState::POPULATED_WITH_FIRST;
    }

    Debug::Log(FOLDER_POPULATE_LOW, "TreeJob::onAddAnotherFirst(): leaving");

    this->updateCursor();
}

void ElissoFolderTree::onNodeSelected()
{
    if (!_pImpl->fInExplicitSelect)
    {
        auto pTreeSelection = _treeView.get_selection();
        Gtk::TreeModel::iterator it;
        if ((it = pTreeSelection->get_selected()))
        {
            const FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
            PFSModelBase pDir = (*it)[cols._colPDir];
            Debug::Log(FOLDER_POPULATE_LOW, "Selected: " + pDir->getRelativePath());
            if (pDir)
            {
                auto pActiveFolderView = _mainWindow.getActiveFolderView();
                if (pActiveFolderView)
                {
                    pActiveFolderView->setDirectory(pDir,
                                                    SetDirectoryFlag::PUSH_TO_HISTORY | SetDirectoryFlag::CLICK_FROM_TREE);
                }

                            // SetDirectoryFlag::CLICK_FROM_TREE prevents the callback to select the node in the
                            // tree again, which is already selected, since we're in the "selected" signal handler.
            }
        }
    }
}

void
ElissoFolderTree::onNodeExpanded(const Gtk::TreeModel::iterator &it,
                                 const Gtk::TreeModel::Path &path)
{
    const FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
    PFSModelBase pDir = (*it)[cols._colPDir];
    Debug::Log(FOLDER_POPULATE_HIGH, "Expanded: " + pDir->getRelativePath());

    switch ((*it)[cols._colState])
    {
        case TreeNodeState::UNKNOWN:
        case TreeNodeState::POPULATED_WITH_FIRST:
            this->spawnPopulate(it);
        break;

        default:
//         case TreeNodeState::POPULATING:
//         case TreeNodeState::POPULATED_WITH_FOLDERS:
//         case TreeNodeState::POPULATE_ERROR:
        break;
    }
}

void
ElissoFolderTree::updateCursor()
{
    _mainWindow.setWaitCursor(_treeView.get_window(),
                              (_pImpl->cThreadsRunning > 0) ? Cursor::WAIT_PROGRESS : Cursor::DEFAULT);
}

Gtk::TreeModel::iterator
ElissoFolderTree::getIterator(const PRowReference &pRowRef)
{
    Gtk::TreePath path = pRowRef->get_path();
    return _pImpl->pTreeStore->get_iter(path);
}

PRowReference
ElissoFolderTree::getRowReference(const Gtk::TreeModel::iterator &it)
{
    Gtk::TreePath path(it);
    return std::make_shared<Gtk::TreeRowReference>(_pImpl->pTreeStore, path);
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
    Gtk::TreeModel::iterator itChild = findIterator(pFS);
    if (itChild)
        _tree._pImpl->pTreeStore->erase(itChild);
}

/* virtual */
void
FolderTreeMonitor::onItemRenamed(PFSModelBase &pFS, const std::string &strOldName, const std::string &strNewName) /* override */
{
    Gtk::TreeModel::iterator itChild = findIterator(pFS);
    if (itChild)
    {
        auto row = *itChild;
        FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
        row[cols._colIconAndName] = strNewName;
    }
}

Gtk::TreeModel::iterator
FolderTreeMonitor::findIterator(PFSModelBase &pFS)
{
    // This notification is only interesting if the object is a directory
    // or a symlink to one. This tests that.
    if (pFS->getContainer())
    {
        FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
        Gtk::TreePath pathWatching = _pRowRefDirWatching->get_path();
        auto itWatching = _tree._pImpl->pTreeStore->get_iter(pathWatching);
        if (itWatching)
        {
            const Gtk::TreeNodeChildren children = itWatching->children();
            for (Gtk::TreeModel::iterator itChild = children.begin();
                 itChild != children.end();
                 ++itChild)
            {
                PFSModelBase pDir = (*itChild)[cols._colPDir];
                if (pDir == pFS)
                    return itChild;
            }
        }
    }
    return {};
}
