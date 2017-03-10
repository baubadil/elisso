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
 *  Menu
 *
 **************************************************************************/

//Layout the actions in a menubar and an application menu:
Glib::ustring ui_info =
R"i____(
<interface>
  <!-- menubar -->
  <menu id='menu-example'>
    <submenu>
      <attribute name='label' translatable='yes'>_File</attribute>
      <section>
        <item>
          <attribute name='label' translatable='yes'>_Quit</attribute>
          <attribute name='action'>win.file-quit</attribute>
          <attribute name='accel'>&lt;Primary&gt;q</attribute>
        </item>
      </section>
    </submenu>
    <submenu>
      <attribute name='label' translatable='yes'>_Edit</attribute>
      <section>
        <item>
          <attribute name='label' translatable='yes'>_Copy</attribute>
          <attribute name='action'>win.copy</attribute>
          <attribute name='accel'>&lt;Primary&gt;c</attribute>
        </item>
        <item>
          <attribute name='label' translatable='yes'>_Paste</attribute>
          <attribute name='action'>win.paste</attribute>
          <attribute name='accel'>&lt;Primary&gt;v</attribute>
        </item>
        <item>
          <attribute name='label' translatable='yes'>_Something</attribute>
          <attribute name='action'>win.something</attribute>
        </item>
      </section>
    </submenu>
    <submenu>
      <attribute name='label' translatable='yes'>_View</attribute>
      <section>
        <item>
          <attribute name='label' translatable='yes'>_Icons</attribute>
          <attribute name='action'>win.view-icons</attribute>
          <attribute name='accel'>&lt;Primary&gt;1</attribute>
        </item>
        <item>
          <attribute name='label' translatable='yes'>_List</attribute>
          <attribute name='action'>win.view-list</attribute>
          <attribute name='accel'>&lt;Primary&gt;2</attribute>
        </item>
        <item>
          <attribute name='label' translatable='yes'>_Compact</attribute>
          <attribute name='action'>win.view-compact</attribute>
          <attribute name='accel'>&lt;Primary&gt;3</attribute>
        </item>
      </section>
    </submenu>
    <submenu>
      <attribute name='label' translatable='yes'>_Go</attribute>
      <section>
        <item>
          <attribute name='label' translatable='yes'>_Parent</attribute>
          <attribute name='action'>win.go-parent</attribute>
          <attribute name='accel'>&lt;Alt&gt;Up</attribute>
        </item>
        <item>
          <attribute name='label' translatable='yes'>_Back</attribute>
          <attribute name='action'>win.go-back</attribute>
          <attribute name='accel'>&lt;Alt&gt;Left</attribute>
        </item>
        <item>
          <attribute name='label' translatable='yes'>_Forward</attribute>
          <attribute name='action'>win.go-forward</attribute>
          <attribute name='accel'>&lt;Alt&gt;Right</attribute>
        </item>
        <item>
          <attribute name='label' translatable='yes'>_Home</attribute>
          <attribute name='action'>win.go-home</attribute>
          <attribute name='accel'>&lt;Alt&gt;Home</attribute>
        </item>
      </section>
    </submenu>
    <submenu>
      <attribute name='label' translatable='yes'>_Help</attribute>
      <section>
        <item>
          <attribute name='label' translatable='yes'>_About</attribute>
          <attribute name='action'>win.about</attribute>
        </item>
      </section>
    </submenu>
  </menu>
</interface>)i____";


/***************************************************************************
 *
 *  ElissoApplication
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

    auto pBuilder = Gtk::Builder::create();
    try
    {
        pBuilder->add_from_string(ui_info);
    }
    catch(const Glib::Error& ex)
    {
        throw FSException("Building menus failed: " + ex.what());
    }

    //Get the menubar and the app menu, and add them to the application:
    auto pMenu = pBuilder->get_object("menu-example");
    this->set_menubar(Glib::RefPtr<Gio::Menu>::cast_dynamic(pMenu));
}

void ElissoApplication::on_activate()
{
    auto p = new ElissoApplicationWindow(*this, nullptr);
    this->add_window(*p);
    p->show();
}

PPixBuf ElissoApplication::getIcon()
{
    if (!_pIcon)
    {
        auto pIconTheme = Gtk::IconTheme::get_default();
        _pIcon = pIconTheme->load_icon("system-file-manager", 256);
    }

    return _pIcon;
}

Glib::ustring ElissoApplication::getSettingsString(const std::string &strKey)
{
    return _pSettings->get_string(strKey);
}

int ElissoApplication::getSettingsInt(const std::string &strKey)
{
    return _pSettings->get_int(strKey);
}

void ElissoApplication::setSettingsString(const std::string &strKey, const Glib::ustring &strData)
{
    _pSettings->set_string(strKey, strData);
}

/* static */
Glib::RefPtr<ElissoApplication> ElissoApplication::create(int argc,
                                                          char *argv[])
{
    return Glib::RefPtr<ElissoApplication>(new ElissoApplication(argc, argv));
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

