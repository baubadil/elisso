/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_TEXTENTRYDIALOG_H
#define ELISSO_TEXTENTRYDIALOG_H

#include <gtkmm.h>

class ElissoFolderView;

/***************************************************************************
 *
 *  TextEntryDialog
 *
 **************************************************************************/

/**
 *  Small dialog with the title and introductory label as given to the constructor
 *  and an entry field below.
 *
 *  This also has two buttons in the Dialog's action area: one with "Cancel" and
 *  the value Gtk::RESPONSE_CANCEL, and one with the given button string and the
 *  valud Gtk::RESPONSE_OK.
 *
 *  You can create an instance of this on the stack, invoke run() on it, and
 *  if the response is Gtk::RESPONSE_OK, retrieve the text with getText().
 */
class TextEntryDialog : public Gtk::Dialog
{
public:
    TextEntryDialog(Gtk::Window &wParent,
                    const Glib::ustring &strTitle,
                    const Glib::ustring &strIntro,
                    const Glib::ustring &strButton);

    void setText(Glib::ustring);

    Glib::ustring getText();

    /**
     *  Calls select_region on the Gtk::Entry. The characters that are selected are those characters at positions from
     *  start_pos up to, but not including end_pos. If end_pos is negative, then the characters selected are those
     *  characters from start_pos to the end of the text. Note that positions are specified in characters, not bytes.
     */
    void selectRegion(int start_pos, int end_pos);

private:
    void enableButtons();

    Gtk::Label _label;
    Gtk::Entry _entry;
    Gtk::Button *_pCreateButton = nullptr;
};

#endif // ELISSO_TEXTENTRYDIALOG_H

