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
#include "ClassDebugDataTests.h"
#include "ClassDebugDataProvider.hpp"
#include "AbstractMemoryPermission.hpp"
#include "SCTestCommon.h"

#define _UTE_STATIC_
#include "ut_j9shr.h"

#define TEST_PASS 0
#define TEST_ERROR -1


static ClassDebugDataProvider * debugProviderMemalloc(J9JavaVM* vm);
static void * debugmemalloc(J9JavaVM* vm);
static void debugmemfree(J9JavaVM* vm, void * memory);

/*
 * The class is used for testing mprotect logic in ClassDebugDataProvider
 */
class MprotectHelper : public AbstractMemoryPermission {
	private:
		UDATA numberOfProtects;
		UDATA amountProtected;
		UDATA numberOfPartialPageReadOnlyProtects;
		UDATA numberOfPartialPageReadWriteProtects;

	public:
		MprotectHelper() {
			numberOfProtects = 0;
			amountProtected = 0;
			numberOfPartialPageReadOnlyProtects = 0;
			numberOfPartialPageReadWriteProtects = 0;
		}
		~MprotectHelper() {
		}
		IDATA setRegionPermissions(J9PortLibrary* portLibrary, void *address, UDATA length, UDATA flags) {
			numberOfProtects += 1;
			amountProtected += length;
			return 0;
		}
		bool isVerbosePages(void) {
			return true;
		}

		bool isMemProtectEnabled() {
			return true;
		}

		bool isMemProtectPartialPagesEnabled() {
			return true;
		}

		void changePartialPageProtection(J9VMThread *currentThread, void *addr, bool readOnly, bool phaseCheck = true) {
			if (readOnly) {
				numberOfPartialPageReadOnlyProtects += 1;
			} else {
				numberOfPartialPageReadWriteProtects += 1;
			}
			return;
		}

		UDATA getNumberOfProtects() {
			return numberOfProtects;
		}

		UDATA getAmountProtected() {
			return amountProtected;
		}

		UDATA getNumberOfPartialPageReadOnlyProtects() {
			return numberOfPartialPageReadOnlyProtects;
		}

		UDATA getNumberOfPartialPageReadWriteProtects() {
			return numberOfPartialPageReadWriteProtects;
		}

		void reset() {
			numberOfProtects = 0;
			amountProtected = 0;
			numberOfPartialPageReadOnlyProtects = 0;
			numberOfPartialPageReadWriteProtects = 0;
		}
};

class DebugAreaUnitTests {
public:
	IDATA debugmemtest1(J9JavaVM* vm);
	IDATA debugmemtest2(J9JavaVM* vm);
	IDATA debugmemtest3(J9JavaVM* vm);
	IDATA debugmemtest4(J9JavaVM* vm);
	IDATA debugmemtest5(J9JavaVM* vm);
	IDATA debugmemtest6(J9JavaVM* vm);
	IDATA debugmemtest7(J9JavaVM* vm);
	IDATA debugmemtest8(J9JavaVM* vm);
	IDATA debugmemtest9(J9JavaVM* vm);
	IDATA debugmemtest10(J9JavaVM* vm);
	IDATA debugmemtest11(J9JavaVM* vm);
	IDATA debugmemtest12(J9JavaVM* vm);
	IDATA debugmemtest13(J9JavaVM* vm);
	IDATA debugmemtest14(J9JavaVM* vm);
	IDATA debugmemtest15(J9JavaVM* vm);
};

IDATA
testClassDebugDataTests(J9JavaVM* vm)
{
	const char * testName = "testClassDebugDataTests";
	IDATA rc = TEST_PASS;
	PORT_ACCESS_FROM_JAVAVM(vm);
	const UDATA onemeg = (1024 * 1024);
	DebugAreaUnitTests tester;
	
	UnitTest::unitTest = UnitTest::CLASS_DEBUG_DATA_TEST;

	if (ClassDebugDataProvider::recommendedSize(onemeg, 4) > 0) {
		rc |= tester.debugmemtest1(vm);
		rc |= tester.debugmemtest2(vm);
		rc |= tester.debugmemtest3(vm);
		rc |= tester.debugmemtest4(vm);
		rc |= tester.debugmemtest5(vm);
		rc |= tester.debugmemtest6(vm);
		rc |= tester.debugmemtest7(vm);
		rc |= tester.debugmemtest8(vm);
		rc |= tester.debugmemtest9(vm);
		rc |= tester.debugmemtest10(vm);
		rc |= tester.debugmemtest11(vm);
		rc |= tester.debugmemtest12(vm);
		rc |= tester.debugmemtest13(vm);
		rc |= tester.debugmemtest14(vm);
		rc |= tester.debugmemtest15(vm);
	} else {
		/* This is being done only to support the initial checking of this code which will create a debug area size of 0.
		 * This is being done just to ensure there are no regressions.
		 */
		j9tty_printf(PORTLIB, "%s: Skipping some tests b/c recommendedSize() returned 0 bytes.\n", testName);
	}

	j9tty_printf(PORTLIB, "%s: %s\n", testName, TEST_PASS == rc ? "PASS" : "FAIL");

	UnitTest::unitTest = UnitTest::NO_TEST;

	if (rc == TEST_ERROR) {
		return TEST_ERROR;
	} else {
		return TEST_PASS;
	}
}

static ClassDebugDataProvider * 
debugProviderMemalloc(J9JavaVM* vm)
{
	PORT_ACCESS_FROM_JAVAVM(vm);
	UDATA memorysize = sizeof(ClassDebugDataProvider);
	void * memory = j9mem_allocate_memory(memorysize, J9MEM_CATEGORY_CLASSES);
	memset(memory, 0, memorysize);
	return (ClassDebugDataProvider *)memory;
}

static void *
debugmemalloc(J9JavaVM* vm)
{
	PORT_ACCESS_FROM_JAVAVM(vm);
	const UDATA onemeg = (1024 * 1024);
	UDATA memorysize = onemeg;
	void * memory = j9mem_allocate_memory(memorysize, J9MEM_CATEGORY_CLASSES);
	memset(memory, 0, onemeg);
	J9SharedCacheHeader * theca = (J9SharedCacheHeader *) memory;
	theca->totalBytes = (U_32)memorysize;
	/* We set the osPageSize to 1 byte for mprotect tests. We can't use a nice
	 * value like 4096 because memory returned by j9mem_allocate_memory() is
	 * not necessarily page aligned. So this will suffice.
	 */
	theca->osPageSize = 1;
	ClassDebugDataProvider::HeaderInit(theca, ClassDebugDataProvider::recommendedSize(onemeg, 4));
	return memory;
}

void
debugmemfree(J9JavaVM* vm, void * memory)
{
	PORT_ACCESS_FROM_JAVAVM(vm);
	j9mem_free_memory(memory);
	return;
}

IDATA
DebugAreaUnitTests::debugmemtest1(J9JavaVM* vm)
{
	IDATA retval = TEST_PASS;
	const char * testName = "debugmemtest1";
	J9SharedCacheHeader * theca = (J9SharedCacheHeader *) debugmemalloc(vm);
	U_64 runtimeFlags = J9SHR_RUNTIMEFLAG_DBG_EXTRA_CHECKS;
	PORT_ACCESS_FROM_JAVAVM(vm);

	j9tty_printf(PORTLIB, "%s: validate ::Init() always passes.\n", testName);

	ClassDebugDataProvider * debugarea = debugProviderMemalloc(vm);
	
	if (debugarea->Init(vm->mainThread, theca, NULL, 0, &runtimeFlags, false) == false) {
		ERRPRINTF("debugarea->Init(...) == false\n");
		retval = TEST_ERROR;
		goto done;
	}

	done:
	debugmemfree(vm, debugarea);
	debugmemfree(vm, theca);
	return retval;
}

IDATA
DebugAreaUnitTests::debugmemtest2(J9JavaVM* vm)
{
	IDATA retval = TEST_PASS;
	const char * testName = "debugmemtest2";
	J9SharedCacheHeader * theca = (J9SharedCacheHeader *) debugmemalloc(vm);
	UDATA size;
	U_64 runtimeFlags = J9SHR_RUNTIMEFLAG_DBG_EXTRA_CHECKS;
	PORT_ACCESS_FROM_JAVAVM(vm);

	j9tty_printf(PORTLIB, "%s: Test empty debug area (Init, getDebugDataSize, getFreeDebugSpaceBytes, getLineNumberTableBytes, and getLocalVariableTableBytes).\n", testName);

	ClassDebugDataProvider * debugarea = debugProviderMemalloc(vm);

	if (debugarea->Init(vm->mainThread, theca, NULL, 0, &runtimeFlags, false) == false) {
		ERRPRINTF("debugarea->Init(...) == false\n");
		retval = TEST_ERROR;
		goto done;
	}

	size = debugarea->getDebugDataSize();
	if (size == 0 || size > 0xffffffff) {
		ERRPRINTF1("debugarea->getDebugDataSize() has returned an invalid size %u\n",size);
		retval = TEST_ERROR;
		goto done;
	} else {
		j9tty_printf(PORTLIB, "\tdebug area size is %u B (%u KB)\n", size, size / 1024);
	}

	if (size != debugarea->getFreeDebugSpaceBytes()) {
		ERRPRINTF("size != getFreeDebugSpaceBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", size, debugarea->getFreeDebugSpaceBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if (0 != debugarea->getLineNumberTableBytes()) {
		ERRPRINTF("0 != getLineNumberTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: 0 != %u\n", debugarea->getLineNumberTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if (0 != debugarea->getLocalVariableTableBytes()) {
		ERRPRINTF("0 != getLocalVariableTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: 0 != %u\n", debugarea->getLocalVariableTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	done:
	debugmemfree(vm, debugarea);
	debugmemfree(vm, theca);
	return retval;
}

IDATA
DebugAreaUnitTests::debugmemtest3(J9JavaVM* vm)
{
	IDATA retval = TEST_PASS;
	const char * testName = "debugmemtest3";
	J9SharedCacheHeader * theca = (J9SharedCacheHeader *) debugmemalloc(vm);
	UDATA initialFreeSize;
	U_64 runtimeFlags = J9SHR_RUNTIMEFLAG_DBG_EXTRA_CHECKS;
	PORT_ACCESS_FROM_JAVAVM(vm);
	U_32 allocsize = 128;
	J9RomClassRequirements sizes;
	J9SharedRomClassPieces pieces;

	memset((void *)&sizes, 0, sizeof(J9RomClassRequirements));
	memset((void *)&pieces, 0, sizeof(J9SharedRomClassPieces));

	j9tty_printf(PORTLIB, "%s: Test allocate from LineNumberTable of debug area.\n", testName);

	ClassDebugDataProvider * debugarea = debugProviderMemalloc(vm);

	if (debugarea->Init(vm->mainThread, theca, NULL, 0, &runtimeFlags, false) == false) {
		ERRPRINTF("debugarea->Init(...) == false\n");
		retval = TEST_ERROR;
		goto done;
	}

	initialFreeSize = debugarea->getFreeDebugSpaceBytes();

	sizes.lineNumberTableSize = allocsize;
	if (debugarea->allocateClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, (const J9RomClassRequirements *)&sizes, &pieces, NULL) != 0) {
		ERRPRINTF("allocateClassDebugData() failed\n");
		retval = TEST_ERROR;
		goto done;
	}
	debugarea->commitClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, NULL);

	if (initialFreeSize < debugarea->getFreeDebugSpaceBytes()) {
		ERRPRINTF("initialFreeSize < debugarea->getFreeDebugSpaceBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u < %u\n", initialFreeSize, debugarea->getFreeDebugSpaceBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if (allocsize != debugarea->getLineNumberTableBytes()) {
		ERRPRINTF("allocsize != getLineNumberTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", allocsize, debugarea->getLineNumberTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if (0 != debugarea->getLocalVariableTableBytes()) {
		ERRPRINTF("0 != getLocalVariableTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: 0 != %u\n", debugarea->getLocalVariableTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	done:
	debugmemfree(vm, debugarea);
	debugmemfree(vm, theca);
	return retval;
}

IDATA
DebugAreaUnitTests::debugmemtest4(J9JavaVM* vm)
{
	IDATA retval = TEST_PASS;
	const char * testName = "debugmemtest4";
	J9SharedCacheHeader * theca = (J9SharedCacheHeader *) debugmemalloc(vm);
	UDATA initialFreeSize;
	U_64 runtimeFlags = J9SHR_RUNTIMEFLAG_DBG_EXTRA_CHECKS;
	PORT_ACCESS_FROM_JAVAVM(vm);
	U_32 allocsize = 128;
	J9RomClassRequirements sizes;
	J9SharedRomClassPieces pieces;
	memset((void *)&sizes, 0, sizeof(J9RomClassRequirements));
	memset((void *)&pieces, 0, sizeof(J9SharedRomClassPieces));

	j9tty_printf(PORTLIB, "%s: Test allocate from LocalVariableTable of debug area.\n", testName);

	ClassDebugDataProvider * debugarea = debugProviderMemalloc(vm);

	if (debugarea->Init(vm->mainThread, theca, NULL, 0, &runtimeFlags, false) == false) {
		ERRPRINTF("debugarea->Init(...) == false\n");
		retval = TEST_ERROR;
		goto done;
	}

	initialFreeSize = debugarea->getFreeDebugSpaceBytes();
	
	sizes.localVariableTableSize = allocsize;
	if (debugarea->allocateClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, (const J9RomClassRequirements *)&sizes, &pieces, NULL) != 0) {
		ERRPRINTF("allocateClassDebugData() failed\n");
		retval = TEST_ERROR;
		goto done;
	}
	debugarea->commitClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, NULL);

	if (initialFreeSize < debugarea->getFreeDebugSpaceBytes()) {
		ERRPRINTF("initialFreeSize < debugarea->getFreeDebugSpaceBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u < %u\n", initialFreeSize, debugarea->getFreeDebugSpaceBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if (allocsize != debugarea->getLocalVariableTableBytes()) {
		ERRPRINTF("allocsize != getLocalVariableTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", allocsize, debugarea->getLocalVariableTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if (0 != debugarea->getLineNumberTableBytes()) {
		ERRPRINTF("0 != getLineNumberTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: 0 != %u\n", debugarea->getLineNumberTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	done:
	debugmemfree(vm, debugarea);
	debugmemfree(vm, theca);
	return retval;
}

IDATA
DebugAreaUnitTests::debugmemtest5(J9JavaVM* vm)
{
	IDATA retval = TEST_PASS;
	const char * testName = "debugmemtest5";
	J9SharedCacheHeader * theca = (J9SharedCacheHeader *) debugmemalloc(vm);
	UDATA initialFreeSize;
	U_64 runtimeFlags = J9SHR_RUNTIMEFLAG_DBG_EXTRA_CHECKS;
	PORT_ACCESS_FROM_JAVAVM(vm);
	U_32 allocsize = 128;
	J9RomClassRequirements sizes;
	J9SharedRomClassPieces pieces;
	memset((void *)&sizes, 0, sizeof(J9RomClassRequirements));
	memset((void *)&pieces, 0, sizeof(J9SharedRomClassPieces));

	j9tty_printf(PORTLIB, "%s: Test allocate from LineNumberTable and LocalVariableTable of debug area.\n", testName);

	ClassDebugDataProvider * debugarea = debugProviderMemalloc(vm);

	if (debugarea->Init(vm->mainThread, theca, NULL, 0, &runtimeFlags, false) == false) {
		ERRPRINTF("debugarea->Init(...) == false\n");
		retval = TEST_ERROR;
		goto done;
	}

	initialFreeSize = debugarea->getFreeDebugSpaceBytes();

	sizes.lineNumberTableSize = allocsize;
	sizes.localVariableTableSize = allocsize;
	if (debugarea->allocateClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, (const J9RomClassRequirements *)&sizes, &pieces, NULL) != 0) {
		ERRPRINTF("allocateClassDebugData() failed\n");
		retval = TEST_ERROR;
		goto done;
	}
	debugarea->commitClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, NULL);

	if ((initialFreeSize - (allocsize * 2)) != debugarea->getFreeDebugSpaceBytes()) {
		ERRPRINTF("(initialFreeSize-(allocsize*2)) != debugarea->getFreeDebugSpaceBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", (initialFreeSize - (allocsize * 2)), debugarea->getFreeDebugSpaceBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if (allocsize != debugarea->getLocalVariableTableBytes()) {
		ERRPRINTF("0 != getLocalVariableTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", 0, debugarea->getLocalVariableTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if (allocsize != debugarea->getLineNumberTableBytes()) {
		ERRPRINTF("0 != getLineNumberTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", allocsize, debugarea->getLocalVariableTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	done:
	debugmemfree(vm, debugarea);
	debugmemfree(vm, theca);
	return retval;
}

IDATA
DebugAreaUnitTests::debugmemtest6(J9JavaVM* vm)
{
	IDATA retval = TEST_PASS;
	const char * testName = "debugmemtest6";
	J9SharedCacheHeader * theca = (J9SharedCacheHeader *) debugmemalloc(vm);
	U_32 initialFreeSize;
	U_64 runtimeFlags = J9SHR_RUNTIMEFLAG_DBG_EXTRA_CHECKS;
	PORT_ACCESS_FROM_JAVAVM(vm);
	U_32 alloctopsize;
	U_32 allocbotsize;
	J9RomClassRequirements sizes;
	J9SharedRomClassPieces pieces;
	memset((void *)&sizes, 0, sizeof(J9RomClassRequirements));
	memset((void *)&pieces, 0, sizeof(J9SharedRomClassPieces));

	void * alloctopmem = NULL;
	void * allocbotmem = NULL;

	j9tty_printf(PORTLIB, "%s: Test filling the debug area.\n", testName);

	ClassDebugDataProvider * debugarea = debugProviderMemalloc(vm);

	if (debugarea->Init(vm->mainThread, theca, NULL, 0, &runtimeFlags, false) == false) {
		ERRPRINTF("debugarea->Init(...) == false\n");
		retval = TEST_ERROR;
		goto done;
	}

	initialFreeSize = debugarea->getFreeDebugSpaceBytes();
	alloctopsize = initialFreeSize / 2 + initialFreeSize % 2;
	allocbotsize = initialFreeSize / 2;

	sizes.lineNumberTableSize = alloctopsize;
	sizes.localVariableTableSize = allocbotsize;
	if (debugarea->allocateClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, (const J9RomClassRequirements *)&sizes, &pieces, NULL) != 0) {
		ERRPRINTF("allocateClassDebugData() failed\n");
		retval = TEST_ERROR;
		goto done;
	}
	alloctopmem = pieces.lineNumberTable;
	allocbotmem = pieces.localVariableTable;
	debugarea->commitClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, NULL);
	

	if (alloctopmem == NULL) {
		ERRPRINTF("alloctopmem == NULL\n");
		j9tty_printf(PORTLIB, "\tError: %p == %p\n", 0, alloctopmem, NULL);
		retval = TEST_ERROR;
		goto done;
	}

	if (allocbotmem == NULL) {
		ERRPRINTF("allocbotmem != NULL\n");
		j9tty_printf(PORTLIB, "\tError: %p == %p\n", 0, allocbotmem, NULL);
		retval = TEST_ERROR;
		goto done;
	}

	if (0 != debugarea->getFreeDebugSpaceBytes()) {
		ERRPRINTF("0 != debugarea->getFreeDebugSpaceBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", 0, debugarea->getFreeDebugSpaceBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if (allocbotsize != debugarea->getLocalVariableTableBytes()) {
		ERRPRINTF("allocbotsize != getLocalVariableTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", allocbotsize, debugarea->getLocalVariableTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if (alloctopsize != debugarea->getLineNumberTableBytes()) {
		ERRPRINTF("alloctopsize != getLineNumberTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", alloctopsize, debugarea->getLocalVariableTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	done:
	debugmemfree(vm, debugarea);
	debugmemfree(vm, theca);
	return retval;
}

IDATA
DebugAreaUnitTests::debugmemtest7(J9JavaVM* vm)
{
	IDATA retval = TEST_PASS;
	const char * testName = "debugmemtest7";
	J9SharedCacheHeader * theca = (J9SharedCacheHeader *) debugmemalloc(vm);
	UDATA initialFreeSize;
	U_64 runtimeFlags = J9SHR_RUNTIMEFLAG_DBG_EXTRA_CHECKS;
	PORT_ACCESS_FROM_JAVAVM(vm);
	U_32 allocsize = 4096;
	J9RomClassRequirements sizes;
	J9SharedRomClassPieces pieces;
	MprotectHelper prot;
	memset((void *)&sizes, 0, sizeof(J9RomClassRequirements));
	memset((void *)&pieces, 0, sizeof(J9SharedRomClassPieces));

	j9tty_printf(PORTLIB, "%s: Test allocate of mprotected LineNumberTable memory.\n", testName);

	ClassDebugDataProvider * debugarea = debugProviderMemalloc(vm);

	if (debugarea->Init(vm->mainThread, theca, NULL, 0, &runtimeFlags, false) == false) {
		ERRPRINTF("debugarea->Init(...) == false\n");
		retval = TEST_ERROR;
		goto done;
	}

	initialFreeSize = debugarea->getFreeDebugSpaceBytes();

	sizes.lineNumberTableSize = allocsize;
	if (debugarea->allocateClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, (const J9RomClassRequirements *)&sizes, &pieces, &prot) != 0) {
		ERRPRINTF("allocateClassDebugData() failed\n");
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getNumberOfPartialPageReadWriteProtects() != 1) {
		ERRPRINTF("prot.getNumberOfPartialPageReadWriteProtects() != 1\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfPartialPageReadWriteProtects(), 1);
		retval = TEST_ERROR;
		goto done;
	}

	debugarea->commitClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, &prot);

	if (prot.getNumberOfPartialPageReadOnlyProtects() != 1) {
		ERRPRINTF("prot.getNumberOfPartialPageReadOnlyProtects() != 1\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfPartialPageReadOnlyProtects(), 1);
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getNumberOfProtects() != 1) {
		ERRPRINTF("prot.getNumberOfProtects() != 1\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfProtects(), 1);
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getAmountProtected() != allocsize) {
		ERRPRINTF("prot.getAmountProtected() != allocsize\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getAmountProtected(), allocsize);
		retval = TEST_ERROR;
		goto done;
	}

	if (initialFreeSize < debugarea->getFreeDebugSpaceBytes()) {
		ERRPRINTF("initialFreeSize < debugarea->getFreeDebugSpaceBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u < %u\n", initialFreeSize, debugarea->getFreeDebugSpaceBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if (allocsize != debugarea->getLineNumberTableBytes()) {
		ERRPRINTF("allocsize != getLineNumberTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", allocsize, debugarea->getLineNumberTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if (0 != debugarea->getLocalVariableTableBytes()) {
		ERRPRINTF("0 != getLocalVariableTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: 0 != %u\n", debugarea->getLocalVariableTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	done:
	debugmemfree(vm, debugarea);
	debugmemfree(vm, theca);
	return retval;
}

IDATA
DebugAreaUnitTests::debugmemtest8(J9JavaVM* vm)
{
	IDATA retval = TEST_PASS;
	const char * testName = "debugmemtest8";
	J9SharedCacheHeader * theca = (J9SharedCacheHeader *) debugmemalloc(vm);
	UDATA initialFreeSize;
	U_64 runtimeFlags = J9SHR_RUNTIMEFLAG_DBG_EXTRA_CHECKS;
	PORT_ACCESS_FROM_JAVAVM(vm);
	U_32 allocsize = (4096*2);
	J9RomClassRequirements sizes;
	J9SharedRomClassPieces pieces;
	MprotectHelper prot;
	memset((void *)&sizes, 0, sizeof(J9RomClassRequirements));
	memset((void *)&pieces, 0, sizeof(J9SharedRomClassPieces));

	j9tty_printf(PORTLIB, "%s: Test allocate of mprotected LocalVariableTable memory.\n", testName);

	ClassDebugDataProvider * debugarea = debugProviderMemalloc(vm);

	if (debugarea->Init(vm->mainThread, theca, NULL, 0, &runtimeFlags, false) == false) {
		ERRPRINTF("debugarea->Init(...) == false\n");
		retval = TEST_ERROR;
		goto done;
	}

	initialFreeSize = debugarea->getFreeDebugSpaceBytes();

	sizes.localVariableTableSize = allocsize;
	if (debugarea->allocateClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, (const J9RomClassRequirements *)&sizes, &pieces, &prot) != 0) {
		ERRPRINTF("allocateClassDebugData() failed\n");
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getNumberOfPartialPageReadWriteProtects() != 1) {
		ERRPRINTF("prot.getNumberOfPartialPageReadWriteProtects() != 1\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfPartialPageReadWriteProtects(), 1);
		retval = TEST_ERROR;
		goto done;
	}

	debugarea->commitClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, &prot);

	if (prot.getNumberOfPartialPageReadOnlyProtects() != 1) {
		ERRPRINTF("prot.getNumberOfPartialPageReadOnlyProtects() != 1\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfPartialPageReadOnlyProtects(), 1);
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getNumberOfProtects() != 1) {
		ERRPRINTF("prot.getNumberOfProtects() != 1\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfProtects(), 1);
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getAmountProtected() != allocsize) {
		ERRPRINTF("prot.getAmountProtected() != allocsize\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getAmountProtected(), allocsize);
		retval = TEST_ERROR;
		goto done;
	}

	if (initialFreeSize < debugarea->getFreeDebugSpaceBytes()) {
		ERRPRINTF("initialFreeSize < debugarea->getFreeDebugSpaceBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u < %u\n", initialFreeSize, debugarea->getFreeDebugSpaceBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if (allocsize != debugarea->getLocalVariableTableBytes()) {
		ERRPRINTF("allocsize != getLocalVariableTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", allocsize, debugarea->getLocalVariableTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if (0 != debugarea->getLineNumberTableBytes()) {
		ERRPRINTF("0 != getLineNumberTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: 0 != %u\n", debugarea->getLineNumberTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	done:
	debugmemfree(vm, debugarea);
	debugmemfree(vm, theca);
	return retval;
}

IDATA
DebugAreaUnitTests::debugmemtest9(J9JavaVM* vm)
{
	IDATA retval = TEST_PASS;
	const char * testName = "debugmemtest9";
	J9SharedCacheHeader * theca = (J9SharedCacheHeader *) debugmemalloc(vm);
	UDATA initialFreeSize;
	U_64 runtimeFlags = J9SHR_RUNTIMEFLAG_DBG_EXTRA_CHECKS;
	PORT_ACCESS_FROM_JAVAVM(vm);
	U_32 allocsize = 4096;
	J9RomClassRequirements sizes;
	J9SharedRomClassPieces pieces;
	MprotectHelper prot;
	memset((void *)&sizes, 0, sizeof(J9RomClassRequirements));
	memset((void *)&pieces, 0, sizeof(J9SharedRomClassPieces));

	j9tty_printf(PORTLIB, "%s: Test allocate of mprotected LineNumberTable and mprotected LocalVariableTable memory.\n", testName);

	ClassDebugDataProvider * debugarea = debugProviderMemalloc(vm);

	if (debugarea->Init(vm->mainThread, theca, NULL, 0, &runtimeFlags, false) == false) {
		ERRPRINTF("debugarea->Init(...) == false\n");
		retval = TEST_ERROR;
		goto done;
	}

	initialFreeSize = debugarea->getFreeDebugSpaceBytes();

	sizes.lineNumberTableSize = allocsize;
	sizes.localVariableTableSize = allocsize;
	if (debugarea->allocateClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, (const J9RomClassRequirements *)&sizes, &pieces, &prot) != 0) {
		ERRPRINTF("allocateClassDebugData() failed\n");
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getNumberOfPartialPageReadWriteProtects() != 2) {
		ERRPRINTF("prot.getNumberOfPartialPageReadWriteProtects() != 2\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfPartialPageReadWriteProtects(), 2);
		retval = TEST_ERROR;
		goto done;
	}

	debugarea->commitClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, &prot);

	if (prot.getNumberOfPartialPageReadOnlyProtects() != 2) {
		ERRPRINTF("prot.getNumberOfPartialPageReadOnlyProtects() != 2\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfPartialPageReadOnlyProtects(), 2);
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getNumberOfProtects() != 2) {
		ERRPRINTF("prot.getNumberOfProtects() != 2\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfProtects(), 2);
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getAmountProtected() != (allocsize*2)) {
		ERRPRINTF("prot.getAmountProtected() != (allocsize*2)\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getAmountProtected(), (allocsize*2));
		retval = TEST_ERROR;
		goto done;
	}

	if ((initialFreeSize - (allocsize * 2)) != debugarea->getFreeDebugSpaceBytes()) {
		ERRPRINTF("(initialFreeSize-(allocsize*2)) != debugarea->getFreeDebugSpaceBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", (initialFreeSize - (allocsize * 2)), debugarea->getFreeDebugSpaceBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if (allocsize != debugarea->getLocalVariableTableBytes()) {
		ERRPRINTF("0 != getLocalVariableTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", 0, debugarea->getLocalVariableTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if (allocsize != debugarea->getLineNumberTableBytes()) {
		ERRPRINTF("0 != getLineNumberTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", allocsize, debugarea->getLocalVariableTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	done:
	debugmemfree(vm, debugarea);
	debugmemfree(vm, theca);
	return retval;
}

IDATA
DebugAreaUnitTests::debugmemtest10(J9JavaVM* vm)
{
	IDATA retval = TEST_PASS;
	const char * testName = "debugmemtest10";
	J9SharedCacheHeader * theca = (J9SharedCacheHeader *) debugmemalloc(vm);
	UDATA initialFreeSize;
	U_64 runtimeFlags = J9SHR_RUNTIMEFLAG_DBG_EXTRA_CHECKS;
	PORT_ACCESS_FROM_JAVAVM(vm);
	U_32 allocsize = 4096;
	J9RomClassRequirements sizes;
	J9SharedRomClassPieces pieces;
	MprotectHelper prot;
	memset((void *)&sizes, 0, sizeof(J9RomClassRequirements));
	memset((void *)&pieces, 0, sizeof(J9SharedRomClassPieces));

	j9tty_printf(PORTLIB, "%s: Test mprotecting debug area updates.\n", testName);

	ClassDebugDataProvider * debugarea = debugProviderMemalloc(vm);

	if (debugarea->Init(vm->mainThread, theca, NULL, 0, &runtimeFlags, false) == false) {
		ERRPRINTF("debugarea->Init(...) == false\n");
		retval = TEST_ERROR;
		goto done;
	}

	initialFreeSize = debugarea->getFreeDebugSpaceBytes();

	/*Simulate an update by another JVM*/
	theca->lineNumberTableNextSRP+=allocsize;
	theca->localVariableTableNextSRP-=allocsize;

	debugarea->processUpdates(vm->mainThread,&prot);

	if (prot.getNumberOfProtects() != 2) {
		ERRPRINTF("prot.getNumberOfProtects() != 2\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfProtects(), 2);
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getAmountProtected() != (allocsize*2)) {
		ERRPRINTF("prot.getAmountProtected() != (allocsize*2)\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getAmountProtected(), (allocsize*2));
		retval = TEST_ERROR;
		goto done;
	}

	if ((initialFreeSize - (allocsize * 2)) != debugarea->getFreeDebugSpaceBytes()) {
		ERRPRINTF("(initialFreeSize-(allocsize*2)) != debugarea->getFreeDebugSpaceBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", (initialFreeSize - (allocsize * 2)), debugarea->getFreeDebugSpaceBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if (allocsize != debugarea->getLocalVariableTableBytes()) {
		ERRPRINTF("allocsize != getLocalVariableTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", allocsize, debugarea->getLocalVariableTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if (allocsize != debugarea->getLineNumberTableBytes()) {
		ERRPRINTF("0 != getLineNumberTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", allocsize, debugarea->getLocalVariableTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	done:
	debugmemfree(vm, debugarea);
	debugmemfree(vm, theca);
	return retval;
}

IDATA
DebugAreaUnitTests::debugmemtest11(J9JavaVM* vm)
{
	IDATA retval = TEST_PASS;
	const char * testName = "debugmemtest11";
	J9SharedCacheHeader * theca = (J9SharedCacheHeader *) debugmemalloc(vm);
	UDATA initialFreeSize;
	U_64 runtimeFlags = J9SHR_RUNTIMEFLAG_DBG_EXTRA_CHECKS;
	PORT_ACCESS_FROM_JAVAVM(vm);
	U_32 allocsize1 = 0;
	U_32 allocsize2 = (4096/2);
	J9RomClassRequirements sizes;
	J9SharedRomClassPieces pieces;
	MprotectHelper prot;
	memset((void *)&sizes, 0, sizeof(J9RomClassRequirements));
	memset((void *)&pieces, 0, sizeof(J9SharedRomClassPieces));

	j9tty_printf(PORTLIB, "%s: Test mprotecting the entire debug area.\n", testName);

	ClassDebugDataProvider * debugarea = debugProviderMemalloc(vm);

	if (debugarea->Init(vm->mainThread, theca, NULL, 0, &runtimeFlags, false) == false) {
		ERRPRINTF("debugarea->Init(...) == false\n");
		retval = TEST_ERROR;
		goto done;
	}

	initialFreeSize = debugarea->getFreeDebugSpaceBytes();
	allocsize1 = (((U_32)initialFreeSize)/2) - allocsize2;

	/*Put some data in the debug area*/
	sizes.lineNumberTableSize = allocsize1;
	sizes.localVariableTableSize = allocsize1;
	if (debugarea->allocateClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, (const J9RomClassRequirements *)&sizes, &pieces, &prot) != 0) {
		ERRPRINTF("allocateClassDebugData() failed\n");
		j9tty_printf(PORTLIB, "\tError: allocateClassDebugData() failed (free:%u sizes.lineNumberTableSize:%u sizes.localVariableTableSize:%u)\n", 
			debugarea->getFreeDebugSpaceBytes(), sizes.lineNumberTableSize, sizes.localVariableTableSize);
		retval = TEST_ERROR;
		goto done;
	}
	debugarea->commitClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, &prot);
	
	/*Fill the remaining debug area*/
	sizes.lineNumberTableSize = allocsize2;
	sizes.localVariableTableSize = allocsize2;
	if (debugarea->allocateClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, (const J9RomClassRequirements *)&sizes, &pieces, &prot) != 0) {
		ERRPRINTF("allocateClassDebugData() failed\n");
		j9tty_printf(PORTLIB, "\tError: allocateClassDebugData() failed (free:%u sizes.lineNumberTableSize:%u sizes.localVariableTableSize:%u)\n", 
			debugarea->getFreeDebugSpaceBytes(), sizes.lineNumberTableSize, sizes.localVariableTableSize);
		retval = TEST_ERROR;
		goto done;
	}

	debugarea->commitClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, &prot);
	
	if (prot.getNumberOfPartialPageReadWriteProtects() != 4) {
		ERRPRINTF("prot.getNumberOfPartialPageReadWriteProtects() != 4");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfPartialPageReadWriteProtects(), 4);
		retval = TEST_ERROR;
		goto done;
	}
	
	if (prot.getNumberOfPartialPageReadOnlyProtects() != 4) {
		ERRPRINTF("prot.getNumberOfPartialPageReadOnlyProtects() != 4");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfPartialPageReadOnlyProtects(), 4);
		retval = TEST_ERROR;
		goto done;
	}

	/*The last mprotect should execute only once*/
	if (prot.getNumberOfProtects() != 3) {
		ERRPRINTF("prot.getNumberOfProtects() != 3\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfProtects(), 3);
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getAmountProtected() != initialFreeSize) {
		ERRPRINTF("prot.getAmountProtected() != initialFreeSize\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getAmountProtected(), initialFreeSize);
		retval = TEST_ERROR;
		goto done;
	}

	if (0 != debugarea->getFreeDebugSpaceBytes()) {
		ERRPRINTF("0 != debugarea->getFreeDebugSpaceBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", (0), debugarea->getFreeDebugSpaceBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if ((initialFreeSize/2) != debugarea->getLocalVariableTableBytes()) {
		ERRPRINTF("(initialFreeSize/2) != getLocalVariableTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", (initialFreeSize/2), debugarea->getLocalVariableTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	if ((initialFreeSize/2) != debugarea->getLineNumberTableBytes()) {
		ERRPRINTF("(initialFreeSize/2) != getLineNumberTableBytes()\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", (initialFreeSize/2), debugarea->getLocalVariableTableBytes());
		retval = TEST_ERROR;
		goto done;
	}

	done:
	debugmemfree(vm, debugarea);
	debugmemfree(vm, theca);
	return retval;
}

IDATA
DebugAreaUnitTests::debugmemtest12(J9JavaVM* vm)
{
	IDATA retval = TEST_PASS;
	const char * testName = "debugmemtest12";
	U_64 runtimeFlags = J9SHR_RUNTIMEFLAG_DBG_EXTRA_CHECKS;
	J9SharedCacheHeader * theca = (J9SharedCacheHeader *) debugmemalloc(vm);
	ClassDebugDataProvider * debugarea = debugProviderMemalloc(vm);
	UDATA oldLVTSRP = 0;
	UDATA newLVTSRP = 0;
	UDATA oldLNTSRP = 0;
	UDATA newLNTSRP = 0;
	PORT_ACCESS_FROM_JAVAVM(vm);

	/* Note:
	 * This test will not run if the theca->localVariableTableNextSRP is further than theca->localVariableTableNextSRP
	 * bytes from the end of memory.
	 */
	j9tty_printf(PORTLIB, "%s: Test ::isOk() error checking math when cache is mapped to end of memory.\n", testName);

	if (debugarea->Init(vm->mainThread, theca, NULL, 0, &runtimeFlags, false) == false) {
		ERRPRINTF("debugarea->Init(vm->mainThread, theca, NULL, 0) == false\n");
		retval = TEST_ERROR;
		goto done;
	}

	if (debugarea->isOk(vm->mainThread, true, true, true) == false) {
		ERRPRINTF2("debugarea->isOk(...) failed when we expect it to pass (reason:%d value:%p)\n",debugarea->getFailureReason(),debugarea->getFailureValue());
		retval = TEST_ERROR;
		goto done;
	}

	oldLVTSRP = theca->localVariableTableNextSRP;
	newLVTSRP = (0 - (UDATA)&(theca->localVariableTableNextSRP));
	oldLNTSRP = theca->lineNumberTableNextSRP;
	newLNTSRP = (0 - (UDATA)&(theca->localVariableTableNextSRP)) - (oldLVTSRP - oldLNTSRP);

	if (newLVTSRP > IDATA_MAX) {
		/*SRP's can't be played with b/c they will be out of range*/
		INFOPRINTF("This test can't be run because SRP will be out of range skipping this test.");
		goto done;
	}

	theca->localVariableTableNextSRP= newLVTSRP;
	theca->lineNumberTableNextSRP= newLNTSRP;

	if (debugarea->getLVTNextAddress() != NULL) {
		ERRPRINTF1("debugarea->getLVTNextAddress() did not return NULL as expected (return value:%p)\n",debugarea->getLVTNextAddress());
		retval = TEST_ERROR;
		goto done;
	}

	if (debugarea->isOk(vm->mainThread, true, true, true) == false) {
		ERRPRINTF2("debugarea->isOk(...) failed unexpectedly (reason:%d value:%p)\n",debugarea->getFailureReason(),debugarea->getFailureValue());
		retval = TEST_ERROR;
		goto done;
	}
	
	done:
	debugmemfree(vm, debugarea);
	debugmemfree(vm, theca);
	return retval;
}

IDATA
DebugAreaUnitTests::debugmemtest13(J9JavaVM* vm)
{
	IDATA retval = TEST_PASS;
	const char * testName = "debugmemtest13";
	J9SharedCacheHeader * theca = (J9SharedCacheHeader *) debugmemalloc(vm);
	ClassDebugDataProvider * debugarea = debugProviderMemalloc(vm);
	J9SharedCacheHeader origHeader;
	U_64 runtimeFlags = J9SHR_RUNTIMEFLAG_DBG_EXTRA_CHECKS;
	PORT_ACCESS_FROM_JAVAVM(vm);

	/*
	 * This test attempts to obtain code coverage on the isOk method on the debug area.
	 * It is intended to compliment similar testing in CorruptCacheTest.cpp which is 
	 * not able to manipulate debugarea class easily.
	 */
	j9tty_printf(PORTLIB, "%s: Test ::isOk() corruption code testing for code coverage.\n", testName);

	/* Disable Trc_SHR_Assert_False() in ClassDebugDataProvider::isOk() */
	j9shr_UtActive[1013] = 0;

	if (debugarea->Init(vm->mainThread, theca, NULL, 0, &runtimeFlags, false) == false) {
		ERRPRINTF("debugarea->Init(...) == false\n");
		retval = TEST_ERROR;
		goto done;
	}
	origHeader = *theca;

	INFOPRINTF("1.) No Corruption");
	if (debugarea->isOk(vm->mainThread, true, true, true) == false) {
		ERRPRINTF2("debugarea->isOk(...) failed when we expect it to pass (reason:%d value:%p)\n",debugarea->getFailureReason(),debugarea->getFailureValue());
		retval = TEST_ERROR;
		goto done;
	}
	
	INFOPRINTF("2.) CACHE_DEBUGAREA_BAD_SIZE");
	theca->totalBytes = 0;
	if (debugarea->isOk(vm->mainThread, true, true, true) == true) {
		ERRPRINTF("debugarea->isOk(...) passed unexpectedly when CACHE_DEBUGAREA_BAD_SIZE was expected\n");
		retval = TEST_ERROR;
		goto done;
	}
	if (debugarea->getFailureReason() != CACHE_DEBUGAREA_BAD_SIZE) {
		ERRPRINTF2("debugarea->getFailureReason(...) returned %d when CACHE_DEBUGAREA_BAD_SIZE, or %d, was expected\n",debugarea->getFailureReason(), CACHE_DEBUGAREA_BAD_SIZE);
		retval = TEST_ERROR;
		goto done;
	}
	*theca=origHeader;
	debugarea->failureReason = NO_CORRUPTION;
	debugarea->failureValue = 0;
	
	INFOPRINTF("3.) CACHE_DEBUGAREA_BAD_FREE_SPACE");
	theca->lineNumberTableNextSRP = theca->localVariableTableNextSRP;
	theca->localVariableTableNextSRP = origHeader.lineNumberTableNextSRP;
	if (debugarea->isOk(vm->mainThread, true, true, true) == true) {
		ERRPRINTF("debugarea->isOk(...) passed unexpectedly when CACHE_DEBUGAREA_BAD_FREE_SPACE was expected\n");
		retval = TEST_ERROR;
		goto done;
	}
	if (debugarea->getFailureReason() != CACHE_DEBUGAREA_BAD_FREE_SPACE) {
		ERRPRINTF2("debugarea->getFailureReason(...) returned %d when CACHE_DEBUGAREA_BAD_FREE_SPACE, or %d, was expected\n",debugarea->getFailureReason(), CACHE_DEBUGAREA_BAD_FREE_SPACE);
		retval = TEST_ERROR;
		goto done;
	}
	*theca=origHeader;
	debugarea->failureReason = NO_CORRUPTION;
	debugarea->failureValue = 0;
	
	 
 	INFOPRINTF("4.) CACHE_DEBUGAREA_BAD_FREE_SPACE_SIZE");
	theca->lineNumberTableNextSRP = 0;
	if (debugarea->isOk(vm->mainThread, true, true, true) == true) {
		ERRPRINTF("debugarea->isOk(...) passed unexpectedly when CACHE_DEBUGAREA_BAD_FREE_SPACE_SIZE was expected\n");
		retval = TEST_ERROR;
		goto done;
	}
	if (debugarea->getFailureReason() != CACHE_DEBUGAREA_BAD_FREE_SPACE_SIZE) {
		ERRPRINTF2("debugarea->getFailureReason(...) returned %d when CACHE_DEBUGAREA_BAD_FREE_SPACE_SIZE, or %d, was expected\n",debugarea->getFailureReason(), CACHE_DEBUGAREA_BAD_FREE_SPACE_SIZE);
		retval = TEST_ERROR;
		goto done;
	}
	*theca=origHeader;
	debugarea->failureReason = NO_CORRUPTION;
	debugarea->failureValue = 0;
	 
	INFOPRINTF("5.) CACHE_DEBUGAREA_BAD_LNT_HEADER_INFO");
	theca->lineNumberTableNextSRP = sizeof(UDATA);
	theca->localVariableTableNextSRP = (theca->debugRegionSize - sizeof(UDATA));
	if (debugarea->isOk(vm->mainThread, true, true, true) == true) {
		ERRPRINTF("debugarea->isOk(...) passed unexpectedly when CACHE_DEBUGAREA_BAD_LNT_HEADER_INFO was expected\n");
		retval = TEST_ERROR;
		goto done;
	}
	if (debugarea->getFailureReason() != CACHE_DEBUGAREA_BAD_LNT_HEADER_INFO) {
		ERRPRINTF2("debugarea->getFailureReason(...) returned %d when CACHE_DEBUGAREA_BAD_LNT_HEADER_INFO, or %d, was expected\n",debugarea->getFailureReason(), CACHE_DEBUGAREA_BAD_LNT_HEADER_INFO);
		retval = TEST_ERROR;
		goto done;
	}
	*theca=origHeader;
	debugarea->failureReason = NO_CORRUPTION;
	debugarea->failureValue = 0;
	 
	 
	INFOPRINTF("6.) CACHE_DEBUGAREA_BAD_LVT_HEADER_INFO");
	theca->lineNumberTableNextSRP += theca->totalBytes;
	theca->localVariableTableNextSRP += theca->totalBytes;
	if (debugarea->isOk(vm->mainThread, true, true, true) == true) {
		ERRPRINTF("debugarea->isOk(...) passed unexpectedly when CACHE_DEBUGAREA_BAD_LVT_HEADER_INFO was expected\n");
		retval = TEST_ERROR;
		goto done;
	}
	if (debugarea->getFailureReason() != CACHE_DEBUGAREA_BAD_LVT_HEADER_INFO) {
		ERRPRINTF2("debugarea->getFailureReason(...) returned %d when CACHE_DEBUGAREA_BAD_LVT_HEADER_INFO, or %d, was expected\n",debugarea->getFailureReason(), CACHE_DEBUGAREA_BAD_LVT_HEADER_INFO);
		retval = TEST_ERROR;
		goto done;
	}
	*theca=origHeader;
	debugarea->failureReason = NO_CORRUPTION;
	debugarea->failureValue = 0;
	 
	/*
	 * Test 6: CACHE_DEBUGAREA_BAD_SIZES_FOR_COMMIT
	 */
	INFOPRINTF("Subtest 6: CACHE_DEBUGAREA_BAD_SIZES_FOR_COMMIT");	 
	debugarea->_storedLineNumberTableBytes = (U_32)((UDATA)(debugarea->_lvtLastUpdate) - (UDATA)(debugarea->_lntLastUpdate))+1;
	if (debugarea->isOk(vm->mainThread, true, true, true) == true) {
		ERRPRINTF1("debugarea->isOk(...) pass unexpectedly (_storedLineNumberTableBytes:%d)\n",debugarea->_storedLineNumberTableBytes);
		retval = TEST_ERROR;
		goto done;
	}
	
	if (debugarea->getFailureReason() != CACHE_DEBUGAREA_BAD_SIZES_FOR_COMMIT) {
		ERRPRINTF2("debugarea->getFailureReason() returned %d when CACHE_DEBUGAREA_BAD_SIZES_FOR_COMMIT, or %d, was expected\n",debugarea->getFailureReason(), CACHE_DEBUGAREA_BAD_SIZES_FOR_COMMIT);
		retval = TEST_ERROR;
		goto done;
	}
	debugarea->_storedLineNumberTableBytes = 0;
	
	debugarea->_storedLocalVariableTableBytes = (U_32)((UDATA)(debugarea->_lvtLastUpdate) - (UDATA)(debugarea->_lntLastUpdate))+1;
	if (debugarea->isOk(vm->mainThread, true, true, true) == true) {
		ERRPRINTF1("debugarea->isOk(...) pass unexpectedly (_storedLineNumberTableBytes:%d)\n",debugarea->_storedLineNumberTableBytes);
		retval = TEST_ERROR;
		goto done;
	}
	
	if (debugarea->getFailureReason() != CACHE_DEBUGAREA_BAD_SIZES_FOR_COMMIT) {
		ERRPRINTF2("debugarea->getFailureReason() returned %d when CACHE_DEBUGAREA_BAD_SIZES_FOR_COMMIT, or %d, was expected\n",debugarea->getFailureReason(), CACHE_DEBUGAREA_BAD_SIZES_FOR_COMMIT);
		retval = TEST_ERROR;
		goto done;
	}
	debugarea->_storedLocalVariableTableBytes = 0;
	*theca=origHeader;
	debugarea->failureReason = NO_CORRUPTION;
	debugarea->failureValue = 0;
	 
done:
	/* Enable Trc_SHR_Assert_False() back */
	j9shr_UtActive[1013] = 1;
	j9tty_printf(PORTLIB,"\n");
	debugmemfree(vm, debugarea);
	debugmemfree(vm, theca);
	return retval;
}

IDATA
DebugAreaUnitTests::debugmemtest14(J9JavaVM* vm)
{
	IDATA retval = TEST_PASS;
	const char * testName = "debugmemtest14";
	U_32 smallCacheSize = 50 * 1024 * 1024;	/* 50 MB */
	U_32 largeCacheSize = 15 * smallCacheSize;	/* 15 * 50 = 750 MB */
	U_32 size1, size2;
	U_64 percent1, percent2;
	U_32 expectedPercent = ClassDebugDataProvider::getRecommendedPercentage();
	PORT_ACCESS_FROM_JAVAVM(vm);

	j9tty_printf(PORTLIB, "%s: Test ::recommendedSize() for overflow when using large cache.\n", testName);

	size1 = ClassDebugDataProvider::recommendedSize(smallCacheSize, 4);
	percent1 = ((U_64)size1 * 100) / smallCacheSize;
	if (percent1 < expectedPercent - 1) {	/* give a leeway of 1% to account for truncation due to integer arithmetic */
		ERRPRINTF4("Expected percent of ClassDebugDataArea size is %d%%, but got %d%% (cache size: %u bytes, ClassDebugDataArea: %u bytes)\n",
				expectedPercent, (U_32)percent1, smallCacheSize, size1);
		retval = TEST_ERROR;
		goto done;
	} else {
		INFOPRINTF2("ClassDebugDataArea size for small cache: %u (%d%%)\n", size1, (U_32)percent1);
	}

	size2 = ClassDebugDataProvider::recommendedSize(largeCacheSize, 4);
	percent2 = ((U_64)size2 * 100) / largeCacheSize;
	if (percent2 < expectedPercent - 1) {	/* give a leeway of 1% to account for truncation due to integer arithmetic */
		ERRPRINTF4("Expected percent of ClassDebugDataArea size is %d%%, but got %d%% (cache size: %u bytes, ClassDebugDataArea: %u bytes)\n",
				expectedPercent, (U_32)percent2, largeCacheSize, size2);
		retval = TEST_ERROR;
		goto done;
	} else {
		INFOPRINTF2("ClassDebugDataArea size for large cache: %u (%d%%)\n", size2, (U_32)percent2);
	}

done:
	return retval;
}

IDATA
DebugAreaUnitTests::debugmemtest15(J9JavaVM* vm)
{
	IDATA retval = TEST_PASS;
	const char * testName = "debugmemtest15";
	J9SharedCacheHeader * theca = NULL;
	ClassDebugDataProvider * debugArea = NULL;
	U_32 pageSize = 4096;
	UDATA memorySize = (1 * 1024 * 1024);	// 1 MB
	UDATA debugRegionSize = memorySize / 2;	// 512 KB
	UDATA initialFreeSize;
	U_64 runtimeFlags = J9SHR_RUNTIMEFLAG_DBG_EXTRA_CHECKS | J9SHR_RUNTIMEFLAG_MPROTECT_PARTIAL_PAGES_ON_STARTUP;
	PORT_ACCESS_FROM_JAVAVM(vm);
	U_32 lntSize = 0;
	U_32 lvtSize = 0;
	J9RomClassRequirements sizes;
	J9SharedRomClassPieces pieces;
	MprotectHelper prot;
	void *memAlloc = NULL;

	memset((void *)&sizes, 0, sizeof(J9RomClassRequirements));
	memset((void *)&pieces, 0, sizeof(J9SharedRomClassPieces));

	memAlloc = j9mem_allocate_memory(memorySize + pageSize, J9MEM_CATEGORY_CLASSES);
	if (NULL == memAlloc) {
		ERRPRINTF("Failed to allocate memory for the cache\n");
		goto done;
	}
	theca = (J9SharedCacheHeader *)ROUND_UP_TO(pageSize, (UDATA)memAlloc);
	theca->totalBytes = (U_32)memorySize;
	theca->osPageSize = pageSize;
	ClassDebugDataProvider::HeaderInit(theca, (U_32)debugRegionSize);

	j9tty_printf(PORTLIB, "%s: Test partial page protection in the debug area.\n", testName);

	debugArea = debugProviderMemalloc(vm);

	if (debugArea->Init(vm->mainThread, theca, NULL, 0, &runtimeFlags, false) == false) {
		ERRPRINTF("debugArea->Init(...) == false\n");
		retval = TEST_ERROR;
		goto done;
	}

	/*
	 * Before adding data,
	 *
	 *   <------------------------ t -------------------------->
	 *    _____________________________________________________
	 *   |        |        |        |        |        |        |
	 *   |        |        |        |        |        |        |
	 *   |________|________|________|________|________|________|
	 *   A                                                     A
	 *   |                                                     |
	 *   |                                                     |
	 *  LNT                                                   LVT
	 *
	 *
    */

	initialFreeSize = debugArea->getFreeDebugSpaceBytes();

	/**
	 * After adding 1st set of data LVT and LNT are 2 pages apart:
	 *
	 *    size of LNT = pagesize + 100
	 *    size of LVT = t - (size of LNT) - 2 * pagesize
	 *
	 *   <------------------------ t -------------------------->
	 *    _____________________________________________________
	 *   |        |        |        |        |        |        |
	 *   |        |        |        |        |        |        |
	 *   |________|________|________|________|________|________|
	 *                       A                 A
	 *                       |<--x-->          |<--x-->
	 *   <------lntsize----->|                 |<----lvtsize--->
	 *                      LNT               LVT
	 *
	 *   number of partial pages to be unprotected before allocation: 2 (both LNT and LVT page)
	 *   number of partial pages to be protected after allocation: 2 (both LNT and LVT page)
	 */

	lntSize = pageSize + 100;
	lvtSize = (U_32)initialFreeSize - lntSize - 2 * pageSize;

	sizes.lineNumberTableSize = lntSize;
	sizes.localVariableTableSize = lvtSize;
	if (debugArea->allocateClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, (const J9RomClassRequirements *)&sizes, &pieces, &prot) != 0) {
		ERRPRINTF("allocateClassDebugData() failed\n");
		j9tty_printf(PORTLIB, "\tError: allocateClassDebugData() failed (free:%u sizes.lineNumberTableSize:%u sizes.localVariableTableSize:%u)\n",
			debugArea->getFreeDebugSpaceBytes(), sizes.lineNumberTableSize, sizes.localVariableTableSize);
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getNumberOfPartialPageReadWriteProtects() != 2) {
		ERRPRINTF("prot.getNumberOfPartialPageReadWriteProtects() != 2\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfPartialPageReadWriteProtects(), 2);
		retval = TEST_ERROR;
		goto done;
	}

	debugArea->commitClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, &prot);

	if (prot.getNumberOfPartialPageReadOnlyProtects() != 2) {
		ERRPRINTF("prot.getNumberOfPartialPageReadWriteProtects() != 2\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfPartialPageReadWriteProtects(), 2);
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getNumberOfProtects() != 2) {
		ERRPRINTF("prot.getNumberOfProtects() != 2\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfProtects(), 2);
		retval = TEST_ERROR;
		goto done;
	}

	/**
	 * After adding 2nd set of data LVT and LNT are 1 page apart:
	 *
	 * 	  size of LNT = pagesize
	 * 	  size of LVT = 0
	 *
	 *   <------------------------ t -------------------------->
	 *    _____________________________________________________
	 *   |        |        |        |        |        |        |
	 *   |        |        |        |        |        |        |
	 *   |________|________|________|________|________|________|
	 *                                A        A
	 *                                |        |
	 *                                |        |
	 *                               LNT      LVT
	 *
	 *   number of partial pages to be unprotected before allocation: 1 (only LNT page)
	 *   number of partial pages to be protected after allocation: 1 (only LNT page)
	 *
	 */

	lntSize = pageSize;
	lvtSize = 0;

	prot.reset();
	sizes.lineNumberTableSize = lntSize;
	sizes.localVariableTableSize = lvtSize;
	if (debugArea->allocateClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, (const J9RomClassRequirements *)&sizes, &pieces, &prot) != 0) {
		ERRPRINTF("allocateClassDebugData() failed\n");
		j9tty_printf(PORTLIB, "\tError: allocateClassDebugData() failed (free:%u sizes.lineNumberTableSize:%u sizes.localVariableTableSize:%u)\n",
			debugArea->getFreeDebugSpaceBytes(), sizes.lineNumberTableSize, sizes.localVariableTableSize);
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getNumberOfPartialPageReadWriteProtects() != 1) {
		ERRPRINTF("prot.getNumberOfPartialPageReadWriteProtects() != 1\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfPartialPageReadWriteProtects(), 1);
		retval = TEST_ERROR;
		goto done;
	}

	debugArea->commitClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, &prot);

	if (prot.getNumberOfPartialPageReadOnlyProtects() != 1) {
		ERRPRINTF("prot.getNumberOfPartialPageReadWriteProtects() != 1\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfPartialPageReadWriteProtects(), 1);
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getNumberOfProtects() != 1) {
		ERRPRINTF("prot.getNumberOfProtects() != 1\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfProtects(), 1);
		retval = TEST_ERROR;
		goto done;
	}

	/**
	 * After adding 3rd set of data LVT and LNT are on same page:
	 *
	 *   size of LNT = 0
	 *   size of LVT = pagesize/2
	 *
	 *   <------------------------ t -------------------------->
	 *    _____________________________________________________
	 *   |        |        |        |        |        |        |
	 *   |        |        |        |        |        |        |
	 *   |________|________|________|________|________|________|
	 *                                A    A
	 *                                |    |
	 *                                |    |
	 *                               LNT   LVT
	 *
	 *   number of partial pages to be unprotected before allocation: 2 (both LNT and LVT pages)
	 *   number of partial pages to be protected after allocation: 1 (LNT and LVT are on same page)
	 */
	lntSize = 0;
	lvtSize = pageSize / 2;

	prot.reset();
	sizes.lineNumberTableSize = lntSize;
	sizes.localVariableTableSize = lvtSize;
	if (debugArea->allocateClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, (const J9RomClassRequirements *)&sizes, &pieces, &prot) != 0) {
		ERRPRINTF("allocateClassDebugData() failed\n");
		j9tty_printf(PORTLIB, "\tError: allocateClassDebugData() failed (free:%u sizes.lineNumberTableSize:%u sizes.localVariableTableSize:%u)\n",
			debugArea->getFreeDebugSpaceBytes(), sizes.lineNumberTableSize, sizes.localVariableTableSize);
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getNumberOfPartialPageReadWriteProtects() != 2) {
		ERRPRINTF("prot.getNumberOfPartialPageReadWriteProtects() != 2\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfPartialPageReadWriteProtects(), 2);
		retval = TEST_ERROR;
		goto done;
	}

	debugArea->commitClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, &prot);

	if (prot.getNumberOfPartialPageReadOnlyProtects() != 1) {
		ERRPRINTF("prot.getNumberOfPartialPageReadWriteProtects() != 1\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfPartialPageReadWriteProtects(), 1);
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getNumberOfProtects() != 1) {
		ERRPRINTF("prot.getNumberOfProtects() != 1\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfProtects(), 1);
		retval = TEST_ERROR;
		goto done;
	}

	/**
	 * After adding 4th set of data LVT and LNT are next to each other:
	 *
	 * 	 size of LNT = rest of free space
	 * 	 size of LVT = 0
	 *
	 *   <------------------------ t -------------------------->
	 *    _____________________________________________________
	 *   |        |        |        |        |        |        |
	 *   |        |        |        |        |        |        |
	 *   |________|________|________|________|________|________|
	 *                                 AA
	 *                                 ||
	 *                                 ||
	 *                              LNT  LVT
	 *
	 *   number of partial pages to be unprotected before allocation: 2 (same page is unprotected twice)
	 *   number of partial pages to be protected after allocation: 1 (LNT and LVT are on same page)
	 *
	 */
	lntSize = debugArea->getFreeDebugSpaceBytes();
	lvtSize = 0;

	prot.reset();
	sizes.lineNumberTableSize = lntSize;
	sizes.localVariableTableSize = lvtSize;
	if (debugArea->allocateClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, (const J9RomClassRequirements *)&sizes, &pieces, &prot) != 0) {
		ERRPRINTF("allocateClassDebugData() failed\n");
		j9tty_printf(PORTLIB, "\tError: allocateClassDebugData() failed (free:%u sizes.lineNumberTableSize:%u sizes.localVariableTableSize:%u)\n",
			debugArea->getFreeDebugSpaceBytes(), sizes.lineNumberTableSize, sizes.localVariableTableSize);
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getNumberOfPartialPageReadWriteProtects() != 2) {
		ERRPRINTF("prot.getNumberOfPartialPageReadWriteProtects() != 2\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfPartialPageReadWriteProtects(), 2);
		retval = TEST_ERROR;
		goto done;
	}

	debugArea->commitClassDebugData(vm->mainThread, (U_16)strlen(testName), testName, &prot);

	if (prot.getNumberOfPartialPageReadOnlyProtects() != 1) {
		ERRPRINTF("prot.getNumberOfPartialPageReadWriteProtects() != 1\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfPartialPageReadWriteProtects(), 1);
		retval = TEST_ERROR;
		goto done;
	}

	if (prot.getNumberOfProtects() != 1) {
		ERRPRINTF("prot.getNumberOfProtects() != 1\n");
		j9tty_printf(PORTLIB, "\tError: %u != %u\n", prot.getNumberOfProtects(), 1);
		retval = TEST_ERROR;
		goto done;
	}

done:
	if (NULL != debugArea) {
		debugmemfree(vm, debugArea);
	}
	if (NULL != memAlloc) {
		j9mem_free_memory(memAlloc);
	}
	return retval;
}
