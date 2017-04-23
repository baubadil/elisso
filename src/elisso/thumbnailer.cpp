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
#include <thread>
#include <mutex>
#include <condition_variable>
#include <ctime>
#include <ratio>
#include <chrono>

struct ThumbnailTemp
{
    PThumbnail      pThumb;
    PPixBuf         ppbOrig;

    ThumbnailTemp(PThumbnail pThumb_, PPixBuf ppbOrig_)
        : pThumb(pThumb_),
          ppbOrig(ppbOrig_)
    { }
};

typedef std::shared_ptr<ThumbnailTemp> PThumbnailTemp;


/***************************************************************************
 *
 *  Thumbnailer::Impl (private)
 *
 **************************************************************************/

/**
 *  Templated input queue for a worker thread.
 */
template<class P>
struct ThreadQueue
{
    std::mutex                  mutex;
    std::condition_variable     cond;
    std::deque<P>               deq;

    /**
     *  To be called by employer thread to add work to the queue.
     *  Requests the lock, adds p to the queue and post the condition variable
     *  so the worker thread wakes up in fetch().
     */
    void post(P p)
    {
        {
            std::unique_lock<std::mutex> lock(mutex);
            deq.push_back(p);
        }
        cond.notify_one();
    }

    /**
     *  To be called by the worker thread to pick up the next item to be worked
     *  on. Blocks if the queue is empty and does not return until an item
     *  as been posted in the queue.
     */
    P fetch()
    {
        // Block on the condition variable and make sure the deque isn't empty.
        std::unique_lock<std::mutex> lock(mutex);
        // Loop until the condition variable has been posted AND something's in the
        // queue (condition variables can wake up spuriously).
        while (!deq.size())
            cond.wait(lock);
        // Lock has been reacquired now.

        P p = deq.at(0);
        deq.pop_front();
        return p;
    }

    /**
     *  To be called by employer thread if it decides that all work should be
     *  stopped and the work queue should be emptied after all.
     */
    void clear()
    {
        std::unique_lock<std::mutex> lock(mutex);
        deq.clear();
    }
};

/**
 *  Our private Impl structure, anonymously declared in the Thumbnailer class.
 *
 *  This combines four things:
 *
 *   -- It contains three ThreadQueue instantiations, for the three threads that
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
struct Thumbnailer::Impl : WorkerResult<PThumbnail>
{
    ThreadQueue<PThumbnail>     qFileReader;
    ThreadQueue<PThumbnailTemp> qScalerIconSmall;
    ThreadQueue<PThumbnailTemp> qScalerIconBig;
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
    new std::thread([this]()
    {
        this->fileReaderThread();
    });

    // Create the pixbuf scaler thread.
    new std::thread([this]()
    {
        this->scalerSmallThread();
    });

    // Create the pixbuf scaler thread.
    new std::thread([this]()
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
 *  Adds an FSFile to the queues to be thumbnailed.
 */
void
Thumbnailer::enqueue(PFSFile pFile, PRowReference pRowRef)
{
    Debug::Log(THUMBNAILER, string(__func__) + ":  " + pFile->getBasename());
    _pImpl->qFileReader.post(make_shared<Thumbnail>(pFile, pRowRef));
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
 *  Clears the queues for all background threads. This is useful whenever a
 *  folder view gets populated to make sure we don't keep the system busy
 *  with a thousand thumbnails that will never be seen, since the new populate
 *  will trigger another queue fill.
 */
void
Thumbnailer::stop()
{
    _pImpl->qFileReader.clear();
    _pImpl->qScalerIconSmall.clear();
    _pImpl->qScalerIconBig.clear();
}

void Thumbnailer::fileReaderThread()
{
    Debug::Log(THUMBNAILER, string(__func__) + " started, blocking");

    PThumbnail pThumbnailIn;
    while(1)
    {
        // Block until someone wants a file.
        pThumbnailIn = _pImpl->qFileReader.fetch();

        using namespace std::chrono;
        steady_clock::time_point t1 = steady_clock::now();

        PPixBuf ppb = Gdk::Pixbuf::create_from_file(pThumbnailIn->pFile->getRelativePath());
        if (ppb)
        {
            milliseconds time_span = duration_cast<milliseconds>(steady_clock::now() - t1);
            Debug::Log(THUMBNAILER, string(__func__) + ": reading file \"" + pThumbnailIn->pFile->getBasename() + "\" took " + to_string(time_span.count()) + "ms");

            auto pThumbnailTemp = make_shared<ThumbnailTemp>(pThumbnailIn,
                                                             ppb);
            _pImpl->qScalerIconSmall.post(pThumbnailTemp);
            _pImpl->qScalerIconBig.post(pThumbnailTemp);
        }
    }
}

PPixBuf Thumbnailer::scale(PPixBuf ppbIn, size_t thumbsize)
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
                               Gdk::InterpType::INTERP_TILES);
                               // Gdk::InterpType::INTERP_BILINEAR);

    Glib::ustring strOrientation = ppbIn->get_option("orientation");
    if (strOrientation == "8")
        ppbOut = ppbOut->rotate_simple(Gdk::PixbufRotation::PIXBUF_ROTATE_COUNTERCLOCKWISE);
//     Debug::Log(THUMBNAILER, "orientation: " + strOrientation);

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

        PPixBuf ppb = scale(pTemp->ppbOrig, ICON_SIZE_SMALL);

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

        PPixBuf ppb = scale(pTemp->ppbOrig, ICON_SIZE_BIG);

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
