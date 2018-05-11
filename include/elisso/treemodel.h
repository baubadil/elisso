/*
 * elisso -- fast and friendly gtkmm file manager. (C) 2016--2017 Baubadil GmbH.
 *
 * elisso is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation, in version 2 as it comes
 * in the "LICENSE" file of the elisso main distribution. This program is distributed in the hope
 * that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the LICENSE file for more details.
 */

#ifndef ELISSO_TREEMODEL_H
#define ELISSO_TREEMODEL_H

#include <gtkmm.h>

#include "elisso/fsmodel_gio.h"

class FolderTreeModel;
typedef Glib::RefPtr<FolderTreeModel> PFolderTreeModel;


/***************************************************************************
 *
 *  FolderTreeModelColumns
 *
 **************************************************************************/

enum class TreeNodeState : uint8_t
{
    UNKNOWN,
    POPULATING,
    POPULATED_WITH_FIRST,
    POPULATED_WITH_FOLDERS,
    POPULATE_ERROR
};

class FolderTreeMonitor;
typedef std::shared_ptr<FolderTreeMonitor> PFolderTreeMonitor;

/**
 *  Conventional columns record as conventionally used with gtkmm
 *  to describe the columns exported by the FolderTreeModel.
 */
class FolderTreeModelColumns : public Gtk::TreeModelColumnRecord
{
public:
    FolderTreeModelColumns()
    {
        add(_colIconAndName);
    }

    Gtk::TreeModelColumn<Glib::ustring>             _colIconAndName;

    static FolderTreeModelColumns& Get()
    {
        if (!s_p)
            s_p = new FolderTreeModelColumns;
        return *s_p;
    }

private:
    static FolderTreeModelColumns *s_p;
};


/***************************************************************************
 *
 *  FolderTreeModelRow
 *
 **************************************************************************/

struct FolderTreeModelRow;
typedef std::shared_ptr<FolderTreeModelRow> PFolderTreeModelRow;
typedef std::vector<PFolderTreeModelRow> RowsVector;
typedef std::map<Glib::ustring, PFolderTreeModelRow> RowsMap;

/**
 *  One row in the FolderTreeModel. This is a public structure so we can access
 *  the model with functions other than the GTK TreeModel interface, for speed.
 *
 *  Every row has one field, which corresponds to the one column officially
 *  exported by the model ("name"). This is accessed by the TreeView control
 *  when it paints itself. In addition however, for speed, the lists and maps of
 *  children and monitors etc. can be accessed directly by Elisso.
 */
struct FolderTreeModelRow
{
    Glib::ustring             name;

    // Additional private data, not retrievable by GTK.
    unsigned                  overrideSort;         // To keep "home" sorted before "file system" etc.
    Glib::ustring             nameUpper;
    TreeNodeState             state;
    PFolderTreeModelRow       pParent;
    int                       uRowIndex2;
    int                       uRowIndexCopy;        // For when sorting.
    PFSModelBase              pDir;
    PFolderTreeMonitor        pMonitor;

    // The list of children. Additionally we maintain a map sorted
    // by file name to allow for looking up rows quickly. Note that
    // the "root" list and maps is not here, but in FolderTreeModel::Impl.
    RowsVector                vChildren;
    RowsMap                   mapChildren2;

    FolderTreeModelRow(PFolderTreeModelRow pParent_,
                       int uRowIndex_,
                       unsigned overrideSort_,
                       PFSModelBase pDir_,
                       const Glib::ustring &strName);

    int getIndex() const
    {
        return uRowIndex2;
    }

};


/***************************************************************************
 *
 *  FolderTreeModel
 *
 **************************************************************************/

/**
 *  Custom tree model that is used in the folders tree on the left of the elisso
 *  window. Originally we used Gtk::TreeStore but that turned out to be a bit slow.
 *  The main advantage of this model is that it provides methods for looking up
 *  nodes by file name quickly to find out whether a node is already in the tree.
 *  This is much faster than using the official TreeModel iterators.
 *
 *  Sorting works a bit differently from Gtk::TreeStore. Instead of setting a sort
 *  function on a column, there is a sort() method which sorts the children of one
 *  row once.
 */
class FolderTreeModel : public Gtk::TreeModel,
                        public Glib::Object
{
public:
    static PFolderTreeModel create();

    Gtk::TreeModelColumn<Glib::ustring>& get_model_column(int column);

    PFolderTreeModelRow append(PFolderTreeModelRow pParent,
                               unsigned overrideSort,
                               PFSModelBase pDir,
                               const Glib::ustring &strName);
    void rename(PFolderTreeModelRow pRow, const Glib::ustring &strNewName);
    void remove(PFolderTreeModelRow pParent, PFolderTreeModelRow pRemoveRow);
    void sort(PFolderTreeModelRow pParent);

    PFolderTreeModelRow findRow(PFolderTreeModelRow pParent, const Glib::ustring &strName);
    PFolderTreeModelRow findRow(const iterator &iter) const;
    int getPathInts(PFolderTreeModelRow pRow, int **pai) const;
    Gtk::TreePath getPath(PFolderTreeModelRow pRow) const;

protected:
    FolderTreeModel();
    virtual ~FolderTreeModel();

    // Overrides:
    virtual Gtk::TreeModelFlags get_flags_vfunc() const override;
    virtual int get_n_columns_vfunc() const override;
    virtual GType get_column_type_vfunc(int index) const override;

    virtual bool iter_next_vfunc(const iterator &iter, iterator &iterNext) const override;
    virtual bool get_iter_vfunc(const Path &path, iterator &iter) const override;
    virtual bool iter_children_vfunc(const iterator &parent, iterator &iter) const override;
    virtual bool iter_parent_vfunc(const iterator &child, iterator &iter) const override;
    virtual bool iter_nth_child_vfunc(const iterator &parent, int n, iterator &iter) const override;
    virtual int iter_n_root_children_vfunc() const override;

    virtual void get_value_vfunc(const TreeModel::iterator &iter, int column, Glib::ValueBase &value) const override;
    virtual void set_value_impl(const iterator &iter, int column, const Glib::ValueBase &value) override;

    virtual Path get_path_vfunc(const iterator &iter) const override;

    virtual bool iter_has_child_vfunc(const iterator &iter) const override;
    virtual int iter_n_children_vfunc(const iterator &iter) const override;
    virtual bool iter_nth_root_child_vfunc(int n, iterator &iter) const override;

private:
    bool isTreeIterValid(const iterator &iter) const;
    void makeIter(iterator &iter, FolderTreeModelRow *pParent, int rowIndex) const;
    GtkTreePath* makeCTreePath(PFolderTreeModelRow pRow, iterator &iter) const;

    struct Impl;
    Impl *_pImpl;
};

#endif
