/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_PREVIEWPANE_H
#define ELISSO_PREVIEWPANE_H

#include "elisso/elisso.h"
#include "elisso/fsmodel_gio.h"

class ElissoPreviewWindow;

class ElissoPreviewPane : virtual public Gtk::EventBox
{
public:
    ElissoPreviewPane(ElissoPreviewWindow &parent_);

    /**
     *  We return true to stop other handlers from being invoked for the event, or
     *  false to propagate the event further.
     */
    virtual bool on_button_press_event(GdkEventButton *pEvent) override;

    virtual bool on_scroll_event(GdkEventScroll *pEvent) override;

    virtual bool on_key_press_event(GdkEventKey *pEvent) override;

private:
    ElissoPreviewWindow &parent;

};

#endif


