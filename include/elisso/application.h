/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_APPLICATION_H
#define ELISSO_APPLICATION_H

#include "elisso/elisso.h"

#include "elisso/fsmodel_gio.h"
#include "xwp/basetypes.h"

class ElissoApplication;
typedef Glib::RefPtr<ElissoApplication> PElissoApplication;

typedef Glib::RefPtr<Gio::Menu> PMenu;
typedef Glib::RefPtr<Gio::MenuItem> PMenuItem;


/***************************************************************************
 *
 *  ElissoApplication
 *
 **************************************************************************/

/**
 *  The Gtk::Application derivative which handles our GSettings instance, icon,
 *  main menu, command line.
 */
class ElissoApplication : public Gtk::Application
{
public:
    static PElissoApplication create(int argc,
                                     char *argv[]);

    PPixbuf getIcon();

    Glib::ustring getSettingsString(const std::string &strKey);

    int getSettingsInt(const std::string &strKey);

    void setSettingsString(const std::string &strKey,
                           const Glib::ustring &strData);

    PMenu addMenuSection(PMenu pMenu);
    PMenuItem addMenuItem(PMenu pMenu,
                          const Glib::ustring &strName,
                          const Glib::ustring &strAction,
                          const Glib::ustring &strAccelerator = "");

    /**
     *  Loads a default icon for the given file (or folder) from the default icon theme.
     */
    PPixbuf getDefaultIcon(PFSModelBase pFS, int size);

protected:
    ElissoApplication(int argc,
                      char *argv[]);

    /**
     *  Handler for the "startup" signal, which gets fired when the first instance of a
     *  Gtk::Application is being created. Subsequent invocations of a single-instance
     *  Gtk::Application will instead send additional "activate" or "open" signals
     *  to the first instance.
     *
     *  We use this to initialize the one and only instance, namely, the main menu.
     *  Doing that in the constructor doesn't work.
     */
    void on_startup() override;

    /**
     *  Handler for the "activate" signal, which gets sent whenever an instance
     *  of Gtk::Application is created. If a second instance of a single-instance
     *  Gtk::Application is created, this is sent to the first instance. "Activate"
     *  is emitted if an application is started without command line arguments; otherwise,
     *  since we have specified APPLICATION_HANDLES_OPEN, "open" gets emitted.
     *
     *  We create another elisso window with the user's home directory.
     */
    void on_activate() override;

    /**
     *
     *  Since we have specified APPLICATION_HANDLES_OPEN, "open" gets emitted on the
     *  primary instance if an application was started with file command line arguments.
     *  Otherwise (no arguments), "activate" gets emitted.
     *
     *  We open additional tabs with the given directories.
     */
    void on_open(const type_vec_files &files, const Glib::ustring &hint) override;

    PPixbuf                         _pIcon;
    Glib::RefPtr<Gio::Settings>     _pSettings;

    Glib::RefPtr<Gtk::IconTheme>    _pIconTheme;
};

#endif
