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

class ElissoApplication;
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
    EDIT_SELECT_NEXT_PREVIEWABLE,
    EDIT_SELECT_PREVIOUS_PREVIEWABLE,
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
    VIEW_SHOW_PREVIEW,
    VIEW_REFRESH,
    GO_BACK,
    GO_FORWARD,
    GO_PARENT,
    GO_HOME,
    GO_COMPUTER,
    GO_TRASH,
    GO_LOCATION,
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
 *  and thus consumes the right two thirds of a folder window.
 *
 *  See ElissoApplicationWindow for the window hierarchy.
 *
 *  The class derives from Overlay to be able to overlay a "loading" spinner while
 *  the folder is populating. It has exactly one child, a Gtk::Paned with two
 *  more children:
 *
 *   -- most importantly, on the left, a ScrolledWindow, which in turn contains
 *      either a TreeView or IconView child, depending on which view is selected.
 *      Both views are constructed in the ElissoFolderView constructor, but inserted
 *      and removed by setViewMode() when the view is changed;
 *
 *   -- an ElissoPreviewPane on the right, which is a subclassed Gtk::EventBox
 *      containing a  Gtk::Image for previews. This is hidden and only shown when
 *      a previewable file is selected in the folder contents on the left.
 */
class ElissoFolderView : public Gtk::Overlay, ProhibitCopy
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
     *  Returns the FsDirectory that's currently showing. In the event that
     *  we're displaying a directory via a symlink, an FsSymlink is returned
     *  that points to the directory.
     */
    PFsObject getDirectory()
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
    bool setDirectory(PFsObject pDirOrSymlinkToDir,
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

    /**
     *  Switches the folder's view between list, icon or compact view, and shows, hides and adjusts
     *  all the  Gtk controls accordingly.
     *
     *  setError() calls this with the view mode FolderViewMode::ERROR when errors occur.
     */
    void setViewMode(FolderViewMode m);

    /**
     *  Shows or hides the preview pane.
     */
    void showPreviewPane(bool fShow);

    /**
     *  Replaces the entire view with an error message. This sets both the state and the view to
     *  special error modes, which the user can get out of by selecting a different folder again.
     */
    void setError(Glib::ustring strError);

    /**
     *  Gets called whenever the folder selection changes to display info on the selected items.
     */
    void updateStatusbar(FileSelection *pSel);

    /*
     *  Public selection methods
     */

    /**
     *  Selects all items in the folder (e.g. because of ctrl+a).
     */
    void selectAll();

    /**
     *  Attempts to select the next or previous file in the currently displayed folder which
     *  might be previewable. If fNext is true, then the next file is displayed; otherwise the
     *  previous. If nothing is currently selected or more than one file is selected, this
     *  does nothing.
     */
    void selectPreviewable(bool fNext);

    /**
     *  Returns the single selected folder (directory or symlink pointing to one),
     *  or nullptr if either nothing is selected or the selection is not a single
     *  such folder.
     */
    PFsObject getSelectedFolder();

    /*
     *  Public file action methods
     */

    /**
     *  Fills the given FileSelection object with the selected items from the
     *  active icon or tree view. Returns the total no. of item selected.
     */
    size_t getSelection(FileSelection &sel);

    /**
     *  Called from our mouse button handler override to handle mouse button 1 and 3 clicks
     *  with advanced selections.
     */
    MouseButton3ClickType handleClick(GdkEventButton *pEvent,       //!< in: mouse button event
                                      Gtk::TreeModel::Path &path);  //!< out: path of selected item

    /**
     *  Invokes grab_focus() on either the icon or tree (list) view, depending on the view mode.
     */
    void grabFocus();

    /**
     *  Called from ElissoApplicationWindow::handleViewAction() to handle
     *  those actions that operate on the current folder view or the
     *  files therein.
     *
     *  Some of these are asynchronous, most are not.
     */
    void handleAction(FolderAction action);

    /**
     *  Called from handleAction() to process clipboard copy and cut events.
     */
    void handleClipboardCopyOrCut(bool fCut);

    /**
     *  Called from handleAction() to process clipboard paste events.
     *  This may launch a FileOperation with FileOperationType::COPY or MOVE.
     */
    void handleClipboardPaste();

    /**
     *  Called from handleAction() to prompts for a name and then create a new subdirectory
     *  in the folder that is currently showing. Returns the directory object.
     *
     *  This may throw FSException.
     */
    PFsDirectory handleCreateSubfolder();

    /**
     *  Called from handleAction to prompt for the name of a new empty file and then create it
     *  in the folder that is currently showing. Returns the new file object.
     *
     *  This may throw FSException.
     */
    PFSFile handleCreateEmptyFile();

    /**
     *  Called from handleAction() to prompt for a new name for the selected file or folder and
     *  then rename it.
     */
    void handleRenameSelected();

    /**
     *  Called from handleAction()  to trash all files which are currently selected in the folder contents.
     *  This may launch a FileOperation with FileOperationType::TRASH.
     */
    void handleTrashSelected();

#ifdef USE_TESTFILEOPS
    void testFileopsSelected();
#endif

private:
    friend class FolderViewMonitor;
    friend class TreeViewPlus;
    friend class ElissoPreviewPane;

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
    PFsObject getFsObjFromRow(Gtk::TreeModel::Row &row);

    Gtk::ListStore::iterator insertFile(PFsObject pFS);
    void removeFile(PFsObject pFS);
    void renameFile(PFsObject pFS, const std::string &strOldName, const std::string &strNewName);
    void connectModel(bool fConnect);

    void setNotebookTabTitle();

    PPixbuf loadIcon(PFsObject pFS,
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

    /**
     *  Called from ElissoPreviewPane when a preview has finished loading. We can then
     *  scroll the folder contents into view if necessary. The currently selected item
     *  may be scrolled out of view if a "next" or "previous" item was selected through
     *  folder actions or by clicking on the preview pane.
     */
    void onPreviewReady(PFsGioFile pFile);

    ElissoApplication& getApplication();

    size_t                      _id;

    ElissoApplicationWindow     &_mainWindow;
    PFsObject                   _pDir;

    struct Impl;
    Impl                        *_pImpl;
};


/***************************************************************************
 *
 *  FolderViewMonitor
 *
 **************************************************************************/

/**
 *  FsMonitorBase subclassed tailored for the folder contents list.
 *
 *  This is for updating the folder contents when file operations are
 *  going on.
 */
class FolderViewMonitor : public FsMonitorBase
{
public:
    FolderViewMonitor(ElissoFolderView &view)
        : FsMonitorBase(),
          _view(view)
    { };

    virtual void onItemAdded(PFsObject &pFS) override;
    virtual void onItemRemoved(PFsObject &pFS) override;
    virtual void onItemRenamed(PFsObject &pFS, const std::string &strOldName, const std::string &strNewName) override;

private:
    ElissoFolderView &_view;
};
typedef shared_ptr<FolderViewMonitor> PFolderViewMonitor;

#endif // ELISSO_FOLDERVIEW_H
