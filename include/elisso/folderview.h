/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_FOLDERVIEW_H
#define ELISSO_FOLDERVIEW_H

#include "elisso/elisso.h"
#include "elisso/fsmodel_gio.h"
#include "elisso/treeviewplus.h"

#include "xwp/flagset.h"

#ifdef USE_XICONVIEW
#include "x-gtk/x-iconview_cpp.h"
#endif

class ElissoFolderView;
struct FileSelection;
class Thumbnailer;

struct Thumbnail;
typedef std::shared_ptr<Thumbnail> PThumbnail;

typedef Glib::RefPtr<Gio::AppInfo> PAppInfo;

struct ViewPopulatedResult;
typedef std::shared_ptr<ViewPopulatedResult> PViewPopulatedResult;

/**
 *  Type of GTK cursor to display. Used by ElissoFolderView::setWaitCursor(),
 *  which calls ElissoApplicationWindow::setWaitCursor, which loads the
 *  corresponding GTK stock cursors from the cursor theme.
 */
enum class Cursor
{
    DEFAULT,            // Standard pointer, used most of the time.
    WAIT_PROGRESS,      // Pointer with a clock attached, to indicate activity in a background thread, but GUI is responsive.
    WAIT_BLOCKED        // Wait clock, while GUI thread is not responsive.
};

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
    UNDEFINED,          // After initialization only.
    POPULATING,         /* setDirectory() has been called, and a populate thread is running in
                           the background. All calls are valid during this time, including another
                           setDirectory() (which will kill the existing populate thread). */
    REFRESHING,         // Like POPULATING except that we're not clearing the view first.
    INSERTING,          /* Temporary state after the populate thread has finished and items are being inserted
                           into the tree/icon view's model. Since this has to happen on the GUI thread, this
                           might block the GUI for a second. */
    POPULATED,          /* The populate thread was successful, and the contents of _pDir are showing. */
    ERROR               /* An error occured. This hides the tree or icon view containers and displays the
                           error message instead. The only way to get out of this state is to call
                           setDirectory() to try and display a directory again. */
};

enum class FolderAction
{
    EDIT_COPY,
    EDIT_CUT,
    EDIT_PASTE,
    EDIT_SELECT_ALL,
    EDIT_OPEN_SELECTED,
    FILE_CREATE_FOLDER,
    FILE_CREATE_DOCUMENT,
    EDIT_RENAME,
    EDIT_TRASH,
#ifdef USE_TESTFILEOPS
    EDIT_TEST_FILEOPS,
#endif
    VIEW_ICONS,
    VIEW_LIST,
    VIEW_COMPACT,
    VIEW_REFRESH,
    GO_BACK,
    GO_FORWARD,
    GO_PARENT,
    GO_HOME,
    GO_COMPUTER,
    GO_TRASH,
};

class ElissoApplicationWindow;

/**
 *  Bit flags to be used with setDirectory().
 */
enum class SetDirectoryFlag : uint8_t
{
    PUSH_TO_HISTORY         = (1 << 0),         // If set, the directory is added to the back/forward history stack.
    SELECT_PREVIOUS         = (1 << 1),         // If set, the previous directory is selected in the new contents list (useful for go back/parent).
    CLICK_FROM_TREE         = (1 << 2),         // If set, the contents are shown as a result of a click on the tree view, and we can spare the effort of finding the node in the tree.
    IS_REFRESH              = (1 << 3),         // If set, do not empty container; in that case, current pDir must be the same as the old pDir
};
// DEFINE_FLAGSET(SetDirectoryFlag)

typedef FlagSet<SetDirectoryFlag> SetDirectoryFlagSet;


/***************************************************************************
 *
 *  ElissoFolderView
 *
 **************************************************************************/

/**
 *  The ElissoFolderView is always created as a tab under the main window's notebook
 *  and thus consumes the right two thirds of a folder window. The class derives from
 *  Overlay to be able to overlay a "loading" spinner. Most importantly though,
 *  it contains a ScrolledWindow, which in turn contains either a TreeView or
 *  IconView child, depending on which view is selected. Both views are
 *  constructed in the ElissoFolderView constructor, but inserted and removed
 *  from the ScrolledWindow when the view is changed.
 */
class ElissoFolderView : public Gtk::Overlay
{
public:
    /**
     *  The constructor. This looks up the main window's notebook, creates the
     *  folder view and inserts it as a notebook tab.
     */
    ElissoFolderView(ElissoApplicationWindow &mainWindow, int &iPageInserted);

    /**
     *  The destructor.
     */
    virtual ~ElissoFolderView();

    /*
     *  Public view methods
     */

    /**
     *  Returns the tab ID. This is simply an integer to allow for quick comparison
     *  which tab an event came from.
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

    /**
     *  Gets called whenever the notebook tab on the right needs to be populated with the contents
     *  of another folder, in particular:
     *
     *   -- when a new folder tab is added (in particular on application startup);
     *
     *   -- if the user double-clicks on another folder in the folder contents (via onFileActivated());
     *
     *   -- if the user single-clicks on a folder in the tree on the left.
     *
     *  This starts a populate thread if needed, whose execution can take a long time.
     *
     *  Returns true if a populate thread was started, or false if the folder had already been populated.
     */
    bool setDirectory(PFSModelBase pDirOrSymlinkToDir,
                      SetDirectoryFlagSet fl);

    /**
     *  Handler for FolderAction::VIEW_REFRESH.
     */
    void refresh();

    /**
     *  Returns true if the "back" action should be enabled.
     */
    bool canGoBack();

    /**
     *  Handler for FolderAction::GO_BACK.
     */
    bool goBack();

    /**
     *  Returns true if the "forward" action should be enabled.
     */
    bool canGoForward();

    /**
     *  Handler for FolderAction::GO_FORWARD.
     */
    bool goForward();

    /**
     *  Sets a new state for the view.
     */
    void setState(ViewState s);

    void setViewMode(FolderViewMode m);

    void setError(Glib::ustring strError);

    void updateStatusbar(FileSelection *pSel);

    /*
     *  Public selection methods
     */
    void selectAll();

    PFSModelBase getSelectedFolder();

    /*
     *  Public file action methods
     */

    size_t getSelection(FileSelection &sel);

    /**
     *  Called from our mouse button handler override to handle mouse button 1 and 3 clicks
     *  with advanced selections.
     */
    MouseButton3ClickType handleClick(GdkEventButton *pEvent,       //!< in: mouse button event
                                      Gtk::TreeModel::Path &path);  //!< out: path of selected item

    void handleAction(FolderAction action);

    void clipboardCopyOrCutSelected(bool fCut);
    void clipboardPaste();

    PFSDirectory createSubfolderDialog();

    PFSFile createEmptyFileDialog();

    void renameSelected();

    /**
     *  Trashes all files which are currently selected in the folder contents.
     *  This launches a FileOperation with FileOperationType::TRASH.
     */
    void trashSelected();

#ifdef USE_TESTFILEOPS
    void testFileopsSelected();
#endif

private:
    friend class FolderViewMonitor;
    friend class TreeViewPlus;

    void setWaitCursor(Cursor cursor);
    void dumpStack();

    /**
     *  Gets called when the populate thread within setDirectory() has finished. We must now
     *  inspect the PopulateThread in the implementation struct for the results.
     *
     *  This also calls selectInFolderTree(), which causes the folder tree to follow the
     *  newly selected folder and populate sub-folders, if necessary.
     */
    void onPopulateDone(PViewPopulatedResult p);

    /**
     *  Returns the filesystem object for the given row, looking it up by name, or nullptr
     *  if it could not be found.
     */
    PFSModelBase getFileFromRow(Gtk::TreeModel::Row &row);

    Gtk::ListStore::iterator insertFile(PFSModelBase pFS);
    void removeFile(PFSModelBase pFS);
    void renameFile(PFSModelBase pFS, const std::string &strOldName, const std::string &strNewName);
    void connectModel(bool fConnect);

    void setNotebookTabTitle();

    PPixbuf loadIcon(PFSModelBase pFS,
                     FSTypeResolved tr,
                     int size,
                     bool *pfThumbnailing);
    void onThumbnailReady();

    bool isSelected(Gtk::TreeModel::Path &path);
    bool getPathAtPos(int x,
                      int y,
                      Gtk::TreeModel::Path &path);
    int countSelectedItems();
    void selectExactlyOne(Gtk::TreeModel::Path &path);

    void setIconViewColumns();
    void setListViewColumns();

    void onPathActivated(const Gtk::TreeModel::Path &path);
    void onSelectionChanged();

    size_t                      _id;

    ElissoApplicationWindow     &_mainWindow;
    struct Impl;
    Impl                        *_pImpl;

    ViewState                   _state = ViewState::UNDEFINED;
    Glib::ustring               _strError;          // only with ViewState::ERROR, use setError() to set
    FolderViewMode              _mode = FolderViewMode::UNDEFINED;
    FolderViewMode              _modeBeforeError = FolderViewMode::UNDEFINED;

    Gtk::Label                  _labelNotebookPage;
    Gtk::Label                  _labelNotebookMenu;

    Gtk::ScrolledWindow         _scrolledWindow;        // Parent of both icon and tree view.

#ifdef USE_XICONVIEW
    Gtk::XIconView              _iconView;
#else
    Gtk::IconView               _iconView;
#endif
    TreeViewPlus                _treeView;
//     Gtk::FlowBox                _compactView;
    Gtk::InfoBar                _infoBarError;
    Gtk::Label                  _infoBarLabel;
    Gtk::CellRendererPixbuf     _cellRendererIconSmall;
    Gtk::CellRendererPixbuf     _cellRendererIconBig;
    Gtk::CellRendererText       _cellRendererSize;

    Gtk::EventBox               *_pLoading = nullptr;

    PFSModelBase                _pDir;
    std::vector<std::string>    _aPathHistory;
    uint32_t                    _uPathHistoryOffset = 0;
};


/***************************************************************************
 *
 *  FolderViewMonitor
 *
 **************************************************************************/

/**
 *  FSMonitorBase subclassed tailored for the folder contents list.
 *
 *  This is for updating the folder contents when file operations are
 *  going on.
 */
class FolderViewMonitor : public FSMonitorBase
{
public:
    FolderViewMonitor(ElissoFolderView &view)
        : FSMonitorBase(),
          _view(view)
    { };

    virtual void onItemAdded(PFSModelBase &pFS) override;
    virtual void onItemRemoved(PFSModelBase &pFS) override;
    virtual void onItemRenamed(PFSModelBase &pFS, const std::string &strOldName, const std::string &strNewName) override;

private:
    ElissoFolderView &_view;
};
typedef shared_ptr<FolderViewMonitor> PFolderViewMonitor;

#endif // ELISSO_FOLDERVIEW_H
