/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/contenttype.h"

#include <gtkmm.h>
#include "elisso/fsmodel_gio.h"
#include "xwp/debug.h"
#include "xwp/stringhelp.h"
#include "xwp/thread.h"


/***************************************************************************
 *
 *  Globals
 *
 **************************************************************************/

// Mutex protecting all the below.
Mutex                               g_mtxContentTypes;
bool                                g_fAllLoaded = false;

// Our list of ContentType instances.
map<string, ContentType*>           g_mapTypesByName;

// List of formats supported by GTK's PixbufLoader.
std::vector<Gdk::PixbufFormat>      g_vFormats;

// Map of upper-cased file extensions with pointers into vFormats.
map<string, Gdk::PixbufFormat*>     g_mapFormats;


class ContentTypeLock : virtual Lock
{
public:
    ContentTypeLock()
        : Lock(g_mtxContentTypes)
    { }
};


/***************************************************************************
 *
 *  ContentType
 *
 **************************************************************************/

/* static */
const ContentType*
ContentType::Guess(PFSFile pFile)
{
    ContentType *pTypeReturn = nullptr;

    if (pFile)
    {
        gboolean fResultUncertain = false;
        gchar *pGuess = g_content_type_guess(pFile->getPath().c_str(),
                                             nullptr,
                                             0,
                                             &fResultUncertain);
        if (pGuess)
        {
            if (!fResultUncertain)
            {
                // Initialize the system.
                ContentTypeLock lock;
                GetAll(lock);

                auto it = g_mapTypesByName.find(pGuess);
                if (it != g_mapTypesByName.end())
                    pTypeReturn = it->second;
            }

            g_free(pGuess);
        }
    }

    return pTypeReturn;
}

/* static */
const Gdk::PixbufFormat*
ContentType::IsImageFile(PFSFile pFile)
{
    const string &strBasename = pFile->getBasename();
    string strExtension = strToUpper(getExtensionString(strBasename));

    ContentTypeLock lock;
    GetAll(lock);

    auto it = g_mapFormats.find(strExtension);
    if (it != g_mapFormats.end())
        return it->second;

    return nullptr;
}

ContentType::ContentType(const char *pcszName, const char *pcszDescription, const char *pcszMimeType)
    : _strName(pcszName),
      _strDescription(pcszDescription),
      _strMimeType(pcszMimeType)
{
    g_mapTypesByName[_strName] = this;
}

PAppInfo
ContentType::getDefaultAppInfo() const
{
    PAppInfo pAppInfo = Gio::AppInfo::get_default_for_type(_strName, false);
    return pAppInfo;
}

AppInfoList
ContentType::getAllAppInfos() const
{
    AppInfoList pList = Gio::AppInfo::get_all_for_type(_strName);
    return pList;
}

/* static */
void
ContentType::GetAll(ContentTypeLock &lock)
{
    if (!g_fAllLoaded)
    {
        // Build the map of supported image formats, sorted by extension in upper case.
        g_vFormats = Gdk::Pixbuf::get_formats();
        for (auto &fmt : g_vFormats)
            for (const auto &ext : fmt.get_extensions())
                g_mapFormats[strToUpper(ext)] = &fmt;

        GList *pList = g_content_types_get_registered();

        for (auto p = pList; p != NULL; p = p->next)
        {
            gchar *pcszName = (char*)p->data;
            gchar *pcszDescription = g_content_type_get_description((char*)p->data);
            gchar *pcszMimeType = g_content_type_get_mime_type((char*)p->data);

            // This stores itself in the global map.
            new ContentType(pcszName, pcszDescription, pcszMimeType);

            g_free(pcszDescription);
            g_free(pcszMimeType);
        }

        g_list_free_full(pList, g_free);

        g_fAllLoaded = true;
    }
}

