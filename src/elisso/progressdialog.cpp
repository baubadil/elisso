/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#include "elisso/progressdialog.h"

#include "elisso/elisso.h"
#include "elisso/fileops.h"

#include "xwp/debug.h"
#include "xwp/except.h"

class OperationRow;
typedef std::shared_ptr<OperationRow> POperationRow;
typedef std::list<POperationRow> OperationRowsList;


/***************************************************************************
 *
 *  ProgressDialog::Impl
 *
 **************************************************************************/

struct ProgressDialog::Impl
{
    OperationRowsList                       llOpRows;
    std::map<uint, POperationRow> mapOpRows;
};


/***************************************************************************
 *
 *  OperationRow
 *
 **************************************************************************/

/**
 *  Each OperationRow consists of a label, a progress bar, and a "cancel" button.
 */
class OperationRow : public Gtk::Frame,
                     public enable_shared_from_this<OperationRow>
{
public:
    OperationRow(ProgressDialog &dlg,
                 PFileOperation pOp)
        : _parentDlg(dlg),
          _pOp(pOp),
          _boxMain(Gtk::Orientation::ORIENTATION_VERTICAL)
    {
        // Width of the border around the frame.
        set_border_width(5);

        Glib::ustring str = getDescription();
        dlg.set_title(str);

        // Spacing between children of the box.
        _boxMain.set_spacing(5);
        // Spacing outside the box (inside the frame).
        _boxMain.property_margin() = 10;

        Glib::ustring strLabel(Glib::Markup::escape_text(str));
        _label.set_width_chars(50);
        _label.set_max_width_chars(200);
        _label.set_markup(strLabel);

        _boxMain.pack_start(_label, false, false);

        _boxProgressAndCancel.set_spacing(5);
        _progressBar.set_show_text(true);
        _cancelButton.set_label("Cancel");
        _cancelButton.signal_clicked().connect([this]()
        {
            if (_pOp)
            {
                if (!_pOp->getError().empty())
                    _parentDlg.removeOperationDone(shared_from_this());
                else
                    _pOp->cancel();
            }
        });
        _boxProgressAndCancel.pack_start(_progressBar, true, true);
        _boxProgressAndCancel.pack_start(_cancelButton, false, false);

        _boxMain.pack_start(_boxProgressAndCancel, false, false);

        add(_boxMain);
    }

    void
    update(PFsObject pfsCurrent,
           double dProgress)
    {
        Debug::Log(PROGRESSDIALOG, __func__);
        if (pfsCurrent)
        {
            if (pfsCurrent != _pfsLast)
            {
                _pfsLast = pfsCurrent;

                switch (_pOp->getType())
                {
                    case FileOperationType::TEST:
                        _label.set_markup("Testing <b>" + Glib::Markup::escape_text(pfsCurrent->getBasename()) + "</b>");
                    break;

                    case FileOperationType::TRASH:
                        _label.set_markup("Sending <b>" + Glib::Markup::escape_text(pfsCurrent->getBasename()) + "</b> to trash");
                    break;

                    case FileOperationType::MOVE:
                        _label.set_markup("Moving <b>" + Glib::Markup::escape_text(pfsCurrent->getBasename()) + "</b>");
                    break;

                    case FileOperationType::COPY:
                        _label.set_markup("Copying <b>" + Glib::Markup::escape_text(pfsCurrent->getBasename()) + "</b>");
                    break;
                }
            }
        }
        else
        {
            std::string strDescription = getDescription();

            auto strError = _pOp->getError();
            if (strError.length())
                strDescription += ": " + strError;
            else
            {
                _cancelButton.set_label("OK!");
//                 auto pContext = get_style_context();
//                 pContext->add_class("file-ops-success");
            }

            _label.set_text(strDescription);
        }

        if (dProgress != _dProgressLast)
        {
            _dProgressLast = dProgress;
            _progressBar.set_fraction(dProgress);
        }
    }

    void setError(const Glib::ustring &strError)
    {
        Debug::Log(PROGRESSDIALOG, __func__);
        _label.set_markup("<b>Error:</b> " + Glib::Markup::escape_text(strError));
        _cancelButton.set_label("Close");
    }

    Glib::ustring
    getDescription()
    {
        switch (_pOp->getType())
        {
            case FileOperationType::TEST:
                return "Testing file operations";

            case FileOperationType::TRASH:
                return "Sending files to trash";

            case FileOperationType::MOVE:
                return "Moving files";

            case FileOperationType::COPY:
                return "Copying files";
        }

        return "";
    }

    ProgressDialog      &_parentDlg;
    PFileOperation      _pOp;

    Gtk::Box            _boxMain;
    Gtk::Label          _label;
    Gtk::Box            _boxProgressAndCancel;
    Gtk::ProgressBar    _progressBar;
    Gtk::Button         _cancelButton;

    PFsObject           _pfsLast;
    double              _dProgressLast = -1;
};

/***************************************************************************
 *
 *  ProgressDialog
 *
 **************************************************************************/

ProgressDialog::ProgressDialog(Gtk::Window &wParent)
    : _pImpl(new Impl),
      _vbox(Gtk::Orientation::ORIENTATION_VERTICAL)
{
    set_border_width(5);

    set_size_request(50, -1);

    set_type_hint(Gdk::WindowTypeHint::WINDOW_TYPE_HINT_DIALOG);
    set_transient_for(wParent);

    add(_vbox);

    show_all();

    // Give us the focus.
    present();
}

/* virtual */
ProgressDialog::~ProgressDialog()
{
    delete _pImpl;
}

void
ProgressDialog::addOperation(PFileOperation pOp)
{
    auto pOpBox = std::make_shared<OperationRow>(*this, pOp);
    // Store in list for sequence.
    _pImpl->llOpRows.push_back(pOpBox);
    // Store in map for lookup.
    _pImpl->mapOpRows[pOp->getId()] = pOpBox;

    _vbox.pack_start(*pOpBox, false, false, 5);
    show_all();
}

void
ProgressDialog::updateOperation(PFileOperation pOp,
                                PFsObject pFSCurrent,
                                double dProgress)
{
    auto it1 = _pImpl->mapOpRows.find(pOp->getId());
    if (it1 == _pImpl->mapOpRows.end())
        throw FSException("Cannot find file operation box in map");
    // Make a copy of the pointer before removing the item from the map.
    POperationRow pOpBox = it1->second;

    if (pFSCurrent)
        pOpBox->update(pFSCurrent, dProgress);
    else
    {
        // Operation done:
        // Remove the item from the map.
        pOpBox->update(nullptr, 100);

        Glib::signal_timeout().connect([this, pOpBox]() -> bool
        {
            this->removeOperationDone(pOpBox);
            return false;       // disconnect
        }, 1000);
    }
}

void
ProgressDialog::setError(PFileOperation pOp, const Glib::ustring &strError)
{
    auto it1 = _pImpl->mapOpRows.find(pOp->getId());
    if (it1 == _pImpl->mapOpRows.end())
        throw FSException("Cannot find file operation box in map");

    POperationRow pOpBox = it1->second;
    pOpBox->setError(strError);
}

void
ProgressDialog::removeOperationDone(POperationRow pRow)
{
    Debug d(DEBUG_ALWAYS, __func__);

    uint id = pRow->_pOp->getId();

    // Remove the item from the list.
    auto it2 = std::find(_pImpl->llOpRows.begin(), _pImpl->llOpRows.end(), pRow);
    if (it2 == _pImpl->llOpRows.end())
        throw FSException("Cannot find file operation box in list");
    _pImpl->llOpRows.erase(it2);

    auto it3 = _pImpl->mapOpRows.find(id);
    if (it3 == _pImpl->mapOpRows.end())
        throw FSException("Cannot find file operation box in map");

    _pImpl->mapOpRows.erase(it3);
        // This releases the last reference, destroys the row and removes it from this VBox automatically.

    if (_pImpl->llOpRows.empty())
        hide();
}
