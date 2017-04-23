/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/contenttype.h"

#include <gtkmm.h>
#include "elisso/fsmodel.h"
#include "xwp/debug.h"
#include <atomic>

/***************************************************************************
 *
 *  Globals
 *
 **************************************************************************/

map<string, ContentType*> g_mapTypesByName;


/***************************************************************************
 *
 *  ContentType
 *
 **************************************************************************/

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
    PAppInfo pAppInfo = Glib::wrap(g_app_info_get_default_for_type(_strName.c_str(), FALSE));
    return pAppInfo;
}

/**
 *  Returns the ContentType for the given file.
 *
 *  The type is determined exclusively based on the file name extension. If the
 *  extension is unknown, or if there is no extension, then nullptr is returned.
 */
/* static */
const ContentType*
ContentType::Guess(PFSFile pFile)
{
    ContentType *pTypeReturn = nullptr;

    if (pFile)
    {
        GetAll();

        gboolean fResultUncertain = false;
        gchar *pGuess = g_content_type_guess(pFile->getRelativePath().c_str(),
                                             nullptr,
                                             0,
                                             &fResultUncertain);
        if (pGuess)
        {
            if (!fResultUncertain)
            {
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
void
ContentType::GetAll()
{
    static std::atomic<bool> s_fLoaded(false);

    if (!s_fLoaded)
    {
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

        s_fLoaded = true;
    }
}

