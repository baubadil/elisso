/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#define DEF_STRING_IMPLEMENTATION

#include "elisso/mainwindow.h"

#include "xwp/debug.h"

/***************************************************************************
 *
 *  Globals
 *
 **************************************************************************/

/***************************************************************************
 *
 *  Entry point
 *
 **************************************************************************/

int main(int argc, char *argv[])
{
    g_flDebugSet = -1;

    auto app = Gtk::Application::create(argc,
                                        argv,
                                        "org.baubadil.elisso");

    char szCWD[1024];
    auto strHome = getcwd(szCWD, sizeof(szCWD));
    ElissoApplicationWindow window(strHome);
    // Show the window and return when it's closed.
    return app->run(window);
}

