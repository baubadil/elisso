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

#include <thread>


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
 *  WorkerResult class template
 *
 **************************************************************************/

/**
 *  A worker result structure combines a Glib::Dispatcher with an STL double-ended queue
 *  to implement a "producer-consumer" model for a worker thread and the GTK main thread.
 *
 *  For this to work, an instance of this templated struct is best created in window
 *  instance data, and the constructor should call connect() with a function that
 *  handles the arrival of data by calling fetchResult().
 *
 *  The P template argument is assumed to be an object created by the worker thread.
 *  The worker thread calls addResult(), which signals the dispatcher.
 *
 *  On the GUI thread, the disp
 *
 *  The worker thread is not part of this structure.
 */
template<class P>
class WorkerResult
{
public:
    void connect(std::function<void ()> fn)
    {
        dispatcher.connect(fn);
    }

    void addResult(P pResult)
    {
        LockBase lock(mutex);
        deque.push_back(pResult);
        dispatcher.emit();
    }

    P fetchResult()
    {
        LockBase lock(mutex);
        P p;
        if (deque.size())
        {
            p = deque.at(0);
            deque.pop_front();
        }
        return p;
    }

protected:
    std::recursive_mutex    mutex;
    Glib::Dispatcher        dispatcher;
    std::deque<P>           deque;
};


/***************************************************************************
 *
 *  ElissoTreeView::Impl (private)
 *
 **************************************************************************/

struct ResultBase
{
    PFSModelBase            _pDirOrSymlink;
    PRowReference           _pRowRef;

    ResultBase(PFSModelBase pDirOrSymlink,
               const PRowReference &pRowRef)
      : _pDirOrSymlink(pDirOrSymlink),
        _pRowRef(pRowRef)
    { }
};

struct Populated : ResultBase
{
    FSList                  _llContents;

    Populated(PFSModelBase pDirOrSymlink,
              const PRowReference &pRowRef)
      : ResultBase(pDirOrSymlink, pRowRef)
    { }
};

typedef std::shared_ptr<Populated> PPopulated;

struct AddOneFirst : ResultBase
{
    PFSModelBase            _pFirstSubfolder;

    AddOneFirst(PFSModelBase pDirOrSymlink,
                const PRowReference &pRowRef)
      : ResultBase(pDirOrSymlink, pRowRef)
    { }
};

struct ElissoTreeView::Impl
{
    std::list<PFSDirectory>         llTreeRoots;

    Glib::RefPtr<Gtk::TreeStore>    pTreeStore;

    WorkerResult<PPopulated>        workerPopulated;
    WorkerResult<PAddOneFirst>      workerAddOneFirst;
};


/***************************************************************************
 *
 *  JobsLock
 *
 **************************************************************************/

std::recursive_mutex g_mutexJobs;

class JobsLock : public LockBase
{
public:
    JobsLock() : LockBase(g_mutexJobs) { };
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

    // Connect the GUI thread dispatcher for when a folder populate is done.
    _pImpl->workerPopulated.connect([this]()
    {
        Debug::Enter(FOLDER_POPULATE, "workerPopulated.dispatcher");
        this->onPopulateDone();
        Debug::Leave();
    });
    // Connect the GUI thread dispatcher for when a folder populate is done.
    _pImpl->workerAddOneFirst.connect([this]()
    {
        Debug::Enter(FOLDER_POPULATE, "workerAddOneFirst.dispatcher");
        this->onAddAnotherFirst();
        Debug::Leave();
    });

    this->add(_treeView);

    this->show_all_children();

    FSLock lock;
    this->addTreeRoot("Home", FSDirectory::GetHome(lock));
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

/**
 *  Spawns a TreeJob to populate the tree node represented by the given iterator.
 */
bool ElissoTreeView::spawnPopulate(const Gtk::TreeModel::iterator &it)
{
    bool rc = false;

    const FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
    if ((*it)[cols._colState] != TreeNodeState::POPULATED_WITH_FOLDERS)
    {
        PFSModelBase pDir = (*it)[cols._colPDir];
        FSLock lock;
        PFSDirectory pDir2 = pDir->resolveDirectory(lock);
        if (pDir2)
        {
            Debug::Log(FOLDER_POPULATE, "ElissoTreeView::spawnPopulate(\"" + ((pDir2) ? pDir2->getRelativePath() : "NULL") + "\")");

            (*it)[cols._colState] = TreeNodeState::POPULATING;

            auto pRowRefPopulating = this->getRowReference(it);

            /*
             * Launch the thread!
             */
            new std::thread([this, pDir, pDir2, pRowRefPopulating]()
            {
                // Create an FSList on the thread's stack and have it filled by the back-end.
                PPopulated pResult = std::make_shared<Populated>(pDir, pRowRefPopulating);

                {
                    FSLock lock;
                    pDir2->getContents(pResult->_llContents,
                                       FSDirectory::Get::FOLDERS_ONLY,
                                       lock);
                }

                // Hand the results over to the instance: add it to the queue, signal the dispatcher.
                this->_pImpl->workerPopulated.addResult(pResult);
                // This triggers onPopulateDone().
            });

            Debug::Log(FOLDER_POPULATE, "spawned");

            rc = true;
        }
    }

    return rc;
}

void ElissoTreeView::onPopulateDone()
{
    FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();

    // Fetch the Populated result from the queue.
    PPopulated pPopulated= this->_pImpl->workerPopulated.fetchResult();

    Debug::Log(FOLDER_POPULATE, "TreeJob::onPopulateDone(\"" + pPopulated->_pDirOrSymlink->getRelativePath() + "\")");

    auto itPopulating = this->getIterator(pPopulated->_pRowRef);

    // Build a map of tree iterators sorted by file name so we can look up existing nodes quickly.
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
    FSLock lock;
    for (auto &pFS : pPopulated->_llContents)
        if (!pFS->isHidden(lock))
        {
            Gtk::TreeModel::iterator itChild = children.end();
            Glib::ustring strName = pFS->getBasename();
            auto itMap = mapChildren.find(strName);
            if (itMap != mapChildren.end())
                itChild = itMap->second;
            else
            {
                itChild = _pImpl->pTreeStore->append(children);
                (*itChild)[cols._colIconAndName] = strName;
                (*itChild)[cols._colPDir] = pFS;
                (*itChild)[cols._colState] = TreeNodeState::UNKNOWN;
            }

            auto pRowRef = this->getRowReference(itChild);
            pllToAddFirst->push_back(std::make_shared<AddOneFirst>(pFS, pRowRef));
        }

    if (pllToAddFirst->size())
        this->spawnAddFirstSubfolders(pllToAddFirst);
}

void ElissoTreeView::spawnAddFirstSubfolders(PAddOneFirstsList pllToAddFirst)
{
    /*
     * Launch the thread!
     */
    new std::thread([this, pllToAddFirst]()
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
//                     if (!pFS->isHidden(flock))
                    {
                        pAddOneFirst->_pFirstSubfolder = pFS;
                        flock.release();

                        _pImpl->workerAddOneFirst.addResult(pAddOneFirst);
                        break;
                    }
            }
        }
    });
}

/**
 *  Called when this->_dispatcherAddFirst was signalled, which means the add-first
 *  thread has pushed a new item onto the queue.
 */
void ElissoTreeView::onAddAnotherFirst()
{
    auto pAddOneFirst = this->_pImpl->workerAddOneFirst.fetchResult();
    if (pAddOneFirst)
    {
        FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
        PFSModelBase pFSChild = pAddOneFirst->_pDirOrSymlink;
        PFSModelBase pFSGrandchild = pAddOneFirst->_pFirstSubfolder;
        Debug::Log(FOLDER_POPULATE, "TreeJob::onAddAnotherFirst(): popped \"" + pFSChild->getBasename() + "\"");
        Gtk::TreePath path = pAddOneFirst->_pRowRef->get_path();
        auto it = _pImpl->pTreeStore->get_iter(path);
        if (pFSGrandchild)
        {
            Gtk::TreeModel::iterator itGrandChild = _pImpl->pTreeStore->append(it->children());
            (*itGrandChild)[cols._colIconAndName] = pFSGrandchild->getBasename();
            (*itGrandChild)[cols._colPDir] = pFSGrandchild;
            (*itGrandChild)[cols._colState] = TreeNodeState::UNKNOWN;
        }
        (*it)[cols._colState] = TreeNodeState::POPULATED_WITH_FIRST;
    }

    Debug::Log(FOLDER_POPULATE, "TreeJob::onAddAnotherFirst(): leaving");
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

Gtk::TreeModel::iterator ElissoTreeView::getIterator(const PRowReference &pRowRef)
{
    Gtk::TreePath path = pRowRef->get_path();
    return _pImpl->pTreeStore->get_iter(path);
}

PRowReference ElissoTreeView::getRowReference(const Gtk::TreeModel::iterator &it)
{
    Gtk::TreePath path(it);
    return std::make_shared<Gtk::TreeRowReference>(_pImpl->pTreeStore, path);
}
