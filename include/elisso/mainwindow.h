/*
 * elisso (C) 2016--2017 Baubadil GmbH.
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


/***************************************************************************
 *
 *  ElissoApplicationWindow
 *
 **************************************************************************/

/**
 *  Window hierarchy:
 *
 *  ElissoApplicationWindow
 *   |
 *   +- GtkMenuBar
 *   |
 *   +- GtkBox
 *       |
 *       +- GtkToolbar
 *       |
 *       +- GtkPaned: to split between tree (left) and notebook (right)
 *           |
 *           +- ElissoFolderTree (subclass of GtkScrolledWindow)
 *           |
 *           +- GtkNotebook
 *               |
 *               +- ElissoFolderView (subclass of GtkScrolledWindow)
 *               |   |
 *               |   +- GtkTreeView (list view)
 *               |
 *               +- ElissoFolderView (subclass of GtkScrolledWindow)
 *                   |
 *                   +- GtkTreeView (list view)
 *
 *
 *  There is only a constructor, no "create" method with a refptr, since
 *  the constructor adds the new instance to the GtkApplication behing
 *  ElissoApplication.
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
     *  Returns the ElissoFolderTree that makes up the left of the member Gtk::Paned.
     */
    ElissoFolderTree& getTreeView()
    {
        return _treeViewLeft;
    }

    /**
     *  Returns the notebook that makes up the right of the member Gtk::Paned, which in turns
     *  has ElissoFolderView widgets as notebook pages.
     */
    Gtk::Notebook& getNotebook()
    {
        return _notebook;
    }

    ElissoFolderView* getActiveFolderView();

    void addFolderTab(PFSModelBase pDirOrSymlink);
    void addFolderTab(const std::string &strError);

    void enableEditActions(size_t cFolders, size_t cOtherFiles);
    void enableBackForwardActions();

    void onLoadingFolderView(ElissoFolderView &view);
    void onFolderViewLoaded(ElissoFolderView &view);
    void selectInFolderTree(PFSModelBase pDir);

    void openFolderInTerminal(PFSModelBase pFS);

protected:
    void initActionHandlers();
    void setSizeAndPosition();
    void setWindowTitle(Glib::ustring str);
    void enableViewTabActions();
    void enableViewTypeActions(bool f);

    void handleViewAction(const std::string &strAction);

    void closeFolderTab(ElissoFolderView &viewClose);

    Gtk::ToolButton* makeToolButton(const Glib::ustring &strIconName,
                                    PSimpleAction pAction,
                                    bool fAlignRight = false);

    // Override window signal handlers
    virtual void on_size_allocate(Gtk::Allocation& allocation) override;
    virtual bool on_window_state_event(GdkEventWindowState*) override;
    virtual bool on_delete_event(GdkEventAny *) override;

    ElissoApplication               &_app;

    PSimpleAction                   _pActionEditOpenSelected;
    PSimpleAction                   _pActionEditOpenSelectedInTab;
    PSimpleAction                   _pActionEditOpenSelectedInTerminal;
    PSimpleAction                   _pActionEditCopy;
    PSimpleAction                   _pActionEditCut;
    PSimpleAction                   _pActionEditPaste;
    PSimpleAction                   _pActionEditSelectAll;
    PSimpleAction                   _pActionEditRename;
    PSimpleAction                   _pActionEditTrash;
    PSimpleAction                   _pActionEditTestFileops;
    PSimpleAction                   _pActionEditProperties;

    PSimpleAction                   _pActionGoBack;
    Gtk::ToolButton                 *_pButtonGoBack;
    PSimpleAction                   _pActionGoForward;
    Gtk::ToolButton                 *_pButtonGoForward;
    PSimpleAction                   _pActionGoParent;
    Gtk::ToolButton                 *_pButtonGoParent;
    PSimpleAction                   _pActionGoHome;
    Gtk::ToolButton                 *_pButtonGoHome;

    PSimpleAction                   _pActionViewNextTab;
    PSimpleAction                   _pActionViewPreviousTab;
    PSimpleAction                   _pActionViewIcons;
    Gtk::ToolButton                 *_pButtonViewIcons;
    PSimpleAction                   _pActionViewList;
    Gtk::ToolButton                 *_pButtonViewList;
    PSimpleAction                   _pActionViewCompact;
//     Gtk::ToolButton                 *_pButtonViewCompact;
    PSimpleAction                   _pActionViewRefresh;
    Gtk::ToolButton                 *_pButtonViewRefresh;

    int                             _x = 0, _y = 0, _width = 100, _height = 100;
    bool                            _fIsMaximized = false,
                                    _fIsFullscreen = false;

    Gtk::Box                        _mainVBox;
    Gtk::Paned                      _vPaned;
    ElissoFolderTree                _treeViewLeft;
    Gtk::Notebook                   _notebook;
};

#endif // ELISSO_MAINWINDOW_H
