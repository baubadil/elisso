/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_TREEVIEWPLUS_H
#define ELISSO_TREEVIEWPLUS_H

#include <gtkmm.h>

class ElissoFolderView;

/***************************************************************************
 *
 *  TreeViewPlus
 *
 **************************************************************************/

enum class MouseButton3ClickType
{
    SINGLE_ROW_SELECTED = 1,        // MB3 click on a single row. This could also be the row that was automatically selected after the click.
    MULTIPLE_ROWS_SELECTED,         // MB3 click on one row that is selected, but there is at least one other row that is selected.
    WHITESPACE                      // MB3 click on tree view whitespace (not on a row).
};

class TreeViewPlus : public Gtk::TreeView
{
public:
    void setParent(ElissoFolderView &view)
    {
        this->_pView = &view;
    }

protected:
    bool on_button_press_event(GdkEventButton* button_event) override;

private:
    ElissoFolderView *_pView = NULL;
};

#endif // ELISSO_TREEVIEWPLUS_H
