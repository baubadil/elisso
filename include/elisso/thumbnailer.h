/*
 * elisso (C) 2016--2017 Baubadil GmbH.
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
#include "elisso/fsmodel.h"

struct Thumbnail
{
    PFSFile         pFile;
    PRowReference   pRowRef;
    PPixbuf         ppbIconSmall;
    PPixbuf         ppbIconBig;

    Thumbnail(PFSFile pFile_, PRowReference pRowRef_)
        : pFile(pFile_), pRowRef(pRowRef_)
    { }
};
typedef std::shared_ptr<Thumbnail> PThumbnail;

/**
 *  Thumbnailer with three background threads that communicate with the GUI thread.
 *
 *  Create an instance of this and call connect() with a lambda that will receive
 *  thumbnails on the GUI thread.
 *
 *  Then call enqueue() for every file that should be thumbnailed. Three background
 *  threads will then try to parallelize file reading and scaling as much as possible.
 *  The scaled pixbuf will then be sent back to the GUI thread, and the callback
 *  you gave to connect() will be called. That callback should call fetchResult()
 *  to get the thumbnail.
 *
 *  This gives us a 20-50% performance increase.
 *
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
 */
class Thumbnailer
{
public:
    Thumbnailer();
    ~Thumbnailer();

    sigc::connection connect(std::function<void ()> fn);

    void enqueue(PFSFile pFile, PRowReference pRowRef);

    PThumbnail fetchResult();

    void stop();

private:

    void fileReaderThread();

    PPixbuf scale(PFSFile pFS, PPixbuf ppbIn, size_t size);

    void scalerSmallThread();
    void scalerBigThread();

    struct Impl;
    Impl    *_pImpl;

};

#endif // ELISSO_THUMBNAILER_H


