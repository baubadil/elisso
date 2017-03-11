/*
 * elisso -- PHP documentation tool. (C) 2015--2016 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_APPLICATION_H
#define ELISSO_APPLICATION_H

#include <gtkmm.h>

#include "xwp/basetypes.h"

typedef Glib::RefPtr<Gdk::Pixbuf> PPixBuf;

/***************************************************************************
 *
 *  ElissoApplication
 *
 **************************************************************************/

class ElissoApplication : public Gtk::Application
{
public:
    static Glib::RefPtr<ElissoApplication> create(int argc,
                                                  char *argv[]);

    PPixBuf getIcon();

    Glib::ustring getSettingsString(const std::string &strKey);
    int getSettingsInt(const std::string &strKey);

    void setSettingsString(const std::string &strKey,
                           const Glib::ustring &strData);

protected:
    ElissoApplication(int argc,
                      char *argv[]);

    void on_startup();

    void on_activate();

    void addMenuItem(Glib::RefPtr<Gio::Menu> pMenu,
                     const Glib::ustring &strName,
                     const Glib::ustring &strAction,
                     const Glib::ustring &strAccelerator = "");

    PPixBuf                     _pIcon;
    Glib::RefPtr<Gio::Settings> _pSettings;
};

#endif
