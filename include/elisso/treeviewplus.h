/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
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

class ElissoApplicationWindow;

enum class MouseButton3ClickType
{
    TREE_ITEM_SELECTED = 1,         // MB3 click on row in tree view on the left.
    SINGLE_ROW_SELECTED,            // MB3 click on a single row in contents. This could also be the row that was automatically selected after the click.
    MULTIPLE_ROWS_SELECTED,         // MB3 click on one row in contents that is selected, but there is at least one other row that is selected.
    WHITESPACE,                     // MB3 click on contents whitespace (not on a row).
};

enum class TreeViewPlusMode
{
    UNKNOWN,
    IS_FOLDER_TREE_LEFT,
    IS_FOLDER_CONTENTS_RIGHT
};

/**
 *  TreeViewPlus is our TreeView override to be able to handle button clicks for
 *  popup menus. This is used as a child for both the tree view on the left
 *  (ElissoFolderTree) and the tree view of the folder contents on the right
 *  (ElissoFolderView).
 */
class TreeViewPlus : public Gtk::TreeView
{
public:
    void setParent(ElissoApplicationWindow &mainWindow, TreeViewPlusMode mode)
    {
        this->_pMainWindow = &mainWindow;
        this->_mode = mode;
    }

protected:
    bool on_button_press_event(GdkEventButton* button_event) override;

private:
    TreeViewPlusMode _mode = TreeViewPlusMode::UNKNOWN;
    ElissoApplicationWindow *_pMainWindow = NULL;
};

#endif // ELISSO_TREEVIEWPLUS_H
