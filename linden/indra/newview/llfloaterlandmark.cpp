/** 
 * @file lllandmark.cpp
 * @author Richard Nelson, James Cook, Sam Kolb
 * @brief LLLandmarkCtrl class implementation including related functions
 *
 * $LicenseInfo:firstyear=2002&license=internal$
 * 
 * Copyright (c) 2002-2007, Linden Research, Inc.
 * 
 * The following source code is PROPRIETARY AND CONFIDENTIAL. Use of
 * this source code is governed by the Linden Lab Source Code Disclosure
 * Agreement ("Agreement") previously entered between you and Linden
 * Lab. By accessing, using, copying, modifying or distributing this
 * software, you acknowledge that you have been informed of your
 * obligations under the Agreement and agree to abide by those obligations.
 * 
 * ALL LINDEN LAB SOURCE CODE IS PROVIDED "AS IS." LINDEN LAB MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llfloaterlandmark.h"

#include "llagent.h"
#include "llviewerimagelist.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llbutton.h"
#include "lldraghandle.h"
#include "llfocusmgr.h"
#include "llviewerimage.h"
#include "llviewerparcelmgr.h"
#include "llfolderview.h"
#include "llinventory.h"
#include "llinventorymodel.h"
#include "llinventoryview.h"
#include "lllineeditor.h"
#include "llui.h"
#include "llviewerinventory.h"
#include "llpermissions.h"
#include "llsaleinfo.h"
#include "llassetstorage.h"
#include "lltextbox.h"
#include "llparcel.h"
#include "llresizehandle.h"
#include "llscrollcontainer.h"
#include "lltoolmgr.h"
#include "lltoolpipette.h"

#include "lltool.h"
#include "llviewerwindow.h"
#include "llviewerobject.h"
#include "llviewercontrol.h"
#include "llglheaders.h"
#include "llvieweruictrlfactory.h"

#include "roles_constants.h"



static const S32 CLOSE_BTN_WIDTH = 100;
const S32 PIPETTE_BTN_WIDTH = 32;
static const S32 HPAD = 4;
static const S32 VPAD = 4;
static const S32 LINE = 16;
static const S32 SMALL_BTN_WIDTH = 64;
static const S32 TEX_PICKER_MIN_WIDTH = 
	(HPAD +
	CLOSE_BTN_WIDTH +
	HPAD +
	CLOSE_BTN_WIDTH +
	HPAD + 
	SMALL_BTN_WIDTH +
	HPAD +
	SMALL_BTN_WIDTH +
	HPAD + 
	30 +
	RESIZE_HANDLE_WIDTH * 2);
static const S32 CLEAR_BTN_WIDTH = 50;
static const S32 TEX_PICKER_MIN_HEIGHT = 290;
static const S32 FOOTER_HEIGHT = 100;
static const S32 BORDER_PAD = HPAD;
static const S32 TEXTURE_INVENTORY_PADDING = 30;
static const F32 CONTEXT_CONE_IN_ALPHA = 0.0f;
static const F32 CONTEXT_CONE_OUT_ALPHA = 1.f;
static const F32 CONTEXT_FADE_TIME = 0.08f;

//static const char CURRENT_IMAGE_NAME[] = "Current Landmark";
//static const char WHITE_IMAGE_NAME[] = "Blank Landmark";
//static const char NO_IMAGE_NAME[] = "None";


LLFloaterLandmark::LLFloaterLandmark(const LLSD& data)
	:
	mTentativeLabel(NULL),
	mResolutionLabel(NULL),
	mIsDirty( FALSE ),
	mActive( TRUE ),
	mSearchEdit(NULL),
	mContextConeOpacity(0.f)
{
	gUICtrlFactory->buildFloater(this,"floater_landmark_ctrl.xml");

	mTentativeLabel = LLUICtrlFactory::getTextBoxByName(this,"Multiple");

	mResolutionLabel = LLUICtrlFactory::getTextBoxByName(this,"unknown");

		
	childSetCommitCallback("show_folders_check", onShowFolders, this);
	childSetVisible("show_folders_check", FALSE);
	
	mSearchEdit = (LLSearchEditor*)getCtrlByNameAndType("inventory search editor", WIDGET_TYPE_SEARCH_EDITOR);
	mSearchEdit->setSearchCallback(onSearchEdit, this);
		
	mInventoryPanel = (LLInventoryPanel*)this->getCtrlByNameAndType("inventory panel", WIDGET_TYPE_INVENTORY_PANEL);

	if(mInventoryPanel)
	{
		U32 filter_types = 0x0;
		filter_types |= 0x1 << LLInventoryType::IT_LANDMARK;
		// filter_types |= 0x1 << LLInventoryType::IT_SNAPSHOT;

		mInventoryPanel->setFilterTypes(filter_types);
		//mInventoryPanel->setFilterPermMask(getFilterPermMask());  //Commented out due to no-copy texture loss.
		mInventoryPanel->setSelectCallback(onSelectionChange, this);
		mInventoryPanel->setShowFolderState(LLInventoryFilter::SHOW_NON_EMPTY_FOLDERS);
		mInventoryPanel->setAllowMultiSelect(FALSE);

		// store this filter as the default one
		mInventoryPanel->getRootFolder()->getFilter()->markDefault();

		// Commented out to stop opening all folders with textures
		mInventoryPanel->openDefaultFolderForType(LLAssetType::AT_LANDMARK);
		
		// don't put keyboard focus on selected item, because the selection callback
		// will assume that this was user input
		mInventoryPanel->setSelection(findItemID(mImageAssetID, FALSE), TAKE_FOCUS_NO);
	}

	mSavedFolderState = new LLSaveFolderState();
	mNoCopyLandmarkSelected = FALSE;
		
	childSetAction("Close", LLFloaterLandmark::onBtnClose,this);
	childSetAction("New", LLFloaterLandmark::onBtnNew,this);
	childSetAction("NewFolder", LLFloaterLandmark::onBtnNewFolder,this);
	childSetAction("Edit", LLFloaterLandmark::onBtnEdit,this);
	childSetAction("Rename", LLFloaterLandmark::onBtnRename,this);
	childSetAction("Delete", LLFloaterLandmark::onBtnDelete,this);

	setCanMinimize(FALSE);

	mSavedFolderState->setApply(FALSE);
}

LLFloaterLandmark::~LLFloaterLandmark()
{
	delete mSavedFolderState;
}

void LLFloaterLandmark::setActive( BOOL active )					
{
	mActive = active; 
}

// virtual
BOOL LLFloaterLandmark::handleDragAndDrop( 
		S32 x, S32 y, MASK mask,
		BOOL drop,
		EDragAndDropType cargo_type, void *cargo_data, 
		EAcceptance *accept,
		LLString& tooltip_msg)
{
	BOOL handled = FALSE;

	if (cargo_type == DAD_LANDMARK)
	{
		LLInventoryItem *item = (LLInventoryItem *)cargo_data;

		BOOL copy = item->getPermissions().allowCopyBy(gAgent.getID());
		BOOL mod = item->getPermissions().allowModifyBy(gAgent.getID());
		BOOL xfer = item->getPermissions().allowOperationBy(PERM_TRANSFER,
															gAgent.getID());

		PermissionMask item_perm_mask = 0;
		if (copy) item_perm_mask |= PERM_COPY;
		if (mod)  item_perm_mask |= PERM_MODIFY;
		if (xfer) item_perm_mask |= PERM_TRANSFER;
		
		//PermissionMask filter_perm_mask = getFilterPermMask();  Commented out due to no-copy texture loss.
		PermissionMask filter_perm_mask = mImmediateFilterPermMask;
		if ( (item_perm_mask & filter_perm_mask) == filter_perm_mask )
		{

			*accept = ACCEPT_YES_SINGLE;
		}
		else
		{
			*accept = ACCEPT_NO;
		}
	}
	else
	{
		*accept = ACCEPT_NO;
	}

	handled = TRUE;
	lldebugst(LLERR_USER_INPUT) << "dragAndDrop handled by LLFloaterLandmark " << getName() << llendl;

	return handled;
}

BOOL LLFloaterLandmark::handleKeyHere(KEY key, MASK mask, BOOL called_from_parent)
{
	LLFolderView* root_folder = mInventoryPanel->getRootFolder();

	if (root_folder && mSearchEdit)
	{
		if (!called_from_parent && mSearchEdit->hasFocus() &&
		    (key == KEY_RETURN || key == KEY_DOWN) &&
		    mask == MASK_NONE)
		{
			if (!root_folder->getCurSelectedItem())
			{
				LLFolderViewItem* itemp = root_folder->getItemByID(gAgent.getInventoryRootID());
				if (itemp)
				{
					root_folder->setSelection(itemp, FALSE, FALSE);
				}
			}
			root_folder->scrollToShowSelection();
			
			// move focus to inventory proper
			root_folder->setFocus(TRUE);
			
			return TRUE;
		}
		
		if (root_folder->hasFocus() && key == KEY_UP)
		{
			mSearchEdit->focusFirstItem(TRUE);
		}
	}

	return LLFloater::handleKeyHere(key, mask, called_from_parent);
}

// virtual
void LLFloaterLandmark::onClose(bool app_quitting)
{
	destroy();
}



const LLUUID& LLFloaterLandmark::findItemID(const LLUUID& asset_id, BOOL copyable_only)
{
	LLViewerInventoryCategory::cat_array_t cats;
	LLViewerInventoryItem::item_array_t items;
	LLAssetIDMatches asset_id_matches(asset_id);
	gInventory.collectDescendentsIf(LLUUID::null,
							cats,
							items,
							LLInventoryModel::INCLUDE_TRASH,
							asset_id_matches);

	if (items.count())
	{
		// search for copyable version first
		for (S32 i = 0; i < items.count(); i++)
		{
			LLInventoryItem* itemp = items[i];
			LLPermissions item_permissions = itemp->getPermissions();
			if (item_permissions.allowCopyBy(gAgent.getID(), gAgent.getGroupID()))
			{
				return itemp->getUUID();
			}
		}
		// otherwise just return first instance, unless copyable requested
		if (copyable_only)
		{
			return LLUUID::null;
		}
		else
		{
			return items[0]->getUUID();
		}
	}

	return LLUUID::null;
}

// static
void LLFloaterLandmark::onBtnClose(void* userdata)
{
	LLFloaterLandmark* self = (LLFloaterLandmark*) userdata;
	self->mIsDirty = FALSE;
	self->close();
}

// static
void LLFloaterLandmark::onBtnEdit(void* userdata)
{
	LLFloaterLandmark* self = (LLFloaterLandmark*) userdata;
	// There isn't one, so make a new preview
	LLViewerInventoryItem* itemp = gInventory.getItem(self->mImageAssetID);
	if(itemp)
	{
		open_landmark(itemp, itemp->getName(), TRUE);
	}
}
// static
void LLFloaterLandmark::onBtnNew(void* userdata)
{
	LLViewerRegion* agent_region = gAgent.getRegion();
	if(!agent_region)
	{
		llwarns << "No agent region" << llendl;
		return;
	}
	LLParcel* agent_parcel = gParcelMgr->getAgentParcel();
	if (!agent_parcel)
	{
		llwarns << "No agent parcel" << llendl;
		return;
	}
	if (!agent_parcel->getAllowLandmark()
		&& !LLViewerParcelMgr::isParcelOwnedByAgent(agent_parcel, GP_LAND_ALLOW_LANDMARK))
	{
		gViewerWindow->alertXml("CannotCreateLandmarkNotOwner");
		return;
	}

	LLUUID folder_id;
	folder_id = gInventory.findCategoryUUIDForType(LLAssetType::AT_LANDMARK);
	std::string pos_string;
	gAgent.buildLocationString(pos_string);

	create_inventory_item(gAgent.getID(), gAgent.getSessionID(),
		folder_id, LLTransactionID::tnull,
		pos_string, pos_string, // name, desc
		LLAssetType::AT_LANDMARK,
		LLInventoryType::IT_LANDMARK,
		NOT_WEARABLE, PERM_ALL, 
		NULL);
}
// static
void LLFloaterLandmark::onBtnNewFolder(void* userdata)
{

}
// static
void LLFloaterLandmark::onBtnDelete(void* userdata)
{
	LLFloaterLandmark* self = (LLFloaterLandmark*)userdata;

	LLViewerInventoryItem* item = gInventory.getItem(self->mImageAssetID);
	if(item)
	{
		// Move the item to the trash
		LLUUID trash_id = gInventory.findCategoryUUIDForType(LLAssetType::AT_TRASH);
		if (item->getParentUUID() != trash_id)
		{
			LLInventoryModel::update_list_t update;
			LLInventoryModel::LLCategoryUpdate old_folder(item->getParentUUID(),-1);
			update.push_back(old_folder);
			LLInventoryModel::LLCategoryUpdate new_folder(trash_id, 1);
			update.push_back(new_folder);
			gInventory.accountForUpdate(update);

			LLPointer<LLViewerInventoryItem> new_item = new LLViewerInventoryItem(item);
			new_item->setParent(trash_id);
			// no need to restamp it though it's a move into trash because
			// it's a brand new item already.
			new_item->updateParentOnServer(FALSE);
			gInventory.updateItem(new_item);
			gInventory.notifyObservers();
		}
	}

	// Delete the item entirely
	/*
	item->removeFromServer();
	gInventory.deleteObject(item->getUUID());
	gInventory.notifyObservers();
	*/


}

// static
void LLFloaterLandmark::onBtnRename(void* userdata)
{

}

// static 
void LLFloaterLandmark::onSelectionChange(const std::deque<LLFolderViewItem*> &items, BOOL user_action, void* data)
{
	LLFloaterLandmark* self = (LLFloaterLandmark*)data;
	if (items.size())
	{
		LLFolderViewItem* first_item = items.front();
		LLInventoryItem* itemp = gInventory.getItem(first_item->getListener()->getUUID());
		self->mNoCopyLandmarkSelected = FALSE;
		if (itemp)
		{
			if (!itemp->getPermissions().allowCopyBy(gAgent.getID()))
			{
				self->mNoCopyLandmarkSelected = TRUE;
			}
			self->mImageAssetID = itemp->getUUID();
			self->mIsDirty = TRUE;
		}
	}
}

// static
void LLFloaterLandmark::onShowFolders(LLUICtrl* ctrl, void *user_data)
{
	LLCheckBoxCtrl* check_box = (LLCheckBoxCtrl*)ctrl;
	LLFloaterLandmark* picker = (LLFloaterLandmark*)user_data;

	if (check_box->get())
	{
		picker->mInventoryPanel->setShowFolderState(LLInventoryFilter::SHOW_NON_EMPTY_FOLDERS);
	}
	else
	{
		picker->mInventoryPanel->setShowFolderState(LLInventoryFilter::SHOW_NO_FOLDERS);
	}
}

void LLFloaterLandmark::onSearchEdit(const LLString& search_string, void* user_data )
{
	LLFloaterLandmark* picker = (LLFloaterLandmark*)user_data;

	std::string upper_case_search_string = search_string;
	LLString::toUpper(upper_case_search_string);

	if (upper_case_search_string.empty())
	{
		if (picker->mInventoryPanel->getFilterSubString().empty())
		{
			// current filter and new filter empty, do nothing
			return;
		}

		picker->mSavedFolderState->setApply(TRUE);
		picker->mInventoryPanel->getRootFolder()->applyFunctorRecursively(*picker->mSavedFolderState);
		// add folder with current item to list of previously opened folders
		LLOpenFoldersWithSelection opener;
		picker->mInventoryPanel->getRootFolder()->applyFunctorRecursively(opener);
		picker->mInventoryPanel->getRootFolder()->scrollToShowSelection();

	}
	else if (picker->mInventoryPanel->getFilterSubString().empty())
	{
		// first letter in search term, save existing folder open state
		if (!picker->mInventoryPanel->getRootFolder()->isFilterModified())
		{
			picker->mSavedFolderState->setApply(FALSE);
			picker->mInventoryPanel->getRootFolder()->applyFunctorRecursively(*picker->mSavedFolderState);
		}
	}

	picker->mInventoryPanel->setFilterSubString(upper_case_search_string);
}
