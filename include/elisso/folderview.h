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

#include "elisso/elisso.h"
#include "elisso/fsmodel.h"
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
    INSERTING,          /* Temporary state after the populate thread has finished and items are being inserted
                           into the tree/icon view's model. Since this has to happen on the GUI thread, this
                           might block the GUI for a second. */
    POPULATED,          /* The populate thread was successful, and the contents of _pDir are showing. */
    ERROR               /* An error occured. This hides the tree or icon view containers and displays the
                           error message instead. The only way to get out of this state is to call
                           setDirectory() to try and display a directory again. */
};

class ElissoApplicationWindow;

/**
 *  Bit flags to be used with setDirectory().
 */
enum class SetDirectoryFlag : uint8_t
{
    PUSH_TO_HISTORY         = (1 << 0),         // If set, the directory is added to the back/forward history stack.
    SELECT_PREVIOUS         = (1 << 1),         // If set, the previous directory is selected in the new contents list (useful for go back/parent).
    CLICK_FROM_TREE         = (1 << 2)          // If set, the contents are shown as a result of a click on the tree view, and we can spare the effort of finding the node in the tree.
};
// DEFINE_FLAGSET(SetDirectoryFlag)

typedef FlagSet<SetDirectoryFlag> SetDirectoryFlagSet;


/***************************************************************************
 *
 *  ElissoFolderView
 *
 **************************************************************************/

/**
 *  The ElissoFolderView is the right two thirds of a folder window and derives from
 *  Overlay to be able to overlay a "loading" spinner. Most importantly though,
 *  it contains a ScrolledWindow, which in turn contains either a TreeView or
 *  IconView child, depending on which view is selected. Both views are
 *  constructed in the ElissoFolderView constructor, but inserted and removed
 *  from the ScrolledWindow when the view is changed.
 */
class ElissoFolderView : public Gtk::Overlay
{
public:
    ElissoFolderView(ElissoApplicationWindow &mainWindow, int &iPageInserted);
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

    void updateStatusbar(FileSelection *pSel);

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

    void openFile(PFSModelBase pFS,
                  PAppInfo pAppInfo);

    PFSDirectory createSubfolderDialog();

    PFSFile createEmptyFileDialog();

    void renameSelected();

    void trashSelected();

#ifdef USE_TESTFILEOPS
    void testFileopsSelected();
#endif

    class PopulateThread;

private:
    friend class FolderViewMonitor;
    friend class TreeViewPlus;

    void setWaitCursor(Cursor cursor);
    void dumpStack();
    void onPopulateDone(PViewPopulatedResult p);
    Gtk::ListStore::iterator insertFile(PFSModelBase pFS);
    void removeFile(PFSModelBase pFS);
    void renameFile(PFSModelBase pFS, const std::string &strOldName, const std::string &strNewName);
    void connectModel(bool fConnect);

    void setNotebookTabTitle();

    PPixbuf loadIcon(const Gtk::TreeModel::iterator& it,
                     PFSModelBase pFS,
                     int size,
                     bool *pfThumbnailing);
    void onThumbnailReady();
    PPixbuf cellDataFuncIcon(const Gtk::TreeModel::iterator& it,
                             Gtk::TreeModelColumn<PPixbuf> &column,
                             int iconSize);

    bool isSelected(Gtk::TreeModel::Path &path);
    bool getPathAtPos(int x, int y, Gtk::TreeModel::Path &path);
    int countSelectedRows();
    void selectExactlyOne(Gtk::TreeModel::Path &path);

    bool onButtonPressedEvent(GdkEventButton *pEvent);
    void setIconViewColumns();
    void setListViewColumns();

    size_t getSelection(FileSelection &sel);

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
