/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/fileops.h"

#include "elisso/elisso.h"
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
    sigc::connection        connDispatch;
    sigc::connection        connTimer;

    PProgressDialog         *_ppProgressDialog;

    // Shared source container of all objects, set & validated during init.
    FSContainer             *pSourceContainer = nullptr;
    // Target container for copy and move operations only.
    FSContainer             *pTargetContainer = nullptr;

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
 *  Factory method which creates a shared_ptr<FileOperation> and starts the thread
 *  that operates on it.
 */
/* static */
PFileOperation
FileOperation::Create(Type t,
                      const FSVector &vFiles,
                      PFSModelBase pTarget,                 //!< in: target directory or symlink to directory (required for copy or move, otherwise nullptr)
                      FileOperationsList &refQueue,         //!< in: list to append new FileOperation instance to
                      PProgressDialog *ppProgressDialog,    //!< in: progress dialog to append file operation to
                      Gtk::Window *pParentWindow)           //!< in: parent window for (modal) progress dialog
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
        p->onProcessingNextItem(pFS);
    });

    // Instantiate a timer for progress reporting.
    p->_pImpl->connTimer = Glib::signal_timeout().connect([p]() -> bool
    {
        p->onProgress();
        return true;
    }, UPDATE_PROGRESS_MILLIS);

    // Enqueue *this in the caller's file operations list.
    refQueue.push_back(p);

    // If the parent has given us a pointer to a progress dialog pointer, update
    // or create the dialog therein.
    if ((p->_pImpl->_ppProgressDialog = ppProgressDialog))
    {
        if (!*ppProgressDialog)
            *ppProgressDialog = make_shared<ProgressDialog>(*pParentWindow);

        (*ppProgressDialog)->addOperation(p);
    }

    // Deep-copy the list of files to operate on.
    for (auto &pFS : vFiles)
    {
        p->_vFiles.push_back(pFS);
        auto pParent = pFS->getParent();
        if (!pParent)
            throw FSException("File has no parent");
        FSContainer *pContainerThis = pParent->getContainer();
        if (!p->_pImpl->pSourceContainer)
            p->_pImpl->pSourceContainer = pContainerThis;
        else if (pContainerThis != p->_pImpl->pSourceContainer)
            throw FSException("Files in given list have more than one parent container");
    }

    if ((p->_pTarget = pTarget))
        if (!(p->_pImpl->pTargetContainer = pTarget->getContainer()))
            throw FSException("Missing target container");

    // Launch the thread.
    XWP::Thread::Create([p]()
    {
        /*
         *  Thread function!
         */
        p->threadFunc();
    });

    return p;
}

/**
 *  Protected constructor, only to be used by Create().
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
 *
 *  The semantics of the variables are:
 *
 *   -- Type::TEST: does nothing really. FSModelBase::testFileOps() only waits a little
 *      while for testing the progress dialog.
 *
 *   -- Type::TRASH: sends all files on the list to the desktop's trash can. This removes
 *      the files from all views they were inserted into; a target folder is not needed.
 *
 *   -- Type::MOVES: moves all files to the target folder given to the constructor.
 *      This removes the files from all views where they were inserted and inserts them
 *      into views of the target folder, if they are currently being monitored.
 *
 *   -- Type::COPY: copies all files to the target folder given to the constructor.
 *      This inserts the copies into views of the target folder.
 *
 *  This passes the source file pointer to postResultToGUI() except in the case of COPY,
 *  when this passes the copy.
 */
void
FileOperation::threadFunc()
{
    try
    {
        size_t cFiles = _vFiles.size();
        size_t cCurrent = 0;
        for (auto &pFS : _vFiles)
        {
            {
                // Temporarily request the lock.
                Lock lock(mutex);
                _pImpl->pFSCurrent = pFS;
                _pImpl->dProgress = (double)cCurrent / (double)cFiles;
            }

            // This is what gets posted to the GUI callback. This is
            PFSModelBase pFSForGUI(pFS);

            switch (_t)
            {
                case Type::TEST:
                    pFS->testFileOps();
                break;

                case Type::TRASH:
                    pFS->sendToTrash();
                break;

                case Type::MOVE:
                    pFS->moveTo(_pTarget);
                break;

                case Type::COPY:
                    pFSForGUI = pFS->copyTo(_pTarget);
                break;
            }

            if (_stopFlag)
                throw FSCancelledException();

            postResultToGui(pFSForGUI);     // Temporarily requests the lock.

            ++cCurrent;
        }
    }
    catch (exception &e)
    {
        _strError = e.what();
    }

    // Report "finished" by pushing a nullptr.
    postResultToGui(nullptr);
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
 *  GUI callback invoked by the dispatcher for every item that is about to be processed
 *  (when the thread calls postResultToGUI()). This should update the folder contents model.
 *
 *  pFS is the source file EXCEPT in the case of "copy", where it is the new copy of
 *  the file, since that's what's needed in the GUI.
 */
void
FileOperation::onProcessingNextItem(PFSModelBase pFS)
{
    if (pFS)
    {
        Debug::Log(FILE_HIGH, "File ops item processed: " + pFS->getPath());

        switch (_t)
        {
            case Type::TEST:
            break;

            case Type::TRASH:
                _pImpl->pSourceContainer->notifyFileRemoved(pFS);
            break;

            case Type::MOVE:
                _pImpl->pSourceContainer->notifyFileRemoved(pFS);
                _pImpl->pTargetContainer->notifyFileAdded(pFS);
            break;

            case Type::COPY:
                // pFS has the newly copied file, not the source file.
                _pImpl->pTargetContainer->notifyFileAdded(pFS);
            break;
        }
    }
    else
    {
        // Finished:
        Debug::Log(FILE_HIGH, "File ops item processed: NULL");

        // nullptr means everything done: then we need to destroy
        // all shared_ptr instances pointing to this, which will
        // invoke the destructor.

        auto pThis = shared_from_this();

        // 1) Remove us from the progress dialog, if one exists.
        if (_pImpl->_ppProgressDialog)
        {
            if (_strError.empty())
            {
                (*_pImpl->_ppProgressDialog)->updateOperation(pThis,
                                                              nullptr,
                                                              100);
                // 2) We are stored in the parent's queue of file operations; remove us there.
                size_t c = _refQueue.size();
                _refQueue.remove(pThis);
                if (_refQueue.size() != c - 1)
                    throw FSException("failed to remove fileops from list");
            }
            else
                (*_pImpl->_ppProgressDialog)->setError(pThis, _strError);
        }

        // 1) The Glib timer and dispatcher each have a lambda with a shared_ptr to
        //    this, which is really a functor with a copy of the shared_ptr.
        //    Disconnecting these two will free the functor and release the shared_ptr
        //    instances.
        _pImpl->connTimer.disconnect();
        _pImpl->connDispatch.disconnect();
        // Now the destructor has been called, and we're dead.
    }
}
