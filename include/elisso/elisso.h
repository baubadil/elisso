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
// const DebugFlag CMD_TOP         = (1 << 20);
const DebugFlag FOLDER_POPULATE     = (1 << 21);
const DebugFlag FOLDER_STACK        = (1 << 22);

using namespace XWP;

DEF_STRING(ACTION_FILE_QUIT, "file-quit");
DEF_STRING(ACTION_VIEW_ICONS, "view-icons");
DEF_STRING(ACTION_VIEW_LIST, "view-list");
DEF_STRING(ACTION_VIEW_COMPACT, "view-compact");
DEF_STRING(ACTION_GO_PARENT, "go-parent");
DEF_STRING(ACTION_GO_BACK, "go-back");
DEF_STRING(ACTION_GO_FORWARD, "go-forward");
DEF_STRING(ACTION_GO_HOME, "go-home");
DEF_STRING(ACTION_ABOUT, "about");

DEF_STRING(SETTINGS_WINDOWPOS, "window-pos");

#endif // ELISSO_H
