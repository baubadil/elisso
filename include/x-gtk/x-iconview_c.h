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

#ifndef _ELISSO_XICONVIEW_H_
#define _ELISSO_XICONVIEW_H_

#include <gtk/gtkcontainer.h>
#include <gtk/gtktreemodel.h>
#include <gtk/gtkcellrenderer.h>
#include <gtk/gtkcellarea.h>
#include <gtk/gtkselection.h>
#include <gtk/gtktooltip.h>

#define TYPE_XICONVIEW            (xiconview_get_type())
#define XICONVIEW(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_XICONVIEW, XGtkIconView))
#define XICONVIEW_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_XICONVIEW, XGtkIconViewClass))
#define IS_XICONVIEW(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_XICONVIEW))
#define IS_XICONVIEW_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_XICONVIEW))
#define XICONVIEW_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_XICONVIEW, XGtkIconViewClass))

struct XGtkIconView;
struct XGtkIconViewClass;
struct XGtkIconViewPrivate;

/**
 * XGtkIconViewForeachFunc:
 * @icon_view: a #XGtkIconView
 * @path: The #GtkTreePath of a selected row
 * @data: (closure): user data
 *
 * A function used by xiconview_selected_foreach() to map all
 * selected rows.  It will be called on every selected row in the view.
 */
typedef void (* XGtkIconViewForeachFunc)     (XGtkIconView      *icon_view,
        GtkTreePath      *path,
        gpointer          data);

struct XGtkIconView
{
    GtkContainer parent;

    /*< private >*/
    XGtkIconViewPrivate *priv;
};

struct XGtkIconViewClass
{
    GtkContainerClass parent_class;

    void    (* item_activated)         (XGtkIconView      *icon_view,
                                        GtkTreePath      *path);
    void    (* selection_changed)      (XGtkIconView      *icon_view);

    /* Key binding signals */
    void    (* select_all)             (XGtkIconView      *icon_view);
    void    (* unselect_all)           (XGtkIconView      *icon_view);
    void    (* select_cursor_item)     (XGtkIconView      *icon_view);
    void    (* toggle_cursor_item)     (XGtkIconView      *icon_view);
    gboolean (* move_cursor)           (XGtkIconView      *icon_view,
                                        GtkMovementStep   step,
                                        gint              count);
    gboolean (* activate_cursor_item)  (XGtkIconView      *icon_view);

    /* Padding for future expansion */
    void (*_gtk_reserved1) (void);
    void (*_gtk_reserved2) (void);
    void (*_gtk_reserved3) (void);
    void (*_gtk_reserved4) (void);
};

GDK_AVAILABLE_IN_ALL
GType          xiconview_get_type          (void) G_GNUC_CONST;
GDK_AVAILABLE_IN_ALL
GtkWidget *    xiconview_new               (void);
GDK_AVAILABLE_IN_ALL
GtkWidget *    xiconview_new_with_area     (GtkCellArea    *area);
GDK_AVAILABLE_IN_ALL
GtkWidget *    xiconview_new_with_model    (GtkTreeModel   *model);

GDK_AVAILABLE_IN_ALL
void           xiconview_set_model         (XGtkIconView    *icon_view,
        GtkTreeModel   *model);
GDK_AVAILABLE_IN_ALL
GtkTreeModel * xiconview_get_model         (XGtkIconView    *icon_view);
GDK_AVAILABLE_IN_ALL
void           xiconview_set_text_column   (XGtkIconView    *icon_view,
        gint            column);
GDK_AVAILABLE_IN_ALL
gint           xiconview_get_text_column   (XGtkIconView    *icon_view);
GDK_AVAILABLE_IN_ALL
void           xiconview_set_markup_column (XGtkIconView    *icon_view,
        gint            column);
GDK_AVAILABLE_IN_ALL
gint           xiconview_get_markup_column (XGtkIconView    *icon_view);
GDK_AVAILABLE_IN_ALL
void           xiconview_set_pixbuf_column (XGtkIconView    *icon_view,
        gint            column);
GDK_AVAILABLE_IN_ALL
gint           xiconview_get_pixbuf_column (XGtkIconView    *icon_view);

GDK_AVAILABLE_IN_ALL
void           xiconview_set_item_orientation (XGtkIconView    *icon_view,
        GtkOrientation  orientation);
GDK_AVAILABLE_IN_ALL
GtkOrientation xiconview_get_item_orientation (XGtkIconView    *icon_view);
GDK_AVAILABLE_IN_ALL
void           xiconview_set_columns       (XGtkIconView    *icon_view,
        gint            columns);
GDK_AVAILABLE_IN_ALL
gint           xiconview_get_columns       (XGtkIconView    *icon_view);
GDK_AVAILABLE_IN_ALL
void           xiconview_set_item_width    (XGtkIconView    *icon_view,
        gint            item_width);
GDK_AVAILABLE_IN_ALL
gint           xiconview_get_item_width    (XGtkIconView    *icon_view);
GDK_AVAILABLE_IN_ALL
void           xiconview_set_spacing       (XGtkIconView    *icon_view,
        gint            spacing);
GDK_AVAILABLE_IN_ALL
gint           xiconview_get_spacing       (XGtkIconView    *icon_view);
GDK_AVAILABLE_IN_ALL
void           xiconview_set_row_spacing   (XGtkIconView    *icon_view,
        gint            row_spacing);
GDK_AVAILABLE_IN_ALL
gint           xiconview_get_row_spacing   (XGtkIconView    *icon_view);
GDK_AVAILABLE_IN_ALL
void           xiconview_set_column_spacing (XGtkIconView    *icon_view,
        gint            column_spacing);
GDK_AVAILABLE_IN_ALL
gint           xiconview_get_column_spacing (XGtkIconView    *icon_view);
GDK_AVAILABLE_IN_ALL
void           xiconview_set_margin        (XGtkIconView    *icon_view,
        gint            margin);
GDK_AVAILABLE_IN_ALL
gint           xiconview_get_margin        (XGtkIconView    *icon_view);
GDK_AVAILABLE_IN_ALL
void           xiconview_set_item_padding  (XGtkIconView    *icon_view,
        gint            item_padding);
GDK_AVAILABLE_IN_ALL
gint           xiconview_get_item_padding  (XGtkIconView    *icon_view);

GDK_AVAILABLE_IN_ALL
GtkTreePath *  xiconview_get_path_at_pos   (XGtkIconView     *icon_view,
        gint             x,
        gint             y);
GDK_AVAILABLE_IN_ALL
gboolean       xiconview_get_item_at_pos   (XGtkIconView     *icon_view,
        gint              x,
        gint              y,
        GtkTreePath     **path,
        GtkCellRenderer **cell);
GDK_AVAILABLE_IN_ALL
gboolean       xiconview_get_visible_range (XGtkIconView      *icon_view,
        GtkTreePath     **start_path,
        GtkTreePath     **end_path);
GDK_AVAILABLE_IN_3_8
void           xiconview_set_activate_on_single_click (XGtkIconView  *icon_view,
        gboolean      single);
GDK_AVAILABLE_IN_3_8
gboolean       xiconview_get_activate_on_single_click (XGtkIconView  *icon_view);

GDK_AVAILABLE_IN_ALL
void           xiconview_selected_foreach   (XGtkIconView            *icon_view,
        XGtkIconViewForeachFunc  func,
        gpointer                data);
GDK_AVAILABLE_IN_ALL
void           xiconview_set_selection_mode (XGtkIconView            *icon_view,
        GtkSelectionMode        mode);
GDK_AVAILABLE_IN_ALL
GtkSelectionMode xiconview_get_selection_mode (XGtkIconView            *icon_view);
GDK_AVAILABLE_IN_ALL
void             xiconview_select_path        (XGtkIconView            *icon_view,
        GtkTreePath            *path);
GDK_AVAILABLE_IN_ALL
void             xiconview_unselect_path      (XGtkIconView            *icon_view,
        GtkTreePath            *path);
GDK_AVAILABLE_IN_ALL
gboolean         xiconview_path_is_selected   (XGtkIconView            *icon_view,
        GtkTreePath            *path);
GDK_AVAILABLE_IN_ALL
gint             xiconview_get_item_row       (XGtkIconView            *icon_view,
        GtkTreePath            *path);
GDK_AVAILABLE_IN_ALL
gint             xiconview_get_item_column    (XGtkIconView            *icon_view,
        GtkTreePath            *path);
GDK_AVAILABLE_IN_ALL
GList           *xiconview_get_selected_items (XGtkIconView            *icon_view);
GDK_AVAILABLE_IN_ALL
void             xiconview_select_all         (XGtkIconView            *icon_view);
GDK_AVAILABLE_IN_ALL
void             xiconview_unselect_all       (XGtkIconView            *icon_view);
GDK_AVAILABLE_IN_ALL
void             xiconview_item_activated     (XGtkIconView            *icon_view,
        GtkTreePath            *path);
GDK_AVAILABLE_IN_ALL
void             xiconview_set_cursor         (XGtkIconView            *icon_view,
        GtkTreePath            *path,
        GtkCellRenderer        *cell,
        gboolean                start_editing);
GDK_AVAILABLE_IN_ALL
gboolean         xiconview_get_cursor         (XGtkIconView            *icon_view,
        GtkTreePath           **path,
        GtkCellRenderer       **cell);
GDK_AVAILABLE_IN_ALL
void             xiconview_scroll_to_path     (XGtkIconView            *icon_view,
        GtkTreePath            *path,
        gboolean                use_align,
        gfloat                  row_align,
        gfloat                  col_align);

/* Drag-and-Drop support */
GDK_AVAILABLE_IN_ALL
void                   xiconview_enable_model_drag_source (XGtkIconView              *icon_view,
        GdkModifierType           start_button_mask,
        const GtkTargetEntry     *targets,
        gint                      n_targets,
        GdkDragAction             actions);
GDK_AVAILABLE_IN_ALL
void                   xiconview_enable_model_drag_dest   (XGtkIconView              *icon_view,
        const GtkTargetEntry     *targets,
        gint                      n_targets,
        GdkDragAction             actions);
GDK_AVAILABLE_IN_ALL
void                   xiconview_unset_model_drag_source  (XGtkIconView              *icon_view);
GDK_AVAILABLE_IN_ALL
void                   xiconview_unset_model_drag_dest    (XGtkIconView              *icon_view);
GDK_AVAILABLE_IN_ALL
void                   xiconview_set_reorderable          (XGtkIconView              *icon_view,
        gboolean                  reorderable);
GDK_AVAILABLE_IN_ALL
gboolean               xiconview_get_reorderable          (XGtkIconView              *icon_view);


/* These are useful to implement your own custom stuff. */
GDK_AVAILABLE_IN_ALL
void                   xiconview_set_drag_dest_item       (XGtkIconView              *icon_view,
        GtkTreePath              *path,
        GtkIconViewDropPosition   pos);
GDK_AVAILABLE_IN_ALL
void                   xiconview_get_drag_dest_item       (XGtkIconView              *icon_view,
        GtkTreePath             **path,
        GtkIconViewDropPosition  *pos);
GDK_AVAILABLE_IN_ALL
gboolean               xiconview_get_dest_item_at_pos     (XGtkIconView              *icon_view,
        gint                      drag_x,
        gint                      drag_y,
        GtkTreePath             **path,
        GtkIconViewDropPosition  *pos);
GDK_AVAILABLE_IN_ALL
cairo_surface_t       *xiconview_create_drag_icon         (XGtkIconView              *icon_view,
        GtkTreePath              *path);

GDK_AVAILABLE_IN_ALL
void    xiconview_convert_widget_to_bin_window_coords     (XGtkIconView *icon_view,
        gint         wx,
        gint         wy,
        gint        *bx,
        gint        *by);
GDK_AVAILABLE_IN_3_6
gboolean xiconview_get_cell_rect                          (XGtkIconView     *icon_view,
        GtkTreePath     *path,
        GtkCellRenderer *cell,
        GdkRectangle    *rect);


GDK_AVAILABLE_IN_ALL
void    xiconview_set_tooltip_item                        (XGtkIconView          *icon_view,
        GtkTooltip      *tooltip,
        GtkTreePath     *path);
GDK_AVAILABLE_IN_ALL
void    xiconview_set_tooltip_cell                        (XGtkIconView     *icon_view,
        GtkTooltip      *tooltip,
        GtkTreePath     *path,
        GtkCellRenderer *cell);
GDK_AVAILABLE_IN_ALL
gboolean xiconview_get_tooltip_context                    (XGtkIconView       *icon_view,
        gint              *x,
        gint              *y,
        gboolean           keyboard_tip,
        GtkTreeModel     **model,
        GtkTreePath      **path,
        GtkTreeIter       *iter);
GDK_AVAILABLE_IN_ALL
void     xiconview_set_tooltip_column                     (XGtkIconView       *icon_view,
        gint               column);
GDK_AVAILABLE_IN_ALL
gint     xiconview_get_tooltip_column                     (XGtkIconView       *icon_view);

#endif // _ELISSO_XICONVIEW_H_

