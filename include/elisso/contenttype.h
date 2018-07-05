/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
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

namespace Gdk { class PixbufFormat; };

class ContentTypeLock;

/**
 *  Representation of a Gio content type. Each such type has a name, description,
 *  and mime type, and can have a bunch of application infos associated with it.
 *
 *  The constructor is private. The public entry point is the static Guess() method,
 *  which returns the content type for a given file from an internal cached list.
 *
 *  We also throw the image type function into this class, which are implemented
 *  separately in Gtk+, but are related from our point of view.
 */
class ContentType
{
public:
    /**
     *  Returns the cached ContentType for the given file from the internal list.
     *  The list is initialized on the first call.
     *
     *  The type is determined exclusively based on the file name extension. If the
     *  extension is unknown, or if there is no extension, then nullptr is returned.
     */
    static const ContentType* Guess(PFSFile pFile);

    /**
     *  Returns the Gdk::PixbufFormat for the given file, or nullptr if it is
     *  not an image file.
     */
    static const Gdk::PixbufFormat* IsImageFile(PFSFile);

    /**
     *  Returns the description for this content type, which is what is displayed
     *  in the "type" column of a file details view.
     */
    const std::string& getDescription() const
    {
        return _strDescription;
    }

    /**
     *  Returns the AppInfo that has been configured with Gio as the default
     *  application for this content type.
     */
    PAppInfo getDefaultAppInfo() const;

    /**
     *  Returns all Gio::AppInfo instances that have configured with Gio as
     *  associated applications for this content type.
     */
    AppInfoList getAllAppInfos() const;

private:
    ContentType(const char *pcszName, const char *pcszDescription, const char *pcszMimeType);

    static void GetAll(ContentTypeLock &lock);

    std::string _strName,
                 _strDescription,
                _strMimeType;
};

#endif
