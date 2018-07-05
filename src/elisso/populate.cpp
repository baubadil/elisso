/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/populate.h"


/***************************************************************************
 *
 *  Globals
 *
 **************************************************************************/

std::atomic<uint> g_uPopulateThreadID(0);


/***************************************************************************
 *
 *  PopulateThread
 *
 **************************************************************************/

/**
 *  Creates an instance and returns a shared_ptr to it. This returns a shared_ptr,
 *  which the caller can use to control the populate thread; another shared_ptr
 *  is held by the running thread, so the instance gets deleted when both the
 *  caller and the thread fund have released it.
 */
/* static */
PPopulateThread
PopulateThread::Create(PFsObject &pDir,               //!< in: directory or symlink to directory to populate
                       PViewPopulatedWorker pWorkerResult,
                       bool fClickFromTree,              //!< in: stored in instance data for dispatcher handler
                       bool fFollowSymlinks,             //!< in: whether to call follow() on each symlink in the thread
                       PFsObject pDirSelectPrevious)  //!< in: if set, select this item after populating
{
    /* This nasty trickery is necessary to make std::make_shared work with a protected constructor. */
    class Derived : public PopulateThread
    {
    public:
        Derived(PFsObject &pDir, PViewPopulatedWorker pWorkerResult, PFsObject pDirSelectPrevious)
            : PopulateThread(pDir, pWorkerResult, pDirSelectPrevious) { }
    };

    auto p = std::make_shared<Derived>(pDir, pWorkerResult, pDirSelectPrevious);

    // We capture the shared_ptr "p" without &, meaning we create a copy, which increases the refcount
    // while the thread is running.
    XWP::Thread::Create([p, fClickFromTree, fFollowSymlinks]()
    {
        /*
         *  Thread function!
         */
        p->threadFunc(p->_id,
                      fClickFromTree,
                      fFollowSymlinks);
    });

    return p;
}

/**
 *  Constructor.
 */
PopulateThread::PopulateThread(PFsObject &pDir,
                               PViewPopulatedWorker pWorkerResult,
                               PFsObject pDirSelectPrevious)
    : _pDir(pDir),
      _pWorkerResult(pWorkerResult),
      _pDirSelectPrevious(pDirSelectPrevious)
{
    _id = ++g_uPopulateThreadID;
}

void
PopulateThread::threadFunc(uint idPopulateThread,
                           bool fClickFromTree,
                           bool fFollowSymlinks)
{
    PViewPopulatedResult pResult = std::make_shared<ViewPopulatedResult>(idPopulateThread,
                                                                         fClickFromTree,
                                                                         _pDirSelectPrevious);
    try
    {
        FsContainer *pCnr = _pDir->getContainer();
        if (pCnr)
            pCnr->getContents(*pResult->pvContents,
                              FsDirectory::Get::ALL,
                              &pResult->vAdded,
                              &pResult->vRemoved,
                              &_stopFlag,
                              fFollowSymlinks);
    }
    catch (exception &e)
    {
        pResult->strError = e.what();
    }

    if (!_stopFlag)
        // Trigger the dispatcher, which will call "populate done".
        _pWorkerResult->postResultToGui(pResult);
}
