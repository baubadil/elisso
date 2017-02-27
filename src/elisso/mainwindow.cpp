/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/mainwindow.h"

ElissoApplicationWindow::ElissoApplicationWindow(const Glib::ustring &strInitialPath)
    : _folderView()
{
    this->set_border_width(10);
    this->set_default_size(1000, 600);
    this->add(_folderView);
    _folderView.show();

    _folderView.setPath(strInitialPath);
}

/* virtual */
ElissoApplicationWindow::~ElissoApplicationWindow()
{
}
