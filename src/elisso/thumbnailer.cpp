/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/thumbnailer.h"

#include "elisso/worker.h"
#include "elisso/application.h"
#include "elisso/contenttype.h"
#include "xwp/debug.h"
#include "xwp/stringhelp.h"
#include "xwp/except.h"
#include <ctime>
#include <ratio>
#include <cstdio>
#include <cstdlib>
#include <vector>

/**
 *  Simple structure to temporarily hold the complete binary contents
 *  of a file. The constructor reads them from disk via fopen().
 */
struct FileContents
{
    FileContents(PGioFile pFile)
         : _pData(nullptr), _size(0)
    {
        try
        {
            auto pStream = pFile->read();
            Glib::RefPtr<Gio::FileInfo> pInfo = pStream->query_info(G_FILE_ATTRIBUTE_STANDARD_SIZE);
            _size = pInfo->get_attribute_uint64(G_FILE_ATTRIBUTE_STANDARD_SIZE);

            if (!(_pData = (char*)malloc(_size)))
                throw FSException("Not enough memory");
            gsize zRead;
            pStream->read_all(_pData, _size, zRead);
            pStream->close();
        }
        catch (Gio::Error &e)
        {
            throw FSException(e.what());
        }
    }

    ~FileContents()
    {
        if (_pData)
            free(_pData);
    }

    char *_pData;
    size_t _size;
};
typedef std::shared_ptr<FileContents> PFileContents;

struct ThumbnailTemp
{
    PThumbnail      pThumb;
    PFileContents   pFileContents;
    PPixbuf         ppbOrig;

    ThumbnailTemp(PThumbnail &pThumb_, PFileContents &pFileContents_)
        : pThumb(pThumb_),
          pFileContents(pFileContents_)
    { }

    ~ThumbnailTemp()
    { }

    void setLoaded(PPixbuf p)
    {
        // Release the memory of the loaded file.
        pFileContents = nullptr;
        ppbOrig = p;
    }
};

typedef std::shared_ptr<ThumbnailTemp> PThumbnailTemp;



/***************************************************************************
 *
 *  Thumbnailer::Impl (private)
 *
 **************************************************************************/

/**
 *  Our private Impl structure, anonymously declared in the Thumbnailer class.
 *
 *  This combines four things:
 *
 *   -- It contains six ThreadQueue instantiations, for the six threads that
 *      we are running here. Each of these has its own mutex, std::dequeue and
 *      condition variable.
 *
 *   -- It derives from WorkerResult so we get a Glib dispatcher to be able to
 *      post the final PThumbnail back to the GUI that called enqueue(). This
 *      has its own mutex, a std::dequeue and a Glib::Dispatcher.
 *
 *  When external caller (probably on the GUI thread) post an FSFile to
 *  be thumbnailed, it gets added to qFileReader; the file reader thread then
 *  reads the image file from disk. It then posts the full-size pixbuf to
 *  both scaler thread queues, which wake up and scale the pixbuf to two
 *  resolutions. When both results are ready, the WorkerResult is posted
 *  so that the GUI thread receives the result.
 */
struct Thumbnailer::Impl : WorkerResultQueue<PThumbnail>
{
    std::vector<std::thread*>           aThreads;
    WorkerInputQueue<PThumbnail>        qFileReader_;

    unsigned                            cPixbufLoaders;
    WorkerInputQueue<PThumbnailTemp>    *paqPixbufLoaders;

    WorkerInputQueue<PThumbnailTemp>    qScalerIconSmall;
    WorkerInputQueue<PThumbnailTemp>    qScalerIconBig;

    Impl()
    {
        // This returns 8 on a four-core machine with 8 hyperthreads.
        // 3 pixbuf threads are good fit for that, so scale accordingly.
        unsigned int cHyperThreads = XWP::Thread::getHardwareConcurrency();
        cPixbufLoaders = MAX(1, (cHyperThreads / 2 - 1));
        // Would love to have a vector here to make this easier but WorkerInputQueue is not copyable.
        paqPixbufLoaders = new WorkerInputQueue<PThumbnailTemp>[cPixbufLoaders];

        Debug::Log(THUMBNAILER, "Thumbnailer: std::thread::hardware_concurrency=" + to_string(cHyperThreads) + " => " + to_string(cPixbufLoaders) + " JPEG threads");
    }

    ~Impl()
    {
        delete[] paqPixbufLoaders;
    }
};


/***************************************************************************
 *
 *  Thumbnailer
 *
 **************************************************************************/

Thumbnailer::Thumbnailer(ElissoApplication &app)
    : _pImpl(new Impl),
      _app(app)
{
    Debug::Log(THUMBNAILER, "Thumbnailer constructed");

     // Create the file reader thread.
    _pImpl->aThreads.push_back(XWP::Thread::Create([this]()
    {
        this->fileReaderThread();
    }, false));

    // Create the pixmap loader threads.
    for (uint u = 0;
         u < _pImpl->cPixbufLoaders;
         ++u)
    {
        _pImpl->aThreads.push_back(XWP::Thread::Create([this, u]()
        {
            this->pixbufLoaderThread(u);
        }, false));
    }

    // Create the pixbuf scaler thread.
    _pImpl->aThreads.push_back(XWP::Thread::Create([this]()
    {
        this->scalerSmallThread();
    }, false));

    // Create the pixbuf scaler thread.
    _pImpl->aThreads.push_back(XWP::Thread::Create([this]()
    {
        this->scalerBigThread();
    }, false));
}

Thumbnailer::~Thumbnailer()
{
    Debug::Message("~Thumbnailer");
    this->clearQueues();

    // Stop the threads by posting nullptr to each queue.
    _pImpl->qFileReader_.post(nullptr);
    std::vector<WorkerInputQueue<PThumbnailTemp>*> vQueues;
    for (uint u = 0;  u < _pImpl->cPixbufLoaders;  ++u)
        _pImpl->paqPixbufLoaders[u].post(nullptr);
    _pImpl->qScalerIconSmall.post(nullptr);
    _pImpl->qScalerIconBig.post(nullptr);

    int i = 0;
    for (auto pThread : _pImpl->aThreads)
    {
        Debug::Log(DEBUG_ALWAYS, "stopping thread " + to_string(i++));
        pThread->join();
    }

    delete _pImpl;
}

sigc::connection
Thumbnailer::connect(std::function<void ()> fn)
{
    return _pImpl->connect(fn);
}

void
Thumbnailer::enqueue(PFsGioFile pFile)
{
    Debug::Log(THUMBNAILER, string(__func__) + ":  " + pFile->getBasename());
    _pImpl->qFileReader_.post(make_shared<Thumbnail>(pFile));
}

PThumbnail
Thumbnailer::fetchResult()
{
    return _pImpl->fetchResult();
}

bool
Thumbnailer::isBusy()
{
    if (_pImpl->qFileReader_.size() > 0)
        return true;
    std::vector<WorkerInputQueue<PThumbnailTemp>*> vQueues;
    for (uint u = 0;
         u < _pImpl->cPixbufLoaders;
         ++u)
        if (_pImpl->paqPixbufLoaders[u].size() > 0)
            return true;
    if (_pImpl->qScalerIconBig.size() > 0)
        return true;

    return false;
}

void
Thumbnailer::clearQueues()
{
    // For each of the queues, we need to
    // 1) reset the FSFlag::THUMBNAILING for each of the files still left
    //    in the queue, or else enqueue will reject them if the folder is
    //    selected again;
    for (auto &pThumb : _pImpl->qFileReader_.deq)
        pThumb->pFile->clearFlag(FSFlag::THUMBNAILING);
    // 2) actually clear the queue.
    _pImpl->qFileReader_.clear();

    // The other queues are all of the same type, so
    // let's make a stack of pointers to them and then
    // clear the flags and the queues for each to avoid
    // code duplication.
    std::vector<WorkerInputQueue<PThumbnailTemp>*> vQueues;
    for (uint u = 0;
         u < _pImpl->cPixbufLoaders;
         ++u)
        vQueues.push_back(&_pImpl->paqPixbufLoaders[u]);
    vQueues.push_back(&_pImpl->qScalerIconSmall);
    vQueues.push_back(&_pImpl->qScalerIconBig);

    for (auto &p : vQueues)
    {
        for (auto &pTemp : p->deq)
            pTemp->pThumb->pFile->clearFlag(FSFlag::THUMBNAILING);
        p->clear();
    }
}

/**
 *  First thread spawned by the constructor. This blocks on the primary
 *  queue that is fed by enqueue() and then creates a FileContents for
 *  every such file. We then test for the file type; if it's an image
 *  file, the data gets loaded and passed on to the pixbuf reader thread,
 *  otherwise we determine a default icon here.
 */
void
Thumbnailer::fileReaderThread()
{
    Debug::Log(THUMBNAILER, string(__func__) + " started, blocking");

    PThumbnail pThumbnailIn;
    while(1)
    {
        try
        {
            // Block until someone has queued a file.
            if (!(pThumbnailIn = _pImpl->qFileReader_.fetch()))
                // NULL means terminate thread.
                break;

            using namespace std::chrono;
            steady_clock::time_point t1 = steady_clock::now();

            if (!(pThumbnailIn->pFormat2 = ContentType::IsImageFile(pThumbnailIn->pFile)))
            {
                // Is not an image file:
                pThumbnailIn->ppbIconBig = _app.getFileTypeIcon(*pThumbnailIn->pFile, ICON_SIZE_BIG);
                pThumbnailIn->ppbIconSmall = _app.getFileTypeIcon(*pThumbnailIn->pFile, ICON_SIZE_SMALL);

                // In this case, post back to GUI immediately.
                _pImpl->postResultToGui(pThumbnailIn);
            }
            else
            {
                // Is image file:
                std::shared_ptr<FileContents> pFileContents = make_shared<FileContents>(g_pFsGioImpl->getGioFile(*pThumbnailIn->pFile));

                auto pThumbnailTemp = make_shared<ThumbnailTemp>(pThumbnailIn,
                                                                 pFileContents);

                milliseconds time_span = duration_cast<milliseconds>(steady_clock::now() - t1);
                Debug::Log(THUMBNAILER, string(__func__) + ": reading file \"" + pThumbnailIn->pFile->getBasename() + "\" took " + to_string(time_span.count()) + "ms");

                // Find the queue that's least busy. There is a race between
                // our size() query and the post() call later, but it's still
                // a good indicator which of the threads to bother.
                size_t uLeastBusyThread = 0;
                size_t uLeastBusyQueueSize = 99999999;
                for (uint u = 0;
                     u < _pImpl->cPixbufLoaders;
                     ++u)
                {
                    size_t sz = _pImpl->paqPixbufLoaders[u].size();
                    if (sz < uLeastBusyQueueSize)
                    {
                        uLeastBusyThread = u;
                        uLeastBusyQueueSize = sz;
                    }
                }

                Debug::Log(THUMBNAILER, string(__func__) + ": queue " + to_string(uLeastBusyThread) + " is least busy (" + to_string(uLeastBusyQueueSize) + "), queueing there");
                _pImpl->paqPixbufLoaders[uLeastBusyThread].post(pThumbnailTemp);

    //                 ppb = Gdk::Pixbuf::create_from_file(strPath);

            }
        }
        catch (exception &e)
        {
            Debug::Log(CMD_TOP, string("Exception in Thumbnailer::fileReaderThread(): ") + e.what());
        }
    }
}

/**
 *  Thread func for the second class of threads, which gets spawned C_LOADER_THREADS
 *  times in order to parse the input files into a Pixbuf via PixbufLoader with optimal
 *  parallel processing.
 *
 *  The aqPixbufLoaders queues get fed by the single fileReaderThread; the results are
 *  then passed on to the two scaler threads.
 */
void
Thumbnailer::pixbufLoaderThread(uint threadno)
{
    Debug::Log(THUMBNAILER, string(__func__) + " started, blocking");

    PThumbnailTemp pTemp;
    while (1)
    {
        // Block until someone has queued a file.
        if (!(pTemp = _pImpl->paqPixbufLoaders[threadno].fetch()))
            // NULL means terminate thread.
            break;

        using namespace std::chrono;
        steady_clock::time_point t1 = steady_clock::now();

        PPixbuf ppb;
        try
        {
            auto pLoader = Gdk::PixbufLoader::create(pTemp->pThumb->pFormat2->get_name());
            if (pLoader)
            {
                pLoader->write((const guint8*)pTemp->pFileContents->_pData, pTemp->pFileContents->_size);       // can throw
                ppb = pLoader->get_pixbuf();
                pLoader->close();
            }
        }
        catch (...)
        {
            // No error handling. If we can't read the file, no thumbnail. Period.
        }

        if (ppb)
        {
            milliseconds time_span = duration_cast<milliseconds>(steady_clock::now() - t1);
            Debug::Log(THUMBNAILER, string(__func__) + to_string(threadno) + ": loading \"" + pTemp->pThumb->pFile->getBasename() + "\" took " + to_string(time_span.count()) + "ms");

            pTemp->setLoaded(ppb);

            _pImpl->qScalerIconSmall.post(pTemp);
            _pImpl->qScalerIconBig.post(pTemp);
        }
    }
}

PPixbuf Thumbnailer::scale(PFsGioFile pFS,
                           PPixbuf ppbIn,
                           size_t thumbsize)
{
    size_t
        cxSrc = ppbIn->get_width(),
        cySrc = ppbIn->get_height(),
        cxThumb = thumbsize,
        cyThumb = thumbsize;
    if (cxSrc > cySrc)      // landscape
        cyThumb = thumbsize * cySrc / cxSrc;
    else                    // portrait
        cxThumb = thumbsize * cxSrc / cySrc;

    auto ppbOut = ppbIn->scale_simple(cxThumb,
                                      cyThumb,
                                      Gdk::InterpType::INTERP_BILINEAR);

    if (ppbOut)
    {
        Glib::ustring strOrientation = ppbIn->get_option("orientation");
        if (!strOrientation.empty())
        {
//             Debug::Log(DEBUG_ALWAYS, "orientation of \"" + pFS->getBasename() + "\": " + strOrientation);

            // 1 means orientation is not rotated.
            // 6 means orientation 45° counter-clockwise.
            if (strOrientation == "6")
                ppbOut = ppbOut->rotate_simple(Gdk::PixbufRotation::PIXBUF_ROTATE_CLOCKWISE);
            // 8 means orientation 45° clockwise.
            else if (strOrientation == "8")
                ppbOut = ppbOut->rotate_simple(Gdk::PixbufRotation::PIXBUF_ROTATE_COUNTERCLOCKWISE);
        }
    }

    pFS->setThumbnail(thumbsize, ppbOut);

    return ppbOut;
}

void Thumbnailer::scalerSmallThread()
{
    Debug::Log(THUMBNAILER, string(__func__) + " started, blocking");

    PThumbnailTemp pTemp;
    while (1)
    {
        // Block until fileReaderThread() has posted something.
        if (!(pTemp = _pImpl->qScalerIconSmall.fetch()))
            // NULL means terminate thread.
            break;

        using namespace std::chrono;
        steady_clock::time_point t1 = steady_clock::now();

        PPixbuf ppb = scale(pTemp->pThumb->pFile, pTemp->ppbOrig, ICON_SIZE_SMALL);

        bool fBothThumbnailsReady = false;
        if (ppb)
        {
            milliseconds time_span = duration_cast<milliseconds>(steady_clock::now() - t1);
            Debug::Log(THUMBNAILER, string(__func__) + ": scaling file \"" + pTemp->pThumb->pFile->getBasename() + "\" took " + to_string(time_span.count()) + "ms");

            Lock lock(mutex);
            pTemp->pThumb->ppbIconSmall = ppb;
            // If the other thumbnail is also ready, then post final result back to GUI.
            if (pTemp->pThumb->ppbIconBig)
                fBothThumbnailsReady = true;
        }

        if (fBothThumbnailsReady)
            _pImpl->postResultToGui(pTemp->pThumb);
    }
}

void Thumbnailer::scalerBigThread()
{
    Debug::Log(THUMBNAILER, string(__func__) + " started, blocking");

    PThumbnailTemp pTemp;
    while (1)
    {
        // Block until fileReaderThread() has posted something.
        if (!(pTemp = _pImpl->qScalerIconBig.fetch()))
            // NULL means terminate thread.
            break;

        using namespace std::chrono;
        steady_clock::time_point t1 = steady_clock::now();

        PPixbuf ppb = scale(pTemp->pThumb->pFile, pTemp->ppbOrig, ICON_SIZE_BIG);

        bool fBothThumbnailsReady = false;
        if (ppb)
        {
            milliseconds time_span = duration_cast<milliseconds>(steady_clock::now() - t1);
            Debug::Log(THUMBNAILER, string(__func__) + ": scaling file \"" + pTemp->pThumb->pFile->getBasename() + "\" took " + to_string(time_span.count()) + "ms");

            Lock lock(mutex);
            pTemp->pThumb->ppbIconBig = ppb;
            // If the other thumbnail is also ready, then post final result back to GUI.
            if (pTemp->pThumb->ppbIconSmall)
                fBothThumbnailsReady = true;
        }

        if (fBothThumbnailsReady)
            _pImpl->postResultToGui(pTemp->pThumb);
    }
}
