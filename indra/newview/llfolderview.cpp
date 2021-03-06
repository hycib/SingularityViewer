/** 
 * @file llfolderview.cpp
 * @brief Implementation of the folder view collection of classes.
 *
 * $LicenseInfo:firstyear=2001&license=viewergpl$
 * 
 * Copyright (c) 2001-2009, Linden Research, Inc.
 * 
 * Second Life Viewer Source Code
 * The source code in this file ("Source Code") is provided by Linden Lab
 * to you under the terms of the GNU General Public License, version 2.0
 * ("GPL"), unless you have obtained a separate licensing agreement
 * ("Other License"), formally executed by you and Linden Lab.  Terms of
 * the GPL can be found in doc/GPL-license.txt in this distribution, or
 * online at http://secondlifegrid.net/programs/open_source/licensing/gplv2
 * 
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file doc/FLOSS-exception.txt in this software distribution, or
 * online at
 * http://secondlifegrid.net/programs/open_source/licensing/flossexception
 * 
 * By copying, modifying or distributing this software, you acknowledge
 * that you have read and understood your obligations described above,
 * and agree to abide by those obligations.
 * 
 * ALL LINDEN LAB SOURCE CODE IS PROVIDED "AS IS." LINDEN LAB MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llfolderview.h"

#include "llcallbacklist.h"
#include "llfloaterinventory.h"
#include "llinventorybridge.h"
#include "llinventoryclipboard.h" // *TODO: remove this once hack below gone.
#include "llinventoryfilter.h"
#include "llinventoryfunctions.h"
#include "llinventorymodelbackgroundfetch.h"
#include "llfoldertype.h"
#include "llkeyboard.h"
#include "lllineeditor.h"
#include "llmenugl.h"
#include "llresmgr.h"
#include "llpreview.h"
#include "llscrollcontainer.h" // hack to allow scrolling
#include "lltooldraganddrop.h"
#include "lltrans.h"
#include "llui.h"
#include "llviewertexture.h"
#include "llviewertexturelist.h"
#include "llviewerjointattachment.h"
#include "llviewermenu.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"
#include "llviewerfoldertype.h"
#include "llviewerwindow.h"
#include "llvoavatar.h"
#include "llfloaterproperties.h"
#include "llnotificationsutil.h"

// Linden library includes
#include "lldbstrings.h"
#include "llfocusmgr.h"
#include "llfontgl.h"
#include "llgl.h" 
#include "llrender.h"
#include "llinventory.h"

// Third-party library includes
#include <algorithm>

///----------------------------------------------------------------------------
/// Local function declarations, constants, enums, and typedefs
///----------------------------------------------------------------------------

const S32 RENAME_WIDTH_PAD = 4;
const S32 RENAME_HEIGHT_PAD = 6;
const S32 AUTO_OPEN_STACK_DEPTH = 16;
const S32 MIN_ITEM_WIDTH_VISIBLE = LLFolderViewItem::ICON_WIDTH
			+ LLFolderViewItem::ICON_PAD 
			+ LLFolderViewItem::ARROW_SIZE 
			+ LLFolderViewItem::TEXT_PAD 
			+ /*first few characters*/ 40;
const S32 MINIMUM_RENAMER_WIDTH = 80;


enum {
	SIGNAL_NO_KEYBOARD_FOCUS = 1,
	SIGNAL_KEYBOARD_FOCUS = 2
};

F32 LLFolderView::sAutoOpenTime = 1.f;

void delete_selected_item(void* user_data);
void copy_selected_item(void* user_data);
void open_selected_items(void* user_data);
void properties_selected_items(void* user_data);
void paste_items(void* user_data);
void renamer_focus_lost( LLFocusableElement* handler, void* user_data );

///----------------------------------------------------------------------------
/// Class LLFolderViewItem
///----------------------------------------------------------------------------





















//---------------------------------------------------------------------------

// Tells all folders in a folderview to sort their items
// (and only their items, not folders) by a certain function.
class LLSetItemSortFunction : public LLFolderViewFunctor
{
public:
	LLSetItemSortFunction(U32 ordering)
		: mSortOrder(ordering) {}
	virtual ~LLSetItemSortFunction() {}
	virtual void doFolder(LLFolderViewFolder* folder);
	virtual void doItem(LLFolderViewItem* item);

	U32 mSortOrder;
};


// Set the sort order.
void LLSetItemSortFunction::doFolder(LLFolderViewFolder* folder)
{
	folder->setItemSortOrder(mSortOrder);
}

// Do nothing.
void LLSetItemSortFunction::doItem(LLFolderViewItem* item)
{
	return;
}

//---------------------------------------------------------------------------

// Tells all folders in a folderview to close themselves
// For efficiency, calls setOpenArrangeRecursively().
// The calling function must then call:
//	LLFolderView* root = getRoot();
//	if( root )
//	{
//		root->arrange( NULL, NULL );
//		root->scrollToShowSelection();
//	}
// to patch things up.
class LLCloseAllFoldersFunctor : public LLFolderViewFunctor
{
public:
	LLCloseAllFoldersFunctor(BOOL close) { mOpen = !close; }
	virtual ~LLCloseAllFoldersFunctor() {}
	virtual void doFolder(LLFolderViewFolder* folder);
	virtual void doItem(LLFolderViewItem* item);

	BOOL mOpen;
};


// Set the sort order.
void LLCloseAllFoldersFunctor::doFolder(LLFolderViewFolder* folder)
{
	folder->setOpenArrangeRecursively(mOpen);
}

// Do nothing.
void LLCloseAllFoldersFunctor::doItem(LLFolderViewItem* item)
{ }

///----------------------------------------------------------------------------
/// Class LLFolderView
///----------------------------------------------------------------------------

// Default constructor
LLFolderView::LLFolderView( const std::string& name, LLUIImagePtr root_folder_icon, 
						   const LLRect& rect, const LLUUID& source_id, LLView *parent_view ) :
#if LL_WINDOWS
#pragma warning( push )
#pragma warning( disable : 4355 ) // warning C4355: 'this' : used in base member initializer list
#endif
	LLFolderViewFolder( name, root_folder_icon, this, NULL ),
#if LL_WINDOWS
#pragma warning( pop )
#endif
	mScrollContainer( NULL ),
	mPopupMenuHandle(),
	mAllowMultiSelect(TRUE),
	mShowFolderHierarchy(FALSE),
	mSourceID(source_id),
	mRenameItem( NULL ),
	mNeedsScroll( FALSE ),
	mLastScrollItem( NULL ),
	mNeedsAutoSelect( FALSE ),
	mAutoSelectOverride(FALSE),
	mNeedsAutoRename(FALSE),
	mDebugFilters(FALSE),
	mSortOrder(LLInventoryFilter::SO_FOLDERS_BY_NAME),	// This gets overridden by a pref immediately
	mSearchType(1),
	mFilter( new LLInventoryFilter(name) ),
	mShowSelectionContext(FALSE),
	mShowSingleSelection(FALSE),
	mArrangeGeneration(0),
	mUserData(NULL),
	mSelectCallback(NULL),
	mSignalSelectCallback(0),
	mMinWidth(0),
	mDragAndDropThisFrame(FALSE)
{
	LLRect new_rect(rect.mLeft, rect.mBottom + getRect().getHeight(), rect.mLeft + getRect().getWidth(), rect.mBottom);
	setRect( rect );
	reshape(rect.getWidth(), rect.getHeight());
	mIsOpen = TRUE; // this view is always open.
	mAutoOpenItems.setDepth(AUTO_OPEN_STACK_DEPTH);
	mAutoOpenCandidate = NULL;
	mAutoOpenTimer.stop();
	mKeyboardSelection = FALSE;
	mIndentation = -LEFT_INDENTATION; // children start at indentation 0
	gIdleCallbacks.addFunction(idle, this);

	//clear label
	// go ahead and render root folder as usual
	// just make sure the label ("Inventory Folder") never shows up
	mLabel = LLStringUtil::null;

	mRenamer = new LLLineEditor(std::string("ren"), getRect(), LLStringUtil::null, sFont,
								DB_INV_ITEM_NAME_STR_LEN,
								&LLFolderView::commitRename,
								NULL,
								NULL,
								this,
								&LLLineEditor::prevalidatePrintableNotPipe);
	//mRenamer->setWriteableBgColor(LLColor4::white);
	// Escape is handled by reverting the rename, not commiting it (default behavior)
	mRenamer->setCommitOnFocusLost(TRUE);
	mRenamer->setVisible(FALSE);
	addChild(mRenamer);

	// make the popup menu available
	LLMenuGL* menu = LLUICtrlFactory::getInstance()->buildMenu("menu_inventory.xml", parent_view);
	if (!menu)
	{
		menu = new LLMenuGL(LLStringUtil::null);
	}
	menu->setBackgroundColor(gColors.getColor("MenuPopupBgColor"));
	menu->setVisible(FALSE);
	mPopupMenuHandle = menu->getHandle();

	setTabStop(TRUE);
}

// Destroys the object
LLFolderView::~LLFolderView( void )
{
	// The release focus call can potentially call the
	// scrollcontainer, which can potentially be called with a partly
	// destroyed scollcontainer. Just null it out here, and no worries
	// about calling into the invalid scroll container.
	// Same with the renamer.
	mScrollContainer = NULL;
	mRenameItem = NULL;
	mRenamer = NULL;
	gFocusMgr.releaseFocusIfNeeded( this );

	if( gEditMenuHandler == this )
	{
		gEditMenuHandler = NULL;
	}

	mAutoOpenItems.removeAllNodes();
	gIdleCallbacks.deleteFunction(idle, this);

	LLView::deleteViewByHandle(mPopupMenuHandle);

	if(mRenamer == gFocusMgr.getTopCtrl())
	{
		gFocusMgr.setTopCtrl(NULL);
	}

	mAutoOpenItems.removeAllNodes();
	clearSelection();
	mItems.clear();
	mFolders.clear();

	mItemMap.clear();

	delete mFilter;
	mFilter = NULL;
}

BOOL LLFolderView::canFocusChildren() const
{
	return FALSE;
}

void LLFolderView::checkTreeResortForModelChanged()
{
	if (mSortOrder & LLInventoryFilter::SO_DATE && !(mSortOrder & LLInventoryFilter::SO_FOLDERS_BY_NAME))
	{
		// This is the case where something got added or removed.  If we are date sorting
		// everything including folders, then we need to rebuild the whole tree.
		// Just set to something not SO_DATE to force the folder most resent date resort.
		mSortOrder = mSortOrder & ~LLInventoryFilter::SO_DATE;
		setSortOrder(mSortOrder | LLInventoryFilter::SO_DATE);
	}
}

static LLFastTimer::DeclareTimer FTM_SORT("Sort Inventory");

void LLFolderView::setSortOrder(U32 order)
{
	if (order != mSortOrder)
	{
		LLFastTimer t(FTM_SORT);
		mSortOrder = order;

		for (folders_t::iterator iter = mFolders.begin();
			 iter != mFolders.end();)
		{
			folders_t::iterator fit = iter++;
			(*fit)->sortBy(order);
		}

		arrangeAll();
	}
}


U32 LLFolderView::getSortOrder() const
{
	return mSortOrder;
}

U32 LLFolderView::toggleSearchType(std::string toggle)
{
	if (toggle == "name")
	{
		if (mSearchType & 1)
		{
			mSearchType &= 6;
		}
		else
		{
			mSearchType |= 1;
		}
	}
	else if (toggle == "description")
	{
		if (mSearchType & 2)
		{
			mSearchType &= 5;
		}
		else
		{
			mSearchType |= 2;
		}
	}
	else if (toggle == "creator")
	{
		if (mSearchType & 4)
		{
			mSearchType &= 3;
		}
		else
		{
			mSearchType |= 4;
		}
	}
	if (mSearchType == 0)
	{
		mSearchType = 1;
	}

	if (getFilterSubString().length())
	{
		mFilter->setModified(LLInventoryFilter::FILTER_RESTART);
	}

	return mSearchType;
}

U32 LLFolderView::getSearchType() const
{
	return mSearchType;
}

BOOL LLFolderView::addFolder( LLFolderViewFolder* folder)
{
	// enforce sort order of My Inventory followed by Library
	if (folder->getListener()->getUUID() == gInventory.getLibraryRootFolderID())
	{
		mFolders.push_back(folder);
	}
	else
	{
		mFolders.insert(mFolders.begin(), folder);
	}
	if (folder->numSelected())
	{
		recursiveIncrementNumDescendantsSelected(folder->numSelected());
	}
	folder->setOrigin(0, 0);
	folder->reshape(getRect().getWidth(), 0);
	folder->setVisible(FALSE);
	addChild( folder );
	folder->dirtyFilter();
	folder->requestArrange();
	return TRUE;
}

void LLFolderView::closeAllFolders()
{
	// Close all the folders
	setOpenArrangeRecursively(FALSE, LLFolderViewFolder::RECURSE_DOWN);
}

void LLFolderView::openFolder(const std::string& foldername)
{
	LLFolderViewFolder* inv = getChild<LLFolderViewFolder>(foldername);
	if (inv)
	{
		setSelection(inv, FALSE, FALSE);
		inv->setOpen(TRUE);
	}
}

void LLFolderView::setOpenArrangeRecursively(BOOL openitem, ERecurseType recurse)
{
	// call base class to do proper recursion
	LLFolderViewFolder::setOpenArrangeRecursively(openitem, recurse);
	// make sure root folder is always open
	mIsOpen = TRUE;
}

static LLFastTimer::DeclareTimer FTM_ARRANGE("Arrange");

// This view grows and shinks to enclose all of its children items and folders.
S32 LLFolderView::arrange( S32* unused_width, S32* unused_height, S32 filter_generation )
{
	LLFastTimer t2(FTM_ARRANGE);

	filter_generation = mFilter->getMinRequiredGeneration();
	mMinWidth = 0;

	mHasVisibleChildren = hasFilteredDescendants(filter_generation);
	// arrange always finishes, so optimistically set the arrange generation to the most current
	mLastArrangeGeneration = getRoot()->getArrangeGeneration();

	LLInventoryFilter::EFolderShow show_folder_state =
		getRoot()->getFilter()->getShowFolderState();

	S32 total_width = LEFT_PAD;
	S32 running_height = mDebugFilters ? llceil(sSmallFont->getLineHeight()) : 0;
	S32 target_height = running_height;
	S32 parent_item_height = getRect().getHeight();

	for (folders_t::iterator iter = mFolders.begin();
		 iter != mFolders.end();)
	{
		folders_t::iterator fit = iter++;
		LLFolderViewFolder* folderp = (*fit);
		if (getDebugFilters())
		{
			folderp->setVisible(TRUE);
		}
		else
		{
			folderp->setVisible(show_folder_state == LLInventoryFilter::SHOW_ALL_FOLDERS || // always show folders?
									(folderp->getFiltered(filter_generation) || folderp->hasFilteredDescendants(filter_generation))); // passed filter or has descendants that passed filter
		}
		if (folderp->getVisible())
		{
			S32 child_height = 0;
			S32 child_width = 0;
			S32 child_top = parent_item_height - running_height;
			
			target_height += folderp->arrange( &child_width, &child_height, filter_generation );

			mMinWidth = llmax(mMinWidth, child_width);
			total_width = llmax( total_width, child_width );
			running_height += child_height;
			folderp->setOrigin( ICON_PAD, child_top - (*fit)->getRect().getHeight() );
		}
	}

	for (items_t::iterator iter = mItems.begin();
		 iter != mItems.end();)
	{
		items_t::iterator iit = iter++;
		LLFolderViewItem* itemp = (*iit);
		itemp->setVisible(itemp->getFiltered(filter_generation));

		if (itemp->getVisible())
		{
			S32 child_width = 0;
			S32 child_height = 0;
			S32 child_top = parent_item_height - running_height;
			
			target_height += itemp->arrange( &child_width, &child_height, filter_generation );
			itemp->reshape(itemp->getRect().getWidth(), child_height);

			mMinWidth = llmax(mMinWidth, child_width);
			total_width = llmax( total_width, child_width );
			running_height += child_height;
			itemp->setOrigin( ICON_PAD, child_top - itemp->getRect().getHeight() );
		}
	}

	S32 dummy_s32;
	BOOL dummy_bool;
	S32 min_width;
	mScrollContainer->calcVisibleSize( &min_width, &dummy_s32, &dummy_bool, &dummy_bool);
	reshape( llmax(min_width, total_width), running_height );

	S32 new_min_width;
	mScrollContainer->calcVisibleSize( &new_min_width, &dummy_s32, &dummy_bool, &dummy_bool);
	if (new_min_width != min_width)
	{
		reshape( llmax(min_width, total_width), running_height );
	}

	mTargetHeight = (F32)target_height;
	return llround(mTargetHeight);
}

const std::string LLFolderView::getFilterSubString(BOOL trim)
{
	return mFilter->getFilterSubString(trim);
}

static LLFastTimer::DeclareTimer FTM_FILTER("Filter Inventory");

void LLFolderView::filter( LLInventoryFilter& filter )
{
	LLFastTimer t2(FTM_FILTER);
	filter.setFilterCount(llclamp(gSavedSettings.getS32("FilterItemsPerFrame"), 1, 5000));

	if (getCompletedFilterGeneration() < filter.getCurrentGeneration())
	{
		mPassedFilter = FALSE;
		mMinWidth = 0;
		LLFolderViewFolder::filter(filter);
	}
	else
	{
		mPassedFilter = TRUE;
	}
}

void LLFolderView::reshape(S32 width, S32 height, BOOL called_from_parent)
{
	S32 min_width = 0;
	S32 dummy_height;
	BOOL dummy_bool;
	if (mScrollContainer)
	{
		mScrollContainer->calcVisibleSize( &min_width, &dummy_height, &dummy_bool, &dummy_bool);
	}
	width = llmax(mMinWidth, min_width);
	LLView::reshape(width, height, called_from_parent);
}

void LLFolderView::addToSelectionList(LLFolderViewItem* item)
{
	if (item->isSelected())
	{
		removeFromSelectionList(item);
	}
	if (mSelectedItems.size())
	{
		mSelectedItems.back()->setIsCurSelection(FALSE);
	}
	item->setIsCurSelection(TRUE);
	mSelectedItems.push_back(item);
}

void LLFolderView::removeFromSelectionList(LLFolderViewItem* item)
{
	if (mSelectedItems.size())
	{
		mSelectedItems.back()->setIsCurSelection(FALSE);
	}

	selected_items_t::iterator item_iter;
	for (item_iter = mSelectedItems.begin(); item_iter != mSelectedItems.end();)
	{
		if (*item_iter == item)
		{
			item_iter = mSelectedItems.erase(item_iter);
		}
		else
		{
			++item_iter;
		}
	}
	if (mSelectedItems.size())
	{
		mSelectedItems.back()->setIsCurSelection(TRUE);
	}
}

LLFolderViewItem* LLFolderView::getCurSelectedItem( void )
{
	if(mSelectedItems.size())
	{
		LLFolderViewItem* itemp = mSelectedItems.back();
		llassert(itemp->getIsCurSelection());
		return itemp;
	}
	return NULL;
}


// Record the selected item and pass it down the hierachy.
BOOL LLFolderView::setSelection(LLFolderViewItem* selection, BOOL openitem,
								BOOL take_keyboard_focus)
{
	if( selection == this )
	{
		return FALSE;
	}

	if( selection && take_keyboard_focus)
	{
		setFocus(TRUE);
	}

	// clear selection down here because change of keyboard focus can potentially
	// affect selection
	clearSelection();

	if(selection)
	{
		addToSelectionList(selection);
	}

	BOOL rv = LLFolderViewFolder::setSelection(selection, openitem, take_keyboard_focus);
	if(openitem && selection)
	{
		selection->getParentFolder()->requestArrange();
	}

	llassert(mSelectedItems.size() <= 1);

	mSignalSelectCallback = take_keyboard_focus ? SIGNAL_KEYBOARD_FOCUS : SIGNAL_NO_KEYBOARD_FOCUS;

	return rv;
}

BOOL LLFolderView::changeSelection(LLFolderViewItem* selection, BOOL selected)
{
	BOOL rv = FALSE;

	// can't select root folder
	if(!selection || selection == this)
	{
		return FALSE;
	}

	if (!mAllowMultiSelect)
	{
		clearSelection();
	}

	selected_items_t::iterator item_iter;
	for (item_iter = mSelectedItems.begin(); item_iter != mSelectedItems.end(); ++item_iter)
	{
		if (*item_iter == selection)
		{
			break;
		}
	}

	BOOL on_list = (item_iter != mSelectedItems.end());

	if(selected && !on_list)
	{
		addToSelectionList(selection);
	}
	if(!selected && on_list)
	{
		removeFromSelectionList(selection);
	}

	rv = LLFolderViewFolder::changeSelection(selection, selected);

	mSignalSelectCallback = SIGNAL_KEYBOARD_FOCUS;
	
	return rv;
}

void LLFolderView::extendSelection(LLFolderViewItem* selection, LLFolderViewItem* last_selected, LLDynamicArray<LLFolderViewItem*>& items)
{
	// now store resulting selection
	if (mAllowMultiSelect)
	{
		LLFolderViewItem *cur_selection = getCurSelectedItem();
		LLFolderViewFolder::extendSelection(selection, cur_selection, items);
		for (S32 i = 0; i < items.count(); i++)
		{
			addToSelectionList(items[i]);
		}
	}
	else
	{
		setSelection(selection, FALSE, FALSE);
	}

	mSignalSelectCallback = SIGNAL_KEYBOARD_FOCUS;
}

static LLFastTimer::DeclareTimer FTM_SANITIZE_SELECTION("Sanitize Selection");
void LLFolderView::sanitizeSelection()
{
	LLFastTimer _(FTM_SANITIZE_SELECTION);
	// store off current item in case it is automatically deselected
	// and we want to preserve context
	LLFolderViewItem* original_selected_item = getCurSelectedItem();

	// Cache "Show all folders" filter setting
	BOOL show_all_folders = (getRoot()->getFilter()->getShowFolderState() == LLInventoryFilter::SHOW_ALL_FOLDERS);

	std::vector<LLFolderViewItem*> items_to_remove;
	selected_items_t::iterator item_iter;
	for (item_iter = mSelectedItems.begin(); item_iter != mSelectedItems.end(); ++item_iter)
	{
		LLFolderViewItem* item = *item_iter;

		// ensure that each ancestor is open and potentially passes filtering
		BOOL visible = item->potentiallyVisible(); // initialize from filter state for this item
		// modify with parent open and filters states
		LLFolderViewFolder* parent_folder = item->getParentFolder();
		if ( parent_folder )
		{
			if ( show_all_folders )
			{	// "Show all folders" is on, so this folder is visible
				visible = TRUE;
			}
			else
			{	// Move up through parent folders and see what's visible
				while(parent_folder)
				{
					visible = visible && parent_folder->isOpen() && parent_folder->potentiallyVisible();
					parent_folder = parent_folder->getParentFolder();
				}
			}
		}

		//  deselect item if any ancestor is closed or didn't pass filter requirements.
		if (!visible)
		{
			items_to_remove.push_back(item);
		}

		// disallow nested selections (i.e. folder items plus one or more ancestors)
		// could check cached mum selections count and only iterate if there are any
		// but that may be a premature optimization.
		selected_items_t::iterator other_item_iter;
		for (other_item_iter = mSelectedItems.begin(); other_item_iter != mSelectedItems.end(); ++other_item_iter)
		{
			LLFolderViewItem* other_item = *other_item_iter;
			for( parent_folder = other_item->getParentFolder(); parent_folder; parent_folder = parent_folder->getParentFolder())
			{
				if (parent_folder == item)
				{
					// this is a descendent of the current folder, remove from list
					items_to_remove.push_back(other_item);
					break;
				}
			}
		}

		// Don't allow invisible items (such as root folders) to be selected.
		if (item == getRoot())
		{
			items_to_remove.push_back(item);
		}
	}

	std::vector<LLFolderViewItem*>::iterator item_it;
	for (item_it = items_to_remove.begin(); item_it != items_to_remove.end(); ++item_it )
	{
		changeSelection(*item_it, FALSE); // toggle selection (also removes from list)
	}

	// if nothing selected after prior constraints...
	if (mSelectedItems.empty())
	{
		// ...select first available parent of original selection, or "My Inventory" otherwise
		LLFolderViewItem* new_selection = NULL;
		if (original_selected_item)
		{
			for(LLFolderViewFolder* parent_folder = original_selected_item->getParentFolder();
				parent_folder;
				parent_folder = parent_folder->getParentFolder())
			{
				if (parent_folder->potentiallyVisible())
				{
					// give initial selection to first ancestor folder that potentially passes the filter
					if (!new_selection)
					{
						new_selection = parent_folder;
					}

					// if any ancestor folder of original item is closed, move the selection up 
					// to the highest closed
					if (!parent_folder->isOpen())
					{	
						new_selection = parent_folder;
					}
				}
			}
		}
		else
		{
			// nothing selected to start with, so pick "My Inventory" as best guess
			new_selection = getItemByID(gInventory.getRootFolderID());
		}

		if (new_selection)
		{
			setSelection(new_selection, FALSE, FALSE);
		}
	}
}

void LLFolderView::clearSelection()
{
	if (mSelectedItems.size() > 0)
	{
		recursiveDeselect(FALSE);
		mSelectedItems.clear();
	}
}

BOOL LLFolderView::getSelectionList(std::set<LLUUID> &selection)
{
	for (selected_items_t::const_iterator item_it = mSelectedItems.begin(); 
		 item_it != mSelectedItems.end(); 
		 ++item_it)
	{
		selection.insert((*item_it)->getListener()->getUUID());
	}

	return (selection.size() != 0);
}

BOOL LLFolderView::startDrag(LLToolDragAndDrop::ESource source)
{
	std::vector<EDragAndDropType> types;
	uuid_vec_t cargo_ids;
	selected_items_t::iterator item_it;
	BOOL can_drag = TRUE;
	if (!mSelectedItems.empty())
	{
		for (item_it = mSelectedItems.begin(); item_it != mSelectedItems.end(); ++item_it)
		{
			EDragAndDropType type = DAD_NONE;
			LLUUID id = LLUUID::null;
			can_drag = can_drag && (*item_it)->getListener()->startDrag(&type, &id);

			types.push_back(type);
			cargo_ids.push_back(id);
		}

		LLToolDragAndDrop::getInstance()->beginMultiDrag(types, cargo_ids, source, mSourceID); 
	}
	return can_drag;
}

void LLFolderView::commitRename( LLUICtrl* renamer, void* user_data )
{
	LLFolderView* root = reinterpret_cast<LLFolderView*>(user_data);
	if( root )
	{
		root->finishRenamingItem();
	}
}

void LLFolderView::draw()
{
	if (mDebugFilters)
	{
		std::string current_filter_string = llformat("Current Filter: %d, Least Filter: %d, Auto-accept Filter: %d",
										mFilter->getCurrentGeneration(), mFilter->getMinRequiredGeneration(), mFilter->getMustPassGeneration());
		sSmallFont->renderUTF8(current_filter_string, 0, 2, 
			getRect().getHeight() - sSmallFont->getLineHeight(), LLColor4(0.5f, 0.5f, 0.8f, 1.f), 
			LLFontGL::LEFT, LLFontGL::BOTTOM, LLFontGL::NORMAL, LLFontGL::NO_SHADOW,  S32_MAX, S32_MAX, NULL, FALSE );
	}

	// if cursor has moved off of me during drag and drop
	// close all auto opened folders
	if (!mDragAndDropThisFrame)
	{
		closeAutoOpenedFolders();
	}
	if(this == gFocusMgr.getKeyboardFocus() && !getVisible())
	{
		gFocusMgr.setKeyboardFocus( NULL );
	}

	// while dragging, update selection rendering to reflect single/multi drag status
	if (LLToolDragAndDrop::getInstance()->hasMouseCapture())
	{
		EAcceptance last_accept = LLToolDragAndDrop::getInstance()->getLastAccept();
		if (last_accept == ACCEPT_YES_SINGLE || last_accept == ACCEPT_YES_COPY_SINGLE)
		{
			setShowSingleSelection(TRUE);
		}
		else
		{
			setShowSingleSelection(FALSE);
		}
	}
	else
	{
		setShowSingleSelection(FALSE);
	}


	if (mSearchTimer.getElapsedTimeF32() > gSavedSettings.getF32("TypeAheadTimeout") || !mSearchString.size())
	{
		mSearchString.clear();
	}

	if (hasVisibleChildren()
		|| mFilter->getShowFolderState() == LLInventoryFilter::SHOW_ALL_FOLDERS)
	{
		mStatusText.clear();
	}
	else
	{
		if (LLInventoryModelBackgroundFetch::instance().backgroundFetchActive() || mCompletedFilterGeneration < mFilter->getMinRequiredGeneration())
		{
			mStatusText = std::string("Searching..."); // *TODO:translate
			sFont->renderUTF8(mStatusText, 0, 2, 1, sSearchStatusColor, LLFontGL::LEFT, LLFontGL::TOP, LLFontGL::NORMAL, LLFontGL::NO_SHADOW, S32_MAX, S32_MAX, NULL, FALSE );
		}
		else
		{
			mStatusText = std::string("No matching items found in inventory."); // *TODO:translate
			sFont->renderUTF8(mStatusText, 0, 2, 1, sSearchStatusColor, LLFontGL::LEFT, LLFontGL::TOP, LLFontGL::NORMAL, LLFontGL::NO_SHADOW, S32_MAX, S32_MAX, NULL, FALSE );
		}
	}

	LLFolderViewFolder::draw();

	mDragAndDropThisFrame = FALSE;
}

void LLFolderView::finishRenamingItem( void )
{
	if(!mRenamer)
	{
		return;
	}
	if( mRenameItem )
	{
		mRenameItem->rename( mRenamer->getText() );
	}

	mRenamer->setCommitOnFocusLost( FALSE );
	mRenamer->setFocus( FALSE );
	mRenamer->setVisible( FALSE );
	mRenamer->setCommitOnFocusLost( TRUE );
	gFocusMgr.setTopCtrl( NULL );

	if( mRenameItem )
	{
		setSelectionFromRoot( mRenameItem, TRUE );
		mRenameItem = NULL;
	}

	// List is re-sorted alphabeticly, so scroll to make sure the selected item is visible.
	scrollToShowSelection();
}

void LLFolderView::closeRenamer( void )
{
	// will commit current name (which could be same as original name)
	mRenamer->setFocus( FALSE );
	mRenamer->setVisible( FALSE );
	gFocusMgr.setTopCtrl( NULL );

	if( mRenameItem )
	{
		setSelectionFromRoot( mRenameItem, TRUE );
		mRenameItem = NULL;
	}
}

void LLFolderView::removeSelectedItems( void )
{
	if(getVisible() && getEnabled())
	{
		// just in case we're removing the renaming item.
		mRenameItem = NULL;

		// create a temporary structure which we will use to remove
		// items, since the removal will futz with internal data
		// structures.
		std::vector<LLFolderViewItem*> items;
		S32 count = mSelectedItems.size();
		if(count == 0) return;
		LLFolderViewItem* item = NULL;
		selected_items_t::iterator item_it;
		for (item_it = mSelectedItems.begin(); item_it != mSelectedItems.end(); ++item_it)
		{
			item = *item_it;
			if (item && item->isRemovable())
			{
				items.push_back(item);
			}
			else
			{
				llinfos << "Cannot delete " << item->getName() << llendl;
				return;
			}
		}

		// iterate through the new container.
		count = items.size();
		LLUUID new_selection_id;
		if(count == 1)
		{
			LLFolderViewItem* item_to_delete = items[0];
			LLFolderViewFolder* parent = item_to_delete->getParentFolder();
			LLFolderViewItem* new_selection = item_to_delete->getNextOpenNode(FALSE);
			if (!new_selection)
			{
				new_selection = item_to_delete->getPreviousOpenNode(FALSE);
			}
			if(parent)
			{
				if (parent->removeItem(item_to_delete))
				{
					// change selection on successful delete
					if (new_selection)
					{
						setSelectionFromRoot(new_selection, new_selection->isOpen(), hasFocus());
					}
					else
					{
						setSelectionFromRoot(NULL, hasFocus());
					}
				}
			}
			arrangeAll();
		}
		else if (count > 1)
		{
			LLDynamicArray<LLFolderViewEventListener*> listeners;
			LLFolderViewEventListener* listener;
			LLFolderViewItem* last_item = items[count - 1];
			LLFolderViewItem* new_selection = last_item->getNextOpenNode(FALSE);
			while(new_selection && new_selection->isSelected())
			{
				new_selection = new_selection->getNextOpenNode(FALSE);
			}
			if (!new_selection)
			{
				new_selection = last_item->getPreviousOpenNode(FALSE);
				while (new_selection && new_selection->isSelected())
				{
					new_selection = new_selection->getPreviousOpenNode(FALSE);
				}
			}
			if (new_selection)
			{
				setSelectionFromRoot(new_selection, new_selection->isOpen(), hasFocus());
			}
			else
			{
				setSelectionFromRoot(NULL, hasFocus());
			}

			for(S32 i = 0; i < count; ++i)
			{
				listener = items[i]->getListener();
				if(listener && (listeners.find(listener) == LLDynamicArray<LLFolderViewEventListener*>::FAIL))
				{
					listeners.put(listener);
				}
			}
			listener = listeners.get(0);
			if(listener)
			{
				listener->removeBatch(listeners);
			}
		}
		arrangeAll();
		scrollToShowSelection();
	}
}

// open the selected item.
void LLFolderView::openSelectedItems( void )
{
	if(getVisible() && getEnabled())
	{
		if (mSelectedItems.size() == 1)
		{
			mSelectedItems.front()->openItem();
		}
		else
		{
			S32 left, top;
			gFloaterView->getNewFloaterPosition(&left, &top);
			LLMultiPreview* multi_previewp = new LLMultiPreview(LLRect(left, top, left + 300, top - 100));
			gFloaterView->getNewFloaterPosition(&left, &top);
			LLMultiProperties* multi_propertiesp = new LLMultiProperties(LLRect(left, top, left + 300, top - 100));

			selected_items_t::iterator item_it;
			for (item_it = mSelectedItems.begin(); item_it != mSelectedItems.end(); ++item_it)
			{
				// IT_{OBJECT,ATTACHMENT} creates LLProperties
				// floaters; others create LLPreviews.  Put
				// each one in the right type of container.
				LLFolderViewEventListener* listener = (*item_it)->getListener();
				bool is_prop = listener && (listener->getInventoryType() == LLInventoryType::IT_OBJECT || listener->getInventoryType() == LLInventoryType::IT_ATTACHMENT);
				if (is_prop)
					LLFloater::setFloaterHost(multi_propertiesp);
				else
					LLFloater::setFloaterHost(multi_previewp);
				(*item_it)->openItem();
			}

			LLFloater::setFloaterHost(NULL);
			// *NOTE: LLMulti* will safely auto-delete when open'd
			// without any children.
			multi_previewp->open();
			multi_propertiesp->open();
		}
	}
}

void LLFolderView::propertiesSelectedItems( void )
{
	if(getVisible() && getEnabled())
	{
		if (mSelectedItems.size() == 1)
		{
			LLFolderViewItem* folder_item = mSelectedItems.front();
			if(!folder_item) return;
			folder_item->getListener()->showProperties();
		}
		else
		{
			S32 left, top;
			gFloaterView->getNewFloaterPosition(&left, &top);

			LLMultiProperties* multi_propertiesp = new LLMultiProperties(LLRect(left, top, left + 100, top - 100));

			LLFloater::setFloaterHost(multi_propertiesp);

			selected_items_t::iterator item_it;
			for (item_it = mSelectedItems.begin(); item_it != mSelectedItems.end(); ++item_it)
			{
				(*item_it)->getListener()->showProperties();
			}

			LLFloater::setFloaterHost(NULL);
			multi_propertiesp->open();		/* Flawfinder: ignore */
		}
	}
}

void LLFolderView::autoOpenItem( LLFolderViewFolder* item )
{
	if (mAutoOpenItems.check() == item || mAutoOpenItems.getDepth() >= (U32)AUTO_OPEN_STACK_DEPTH)
	{
		return;
	}

	// close auto-opened folders
	LLFolderViewFolder* close_item = mAutoOpenItems.check();
	while (close_item && close_item != item->getParentFolder())
	{
		mAutoOpenItems.pop();
		close_item->setOpenArrangeRecursively(FALSE);
		close_item = mAutoOpenItems.check();
	}

	item->requestArrange();

	mAutoOpenItems.push(item);
	
	item->setOpen(TRUE);
	scrollToShowItem(item);
}

void LLFolderView::closeAutoOpenedFolders()
{
	while (mAutoOpenItems.check())
	{
		LLFolderViewFolder* close_item = mAutoOpenItems.pop();
		close_item->setOpen(FALSE);
	}

	if (mAutoOpenCandidate)
	{
		mAutoOpenCandidate->setAutoOpenCountdown(0.f);
	}
	mAutoOpenCandidate = NULL;
	mAutoOpenTimer.stop();
}

BOOL LLFolderView::autoOpenTest(LLFolderViewFolder* folder)
{
	if (folder && mAutoOpenCandidate == folder)
	{
		if (mAutoOpenTimer.getStarted())
		{
			if (!mAutoOpenCandidate->isOpen())
			{
				mAutoOpenCandidate->setAutoOpenCountdown(clamp_rescale(mAutoOpenTimer.getElapsedTimeF32(), 0.f, sAutoOpenTime, 0.f, 1.f));
			}
			if (mAutoOpenTimer.getElapsedTimeF32() > sAutoOpenTime)
			{
				autoOpenItem(folder);
				mAutoOpenTimer.stop();
				return TRUE;
			}
		}
		return FALSE;
	}

	// otherwise new candidate, restart timer
	if (mAutoOpenCandidate)
	{
		mAutoOpenCandidate->setAutoOpenCountdown(0.f);
	}
	mAutoOpenCandidate = folder;
	mAutoOpenTimer.start();
	return FALSE;
}

BOOL LLFolderView::canCopy() const
{
	if (!(getVisible() && getEnabled() && (mSelectedItems.size() > 0)))
	{
		return FALSE;
	}
	
	for (selected_items_t::const_iterator selected_it = mSelectedItems.begin(); selected_it != mSelectedItems.end(); ++selected_it)
	{
		const LLFolderViewItem* item = *selected_it;
		if (!item->getListener()->isItemCopyable())
		{
			return FALSE;
		}
	}
	return TRUE;
}

// copy selected item
void LLFolderView::copy()
{
	// *NOTE: total hack to clear the inventory clipboard
	LLInventoryClipboard::instance().reset();
	S32 count = mSelectedItems.size();
	if(getVisible() && getEnabled() && (count > 0))
	{
		LLFolderViewEventListener* listener = NULL;
		selected_items_t::iterator item_it;
		for (item_it = mSelectedItems.begin(); item_it != mSelectedItems.end(); ++item_it)
		{
			listener = (*item_it)->getListener();
			if(listener)
			{
				listener->copyToClipboard();
			}
		}
	}
	mSearchString.clear();
}

BOOL LLFolderView::canCut() const
{
	return FALSE;
}

void LLFolderView::cut()
{
	// implement Windows-style cut-and-leave
}

BOOL LLFolderView::canPaste() const
{
	if (mSelectedItems.empty())
	{
		return FALSE;
	}

	if(getVisible() && getEnabled())
	{
		for (selected_items_t::const_iterator item_it = mSelectedItems.begin();
			 item_it != mSelectedItems.end(); ++item_it)
		{
			// *TODO: only check folders and parent folders of items
			const LLFolderViewItem* item = (*item_it);
			const LLFolderViewEventListener* listener = item->getListener();
			if(!listener || !listener->isClipboardPasteable())
			{
				const LLFolderViewFolder* folderp = item->getParentFolder();
				listener = folderp->getListener();
				if (!listener || !listener->isClipboardPasteable())
				{
					return FALSE;
				}
			}
		}
		return TRUE;
	}
	return FALSE;
}

// paste selected item
void LLFolderView::paste()
{
	if(getVisible() && getEnabled())
	{
		// find set of unique folders to paste into
		std::set<LLFolderViewItem*> folder_set;

		selected_items_t::iterator selected_it;
		for (selected_it = mSelectedItems.begin(); selected_it != mSelectedItems.end(); ++selected_it)
		{
			LLFolderViewItem* item = *selected_it;
			LLFolderViewEventListener* listener = item->getListener();
			if (listener->getInventoryType() != LLInventoryType::IT_CATEGORY)
			{
				item = item->getParentFolder();
			}
			folder_set.insert(item);
		}

		std::set<LLFolderViewItem*>::iterator set_iter;
		for(set_iter = folder_set.begin(); set_iter != folder_set.end(); ++set_iter)
		{
			LLFolderViewEventListener* listener = (*set_iter)->getListener();
			if(listener && listener->isClipboardPasteable())
			{
				listener->pasteFromClipboard();
			}
		}
	}
	mSearchString.clear();
}

// public rename functionality - can only start the process
void LLFolderView::startRenamingSelectedItem( void )
{
	// make sure selection is visible
	scrollToShowSelection();

	S32 count = mSelectedItems.size();
	LLFolderViewItem* item = NULL;
	if(count > 0)
	{
		item = mSelectedItems.front();
	}
	if(getVisible() && getEnabled() && (count == 1) && item && item->getListener() &&
	   item->getListener()->isItemRenameable())
	{
		mRenameItem = item;

		S32 x = ARROW_SIZE + TEXT_PAD + ICON_WIDTH + ICON_PAD - 1 + item->getIndentation();
		S32 y = llfloor(item->getRect().getHeight()-sFont->getLineHeight()-2);
		item->localPointToScreen( x, y, &x, &y );
		screenPointToLocal( x, y, &x, &y );
		mRenamer->setOrigin( x, y );

		S32 scroller_height = 0;
		S32 scroller_width = gViewerWindow->getWindowWidth();
		BOOL dummy_bool;
		if (mScrollContainer)
		{
			mScrollContainer->calcVisibleSize( &scroller_width, &scroller_height, &dummy_bool, &dummy_bool);
		}

		S32 width = llmax(llmin(item->getRect().getWidth() - x, scroller_width - x - getRect().mLeft), MINIMUM_RENAMER_WIDTH);
		S32 height = llfloor(sFont->getLineHeight() + RENAME_HEIGHT_PAD);
		mRenamer->reshape( width, height, TRUE );

		mRenamer->setText(item->getName());
		mRenamer->selectAll();
		mRenamer->setVisible( TRUE );
		// set focus will fail unless item is visible
		mRenamer->setFocus( TRUE );
		mRenamer->setLostTopCallback(onRenamerLost);
		gFocusMgr.setTopCtrl( mRenamer );
	}
}

void LLFolderView::setFocus(BOOL focus)
{
	if (focus)
	{
		if(!hasFocus())
		{
			gEditMenuHandler = this;
		}
	}

	LLFolderViewFolder::setFocus(focus);
}

BOOL LLFolderView::handleKeyHere( KEY key, MASK mask )
{
	BOOL handled = FALSE;

	// SL-51858: Key presses are not being passed to the Popup menu.
	// A proper fix is non-trivial so instead just close the menu.
	LLMenuGL* menu = (LLMenuGL*)mPopupMenuHandle.get();
	if (menu && menu->isOpen())
	{
		LLMenuGL::sMenuContainer->hideMenus();
	}

	LLView *item = NULL;
	if (getChildCount() > 0)
	{
		item = *(getChildList()->begin());
	}

	switch( key )
	{
	case KEY_F2:
		mSearchString.clear();
		startRenamingSelectedItem();
		handled = TRUE;
		break;

	case KEY_RETURN:
		if (mask == MASK_NONE)
		{
			if( mRenameItem && mRenamer->getVisible() )
			{
				finishRenamingItem();
				mSearchString.clear();
				handled = TRUE;
			}
			else
			{
				LLFolderView::openSelectedItems();
				handled = TRUE;
			}
		}
		break;

	case KEY_ESCAPE:
		if( mRenameItem && mRenamer->getVisible() )
		{
			closeRenamer();
			handled = TRUE;
		}
		mSearchString.clear();
		break;

	case KEY_PAGE_UP:
		mSearchString.clear();
		mScrollContainer->pageUp(30);
		handled = TRUE;
		break;

	case KEY_PAGE_DOWN:
		mSearchString.clear();
		mScrollContainer->pageDown(30);
		handled = TRUE;
		break;

	case KEY_HOME:
		mSearchString.clear();
		mScrollContainer->goToTop();
		handled = TRUE;
		break;

	case KEY_END:
		mSearchString.clear();
		mScrollContainer->goToBottom();
		break;

	case KEY_DOWN:
		if((mSelectedItems.size() > 0) && mScrollContainer)
		{
			LLFolderViewItem* last_selected = getCurSelectedItem();

			if (!mKeyboardSelection)
			{
				setSelection(last_selected, FALSE, TRUE);
				mKeyboardSelection = TRUE;
			}

			LLFolderViewItem* next = NULL;
			if (mask & MASK_SHIFT)
			{
				// don't shift select down to children of folders (they are implicitly selected through parent)
				next = last_selected->getNextOpenNode(FALSE);
				if (next)
				{
					if (next->isSelected())
					{
						// shrink selection
						changeSelectionFromRoot(last_selected, FALSE);
					}
					else if (last_selected->getParentFolder() == next->getParentFolder())
					{
						// grow selection
						changeSelectionFromRoot(next, TRUE);
					}
				}
			}
			else
			{
				next = last_selected->getNextOpenNode();
				if( next )
				{
					if (next == last_selected)
					{
						return FALSE;
					}
					setSelection( next, FALSE, TRUE );
				}
			}
			scrollToShowSelection();
			mSearchString.clear();
			handled = TRUE;
		}
		break;

	case KEY_UP:
		if((mSelectedItems.size() > 0) && mScrollContainer)
		{
			LLFolderViewItem* last_selected = mSelectedItems.back();

			if (!mKeyboardSelection)
			{
				setSelection(last_selected, FALSE, TRUE);
				mKeyboardSelection = TRUE;
			}

			LLFolderViewItem* prev = NULL;
			if (mask & MASK_SHIFT)
			{
				// don't shift select down to children of folders (they are implicitly selected through parent)
				prev = last_selected->getPreviousOpenNode(FALSE);
				if (prev)
				{
					if (prev->isSelected())
					{
						// shrink selection
						changeSelectionFromRoot(last_selected, FALSE);
					}
					else if (last_selected->getParentFolder() == prev->getParentFolder())
					{
						// grow selection
						changeSelectionFromRoot(prev, TRUE);
					}
				}
			}
			else
			{
				prev = last_selected->getPreviousOpenNode();
				if( prev )
				{
					if (prev == this)
					{
						return FALSE;
					}
					setSelection( prev, FALSE, TRUE );
				}
			}
			scrollToShowSelection();
			mSearchString.clear();

			handled = TRUE;
		}
		break;

	case KEY_RIGHT:
		if(mSelectedItems.size())
		{
			LLFolderViewItem* last_selected = getCurSelectedItem();
			last_selected->setOpen( TRUE );
			mSearchString.clear();
			handled = TRUE;
		}
		break;

	case KEY_LEFT:
		if(mSelectedItems.size())
		{
			LLFolderViewItem* last_selected = getCurSelectedItem();
			LLFolderViewItem* parent_folder = last_selected->getParentFolder();
			if (!last_selected->isOpen() && parent_folder && parent_folder->getParentFolder())
			{
				setSelection(parent_folder, FALSE, TRUE);
			}
			else
			{
				last_selected->setOpen( FALSE );
			}
			mSearchString.clear();
			scrollToShowSelection();
			handled = TRUE;
		}
		break;
	}

	if (!handled && hasFocus())
	{
		if (key == KEY_BACKSPACE)
		{
			mSearchTimer.reset();
			if (mSearchString.size())
			{
				mSearchString.erase(mSearchString.size() - 1, 1);
			}
			search(getCurSelectedItem(), mSearchString, FALSE);
			handled = TRUE;
		}
	}

	return handled;
}


BOOL LLFolderView::handleUnicodeCharHere(llwchar uni_char)
{
	if ((uni_char < 0x20) || (uni_char == 0x7F)) // Control character or DEL
	{
		return FALSE;
	}

	if (uni_char > 0x7f)
	{
		llwarns << "LLFolderView::handleUnicodeCharHere - Don't handle non-ascii yet, aborting" << llendl;
		return FALSE;
	}

	BOOL handled = FALSE;
	if (hasFocus())
	{
		// SL-51858: Key presses are not being passed to the Popup menu.
		// A proper fix is non-trivial so instead just close the menu.
		LLMenuGL* menu = (LLMenuGL*)mPopupMenuHandle.get();
		if (menu && menu->isOpen())
		{
			LLMenuGL::sMenuContainer->hideMenus();
		}

		//do text search
		if (mSearchTimer.getElapsedTimeF32() > gSavedSettings.getF32("TypeAheadTimeout"))
		{
			mSearchString.clear();
		}
		mSearchTimer.reset();
		if (mSearchString.size() < 128)
		{
			mSearchString += uni_char;
		}
		search(getCurSelectedItem(), mSearchString, FALSE);

		handled = TRUE;
	}

	return handled;
}


BOOL LLFolderView::canDoDelete() const
{
	if (mSelectedItems.size() == 0) return FALSE;

	for (selected_items_t::const_iterator item_it = mSelectedItems.begin(); item_it != mSelectedItems.end(); ++item_it)
	{
		if (!(*item_it)->getListener()->isItemRemovable())
		{
			return FALSE;
		}
	}
	return TRUE;
}

void LLFolderView::doDelete()
{
	if(mSelectedItems.size() > 0)
	{				
		removeSelectedItems();
	}
}


BOOL LLFolderView::handleMouseDown( S32 x, S32 y, MASK mask )
{
	mKeyboardSelection = FALSE;
	mSearchString.clear();

	setFocus(TRUE);

	return LLView::handleMouseDown( x, y, mask );
}

void LLFolderView::onFocusLost( )
{
	if( gEditMenuHandler == this )
	{
		gEditMenuHandler = NULL;
	}
	LLUICtrl::onFocusLost();
}

BOOL LLFolderView::search(LLFolderViewItem* first_item, const std::string &search_string, BOOL backward)
{
	// get first selected item
	LLFolderViewItem* search_item = first_item;

	// make sure search string is upper case
	std::string upper_case_string = search_string;
	LLStringUtil::toUpper(upper_case_string);

	// if nothing selected, select first item in folder
	if (!search_item)
	{
		// start from first item
		search_item = getNextFromChild(NULL);
	}

	// search over all open nodes for first substring match (with wrapping)
	BOOL found = FALSE;
	LLFolderViewItem* original_search_item = search_item;
	do
	{
		// wrap at end
		if (!search_item)
		{
			if (backward)
			{
				search_item = getPreviousFromChild(NULL);
			}
			else
			{
				search_item = getNextFromChild(NULL);
			}
			if (!search_item || search_item == original_search_item)
			{
				break;
			}
		}

		std::string current_item_label(search_item->getSearchableLabel());
		S32 search_string_length = llmin(upper_case_string.size(), current_item_label.size());
		if (!current_item_label.compare(0, search_string_length, upper_case_string))
		{
			found = TRUE;
			break;
		}
		if (backward)
		{
			search_item = search_item->getPreviousOpenNode();
		}
		else
		{
			search_item = search_item->getNextOpenNode();
		}

	} while(search_item != original_search_item);
	

	if (found)
	{
		setSelection(search_item, FALSE, TRUE);
		scrollToShowSelection();
	}

	return found;
}

BOOL LLFolderView::handleDoubleClick( S32 x, S32 y, MASK mask )
{
	// skip LLFolderViewFolder::handleDoubleClick()
	return LLView::handleDoubleClick( x, y, mask );
}

BOOL LLFolderView::handleRightMouseDown( S32 x, S32 y, MASK mask )
{
	// all user operations move keyboard focus to inventory
	// this way, we know when to stop auto-updating a search
	setFocus(TRUE);

	BOOL handled = childrenHandleRightMouseDown(x, y, mask) != NULL;
	S32 count = mSelectedItems.size();
	LLMenuGL* menu = (LLMenuGL*)mPopupMenuHandle.get();
	if(handled && (count > 0) && menu)
	{
		//menu->empty();
		const LLView::child_list_t *list = menu->getChildList();

		LLView::child_list_t::const_iterator menu_itor;
		for (menu_itor = list->begin(); menu_itor != list->end(); ++menu_itor)
		{
			(*menu_itor)->setVisible(TRUE);
			(*menu_itor)->setEnabled(TRUE);
		}
		
		// Successively filter out invalid options
		selected_items_t::iterator item_itor;
		U32 flags = FIRST_SELECTED_ITEM;
		for (item_itor = mSelectedItems.begin(); item_itor != mSelectedItems.end(); ++item_itor)
		{
			(*item_itor)->buildContextMenu(*menu, flags);
			flags = 0x0;
		}

		menu->arrange();
		menu->updateParent(LLMenuGL::sMenuContainer);
		LLMenuGL::showPopup(this, menu, x, y);
	}
	else
	{
		if(menu && menu->getVisible())
		{
			menu->setVisible(FALSE);
		}
		setSelection(NULL, FALSE, TRUE);
	}
	return handled;
}

BOOL LLFolderView::handleHover( S32 x, S32 y, MASK mask )
{
	return LLView::handleHover( x, y, mask );
}

BOOL LLFolderView::handleDragAndDrop(S32 x, S32 y, MASK mask, BOOL drop,
									 EDragAndDropType cargo_type,
									 void* cargo_data, 
									 EAcceptance* accept,
									 std::string& tooltip_msg)
{
	mDragAndDropThisFrame = TRUE;
	BOOL handled = LLView::handleDragAndDrop(x, y, mask, drop, cargo_type, cargo_data,
											 accept, tooltip_msg);

	if (handled)
	{
		lldebugst(LLERR_USER_INPUT) << "dragAndDrop handled by LLFolderView" << llendl;
	}

	return handled;
}

BOOL LLFolderView::handleScrollWheel(S32 x, S32 y, S32 clicks)
{
	if (mScrollContainer)
	{
		return mScrollContainer->handleScrollWheel(x, y, clicks);
	}
	return FALSE;
}

void LLFolderView::deleteAllChildren()
{
	if(mRenamer == gFocusMgr.getTopCtrl())
	{
		gFocusMgr.setTopCtrl(NULL);
	}
	LLView::deleteViewByHandle(mPopupMenuHandle);
	mPopupMenuHandle = LLHandle<LLView>();
	mRenamer = NULL;
	mRenameItem = NULL;
	clearSelection();
	LLView::deleteAllChildren();
}

void LLFolderView::scrollToShowSelection()
{
	if (mSelectedItems.size())
	{
		mNeedsScroll = TRUE;
	}
}

// If the parent is scroll containter, scroll it to make the selection
// is maximally visible.
void LLFolderView::scrollToShowItem(LLFolderViewItem* item)
{
	// don't scroll to items when mouse is being used to scroll/drag and drop
	if (gFocusMgr.childHasMouseCapture(mScrollContainer))
	{
		mNeedsScroll = FALSE;
		return;
	}
	if(item && mScrollContainer)
	{
		LLRect local_rect = item->getRect();
		LLRect item_scrolled_rect; // item position relative to display area of scroller
		
		S32 icon_height = mIcon.isNull() ? 0 : mIcon->getHeight(); 
		S32 label_height = llround(sFont->getLineHeight()); 
		// when navigating with keyboard, only move top of folders on screen, otherwise show whole folder
		S32 max_height_to_show = gFocusMgr.childHasKeyboardFocus(this) ? (llmax( icon_height, label_height ) + ICON_PAD) : local_rect.getHeight(); 
		item->localPointToOtherView(item->getIndentation(), llmax(0, local_rect.getHeight() - max_height_to_show), &item_scrolled_rect.mLeft, &item_scrolled_rect.mBottom, mScrollContainer);
		item->localPointToOtherView(local_rect.getWidth(), local_rect.getHeight(), &item_scrolled_rect.mRight, &item_scrolled_rect.mTop, mScrollContainer);

		item_scrolled_rect.mRight = llmin(item_scrolled_rect.mLeft + MIN_ITEM_WIDTH_VISIBLE, item_scrolled_rect.mRight);
		LLCoordGL scroll_offset(-mScrollContainer->getBorderWidth() - item_scrolled_rect.mLeft, 
				mScrollContainer->getRect().getHeight() - item_scrolled_rect.mTop - 1);

		S32 max_scroll_offset = getVisibleRect().getHeight() - item_scrolled_rect.getHeight();
		if (item != mLastScrollItem || // if we're scrolling to focus on a new item
		// or the item has just appeared on screen and it wasn't onscreen before
			(scroll_offset.mY > 0 && scroll_offset.mY < max_scroll_offset && 
			(mLastScrollOffset.mY < 0 || mLastScrollOffset.mY > max_scroll_offset)))
		{
			// we now have a position on screen that we want to keep stable
			// offset of selection relative to top of visible area
			mLastScrollOffset = scroll_offset;
			mLastScrollItem = item;
		}

		mScrollContainer->scrollToShowRect( item_scrolled_rect, mLastScrollOffset );

		// after scrolling, store new offset
		// in case we don't have room to maintain the original position
		LLCoordGL new_item_left_top;
		item->localPointToOtherView(item->getIndentation(), item->getRect().getHeight(), &new_item_left_top.mX, &new_item_left_top.mY, mScrollContainer);
		mLastScrollOffset.set(-mScrollContainer->getBorderWidth() - new_item_left_top.mX, mScrollContainer->getRect().getHeight() - new_item_left_top.mY - 1);
	}
}

LLRect LLFolderView::getVisibleRect()
{
	S32 visible_height = mScrollContainer->getRect().getHeight();
	S32 visible_width = mScrollContainer->getRect().getWidth();
	LLRect visible_rect;
	visible_rect.setLeftTopAndSize(-getRect().mLeft, visible_height - getRect().mBottom, visible_width, visible_height);
	return visible_rect;
}

BOOL LLFolderView::getShowSelectionContext()
{
	if (mShowSelectionContext)
	{
		return TRUE;
	}
	LLMenuGL* menu = (LLMenuGL*)mPopupMenuHandle.get();
	if (menu && menu->getVisible())
	{
		return TRUE;
	}
	return FALSE;
}

void LLFolderView::setShowSingleSelection(BOOL show)
{
	if (show != mShowSingleSelection)
	{
		mMultiSelectionFadeTimer.reset();
		mShowSingleSelection = show;
	}
}

void LLFolderView::addItemID(const LLUUID& id, LLFolderViewItem* itemp)
{
	mItemMap[id] = itemp;
}

void LLFolderView::removeItemID(const LLUUID& id)
{
	mItemMap.erase(id);
}

LLFastTimer::DeclareTimer FTM_GET_ITEM_BY_ID("Get FolderViewItem by ID");
LLFolderViewItem* LLFolderView::getItemByID(const LLUUID& id)
{
	LLFastTimer _(FTM_GET_ITEM_BY_ID);
	if (id.isNull())
	{
		return this;
	}

	std::map<LLUUID, LLFolderViewItem*>::iterator map_it;
	map_it = mItemMap.find(id);
	if (map_it != mItemMap.end())
	{
		return map_it->second;
	}

	return NULL;
}



static LLFastTimer::DeclareTimer FTM_AUTO_SELECT("Open and Select");
static LLFastTimer::DeclareTimer FTM_INVENTORY("Inventory");
extern std::set<LLFolderViewItem*> sFolderViewItems;	//dumb hack
// Main idle routine
void LLFolderView::doIdle()
{
	LLFastTimer t2(FTM_INVENTORY);

	BOOL debug_filters = gSavedSettings.getBOOL("DebugInventoryFilters");
	if (debug_filters != getDebugFilters())
	{
		mDebugFilters = debug_filters;
		arrangeAll();
	}

	mFilter->clearModified();
	BOOL filter_modified_and_active = mCompletedFilterGeneration < mFilter->getCurrentGeneration() && 
										mFilter->isNotDefault();
	mNeedsAutoSelect = filter_modified_and_active &&
							!(gFocusMgr.childHasKeyboardFocus(this) || gFocusMgr.getMouseCapture());
	
	// filter to determine visiblity before arranging
	filterFromRoot();

	// automatically show matching items, and select first one
	// do this every frame until user puts keyboard focus into the inventory window
	// signaling the end of the automatic update
	// only do this when mNeedsFilter is set, meaning filtered items have
	// potentially changed
	if (mNeedsAutoSelect)
	{
		LLFastTimer t3(FTM_AUTO_SELECT);
		// select new item only if a filtered item not currently selected
		LLFolderViewItem* selected_itemp = mSelectedItems.empty() ? NULL : mSelectedItems.back();
		if (selected_itemp != NULL && sFolderViewItems.count(selected_itemp) == 0)
		{
			// There is a crash bug due to a race condition: when a folder view item is
			// destroyed, its address may still appear in mSelectedItems a couple of doIdle()
			// later, even if you explicitely clear this list and dirty the filters in the
			// destructor...
			// This code avoids the crash bug.
			llwarns << "Invalid folder view item (" << selected_itemp << ") in selection: clearing the latter." << llendl;
			dirtyFilter();
			clearSelection();
			requestArrange();
		}
		else if (!mAutoSelectOverride && (!selected_itemp || !selected_itemp->getFiltered()))
		{
			// select first filtered item
			LLSelectFirstFilteredItem filter;
			applyFunctorRecursively(filter);
		}
		scrollToShowSelection();
	}

	BOOL is_visible = isInVisibleChain();

	if ( is_visible )
	{
		sanitizeSelection();
		if( needsArrange() )
		{
			arrangeFromRoot();
		}
	}

	if (mSelectedItems.size() && mNeedsScroll)
	{
		scrollToShowItem(mSelectedItems.back());
		// continue scrolling until animated layout change is done
		if (getCompletedFilterGeneration() >= mFilter->getMinRequiredGeneration() &&
			(!needsArrange() || !is_visible))
		{
			mNeedsScroll = FALSE;
		}
	}

	if (mSignalSelectCallback && mSelectCallback)
	{
		//RN: we use keyboard focus as a proxy for user-explicit actions
		BOOL take_keyboard_focus = (mSignalSelectCallback == SIGNAL_KEYBOARD_FOCUS);
		mSelectCallback(mSelectedItems, take_keyboard_focus, mUserData);
	}
	mSignalSelectCallback = FALSE;
}


//static
void LLFolderView::idle(void* user_data)
{
	LLFolderView* self = (LLFolderView*)user_data;
	if ( self )
	{	// Do the real idle 
		self->doIdle();
	}
}


void LLFolderView::dumpSelectionInformation()
{
	llinfos << "LLFolderView::dumpSelectionInformation()" << llendl;
	llinfos << "****************************************" << llendl;
	selected_items_t::iterator item_it;
	for (item_it = mSelectedItems.begin(); item_it != mSelectedItems.end(); ++item_it)
	{
		llinfos << "  " << (*item_it)->getName() << llendl;
	}
	llinfos << "****************************************" << llendl;
}

///----------------------------------------------------------------------------
/// Local function definitions
///----------------------------------------------------------------------------


//static 
void LLFolderView::onRenamerLost( LLUICtrl* renamer, void* user_data)
{
	renamer->setVisible(FALSE);
}

LLInventoryFilter* LLFolderView::getFilter()
{
	return mFilter;
}

void LLFolderView::setFilterPermMask( PermissionMask filter_perm_mask )
{
	mFilter->setFilterPermissions(filter_perm_mask);
}

bool LLFolderView::getFilterWorn() const 
{
	return mFilter->getFilterWorn();
}

U32 LLFolderView::getFilterTypes() const
{
	return mFilter->getFilterTypes();
}

PermissionMask LLFolderView::getFilterPermissions() const
{
	return mFilter->getFilterPermissions();
}

BOOL LLFolderView::isFilterModified()
{
	return mFilter->isNotDefault();
}

void delete_selected_item(void* user_data)
{
	if(user_data)
	{
		LLFolderView* fv = reinterpret_cast<LLFolderView*>(user_data);
		fv->removeSelectedItems();
	}
}

void copy_selected_item(void* user_data)
{
	if(user_data)
	{
		LLFolderView* fv = reinterpret_cast<LLFolderView*>(user_data);
		fv->copy();
	}
}

void paste_items(void* user_data)
{
	if(user_data)
	{
		LLFolderView* fv = reinterpret_cast<LLFolderView*>(user_data);
		fv->paste();
	}
}

void open_selected_items(void* user_data)
{
	if(user_data)
	{
		LLFolderView* fv = reinterpret_cast<LLFolderView*>(user_data);
		fv->openSelectedItems();
	}
}

void properties_selected_items(void* user_data)
{
	if(user_data)
	{
		LLFolderView* fv = reinterpret_cast<LLFolderView*>(user_data);
		fv->propertiesSelectedItems();
	}
}

///----------------------------------------------------------------------------
/// Class LLFolderViewEventListener
///----------------------------------------------------------------------------

void LLFolderViewEventListener::arrangeAndSet(LLFolderViewItem* focus,
											  BOOL set_selection,
											  BOOL take_keyboard_focus)
{
	if(!focus) return;
	LLFolderView* root = focus->getRoot();
	focus->getParentFolder()->requestArrange();
	if(set_selection)
	{
		focus->setSelectionFromRoot(focus, TRUE, take_keyboard_focus);
		if(root)
		{
			root->scrollToShowSelection();
		}
	}
}


