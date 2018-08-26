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
 *  Globals
 *
 **************************************************************************/

uint g_lastOperationID = 0;


/***************************************************************************
 *
 *  FileSelection
 *
 **************************************************************************/

PFsGioFile
FileSelection::getTheOneSelectedFile()
{
    if (    (vFolders.size() == 0)
         && (vOthers.size() == 1)
       )
    {
        PFsObject pFS = vOthers.front();
        if (pFS)
        {
            auto t = pFS->getResolvedType();
            PFsGioFile pFile = g_pFsGioImpl->getFile(pFS, t);
            return pFile;       // can be nullptr
        }
    }

    return nullptr;
}


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
    FsContainer             *pSourceContainer = nullptr;
    // Target container for copy and move operations only.
    FsContainer             *pTargetContainer = nullptr;

    // Progress data. Protected by the parent WorkerResult mutex.
    PFsObject               pFSCurrent;
    double                  dProgress = 0;
};


/***************************************************************************
 *
 *  FileOperation
 *
 **************************************************************************/

/* static */
PFileOperation
FileOperation::Create(FileOperationType t,
                      const FSVector &vFiles,
                      PFsObject pTarget,                 //!< in: target directory or symlink to directory (required for copy or move, otherwise nullptr)
                      FileOperationsList &refQueue,         //!< in: list to append new FileOperation instance to
                      PProgressDialog *ppProgressDialog,    //!< in: progress dialog to append file operation to
                      Gtk::Window *pParentWindow)           //!< in: parent window for (modal) progress dialog
{
    /* This nasty trickery is necessary to make std::make_shared work with a protected constructor. */
    class Derived : public FileOperation
    {
    public:
        Derived(FileOperationType t, FileOperationsList &refQueue)
            : FileOperation(t, refQueue) { }
    };

    // Create the instance.
    auto pOp = make_shared<Derived>(t, refQueue);

    // Connect the dispatcher from the parent WorkerResult.
    pOp->_pImpl->connDispatch = pOp->connect([pOp]()
    {
        auto pFS = pOp->fetchResult();
        pOp->onProcessingNextItem(pFS);
    });

    // Instantiate a timer for progress reporting.
    pOp->_pImpl->connTimer = Glib::signal_timeout().connect([pOp]() -> bool
    {
        pOp->onProgress();
        return true;
    }, UPDATE_PROGRESS_MILLIS);

    // Enqueue *this in the caller's file operations list.
    refQueue.push_back(pOp);

    // If the parent has given us a pointer to a progress dialog pointer, update
    // or create the dialog therein.
    if ((pOp->_pImpl->_ppProgressDialog = ppProgressDialog))
    {
        if (!*ppProgressDialog)
            *ppProgressDialog = make_shared<ProgressDialog>(*pParentWindow);

        (*ppProgressDialog)->addOperation(pOp);
    }

    // Deep-copy the list of files to operate on.
    for (auto &pFS : vFiles)
    {
        pOp->_vFiles.push_back(pFS);
        auto pParent = pFS->getParent();
        if (!pParent)
            throw FSException("File has no parent");
        FsContainer *pContainerThis = pParent->getContainer();
        if (!pOp->_pImpl->pSourceContainer)
            pOp->_pImpl->pSourceContainer = pContainerThis;
        else if (pContainerThis != pOp->_pImpl->pSourceContainer)
            throw FSException("Files in given list have more than one parent container");
    }

    if ((pOp->_pTarget = pTarget))
        if (!(pOp->_pImpl->pTargetContainer = pTarget->getContainer()))
            throw FSException("Missing target container");

    // Launch the thread.
    XWP::Thread::Create([pOp]()
    {
        /*
         *  Thread function!
         */
        pOp->threadFunc();
    });

    return pOp;
}

FileOperation::FileOperation(FileOperationType t,
                             FileOperationsList  &refQueue)
    : _t(t),
       _id(++g_lastOperationID),
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
                Lock lock(_mutex);
                _pImpl->pFSCurrent = pFS;
                _pImpl->dProgress = (double)cCurrent / (double)cFiles;
            }

            // This is what gets posted to the GUI callback. This is in
            // a separate variable because it will be changed by COPY.
            PFsObject pFSForGUI(pFS);

            switch (_t)
            {
                case FileOperationType::TEST:
                    pFS->testFileOps();
                break;

                case FileOperationType::TRASH:
                    pFS->sendToTrash();
                break;

                case FileOperationType::MOVE:
                    pFS->moveTo(_pTarget);
                break;

                case FileOperationType::COPY:
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
    Lock lock(_mutex);
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
FileOperation::onProcessingNextItem(PFsObject pFS)
{
    if (pFS)
    {
        Debug::Log(FILE_HIGH, "File ops item processed: " + pFS->getPath());

        switch (_t)
        {
            case FileOperationType::TEST:
            break;

            case FileOperationType::TRASH:
                _pImpl->pSourceContainer->notifyFileRemoved(pFS);
            break;

            case FileOperationType::MOVE:
                _pImpl->pSourceContainer->notifyFileRemoved(pFS);
                _pImpl->pTargetContainer->notifyFileAdded(pFS);
            break;

            case FileOperationType::COPY:
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
