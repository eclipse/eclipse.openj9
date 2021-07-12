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

#ifndef CLASSDEBUGDATAPROVIDER_H_
#define CLASSDEBUGDATAPROVIDER_H_

/* @ddr_namespace: default */
#include "j9.h"
#include "SCTransactionCTypes.h"
#include "AbstractMemoryPermission.hpp"

class ClassDebugDataProvider
{
public:	
	void initialize();
	bool Init(J9VMThread* currentThread, J9SharedCacheHeader * ca, AbstractMemoryPermission * permSetter, UDATA verboseFlags, U_64 * runtimeFlags, bool startupForStats);
	UDATA getJavacoreData(J9JavaVM *vm, J9SharedClassJavacoreDataDescriptor* descriptor, J9SharedCacheHeader * ca);
	IDATA allocateClassDebugData(J9VMThread* currentThread, U_16 classnameLength, const char* classnameData, const J9RomClassRequirements * sizes, J9SharedRomClassPieces * pieces, AbstractMemoryPermission * permSetter);
	void rollbackClassDebugData(J9VMThread* currentThread, U_16 classnameLength, const char* classnameData, AbstractMemoryPermission * permSetter);
	bool commitClassDebugData(J9VMThread* currentThread, U_16 classnameLength, const char* classnameData, AbstractMemoryPermission * permSetter);
	U_32 getDebugDataSize();
	U_32 getFreeDebugSpaceBytes();
	U_32 getLineNumberTableBytes();
	U_32 getLocalVariableTableBytes();
	void * getDebugAreaStartAddress();
	void * getDebugAreaEndAddress();
	bool processUpdates(J9VMThread* currentThread, AbstractMemoryPermission * permSetter);
	IDATA getFailureReason() {
		return failureReason;
	}
	UDATA getFailureValue() {
		return failureValue;
	}

	U_32 getStoredDebugDataBytes(void) {
		return _storedLineNumberTableBytes + _storedLocalVariableTableBytes;
	}

	void protectUnusedPages(J9VMThread* currentThread, AbstractMemoryPermission * permSetter);
	void unprotectUnusedPages(J9VMThread* currentThread, AbstractMemoryPermission * permSetter);
	void protectPartiallyFilledPages(J9VMThread* currentThread, AbstractMemoryPermission * permSetter, bool phaseCheck = true);
	void unprotectPartiallyFilledPages(J9VMThread* currentThread, AbstractMemoryPermission * permSetter, bool phaseCheck = true);

	static bool HeaderInit(J9SharedCacheHeader * ca, U_32 size);
	static U_32 getRecommendedPercentage();
	static U_32 recommendedSize(U_32 freeBlockBytesInCache, U_32 align);

private:
	J9SharedCacheHeader * _theca;
	U_32 _storedLineNumberTableBytes;
	U_32 _storedLocalVariableTableBytes;
	void * _lntLastUpdate;
	void * _lvtLastUpdate;
	IDATA failureReason;
	UDATA failureValue;
	UDATA _verboseFlags;
	U_64 * _runtimeFlags;

	void *operator new(size_t size);
	ClassDebugDataProvider();
	bool isEnoughFreeSpace(UDATA size);
	void * getNextLineNumberTable(UDATA size);
	void * getNextLocalVariableTable(UDATA size);
	void commitLineNumberTable();
	void commitLocalVariableTable();
	void setPermission(J9VMThread* currentThread, AbstractMemoryPermission * permSetter,
			void * lntProtectLow,
			void * lntProtectHigh,
			void * lvtProtectLow,
			void * lvtProtectHigh,
			bool readOnly = true);
	void * getLNTNextAddress();
	void * getLVTNextAddress();
	static void setLNTNextAddress(J9SharedCacheHeader * ca, void *addr);
	static void setLVTNextAddress(J9SharedCacheHeader * ca, void *addr);
	void updateLNTWithSize(UDATA size);
	void updateLVTWithSize(UDATA size);
	bool isOk(J9VMThread* currentThread, bool assertOnFailure, bool setCorruptionCode, bool assertForCorruptionCode);
	void changePartialPageProtection(J9VMThread *currentThread, AbstractMemoryPermission * permSetter, void *addr, bool readOnly, bool phaseCheck = true);
	friend class DebugAreaUnitTests;

};

#endif /* CLASSDEBUGDATAPROVIDER_H_ */
