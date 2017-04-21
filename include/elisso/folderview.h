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

#include "elisso/fsmodel.h"
#include "elisso/treeviewplus.h"

#include "xwp/flagset.h"

class ElissoFolderView;


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

/**
 *  Bit flags to be used with setDirectory().
 */
enum class SetDirectoryFlags : uint8_t
{
    PUSH_TO_HISTORY         = (1 << 0),         // If set, the directory is added to the back/forward history stack.
    SCROLL_TO_PREVIOUS      = (1 << 1),         // If set, the previous directory is selected in the new contents list (useful for go back/parent).
    CLICK_FROM_TREE         = (1 << 2)          // If set, the contents are shown as a result of a click on the tree view, and we can spare the effort of finding the node in the tree.
};

typedef FlagSet<SetDirectoryFlags> SetDirectoryFlagSet;

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

    /*
     *  Public view methods
     */
    size_t getID()
    {
        return _id;
    }

    /**
     *  Returns the parent application window, properly cast.
     */
    ElissoApplicationWindow& getApplicationWindow()
    {
        return _mainWindow;
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

    /*
     *  Public selection methods
     */
    void selectAll();

    PFSModelBase getSelectedFolder();

    void onMouseButton3Pressed(GdkEventButton *pEvent, MouseButton3ClickType clickType);

    /*
     *  Public file action methods
     */
    void handleAction(const std::string &strAction);

    void openFile(PFSModelBase pFS);

    PFSDirectory createSubfolderDialog();
    void trashSelected();
    void testFileopsSelected();

    class PopulateThread;

private:
    friend class FolderViewMonitor;

    void dumpStack();
    void onPopulateDone();
    void removeFile(PFSModelBase pFS);
    void insertFile(PFSModelBase pFS);
    void connectModel(bool fConnect);
    void setListViewColumns();

    struct Selection;
    size_t getSelection(Selection &sel);

    void onPathActivated(const Gtk::TreeModel::Path &path);
    void onSelectionChanged();

    size_t                      _id;

    ElissoApplicationWindow     &_mainWindow;

    ViewState                   _state = ViewState::UNDEFINED;
    Glib::ustring               _strError;          // only with ViewState::ERROR, use setError() to set
    FolderViewMode              _mode = FolderViewMode::UNDEFINED;
    FolderViewMode              _modeBeforeError = FolderViewMode::UNDEFINED;

    Gtk::IconView               _iconView;
    TreeViewPlus                _treeView;
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

/***************************************************************************
 *
 *  FolderViewMonitor
 *
 **************************************************************************/

class FolderViewMonitor : public FSMonitorBase
{
public:
    FolderViewMonitor(ElissoFolderView &view)
        : FSMonitorBase(),
          _view(view)
    { };

    virtual void onItemRemoved(PFSModelBase &pFS) override;
    virtual void onDirectoryAdded(PFSDirectory &pDir) override;

private:
    ElissoFolderView &_view;
};
typedef shared_ptr<FolderViewMonitor> PFolderViewMonitor;

#endif // ELISSO_FOLDERVIEW_H
