    /*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_H
#define ELISSO_H

#define ELISSO_VERSION "0.1.0"

#include <gtkmm.h>

#include "xwp/stringhelp.h"
#include "xwp/debug.h"

typedef std::shared_ptr<Gtk::TreeRowReference> PRowReference;

Glib::ustring implode(const std::string &strGlue, const std::vector<Glib::ustring> v);

// #define USE_XICONVIEW
// #define USE_TESTFILEOPS

// const DebugFlag DEBUG_ALWAYS          = 0;

    // low-level
// const DebugFlag XWPTAGS         = (1 <<  1);
// const DebugFlag FILE_LOW        = (1 <<  3);

    // mid-level
// const DebugFlag FILE_MID        = (1 << 15);
// const DebugFlag XICONVIEW       = (1 << 16);

    // high-level
// const DebugFlag FILE_HIGH            = (1 << 19);
// const DebugFlag CMD_TOP              = (1 << 20);

// const DebugFlag FOLDER_POPULATE_HIGH    = (1 << 21);
// const DebugFlag FOLDER_POPULATE_LOW     = (1 << 22);
// const DebugFlag FILEMONITORS            = (1 << 23);
const DebugFlag FOLDER_STACK            = (1 << 24);
const DebugFlag THUMBNAILER             = (1 << 25);
const DebugFlag WINDOWHIERARCHY         = (1 << 26);
const DebugFlag CLIPBOARD               = (1 << 27);
const DebugFlag PROGRESSDIALOG          = (1 << 28);
const DebugFlag TREEMODEL               = (1 << 29);
const DebugFlag MOUNTS                  = (1 << 30);

#define ICON_SIZE_SMALL     16
#define ICON_SIZE_BIG       128

using namespace XWP;

enum class FileOperationType
{
    TEST,
    TRASH,
    MOVE,
    COPY
};

DEF_STRING(APPLICATION_NAME, "elisso");

DEF_STRING(CLIPBOARD_TARGET_UTF8_STRING, "UTF8_STRING");
DEF_STRING(CLIPBOARD_TARGET_GNOME_COPIED_FILES, "x-special/gnome-copied-files");

DEF_STRING(ICON_FILE_MANAGER, "system-file-manager");       // our application icon for now
DEF_STRING(ICON_FOLDER_GENERIC, "folder");
DEF_STRING(ICON_FILE_GENERIC, "text-x-generic");
DEF_STRING(ICON_FILE_LOADING, "image-loading");

DEF_STRING(MENUITEM_OPEN, "Open");
DEF_STRING(MENUITEM_OPEN_IN_TAB, "Open in new tab");
DEF_STRING(MENUITEM_OPEN_IN_TERMINAL, "Open in terminal");
DEF_STRING(MENUITEM_TRASH, "Move to trash");

DEF_STRING(TYPE_FILE, "File");
DEF_STRING(TYPE_FOLDER, "Folder");
DEF_STRING(TYPE_LINK_TO_FOLDER, "Link to folder");
DEF_STRING(TYPE_LINK_TO_FILE, "Link to file");
DEF_STRING(TYPE_LINK_TO_OTHER, "Link");
DEF_STRING(TYPE_LINK_TO, "Link to ");
DEF_STRING(TYPE_BROKEN_LINK, "Broken link");
DEF_STRING(TYPE_SPECIAL, "Special file");
DEF_STRING(TYPE_MOUNTABLE, "Mountable");

DEF_STRING(ACTION_FILE_NEW_TAB, "file-new-tab");
DEF_STRING(ACTION_FILE_NEW_WINDOW, "file-new-window");
DEF_STRING(ACTION_FILE_OPEN_IN_TERMINAL, "file-open-in-terminal");
DEF_STRING(ACTION_FILE_CREATE_FOLDER, "file-create-folder");
DEF_STRING(ACTION_FILE_CREATE_DOCUMENT, "file-create-document");
DEF_STRING(ACTION_FILE_PROPERTIES, "file-properties");
DEF_STRING(ACTION_FILE_QUIT, "file-quit");
DEF_STRING(ACTION_FILE_CLOSE_TAB, "file-close-tab");

// Items that operate on the selected rows in the contents.
DEF_STRING(ACTION_EDIT_OPEN_SELECTED, "edit-open-selected");
DEF_STRING(ACTION_EDIT_OPEN_SELECTED_IN_TAB, "edit-open-selected-in-tab");
DEF_STRING(ACTION_EDIT_OPEN_SELECTED_IN_TERMINAL, "edit-open-selected-in-terminal");
DEF_STRING(ACTION_EDIT_COPY, "edit-copy");
DEF_STRING(ACTION_EDIT_CUT, "edit-cut");
DEF_STRING(ACTION_EDIT_PASTE, "edit-paste");
DEF_STRING(ACTION_EDIT_SELECT_ALL, "edit-select-all");
DEF_STRING(ACTION_EDIT_SELECT_NEXT_PREVIEWABLE, "edit-select-next-previewable");
DEF_STRING(ACTION_EDIT_SELECT_PREVIOUS_PREVIEWABLE, "edit-select-previous-previewable");
DEF_STRING(ACTION_EDIT_RENAME, "edit-rename");
DEF_STRING(ACTION_EDIT_TRASH, "edit-trash");
#ifdef USE_TESTFILEOPS
DEF_STRING(ACTION_EDIT_TEST_FILEOPS, "edit-test-fileops");
#endif
DEF_STRING(ACTION_EDIT_PROPERTIES, "edit-properties");

// Items that operate on the selected rows in the tree. Tree popup menu only.
DEF_STRING(ACTION_TREE_OPEN_SELECTED, "tree-open-selected");
DEF_STRING(ACTION_TREE_OPEN_SELECTED_IN_TAB, "tree-open-selected-in-tab");
DEF_STRING(ACTION_TREE_OPEN_SELECTED_IN_TERMINAL, "tree-open-selected-in-terminal");
DEF_STRING(ACTION_TREE_TRASH_SELECTED, "tree-trash-selected");

DEF_STRING(ACTION_VIEW_NEXT_TAB, "view-next-tab");
DEF_STRING(ACTION_VIEW_PREVIOUS_TAB, "view-previous-tab");
DEF_STRING(ACTION_VIEW_ICONS, "view-icons");
DEF_STRING(ACTION_VIEW_LIST, "view-list");
DEF_STRING(ACTION_VIEW_COMPACT, "view-compact");
DEF_STRING(ACTION_VIEW_SHOW_PREVIEW, "view-show-preview");
DEF_STRING(ACTION_VIEW_REFRESH, "view-refresh");

DEF_STRING(ACTION_GO_PARENT, "go-parent");
DEF_STRING(ACTION_GO_BACK, "go-back");
DEF_STRING(ACTION_GO_FORWARD, "go-forward");
DEF_STRING(ACTION_GO_HOME, "go-home");
DEF_STRING(ACTION_GO_COMPUTER, "go-computer");
DEF_STRING(ACTION_GO_TRASH, "go-trash");
DEF_STRING(ACTION_ABOUT, "about");

DEF_STRING(SETTINGS_WINDOWPOS, "window-pos");
DEF_STRING(SETTINGS_LIST_COLUMN_WIDTHS, "list-column-widths");

#endif // ELISSO_H
