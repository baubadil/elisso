/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/previewpane.h"
#include "elisso/contenttype.h"
#include "elisso/thumbnailer.h"
#include "elisso/folderview.h"
#include "elisso/mainwindow.h"
#include "elisso/worker.h"

enum class PreviewState
{
    UNKNOWN,
    LOADING,
    SHOWING,
    ERROR
};

struct PreviewFile
{
    PFsGioFile              pFile;
    const Gdk::PixbufFormat *pFormat;
    PPixbuf                 pPixbufFullsize;
    string                  strError;

    PreviewFile(PFsGioFile pFile_,
                const Gdk::PixbufFormat *pFormat_)
        : pFile(pFile_),
          pFormat(pFormat_)
    { }
};
typedef shared_ptr<PreviewFile> PPreviewFile;

typedef WorkerResultQueue<PPreviewFile> PreviewWorker;
typedef shared_ptr<PreviewWorker> PPreviewWorker;

struct ElissoPreviewPane::Impl
{
    Gtk::Image              image;
    PreviewState            state = PreviewState::UNKNOWN;
    PPreviewWorker          pWorker;
    // Keep a copy of the last file given to setFile() so we
    // can avoid unnecessary work if the user runs through a lot
    // of files quickly.
    PFsGioFile              pCurrentFile;
};

ElissoPreviewPane::ElissoPreviewPane(ElissoFolderView &folderView)
    : _folderView(folderView),
      _pImpl(new Impl)
{
    this->add_events(Gdk::KEY_PRESS_MASK | Gdk::SCROLL_MASK);
    this->set_can_focus(true);

//     this->signal_key_press_event().connect([](GdkEventKey *key_event) -> bool {
//         Debug::Message("key press");
//         return true;
//     });

    this->add(_pImpl->image);
    _pImpl->image.show();

    _pImpl->pWorker = make_shared<PreviewWorker>();

    _pImpl->pWorker->connect([this](){
        PPreviewFile p = _pImpl->pWorker->fetchResult();
        if (!p->strError.empty())
            Debug::Message(p->strError);
        else if (p->pPixbufFullsize)
        {
            // Only do this if this file is actually the last one given
            // to setFile(). Otherwise another file is already in the queue.
            if (p->pFile == _pImpl->pCurrentFile)
            {
                size_t cxTarget = _pImpl->image.get_width();
                size_t cyTarget = _pImpl->image.get_height();

                auto ppbScaled = Thumbnailer::ScaleAndRotate(p->pPixbufFullsize,
                                                             cxTarget,
                                                             cyTarget);

                _pImpl->image.set(ppbScaled);
                grab_focus();

                _folderView.onPreviewReady(p->pFile);
            }
        }
    });
}

/* virtual */
ElissoPreviewPane::~ElissoPreviewPane()
{
    delete _pImpl;
}

bool
ElissoPreviewPane::setFile(PFsGioFile pFile)
{
    bool rc = false;
    if (pFile)
    {
        const Gdk::PixbufFormat *pFormat;
        if ((pFormat = ContentType::IsImageFile(pFile)))
        {
            PPreviewWorker pWorker = _pImpl->pWorker;

            PPreviewFile pInput = std::make_shared<PreviewFile>(pFile, pFormat);

            new std::thread([pWorker, pInput]()
            {
                try
                {
                    FileContents fc(*pInput->pFile);

                    auto pLoader = Gdk::PixbufLoader::create(pInput->pFormat->get_name());
                    if (pLoader)
                    {
                        pLoader->write((const guint8*)fc._pData, fc._size);       // can throw

                        pInput->pPixbufFullsize = pLoader->get_pixbuf();
                        pLoader->close();
                    }
                }
                catch (std::exception &e)
                {
                    pInput->strError = e.what();
                }

                pWorker->postResultToGui(pInput);
            });

            _pImpl->pCurrentFile = pFile;

            grab_focus();

            rc = true;
        }
    }

    return rc;
}

/* virtual */
bool
ElissoPreviewPane::on_button_press_event(GdkEventButton *pEvent) /* override */
{
    if (pEvent->type == GDK_BUTTON_PRESS)
    {
        Debug::Message("button press: " + to_string(pEvent->button));
        switch (pEvent->button)
        {
            case 1:
            // GTK+ routes mouse button 9 to the "forward" event.
            case 9:
                fireNext();
                return true;

            // GTK+ routes mouse button 8 to the "back" event.
            case 8:
                firePrevious();
                return true;
        }
    }

    return Gtk::EventBox::on_button_press_event(pEvent);
}

/* virtual */
bool
ElissoPreviewPane::on_scroll_event(GdkEventScroll *pEvent) /* override */
{
    switch (pEvent->direction)
    {
        case GDK_SCROLL_DOWN:
            fireNext();
            return true;

        case GDK_SCROLL_UP:
            firePrevious();
            return true;

        default: break;
    }

    return Gtk::EventBox::on_scroll_event(pEvent);

}

/* virtual */
bool
ElissoPreviewPane::on_key_press_event(GdkEventKey *pEvent) /* override */
{
    if (pEvent->type == GDK_KEY_PRESS)
    {
//         Debug::Message("key press down: " + to_string(pEvent->keyval));
        if (    (pEvent->keyval == GDK_KEY_space)
             && (((int)pEvent->state & ((int)GDK_SHIFT_MASK | (int)GDK_CONTROL_MASK | (int)GDK_MOD1_MASK)) == 0)
           )
        {
            fireNext();
        }
    }

    return Gtk::EventBox::on_key_press_event(pEvent);
}

void
ElissoPreviewPane::fireNext()
{
    _folderView.getApplicationWindow().activate_action(ACTION_EDIT_SELECT_NEXT_PREVIEWABLE);
}

void
ElissoPreviewPane::firePrevious()
{
    _folderView.getApplicationWindow().activate_action(ACTION_EDIT_SELECT_PREVIOUS_PREVIEWABLE);
}
