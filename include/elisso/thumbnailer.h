/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_THUMBNAILER_H
#define ELISSO_THUMBNAILER_H

#include "elisso/elisso.h"
#include "elisso/fsmodel_gio.h"
// #include "xwp/except.h"

struct Thumbnail
{
    PFsGioFile              pFile;
    const Gdk::PixbufFormat *pFormat2;
    PPixbuf                 ppbIconSmall;
    PPixbuf                 ppbIconBig;

    Thumbnail(PFsGioFile pFile_)
        : pFile(pFile_)
    {
    }
};
typedef std::shared_ptr<Thumbnail> PThumbnail;

struct ThumbnailTemp;
typedef std::shared_ptr<ThumbnailTemp> PThumbnailTemp;

class ElissoApplication;

/**
 *  Thumbnailer with three types of background threads that communicate with the GUI thread.
 *
 *  Create an instance of this and call connect() with a lambda that will receive
 *  finished thumbnails on the GUI thread.
 *
 *  Then call enqueue() from the GUI thread for every file that should be thumbnailed.
 *  From then on three types of threads will process the file's contents with as much
 *  concurrency as possible:
 *
 *   1) The "file reader" thread does a simple fopen() and reads the complete image file's
 *      contents into memory.
 *
 *   2) From there the file contents in memory get passed to one of the "pixbuf loader"
 *      threads. The C_LOADER_THREADS class constant determines how many of them should
 *      be started; the "file reader" thread posts the file contents into the "pixbuf loader"
 *      thread whose queue is the least busy. The "pixbuf loader" thread then uses
 *      GdkPixbufLoader with the format of the file to parse the in-memory conents and
 *      create a full-size GdkPixbuf from it. This is CPU-bound only, so we can run
 *      several of these in parallel.
 *
 *   3) Two additional threads then scale each such pixbuf to the "big" and "small" icon
 *      sizes. Whichever scaler finishes last (meaning that both sizes are finished),
 *      then calls the Glib dispatcher which signals to your GUI thread that the thumbnail
 *      is done. This will call the callback you gave to connect(). That callback should
 *      call fetchResult() to get the thumbnail.
 *
 *  The first attempt of this only parallelized 1) and 3).
 *  Some detailed statistics for a few sample files:
 *
              File size       fileReader  scalerSmall   scalerBig         Sum     Max     Improv    Improv
                (MB)            (ms)        (ms)         (ms)             (ms)    (ms)    (ms)       (%)
    Local SSD   6.5             203         35            51              289     203     86         29.76%
                6.5             137         38            42              217     137     80         36.87%
                5.4             127         52            64              243     127     116        47.74%
    NAS NFS     6.5             172         51            46              269     172     97         36.06%
                6.2             156         50            59              265     156     109        41.13%
                7.5             180         37            43              260     180     80         30.77%
    NAS NFS     10              222         40            50              312     222     90         28.85%

 *  So, most time is spent in the file reader (which reads from disk and parses the JPEG into a Pixbuf).
 *  Whether the file is on a local disk or in the network doesn't seem to make much of a difference.
 *
 *  This improved code has replaced the single GdkPixbuf::from_file() call by splitting the job into
 *  the thread classes 1) and 2), and it turns out that 2) can speed up things greatly by running
 *  it four times in parallel. I can get up to 95% CPU usage out of my 4-core (8 hyperthreads)
 *  system. It doees feel four times as fast.
 */
class Thumbnailer
{
public:
    /**
     *  Constructor. Each ElissoFolderView has an instance in the implementation,
     *  so this gets called once for each folder view that is created.
     */
    Thumbnailer(ElissoApplication &app);

    /**
     *  Destructor. This calls clearQueues() in turn and then stops all threads.
     */
    ~Thumbnailer();

    /**
     *  To be called once on the GUI thread after creation with
     *  a lambda that should be called every time a thumbnail is
     *  ready. That lambda should call fetchResult().
     */
    sigc::connection connect(std::function<void ()> fn);

    /**
     *  Adds a file to the queues to be thumbnailed. Call this on the GUI
     *  thread. This will then pass the file to the first worker thread for
     *  type testing; if it's an image file, it will get passed through
     *  further background tests. In any case, the file will arrive back
     *  at the GUI thread in the lambda passed to connect().
     */
    void enqueue(PFsGioFile pFile);

    /**
     *  Forwarder method to WorkerResult::fetchResult(). To be called
     *  by the lambda that was passed to connect().
     */
    PThumbnail fetchResult();

    /**
     *  Returns true if the queues are not empty.
     */
    bool isBusy();

    /**
     *  Clears the queues for all background threads. This is useful whenever a
     *  new folder view gets populated to make sure we don't keep the system busy
     *  with a thousand thumbnails from a previous populate that will never be seen,
     *  since the new populate will trigger another queue fill.
     */
    void clearQueues();

    /**
     *  Public static helper that can be called from anywhere to scale and rotate
     *  the given pixbuf. Returns a new pixbuf or nullptr on errors.
     */
    static PPixbuf ScaleAndRotate(PPixbuf ppbIn,
                                  size_t cxTarget,
                                  size_t cyTarget);

private:
    void fileReaderThread();

    void pixbufLoaderThread(uint threadno);

    PPixbuf scale(PFsGioFile pFS, PPixbuf ppbIn, size_t size);

    void scalerSmallThread();
    void scalerBigThread();

    struct Impl;
    Impl    *_pImpl;

    ElissoApplication &_app;
};

#endif // ELISSO_THUMBNAILER_H


