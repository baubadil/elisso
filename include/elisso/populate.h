/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_POPULATE_H
#define ELISSO_POPULATE_H

#include "elisso/fileops.h"
#include "elisso/worker.h"


/***************************************************************************
 *
 *  PopulateResult
 *
 **************************************************************************/

struct ViewPopulatedResult
{
    // Three lists for getContents:
    PFSVector       pvContents;         // Complete folder contents.
    FSVector        vAdded;             // Files that were added. Useful for refresh.
    FSVector        vRemoved;           // Files that were removed. Useful for refresh.
    uint            idPopulateThread;
    bool            fClickFromTree;     // true if SetDirectoryFlag::CLICK_FROM_TREE was set.
    Glib::ustring   strError;

    ViewPopulatedResult(uint idPopulateThread_, bool fClickFromTree_)
        : pvContents(make_shared<FSVector>()),
          idPopulateThread(idPopulateThread_),
          fClickFromTree(fClickFromTree_)
    { }
};
typedef std::shared_ptr<ViewPopulatedResult> PViewPopulatedResult;

typedef WorkerResultQueue<PViewPopulatedResult> ViewPopulatedWorker;
typedef std::shared_ptr<ViewPopulatedWorker> PViewPopulatedWorker;


/***************************************************************************
 *
 *  PopulateThread
 *
 **************************************************************************/

class PopulateThread;
typedef std::shared_ptr<PopulateThread> PPopulateThread;

/**
 *  Populate thread implementation. This works as follows:
 *
 *   1) ElissoFolderView::setDirectory() calls Create(), which creates a shared_ptr
 *      to an instance and spawns the thread. The instance is also passed to the
 *      thread, which increases the refcount.
 *
 *   2) Create() takes a reference to a Glib::Dispatcher which gets fired when
 *      the populate thread ends. Create() also takes a reference to an FSList,
 *      which gets filled by the populate thread with the contents from
 *      FSContainer::getContents().
 *
 *   3) When the dispatcher then fires on the GUI thread, it should check
 *      getError() if an exception occured on the populate thread. If not,
 *      it can take the folder contents that was referenced by Create()
 *      and fill the folder view with it.
 *
 *   4) If, for any reason, the populate needs to be stopped early, the caller
 *      can call stop() which will set the stop flag passed to
 *      FSContainer::getContents() and block until the populate thread has ended.
 */
class PopulateThread : public ProhibitCopy
{
public:
    static PPopulateThread Create(PFSModelBase &pDir,
                                  PViewPopulatedWorker pWorkerResult,
                                  bool fClickFromTree,
                                  PFSModelBase pDirSelectPrevious);

    /**
     *  Returns the unique thread ID for this populate thread. This allows for identifying which
     *  results come from which thread.
     */
    uint getID()
    {
        return _id;
    }

    /**
     *  Sets the stop flag for the thread and returns immediately; does not wait for the
     *  thread to terminate.
     */
    void stop()
    {
        _stopFlag.set();
    }

    bool shouldBeSelected(PFSModelBase &pFS)
    {
        return (pFS == _pDirSelectPrevious);
    }

private:
    /**
     *  Constructor.
     */
    PopulateThread(PFSModelBase &pDir,
                   PViewPopulatedWorker pWorkerResult,
                   PFSModelBase pDirSelectPrevious);

    void threadFunc(uint idPopulateThread, bool fClickFromTree);

    uint                _id;
    PFSModelBase        _pDir;
    PViewPopulatedWorker _pWorkerResult;
    StopFlag            _stopFlag;
    PFSModelBase        _pDirSelectPrevious;
};

#endif // ELISSO_POPULATE_H
