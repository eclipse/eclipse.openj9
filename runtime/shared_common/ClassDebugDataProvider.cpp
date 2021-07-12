/*******************************************************************************
 * Copyright (c) 2001, 2021 IBM Corp. and others
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution and
 * is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following
 * Secondary Licenses when the conditions for such availability set
 * forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
 * General Public License, version 2 with the GNU Classpath
 * Exception [1] and GNU General Public License, version 2 with the
 * OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] http://openjdk.java.net/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0 WITH Classpath-exception-2.0 OR LicenseRef-GPL-2.0 WITH Assembly-exception
 *******************************************************************************/

/**
 * Class Debug Data Layout:
 * The class debug data is laid out as illustrated below:
 *
 * *-----------------*--------------------*-------------------*
 * | J9RomClass:     |                    | J9RomClass:       |
 * | LineNumberTable | ... free space ... | LocalVariableTable|
 * |                 |                    |                   |
 * *-----------------*--------------------*-------------------*
 */

#include "ClassDebugDataProvider.hpp"
#include "UnitTest.hpp"
#include "sharedconsts.h"
#include "ut_j9shr.h"
#include "j9shrnls.h"
#include "j9protos.h"

#define CREATE_CLASS_DEBUG_AREA 1

/*Note: GET_DEBUG_MEM_START is the same as CASTART*/
typedef char* BlockPtr;
#define GET_DEBUG_MEM_START(ca) (((BlockPtr)(ca)) + sizeof(J9SharedCacheHeader))
#define GET_DEBUG_MEM_END(ca) (((BlockPtr)(ca)) + sizeof(J9SharedCacheHeader) + (ca)->debugRegionSize)
#define GET_DEBUG_LENTH(ca) ((U_32)((ca)->debugRegionSize))
#define GET_DEBUG_FLAGS(ca) ((ca)->extraFlags)

#define SET_DEBUG_LENTH(ca, size) \
	do {\
		((ca)->debugRegionSize) = (UDATA)size; \
	} while(0)

#define DA_TRACE2(verboseLevel, nlsFlags, var1, p1, p2) if (_verboseFlags & verboseLevel) j9nls_printf(PORTLIB, nlsFlags, var1, p1, p2)
#define DA_TRACE3(verboseLevel, nlsFlags, var1, p1, p2, p3) if (_verboseFlags & verboseLevel) j9nls_printf(PORTLIB, nlsFlags, var1, p1, p2, p3)
#define DA_TRACE4(verboseLevel, nlsFlags, var1, p1, p2, p3, p4) if (_verboseFlags & verboseLevel) j9nls_printf(PORTLIB, nlsFlags, var1, p1, p2, p3, p4)

void
ClassDebugDataProvider::initialize()
{
	/* ensure _theca is either NULL or points to the cache header.
	 * getJavacoreData() will update _theca if its NULL. 
	 */
	_theca = NULL;
}

bool 
ClassDebugDataProvider::Init(J9VMThread* currentThread, J9SharedCacheHeader * ca, AbstractMemoryPermission * permSetter, UDATA verboseFlags, U_64 * runtimeFlags, bool startupForStats)
{
	bool retval = true;
	Trc_SHR_ClassDebugData_Init_Entry(currentThread, ca);
	_verboseFlags = verboseFlags;
	failureReason = NO_CORRUPTION;
	failureValue = 0;
	_theca = ca;
	_storedLineNumberTableBytes = 0;
	_storedLocalVariableTableBytes = 0;
	_runtimeFlags = runtimeFlags;
	
	/*Note:
	 * No need to fire asserts because cache will be marked as corrupt.
	 */
	if (false == isOk(currentThread, false, true, false)) {
		retval = false;
		goto done;
	}

	if (!startupForStats) {
		/* Memory protection of the entire cache is done by the caller when startupForStats is true */
		this->setPermission(currentThread, permSetter,
				getDebugAreaStartAddress(),
				getLNTNextAddress(),
				getLVTNextAddress(),
				getDebugAreaEndAddress());
	}

	_lntLastUpdate = getLNTNextAddress();
	_lvtLastUpdate = getLVTNextAddress();

	done:
	Trc_SHR_ClassDebugData_Init_Exit(currentThread, retval);
	return retval;
}

bool 
ClassDebugDataProvider::isOk(J9VMThread* currentThread, bool assertOnFailure, bool setCommitCorruptionCodes, bool assertForCorruptionCode)
{
	bool retval = true;
	UDATA startAddr = 0;
	UDATA endAddr = 0;
	IDATA failReason = NO_CORRUPTION;
	UDATA failValue = 0;
	PORT_ACCESS_FROM_JAVAVM(currentThread->javaVM);
		
	Trc_SHR_ClassDebugData_isOk_Entry(currentThread);

	/*Verify the debug area size is not larger than the allocated cache*/
	if (_theca->debugRegionSize > _theca->totalBytes) {
		DA_TRACE2(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_DEBUG_AREA_BAD_SIZE, _theca->debugRegionSize, _theca->totalBytes);
		Trc_SHR_ClassDebugData_isOk_AreaSizeCorrupt(currentThread, _theca->debugRegionSize, _theca->totalBytes);
		if (true == assertOnFailure) {
			Trc_SHR_Assert_False(_theca->debugRegionSize > _theca->totalBytes);
		}
		failReason = CACHE_DEBUGAREA_BAD_SIZE;
		failValue = startAddr;
		retval = false;
		goto done;
	}

	/*Verify the free space boundaries in the debug area looks sane*/
	startAddr = (UDATA) getLNTNextAddress();
	endAddr = (UDATA) getLVTNextAddress();
	if ((startAddr > endAddr) && (endAddr != 0)) {
	
		DA_TRACE2(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_DEBUG_AREA_BAD_FREE_SPACE, startAddr, endAddr);
		Trc_SHR_ClassDebugData_isOk_FreeSpaceCorrupt(currentThread, startAddr, endAddr);
		if (true == assertOnFailure) {
			Trc_SHR_Assert_False((startAddr > endAddr) && (endAddr != 0));
		}
		failReason = CACHE_DEBUGAREA_BAD_FREE_SPACE;
		failValue = startAddr;
		retval = false;
		goto done;
	}
	
	/*Verify the amount of free space in the debug area looks sane*/
	startAddr = (UDATA) getLNTNextAddress();
	endAddr = (UDATA) getLVTNextAddress();
	if ((endAddr - startAddr) > _theca->debugRegionSize) {
		DA_TRACE3(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_DEBUG_AREA_BAD_FREE_SPACE_SIZE, endAddr, startAddr, _theca->debugRegionSize);
		Trc_SHR_ClassDebugData_isOk_FreeSpaceSizeCorrupt(currentThread, endAddr, startAddr, _theca->debugRegionSize);
		if (true == assertOnFailure) {
			Trc_SHR_Assert_False((endAddr - startAddr) > _theca->debugRegionSize);
		}
		failReason = CACHE_DEBUGAREA_BAD_FREE_SPACE_SIZE;
		failValue = startAddr;
		retval = false;
		goto done;
	}
		
	/*Verify the allocated amount of LNT data in the debug area looks sane*/
	startAddr = (UDATA)getDebugAreaStartAddress();
	endAddr = (UDATA) getLNTNextAddress();
	if ((startAddr > endAddr) && (endAddr != 0)) {
		DA_TRACE2(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_DEBUG_AREA_BAD_LNT_HEADER_INFO, startAddr, endAddr);
		Trc_SHR_ClassDebugData_isOk_LNTCorrupt(currentThread, startAddr, endAddr);
		if (true == assertOnFailure) {
			Trc_SHR_Assert_False((startAddr > endAddr) && (endAddr != 0));
		}
		failReason = CACHE_DEBUGAREA_BAD_LNT_HEADER_INFO;
		failValue = endAddr;
		retval = false;
		goto done;
	}
	
	/*Verify the allocated amount of LVT data in the debug area looks sane*/
	startAddr = (UDATA) getLVTNextAddress();
	endAddr = (UDATA)getDebugAreaEndAddress();
	if (startAddr > endAddr) {
		DA_TRACE2(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_DEBUG_AREA_BAD_LVT_HEADER_INFO, startAddr, endAddr);
		Trc_SHR_ClassDebugData_isOk_LVTCorrupt(currentThread, startAddr, endAddr);
		if (true == assertOnFailure) {
			Trc_SHR_Assert_False((startAddr > endAddr) && (endAddr != 0));
		}
		failReason = CACHE_DEBUGAREA_BAD_LVT_HEADER_INFO;
		failValue = startAddr;
		retval = false;
		goto done;
	}
	
	/*Make sure the LNT and LVT data to commited make sense*/
	startAddr = (UDATA)getLNTNextAddress() + _storedLineNumberTableBytes;
	endAddr = (UDATA)getLVTNextAddress() - _storedLocalVariableTableBytes;	
	if ((startAddr > endAddr) && (endAddr != 0)) {
		/* During allocate setCommitCorruptionCodes is set to true.
		 * If there are problems any changes made will be rolled back
		 * when false is returned. The cache is not marked corrupt in
		 * this case
		 */
		if (true == setCommitCorruptionCodes) {
			DA_TRACE4(J9SHR_VERBOSEFLAG_ENABLE_VERBOSE_DEFAULT, J9NLS_ERROR, J9NLS_SHRC_DEBUG_AREA_BAD_SIZES_FOR_COMMIT, _storedLineNumberTableBytes, _storedLocalVariableTableBytes, startAddr, endAddr);
			failReason = CACHE_DEBUGAREA_BAD_SIZES_FOR_COMMIT;
			failValue = startAddr;
		}	
		Trc_SHR_ClassDebugData_isOk_BadSizesForCommitCorrupt(currentThread,
			(UDATA)getLNTNextAddress(), _storedLineNumberTableBytes,
			(UDATA)getLVTNextAddress(), _storedLocalVariableTableBytes);
		/* During allocate assertForCorruptionCode is set to true.
		 * If there are problems we want to assert to catch issues as
		 * early as possible.
		 */
		if ((true == assertOnFailure) || (true == assertForCorruptionCode)) {
			Trc_SHR_Assert_False((startAddr > endAddr) && (endAddr != 0));
		}
		retval = false;
		goto done;
	}		
	done:
	/*Keep the original corruption reason*/
	if (failureReason == NO_CORRUPTION) {
		failureReason = failReason;
		failureValue = failValue;
	}
	if (retval == true) {
		Trc_SHR_ClassDebugData_isOk_Exit(currentThread,1);
	} else {
		Trc_SHR_ClassDebugData_isOk_Exit(currentThread,0);
	}
	return retval;
}

UDATA
ClassDebugDataProvider::getJavacoreData(J9JavaVM *vm, J9SharedClassJavacoreDataDescriptor* descriptor, J9SharedCacheHeader * ca)
{
	/* As this may be called before the Init() function, ensure _theca is initialized
	 * so the helper functions will work.
	 */
	if (NULL == _theca) {
		_theca = ca;
	}
	
	descriptor->debugAreaSize = getDebugDataSize();
	descriptor->debugAreaLineNumberTableBytes = getLineNumberTableBytes();
	descriptor->debugAreaLocalVariableTableBytes = getLocalVariableTableBytes();
	descriptor->debugAreaUsed = 100;
	if (descriptor->debugAreaSize > 0) {
		descriptor->debugAreaUsed = ((descriptor->debugAreaSize - getFreeDebugSpaceBytes()) * 100) / descriptor->debugAreaSize;
	}
	
	return 1;
}

IDATA
ClassDebugDataProvider::allocateClassDebugData(J9VMThread* currentThread, U_16 classnameLength, const char* classnameData, const J9RomClassRequirements * sizes, J9SharedRomClassPieces * pieces, AbstractMemoryPermission * permSetter)
{
	IDATA retval = 0;
	bool allocatedSomething = false;
	U_64 sizeToBeAllocated = sizes->lineNumberTableSize + sizes->localVariableTableSize;

	Trc_SHR_ClassDebugData_allocateClassDebugData_Enter(currentThread, classnameLength, classnameData, sizes->lineNumberTableSize, sizes->localVariableTableSize);

	if (getFreeDebugSpaceBytes() < sizeToBeAllocated) {
		Trc_SHR_ClassDebugData_allocateClassDebugData_Event_NotEnoughFreeSpace(currentThread, classnameLength, classnameData, sizeToBeAllocated, getFreeDebugSpaceBytes());
		retval = -1;
		goto done;
	}

	if (0 < sizes->lineNumberTableSize) {
		pieces->lineNumberTable = getNextLineNumberTable(sizes->lineNumberTableSize);
		if (NULL == pieces->lineNumberTable) {
			Trc_SHR_ClassDebugData_allocateClassDebugData_Event_FailedToAllocLineNumberTable(currentThread, classnameLength, classnameData, sizes->lineNumberTableSize, sizes->localVariableTableSize);
			retval = -1;
			goto done;
		} else {
			if (NULL != permSetter) {
				UDATA pageSize = this->_theca->osPageSize;

				/* Mark the page as read-write where data is to be written */
				changePartialPageProtection(currentThread, permSetter, pieces->lineNumberTable, false);
				
				/* In case lineNumberTable data spills to page pointed by localVariableTable,
				 * then the page pointed by localVariableTable also needs to be marked as read-write.
				 * However, if we are also adding local variable table data, then that page would anyway be marked read-write
				 * when allocating space for local variable table data.
				 */
				if ((0 != pageSize) && (0 == sizes->localVariableTableSize)) {
					BlockPtr lntEnd = (BlockPtr)((UDATA)pieces->lineNumberTable + sizes->lineNumberTableSize);
					BlockPtr lntEndPage = (BlockPtr)ROUND_DOWN_TO(pageSize, (UDATA)lntEnd);
					BlockPtr lvtStart = (BlockPtr)getLVTNextAddress();
					BlockPtr lvtStartPage = (BlockPtr)ROUND_DOWN_TO(pageSize, (UDATA)lvtStart);

					if (lntEndPage == lvtStartPage) {
						changePartialPageProtection(currentThread, permSetter, lntEnd, false);
					}
				}
			}
		}
		allocatedSomething = true;
	}

	if (0 < sizes->localVariableTableSize) {
		pieces->localVariableTable = getNextLocalVariableTable(sizes->localVariableTableSize);
		if (NULL == pieces->localVariableTable) {
			Trc_SHR_ClassDebugData_allocateClassDebugData_Event_FailedToAllocLocalVariableTable(currentThread, classnameLength, classnameData, sizes->lineNumberTableSize, sizes->localVariableTableSize);
			retval = -1;
			goto done;
		} else {
			if (NULL != permSetter) {
				UDATA pageSize = this->_theca->osPageSize;

				/* Mark the page as read-write where data is to be written */
				changePartialPageProtection(currentThread, permSetter, (void *)((UDATA)pieces->localVariableTable + sizes->localVariableTableSize), false);
				
				/* In case localVariableTable data spills to page pointed by lineNumberTable,
				 * then the page pointed by lineNumberTable also needs to be marked as read-write.
				 * However, if we are also adding line number table data, then that page would anyway be marked read-write
				 * when allocating space for line number table data.
				 */
				if ((0 != pageSize) && (0 == sizes->lineNumberTableSize)) {
					BlockPtr lvtStartPage = (BlockPtr)ROUND_DOWN_TO(pageSize, (UDATA)pieces->localVariableTable);
					BlockPtr lntStart = (BlockPtr)getLNTNextAddress();
					BlockPtr lntStartPage = (BlockPtr)ROUND_DOWN_TO(pageSize, (UDATA)lntStart);

					if (lvtStartPage == lntStartPage) {
						changePartialPageProtection(currentThread, permSetter, pieces->localVariableTable, false);
					}
				}
			}
		}
		allocatedSomething = true;
	}

	if (allocatedSomething == false) {
		retval = -1;
		goto done;
	}

	if (0 != ((*_runtimeFlags) & J9SHR_RUNTIMEFLAG_DBG_EXTRA_CHECKS)) {
		/*Note:
		 * Asserts could be triggered in ::isOk if sizes to be committed are bad.
		 * In this case the cache is not marked as corrupt because changes can
		 * be rolled back at this point.
		 * 
		 * These checks are only performed when -Xshareclasses:doExtraAreaChecks 
		 * is used, in order to avoid the possibility of a performance hit.
		 */
		if (false == isOk(currentThread, false, false, true)) {
			retval = -1;
			goto done;
		}
	}

done:
	if (retval == -1) {
		if (NULL != pieces->localVariableTable) {
			pieces->localVariableTable = NULL;
		}
		if (NULL != pieces->lineNumberTable) {
			pieces->lineNumberTable = NULL;
		}
		rollbackClassDebugData(currentThread, classnameLength, classnameData, permSetter);
	}
	Trc_SHR_ClassDebugData_allocateClassDebugData_Exit(currentThread, classnameLength, classnameData, pieces->localVariableTable, pieces->lineNumberTable);
	return retval;
}

void
ClassDebugDataProvider::rollbackClassDebugData(J9VMThread* currentThread, U_16 classnameLength, const char* classnameData, AbstractMemoryPermission * permSetter)
{
	Trc_SHR_ClassDebugData_rollbackClassDebugData_Enter(currentThread, classnameLength, classnameData, _storedLineNumberTableBytes, _storedLocalVariableTableBytes);
	_storedLineNumberTableBytes = 0;
	_storedLocalVariableTableBytes = 0;
	/* Since we change the permission of pages to read-write in allocateClassDebugData(), 
	 * we need to restore their state to read-only when rolling back.
	 */
	if (NULL != permSetter) {
		changePartialPageProtection(currentThread, permSetter, getLNTNextAddress(), true);
		changePartialPageProtection(currentThread, permSetter, getLVTNextAddress(), true);
	}
	Trc_SHR_ClassDebugData_rollbackClassDebugData_Exit(currentThread, classnameLength, classnameData, _storedLineNumberTableBytes, _storedLocalVariableTableBytes);
}

bool
ClassDebugDataProvider::commitClassDebugData(J9VMThread* currentThread, U_16 classnameLength, const char* classnameData, AbstractMemoryPermission * permSetter)
{
	bool commitedData = false;
	/* Cache the number of bytes to be stored in local variables to be used in exit trace point
	 * as _storedLineNumberTableBytes and _storedLocalVariableTableBytes are reset to 0 
	 * during commitLineNumberTable() and commitLocalVariableTable().
	 * 
	 */
	U_32 preCommitLineNumberTableBytes = _storedLineNumberTableBytes;
	U_32 preCommitLocalVariableTableBytes = _storedLocalVariableTableBytes;
	void *lntNextAddr = NULL;
	void *lvtNextAddr = NULL;
	
	Trc_SHR_ClassDebugData_commitClassDebugData_Enter(currentThread, classnameLength, classnameData, _storedLineNumberTableBytes, _storedLocalVariableTableBytes);

	if (0 != ((*_runtimeFlags) & J9SHR_RUNTIMEFLAG_DBG_EXTRA_CHECKS)) {
		/*Note:
		 * Asserts could be triggered here if isOk fails, and the cache can
		 * be marked as corrupt by the caller if isOk fails.
		 * 
		 * These checks are only performed when -Xshareclasses:doExtraAreaChecks 
		 * is used, in order to avoid the possibility of a performance hit.
		 */
		if (false == isOk(currentThread, true, true, true)) {
			goto done;
		}
	}

	if (_storedLineNumberTableBytes <= 0 && _storedLocalVariableTableBytes <= 0) {
		Trc_SHR_ClassDebugData_NothingToCommit_Event(currentThread, classnameLength, classnameData, preCommitLineNumberTableBytes, preCommitLocalVariableTableBytes);
		goto done;
	}

	if (_storedLineNumberTableBytes > 0) {
		commitLineNumberTable();
		Trc_SHR_ClassDebugData_commitClassDebugData_LineNumberTable_Event(currentThread, classnameLength, classnameData, _storedLineNumberTableBytes, _storedLocalVariableTableBytes);
		commitedData = true;
	}
	if (_storedLocalVariableTableBytes > 0) {
		commitLocalVariableTable();
		Trc_SHR_ClassDebugData_commitClassDebugData_LocalVariableTable_Event(currentThread, classnameLength, classnameData, _storedLineNumberTableBytes, _storedLocalVariableTableBytes);
		commitedData = true;
	}

	if (false == commitedData) {
		goto done;
	}

	lntNextAddr = getLNTNextAddress();
	lvtNextAddr = getLVTNextAddress();
	
	this->setPermission(currentThread, permSetter,
			_lntLastUpdate,
			lntNextAddr,
			lvtNextAddr,
			_lvtLastUpdate);

	if (NULL != permSetter) {
		if (preCommitLineNumberTableBytes > 0) {
			changePartialPageProtection(currentThread, permSetter, lntNextAddr, true);
		}
		if (preCommitLocalVariableTableBytes > 0) {
			changePartialPageProtection(currentThread, permSetter, lvtNextAddr, true);
		}
	}

	_lntLastUpdate = lntNextAddr;
	_lvtLastUpdate = lvtNextAddr;

done:
	Trc_SHR_ClassDebugData_commitClassDebugData_Exit(currentThread, classnameLength, classnameData, preCommitLineNumberTableBytes, preCommitLocalVariableTableBytes);
	return commitedData;
}

bool 
ClassDebugDataProvider::isEnoughFreeSpace(UDATA size)
{
	Trc_SHR_ClassDebugData_isEnoughFreeSpace_Entry(size);
	if (getFreeDebugSpaceBytes() >= size) {
		Trc_SHR_ClassDebugData_isEnoughFreeSpace_TrueExit(size, getFreeDebugSpaceBytes());
		return true;
	} else {
		Trc_SHR_ClassDebugData_isEnoughFreeSpace_FalseExit(size, getFreeDebugSpaceBytes());
		return false;
	}
}

void *
ClassDebugDataProvider::getNextLineNumberTable(UDATA size)
{
	UDATA retval = 0;
	Trc_SHR_ClassDebugData_getNextLineNumberTable_Entry(size);
	retval = (UDATA) getLNTNextAddress();
	if (retval != 0) {
		_storedLineNumberTableBytes += (U_32)size;
	}
	Trc_SHR_ClassDebugData_getNextLineNumberTable_Exit(retval);
	return (void *) retval;
}

void *
ClassDebugDataProvider::getNextLocalVariableTable(UDATA size)
{
	UDATA retval = 0;
	Trc_SHR_ClassDebugData_getNextLocalVariableTable_Entry(size);
	retval = (UDATA) getLVTNextAddress();
	if (retval != 0) {
		_storedLocalVariableTableBytes += (U_32)size;
	}
	Trc_SHR_ClassDebugData_getNextLocalVariableTable_Exit((void *)(retval-size));
	return (void *) (retval-size);
}

void
ClassDebugDataProvider::commitLineNumberTable()
{
	UDATA newdatastart = 0;
	Trc_SHR_ClassDebugData_commitLineNumberTable_Entry(_storedLineNumberTableBytes);
	newdatastart = (UDATA) getLNTNextAddress();
	updateLNTWithSize(_storedLineNumberTableBytes);
	_storedLineNumberTableBytes = 0;
	Trc_SHR_ClassDebugData_commitLineNumberTable_Exit((void*)newdatastart);
	return;
}

void
ClassDebugDataProvider::commitLocalVariableTable()
{
	UDATA newdatastart = 0;
	Trc_SHR_ClassDebugData_commitLocalVariableTable_Entry(_storedLocalVariableTableBytes);
	updateLVTWithSize(_storedLocalVariableTableBytes);
	newdatastart = (UDATA) getLVTNextAddress();
	_storedLocalVariableTableBytes = 0;
	Trc_SHR_ClassDebugData_commitLocalVariableTable_Exit((void *)newdatastart);
	return;
}

U_32 
ClassDebugDataProvider::getDebugDataSize()
{
	Trc_SHR_ClassDebugData_getDebugDataSize_Entry();
	Trc_SHR_ClassDebugData_getDebugDataSize_Exit(GET_DEBUG_LENTH(_theca));
	return GET_DEBUG_LENTH(_theca);
}

U_32 
ClassDebugDataProvider::getFreeDebugSpaceBytes()
{
	UDATA freeStart = 0;
	UDATA freeEnd = 0;

	Trc_SHR_ClassDebugData_getFreeDebugSpaceBytes_Entry();

	freeStart = (UDATA) getLNTNextAddress();
	freeEnd = (UDATA) getLVTNextAddress();

	Trc_SHR_ClassDebugData_getFreeDebugSpaceBytes_Exit((freeEnd - freeStart));
	return (U_32)(freeEnd - freeStart);
}

U_32 
ClassDebugDataProvider::getLineNumberTableBytes()
{
	UDATA dataStart = 0;
	UDATA dataEnd = 0;

	Trc_SHR_ClassDebugData_getLineNumberTableBytes_Entry();

	dataStart = (UDATA)getDebugAreaStartAddress();
	dataEnd = (UDATA) getLNTNextAddress();

	Trc_SHR_ClassDebugData_getLineNumberTableBytes_Exit((dataEnd - dataStart));
	return (U_32)(dataEnd - dataStart);
}

U_32 
ClassDebugDataProvider::getLocalVariableTableBytes()
{
	UDATA dataStart = 0;
	UDATA dataEnd = 0;

	Trc_SHR_ClassDebugData_getLocalVariableTableBytes_Entry();

	dataStart = (UDATA) getLVTNextAddress();
	dataEnd = ((UDATA)getDebugAreaEndAddress());

	Trc_SHR_ClassDebugData_getLocalVariableTableBytes_Exit((dataEnd - dataStart));
	return (U_32)(dataEnd - dataStart);
}

void *
ClassDebugDataProvider::getDebugAreaStartAddress()
{
	void * retval = ((void *)GET_DEBUG_MEM_START(_theca));
	Trc_SHR_ClassDebugData_getDebugAreaStartAddress_Event(retval);
	return retval;
}
void *
ClassDebugDataProvider::getDebugAreaEndAddress()
{
	void * retval = (void *)(GET_DEBUG_MEM_END(_theca));
	Trc_SHR_ClassDebugData_getDebugAreaEndAddress_Event(retval);
	return retval;
}

bool 
ClassDebugDataProvider::HeaderInit(J9SharedCacheHeader * ca, U_32 size)
{
	UDATA startaddr = 0;
	UDATA endaddr = 0;

	Trc_SHR_ClassDebugData_HeaderInitForDebug_Entry(ca, size);

	SET_DEBUG_LENTH(ca, size);

	startaddr = ((UDATA)GET_DEBUG_MEM_START(ca));
	setLNTNextAddress(ca, (void*)startaddr);

	endaddr = (UDATA)GET_DEBUG_MEM_END(ca);
	setLVTNextAddress(ca, (void*)endaddr);
	
	Trc_SHR_ClassDebugData_HeaderInitForDebug_Exit(1);
	return true;
}

U_32 
ClassDebugDataProvider::getRecommendedPercentage()
{
#if defined (CREATE_CLASS_DEBUG_AREA)
	U_32 retval = 8;
#else
	U_32 retval = 0;
#endif
	Trc_SHR_ClassDebugData_getRecommendedPercentage_Event((UDATA)retval);
	return retval;
}

U_32 
ClassDebugDataProvider::recommendedSize(U_32 freeBlockBytesInCache, U_32 align)
{
#if defined (CREATE_CLASS_DEBUG_AREA)
	U_64 retval = ((U_64)freeBlockBytesInCache * ClassDebugDataProvider::getRecommendedPercentage())/100;

	if (retval > (retval % align)) {
		retval = retval - (retval % align);
	} else {
		retval = 0;
	}
	/*Use 8% of the free block space in the cache*/
	Trc_SHR_ClassDebugData_recommendedDebugAreaSize_Event((U_32)retval);
	return (U_32)retval;
#else
	return 0;
#endif
}

/**
 * Sets the protection for the memory pages of LineNumberTable and LocalVariableTable
 *
 * @param[in] currentThread Pointer to current J9VMThread.
 * @param[in] permSetter Specifies memory permission policy.
 * @param[in] lntProtectLow	Starting address of LineNumberTable where memory permission is to be set.
 * @param[in] lntProtectHigh Ending address of LineNumberTable where memory permission is to be set.
 * @param[in] lvtProtectLow	Starting address of LocalVariableTable where memory permission is to be set.
 * @param[in] lvtProtectHigh Ending address of LocalVariableTable where memory permission is to be set.
 * @param[in] readOnly True if the page(s) are to be marked read-only. False if they are to be marked read-write.
 * 
 * @return void
 */
void 
ClassDebugDataProvider::setPermission(J9VMThread* currentThread, AbstractMemoryPermission * permSetter,
			void * lntProtectLow,
			void * lntProtectHigh,
			void * lvtProtectLow,
			void * lvtProtectHigh,
			bool readOnly)
{
	UDATA pageSize = this->_theca->osPageSize;
	PORT_ACCESS_FROM_JAVAVM(currentThread->javaVM);

	Trc_SHR_ClassDebugData_setPermission_Enter(currentThread, permSetter,
		lntProtectLow,
		lntProtectHigh,
		lvtProtectLow,
		lvtProtectHigh,
		readOnly);

	/*These asserts are here simply to make sure this method is called correctly.*/
	Trc_SHR_Assert_True(lntProtectLow <= lntProtectHigh);
	Trc_SHR_Assert_True(lvtProtectLow <= lvtProtectHigh);

	if (NULL == permSetter) {
		/*When called from shrtest 'permSetter' MAY be null.*/
		goto done;
	}

	if (false == permSetter->isMemProtectEnabled()) {
		/*Only proceed if -Xshareclasses:mprotect=none was not used.*/
		Trc_SHR_ClassDebugData_setPermission_MprotectDisabled_Event(currentThread);
		goto done;
	}

	if (pageSize > 0) {
		bool doVerbosePages = permSetter->isVerbosePages();
		UDATA pageProtectFlag = J9PORT_PAGE_PROTECT_READ;
		
		
		if ((UnitTest::CLASS_DEBUG_DATA_TEST != UnitTest::unitTest)
			&& (ROUND_DOWN_TO(pageSize, (UDATA)lntProtectLow) < (UDATA)GET_DEBUG_MEM_START(_theca))
		) {
			/* Do not change the memory protection of header page. The header page is handled by (un)protectHeaderReadWriteArea(). */
			lntProtectLow = (void*)ROUND_UP_TO(pageSize, (UDATA)lntProtectLow);
			if (ROUND_DOWN_TO(pageSize, (UDATA)lntProtectHigh) < (UDATA)GET_DEBUG_MEM_START(_theca)) {
				/* Only touches the header page, do nothing here. The header page is handled by (un)protectHeaderReadWriteArea().
				 * In the case of unprotecting, the header page is already unprotected by j9shr_classStoreTransaction_createSharedClass() 
				 * before reaching here.
				 */
				goto done;
			}
		}

		if (!readOnly) {
			pageProtectFlag = J9PORT_PAGE_PROTECT_READ | J9PORT_PAGE_PROTECT_WRITE;
		}

		if ( lntProtectHigh == lvtProtectLow) {
			/* The whole debug area will be memory protected after the next call to setRegionPermissions().
			 * Only new data is mprotected, it is assumed the rest was protected earlier.
			 */
			UDATA startMprotect = ROUND_DOWN_TO(pageSize, (UDATA)lntProtectLow);
			UDATA endMprotect = ROUND_UP_TO(pageSize, (UDATA)lvtProtectHigh);
			UDATA totalRoundedSize = (endMprotect - startMprotect);

			if (totalRoundedSize > 0) {
				if (permSetter->setRegionPermissions(PORTLIB, (void *)startMprotect, totalRoundedSize, pageProtectFlag) != 0) {
					Trc_SHR_ClassDebugData_setPermission_CouldNotSetAllDebugData_Event(currentThread, (void *)startMprotect, (void *)endMprotect, totalRoundedSize, pageProtectFlag);
					Trc_SHR_Assert_ShouldNeverHappen();
				} else {
					Trc_SHR_ClassDebugData_setPermission_SetAllDebugData_Event(currentThread, (void *)startMprotect, (void *)endMprotect, totalRoundedSize, pageProtectFlag);
					if (doVerbosePages) {
						j9tty_printf(PORTLIB, "Set memory region permissions in ClassDebugDataProvider::commitClassDebugData() for debug data addresses %p to %p - for %d bytes to %zu\n", (void *)startMprotect, (void *)endMprotect, totalRoundedSize, pageProtectFlag);
					}
				}
			}
		} else {
			/* During regular operation memory protect only the newly filled parts of the debug area.
			 * During startup memory protect already contained data.
			 */
			UDATA roundLntProtectLow = ROUND_DOWN_TO(pageSize, (UDATA)lntProtectLow);
			UDATA roundLntProtectHigh = ROUND_DOWN_TO(pageSize, (UDATA)lntProtectHigh);
			UDATA lntRoundedSize = (roundLntProtectHigh - roundLntProtectLow);
			
			UDATA roundLvtProtectLow = ROUND_UP_TO(pageSize, (UDATA)lvtProtectLow);
			UDATA roundLvtProtectHigh = ROUND_UP_TO(pageSize, (UDATA)lvtProtectHigh);
			UDATA lvtRoundedSize = (roundLvtProtectHigh - roundLvtProtectLow);
	
			if (lntRoundedSize != 0) {
				if (permSetter->setRegionPermissions(PORTLIB, (void *)roundLntProtectLow, lntRoundedSize, pageProtectFlag) != 0) {
					Trc_SHR_ClassDebugData_setPermission_CouldNotSetLineNumberTableData_Event(currentThread, (void *)roundLntProtectLow, (void *)roundLntProtectHigh, lntRoundedSize, pageProtectFlag);
					Trc_SHR_Assert_ShouldNeverHappen();
				} else {
					Trc_SHR_ClassDebugData_setPermission_SetLineNumberTableData_Event(currentThread, (void *)roundLntProtectLow, (void *)roundLntProtectHigh, lntRoundedSize, pageProtectFlag);
					if (doVerbosePages) {
						j9tty_printf(PORTLIB, "Set memory region permissions in ClassDebugDataProvider::commitClassDebugData() for LineNumberTable addresses %p to %p - for %d bytes to %zu\n", (void *)roundLntProtectLow, (void *)roundLntProtectHigh, lntRoundedSize, pageProtectFlag);
					}
				}
			}

			if (lvtRoundedSize != 0) {
				if (permSetter->setRegionPermissions(PORTLIB, (void *)roundLvtProtectLow, lvtRoundedSize, pageProtectFlag) != 0) {
					Trc_SHR_ClassDebugData_setPermission_CouldNotSetLocalVariableTableData_Event(currentThread, (void *)roundLvtProtectLow, (void *)roundLvtProtectHigh, lvtRoundedSize, pageProtectFlag);
					Trc_SHR_Assert_ShouldNeverHappen();
				} else {
					Trc_SHR_ClassDebugData_setPermission_SetLocalVariableTableData_Event(currentThread, (void *)roundLvtProtectLow, (void *)roundLvtProtectHigh, lvtRoundedSize, pageProtectFlag);
					if (doVerbosePages) {
						j9tty_printf(PORTLIB, "Set memory region permissions in ClassDebugDataProvider::commitClassDebugData() for LocalVariableTable addresses %p to %p - for %d bytes to %zu\n", (void *)roundLvtProtectLow, (void *)roundLvtProtectHigh, lvtRoundedSize, pageProtectFlag);
					}
				}
			}
		}
	}

	done:
	Trc_SHR_ClassDebugData_setPermission_Exit(currentThread, permSetter,
		lntProtectLow,
		lntProtectHigh,
		lvtProtectLow,
		lvtProtectHigh,
		readOnly);
}


bool
ClassDebugDataProvider::processUpdates(J9VMThread* currentThread, AbstractMemoryPermission * permSetter)
{
	bool foundUpdate = false;

	/* It is important to call getLNTNextAddress() and getLVTNextAddress() only
	 * once b/c the contents of the cache header may change while we are in this
	 * method.
	 */
	void * lntCurrent = getLNTNextAddress();
	void * lvtCurrent = getLVTNextAddress();

	Trc_SHR_ClassDebugData_processUpdates_Entry(currentThread, permSetter);

	if (_lntLastUpdate != lntCurrent) {
		foundUpdate = true;
		Trc_SHR_ClassDebugData_processUpdates_FoundNewLineNumData(currentThread, permSetter, ((UDATA)lntCurrent - (UDATA)_lntLastUpdate));
	}

	if (_lvtLastUpdate != lvtCurrent) {
		foundUpdate = true;
		Trc_SHR_ClassDebugData_processUpdates_FoundNewLocalVarData(currentThread, permSetter, ((UDATA)_lvtLastUpdate - (UDATA)lvtCurrent));
	}

	if (true == foundUpdate) {
		this->setPermission(currentThread, permSetter, _lntLastUpdate, lntCurrent, lvtCurrent, _lvtLastUpdate);
		_lntLastUpdate = lntCurrent;
		_lvtLastUpdate = lvtCurrent;

	}

	Trc_SHR_ClassDebugData_processUpdates_Exit(currentThread, permSetter, (true==foundUpdate)?"true":"false");
	return foundUpdate;
}

void *
ClassDebugDataProvider::getLNTNextAddress() {
	return WSRP_GET(_theca->lineNumberTableNextSRP, void*);
}

void *
ClassDebugDataProvider::getLVTNextAddress() {
	return WSRP_GET(_theca->localVariableTableNextSRP, void*);
}

void 
ClassDebugDataProvider::setLNTNextAddress(J9SharedCacheHeader * ca, void *addr)
{
	UDATA newValue = ((UDATA)addr) - ((UDATA)&(ca->lineNumberTableNextSRP));
	ca->lineNumberTableNextSRP = newValue;
}

void
ClassDebugDataProvider::setLVTNextAddress(J9SharedCacheHeader * ca, void *addr)
{
	UDATA newValue = ((UDATA)addr) - ((UDATA)&(ca->localVariableTableNextSRP));
	ca->localVariableTableNextSRP = newValue;
}

void
ClassDebugDataProvider::updateLNTWithSize(UDATA size)
{
	_theca->lineNumberTableNextSRP = _theca->lineNumberTableNextSRP + size;
}

void
ClassDebugDataProvider::updateLVTWithSize(UDATA size)
{
	_theca->localVariableTableNextSRP = _theca->localVariableTableNextSRP - size;
}

/**
 * This method is called when the ROMClass area in shared cache is full.
 * As a result no new data can be added to class debug area,
 * and the partially filled page and unused pages can be mprotected.
 * 
 * @param[in] currentThread The current J9VMThread
 * @param[in] permSetter specifies memory permission policy
 * 
 * @return void
 */
void
ClassDebugDataProvider::protectUnusedPages(J9VMThread* currentThread, AbstractMemoryPermission * permSetter)
{
	void *lntStartAddr = getLNTNextAddress();
	void *lvtStartAddr = getLVTNextAddress();
	/* Setting the end of LineNumberTable area to be protected same as start of LocalVariableTable area should
	 * cover the partially filled page in LineNumberTable area and LocalVariableTable area
	 * and any unused pages in between.
	 */
	void *lntEndAddr = lvtStartAddr;

	/**
	 *
	 *   LineNumberTable                        LocalVariableTable
	 * *---------------------*--------------------*-------------------*
	 * |    |    |    |      |                    |    |    |    |    |
	 * |    |    |    |      | ... free space ... |    |    |    |    |
	 * |    |    |    |      |                    |    |    |    |    |
	 * *---------------------*--------------------*-------------------*
	 *                   ^                           ^
	 *              lntStartAddr                lvtStartAddr(=lntEndAddr)
	 *                |--------------------------------|
	 *             pages to be mprotected by setPermission()
	 */

	this->setPermission(currentThread, permSetter, lntStartAddr, lntEndAddr, lvtStartAddr, lvtStartAddr);
}


/**
 * This method unprotect the unused pages in debug area.
 *
 * @param[in] currentThread The current J9VMThread
 * @param[in] permSetter specifies memory permission policy
 *
 * @return void
 */
void
ClassDebugDataProvider::unprotectUnusedPages(J9VMThread* currentThread, AbstractMemoryPermission * permSetter)
{
	void *lntStartAddr = getLNTNextAddress();
	void *lvtStartAddr = getLVTNextAddress();
	/* Setting the end of LineNumberTable area to be unprotected same as start of LocalVariableTable area should
	 * cover the partially filled page in LineNumberTable area and LocalVariableTable area
	 * and any unused pages in between.
	 */
	void *lntEndAddr = lvtStartAddr;

	/**
	 *
	 *   LineNumberTable                        LocalVariableTable
	 * *---------------------*--------------------*-------------------*
	 * |    |    |    |      |                    |    |    |    |    |
	 * |    |    |    |      | ... free space ... |    |    |    |    |
	 * |    |    |    |      |                    |    |    |    |    |
	 * *---------------------*--------------------*-------------------*
	 *                   ^                           ^
	 *              lntStartAddr                lvtStartAddr(=lntEndAddr)
	 *                |--------------------------------|
	 *             pages to be unprotected by setPermission()
	 */

	this->setPermission(currentThread, permSetter, lntStartAddr, lntEndAddr, lvtStartAddr, lvtStartAddr, false);
}

/**
 * Protect the last used pages for storing lineNumberTable and localVariableTable in class debug region
 *
 * @param [in] currentThread pointer to current J9VMThread
 * @param [in] permSetter specifies memory permission policy
 * @param [in] phaseCheck Whether to check JVM phase when protecting the pages.
 *
 * @return void
 */
void
ClassDebugDataProvider::protectPartiallyFilledPages(J9VMThread* currentThread, AbstractMemoryPermission * permSetter, bool phaseCheck)
{
	void *lntStartAddr = getLNTNextAddress();
	void *lvtStartAddr = getLVTNextAddress();

	if (NULL != permSetter) {
		changePartialPageProtection(currentThread, permSetter, lntStartAddr, true, phaseCheck);
		changePartialPageProtection(currentThread, permSetter, lvtStartAddr, true, phaseCheck);
	}
}

/**
 * Unprotect the last used pages for storing lineNumberTable and localVariableTable in class debug region
 *
 * @param[in] currentThread pointer to current J9VMThread
 * @param[in] permSetter specifies memory permission policy
 * @param[in] phaseCheck Whether to check JVM phase when protecting the pages.
 *
 * @return void
 */
void
ClassDebugDataProvider::unprotectPartiallyFilledPages(J9VMThread* currentThread, AbstractMemoryPermission * permSetter, bool phaseCheck)
{
	void *lntStartAddr = getLNTNextAddress();
	void *lvtStartAddr = getLVTNextAddress();

	if (NULL != permSetter) {
		changePartialPageProtection(currentThread, permSetter, lntStartAddr, false, phaseCheck);
		changePartialPageProtection(currentThread, permSetter, lvtStartAddr, false, phaseCheck);
	}
}

void
ClassDebugDataProvider::changePartialPageProtection(J9VMThread *currentThread, AbstractMemoryPermission * permSetter, void *addr, bool readOnly, bool phaseCheck)
{
	UDATA pageSize = this->_theca->osPageSize;
	/* If it touches the header page, do nothing here. The header page is handled by (un)protectHeaderReadWriteArea(). */
	bool skip = (0 == pageSize) || (ROUND_DOWN_TO(pageSize, (UDATA)addr) < (UDATA)GET_DEBUG_MEM_START(_theca));
	skip = skip && (UnitTest::CLASS_DEBUG_DATA_TEST != UnitTest::unitTest);
	if (skip) {
		
		return;
	}
	permSetter->changePartialPageProtection(currentThread, addr, readOnly, phaseCheck);
}

