/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/folderview.h"
#include "elisso/fsmodel.h"

#include "xwp/debug.h"

#include <thread>
#include <iostream>


/***************************************************************************
 *
 *  FolderModelColumns (private)
 *
 **************************************************************************/

class FolderModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    FolderModelColumns()
    {
        add(m_col_filename);
        add(m_col_size);
        add(m_col_icon);
        add(m_col_typeResolved);
        add(m_col_typeString);
    }

    Gtk::TreeModelColumn<Glib::ustring>     m_col_filename;
    Gtk::TreeModelColumn<u_int64_t>         m_col_size;
    Gtk::TreeModelColumn<Glib::ustring>     m_col_icon;
    Gtk::TreeModelColumn<FSTypeResolved>    m_col_typeResolved;
    Gtk::TreeModelColumn<Glib::ustring>     m_col_typeString;

    static FolderModelColumns& Get()
    {
        if (!s_p)
            s_p = new FolderModelColumns;
        return *s_p;
    }

private:
    static FolderModelColumns *s_p;
};

FolderModelColumns* FolderModelColumns::s_p = nullptr;


/***************************************************************************
 *
 *  FolderViewImpl (private)
 *
 **************************************************************************/

struct ElissoFolderView::Impl
{
    PFSDirectory                    pDir;
    bool                            fPopulating;
    Glib::Dispatcher                dispatcherPopulateDone;
    Glib::RefPtr<Gtk::ListStore>    pListStore;
    FSList                          llFolderContents;

    Impl()
        : fPopulating(false)
    {
    }
};

/***************************************************************************
 *
 *  ElissoFolderView
 *
 **************************************************************************/

ElissoFolderView::ElissoFolderView()
    : Gtk::ScrolledWindow(),
      treeview(),
      _pImpl(new ElissoFolderView::Impl())
{
    _pImpl->dispatcherPopulateDone.connect([this]()
    {
        this->onPopulateDone();
    });

    this->add(treeview);

    FolderModelColumns &cols = FolderModelColumns::Get();
    _pImpl->pListStore = Gtk::ListStore::create(cols);
    treeview.set_model(_pImpl->pListStore);

    int i;
    Gtk::TreeView::Column* pColumn;

    i = treeview.append_column("Icon", cols.m_col_icon);

    i = treeview.append_column("Name", cols.m_col_filename);
    if ((pColumn = treeview.get_column(i - 1)))
        pColumn->set_sort_column(cols.m_col_filename);

    i = treeview.append_column("Type", cols.m_col_typeString);
    if ((pColumn = treeview.get_column(i - 1)))
        pColumn->set_sort_column(cols.m_col_typeString);

    i = treeview.append_column("Size", cols.m_col_size);
    if ((pColumn = treeview.get_column(i - 1)))
        pColumn->set_sort_column(cols.m_col_size);

    treeview.signal_row_activated().connect([this](const Gtk::TreeModel::Path &path,
                                                   Gtk::TreeViewColumn *pColumn)
    {
        Gtk::TreeModel::iterator iter = _pImpl->pListStore->get_iter(path);
        if (iter)
        {
            Gtk::TreeModel::Row row = *iter;
            FolderModelColumns &cols = FolderModelColumns::Get();
            if (row[cols.m_col_typeResolved] == FSTypeResolved::DIRECTORY)
            {
                std::string strBasename = Glib::filename_from_utf8(row[cols.m_col_filename]);
                std::string strPathNew = this->_strPath + "/" + strBasename;
                Debug::Log(DEBUG_ALWAYS, "Folder activated: " + strPathNew);
                this->setPath(strPathNew);
            }
        }
    });

    treeview.show();
}

/* virtual */
ElissoFolderView::~ElissoFolderView()
{
    delete _pImpl;
}

/**
 *  Returns true if a populate thread was started, or false if the folder had already been populated.
 */
bool ElissoFolderView::setPath(const std::string &strPath)
{
    _strPath = strPath;
    Debug::Log(DEBUG_ALWAYS, "setPath(\"" + strPath + ")\"");

    FSLock lock;
    if (_pImpl->pDir = FSModelBase::FindDirectory(strPath, lock))
        return this->spawnPopulate();

    return false;
}

/**
 *  Returns true if a populate thread was started, or false if the folder had already been populated.
 */
bool ElissoFolderView::spawnPopulate()
{
    bool rc = false;
    if (!_pImpl->fPopulating)
    {
        // Remove all old data, if any.
        _pImpl->pListStore->clear();

        _pImpl->fPopulating = true;

        new std::thread([this]()
                {
                    FSLock lock;
                    this->populate(lock);
                    // Trigger the dispatcher, which will call "populate done".
                    this->_pImpl->dispatcherPopulateDone.emit();
                });

        rc = true;
    }

    return rc;
}

/**
 *  Normally called on a worker thread.
 */
void ElissoFolderView::populate(FSLock &lock)
{
    this->_pImpl->pDir->getContents(_pImpl->llFolderContents,
                                    false);
}

void ElissoFolderView::onPopulateDone()
{
    const FolderModelColumns &cols = FolderModelColumns::Get();

    for (auto &pFS : _pImpl->llFolderContents)
    {
        auto row = *(_pImpl->pListStore->append());
        row[cols.m_col_icon] = pFS->getIcon();
        row[cols.m_col_filename] = pFS->getBasename();
        row[cols.m_col_size] = pFS->getFileSize();

        auto t = pFS->getResolvedType();
        row[cols.m_col_typeResolved] = t;

        const char *p = "Special";
        switch (t)
        {
            case FSTypeResolved::FILE: p = "File"; break;
            case FSTypeResolved::DIRECTORY: p = "Folder"; break;
            case FSTypeResolved::SYMLINK_TO_FILE: p = "Link to file"; break;
            case FSTypeResolved::SYMLINK_TO_DIRECTORY: p = "Link to folder"; break;
            case FSTypeResolved::BROKEN_SYMLINK: p = "Broken link"; break;
            default: break;
        }

        row[cols.m_col_typeString] = Glib::ustring(p);
    }

    _pImpl->fPopulating = false;
}
