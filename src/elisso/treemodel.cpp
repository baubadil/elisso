
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
                                       unsigned sort_,
                                       PFSModelBase pDir_)
    : sort(sort_),
      name(pDir_->getBasename()),
      state(TreeNodeState::UNKNOWN),
      pParent(pParent_),
      uRowIndex(uRowIndex_),
      pDir(pDir_)
{
}


/***************************************************************************
 *
 *  FolderTreeModel::Impl
 *
 **************************************************************************/

struct FolderTreeModel::Impl
{
    int                     stamp = 1;
    RowsMap                 mapRows;
};


/***************************************************************************
 *
 *  FolderTreeModel
 *
 **************************************************************************/

/* static */
PFolderTreeModel
FolderTreeModel::create()
{
    return PFolderTreeModel(new FolderTreeModel);
}

FolderTreeModel::FolderTreeModel()
    : Glib::ObjectBase(typeid(FolderTreeModel)),
      Glib::Object(),
      _pImpl(new Impl)
{
}

FolderTreeModel::~FolderTreeModel()
{
}

void dumpModel(RowsMap &m, int iLevel = 0)
{
    if (m.size())
    {
        Debug::Enter(TREEMODEL, "Dumping model level " + to_string(iLevel));
        for (auto &it : m)
        {
            PFolderTreeModelRow pRow = it.second;
            Debug::Log(TREEMODEL, pRow->pDir->getBasename());
            dumpModel(pRow->mapChildren, iLevel + 1);
        }
        Debug::Leave();
    }
}

PFolderTreeModelRow
FolderTreeModel::append(PFolderTreeModelRow pParent,
                        unsigned sort,
                        PFSModelBase pDir)
{
    unsigned uRowIndex = pParent ? pParent->mapChildren.size() : _pImpl->mapRows.size();

    PFolderTreeModelRow pNewRow = make_shared<FolderTreeModelRow>(pParent,
                                                                  uRowIndex,
                                                                  sort,
                                                                  pDir);

    if (pParent)
        pParent->mapChildren[pNewRow->name] = pNewRow;
    else
        _pImpl->mapRows[pNewRow->name] = pNewRow;

//     auto path = getPath(pNewRow);
//     row_inserted(path, iter);

    int *pai;
    int z;
    if ((z = getPathInts(pNewRow, &pai)))
    {
        auto pPath = gtk_tree_path_new_from_indicesv(pai, z);
        iterator iter;
        FolderTreeModelRow *pParent = (pNewRow->pParent) ? &(*pNewRow->pParent) : nullptr;
        makeIter(iter, pParent, pNewRow->getIndex());
        gtk_tree_model_row_inserted(gobj(),
                                    pPath,
                                    iter.gobj());
        gtk_tree_path_free(pPath);
        free(pai);
    }

//     Debug::Enter(TREEMODEL, "Dumping model after append");
//     dumpModel(_pImpl->mapRows);
//     Debug::Leave();

    return pNewRow;
}

PFolderTreeModelRow
FolderTreeModel::findRow(PFolderTreeModelRow pParent, const Glib::ustring &strName)
{
    RowsMap &m = (pParent) ? pParent->mapChildren : _pImpl->mapRows;
    auto it = m.find(strName);
    if (it != m.end())
        return it->second;

    return nullptr;
}

PFolderTreeModelRow
FolderTreeModel::findRow(const iterator &iter) const
{
    if (isTreeIterValid(iter))
    {
        FolderTreeModelRow *pParent = (FolderTreeModelRow*)iter.gobj()->user_data;
        int iRowNumber = (int)(size_t)(iter.gobj()->user_data2);
        RowsMap &m = (pParent) ? pParent->mapChildren : _pImpl->mapRows;

        if (iRowNumber < (int)m.size())
        {
            auto it = m.begin();
            std::advance(it, iRowNumber);

            PFolderTreeModelRow &p = it->second;
            Debug::Log(TREEMODEL, string(__func__) + "(parent=" + (pParent ? quote(pParent->pDir->getBasename()) : "NULL") + ":" + to_string(iRowNumber) + "): " + p->pDir->getBasename());
            return p;
        }

        Debug::Log(TREEMODEL, string(__func__) + "(" + to_string(iRowNumber) + "): ERROR");
    }

    Debug::Log(TREEMODEL, string(__func__) + "(): INVALID ITER");

    return nullptr;
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

Gtk::TreeModelFlags
FolderTreeModel::get_flags_vfunc() const
{
    return Gtk::TreeModelFlags(0);
}

/**
 *  TreeModel vfunc implementation. Returns the number of columns supported by tree_model.
 */
int
FolderTreeModel::get_n_columns_vfunc() const
{
    auto &cols = FolderTreeModelColumns::Get();
    return cols.size();
}

/**
 *  TreeModel vfunc implementation. Returns the type of the column.
 */
GType
FolderTreeModel::get_column_type_vfunc(int index) const
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
void
FolderTreeModel::get_value_vfunc(const TreeModel::iterator &iter,
                                 int column,
                                 Glib::ValueBase &value) const
{
    auto pRow = findRow(iter);

    if (pRow)
    {
        auto &cols = FolderTreeModelColumns::Get();
        if (column <= (int)cols.size())
        {
            GType type = cols.types()[column];
            value.init(type);

            switch (column)
            {
                case 0: // cols._colMajorSort:
                {
                    Glib::Value<uint8_t> v;
                    v.init(type);
                    v.set(pRow->sort);
                    value = v;
                }
                break;
                case 1: // cols._colIconAndName:
                {
                    Debug::Log(TREEMODEL, string(__func__) + "(" + quote(pRow->pDir->getBasename()) + ")");

                    Glib::Value<Glib::ustring> v;
                    v.init(type);
                    v.set(pRow->name);
                    value = v;
                }
                break;
                case 3: // cols._colState:
                {
                    Glib::Value<uint8_t> v;
                    v.init(type);
                    v.set((uint8_t)pRow->state);
                    value = v;
                }
                break;
            }
        }
    }
}

/* virtual */
void
FolderTreeModel::set_value_impl(const iterator &row, int column, const Glib::ValueBase &value) /* override */
{
}

/**
 *  TreeModel vfunc implementation. Sets iter_next to refer to the node following iter it at the current
 *  level. If there is no next iter, false is returned and iter_next is set to be invalid.
 */
bool
FolderTreeModel::iter_next_vfunc(const iterator &iter, iterator &iterNext) const
{
    Debug::Enter(TREEMODEL, __func__);
    bool rc = false;

    iterNext = iterator();

    if (isTreeIterValid(iter))
    {
        FolderTreeModelRow *pParent = (FolderTreeModelRow*)iter.gobj()->user_data;
        int iRowNumber = (int)(size_t)(iter.gobj()->user_data2);
        RowsMap &m = (pParent) ? pParent->mapChildren : _pImpl->mapRows;

        Debug::Log(TREEMODEL, string(__func__) + "(" + quote(pParent->pDir->getBasename()) + ", " + to_string(iRowNumber) + ")");

        iRowNumber++;
        if (iRowNumber < (int)m.size())
        {
            makeIter(iterNext, pParent, iRowNumber);
            rc = true;
        }
    }

    Debug::Leave("returning " + string((rc) ? "true" : "false"));
    return rc;
}

/**
 *  TreeModel vfunc implementation. Sets iter to refer to the first child of parent.
 *  If parent has no children, false is returned and iter is set to be invalid.
 */
bool
FolderTreeModel::iter_children_vfunc(const iterator &parent, iterator &iter) const
{
    return iter_nth_child_vfunc(parent, 0, iter);
}

/**
 *  TreeModel vfunc implementation. Returns true if iter has children, false otherwise.
 */
bool
FolderTreeModel::iter_has_child_vfunc(const iterator &iter) const
{
    return (iter_n_children_vfunc(iter) > 0);
}

/**
 *  TreeModel vfunc implementation. Returns the number of children that iter has. See also iter_n_root_children_vfunc().
 */
int
FolderTreeModel::iter_n_children_vfunc(const iterator &iter) const
{
    int z = 0;
    Debug::Enter(TREEMODEL, __func__);

    PFolderTreeModelRow pRow = findRow(iter);
    if (pRow)
    {
        Debug::Log(TREEMODEL, string(__func__) + "(" + quote(pRow->pDir->getBasename()) + ") => " + to_string(pRow->mapChildren.size()) + " children");
        z = pRow->mapChildren.size();
    }

    Debug::Leave("returning " + to_string(z));
    return z;
}

/**
 *  TreeModel vfunc implementation. Returns the number of columns supported by tree_model.
 */
int
FolderTreeModel::iter_n_root_children_vfunc() const
{
    return _pImpl->mapRows.size();
}

/**
 *  TreeModel vfunc implementation. Sets iter to be the child of parent using the given index.
 *  The first index is 0. If n is too big, or parent has no children, iter is set to an invalid
 *  iterator and false is returned. See also iter_nth_root_child_vfunc().
 */
bool
FolderTreeModel::iter_nth_child_vfunc(const iterator &parent,
                                      int n,
                                      iterator &iter) const
{
    bool rc = false;
    Debug::Enter(TREEMODEL, __func__);

    iter = iterator();

    auto pParentRow = findRow(parent);
    if (pParentRow)
    {
        if (n < (int)pParentRow->mapChildren.size())
        {
            RowsMap::const_iterator it3 = pParentRow->mapChildren.begin();
            std::advance(it3, n);
            auto pRowResult = it3->second;
            makeIter(iter, &*pParentRow, n);
            rc = true;
        }
    }

    Debug::Leave("returning " + string((rc) ? "true" : "false"));
    return rc;
}

/**
 *  TreeModel vfunc implementation. Sets iter to be the child of at the root level using the given
 *  index. The first index is 0. If n is too big, or if there are no children, iter is set to an
 *  invalid iterator and false is returned. See also iter_nth_child_vfunc().
 */
bool
FolderTreeModel::iter_nth_root_child_vfunc(int n, iterator &iter) const
{
    bool rc = false;
    Debug::Enter(TREEMODEL, __func__);

    iter = iterator();

    if (n < (int)_pImpl->mapRows.size())
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
bool
FolderTreeModel::iter_parent_vfunc(const iterator &child, iterator &iter) const
{
    bool rc = true;
    Debug::Enter(TREEMODEL, __func__);
    iter = iterator();

    auto pRow = findRow(child);
    if (pRow)
    {
        if (pRow->pParent)
        {
            auto pRowResult = pRow->pParent;
            FolderTreeModelRow *pGrandParent = (pRowResult->pParent) ? &(*pRowResult->pParent) : nullptr;
            makeIter(iter, pGrandParent, pRowResult->getIndex());
            rc = true;
        }
    }

    Debug::Leave("returning " + string((rc) ? "true" : "false"));
    return rc;
}

/**
 *  TreeModel vfunc implementation. Returns a Path referenced by iter.
 */
Gtk::TreeModel::Path
FolderTreeModel::get_path_vfunc(const iterator &iter) const
{
    auto pRow = findRow(iter);
    if (pRow)
        return getPath(pRow);

    return Path();
}

/**
 *  TreeModel vfunc implementation. Sets iter to a valid iterator pointing to path
 */
bool
FolderTreeModel::get_iter_vfunc(const Path &path, iterator &iter) const
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
            RowsMap &m = (pRow) ? pRow->mapChildren : _pImpl->mapRows;
            if (u < (int)m.size())
            {
                auto it = m.begin();
                std::advance(it, u);
                pRow = it->second;
            }
            else
            {
                pRow = nullptr;
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
