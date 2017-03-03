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

#include "xwp/basetypes.h"
#include "xwp/stringhelp.h"
#include "xwp/debug.h"
#include "xwp/regex.h"

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


#endif // ELISSO_H
