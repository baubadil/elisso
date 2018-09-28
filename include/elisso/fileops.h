/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_FILEOPS_H
#define ELISSO_FILEOPS_H

#include "elisso/fsmodel_gio.h"
#include "elisso/worker.h"
#include "elisso/elisso.h"


/***************************************************************************
 *
 *  ElissoFolderView::Selection
 *
 **************************************************************************/

/**
 *  Lists the items that are currently selected in a folder view.
 *  If nothing is selected, all lists are empty.
 */
struct FileSelection
{
    FSVector vFolders;       // directories or symlinks to directories
    FSVector vOthers;        // other files
    FSVector vAll;           // both lists combined, in order

    /**
     *  Useful helper that returns a file if exactly one file is selected,
     *  or nullptr otherwise (if nothing is selected, or more than one item,
     *  or the one selected item isn't a file).
     */
    PFsGioFile getTheOneSelectedFile();
};


/***************************************************************************
 *
 *  FileOperation
 *
 **************************************************************************/

class FileOperation;
typedef std::shared_ptr<FileOperation> PFileOperation;
typedef std::list<PFileOperation> FileOperationsList;

class ProgressDialog;
typedef std::shared_ptr<ProgressDialog> PProgressDialog;

/**
 *  A FileOperation instance is a temporary object that is constructed from a FileSelection of
 *  file-system objects on which a file operation such as "trash" should be performed in a
 *  background thread.
 *
 *  The Create() factory method takes the FileSelection and the Type (e.g. Type::TRASH). It
 *  also adds the FileOperation to a given list which should reside in the caller's instance
 *  data so it can keep track whether file operations are going on.
 *
 *  Create() also spawns a background thread which actually modifies the given files. It
 *  creates a GUI dialog which displays progress, and all file-system monitors attached to
 *  containers are invoked correctly on the GUI thread.
 *
 *  This inherits from WorkerResult<PFsObject> so the worker thread can push file-system
 *  objects onto the member deque for file-system monitor processing on the GUI thread.
 */
class FileOperation : public WorkerResultQueue<PFsObject>,
                      public enable_shared_from_this<FileOperation>
{
public:
    static const uint UPDATE_PROGRESS_MILLIS = 100;

    /**
     *  Factory method which creates a shared_ptr<FileOperation> and starts the thread
     *  that operates on it.
     */
    static PFileOperation Create(FileOperationType t,
                                 const FSVector &vFiles,
                                 PFsObject pTarget,
                                 FileOperationsList &refQueue,
                                 PProgressDialog *ppProgressDialog,
                                 Gtk::Window *pParentWindow);

    FileOperationType getType() const
    {
        return _t;
    }

    uint getId() const
    {
        return _id;
    }

    /**
     *  Cancels the ongoing file operation by setting the worker thread's stop flag.
     *  This is for the implementation of a "Cancel" button in a progress dialog.
     */
    void cancel();

    const std::string& getError() const
    {
        return _strError;
    }

    void done(PFileOperation pThis);

protected:
    /**
     *  Protected constructor, only to be used by Create().
     */
    FileOperation(FileOperationType t,
                  FileOperationsList  &refQueue);
    ~FileOperation();

    /**
     *  Thread function. The std::thread gets spawned in Create() and simply calls this method.
     *  This operated on the files in _llFiles (given to Create()) depending on the operation
     *  type.
     *
     *  The semantics of the variables are:
     *
     *   -- FileOperationType::TEST: does nothing really. FsObject::testFileOps() only waits a little
     *      while for testing the progress dialog.
     *
     *   -- FileOperationType::TRASH: sends all files on the list to the desktop's trash can. This removes
     *      the files from all views they were inserted into; a target folder is not needed.
     *
     *   -- FileOperationType::MOVE: moves all files to the target folder given to the constructor.
     *      This removes the files from all views where they were inserted and inserts them
     *      into views of the target folder, if they are currently being monitored.
     *
     *   -- FileOperationType::COPY: copies all files to the target folder given to the constructor.
     *      This inserts the copies into views of the target folder.
     *
     *  This passes the source file pointer to postResultToGUI() except in the case of COPY,
     *  when this passes the copy.
     */
    void threadFunc();

    void onProgress();
    void onProcessingNextItem(PFsObject pFS);

    FileOperationType   _t;
    uint                _id;
    StopFlag            _stopFlag;
    std::string         _strError;
    FileOperationsList  &_refQueue;
    FSVector            _vFiles;
    PFsObject           _pTarget;
    struct Impl;
    Impl                *_pImpl;
};

#endif // ELISSO_FILEOPS_H
