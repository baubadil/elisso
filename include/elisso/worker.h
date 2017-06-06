/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
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
#include <condition_variable>
#include <deque>

#include "xwp/thread.h"


/***************************************************************************
 *
 *  WorkerInputQueue class template
 *
 **************************************************************************/

/**
 *  Templated input queue for a worker thread. This model is designed for a
 *  thread that is always running, but blocked on a condition variable until
 *  there is work to do; it then fetches an item of type P from the queue.
 */
template<class P>
struct WorkerInputQueue : public ProhibitCopy
{
    std::mutex                  mutex;
    std::condition_variable     cond;
    std::deque<P>               deq;

    /**
     *  Returns the no. of items queued. This is not atomic if you use it with post()
     *  but it can give an indication if the queue is busy.
     */
    size_t size()
    {
        std::unique_lock<std::mutex> lock(mutex);
        return deq.size();
    }

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


/***************************************************************************
 *
 *  WorkerResultQueue class template
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
class WorkerResultQueue : public ProhibitCopy
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
    Mutex               mutex;
    Glib::Dispatcher    dispatcher;
    std::deque<P>       deque;
};

#endif // ELISSO_WORKER_H
