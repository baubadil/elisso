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

#ifndef __GTK_ICON_VIEW_PRIVATE_H__
#define __GTK_ICON_VIEW_PRIVATE_H__

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

typedef std::pair<GtkOrientation, gint> SizesMapKey;
struct ItemSize
{
    int minimum;;
    int natural;
};
typedef std::map<SizesMapKey, ItemSize> SizesMap;

struct XGtkIconViewItem
{
    GdkRectangle rectCell;      // x, width, y, height

    gint index;

    gint row, col;

    bool selected = false;
    bool selected_before_rubberbanding = false;

    GtkTreeIter iter;      // Only valid temporarily after _xiconview_build_iters() has been called.

    // Caches for item sizes.
//     int minimum = -1;
//     int natural = -1;
    SizesMap    mapSizesCache;

    XGtkIconViewItem()
    {
        rectCell.width = -1;
        rectCell.height = -1;
    }
};
typedef std::shared_ptr<XGtkIconViewItem> PXGtkIconViewItem;

/**
 *  The original IconView code simply did a complete re-layout whenever the smallest things changed
 *  in the model. For example, if the view had 10,000 items and a pixmap was changed for a single
 *  item in the model, all 10,000 items would be completely recalculated.
 *
 *  Instead, we now maintain some state about whether the layout is up-to-date with respect to the model.
 */
enum class LayoutState
{
    EMPTY,                  // Icon view has no model, or model is empty. Nothing to do.
    TOTALLY_DIRTY,          // Model is set and not empty, and we haven't done a layout at all. This is the whole thing as with the old code.
    SOMEWHAT_DIRTY,         // Model is set and not empty, and we did a layout before, but at least one item is out of date. For example,
                            // if an item in the model has changed, and its size needs to be recalculated, which might trigger a reflow.
    CLEAN                   // Model is set and not empty, and the layout is up to date.
};

struct XGtkIconViewPrivate
{
    GtkCellArea        *cell_area;
    GtkCellAreaContext *cell_area_context;

    gulong              add_editable_id;
    gulong              remove_editable_id;
    gulong              context_changed_id;

    LayoutState         layoutState = LayoutState::EMPTY;

    GPtrArray          *row_contexts;       // Array of CellAreaContext instances, one for each icon view ROW.

    gint                width,
                        height;

    GtkSelectionMode    selection_mode;

    GdkWindow           *bin_window;

    GList               *children;          // Container children, including editable cells. (Not sure if there are others.)

    GtkTreeModel        *model;

    // The list of items (cells) in the icon view. There is one item for each row in the model (which is a cell in the icon view).
    std::vector<PXGtkIconViewItem> vItems;

    GtkAdjustment       *hadjustment;
    GtkAdjustment       *vadjustment;

    gint rubberband_x1, rubberband_y1;
    gint rubberband_x2, rubberband_y2;
    GdkDevice *rubberband_device;
//   GtkCssNode *rubberband_node;

    guint               scroll_timeout_id;
    gint                scroll_value_diff;
    gint                event_last_x, event_last_y;

    PXGtkIconViewItem   anchor_item;
    PXGtkIconViewItem   cursor_item;

    PXGtkIconViewItem   last_single_clicked;
    PXGtkIconViewItem   last_prelight;

    GtkOrientation      item_orientation;

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


/* GObject vfuncs */
static void xiconview_cell_layout_init(GtkCellLayoutIface *iface);
static void xiconview_dispose(GObject *object);
static void xiconview_constructed(GObject *object);
static void xiconview_set_property(GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);
static void xiconview_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
/* GtkWidget vfuncs */
static void xiconview_destroy(GtkWidget *widget);
static void xiconview_realize(GtkWidget *widget);
static void xiconview_unrealize(GtkWidget *widget);
static GtkSizeRequestMode xiconview_get_request_mode(GtkWidget *widget);
static void xiconview_get_preferred_width(GtkWidget *widget, gint *minimum, gint *natural);
static void xiconview_get_preferred_width_for_height(GtkWidget *widget, gint height, gint *minimum, gint *natural);
static void xiconview_get_preferred_height(GtkWidget *widget, gint *minimum, gint *natural);
static void xiconview_get_preferred_height_for_width(GtkWidget *widget, gint width, gint *minimum, gint *natural);
static void xiconview_size_allocate(GtkWidget *widget, GtkAllocation *allocation);
static gboolean xiconview_draw(GtkWidget *widget, cairo_t *cr);
static gboolean xiconview_motion(GtkWidget *widget, GdkEventMotion *event);
static gboolean xiconview_leave(GtkWidget *widget, GdkEventCrossing *event);
static gboolean xiconview_button_press(GtkWidget *widget, GdkEventButton *event);
static gboolean xiconview_button_release(GtkWidget *widget, GdkEventButton *event);
static gboolean xiconview_key_press(GtkWidget *widget, GdkEventKey *event);
static gboolean xiconview_key_release(GtkWidget *widget, GdkEventKey *event);

/* GtkContainer vfuncs */
static void xiconview_remove(GtkContainer *container, GtkWidget *widget);
static void
xiconview_forall(GtkContainer *container, gboolean include_internals, GtkCallback callback, gpointer callback_data);

/* XGtkIconView vfuncs */
static void xiconview_real_select_all(XGtkIconView *icon_view);
static void xiconview_real_unselect_all(XGtkIconView *icon_view);
static void xiconview_real_select_cursor_item(XGtkIconView *icon_view);
static void xiconview_real_toggle_cursor_item(XGtkIconView *icon_view);
static gboolean xiconview_real_activate_cursor_item(XGtkIconView *icon_view);

/* Internal functions */
static void xiconview_set_hadjustment_values(XGtkIconView *icon_view);
static void xiconview_set_vadjustment_values(XGtkIconView *icon_view);
static void xiconview_set_hadjustment(XGtkIconView *icon_view, GtkAdjustment *adjustment);
static void xiconview_set_vadjustment(XGtkIconView *icon_view, GtkAdjustment *adjustment);
static void xiconview_adjustment_changed(GtkAdjustment *adjustment, XGtkIconView *icon_view);
static void xiconview_layout(XGtkIconView *icon_view);
static void xiconview_paint_item(XGtkIconView *icon_view,
                                 cairo_t *cr,
                                 PXGtkIconViewItem pItem,
                                 gint x,
                                 gint y,
                                 gboolean draw_focus);
static void xiconview_paint_rubberband(XGtkIconView *icon_view, cairo_t *cr);
static void xiconview_queue_draw_path(XGtkIconView *icon_view, GtkTreePath *path);
static void xiconview_queue_draw_item(XGtkIconView *icon_view, PXGtkIconViewItem pItem);
static void xiconview_start_rubberbanding(XGtkIconView *icon_view, GdkDevice *device, gint x, gint y);
static void xiconview_stop_rubberbanding(XGtkIconView *icon_view);
static void xiconview_update_rubberband_selection(XGtkIconView *icon_view);
static gboolean xiconview_item_hit_test(XGtkIconView *icon_view,
                                        PXGtkIconViewItem pItem,
                                        gint x,
                                        gint y,
                                        gint width,
                                        gint height);
static gboolean xiconview_unselect_all_internal(XGtkIconView *icon_view);
static void xiconview_update_rubberband(gpointer data);
static void xiconview_item_invalidate_size(PXGtkIconViewItem &pItem);
static void xiconview_invalidate_sizes(XGtkIconView *icon_view);
static void
xiconview_add_move_binding(GtkBindingSet *binding_set, guint keyval, guint modmask, GtkMovementStep step, gint count);
static gboolean xiconview_real_move_cursor(XGtkIconView *icon_view, GtkMovementStep step, gint count);
static void xiconview_move_cursor_up_down(XGtkIconView *icon_view, gint count);
static void xiconview_move_cursor_page_up_down(XGtkIconView *icon_view, gint count);
static void xiconview_move_cursor_left_right(XGtkIconView *icon_view, gint count);
static void xiconview_move_cursor_start_end(XGtkIconView *icon_view, gint count);
static void xiconview_scroll_to_item(XGtkIconView *icon_view, PXGtkIconViewItem pItem);
static gboolean
xiconview_select_all_between(XGtkIconView *icon_view, PXGtkIconViewItem anchor, PXGtkIconViewItem cursor);

static void xiconview_ensure_cell_area(XGtkIconView *icon_view, GtkCellArea *cell_area);

static GtkCellArea *xiconview_cell_layout_get_area(GtkCellLayout *layout);

static void xiconview_item_selected_changed(XGtkIconView *icon_view,
                                            PXGtkIconViewItem pItem);

static void xiconview_add_editable(GtkCellArea *area,
                                   GtkCellRenderer *renderer,
                                   GtkCellEditable *editable,
                                   GdkRectangle *cell_area,
                                   const gchar *path,
                                   XGtkIconView *icon_view);
static void xiconview_remove_editable(GtkCellArea *area,
                                      GtkCellRenderer *renderer,
                                      GtkCellEditable *editable,
                                      XGtkIconView *icon_view);
static void update_text_cell(XGtkIconView *icon_view);
static void update_pixbuf_cell(XGtkIconView *icon_view);

/* Source side drag signals */
static void xiconview_drag_begin(GtkWidget *widget, GdkDragContext *context);
static void xiconview_drag_end(GtkWidget *widget, GdkDragContext *context);
static void xiconview_drag_data_get(
    GtkWidget *widget, GdkDragContext *context, GtkSelectionData *selection_data, guint info, guint time);
static void xiconview_drag_data_delete(GtkWidget *widget, GdkDragContext *context);

/* Target side drag signals */
static void xiconview_drag_leave(GtkWidget *widget, GdkDragContext *context, guint time);
static gboolean xiconview_drag_motion(GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time);
static gboolean xiconview_drag_drop(GtkWidget *widget, GdkDragContext *context, gint x, gint y, guint time);
static void xiconview_drag_data_received(GtkWidget *widget,
                                         GdkDragContext *context,
                                         gint x,
                                         gint y,
                                         GtkSelectionData *selection_data,
                                         guint info,
                                         guint time);
static gboolean xiconview_maybe_begin_drag(XGtkIconView *icon_view, GdkEventMotion *event);

static void remove_scroll_timeout(XGtkIconView *icon_view);

/* GtkBuildable */
static GtkBuildableIface *parent_buildable_iface;
static void xiconview_buildable_init(GtkBuildableIface *iface);
// static gboolean xiconview_buildable_custom_tag_start (GtkBuildable
// *buildable,
//         GtkBuilder    *builder,
//         GObject       *child,
//         const gchar   *tagname,
//         GMarkupParser *parser,
//         gpointer      *data);
// static void     xiconview_buildable_custom_tag_end   (GtkBuildable
// *buildable,
//         GtkBuilder    *builder,
//         GObject       *child,
//         const gchar   *tagname,
//         gpointer      *data);

#endif /* __GTK_ICON_VIEW_PRIVATE_H__ */

