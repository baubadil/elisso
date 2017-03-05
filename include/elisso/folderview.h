/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_FOLDERVIEW_H
#define ELISSO_FOLDERVIEW_H

#include <gtkmm.h>

#include "elisso/fsmodel.h"


/***************************************************************************
 *
 *  ElissoFolderView
 *
 **************************************************************************/

enum class FolderViewMode
{
    UNDEFINED,
    ICONS,
    LIST,
    COMPACT
};

enum class ViewState
{
    UNDEFINED,
    POPULATING,
    POPULATED
};

class ElissoApplicationWindow;

class ElissoFolderView;
typedef Glib::RefPtr<ElissoFolderView> PElissoFolderView;

/**
 *  The folder view is the right half of a folder window and derives from
 *  ScrolledWindow. It contains either a  TreeView or IconView child, depending
 *  on which view is selected.
 */
class ElissoFolderView : public Gtk::ScrolledWindow
{
public:
    ElissoFolderView(ElissoApplicationWindow &mainWindow);
    virtual ~ElissoFolderView();

    PFSModelBase getDirectory()
    {
        return _pDir;
    }

    bool setDirectory(PFSModelBase pDirOrSymlinkToDir,
                      bool fPushToHistory = true);

    bool canGoBack();
    bool goBack();
    bool canGoForward();
    bool goForward();

    void setState(ViewState s);
    void setViewMode(FolderViewMode m);

    void onFileActivated(PFSModelBase pFS);

    bool spawnPopulate();

private:
    void dumpStack();
    void onPopulateDone();
    void connectModel(bool fConnect);

    ElissoApplicationWindow     &_mainWindow;

    ViewState                   _state = ViewState::UNDEFINED;
    FolderViewMode              _mode = FolderViewMode::UNDEFINED;
    Gtk::IconView               _iconView;
    Gtk::TreeView               _treeView;
    Gtk::FlowBox                _compactView;

    PFSModelBase                _pDir;
    std::vector<std::string>    _aPathHistory;
    uint32_t                    _uPreviousOffset = 0;

    struct Impl;
    Impl            *_pImpl;
};

#endif // ELISSO_FOLDERVIEW_H
