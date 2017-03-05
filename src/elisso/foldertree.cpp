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

class FolderTreeModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    FolderTreeModelColumns()
    {
        add(_colIconAndName);
        add(_colPDir);
    }

    Gtk::TreeModelColumn<Glib::ustring>             _colIconAndName;
    Gtk::TreeModelColumn<PFSModelBase>              _colPDir;

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

class TreeJob : public std::enable_shared_from_this<TreeJob>
{
public:
    static PTreeJob Create(Glib::RefPtr<Gtk::TreeStore> &pTreeStore,
                           PFSDirectory pDir,
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

        Debug::Log(FOLDER_POPULATE, "ElissoTreeView::spawnPopulate(\"" + pDir->getRelativePath() + "\")");

        new std::thread([p, pDir]()
        {
            // Create an FSList on the thread's stack and have it filled by the back-end.
            PFSList pllContents = std::make_shared<FSList>();
            pDir->getContents(*pllContents,
                              true);     // folders only

            // Hand the list over to the instance.
            JobsLock lock;
            p->_pllContents = pllContents;

            // Trigger the dispatcher, which will call "populate done".
            p->_dispatcherPopulateDone.emit();
        });

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
        // Connect the GUI thread dispatcher for when a folder populate is done.
        _dispatcherPopulateDone.connect([this]()
        {
            this->onPopulateDone();
        });
    }

    void onPopulateDone()
    {
        Debug::Log(FOLDER_POPULATE, "TreeJob::onPopulateDone(\"" + _pDir->getRelativePath() + "\")");

        FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();

        JobsLock lock;
        for (auto &p : *_pllContents)
            if (!p->isHidden())
            {
                Gtk::TreeModel::iterator itChild;
                itChild = _pTreeStore->append(_it->children());
                (*itChild)[cols._colIconAndName] = p->getBasename();
                (*itChild)[cols._colPDir] = p;
            }
    }

    TreeJobID                       _id;
    Glib::RefPtr<Gtk::TreeStore>    _pTreeStore;
    PFSDirectory                    _pDir;
    Gtk::TreeModel::iterator        _it;        // DO NOT TOUCH UNLESS FROM GUI THREAD
    PFSList                         _pllContents;

    // GUI thread dispatcher for when a folder populate is done.
    Glib::Dispatcher                _dispatcherPopulateDone;

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
    FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
    _pImpl->pTreeStore = Gtk::TreeStore::create(cols);

    _treeView.set_model(_pImpl->pTreeStore);

    _treeView.set_headers_visible(false);
    _treeView.append_column("Name", cols._colIconAndName);

    this->add(_treeView);

    this->show_all_children();

    this->addTreeRoot("Home", FSDirectory::GetHome());
    this->addTreeRoot("File system", FSDirectory::GetRoot());
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

    this->spawnPopulate(itRoot);

    Gtk::TreePath path = Gtk::TreePath(itRoot);

    _pImpl->llTreeRoots.push_back(pDir);
}

bool ElissoTreeView::spawnPopulate(Gtk::TreeModel::iterator &it)
{
    bool rc = false;

    const FolderTreeModelColumns &cols = FolderTreeModelColumns::Get();
    PFSModelBase pDir = (*it)[cols._colPDir];

    PFSDirectory pDir2;
    if ((pDir2 = pDir->resolveDirectory()))
        auto pJob = TreeJob::Create(_pImpl->pTreeStore, pDir2, it);

    return rc;
}

void ElissoTreeView::onPopulateDone()
{
//     Debug::Log(FOLDER_POPULATE, "ElissoTreeView::onPopulateDone(\"" + pDir->getRelativePath() + "\")");
}
