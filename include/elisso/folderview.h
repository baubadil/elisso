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

#include "xwp/flagset.h"

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
    COMPACT,
    ERROR
};

enum class ViewState
{
    UNDEFINED,
    POPULATING,
    POPULATED,
    ERROR
};

class ElissoApplicationWindow;

enum class SetDirectoryFlags : uint8_t
{
    PUSH_TO_HISTORY         = (1 << 0),
    SCROLL_TO_PREVIOUS      = (1 << 1)
};

typedef FlagSet<SetDirectoryFlags> SetDirectoryFlagSet;

// DEFINE_BITSET(SetDirectoryFlags);

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

    /**
     *  Returns the FSDirectory that's currently showing. In the event that
     *  we're displaying a directory via a symlink, an FSSymlink is returned
     *  that points to the directory.
     */
    PFSModelBase getDirectory()
    {
        return _pDir;
    }

    bool setDirectory(PFSModelBase pDirOrSymlinkToDir,
                      SetDirectoryFlagSet fl);
    void refresh();

    bool canGoBack();
    bool goBack();
    bool canGoForward();
    bool goForward();

    void setState(ViewState s);
    void setViewMode(FolderViewMode m);
    void setError(Glib::ustring strError);

    void openFile(PFSModelBase pFS);
    void openTerminalOnSelectedFolder();

    struct PopulateData;

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
    Glib::ustring               _strError;          // only with ViewState::ERROR, use setError() to set
    FolderViewMode              _mode = FolderViewMode::UNDEFINED;
    FolderViewMode              _modeBeforeError = FolderViewMode::UNDEFINED;

    Gtk::IconView               _iconView;
    TreeViewWithPopup           _treeView;
//     Gtk::FlowBox                _compactView;
    Gtk::InfoBar                _infoBarError;
    Gtk::Label                  _infoBarLabel;
    Gtk::CellRendererPixbuf     _cellRendererIconSmall;

    PFSModelBase                _pDir;
    std::vector<std::string>    _aPathHistory;
    uint32_t                    _uPreviousOffset = 0;

    struct Impl;
    Impl                        *_pImpl;
};

#endif // ELISSO_FOLDERVIEW_H
