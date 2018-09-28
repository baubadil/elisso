/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_PROGRESSDIALOG_H
#define ELISSO_PROGRESSDIALOG_H

#include <gtkmm.h>

#include <memory>

class FileOperation;
typedef std::shared_ptr<FileOperation> PFileOperation;

class FsObject;
typedef std::shared_ptr<FsObject> PFsObject;

class OperationRow;
typedef std::shared_ptr<OperationRow> POperationRow;


/***************************************************************************
 *
 *  ProgressDialog
 *
 **************************************************************************/

/**
 *
 */
class ProgressDialog : public Gtk::Window, public std::enable_shared_from_this<ProgressDialog>
{
public:
    /**
     *  Constructor.
     */
    ProgressDialog(Gtk::Window &wParent);

    /**
     *  Destructor.
     */
    virtual ~ProgressDialog();

    /**
     *  Adds a new operation to the progress dialog. The dialog's contents is a VBox,
     *  and this method adds another item to it. Each such item has a label and a
     *  progress bar.
     */
    void addOperation(PFileOperation pOp);

    /**
     *  Updates the operation that was previously added with addOperation
     *  with a new file and progress. This gets called periodically by
     *  FSOperation::onProgress().
     *
     *  As a special case, calling this with pFSCurrent == nullptr declares
     *  the operation finished and removes it from the dialog's VBox.
     */
    void updateOperation(PFileOperation pOp,
                         PFsObject pFSCurrent,
                         double dProgress);

    void setError(PFileOperation pOp, const Glib::ustring &strError);

private:
    friend class OperationRow;
    void removeOperationDone(POperationRow pRow, bool fManualClose);

    struct      Impl;
    Impl        *_pImpl;
    Gtk::Box    _vbox;
};

#endif // ELISSO_PROGRESSDIALOG_H

