/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/treeviewplus.h"

#include "elisso/elisso.h"
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
        if (pEvent->type == GDK_BUTTON_PRESS)
        {
            Debug::Log(DEBUG_ALWAYS, "button " + to_string(pEvent->button) + " pressed");

            switch (pEvent->button)
            {
                case 1:
                case 3:
                {
                    auto pSel = this->get_selection();
                    MouseButton3ClickType clickType = MouseButton3ClickType::WHITESPACE;
                    // Figure out if the click was on a row or whitespace, and which row if any.
                    // There are two variants for this call -- but the more verbose one with a column returns
                    // a column always even if the user clicks on the whitespace to the right of the column
                    // so there's no point.
                    Gtk::TreeModel::Path path;
                    if (get_path_at_pos((int)pEvent->x, (int)pEvent->y, path))
                    {
                        if (pEvent->button == 3)
                        {
                            // Click on a row: with mouse button 3, figure out if it's selected.
                            if (pSel->is_selected(path))
                            {
                                // Click on row that's selected: then show context even if it's whitespace.
                                Debug::Log(DEBUG_ALWAYS, "row is selected");
                                if (pSel->count_selected_rows() == 1)
                                    clickType = MouseButton3ClickType::SINGLE_ROW_SELECTED;
                                else
                                    clickType = MouseButton3ClickType::MULTIPLE_ROWS_SELECTED;
                            }
                            else
                            {
                                Debug::Log(DEBUG_ALWAYS, "row is NOT selected");
                                if (!is_blank_at_pos((int)pEvent->x, (int)pEvent->y))
                                {
                                    pSel->unselect_all();
                                    pSel->select(path);
                                    clickType = MouseButton3ClickType::SINGLE_ROW_SELECTED;
                                }
                            }
                        }
                    }

                    // On right-click, open a pop-up menu.
                    if (pEvent->button == 3)
                    {
                        this->_pView->onMouseButton3Pressed(pEvent, clickType);
                        return true;        // do not propagate
                    }
                }
                break;

                // GTK+ routes mouse button 8 to the "back" event.
                case 8:
                    this->getToplevelWindow().activate_action(ACTION_GO_BACK);
                break;

                // GTK+ routes mouse button 9 to the "forward" event.
                case 9:
                    this->getToplevelWindow().activate_action(ACTION_GO_FORWARD);
                break;

                default:
                break;
            }
        }

    return Gtk::TreeView::on_button_press_event(pEvent);
}

Gtk::ApplicationWindow&
TreeViewPlus::getToplevelWindow()
{
    Gtk::ApplicationWindow *pWin = nullptr;
    auto p = this->get_toplevel();
    if ((p) && (p->get_is_toplevel()))
        pWin = static_cast<Gtk::ApplicationWindow*>(p);

    if (!pWin)
        throw FSException("Cannot find toplevel application window");

    return *pWin;
}
