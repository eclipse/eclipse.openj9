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
#ifndef SHCDATATYPES_H_
#define SHCDATATYPES_H_

/* @ddr_namespace: default */
#include "j9.h"
#include "j9comp.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 
 * re: Out-of-process support
 * The macros in this header have not all been converted to support out-of-process yet.
 * When adding debug extensions, you may need to convert additional macros.
 * Use the J9SHR_READMEM() helper below.
 */
#define J9SHR_READMEM(x_) (x_)

/**
 * CACHE ENTRIES
 *
 * Each entry in the cache is an ShcItem followed by data ending in an ShcItemHdr
 * It's backwards because the items are allocated backwards.
 *
 * *------------------------------------------*-----*---------------*
 * |                |                         |     |               |
 * |   ShcItem      |         Data            | Pad |  ShcItemHdr   |
 * |                |                         |     |               |
 * |                |                         |     |               |
 * *------------------------------------------*-----*---------------*
 * <-------------------- itemHdr->itemLen -------------------------->
 * <-------------------- item->dataLen ------------->
 *
 * itemHdr->itemLen is used to walk the cache backwards
 * item->dataLen can be used to determine the size of the record or walk the cache forwards
 */
#define TYPE_UNINITIALIZED 0
#define TYPE_ROMCLASS 1
#define TYPE_CLASSPATH 2
#define TYPE_ORPHAN 3
#define TYPE_COMPILED_METHOD 4
#define TYPE_SCOPE 5
#define TYPE_SCOPED_ROMCLASS 6
#define TYPE_BYTE_DATA 7
#define TYPE_UNINDEXED_BYTE_DATA 8
/* TYPE_CHAR_ARRAY is obsolete, reuse its value as TYPE_INVALIDATED_COMPILED_METHOD */
#define TYPE_INVALIDATED_COMPILED_METHOD 9
#define TYPE_CACHELET 10
#define TYPE_ATTACHED_DATA 11
#define TYPE_PREREQ_CACHE 12
/* This macro should have same value as the last valid macro defining a type of cache data */
#define MAX_DATA_TYPES 12

typedef struct ShcItemHdr {
	U_32 itemLen; 		/* lower bit set means item is stale */
} ShcItemHdr;

/* Macros for dealing with ItemHdrs */

/* ->itemLen should never be set or read without these macros as the lower bit is used to indicate staleness */
#define CCITEMLEN(ih) (J9SHR_READMEM((ih)->itemLen) & 0xFFFFFFFE)
#define CCITEMSTALE(ih) (J9SHR_READMEM((ih)->itemLen) & 0x1)
#define CCSETITEMLEN(ih, len) ((ih)->itemLen = (len & 0x1) ? len+1 : len)
#define CCSETITEMSTALE(ih) ((ih)->itemLen |= 0x1)
#define CCITEM(ih) (((U_8*)(ih)) - (CCITEMLEN(ih) - sizeof(ShcItemHdr)))
#define CCITEMNEXT(ih) ((ShcItemHdr*)(CCITEM(ih) - sizeof(ShcItemHdr)))

/* Macros for dealing with Items */

typedef struct ShcItem {
	U_32 dataLen; /* length of data including ShcItem and padding */
	U_16 dataType;	/* type of data held by the ShcItem */
	U_16 jvmID;
} ShcItem;

#define ITEMDATA(i) (((U_8*)(i)) + sizeof(ShcItem))
#define ITEMEND(i) (((U_8*)(i)) + J9SHR_READMEM((i)->dataLen))
#define ITEMJVMID(i) (J9SHR_READMEM((i)->jvmID))
#define ITEMTYPE(i) (J9SHR_READMEM((i)->dataType))
#define ITEMDATALEN(i) (J9SHR_READMEM((i)->dataLen) - sizeof(ShcItem))

typedef struct J9SharedClassMetadataWalkState {
	void* metaStart;
	ShcItemHdr* entry;
	UDATA length;
	UDATA includeStale;
	U_16 limitDataType;
	void *savedMetaStart;
	ShcItemHdr *savedEntry;
} J9SharedClassMetadataWalkState;

/* Macros to deal with padding */

#define SHC_WORDALIGN 4
#define SHC_DOUBLEALIGN 8
#define SHC_PAD(bytes, align) (((bytes) % align == 0) ? (bytes) : ((bytes) + align - ((bytes) % align)))
#define SHC_ROMCLASSPAD(bytes) SHC_PAD(bytes, SHC_DOUBLEALIGN)
#define SHC_FINAL_ROMCLASS_U32(rc) (*((U_32*)(((U_8*)rc) + J9SHR_READMEM((rc)->romSize)) - 1))

/* Data wrappers and macros for cache records */

typedef struct ClasspathWrapper {
	I_16 staleFromIndex;
	U_32 classpathItemSize;	/* ClasspathItem follows directly after */
} ClasspathWrapper;

#define CPW_NOT_STALE 0x7FFF

#define CPWDATA(cpw) ((U_8*)(cpw) + sizeof(ClasspathWrapper))
#define CPWLEN(cpw) (sizeof(ClasspathWrapper) + J9SHR_READMEM((cpw)->ClasspathItemSize))

typedef struct J9ShrOffset {
#if defined(J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE)
	U_32 cacheLayer;
#endif /* defined(J9VM_OPT_MULTI_LAYER_SHARED_CLASS_CACHE) */
	/* This struct is introduced in cache generation 41.
	 * For cache generation 41: offset is U_32. It is always the offset from _theca.
	 * For cache generation >=43: It is offset from CASEGSTART if is it >=0. Otherwise, it is offset from CAEND 
	 */
	I_32 offset;
} J9ShrOffset;

typedef struct ROMClassWrapper {
	J9ShrOffset theCpOffset;
	I_16 cpeIndex;
	J9ShrOffset romClassOffset;	/* ROMClass stored in different part of cache */
	I_64 timestamp;
} ROMClassWrapper;

typedef struct ScopedROMClassWrapper {
	J9ShrOffset theCpOffset;
	I_16 cpeIndex;
	J9ShrOffset romClassOffset;	/* ROMClass stored in different part of cache */
	I_64 timestamp;
	J9ShrOffset modContextOffset;
	J9ShrOffset partitionOffset;
} ScopedROMClassWrapper;

typedef struct OrphanWrapper {
	J9ShrOffset romClassOffset; /* ROMClass stored in different part of cache */
} OrphanWrapper;

typedef struct CompiledMethodWrapper {
	J9ShrOffset romMethodOffset;	/* ROMMethod stored in different part of cache */
	U_32 dataLength;
	U_32 codeLength;
} CompiledMethodWrapper;

#define CMWDATA(cmw) (((U_8*)(cmw)) + sizeof(CompiledMethodWrapper))
#define CMWCODE(cmw) (((U_8*)(cmw)) + sizeof(CompiledMethodWrapper) + J9SHR_READMEM((cmw)->dataLength))
#define CMWITEM(cmw) (((U_8*)(cmw)) - sizeof(ShcItem))

typedef struct CharArrayWrapper {
	J9ShrOffset romStringOffset;
	U_32 objectSize;
} CharArrayWrapper;

#define CAWDATA(caw) (((U_8*)(caw)) + sizeof(CharArrayWrapper))
#define CAWITEM(caw) (((U_8*)(caw)) - sizeof(ShcItem))

typedef struct ByteDataWrapper {
	U_32 dataLength;
	J9ShrOffset tokenOffset;
	J9ShrOffset externalBlockOffset;
	U_8 dataType;
	U_8 inPrivateUse;
	U_16 privateOwnerID;	
} ByteDataWrapper;

#define BDWITEM(bdw) (((U_8*)(bdw)) - sizeof(ShcItem))
#define BDWLEN(bdw) J9SHR_READMEM((bdw)->dataLength)
#define BDWTYPE(bdw)  J9SHR_READMEM((bdw)->dataType)
#define BDWINPRIVATEUSE(bdw) J9SHR_READMEM((bdw)->inPrivateUse)
#define BDWPRIVATEOWNERID(bdw) J9SHR_READMEM((bdw)->privateOwnerID)

typedef struct CacheletHints {
	UDATA length; /* in bytes */
	UDATA dataType;
	U_8* data;
} CacheletHints;

typedef struct CacheletWrapper {
	J9SRP dataStart;
	UDATA dataLength;
	UDATA numHints;
	UDATA numSegments; /* number of segments in this cachelet */
	UDATA segmentStartOffset; /* offset from cachelet start to first segment */
	UDATA lastSegmentAlloc; /* last segment may not be full */
} CacheletWrapper;

#define CLETHINTS(clet) (((U_8*)(clet)) + sizeof(CacheletWrapper))
#define CLETDATA(clet) (((U_8*)(clet)) + J9SHR_READMEM((clet)->dataStart))

typedef struct AttachedDataWrapper {
	J9ShrOffset cacheOffset;
	U_32 dataLength;
	U_16 type;
	U_16 updateCount;
	I_32 corrupt;
} AttachedDataWrapper;

#define ADWLEN(adw) J9SHR_READMEM((adw)->dataLength)
#define ADWTYPE(adw) J9SHR_READMEM((adw)->type)
#define ADWCORRUPT(adw) J9SHR_READMEM((adw)->corrupt)
#define ADWUPDATECOUNT(adw) J9SHR_READMEM((adw)->updateCount)
#define ADWDATA(adw) (((U_8*)(adw)) + sizeof(AttachedDataWrapper))
#define ADWITEM(adw) (((U_8*)(adw)) - sizeof(ShcItem))

#ifdef __cplusplus
}
#endif

#endif /*SHCDATATYPES_H_*/
