
/* gtkmm example Copyright (C) 2002 gtkmm development team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */

#include <iostream>
#include "elisso/treemodel.h"
#include "elisso/elisso.h"
#include "xwp/debug.h"
#include "xwp/except.h"
#include "xwp/stringhelp.h"
#include <cassert>


/***************************************************************************
 *
 *  Globals
 *
 **************************************************************************/

FolderTreeModelColumns* FolderTreeModelColumns::s_p = nullptr;


/***************************************************************************
 *
 *  FolderTreeModelRow
 *
 **************************************************************************/

FolderTreeModelRow::FolderTreeModelRow(PFolderTreeModelRow pParent_,
                                       int uRowIndex_,
                                       unsigned overrideSort_,
                                       PFSModelBase pDir_,
                                       const Glib::ustring &strName)
    : name(strName),
      overrideSort(overrideSort_),
      state(TreeNodeState::UNKNOWN),
      pParent(pParent_),
      uRowIndex2(uRowIndex_),
      uRowIndexCopy(uRowIndex_),
      pDir(pDir_)
{
    nameUpper = strToUpper(name);
}


/***************************************************************************
 *
 *  FolderTreeModel::Impl
 *
 **************************************************************************/

struct FolderTreeModel::Impl
{
    int                     stamp = 1;

    RowsVector              vRows;
    RowsMap                 mapRows2;
};


/***************************************************************************
 *
 *  FolderTreeModel
 *
 **************************************************************************/

/**
 *  Static factory method to create a new managed refpointer instance.
 */
/* static */
PFolderTreeModel
FolderTreeModel::create()
{
    return PFolderTreeModel(new FolderTreeModel);
}

/**
 *  Protected constructor.
 */
FolderTreeModel::FolderTreeModel()
    : Glib::ObjectBase(typeid(FolderTreeModel)),
      Glib::Object(),
      _pImpl(new Impl)
{
}

/**
 *  Protected destructor.
 */
FolderTreeModel::~FolderTreeModel()
{
}

/**
 *  Debug function.
 */
void dumpModel(RowsVector &v,
               int iLevel = 0)
{
    if (v.size())
    {
        Debug::Enter(TREEMODEL, "Dumping model level " + to_string(iLevel));
        for (auto &pRow : v)
        {
            Debug::Log(TREEMODEL, pRow->pDir->getBasename());
            dumpModel(pRow->vChildren, iLevel + 1);
        }
        Debug::Leave();
    }
}

/**
 *  Public method to append a new row to the model, either at the root level
 *  (if pParent is nullptr) or as a child of pParent.
 *
 *  The new node will be appended to the end of the list without sorting.
 *  To have all the children of a parent node sorted, call sort() after
 *  you're done inserting nodes under the same parent; this is much more
 *  efficient than sorting after every insertion.
 *
 *  The new node receives its title automatically from the given directory.
 *  That basename will also be inserted into an internal map so that
 *  findRow() can look up nodes from a filename efficiently.
 *
 *  This emits the "row-inserted" signal.
 */
PFolderTreeModelRow
FolderTreeModel::append(PFolderTreeModelRow pParent,
                        unsigned overrideSort,
                        PFSModelBase pDir,
                        const Glib::ustring &strName)
{
    // Prevent duplicates. If it exists, return the existing.
    auto p = findRow(pParent, strName);
    if (p)
        return p;

    unsigned uRowIndex = pParent ? pParent->vChildren.size() : _pImpl->vRows.size();
    PFolderTreeModelRow pNewRow = make_shared<FolderTreeModelRow>(pParent,
                                                                  uRowIndex,
                                                                  overrideSort,
                                                                  pDir,
                                                                  strName);

    if (pParent)
    {
        pParent->vChildren.push_back(pNewRow);
        pParent->mapChildren2[pNewRow->name] = pNewRow;
    }
    else
    {
        _pImpl->vRows.push_back(pNewRow);
        _pImpl->mapRows2[pNewRow->name] = pNewRow;
    }

    // Fire the "row-inserted" signal.
    iterator iter;
    auto pPath = makeCTreePath(pNewRow, iter);
    if (pPath)
    {
        gtk_tree_model_row_inserted(gobj(),
                                    pPath,
                                    iter.gobj());
        gtk_tree_path_free(pPath);
    }

//     Debug::Enter(TREEMODEL, "Dumping model after append");
//     dumpModel(_pImpl->mapRows);
//     Debug::Leave();

    // Invalidate all iterators.
    _pImpl->stamp++;

    return pNewRow;
}

/**
 *  Changes the "name" column in the given row. Does NOT call sort() automatically;
 *  the caller should do that.
 *
 *  This emits the "row-changed" signal.
 */
void
FolderTreeModel::rename(PFolderTreeModelRow pRow,
                        const Glib::ustring &strNewName)
{
    pRow->name = strNewName;
    pRow->nameUpper = strToUpper(strNewName);

    iterator iter;
    auto pPath = makeCTreePath(pRow, iter);
    if (pPath)
    {
        gtk_tree_model_row_changed(gobj(),
                                   pPath,
                                   iter.gobj());
        gtk_tree_path_free(pPath);
    }
}

/**
 *  Removes the given row from the given parent (or from the root list of nodes,
 *  if pParent is nullptr).
 *
 *  This emits the "row-deleted" signal.
 */
void
FolderTreeModel::remove(PFolderTreeModelRow pParent,            //!<in: or nullptr
                        PFolderTreeModelRow pRemoveRow)
{
    // Get the path for the signal first, before we remove the row.
    iterator iter;
    auto pPath = makeCTreePath(pRemoveRow, iter);

    // First remove it from the map.
    RowsMap &m = (pParent) ? pParent->mapChildren2 : _pImpl->mapRows2;
    auto itM = m.find(pRemoveRow->name);
    assert(itM != m.end());
    m.erase(itM);

    // Then remove it from the array and renumber all subsequent items.
    auto iRowIndex = pRemoveRow->getIndex();
    RowsVector &v = (pParent) ? pParent->vChildren : _pImpl->vRows;
    RowsVector::iterator itV = v.begin() + iRowIndex;
    v.erase(itV);

    // Using the same row index again to point to the element that follows the one being removed.
    itV = v.begin() + iRowIndex;
    while (itV != v.end())
    {
        auto pRow = *itV;
        --pRow->uRowIndex2;
        --pRow->uRowIndexCopy;
        itV++;
    }

    // Fire the "row-deleted" signal with the path we created above.
    if (pPath)
    {
        gtk_tree_model_row_deleted(gobj(),
                                   pPath);

        // If the deleted node was the only child then we also need to fire
        // "row-has-child-toggled" to remove the expander icon.
        if (pParent)
            if (pParent->vChildren.size() == 0)
            {
                gtk_tree_path_up(pPath);
                makeIter(iter,
                         (pParent->pParent) ? &*pParent->pParent : nullptr,
                         pParent->getIndex());
                gtk_tree_model_row_has_child_toggled(gobj(),
                                                     pPath,
                                                     iter.gobj());
            }

        gtk_tree_path_free(pPath);
    }
}

/**
 *  Sorts the children under the given parent (or the root nodes, if nullptr) once.
 *  This is different from Gtk::TreeStore which supports the idea of a "sort function
 *  for a column".
 *
 *  Our method is more efficient for our model where we insert a lot of
 *  nodes in bursts under one parent, and only that parent should be sorted, instead of
 *  having a permanent sort function active all the time that keeps sorting things
 *  when there isn't much to sort.
 *
 *  This emits the "rows-reordered" signal.
 */
void
FolderTreeModel::sort(PFolderTreeModelRow pParent)
{
    // Can only sort subtrees.
    if (!pParent)
        return;

    RowsVector &v = pParent->vChildren;

    // Allocate an array of ints for the "rows-reordered" signal.
    std::unique_ptr<int[]> paiNewOrder(new int[v.size()]);
        // This will correctly call delete[]; https://stackoverflow.com/questions/13061979/shared-ptr-to-an-array-should-it-be-used

    // This is our magic sort function. We sort by name, case-insensitively,
    // unless the rows have a different "sort" integer value, which takes
    // priority. These are only set for the root nodes, so everything else
    // gets sorted by name.
    std::sort(v.begin(), v.end(), [](const PFolderTreeModelRow &pA,
                                     const PFolderTreeModelRow &pB)
    {
        if (pA->overrideSort == pB->overrideSort)
            return (pA->nameUpper.compare(pB->nameUpper) < 0);
        return (pA->overrideSort < pB->overrideSort);
    });

    // Housekeeping: update all integer indices in the rows, which may all have changed,
    // and build the array of its for the "rows-reordered" signal, which must be in
    // "new_order[newpos] = oldpos" format.
    unsigned u = 0;
    for (auto &pRow : v)
    {
//         Debug::Log(TREEMODEL, to_string(u) + " => " + quote(pRow->name) + " (" + to_string(pRow->overrideSort) + ")");

        pRow->uRowIndex2 = u;
        paiNewOrder[u] = pRow->uRowIndexCopy;
        pRow->uRowIndexCopy = u;
        ++u;
    }

    // Now emit the "rows-reordered" signal.
    iterator iter;
    auto pPath = makeCTreePath(pParent, iter);
    if (pPath)
    {
        gtk_tree_model_rows_reordered(gobj(),
                                      pPath,
                                      iter.gobj(),
                                      paiNewOrder.get());
        gtk_tree_path_free(pPath);
    }

    // Invalidate all iterators.
    _pImpl->stamp++;
}

/**
 *  Public interface to allow for quickly looking up a row by file name. Returns nullptr
 *  if no row exists for the name.
 *
 *  This function is the main reason we implement our own tree model. This is much quicker
 *  than iterating through all the nodes with the official tree model interfaces.
 */
PFolderTreeModelRow
FolderTreeModel::findRow(PFolderTreeModelRow pParent,
                         const Glib::ustring &strName)
{
    RowsMap &m = (pParent) ? pParent->mapChildren2 : _pImpl->mapRows2;
    auto it = m.find(strName);
    if (it != m.end())
        return it->second;

    return nullptr;
}

/**
 *  Second interface to look up a row for the given iterator. This is used a lot internally
 *  as well from the public TreeView interface implementations.
 */
PFolderTreeModelRow
FolderTreeModel::findRow(const iterator &iter) const
{
    PFolderTreeModelRow pReturn;

    Glib::ustring strName = "INVALID";
    Glib::ustring strResult = "ERROR";

    if (isTreeIterValid(iter))
    {
        FolderTreeModelRow *pParent = (FolderTreeModelRow*)iter.gobj()->user_data;
        int iRowNumber = (int)(size_t)(iter.gobj()->user_data2);
        RowsVector &v = (pParent) ? pParent->vChildren : _pImpl->vRows;

        strName = "parent=" + (pParent ? quote(pParent->name) : "NULL") + ":" + to_string(iRowNumber);

        if (iRowNumber < (int)v.size())
        {
            auto it = v.begin();
            std::advance(it, iRowNumber);

            pReturn = *it;
            strResult = quote(pReturn->name);
        }
    }

    Debug::Log(TREEMODEL, string(__func__) + "(" + strName + ") => " + strResult);

    return pReturn;
}

int
FolderTreeModel::getPathInts(PFolderTreeModelRow pRow, int **ppai) const
{
    std::list<int> lli = { pRow->getIndex() };
    PFolderTreeModelRow pRow2 = pRow->pParent;
    while (pRow2)
    {
        lli.push_front(pRow2->getIndex());
        pRow2 = pRow2->pParent;
    }

    int z = lli.size();
    if ((*ppai = (int*)malloc(sizeof(int) * z)))
    {
        StringVector svDebug;

        int *p = *ppai;
        for (auto &i : lli)
        {
            *p++ = i;
            svDebug.push_back(to_string(i));
        }

        Debug::Log(TREEMODEL, string(__func__) + "(" + quote(pRow->pDir->getBasename()) + ") => " + quote(implode(":", svDebug)));

        return z;
    }

    return 0;
}

Gtk::TreePath
FolderTreeModel::getPath(PFolderTreeModelRow pRow) const
{
    int *pai;
    int z;
    if ((z = getPathInts(pRow, &pai)))
    {
        auto pPath = gtk_tree_path_new_from_indicesv(pai, z);
        free(pai);
        return Glib::wrap(pPath);
    }

    return Path();
}

/**
 *  TreeModel vfunc implementation: returns a set of flags supported by this interface.
 *  The flags are a bitwise combination of Gtk::TreeModelFlags. The flags supported should not
 *  change during the lifetime of the tree_model.
 *
 *  Flags are:
 *   -- TREE_MODEL_ITERS_PERSIST Iterators survive all signals emitted by the tree.
 *   -- TREE_MODEL_LIST_ONLY The model is a list only, and never has children.
 */
/* virtual */
Gtk::TreeModelFlags
FolderTreeModel::get_flags_vfunc() const /* override */
{
    return Gtk::TreeModelFlags(0);
}

/**
 *  TreeModel vfunc implementation. Returns the number of columns supported by tree_model.
 */
/* virtual */
int
FolderTreeModel::get_n_columns_vfunc() const /* override */
{
    auto &cols = FolderTreeModelColumns::Get();
    return cols.size();
}

/**
 *  TreeModel vfunc implementation. Returns the type of the column.
 */
/* virtual */
GType
FolderTreeModel::get_column_type_vfunc(int index) const /* override */
{
    auto &cols = FolderTreeModelColumns::Get();
    if (index <= (int)cols.size())
    {
        auto &cols = FolderTreeModelColumns::Get();
        return cols.types()[index];
    }

    return 0;
}

/**
 *  TreeModel vfunc implementation. Copies the value of the given row and column to the given
 *  Glib::ValueBase buffer.
 */
/* virtual */
void
FolderTreeModel::get_value_vfunc(const TreeModel::iterator &iter,
                                 int column,
                                 Glib::ValueBase &value) const /* override */
{
    Debug::Enter(TREEMODEL, __func__);

    Glib::ustring strName = "INVALID";
    Glib::ustring strResult = "FALSE";

    auto pRow = findRow(iter);
    if (pRow)
    {
        strName = quote(pRow->name);

        auto &cols = FolderTreeModelColumns::Get();
        if (column <= (int)cols.size())
        {
            GType type = cols.types()[column];
            value.init(type);

            switch (column)
            {
                case 0: // cols._colIconAndName:
                    // The C++ interfaces for GValue are a bit complicated, let's just use the C call.
                    g_value_set_string(value.gobj(), pRow->name.c_str());
                    strResult = quote(pRow->name);
                break;
            }
        }
    }

    Debug::Leave(strName + " => " + strResult);
}

/**
 *  gtkmm docs say to override and implement this in a derived TreeModel class, so
 *  that Row::operator() and Row::set_value() work.
 *  We have only once column, so we call rename(), which emits the "row-changed" signal.
 *  This method is not used however since our folder tree code calls rename() directly.
 */
/* virtual */
void
FolderTreeModel::set_value_impl(const iterator &iter,
                                int column,
                                const Glib::ValueBase &value) /* override */
{
    Debug::Enter(TREEMODEL, __func__);

    Glib::ustring strName = "INVALID";
    Glib::ustring strResult = "FALSE";

    auto pRow = findRow(iter);
    if (pRow)
    {
        strName = quote(pRow->name);

        if (column == 0)
        {
            Glib::ustring str(g_value_get_string(value.gobj()));
            rename(pRow, str);
            strResult = quote(str);
        }
    }

    Debug::Leave(strName + " => " + strResult);
}

/**
 *  TreeModel vfunc implementation. Sets iter_next to refer to the node following iter it at the current
 *  level. If there is no next iter, false is returned and iter_next is set to be invalid.
 */
/* virtual */
bool
FolderTreeModel::iter_next_vfunc(const iterator &iter,
                                 iterator &iterNext) const /* override */
{
    bool rc = false;

    iterNext = iterator();

    Glib::ustring strName = "INVALID";
    Glib::ustring strResult = "FALSE";

    if (isTreeIterValid(iter))
    {
        FolderTreeModelRow *pParent = (FolderTreeModelRow*)iter.gobj()->user_data;
        int iRowNumber = (int)(size_t)(iter.gobj()->user_data2);
        RowsVector &v = (pParent) ? pParent->vChildren : _pImpl->vRows;

        strName = (pParent ? quote(pParent->name) : "NULL" ) + ":" + to_string(iRowNumber);

        iRowNumber++;
        if (iRowNumber < (int)v.size())
        {
            makeIter(iterNext, pParent, iRowNumber);

            strResult = to_string(iRowNumber);
            rc = true;
        }
    }

    Debug::Log(TREEMODEL, string(__func__) + "(" + strName + ") => " + strResult);

    return rc;
}

/**
 *  TreeModel vfunc implementation. Sets iter to refer to the first child of parent.
 *  If parent has no children, false is returned and iter is set to be invalid.
 */
/* virtual */
bool
FolderTreeModel::iter_children_vfunc(const iterator &parent, iterator &iter) const /* override */
{
    return iter_nth_child_vfunc(parent, 0, iter);
}

/**
 *  TreeModel vfunc implementation. Returns true if iter has children, false otherwise.
 */
/* virtual */
bool
FolderTreeModel::iter_has_child_vfunc(const iterator &iter) const /* override */
{
    return (iter_n_children_vfunc(iter) > 0);
}

/**
 *  TreeModel vfunc implementation. Returns the number of children that iter has. See also iter_n_root_children_vfunc().
 */
/* virtual */
int
FolderTreeModel::iter_n_children_vfunc(const iterator &iter) const /* override */
{
    int z = 0;
    Debug::Enter(TREEMODEL, __func__);

    PFolderTreeModelRow pRow = findRow(iter);
    if (pRow)
    {
        Debug::Log(TREEMODEL, string(__func__) + "(" + quote(pRow->pDir->getBasename()) + ") => " + to_string(pRow->vChildren.size()) + " children");
        z = pRow->vChildren.size();
    }

    Debug::Leave("returning " + to_string(z));
    return z;
}

/**
 *  TreeModel vfunc implementation. Returns the number of columns supported by tree_model.
 */
/* virtual */
int
FolderTreeModel::iter_n_root_children_vfunc() const /* override */
{
    return _pImpl->vRows.size();
}

/**
 *  TreeModel vfunc implementation. Sets iter to be the child of parent using the given index.
 *  The first index is 0. If n is too big, or parent has no children, iter is set to an invalid
 *  iterator and false is returned. See also iter_nth_root_child_vfunc().
 */
/* virtual */
bool
FolderTreeModel::iter_nth_child_vfunc(const iterator &parent,
                                      int n,
                                      iterator &iter) const /* override */
{
    bool rc = false;
    Debug::Enter(TREEMODEL, __func__);

    iter = iterator();

    Glib::ustring strName("INVALID");
    Glib::ustring strResult("FALSE");

    auto pParentRow = findRow(parent);
    if (pParentRow)
    {
        strName = quote(pParentRow->name);

        if (n < (int)pParentRow->vChildren.size())
        {
            RowsVector::const_iterator it3 = pParentRow->vChildren.begin();
            std::advance(it3, n);
            auto pRowResult = *it3;
            makeIter(iter, &*pParentRow, n);

            strResult = quote(pRowResult->name) + "==" + pParentRow->name + ":" + to_string(n);
            rc = true;
        }
    }

    Debug::Leave(strName + " => " + strResult);
    return rc;
}

/**
 *  TreeModel vfunc implementation. Sets iter to be the child of at the root level using the given
 *  index. The first index is 0. If n is too big, or if there are no children, iter is set to an
 *  invalid iterator and false is returned. See also iter_nth_child_vfunc().
 */
/* virtual */
bool
FolderTreeModel::iter_nth_root_child_vfunc(int n, iterator &iter) const /* override */
{
    bool rc = false;
    Debug::Enter(TREEMODEL, __func__);

    iter = iterator();

    if (n < (int)_pImpl->vRows.size())
    {
        makeIter(iter,
                 nullptr,         // parent
                 n);
        rc = true;
    }

    Debug::Leave();
    return rc;
}

/**
 *  TreeModel vfunc implementation. Sets iter to be the parent of child. If child is at the toplevel,
 *  and doesn't have a parent, then iter is set to an invalid iterator and false is returned.
 */
/* virtual */
bool
FolderTreeModel::iter_parent_vfunc(const iterator &child, iterator &iter) const /* override */
{
    bool rc = true;
    Debug::Enter(TREEMODEL, __func__);
    iter = iterator();

    Glib::ustring strName("INVALID");
    Glib::ustring strResult("FALSE");

    auto pRow = findRow(child);
    if (pRow)
    {
        strName = quote(pRow->name);

        if (pRow->pParent)
        {
            auto pRowResult = pRow->pParent;
            FolderTreeModelRow *pGrandParent = (pRowResult->pParent) ? &(*pRowResult->pParent) : nullptr;
            int iRowNumber = pRowResult->getIndex();
            makeIter(iter, pGrandParent, iRowNumber);

            strResult = quote(pRowResult->name) + "==" + (pGrandParent ? pGrandParent->name : "NULL") + ":" + to_string(iRowNumber);
            rc = true;
        }
    }

    Debug::Leave(strName + " => " + strResult);

    return rc;
}

/**
 *  TreeModel vfunc implementation. Returns a Path referenced by iter.
 */
/* virtual */
Gtk::TreeModel::Path
FolderTreeModel::get_path_vfunc(const iterator &iter) const /* override */
{
    Debug::Enter(TREEMODEL, __func__);

    Path p;

    auto pRow = findRow(iter);
    if (pRow)
        p = getPath(pRow);

    Debug::Leave();

    return p;

}

/**
 *  TreeModel vfunc implementation. Sets iter to a valid iterator pointing to path
 */
/* virtual */
bool
FolderTreeModel::get_iter_vfunc(const Path &path, iterator &iter) const /* override */
{
    Debug::Enter(TREEMODEL, __func__);
    bool rc = false;

    iter = iterator();

    PFolderTreeModelRow pRow;
    gint iDepth;
    gint *pai = gtk_tree_path_get_indices_with_depth((GtkTreePath*)path.gobj(),
                                                     &iDepth);
    if (pai)
    {
        for (int u = 0; u < iDepth; ++u)
        {
            RowsVector &v = (pRow) ? pRow->vChildren : _pImpl->vRows;

            gint idx = pai[u];
            if (idx < (int)v.size())
            {
                auto it = v.begin();
                std::advance(it, idx);
                pRow = *it;

                Debug::Log(TREEMODEL, "path-component " + to_string(idx) + " => " + quote(pRow->name));
            }
            else
            {
                pRow = nullptr;
                Debug::Log(TREEMODEL, "path-component " + to_string(idx) + " is INVALID");
                break;
            }
        }
    }

    if (pRow)
    {
        FolderTreeModelRow *pParent = (pRow->pParent) ? &(*pRow->pParent) : nullptr;
        makeIter(iter, pParent, pRow->getIndex());
        rc = true;
    }

    Debug::Leave("returning " + string((rc) ? "true" : "false"));
    return rc;
}

bool
FolderTreeModel::isTreeIterValid(const iterator &iter) const
{
    return _pImpl->stamp == iter.get_stamp();
}

void
FolderTreeModel::makeIter(iterator &iter,
                          FolderTreeModelRow *pParent,
                          int rowIndex) const
{
    iter.set_stamp(_pImpl->stamp);
    iter.gobj()->user_data = (void*)pParent;
    iter.gobj()->user_data2 = (void*)(size_t)rowIndex;
}

/**
 *  Creates a C GtkTreePath instance. Caller must free() with gtk_tree_path_free().
 */
GtkTreePath*
FolderTreeModel::makeCTreePath(PFolderTreeModelRow pRow,
                               iterator &iter) const
{
    int *pai;
    int z;
    if ((z = getPathInts(pRow, &pai)))
    {
        auto pPath = gtk_tree_path_new_from_indicesv(pai, z);
        FolderTreeModelRow *pParent = (pRow->pParent) ? &(*pRow->pParent) : nullptr;
        free(pai);

        makeIter(iter, pParent, pRow->getIndex());
        return pPath;
    }

    return nullptr;
}
