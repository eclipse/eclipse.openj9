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

#include "hashtable_api.h"
#include "j2sever.h"
#include "j9consts.h"
#include "j9protos.h"
#include "objhelp.h"
#include "ModronAssertions.h"

#include "EnvironmentBase.hpp"
#include "GCExtensions.hpp"
#include "StringTable.hpp"
#include "VMHelpers.hpp"

/* the following is all ones except the least significant bit */
#define TYPE_UTF8 ((UDATA)1)

extern "C" {

typedef struct stringTableUTF8Query {
	U_8 *utf8Data;
	UDATA utf8Length;
	U_32 hash;
} stringTableUTF8Query;

static UDATA stringHashFn (void *key, void *userData);
static BOOLEAN stringHashEqualFn (void *leftKey, void *rightKey, void *userData);
static IDATA stringComparatorFn(struct J9AVLTree *tree, struct J9AVLTreeNode *leftNode, struct J9AVLTreeNode *rightNode);
static j9object_t setupCharArray(J9VMThread *vmThread, j9object_t sourceString, j9object_t newString);

MM_StringTable *
MM_StringTable::newInstance(MM_EnvironmentBase *env, UDATA tableCount)
{
	MM_StringTable *stringTable = (MM_StringTable *)env->getForge()->allocate(sizeof(MM_StringTable), MM_AllocationCategory::FIXED, J9_GET_CALLSITE());
	if (stringTable) {
		new(stringTable) MM_StringTable(env, tableCount);
		if(!stringTable->initialize(env)) {
			stringTable->kill(env);
			return NULL;
		}
	}
	return stringTable;
}

bool
MM_StringTable::initialize(MM_EnvironmentBase *env)
{
	J9JavaVM *javaVM = (J9JavaVM*)env->getOmrVM()->_language_vm;
	PORT_ACCESS_FROM_ENVIRONMENT(env);
	U_32 initialSize = 128;
	U_32 listToTreeThreshold = MM_GCExtensions::getExtensions(env)->_stringTableListToTreeThreshold;

	_table = (J9HashTable **)j9mem_allocate_memory(sizeof(J9HashTable *) * _tableCount, OMRMEM_CATEGORY_MM);
	if (NULL == _table) {
		return false;
	}
	memset(_table, 0, sizeof(J9HashTable *) * _tableCount);

	_mutex = (omrthread_monitor_t *)j9mem_allocate_memory(sizeof(omrthread_monitor_t) * _tableCount, OMRMEM_CATEGORY_MM);
	if (NULL == _mutex) {
		return false;
	}
	memset(_mutex, 0, sizeof(omrthread_monitor_t) * _tableCount);

	for (UDATA tableIndex = 0; tableIndex < _tableCount; tableIndex++) {
		_table[tableIndex] = collisionResilientHashTableNew(OMRPORT_FROM_J9PORT(javaVM->portLibrary), J9_GET_CALLSITE(), initialSize, sizeof(UDATA), 0, OMRMEM_CATEGORY_MM, listToTreeThreshold, stringHashFn, stringComparatorFn, NULL, javaVM);
		if (NULL == _table[tableIndex]) {
			return false;
		}
		if (0 != omrthread_monitor_init_with_name(&_mutex[tableIndex], 0, "GC string table")) {
			return false;
		}
	}

	memset(_cache, 0, sizeof(_cache));

	return true;
}


void
MM_StringTable::tearDown(MM_EnvironmentBase *env)
{
	PORT_ACCESS_FROM_ENVIRONMENT(env);

	if (NULL !=_table) {
		for (UDATA tableIndex = 0; tableIndex < _tableCount; tableIndex++) {
			if (NULL != _table[tableIndex]) {
				hashTableFree(_table[tableIndex]);
				_table[tableIndex] = NULL;
			}
		}
		j9mem_free_memory(_table);
		_table = NULL;
	}

	if (NULL != _mutex) {
		for (UDATA tableIndex = 0; tableIndex < _tableCount; tableIndex++) {
			if (_mutex[tableIndex]) {
				omrthread_monitor_destroy(_mutex[tableIndex]);
				_mutex[tableIndex] = NULL;
			}
		}
		j9mem_free_memory(_mutex);
		_mutex = NULL;
	}
}


void
MM_StringTable::kill(MM_EnvironmentBase *env)
{
	tearDown(env);
	env->getForge()->free(this);
}

/**
 * Find a string in the hash table.
 * @param table hash table
 * @param entry pointer to a String object or pointer to a stringTableUTF8Query
 * @return pointer to a String or NULL in case of failure.
 */
j9object_t
MM_StringTable::hashAt(UDATA tableIndex, j9object_t string)
{
	j9object_t *result;

	result = (j9object_t*)hashTableFind(_table[tableIndex], &string);
	if (NULL != result) {
		return *result;
	} else {
		return NULL;
	}
}

j9object_t
MM_StringTable::hashAtUTF8(UDATA tableIndex, U_8 *utf8Data, UDATA utf8Length, U_32 hash)
{
	stringTableUTF8Query query;
	void *ptr;

	query.utf8Data = utf8Data;
	query.utf8Length = utf8Length;
	query.hash = hash;
	ptr = &query;
	ptr = (void *) ((UDATA) ptr | TYPE_UTF8); /* Least significant bit indicates that this is a pointer to a stringTableUTF8Query */
	return hashAt(tableIndex, (j9object_t)ptr);
}

j9object_t
MM_StringTable::hashAtPut(UDATA tableIndex, j9object_t string)
{
	if (NULL == hashTableAdd(_table[tableIndex], &string)) {
		/* Failure to allocate a new node */
		return NULL;
	} else {
		return string;
	}
}


j9object_t
MM_StringTable::addStringToInternTable(J9VMThread *vmThread, j9object_t string)
{
	j9object_t internedString;

	UDATA hash = stringHashFn(&string, vmThread->javaVM);
	UDATA tableIndex = getTableIndex(hash);

	lockTable(tableIndex);

	internedString = (j9object_t) hashAt(tableIndex, string);

	if (NULL == internedString) {
		internedString = hashAtPut(tableIndex, string);
	}

	unlockTable(tableIndex);

	if (NULL == internedString) {
		Trc_MM_StringTable_stringAddToInternTableFailed(vmThread, string, _table, tableIndex);
	}

	return internedString;
}


static IDATA
stringComparatorFn(struct J9AVLTree *tree, struct J9AVLTreeNode *leftNode, struct J9AVLTreeNode *rightNode)
{
	J9JavaVM *javaVM = (J9JavaVM*) tree->userData;
	j9object_t right_s = NULL;
	U_32 rightLength = 0;
	j9object_t right_p = NULL;
	U_32 right_i = 0;
	UDATA stu8Ptr = 0;
	IDATA rc = 0;
	bool rightCompressed = false;

	/* leftNode data may be a pointer to a low-tagged pointer to a struct (it is the node used for hashTableFind) */
	stu8Ptr = *((UDATA*) (leftNode+1)); 

	/* Get at the String information  */
	right_s = J9WEAKROOT_OBJECT_LOAD_VM(javaVM, (j9object_t *)(rightNode+1));

	rightLength = J9VMJAVALANGSTRING_LENGTH_VM(javaVM, right_s);
	right_p = J9VMJAVALANGSTRING_VALUE_VM(javaVM, right_s);
	rightCompressed = IS_STRING_COMPRESSED_VM(javaVM, right_s);

	if (stu8Ptr & TYPE_UTF8) {
		stringTableUTF8Query *leftUTF8 = (stringTableUTF8Query*) (stu8Ptr & ~TYPE_UTF8);
		U_32 leftLength = (U_32) leftUTF8->utf8Length;
		U_8 *u8Ptr = leftUTF8->utf8Data;
		U_32 left_i = 0;
		U_32 i = 0;

		for (i = 0; i < rightLength; i++) {
			U_16 leftChar = 0;
			U_16 rightChar = 0;
			U_32 consumed = 0;

			consumed = (U_32)VM_VMHelpers::decodeUTF8CharN(u8Ptr+left_i, &leftChar, leftLength-left_i);
			if (0 == consumed) { 
				/* reached the end of the left string early */
				rc = -1;
				goto done;
			}
			left_i += consumed;
			if (rightCompressed) {
				rightChar = (U_16)J9JAVAARRAYOFBYTE_LOAD_VM(javaVM, right_p, right_i) & (U_16)0xFF;
			} else {
				rightChar = J9JAVAARRAYOFCHAR_LOAD_VM(javaVM, right_p, right_i);
			}
			right_i += 1;
			if (leftChar != rightChar) {
				/* consider shorter strings as "lower" than longer strings */
				rc = (IDATA)leftChar - (IDATA)rightChar;
				goto done;
			}
		}

		/* handle the case where the left key is longer than the right key */
		if (left_i != leftLength) {
			rc = 1;
			goto done;
		}

		if (!checkStringConstantLive(javaVM, right_s)) {
			/* Consider dead strings as 'lower than' live strings for the comparator.
			 * Also consider UTF8s as alive.
			 */
			rc = -1;
			goto done;
		}

		/* The strings had the same characters with the same length and both were alive */
		rc = 0;
	} else {
		j9object_t left_s = NULL;
		U_32 leftLength = 0;
		j9object_t left_p = NULL;
		U_32 left_i = 0;
		U_32 i = 0;
		bool leftCompressed = false;

		left_s = J9WEAKROOT_OBJECT_LOAD_VM(javaVM, (j9object_t *)(leftNode+1));

		leftLength = J9VMJAVALANGSTRING_LENGTH_VM(javaVM, left_s);
		left_p = J9VMJAVALANGSTRING_VALUE_VM(javaVM, left_s);
		leftCompressed = IS_STRING_COMPRESSED_VM(javaVM, left_s);

		for (i = 0; i < OMR_MIN(leftLength, rightLength); i++) {
			U_16 leftChar = 0;
			U_16 rightChar = 0;
			
			if (leftCompressed) {
				leftChar = (U_16)J9JAVAARRAYOFBYTE_LOAD_VM(javaVM, left_p, left_i) & (U_16)0xFF;
			} else {
				leftChar = J9JAVAARRAYOFCHAR_LOAD_VM(javaVM, left_p, left_i);
			}
			left_i += 1;
			
			if (rightCompressed) {
				rightChar = (U_16)J9JAVAARRAYOFBYTE_LOAD_VM(javaVM, right_p, right_i) & (U_16)0xFF;
			} else {
				rightChar = J9JAVAARRAYOFCHAR_LOAD_VM(javaVM, right_p, right_i);
			}
			right_i += 1;
			
			if (leftChar != rightChar) {
				rc = (IDATA)leftChar - (IDATA)rightChar;
				goto done;
			}
		}
		if (leftLength != rightLength) {
			rc = (IDATA)leftLength - (IDATA)rightLength;
			goto done;
		}

		/* Consider 'live' strings as 'higher' than their dead counterparts. */
		BOOLEAN leftStringAlive = checkStringConstantLive(javaVM, left_s);
		BOOLEAN rightStringAlive = checkStringConstantLive(javaVM, right_s);

		if (leftStringAlive && (!rightStringAlive)) {
			rc = 1;
		} else if (rightStringAlive && (!leftStringAlive)) {
			rc = -1;
		} else if (rightStringAlive == leftStringAlive) {
			rc = 0;
		} else {
			Assert_MM_unreachable();
		}
	}

done:
	return rc;

}

/**
 * Compare two strings for equality.  The second string may be a Java string or a UTF8 string.
 * @param leftKey pointer to a String object
 * @param rightKey pointer to a string. If the least significant bit is zero, it is a pointer to a pointer to stringTableUTF8Query struct, otherwise it is a String object.
 * @param userData pointer to Java VM Thread
 */

static BOOLEAN
stringHashEqualFn(void *leftKey, void *rightKey, void *userData)
{
	J9JavaVM *javaVM = (J9JavaVM*) userData;
	j9object_t left_s = NULL;
	U_32 leftLength = 0;
	j9object_t left_p = NULL;
	U_32 left_i = 0;
	UDATA stu8Ptr = 0;
	UDATA rc = TRUE;
	bool leftCompressed = false;

	stu8Ptr = *((UDATA*) rightKey); /* rightKey may be a pointer to a low-tagged pointer to a struct */

	/* Get at the String information  */
	left_s = *(j9object_t *)leftKey;
	leftLength = J9VMJAVALANGSTRING_LENGTH_VM(javaVM, left_s);
	left_p = J9VMJAVALANGSTRING_VALUE_VM(javaVM, left_s);
	leftCompressed = IS_STRING_COMPRESSED_VM(javaVM, left_s);

	if (stu8Ptr & TYPE_UTF8) {
		stringTableUTF8Query *rightUTF8 = (stringTableUTF8Query*) (stu8Ptr & ~TYPE_UTF8);
		U_32 rightLength = (U_32) rightUTF8->utf8Length;
		U_8 *u8Ptr = rightUTF8->utf8Data;
		U_32 right_i = 0;
		U_32 i;

		for (i = 0; i < leftLength; i++) {
			U_16 leftChar, rightChar;
			U_32 consumed;

			consumed = (U_32)VM_VMHelpers::decodeUTF8CharN(u8Ptr+right_i, &rightChar, rightLength-right_i);
			if (0 == consumed) { /* reached the end of the string early */
				return FALSE;
			}
			right_i += consumed;
			if (leftCompressed) {
				leftChar = (U_16)J9JAVAARRAYOFBYTE_LOAD_VM(javaVM, left_p, left_i) & (U_16)0xFF;
			} else {
				leftChar = J9JAVAARRAYOFCHAR_LOAD_VM(javaVM, left_p, left_i);
			}
			left_i++;
			if (leftChar != rightChar) {
				return FALSE;
			}
		}

		/* handle the case where the left key is shorter than the right key */
		if (right_i != rightLength) {
			return FALSE;
		}

		/*
		 * Only consider it to be a match if both string constants are live (i.e. they aren't about to be cleared).
		 * Since the right string is UTF8, re-use the left string as a placeholder
		 */
		rc = checkStringConstantsLive(javaVM, left_s, left_s);
	} else {
		j9object_t right_s = *(j9object_t *)rightKey;
		U_32 right_i = 0;
		j9object_t right_p = J9VMJAVALANGSTRING_VALUE_VM(javaVM, right_s);
		U_32 rightLength = J9VMJAVALANGSTRING_LENGTH_VM(javaVM, right_s);
		U_32 i;
		bool rightCompressed = IS_STRING_COMPRESSED_VM(javaVM, right_s);

		/* Lengths have different significance for String and UTF8 */
		if ((J9VMJAVALANGSTRING_HASH_VM(javaVM, left_s) != J9VMJAVALANGSTRING_HASH_VM(javaVM, right_s))
			|| (leftLength != rightLength))	{
			return FALSE;
		}

		for (i = 0; i < leftLength; i++) {
			U_16 leftChar, rightChar;
			if (rightCompressed) {
				rightChar = (U_16)J9JAVAARRAYOFBYTE_LOAD_VM(javaVM, right_p, right_i) & (U_16)0xFF;
			} else {
				rightChar = J9JAVAARRAYOFCHAR_LOAD_VM(javaVM, right_p, right_i);
			}

			right_i++;
			if (leftCompressed) {
				leftChar = (U_16)J9JAVAARRAYOFBYTE_LOAD_VM(javaVM, left_p, left_i) & (U_16)0xFF;
			} else {
				leftChar = J9JAVAARRAYOFCHAR_LOAD_VM(javaVM, left_p, left_i);
			}
			left_i++;
			if (leftChar != rightChar) {
				return FALSE;
			}
		}

		/*
		 * Only consider it to be a match if both string constants are live (i.e. they aren't about to be cleared).
		 */
		rc = checkStringConstantsLive(javaVM, left_s, right_s);
	}

	return rc;
}

BOOLEAN
j9gc_stringHashEqualFn(void *leftKey, void *rightKey, void *userData)
{
	return stringHashEqualFn(leftKey, rightKey, userData);
}

U_32 
computeJavaHashForExpandedString(J9JavaVM *javaVM, j9object_t s)
{
	U_32 hash = 0;
	I_32 i;
	I_32 length = J9VMJAVALANGSTRING_LENGTH_VM(javaVM, s);
	j9object_t bytes = J9VMJAVALANGSTRING_VALUE_VM(javaVM, s);

	if (IS_STRING_COMPRESSED_VM(javaVM, s)) {
		for (i = 0; i < length; ++i) {
			hash = (hash << 5) - hash + ((U_16)J9JAVAARRAYOFBYTE_LOAD_VM(javaVM, bytes, i) & (U_16)0xFF);
		}
	} else {
		for (i = 0; i < length; ++i) {
			hash = (hash << 5) - hash + ((U_16)J9JAVAARRAYOFCHAR_LOAD_VM(javaVM, bytes, i));
		}
	}

	return hash;
}

/**
 * stringHashFn
 * Hash a string stored either as a UTF8 string or String object
 * @param key pointer to a low-tagged pointer either to a struct containing a UTF string pointer and length (bit 0 set) or a String object (bit 0 cleared)
 * @param userData pointer to javaVM
 */
static UDATA
stringHashFn(void *key, void *userData)
{
	Assert_MM_false(userData == NULL);
	J9JavaVM *javaVM = (J9JavaVM*) userData;
	U_32 hashCode;
	UDATA stu8Ptr;

	stu8Ptr = *((UDATA*) key);
	if (stu8Ptr & TYPE_UTF8) { /* test least significant bit */
		stringTableUTF8Query* u8Ptr;

		u8Ptr =  (stringTableUTF8Query*) (stu8Ptr & ~TYPE_UTF8); /* clear the bit */
		hashCode = u8Ptr->hash;
	} else {
		j9object_t s = *(j9object_t*)key;
		hashCode = J9VMJAVALANGSTRING_HASH_VM(javaVM, s);
		if (hashCode == 0) {
			hashCode = computeJavaHashForExpandedString(javaVM, s);
			J9VMJAVALANGSTRING_SET_HASH_VM(javaVM, s, hashCode);
		}
	}

	return hashCode;
}

UDATA
j9gc_stringHashFn(void *key, void *userData)
{
	return stringHashFn(key, userData);
}

/**
 * Assuming incoming UTF8 byte stream contains only ISO-8859-1/Latin-1 characters.
 * Decode data & store to byteArray.
 * 
 * @param vmThread[in] the VM thread
 * @param data[in] the UTF8 byte stream
 * @param length[in] the length of incoming UTF8 byte stream
 * @param byteArray[out] the target byte array to store the data
 * @param translateSlashes[in] the flag to translate slashes
 * 
 * @return last slash position to be restored to `/` for anonymous classes
 */
MMINLINE static UDATA
storeLatin1ByteArrayhelper(J9VMThread *vmThread, U_8 *data, UDATA length, j9object_t byteArray, bool translateSlashes)
{
	UDATA lastSlash = 0;
	UDATA index = 0;
	while (0 != length) {
		U_16 unicode = 0;
		UDATA consumed = VM_VMHelpers::decodeUTF8CharN(data, &unicode, length);
		Assert_GC_true_with_message2(MM_EnvironmentBase::getEnvironment(vmThread->omrVMThread), 0 != consumed, "Invalid UTF8 character at %p, %zu", data, length);
		data += consumed;
		length -= consumed;
		if (translateSlashes && ('/' == unicode)) {
			lastSlash = index;
			unicode = '.';
		}
		J9JAVAARRAYOFBYTE_STORE(vmThread, byteArray, index, unicode);
		index += 1;
	}
	return lastSlash;
}

j9object_t
j9gc_createJavaLangString(J9VMThread *vmThread, U_8 *data, UDATA length, UDATA stringFlags)
{
	J9JavaVM *vm = vmThread->javaVM;
	J9InternalVMFunctions * const vmFuncs = vm->internalVMFunctions;
	MM_StringTable *stringTable = MM_GCExtensions::getExtensions(vm->omrVM)->getStringTable();
	j9object_t result = NULL;
	j9object_t charArray = NULL;
	bool compressStrings = IS_STRING_COMPRESSION_ENABLED_VM(vm);
	bool isUnicode = J9_ARE_ANY_BITS_SET(stringFlags, J9_STR_UNICODE);
	bool translateSlashes = J9_ARE_ANY_BITS_SET(stringFlags, J9_STR_XLAT);
	bool anonClassName = J9_ARE_ANY_BITS_SET(stringFlags, J9_STR_ANON_CLASS_NAME);
	bool internString = J9_ARE_ANY_BITS_SET(stringFlags, J9_STR_INTERN);

	Trc_MM_createJavaLangString_Entry(vmThread, length, data, stringFlags);

	/* Determine if the string contains only ASCII (<= 0x7F) characters */
	bool isASCII = true;
	/* Determine if the string contains only Latin-1 (<= 0xFF) characters */
	bool isASCIIorLatin1 = true;
	if (isUnicode) {
		/* Translation and anon class name are not currently supported for unicode input */
		Assert_MM_false(translateSlashes || anonClassName);
		/* Currently, the only users of isASCII when isUnicode is true are also gated by compressStrings, so
		 * don't bother computing isASCII if compression is off.
		 */
		if (compressStrings) {
			U_16 *unicodeData = (U_16*)data;
			for (UDATA i = 0; i < length; ++i) {
				if (unicodeData[i] > 0x7F) {
					isASCII = false;
					if (J2SE_VERSION(vm) >= J2SE_V17) {
						for (UDATA j = i; j < length; ++j) {
							if (unicodeData[j] > 0xFF) {
								isASCIIorLatin1 = false;
								break;
							}
						}
					} else {
						isASCIIorLatin1 = false;
					}
					break;
				}
			}
		} else {
			/* For future safety, ensure isASCII is false if not computed correctly */
			isASCII = false;
			isASCIIorLatin1 = false;
		}
	} else {
		for (UDATA i = 0; i < length; ++i) {
			if (data[i] > 0x7F) {
				isASCII = false;
				if (compressStrings && (J2SE_VERSION(vm) >= J2SE_V17)) {
					U_8 *dataTmp = data + i;
					UDATA lengthTmp = length - i;
					isASCIIorLatin1 = VM_VMHelpers::isLatin1String(dataTmp, lengthTmp);
				} else {
					isASCIIorLatin1 = false;
				}
				break;
			}
		}
	}

	/* For strings that should be interned, and don't need to be converted or translated,
	 * check if they are already in the string hashtable.  Otherwise, don't bother with
	 * the work to check the table.  This may result in slightly higher footprint but saves
	 * on the common case where we don't expect the string to have been interned.
	 *
	 * See if the string is already in the table. Race condition where another thread may
	 * add the string before this one is not fatal and is ignored.
	 */

	if (internString && !translateSlashes && !isUnicode) {
		UDATA hash = 0;

		if (isASCII) {
			for (UDATA i = 0; i < length; ++i) {
				hash = (hash << 5) - hash + data[i];
			}
		} else {
			hash = VM_VMHelpers::computeHashForUTF8(data, length);
		}

		UDATA tableIndex = stringTable->getTableIndex(hash);

		stringTable->lockTable(tableIndex);
		result = stringTable->hashAtUTF8(tableIndex, data, length, (U_32)hash);
		stringTable->unlockTable(tableIndex);
	}

	if (NULL == result) {
		UDATA unicodeLength = 0;
		UDATA allocateFlags = J9_ARE_ANY_BITS_SET(stringFlags, J9_STR_INSTRUMENTABLE)
				? J9_GC_ALLOCATE_OBJECT_INSTRUMENTABLE
				: J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE;

		if (J9_ARE_ANY_BITS_SET(stringFlags, J9_STR_TENURE | J9_STR_INTERN)) {
			allocateFlags |= J9_GC_ALLOCATE_OBJECT_TENURED;
		}

		J9Class *stringClass = J9VMJAVALANGSTRING_OR_NULL(vm);
		result = J9AllocateObject(vmThread, stringClass, allocateFlags);
		if (result == NULL) {
			goto nomem;
		}

		if (isUnicode) {
			unicodeLength = length / 2;
		} else {
			if (isASCII) {
				unicodeLength = length;
			} else {
				UDATA tempLength = length;
				U_8 *tempData = data;
				while (tempLength != 0) {
					U_16 unicode = 0;
					UDATA consumed = VM_VMHelpers::decodeUTF8CharN(tempData, &unicode, tempLength);
					Assert_GC_true_with_message2(MM_EnvironmentBase::getEnvironment(vmThread->omrVMThread), 0 != consumed, "Invalid UTF8 character at %p, %zu", tempData, tempLength);
					tempData += consumed;
					tempLength -= consumed;
					unicodeLength += 1;
				}
			}
		}

		PUSH_OBJECT_IN_SPECIAL_FRAME(vmThread, result);

		if (J9_ARE_ANY_BITS_SET(vm->runtimeFlags, J9_RUNTIME_STRING_BYTE_ARRAY)) {
			if (compressStrings && isASCIIorLatin1) {
				charArray = J9AllocateIndexableObject(vmThread, vm->byteArrayClass, (U_32) unicodeLength, allocateFlags);
			} else {
				charArray = J9AllocateIndexableObject(vmThread, vm->byteArrayClass, (U_32) unicodeLength * 2, allocateFlags);
			}
		} else {
			if (compressStrings && isASCII) {
				charArray = J9AllocateIndexableObject(vmThread, vm->charArrayClass, (U_32) (unicodeLength + 1) / 2, allocateFlags);
			} else {
				charArray = J9AllocateIndexableObject(vmThread, vm->charArrayClass, (U_32) unicodeLength, allocateFlags);
			}
		}

		result = POP_OBJECT_IN_SPECIAL_FRAME(vmThread);

		if (charArray == NULL) {
			goto nomem;
		}

		if (isUnicode) {
			U_16 * unicodeData = (U_16 *) data;

			if (compressStrings && isASCIIorLatin1) {
				for (UDATA i = 0; i < unicodeLength; ++i) {
					J9JAVAARRAYOFBYTE_STORE(vmThread, charArray, i, (I_8)unicodeData[i]);
				}
			} else {
				for (UDATA i = 0; i < unicodeLength; ++i) {
					J9JAVAARRAYOFCHAR_STORE(vmThread, charArray, i, unicodeData[i]);
				}
			}
		} else {
			if (compressStrings && isASCIIorLatin1) {
				UDATA lastSlash = 0;
				if (translateSlashes) {
					if (isASCII) {
						for (UDATA i = 0; i < length; ++i) {
							U_8 c = data[i];
							if ('/' == c) {
								lastSlash = i;
								c = '.';
							}
							J9JAVAARRAYOFBYTE_STORE(vmThread, charArray, i, c);
						}
					} else {
						lastSlash = storeLatin1ByteArrayhelper(vmThread, data, length, charArray, true);
					}
				} else {
					if (isASCII) {
						for (UDATA i = 0; i < length; ++i) {
							J9JAVAARRAYOFBYTE_STORE(vmThread, charArray, i, data[i]);
						}
					} else {
						lastSlash = storeLatin1ByteArrayhelper(vmThread, data, length, charArray, false);
					}
				}

				/* Anonymous classes have the following name format [className]/[ROMADDRESS], so we have to fix up the name
				 * because the previous loops have converted '/' to '.' already.
				 */
				if (anonClassName && (0 != lastSlash)) {
					J9JAVAARRAYOFBYTE_STORE(vmThread, charArray, lastSlash, (U_8)'/');
				}
			} else {
				UDATA lastSlash = 0;
				if (isASCII) {
					if (translateSlashes) {
						for (UDATA i = 0; i < length; ++i) {
							U_8 c = data[i];
							if ('/' == c) {
								lastSlash = i;
								c = '.';
							}
							J9JAVAARRAYOFCHAR_STORE(vmThread, charArray, i, c);
						}
					} else {
						for (UDATA i = 0; i < length; ++i) {
							J9JAVAARRAYOFCHAR_STORE(vmThread, charArray, i, data[i]);
						}
					}
				} else {
					UDATA writeIndex = 0;
					UDATA tempLength = length;
					U_8* tempData = data;
					while (tempLength > 0) {
						U_16 unicode = 0;
						UDATA consumed = VM_VMHelpers::decodeUTF8Char(tempData, &unicode);
						if (translateSlashes) {
							if ((U_16)'/' == unicode) {
								lastSlash = writeIndex;
								unicode = (U_16)'.';
							}
						}
						J9JAVAARRAYOFCHAR_STORE(vmThread, charArray, writeIndex, unicode);
						writeIndex += 1;
						tempData += consumed;
						tempLength -= consumed;
					}
				}

				/* Anonymous classes have the following name format [className]/[ROMADDRESS], so we have to fix up the name
				 * because the previous loops have converted '/' to '.' already.
				 */
				if (anonClassName && (0 != lastSlash)) {
					J9JAVAARRAYOFCHAR_STORE(vmThread, charArray, lastSlash, (U_16)'/');
				}
			}
		}

		J9VMJAVALANGSTRING_SET_VALUE(vmThread, result, charArray);

		if (compressStrings) {
			if (isASCIIorLatin1) {
				if (J2SE_VERSION(vm) >= J2SE_V11) {
					J9VMJAVALANGSTRING_SET_CODER(vmThread, result, 0);
				} else {
					J9VMJAVALANGSTRING_SET_COUNT(vmThread, result, (I_32)unicodeLength);
				}
			} else {
				if (J2SE_VERSION(vm) >= J2SE_V11) {
					J9VMJAVALANGSTRING_SET_CODER(vmThread, result, 1);
				} else {
					J9VMJAVALANGSTRING_SET_COUNT(vmThread, result, (I_32)unicodeLength | (I_32)0x80000000);
				}

				if (J9VMJAVALANGSTRING_COMPRESSIONFLAG(vmThread, stringClass) == 0) {
					/*
					 * jitHookClassPreinitialize will process initialization events for String compression sideEffectGuards
					 * so we must initialize the class if this is the first time we are loading it
					 */
					J9Class* flagClass = vmFuncs->internalFindKnownClass(vmThread, J9VMCONSTANTPOOL_JAVALANGSTRINGSTRINGCOMPRESSIONFLAG, J9_FINDKNOWNCLASS_FLAG_INITIALIZE);

					if (NULL == flagClass) {
						goto nomem;
					} else {
						j9object_t flag = J9AllocateObject(vmThread, flagClass, allocateFlags);

						if (NULL == flag) {
							goto nomem;
						}

						J9VMJAVALANGSTRING_SET_COMPRESSIONFLAG(vmThread, stringClass, flag);
					}
				}
			}
		} else {
			if (J2SE_VERSION(vm) >= J2SE_V11) {
				J9VMJAVALANGSTRING_SET_CODER(vmThread, result, 1);
			} else {
				J9VMJAVALANGSTRING_SET_COUNT(vmThread, result, (I_32)unicodeLength);
			}
		}

		MM_AtomicOperations::writeBarrier();

		/* Intern the String if requested */

		if (internString) {
			result = stringTable->addStringToInternTable(vmThread, result);
			if (NULL == result) {
				goto nomem;
			}
		}
	}

	Trc_MM_createJavaLangString_Exit(vmThread, result);
	return result;

nomem:
	vmFuncs->setHeapOutOfMemoryError(vmThread);
	return NULL;
}

/**
 * Points newString at sourceString's char[].
 * If necessary, a new copy of the char[] will be created
 * in tenured memory and the newString will point to the copy.
 * newString may move because this function could trigger
 * a GC, therefore the newString will be returned on success.
 *
 * @param[in] vmThread A pointer to the current thread
 * @param[in] sourceString A pointer to the source string
 * @param[in] newString A pointer to the newly created string that's about to be interned
 *
 * @return The new string on success, otherwise NULL
 */
j9object_t
setupCharArray(J9VMThread *vmThread, j9object_t sourceString, j9object_t newString)
{
	J9JavaVM *vm = vmThread->javaVM;
	bool isCompressed = IS_STRING_COMPRESSED(vmThread, sourceString);
	I_32 length = J9VMJAVALANGSTRING_LENGTH(vmThread, sourceString);
	j9object_t result = NULL;
	j9object_t newChars = NULL;

	PUSH_OBJECT_IN_SPECIAL_FRAME(vmThread, sourceString);
	PUSH_OBJECT_IN_SPECIAL_FRAME(vmThread, newString);

	/* Construct the interned string data from the original */
	if (J9_ARE_ANY_BITS_SET(vm->runtimeFlags, J9_RUNTIME_STRING_BYTE_ARRAY)) {
		if (isCompressed) {
			newChars = J9AllocateIndexableObject(vmThread, vm->byteArrayClass, (U_32) length, J9_GC_ALLOCATE_OBJECT_TENURED | J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
		} else {
			newChars = J9AllocateIndexableObject(vmThread, vm->byteArrayClass, (U_32) length * 2, J9_GC_ALLOCATE_OBJECT_TENURED | J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
		}
	} else {
		if (isCompressed) {
			newChars = J9AllocateIndexableObject(vmThread, vm->charArrayClass, (U_32) (length + 1) / 2, J9_GC_ALLOCATE_OBJECT_TENURED | J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
		} else {
			newChars = J9AllocateIndexableObject(vmThread, vm->charArrayClass, (U_32) length, J9_GC_ALLOCATE_OBJECT_TENURED | J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
		}
	}

	if (NULL != newChars) {
		I_32 i = 0;
		j9object_t oldChars = NULL;
		
		newString = POP_OBJECT_IN_SPECIAL_FRAME(vmThread);
		sourceString = POP_OBJECT_IN_SPECIAL_FRAME(vmThread);

		oldChars = J9VMJAVALANGSTRING_VALUE(vmThread, sourceString);

		if (isCompressed) {
			for (i = 0; i < length; ++i) {
				J9JAVAARRAYOFBYTE_STORE(vmThread, newChars, i, J9JAVAARRAYOFBYTE_LOAD(vmThread, oldChars, i));
			}
		} else {
			for (i = 0; i < length; ++i) {
				J9JAVAARRAYOFCHAR_STORE(vmThread, newChars, i, J9JAVAARRAYOFCHAR_LOAD(vmThread, oldChars, i));
			}
		}

		J9VMJAVALANGSTRING_SET_VALUE(vmThread, newString, newChars);

		if (J2SE_VERSION(vm) >= J2SE_V11) {
			J9VMJAVALANGSTRING_SET_CODER(vmThread, newString, J9VMJAVALANGSTRING_CODER(vmThread, sourceString));
		} else {
			J9VMJAVALANGSTRING_SET_COUNT(vmThread, newString, J9VMJAVALANGSTRING_COUNT(vmThread, sourceString));
		}

		result = newString;
	}

	return result;
}


j9object_t	
j9gc_internString(J9VMThread *vmThread, j9object_t sourceString)
{
	J9JavaVM *vm = vmThread->javaVM;
	J9InternalVMFunctions * const vmFuncs = vm->internalVMFunctions;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(vm->omrVM);
	MM_StringTable *stringTable = extensions->getStringTable();
	j9object_t internedString = NULL;
	j9object_t *candidatePtr = NULL;
	j9object_t candidate = NULL;

	UDATA hash = stringHashFn(&sourceString, vm);

	candidatePtr = stringTable->getStringInternCache(hash);
	candidate = *candidatePtr;

	if ((NULL != candidate) && stringHashEqualFn(&candidate, &sourceString, vm)) {
		/*
		 * This can only be used if the current candidate pointer is live.
		 * Pass in candidate twice since we only have one string.
		 */
		if (checkStringConstantsLive(vm, candidate, candidate)) {
			Trc_MM_stringTableCacheHit(vmThread, candidate);
			return candidate;
		}
	}

	UDATA tableIndex = stringTable->getTableIndex(hash);

	stringTable->lockTable(tableIndex);
	internedString = stringTable->hashAt(tableIndex, sourceString);
	stringTable->unlockTable(tableIndex);
	
	if (NULL == internedString) {
		j9object_t newString = NULL;

		PUSH_OBJECT_IN_SPECIAL_FRAME(vmThread, sourceString);
		newString = J9AllocateObject(vmThread, J9OBJECT_CLAZZ(vmThread, sourceString), J9_GC_ALLOCATE_OBJECT_TENURED | J9_GC_ALLOCATE_OBJECT_NON_INSTRUMENTABLE);
		sourceString = POP_OBJECT_IN_SPECIAL_FRAME(vmThread);
		if (NULL != newString) {
			/* newString may move because setupCharArray may trigger a GC */
			newString = setupCharArray(vmThread, sourceString, newString);
			if (NULL != newString) {
				internedString = stringTable->addStringToInternTable(vmThread, newString);
			}
		}

		if (NULL == internedString) {
			vmFuncs->setHeapOutOfMemoryError(vmThread);
		}
	}

	*candidatePtr = internedString;
	Trc_MM_stringTableCacheMiss(vmThread, internedString);
	return internedString;
}

} /* end of extern "C" */
