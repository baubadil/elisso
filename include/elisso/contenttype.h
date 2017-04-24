/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_CONTENTTYPE_H
#define ELISSO_CONTENTTYPE_H

#include <gtkmm.h>
#include <memory>

class FSFile;
typedef std::shared_ptr<FSFile> PFSFile;

typedef Glib::RefPtr<Gio::AppInfo> PAppInfo;
typedef std::list<PAppInfo> AppInfoList;

class ContentType
{
public:
    static const ContentType* Guess(PFSFile pFile);

    const std::string& getDescription() const
    {
        return _strDescription;
    }

    PAppInfo getDefaultAppInfo() const;
    AppInfoList getAllAppInfos() const;

private:
    ContentType(const char *pcszName, const char *pcszDescription, const char *pcszMimeType);

    static void GetAll();

    std::string _strName,
                 _strDescription,
                _strMimeType;
};

#endif
