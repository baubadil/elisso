/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/previewwindow.h"

#include "elisso/previewpane.h"
#include "elisso/contenttype.h"
#include "elisso/thumbnailer.h"
#include "elisso/folderview.h"
#include "elisso/mainwindow.h"
#include "elisso/worker.h"

/***************************************************************************
 *
 *  Preview worker
 *
 **************************************************************************/

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

struct ElissoPreviewWindow::Impl
{
    ElissoPreviewPane       previewPane;
    Gtk::Image              image;
    PreviewState            state = PreviewState::UNKNOWN;
    PPreviewWorker          pWorker;
    // Keep a copy of the last file given to setFile() so we
    // can avoid unnecessary work if the user runs through a lot
    // of files quickly.
    PFsGioFile              pCurrentFile;
    ElissoFolderView        *pCurrentFolderView = nullptr;

    uint                    cFilesLoaded = 0;

    Impl(ElissoPreviewWindow &parent)
        : previewPane(parent)
    { };
};


/***************************************************************************
 *
 *  ElissoPreviewWindow implementation
 *
 **************************************************************************/

ElissoPreviewWindow::ElissoPreviewWindow()
    : _pImpl(new Impl(*this))
{
    Debug::Log(DEBUG_ALWAYS, __FUNCTION__);

    _pImpl->previewPane.add(_pImpl->image);
    this->add(_pImpl->previewPane);
    _pImpl->image.show();
    _pImpl->previewPane.show();

    _pImpl->pWorker = make_shared<PreviewWorker>();

    _pImpl->pWorker->connect([this]()
    {
        PPreviewFile p = _pImpl->pWorker->fetchResult();
        this->onFileLoaded(p);
    });
}

/* virtual */
ElissoPreviewWindow::~ElissoPreviewWindow()
{
    delete _pImpl;
}

bool ElissoPreviewWindow::setFile(PFsGioFile pFile,
                                  ElissoFolderView &currentFolderView)
{
    bool rc = false;

    _pImpl->pCurrentFolderView = &currentFolderView;

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

            this->set_title("Loading " + pFile->getBasename());

            if (!(_pImpl->cFilesLoaded++))
            {
                auto &main = currentFolderView.getApplicationWindow();

                int main_x, main_y;
                main.get_position(main_x, main_y);
                int x = 100,
                    width = 200,
                    y = main_y,
                    height = main.get_height();
                this->set_default_size(width, height);
                this->move(x, y);
            }

            this->show();
            grab_focus();

            rc = true;
        }
    }
    else
    {
        _pImpl->pCurrentFolderView = nullptr;
        this->hide();
    }

    return rc;
}

void
ElissoPreviewWindow::onFileLoaded(PPreviewFile p)
{
    if (!p->strError.empty())
        Debug::Message(p->strError);
    else if (p->pPixbufFullsize)
    {
        // Only do this if this file is actually the last one given
        // to setFile(). Otherwise another file is already in the queue.
        if (p->pFile == _pImpl->pCurrentFile)
        {
            this->set_title(p->pFile->getBasename());

            size_t cxTarget = _pImpl->image.get_width();
            size_t cyTarget = _pImpl->image.get_height();

            auto ppbScaled = Thumbnailer::ScaleAndRotate(p->pPixbufFullsize,
                                                         cxTarget,
                                                         cyTarget);

            _pImpl->image.set(ppbScaled);
            grab_focus();

            _pImpl->pCurrentFolderView->onPreviewReady(p->pFile);
        }
    }
}

void
ElissoPreviewWindow::fireNext()
{
    if (_pImpl->pCurrentFolderView)
        _pImpl->pCurrentFolderView->getApplicationWindow().activate_action(ACTION_EDIT_SELECT_NEXT_PREVIEWABLE);
}

void
ElissoPreviewWindow::firePrevious()
{
    if (_pImpl->pCurrentFolderView)
        _pImpl->pCurrentFolderView->getApplicationWindow().activate_action(ACTION_EDIT_SELECT_PREVIOUS_PREVIEWABLE);
}
