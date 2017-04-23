/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_WORKER_H
#define ELISSO_WORKER_H

#include <gtkmm.h>
#include <mutex>
#include <deque>

#include "xwp/lock.h"


/***************************************************************************
 *
 *  WorkerResult class template
 *
 **************************************************************************/

/**
 *  A worker result structure combines a Glib::Dispatcher with an STL double-ended queue
 *  to implement a "producer-consumer" model for a worker thread and the GTK main thread.
 *
 *  The worker thread is not part of this structure.
 *
 *  The P template argument is assumed to be a queable structure, best as a shared_ptr
 *  to something.
 *
 *  After creating an instance of this, you must manually call connect() with a callback
 *  that gets connected to the Glib dispatcher. This will then handle arrival of data.
 *
 *  The worker thread should create instances of P and call addResult(), which will add
 *  the P to the queue and fire the dispatcher, which will then call the callback given
 *  to connect() on the GUI thread.
 *
 *  The queue is properly protected by a mutex.
 *
 *  Something like this:


            WorkerResult<PMyStruct> w;
            workerResult.connect([&w]()
            {
                // Getting the result on the GUI thread.
                PMyStruct p = w.fetchResult();
                ...
            });

            new std::thread([&w]()
            {
                PMyStruct p = std::make_shared<MyStruct>(...);
                w.postResultToGUI(p);
            }
 */
template<class P>
class WorkerResult : public ProhibitCopy
{
public:
    sigc::connection connect(std::function<void ()> fn)
    {
        return dispatcher.connect(fn);
    }

    void postResultToGUI(P pResult)
    {
        // Do not hold the mutex while messing with the dispatcher -> that could deadlock.
        {
            Lock lock(mutex);
            deque.push_back(pResult);
        }
        dispatcher.emit();
    }

    P fetchResult()
    {
        Lock lock(mutex);
        P p;
        if (deque.size())
        {
            p = deque.at(0);
            deque.pop_front();
        }
        return p;
    }

protected:
    std::recursive_mutex    mutex;
    Glib::Dispatcher        dispatcher;
    std::deque<P>           deque;
};

#endif // ELISSO_WORKER_H
