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
    FsVector        vAdded;             // Files that were added. Useful for refresh.
    FsVector        vRemoved;           // Files that were removed. Useful for refresh.
    uint            idPopulateThread;
    bool            fClickFromTree;     // true if SetDirectoryFlag::CLICK_FROM_TREE was set.
    PFsObject       pDirSelectPrevious; // Item to select among populate results, or nullptr.
    Glib::ustring   strError;

    ViewPopulatedResult(uint idPopulateThread_, bool fClickFromTree_, PFsObject pDirSelectPrevious_)
        : pvContents(make_shared<FsVector>()),
          idPopulateThread(idPopulateThread_),
          fClickFromTree(fClickFromTree_),
          pDirSelectPrevious(pDirSelectPrevious_)
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
 *   2) Create() takes a reference to a ViewPopulatedWorker with a Glib::Dispatcher
 *      which gets fired when the populate thread ends. That returns a
 *      ViewPopulatedResult with the results from the populate thread's
 *      FsContainer::getContents() call.
 *
 *   3) When the dispatcher then fires on the GUI thread, it should check
 *      ViewPopulatedResult::strError if an exception occured on the populate thread.
 *      If not, it can fill the folder view with the results.
 *
 *   4) If, for any reason, the populate needs to be stopped early, the caller
 *      can call stop() which will set the stop flag passed to
 *      FsContainer::getContents().
 */
class PopulateThread : public ProhibitCopy
{
public:
    static PPopulateThread Create(PFsObject &pDir,
                                  PViewPopulatedWorker pWorkerResult,
                                  bool fClickFromTree,
                                  bool fFollowSymlinks,
                                  PFsObject pDirSelectPrevious);

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

private:
    /**
     *  Constructor.
     */
    PopulateThread(PFsObject &pDir,
                   PViewPopulatedWorker pWorkerResult,
                   PFsObject pDirSelectPrevious);

    void threadFunc(uint idPopulateThread,
                    bool fClickFromTree,
                    bool fFollowSymlinks);

    uint                _id;
    PFsObject           _pDir;
    PViewPopulatedWorker _pWorkerResult;
    StopFlag            _stopFlag;
    PFsObject           _pDirSelectPrevious;
};

#endif // ELISSO_POPULATE_H
