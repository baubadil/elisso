/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#define DEF_STRING_IMPLEMENTATION

#include "elisso/elisso.h"
#include "elisso/application.h"
#include "elisso/mainwindow.h"

#include "xwp/except.h"
#include "xwp/exec.h"


/***************************************************************************
 *
 *  Public ElissoApplication methods
 *
 **************************************************************************/

/**
 *  Public factory to create a refptr to a new ElissoApplication. The constructor is
 *  protected.
 */
/* static */
PElissoApplication
ElissoApplication::create(int argc,
                          char *argv[])
{
    return PElissoApplication(new ElissoApplication(argc, argv));
}

/**
 *  Returns the application icon. Fetches and caches it on the first call.
 */
PPixBuf ElissoApplication::getIcon()
{
    if (!_pIcon)
    {
        auto pIconTheme = Gtk::IconTheme::get_default();
        _pIcon = pIconTheme->load_icon("system-file-manager", 256);
    }

    return _pIcon;
}

/**
 *  Returns a settings string from the application's GSettings.
 */
Glib::ustring ElissoApplication::getSettingsString(const std::string &strKey)
{
    return _pSettings->get_string(strKey);
}

/**
 *  Returns a settings integer from the application's GSettings.
 */
int ElissoApplication::getSettingsInt(const std::string &strKey)
{
    return _pSettings->get_int(strKey);
}

/**
 *  Writes a settings string into the application's GSettings.
 */
void ElissoApplication::setSettingsString(const std::string &strKey, const Glib::ustring &strData)
{
    _pSettings->set_string(strKey, strData);
}


/***************************************************************************
 *
 *  Protected ElissoApplication methods
 *
 **************************************************************************/

ElissoApplication::ElissoApplication(int argc,
                                     char *argv[])
    :   Gtk::Application(argc,
                         argv,
                         "org.baubadil.elisso")
{
    // This is a bit of an elabore setup to load our settings schema without
    // having to install it as root under /usr/share/. It is complicated by
    // the fact that we have to use the raw C API since gtkmm has no complete
    // bindings for SettingsSchemaSource, it seems.
    std::string strExecutable = getExecutableFileName(argv[0]);
    // /home/ufm/src/elisso/out/linux.amd64/debug/stage/bin/elisso
    std::string strParentDir = getDirnameString(strExecutable);
    std::string strGrandParentDir = getDirnameString(strParentDir);

    auto pSource_c = g_settings_schema_source_new_from_directory((strGrandParentDir + "/share").c_str(),
                                                                 NULL,
                                                                 false,
                                                                 NULL);
    Glib::RefPtr<Gio::SettingsSchemaSource> pSource = Glib::wrap(pSource_c);

    auto pSchema_c = g_settings_schema_source_lookup(pSource_c, this->get_id().c_str(), false);
    GSettings *pSettings_c = g_settings_new_full(pSchema_c,
                                                 NULL,      // default backend
                                                 NULL);     // default path

    _pSettings = Glib::wrap(pSettings_c);

}

void ElissoApplication::on_startup()
{
    Gtk::Application::on_startup();

    auto pMenuBar = Gio::Menu::create();

    auto pMenuFile = Gio::Menu::create();
    pMenuBar->append_submenu("_File", pMenuFile);
    addMenuItem(pMenuFile, "New _tab", ACTION_FILE_NEW_TAB, "<Primary>t");
    addMenuItem(pMenuFile, "New _window", ACTION_FILE_NEW_WINDOW, "<Primary>n");
    addMenuItem(pMenuFile, "_Quit", ACTION_FILE_QUIT, "<Primary>q");
    addMenuItem(pMenuFile, "Close tab", ACTION_FILE_CLOSE_TAB, "<Primary>w");

    auto pMenuEdit = Gio::Menu::create();
    pMenuBar->append_submenu("_Edit", pMenuEdit);
    addMenuItem(pMenuEdit, "_Copy", ACTION_EDIT_COPY, "<Primary>c");
    addMenuItem(pMenuEdit, "Cu_t", ACTION_EDIT_CUT, "<Primary>x");
    addMenuItem(pMenuEdit, "_Paste", ACTION_EDIT_PASTE, "<Primary>v");

    auto pMenuView = Gio::Menu::create();
    pMenuBar->append_submenu("_View", pMenuView);
    addMenuItem(pMenuView, "Icons", ACTION_VIEW_ICONS, "<Primary>1");
    addMenuItem(pMenuView, "List", ACTION_VIEW_LIST, "<Primary>2");
    addMenuItem(pMenuView, "Compact", ACTION_VIEW_COMPACT, "<Primary>3");
    addMenuItem(pMenuView, "Refresh", ACTION_VIEW_REFRESH, "<Primary>r");

    auto pMenuGo = Gio::Menu::create();
    pMenuBar->append_submenu("_Go", pMenuGo);
    addMenuItem(pMenuGo, "Parent", ACTION_GO_PARENT, "<Alt>Up");
    addMenuItem(pMenuGo, "Back", ACTION_GO_BACK, "<Alt>Left");
    addMenuItem(pMenuGo, "Forward", ACTION_GO_FORWARD, "<Alt>Right");
    addMenuItem(pMenuGo, "Home", ACTION_GO_HOME, "<Alt>Home");

    auto pMenuHelp = Gio::Menu::create();
    pMenuBar->append_submenu("_Help", pMenuHelp);
    addMenuItem(pMenuHelp, "About", ACTION_ABOUT);

    this->set_menubar(pMenuBar);
}

void ElissoApplication::on_activate()
{
    auto p = new ElissoApplicationWindow(*this, nullptr);
    this->add_window(*p);
    p->show();
}

void
ElissoApplication::addMenuItem(Glib::RefPtr<Gio::Menu> pMenu,
                               const Glib::ustring &strName,
                               const Glib::ustring &strAction,          //!< in: will be prefixed with "win."
                               const Glib::ustring &strAccelerator /* = ""*/ )
{
    Glib::ustring strActionLong = "win." + strAction;
    auto pMenuItem = Gio::MenuItem::create(strName, strActionLong);
    pMenu->append_item(pMenuItem);
    if (strAccelerator.length())
    {
        std::vector<Glib::ustring> sv = { strAccelerator };
        this->set_accels_for_action(strActionLong, sv);
    }
}


/***************************************************************************
 *
 *  Entry point
 *
 **************************************************************************/

int main(int argc, char *argv[])
{
    g_flDebugSet = FOLDER_POPULATE;

    auto app = ElissoApplication::create(argc,
                                         argv);
    return app->run();
}

