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

#include "xwp/except.h"
#include "xwp/exec.h"

#include "elisso/application.h"
#include "elisso/mainwindow.h"


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
PPixBuf
ElissoApplication::getIcon()
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
Glib::ustring
ElissoApplication::getSettingsString(const std::string &strKey)
{
    return _pSettings->get_string(strKey);
}

/**
 *  Returns a settings integer from the application's GSettings.
 */
int
ElissoApplication::getSettingsInt(const std::string &strKey)
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

Glib::RefPtr<Gio::Menu>
ElissoApplication::addMenuSection(Glib::RefPtr<Gio::Menu> pMenu)
{
    auto pSection = Gio::Menu::create();
    pMenu->append_section(pSection);
    return pSection;
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
 *  Protected ElissoApplication methods
 *
 **************************************************************************/

ElissoApplication::ElissoApplication(int argc,
                                     char *argv[])
    :   Gtk::Application(argc,
                         argv,
                         "org.baubadil.elisso",
                         Gio::APPLICATION_HANDLES_OPEN)
{
    // This is a bit of an elabore setup to load our settings schema without
    // having to install it as root under /usr/share/. It is complicated by
    // the fact that we have to use the raw C API since gtkmm has no complete
    // bindings for SettingsSchemaSource, it seems.
    std::string strExecutable = getExecutableFileName(argv[0]);
    // $(HOME)/src/elisso/out/linux.amd64/debug/stage/bin/elisso
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

/**
 *  Handler for the "startup" signal, which gets fired when the first instance of a
 *  Gtk::Application is being created. Subsequent invocations of a single-instance
 *  Gtk::Application will instead send additional "activate" or "open" signals
 *  to the first instance.
 */
void
ElissoApplication::on_startup() /* override */
{
    Debug::Log(DEBUG_ALWAYS, __FUNCTION__);

    Gtk::Application::on_startup();

    auto pMenuBar = Gio::Menu::create();

    auto pMenuFile = Gio::Menu::create();
    pMenuBar->append_submenu("_File", pMenuFile);
    auto pFileSection1 = addMenuSection(pMenuFile);
    addMenuItem(pFileSection1, "New _tab", ACTION_FILE_NEW_TAB, "<Primary>t");
    addMenuItem(pFileSection1, "New _window", ACTION_FILE_NEW_WINDOW, "<Primary>n");
    auto pFileSection2 = addMenuSection(pMenuFile);
    addMenuItem(pFileSection2, "_Quit", ACTION_FILE_QUIT, "<Primary>q");
    addMenuItem(pFileSection2, "Close tab", ACTION_FILE_CLOSE_TAB, "<Primary>w");

    auto pMenuEdit = Gio::Menu::create();
    pMenuBar->append_submenu("_Edit", pMenuEdit);
    auto pEditSection1 = addMenuSection(pMenuEdit);
    addMenuItem(pEditSection1, "_Copy", ACTION_EDIT_COPY, "<Primary>c");
    addMenuItem(pEditSection1, "Cu_t", ACTION_EDIT_CUT, "<Primary>x");
    addMenuItem(pEditSection1, "_Paste", ACTION_EDIT_PASTE, "<Primary>v");
    auto pEditSection2 = addMenuSection(pMenuEdit);
    addMenuItem(pEditSection2, "_Open", ACTION_EDIT_OPEN);
    addMenuItem(pEditSection2, "Open in new ta_b", ACTION_EDIT_OPEN_IN_TAB);
    addMenuItem(pEditSection2, "Open in _terminal", ACTION_EDIT_OPEN_IN_TERMINAL);

    auto pMenuView = Gio::Menu::create();
    pMenuBar->append_submenu("_View", pMenuView);
    auto pViewSection = addMenuSection(pMenuView);
    addMenuItem(pViewSection, "Icons", ACTION_VIEW_ICONS, "<Primary>1");
    addMenuItem(pViewSection, "List", ACTION_VIEW_LIST, "<Primary>2");
    addMenuItem(pViewSection, "Compact", ACTION_VIEW_COMPACT, "<Primary>3");
    auto pViewSection2 = addMenuSection(pMenuView);
    addMenuItem(pViewSection2, "Refresh", ACTION_VIEW_REFRESH, "<Primary>r");

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

/**
 *  Handler for the "activate" signal, which gets sent whenever an instance
 *  of Gtk::Application is created. If a second instance of a single-instance
 *  Gtk::Application is created, this is sent to the first instance.
 *
 *  "Activate" is emitted if an application is started without command line arguments.
 *  Otherwise (since we have specified APPLICATION_HANDLES_OPEN), "open" gets emitted.
 */
void
ElissoApplication::on_activate() /* override */
{
    Debug::Log(DEBUG_ALWAYS, __FUNCTION__);
    auto p = new ElissoApplicationWindow(*this, nullptr);
    this->add_window(*p);
    p->show();
}

/**
 *
 *  Since we have specified APPLICATION_HANDLES_OPEN, "open" gets emitted on the
 *  primary instance if an application was started with file command line arguments.
 *  Otherwise (no arguments), "activate" gets emitted.
 */
void
ElissoApplication::on_open(const type_vec_files &files,
                           const Glib::ustring &hint) /* override */
{
    Debug::Log(DEBUG_ALWAYS, __FUNCTION__);

    ElissoApplicationWindow *pWindow = nullptr;
    for (auto &pFile : files)
    {
        std::string strPath = pFile->get_path();
        PFSModelBase pFSBase = FSModelBase::FindPath(strPath);
        if (!pFSBase)
            throw FSException("Command-line argument \"" + strPath + "\" is not a file");

        Debug::Log(DEBUG_ALWAYS, std::string(__FUNCTION__) + ": handling " + strPath);

        if (!pWindow)
            // first file: new window
            pWindow = new ElissoApplicationWindow(*this, pFSBase);
        else
        {
            // additional tabs in existing window
            Glib::signal_idle().connect_once([pWindow, pFSBase]()
            {
                pWindow->addFolderTab(pFSBase);
            });
        }
    }

    if (pWindow)
    {
        this->add_window(*pWindow);
        pWindow->show();
    }
}


/***************************************************************************
 *
 *  Entry point
 *
 **************************************************************************/

int
main(int argc, char *argv[])
{
    g_flDebugSet = FOLDER_POPULATE_HIGH;

    auto app = ElissoApplication::create(argc,
                                         argv);
    return app->run();
}

