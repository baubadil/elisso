
/*
 * elisso (C) 2016--2017 Baubadil GmbH.
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

class FSModelBase;
typedef std::shared_ptr<FSModelBase> PFSModelBase;

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
    ProgressDialog(Gtk::Window &wParent);
    ~ProgressDialog();

    void addOperation(PFileOperation pOp);
    void updateOperation(PFileOperation pOp,
                         PFSModelBase pFSCurrent,
                         double dProgress);

private:
    void removeOperationDone(POperationRow pRow);

    struct      Impl;
    Impl        *_pImpl;
    Gtk::Box    _vbox;
};

#endif // ELISSO_PROGRESSDIALOG_H

