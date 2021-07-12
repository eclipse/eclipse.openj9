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

#include "j9port.h"
#include "j9.h"

#if !defined(UNITTESTING_HPP_INCLUDED)
#define UNITTESTING_HPP_INCLUDED

class UnitTest {
public:
	enum {
		NO_TEST = 0,
		ATTACHED_DATA_TEST,
		ATTACHED_DATA_UPDATE_COUNT_TEST,
		ATTACHED_DATA_CORRUPT_COUNT_TEST,
		BYTE_DATA_TEST,
		COMPILED_METHOD_TEST,
		COMPOSITE_CACHE_SIZES_TEST,
		COMPOSITE_CACHE_TEST,
		COMPOSITE_CACHE_TEST_SKIP_WRITE_COUNTER_UPDATE,
		CORRUPT_CACHE_TEST,
		SHAREDCACHE_API_TEST,
		MINMAX_TEST,
		PROTECT_NEW_ROMCLASS_DATA_TEST,
		CACHE_FULL_TEST,
		PROTECTA_SHARED_CACHE_DATA_TEST,
		STARTUP_HINTS_TEST,
		OSCACHE_TEST,
		CLASS_DEBUG_DATA_TEST
	};

	static IDATA unitTest;

	/* Used in SH_CompositeCache::startup() for memory protection on Win32.
	 * Any unit test using this field should ensure that it is OS page aligned.
	 */
	static void *cacheMemory;

	/* Used in SH_CompositeCache::startup() for memory protection on Win32.
	 * Any unit test using this field should ensure that it is OS page aligned.
	 */
	static U_32 cacheSize;
};

#endif	/* UNITTESTING_HPP_INCLUDED */
