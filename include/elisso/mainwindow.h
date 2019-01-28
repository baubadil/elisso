/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_MAINWINDOW_H
#define ELISSO_MAINWINDOW_H

#include "elisso/application.h"
#include "elisso/folderview.h"
#include "elisso/foldertree.h"

class ElissoAction;
typedef Glib::RefPtr<Gio::SimpleAction> PSimpleAction;

class ElissoApplication;

enum class ShowHideOrNothing : uint8_t
{
    SHOW,
    HIDE,
    DO_NOTHING
};


/***************************************************************************
 *
 *  ElissoApplicationWindow
 *
 **************************************************************************/

/**
 *  There is only a constructor, no "create" method with a refptr, since
 *  the constructor adds the new instance to the GtkApplication behind
 *  ElissoApplication.
 *
 *  Window hierarchy:
 *
 *  ElissoApplicationWindow
 *   |
 *   +- GtkMenuBar
 *   |
 *   +- GtkBox (parent for toolbar)
 *       |
 *       +- GtkToolbar
 *       |
 *       +- GtkPaned: parent to split between tree (left) and notebook (right)
 *           |
 *           +- ElissoFolderTreeMgr (derived from GtkScrolledWindow)
 *           |   |
 *           |   +- TreeViewPlus (our TreeView subclass)
 *           |
 *           +- GtkBox (parent for notebook and status bar)
 *               |
 *               +- GtkNotebook
 *               |   |
 *               |   +- Tab: ElissoFolderView (derived from GtkOverlay)
 *               |   |   |
 *               |   |   +- GtkPaned: to split between the icon/list (left) and image preview (right)
 *               |   |       |
 *               |   |       +- GtkScrolledWindow (icon/list view parent)
 *               |   |       |   |
 *               |   |       |   +- GtkTreeView, GtkIconView
 *               |   |       |
 *               |   |       +- GtkScrolledWindow
 *               |   |           |
 *               |   |           +- ElissoFilePreview
 *               |   |
 *               |   +- maybe another Tab: ElissoFolderView (subclass of GtkOverlay)
 *               |     .....
 *               |
 *               +- GtkStatusBar
 *
 */
class ElissoApplicationWindow : public Gtk::ApplicationWindow
{
public:
    ElissoApplicationWindow(ElissoApplication &app);

    virtual ~ElissoApplicationWindow();

    /**
     *  Returns the ElissoApplication that created us.
     */
    ElissoApplication& getApplication()
    {
        return _app;
    }

    int errorBox(Glib::ustring strMessage);

    /**
     *  Returns the notebook that makes up the right of the member Gtk::Paned, which in turns
     *  has ElissoFolderView widgets as notebook pages.
     */
    Gtk::Notebook& getNotebook();

    /**
     *  Returns the ElissoFolderView that is on the currently active notebook page.
     */
    ElissoFolderView* getActiveFolderView();

    /**
     *  Adds a new tab to the GTK notebook in the right pane with an ElissoFolderView
     *  for the given directory inside.
     *
     *  If pDir is nullptr, we retrieve the user's home directory from the FS backend.
     */
    void addFolderTab(PFsObject pDirOrSymlink);

    void addFolderTab(const std::string &strError);

    void focusPathEntryField();

    void setWaitCursor(Glib::RefPtr<Gdk::Window> pWindow,
                       Cursor cursor);

    /**
     *  Called from ElissoFolderView::onSelectionChanged() whenever the selection changes.
     *  pSel can be nullptr to disable all items.
     */
    void enableEditActions(FileSelection *pSel);

    /**
     *  Called from ElissoFolderView::setDirectory() to enable back/forward actions.
     */
    void enableBackForwardActions();

    /**
     *  Called from ElissoFolderView::handleAction to change the preview menu item state.
     */
    void setShowingPreview(bool fShowingPreview);

    void onLoadingFolderView(ElissoFolderView &view);

    /**
     *  Gets called when the main window needs updating as a result of the folder view changing. In particular:
     *
     *   -- when the notebook page changes;
     *
     *   -- when the state of the current notebook page changes.
     *
     *  In both cases we want to update the title bar, and we want to update the tree to point to the
     *  folder that's displaying on the right.
     */

    void onFolderViewLoaded(ElissoFolderView &view);

    void onNotebookTabChanged(ElissoFolderView &view);

    /**
     *  Sets the main window title to the full path of the current folder view.
     *  This is in a separate method because it needs to be called both from
     *  onLoadingFolderView() and onNotebookTabChanged(). Returns the full path
     *  so the caller can use it elsewhere.
     */
    Glib::ustring updateWindowTitle(ElissoFolderView &view);

    /**
     *  Updates the left status bar with the "current" text.
     *  This is shared between all folder views, so the folder view calls
     *  this after having composed a meaningful text.
     */
    void setStatusbarCurrent(const Glib::ustring &str);

    void setThumbnailerProgress(uint current, uint max, ShowHideOrNothing shn);

    /**
     *  Updates the right status bar with the free space on the file system
     *  that the given directory belongs to (which is the one currently seen
     *  in the folder).
     *
     */
    void setStatusbarFree(PFsObject pDir);

    void selectInFolderTree(PFsObject pDir);

    /**
     *  Handler for the "button press event" signal for
     *
     *   -- the IconView when the folder contents is in icon mode;
     *
     *   -- the TreeViewPlus of the folder contents when in list mode;
     *
     *   -- the TreeViewPlus of the folder tree on the left.
     *
     *  If this returns true, then the event has been handled, and the parent handler should NOT
     *  be called.
     */
    bool onButtonPressedEvent(GdkEventButton *pEvent, TreeViewPlusMode mode);

    /**
     *  This is called by our subclass of the GTK TreeView, TreeViewPlus, which
     *  emulates the right-click and selection behavior of Nautilus/Nemo.
     *
     *  Types of context menus to be created:
     *
     *   -- One file selected.              Open, Copy/Cut/Paste, Rename, Delete
     *
     *   -- One folder selected, closed.    Open, Copy/Cut/Paste, Rename, Delete
     *
     *   -- One folder selected, opened.    Open, Copy/Cut/Paste, Rename, Delete
     *
     *   -- Multiple objects selected.      Copy/Cut/Paste, Delete
     */
    void onMouseButton3Pressed(GdkEventButton *pEvent, MouseButton3ClickType clickType);

    /**
     *  Opens the given file-system object.
     *  As a special case, if pFS == nullptr, we try to get the current selection,
     *  but will take it only if exactly one object is selected.
     *
     *  If the object is a directory or a symlink to one, we call setDirectory().
     *  Otherwise we open the file with the
     */
    void openFile(PFsObject pFS,
                  PAppInfo pAppInfo);

    enum class OpenFolder
    {
        TERMINAL,
        NEMO
    };

    void openFolderExternally(PFsObject pFS, OpenFolder o);

    void addFileOperation(FileOperationType type,
                          const FsVector &vFiles,
                          PFsObject pTarget);
    bool areFileOperationsRunning() const;

    /**
     *  Shows and positions the preview hidden with the given image file. The window is shown immediately,
     *  and loading the image file is offloaded to a separate thread. If the window is already visible,
     *  the existing file is replaced.
     *
     *  If pFile is nullptr, the preview window is hidden.
     */
    void showPreviewWindow(PFsGioFile pFile,
                           ElissoFolderView &currentFolderView);

protected:
    void initActionHandlers();

    /**
     *  Adds an action handler that calls handleActiveViewAction() when triggered.
     */
    PSimpleAction addActiveViewActionHandler(const string &strAction);

    /**
     *  Adds an action handler that calls handleTreeAction() when triggered.
     */
    PSimpleAction addTreeActionHandler(const string &strAction);

    void setSizeAndPosition();

    void setWindowTitle(Glib::ustring str);

    void enableViewTabActions();

    void enableViewTypeActions(bool f);

    /**
     *  Handles all actions that operate on the currently active folder view
     *  in the notebook.
     */
    void handleActiveViewAction(const std::string &strAction);

    ElissoFolderView* doAddTab();

    /**
     *  Closes the notebook tab for the given ElissoFolderView. If this is the last
     *  tab, it closes the entire ElissoApplicationWindow.
     */
    void closeFolderTab(ElissoFolderView &viewClose);

    /**
     *  Called once on window creation and then via a signal callback whenever the
     *  clipboard contents change. This updates whether the "paste" action is available.
     */
    void onClipboardChanged();

    // Override window signal handlers
    virtual void on_size_allocate(Gtk::Allocation& allocation) override;
    virtual bool on_window_state_event(GdkEventWindowState*) override;
    virtual bool on_delete_event(GdkEventAny *) override;

    ElissoApplication               &_app;

    struct Impl;
    Impl                            *_pImpl;
};

#endif // ELISSO_MAINWINDOW_H
