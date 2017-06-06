/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/treeviewplus.h"

#include "elisso/folderview.h"

#include "xwp/except.h"


/***************************************************************************
 *
 *  TreeViewPlus
 *
 **************************************************************************/

/**
 *  Override the mouse-button-pressed signal handler. This is to provide the following
 *  features:
 *
 *   -- We want to show popup menus.
 *
 *   -- We want to emulate the selection behavior of the custom Nautilus/Nemo folder views,
 *      which is a bit different from that of the GTK TreeView:
 *
 *       -- With Nautilus/Nemo, if the user right-clicks on a row and the row is selected, a
 *          popup menu opens with actions that apply to all selected rows. (If the user clicks
 *          on a row that is NOT selected however, everything is deselected and the selection
 *          switches to that row.
 *
 *       -- With Nautilus/Nemo, clicking on whitespace with MB1 deselects everything.
 *
 *       -- With Nautilus/Nemo, clicking on whitespace with MB3 also deselects everything; the
 *          popup window then applies to the folder being displayed.
 */
bool
TreeViewPlus::on_button_press_event(GdkEventButton *pEvent) /* override */
{
    if (this->_pView)
        if (this->_pView->onButtonPressedEvent(pEvent))
            return true;        // handled, do not propagate

    return Gtk::TreeView::on_button_press_event(pEvent);
}

