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
#include "elisso/folderview.h"
#include "elisso/fsmodel.h"
#include "elisso/mainwindow.h"

#include "xwp/except.h"

#include <thread>
#include <iostream>


/***************************************************************************
 *
 *  FolderContentsModelColumns (private)
 *
 **************************************************************************/

class FolderContentsModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    FolderContentsModelColumns()
    {
        add(_colFilename);
        add(_colSize);
        add(_colIconSmall);
        add(_colIconBig);
        add(_colTypeResolved);
        add(_colTypeString);
    }

    Gtk::TreeModelColumn<Glib::ustring>             _colFilename;
    Gtk::TreeModelColumn<u_int64_t>                 _colSize;
    Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf>> _colIconSmall;
    Gtk::TreeModelColumn<Glib::RefPtr<Gdk::Pixbuf>> _colIconBig;
    Gtk::TreeModelColumn<FSTypeResolved>            _colTypeResolved;
    Gtk::TreeModelColumn<Glib::ustring>             _colTypeString;

    static FolderContentsModelColumns& Get()
    {
        if (!s_p)
            s_p = new FolderContentsModelColumns;
        return *s_p;
    }

private:
    static FolderContentsModelColumns *s_p;
};

FolderContentsModelColumns* FolderContentsModelColumns::s_p = nullptr;


/***************************************************************************
 *
 *  ElissoFolderView::Impl (private)
 *
 **************************************************************************/

struct ElissoFolderView::Impl
{
    // GUI thread dispatcher for when a folder populate is done.
    Glib::Dispatcher                dispatcherPopulateDone;

    Glib::RefPtr<Gtk::ListStore>    pListStore;
    FSList                          llFolderContents;
    Glib::RefPtr<Gtk::IconTheme>    pIconTheme;

    Impl()
        : pIconTheme(Gtk::IconTheme::get_default())
    {
    }

    void onTreeModelPathActivated(ElissoFolderView &view,
                                  const Gtk::TreeModel::Path &path)
    {
        Gtk::TreeModel::iterator iter = pListStore->get_iter(path);
        if (iter)
        {
            Gtk::TreeModel::Row row = *iter;
            FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();
            std::string strBasename = Glib::filename_from_utf8(row[cols._colFilename]);
            FSLock lock;
            PFSDirectory pDir = view._pDir->resolveDirectory(lock);
            if (pDir)
            {
                FSLock lock;
                PFSModelBase pFS = pDir->find(strBasename, lock);
                if (pFS)
                    view.onFileActivated(pFS);
            }
        }
    }
};


/***************************************************************************
 *
 *  ElissoFolderView
 *
 **************************************************************************/

ElissoFolderView::ElissoFolderView(ElissoApplicationWindow &mainWindow)
    : Gtk::ScrolledWindow(),
      _mainWindow(mainWindow),
      _iconView(),
      _treeView(),
      _compactView(),
      _pImpl(new ElissoFolderView::Impl())
{
    // Allow multiple selections.
    auto pTreeSel = _treeView.get_selection();
    pTreeSel->set_mode(Gtk::SELECTION_MULTIPLE);
//     auto pIconSel = _iconView.get_selection();
//     pIconSel->set_mode(Gtk::SELECTION_MULTIPLE);

    // Connect the GUI thread dispatcher for when a folder populate is done.
    _pImpl->dispatcherPopulateDone.connect([this]()
    {
        this->onPopulateDone();
    });

    FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();
    _pImpl->pListStore = Gtk::ListStore::create(cols);

    /* Set up the icon view */
    _iconView.set_pixbuf_column(cols._colIconBig);
    _iconView.set_text_column(cols._colFilename);

    _iconView.signal_item_activated().connect([this](const Gtk::TreeModel::Path &path)
    {
        _pImpl->onTreeModelPathActivated(*this, path);
    });

    /* Set up the list view */
    int i;
    Gtk::TreeView::Column* pColumn;

    i = _treeView.append_column("Icon", cols._colIconSmall);

    i = _treeView.append_column("Name", cols._colFilename);
    if ((pColumn = _treeView.get_column(i - 1)))
        pColumn->set_sort_column(cols._colFilename);

    i = _treeView.append_column("Type", cols._colTypeString);
    if ((pColumn = _treeView.get_column(i - 1)))
        pColumn->set_sort_column(cols._colTypeString);

    i = _treeView.append_column("Size", cols._colSize);
    if ((pColumn = _treeView.get_column(i - 1)))
        pColumn->set_sort_column(cols._colSize);

    _treeView.signal_row_activated().connect([this](const Gtk::TreeModel::Path &path,
                                                    Gtk::TreeViewColumn *pColumn)
    {
        _pImpl->onTreeModelPathActivated(*this, path);
    });

    /* Set up the compact view */
//     _compactView.

    this->setViewMode(FolderViewMode::LIST);
}

/* virtual */
ElissoFolderView::~ElissoFolderView()
{
    delete _pImpl;
}

/**
 *  This gets called whenever the notebook tab on the right needs to be populated with the contents
 *  of another folder, in particular:
 *
 *   -- on startup, when the main window is constructed for the first time;
 *
 *   -- if the user double-clicks on another folder in the folder contents (via onFileActivated());
 *
 *   -- if the user single-clicks on a folder in the tree on the left.
 *
 *  Returns true if a populate thread was started, or false if the folder had already been populated.
 */
bool ElissoFolderView::setDirectory(PFSModelBase pDirOrSymlinkToDir,
                                    bool fPushToHistory /* = true */)
{
    bool rc = false;
    FSLock lock;
    auto t = pDirOrSymlinkToDir->getResolvedType(lock);

    switch (t)
    {
        case FSTypeResolved::DIRECTORY:
        case FSTypeResolved::SYMLINK_TO_DIRECTORY:
        {
            // If we have a directory already, push the path into the history.
            if (fPushToHistory)
                if (_pDir)
                {
                    auto strFull = _pDir->getRelativePath();
                    // Do not push if this is the same as the last item on the stack.
                    if (    (!_aPathHistory.size())
                         || (_aPathHistory.back() != strFull)
                       )
                        _aPathHistory.push_back(strFull);

                    _uPreviousOffset = 0;
                }

            _pDir = pDirOrSymlinkToDir;
            // Remove all old data, if any.
            _pImpl->pListStore->clear();
            _pImpl->llFolderContents.clear();
            rc = this->spawnPopulate();

            dumpStack();
        }
        break;

        default:
        break;
    }

    _mainWindow.enableActions();

    return rc;
}

void ElissoFolderView::dumpStack()
{
    uint i = 0;
    for (auto &s : _aPathHistory)
        Debug::Log(FOLDER_STACK, "stack item " + to_string(i++) + ": " + s);
    Debug::Log(FOLDER_STACK, "offset: " + to_string(_uPreviousOffset));
}

bool ElissoFolderView::canGoBack()
{
    return (_uPreviousOffset < _aPathHistory.size());
}

bool ElissoFolderView::goBack()
{
    if (this->canGoBack())
    {
        ++_uPreviousOffset;
        std::string strPrevious = _aPathHistory[_aPathHistory.size() - _uPreviousOffset];
        PFSModelBase pDirOld = _pDir;
        PFSModelBase pDir;
        FSLock lock;
        if ((pDir = FSModelBase::FindPath(strPrevious, lock)))
            if (this->setDirectory(pDir,
                                   false))      // do not push on history stack
            {
                std::vector<std::string>::iterator it = _aPathHistory.begin() + _uPreviousOffset;
                _aPathHistory.insert(it, pDirOld->getRelativePath());
                return true;
            }

    }

    return false;
}

bool ElissoFolderView::canGoForward()
{
    return (_uPreviousOffset > _aPathHistory.size());
}

bool ElissoFolderView::goForward()
{
    if (this->canGoForward())
    {
        std::string strPrevious = _aPathHistory[_aPathHistory.size() - --_uPreviousOffset];
        PFSDirectory pDir;
        FSLock lock;
        if ((pDir = FSModelBase::FindDirectory(strPrevious, lock)))
            if (this->setDirectory(pDir,
                                   false))      // do not push on history stack
            {
                return true;
            }
    }

    return false;
}

void ElissoFolderView::setState(ViewState s)
{
    if (s != _state)
    {
        switch (s)
        {
            case ViewState::POPULATING:
            {
                this->connectModel(false);
                _mainWindow.set_title("Loading " + _pDir->getBasename() + "...");
                Gtk::Spinner *pSpinner = new Gtk::Spinner();
                _mainWindow.getNotebook().set_tab_label(*this, *pSpinner);
                pSpinner->show();
                pSpinner->start();
            }
            break;

            case ViewState::POPULATED:
            {
                this->connectModel(true);
                Glib::ustring strTitle("Error");
                if (_pDir)
                {
                    std::string str = _pDir->getRelativePath();
                    strTitle = str;
                    _mainWindow.getNotebook().set_tab_label_text(*this,
                                                                 _pDir->getBasename());
                }
                _mainWindow.set_title(strTitle);

            }
            break;

            default:
            break;
        }

        _state = s;
    }
}

void ElissoFolderView::setViewMode(FolderViewMode m)
{
    if (m != _mode)
    {
        switch (_mode)
        {
            case FolderViewMode::ICONS:
                _iconView.hide();
                this->remove();
            break;

            case FolderViewMode::LIST:
                _treeView.hide();
                this->remove();
            break;

            case FolderViewMode::COMPACT:
                _compactView.hide();
                this->remove();
            break;

            default:
            break;
        }

        switch (m)
        {
            case FolderViewMode::ICONS:
                this->add(_iconView);
                _iconView.show();
            break;

            case FolderViewMode::LIST:
                this->add(_treeView);
                _treeView.show();
            break;

            case FolderViewMode::COMPACT:
                this->add(_compactView);
                _compactView.show();
            break;

            default:
            break;
        }

        _mode = m;

        this->connectModel(_state == ViewState::POPULATED);
    }
}

void ElissoFolderView::onFileActivated(PFSModelBase pFS)
{
    FSLock lock;
    FSTypeResolved t = pFS->getResolvedType(lock);

    switch (t)
    {
        case FSTypeResolved::DIRECTORY:
        case FSTypeResolved::SYMLINK_TO_DIRECTORY:
        {
            PFSDirectory pDir;
            FSLock lock;
            if ((pDir = pFS->resolveDirectory(lock)))
                this->setDirectory(pDir);
        }
        break;

        case FSTypeResolved::FILE:
        case FSTypeResolved::SYMLINK_TO_FILE:
        {
            std::string strPath = pFS->getRelativePath();
            char *pContentType = g_content_type_guess(strPath.c_str(),
                                                      nullptr,
                                                      0,
                                                      NULL);            // image/jpeg
            if (pContentType)
            {
                GAppInfo *pAppInfo_c = g_app_info_get_default_for_type(pContentType, FALSE);
                Glib::RefPtr<Gio::AppInfo> pAppInfo = Glib::wrap(pAppInfo_c);
                if (pAppInfo)
                    pAppInfo->launch(pFS->getGioFile());
                else
                    _mainWindow.errorBox("Cannot determine default application for file \"" + pFS->getRelativePath() + "\"");

//                 Gtk::MessageDialog dialog(_mainWindow,
//                                         "File clicked",
//                                         false /* use_markup */,
//                                         Gtk::MESSAGE_QUESTION,
//                                         Gtk::BUTTONS_OK_CANCEL);
//                 dialog.set_secondary_text("Launch \"" + pAppInfo->get_commandline() + "\"?");
//                 if (dialog.run() == Gtk::RESPONSE_OK)

                g_free(pContentType);
            }
        }
        break;

        default:
        break;
    }
}

void ElissoFolderView::connectModel(bool fConnect)
{
    switch (_mode)
    {
        case FolderViewMode::ICONS:
            if (fConnect)
                _iconView.set_model(_pImpl->pListStore);
            else
                _iconView.unset_model();
        break;

        case FolderViewMode::LIST:
            if (fConnect)
                _treeView.set_model(_pImpl->pListStore);
            else
                _treeView.unset_model();
        break;

        case FolderViewMode::COMPACT:
            if (fConnect)
            {
                FSLock lock;
                for (auto &pFS : _pImpl->llFolderContents)
                    if (!pFS->isHidden(lock))
                    {
                        Glib::ustring strLabel = pFS->getBasename();
                        auto p = Gtk::manage(new Gtk::Label(strLabel));
                        _compactView.insert(*p, -1);
                    }
                _compactView.show_all();
            }
            else
            {
                auto childList = _compactView.get_children();
                for (auto &p : childList)
                    _compactView.remove(*p);
            }

        break;

        default:
        break;
    }
}

/**
 *  Returns true if a populate thread was started, or false if the folder had already been populated.
 */
bool ElissoFolderView::spawnPopulate()
{
    bool rc = false;
    if (_state != ViewState::POPULATING)
    {
        this->setState(ViewState::POPULATING);

        Debug::Log(FOLDER_POPULATE, "ElissoFolderView::spawnPopulate(\"" + _pDir->getRelativePath() + "\")");

        new std::thread([this]()
        {
            FSLock lock;
            PFSDirectory pDir = this->_pDir->resolveDirectory(lock);
            if (pDir)
                pDir->getContents(_pImpl->llFolderContents,
                                  FSDirectory::Get::ALL,
                                  lock);
            lock.release();

            // Trigger the dispatcher, which will call "populate done".
            this->_pImpl->dispatcherPopulateDone.emit();
        });

        rc = true;
    }

    return rc;
}

void ForEachSubstring(const Glib::ustring &str,
                      const Glib::ustring &strDelimiter,
                      std::function<void (const Glib::ustring&)> fnParticle)
{
    size_t p1 = 0;
    size_t p2;
    while ((p2 = str.find(strDelimiter, p1)) != string::npos)
    {
        int len = p2 - p1;
        if (len > 0)
            fnParticle(str.substr(p1, len));
        p1 = p2 + 1;
    }

    fnParticle(str.substr(p1));
}

void ElissoFolderView::onPopulateDone()
{
    const FolderContentsModelColumns &cols = FolderContentsModelColumns::Get();

    Debug::Log(FOLDER_POPULATE, "ElissoFolderView::onPopulateDone(\"" + _pDir->getRelativePath() + "\")");

    FSLock lock;
    for (auto &pFS : _pImpl->llFolderContents)
        if (!pFS->isHidden(lock))
        {
            auto row = *(_pImpl->pListStore->append());

            Glib::ustring strIcons = pFS->getIcon();
            std::vector<Glib::ustring> sv;
            ForEachSubstring(   strIcons,
                                " ",
                                [&sv](const Glib::ustring &strParticle)
                                {
                                    if (!strParticle.empty())
                                        sv.push_back(strParticle);
                                });

            Glib::RefPtr<Gdk::Pixbuf> pb1 = _pImpl->pIconTheme->choose_icon(sv, 16).load_icon();
            row[cols._colIconSmall] = pb1;
            Glib::RefPtr<Gdk::Pixbuf> pb2 = _pImpl->pIconTheme->choose_icon(sv, 50).load_icon();
            row[cols._colIconBig] = pb2;

            row[cols._colFilename] = pFS->getBasename();
            row[cols._colSize] = pFS->getFileSize();

            FSLock lock;
            auto t = pFS->getResolvedType(lock);
            row[cols._colTypeResolved] = t;

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

            row[cols._colTypeString] = Glib::ustring(p);
        }

    this->setState(ViewState::POPULATED);
}

