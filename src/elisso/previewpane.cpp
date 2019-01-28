/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/previewpane.h"

#include "elisso/previewwindow.h"

ElissoPreviewPane::ElissoPreviewPane(ElissoPreviewWindow &parent_)
    : parent(parent_)
{
    this->add_events(Gdk::KEY_PRESS_MASK | Gdk::SCROLL_MASK);
    this->set_can_focus(true);
}

/* virtual */
bool
ElissoPreviewPane::on_button_press_event(GdkEventButton *pEvent) /* override */
{
    if (pEvent->type == GDK_BUTTON_PRESS)
    {
        Debug::Message("button press: " + to_string(pEvent->button));
        switch (pEvent->button)
        {
            case 1:
            // GTK+ routes mouse button 9 to the "forward" event.
            case 9:
                parent.fireNext();
                return true;

            // GTK+ routes mouse button 8 to the "back" event.
            case 8:
                parent.firePrevious();
                return true;
        }
    }

    return Gtk::EventBox::on_button_press_event(pEvent);
}

/* virtual */
bool
ElissoPreviewPane::on_scroll_event(GdkEventScroll *pEvent) /* override */
{
    switch (pEvent->direction)
    {
        case GDK_SCROLL_DOWN:
            parent.fireNext();
            return true;

        case GDK_SCROLL_UP:
            parent.firePrevious();
            return true;

        default: break;
    }

    return Gtk::EventBox::on_scroll_event(pEvent);

}

/* virtual */
bool
ElissoPreviewPane::on_key_press_event(GdkEventKey *pEvent) /* override */
{
    if (pEvent->type == GDK_KEY_PRESS)
    {
//         Debug::Message("key press down: " + to_string(pEvent->keyval));
        if (    (pEvent->keyval == GDK_KEY_space)
             && (((int)pEvent->state & ((int)GDK_SHIFT_MASK | (int)GDK_CONTROL_MASK | (int)GDK_MOD1_MASK)) == 0)
           )
        {
            parent.fireNext();
        }
    }

    return Gtk::EventBox::on_key_press_event(pEvent);
}

