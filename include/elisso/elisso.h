/*
 * elisso -- PHP documentation tool. (C) 2015--2016 Baubadil GmbH.
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

#include "xwp/stringhelp.h"
#include "xwp/debug.h"

// const DebugFlag DEBUG_ALWAYS          = 0;

    // low-level
// const DebugFlag XWPTAGS         = (1 <<  1);
// const DebugFlag FILE_LOW        = (1 <<  3);

    // mid-level
// const DebugFlag FILE_MID        = (1 << 15);

    // high-level
// const DebugFlag CMD_TOP              = (1 << 20);
const DebugFlag FOLDER_POPULATE_HIGH    = (1 << 21);
const DebugFlag FOLDER_POPULATE_LOW     = (1 << 22);
const DebugFlag FOLDER_STACK            = (1 << 23);

using namespace XWP;

DEF_STRING(APPLICATION_NAME, "elisso");

DEF_STRING(ACTION_FILE_NEW_TAB, "file-new-tab");
DEF_STRING(ACTION_FILE_NEW_WINDOW, "file-new-window");
DEF_STRING(ACTION_FILE_QUIT, "file-quit");
DEF_STRING(ACTION_FILE_CLOSE_TAB, "file-close-tab");

DEF_STRING(ACTION_EDIT_OPEN, "edit-open");
DEF_STRING(ACTION_EDIT_TERMINAL, "edit-terminal");
DEF_STRING(ACTION_EDIT_COPY, "edit-copy");
DEF_STRING(ACTION_EDIT_CUT, "edit-cut");
DEF_STRING(ACTION_EDIT_PASTE, "edit-paste");
DEF_STRING(ACTION_EDIT_RENAME, "edit-rename");
DEF_STRING(ACTION_EDIT_TRASH, "edit-trash");
DEF_STRING(ACTION_EDIT_PROPERTIES, "edit-properties");

DEF_STRING(ACTION_VIEW_ICONS, "view-icons");
DEF_STRING(ACTION_VIEW_LIST, "view-list");
DEF_STRING(ACTION_VIEW_COMPACT, "view-compact");
DEF_STRING(ACTION_VIEW_REFRESH, "view-refresh");

DEF_STRING(ACTION_GO_PARENT, "go-parent");
DEF_STRING(ACTION_GO_BACK, "go-back");
DEF_STRING(ACTION_GO_FORWARD, "go-forward");
DEF_STRING(ACTION_GO_HOME, "go-home");
DEF_STRING(ACTION_ABOUT, "about");

DEF_STRING(SETTINGS_WINDOWPOS, "window-pos");
DEF_STRING(SETTINGS_LIST_COLUMN_WIDTHS, "list-column-widths");

#endif // ELISSO_H
