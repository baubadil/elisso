/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/fileops.h"

#include <thread>

#include "elisso/progressdialog.h"

#include "xwp/debug.h"
#include "xwp/except.h"


/***************************************************************************
 *
 *  FileOperation::Impl
 *
 **************************************************************************/

struct FileOperation::Impl
{
    std::thread             *pThread = nullptr;
    sigc::connection        connDispatch;
    sigc::connection        connTimer;

    PProgressDialog         *_ppProgressDialog;

    // Progress data. Protected by the parent WorkerResult mutex.
    PFSModelBase            pFSCurrent;
    double                  dProgress = 0;
};


/***************************************************************************
 *
 *  FileOperation
 *
 **************************************************************************/

/**
 *  Factory method which creates a shared_ptr<FileOperation>.
 */
/* static */
PFileOperation
FileOperation::Create(Type t,
                      const FileSelection &sel,
                      FileOperationsList &refQueue,
                      PProgressDialog *ppProgressDialog,
                      Gtk::Window *pParentWindow)
{
    /* This nasty trickery is necessary to make std::make_shared work with a protected constructor. */
    class Derived : public FileOperation
    {
    public:
        Derived(Type t, FileOperationsList &refQueue)
            : FileOperation(t, refQueue) { }
    };

    // Create the instance.
    auto p = make_shared<Derived>(t, refQueue);

    // Connect the dispatcher from the parent WorkerResult.
    p->_pImpl->connDispatch = p->connect([p]()
    {
        auto pFS = p->fetchResult();
        p->onItemProcessed(pFS);
    });

    // Instantiate a timer for progress reporting.
    p->_pImpl->connTimer = Glib::signal_timeout().connect([p]() -> bool
    {
        p->onProgress();
        return true;
    }, UPDATE_PROGRESS_MILLIS);

    // Enqueue the instance in the parent list.
    refQueue.push_back(p);

    // If the parent has given us a pointer to a progress dialog pointer, update
    // or create the dialog therein.
    if ((p->_pImpl->_ppProgressDialog = ppProgressDialog))
    {
        if (!*ppProgressDialog)
            *ppProgressDialog = make_shared<ProgressDialog>(*pParentWindow);

        (*ppProgressDialog)->addOperation(p);
    }

    // Copy the list of files to operate on.
    p->_llFiles = sel.llAll;

    // Launch the thread.
    p->_pImpl->pThread = new std::thread([p]()
    {
        /*
         *  Thread function!
         */
        p->threadFunc();
    });

    return p;
}

/**
 *  Protected constructor.
 */
FileOperation::FileOperation(Type t,
                             FileOperationsList  &refQueue)
    : _t(t),
      _refQueue(refQueue),
      _pImpl(new Impl)
{
}

FileOperation::~FileOperation()
{
    Debug::Log(FILE_HIGH, __func__);
    delete _pImpl;
}

void FileOperation::cancel()
{
    _stopFlag.set();
}

/**
 *  Thread function. The std::thread gets spawned in Create() and simply calls this method.
 *  This operated on the files in _llFiles (given to Create()) depending on the operation
 *  type.
 */
void
FileOperation::threadFunc()
{
    try
    {
        size_t cFiles = _llFiles.size();
        size_t cCurrent = 0;
        for (auto &pFS : _llFiles)
        {
            postResultToGUI(pFS);     // Temporarily requests the lock.

            {
                // Request the lock again.
                Lock lock(mutex);
                _pImpl->pFSCurrent = pFS;
                _pImpl->dProgress = (double)cCurrent / (double)cFiles;
            }

            switch (_t)
            {
                case Type::TEST:
                    pFS->testFileOps();
                break;

                case Type::TRASH:
                    pFS->sendToTrash();
                break;
            }

            if (_stopFlag)
                throw FSCancelledException();

            ++cCurrent;
        }
    }
    catch(exception &e)
    {
        _strError = e.what();
    }

    // Report "finished" by pushing a nullptr.
    postResultToGUI(nullptr);
}

/**
 *  GUI callback for the progress timer. This just updates the dialog.
 */
void
FileOperation::onProgress()
{
    Lock lock(mutex);
//     Debug::Log(FILE_HIGH, "File ops progress: " + to_string(_pImpl->dProgress * 100) + "%");

    if (_pImpl->_ppProgressDialog)
        if (_pImpl->pFSCurrent)     // Do NOT call with nullptr because that destroys the dialog
            (*_pImpl->_ppProgressDialog)->updateOperation(shared_from_this(),
                                                          _pImpl->pFSCurrent,
                                                          _pImpl->dProgress);
}

/**
 *  GUI callback invoked by the "item processed" dispatcher for every item that
 *  was processed. This should update the folder contents model.
 */
void
FileOperation::onItemProcessed(PFSModelBase pFS)
{
    if (pFS)
    {
        Debug::Log(FILE_HIGH, "File ops item processed: " + pFS->getRelativePath());

        switch (_t)
        {
            case Type::TEST:
            break;

            case Type::TRASH:
                pFS->notifyFileRemoved();
            break;
        }
    }
    else
    {
        Debug::Log(FILE_HIGH, "File ops item processed: NULL");

        // nullptr means everything done: then we need to destroy
        // all shared_ptr instances pointing to this, which will
        // invoke the destructor.

        auto pThis = shared_from_this();

        // 1) Remove us from the progress dialog, if one exists.
        if (_pImpl->_ppProgressDialog)
            (*_pImpl->_ppProgressDialog)->updateOperation(pThis,
                                                          nullptr,
                                                          100);

        // 2) We are stored in the parent's queue of file operations; remove us there.
        size_t c = _refQueue.size();
        _refQueue.remove(pThis);
        if (_refQueue.size() != c - 1)
            throw FSException("failed to remove fileops from list");

        // 1) The Glib timer and dispatcher each have a lambda with a shared_ptr to
        //    this, which is really a functor with a copy of the shared_ptr.
        //    Disconnecting these two will free the functor and release the shared_ptr
        //    instances.
        _pImpl->connTimer.disconnect();
        _pImpl->connDispatch.disconnect();
        // Now the destructor has been called, and we're dead.
    }
}
