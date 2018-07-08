/* gtkiconview.h
 * Copyright (C) 2002, 2004  Anders Carlsson <andersca@gnome.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __XGTK_ICON_VIEW_PRIVATE_H__
#define __XGTK_ICON_VIEW_PRIVATE_H__

#include "x-gtk/x-iconview_c.h"

#include <list>
#include <memory>

// #include "gtk/gtkcssnodeprivate.h"

#define P_(a) (a)
#define I_(a) (a)
#define GTK_PARAM_READABLE G_PARAM_READABLE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB
#define GTK_PARAM_WRITABLE G_PARAM_WRITABLE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB
#define GTK_PARAM_READWRITE G_PARAM_READWRITE|G_PARAM_STATIC_NAME|G_PARAM_STATIC_NICK|G_PARAM_STATIC_BLURB

extern void _gtk_marshal_BOOLEAN__VOID (GClosure     *closure,
                                        GValue       *return_value,
                                        guint         n_param_values,
                                        const GValue *param_values,
                                        gpointer      invocation_hint,
                                        gpointer      marshal_data);
extern void _gtk_marshal_BOOLEAN__ENUM_INT (GClosure     *closure,
        GValue       *return_value,
        guint         n_param_values,
        const GValue *param_values,
        gpointer      invocation_hint,
        gpointer      marshal_data);

struct XGtkIconViewItem
{
    GdkRectangle cell_area;

    gint index;

    gint row, col;

    bool selected = false;
    bool selected_before_rubberbanding = false;

    XGtkIconViewItem()
    {
        cell_area.width = -1;
        cell_area.height = -1;
    }
};
typedef std::shared_ptr<XGtkIconViewItem> PXGtkIconViewItem;

struct XGtkIconViewPrivate
{
    GtkCellArea        *cell_area;
    GtkCellAreaContext *cell_area_context;

    gulong              add_editable_id;
    gulong              remove_editable_id;
    gulong              context_changed_id;

    GPtrArray          *row_contexts;       /* Array of CellAreaContext instances, one for each icon view (not model). */

    gint width, height;

    GtkSelectionMode selection_mode;

    GdkWindow *bin_window;

    GList *children;

    GtkTreeModel *model;

    // GList *items;
    std::vector<PXGtkIconViewItem> vItems;

    GtkAdjustment *hadjustment;
    GtkAdjustment *vadjustment;

    gint rubberband_x1, rubberband_y1;
    gint rubberband_x2, rubberband_y2;
    GdkDevice *rubberband_device;
//   GtkCssNode *rubberband_node;

    guint scroll_timeout_id;
    gint scroll_value_diff;
    gint event_last_x, event_last_y;

    PXGtkIconViewItem anchor_item;
    PXGtkIconViewItem cursor_item;

    PXGtkIconViewItem last_single_clicked;
    PXGtkIconViewItem last_prelight;

    GtkOrientation item_orientation;

    gint columns;
    gint item_width;
    gint spacing;
    gint row_spacing;
    gint column_spacing;
    gint margin;
    gint item_padding;

    gint text_column;
    gint markup_column;
    gint pixbuf_column;
    gint tooltip_column;

    GtkCellRenderer *pixbuf_cell;
    GtkCellRenderer *text_cell;

    /* Drag-and-drop. */
    GdkModifierType start_button_mask;
    guint pressed_button;
    gint press_start_x;
    gint press_start_y;

    GdkDragAction source_actions;
    GdkDragAction dest_actions;

    GtkTreeRowReference *dest_item;
    GtkIconViewDropPosition dest_pos;

    /* scroll to */
    GtkTreeRowReference *scroll_to_path;
    gfloat scroll_to_row_align;
    gfloat scroll_to_col_align;
    guint scroll_to_use_align : 1;

    guint source_set : 1;
    guint dest_set : 1;
    guint reorderable : 1;
    guint empty_view_drop :1;
    guint activate_on_single_click : 1;

    guint modify_selection_pressed : 1;
    guint extend_selection_pressed : 1;

    guint draw_focus : 1;

    /* GtkScrollablePolicy needs to be checked when
     * driving the scrollable adjustment values */
    guint hscroll_policy : 1;
    guint vscroll_policy : 1;

    guint doing_rubberband : 1;
};

void _xiconview_set_cell_data(XGtkIconView *icon_view,
                              PXGtkIconViewItem &pItem);
void _xiconview_set_cursor_item(XGtkIconView *icon_view,
                                PXGtkIconViewItem &pItem,
                                GtkCellRenderer *cursor_cell);
PXGtkIconViewItem _xiconview_get_item_at_coords(XGtkIconView *icon_view,
                                                    gint x,
                                                    gint y,
                                                    gboolean                only_in_cell,
                                                    GtkCellRenderer       **cell_at_pos);
void                 _xiconview_select_item(XGtkIconView *icon_view,
                                            PXGtkIconViewItem &pItem);
void                 _xiconview_unselect_item(XGtkIconView *icon_view,
                                              PXGtkIconViewItem &pItem);

void gtk_adjustment_animate_to_value (GtkAdjustment *adjustment,
                                      gdouble        value);


#endif /* __XGTK_ICON_VIEW_PRIVATE_H__ */

