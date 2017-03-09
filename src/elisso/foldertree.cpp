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
#include "elisso/treejob.h"


FolderTreeModelColumns* FolderTreeModelColumns::s_p = nullptr;


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
