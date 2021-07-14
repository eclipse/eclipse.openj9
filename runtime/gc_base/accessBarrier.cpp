/*******************************************************************************
 * Copyright (c) 1991, 2021 IBM Corp. and others
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
 * @file
 * @ingroup GC_Base
 */

#include "j9.h"
#include "j9cfg.h"

#include "Debug.hpp"
#include "GCExtensions.hpp"
#include "ObjectAccessBarrier.hpp"

#include "ModronAssertions.h"

extern "C" {

#if defined(J9VM_GC_OBJECT_ACCESS_BARRIER)

J9Object *
j9gc_objaccess_mixedObjectReadObject(J9VMThread *vmThread, J9Object *srcObject, UDATA offset, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->mixedObjectReadObject(vmThread, srcObject, offset, 0 != isVolatile);
}

void
j9gc_objaccess_mixedObjectStoreObject(J9VMThread *vmThread, J9Object *destObject, UDATA offset, J9Object *value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->mixedObjectStoreObject(vmThread, destObject, offset, value, 0 != isVolatile);
}

void *
j9gc_objaccess_mixedObjectReadAddress(J9VMThread *vmThread, J9Object *srcObject, UDATA offset, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->mixedObjectReadAddress(vmThread, srcObject, offset, 0 != isVolatile);
}

void
j9gc_objaccess_mixedObjectStoreAddress(J9VMThread *vmThread, J9Object *destObject, UDATA offset, void * value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->mixedObjectStoreAddress(vmThread, destObject, offset, value, 0 != isVolatile);
}

UDATA
j9gc_objaccess_mixedObjectReadU32(J9VMThread *vmThread, J9Object *srcObject, UDATA offset, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->mixedObjectReadU32(vmThread, srcObject, offset, 0 != isVolatile);
}

void
j9gc_objaccess_mixedObjectStoreU32(J9VMThread *vmThread, J9Object *destObject, UDATA offset, U_32 value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->mixedObjectStoreU32(vmThread, destObject, offset, value, 0 != isVolatile);
}

IDATA
j9gc_objaccess_mixedObjectReadI32(J9VMThread *vmThread, J9Object *srcObject, UDATA offset, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->mixedObjectReadI32(vmThread, srcObject, offset, 0 != isVolatile);
}

void
j9gc_objaccess_mixedObjectStoreI32(J9VMThread *vmThread, J9Object *destObject, UDATA offset, I_32 value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->mixedObjectStoreI32(vmThread, destObject, offset, value, 0 != isVolatile);
}

U_64
j9gc_objaccess_mixedObjectReadU64(J9VMThread *vmThread, J9Object *srcObject, UDATA offset, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->mixedObjectReadU64(vmThread, srcObject, offset, 0 != isVolatile);
}

void
j9gc_objaccess_mixedObjectStoreU64(J9VMThread *vmThread, J9Object *destObject, UDATA offset, U_64 value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->mixedObjectStoreU64(vmThread, destObject, offset, value, 0 != isVolatile);
}

I_64
j9gc_objaccess_mixedObjectReadI64(J9VMThread *vmThread, J9Object *srcObject, UDATA offset, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->mixedObjectReadI64(vmThread, srcObject, offset, 0 != isVolatile);
}

void
j9gc_objaccess_mixedObjectStoreI64(J9VMThread *vmThread, J9Object *destObject, UDATA offset, I_64 value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->mixedObjectStoreI64(vmThread, destObject, offset, value, 0 != isVolatile);
}

void 
j9gc_objaccess_mixedObjectStoreU64Split(J9VMThread *vmThread, J9Object *destObject, UDATA delta, U_32 valueSlot0, U_32 valueSlot1, UDATA isVolatile)
{
	U_64 value64;
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;

	/* This is endian-independent. valueSlot0 is always the low 4 bytes in memory */
	*(U_32 *)&value64 = valueSlot0;
	*(((U_32 *)&value64) + 1) = valueSlot1;

	barrier->mixedObjectStoreU64(vmThread, destObject, delta, value64, 0 != isVolatile);
}

J9Object *
j9gc_objaccess_indexableReadObject(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->indexableReadObject(vmThread, srcObject, index, 0 != isVolatile);
}

void
j9gc_objaccess_indexableStoreObject(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, J9Object *value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->indexableStoreObject(vmThread, destObject, index, value, 0 != isVolatile);
}

void *
j9gc_objaccess_indexableReadAddress(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->indexableReadAddress(vmThread, srcObject, index, 0 != isVolatile);
}

void
j9gc_objaccess_indexableStoreAddress(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, void * value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->indexableStoreAddress(vmThread, destObject, index, value, 0 != isVolatile);
}

UDATA
j9gc_objaccess_indexableReadU8(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return (U_32)barrier->indexableReadU8(vmThread, srcObject, index, 0 != isVolatile);
}

void
j9gc_objaccess_indexableStoreU8(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, U_32 value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->indexableStoreU8(vmThread, destObject, index, (U_8)value, 0 != isVolatile);
}

IDATA
j9gc_objaccess_indexableReadI8(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return (I_32)barrier->indexableReadI8(vmThread, srcObject, index, 0 != isVolatile);
}

void
j9gc_objaccess_indexableStoreI8(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, I_32 value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->indexableStoreI8(vmThread, destObject, index, (I_8)value, 0 != isVolatile);
}

UDATA
j9gc_objaccess_indexableReadU16(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return (U_32)barrier->indexableReadU16(vmThread, srcObject, index, 0 != isVolatile);
}

void
j9gc_objaccess_indexableStoreU16(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, U_32 value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->indexableStoreU16(vmThread, destObject, index, (U_16)value, 0 != isVolatile);
}

IDATA
j9gc_objaccess_indexableReadI16(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return (I_32)barrier->indexableReadI16(vmThread, srcObject, index, 0 != isVolatile);
}

void
j9gc_objaccess_indexableStoreI16(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, I_32 value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->indexableStoreI16(vmThread, destObject, index, (I_16)value, 0 != isVolatile);
}

UDATA
j9gc_objaccess_indexableReadU32(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->indexableReadU32(vmThread, srcObject, index, 0 != isVolatile);
}

void
j9gc_objaccess_indexableStoreU32(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, U_32 value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->indexableStoreU32(vmThread, destObject, index, value, 0 != isVolatile);
}

IDATA
j9gc_objaccess_indexableReadI32(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->indexableReadI32(vmThread, srcObject, index, 0 != isVolatile);
}

void
j9gc_objaccess_indexableStoreI32(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, I_32 value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->indexableStoreI32(vmThread, destObject, index, value, 0 != isVolatile);
}

U_64
j9gc_objaccess_indexableReadU64(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->indexableReadU64(vmThread, srcObject, index, 0 != isVolatile);
}

void
j9gc_objaccess_indexableStoreU64(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, U_64 value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->indexableStoreU64(vmThread, destObject, index, value, 0 != isVolatile);
}

I_64
j9gc_objaccess_indexableReadI64(J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->indexableReadI64(vmThread, srcObject, index, 0 != isVolatile);
}

void
j9gc_objaccess_indexableStoreI64(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, I_64 value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->indexableStoreI64(vmThread, destObject, index, value, 0 != isVolatile);
}

void 
j9gc_objaccess_indexableStoreU64Split(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, U_32 valueSlot0, U_32 valueSlot1, UDATA isVolatile)
{
	U_64 value64;
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;

	/* This is endian-independent. valueSlot0 is always the low 4 bytes in memory */
	*(U_32 *)&value64 = valueSlot0;
	*(((U_32 *)&value64) + 1) = valueSlot1;

	barrier->indexableStoreU64(vmThread, destObject, index, value64, 0 != isVolatile);
}

J9Object *
j9gc_objaccess_staticReadObject(J9VMThread *vmThread, J9Class *clazz, J9Object **srcSlot, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->staticReadObject(vmThread, clazz, srcSlot, 0 != isVolatile);
}

void
j9gc_objaccess_staticStoreObject(J9VMThread *vmThread, J9Class *clazz, J9Object **destSlot, J9Object *value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->staticStoreObject(vmThread, clazz, destSlot, value, 0 != isVolatile);
}

void *
j9gc_objaccess_staticReadAddress(J9VMThread *vmThread, J9Class *clazz, void **srcSlot, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->staticReadAddress(vmThread, clazz, srcSlot, 0 != isVolatile);
}

void
j9gc_objaccess_staticStoreAddress(J9VMThread *vmThread, J9Class *clazz, void **destSlot, void *value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->staticStoreAddress(vmThread, clazz, destSlot, value, 0 != isVolatile);
}

UDATA
j9gc_objaccess_staticReadU32(J9VMThread *vmThread, J9Class *clazz, U_32 *srcSlot, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->staticReadU32(vmThread, clazz, srcSlot, 0 != isVolatile);
}

void
j9gc_objaccess_staticStoreU32(J9VMThread *vmThread, J9Class *clazz, U_32 *destSlot, U_32 value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->staticStoreU32(vmThread, clazz, destSlot, value, 0 != isVolatile);
}

IDATA
j9gc_objaccess_staticReadI32(J9VMThread *vmThread, J9Class *clazz, I_32 *srcSlot, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->staticReadI32(vmThread, clazz, srcSlot, 0 != isVolatile);
}

void
j9gc_objaccess_staticStoreI32(J9VMThread *vmThread, J9Class *clazz, I_32 *destSlot, I_32 value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->staticStoreI32(vmThread, clazz, destSlot, value, 0 != isVolatile);
}

U_64
j9gc_objaccess_staticReadU64(J9VMThread *vmThread, J9Class *clazz, U_64 *srcSlot, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->staticReadU64(vmThread, clazz, srcSlot, 0 != isVolatile);
}

void
j9gc_objaccess_staticStoreU64(J9VMThread *vmThread, J9Class *clazz, U_64 *destSlot, U_64 value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->staticStoreU64(vmThread, clazz, destSlot, value, 0 != isVolatile);
}

I_64
j9gc_objaccess_staticReadI64(J9VMThread *vmThread, J9Class *clazz, I_64 *srcSlot, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->staticReadI64(vmThread, clazz, srcSlot, 0 != isVolatile);
}

void
j9gc_objaccess_staticStoreI64(J9VMThread *vmThread, J9Class *clazz, I_64 *destSlot, I_64 value, UDATA isVolatile) {
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->staticStoreI64(vmThread, clazz, destSlot, value, 0 != isVolatile);
}

void 
j9gc_objaccess_staticStoreU64Split(J9VMThread *vmThread, J9Class *clazz, U_64 *destSlot, U_32 valueSlot0, U_32 valueSlot1, UDATA isVolatile)
{
	U_64 value64;
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;

	/* This is endian-independent. valueSlot0 is always the low 4 bytes in memory */
	*(U_32 *)&value64 = valueSlot0;
	*(((U_32 *)&value64) + 1) = valueSlot1;

	barrier->staticStoreU64(vmThread, clazz, destSlot, value64, 0 != isVolatile);
}


/* TODO: After all array accesses in the VM have been made arraylet safe, 
 * it should be possible to delete this method + its associated ENVY and 
 * ObjectAccessBarrier methods
 */
U_8 *
j9gc_objaccess_getArrayObjectDataAddress(J9VMThread *vmThread, J9IndexableObject *arrayObject)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->getArrayObjectDataAddress(vmThread, arrayObject);
}

j9objectmonitor_t *
j9gc_objaccess_getLockwordAddress(J9VMThread *vmThread, J9Object *object)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->getLockwordAddress(vmThread, object);
}

void
j9gc_objaccess_storeObjectToInternalVMSlot(J9VMThread *vmThread, J9Object** destSlot, J9Object *value) 
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->storeObjectToInternalVMSlot(vmThread, destSlot, value);
}

J9Object*
j9gc_objaccess_readObjectFromInternalVMSlot(J9VMThread *vmThread, J9JavaVM *vm, J9Object **srcSlot)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vm)->accessBarrier;
	return barrier->readObjectFromInternalVMSlot(vmThread, srcSlot);
}

/* TODO: VMDESIGN 1016 - fix this API to use fj9object_t* instead of J9Object** */
UDATA
j9gc_objaccess_compareAndSwapObject(J9VMThread *vmThread, J9Object *destObject, J9Object **destAddress, J9Object *compareObject, J9Object *swapObject)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;

	if (barrier->compareAndSwapObject(vmThread, destObject, (fj9object_t*)destAddress, compareObject, swapObject)) {
		return 1;
	}
	return 0;
}

UDATA
j9gc_objaccess_staticCompareAndSwapObject(J9VMThread *vmThread, J9Class *destClass, j9object_t *destAddress, J9Object *compareObject, J9Object *swapObject)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;

	if (barrier->staticCompareAndSwapObject(vmThread, destClass, destAddress, compareObject, swapObject)) {
		return 1;
	}
	return 0;
}

UDATA
j9gc_objaccess_mixedObjectCompareAndSwapInt(J9VMThread *vmThread, J9Object *destObject, UDATA offset, U_32 compareValue, U_32 swapValue)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	if (barrier->mixedObjectCompareAndSwapInt(vmThread, destObject, offset, compareValue, swapValue)) {
		return 1;
	}
	return 0;
}

UDATA
j9gc_objaccess_staticCompareAndSwapInt(J9VMThread *vmThread, J9Class *destClass, U_32 *destAddress, U_32 compareValue, U_32 swapValue)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	if (barrier->staticCompareAndSwapInt(vmThread, destClass, destAddress, compareValue, swapValue)) {
		return 1;
	}
	return 0;
}

UDATA
j9gc_objaccess_mixedObjectCompareAndSwapLong(J9VMThread *vmThread, J9Object *destObject, UDATA offset, U_64 compareValue, U_64 swapValue)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	if (barrier->mixedObjectCompareAndSwapLong(vmThread, destObject, offset, compareValue, swapValue)) {
		return 1;
	}
	return 0;
}

UDATA 
j9gc_objaccess_mixedObjectCompareAndSwapLongSplit(J9VMThread *vmThread, J9Object *destObject, UDATA offset, U_32 compareValueSlot0, U_32 compareValueSlot1, U_32 swapValueSlot0, U_32 swapValueSlot1)
{
	U_64 compareValue64 = 0;
	U_64 swapValue64 = 0;
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;

	/* This is endian-independent. valueSlot0 is always the low 4 bytes in memory */
	*(U_32 *)&compareValue64 = compareValueSlot0;
	*(((U_32 *)&compareValue64) + 1) = compareValueSlot1;
	*(U_32 *)&swapValue64 = swapValueSlot0;
	*(((U_32 *)&swapValue64) + 1) = swapValueSlot1;

	if (barrier->mixedObjectCompareAndSwapLong(vmThread, destObject, offset, compareValue64, swapValue64)) {
		return 1;
	}
	return 0;
}

UDATA
j9gc_objaccess_staticCompareAndSwapLong(J9VMThread *vmThread, J9Class *destClass, U_64 *destAddress, U_64 compareValue, U_64 swapValue)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	if (barrier->staticCompareAndSwapLong(vmThread, destClass, destAddress, compareValue, swapValue)) {
		return 1;
	}
	return 0;
}

UDATA 
j9gc_objaccess_staticCompareAndSwapLongSplit(J9VMThread *vmThread, J9Class *destClass, U_64 *destAddress, U_32 compareValueSlot0, U_32 compareValueSlot1, U_32 swapValueSlot0, U_32 swapValueSlot1)
{
	U_64 compareValue64 = 0;
	U_64 swapValue64 = 0;
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;

	/* This is endian-independent. valueSlot0 is always the low 4 bytes in memory */
	*(U_32 *)&compareValue64 = compareValueSlot0;
	*(((U_32 *)&compareValue64) + 1) = compareValueSlot1;
	*(U_32 *)&swapValue64 = swapValueSlot0;
	*(((U_32 *)&swapValue64) + 1) = swapValueSlot1;

	if (barrier->staticCompareAndSwapLong(vmThread, destClass, destAddress, compareValue64, swapValue64)) {
		return 1;
	}
	return 0;
}

j9object_t
j9gc_objaccess_compareAndExchangeObject(J9VMThread *vmThread, J9Object *destObject, J9Object **destAddress, J9Object *compareObject, J9Object *swapObject)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->compareAndExchangeObject(vmThread, destObject, (fj9object_t*)destAddress, compareObject, swapObject);
}

j9object_t
j9gc_objaccess_staticCompareAndExchangeObject(J9VMThread *vmThread, J9Class *destClass, j9object_t *destAddress, J9Object *compareObject, J9Object *swapObject)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->staticCompareAndExchangeObject(vmThread, destClass, destAddress, compareObject, swapObject);
}

U_32
j9gc_objaccess_mixedObjectCompareAndExchangeInt(J9VMThread *vmThread, J9Object *destObject, UDATA offset, U_32 compareValue, U_32 swapValue)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->mixedObjectCompareAndExchangeInt(vmThread, destObject, offset, compareValue, swapValue);
}

U_32
j9gc_objaccess_staticCompareAndExchangeInt(J9VMThread *vmThread, J9Class *destClass, U_32 *destAddress, U_32 compareValue, U_32 swapValue)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->staticCompareAndExchangeInt(vmThread, destClass, destAddress, compareValue, swapValue);
}

U_64
j9gc_objaccess_mixedObjectCompareAndExchangeLong(J9VMThread *vmThread, J9Object *destObject, UDATA offset, U_64 compareValue, U_64 swapValue)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->mixedObjectCompareAndExchangeLong(vmThread, destObject, offset, compareValue, swapValue);
}

U_64
j9gc_objaccess_staticCompareAndExchangeLong(J9VMThread *vmThread, J9Class *destClass, U_64 *destAddress, U_64 compareValue, U_64 swapValue)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->staticCompareAndExchangeLong(vmThread, destClass, destAddress, compareValue, swapValue);
}

/**
 * Fills array (or part of array) with specific object value.
 * Example, compressed pointers access barrier will mangle the value
 * pointer before filling up the array with it.
 */
void
j9gc_objaccess_fillArrayOfObjects(J9VMThread *vmThread, J9IndexableObject *destObject, I_32 destIndex, I_32 count, J9Object *value)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->fillArrayOfObjects(vmThread, destObject, destIndex, count, value);
}

/**
 * Returns the value used to shift a pointer when converting it between the compressed pointers heap and real heap
 * @return The shift amount.
 */
UDATA
j9gc_objaccess_compressedPointersShift(J9VMThread *vmThread)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->compressedPointersShift();
}

/**
 * Returns the shadow heap base for compressed pointers.
 * Used by the JIT in the interim solution until we store J9 class objects
 * on the heap.
 * @return shadow heap base.
 */
UDATA
j9gc_objaccess_compressedPointersShadowHeapBase(J9VMThread *vmThread)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->compressedPointersShadowHeapBase(vmThread);
}

/**
 * Returns the shadow heap top for compressed pointers.
 * Used by the JIT in the interim solution until we store J9 class objects
 * on the heap.
 * @return shadow heap top.
 */
UDATA
j9gc_objaccess_compressedPointersShadowHeapTop(J9VMThread *vmThread)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->compressedPointersShadowHeapTop(vmThread);
}

/**
 * Converts token (e.g. compressed pointer value) into real heap pointer.
 * @return The converted real help pointer.
 */
mm_j9object_t
j9gc_objaccess_pointerFromToken(J9VMThread *vmThread, fj9object_t token)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->convertPointerFromToken(token);
}

/**
 * Converts real heap pointer into token (e.g. compressed pointer value).
 * @return The converted token value (e.g. the compressed pointer value).
 */
fj9object_t
j9gc_objaccess_tokenFromPointer(J9VMThread *vmThread, mm_j9object_t object)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->convertTokenFromPointer(object);
}
#endif /* J9VM_GC_OBJECT_ACCESS_BARRIER */

/**
 * Called by certain specs to clone objects. See J9VMObjectAccessBarrier#cloneObject:intoObject:
 */
void
j9gc_objaccess_cloneObject(J9VMThread *vmThread, J9Object *srcObject, J9Object *destObject)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->cloneObject(vmThread, srcObject, destObject);
}

/**
 *
 */
BOOLEAN
j9gc_objaccess_structuralCompareFlattenedObjects(J9VMThread *vmThread, J9Class *valueClass, j9object_t lhsObject, j9object_t rhsObject, UDATA startOffset)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->structuralCompareFlattenedObjects(vmThread, valueClass, lhsObject, rhsObject, startOffset);
}

/**
 * Called by certain specs to copy objects
 */
void
j9gc_objaccess_copyObjectFields(J9VMThread *vmThread, J9Class *valueClass, J9Object *srcObject, UDATA srcOffset, J9Object *destObject, UDATA destOffset)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->copyObjectFields(vmThread, valueClass, srcObject, srcOffset, destObject, destOffset);
}

/**
 * Called by Interpreter during aastore of a flattend array
 */
void
j9gc_objaccess_copyObjectFieldsToFlattenedArrayElement(J9VMThread *vmThread, J9ArrayClass *arrayClazz, j9object_t srcObject, J9IndexableObject *arrayRef, I_32 index)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->copyObjectFieldsToFlattenedArrayElement(vmThread, arrayClazz, srcObject, arrayRef, index);
}

/**
 * Called by Interpreter during aaload of a flattend array
 */
void
j9gc_objaccess_copyObjectFieldsFromFlattenedArrayElement(J9VMThread *vmThread, J9ArrayClass *arrayClazz, j9object_t destObject, J9IndexableObject *arrayRef, I_32 index)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->copyObjectFieldsFromFlattenedArrayElement(vmThread, arrayClazz, destObject, arrayRef, index);
}

/**
 * Called by certain specs to clone objects. See J9VMObjectAccessBarrier#cloneArray:into:sizeInElements:class:
 */
void
j9gc_objaccess_cloneIndexableObject(J9VMThread *vmThread, J9IndexableObject *srcObject, J9IndexableObject *destObject)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->cloneIndexableObject(vmThread, srcObject, destObject);
}

/**
 * Helper function to clone an object to tenure if required so that it can be saved in the constantpool.
 */
j9object_t
j9gc_objaccess_asConstantPoolObject(J9VMThread *vmThread, j9object_t toConvert, UDATA allocationFlags)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->asConstantPoolObject(vmThread, toConvert, allocationFlags);
}

/**
 * This is only called by jitted code -- all other write barriers go through
 * the full substituting barrier (mixedObjectStoreObject()/indexableStoreObject()/
 * staticStoreObject().
 * @note This function does not work in barrier check VMs
 */
void
J9WriteBarrierPre(J9VMThread *vmThread, J9Object *dstObject, fj9object_t *dstAddress, J9Object *srcObject)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread->javaVM)->accessBarrier;
	barrier->preObjectStore(vmThread, dstObject, dstAddress, srcObject);
}

void
J9WriteBarrierPreClass(J9VMThread *vmThread, J9Object *dstClassObject, J9Object **dstAddress, J9Object *srcObject)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread->javaVM)->accessBarrier;
	barrier->preObjectStore(vmThread, dstClassObject, dstAddress, srcObject);
}

void
J9WriteBarrierPost(J9VMThread *vmThread, J9Object *dstObject, J9Object *srcObject)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread->javaVM)->accessBarrier;
	barrier->postObjectStore(vmThread, dstObject, (fj9object_t*)0, srcObject);
}

void
J9WriteBarrierPostClass(J9VMThread *vmThread, J9Class *dstClazz, J9Object *srcObject)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread->javaVM)->accessBarrier;
	barrier->postObjectStore(vmThread, dstClazz, (J9Object**)0, srcObject);
}

void
J9WriteBarrierBatch(J9VMThread *vmThread, J9Object *dstObject)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread->javaVM)->accessBarrier;
	/* In Metronome, write barriers are always pre-store */
	barrier->preBatchObjectStore(vmThread, dstObject);	
}

void
J9WriteBarrierClassBatch(J9VMThread *vmThread, J9Class *dstClazz)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread->javaVM)->accessBarrier;
	/* In Metronome, write barriers are always pre-store */
	barrier->preBatchObjectStore(vmThread, dstClazz);	
}

void
J9ReadBarrier(J9VMThread *vmThread, fj9object_t *srcAddress)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread->javaVM)->accessBarrier;
	barrier->preObjectRead(vmThread, NULL, srcAddress);
}

void
J9ReadBarrierClass(J9VMThread *vmThread, j9object_t *srcAddress)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread->javaVM)->accessBarrier;
	barrier->preObjectRead(vmThread, NULL, srcAddress);
}

j9object_t
j9gc_weakRoot_readObject(J9VMThread *vmThread, j9object_t *srcAddress)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread->javaVM)->accessBarrier;
	barrier->preWeakRootSlotRead(vmThread, srcAddress);
	return *srcAddress;
}

j9object_t
j9gc_weakRoot_readObjectVM(J9JavaVM *vm, j9object_t *srcAddress)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vm)->accessBarrier;
	barrier->preWeakRootSlotRead(vm, srcAddress);
	return *srcAddress;
}


void
j9gc_objaccess_recentlyAllocatedObject(J9VMThread *vmThread, J9Object *dstObject) 
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->recentlyAllocatedObject(vmThread, dstObject);
}

void
j9gc_objaccess_postStoreClassToClassLoader(J9VMThread *vmThread, J9ClassLoader* destClassLoader, J9Class* srcClass)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->postStoreClassToClassLoader(vmThread, destClassLoader, srcClass);
}

I_32
j9gc_objaccess_getObjectHashCode(J9JavaVM *vm, J9Object* object)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vm)->accessBarrier;
	return barrier->getObjectHashCode(vm, object);
}

void*
j9gc_objaccess_jniGetPrimitiveArrayCritical(J9VMThread* vmThread, jarray array, jboolean *isCopy)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread->javaVM)->accessBarrier;
	return barrier->jniGetPrimitiveArrayCritical(vmThread, array, isCopy);
}

void
j9gc_objaccess_jniReleasePrimitiveArrayCritical(J9VMThread* vmThread, jarray array, void * elems, jint mode)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread->javaVM)->accessBarrier;
	barrier->jniReleasePrimitiveArrayCritical(vmThread, array, elems, mode);
}

const jchar*
j9gc_objaccess_jniGetStringCritical(J9VMThread* vmThread, jstring str, jboolean *isCopy)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread->javaVM)->accessBarrier;
	return barrier->jniGetStringCritical(vmThread, str, isCopy);
}

void
j9gc_objaccess_jniReleaseStringCritical(J9VMThread* vmThread, jstring str, const jchar* elems)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread->javaVM)->accessBarrier;
	return barrier->jniReleaseStringCritical(vmThread, str, elems);
}

UDATA
j9gc_objaccess_checkClassLive(J9JavaVM *javaVM, J9Class *classPtr)
{
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(javaVM)->accessBarrier;
	return (barrier->checkClassLive(javaVM, classPtr)) ? 1 : 0;
#else /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */
	return 1;
#endif /* defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING) */
}

J9Object*
j9gc_objaccess_referenceGet(J9VMThread *vmThread, j9object_t refObject)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	return barrier->referenceGet(vmThread, refObject);
}

void
j9gc_objaccess_referenceReprocess(J9VMThread *vmThread, j9object_t refObject)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread)->accessBarrier;
	barrier->referenceReprocess(vmThread, refObject);
}

BOOLEAN
checkStringConstantsLive(J9JavaVM *javaVM, j9object_t stringOne, j9object_t stringTwo)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(javaVM)->accessBarrier;
	return barrier->checkStringConstantsLive(javaVM, stringOne, stringTwo);
}

BOOLEAN
checkStringConstantLive(J9JavaVM *javaVM, j9object_t string)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(javaVM)->accessBarrier;
	return barrier->checkStringConstantLive(javaVM, string);
}

void
j9gc_objaccess_jniDeleteGlobalReference(J9VMThread *vmThread, J9Object *reference)
{
	MM_ObjectAccessBarrier *barrier = MM_GCExtensions::getExtensions(vmThread->javaVM)->accessBarrier;
	barrier->jniDeleteGlobalReference(vmThread, reference);
}

} /* extern "C" */

