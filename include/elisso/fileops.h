/*
 * elisso (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_FILEOPS_H
#define ELISSO_FILEOPS_H

#include "elisso/fsmodel.h"
#include "elisso/worker.h"

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
    FSList llFolders;       // directories or symlinks to directories
    FSList llOthers;        // other files
    FSList llAll;           // both lists combined, in order
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
 *  This inherits from WorkerResult<PFSModelBase> so the worker thread can push file-system
 *  objects onto the member deque for file-system monitor processing on the GUI thread.
 */
class FileOperation : public WorkerResult<PFSModelBase>,
                      public enable_shared_from_this<FileOperation>
{
public:
    static const uint UPDATE_PROGRESS_MILLIS = 100;

    enum class Type
    {
        TEST,
        TRASH
    };

    static PFileOperation Create(Type t,
                                 const FileSelection &sel,
                                 FileOperationsList &refQueue,
                                 PProgressDialog *ppProgressDialog,
                                 Gtk::Window *pParentWindow);

    Type getType() const
    {
        return _t;
    }

    void cancel();

    const std::string& getError() const
    {
        return _strError;
    }

protected:
    FileOperation(Type t,
                  FileOperationsList  &refQueue);
    ~FileOperation();

    void threadFunc();

    void onProgress();
    void onItemProcessed(PFSModelBase pFS);

    Type                _t;
    StopFlag            _stopFlag;
    std::string         _strError;
    FileOperationsList  &_refQueue;
    FSList              _llFiles;
    struct Impl;
    Impl                *_pImpl;
};

#endif // ELISSO_FILEOPS_H
