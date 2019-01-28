
/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_PREVIEWWINDOW_H
#define ELISSO_PREVIEWWINDOW_H

#include "elisso/elisso.h"
#include "elisso/fsmodel_gio.h"

class PreviewFile;
typedef shared_ptr<PreviewFile> PPreviewFile;
class ElissoFolderView;

class ElissoPreviewWindow : virtual public Gtk::Window
{
public:
    ElissoPreviewWindow();
    virtual ~ElissoPreviewWindow();

    /**
     *  Causes the preview pane to attempt to display the file. Returns true if
     *  the file type was determined to be previewable and the loader thread was
     *  started, but this does not mean yet that the file was displayed correctly.
     *
     *  Setting pFile to nullptr will hide the preview window.
     *
     *  pCurrentFolderView must always be set to the folder view sending the request,
     *  since the preview window is shared between all folder view tabs of an
     *  Elisso main window.
     */
    bool setFile(PFsGioFile pFile,
                 ElissoFolderView &currentFolderView);

    void fireNext();

    void firePrevious();

private:
    void onFileLoaded(PPreviewFile p);

    struct Impl;
    Impl *_pImpl;
};

#endif


