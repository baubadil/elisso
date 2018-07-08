/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#define DEF_STRING_IMPLEMENTATION

#include "elisso/application.h"

#include "elisso/mainwindow.h"

#include "xwp/except.h"
#include "xwp/exec.h"

#include <malloc.h>


Glib::ustring implode(const std::string &strGlue, const std::vector<Glib::ustring> v)
{
    Glib::ustring str;
    for (const auto &s : v)
        if (!s.empty())
        {
            if (!str.empty())
                str += strGlue + s;
            else
                str += s;
        }

    return str;
}

/***************************************************************************
 *
 *  Public ElissoApplication methods
 *
 **************************************************************************/

struct ElissoApplication::Impl
{
    PPixbuf                         pIcon;
    Glib::RefPtr<Gio::Settings>     pSettings;
    Glib::RefPtr<Gtk::IconTheme>    pIconTheme;

    Impl()
        : pIconTheme(Gtk::IconTheme::get_default())
    {}
};

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
PPixbuf
ElissoApplication::getIcon()
{
    if (!_pImpl->pIcon)
        _pImpl->pIcon = _pImpl->pIconTheme->load_icon(ICON_FILE_MANAGER, 256);

    return _pImpl->pIcon;
}

/**
 *  Returns a settings string from the application's GSettings.
 */
Glib::ustring
ElissoApplication::getSettingsString(const std::string &strKey)
{
    return _pImpl->pSettings->get_string(strKey);
}

/**
 *  Returns a settings integer from the application's GSettings.
 */
int
ElissoApplication::getSettingsInt(const std::string &strKey)
{
    return _pImpl->pSettings->get_int(strKey);
}

/**
 *  Writes a settings string into the application's GSettings.
 */
void ElissoApplication::setSettingsString(const std::string &strKey, const Glib::ustring &strData)
{
    _pImpl->pSettings->set_string(strKey, strData);
}

PMenu
ElissoApplication::addMenuSection(PMenu pMenu)
{
    auto pSection = Gio::Menu::create();
    pMenu->append_section(pSection);
    return pSection;
}

PMenuItem
ElissoApplication::addMenuItem(PMenu pMenu,
                               const Glib::ustring &strName,
                               const Glib::ustring &strAction,          //!< in: will be prefixed with "win."
                               const Glib::ustring &strAccelerator /* = ""*/ )
{
    Glib::ustring strActionLong = "win." + strAction;
    auto pMenuItem = Gio::MenuItem::create(strName, strActionLong);
    pMenu->append_item(pMenuItem);
    if (!strAccelerator.empty())
    {
        std::vector<Glib::ustring> sv = { strAccelerator };
        this->set_accels_for_action(strActionLong, sv);
    }

    return pMenuItem;
}

void
ForEachUString(const Glib::ustring &str,
               const Glib::ustring &strDelimiter,
               std::function<void (const Glib::ustring&)> fnParticle)
{
    size_t p1 = 0;
    size_t p2;
    while ((p2 = str.find(strDelimiter, p1)) != string::npos)
    {
        int len = p2 - p1;
        if (len > 0)
            fnParticle(str.substr(p1, len));
        p1 = p2 + 1;
    }

    fnParticle(str.substr(p1));
}

PPixbuf
ElissoApplication::getStockIcon(const string &strName,
                                int size)
{
    PPixbuf p;

//     static std::map<string, Glib::RefPtr<Gdk::Pixbuf>> mapStockIconsCache;
//     if (!STL_EXISTS(mapSharedFolderIcons, size))
//         // first call for this size:
//         mapSharedFolderIcons[size] = getApplication().getFileTypeIcon(*pFS, size);
//
//     pReturn = mapSharedFolderIcons[size];
    p = _pImpl->pIconTheme->load_icon(strName, size, Gtk::IconLookupFlags::ICON_LOOKUP_FORCE_SIZE);

    return p;
}

PPixbuf
ElissoApplication::getFileTypeIcon(FsObject &fs,
                                   int size)
{
    PPixbuf p;

    Glib::ustring strIcons;
    try
    {
        auto pIcon = g_pFsGioImpl->getGioFile(fs)->query_info()->get_icon();
        strIcons = pIcon->to_string();
    }
    catch (Gio::Error &e) { }

    // The following doesn't work, we need to parse the string returned from the file.
    // p = _pIconTheme->load_icon(strIcons, size, Gtk::IconLookupFlags::ICON_LOOKUP_FORCE_SIZE);

    if (!strIcons.empty())
    {
        std::vector<Glib::ustring> sv;
        ForEachUString( strIcons,
                        " ",
                        [&sv](const Glib::ustring &strParticle)
                        {
                            if (!strParticle.empty())
                                sv.push_back(strParticle);

                        });

        if (sv.size())
        {
            Gtk::IconInfo i = _pImpl->pIconTheme->choose_icon(sv, size, Gtk::IconLookupFlags::ICON_LOOKUP_FORCE_SIZE);
            if (i)
                p = i.load_icon();
        }
    }

    if (!p)
        p = getStockIcon(ICON_FILE_GENERIC, size);

    return p;
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
                         Gio::APPLICATION_HANDLES_OPEN),
        _pImpl(new Impl)
{
    /*
     * Settings instance
     */
    // This is a bit of an elaborate setup to load our settings schema without
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

    _pImpl->pSettings = Glib::wrap(pSettings_c);
}

/* virtual */
ElissoApplication::~ElissoApplication()
{
    delete _pImpl;
}

void
ElissoApplication::on_startup() /* override */
{
    Debug d(CMD_TOP, __func__);

    Gtk::Application::on_startup();

    auto pMenuBar = Gio::Menu::create();

    auto pSubmenu = Gio::Menu::create();
    pMenuBar->append_submenu("_File", pSubmenu);
    auto pSubSection = addMenuSection(pSubmenu);
    addMenuItem(pSubSection, "New _tab", ACTION_FILE_NEW_TAB, "<Primary>t");
    addMenuItem(pSubSection, "New _window", ACTION_FILE_NEW_WINDOW, "<Primary>n");
    addMenuItem(pSubSection, "Open current folder in ter_minal", ACTION_FILE_OPEN_IN_TERMINAL, "<Primary><Shift>m");

    pSubSection = addMenuSection(pSubmenu);
    addMenuItem(pSubSection, "Create new folder", ACTION_FILE_CREATE_FOLDER, "<Primary><Shift>n");
    addMenuItem(pSubSection, "Create empty document", ACTION_FILE_CREATE_DOCUMENT);

    pSubSection = addMenuSection(pSubmenu);
    addMenuItem(pSubSection, "Current folder properties", ACTION_FILE_PROPERTIES);

    pSubSection = addMenuSection(pSubmenu);
    addMenuItem(pSubSection, "_Quit", ACTION_FILE_QUIT, "<Primary>q");
    addMenuItem(pSubSection, "Close current tab", ACTION_FILE_CLOSE_TAB, "<Primary>w");

    pSubmenu = Gio::Menu::create();
    pMenuBar->append_submenu("_Edit", pSubmenu);
    pSubSection = addMenuSection(pSubmenu);
    addMenuItem(pSubSection, "_Copy", ACTION_EDIT_COPY, "<Primary>c");
    addMenuItem(pSubSection, "Cu_t", ACTION_EDIT_CUT, "<Primary>x");
    addMenuItem(pSubSection, "_Paste", ACTION_EDIT_PASTE, "<Primary>v");
    pSubSection = addMenuSection(pSubmenu);
    addMenuItem(pSubSection, "Select _all", ACTION_EDIT_SELECT_ALL, "<Primary>a");
    addMenuItem(pSubSection, "Select _next file for preview", ACTION_EDIT_SELECT_NEXT_PREVIEWABLE);
    addMenuItem(pSubSection, "Select pre_vious file for preview", ACTION_EDIT_SELECT_PREVIOUS_PREVIEWABLE);
    pSubSection = addMenuSection(pSubmenu);
    addMenuItem(pSubSection, "_Open selected", ACTION_EDIT_OPEN_SELECTED);
    addMenuItem(pSubSection, "Open selected in new ta_b", ACTION_EDIT_OPEN_SELECTED_IN_TAB);
    addMenuItem(pSubSection, "Open selected in ter_minal", ACTION_EDIT_OPEN_SELECTED_IN_TERMINAL);
    pSubSection = addMenuSection(pSubmenu);
    addMenuItem(pSubSection, "_Rename selected", ACTION_EDIT_RENAME, "F2");
    addMenuItem(pSubSection, "Tras_h selected", ACTION_EDIT_TRASH);

    pSubmenu = Gio::Menu::create();
    pMenuBar->append_submenu("_View", pSubmenu);
    pSubSection = addMenuSection(pSubmenu);
    addMenuItem(pSubSection, "Next tab", ACTION_VIEW_NEXT_TAB, "<Primary>Page_Down");
    addMenuItem(pSubSection, "Previous tab", ACTION_VIEW_PREVIOUS_TAB, "<Primary>Page_Up");
    pSubSection = addMenuSection(pSubmenu);
    addMenuItem(pSubSection, "Icons", ACTION_VIEW_ICONS, "<Primary>1");
    addMenuItem(pSubSection, "List", ACTION_VIEW_LIST, "<Primary>2");
    addMenuItem(pSubSection, "Compact", ACTION_VIEW_COMPACT, "<Primary>3");
    pSubSection = addMenuSection(pSubmenu);
    addMenuItem(pSubSection, "Show _preview pane", ACTION_VIEW_SHOW_PREVIEW);
    pSubSection = addMenuSection(pSubmenu);
    addMenuItem(pSubSection, "Refresh", ACTION_VIEW_REFRESH, "<Primary>r");

    pSubmenu = Gio::Menu::create();
    pMenuBar->append_submenu("_Go", pSubmenu);
    pSubSection = addMenuSection(pSubmenu);
    addMenuItem(pSubSection, "Parent", ACTION_GO_PARENT, "<Alt>Up");
    addMenuItem(pSubSection, "Back", ACTION_GO_BACK, "<Alt>Left");
    addMenuItem(pSubSection, "Forward", ACTION_GO_FORWARD, "<Alt>Right");
    pSubSection = addMenuSection(pSubmenu);
    addMenuItem(pSubSection, "Home", ACTION_GO_HOME, "<Alt>Home");
    addMenuItem(pSubSection, "Computer", ACTION_GO_COMPUTER);
    addMenuItem(pSubSection, "Trash", ACTION_GO_TRASH);

    pSubmenu = Gio::Menu::create();
    pMenuBar->append_submenu("_Help", pSubmenu);
    addMenuItem(pSubmenu, "About", ACTION_ABOUT);

    this->set_menubar(pMenuBar);
}

void
ElissoApplication::on_activate() /* override */
{
    Debug d(CMD_TOP, __func__);
    auto p = new ElissoApplicationWindow(*this);
    p->addFolderTab(FsObject::GetHome());
    this->add_window(*p);
    p->show();
}

void
ElissoApplication::on_open(const type_vec_files &files,
                           const Glib::ustring &hint) /* override */
{
    Debug d(CMD_TOP, __func__);

    ElissoApplicationWindow *pWindow = new ElissoApplicationWindow(*this);
    pWindow->present();

    for (auto &pFile : files)
    {
        std::string strPath = pFile->get_path();

        try
        {
            Debug::Log(CMD_TOP, std::string(__FUNCTION__) + ": handling " + strPath);
            auto pDir = FsObject::FindPath(strPath);
            FSTypeResolved t;
            if (!pDir->isDirectoryOrSymlinkToDirectory(t))
                throw FSException(quote(strPath) + " is not a directory");
            pWindow->addFolderTab(pDir);
        }
        catch (std::exception &e)
        {
            Debug::Log(CMD_TOP, std::string(__FUNCTION__) + ": error " + e.what());
            std::string strError = e.what();
            pWindow->addFolderTab(strError);
        }
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
    g_flDebugSet =  0
                  | CMD_TOP
                  | FOLDER_POPULATE_HIGH
//                   | FOLDER_POPULATE_LOW
//                   | FSEXCEPTION
//                   | FOLDER_INSERT
//                   | FILE_LOW
//                   | FILE_MID
//                   | FILE_HIGH
//                   | THUMBNAILER
                  | XICONVIEW
//                   | WINDOWHIERARCHY
//                   | FILEMONITORS
//                   | FOLDER_STACK
//                   | CLIPBOARD
//                   | PROGRESSDIALOG
//                   | TREEMODEL
//                    | MOUNTS
                  ;

    mallopt(M_ARENA_MAX, 2);

    FsGioImpl::Init();

    auto app = ElissoApplication::create(argc,
                                         argv);
    int rc = 0;
    try
    {
        rc = app->run();
    }
    catch (exception &e)
    {
        Gtk::MessageDialog dlg("elisso: unhandled exception",
                               false /* use_markup */,
                               Gtk::MESSAGE_ERROR,
                               Gtk::BUTTONS_CLOSE);
        dlg.set_secondary_text(e.what());
        dlg.run();
    }

    return rc;
}
