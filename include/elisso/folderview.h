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

class ElissoFolderView;

/***************************************************************************
 *
 *  TreeViewWithPopup
 *
 **************************************************************************/

class TreeViewWithPopup : public Gtk::TreeView
{
    friend class ElissoFolderView;

protected:
    bool on_button_press_event(GdkEventButton* button_event) override;

private:
    void setParent(ElissoFolderView &view)
    {
        this->_pView = &view;
    }

    ElissoFolderView *_pView = NULL;
};


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
    POPULATED,
    POPULATE_ERROR
};

class ElissoApplicationWindow;

/**
 *  The folder view is the right half of a folder window and derives from
 *  ScrolledWindow. It contains either a  TreeView or IconView child, depending
 *  on which view is selected.
 */
class ElissoFolderView : public Gtk::ScrolledWindow
{
    friend class TreeViewWithPopup;

public:
    ElissoFolderView(ElissoApplicationWindow &mainWindow);
    virtual ~ElissoFolderView();

    size_t getID()
    {
        return _id;
    }

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

    void openFile(PFSModelBase pFS);
    void openTerminalOnSelectedFolder();

    bool spawnPopulate();

private:
    void dumpStack();
    void onPopulateDone();
    void connectModel(bool fConnect);
    void setListViewColumns();

    PFSModelBase getFSObject(Gtk::TreeModel::iterator &iter);
    struct Selection;
    size_t getSelection(Selection &sel);

    void onPathActivated(const Gtk::TreeModel::Path &path);
    void onSelectionChanged();
    void onMouseButton3Pressed(GdkEventButton* event);

    size_t                      _id;

    ElissoApplicationWindow     &_mainWindow;

    ViewState                   _state = ViewState::UNDEFINED;
    FolderViewMode              _mode = FolderViewMode::UNDEFINED;
    Gtk::IconView               _iconView;
    TreeViewWithPopup           _treeView;
//     Gtk::FlowBox                _compactView;

    PFSModelBase                _pDir;
    std::vector<std::string>    _aPathHistory;
    uint32_t                    _uPreviousOffset = 0;

    struct Impl;
    Impl                        *_pImpl;
};

#endif // ELISSO_FOLDERVIEW_H
