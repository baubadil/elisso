/*
 * elisso (C) 2016--2017 Baubadil GmbH.
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
 *  Creates an instance and returns a shared_ptr to it. Caller MUST store that shared_ptr
 *  in instance data until the thread ends.
 */
/* static */
PPopulateThread
PopulateThread::Create(PFSModelBase &pDir,               //!< in: directory or symlink to directory to populate
                       PViewPopulatedWorker pWorkerResult,
                       bool fClickFromTree,              //!< in: stored in instance data for dispatcher handler
                       PFSModelBase pDirSelectPrevious)  //!< in: if set, select this item after populating
{
    /* This nasty trickery is necessary to make std::make_shared work with a protected constructor. */
    class Derived : public PopulateThread
    {
    public:
        Derived(PFSModelBase &pDir, PViewPopulatedWorker pWorkerResult, PFSModelBase pDirSelectPrevious)
            : PopulateThread(pDir, pWorkerResult, pDirSelectPrevious) { }
    };

    auto p = std::make_shared<Derived>(pDir, pWorkerResult, pDirSelectPrevious);

    // We capture the shared_ptr "p" without &, meaning we create a copy, which increases the refcount
    // while the thread is running.
    XWP::Thread::Create([p, fClickFromTree]()
    {
        /*
         *  Thread function!
         */
        p->threadFunc(p->_id, fClickFromTree);
    });

    return p;
}

/**
 *  Constructor.
 */
PopulateThread::PopulateThread(PFSModelBase &pDir,
                               PViewPopulatedWorker pWorkerResult,
                               PFSModelBase pDirSelectPrevious)
    : _pDir(pDir),
      _pWorkerResult(pWorkerResult),
      _pDirSelectPrevious(pDirSelectPrevious)
{
    _id = ++g_uPopulateThreadID;
}

void
PopulateThread::threadFunc(uint idPopulateThread,
                           bool fClickFromTree)
{
    PViewPopulatedResult pResult = std::make_shared<ViewPopulatedResult>(idPopulateThread, fClickFromTree);
    try
    {
        FSContainer *pCnr = _pDir->getContainer();
        if (pCnr)
            pCnr->getContents(*pResult->pvContents,
                              FSDirectory::Get::ALL,
                              &pResult->vAdded,
                              &pResult->vRemoved,
                              &_stopFlag);
    }
    catch (exception &e)
    {
        pResult->strError = e.what();
    }

    if (!_stopFlag)
        // Trigger the dispatcher, which will call "populate done".
        _pWorkerResult->postResultToGUI(pResult);
}
