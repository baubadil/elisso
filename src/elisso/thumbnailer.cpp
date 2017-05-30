/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/thumbnailer.h"

#include "elisso/worker.h"
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
    WorkerInputQueue<PThumbnail>        qFileReader_;

    unsigned                            cPixbufLoaders;
    WorkerInputQueue<PThumbnailTemp>    *paqPixbufLoaders;

    WorkerInputQueue<PThumbnailTemp>    qScalerIconSmall;
    WorkerInputQueue<PThumbnailTemp>    qScalerIconBig;

    // List of formats supported by GTK's PixbufLoader.
    std::vector<Gdk::PixbufFormat>      vFormats;
    // Map of upper-cased file extensions with pointers into vFormats.
    map<string, Gdk::PixbufFormat*>     mapFormats;

    Impl()
        : vFormats(Gdk::Pixbuf::get_formats())
    {
        // Build the map of supported image formats, sorted by extension in upper case.
        for (auto &fmt : vFormats)
            for (const auto &ext : fmt.get_extensions())
                mapFormats[strToUpper(ext)] = &fmt;

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

/**
 *  Constructor.
 */
Thumbnailer::Thumbnailer()
    : _pImpl(new Impl)
{
    Debug::Log(THUMBNAILER, "Thumbnailer constructed");

     // Create the file reader thread.
    XWP::Thread::Create([this]()
    {
        this->fileReaderThread();
    });

    // Create the pixmap loader threads.
    for (uint u = 0;
         u < _pImpl->cPixbufLoaders;
         ++u)
    {
        XWP::Thread::Create([this, u]()
        {
            this->pixbufLoaderThread(u);
        });
    }

    // Create the pixbuf scaler thread.
    XWP::Thread::Create([this]()
    {
        this->scalerSmallThread();
    });

    // Create the pixbuf scaler thread.
    XWP::Thread::Create([this]()
    {
        this->scalerBigThread();
    });
}

Thumbnailer::~Thumbnailer()
{
    delete _pImpl;
}

/**
 *  To be called once on the GUI thread after creation with
 *  a lambda that should be called every time a thumbnail is
 *  ready. That lambda should call fetchResult().
 */
sigc::connection
Thumbnailer::connect(std::function<void ()> fn)
{
    return _pImpl->connect(fn);
}

/**
 *  Tests by file extension whether the file is of one of the types
 *  supported by the thumbnailer (which is all the types supported by
 *  GdkPixBuf internally).
 *
 *  Returns nullptr if not.
 */
const Gdk::PixbufFormat*
Thumbnailer::isImageFile(PFSModelBase pFile)
{
    const string &strBasename = pFile->getBasename();
    string strExtension = strToUpper(getExtensionString(strBasename));
    auto it = _pImpl->mapFormats.find(strExtension);
    if (it != _pImpl->mapFormats.end())
        return it->second;

    return nullptr;
}

/**
 *  Adds an FSFile to the queues to be thumbnailed. With pFormat, pass in
 *  the pixbuf format pointer returned by isImageFile(), which you should
 *  call first.
 */
void
Thumbnailer::enqueue(PFSFile pFile,
                     const Gdk::PixbufFormat *pFormat,
                     PRowReference pRowRef)
{
    Debug::Log(THUMBNAILER, string(__func__) + ":  " + pFile->getBasename());
    _pImpl->qFileReader_.post(make_shared<Thumbnail>(pFile, pFormat, pRowRef));
}

/**
 *  Forwarder method to WorkerResult::fetchResult(). To be called
 *  by the lambda that was passed to connect().
 */
PThumbnail
Thumbnailer::fetchResult()
{
    return _pImpl->fetchResult();
}

/**
 *  Returns true if the queues are not empty.
 */
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

/**
 *  Clears the queues for all background threads. This is useful whenever a
 *  folder view gets populated to make sure we don't keep the system busy
 *  with a thousand thumbnails that will never be seen, since the new populate
 *  will trigger another queue fill.
 */
void
Thumbnailer::stop()
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
 *  every such file. It then passes the file contents on to the least
 *  busy pixbuf loader thread.
 */
void Thumbnailer::fileReaderThread()
{
    Debug::Log(THUMBNAILER, string(__func__) + " started, blocking");

    PThumbnail pThumbnailIn;
    while(1)
    {
        // Block until someone has queued a file.
        pThumbnailIn = _pImpl->qFileReader_.fetch();

        using namespace std::chrono;
        steady_clock::time_point t1 = steady_clock::now();

        if (pThumbnailIn->pFormat)
        {
            std::shared_ptr<FileContents> pFileContents = make_shared<FileContents>(pThumbnailIn->pFile->getGioFile());

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
}

/**
 *  Thread func for the second class of threads, which gets spawned C_LOADER_THREADS
 *  times in order to parse the input files into a Pixbuf via PixbufLoader with optimal
 *  parallel processing.
 *
 *  The aqPixbufLoaders queues get fed by the single fileReaderThread; the results are
 *  then passed on to the two scaler threads.
 */
void Thumbnailer::pixbufLoaderThread(uint threadno)
{
    Debug::Log(THUMBNAILER, string(__func__) + " started, blocking");

    PThumbnailTemp pTemp;
    while (1)
    {
        // Block until someone has queued a file.
        pTemp = _pImpl->paqPixbufLoaders[threadno].fetch();

        using namespace std::chrono;
        steady_clock::time_point t1 = steady_clock::now();

        PPixbuf ppb;
        try
        {
            auto pLoader = Gdk::PixbufLoader::create(pTemp->pThumb->pFormat->get_name());
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

PPixbuf Thumbnailer::scale(PFSFile pFS,
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
        pTemp = _pImpl->qScalerIconSmall.fetch();

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
            _pImpl->postResultToGUI(pTemp->pThumb);
    }
}

void Thumbnailer::scalerBigThread()
{
    Debug::Log(THUMBNAILER, string(__func__) + " started, blocking");

    PThumbnailTemp pTemp;
    while (1)
    {
        // Block until fileReaderThread() has posted something.
        pTemp = _pImpl->qScalerIconBig.fetch();

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
            _pImpl->postResultToGUI(pTemp->pThumb);
    }
}
