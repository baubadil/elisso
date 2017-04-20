/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/textentrydialog.h"

#include "xwp/debug.h"


/***************************************************************************
 *
 *  TextEntryDialog
 *
 **************************************************************************/

TextEntryDialog::TextEntryDialog(Gtk::Window &wParent,
                                 const Glib::ustring &strTitle,
                                 const Glib::ustring &strIntro,
                                 const Glib::ustring &strButton)
    : Gtk::Dialog(strTitle,
                  Gtk::DialogFlags::DIALOG_MODAL | Gtk::DialogFlags::DIALOG_USE_HEADER_BAR)
{
    set_border_width(5);

    _label.set_line_wrap(true);
    _label.set_max_width_chars(100);
    _label.set_markup(strIntro);

    _entry.set_activates_default(true);
    auto pBuffer = _entry.get_buffer();
    pBuffer->signal_inserted_text().connect([this](guint, const gchar*, guint){
        this->enableButtons();
    });
    pBuffer->signal_deleted_text().connect([this](guint, guint){
        this->enableButtons();
    });

    auto pBox = get_content_area();
    pBox->pack_start(_label, Gtk::PackOptions::PACK_SHRINK, 5);
    pBox->pack_start(_entry, Gtk::PackOptions::PACK_SHRINK, 5);

    add_button("Cancel", Gtk::RESPONSE_CANCEL);
    _pCreateButton = add_button(strButton, Gtk::RESPONSE_OK);
    set_default_response(Gtk::RESPONSE_OK);

    enableButtons();

    set_transient_for(wParent);

    show_all();
}

Glib::ustring
TextEntryDialog::getText()
{
    return _entry.get_buffer()->get_text();
}

void
TextEntryDialog::enableButtons()
{
    bool fEnable = (_entry.get_buffer()->get_length() > 0);
    this->_pCreateButton->set_sensitive(fEnable);
}
