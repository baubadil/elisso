
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

FolderTreeModelRow::FolderTreeModelRow(unsigned sort_,
                                       PFSModelBase pDir_,
                                       unsigned uRowIndex_)
    : sort(sort_),
      name(pDir_->getBasename()),
      state(TreeNodeState::UNKNOWN),
      uRowIndex(uRowIndex_),
      pDir(pDir_)
{
}


/***************************************************************************
 *
 *  FolderTreeModel::GlueItem
 *
 **************************************************************************/

//This maps the GtkTreeIters to potential paths:
//Each GlueItem might be stored in more than one GtkTreeIter,
//but it will be deleted only once, because it is stored
//only once in the GlueList.
//GtkTreeIter::user_data might contain orphaned GlueList pointers,
//but nobody will access them because GtkTreeIter::stamp will have the
//wrong value, marking the user_data as invalid.
class GlueItem
{
public:
    GlueItem(int row_number)
        : m_row_number(row_number)
    { };

    int get_row_number() const
    {
        return m_row_number;
    }

protected:
    int m_row_number;
};

typedef std::shared_ptr<GlueItem> PGlueItem;

// FolderTreeModel::GlueList::~GlueList()
// {
//     //Delete each GlueItem in the list:
//     for (const auto &pItem : m_list)
//     {
//         delete pItem;
//     }
// }


/***************************************************************************
 *
 *  FolderTreeModel::Impl
 *
 **************************************************************************/

struct FolderTreeModel::Impl
{
    std::vector<PGlueItem>  vGlueItems;
    int                     stamp = 1;      //When the model's stamp and the TreeIter's stamp are equal, the TreeIter is valid.

    RowsVector vRows;
    RowsMap    mapRows;
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
    : Glib::ObjectBase( typeid(FolderTreeModel) ), //register a custom GType.
      Glib::Object(), //The custom GType is actually registered here.
      _pImpl(new Impl)
{
    //Initialize our underlying data:
//     const RowsVector::size_type rows_count = 100;
//     m_rows.resize(rows_count); //100 rows.
//     for (unsigned int row_number = 0; row_number < rows_count; ++row_number)
//     {
//         //Create the row:
//         m_rows[row_number].resize(columns_count); // 10 cells (columns) for each row.
//
//         for (unsigned int column_number = 0; column_number < columns_count; ++column_number)
//         {
//             // Set the data in the row cells:
//             // It is more likely that you would be reusing existing data from some other data structure,
//             // instead of generating the data here.
//
//             char buffer[20]; //You could use a std::stringstream instead.
//             g_snprintf(buffer, sizeof(buffer), "%d, %d", row_number, column_number);
//
//             (m_rows[row_number])[column_number] = buffer; //Note that all 10 columns here are of the same type.
//         }
//     }
//
    //The Column information that can be used with TreeView::append(), TreeModel::iterator[], etc.
//     auto &cols = FolderTreeModelColumns::Get();
//     size_t cColumns = cols.size();
//     _pImpl->vModelColumns.resize(cColumns);
//     for (size_t i = 0; i < cColumns; ++i)
//         _pImpl->vModelColumns.push_back(make_shared<Gtk::TreeModelColumnBase>(
//     for (unsigned int column_number = 0; column_number < columns_count; ++column_number)
//     {
//         m_column_record.add( _pImpl->vModelColumns[column_number] );
//     }
}

FolderTreeModel::~FolderTreeModel()
{
}

PFolderTreeModelRow
FolderTreeModel::append(PFolderTreeModelRow pParent,
                        unsigned sort,
                        PFSModelBase pDir)
{
    unsigned uRowIndex = pParent ? pParent->vChildren2.size() : _pImpl->vRows.size();

    PFolderTreeModelRow pRow = make_shared<FolderTreeModelRow>(sort, pDir, uRowIndex);

    if (pParent)
    {
        pParent->vChildren2.push_back(pRow);
        pParent->mapChildren[pRow->name] = pRow;
    }
    else
    {
        _pImpl->vRows.push_back(pRow);
        _pImpl->mapRows[pRow->name] = pRow;
    }

    return pRow;
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
FolderTreeModel::findRow(const iterator &iter)
{
    RowsVector::const_iterator it2 = get_data_row_iter_from_tree_row_iter(iter);
    if (it2 == _pImpl->vRows.end())
        return nullptr;

    return *it2;
}

Gtk::TreePath
FolderTreeModel::getPath(PFolderTreeModelRow pRow) const
{
    auto iter = iterator();
    iter.set_stamp(_pImpl->stamp);

    iter.gobj()->user_data = createGlueItem(pRow->getIndex());

    return Gtk::TreePath(iter);
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
    if (check_treeiter_validity(iter))
    {
        auto &cols = FolderTreeModelColumns::Get();

        if (column <= (int)cols.size())
        {
            RowsVector::const_iterator it2 = get_data_row_iter_from_tree_row_iter(iter);
            if (it2 != _pImpl->vRows.end())
            {
                const PFolderTreeModelRow &pRow = *it2;

                GType type = cols.types()[column];
                value.init(type);

                switch (column)
                {
                    case 0: // cols._colMajorSort:
                    {
                        Glib::Value<uint8_t> v;
                        v.set(pRow->sort);
                        value = v;
                    }
                    break;
                    case 1: // cols._colIconAndName:
                    {
                        Glib::Value<Glib::ustring> v;
                        v.set(pRow->name);
                        value = v;
                    }
                    break;
                    case 3: // cols._colState:
                    {
                        Glib::Value<uint8_t> v;
                        v.set((uint8_t)pRow->state);
                        value = v;
                    }
                    break;
                }
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
FolderTreeModel::iter_next_vfunc(const iterator &iter, iterator &iter_next) const
{
    iter_next = iterator();

    if (check_treeiter_validity(iter))
    {
        iter_next.set_stamp(_pImpl->stamp);

        const auto pItem = (const GlueItem*)iter.gobj()->user_data;
        RowsVector::size_type row_index = pItem->get_row_number();

        row_index++;
        if (row_index < _pImpl->vRows.size())
        {
            iter_next.gobj()->user_data = createGlueItem(row_index);
            return true; //success
        }
    }

    return false; //There is no next row.
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
    if (check_treeiter_validity(iter))
    {
        RowsVector::const_iterator it2 = get_data_row_iter_from_tree_row_iter(iter);
        if (it2 != _pImpl->vRows.end())
        {
            const PFolderTreeModelRow &pRow = *it2;
            return pRow->vChildren2.size();
        }
    }

    return 0; //There are no children
}

/**
 *  TreeModel vfunc implementation. Returns the number of columns supported by tree_model.
 */
int
FolderTreeModel::iter_n_root_children_vfunc() const
{
    return _pImpl->vRows.size();
}

/**
 *  TreeModel vfunc implementation. Sets iter to be the child of parent using the given index.
 *  The first index is 0. If n is too big, or parent has no children, iter is set to an invalid
 *  iterator and false is returned. See also iter_nth_root_child_vfunc() TODO
 */
bool
FolderTreeModel::iter_nth_child_vfunc(const iterator &parent, int /* n */, iterator &iter) const
{
    iter = iterator(); //Set is as invalid, as the TreeModel documentation says that it should be.

    if (!check_treeiter_validity(parent))
    {
        return false;
    }

    return false; //There are no children.
}

/**
 *  TreeModel vfunc implementation. Sets iter to be the child of at the root level using the given
 *  index. The first index is 0. If n is too big, or if there are no children, iter is set to an
 *  invalid iterator and false is returned. See also iter_nth_child_vfunc().
 */
bool
FolderTreeModel::iter_nth_root_child_vfunc(int n, iterator &iter) const
{
    iter = iterator();

    if (n < (int)_pImpl->vRows.size())
    {
        iter.set_stamp(_pImpl->stamp);
        unsigned row_index = n;
        iter.gobj()->user_data = createGlueItem(row_index);
        return true;
    }

    return false; //There are no children.
}

/**
 *  TreeModel vfunc implementation. Sets iter to be the parent of child. If child is at the toplevel,
 *  and doesn't have a parent, then iter is set to an invalid iterator and false is returned. TODO
 */
bool
FolderTreeModel::iter_parent_vfunc(const iterator &child, iterator &iter) const
{
    if (!check_treeiter_validity(child))
    {
        iter = iterator(); //Set is as invalid, as the TreeModel documentation says that it should be.
        return false;
    }

    iter = iterator(); //Set is as invalid, as the TreeModel documentation says that it should be.
    return false; //There are no children, so no parents.
}

/**
 *  TreeModel vfunc implementation. Returns a Path referenced by iter. TODO
 */
Gtk::TreeModel::Path
FolderTreeModel::get_path_vfunc(const iterator &/* iter */) const
{
    return Path();
}

/**
 *  TreeModel vfunc implementation. Sets iter to a valid iterator pointing to path
 */
bool
FolderTreeModel::get_iter_vfunc(const Path &path, iterator &iter) const
{
    iter = iterator();

    unsigned sz = path.size();
    if (!sz)
        return false;

    if (sz > 1) //There are no children.
        return false;

    //This is a new GtkTreeIter, so it needs the current stamp value.
    //See the comment in the constructor.
    iter = iterator(); //clear the input parameter.
    iter.set_stamp(_pImpl->stamp);

    //Store the row_index in the GtkTreeIter:
    //See also iter_next_vfunc()
    //TODO: Store a pointer to some more complex data type such as a RowsVector::iterator.

    unsigned row_index = path[0];

    //Store the GlueItem in the GtkTreeIter.
    //This will be deleted in the GlueList destructor,
    //which will be called when the old GtkTreeIters are marked as invalid,
    //when the stamp value changes.
    iter.gobj()->user_data = createGlueItem(row_index);

    return true;
}

// Gtk::TreeModelColumn<Glib::ustring>&
// FolderTreeModel::get_model_column(int column)
// {
//     return _pImpl->vModelColumns[column];
// }

RowsVector::iterator
FolderTreeModel::get_data_row_iter_from_tree_row_iter(const iterator &iter)
{
    //Don't call this on an invalid iter.
    const auto pItem = (const GlueItem*)iter.gobj()->user_data;

    RowsVector::size_type row_index = pItem->get_row_number();
    if (row_index > _pImpl->vRows.size())
        return _pImpl->vRows.end();
    else
        return _pImpl->vRows.begin() + row_index;
}

RowsVector::const_iterator
FolderTreeModel::get_data_row_iter_from_tree_row_iter(const iterator &iter) const
{
    //Don't call this on an invalid iter.
    const auto pItem = (const GlueItem*)iter.gobj()->user_data;

    RowsVector::size_type row_index = pItem->get_row_number();
    if (row_index > _pImpl->vRows.size())
        return _pImpl->vRows.end();
    else
        return _pImpl->vRows.begin() + row_index;
}

bool
FolderTreeModel::check_treeiter_validity(const iterator &iter) const
{
    // Anything that modifies the model's structure should change the model's stamp,
    // so that old iters are ignored.
    return _pImpl->stamp == iter.get_stamp();
}

void*
FolderTreeModel::createGlueItem(int rowIndex) const
{
    auto pItemNew = std::make_shared<GlueItem>(rowIndex);
    _pImpl->vGlueItems.push_back(pItemNew);
    GlueItem *pv = &(*pItemNew);
    return pv;
}
