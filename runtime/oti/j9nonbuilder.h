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

/* This file contains J9-specific things which have been moved from builder.
 * DO NOT include it directly - it is automatically included at the end of j9generated.h
 * (which also should not be directly included).  Include j9.h to get this file.
 */

#ifndef J9NONBUILDER_H
#define J9NONBUILDER_H

/* @ddr_namespace: map_to_type=J9NonbuilderConstants */

#include "j9vmconstantpool.h"
#include "j9consts.h"
#include "jvmti.h"
#include "j9javaaccessflags.h"

#define J9VM_MAX_HIDDEN_FIELDS_PER_CLASS 8

#define J9VM_DLT_HISTORY_SIZE  16
#define J9VM_OBJECT_MONITOR_CACHE_SIZE  32
#define J9VM_ASYNC_MAX_HANDLERS 32

/* The bit fields used by verifyQualifiedName to verify a qualified class name */
#define CLASSNAME_INVALID			0
#define CLASSNAME_VALID_NON_ARRARY	0x1
#define CLASSNAME_VALID_ARRARY		0x2
#define CLASSNAME_VALID				(CLASSNAME_VALID_NON_ARRARY | CLASSNAME_VALID_ARRARY)

/* @ddr_namespace: map_to_type=J9JITDataCacheConstants */

/* Constants from J9JITDataCacheConstants */
#define J9DataTypeAotMethodHeader 0x80
#define J9DataTypeAOTPersistentInfo 0x400
#define J9DataTypeExceptionInfo 0x1
#define J9DataTypeHashTable 0x20
#define J9DataTypeInUse 0x200
#define J9DataTypeMccHtEntry 0x40
#define J9DataTypeRelocationData 0x4
#define J9DataTypeStackAtlas 0x2
#define J9DataTypeThunkMappingData 0x10
#define J9DataTypeThunkMappingList 0x8
#define J9DataTypeUnallocated 0x100

/* @ddr_namespace: map_to_type=J9JavaClassFlags */

/* Constants from J9JavaClassFlags */
#define J9ClassDoNotAttemptToSetInitCache 0x1
#define J9ClassHasIllegalFinalFieldModifications 0x2
#define J9ClassReusedStatics 0x4
#define J9ClassContainsJittedMethods 0x8
#define J9ClassContainsMethodsPresentInMCCHash 0x10
#define J9ClassGCScanned 0x20
#define J9ClassIsAnonymous 0x40
#define J9ClassIsFlattened 0x80
#define J9ClassHasWatchedFields 0x100
#define J9ClassReservableLockWordInit 0x200
#define J9ClassIsValueType 0x400
#define J9ClassLargestAlignmentConstraintReference 0x800
#define J9ClassLargestAlignmentConstraintDouble 0x1000
#define J9ClassIsExemptFromValidation 0x2000
#define J9ClassContainsUnflattenedFlattenables 0x4000
#define J9ClassCanSupportFastSubstitutability 0x8000
#define J9ClassHasReferences 0x10000
#define J9ClassRequiresPrePadding 0x20000
#define J9ClassIsValueBased 0x40000
#define J9ClassHasIdentity 0x80000

/* @ddr_namespace: map_to_type=J9FieldFlags */

/* Constants from J9FieldFlags */
#define J9FieldFlagConstant 0x400000
#define J9FieldFlagHasFieldAnnotations 0x20000000
#define J9FieldFlagHasGenericSignature 0x40000000
#define J9FieldFlagHasTypeAnnotations 0x800000
#define J9FieldFlagIsContended 0x10000000
#define J9FieldFlagObject 0x20000
#define J9FieldFlagFlattened 0x1000000
#define J9FieldFlagUnused_2000000 0x2000000
#define J9FieldFlagUnused_4000000 0x4000000
#define J9FieldFlagPutResolved 0x8000000
#define J9FieldFlagResolved 0x80000000
#define J9FieldFlagsBaseTypeShift 0x3
#define J9FieldSizeDouble 0x40000
#define J9FieldTypeBoolean 0x80000
#define J9FieldTypeByte 0x200000
#define J9FieldTypeChar 0x0
#define J9FieldTypeDouble 0x180000
#define J9FieldTypeFloat 0x100000
#define J9FieldTypeInt 0x300000
#define J9FieldTypeLong 0x380000
#define J9FieldTypeMask 0x380000
#define J9FieldTypeShort 0x280000

/* @ddr_namespace: map_to_type=J9RecordComponentFlags */

/* Constants from J9RecordComponentFlags */
#define J9RecordComponentFlagHasGenericSignature 0x1
#define J9RecordComponentFlagHasAnnotations 0x2
#define J9RecordComponentFlagHasTypeAnnotations 0x4

/* @ddr_namespace: map_to_type=J9ArrayShapeFlags */

/* Constants from J9ArrayShapeFlags */
#define J9ArraySizeBytes 0x0
#define J9ArraySizeDoubles 0x3
#define J9ArraySizeLongs 0x2
#define J9ArraySizeWords 0x1

/* @ddr_namespace: map_to_type=J9ClassInitFlags */

/* Constants from J9ClassInitFlags */
#define J9ClassInitNotInitialized 0x0
#define J9ClassInitSucceeded 0x1
#define J9ClassInitFailed 0x2
#define J9ClassInitUnverified 0x3
#define J9ClassInitUnprepared 0x4
#define J9ClassInitStatusMask 0xFF

/* @ddr_namespace: map_to_type=J9DescriptionBits */

/* Constants from J9DescriptionBits */
#define J9DescriptionCpTypeClass 0x2
#define J9DescriptionCpTypeConstantDynamic 0x5
#define J9DescriptionCpTypeMask 0xF
#define J9DescriptionCpTypeMethodHandle 0x4
#define J9DescriptionCpTypeMethodType 0x3
#define J9DescriptionCpTypeObject 0x1
#define J9DescriptionCpTypeScalar 0x0
#define J9DescriptionCpTypeShift 0x4
#define J9DescriptionImmediate 0x1

/* Rom CP Flag for constant dynamic with primitive type */
#define J9DescriptionReturnTypeBoolean 0x1
#define J9DescriptionReturnTypeByte 0x2
#define J9DescriptionReturnTypeChar 0x3
#define J9DescriptionReturnTypeShort 0x4
#define J9DescriptionReturnTypeFloat 0x5
#define J9DescriptionReturnTypeInt 0x6
#define J9DescriptionReturnTypeDouble 0x7
#define J9DescriptionReturnTypeLong 0x8
/* ROM CP have 32 bit slots, currently the least significant 20 bits are in use
 * so the Constant Dynamic return types will be shifted left to use the free bits
 */
#define J9DescriptionReturnTypeShift 20
#define J9DescriptionCpBsmIndexMask 0xFFFF

/* @ddr_namespace: map_to_type=J9NativeTypeCodes */

/* Constants from J9NativeTypeCodes */
#define J9NtcBoolean 0x1
#define J9NtcByte 0x2
#define J9NtcChar 0x3
#define J9NtcClass 0xA
#define J9NtcDouble 0x7
#define J9NtcFloat 0x5
#define J9NtcInt 0x6
#define J9NtcLong 0x8
#define J9NtcObject 0x9
#define J9NtcPointer 0xB
#define J9NtcShort 0x4
#define J9NtcVoid 0x0

/* @ddr_namespace: map_to_type=J9VMRuntimeFlags */

/* Constants from J9VMRuntimeFlags */
#define J9RuntimeFlagAggressive 0x100000
#define J9RuntimeFlagAggressiveVerification 0x1000000
#define J9RuntimeFlagAlwaysCopyJNICritical 0x10
#define J9RuntimeFlagAOTStripped 0x800
#define J9RuntimeFlagArgencodingLatin 0x4000
#define J9RuntimeFlagArgencodingUnicode 0x2000
#define J9RuntimeFlagArgencodingUtf8 0x8000
#define J9RuntimeFlagUnused0x80 0x80 /* unused flag */
#define J9RuntimeFlagCleanUp 0x40
#define J9RuntimeFlagJavaBaseModuleCreated 0x8
#define J9RuntimeFlagDFPBD 0x4000000
#define J9RuntimeFlagDisableVMShutdown 0x8000000
#define J9RuntimeFlagExitStarted 0x800000
#define J9RuntimeFlagExtendedMethodBlock 0x200000
#define J9RuntimeFlagInitialized 0x4
#define J9RuntimeFlagJITActive 0x20
#define J9RuntimeFlagNoPriorities 0x2000000
#define J9RuntimeFlagOmitStackTraces 0x20000000
#define J9RuntimeFlagPaintStack 0x400
#define J9RuntimeFlagPPC32On64 0x80000000
#define J9RuntimeFlagReportStackUse 0x1
#define J9RuntimeFlagUnused0x10000000 0x10000000
#define J9RuntimeFlagShowVersion 0x40000000
#define J9RuntimeFlagShutdown 0x100
#define J9RuntimeFlagShutdownStarted 0x400000
#define J9RuntimeFlagSniffAndWhack 0x1000
#define J9RuntimeFlagVerify 0x2
#define J9RuntimeFlagXfuture 0x200

/* @ddr_namespace: map_to_type=J9RemoteDbgInfoConstants */

/* Constants from J9RemoteDbgInfoConstants */
#define RDBGID_JXE 0x2
#define RDBGID_ROM 0x1
#define RDBGInfo_Ok 0x0
#define RDBGInfoErr_NoInfo 0x1
#define RDBGInfoFlag_ClassInfo 0x2
#define RDBGInfoFlag_Locked 0x8
#define RDBGInfoFlag_MethodInfo 0x1
#define RDBGInfoFlag_SourceDebugExtension 0x4
#define RDBGRequestType_ClassInfoVM 0x2
#define RDBGRequestType_MethodInfoVM 0x3
#define RDBGRequestType_SourceDebugExtension 0x6
#define RDBGRequestType_SymbolFile 0x1
#define RDBGRequestType_TargetName 0x4
#define RDBGRequestType_TranslateClass 0x5

/* @ddr_namespace: map_to_type=J9NonbuilderConstants */

#define J9_ROMCLASS_OPTINFO_SOURCE_FILE_NAME 0x1
#define J9_ROMCLASS_OPTINFO_GENERIC_SIGNATURE 0x2
#define J9_ROMCLASS_OPTINFO_SOURCE_DEBUG_EXTENSION 0x4
#define J9_ROMCLASS_OPTINFO_TYPE_TABLE 0x20
#define J9_ROMCLASS_OPTINFO_ENCLOSING_METHOD 0x40
#define J9_ROMCLASS_OPTINFO_SIMPLE_NAME 0x80
#define J9_ROMCLASS_OPTINFO_LOCAL 0x1000
#define J9_ROMCLASS_OPTINFO_REMOTE 0x2000
#define J9_ROMCLASS_OPTINFO_VERIFY_EXCLUDE 0x4000
#define J9_ROMCLASS_OPTINFO_CLASS_ANNOTATION_INFO 0x8000
#define J9_ROMCLASS_OPTINFO_VARIABLE_TABLE_HAS_GENERIC 0x10000
#define J9_ROMCLASS_OPTINFO_UNUSED_20000 0x20000
#define J9_ROMCLASS_OPTINFO_UNUSED_40000 0x40000
#define J9_ROMCLASS_OPTINFO_UNUSED_80000 0x80000
#define J9_ROMCLASS_OPTINFO_UNUSED_100000 0x100000
#define J9_ROMCLASS_OPTINFO_UNUSED 0x200000
#define J9_ROMCLASS_OPTINFO_TYPE_ANNOTATION_INFO 0x400000
#define J9_ROMCLASS_OPTINFO_RECORD_ATTRIBUTE 0x800000
#define J9_ROMCLASS_OPTINFO_PERMITTEDSUBCLASSES_ATTRIBUTE 0x1000000
#define J9_ROMCLASS_OPTINFO_INJECTED_INTERFACE_INFO 0x2000000

/* Constants for checkVisibility return results */
#define J9_VISIBILITY_ALLOWED 1
#define J9_VISIBILITY_NON_MODULE_ACCESS_ERROR 0
#define J9_VISIBILITY_MODULE_READ_ACCESS_ERROR -1
#define J9_VISIBILITY_MODULE_PACKAGE_EXPORT_ERROR -2
#if JAVA_SPEC_VERSION >= 11
#define J9_VISIBILITY_NEST_HOST_LOADING_FAILURE_ERROR -3
#define J9_VISIBILITY_NEST_HOST_DIFFERENT_PACKAGE_ERROR -4
#define J9_VISIBILITY_NEST_MEMBER_NOT_CLAIMED_ERROR -5
#endif /* JAVA_SPEC_VERSION >= 11 */

#define J9_CATCHTYPE_VALUE_FOR_SYNTHETIC_HANDLER_4BYTES 0xFFFFFFFF
#define J9_CATCHTYPE_VALUE_FOR_SYNTHETIC_HANDLER_2BYTES 0xFFFF

typedef enum {
	J9FlushCompQueueDataBreakpoint
} J9JITFlushCompilationQueueReason;

/* Forward struct declarations. */
struct DgRasInterface;
struct J9BytecodeVerificationData;
struct J9CfrClassFile;
struct J9Class;
struct J9ClassLoader;
struct J9ConstantPool;
struct J9HashTable;
struct J9HookedNative;
struct J9JavaVM;
struct J9JVMTIClassPair;
struct J9JXEDescription;
struct J9MemorySegment;
struct J9MemorySegmentList;
struct J9Method;
struct J9MM_HeapRootSlotDescriptor;
struct J9MM_IterateHeapDescriptor;
struct J9MM_IterateObjectDescriptor;
struct J9MM_IterateObjectRefDescriptor;
struct J9MM_IterateRegionDescriptor;
struct J9MM_IterateSpaceDescriptor;
struct J9ObjectMonitorInfo;
struct J9Pool;
struct J9PortLibrary;
struct J9RASdumpAgent;
struct J9RASdumpContext;
struct J9RASdumpFunctions;
struct J9ROMClass;
struct J9ROMMethod;
struct J9SharedInternSRPHashTableEntry;
struct J9Statistic;
struct J9UTF8;
struct J9VerboseSettings;
struct J9VMInterface;
struct J9VMThread;
struct JNINativeInterface_;
struct OMR_VM;
struct VMIZipFile;
struct TR_AOTHeader;
struct J9BranchTargetStack;

/* @ddr_namespace: map_to_type=J9CfrError */

/* Jazz 82615: Both errorPC (current pc value) and errorFrameBCI (bci value in the stack map frame)
 * are required in detailed error message when error occurs during static verification.
 * Note: we use verboseErrorType in the error message framework to store the specific error type
 * in the case of errorCode = J9NLS_BCV_ERR_INCONSISTENT_STACK__ID.
 */
typedef struct J9CfrError {
	U_16 errorCode;
	U_16 errorAction;
	U_32 errorCatalog;
	U_32 errorOffset;
	I_32 errorMethod;
	U_32 errorPC;
	U_32 errorFrameBCI;
	I_32 errorFrameIndex;
	U_32 errorDataIndex;  /* Used to store the invalid index value when the verification error occurs */
	I_32 verboseErrorType;
	I_32 errorBsmIndex;
	U_32 errorBsmArgsIndex;
	U_32 errorCPType;
	struct J9CfrMethod* errorMember;
	struct J9CfrConstantPoolInfo* constantPool;
} J9CfrError;

#define CFR_ThrowNoClassDefFoundError  J9VMCONSTANTPOOL_JAVALANGNOCLASSDEFFOUNDERROR
#define CFR_ThrowOutOfMemoryError  J9VMCONSTANTPOOL_JAVALANGOUTOFMEMORYERROR
#define CFR_NoAction  0
#define CFR_ThrowClassCircularityError  J9VMCONSTANTPOOL_JAVALANGCLASSCIRCULARITYERROR
#define CFR_ThrowUnsupportedClassVersionError  J9VMCONSTANTPOOL_JAVALANGUNSUPPORTEDCLASSVERSIONERROR
#define CFR_ThrowVerifyError  J9VMCONSTANTPOOL_JAVALANGVERIFYERROR
#define CFR_ThrowClassFormatError  J9VMCONSTANTPOOL_JAVALANGCLASSFORMATERROR

typedef struct J9ROMClassCfrError {
	U_16 errorCode;
	U_16 errorAction;
	U_32 errorCatalog;
	U_32 errorOffset;
	I_32 errorMethod;
	U_32 errorPC;
	J9SRP errorMember;
	J9SRP constantPool;
} J9ROMClassCfrError;

#define J9ROMCLASSCFRERROR_ERRORMEMBER(base) SRP_GET((base)->errorMember, struct J9ROMClassCfrMember*)
#define J9ROMCLASSCFRERROR_CONSTANTPOOL(base) SRP_GET((base)->constantPool, struct J9ROMClassCfrConstantPoolInfo*)

typedef struct J9ROMClassCfrMember {
	U_16 accessFlags;
	U_16 nameIndex;
	U_16 descriptorIndex;
	U_16 attributesCount;
} J9ROMClassCfrMember;

/* @ddr_namespace: map_to_type=J9ContendedLoadTableEntry */

typedef struct J9ContendedLoadTableEntry {
	U_8* className; /* these three fields are used as search keys */
	UDATA classNameLength;
	struct J9ClassLoader* classLoader;
	UDATA hashValue; /* need this to get the hash value when the className pointer is null */

	IDATA count; /* number of threads trying to load the class */
	UDATA status;
	struct J9VMThread *thread; /* to detect class circularity */
} J9ContendedLoadTableEntry;

#define CLASSLOADING_DUMMY 0
#define	CLASSLOADING_LOAD_IN_PROGRESS 1
#define	CLASSLOADING_LOAD_SUCCEEDED 2
#define	CLASSLOADING_LOAD_FAILED 3
#define	CLASSLOADING_DONT_CARE 4

typedef struct J9ROMClassCfrConstantPoolInfo {
	U_8 tag;
	U_8 flags1;
	U_16 nextCPIndex;
	U_32 slot1;
	U_32 slot2;
	J9SRP bytes;
} J9ROMClassCfrConstantPoolInfo;

#define J9ROMCLASSCFRCONSTANTPOOLINFO_BYTES(base) SRP_GET((base)->bytes, U_8*)

typedef struct J9JSRIAddressMap {
	struct J9JSRICodeBlock** entries;
	U_8* reachable;
	U_16* lineNumbers;
} J9JSRIAddressMap;

typedef struct J9JSRICodeBlock {
	U_32 originalPC;
	U_32 length;
	U_32 newPC;
	U_32 coloured;
	struct J9JSRICodeBlock* primaryChild;
	struct J9JSRICodeBlock* secondaryChild;
	struct J9JSRICodeBlock* nextBlock;
	struct J9JSRIJSRData* jsrData;
} J9JSRICodeBlock;

/* Jazz 82615: we use verboseErrorType in the error message framework to store the specific error type
 * in the case of errorCode = J9NLS_BCV_ERR_INCONSISTENT_STACK__ID.
 */
typedef struct J9JSRIData {
	struct J9PortLibrary * portLib;
	struct J9CfrAttributeCode* codeAttribute;
	U_8* freePointer;
	U_8* segmentEnd;
	U_8* sourceBuffer;
	UDATA sourceBufferSize;
	U_8* destBuffer;
	UDATA destBufferSize;
	UDATA destBufferIndex;
	struct J9JSRIAddressMap* map;
	UDATA mapSize;
	UDATA maxStack;
	UDATA maxLocals;
	struct J9CfrConstantPoolInfo* constantPool;
	struct J9JSRIExceptionListEntry* originalExceptionTable;
	struct J9JSRIExceptionListEntry* exceptionTable;
	struct J9JSRICodeBlock* firstOutput;
	struct J9JSRICodeBlock* lastOutput;
	struct J9Pool* codeBlockPool;
	struct J9Pool* jsrDataPool;
	struct J9Pool* exceptionListEntryPool;
	UDATA wideBranchesNeeded;
	struct J9JSRIBranch* branchStack;
	UDATA flags;
	IDATA errorCode;
	IDATA verifyError;
	UDATA verifyErrorPC;
	UDATA exceptionTableBufferSize;
	struct J9CfrExceptionTableEntry* exceptionTableBuffer;
	U_32 bytesAddedByJSRInliner;
	I_32 verboseErrorType;
	U_32 errorLocalIndex;
} J9JSRIData;

typedef struct J9JSRIExceptionListEntry {
	struct J9JSRIExceptionListEntry* nextInList;
	struct J9CfrExceptionTableEntry* tableEntry;
	struct J9JSRICodeBlock* handlerBlock;
	struct J9JSRIJSRData* jsrData;
	U_32 startPC;
	U_32 endPC;
	U_32 handlerPC;
	U_16 catchType;
} J9JSRIExceptionListEntry;

typedef struct J9JSRIJSRData {
	struct J9JSRICodeBlock* parentBlock;
	struct J9JSRIJSRData* previousJSR;
	U_8* stack;
	U_8* stackBottom;
	U_8* locals;
	U_32* retPCPtr;
	U_32 spawnPC;
	U_32 retPC;
	U_32 originalPC;
} J9JSRIJSRData;

typedef struct J9JSRIBranch {
	UDATA startPC;
	struct J9JSRIJSRData* jsrData;
	struct J9JSRICodeBlock** blockParentPointer;
} J9JSRIBranch;

typedef struct J9JITBreakpointedMethod {
	struct J9Method* method;
	UDATA count;
	struct J9JITBreakpointedMethod* link;
	UDATA hasBeenTranslated;
} J9JITBreakpointedMethod;

typedef struct J9JITFrame {
	void* returnPC;
} J9JITFrame;

/* @ddr_namespace: map_to_type=J9VTuneInterface */

typedef struct J9VTuneInterface {
	UDATA dllHandle;
	void* iJIT_NotifyEvent;
	void* iJIT_RegisterCallback;
	void* iJIT_RegisterCallbackEx;
	void* Initialize;
	void* NotifyEvent;
	void* MethodEntry;
	void* MethodExit;
	UDATA flags;
} J9VTuneInterface;

#define J9VTUNE_TRACE_LINE_NUMBERS  1

/* @ddr_namespace: map_to_type=J9JITExceptionTable */

/* This structure is exposed by the compile_info field of CompiledMethodLoad() / JVMTI_EVENT_COMPILED_METHOD_LOAD.
 * New variables should be added at the end in order to maintain backward compatibility.
 */
typedef struct J9JITExceptionTable {
#if defined(J9VM_RAS_EYECATCHERS)
	struct J9UTF8* className;
	struct J9UTF8* methodName;
	struct J9UTF8* methodSignature;
#endif /* J9VM_RAS_EYECATCHERS */
	struct J9ConstantPool* constantPool;
	struct J9Method* ramMethod;
	UDATA startPC;
	UDATA endWarmPC;
	UDATA startColdPC;
	UDATA endPC;
	UDATA totalFrameSize;
	I_16 slots;
	I_16 scalarTempSlots;
	I_16 objectTempSlots;
	U_16 prologuePushes;
	I_16 tempOffset;
	U_16 numExcptionRanges;
	I_32 size;
	UDATA flags;
	UDATA registerSaveDescription;
	void* gcStackAtlas;
	void* inlinedCalls;
	void* bodyInfo;
	struct J9JITExceptionTable* nextMethod;
	struct J9JITExceptionTable* prevMethod;
	void* debugSlot1;
	void* debugSlot2;
	void* osrInfo;
	void* runtimeAssumptionList;
	I_32 hotness;
	UDATA codeCacheAlloc;
	void* gpuCode;
	void* riData;
} J9JITExceptionTable;

#define JIT_METADATA_FLAGS_USED_FOR_SIZE 0x1
#define JIT_METADATA_GC_MAP_32_BIT_OFFSETS 0x2
#define JIT_METADATA_IS_STUB 0x4
#define JIT_METADATA_NOT_INITIALIZED 0x8
#define JIT_METADATA_IS_REMOTE_COMP 0x10

typedef struct J9JIT16BitExceptionTableEntry {
	U_16 startPC;
	U_16 endPC;
	U_16 handlerPC;
	U_16 catchType;
} J9JIT16BitExceptionTableEntry;

typedef struct J9JIT32BitExceptionTableEntry {
	U_32 startPC;
	U_32 endPC;
	U_32 handlerPC;
	U_32 catchType;
	struct J9Method* ramMethod;
} J9JIT32BitExceptionTableEntry;

typedef struct J9JITStackAtlas {
	U_8* stackAllocMap;
	U_8* internalPointerMap;
	U_16 numberOfMaps;
	U_16 numberOfMapBytes;
	I_16 parmBaseOffset;
	U_16 numberOfParmSlots;
	I_16 localBaseOffset;
	U_16 paddingTo32;
} J9JITStackAtlas;

typedef struct J9JITDataCacheHeader {
	U_32 size;
	U_32 type;
} J9JITDataCacheHeader;

typedef struct J9ThunkMapping {
	void* constantPool;
	U_32 cpIndex;
	void* thunkAddress;
} J9ThunkMapping;

typedef struct J9JITCodeCacheTrampolineList {
	struct J9MemorySegment* codeCache;
	void* heapBase;
	void* heapTop;
	U_32* allocPtr;
	void* targetMap;
	void* unresolvedTargetMap;
	struct J9JITCodeCacheTrampolineList* next;
	U_32 numUnresolvedTargets;
	U_32 referenceCount;
} J9JITCodeCacheTrampolineList;

typedef struct J9JITMethodEquivalence {
	struct J9Method* oldMethod;
	struct J9Method* newMethod;
	UDATA equivalent;
} J9JITMethodEquivalence;

typedef struct J9JITRedefinedClass {
	struct J9Class* oldClass;
	struct J9Class* newClass;
	UDATA methodCount;
	struct J9JITMethodEquivalence* methodList;
} J9JITRedefinedClass;

typedef struct J9HotField {
	struct J9HotField* next;
	uint32_t hotness;
	uint16_t cpuUtil;
	uint8_t hotFieldOffset; 
} J9HotField;

typedef struct J9ClassHotFieldsInfo {
	struct J9HotField* hotFieldListHead;
	struct J9ClassLoader* classLoader;
	BOOLEAN isClassHotFieldListDirty;	
	uint8_t hotFieldOffset1;
	uint8_t hotFieldOffset2;
	uint8_t hotFieldOffset3;
	uint8_t consecutiveHotFieldSelections; 
	uint8_t hotFieldListLength;  
 } J9ClassHotFieldsInfo;

typedef struct J9ROMNameAndSignature {
	J9SRP name;
	J9SRP signature;
} J9ROMNameAndSignature;

#define J9ROMNAMEANDSIGNATURE_NAME(base) NNSRP_GET((base)->name, struct J9UTF8*)
#define J9ROMNAMEANDSIGNATURE_SIGNATURE(base) NNSRP_GET((base)->signature, struct J9UTF8*)

typedef struct J9ROMFieldShape {
	struct J9ROMNameAndSignature nameAndSignature;
	U_32 modifiers;
} J9ROMFieldShape;

#define J9ROMFIELDSHAPE_NAME(base) NNSRP_GET((&((base)->nameAndSignature))->name, struct J9UTF8*)
#define J9ROMFIELDSHAPE_SIGNATURE(base) NNSRP_GET((&((base)->nameAndSignature))->signature, struct J9UTF8*)

typedef struct J9ROMStaticFieldShape {
	struct J9ROMNameAndSignature nameAndSignature;
	U_32 modifiers;
	U_32 initialValue;
} J9ROMStaticFieldShape;

#define J9ROMSTATICFIELDSHAPE_NAME(base) NNSRP_GET((&((base)->nameAndSignature))->name, struct J9UTF8*)
#define J9ROMSTATICFIELDSHAPE_SIGNATURE(base) NNSRP_GET((&((base)->nameAndSignature))->signature, struct J9UTF8*)

typedef struct J9ROMRecordComponentShape {
	struct J9ROMNameAndSignature nameAndSignature;
	U_32 attributeFlags;
} J9ROMRecordComponentShape;

#define J9ROMRECORDCOMPONENTSHAPE_NAME(base) NNSRP_GET((&((base)->nameAndSignature))->name, struct J9UTF8*)
#define J9ROMRECORDCOMPONENTSHAPE_SIGNATURE(base) NNSRP_GET((&((base)->nameAndSignature))->signature, struct J9UTF8*)

/* @ddr_namespace: map_to_type=J9VMExt */

#define J9MEMORYPOOLDATA_MAX_NAME_BUFFER_SIZE   32
#define J9GARBAGECOLLECTORDATA_MAX_NAME_BUFFER_SIZE   32
#define J9VM_MAX_HEAP_MEMORYPOOL_COUNT 4
#define J9VM_MAX_NONHEAP_MEMORYPOOL_COUNT 4

typedef struct J9GarbageCollectionInfo {
	U_32 gcID;
	U_32 arraySize;
	const char *gcAction;
	const char *gcCause;
	U_64 index;
	U_64 startTime;
	U_64 endTime;
	U_64 initialSize[J9VM_MAX_HEAP_MEMORYPOOL_COUNT + J9VM_MAX_NONHEAP_MEMORYPOOL_COUNT];
	U_64 preUsed[J9VM_MAX_HEAP_MEMORYPOOL_COUNT + J9VM_MAX_NONHEAP_MEMORYPOOL_COUNT];
	U_64 preCommitted[J9VM_MAX_HEAP_MEMORYPOOL_COUNT + J9VM_MAX_NONHEAP_MEMORYPOOL_COUNT];
	I_64 preMax[J9VM_MAX_HEAP_MEMORYPOOL_COUNT + J9VM_MAX_NONHEAP_MEMORYPOOL_COUNT];
	U_64 postUsed[J9VM_MAX_HEAP_MEMORYPOOL_COUNT + J9VM_MAX_NONHEAP_MEMORYPOOL_COUNT];
	U_64 postCommitted[J9VM_MAX_HEAP_MEMORYPOOL_COUNT + J9VM_MAX_NONHEAP_MEMORYPOOL_COUNT];
	I_64 postMax[J9VM_MAX_HEAP_MEMORYPOOL_COUNT + J9VM_MAX_NONHEAP_MEMORYPOOL_COUNT];
} J9GarbageCollectionInfo;

typedef struct J9GarbageCollectorData {
	U_32 id;
	char name[J9GARBAGECOLLECTORDATA_MAX_NAME_BUFFER_SIZE];
	U_64 totalGCTime;
	U_64 memoryUsed;
	I_64 totalMemoryFreed;
	U_64 totalCompacts;
	J9GarbageCollectionInfo lastGcInfo;
} J9GarbageCollectorData;

typedef struct J9MemoryPoolData {
	U_32 id;
	char name[J9MEMORYPOOLDATA_MAX_NAME_BUFFER_SIZE];
	U_64 initialSize;
	U_64 preCollectionMaxSize;
	U_64 postCollectionMaxSize;
	U_64 preCollectionSize;
	U_64 preCollectionUsed;
	U_64 postCollectionSize;
	U_64 postCollectionUsed;
	U_64 peakSize;
	U_64 peakUsed;
	U_64 peakMax;
	U_64 usageThreshold;
	U_64 usageThresholdCrossedCount;
	U_64 collectionUsageThreshold;
	U_64 collectionUsageThresholdCrossedCount;
	U_32 notificationState;
} J9MemoryPoolData;

typedef struct J9NonHeapMemoryData {
	U_32 id;
	char name[J9MEMORYPOOLDATA_MAX_NAME_BUFFER_SIZE];
	U_64 initialSize;
	U_64 preCollectionSize;
	U_64 preCollectionUsed;
	U_64 postCollectionSize;
	U_64 postCollectionUsed;
	U_64 peakSize;
	U_64 peakUsed;
	U_64 maxSize;
}J9NonHeapMemoryData;

typedef struct J9JavaLangManagementData {
	I_64 vmStartTime;
	U_64 totalClassLoads;
	U_64 totalClassUnloads;
	U_64 totalCompilationTime;
	I_64 lastCompilationStart;
	omrthread_rwmutex_t managementDataLock;
	UDATA threadsCompiling;
	U_64 totalJavaThreadsStarted;
	U_32 liveJavaThreads;
	U_32 liveJavaDaemonThreads;
	U_32 peakLiveJavaThreads;
	U_32 threadContentionMonitoringFlag;
	U_32 supportedMemoryPools;
	U_32 supportedNonHeapMemoryPools;
	U_32 supportedCollectors;
	U_32 lastGCID;
	struct J9MemoryPoolData *memoryPools;
	struct J9NonHeapMemoryData *nonHeapMemoryPools;
	struct J9GarbageCollectorData *garbageCollectors;
	U_64 preCollectionHeapSize;
	U_64 preCollectionHeapUsed;
	U_64 postCollectionHeapSize;
	U_64 postCollectionHeapUsed;
	omrthread_monitor_t notificationMonitor;
	void *notificationQueue;
	U_32 notificationsPending;
	U_64 notificationCount;
	U_32 notificationEnabled;
	UDATA initialHeapSize;
	UDATA maximumHeapSize;
	omrthread_monitor_t dlparNotificationMonitor;
	void *dlparNotificationQueue;
	U_32 dlparNotificationsPending;
	U_32 dlparNotificationCount;
	U_32 currentNumberOfCPUs;
	U_32 currentProcessingCapacity;
	U_64 currentTotalPhysicalMemory;
	U_32 threadCpuTimeEnabledFlag;
	U_32 isThreadCpuTimeSupported;
	U_32 isCurrentThreadCpuTimeSupported;
	U_64 gcMainCpuTime;
	U_64 gcWorkerCpuTime;
	U_32 gcMaxThreads;
	U_32 gcCurrentThreads;
	char counterPath[2048];
	U_32 isCounterPathInitialized;
} J9JavaLangManagementData;

typedef struct J9LoadROMClassData {
	struct J9Class* classBeingRedefined;
	U_8* className;
	UDATA classNameLength;
	U_8* classData;
	UDATA classDataLength;
	j9object_t classDataObject;
	struct J9ClassLoader* classLoader;
	j9object_t protectionDomain;
	U_8* hostPackageName;
	UDATA hostPackageLength;
	UDATA options;
	struct J9ROMClass* romClass;
	struct J9MemorySegment* romClassSegment;
	void* freeUserData;
	classDataFreeFunction freeFunction;
} J9LoadROMClassData;

typedef struct J9VMSystemProperty {
	char* name;
	char* value;
	UDATA flags;
} J9VMSystemProperty;

#if defined(J9VM_INTERP_CUSTOM_SPIN_OPTIONS)
typedef struct J9ObjectMonitorCustomSpinOptions {
	UDATA thrMaxSpins1BeforeBlocking;
	UDATA thrMaxSpins2BeforeBlocking;
	UDATA thrMaxYieldsBeforeBlocking;
	UDATA thrMaxTryEnterSpins1BeforeBlocking;
	UDATA thrMaxTryEnterSpins2BeforeBlocking;
	UDATA thrMaxTryEnterYieldsBeforeBlocking;
} J9ObjectMonitorCustomSpinOptions;

/* @ddr_namespace: map_to_type=J9VMCustomSpinOptions */

typedef struct J9VMCustomSpinOptions {
	const char* className;
	J9ObjectMonitorCustomSpinOptions j9monitorOptions;
#if defined(OMR_THR_CUSTOM_SPIN_OPTIONS)
	J9ThreadCustomSpinOptions j9threadOptions;
#endif /* OMR_THR_CUSTOM_SPIN_OPTIONS */
} J9VMCustomSpinOptions;
#endif /* J9VM_INTERP_CUSTOM_SPIN_OPTIONS */

#define J9SYSPROP_FLAG_NAME_ALLOCATED  1
#define J9SYSPROP_FLAG_VALUE_ALLOCATED  2
#define J9SYSPROP_FLAG_WRITEABLE  4

#define J9SYSPROP_ERROR_NONE  0
#define J9SYSPROP_ERROR_NOT_FOUND  1
#define J9SYSPROP_ERROR_READ_ONLY  2
#define J9SYSPROP_ERROR_OUT_OF_MEMORY  3
#define J9SYSPROP_ERROR_INVALID_JCL  4
#define J9SYSPROP_ERROR_UNSUPPORTED_PROP 5
#define J9SYSPROP_ERROR_ARG_MISSING 6

typedef struct J9VMDllLoadInfo {
	char dllName[32];
	char alternateDllName[32];
	U_32 loadFlags;
	U_32 completedBits;
	UDATA descriptor;
	IDATA  ( *j9vmdllmain)(struct J9JavaVM *vm, IDATA stage, void *reserved) ;
	char* fatalErrorStr;
	void* reserved;
} J9VMDllLoadInfo;

typedef struct J9SigContext {
	IDATA sigNum;
	void* sigInfo;
	void* uContext;
	struct J9VMThread* onThread;
} J9SigContext;

typedef struct J9JniCheckLocalRefState {
	UDATA numLocalRefs;
	UDATA topFrameCapacity;
	UDATA framesPushed;
	UDATA globalRefCapacity;
	UDATA weakRefCapacity;
} J9JniCheckLocalRefState;

typedef struct J9VMInitArgs {
	JavaVMInitArgs* actualVMArgs;
	struct J9CmdLineOption* j9Options;
	UDATA nOptions;
} J9VMInitArgs;

/* @ddr_namespace: map_to_type=J9IdentityHashData */

typedef struct J9IdentityHashData {
	UDATA hashData1;
	UDATA hashData2;
	UDATA hashData3;
	UDATA hashData4;
	UDATA hashSaltPolicy;
	U_32 hashSaltTable[1];
} J9IdentityHashData;

#define J9_IDENTITY_HASH_SALT_POLICY_STANDARD  1
#define J9_IDENTITY_HASH_SALT_POLICY_REGION  2
#define J9_IDENTITY_HASH_SALT_POLICY_NONE  0

/**
 * Globals variables required by CUDA4J.
 */
typedef struct J9CudaGlobals {
	jclass exceptionClass;
	jmethodID exception_init;
	jmethodID runnable_run;
} J9CudaGlobals;

#if defined(J9VM_OPT_SHARED_CLASSES)

typedef enum J9SharedClassCacheMode {
	J9SharedClassCacheBootstrapOnly,
	J9SharedClassCacheBoostrapAndExtension,
	J9SharedClassCacheUserDefined
} J9SharedClassCacheMode;

typedef struct J9SharedClassTransaction {
	struct J9VMThread* ownerThread;
	struct J9ClassLoader* classloader;
	I_16 entryIndex;
	UDATA loadType;
	U_16 classnameLength;
	U_8* classnameData;
	UDATA transactionState;
	IDATA isOK;
	struct J9UTF8* partitionInCache;
	struct J9UTF8* modContextInCache;
	IDATA helperID;
	void* allocatedMem;
	U_32 allocatedLineNumberTableSize;
	U_32 allocatedLocalVariableTableSize;
	void* allocatedLineNumberTable;
	void* allocatedLocalVariableTable;
	void* ClasspathWrapper; /* points to ClasspathItem if it is full/soft full. Otherwise, points to a ClasspathWrapper in the cache */
	void* cacheAreaForAllocate;
	void* newItemInCache;
	j9object_t updatedItemSize;
	void* findNextRomClass;
	void* findNextIterator;
	void* firstFound;
	UDATA oldVMState;
	UDATA isModifiedClassfile;
	UDATA takeReadWriteLock;
	U_64 cacheFullFlags;
} J9SharedClassTransaction;

typedef struct J9SharedStringTransaction {
	struct J9VMThread* ownerThread;
	UDATA transactionState;
	IDATA isOK;
} J9SharedStringTransaction;

/* @ddr_namespace: map_to_type=J9GenericByID */

typedef struct J9GenericByID {
	U_8 magic;
	U_8 type;
	U_16 id;
	struct J9ClassPathEntry* jclData;
	void* cpData;
} J9GenericByID;

#define CP_TYPE_TOKEN  1
#define CP_TYPE_URL  2
#define CP_TYPE_CLASSPATH  4
#define CP_MAGIC  0xAA

typedef struct J9ClasspathByID {
	struct J9GenericByID header;
	UDATA entryCount;
	U_8* failedMatches;
} J9ClasspathByID;

typedef struct J9ClasspathByIDArray {
	struct J9ClasspathByID** array;
	UDATA size;
	char* partition;
	UDATA partitionHash;
	struct J9ClasspathByIDArray* next;
} J9ClasspathByIDArray;

typedef struct J9URLByID {
	struct J9GenericByID header;
} J9URLByID;

typedef struct J9TokenByID {
	struct J9GenericByID header;
	UDATA tokenHash;
} J9TokenByID;

typedef struct J9SharedInvariantInternTable {
	UDATA  ( *performNodeAction)(struct J9SharedInvariantInternTable *sharedInvariantInternTable, struct J9SharedInternSRPHashTableEntry *node, UDATA action, void* userData) ;
	UDATA flags;
	omrthread_monitor_t tableInternFxMutex;
	struct J9SRPHashTable* sharedInvariantSRPHashtable;
	struct J9SharedInternSRPHashTableEntry* headNode;
	struct J9SharedInternSRPHashTableEntry* tailNode;
	J9SRP* sharedTailNodePtr;
	J9SRP* sharedHeadNodePtr;
	U_32* totalSharedNodesPtr;
	U_32* totalSharedWeightPtr;
	struct J9ClassLoader* systemClassLoader;
} J9SharedInvariantInternTable;

/* @ddr_namespace: map_to_type=J9SharedInternSRPHashTableEntry */

typedef struct J9SharedInternSRPHashTableEntry {
	J9SRP utf8SRP;
	U_16 flags;
	U_16 internWeight;
	J9SRP prevNode;
	J9SRP nextNode;
} J9SharedInternSRPHashTableEntry;

#define STRINGINTERNTABLES_NODE_FLAG_UTF8_IS_SHARED  4
#define STRINGINTERNTABLES_ACTION_VERIFY_BOTH_TABLES  10
#define STRINGINTERNTABLES_ACTION_VERIFY_LOCAL_TABLE_ONLY  13

#define J9SHAREDINTERNSRPHASHTABLEENTRY_UTF8SRP(base) SRP_GET((base)->utf8SRP, struct J9UTF8*)
#define J9SHAREDINTERNSRPHASHTABLEENTRY_PREVNODE(base) SRP_GET((base)->prevNode, struct J9SharedInternSRPHashTableEntry*)
#define J9SHAREDINTERNSRPHASHTABLEENTRY_NEXTNODE(base) SRP_GET((base)->nextNode, struct J9SharedInternSRPHashTableEntry*)

/* @ddr_namespace: map_to_type=J9SharedDataDescriptor */

typedef struct J9SharedDataDescriptor {
	U_8* address;
	UDATA length;
	UDATA type;
	UDATA flags;
} J9SharedDataDescriptor;

#define J9SHRDATA_IS_PRIVATE  1
#define J9SHRDATA_ALLOCATE_ZEROD_MEMORY  2
#define J9SHRDATA_PRIVATE_TO_DIFFERENT_JVM  4
#define J9SHRDATA_USE_READWRITE  8
#define J9SHRDATA_NOT_INDEXED  16
#define J9SHRDATA_SINGLE_STORE_FOR_KEY_TYPE  32
#define J9SHRDATA_SINGLE_STORE_FOR_KEY_TYPE_OVERWRITE  64

typedef struct J9SharedStartupHintsDataDescriptor {
	UDATA flags;
	UDATA heapSize1;
	UDATA heapSize2;
	UDATA unused1;
	UDATA unused2;
	UDATA unused3;
	UDATA unused4;
	UDATA unused5;
	UDATA unused6;
	UDATA unused7;
	UDATA unused8;
	UDATA unused9;
} J9SharedStartupHintsDataDescriptor;

typedef struct J9SharedLocalStartupHints {
	U_64 localStartupHintFlags;
	struct J9SharedStartupHintsDataDescriptor hintsData;
} J9SharedLocalStartupHints;

typedef struct J9SharedClassJavacoreDataDescriptor {
	void* romClassStart;
	void* romClassEnd;
	void* metadataStart;
	void* cacheEndAddress;
	U_64 runtimeFlags;
	UDATA cacheGen;
	I_8 topLayer;
	UDATA cacheSize;
	UDATA freeBytes;
	UDATA percFull;
	const char* cacheName;
	U_32 feature;
	IDATA shmid;
	IDATA semid;
	const char* cacheDir;
	void* writeLockTID;
	void* readWriteLockTID;
	UDATA extraFlags;
	UDATA readWriteBytes;
	UDATA debugAreaSize;
	UDATA debugAreaUsed;
	UDATA debugAreaLineNumberTableBytes;
	UDATA debugAreaLocalVariableTableBytes;
	IDATA totalSize;
	IDATA minAOT;
	IDATA maxAOT;
	IDATA minJIT;
	IDATA maxJIT;
	IDATA corruptionCode;
	UDATA corruptValue;
	UDATA softMaxBytes;
	UDATA otherBytes;
	/* The fields above are stats for the top layer, and the fields below are the summary for all layers */
	UDATA ccCount;
	UDATA ccStartedCount;
	UDATA romClassBytes;
	UDATA aotBytes;
	UDATA jclDataBytes;
	UDATA zipCacheDataBytes;
	UDATA jitHintDataBytes;
	UDATA jitProfileDataBytes;
	UDATA aotDataBytes;
	UDATA aotClassChainDataBytes;
	UDATA aotThunkDataBytes;
	UDATA indexedDataBytes;
	UDATA unindexedDataBytes;
	UDATA numROMClasses;
	UDATA numStaleClasses;
	UDATA percStale;
	UDATA numAOTMethods;
	UDATA numClasspaths;
	UDATA numURLs;
	UDATA numTokens;
	UDATA numJclEntries;
	UDATA numZipCaches;
	UDATA numJitHints;
	UDATA numJitProfiles;
	UDATA numAotDataEntries;
	UDATA numAotClassChains;
	UDATA numAotThunks;
	UDATA objectBytes;
	UDATA numObjects;
	UDATA numStartupHints;
	UDATA startupHintBytes;
} J9SharedClassJavacoreDataDescriptor;

typedef struct J9SharedStringFarm {
	char* freePtr;
	UDATA bytesLeft;
	struct J9SharedStringFarm* next;
} J9SharedStringFarm;

typedef struct J9SharedCacheInfo {
	const char* name;
	UDATA isCompatible;
	UDATA cacheType;
	UDATA os_shmid;
	UDATA os_semid;
	UDATA modLevel;
	UDATA addrMode;
	UDATA isCorrupt;
	UDATA cacheSize;
	UDATA freeBytes;
	I_64 lastDetach;
	UDATA softMaxBytes;
	I_8 layer;
} J9SharedCacheInfo;

typedef struct J9SharedCacheHeader {
	U_32 totalBytes;
	U_32 readWriteBytes;
	UDATA updateSRP;
	UDATA readWriteSRP;
	UDATA segmentSRP;
	UDATA updateCount;
	J9WSRP updateCountPtr;
	volatile UDATA readerCount;
	UDATA unused2;
	UDATA writeHash;
	UDATA unused3;
	UDATA unused4;
	UDATA crashCntr;
	UDATA aotBytes;
	UDATA jitBytes;
	U_16 vmCntr;
	U_8 corruptFlag;
	U_8 roundedPagesFlag;
	I_32 minAOT;
	I_32 maxAOT;
	U_32 locked;
	J9WSRP lockedPtr;
	J9WSRP corruptFlagPtr;
	J9SRP sharedStringHead;
	J9SRP sharedStringTail;
	J9SRP unused1;
	U_32 totalSharedStringNodes;
	U_32 totalSharedStringWeight;
	U_32 readWriteFlags;
	UDATA readWriteCrashCntr;
	UDATA readWriteRebuildCntr;
	UDATA osPageSize;
	UDATA ccInitComplete;
	UDATA crcValid;
	UDATA crcValue;
	UDATA containsCachelets;
	UDATA cacheFullFlags;
	UDATA readWriteVerifyCntr;
	UDATA extraFlags;
	UDATA debugRegionSize;
	UDATA lineNumberTableNextSRP;
	UDATA localVariableTableNextSRP;
	I_32 minJIT;
	I_32 maxJIT;
	IDATA sharedInternTableBytes;
	IDATA corruptionCode;
	UDATA corruptValue;
	UDATA lastMetadataType;
	UDATA writerCount;
	UDATA unused5;
	UDATA unused6;
	U_32 softMaxBytes;
	UDATA unused8;
	UDATA unused9;
	UDATA unused10;
} J9SharedCacheHeader;

#define J9SHAREDCACHEHEADER_UPDATECOUNTPTR(base) WSRP_GET((base)->updateCountPtr, UDATA*)
#define J9SHAREDCACHEHEADER_LOCKEDPTR(base) WSRP_GET((base)->lockedPtr, U_32*)
#define J9SHAREDCACHEHEADER_CORRUPTFLAGPTR(base) WSRP_GET((base)->corruptFlagPtr, U_8*)
#define J9SHAREDCACHEHEADER_SHAREDSTRINGHEAD(base) SRP_GET((base)->sharedStringHead, struct J9SharedInternSRPHashTableEntry*)
#define J9SHAREDCACHEHEADER_SHAREDSTRINGTAIL(base) SRP_GET((base)->sharedStringTail, struct J9SharedInternSRPHashTableEntry*)
#define J9SHAREDCACHEHEADER_UNUSED01(base) SRP_GET((base)->unused01, void*)

/* Maximum length of the cache name (including NULL char) specified by user in the command line */
#define USER_SPECIFIED_CACHE_NAME_MAXLEN 65

typedef struct J9SharedClassCacheDescriptor {
	struct J9SharedCacheHeader* cacheStartAddress;
	void* romclassStartAddress;
	void* metadataStartAddress;
	struct J9MemorySegment* metadataMemorySegment;
	UDATA cacheSizeBytes;
	void* deployedROMClassStartAddress;
	struct J9SharedClassCacheDescriptor* next;
	struct J9SharedClassCacheDescriptor* previous;
} J9SharedClassCacheDescriptor;

typedef struct J9SharedCacheAPI {
	char* ctrlDirName;
	char* cacheName;
	char* modContext;
	char* expireTime;
	U_64 runtimeFlags;
	UDATA verboseFlags;
	UDATA cacheType;
	UDATA parseResult;
	UDATA storageKeyTesting;
	UDATA xShareClassesPresent;
	UDATA cacheDirPerm;
	IDATA  ( *iterateSharedCaches)(struct J9JavaVM *vm, const char *cacheDir, UDATA groupPerm, BOOLEAN useCommandLineValues, IDATA (*callback)(struct J9JavaVM *vm, struct J9SharedCacheInfo *event_data, void *user_data), void *user_data) ;
	IDATA  ( *destroySharedCache)(struct J9JavaVM *vm, const char *cacheDir, const char *name, U_32 cacheType, BOOLEAN useCommandLineValues) ;
	UDATA printStatsOptions;
	char* methodSpecs;
	U_32 softMaxBytes;
	I_32 minAOT;
	I_32 maxAOT;
	I_32 minJIT;
	I_32 maxJIT;
	U_8 sharedCacheEnabled;
	U_8 inContainer; /* It is TRUE only when xShareClassesPresent is FALSE and J9_SHARED_CACHE_DEFAULT_BOOT_SHARING(vm) is TRUE and the JVM is running in container */
	I_8 layer;
} J9SharedCacheAPI;

typedef struct J9SharedClassConfig {
	void* sharedClassCache;
	struct J9SharedClassCacheDescriptor* cacheDescriptorList;
	omrthread_monitor_t jclCacheMutex;
	struct J9Pool* jclClasspathCache;
	struct J9Pool* jclURLCache;
	struct J9Pool* jclTokenCache;
	struct J9HashTable* jclURLHashTable;
	struct J9HashTable* jclUTF8HashTable;
	struct J9Pool* jclJ9ClassPathEntryPool;
	struct J9SharedStringFarm* jclStringFarm;
	struct J9ClassPathEntry* lastBootstrapCPE;
	void* bootstrapCPI;
	U_64 runtimeFlags;
	UDATA verboseFlags;
	UDATA findClassCntr;
	omrthread_monitor_t configMonitor;
	UDATA configLockWord; /* The VM no longer uses this field, but the z/OS JIT doesn't build without it */
	const struct J9UTF8* modContext;
	void* sharedAPIObject;
	const char* ctrlDirName;
	UDATA  ( *getCacheSizeBytes)(struct J9JavaVM* vm) ;
	UDATA  ( *getTotalUsableCacheBytes)(struct J9JavaVM* vm);
	J9SharedClassCacheMode ( *getSharedClassCacheMode)(struct J9JavaVM* vm);
	void  ( *getMinMaxBytes)(struct J9JavaVM* vm, U_32 *softmx, I_32 *minAOT, I_32 *maxAOT, I_32 *minJIT, I_32 *maxJIT);
	I_32  ( *setMinMaxBytes)(struct J9JavaVM* vm, U_32 softmx, I_32 minAOT, I_32 maxAOT, I_32 minJIT, I_32 maxJIT);
	void (* increaseUnstoredBytes)(struct J9JavaVM *vm, U_32 aotBytes, U_32 jitBytes);
	void (* getUnstoredBytes)(struct J9JavaVM *vm, U_32 *softmxUnstoredBytes, U_32 *maxAOTUnstoredBytes, U_32 *maxJITUnstoredBytes);
	UDATA  ( *getFreeSpaceBytes)(struct J9JavaVM* vm) ;
	IDATA  ( *findSharedData)(struct J9VMThread* currentThread, const char* key, UDATA keylen, UDATA limitDataType, UDATA includePrivateData, struct J9SharedDataDescriptor* firstItem, const struct J9Pool* descriptorPool) ;
	const U_8*  ( *storeSharedData)(struct J9VMThread* vmThread, const char* key, UDATA keylen, const struct J9SharedDataDescriptor* data) ;
	UDATA  ( *storeAttachedData)(struct J9VMThread* vmThread, const void* addressInCache, const struct J9SharedDataDescriptor* data, UDATA forceReplace) ;
	const U_8*  ( *findAttachedData)(struct J9VMThread* vmThread, const void* addressInCache, struct J9SharedDataDescriptor* data, IDATA *dataIsCorrupt) ;
	UDATA  ( *updateAttachedData)(struct J9VMThread* vmThread, const void* addressInCache, I_32 updateAtOffset, const J9SharedDataDescriptor* data) ;
	UDATA  ( *updateAttachedUDATA)(struct J9VMThread* vmThread, const void* addressInCache, UDATA type, I_32 updateAtOffset, UDATA value) ;
	void  ( *freeAttachedDataDescriptor)(struct J9VMThread* vmThread, struct J9SharedDataDescriptor* data) ;
	const U_8*  ( *findCompiledMethodEx1)(struct J9VMThread* vmThread, const struct J9ROMMethod* romMethod, UDATA* flags);
	const U_8*  ( *storeCompiledMethod)(struct J9VMThread* vmThread, const struct J9ROMMethod* romMethod, const U_8* dataStart, UDATA dataSize, const U_8* codeStart, UDATA codeSize, UDATA forceReplace) ;
	UDATA  ( *existsCachedCodeForROMMethod)(struct J9VMThread* vmThread, const struct J9ROMMethod* romMethod) ;
	UDATA  ( *acquirePrivateSharedData)(struct J9VMThread* vmThread, const struct J9SharedDataDescriptor* data) ;
	UDATA  ( *releasePrivateSharedData)(struct J9VMThread* vmThread, const struct J9SharedDataDescriptor* data) ;
	UDATA  ( *getJavacoreData)(struct J9JavaVM *vm, struct J9SharedClassJavacoreDataDescriptor* descriptor) ;
	UDATA  ( *isBCIEnabled)(struct J9JavaVM *vm) ;
	void  ( *freeClasspathData)(struct J9JavaVM *vm, void *cpData) ;
	void  ( *jvmPhaseChange)(struct J9VMThread *currentThread, UDATA phase);
	void  (*storeGCHints)(struct J9VMThread* currentThread, UDATA heapSize1, UDATA heapSize2, BOOLEAN forceReplace);
	IDATA  (*findGCHints)(struct J9VMThread* currentThread, UDATA *heapSize1, UDATA *heapSize2);
	void  ( *updateClasspathOpenState)(struct J9JavaVM* vm, struct J9ClassPathEntry* classPathEntries, UDATA entryIndex, UDATA entryCount, BOOLEAN isOpen);
	struct J9MemorySegment* metadataMemorySegment;
	struct J9Pool* classnameFilterPool;
	U_32 softMaxBytes;
	I_32 minAOT;
	I_32 maxAOT;
	I_32 minJIT;
	I_32 maxJIT;
	struct J9SharedLocalStartupHints localStartupHints;
	const char* cacheName;
	I_8 layer;
	U_64 readOnlyCacheRuntimeFlags;
} J9SharedClassConfig;

typedef struct J9SharedClassPreinitConfig {
	UDATA sharedClassCacheSize;
	IDATA sharedClassInternTableNodeCount;
	IDATA sharedClassMinAOTSize;
	IDATA sharedClassMaxAOTSize;
	IDATA sharedClassMinJITSize;
	IDATA sharedClassMaxJITSize;
	IDATA sharedClassReadWriteBytes;
	IDATA sharedClassDebugAreaBytes;
	IDATA sharedClassSoftMaxBytes;
} J9SharedClassPreinitConfig;

typedef struct J9ShrCompositeCacheCommonInfo {
	omrthread_tls_key_t writeMutexEntryCount;
	struct J9VMThread* hasWriteMutexThread;
	struct J9VMThread* hasReadWriteMutexThread;
	struct J9VMThread* hasRefreshMutexThread;
	struct J9VMThread* hasRWMutexThreadMprotectAll;
	U_16 vmID;
	U_32 writeMutexID;
	U_32 readWriteAreaMutexID;
	UDATA cacheIsCorrupt;
	UDATA stringTableStarted;
	UDATA oldWriterCount;
} J9ShrCompositeCacheCommonInfo;

#endif /* J9VM_OPT_SHARED_CLASSES */

typedef struct J9RASSystemInfo {
	struct J9RASSystemInfo* linkPrevious;
	struct J9RASSystemInfo* linkNext;
	U_32 key;
	void* data;
} J9RASSystemInfo;

typedef struct J9RASCrashInfo {
	struct J9VMThread* failingThread;
	UDATA failingThreadID;
	char* gpInfo;
} J9RASCrashInfo;

/* @ddr_namespace: map_to_type=J9RAS */

typedef struct J9RAS {
	U_8 eyecatcher[8];
	U_32 bitpattern1;
	U_32 bitpattern2;
	I_32 version;
	I_32 length;
	void* ddrData;
	UDATA mainThreadOffset;
	UDATA omrthreadNextOffset;
	UDATA osthreadOffset;
	UDATA idOffset;
	UDATA typedefsLen;
	UDATA typedefs;
	UDATA env;
	UDATA vm;
	U_64 buildID;
	U_8 osversion[128];
	U_8 osarch[16];
	U_8 osname[48];
	U_32 cpus;
	void* environment;
	U_64 memory;
	struct J9RASCrashInfo* crashInfo;
	U_8 hostname[256];
	U_8 ipAddresses[256];
	struct J9Statistic** nextStatistic;
	UDATA pid;
	UDATA tid;
	char* serviceLevel;
	struct J9RASSystemInfo* systemInfo;
	I_64 startTimeMillis;
	I_64 startTimeNanos;
	I_64 dumpTimeMillis;
	I_64 dumpTimeNanos;
} J9RAS;

#define J9RASMajorVersion  5
#define J9RASMinorVersion  0
#define J9RASVersion ((J9RASMajorVersion << 16) + J9RASMinorVersion)
#define J9RASCompatible(target)  ((((target)->j9ras.ras.version & 0xffff0000) >> 16 == J9RASMajorVersion) && (((target)->j9ras.ras.version & 0xffff) >= J9RASMinorVersion))

#if defined(J9VM_OPT_VM_LOCAL_STORAGE)

typedef struct J9VMLSTable {
	UDATA initialized;
	UDATA head;
	UDATA freeKeys;
	UDATA keys[256];
} J9VMLSTable;

#endif /* J9VM_OPT_VM_LOCAL_STORAGE */

/* @ddr_namespace: map_to_type=J9Statistic */

typedef struct J9Statistic {
	U_64 dataSlot;
	struct J9Statistic* nextStatistic;
	U_8 dataType;
	U_8 name[1];
} J9Statistic;

#define J9STAT_I8  3
#define J9STAT_I32  7
#define J9STAT_I64  9
#define J9STAT_I16  5
#define J9STAT_IDATA  11
#define J9STAT_UDATA  10
#define J9STAT_U8  2
#define J9STAT_U64  8
#define J9STAT_U16  4
#define J9STAT_FLOAT  13
#define J9STAT_U32  6
#define J9STAT_POINTER  1
#define J9STAT_STRING  12
#define J9STAT_DOUBLE  14

/* @ddr_namespace: map_to_type=J9PackageIDTableEntry */

typedef struct J9PackageIDTableEntry {
	UDATA taggedROMClass;
} J9PackageIDTableEntry;

#define J9PACKAGE_ID_TAG 1
#define J9PACKAGE_ID_GENERATED 2

typedef struct J9JNINameAndSignature {
	const char* name;
	const char* signature;
	U_32 nameLength;
	U_32 signatureLength;
} J9JNINameAndSignature;

typedef struct J9MonitorEnterRecord {
	j9object_t object;
	UDATA* arg0EA;
	UDATA dropEnterCount;
	struct J9MonitorEnterRecord* next;
} J9MonitorEnterRecord;

typedef struct J9ExceptionInfo {
	U_16 catchCount;
	U_16 throwCount;
} J9ExceptionInfo;

typedef struct J9ExceptionHandler {
	U_32 startPC;
	U_32 endPC;
	U_32 handlerPC;
	U_32 exceptionClassIndex;
} J9ExceptionHandler;

#if defined(__xlC__) || defined(J9ZOS390)  /* Covers: Z/OS, AIX, Linux PPC*/
#pragma pack(1)
#elif defined(__ibmxl__) || defined(__GNUC__) || defined(_MSC_VER) /* Covers: Linux PPC LE, Windows, Linux x86 */
#pragma pack(push, 1)
/* Above covers all the platform for J9 VM compile jobs. DDR is compiled with EDG compiler, there following is for DDR compile */
#elif defined(LINUXPPC) || defined(AIXPPC)
#pragma pack(1)
#elif defined(LINUX) || defined(WIN32)
#pragma pack(push, 1)
#else
#error "Unrecognized compiler. Cannot pack struct J9MethodParameter and J9MethodParametersData."
#endif
typedef struct J9MethodParameter {
	J9SRP name;
	U_16 flags;
} J9MethodParameter;

typedef struct J9MethodParametersData {
	U_8 parameterCount;
	J9MethodParameter parameters;
} J9MethodParametersData;

#if defined(__xlC__) || defined(__ibmxl__) || defined(__GNUC__) || defined(_MSC_VER) || defined(J9ZOS390) || defined(LINUX) || defined(AIXPPC) || defined(WIN32)
#pragma pack(pop)
#else
#error "Unrecognized compiler. Cannot pack struct J9MethodParameter and J9MethodParametersData."
#endif

typedef struct J9EnclosingObject {
	U_32 classRefCPIndex;
	J9SRP nameAndSignature;
} J9EnclosingObject;

#define J9ENCLOSINGOBJECT_NAMEANDSIGNATURE(base) SRP_GET((base)->nameAndSignature, struct J9ROMNameAndSignature*)

typedef struct J9MonitorTableListEntry {
	struct J9HashTable* monitorTable;
	struct J9MonitorTableListEntry* next;
} J9MonitorTableListEntry;

typedef struct J9UnsafeMemoryBlock {
	struct J9UnsafeMemoryBlock* linkNext;
	struct J9UnsafeMemoryBlock* linkPrevious;
	U_64 data;
} J9UnsafeMemoryBlock;

typedef struct J9ObjectMonitor {
	omrthread_monitor_t monitor;
#if defined(J9VM_THR_SMART_DEFLATION)
	UDATA proDeflationCount;
	UDATA antiDeflationCount;
#endif /* J9VM_THR_SMART_DEFLATION */
	j9objectmonitor_t alternateLockword;
	U_32 hash;
} J9ObjectMonitor;

typedef struct J9ClassWalkState {
	struct J9JavaVM* vm;
	struct J9MemorySegment* nextSegment;
	U_8* heapPtr;
	struct J9ClassLoader* classLoader;
} J9ClassWalkState;

typedef struct J9DbgStringInternTable {
	struct J9JavaVM* vm;
	struct J9PortLibrary * portLibrary;
	struct J9HashTable* internHashTable;
	struct J9InternHashTableEntry* headNode;
	struct J9InternHashTableEntry* tailNode;
	UDATA nodeCount;
	UDATA maximumNodeCount;
} J9DbgStringInternTable;

typedef struct J9DbgROMClassBuilder {
	struct J9JavaVM* javaVM;
	struct J9PortLibrary * portLibrary;
	U_8* verifyExcludeAttribute;
	void* verifyClassFunction;
	UDATA classFileParserBufferSize;
	UDATA bufferManagerBufferSize;
	U_8* classFileBuffer;
	U_8* anonClassNameBuffer;
	UDATA anonClassNameBufferSize;
	U_8* bufferManagerBuffer;
	struct J9DbgStringInternTable stringInternTable;
} J9DbgROMClassBuilder;

typedef struct J9ROMFieldWalkState {
	struct J9ROMFieldShape* field;
	UDATA fieldsLeft;
} J9ROMFieldWalkState;

typedef struct J9ROMFieldOffsetWalkResult {
	struct J9ROMFieldShape* field;
	UDATA offset;
	UDATA totalInstanceSize;
	UDATA superTotalInstanceSize;
	UDATA index;
	IDATA backfillOffset;
#ifdef J9VM_OPT_VALHALLA_VALUE_TYPES
	struct J9Class* flattenedClass;
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
} J9ROMFieldOffsetWalkResult;

typedef struct J9HiddenInstanceField {
	struct J9UTF8* className;
	struct J9ROMFieldShape* shape;
	UDATA fieldOffset;
	UDATA* offsetReturnPtr;
	struct J9HiddenInstanceField* next;
} J9HiddenInstanceField;

/* @ddr_namespace: map_to_type=J9ROMFieldOffsetWalkState */

typedef struct J9ROMFieldOffsetWalkState {
	struct J9JavaVM *vm;
	struct J9ROMFieldWalkState fieldWalkState;
	struct J9ROMFieldOffsetWalkResult result;
	struct J9ROMClass* romClass;
	UDATA firstSingleOffset;
	UDATA firstObjectOffset;
	UDATA firstDoubleOffset;
	IDATA backfillOffsetToUse;
	U_32 singlesSeen;
	U_32 objectsSeen;
	U_32 doublesSeen;
	U_32 singleStaticsSeen;
	U_32 objectStaticsSeen;
	U_32 doubleStaticsSeen;
	U_32 walkFlags;
	UDATA lockOffset;
	UDATA finalizeLinkOffset;
	struct J9HiddenInstanceField hiddenLockwordField;
	struct J9HiddenInstanceField hiddenFinalizeLinkField;
	struct J9HiddenInstanceField* hiddenInstanceFields[J9VM_MAX_HIDDEN_FIELDS_PER_CLASS];
	UDATA hiddenInstanceFieldCount;
	UDATA hiddenInstanceFieldWalkIndex;
#ifdef J9VM_OPT_VALHALLA_VALUE_TYPES
	struct J9FlattenedClassCache *flattenedClassCache;
	UDATA firstFlatSingleOffset;
	UDATA firstFlatObjectOffset;
	UDATA firstFlatDoubleOffset;
	UDATA currentFlatSingleOffset;
	UDATA currentFlatObjectOffset;
	UDATA currentFlatDoubleOffset;
	BOOLEAN classRequiresPrePadding;
	UDATA flatBackFillSize;
#endif /* J9VM_OPT_VALHALLA_VALUE_TYPES */
} J9ROMFieldOffsetWalkState;

#define J9VM_FIELD_OFFSET_WALK_MAXIMUM_HIDDEN_FIELDS_PER_CLASS  J9VM_MAX_HIDDEN_FIELDS_PER_CLASS
#define J9VM_FIELD_OFFSET_WALK_INCLUDE_STATIC  0x1
#define J9VM_FIELD_OFFSET_WALK_BACKFILL_OBJECT_FIELD  0x20
#define J9VM_FIELD_OFFSET_WALK_INCLUDE_HIDDEN  0x4
#define J9VM_FIELD_OFFSET_WALK_ONLY_OBJECT_SLOTS  0x8
#define J9VM_FIELD_OFFSET_WALK_CALCULATE_INSTANCE_SIZE  0x10
#define J9VM_FIELD_OFFSET_WALK_BACKFILL_SINGLE_FIELD  0x40
#define J9VM_FIELD_OFFSET_WALK_INCLUDE_INSTANCE  0x2
#define J9VM_FIELD_OFFSET_WALK_PREINDEX_INTERFACE_FIELDS  0x80
#define J9VM_FIELD_OFFSET_WALK_BACKFILL_FLAT_OBJECT_FIELD  0x100
#define J9VM_FIELD_OFFSET_WALK_BACKFILL_FLAT_SINGLE_FIELD  0x200


typedef struct J9ROMFullTraversalFieldOffsetWalkState {
	struct J9JavaVM* javaVM;
	struct J9ROMFieldOffsetWalkState fieldOffsetWalkState;
	struct J9Class* clazz;
	struct J9Class* currentClass;
	struct J9Class** walkSuperclasses;
	struct J9ITable* superITable;
	UDATA classIndexAdjust;
	UDATA referenceIndexOffset;
	U_32 walkFlags;
	U_32 remainingClassDepth;
	UDATA fieldOffset;
} J9ROMFullTraversalFieldOffsetWalkState;

typedef struct J9AnnotationInfoEntry {
	J9SRP annotationType;
	J9SRP memberName;
	J9SRP memberSignature;
	U_32 elementPairCount;
	J9SRP annotationData;
	U_32 flags;
} J9AnnotationInfoEntry;

#define J9ANNOTATIONINFOENTRY_ANNOTATIONTYPE(base) SRP_GET((base)->annotationType, struct J9UTF8*)
#define J9ANNOTATIONINFOENTRY_MEMBERNAME(base) SRP_GET((base)->memberName, struct J9UTF8*)
#define J9ANNOTATIONINFOENTRY_MEMBERSIGNATURE(base) SRP_GET((base)->memberSignature, struct J9UTF8*)
#define J9ANNOTATIONINFOENTRY_ANNOTATIONDATA(base) SRP_GET((base)->annotationData, UDATA*)

/* @ddr_namespace: map_to_type=J9AnnotationInfo */

typedef struct J9AnnotationInfo {
	J9SRP defaultValues;
	J9SRP firstClass;
	J9SRP firstField;
	J9SRP firstMethod;
	J9SRP firstParameter;
	J9SRP firstAnnotation;
	U_32 countClass;
	U_32 countField;
	U_32 countMethod;
	U_32 countParameter;
	U_32 countAnnotation;
} J9AnnotationInfo;

#define ANNOTATION_TYPE_CLASS  0
#define ANNOTATION_FLAG_INVISIBLE  0x800000
#define ANNOTATION_TYPE_FIELD  1
#define ANNOTATION_PARM_MASK  0xFF000000
#define ANNOTATION_TYPE_PARAMETER  3
#define ANNOTATION_TYPE_ANNOTATION  4
#define ANNOTATION_TAG_MASK  0xFF
#define ANNOTATION_PARM_SHIFT  24
#define ANNOTATION_TYPE_MASK  7
#define ANNOTATION_TYPE_METHOD  2

#define J9ANNOTATIONINFO_DEFAULTVALUES(base) SRP_GET((base)->defaultValues, struct J9AnnotationInfoEntry*)
#define J9ANNOTATIONINFO_FIRSTCLASS(base) NNSRP_GET((base)->firstClass, struct J9AnnotationInfoEntry*)
#define J9ANNOTATIONINFO_FIRSTFIELD(base) NNSRP_GET((base)->firstField, struct J9AnnotationInfoEntry*)
#define J9ANNOTATIONINFO_FIRSTMETHOD(base) NNSRP_GET((base)->firstMethod, struct J9AnnotationInfoEntry*)
#define J9ANNOTATIONINFO_FIRSTPARAMETER(base) NNSRP_GET((base)->firstParameter, struct J9AnnotationInfoEntry*)
#define J9ANNOTATIONINFO_FIRSTANNOTATION(base) NNSRP_GET((base)->firstAnnotation, struct J9AnnotationInfoEntry*)

typedef struct J9AnnotationState {
	UDATA leftToDo;
	U_8* lastAddr;
} J9AnnotationState;

typedef struct J9Module {
	j9object_t moduleName;
	j9object_t moduleObject;
	j9object_t version;
	struct J9ClassLoader* classLoader;
	struct J9HashTable* readAccessHashTable;
	struct J9HashTable* removeAccessHashTable;
	struct J9HashTable* removeExportsHashTable;
	BOOLEAN isLoose;
	BOOLEAN isOpen;
} J9Module;

typedef struct J9Package {
	struct J9UTF8* packageName;
	U_32 exportToAll;
	U_32 exportToAllUnnamed;
	struct J9Module* module;
	struct J9HashTable* exportsHashTable;
	struct J9ClassLoader* classLoader;
} J9Package;

typedef struct J9ModuleExtraInfo {
	struct J9Module* j9module;
	struct J9UTF8* jrtURL;
	struct J9ClassPathEntry* patchPathEntries;
	UDATA patchPathCount;
} J9ModuleExtraInfo;

/*             Structure of Flattened Class Cache              */
/*                                                             *
 *    Default Value   |   Number of Entries   |                *
 *____________________|_______________________|________________*
 *                    |                       |                *
 *      clazz 0       |   Name & Signature 0  |     offset 0   *
 *____________________|_______________________|________________*
 *                    |                       |                *
 *      clazz 1       |   Name & Signature 1  |     offset 1   *
 *____________________|_______________________|________________*
 *         .          |           .           |        .       *
 *         .          |           .           |        .       *
 *         .          |           .           |        .       *
 *____________________|_______________________|________________*
 *                    |                       |                *
 *      clazz N       |   Name & Signature N  |     offset N   *
 ***************************************************************/

#define J9_VM_FCC_CLASS_FLAGS_MASK ((UDATA) 0xFF)
#define J9_VM_FCC_CLASS_FLAGS_STATIC_FIELD ((UDATA)0x1)

typedef struct J9FlattenedClassCacheEntry {
	struct J9Class* clazz;
	struct J9ROMFieldShape * field;
	UDATA offset;
} J9FlattenedClassCacheEntry;

typedef struct J9FlattenedClassCache {
	j9object_t defaultValue;
	UDATA numberOfEntries;
} J9FlattenedClassCache;

#define J9_VM_FCC_ENTRY_FROM_FCC(flattenedClassCache, index) (((J9FlattenedClassCacheEntry *)((flattenedClassCache) + 1)) + (index))
#define J9_VM_FCC_ENTRY_FROM_CLASS(clazz, index) J9_VM_FCC_ENTRY_FROM_FCC((clazz)->flattenedClassCache, index)
#define J9_VM_FCC_CLASS_FROM_ENTRY(entry) ((J9Class *)((UDATA)(entry)->clazz & ~J9_VM_FCC_CLASS_FLAGS_MASK))
#define J9_VM_FCC_ENTRY_IS_STATIC_FIELD(entry) J9_ARE_ALL_BITS_SET((UDATA)(entry)->clazz, J9_VM_FCC_CLASS_FLAGS_STATIC_FIELD)

struct J9TranslationBufferSet;
typedef struct J9VerboseStruct {
	void  ( *hookDynamicLoadReporting)(struct J9TranslationBufferSet *dynamicLoadBuffers) ;
	U_8*  ( *getCfrExceptionDetails)(struct J9JavaVM *javaVM, J9CfrError *error, U_8* className, UDATA classNameLength, U_8* buf, UDATA* bufLen) ;
	U_8*  ( *getRtvExceptionDetails)(struct J9BytecodeVerificationData* error, U_8* buf, UDATA* bufLen) ;
} J9VerboseStruct;

typedef struct J9InternHashTableEntry {
	struct J9UTF8* utf8;
	struct J9ClassLoader* classLoader;
	U_16 flags;
	U_16 internWeight;
	struct J9InternHashTableEntry* prevNode;
	struct J9InternHashTableEntry* nextNode;
} J9InternHashTableEntry;

typedef struct J9StackElement {
	struct J9StackElement* previous;
	struct J9ClassLoader* classLoader;
	void* element;
} J9StackElement;

typedef struct J9SubclassWalkState {
	struct J9Class* currentClass;
	UDATA rootDepth;
} J9SubclassWalkState;

typedef struct J9ROMClassTOCEntry {
	J9SRP className;
	J9SRP romClass;
} J9ROMClassTOCEntry;

#define J9ROMCLASSTOCENTRY_CLASSNAME(base) NNSRP_GET((base)->className, struct J9UTF8*)
#define J9ROMCLASSTOCENTRY_ROMCLASS(base) NNSRP_GET((base)->romClass, struct J9ROMClass*)

typedef struct J9DynamicLoadStats {
	UDATA nameBufferLength;
	U_8* name;
	UDATA nameLength;
	UDATA readStartTime;
	UDATA readEndTime;
	UDATA loadStartTime;
	UDATA loadEndTime;
	UDATA translateStartTime;
	UDATA translateEndTime;
	UDATA sunSize;
	UDATA romSize;
	UDATA debugSize;
} J9DynamicLoadStats;

typedef struct J9JImageIntf {
	struct J9JavaVM *vm;
	struct J9PortLibrary *portLib;
	UDATA libJImageHandle;
	I_32 (* jimageOpen)(struct J9JImageIntf *jimageIntf, const char *name, UDATA *handle);
	void (* jimageClose)(struct J9JImageIntf *jimageIntf, UDATA handle);
	I_32 (* jimageFindResource)(struct J9JImageIntf *jimageIntf, UDATA handle, const char *moduleName, const char* name, UDATA *resourceLocation, I_64 *size);
	void (* jimageFreeResourceLocation)(struct J9JImageIntf *jimageIntf, UDATA handle, UDATA resourceLocation);
	I_32 (* jimageGetResource)(struct J9JImageIntf *jimageIntf, UDATA handle, UDATA resourceLocation, char *buffer, I_64 bufferSize, I_64 *resourceSize);
	const char * (* jimagePackageToModule)(struct J9JImageIntf *jimageIntf, UDATA handle, const char *packageName);
} J9JImageIntf;

/* @ddr_namespace: map_to_type=J9TranslationBufferSet */

typedef struct J9TranslationLocalBuffer {
	IDATA entryIndex;
	I_32 loadLocationType;
	struct J9ClassPathEntry* cpEntryUsed;
	struct J9ClassPatchMap* patchMap;
} J9TranslationLocalBuffer;

typedef struct J9TranslationBufferSet {
	struct J9DynamicLoadStats* dynamicLoadStats;
	U_8* sunClassFileBuffer;
	UDATA sunClassFileSize;
	UDATA currentSunClassFileSize;
	U_8* searchFilenameBuffer;
	UDATA searchFilenameSize;
	UDATA relocatorDLLHandle;
	UDATA flags;
	U_8* classFileError;
	UDATA classFileSize;
	void* romClassBuilder;
	IDATA  ( *findLocallyDefinedClassFunction)(struct J9VMThread * vmThread, struct J9Module * j9module, U_8 * className, U_32 classNameLength, struct J9ClassLoader * classLoader, struct J9ClassPathEntry * classPath, UDATA classPathEntryCount, UDATA options, struct J9TranslationLocalBuffer *localBuffer) ;
	struct J9Class*  ( *internalDefineClassFunction)(struct J9VMThread* vmThread, void* className, UDATA classNameLength, U_8* classData, UDATA classDataLength, j9object_t classDataObject, struct J9ClassLoader* classLoader, j9object_t protectionDomain, UDATA options, struct J9ROMClass *existingROMClass, struct J9Class *hostClass, struct J9TranslationLocalBuffer *localBuffer) ;
	I_32  ( *closeZipFileFunction)(struct J9VMInterface* vmi, struct VMIZipFile* zipFile) ;
	void  ( *reportStatisticsFunction)(struct J9JavaVM * javaVM, struct J9ClassLoader* loader, struct J9ROMClass* romClass, struct J9TranslationLocalBuffer *localBuffer) ;
	UDATA  ( *internalLoadROMClassFunction)(struct J9VMThread * vmThread, struct J9LoadROMClassData *loadData, struct J9TranslationLocalBuffer *localBuffer) ;
	IDATA  ( *transformROMClassFunction)(struct J9JavaVM *javaVM, struct J9PortLibrary *portLibrary, struct J9ROMClass *romClass, U_8 **classData, U_32 *size) ;
} J9TranslationBufferSet;

#define BCU_UNUSED_2  2
#define BCU_VERBOSE  1
#define BCU_TRACK_UTF8DATA  4
#define BCU_UNUSED_40  64
#define BCU_ENABLE_INVARIANT_INTERNING  8
#define BCU_ENABLE_ROMCLASS_RESIZING  0x100

typedef struct J9BytecodeVerificationData {
	IDATA  ( *verifyBytecodesFunction)(struct J9PortLibrary *portLib, struct J9Class *ramClass, struct J9ROMClass *romClass, struct J9BytecodeVerificationData *verifyData) ;
	UDATA  ( *checkClassLoadingConstraintForNameFunction)(struct J9VMThread* vmThread, struct J9ClassLoader* loader1, struct J9ClassLoader* loader2, U_8* name1, U_8* name2, UDATA length, UDATA copyUTFs) ;
	struct J9UTF8** classNameList;
	struct J9UTF8** classNameListEnd;
	U_8* classNameSegment;
	U_8* classNameSegmentFree;
	U_8* classNameSegmentEnd;
	U_32* bytecodeMap;
	UDATA bytecodeMapSize;
	UDATA* stackMaps;
	UDATA stackMapsSize;
	IDATA stackMapsCount;
	UDATA* liveStack;
	UDATA liveStackSize;
	UDATA stackSize;
	UDATA* unwalkedQueue;
	UDATA unwalkedQueueHead;
	UDATA unwalkedQueueTail;
	UDATA* rewalkQueue;
	UDATA rewalkQueueHead;
	UDATA rewalkQueueTail;
	UDATA rootQueueSize;
	UDATA ignoreStackMaps;
	struct J9ROMClass* romClass;
	struct J9ROMMethod* romMethod;
	UDATA errorPC;
	UDATA errorCode;
	UDATA errorModule;
	UDATA errorTargetType;
	UDATA errorTempData;
	I_16 errorDetailCode;
	U_16 errorArgumentIndex;
	I_32 errorTargetFrameIndex;
	U_32 errorCurrentFramePosition;
	struct J9UTF8* errorClassString;
	struct J9UTF8* errorMethodString;
	struct J9UTF8* errorSignatureString;
	struct J9VMThread* vmStruct;
	struct J9ClassLoader* classLoader;
	omrthread_monitor_t verifierMutex;
	UDATA romClassInSharedClasses;
	UDATA* internalBufferStart;
	UDATA* internalBufferEnd;
	UDATA* currentAlloc;
	UDATA verificationFlags;
	U_8* excludeAttribute;
	struct J9JVMTIClassPair* redefinedClasses;
	UDATA redefinedClassesCount;
	struct J9PortLibrary * portLib;
	struct J9JavaVM* javaVM;
	BOOLEAN createdStackMap;
} J9BytecodeVerificationData;

typedef struct J9BytecodeOffset {
	U_32 first;
	U_32 second;
} J9BytecodeOffset;

typedef struct J9NPEMessageData {
	UDATA npePC;
	U_32 *bytecodeMap;
	UDATA bytecodeMapSize;
	struct J9BytecodeOffset *bytecodeOffset;
	UDATA bytecodeOffsetSize;
	UDATA *stackMaps;
	UDATA stackMapsSize;
	IDATA stackMapsCount;
	struct J9BranchTargetStack *liveStack;
	UDATA liveStackSize;
	UDATA stackSize;
	UDATA *unwalkedQueue;
	UDATA unwalkedQueueHead;
	UDATA unwalkedQueueTail;
	UDATA unwalkedQueueSize;
	struct J9ROMClass *romClass;
	struct J9ROMMethod *romMethod;
	struct J9VMThread *vmThread;
} J9NPEMessageData;

/* @ddr_namespace: map_to_type=J9NativeLibrary */

typedef struct J9NativeLibrary {
	UDATA handle;
	char* name;
#if defined(J9VM_OPT_JAVA_OFFLOAD_SUPPORT)
	UDATA doSwitching;
#endif /* J9VM_OPT_JAVA_OFFLOAD_SUPPORT */
	struct J9NativeLibrary* next;
	UDATA linkMode;
	UDATA userData;
	UDATA flags;
	char* logicalName;
	IDATA  ( *send_lifecycle_event)(struct J9VMThread* vmThread, struct J9NativeLibrary* library, const char * functionName, IDATA defaultResult) ;
	UDATA  ( *close)(struct J9VMThread* vmThread, struct J9NativeLibrary* library) ;
	UDATA  ( *bind_method)(struct J9VMThread* vmThread, struct J9NativeLibrary* library, struct J9Method* method, char* functionName, char* argSignature) ;
	UDATA  ( *dispatch_method)(struct J9VMThread* vmThread, struct J9NativeLibrary* library, struct J9Method* method, struct J9HookedNative* hookInfo, void* arguments, void** returnValue) ;
	struct J9Pool* hookedNatives;
	omrthread_monitor_t hookedNativesMonitor;
} J9NativeLibrary;

#define J9NATIVELIB_LINK_MODE_UNINITIALIZED 0
#define J9NATIVELIB_LINK_MODE_STATIC 1
#define J9NATIVELIB_LINK_MODE_DYNAMIC 2

#define J9NATIVELIB_LOAD_ERR_OUT_OF_MEMORY  3
#define J9NATIVELIB_LOAD_ERR_NOT_FOUND  2
#define J9NATIVELIB_LOAD_ERR_GENERIC  6
#define J9NATIVELIB_FLAG_ALLOW_INL  1
#define J9NATIVELIB_LOAD_ERR_ALREADY_LOADED  1
#define J9NATIVELIB_LOAD_ERR_JNI_ONLOAD_FAILED  4
#define J9NATIVELIB_LOAD_ERR_INVALID  5
#define J9NATIVELIB_LOAD_OK  0

/* @ddr_namespace: map_to_type=J9ClassPathEntry */

typedef struct J9ClassPathEntry {
	U_8* path;
	void* extraInfo;
	U_32 pathLength;
	U_16 type;
	U_16 flags;
	U_32 status;
#if defined(J9VM_ENV_DATA64)
	U_8 paddingToPowerOf2[4];
#else
	U_8 paddingToPowerOf2[12];
#endif
} J9ClassPathEntry;

typedef struct J9ClassPatchMap {
	U_16 size;
	U_16* indexMap;
} J9ClassPatchMap;

#define CPE_STATUS_ASSUME_JXE  					0x1
#define CPE_STATUS_JXE_MISSING_ROM_CLASSES  	0x2
#define CPE_STATUS_JXE_CORRUPT_IMAGE_HEADER  	0x4
#define CPE_STATUS_JXE_OE_NOT_SUPPORTED  		0x8
#define CPE_STATUS_IGNORE_ZIP_LOAD_STATE  		0x10

#define CPE_TYPE_UNKNOWN  						0
#define CPE_TYPE_DIRECTORY						1
#define CPE_TYPE_JAR							2
#define CPE_TYPE_JIMAGE							3
#define CPE_TYPE_UNUSABLE						5

#define CPE_FLAG_BOOTSTRAP 						1
#define CPE_FLAG_USER							2

#define CPE_CLASSPATH_TYPE_MASK  (CPE_FLAG_BOOTSTRAP | CPE_FLAG_USER)

/* J9BCTranslationData is no longer used in the code, but the constants are.
 * DDR requires that the structure continue to exist to hold the constants.
 */

/* @ddr_namespace: map_to_type=J9BCTranslationData */

typedef struct J9BCTranslationData {
	UDATA structureUnusedButConstantsUsedInDDR;
} J9BCTranslationData;

#define BCT_LittleEndianOutput  0
#define BCT_BigEndianOutput  1
#define BCT_StripDebugAttributes  0x100
#define BCT_StaticVerification  0x200
#define BCT_DumpPreverifyData  0x400
#define BCT_Xfuture  0x800
#define BCT_AlwaysSplitBytecodes 0x1000
#define BCT_IntermediateDataIsClassfile  0x2000
#define BCT_ValueTypesEnabled  0x4000
#define BCT_StripDebugLines  0x8000
#define BCT_StripDebugSource  0x10000
#define BCT_StripDebugVars  0x20000
#define BCT_StripSourceDebugExtension  0x40000
#define BCT_RetainRuntimeInvisibleAttributes  0x80000
#define BCT_AnyPreviewVersion  0x100000
#define BCT_EnablePreview 0x200000
#define BCT_Unsafe  0x400000
#define BCT_BasicCheckOnly  0x800000
#define BCT_DumpMaps  0x40000000

#define BCT_J9DescriptionCpTypeScalar  0
#define BCT_J9DescriptionCpTypeObject  1
#define BCT_J9DescriptionCpTypeClass  2
#define BCT_J9DescriptionCpTypeMethodType  3
#define BCT_J9DescriptionCpTypeMethodHandle  4
#define BCT_J9DescriptionCpTypeConstantDynamic 5
#define BCT_J9DescriptionCpTypeShift  4
#define BCT_J9DescriptionCpTypeMask  15

#define BCT_J9DescriptionImmediate  1

#define BCT_MajorClassFileVersionMask  0xFF000000
#define BCT_MajorClassFileVersionMaskShift  24

/* A given Java feature version uses class-file format (version) + 44. */
#define BCT_JavaMajorVersionShifted(java_version)  ((44 + (U_32)(java_version)) << BCT_MajorClassFileVersionMaskShift)
/* When adding support for a new Java feature version, update this appropriately. */
#define BCT_JavaMaxMajorVersionShifted  BCT_JavaMajorVersionShifted(18)

typedef struct J9RAMClassFreeListBlock {
	UDATA size;
	struct J9RAMClassFreeListBlock* nextFreeListBlock;
} J9RAMClassFreeListBlock;

typedef struct J9JavaVMAttachArgs {
	I_32 version;
	const char* name;
	j9object_t* group;
} J9JavaVMAttachArgs;

/* @ddr_namespace: map_to_type=J9ConstantPool */

typedef struct J9ConstantPool {
	struct J9Class* ramClass;
	struct J9ROMConstantPoolItem* romConstantPool;
} J9ConstantPool;

#define J9CPTYPE_UNUSED  0
#define J9CPTYPE_CLASS  1
#define J9CPTYPE_STRING  2
#define J9CPTYPE_INT  3
#define J9CPTYPE_FLOAT  4
#define J9CPTYPE_LONG  5
#define J9CPTYPE_DOUBLE  6
#define J9CPTYPE_FIELD  7
#define J9CPTYPE_INSTANCE_METHOD  9
#define J9CPTYPE_STATIC_METHOD  10
#define J9CPTYPE_HANDLE_METHOD  11
#define J9CPTYPE_INTERFACE_METHOD  12
#define J9CPTYPE_METHOD_TYPE  13
#define J9CPTYPE_METHODHANDLE  14
#define J9CPTYPE_ANNOTATION_UTF8  15
#define J9CPTYPE_CONSTANT_DYNAMIC 17
/*
 * INTERFACE_STATIC and INTERFACE_INSTANCE cpType are used to track the classfile tag of a methodref [CFR_CONSTANT_InterfaceMethodref]
 * These types are need to support correct initializer for constantpool due to how cp entries are pre-initialized
 * These will be used to check for cp consistency during resolve time
 */
#define J9CPTYPE_INTERFACE_STATIC_METHOD 18
#define J9CPTYPE_INTERFACE_INSTANCE_METHOD 19

#define J9_CP_BITS_PER_DESCRIPTION  8
#define J9_CP_DESCRIPTIONS_PER_U32  4
#define J9_CP_DESCRIPTION_MASK  255
#define J9CPTYPE_UNUSED8  8

typedef struct J9RAMConstantPoolItem {
	UDATA slot1;
	UDATA slot2;
} J9RAMConstantPoolItem;

typedef struct J9RAMClassRef {
	struct J9Class* value;
	UDATA modifiers;
} J9RAMClassRef;

typedef struct J9RAMConstantRef {
	UDATA slot1;
	UDATA slot2;
} J9RAMConstantRef;

typedef struct J9RAMSingleSlotConstantRef {
	UDATA value;
	UDATA unused;
} J9RAMSingleSlotConstantRef;

typedef struct J9RAMStringRef {
	j9object_t stringObject;
	UDATA unused;
} J9RAMStringRef;

typedef struct J9RAMConstantDynamicRef {
	j9object_t value;
	j9object_t exception;
} J9RAMConstantDynamicRef;

typedef struct J9RAMFieldRef {
	UDATA volatile valueOffset;
	UDATA volatile flags;
} J9RAMFieldRef;

typedef struct J9RAMMethodHandleRef {
	j9object_t methodHandle;
	UDATA unused;
} J9RAMMethodHandleRef;

typedef struct J9RAMMethodRef {
	UDATA methodIndexAndArgCount;
	struct J9Method* method;
} J9RAMMethodRef;

typedef struct J9RAMInterfaceMethodRef {
	UDATA methodIndexAndArgCount;
	UDATA interfaceClass;
} J9RAMInterfaceMethodRef;

typedef struct J9RAMSpecialMethodRef {
	UDATA methodIndexAndArgCount;
	struct J9Method* method;
} J9RAMSpecialMethodRef;

typedef struct J9RAMStaticMethodRef {
	UDATA methodIndexAndArgCount;
	struct J9Method* method;
} J9RAMStaticMethodRef;

typedef struct J9RAMVirtualMethodRef {
	UDATA volatile methodIndexAndArgCount;
	struct J9Method *volatile method;
} J9RAMVirtualMethodRef;

typedef struct J9RAMMethodTypeRef {
	j9object_t type;
	UDATA slotCount;
} J9RAMMethodTypeRef;

typedef struct J9RAMStaticFieldRef {
	UDATA volatile valueOffset;
	IDATA volatile flagsAndClass;
} J9RAMStaticFieldRef;

typedef struct J9ROMConstantPoolItem {
	U_32 slot1;
	U_32 slot2;
} J9ROMConstantPoolItem;

typedef struct J9ROMClassRef {
	J9SRP name;
	U_32 runtimeFlags;
} J9ROMClassRef;

#define J9ROMCLASSREF_NAME(base) NNSRP_GET((base)->name, struct J9UTF8*)

typedef struct J9ROMConstantRef {
	U_32 slot1;
	U_32 slot2;
} J9ROMConstantRef;

typedef struct J9ROMSingleSlotConstantRef {
	U_32 data;
	U_32 cpType;
} J9ROMSingleSlotConstantRef;

typedef struct J9ROMStringRef {
	J9SRP utf8Data;
	U_32 cpType;
} J9ROMStringRef;

#define J9ROMSTRINGREF_UTF8DATA(base) NNSRP_GET((base)->utf8Data, struct J9UTF8*)

typedef struct J9ROMConstantDynamicRef {
	J9SRP nameAndSignature;
	U_32 bsmIndexAndCpType;
} J9ROMConstantDynamicRef;

#define J9ROMCONSTANTDYNAMICREF_NAMEANDSIGNATURE(base) NNSRP_GET((base)->nameAndSignature, struct J9ROMNameAndSignature*)

typedef struct J9ROMFieldRef {
	U_32 classRefCPIndex;
	J9SRP nameAndSignature;
} J9ROMFieldRef;

#define J9ROMFIELDREF_NAMEANDSIGNATURE(base) NNSRP_GET((base)->nameAndSignature, struct J9ROMNameAndSignature*)

/* @ddr_namespace: map_to_type=J9ROMMethodHandleRef */

typedef struct J9ROMMethodHandleRef {
	U_32 methodOrFieldRefIndex;
	U_32 handleTypeAndCpType;
} J9ROMMethodHandleRef;

#define MH_REF_GETFIELD  1
#define MH_REF_GETSTATIC  2
#define MH_REF_INVOKESPECIAL  7
#define MH_REF_INVOKEINTERFACE  9
#define MH_REF_NEWINVOKESPECIAL  8
#define MH_REF_INVOKESTATIC  6
#define MH_REF_PUTSTATIC  4
#define MH_REF_INVOKEVIRTUAL  5
#define MH_REF_PUTFIELD  3

/* Constants mapped from java.lang.invoke.MethodHandleNatives$Constants
 * These constants are validated by the MethodHandleNatives$Constants.verifyConstants()
 * method when Java assertions are enabled
 */
#define MN_IS_METHOD		0x00010000
#define MN_IS_CONSTRUCTOR	0x00020000
#define MN_IS_FIELD			0x00040000
#define MN_IS_TYPE			0x00080000
#define MN_CALLER_SENSITIVE	0x00100000
#define MN_TRUSTED_FINAL	0x00200000

typedef struct J9ROMMethodRef {
	U_32 classRefCPIndex;
	J9SRP nameAndSignature;
} J9ROMMethodRef;

#define J9ROMMETHODREF_NAMEANDSIGNATURE(base) NNSRP_GET((base)->nameAndSignature, struct J9ROMNameAndSignature*)

typedef struct J9ROMMethodTypeRef {
	J9SRP signature;
	U_32 cpType;
} J9ROMMethodTypeRef;

#define J9ROMMETHODTYPEREF_SIGNATURE(base) NNSRP_GET((base)->signature, struct J9UTF8*)

typedef struct J9NameAndSignature {
	struct J9UTF8* name;
	struct J9UTF8* signature;
} J9NameAndSignature;

typedef struct J9WalkStackFramesAndSlotsStorage {
#if defined(J9VM_ARCH_X86)
#if defined(J9VM_ENV_DATA64)
	UDATA* jit_rax;
	UDATA* jit_rbx;
	UDATA* jit_rcx;
	UDATA* jit_rdx;
	UDATA* jit_rdi;
	UDATA* jit_rsi;
	UDATA* jit_rbp;
	UDATA* jit_rsp;
	UDATA* jit_r8;
	UDATA* jit_r9;
	UDATA* jit_r10;
	UDATA* jit_r11;
	UDATA* jit_r12;
	UDATA* jit_r13;
	UDATA* jit_r14;
	UDATA* jit_r15;
#else /* J9VM_ENV_DATA64 */
	UDATA* jit_eax;
	UDATA* jit_ebx;
	UDATA* jit_ecx;
	UDATA* jit_edx;
	UDATA* jit_edi;
	UDATA* jit_esi;
	UDATA* jit_ebp;
	UDATA* padding;
#endif /* J9VM_ENV_DATA64 */
#elif defined(J9VM_ARCH_POWER) /* J9VM_ARCH_X86 */
	UDATA* jit_r0;
	UDATA* jit_r1;
	UDATA* jit_r2;
	UDATA* jit_r3;
	UDATA* jit_r4;
	UDATA* jit_r5;
	UDATA* jit_r6;
	UDATA* jit_r7;
	UDATA* jit_r8;
	UDATA* jit_r9;
	UDATA* jit_r10;
	UDATA* jit_r11;
	UDATA* jit_r12;
	UDATA* jit_r13;
	UDATA* jit_r14;
	UDATA* jit_r15;
	UDATA* jit_r16;
	UDATA* jit_r17;
	UDATA* jit_r18;
	UDATA* jit_r19;
	UDATA* jit_r20;
	UDATA* jit_r21;
	UDATA* jit_r22;
	UDATA* jit_r23;
	UDATA* jit_r24;
	UDATA* jit_r25;
	UDATA* jit_r26;
	UDATA* jit_r27;
	UDATA* jit_r28;
	UDATA* jit_r29;
	UDATA* jit_r30;
	UDATA* jit_r31;
#elif defined(J9VM_ARCH_RISCV) /* J9VM_ARCH_POWER */
	UDATA* jit_r0;
	UDATA* jit_r1;
	UDATA* jit_r2;
	UDATA* jit_r3;
	UDATA* jit_r4;
	UDATA* jit_r5;
	UDATA* jit_r6;
	UDATA* jit_r7;
	UDATA* jit_r8;
	UDATA* jit_r9;
	UDATA* jit_r10;
	UDATA* jit_r11;
	UDATA* jit_r12;
	UDATA* jit_r13;
	UDATA* jit_r14;
	UDATA* jit_r15;
	UDATA* jit_r16;
	UDATA* jit_r17;
	UDATA* jit_r18;
	UDATA* jit_r19;
	UDATA* jit_r20;
	UDATA* jit_r21;
	UDATA* jit_r22;
	UDATA* jit_r23;
	UDATA* jit_r24;
	UDATA* jit_r25;
	UDATA* jit_r26;
	UDATA* jit_r27;
	UDATA* jit_r28;
	UDATA* jit_r29;
	UDATA* jit_r30;
	UDATA* jit_r31;
#elif defined(J9VM_ARCH_S390) /* J9VM_ARCH_RISCV */
	UDATA* jit_r0;
	UDATA* jit_r1;
	UDATA* jit_r2;
	UDATA* jit_r3;
	UDATA* jit_r4;
	UDATA* jit_r5;
	UDATA* jit_r6;
	UDATA* jit_r7;
	UDATA* jit_r8;
	UDATA* jit_r9;
	UDATA* jit_r10;
	UDATA* jit_r11;
	UDATA* jit_r12;
	UDATA* jit_r13;
	UDATA* jit_r14;
	UDATA* jit_r15;
#if !defined(J9VM_ENV_DATA64)
	UDATA* jit_r16;
	UDATA* jit_r17;
	UDATA* jit_r18;
	UDATA* jit_r19;
	UDATA* jit_r20;
	UDATA* jit_r21;
	UDATA* jit_r22;
	UDATA* jit_r23;
	UDATA* jit_r24;
	UDATA* jit_r25;
	UDATA* jit_r26;
	UDATA* jit_r27;
	UDATA* jit_r28;
	UDATA* jit_r29;
	UDATA* jit_r30;
	UDATA* jit_r31;
#endif /* !J9VM_ENV_DATA64 */
#elif defined(J9VM_ARCH_ARM) /* J9VM_ARCH_S390 */
	UDATA* jit_r0;
	UDATA* jit_r1;
	UDATA* jit_r2;
	UDATA* jit_r3;
	UDATA* jit_r4;
	UDATA* jit_r5;
	UDATA* jit_r6;
	UDATA* jit_r7;
	UDATA* jit_r8;
	UDATA* jit_r9;
	UDATA* jit_r10;
	UDATA* jit_r11;
#elif defined(J9VM_ARCH_AARCH64) /* J9VM_ARCH_ARM */
	UDATA* jit_r0;
	UDATA* jit_r1;
	UDATA* jit_r2;
	UDATA* jit_r3;
	UDATA* jit_r4;
	UDATA* jit_r5;
	UDATA* jit_r6;
	UDATA* jit_r7;
	UDATA* jit_r8;
	UDATA* jit_r9;
	UDATA* jit_r10;
	UDATA* jit_r11;
	UDATA* jit_r12;
	UDATA* jit_r13;
	UDATA* jit_r14;
	UDATA* jit_r15;
	UDATA* jit_r16;
	UDATA* jit_r17;
	UDATA* jit_r18;
	UDATA* jit_r19;
	UDATA* jit_r20;
	UDATA* jit_r21;
	UDATA* jit_r22;
	UDATA* jit_r23;
	UDATA* jit_r24;
	UDATA* jit_r25;
	UDATA* jit_r26;
	UDATA* jit_r27;
	UDATA* jit_r28;
	UDATA* jit_r29;
	UDATA* jit_r30;
	UDATA* jit_r31;
#else /* J9VM_ARCH_AARCH64 */
#error Unknown processor architecture
#endif /* J9VM_ARCH_AARCH64 */
} J9WalkStackFramesAndSlotsStorage;

typedef struct J9I2JState {
	UDATA* returnSP;
	UDATA* a0;
	struct J9Method* literals;
	U_8* pc;
} J9I2JState;

/* @ddr_namespace: map_to_type=J9StackWalkState */

typedef struct J9StackWalkState {
	struct J9StackWalkState* previous;
	struct J9VMThread* walkThread;
	UDATA flags;
	UDATA* bp;
	UDATA* unwindSP;
	U_8* pc;
	U_8 * nextPC;
	UDATA* sp;
	UDATA* arg0EA;
	struct J9Method* literals;
	UDATA* walkSP;
	UDATA argCount;
	struct J9ConstantPool* constantPool;
	struct J9Method* method;
	struct J9JITExceptionTable* jitInfo;
	UDATA frameFlags;
	UDATA resolveFrameFlags;
	UDATA skipCount;
	UDATA maxFrames;
	void* userData1;
	void* userData2;
	void* userData3;
	void* userData4;
	UDATA framesWalked;
	UDATA  ( *frameWalkFunction)(struct J9VMThread * vmThread, struct J9StackWalkState * walkState) ;
	void  ( *objectSlotWalkFunction)(struct J9VMThread * vmThread, struct J9StackWalkState * walkState, j9object_t * objectSlot, const void * stackLocation) ;
	void  ( *returnAddressWalkFunction)(struct J9VMThread * vmThread, struct J9StackWalkState * walkState, UDATA * raSlot) ;
	UDATA* cache;
	void* restartPoint;
	void* restartException;
	void* inlinerMap;
	UDATA inlineDepth;
	UDATA* cacheCursor;
	struct J9JITDecompilationInfo* decompilationRecord;
	struct J9WalkStackFramesAndSlotsStorage registerEAs;
	struct J9VMEntryLocalStorage* walkedEntryLocalStorage;
	struct J9I2JState* i2jState;
	struct J9JITDecompilationInfo* decompilationStack;
	U_8** pcAddress;
	UDATA outgoingArgCount;
	U_8* objectSlotBitVector;
	UDATA elsBitVector;
	void  ( *savedObjectSlotWalkFunction)(struct J9VMThread * vmThread, struct J9StackWalkState * walkState, j9object_t* objectSlot, const void * stackLocation) ;
	IDATA bytecodePCOffset;
	void  ( *dropToCurrentFrame)(struct J9StackWalkState * walkState) ;
	UDATA* j2iFrame;
	UDATA previousFrameFlags;
	IDATA slotIndex;
	UDATA slotType;
	struct J9VMThread* currentThread;
	void* linearSlotWalker;
	void* inlinedCallSite;
	void* stackMap;
	void* inlineMap;
	/* The size of J9StackWalkState must be a multiple of 8 because it is inlined into
	 * J9VMThread where alignment assumotions are being made.
	 */
#if 1 && !defined(J9VM_ENV_DATA64) /* Change to 0 or 1 based on number of fields above */
	U_32 padTo8;
#endif /* !J9VM_ENV_DATA64 */
} J9StackWalkState;

#define J9_STACKWALK_SLOT_TYPE_JIT_REGISTER_MAP  5
#define J9_STACKWALK_SLOT_TYPE_JNI_LOCAL  2
#define J9_STACKWALK_SLOT_TYPE_INTERNAL  4
#define J9_STACKWALK_SLOT_TYPE_PENDING  3
#define J9_STACKWALK_SLOT_TYPE_METHOD_LOCAL  1

typedef struct J9OSRFrame {
	UDATA flags;
	struct J9Method* method;
	UDATA bytecodePCOffset;
	UDATA numberOfLocals;
	UDATA maxStack;
	UDATA pendingStackHeight;
	struct J9MonitorEnterRecord* monitorEnterRecords;
} J9OSRFrame;

typedef struct J9OSRBuffer {
	UDATA numberOfFrames;
	void* jitPC;
} J9OSRBuffer;

/* @ddr_namespace: map_to_type=J9JITDecompilationInfo */

typedef struct J9JITDecompilationInfo {
	struct J9JITDecompilationInfo* next;
	UDATA* bp;
	U_8* pc;
	U_8** pcAddress;
	struct J9Method* method;
	UDATA reason;
	UDATA usesOSR;
	struct J9OSRBuffer osrBuffer;
} J9JITDecompilationInfo;

#define JITDECOMP_CODE_BREAKPOINT  1
#define JITDECOMP_SINGLE_STEP  16
#define JITDECOMP_HOTSWAP  2
#define JITDECOMP_POP_FRAMES  4
#define JITDECOMP_OSR_GLOBAL_BUFFER_USED  0x100
#define JITDECOMP_FRAME_POP_NOTIFICATION  32
#define JITDECOMP_DATA_BREAKPOINT  8
#define JITDECOMP_STACK_LOCALS_MODIFIED  64
#define JITDECOMP_ON_STACK_REPLACEMENT  0x80

/* @ddr_namespace: map_to_type=J9SFStackFrame */

typedef struct J9SFStackFrame {
	struct J9Method* savedCP;
	U_8* savedPC;
	UDATA* savedA0;
} J9SFStackFrame;

#define J9SF_FRAME_TYPE_NATIVE_METHOD  3
#define J9SF_MAX_SPECIAL_FRAME_TYPE  16
#define J9SF_FRAME_TYPE_JNI_NATIVE_METHOD  7
#define J9SF_FRAME_TYPE_JIT_JNI_CALLOUT  6
#define J9SF_FRAME_TYPE_METHOD  2
#define J9SF_FRAME_TYPE_METHODTYPE  8
#define J9SF_FRAME_TYPE_JIT_RESOLVE  5
#define J9SF_FRAME_TYPE_END_OF_STACK  0
#define J9SF_FRAME_TYPE_GENERIC_SPECIAL  1
#define J9SF_A0_INVISIBLE_TAG  2
#define J9SF_A0_REPORT_FRAME_POP_TAG  1

typedef struct J9SFSpecialFrame {
	UDATA specialFrameFlags;
	struct J9Method* savedCP;
	U_8* savedPC;
	UDATA* savedA0;
} J9SFSpecialFrame;

typedef struct J9SFJITStackFrame {
	UDATA specialFrameFlags;
	struct J9Method* savedCP;
	U_8* savedPC;
	UDATA* savedA0;
} J9SFJITStackFrame;

typedef struct J9SFJ2IFrame {
	struct J9I2JState i2jState;
	UDATA* previousJ2iFrame;
#if defined(J9VM_ARCH_X86)
#if defined(J9VM_ENV_DATA64)
	UDATA jit_rbx;
	UDATA jit_r15;
	UDATA jit_r14;
	UDATA jit_r13;
	UDATA jit_r12;
	UDATA jit_r11;
	UDATA jit_r10;
	UDATA jit_r9;
#else /* J9VM_ENV_DATA64 */
	UDATA jit_esi;
	UDATA jit_ecx;
	UDATA jit_ebx;
#endif /* J9VM_ENV_DATA64 */
#elif defined(J9VM_ARCH_POWER) /* J9VM_ARCH_X86 */
	UDATA jit_r31;
	UDATA jit_r30;
	UDATA jit_r29;
	UDATA jit_r28;
	UDATA jit_r27;
	UDATA jit_r26;
	UDATA jit_r25;
	UDATA jit_r24;
	UDATA jit_r23;
	UDATA jit_r22;
	UDATA jit_r21;
	UDATA jit_r20;
	UDATA jit_r19;
	UDATA jit_r18;
	UDATA jit_r17;
	UDATA jit_r16;
#if !defined(J9VM_ENV_DATA64)
	UDATA jit_r15;
#endif /* !J9VM_ENV_DATA64 */
#elif defined(J9VM_ARCH_RISCV) /* J9VM_ARCH_POWER */
	UDATA jit_r0;
	UDATA jit_r1;
	UDATA jit_r2;
	UDATA jit_r3;
	UDATA jit_r4;
	UDATA jit_r5;
	UDATA jit_r6;
	UDATA jit_r7;
	UDATA jit_r8;
	UDATA jit_r9;
	UDATA jit_r10;
	UDATA jit_r11;
	UDATA jit_r12;
	UDATA jit_r13;
	UDATA jit_r14;
	UDATA jit_r15;
	UDATA jit_r16;
	UDATA jit_r17;
	UDATA jit_r18;
	UDATA jit_r19;
	UDATA jit_r20;
	UDATA jit_r21;
	UDATA jit_r22;
	UDATA jit_r23;
	UDATA jit_r24;
	UDATA jit_r25;
	UDATA jit_r26;
	UDATA jit_r27;
	UDATA jit_r28;
	UDATA jit_r29;
	UDATA jit_r30;
	UDATA jit_r31;
#elif defined(J9VM_ARCH_S390) /* J9VM_ARCH_RISCV */
#if !defined(J9VM_ENV_DATA64)
	UDATA jit_r28;
	UDATA jit_r27;
	UDATA jit_r26;
	UDATA jit_r25;
	UDATA jit_r24;
	UDATA jit_r23;
	UDATA jit_r22;
#endif /* !J9VM_ENV_DATA64 */
	UDATA jit_r12;
	UDATA jit_r11;
	UDATA jit_r10;
	UDATA jit_r9;
	UDATA jit_r8;
	UDATA jit_r7;
	UDATA jit_r6;
#elif defined(J9VM_ARCH_ARM) /* J9VM_ARCH_S390 */
	UDATA jit_r10;
	UDATA jit_r9;
	UDATA jit_r6;
#elif defined(J9VM_ARCH_AARCH64) /* J9VM_ARCH_ARM */
	UDATA jit_r21;
	UDATA jit_r22;
	UDATA jit_r23;
	UDATA jit_r24;
	UDATA jit_r25;
	UDATA jit_r26;
	UDATA jit_r27;
	UDATA jit_r28;
#else /* J9VM_ARCH_AARCH64 */
#error Unknown processor architecture
#endif /* J9VM_ARCH_AARCH64 */
	UDATA specialFrameFlags;
	void* exitPoint;
	U_8* returnAddress;
	UDATA* taggedReturnSP;
} J9SFJ2IFrame;

typedef struct J9SFJITResolveFrame {
	j9object_t savedJITException;
	UDATA specialFrameFlags;
	UDATA parmCount;
	void* returnAddress;
	UDATA* taggedRegularReturnSP;
} J9SFJITResolveFrame;

typedef struct J9SFJNICallInFrame {
	UDATA* exitAddress;
	UDATA specialFrameFlags;
	struct J9Method* savedCP;
	U_8* savedPC;
	UDATA* savedA0;
} J9SFJNICallInFrame;

typedef struct J9SFMethodFrame {
	struct J9Method* method;
	UDATA specialFrameFlags;
	struct J9Method* savedCP;
	U_8* savedPC;
	UDATA* savedA0;
} J9SFMethodFrame;

typedef struct J9SFNativeMethodFrame {
	struct J9Method* method;
	UDATA specialFrameFlags;
	struct J9Method* savedCP;
	U_8* savedPC;
	UDATA* savedA0;
} J9SFNativeMethodFrame;

typedef struct J9SFJNINativeMethodFrame {
	struct J9Method* method;
	UDATA specialFrameFlags;
	struct J9Method* savedCP;
	U_8* savedPC;
	UDATA* savedA0;
} J9SFJNINativeMethodFrame;

typedef struct J9SFMethodTypeFrame {
	j9object_t methodType;
	UDATA argStackSlots;
	UDATA descriptionIntCount;
	UDATA specialFrameFlags;
	struct J9Method* savedCP;
	U_8* savedPC;
	UDATA* savedA0;
} J9SFMethodTypeFrame;

/* @ddr_namespace: map_to_type=J9AsyncEventRecord */

typedef struct J9AsyncEventRecord {
	J9AsyncEventHandler handler;
	void* userData;
} J9AsyncEventRecord;

#define J9ASYNC_ERROR_NONE  0
#define J9ASYNC_ERROR_NO_MORE_HANDLERS  -1
#define J9ASYNC_ERROR_INVALID_HANDLER_KEY  -2
#define J9ASYNC_ERROR_HANDLER_NOT_HOOKED  -3
#define J9ASYNC_MAX_HANDLERS  J9VM_ASYNC_MAX_HANDLERS

/* @ddr_namespace: map_to_type=J9VMGCSublistHeader */

typedef struct J9VMGCSublistHeader {
	struct J9VMGCSublist* list;
	UDATA newStoreFlag;
	omrthread_monitor_t mutex;
	UDATA growSize;
	UDATA currentSize;
	UDATA maxSize;
} J9VMGCSublistHeader;

#define J9_GCSUBLISTHEADER_FLAG_NEW_STORE  1

/* @ddr_namespace: map_to_type=J9VMGCSubList */

typedef struct J9VMGCSublist {
	struct J9VMGCSublist* next;
	UDATA newStoreFlag;
	UDATA* listBase;
	UDATA* listCurrent;
	UDATA* listTop;
	UDATA* savedListCurrent;
	UDATA spinlock;
} J9VMGCSublist;

#define J9_GCSUBLIST_FLAG_NEW_STORE  1

typedef struct J9VMGCSublistFragment {
	UDATA* fragmentCurrent;
	UDATA* fragmentTop;
	UDATA fragmentSize;
	void* parentList;
	UDATA deferredFlushID;
	UDATA count;
} J9VMGCSublistFragment;

/* @ddr_namespace: map_to_type=J9DLTInformationBlock */

typedef struct J9DLTInformationBlock {
	struct J9Method* methods[J9VM_DLT_HISTORY_SIZE];
	U_16 bcIndex[J9VM_DLT_HISTORY_SIZE];
	UDATA* temps;
	void* dltEntry;
	UDATA dltSP;
	UDATA inlineTempsBuffer[32];
	I_32 cursor;
#if defined(J9VM_ENV_DATA64)
	U_32 padding;
#endif /* J9VM_ENV_DATA64 */
} J9DLTInformationBlock;

#define J9DLT_HISTORY_SIZE  J9VM_DLT_HISTORY_SIZE

typedef struct J9InternalVMOption {
	void* hasBeenSet;
	void* value;
} J9InternalVMOption;

#if defined(J9VM_GC_REALTIME)

typedef struct J9VMGCSizeClasses {
	UDATA smallCellSizes[J9VMGC_SIZECLASSES_NUM_SMALL + 1];
	UDATA smallNumCells[J9VMGC_SIZECLASSES_NUM_SMALL + 1];
	UDATA sizeClassIndex[J9VMGC_SIZECLASSES_MAX_SMALL_SLOT_COUNT + 1];
} J9VMGCSizeClasses;

typedef struct J9VMGCSegregatedAllocationCacheEntry {
	UDATA* current;
	UDATA* top;
} J9VMGCSegregatedAllocationCacheEntry;

#endif /* J9VM_GC_REALTIME */

#if defined(J9VM_GC_THREAD_LOCAL_HEAP)

typedef struct J9GCVMInfo {
	UDATA tlhSize;
	UDATA tlhThreshold;
} J9GCVMInfo;

typedef struct J9GCThreadInfo {
	UDATA foobar;
} J9GCThreadInfo;

typedef struct J9ModronThreadLocalHeap {
	U_8* heapBase;
	U_8* realHeapTop;
	UDATA objectFlags;
	UDATA refreshSize;
	void* memorySubSpace;
	void* memoryPool;
} J9ModronThreadLocalHeap;

#endif /* J9VM_GC_THREAD_LOCAL_HEAP */

typedef struct J9JavaStack {
	UDATA* end;
	UDATA size;
	struct J9JavaStack* previous;
	UDATA firstReferenceFrame;
} J9JavaStack;

/* @ddr_namespace: map_to_type=J9Object */

typedef struct J9Object {
	/**
	 * An aligned class pointer representing the type of the object. The low order bits must be masked off prior to
	 * dereferencing this pointer. The bit values are described below.
	 * 
	 * @details 
	 * 
	 * The following diagram describes the metadata stored in the low order bit flags of an object class pointer:
	 * 
	 * Bit   31                23                15                7 6 5 4 3 2 1 0
	 *      ┌─────────────────────────────────────────────────────────────────────┐
	 * Word │0 0 0 0 0 0 0 0   0 0 0 0 0 0 0 0   0 0 0 0 0 0 0 0   0 0 0 0 0 0 0 0│
	 *      └──────────────────────────────────────────────────────┬─┬─┬─┬─┬─┬─┬─┬┘
	 *                                                             │ │ │ │ │ │ │ │
	 *                                                             └─┴┬┴─┘ │ │ │ └──► [1] Linked Free Header (Hole)
	 *                                                                │    │ │ └────► [2] Object has been hashed and moved
	 *                                                                │    │ └──────► [3] Slot contains forwarded pointer
	 *                                                                │    └────────► [4] Object has been hashed
	 *                                                                └─────────────► [5] Nursery age (0 - 14) or various remembered states
	 * 
	 * [1] If bit is 0, the slot represents the start of object, ie object header, which depending of forwarded bit
	 *     could be class slot or forwarded pointer.
	 *     If bit is 1, the slot represents the start of a hole, in which case the value is the address of the next
	 *	   connected hole (as part of free memory pool). The address could be null, in which case it is a small 
	 *	   stand-alone hole that is not part of the list, so called dark-matter. A hole will also have one more slot,
	 *	   with info about its size/length.
	 *	   This bit designation is needed when heap objects are iterated in address order - once an object is found,
	 *	   it can be followed by another object, or a hole that needs to be skipped.
	 * [2] If object is hashed (hash bit 4 set), and object is moved by a GC, the object will grow by a hash slot
	 *     (position of hash slot is found in class struct), and this bit will be set.
	 *     This bit is used to find hash value (from object address if not moved or from hash slot if moved) and to
	 *     find proper size of object (for example when heap objects are iterated, to find the position of the next 
	 *     object).
	 * [3] If the object has been moved on the heap and we encounter a stale reference, this bit tells us that the a
	 *     slot within the object contains a forwarding pointer (FP) to where we can find the moved object. FP
	 *     partially overlaps with class slot (it's not quite following it). In 32bit it exactly overlaps (both FP and
	 *     class slots are 32 bit). In 64bit non-CR, again it exactly overlaps (both FP and class slots are 64 bit ).
	 *     In 64bit CR, 1/2 of FP overlaps with class slot, and the other 1/2 of FP follows the class slots (FP is 
	 *     uncommpressed 64 bit, and class slot is 32 bit).
	 * [4] Object has been hashed by application thread, for example by using the object as a key in a hash map
	 * [5] If the object is in the nursery (gencon) these four bits count how many times the object has been flipped.
	 *     If the object is in tenure (gencon) these bits describe the various tenure states. These bits are not used
	 *     under the balanced GC policy. 
	 */
	j9objectclass_t clazz;
} J9Object;

#define OBJECT_HEADER_FORWARDED  1
#define OBJECT_HEADER_MARKED  1
#define OBJECT_HEADER_COPIED_WEAKLY  2
#define OBJECT_HEADER_FLAT_LOCK_CONTENTION  0x80000000
#define OBJECT_HEADER_HASH_MASK  0x7FFF0000
#define OBJECT_HEADER_HAS_BEEN_HASHED  0x20000
#define OBJECT_HEADER_HAS_BEEN_MOVED  0x10000
#define OBJECT_HEADER_HAS_BEEN_HASHED_MASK  0x30000
#define OBJECT_HEADER_FLAGS_SAVE_MASK  0xFFFF0000 /* OBJECT_HEADER_FLAT_LOCK_CONTENTION | OBJECT_HEADER_HASH_MASK */
#define OBJECT_HEADER_SHAPE_MASK  14
#define OBJECT_HEADER_SHAPE_INVALID_0  0
#define OBJECT_HEADER_SHAPE_BYTES  2
#define OBJECT_HEADER_SHAPE_WORDS  4
#define OBJECT_HEADER_SHAPE_LONGS  6
#define OBJECT_HEADER_SHAPE_UNUSED8  8
#define OBJECT_HEADER_SHAPE_DOUBLES  10
#define OBJECT_HEADER_SHAPE_POINTERS  12
#define OBJECT_HEADER_SHAPE_MIXED  14
#define OBJECT_HEADER_HAS_BEEN_HASHED_IN_CLASS  8
#define OBJECT_HEADER_HAS_BEEN_MOVED_IN_CLASS  2
#define OBJECT_HEADER_HAS_BEEN_HASHED_MASK_IN_CLASS  10
#define OBJECT_HEADER_SLOT_REFERENCE  0
#define OBJECT_HEADER_SLOT_BYTE  1
#define OBJECT_HEADER_SLOT_CHAR  2
#define OBJECT_HEADER_SLOT_DOUBLE  3
#define OBJECT_HEADER_SLOT_FLOAT  4
#define OBJECT_HEADER_SLOT_INT  5
#define OBJECT_HEADER_SLOT_LONG  6
#define OBJECT_HEADER_SLOT_SHORT  7
#define OBJECT_HEADER_SLOT_BOOLEAN  8
#define OBJECT_HEADER_INDEXABLE_STANDARD  1
#define OBJECT_HEADER_INDEXABLE_NHS  0x400
#define OBJECT_HEADER_AGE_SHIFT  4
#define OBJECT_HEADER_AGE_MASK  0xF0
#define OBJECT_HEADER_SPACE_ROM  0xF0
#define OBJECT_HEADER_OLD  0x8000
#define OBJECT_HEADER_REMEMBERED  0x4000
#define OBJECT_HEADER_WEAK  0x2000
#define OBJECT_HEADER_FIXED  0x1000
#define OBJECT_HEADER_STACK_ALLOCATED  0x800
#define OBJECT_HEADER_COLOR_MASK  0x300
#define OBJECT_HEADER_COLOR_BLACK  0x100
#define OBJECT_HEADER_COLOR_LIGHT_GREY  0x300
#define OBJECT_HEADER_COLOR_WHITE  0
#define OBJECT_HEADER_STACCATO_MARKED  32
#define OBJECT_HEADER_NON_CM_CANDIDATE  0x2000 /* OBJECT_HEADER_WEAK */
#define OBJECT_HEADER_NOT_REMEMBERED  0
#define OBJECT_HEADER_LOWEST_REMEMBERED  16	/* 1 << OBJECT_HEADER_AGE_SHIFT */
#define OBJECT_HEADER_AGE_INCREMENT  16	/* 1 << OBJECT_HEADER_AGE_SHIFT */
#define OBJECT_HEADER_AGE_DEFAULT  10
#define OBJECT_HEADER_LOCK_INFLATED  1
#define OBJECT_HEADER_LOCK_FLC  2
#define OBJECT_HEADER_LOCK_RESERVED  4
#define OBJECT_HEADER_LOCK_LEARNING  8
#define OBJECT_HEADER_LOCK_FIRST_RECURSION_BIT  0x10
#define OBJECT_HEADER_LOCK_LAST_RECURSION_BIT  0x80
#define OBJECT_HEADER_LOCK_RECURSION_MASK  0xF0
/*
 * OBJECT_HEADER_LOCK_RECURSION_OFFSET (and later versions) are used in
 * the ObjectMonitor_V#.java (eg. ObjectMonitor_V1.java) files used by DDR.
 * Changing them may lead to breaking suport for older corefiles.
 */
#define OBJECT_HEADER_LOCK_RECURSION_OFFSET  3 /* Do not add new uses of this constant. Use new version instead. */
#define OBJECT_HEADER_LOCK_V2_RECURSION_OFFSET  4
#define OBJECT_HEADER_LOCK_LEARNING_FIRST_LC_BIT  0x10 /* LC = Learning Count */
#define OBJECT_HEADER_LOCK_LEARNING_LAST_LC_BIT  0x20
#define OBJECT_HEADER_LOCK_LEARNING_FIRST_RECURSION_BIT  0x40
#define OBJECT_HEADER_LOCK_LEARNING_LAST_RECURSION_BIT  0x80
#define OBJECT_HEADER_LOCK_LEARNING_LC_MASK  0x30
#define OBJECT_HEADER_LOCK_LEARNING_LC_OFFSET  4
#define OBJECT_HEADER_LOCK_LEARNING_RECURSION_MASK  0xC0
#define OBJECT_HEADER_LOCK_LEARNING_RECURSION_OFFSET  6
#define OBJECT_HEADER_LOCK_BITS_MASK  0xFF
#define OBJECT_HEADER_INVALID_ADDR_BITS  3
#define OBJECT_HEADER_MONITOR_ENTER_INTERRUPTED  1
#define OBJECT_HEADER_ILLEGAL_MONITOR_STATE  2

typedef struct J9ObjectCompressed {
	U_32 clazz;
} J9ObjectCompressed;

typedef struct J9ObjectFull {
	UDATA clazz;
} J9ObjectFull;

typedef struct J9IndexableObject {
	j9objectclass_t clazz;
} J9IndexableObject;

typedef struct J9IndexableObjectContiguous {
	j9objectclass_t clazz;
	U_32 size;
#if defined(J9VM_ENV_DATA64) && defined(OMR_GC_FULL_POINTERS)
	U_32 padding;
#endif /* J9VM_ENV_DATA64 && !OMR_GC_COMPRESSED_POINTERS */
#if defined(J9VM_ENV_DATA64)
	void *dataAddr;
#endif /* J9VM_ENV_DATA64 */
} J9IndexableObjectContiguous;

typedef struct J9IndexableObjectContiguousCompressed {
	U_32 clazz;
	U_32 size;
#if defined(J9VM_ENV_DATA64)
	void *dataAddr;
#endif /* J9VM_ENV_DATA64 */
} J9IndexableObjectContiguousCompressed;

typedef struct J9IndexableObjectContiguousFull {
	UDATA clazz;
	U_32 size;
#if defined(J9VM_ENV_DATA64)
	U_32 padding;
	void *dataAddr;
#endif /* J9VM_ENV_DATA64 */
} J9IndexableObjectContiguousFull;

typedef struct J9IndexableObjectDiscontiguous {
	j9objectclass_t clazz;
	U_32 mustBeZero;
	U_32 size;
#if defined(OMR_GC_COMPRESSED_POINTERS) || !defined(J9VM_ENV_DATA64)
	U_32 padding;
#endif /* OMR_GC_COMPRESSED_POINTERS || !J9VM_ENV_DATA64 */
#if defined(J9VM_ENV_DATA64)
	void *dataAddr;
#endif /* J9VM_ENV_DATA64 */
} J9IndexableObjectDiscontiguous;

typedef struct J9IndexableObjectDiscontiguousCompressed {
	U_32 clazz;
	U_32 mustBeZero;
	U_32 size;
	U_32 padding;
#if defined(J9VM_ENV_DATA64)
	void *dataAddr;
#endif /* J9VM_ENV_DATA64 */
} J9IndexableObjectDiscontiguousCompressed;

typedef struct J9IndexableObjectDiscontiguousFull {
	UDATA clazz;
	U_32 mustBeZero;
	U_32 size;
#if !defined(J9VM_ENV_DATA64)
	U_32 padding;
#else /* J9VM_ENV_DATA64 */
	void *dataAddr;
#endif /* !J9VM_ENV_DATA64 */
} J9IndexableObjectDiscontiguousFull;

typedef struct J9InitializerMethods {
	struct J9Method* initialStaticMethod;
	struct J9Method* initialSpecialMethod;
	struct J9Method* initialVirtualMethod;
	struct J9Method* invokePrivateMethod;
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
	struct J9Method* throwDefaultConflict;
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
} J9InitializerMethods;

typedef struct J9VMInterface {
	struct VMInterfaceFunctions_* functions;
	struct J9JavaVM* javaVM;
	struct J9PortLibrary * portLibrary;
} J9VMInterface;

typedef struct J9CheckJNIData {
	UDATA options;
	struct J9HashTable* jniGlobalRefHashTab;
} J9CheckJNIData;

typedef struct J9AttachContext {
	struct j9shsem_handle* semaphore;
} J9AttachContext;

/* @ddr_namespace: map_to_type=J9ROMImageHeader */

typedef struct J9ROMImageHeader {
	U_32 idTag;
	U_32 flagsAndVersion;
	U_32 romSize;
	U_32 classCount;
	J9SRP jxePointer;
	J9SRP tableOfContents;
	J9SRP firstClass;
	J9SRP aotPointer;
	U_8 symbolFileID[16];
} J9ROMImageHeader;

#define J9ROMIMAGEHEADER_FLAGS_INTERPRETABLE  2
#define J9ROMIMAGEHEADER_FLAGS_REQUIRES_JCLS  8
#define J9ROMIMAGEHEADER_FLAGS_REQUIRES_TOOLS_EXT_DIR  16
#define J9ROMIMAGEHEADER_FLAGS_BIG_ENDIAN  1
#define J9ROMIMAGEHEADER_FLAGS_POSITION_INDEPENDENT  4

#define J9ROMIMAGEHEADER_JXEPOINTER(base) SRP_GET((base)->jxePointer, UDATA*)
#define J9ROMIMAGEHEADER_TABLEOFCONTENTS(base) NNSRP_GET((base)->tableOfContents, struct J9ROMClassTOCEntry*)
#define J9ROMIMAGEHEADER_FIRSTCLASS(base) NNSRP_GET((base)->firstClass, struct J9ROMClass*)
#define J9ROMIMAGEHEADER_AOTPOINTER(base) SRP_GET((base)->aotPointer, void*)

/* @ddr_namespace: map_to_type=J9ClassLocation */

struct J9Class;
typedef struct J9ClassLocation {
	struct J9Class *clazz;
	IDATA entryIndex;
	I_32 locationType;
} J9ClassLocation;

#define LOAD_LOCATION_MODULE_NON_GENERATED -3
#define LOAD_LOCATION_CLASSPATH_NON_GENERATED -2
#define LOAD_LOCATION_PATCH_PATH_NON_GENERATED -1
#define LOAD_LOCATION_UNKNOWN 0
#define LOAD_LOCATION_PATCH_PATH 1
#define LOAD_LOCATION_CLASSPATH 2
#define LOAD_LOCATION_MODULE 3

typedef struct J9Class {
	UDATA eyecatcher;
	struct J9ROMClass* romClass;
	struct J9Class** superclasses;
	UDATA classDepthAndFlags;
	U_32 classDepthWithFlags;
	U_32 classFlags;
	struct J9ClassLoader* classLoader;
	j9object_t classObject;
	UDATA volatile initializeStatus;
	struct J9Method* ramMethods;
	UDATA* ramStatics;
	struct J9Class* arrayClass;
	UDATA totalInstanceSize;
	struct J9ITable* lastITable;
	UDATA* instanceDescription;
#if defined(J9VM_GC_LEAF_BITS)
	UDATA* instanceLeafDescription;
#endif /* J9VM_GC_LEAF_BITS */
	UDATA instanceHotFieldDescription;
	UDATA selfReferencingField1;
	UDATA selfReferencingField2;
	struct J9Method* initializerCache;
	UDATA romableAotITable;
	UDATA packageID;
	struct J9Module *module;
	struct J9Class* subclassTraversalLink;
	struct J9Class* subclassTraversalReverseLink;
	void** iTable;
	UDATA castClassCache;
	void** jniIDs;
	UDATA lockOffset;
#if defined(J9VM_ENV_DATA64)
	U_32 paddingForGLRCounters; /* This is used to preserve alignment under 64 bit. */
#endif /* defined(J9VM_ENV_DATA64) */
	U_16 reservedCounter;
	U_16 cancelCounter;
	UDATA newInstanceCount;
	IDATA backfillOffset;
	struct J9Class* replacedClass;
	UDATA finalizeLinkOffset;
	struct J9Class* nextClassInSegment;
	UDATA* ramConstantPool;
	j9object_t* callSites;
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
	j9object_t* invokeCache;
#else /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
	j9object_t* methodTypes;
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
	j9object_t* varHandleMethodTypes;
#if defined(J9VM_INTERP_CUSTOM_SPIN_OPTIONS)
	struct J9VMCustomSpinOptions *customSpinOption;
#endif /* J9VM_INTERP_CUSTOM_SPIN_OPTIONS */
	struct J9Method** staticSplitMethodTable;
	struct J9Method** specialSplitMethodTable;
	struct J9JITExceptionTable* jitMetaDataList;
	struct J9Class* gcLink;
	struct J9Class* hostClass;
#if JAVA_SPEC_VERSION >= 11
	struct J9Class* nestHost;
#endif /* JAVA_SPEC_VERSION >= 11 */
	struct J9FlattenedClassCache* flattenedClassCache;
	struct J9ClassHotFieldsInfo* hotFieldsInfo;
} J9Class;

/* Interface classes can never be instantiated, so the following fields in J9Class will not be used:
 *
 *	totalInstanceSize -> map to iTable method count
 *	finalizeLinkOffset -> map to interface method ordering table
 */
#define J9INTERFACECLASS_ITABLEMETHODCOUNT(clazz) ((clazz)->totalInstanceSize)
#define J9INTERFACECLASS_SET_ITABLEMETHODCOUNT(clazz, value) (clazz)->totalInstanceSize = (value)
#define J9INTERFACECLASS_METHODORDERING(clazz) ((U_32*)((clazz)->finalizeLinkOffset))
#define J9INTERFACECLASS_SET_METHODORDERING(clazz, value) (clazz)->finalizeLinkOffset = (UDATA)(value)

#define J9ARRAYCLASS_SET_STRIDE(clazz, strideLength) ((clazz)->flattenedClassCache) = (J9FlattenedClassCache*)(UDATA)(strideLength)
#define J9ARRAYCLASS_GET_STRIDE(clazz) ((UDATA)((clazz)->flattenedClassCache))

#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
#define J9CLASS_HAS_REFERENCES(clazz) (J9_ARE_ALL_BITS_SET((clazz)->classFlags, J9ClassHasReferences))
#define J9CLASS_HAS_4BYTE_PREPADDING(clazz) (J9_ARE_ALL_BITS_SET((clazz)->classFlags, J9ClassRequiresPrePadding))
#define J9CLASS_PREPADDING_SIZE(clazz) (J9CLASS_HAS_4BYTE_PREPADDING((clazz)) ? sizeof(U_32) : 0)
#else
#define J9CLASS_HAS_REFERENCES(clazz) TRUE
#define J9CLASS_HAS_4BYTE_PREPADDING(clazz) FALSE
#define J9CLASS_PREPADDING_SIZE(clazz) 0
#endif /* defined(J9VM_OPT_VALHALLA_VALUE_TYPES) */

typedef struct J9ArrayClass {
	UDATA eyecatcher;
	struct J9ROMClass* romClass;
	struct J9Class** superclasses;
	UDATA classDepthAndFlags;
	U_32 classDepthWithFlags;
	U_32 classFlags;
	struct J9ClassLoader* classLoader;
	j9object_t classObject;
	UDATA volatile initializeStatus;
	struct J9Class* leafComponentType;
	UDATA arity;
	struct J9Class* arrayClass;
	struct J9Class* componentType;
	struct J9ITable* lastITable;
	UDATA* instanceDescription;
#if defined(J9VM_GC_LEAF_BITS)
	UDATA* instanceLeafDescription;
#endif /* J9VM_GC_LEAF_BITS */
	UDATA instanceHotFieldDescription;
	UDATA selfReferencingField1;
	UDATA selfReferencingField2;
	struct J9Method* initializerCache;
	UDATA romableAotITable;
	UDATA packageID;
	struct J9Module *module;
	struct J9Class* subclassTraversalLink;
	struct J9Class* subclassTraversalReverseLink;
	void** iTable;
	UDATA castClassCache;
	void** jniIDs;
	UDATA lockOffset;
#if defined(J9VM_ENV_DATA64)
	U_32 paddingForGLRCounters; /* This is used to preserve alignment under 64 bit. */
#endif /* defined(J9VM_ENV_DATA64) */
	U_16 reservedCounter;
	U_16 cancelCounter;
	UDATA newInstanceCount;
	IDATA backfillOffset;
	struct J9Class* replacedClass;
	UDATA finalizeLinkOffset;
	struct J9Class* nextClassInSegment;
	UDATA* ramConstantPool;
	j9object_t* callSites;
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
	j9object_t* invokeCache;
#else /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
	j9object_t* methodTypes;
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
	j9object_t* varHandleMethodTypes;
#if defined(J9VM_INTERP_CUSTOM_SPIN_OPTIONS)
	struct J9VMCustomSpinOptions *customSpinOption;
#endif /* J9VM_INTERP_CUSTOM_SPIN_OPTIONS */
	struct J9Method** staticSplitMethodTable;
	struct J9Method** specialSplitMethodTable;
	struct J9JITExceptionTable* jitMetaDataList;
	struct J9Class* gcLink;
	struct J9Class* hostClass;
#if JAVA_SPEC_VERSION >= 11
	struct J9Class* nestHost;
#endif /* JAVA_SPEC_VERSION >= 11 */
	/* Added temporarily for consistency */
	UDATA flattenedElementSize;
	struct J9ClassHotFieldsInfo* hotFieldsInfo;
} J9ArrayClass;


#if defined(LINUX) && defined(J9VM_ARCH_X86) && !defined(OSX)
/* Only enable the assert on linux x86 as not all the versions
 * of the compilers we use (xlc) support static_asserts in on
 * all the platforms.
 */
#if defined(__cplusplus)
/* Hide the asserts from DDR */
#if !defined(TYPESTUBS_H)
static_assert(sizeof(J9Class) == sizeof(J9ArrayClass), "J9Class and J9ArrayClass must be the same size");
#endif /* !TYPESTUBS_H */
#endif /* __cplusplus */
#endif /* LINUX && J9VM_ARCH_X86 */

typedef struct J9HookedNative {
	struct J9NativeLibrary* nativeLibrary;
	UDATA returnType;
	char* argTypes;
	void* jniFunction;
	UDATA userdata;
} J9HookedNative;

/* @ddr_namespace: map_to_type=J9ClassLoader */

typedef struct J9ClassLoader {
	struct J9Pool* sharedLibraries;
	struct J9HashTable* classHashTable;
	struct J9HashTable* romClassOrphansHashTable;
	j9object_t classLoaderObject;
	struct J9ClassPathEntry* classPathEntries;
	UDATA classPathEntryCount;
	struct J9ClassLoader* unloadLink;
	struct J9ClassLoader* gcLinkNext;
	struct J9ClassLoader* gcLinkPrevious;
	UDATA gcFlags;
	struct J9VMThread* gcThreadNotification;
	struct J9Pool* jniIDs;
	UDATA flags;
#if defined(J9VM_NEEDS_JNI_REDIRECTION)
	struct J9JNIRedirectionBlock* jniRedirectionBlocks;
#endif /* J9VM_NEEDS_JNI_REDIRECTION */
	struct J9JITExceptionTable* jitMetaDataList;
	struct J9MemorySegment* classSegments;
	struct J9RAMClassFreeListBlock* ramClassLargeBlockFreeList;
	struct J9RAMClassFreeListBlock* ramClassSmallBlockFreeList;
	struct J9RAMClassFreeListBlock* ramClassTinyBlockFreeList;
	UDATA* ramClassUDATABlockFreeList;
	struct J9HashTable* redefinedClasses;
	struct J9NativeLibrary* librariesHead;
	struct J9NativeLibrary* librariesTail;
	volatile UDATA gcRememberedSet;
	struct J9HashTable* moduleHashTable;
	struct J9HashTable* packageHashTable;
	struct J9HashTable* moduleExtraInfoHashTable;
	struct J9HashTable* classLocationHashTable;
	struct J9HashTable* classRelationshipsHashTable;
	struct J9Pool* hotFieldPool;
	omrthread_monitor_t hotFieldPoolMutex; 
} J9ClassLoader;

#define J9CLASSLOADER_SHARED_CLASSES_ENABLED  8
#define J9CLASSLOADER_SUBSET_VISIBILITY  64
#define J9CLASSLOADER_PARALLEL_CAPABLE  0x100
#define J9CLASSLOADER_ANON_CLASS_LOADER  0x400
#define J9CLASSLOADER_CONTAINS_METHODS_PRESENT_IN_MCC_HASH  32
#define J9CLASSLOADER_CONTAINS_JXES  1
#define J9CLASSLOADER_INVARIANTS_SHARABLE  4
#define J9CLASSLOADER_CLASSPATH_SET  2
#define J9CLASSLOADER_CONTAINS_JITTED_METHODS  16

#define J9CLASSLOADER_CLASSLOADEROBJECT(currentThread, object) J9VMTHREAD_JAVAVM(currentThread)->memoryManagerFunctions->j9gc_objaccess_readObjectFromInternalVMSlot((currentThread), J9VMTHREAD_JAVAVM(currentThread), (j9object_t*)&((object)->classLoaderObject))
#define J9CLASSLOADER_SET_CLASSLOADEROBJECT(currentThread, object, value) J9VMTHREAD_JAVAVM(currentThread)->memoryManagerFunctions->j9gc_objaccess_storeObjectToInternalVMSlot((currentThread), (j9object_t*)&((object)->classLoaderObject), (value))
#define TMP_J9CLASSLOADER_CLASSLOADEROBJECT(object) ((object)->classLoaderObject)

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4200)
#endif /* defined(_MSC_VER) */

typedef struct J9UTF8 {
	U_16 length;
	U_8 data[];
} J9UTF8;

#if defined(_MSC_VER)
#pragma warning(pop)
#endif /* defined(_MSC_VER) */

typedef struct J9ROMClass {
	U_32 romSize;
	U_32 singleScalarStaticCount;
	J9SRP className;
	J9SRP superclassName;
	U_32 modifiers;
	U_32 extraModifiers;
	U_32 interfaceCount;
	J9SRP interfaces;
	U_32 romMethodCount;
	J9SRP romMethods;
	U_32 romFieldCount;
	J9SRP romFields;
	U_32 objectStaticCount;
	U_32 doubleScalarStaticCount;
	U_32 ramConstantPoolCount;
	U_32 romConstantPoolCount;
	J9WSRP intermediateClassData;
	U_32 intermediateClassDataLength;
	U_32 instanceShape;
	J9SRP cpShapeDescription;
	J9SRP outerClassName;
	U_32 memberAccessFlags;
	U_32 innerClassCount;
	J9SRP innerClasses;
#if JAVA_SPEC_VERSION >= 11
	J9SRP nestHost;
	U_16 nestMemberCount;
	U_16 unused;
	J9SRP nestMembers;
#endif /* JAVA_SPEC_VERSION >= 11 */
	U_16 majorVersion;
	U_16 minorVersion;
	U_32 optionalFlags;
	J9SRP optionalInfo;
	U_32 maxBranchCount;
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
	U_32 invokeCacheCount;
#else /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
	U_32 methodTypeCount;
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
	U_32 varHandleMethodTypeCount;
	U_32 bsmCount;
	U_32 callSiteCount;
	J9SRP callSiteData;
	U_32 classFileSize;
	U_32 classFileCPCount;
	U_16 staticSplitMethodRefCount;
	U_16 specialSplitMethodRefCount;
	J9SRP staticSplitMethodRefIndexes;
	J9SRP specialSplitMethodRefIndexes;
	J9SRP varHandleMethodTypeLookupTable;
#if defined(J9VM_ENV_DATA64)
#if JAVA_SPEC_VERSION >= 11
	U_32 padding;
#endif /* JAVA_SPEC_VERSION >= 11 */
#else /* J9VM_ENV_DATA64 */
#if JAVA_SPEC_VERSION < 11
	U_32 padding;
#endif /* JAVA_SPEC_VERSION < 11 */
#endif /* J9VM_ENV_DATA64 */
} J9ROMClass;

#define J9ROMCLASS_CLASSNAME(base) NNSRP_GET((base)->className, struct J9UTF8*)
#define J9ROMCLASS_SUPERCLASSNAME(base) SRP_GET((base)->superclassName, struct J9UTF8*)
#define J9ROMCLASS_INTERFACES(base) NNSRP_GET((base)->interfaces, J9SRP*)
#define J9ROMCLASS_ROMMETHODS(base) NNSRP_GET((base)->romMethods, struct J9ROMMethod*)
#define J9ROMCLASS_ROMFIELDS(base) NNSRP_GET((base)->romFields, struct J9ROMFieldShape*)
#define J9ROMCLASS_INTERMEDIATECLASSDATA(base) WSRP_GET((base)->intermediateClassData, U_8*)
#define J9ROMCLASS_CPSHAPEDESCRIPTION(base) NNSRP_GET((base)->cpShapeDescription, U_32*)
#define J9ROMCLASS_OUTERCLASSNAME(base) SRP_GET((base)->outerClassName, struct J9UTF8*)
#define J9ROMCLASS_INNERCLASSES(base) NNSRP_GET((base)->innerClasses, J9SRP*)
#if JAVA_SPEC_VERSION >= 11
#define J9ROMCLASS_NESTHOSTNAME(base) SRP_GET((base)->nestHost, struct J9UTF8*)
#define J9ROMCLASS_NESTMEMBERS(base) SRP_GET((base)->nestMembers, J9SRP*)
#endif /* JAVA_SPEC_VERSION >= 11 */
#define J9ROMCLASS_OPTIONALINFO(base) SRP_GET((base)->optionalInfo, U_32*)
#define J9ROMCLASS_CALLSITEDATA(base) SRP_GET((base)->callSiteData, U_8*)
#define J9ROMCLASS_STATICSPLITMETHODREFINDEXES(base) SRP_GET((base)->staticSplitMethodRefIndexes, U_16*)
#define J9ROMCLASS_SPECIALSPLITMETHODREFINDEXES(base) SRP_GET((base)->specialSplitMethodRefIndexes, U_16*)

typedef struct J9ROMArrayClass {
	U_32 romSize;
	U_32 singleScalarStaticCount;
	J9SRP className;
	J9SRP superclassName;
	U_32 modifiers;
	U_32 extraModifiers;
	U_32 interfaceCount;
	J9SRP interfaces;
	U_32 romMethodCount;
	U_32 arrayShape;
	U_32 romFieldCount;
	J9SRP romFields;
	U_32 objectStaticCount;
	U_32 doubleScalarStaticCount;
	U_32 ramConstantPoolCount;
	U_32 romConstantPoolCount;
	J9WSRP intermediateClassData;
	U_32 intermediateClassDataLength;
	U_32 instanceShape;
	J9SRP cpShapeDescription;
	J9SRP outerClassName;
	U_32 memberAccessFlags;
	U_32 innerClassCount;
	J9SRP innerClasses;
#if JAVA_SPEC_VERSION >= 11
	J9SRP nestHost;
	U_16 nestMemberCount;
	U_16 unused;
	J9SRP nestMembers;
#endif /* JAVA_SPEC_VERSION >= 11 */
	U_16 majorVersion;
	U_16 minorVersion;
	U_32 optionalFlags;
	J9SRP optionalInfo;
	U_32 maxBranchCount;
	U_32 methodTypeCount;
	U_32 varHandleMethodTypeCount;
	U_32 bsmCount;
	U_32 callSiteCount;
	J9SRP callSiteData;
	U_32 classFileSize;
	U_32 classFileCPCount;
	U_16 staticSplitMethodRefCount;
	U_16 specialSplitMethodRefCount;
	J9SRP staticSplitMethodRefIndexes;
	J9SRP specialSplitMethodRefIndexes;
	J9SRP varHandleMethodTypeLookupTable;
#if defined(J9VM_ENV_DATA64)
#if JAVA_SPEC_VERSION >= 11
	U_32 padding;
#endif /* JAVA_SPEC_VERSION >= 11 */
#else /* J9VM_ENV_DATA64 */
#if JAVA_SPEC_VERSION < 11
	U_32 padding;
#endif /* JAVA_SPEC_VERSION < 11 */
#endif /* J9VM_ENV_DATA64 */
} J9ROMArrayClass;

#define J9ROMARRAYCLASS_CLASSNAME(base) NNSRP_GET((base)->className, struct J9UTF8*)
#define J9ROMARRAYCLASS_SUPERCLASSNAME(base) SRP_GET((base)->superclassName, struct J9UTF8*)
#define J9ROMARRAYCLASS_INTERFACES(base) NNSRP_GET((base)->interfaces, J9SRP*)
#define J9ROMARRAYCLASS_ROMFIELDS(base) NNSRP_GET((base)->romFields, struct J9ROMFieldShape*)
#define J9ROMARRAYCLASS_INTERMEDIATECLASSDATA(base) WSRP_GET((base)->intermediateClassData, U_8*)
#define J9ROMARRAYCLASS_CPSHAPEDESCRIPTION(base) NNSRP_GET((base)->cpShapeDescription, U_32*)
#define J9ROMARRAYCLASS_OUTERCLASSNAME(base) SRP_GET((base)->outerClassName, struct J9UTF8*)
#define J9ROMARRAYCLASS_INNERCLASSES(base) NNSRP_GET((base)->innerClasses, J9SRP*)
#define J9ROMARRAYCLASS_OPTIONALINFO(base) SRP_GET((base)->optionalInfo, U_32*)
#define J9ROMARRAYCLASS_CALLSITEDATA(base) SRP_GET((base)->callSiteData, U_8*)
#define J9ROMARRAYCLASS_STATICSPLITMETHODREFINDEXES(base) SRP_GET((base)->staticSplitMethodRefIndexes, U_16*)
#define J9ROMARRAYCLASS_SPECIALSPLITMETHODREFINDEXES(base) SRP_GET((base)->specialSplitMethodRefIndexes, U_16*)

typedef struct J9ROMReflectClass {
	U_32 romSize;
	U_32 singleScalarStaticCount;
	J9SRP className;
	J9SRP superclassName;
	U_32 modifiers;
	U_32 extraModifiers;
	U_32 interfaceCount;
	J9SRP interfaces;
	U_32 romMethodCount;
	U_32 reflectTypeCode;
	U_32 romFieldCount;
	U_32 elementSize;
	U_32 objectStaticCount;
	U_32 doubleScalarStaticCount;
	U_32 ramConstantPoolCount;
	U_32 romConstantPoolCount;
	J9WSRP intermediateClassData;
	U_32 intermediateClassDataLength;
	U_32 instanceShape;
	J9SRP cpShapeDescription;
	J9SRP outerClassName;
	U_32 memberAccessFlags;
	U_32 innerClassCount;
	J9SRP innerClasses;
#if JAVA_SPEC_VERSION >= 11
	J9SRP nestHost;
	U_16 nestMemberCount;
	U_16 unused;
	J9SRP nestMembers;
#endif /* JAVA_SPEC_VERSION >= 11 */
	U_16 majorVersion;
	U_16 minorVersion;
	U_32 optionalFlags;
	J9SRP optionalInfo;
	U_32 maxBranchCount;
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
	U_32 invokeCacheCount;
#else /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
	U_32 methodTypeCount;
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
	U_32 varHandleMethodTypeCount;
	U_32 bsmCount;
	U_32 callSiteCount;
	J9SRP callSiteData;
	U_32 classFileSize;
	U_32 classFileCPCount;
	U_16 staticSplitMethodRefCount;
	U_16 specialSplitMethodRefCount;
	J9SRP staticSplitMethodRefIndexes;
	J9SRP specialSplitMethodRefIndexes;
	J9SRP varHandleMethodTypeLookupTable;
#if defined(J9VM_ENV_DATA64)
#if JAVA_SPEC_VERSION >= 11
	U_32 padding;
#endif /* JAVA_SPEC_VERSION >= 11 */
#else /* J9VM_ENV_DATA64 */
#if JAVA_SPEC_VERSION < 11
	U_32 padding;
#endif /* JAVA_SPEC_VERSION < 11 */
#endif /* J9VM_ENV_DATA64 */
} J9ROMReflectClass;

#define J9ROMREFLECTCLASS_CLASSNAME(base) NNSRP_GET((base)->className, struct J9UTF8*)
#define J9ROMREFLECTCLASS_SUPERCLASSNAME(base) SRP_GET((base)->superclassName, struct J9UTF8*)
#define J9ROMREFLECTCLASS_INTERFACES(base) NNSRP_GET((base)->interfaces, J9SRP*)
#define J9ROMREFLECTCLASS_INTERMEDIATECLASSDATA(base) WSRP_GET((base)->intermediateClassData, U_8*)
#define J9ROMREFLECTCLASS_CPSHAPEDESCRIPTION(base) NNSRP_GET((base)->cpShapeDescription, U_32*)
#define J9ROMREFLECTCLASS_OUTERCLASSNAME(base) SRP_GET((base)->outerClassName, struct J9UTF8*)
#define J9ROMREFLECTCLASS_INNERCLASSES(base) NNSRP_GET((base)->innerClasses, J9SRP*)
#define J9ROMREFLECTCLASS_OPTIONALINFO(base) SRP_GET((base)->optionalInfo, U_32*)
#define J9ROMREFLECTCLASS_CALLSITEDATA(base) SRP_GET((base)->callSiteData, U_8*)
#define J9ROMREFLECTCLASS_STATICSPLITMETHODREFINDEXES(base) SRP_GET((base)->staticSplitMethodRefIndexes, U_16*)
#define J9ROMREFLECTCLASS_SPECIALSPLITMETHODREFINDEXES(base) SRP_GET((base)->specialSplitMethodRefIndexes, U_16*)

typedef struct J9ROMMethod {
	struct J9ROMNameAndSignature nameAndSignature;
	U_32 modifiers;
	U_16 maxStack;
	U_16 bytecodeSizeLow;
	U_8 bytecodeSizeHigh;
	U_8 argCount;
	U_16 tempCount;
} J9ROMMethod;

#define J9ROMMETHOD_NAMEANDSIGNATURE(base) (&((base)->nameAndSignature))
#define J9ROMMETHOD_NAME(base) NNSRP_GET((&((base)->nameAndSignature))->name, struct J9UTF8*)
#define J9ROMMETHOD_SIGNATURE(base) NNSRP_GET((&((base)->nameAndSignature))->signature, struct J9UTF8*)

/* @ddr_namespace: map_to_type=J9JNIReferenceFrame */

typedef struct J9JNIReferenceFrame {
	UDATA type;
	struct J9JNIReferenceFrame* previous;
	void* references;
} J9JNIReferenceFrame;

#define JNIFRAME_TYPE_USER  1
#define JNIFRAME_TYPE_INTERNAL  0

typedef struct J9Method {
	U_8* bytecodes;
	struct J9ConstantPool* constantPool;
	void* methodRunAddress;

	/**
	 * This field is overloaded and can have several meanings:
	 * 
	 * 1. If the J9_STARTPC_NOT_TRANSLATED bit is set, this method is interpreted
	 *    a. If the value is non-negative, then the field contains the invocation count which is decremented towards
	 *    zero. When the invocation count reaches zero, the method will be queued for JIT compilation. The invocation
	 *    count is represented as:
	 * 
	 *    ```
	 *    (invocationCount << 1) | J9_STARTPC_NOT_TRANSLATED
	 *    ```
	 * 
	 *    b. If the method is a default method which has conflicts then this field contains the RAM method to execute.
	 *    That is, the extra field will contain:
	 * 
	 *    ```
	 *    ((J9Method*)<default method to execute>) | J9_STARTPC_NOT_TRANSLATED
	 *    ```
	 * 
	 *    c. If the value is negative, then it can be one of (see definition for documentation on these):
	 *        - J9_JIT_NEVER_TRANSLATE
	 *              The VM will not decrement the count and the method will stay interpreted
	 *        - J9_JIT_QUEUED_FOR_COMPILATION
	 *              The method has been queued for JIT compilation
	 * 
	 * 2. If the J9_STARTPC_NOT_TRANSLATED bit is not set, this method is JIT compiled
	 * 		a. The field contains the address of the start PC of the JIT compiled method
	 */
	void* volatile extra;
} J9Method;

typedef struct J9JNIMethodID {
	struct J9Method* method;
	UDATA vTableIndex;
} J9JNIMethodID;

typedef struct J9JNIFieldID {
	UDATA index;
	struct J9ROMFieldShape* field;
	UDATA offset;
	struct J9Class* declaringClass;
} J9JNIFieldID;

typedef struct J9ITable {
	struct J9Class* interfaceClass;
	UDATA depth;
	struct J9ITable* next;
} J9ITable;

typedef struct J9VTableHeader {
	UDATA size;
	J9Method* initialVirtualMethod;
	J9Method* invokePrivateMethod;
} J9VTableHeader;

typedef struct J9ClassCastParms {
	struct J9Class* instanceClass;
	struct J9Class* castClass;
} J9ClassCastParms;

/* @ddr_namespace: map_to_type=J9JITConfig */

typedef struct J9JITConfig {
	IDATA  ( *entryPoint)(struct J9JITConfig *jitConfig, struct J9VMThread *vmStruct, J9Method *method, void *oldStartPC) ;
	void *old_fast_jitNewObject;
	void *old_slow_jitNewObject;
	void *old_fast_jitNewObjectNoZeroInit;
	void *old_slow_jitNewObjectNoZeroInit;
	void *old_fast_jitANewArray;
	void *old_slow_jitANewArray;
	void *old_fast_jitANewArrayNoZeroInit;
	void *old_slow_jitANewArrayNoZeroInit;
	void *old_fast_jitNewArray;
	void *old_slow_jitNewArray;
	void *old_fast_jitNewArrayNoZeroInit;
	void *old_slow_jitNewArrayNoZeroInit;
	void *old_slow_jitAMultiNewArray;
	void *old_slow_jitStackOverflow;
	void *old_slow_jitResolveString;
	void *fast_jitAcquireVMAccess;
	void *fast_jitReleaseVMAccess;
	void *old_slow_jitCheckAsyncMessages;
	void *old_fast_jitCheckCast;
	void *old_slow_jitCheckCast;
	void *old_fast_jitCheckCastForArrayStore;
	void *old_slow_jitCheckCastForArrayStore;
	void *old_fast_jitCheckIfFinalizeObject;
	void *old_fast_jitCollapseJNIReferenceFrame;
	void *old_slow_jitHandleArrayIndexOutOfBoundsTrap;
	void *old_slow_jitHandleIntegerDivideByZeroTrap;
	void *old_slow_jitHandleNullPointerExceptionTrap;
	void *old_slow_jitHandleInternalErrorTrap;
	void *old_fast_jitInstanceOf;
	void *old_fast_jitLookupInterfaceMethod;
	void *old_slow_jitLookupInterfaceMethod;
	void *old_fast_jitMethodIsNative;
	void *old_fast_jitMethodIsSync;
	void *old_fast_jitMethodMonitorEntry;
	void *old_slow_jitMethodMonitorEntry;
	void *old_fast_jitMonitorEntry;
	void *old_slow_jitMonitorEntry;
	void *old_fast_jitMethodMonitorExit;
	void *old_slow_jitMethodMonitorExit;
	void *old_slow_jitThrowIncompatibleReceiver;
	void *old_fast_jitMonitorExit;
	void *old_slow_jitMonitorExit;
	void *old_slow_jitReportMethodEnter;
	void *old_slow_jitReportStaticMethodEnter;
	void *old_slow_jitReportMethodExit;
	void *old_slow_jitResolveClass;
	void *old_slow_jitResolveClassFromStaticField;
	void *old_fast_jitResolvedFieldIsVolatile;
	void *old_slow_jitResolveField;
	void *old_slow_jitResolveFieldSetter;
	void *old_slow_jitResolveFieldDirect;
	void *old_slow_jitResolveFieldSetterDirect;
	void *old_slow_jitResolveStaticField;
	void *old_slow_jitResolveStaticFieldSetter;
	void *old_slow_jitResolveStaticFieldDirect;
	void *old_slow_jitResolveStaticFieldSetterDirect;
	void *old_slow_jitResolveInterfaceMethod;
	void *old_slow_jitResolveSpecialMethod;
	void *old_slow_jitResolveStaticMethod;
	void *old_slow_jitResolveVirtualMethod;
	void *old_slow_jitResolveMethodType;
	void *old_slow_jitResolveMethodHandle;
	void *old_slow_jitResolveInvokeDynamic;
	void *old_slow_jitResolveConstantDynamic;
	void *old_slow_jitResolveHandleMethod;
	void *old_slow_jitResolveFlattenableField;
	void *old_slow_jitRetranslateCaller;
	void *old_slow_jitRetranslateCallerWithPreparation;
	void *old_slow_jitRetranslateMethod;
	void *old_slow_jitThrowCurrentException;
	void *old_slow_jitThrowException;
	void *old_slow_jitThrowUnreportedException;
	void *old_slow_jitThrowAbstractMethodError;
	void *old_slow_jitThrowArithmeticException;
	void *old_slow_jitThrowArrayIndexOutOfBounds;
	void *old_slow_jitThrowArrayStoreException;
	void *old_slow_jitThrowArrayStoreExceptionWithIP;
	void *old_slow_jitThrowExceptionInInitializerError;
	void *old_slow_jitThrowIllegalAccessError;
	void *old_slow_jitThrowIncompatibleClassChangeError;
	void *old_slow_jitThrowInstantiationException;
	void *old_slow_jitThrowNullPointerException;
	void *old_slow_jitThrowWrongMethodTypeException;
	void *old_fast_jitTypeCheckArrayStoreWithNullCheck;
	void *old_slow_jitTypeCheckArrayStoreWithNullCheck;
	void *old_fast_jitTypeCheckArrayStore;
	void *old_slow_jitTypeCheckArrayStore;
	void *old_fast_jitWriteBarrierBatchStore;
	void *old_fast_jitWriteBarrierBatchStoreWithRange;
	void *old_fast_jitWriteBarrierJ9ClassBatchStore;
	void *old_fast_jitWriteBarrierJ9ClassStore;
	void *old_fast_jitWriteBarrierStore;
	void *old_fast_jitWriteBarrierStoreGenerational;
	void *old_fast_jitWriteBarrierStoreGenerationalAndConcurrentMark;
	void *old_fast_jitWriteBarrierClassStoreMetronome;
	void *old_fast_jitWriteBarrierStoreMetronome;
	void *old_slow_jitCallJitAddPicToPatchOnClassUnload;
	void *old_slow_jitCallCFunction;
	void *fast_jitPreJNICallOffloadCheck;
	void *fast_jitPostJNICallOffloadCheck;
	void *old_fast_jitObjectHashCode;
	void *old_slow_jitInduceOSRAtCurrentPC;
	void *old_slow_jitInduceOSRAtCurrentPCAndRecompile;
	void *old_slow_jitInterpretNewInstanceMethod;
	void *old_slow_jitNewInstanceImplAccessCheck;
	void *old_slow_jitTranslateNewInstanceMethod;
	void *old_slow_jitReportFinalFieldModified;
	void *old_fast_jitGetFlattenableField;
	void *old_fast_jitCloneValueType;
	void *old_fast_jitWithFlattenableField;
	void *old_fast_jitPutFlattenableField;
	void *old_fast_jitGetFlattenableStaticField;
	void *old_fast_jitPutFlattenableStaticField;
	void *old_fast_jitLoadFlattenableArrayElement;
	void *old_fast_jitStoreFlattenableArrayElement;
	void *old_fast_jitAcmpHelper;
	void *fast_jitNewValue;
	void *fast_jitNewValueNoZeroInit;
	void *fast_jitNewObject;
	void *fast_jitNewObjectNoZeroInit;
	void *fast_jitANewArray;
	void *fast_jitANewArrayNoZeroInit;
	void *fast_jitNewArray;
	void *fast_jitNewArrayNoZeroInit;
	void *fast_jitCheckCast;
	void *fast_jitCheckCastForArrayStore;
	void *fast_jitMethodMonitorEntry;
	void *fast_jitMonitorEntry;
	void *fast_jitMethodMonitorExit;
	void *fast_jitMonitorExit;
	void *fast_jitTypeCheckArrayStore;
	void *fast_jitTypeCheckArrayStoreWithNullCheck;
	void *old_fast_jitVolatileReadLong;
	void *old_fast_jitVolatileWriteLong;
	void *old_fast_jitVolatileReadDouble;
	void *old_fast_jitVolatileWriteDouble;
	void *old_slow_icallVMprJavaSendPatchupVirtual;
	void *c_jitDecompileOnReturn;
	void *c_jitDecompileAtExceptionCatch;
	void *c_jitReportExceptionCatch;
	void *c_jitDecompileAtCurrentPC;
	void *c_jitDecompileBeforeReportMethodEnter;
	void *c_jitDecompileBeforeMethodMonitorEnter;
	void *c_jitDecompileAfterAllocation;
	void *c_jitDecompileAfterMonitorEnter;
	void *old_slow_jitReportInstanceFieldRead;
	void *old_slow_jitReportInstanceFieldWrite;
	void *old_slow_jitReportStaticFieldRead;
	void *old_slow_jitReportStaticFieldWrite;
	struct J9MemorySegment* codeCache;
	struct J9MemorySegment* dataCache;
	struct J9MemorySegmentList* codeCacheList;
	struct J9JITCodeCacheTrampolineList* codeCacheTrampolines;
	struct J9MemorySegmentList* dataCacheList;
	struct J9JavaVM* javaVM;
	omrthread_monitor_t mutex;
	UDATA scratchSpacePageSize;
	char* logFileName;
	UDATA runtimeFlags;
	void* translationFilters;
	UDATA messageNumber;
	UDATA breakMessageNumber;
	UDATA messageThreshold;
	void* outOfMemoryJumpBuffer;
	UDATA translationFiltersFlags;
	UDATA lastGCDataAllocSize;
	UDATA lastExceptionTableAllocSize;
	UDATA gcCount;
	UDATA gcTraceThreshold;
	UDATA inlineSizeLimit;
	U_8* preScavengeAllocateHeapAlloc;
	U_8* preScavengeAllocateHeapBase;
	UDATA bcSizeLimit;
	struct J9AVLTree* translationArtifacts;
	UDATA gcOnResolveThreshold;
	struct J9VTuneInterface* vTuneInterface;
	void* patchupVirtual;
	void* jitSendTargetTable;
	struct J9MemorySegment* scratchSegment;
	void* jitToInterpreterThunks;
	struct J9HashTable* thunkHashTable;
	omrthread_monitor_t thunkHashTableMutex;
	void*  ( *thunkLookUpNameAndSig)(void * jitConfig, void *parm) ;
	UDATA maxInlineDepth;
	UDATA iprofilerBufferSize;
	UDATA codeCacheKB;
	UDATA dataCacheKB;
	UDATA codeCachePadKB;
	UDATA codeCacheTotalKB;
	UDATA dataCacheTotalKB;
	struct J9JITExceptionTable*  ( *jitGetExceptionTableFromPC)(struct J9VMThread * vmThread, UDATA jitPC) ;
	void*  ( *jitGetStackMapFromPC)(struct J9JavaVM * javaVM, struct J9JITExceptionTable * exceptionTable, UDATA jitPC) ;
	void*  ( *jitGetInlinerMapFromPC)(struct J9JavaVM * javaVM, struct J9JITExceptionTable * exceptionTable, UDATA jitPC) ;
	UDATA  ( *getJitInlineDepthFromCallSite)(struct J9JITExceptionTable *metaData, void *inlinedCallSite) ;
	void*  ( *getJitInlinedCallInfo)(struct J9JITExceptionTable * md) ;
	void*  ( *getStackMapFromJitPC)(struct J9JavaVM * javaVM, struct J9JITExceptionTable * exceptionTable, UDATA jitPC) ;
	void*  ( *getFirstInlinedCallSite)(struct J9JITExceptionTable * metaData, void * stackMap) ;
	void*  ( *getNextInlinedCallSite)(struct J9JITExceptionTable * metaData, void * inlinedCallSite) ;
	UDATA  ( *hasMoreInlinedMethods)(void * inlinedCallSite) ;
	void*  ( *getInlinedMethod)(void * inlinedCallSite) ;
	UDATA  ( *getByteCodeIndex)(void * inlinedCallSite) ;
	UDATA  ( *getByteCodeIndexFromStackMap)(struct J9JITExceptionTable *metaData, void *stackMap) ;
	U_32  ( *getJitRegisterMap)(struct J9JITExceptionTable *metadata, void * stackMap) ;
	UDATA  ( *getCurrentByteCodeIndexAndIsSameReceiver)(struct J9JITExceptionTable * methodMetaData, void * stackMap, void * currentInlinedCallSite, UDATA * isSameReceiver) ;
	void  ( *jitCodeBreakpointAdded)(struct J9VMThread * currentThread, J9Method * method) ;
	void  ( *jitCodeBreakpointRemoved)(struct J9VMThread * currentThread, J9Method * method) ;
	void  ( *jitDataBreakpointAdded)(struct J9VMThread * currentThread) ;
	void  ( *jitDataBreakpointRemoved)(struct J9VMThread * currentThread) ;
	void  ( *jitSingleStepAdded)(struct J9VMThread * currentThread) ;
	void  ( *jitSingleStepRemoved)(struct J9VMThread * currentThread) ;
	struct J9JITDecompilationInfo*  ( *jitCleanUpDecompilationStack)(struct J9VMThread * currentThread, J9StackWalkState * walkState, UDATA dropCurrentFrame) ;
	struct J9JITDecompilationInfo*  ( *jitAddDecompilationForFramePop)(struct J9VMThread * currentThread, J9StackWalkState * walkState) ;
	void  ( *jitHotswapOccurred)(struct J9VMThread * currentThread) ;
	void  ( *jitClassesRedefined)(struct J9VMThread * currentThread, UDATA classCount, struct J9JITRedefinedClass * classList) ;
	void  ( *jitFlushCompilationQueue)(struct J9VMThread * currentThread, J9JITFlushCompilationQueueReason reason) ;
	void  ( *jitDecompileMethodForFramePop)(struct J9VMThread * currentThread, UDATA skipCount) ;
	void  ( *jitExceptionCaught)(struct J9VMThread * currentThread) ;
	void  ( *j9jit_printf)(void *voidConfig, char *format, ...) ;
	void* tracingHook;
	void  ( *jitCheckScavengeOnResolve)(struct J9VMThread *currentThread) ;
	void* jitInstanceOf;
	void* jitWriteBarrierStore;
	void* jitWriteBarrierBatchStore;
	void* jitWriteBarrierBatchStoreWithRange;
	void* jitThrowArrayStoreExceptionWithIP;
	void* runJITHandler;
	void* performDLT;
	UDATA targetLittleEndian;
	UDATA codeCacheAlignment;
	UDATA samplingFrequency;
	UDATA samplingTickCount;
	UDATA globalSampleCount;
	UDATA sampleInterval;
	omrthread_monitor_t samplerMonitor;
	omrthread_t samplerThread;
	void* classLibAttributes;
	void*  ( *aotrt_getRuntimeHelper)(int helperNumber) ;
	UDATA  ( *aotrt_lookupSendTargetForThunk)(struct J9JavaVM * javaVM, int thunkNumber) ;
	struct J9JITBreakpointedMethod* breakpointedMethods;
	UDATA dataBreakpointCount;
	UDATA singleStepCount;
	UDATA  ( *entryPointForNewInstance)(struct J9JITConfig *jitConfig, struct J9VMThread *vmStruct, J9Class *aClass) ;
	char* vLogFileName;
	I_32 vLogFile;
	I_32 targetName;
	char* tLogFileName;
	I_32 tLogFile;
	I_32 tLogFileTemp;
	void* traceInfo;
	IDATA verboseOutputLevel;
	omrthread_monitor_t compilationMonitor;
	void* compilationInfo;
	void* aotCompilationInfo;
	void* pseudoTOC;
	void* i2jTransition;
	UDATA* codeCacheFreeList;
	void* methodsToDelete;
	struct J9Method* newInstanceImplMethod;
	UDATA jitFloatReturnUsage;
	void* processorInfo;
	void* i2jReturnTable;
	void* privateConfig;
	void  ( *jitExclusiveVMShutdownPending)(struct J9VMThread *vmThread) ;
	void* jitStatics;
	const char* jitLevelName;
	void  ( *jitHookAboutToRunMain)(struct J9VMThread * vmThread) ;
	struct J9JITHookInterface hookInterface;
	void  ( *disableJit)(struct J9JITConfig *jitConfig) ;
	void  ( *enableJit)(struct J9JITConfig *jitConfig) ;
	I_32  ( *command)(struct J9VMThread *currentThread, const char * cmdString) ;
	IDATA  ( *compileClass)(struct J9VMThread *jitConfig, jclass clazz) ;
	IDATA  ( *compileClasses)(struct J9VMThread *jitConfig, const char * pattern) ;
	UDATA fsdEnabled;
	UDATA inlineFieldWatches;
	void  ( *jitFramePopNotificationAdded)(struct J9VMThread * currentThread, J9StackWalkState * walkState, UDATA inlineDepth) ;
	void  ( *jitStackLocalsModified)(struct J9VMThread * currentThread, J9StackWalkState * walkState) ;
	void  ( *jitReportDynamicCodeLoadEvents)(struct J9VMThread * currentThread) ;
	UDATA  ( *jitSignalHandler)(struct J9VMThread *vmStruct, U_32 gpType, void* gpInfo) ;
	IDATA sampleInterruptHandlerKey;
	omr_error_t ( *runJitdump)(char *label, struct J9RASdumpContext *context, struct J9RASdumpAgent *agent);
	void*  ( *isDLTReady)(struct J9VMThread * currentThread, struct J9Method * method, UDATA bytecodeIndex) ;
	UDATA*  ( *jitLocalSlotAddress)(struct J9VMThread * currentThread, J9StackWalkState *walkState, UDATA slot, UDATA inlineDepth) ;
	void  ( *jitOSRPatchMethod)(struct J9VMThread * currentThread, struct J9JITExceptionTable * metaData, U_8 * pc) ;
	void  ( *jitOSRUnpatchMethod)(struct J9VMThread * currentThread, struct J9JITExceptionTable * metaData, U_8 * pc) ;
	void*  ( *jitOSRGetPatchPoint)(struct J9JITExceptionTable * metaData, void * stackMap) ;
	IDATA  ( *jitCanResumeAtOSRPoint)(struct J9VMThread * currentThread, struct J9JITExceptionTable * metaData, U_8 * pc) ;
	IDATA  ( *retranslateWithPreparation)(struct J9JITConfig *jitConfig, struct J9VMThread *vmStruct, J9Method *method, void *oldStartPC, UDATA reason) ;
	IDATA  ( *retranslateWithPreparationForMethodRedefinition)(struct J9JITConfig *jitConfig, struct J9VMThread *vmStruct, J9Method *method, void *oldStartPC) ;
	void* j2iInvokeWithArguments;
	void*  ( *translateMethodHandle)(struct J9VMThread *currentThread, j9object_t methodHandle, j9object_t arg, U_32 flags) ;
	void* i2jMHTransition;
	IDATA  ( *isAESSupportedByHardware)(struct J9VMThread *currentThread) ;
	IDATA  ( *expandAESKeyInHardware)(char *rawKeys, int *rkeys, I_32 Nr) ;
	IDATA  ( *doAESInHardware)(char *inByteArray, I_32 inOffset, I_32 inLength, char *outByteArray, I_32 outOffset, int *rkeysIntArray, I_32 Nr, I_32 encrypt) ;
	IDATA  ( *getCryptoHardwareFeatures)(struct J9VMThread *currentThread) ;
	void  ( *doSha256InHardware)(char *inByteArray) ;
	void  ( *doSha512InHardware)(char *inByteArray) ;
	UDATA largeCodePageSize;
	UDATA largeCodePageFlags;
	UDATA osrFramesMaximumSize;
	UDATA osrScratchBufferMaximumSize;
	UDATA osrStackFrameMaximumSize;
	void* jitFillOSRBufferReturn;
	IDATA  ( *launchGPU)(struct J9VMThread *vmThread, jobject invokeObject, J9Method *method, int deviceId, I_32 gridDimX, I_32 gridDimY, I_32 gridDimZ, I_32 blockDimX, I_32 blockDimY, I_32 blockDimZ, void **args) ;
	void* jitExitInterpreter0;
	void* jitExitInterpreter1;
	void* jitExitInterpreterF;
	void* jitExitInterpreterD;
	void* jitExitInterpreterJ;
	void* loadPreservedAndBranch;
	void*  ( *setUpForDLT)(struct J9VMThread * currentThread, J9StackWalkState * walkState) ;
	void* b_i2jTransition;
	void  ( *promoteGPUCompile)(struct J9VMThread * currentThread) ;
	void ( *jitDiscardPendingCompilationsOfNatives)(struct J9VMThread *currentThread, J9Class *clazz) ;
	J9Method* ( *jitGetExceptionCatcher)(struct J9VMThread *currentThread, void *handlerPC, struct J9JITExceptionTable *metaData, IDATA *location) ;
	void ( *jitMethodBreakpointed)(struct J9VMThread *currentThread, struct J9Method *method) ;
	void ( *jitMethodUnbreakpointed)(struct J9VMThread *currentThread, struct J9Method *method) ;
	void ( *jitIllegalFinalFieldModification)(struct J9VMThread *currentThread, struct J9Class *fieldClass);
#if defined(J9VM_OPT_OPENJDK_METHODHANDLE)
	void ( *jitSetMutableCallSiteTarget)(struct J9VMThread *vmThread, j9object_t mcs, j9object_t newTarget) ;
#endif /* J9VM_OPT_OPENJDK_METHODHANDLE */
	U_8* (*codeCacheWarmAlloc)(void *codeCache);
	U_8* (*codeCacheColdAlloc)(void *codeCache);
	void ( *printAOTHeaderProcessorFeatures)(struct TR_AOTHeader * aotHeaderAddress, char * buff, const size_t BUFF_SIZE);
	struct OMRProcessorDesc targetProcessor;
	struct OMRProcessorDesc relocatableTargetProcessor;
#if defined(J9VM_OPT_JITSERVER)
	int32_t (*startJITServer)(struct J9JITConfig *jitConfig);
	int32_t (*waitJITServerTermination)(struct J9JITConfig *jitConfig);
	uint64_t clientUID;
	uint64_t serverUID;
#endif /* J9VM_OPT_JITSERVER */
} J9JITConfig;

#define J9JIT_GROW_CACHES  0x100000
#define J9JIT_CG_OPT_LEVEL_HIGH  16
#define J9JIT_PATCHING_FENCE_REQUIRED  0x4000000
#define J9JIT_CG_OPT_LEVEL_BEST_AVAILABLE  8
#define J9JIT_CG_OPT_LEVEL_NONE  2
#define J9JIT_CG_REGISTER_MAPS  32
#define J9JIT_JIT_ATTACHED  0x800000
#define J9JIT_AOT_ATTACHED  0x1000000
#define J9JIT_SCAVENGE_ON_RESOLVE  0x4000
#define J9JIT_CG_BREAK_ON_ENTRY  1
#define J9JIT_JVMPI_INLINE_METHOD_OFF  16
#define J9JIT_SCAVENGE_ON_RUNTIME  0x200000
#define J9JIT_JVMPI_GEN_INLINE_ENTRY_EXIT  4
#define J9JIT_JVMPI_DISABLE_DIRECT_TO_JNI  64
#define J9JIT_TESTMODE  0x1000
#define J9JIT_JVMPI_DISABLE_DIRECT_RECLAIM  0x100
#define J9JIT_GC_NOTIFY  0x40000
#define J9JIT_QUICKSTART  0x200
#define J9JIT_ASSUME_STRICTFP  64
#define J9JIT_JVMPI_GEN_COMPILED_ENTRY_EXIT  1
#define J9JIT_JVMPI_GEN_NATIVE_ENTRY_EXIT  8
#define J9JIT_TOSS_CODE  0x8000
#define J9JIT_DATA_CACHE_FULL  0x20000000
#define J9JIT_DEFER_JIT  0x2000000
#define J9JIT_CG_OPT_LEVEL_LOW  4
#define J9JIT_REPORT_EVENTS  0x20000
#define J9JIT_JVMPI_DISABLE_DIRECT_INLINING_NATIVES  0x80
#define J9JIT_ASSUME_STRICTFPCOMPARES  0x80
#define J9JIT_JVMPI_JIT_DEFAULTS  39
#define J9JIT_JVMPI_GEN_BUILTIN_ENTRY_EXIT  2
#define J9JIT_AOT  0x2000
#define J9JIT_PATCHING_FENCE_TYPE  0x8000000
#define J9JIT_RUNTIME_RESOLVE  0x80000
#define J9JIT_CODE_CACHE_FULL  0x40000000
#define J9JIT_COMPILE_CLINIT  0x400000
#define J9JIT_JVMPI_INLINE_ALLOCATION_OFF  32


#if defined(J9VM_ARCH_X86)

typedef struct J9VMJITRegisterState {
#if defined(J9VM_ENV_DATA64)
	UDATA jit_rax;
	UDATA jit_rbx;
	UDATA jit_rcx;
	UDATA jit_rdx;
	UDATA jit_rdi;
	UDATA jit_rsi;
	UDATA jit_rbp;
	UDATA jit_rsp;
	UDATA jit_r8;
	UDATA jit_r9;
	UDATA jit_r10;
	UDATA jit_r11;
	UDATA jit_r12;
	UDATA jit_r13;
	UDATA jit_r14;
	UDATA jit_r15;
	UDATA jit_fpr0;
	UDATA jit_fpr1;
	UDATA jit_fpr2;
	UDATA jit_fpr3;
	UDATA jit_fpr4;
	UDATA jit_fpr5;
	UDATA jit_fpr6;
	UDATA jit_fpr7;
	UDATA jit_fpr8;
	UDATA jit_fpr9;
	UDATA jit_fpr10;
	UDATA jit_fpr11;
	UDATA jit_fpr12;
	UDATA jit_fpr13;
	UDATA jit_fpr14;
	UDATA jit_fpr15;
#else /* J9VM_ENV_DATA64 */
	UDATA jit_eax;
	UDATA jit_ebx;
	UDATA jit_ecx;
	UDATA jit_edx;
	UDATA jit_edi;
	UDATA jit_esi;
	UDATA jit_ebp;
	UDATA preserved8;
	UDATA jit_fpr0;
	UDATA fpPreserved1hi;
	UDATA jit_fpr1;
	UDATA fpPreserved2hi;
	UDATA jit_fpr2;
	UDATA fpPreserved3hi;
	UDATA jit_fpr3;
	UDATA fpPreserved4hi;
	UDATA jit_fpr4;
	UDATA fpPreserved5hi;
	UDATA jit_fpr5;
	UDATA fpPreserved6hi;
	UDATA jit_fpr6;
	UDATA fpPreserved7hi;
	UDATA jit_fpr7;
	UDATA fpPreserved8hi;
#endif /* J9VM_ENV_DATA64 */
} J9VMJITRegisterState;

#endif /* J9VM_ARCH_X86 */

typedef struct J9VMEntryLocalStorage {
	struct J9VMEntryLocalStorage* oldEntryLocalStorage;
	UDATA* jitGlobalStorageBase;
	struct J9I2JState i2jState;
	UDATA* jitFPRegisterStorageBase;
#if defined(WIN32) && !defined(J9VM_ENV_DATA64)
	UDATA* gpLink;
	UDATA* gpHandler;
	struct J9VMThread* currentVMThread;
#endif /* WIN32 && !J9VM_ENV_DATA64 */
#if defined(J9VM_PORT_ZOS_CEEHDLRSUPPORT)
	struct J9VMThread* currentVMThread;
#endif /* J9VM_PORT_ZOS_CEEHDLRSUPPORT */
#if defined(J9VM_OPT_JAVA_OFFLOAD_SUPPORT)
	UDATA savedJavaOffloadState;
#endif /* J9VM_OPT_JAVA_OFFLOAD_SUPPORT */
	UDATA jitTempSpace[4];
#if defined(J9VM_PORT_ZOS_CEEHDLRSUPPORT)
	UDATA calloutVMState;
	UDATA entryVMState;
	UDATA* ceehdlrGPRBase;
	UDATA* ceehdlrFPRBase;
	UDATA* ceehdlrFPCLocation;
#endif /* J9VM_PORT_ZOS_CEEHDLRSUPPORT */
	UDATA** machineSPSaveSlot;
} J9VMEntryLocalStorage;

typedef struct J9InternalVMLabels {
	void* throwException;
	void* executeCurrentBytecode;
	void* throwCurrentException;
	void* javaCheckAsyncEvents;
	void* javaStackOverflow;
	void* runJavaHandler;
	void* runJNIHandler;
	void* handlePopFrames;
	void* VMprJavaSendNative;
	void* throwExceptionNLS;
	void* throwNativeOutOfMemoryError;
	void* throwHeapOutOfMemoryError;
	void* runMethod;
	void* runMethodHandle;
	void* runMethodInterpreted;
	void* cInterpreter;
} J9InternalVMLabels;

typedef struct J9MemoryManagerFunctions {
	j9object_t  ( *J9AllocateIndexableObject)(struct J9VMThread *vmContext, J9Class *clazz, U_32 size, UDATA allocateFlags) ;
	j9object_t  ( *J9AllocateObject)(struct J9VMThread *vmContext, J9Class *clazz, UDATA allocateFlags) ;
	j9object_t  ( *J9AllocateIndexableObjectNoGC)(struct J9VMThread *vmThread, J9Class *clazz, U_32 numberIndexedFields, UDATA allocateFlags) ;
	j9object_t  ( *J9AllocateObjectNoGC)(struct J9VMThread *vmThread, J9Class *clazz, UDATA allocateFlags) ;
	void  ( *J9WriteBarrierPost)(struct J9VMThread *vmThread, j9object_t destinationObject, j9object_t storedObject) ;
	void  ( *J9WriteBarrierBatch)(struct J9VMThread *vmThread, j9object_t destinationObject) ;
	void  ( *J9WriteBarrierPostClass)(struct J9VMThread *vmThread, J9Class *destinationClazz, j9object_t storedObject) ;
	void  ( *J9WriteBarrierClassBatch)(struct J9VMThread *vmThread, J9Class *destinationClazz) ;
	UDATA  ( *allocateMemoryForSublistFragment)(void *vmThread, J9VMGC_SublistFragment *fragmentPrimitive) ;
	UDATA  ( *j9gc_heap_free_memory)(struct J9JavaVM *javaVM) ;
	UDATA  ( *j9gc_heap_total_memory)(struct J9JavaVM *javaVM) ;
	UDATA  ( *j9gc_is_garbagecollection_disabled)(struct J9JavaVM *javaVM) ;
	UDATA ( *j9gc_allsupported_memorypools)(struct J9JavaVM* javaVM);
	UDATA ( *j9gc_allsupported_garbagecollectors)(struct J9JavaVM* javaVM);
	const char* ( *j9gc_pool_name)(struct J9JavaVM* javaVM, UDATA poolID);
	const char* ( *j9gc_garbagecollector_name)(struct J9JavaVM* javaVM, UDATA gcID);
	UDATA ( *j9gc_is_managedpool_by_collector)(struct J9JavaVM* javaVM, UDATA gcID, UDATA poolID);
	UDATA ( *j9gc_is_usagethreshold_supported)(struct J9JavaVM* javaVM, UDATA poolID);
	UDATA ( *j9gc_is_collectionusagethreshold_supported)(struct J9JavaVM* javaVM, UDATA poolID);
	UDATA ( *j9gc_is_local_collector)(struct J9JavaVM* javaVM, UDATA gcID);
	UDATA ( *j9gc_get_collector_id)(OMR_VMThread *omrVMThread);
	UDATA ( *j9gc_pools_memory)(struct J9JavaVM* javaVM, UDATA poolIDs, UDATA* totals, UDATA* frees, BOOLEAN gcEnd);
	UDATA ( *j9gc_pool_maxmemory)(struct J9JavaVM* javaVM, UDATA poolID);
	UDATA ( *j9gc_pool_memoryusage)(struct J9JavaVM *javaVM, UDATA poolID, UDATA *free, UDATA *total);
	const char* (*j9gc_get_gc_action)(struct J9JavaVM* javaVM, UDATA gcID);
	const char* (*j9gc_get_gc_cause)(struct OMR_VMThread* omrVMthread);
	J9HookInterface** (*j9gc_get_private_hook_interface)(struct J9JavaVM *javaVM);

	int  ( *gcStartupHeapManagement)(struct J9JavaVM * vm) ;
	void  ( *gcShutdownHeapManagement)(struct J9JavaVM * vm) ;
	void ( *jvmPhaseChange)(struct J9VMThread *currentThread, UDATA phase);
	IDATA  ( *initializeMutatorModelJava)(struct J9VMThread* vmThread) ;
	void  ( *cleanupMutatorModelJava)(struct J9VMThread* vmThread) ;
#if defined(J9VM_GC_FINALIZATION)
	int  ( *j9gc_finalizer_startup)(struct J9JavaVM * vm) ;
	void  ( *j9gc_finalizer_shutdown)(struct J9JavaVM * vm) ;
	UDATA ( *j9gc_wait_for_reference_processing)(struct J9JavaVM * vm) ;
	void  ( *runFinalization)(struct J9VMThread *vmThread) ;
#endif /* J9VM_GC_FINALIZATION */
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	UDATA  ( *forceClassLoaderUnload)(struct J9VMThread *vmThread, J9ClassLoader *classLoader) ;
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */
#if defined(J9VM_GC_FINALIZATION)
	UDATA  ( *finalizeObjectCreated)(struct J9VMThread *vmThread, j9object_t object) ;
#endif /* J9VM_GC_FINALIZATION */
	UDATA  ( *j9gc_ext_is_marked)(struct J9JavaVM *javaVM, j9object_t objectPtr) ;
#if defined(J9VM_GC_BATCH_CLEAR_TLH)
	void  ( *allocateZeroedTLHPages)(struct J9JavaVM *javaVM, UDATA flag) ;
	UDATA  ( *isAllocateZeroedTLHPagesEnabled)(struct J9JavaVM *javaVM) ;
#endif /* J9VM_GC_BATCH_CLEAR_TLH */
	UDATA  ( *isStaticObjectAllocateFlags)(struct J9JavaVM *javaVM) ;
	UDATA  ( *getStaticObjectAllocateFlags)(struct J9JavaVM *javaVM) ;
	UDATA  ( *j9gc_scavenger_enabled)(struct J9JavaVM *javaVM) ;
	UDATA  ( *j9gc_concurrent_scavenger_enabled)(struct J9JavaVM *javaVM) ;
	UDATA  ( *j9gc_software_read_barrier_enabled)(struct J9JavaVM *javaVM) ;
	BOOLEAN  ( *j9gc_hot_reference_field_required)(struct J9JavaVM *javaVM) ;
	uint32_t  ( *j9gc_max_hot_field_list_length)(struct J9JavaVM *javaVM) ;
#if defined(J9VM_GC_HEAP_CARD_TABLE)
	UDATA  ( *j9gc_concurrent_getCardSize)(struct J9JavaVM *javaVM) ;
	UDATA  ( *j9gc_concurrent_getHeapBase)(struct J9JavaVM *javaVM) ;
#endif /* J9VM_GC_HEAP_CARD_TABLE */
	UDATA  ( *j9gc_modron_getWriteBarrierType)(struct J9JavaVM *javaVM) ;
	UDATA  ( *j9gc_modron_getReadBarrierType)(struct J9JavaVM *javaVM) ;
	jint  (JNICALL *queryGCStatus)(JavaVM *vm, jint *nHeaps, GCStatus *status, jint statusSize) ;
	void  ( *j9gc_flush_caches_for_walk)(struct J9JavaVM *javaVM) ;
	void  ( *j9gc_flush_nonAllocationCaches_for_walk)(struct J9JavaVM *javaVM) ;
	struct J9HookInterface**  ( *j9gc_get_hook_interface)(struct J9JavaVM *javaVM) ;
	struct J9HookInterface**  ( *j9gc_get_omr_hook_interface)(struct OMR_VM *omrVM) ;
	UDATA  ( *j9gc_get_overflow_safe_alloc_size)(struct J9JavaVM *javaVM) ;
	void*  ( *getVerboseGCFunctionTable)(struct J9JavaVM *javaVM) ;
	I_32  ( *referenceArrayCopy)(struct J9VMThread *vmThread, J9IndexableObject *srcObject, J9IndexableObject *destObject, fj9object_t *srcAddress, fj9object_t *destAddress, I_32 lengthInSlots) ;
	/* TODO: disable this entrypoint once the JIT has been updated */
	I_32  ( *referenceArrayCopyIndex)(struct J9VMThread *vmThread, J9IndexableObject *srcObject, J9IndexableObject *destObject, I_32 srcIndex, I_32 destIndex, I_32 lengthInSlots) ;
	UDATA  ( *alwaysCallReferenceArrayCopyHelper)(struct J9JavaVM *javaVM) ;
	void  ( *j9gc_ext_reachable_objects_do)(struct J9VMThread *vmThread, jvmtiIterationControl (*func)(j9object_t *slotPtr, j9object_t sourcePtr, void *userData, IDATA type, IDATA index, IDATA wasReportedBefore), void *userData, UDATA walkFlags) ;
	void  ( *j9gc_ext_reachable_from_object_do)(struct J9VMThread *vmThread, j9object_t objectPtr, jvmtiIterationControl (*func)(j9object_t *slotPtr, j9object_t sourcePtr, void *userData, IDATA type, IDATA index, IDATA wasReportedBefore), void *userData, UDATA walkFlags) ;
	UDATA  ( *j9gc_jit_isInlineAllocationSupported)(struct J9JavaVM *javaVM) ;
	void  ( *J9WriteBarrierPre)(struct J9VMThread *vmThread, J9Object *dstObject, fj9object_t *dstAddress, J9Object *srcObject) ;
	void  ( *J9WriteBarrierPreClass)(struct J9VMThread *vmThread, J9Object *dstClassObject, J9Object **dstAddress, J9Object *srcObject) ;
	void  ( *J9ReadBarrier)(struct J9VMThread *vmThread, fj9object_t *srcAddress);
	void  ( *J9ReadBarrierClass)(struct J9VMThread *vmThread, j9object_t *srcAddress);
	j9object_t  ( *j9gc_weakRoot_readObject)(struct J9VMThread *vmThread, j9object_t *srcAddress);
	j9object_t  ( *j9gc_weakRoot_readObjectVM)(struct J9JavaVM *vm, j9object_t *srcAddress);
	UDATA  ( *j9gc_ext_check_is_valid_heap_object)(struct J9JavaVM *javaVM, j9object_t ptr, UDATA flags) ;
#if defined(J9VM_GC_FINALIZATION)
	UDATA  ( *j9gc_get_objects_pending_finalization_count)(struct J9JavaVM* vm) ;
#endif /* J9VM_GC_FINALIZATION */
	UDATA  ( *j9gc_set_softmx)(struct J9JavaVM *javaVM, UDATA newsoftmx) ;
	UDATA  ( *j9gc_get_softmx)(struct J9JavaVM *javaVM) ;
	UDATA  ( *j9gc_get_initial_heap_size)(struct J9JavaVM *javaVM) ;
	UDATA  ( *j9gc_get_maximum_heap_size)(struct J9JavaVM *javaVM) ;
	UDATA  ( *j9gc_objaccess_checkClassLive)(struct J9JavaVM *javaVM, J9Class *classPtr) ;
#if defined(J9VM_GC_OBJECT_ACCESS_BARRIER)
	IDATA  ( *j9gc_objaccess_indexableReadI8)(struct J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) ;
	UDATA  ( *j9gc_objaccess_indexableReadU8)(struct J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) ;
	IDATA  ( *j9gc_objaccess_indexableReadI16)(struct J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) ;
	UDATA  ( *j9gc_objaccess_indexableReadU16)(struct J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) ;
	IDATA  ( *j9gc_objaccess_indexableReadI32)(struct J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) ;
	UDATA  ( *j9gc_objaccess_indexableReadU32)(struct J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) ;
	I_64  ( *j9gc_objaccess_indexableReadI64)(struct J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) ;
	U_64  ( *j9gc_objaccess_indexableReadU64)(struct J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) ;
	j9object_t  ( *j9gc_objaccess_indexableReadObject)(struct J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) ;
	void*  ( *j9gc_objaccess_indexableReadAddress)(struct J9VMThread *vmThread, J9IndexableObject *srcObject, I_32 index, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_indexableStoreI8)(struct J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, I_32 value, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_indexableStoreU8)(struct J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, U_32 value, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_indexableStoreI16)(struct J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, I_32 value, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_indexableStoreU16)(struct J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, U_32 value, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_indexableStoreI32)(struct J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, I_32 value, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_indexableStoreU32)(struct J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, U_32 value, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_indexableStoreI64)(struct J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, I_64 value, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_indexableStoreU64)(struct J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, U_64 value, UDATA isVolatile) ;
#if !defined(J9VM_ENV_DATA64)
	void  ( *j9gc_objaccess_indexableStoreU64Split)(struct J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, U_32 valueSlot0, U_32 valueSlot1, UDATA isVolatile) ;
#endif /* !J9VM_ENV_DATA64 */
	void  ( *j9gc_objaccess_indexableStoreObject)(struct J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, j9object_t value, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_indexableStoreAddress)(struct J9VMThread *vmThread, J9IndexableObject *destObject, I_32 index, void *value, UDATA isVolatile) ;
	IDATA  ( *j9gc_objaccess_mixedObjectReadI32)(struct J9VMThread *vmThread, j9object_t srcObject, UDATA offset, UDATA isVolatile) ;
	UDATA  ( *j9gc_objaccess_mixedObjectReadU32)(struct J9VMThread *vmThread, j9object_t srcObject, UDATA offset, UDATA isVolatile) ;
	I_64  ( *j9gc_objaccess_mixedObjectReadI64)(struct J9VMThread *vmThread, j9object_t srcObject, UDATA offset, UDATA isVolatile) ;
	U_64  ( *j9gc_objaccess_mixedObjectReadU64)(struct J9VMThread *vmThread, j9object_t srcObject, UDATA offset, UDATA isVolatile) ;
	j9object_t  ( *j9gc_objaccess_mixedObjectReadObject)(struct J9VMThread *vmThread, j9object_t srcObject, UDATA offset, UDATA isVolatile) ;
	void*  ( *j9gc_objaccess_mixedObjectReadAddress)(struct J9VMThread *vmThread, j9object_t srcObject, UDATA offset, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_mixedObjectStoreI32)(struct J9VMThread *vmThread, j9object_t destObject, UDATA offset, I_32 value, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_mixedObjectStoreU32)(struct J9VMThread *vmThread, j9object_t destObject, UDATA offset, U_32 value, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_mixedObjectStoreI64)(struct J9VMThread *vmThread, j9object_t destObject, UDATA offset, I_64 value, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_mixedObjectStoreU64)(struct J9VMThread *vmThread, j9object_t destObject, UDATA offset, U_64 value, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_mixedObjectStoreObject)(struct J9VMThread *vmThread, j9object_t destObject, UDATA offset, j9object_t value, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_mixedObjectStoreAddress)(struct J9VMThread *vmThread, j9object_t destObject, UDATA offset, void *value, UDATA isVolatile) ;
#if !defined(J9VM_ENV_DATA64)
	void  ( *j9gc_objaccess_mixedObjectStoreU64Split)(struct J9VMThread *vmThread, j9object_t destObject, UDATA offset, U_32 valueSlot0, U_32 valueSlot1, UDATA isVolatile) ;
#endif /* !J9VM_ENV_DATA64 */
	UDATA  ( *j9gc_objaccess_mixedObjectCompareAndSwapInt)(struct J9VMThread *vmThread, j9object_t destObject, UDATA offset, U_32 compareValue, U_32 swapValue) ;
	UDATA  ( *j9gc_objaccess_mixedObjectCompareAndSwapLong)(struct J9VMThread *vmThread, j9object_t destObject, UDATA offset, U_64 compareValue, U_64 swapValue) ;
	U_32  ( *j9gc_objaccess_mixedObjectCompareAndExchangeInt)(struct J9VMThread *vmThread, j9object_t destObject, UDATA offset, U_32 compareValue, U_32 swapValue) ;
	U_64  ( *j9gc_objaccess_mixedObjectCompareAndExchangeLong)(struct J9VMThread *vmThread, j9object_t destObject, UDATA offset, U_64 compareValue, U_64 swapValue) ;
	IDATA  ( *j9gc_objaccess_staticReadI32)(struct J9VMThread *vmThread, J9Class *clazz, I_32 *srcSlot, UDATA isVolatile) ;
	UDATA  ( *j9gc_objaccess_staticReadU32)(struct J9VMThread *vmThread, J9Class *clazz, U_32 *srcSlot, UDATA isVolatile) ;
	I_64  ( *j9gc_objaccess_staticReadI64)(struct J9VMThread *vmThread, J9Class *clazz, I_64 *srcSlot, UDATA isVolatile) ;
	U_64  ( *j9gc_objaccess_staticReadU64)(struct J9VMThread *vmThread, J9Class *clazz, U_64 *srcSlot, UDATA isVolatile) ;
	j9object_t  ( *j9gc_objaccess_staticReadObject)(struct J9VMThread *vmThread, J9Class *clazz, j9object_t *srcSlot, UDATA isVolatile) ;
	void*  ( *j9gc_objaccess_staticReadAddress)(struct J9VMThread *vmThread, J9Class *clazz, void **srcSlot, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_staticStoreI32)(struct J9VMThread *vmThread, J9Class *clazz, I_32 *destSlot, I_32 value, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_staticStoreU32)(struct J9VMThread *vmThread, J9Class *clazz, U_32 *destSlot, U_32 value, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_staticStoreI64)(struct J9VMThread *vmThread, J9Class *clazz, I_64 *destSlot, I_64 value, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_staticStoreU64)(struct J9VMThread *vmThread, J9Class *clazz, U_64 *destSlot, U_64 value, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_staticStoreObject)(struct J9VMThread *vmThread, J9Class *clazz, j9object_t *destSlot, j9object_t value, UDATA isVolatile) ;
	void  ( *j9gc_objaccess_staticStoreAddress)(struct J9VMThread *vmThread, J9Class *clazz, void **destSlot, void *value, UDATA isVolatile) ;
#if !defined(J9VM_ENV_DATA64)
	void  ( *j9gc_objaccess_staticStoreU64Split)(struct J9VMThread *vmThread, J9Class *clazz, U_64 *destSlot, U_32 valueSlot0, U_32 valueSlot1, UDATA isVolatile) ;
#endif /* !J9VM_ENV_DATA64 */
	void  ( *j9gc_objaccess_storeObjectToInternalVMSlot)(struct J9VMThread *vmThread, j9object_t *destSlot, j9object_t value) ;
	j9object_t  ( *j9gc_objaccess_readObjectFromInternalVMSlot)(struct J9VMThread *vmThread, struct J9JavaVM *vm, j9object_t *srcSlot) ;
	U_8*  ( *j9gc_objaccess_getArrayObjectDataAddress)(struct J9VMThread *vmThread, J9IndexableObject *arrayObject) ;
	j9objectmonitor_t*  ( *j9gc_objaccess_getLockwordAddress)(struct J9VMThread *vmThread, j9object_t object) ;
	void  ( *j9gc_objaccess_cloneObject)(struct J9VMThread *vmThread, j9object_t srcObject, j9object_t destObject) ;
	void ( *j9gc_objaccess_copyObjectFields)(struct J9VMThread *vmThread, J9Class* valueClass, j9object_t srcObject, UDATA srcOffset, j9object_t destObject, UDATA destOffset) ;
	void ( *j9gc_objaccess_copyObjectFieldsToFlattenedArrayElement)(struct J9VMThread *vmThread, J9ArrayClass *arrayClazz, j9object_t srcObject, J9IndexableObject *arrayRef, I_32 index) ;
	void ( *j9gc_objaccess_copyObjectFieldsFromFlattenedArrayElement)(struct J9VMThread *vmThread, J9ArrayClass *arrayClazz, j9object_t destObject, J9IndexableObject *arrayRef, I_32 index) ;
	BOOLEAN ( *j9gc_objaccess_structuralCompareFlattenedObjects)(struct J9VMThread *vmThread, J9Class *valueClass, j9object_t lhsObject, j9object_t rhsObject, UDATA startOffset) ;
	void  ( *j9gc_objaccess_cloneIndexableObject)(struct J9VMThread *vmThread, J9IndexableObject *srcObject, J9IndexableObject *destObject) ;
	j9object_t  ( *j9gc_objaccess_asConstantPoolObject)(struct J9VMThread *vmThread, j9object_t toConvert, UDATA allocationFlags) ;
	j9object_t  ( *j9gc_objaccess_referenceGet)(struct J9VMThread *vmThread, j9object_t refObject) ;
	void ( *j9gc_objaccess_referenceReprocess)(struct J9VMThread *vmThread, j9object_t refObject) ;
	void  ( *j9gc_objaccess_jniDeleteGlobalReference)(struct J9VMThread *vmThread, j9object_t reference) ;
	UDATA  ( *j9gc_objaccess_compareAndSwapObject)(struct J9VMThread *vmThread, j9object_t destObject, j9object_t*destAddress, j9object_t compareObject, j9object_t swapObject) ;
	UDATA  ( *j9gc_objaccess_staticCompareAndSwapObject)(struct J9VMThread *vmThread, J9Class *destClass, j9object_t *destAddress, j9object_t compareObject, j9object_t swapObject) ;
	j9object_t  ( *j9gc_objaccess_compareAndExchangeObject)(struct J9VMThread *vmThread, j9object_t destObject, j9object_t*destAddress, j9object_t compareObject, j9object_t swapObject) ;
	j9object_t  ( *j9gc_objaccess_staticCompareAndExchangeObject)(struct J9VMThread *vmThread, J9Class *destClass, j9object_t *destAddress, j9object_t compareObject, j9object_t swapObject) ;
	void  ( *j9gc_objaccess_fillArrayOfObjects)(struct J9VMThread *vmThread, J9IndexableObject *destObject, I_32 destIndex, I_32 count, j9object_t value) ;
	UDATA  ( *j9gc_objaccess_compressedPointersShift)(struct J9VMThread *vmThread) ;
	UDATA  ( *j9gc_objaccess_compressedPointersShadowHeapBase)(struct J9VMThread *vmThread) ;
	UDATA  ( *j9gc_objaccess_compressedPointersShadowHeapTop)(struct J9VMThread *vmThread) ;
#endif /* J9VM_GC_OBJECT_ACCESS_BARRIER */
	const char*  ( *j9gc_get_gcmodestring)(struct J9JavaVM *javaVM) ;
	UDATA  ( *j9gc_get_object_size_in_bytes)(struct J9JavaVM* javaVM, j9object_t objectPtr) ;
	UDATA  ( *j9gc_get_object_total_footprint_in_bytes)(struct J9JavaVM *javaVM, j9object_t objectPtr);
	UDATA  ( *j9gc_modron_global_collect)(struct J9VMThread *vmThread) ;
	UDATA  ( *j9gc_modron_global_collect_with_overrides)(struct J9VMThread *vmThread, U_32 overrideFlags) ;
	UDATA  ( *j9gc_modron_local_collect)(struct J9VMThread *vmThread) ;
	void  ( *j9gc_all_object_and_vm_slots_do)(struct J9JavaVM *javaVM, void *function, void *userData, UDATA walkFlags) ;
	jvmtiIterationControl  ( *j9mm_iterate_heaps)(struct J9JavaVM *vm, J9PortLibrary *portLibrary, UDATA flags, jvmtiIterationControl (*func)(struct J9JavaVM *vm, struct J9MM_IterateHeapDescriptor *heapDesc, void *userData), void *userData) ;
	jvmtiIterationControl  ( *j9mm_iterate_spaces)(struct J9JavaVM *javaVM, J9PortLibrary *portLibrary, struct J9MM_IterateHeapDescriptor *heap, UDATA flags, jvmtiIterationControl (*func)(struct J9JavaVM *vm, struct J9MM_IterateSpaceDescriptor *spaceDesc, void *userData), void *userData) ;
	jvmtiIterationControl  ( *j9mm_iterate_roots)(struct J9JavaVM *javaVM, J9PortLibrary *portLibrary, UDATA flags, jvmtiIterationControl (*func)(void* ptr, struct J9MM_HeapRootSlotDescriptor *rootDesc, void *userData), void *userData) ;
	jvmtiIterationControl  ( *j9mm_iterate_regions)(struct J9JavaVM *vm, J9PortLibrary *portLibrary, struct J9MM_IterateSpaceDescriptor *space, UDATA flags, jvmtiIterationControl (*func)(struct J9JavaVM *vm, struct J9MM_IterateRegionDescriptor *regionDesc, void *userData), void *userData) ;
	jvmtiIterationControl  ( *j9mm_iterate_region_objects)(struct J9JavaVM *vm, J9PortLibrary *portLibrary, struct J9MM_IterateRegionDescriptor *region, UDATA flags, jvmtiIterationControl (*func)(struct J9JavaVM *vm, struct J9MM_IterateObjectDescriptor *objectDesc, void *userData), void *userData) ;
	UDATA  ( *j9mm_find_region_for_pointer)(struct J9JavaVM* javaVM, void *pointer, struct J9MM_IterateRegionDescriptor *regionDesc) ;
	jvmtiIterationControl  ( *j9mm_iterate_object_slots)(struct J9JavaVM *javaVM, J9PortLibrary *portLibrary, struct J9MM_IterateObjectDescriptor *object, UDATA flags, jvmtiIterationControl (*func)(struct J9JavaVM *javaVM, struct J9MM_IterateObjectDescriptor *objectDesc, struct J9MM_IterateObjectRefDescriptor *refDesc, void *userData), void *userData) ;
	void  ( *j9mm_initialize_object_descriptor)(struct J9JavaVM *javaVM, struct J9MM_IterateObjectDescriptor *descriptor, j9object_t object) ;
	jvmtiIterationControl  ( *j9mm_iterate_all_objects)(struct J9JavaVM *vm, J9PortLibrary *portLibrary, UDATA flags, jvmtiIterationControl (*func)(struct J9JavaVM *vm, struct J9MM_IterateObjectDescriptor *object, void *userData), void *userData) ;
	UDATA  ( *j9gc_modron_isFeatureSupported)(struct J9JavaVM *javaVM, UDATA feature) ;
	UDATA  ( *j9gc_modron_getConfigurationValueForKey)(struct J9JavaVM *javaVM, UDATA key, void *value) ;
	const char*  ( *omrgc_get_version)(OMR_VM *omrVM) ;
	UDATA  ( *j9mm_abandon_object)(struct J9JavaVM *javaVM,struct  J9MM_IterateRegionDescriptor *region, struct J9MM_IterateObjectDescriptor *objectDesc) ;
	void  ( *j9mm_get_guaranteed_nursery_range)(struct J9JavaVM* javaVM, void** start, void** end) ;
	UDATA  ( *j9gc_arraylet_getLeafSize)(struct J9JavaVM* javaVM) ;
	UDATA  ( *j9gc_arraylet_getLeafLogSize)(struct J9JavaVM* javaVM) ;
	void  ( *j9gc_set_allocation_sampling_interval)(struct J9JavaVM *vm, UDATA samplingInterval);
	void  ( *j9gc_set_allocation_threshold)(struct J9VMThread *vmThread, UDATA low, UDATA high) ;
	void  ( *j9gc_objaccess_recentlyAllocatedObject)(struct J9VMThread *vmThread, J9Object *dstObject) ;
	void  ( *j9gc_objaccess_postStoreClassToClassLoader)(struct J9VMThread* vmThread, J9ClassLoader* destClassLoader, J9Class* srcClass) ;
	I_32  ( *j9gc_objaccess_getObjectHashCode)(struct J9JavaVM *vm, J9Object* object) ;
	j9object_t  ( *j9gc_createJavaLangString)(struct J9VMThread *vmThread, U_8 *data, UDATA length, UDATA stringFlags) ;
	j9object_t  ( *j9gc_internString)(struct J9VMThread *vmThread, j9object_t sourceString) ;
#if defined(J9VM_GC_FINALIZATION)
	void  ( *j9gc_runFinalizersOnExit)(struct J9VMThread* vmThread, UDATA run) ;
#endif /* J9VM_GC_FINALIZATION */
	void*  ( *j9gc_objaccess_jniGetPrimitiveArrayCritical)(struct J9VMThread* vmThread, jarray array, jboolean *isCopy) ;
	void  ( *j9gc_objaccess_jniReleasePrimitiveArrayCritical)(struct J9VMThread* vmThread, jarray array, void * elems, jint mode) ;
	const jchar*  ( *j9gc_objaccess_jniGetStringCritical)(struct J9VMThread* vmThread, jstring str, jboolean *isCopy) ;
	void  ( *j9gc_objaccess_jniReleaseStringCritical)(struct J9VMThread* vmThread, jstring str, const jchar* elems) ;
	void  ( *j9gc_finalizer_completeFinalizersOnExit)(struct J9VMThread* vmThread) ;
	void  ( *j9gc_get_CPU_times)(struct J9JavaVM *javaVM, U_64* mainCpuMillis, U_64* workerCpuMillis, U_32* maxThreads, U_32* currentThreads) ;
	void*  ( *omrgc_walkLWNRLockTracePool)(void *omrVM, pool_state *state) ;
#if defined(J9VM_GC_OBJECT_ACCESS_BARRIER)
	UDATA  ( *j9gc_objaccess_staticCompareAndSwapInt)(struct J9VMThread *vmThread, J9Class *destClass, U_32 *destAddress, U_32 compareValue, U_32 swapValue) ;
	U_32  ( *j9gc_objaccess_staticCompareAndExchangeInt)(struct J9VMThread *vmThread, J9Class *destClass, U_32 *destAddress, U_32 compareValue, U_32 swapValue) ;
#if !defined(J9VM_ENV_DATA64)
	UDATA  ( *j9gc_objaccess_mixedObjectCompareAndSwapLongSplit)(struct J9VMThread *vmThread, J9Object *destObject, UDATA offset, U_32 compareValueSlot0, U_32 compareValueSlot1, U_32 swapValueSlot0, U_32 swapValueSlot1) ;
#endif /* !J9VM_ENV_DATA64 */
	UDATA  ( *j9gc_objaccess_staticCompareAndSwapLong)(struct J9VMThread *vmThread, J9Class *destClass, U_64 *destAddress, U_64 compareValue, U_64 swapValue) ;
	U_64 ( *j9gc_objaccess_staticCompareAndExchangeLong)(struct J9VMThread *vmThread, J9Class *destClass, U_64 *destAddress, U_64 compareValue, U_64 swapValue) ;
#if !defined(J9VM_ENV_DATA64)
	UDATA  ( *j9gc_objaccess_staticCompareAndSwapLongSplit)(struct J9VMThread *vmThread, J9Class *destClass, U_64 *destAddress, U_32 compareValueSlot0, U_32 compareValueSlot1, U_32 swapValueSlot0, U_32 swapValueSlot1) ;
#endif /* !J9VM_ENV_DATA64 */
#endif /* J9VM_GC_OBJECT_ACCESS_BARRIER */
	UDATA  ( *j9gc_get_bytes_allocated_by_thread)(struct J9VMThread* vmThread) ;
	jvmtiIterationControl  ( *j9mm_iterate_all_ownable_synchronizer_objects)(struct J9VMThread *vmThread, J9PortLibrary *portLibrary, UDATA flags, jvmtiIterationControl (*func)(struct J9VMThread *vmThread, struct J9MM_IterateObjectDescriptor *object, void *userData), void *userData) ;
	UDATA  ( *ownableSynchronizerObjectCreated)(struct J9VMThread *vmThread, j9object_t object) ;
	void  ( *j9gc_notifyGCOfClassReplacement)(struct J9VMThread *vmThread, J9Class *originalClass, J9Class *replacementClass, UDATA isFastHCR) ;
	I_32  ( *j9gc_get_jit_string_dedup_policy)(struct J9JavaVM *javaVM) ;
	UDATA ( *j9gc_stringHashFn)(void *key, void *userData);
	BOOLEAN ( *j9gc_stringHashEqualFn)(void *leftKey, void *rightKey, void *userData);
} J9MemoryManagerFunctions;

typedef struct J9InternalVMFunctions {
	void* reserved0;
	void* reserved1;
	void* reserved2;
	jint  (JNICALL *DestroyJavaVM)(JavaVM * javaVM) ;
	jint  (JNICALL *AttachCurrentThread)(JavaVM * vm, void ** p_env, void * thr_args) ;
	jint  (JNICALL *DetachCurrentThread)(JavaVM * javaVM) ;
	jint  (JNICALL *GetEnv)(JavaVM *jvm, void **penv, jint version) ;
	jint  (JNICALL *AttachCurrentThreadAsDaemon)(JavaVM * vm, void ** p_env, void * thr_args) ;
	UDATA  ( *addSystemProperty)(struct J9JavaVM * vm, const char * name, const char * value, UDATA flags) ;
	UDATA  ( *getSystemProperty)(struct J9JavaVM * vm, const char * name, struct J9VMSystemProperty ** propertyPtr) ;
	UDATA  ( *setSystemProperty)(struct J9JavaVM * vm, struct J9VMSystemProperty * property, const char * value) ;
	struct J9VMDllLoadInfo*  ( *findDllLoadInfo)(J9Pool* aPool, const char* dllName) ;
	void  ( *internalExceptionDescribe)(struct J9VMThread* vmStruct) ;
	struct J9Class*  ( *internalFindClassUTF8)(struct J9VMThread *currentThread, U_8 *className, UDATA classNameLength, struct J9ClassLoader *classLoader, UDATA options) ;
	struct J9Class*  ( *internalFindClassInModule)(struct J9VMThread *currentThread, struct J9Module *j9module, U_8 *className, UDATA classNameLength, struct J9ClassLoader *classLoader, UDATA options) ;
	struct J9Class*  ( *internalFindClassString)(struct J9VMThread* currentThread, j9object_t moduleName, j9object_t className, struct J9ClassLoader* classLoader, UDATA options, UDATA allowedBitsForClassName) ;
	struct J9Class*  ( *hashClassTableAt)(struct J9ClassLoader *classLoader, U_8 *className, UDATA classNameLength) ;
	UDATA  ( *hashClassTableAtPut)(struct J9VMThread *vmThread, struct J9ClassLoader *classLoader, U_8 *className, UDATA classNameLength, struct J9Class *value) ;
	UDATA  ( *hashClassTableDelete)(struct J9ClassLoader *classLoader, U_8 *className, UDATA classNameLength) ;
	void  ( *hashClassTableReplace)(struct J9VMThread* vmThread, struct J9ClassLoader *classLoader, struct J9Class *originalClass, struct J9Class *replacementClass) ;
	struct J9ObjectMonitor *  ( *monitorTableAt)(struct J9VMThread* vmStruct, j9object_t object) ;
	struct J9VMThread*  ( *allocateVMThread)(struct J9JavaVM * vm, omrthread_t osThread, UDATA privateFlags, void * memorySpace, J9Object * threadObject) ;
	void  ( *deallocateVMThread)(struct J9VMThread * vmThread, UDATA decrementZombieCount, UDATA sendThreadDestroyEvent) ;
	struct J9MemorySegment*  ( *allocateMemorySegment)(struct J9JavaVM *javaVM, UDATA size, UDATA type, U_32 memoryCategory) ;
	IDATA  ( *javaThreadProc)(void *entryarg) ;
	char*  ( *copyStringToUTF8WithMemAlloc)(struct J9VMThread *currentThread, j9object_t string, UDATA stringFlags, const char *prependStr, UDATA prependStrLength, char *buffer, UDATA bufferLength, UDATA *utf8Length) ;
	J9UTF8* ( *copyStringToJ9UTF8WithMemAlloc)(struct J9VMThread *vmThread, j9object_t string, UDATA stringFlags, const char *prependStr, UDATA prependStrLength, char *buffer, UDATA bufferLength) ;
	void  ( *internalAcquireVMAccess)(struct J9VMThread * currentThread) ;
	void  ( *internalAcquireVMAccessWithMask)(struct J9VMThread * currentThread, UDATA haltFlags) ;
	void  ( *internalAcquireVMAccessNoMutexWithMask)(struct J9VMThread * vmThread, UDATA haltFlags) ;
	void  ( *internalReleaseVMAccessSetStatus)(struct J9VMThread * vmThread, UDATA flags) ;
	IDATA  ( *instanceFieldOffset)(struct J9VMThread *vmStruct, struct J9Class *clazz, U_8 *fieldName, UDATA fieldNameLength, U_8 *signature, UDATA signatureLength, struct J9Class **definingClass, UDATA *instanceField, UDATA options) ;
	void*  ( *staticFieldAddress)(struct J9VMThread *vmStruct, struct J9Class *clazz, U_8 *fieldName, UDATA fieldNameLength, U_8 *signature, UDATA signatureLength, struct J9Class **definingClass, UDATA *staticField, UDATA options, struct J9Class *sourceClass) ;
	struct J9Class*  ( *internalFindKnownClass)(struct J9VMThread *currentThread, UDATA index, UDATA flags) ;
	struct J9Class*  ( *resolveKnownClass)(struct J9JavaVM * vm, UDATA index) ;
	UDATA  ( *computeHashForUTF8)(const U_8 * string, UDATA size) ;
	IDATA  ( *getStringUTF8Length)(struct J9VMThread *vmThread, j9object_t string) ;
	void  ( *acquireExclusiveVMAccess)(struct J9VMThread * vmThread) ;
	void  ( *releaseExclusiveVMAccess)(struct J9VMThread * vmThread) ;
	void  ( *internalReleaseVMAccess)(struct J9VMThread * currentThread) ;
	void  (JNICALL *sendInit)(struct J9VMThread *vmContext, j9object_t object, struct J9Class *senderClass, UDATA lookupOptions) ;
	void  ( *internalAcquireVMAccessNoMutex)(struct J9VMThread * vmThread) ;
	struct J9Class*  ( *internalCreateArrayClass)(struct J9VMThread* vmThread, struct J9ROMArrayClass* romClass, struct J9Class* elementClass) ;
	IDATA  ( *attachSystemDaemonThread)(struct J9JavaVM * vm, struct J9VMThread ** p_env, const char * threadName) ;
	void  ( *internalAcquireVMAccessClearStatus)(struct J9VMThread * vmThread, UDATA flags) ;
#if defined(J9VM_OPT_REFLECT)
	j9object_t  ( *helperMultiANewArray)(struct J9VMThread *vmStruct, J9ArrayClass *classPtr, UDATA dimensions, I_32 *dimensionArray, UDATA allocationType) ;
#endif /* J9VM_OPT_REFLECT */
	UDATA  ( *javaLookupMethod)(struct J9VMThread *currentThread, struct J9Class *clazz, struct J9ROMNameAndSignature *selector, struct J9Class *senderClass, UDATA options) ;
	UDATA  ( *javaLookupMethodImpl)(struct J9VMThread *currentThread, struct J9Class *clazz, struct J9ROMNameAndSignature *selector, struct J9Class *senderClass, UDATA options, BOOLEAN *foundDefaultConflicts) ;
	void  ( *setCurrentException)(struct J9VMThread *currentThread, UDATA exceptionNumber, UDATA *detailMessage) ;
	void  ( *setCurrentExceptionUTF)(struct J9VMThread * vmThread, UDATA exceptionNumber, const char * detailUTF) ;
	void  ( *setCurrentExceptionNLS)(struct J9VMThread * vmThread, UDATA exceptionNumber, U_32 moduleName, U_32 messageNumber) ;
	void ( *setCurrentExceptionNLSWithArgs)(struct J9VMThread * vmThread, U_32 nlsModule, U_32 nlsID, UDATA exceptionIndex, ...) ;
	void  ( *setCurrentExceptionWithCause)(struct J9VMThread *currentThread, UDATA exceptionNumber, UDATA *detailMessage, j9object_t cause) ;
	UDATA  ( *objectMonitorEnter)(struct J9VMThread* vmStruct, j9object_t object) ;
	IDATA  ( *objectMonitorExit)(struct J9VMThread* vmStruct, j9object_t object) ;
	struct J9Method*  ( *resolveStaticMethodRef)(struct J9VMThread *vmStruct, J9ConstantPool *constantPool, UDATA cpIndex, UDATA resolveFlags) ;
	struct J9Method*  ( *resolveStaticSplitMethodRef)(struct J9VMThread *vmStruct, J9ConstantPool *ramCP, UDATA splitTableIndex, UDATA resolveFlags) ;
	struct J9Method*  ( *resolveSpecialMethodRef)(struct J9VMThread *vmStruct, J9ConstantPool *constantPool, UDATA cpIndex, UDATA resolveFlags) ;
	struct J9Method*  ( *resolveSpecialSplitMethodRef)(struct J9VMThread *vmStruct, J9ConstantPool *constantPool, UDATA splitTableIndex, UDATA resolveFlags) ;
	void*  ( *resolveStaticFieldRef)(struct J9VMThread *vmStruct, J9Method *method, J9ConstantPool *constantPool, UDATA fieldIndex, UDATA resolveFlags, struct J9ROMFieldShape **resolvedField) ;
	IDATA  ( *resolveInstanceFieldRef)(struct J9VMThread *vmStruct, J9Method *method, J9ConstantPool *constantPool, UDATA fieldIndex, UDATA resolveFlags, struct J9ROMFieldShape **resolvedField) ;
	struct J9ClassLoader*  ( *allocateClassLoader)(struct J9JavaVM* javaVM) ;
	void  (JNICALL *internalSendExceptionConstructor)(struct J9VMThread *vmContext, struct J9Class *exceptionClass, j9object_t exception, j9object_t detailMessage, UDATA constructorIndex) ;
	IDATA  ( *instanceOfOrCheckCast)( struct J9Class *instanceClass, struct J9Class *castClass ) ;
	struct J9Class*  ( *internalCreateRAMClassFromROMClass)(struct J9VMThread *vmThread, struct J9ClassLoader *classLoader, struct J9ROMClass *romClass, UDATA options, struct J9Class* elementClass, j9object_t protectionDomain, struct J9ROMMethod ** methodRemapArray, IDATA entryIndex, I_32 locationType, struct J9Class *classBeingRedefined, struct J9Class *hostClass) ;
	j9object_t  ( *resolveStringRef)(struct J9VMThread *vmStruct, J9ConstantPool *constantPool, UDATA cpIndex, UDATA resolveFlags) ;
	void  ( *exitJavaVM)(struct J9VMThread * vmThread, IDATA rc) ;
	void  ( *internalRunPreInitInstructions)(struct J9Class * ramClass, struct J9VMThread * vmThread) ;
	struct J9Class*  ( *resolveClassRef)(struct J9VMThread *vmStruct, J9ConstantPool *constantPool, UDATA cpIndex, UDATA resolveFlags) ;
	struct J9VMThread*  ( *currentVMThread)(struct J9JavaVM * vm) ;
	void  ( *freeMemorySegment)(struct J9JavaVM *javaVM, struct J9MemorySegment *segment, BOOLEAN freeDescriptor) ;
	void  ( *jniPopFrame)(struct J9VMThread * vmThread, UDATA type) ;
	UDATA  ( *resolveVirtualMethodRef)(struct J9VMThread *vmStruct, J9ConstantPool *constantPool, UDATA cpIndex, UDATA resolveFlags, struct J9Method **resolvedMethod) ;
	struct J9Method*  ( *resolveInterfaceMethodRef)(struct J9VMThread *vmStruct, J9ConstantPool *constantPool, UDATA cpIndex, UDATA resolveFlags) ;
	UDATA  ( *getVTableOffsetForMethod)(struct J9Method * method, struct J9Class *clazz, struct J9VMThread *vmThread) ;
	IDATA  ( *checkVisibility)(struct J9VMThread* currentThread, struct J9Class* sourceClass, struct J9Class* destClass, UDATA modifiers, UDATA lookupOptions) ;
	void  (JNICALL *sendClinit)(struct J9VMThread *vmContext, struct J9Class *clazz) ;
	void  ( *freeStackWalkCaches)(struct J9VMThread * currentThread, J9StackWalkState * walkState) ;
	UDATA  ( *genericStackDumpIterator)(struct J9VMThread *currentThread, J9StackWalkState *walkState) ;
	UDATA  ( *exceptionHandlerSearch)(struct J9VMThread *currentThread, J9StackWalkState *walkState) ;
	UDATA  ( *internalCreateBaseTypePrimitiveAndArrayClasses)(struct J9VMThread *currentThread) ;
	UDATA  ( *isExceptionTypeCaughtByHandler)(struct J9VMThread *currentThread, struct J9Class *thrownExceptionClass, J9ConstantPool *constantPool, UDATA handlerIndex, J9StackWalkState *walkState) ;
#if defined(J9VM_IVE_ROM_IMAGE_HELPERS) || (defined(J9VM_OPT_DYNAMIC_LOAD_SUPPORT) && defined(J9VM_OPT_ROM_IMAGE_SUPPORT))
	struct J9MemorySegment*  ( *romImageNewSegment)(struct J9JavaVM *vm, struct J9ROMImageHeader *header, UDATA isBaseType, struct J9ClassLoader *classLoader ) ;
#endif /* J9VM_IVE_ROM_IMAGE_HELPERS || (J9VM_OPT_DYNAMIC_LOAD_SUPPORT && J9VM_OPT_ROM_IMAGE_SUPPORT) */
	void  (JNICALL *runCallInMethod)(JNIEnv *env, jobject receiver, jclass clazz, jmethodID methodID, void* args) ;
	j9object_t  ( *catUtfToString4)(struct J9VMThread * vmThread, const U_8 *data1, UDATA length1, const U_8 *data2, UDATA length2, const U_8 *data3, UDATA length3, const U_8 *data4, UDATA length4) ;
	struct J9MemorySegmentList*  ( *allocateMemorySegmentList)(struct J9JavaVM * javaVM, U_32 numberOfMemorySegments, U_32 memoryCategory) ;
	struct J9MemorySegmentList*  ( *allocateMemorySegmentListWithFlags)(struct J9JavaVM * javaVM, U_32 numberOfMemorySegments, UDATA flags, U_32 memoryCategory) ;
	void  ( *freeMemorySegmentList)(struct J9JavaVM *javaVM, struct J9MemorySegmentList *segmentList) ;
	struct J9MemorySegment*  ( *allocateMemorySegmentInList)(struct J9JavaVM *javaVM, struct J9MemorySegmentList *segmentList, UDATA size, UDATA type, U_32 memoryCategory) ;
	struct J9MemorySegment*  ( *allocateVirtualMemorySegmentInList)(struct J9JavaVM *javaVM, struct J9MemorySegmentList *segmentList, UDATA size, UDATA type, J9PortVmemParams *vmemParams) ;
	struct J9MemorySegment*  ( *allocateMemorySegmentListEntry)(struct J9MemorySegmentList *segmentList) ;
	struct J9MemorySegment*  ( *allocateClassMemorySegment)(struct J9JavaVM *javaVM, UDATA requiredSize, UDATA segmentType, struct J9ClassLoader *classLoader, UDATA allocationIncrement) ;
#if defined(J9VM_GC_FINALIZATION)
	void  ( *jniResetStackReferences)(JNIEnv *env) ;
#endif /* J9VM_GC_FINALIZATION */
	void  ( *freeClassLoader)(struct J9ClassLoader *classLoader, struct J9JavaVM *javaVM, struct J9VMThread *vmThread, UDATA needsFrameBuild) ;
	jobject  (JNICALL *j9jni_createLocalRef)(JNIEnv *env, j9object_t object) ;
	void  (JNICALL *j9jni_deleteLocalRef)(JNIEnv *env, jobject localRef) ;
	jobject  (JNICALL *j9jni_createGlobalRef)(JNIEnv *env, j9object_t object, jboolean isWeak) ;
	void  (JNICALL *j9jni_deleteGlobalRef)(JNIEnv *env, jobject globalRef, jboolean isWeak) ;
	UDATA  ( *javaCheckAsyncMessages)(struct J9VMThread *currentThread, UDATA throwExceptions) ;
	struct J9JNIFieldID*  ( *getJNIFieldID)(struct J9VMThread *currentThread, struct J9Class* declaringClass, struct J9ROMFieldShape* field, UDATA offset, UDATA *inconsistentData) ;
	struct J9JNIMethodID*  ( *getJNIMethodID)(struct J9VMThread *currentThread, struct J9Method *method) ;
	void  ( *initializeMethodRunAddress)(struct J9VMThread *vmThread, struct J9Method *method) ;
	UDATA  ( *growJavaStack)(struct J9VMThread * vmThread, UDATA newStackSize) ;
	void  ( *freeStacks)(struct J9VMThread * vmThread, UDATA * bp) ;
#if defined(J9VM_INTERP_SIG_QUIT_THREAD) || defined(J9VM_RAS_DUMP_AGENTS)
	void  ( *printThreadInfo)(struct J9JavaVM *vm, struct J9VMThread *self, char *toFile, BOOLEAN allThreads) ;
#endif /* J9VM_INTERP_SIG_QUIT_THREAD || J9VM_RAS_DUMP_AGENTS */
	void  (JNICALL *initializeAttachedThread)(struct J9VMThread *vmContext, const char *name, j9object_t *group, UDATA daemon, struct J9VMThread *initializee) ;
	void  ( *initializeMethodRunAddressNoHook)(struct J9JavaVM* vm, J9Method *method) ;
	void  (JNICALL *sidecarInvokeReflectMethod)(struct J9VMThread *vmContext, jobject methodRef, jobject recevierRef, jobjectArray argsRef) ;
	void  (JNICALL *sidecarInvokeReflectConstructor)(struct J9VMThread *vmContext, jobject constructorRef, jobject recevierRef, jobjectArray argsRef) ;
	struct J9MemorySegmentList*  ( *allocateMemorySegmentListWithSize)(struct J9JavaVM * javaVM, U_32 numberOfMemorySegments, UDATA sizeOfElements, U_32 memoryCategory) ;
	void  ( *freeMemorySegmentListEntry)(struct J9MemorySegmentList *segmentList, struct J9MemorySegment *segment) ;
	void  ( *acquireExclusiveVMAccessFromExternalThread)(struct J9JavaVM * vm) ;
	void  ( *releaseExclusiveVMAccessFromExternalThread)(struct J9JavaVM * vm) ;
#if defined(J9VM_GC_REALTIME)
	UDATA  ( *requestExclusiveVMAccessMetronome)(struct J9JavaVM *vm, UDATA block, UDATA *responsesRequired, UDATA *gcPriority) ;
	void  ( *waitForExclusiveVMAccessMetronome)(struct J9VMThread * vmThread, UDATA responsesRequired) ;
	void  ( *releaseExclusiveVMAccessMetronome)(struct J9VMThread * vmThread) ;
	UDATA  ( *requestExclusiveVMAccessMetronomeTemp)(struct J9JavaVM *vm, UDATA block, UDATA *vmResponsesRequired, UDATA *jniResponsesRequired, UDATA *gcPriority) ;
	void  ( *waitForExclusiveVMAccessMetronomeTemp)(struct J9VMThread * vmThread, UDATA vmResponsesRequired, UDATA jniResponsesRequired) ;
#endif /* J9VM_GC_REALTIME */
#if defined(J9VM_GC_JNI_ARRAY_CACHE)
	void  ( *cleanupVMThreadJniArrayCache)(struct J9VMThread *vmThread) ;
#endif /* J9VM_GC_JNI_ARRAY_CACHE */
	struct J9ObjectMonitor *  ( *objectMonitorInflate)(struct J9VMThread* vmStruct, j9object_t object, UDATA lock) ;
	UDATA  ( *objectMonitorEnterNonBlocking)(struct J9VMThread *currentThread, j9object_t object) ;
	UDATA  ( *objectMonitorEnterBlocking)(struct J9VMThread *currentThread) ;
	void  ( *fillJITVTableSlot)(struct J9VMThread *vmStruct, UDATA *currentSlot, struct J9Method *ramMethod) ;
	IDATA  ( *findArgInVMArgs)(J9PortLibrary *portLibrary, struct J9VMInitArgs* j9vm_args, UDATA match, const char* optionName, const char* optionValue, UDATA doConsumeArgs) ;
	IDATA  ( *optionValueOperations)(J9PortLibrary *portLibrary, struct J9VMInitArgs* j9vm_args, IDATA element, IDATA action, char** valuesBuffer, UDATA bufSize, char delim, char separator, void* reserved) ;
	void  ( *dumpStackTrace)(struct J9VMThread *currentThread) ;
	UDATA  ( *loadJ9DLL)(struct J9JavaVM * vm, struct J9VMDllLoadInfo* info) ;
	UDATA  ( *runJVMOnLoad)(struct J9JavaVM* vm, struct J9VMDllLoadInfo* loadInfo, char* options) ;
	struct J9ROMClass*  ( *checkRomClassForError)( struct J9ROMClass *romClass, struct J9VMThread *vmThread ) ;
	void  ( *setExceptionForErroredRomClass)( struct J9ROMClass *romClass, struct J9VMThread *vmThread ) ;
	UDATA  ( *jniVersionIsValid)(UDATA jniVersion) ;
	struct J9Class*  ( *allClassesStartDo)(struct J9ClassWalkState* state, struct J9JavaVM* vm, struct J9ClassLoader* classLoader) ;
	struct J9Class*  ( *allClassesNextDo)(struct J9ClassWalkState* state) ;
	void  ( *allClassesEndDo)(struct J9ClassWalkState* state) ;
	struct J9Class*  ( *allLiveClassesStartDo)(struct J9ClassWalkState* state, struct J9JavaVM* vm, struct J9ClassLoader* classLoader) ;
	struct J9Class*  ( *allLiveClassesNextDo)(struct J9ClassWalkState* state) ;
	void  ( *allLiveClassesEndDo)(struct J9ClassWalkState* state) ;
	struct J9ClassLoader*  ( *allClassLoadersStartDo)(struct J9ClassLoaderWalkState* state, struct J9JavaVM* vm, UDATA flags) ;
	struct J9ClassLoader*  ( *allClassLoadersNextDo)(struct J9ClassLoaderWalkState* state) ;
	void  ( *allClassLoadersEndDo)(struct J9ClassLoaderWalkState* state) ;
#if defined(J9VM_OPT_ROM_IMAGE_SUPPORT) || defined(J9VM_IVE_ROM_IMAGE_HELPERS)
	struct J9ROMClass*  ( *romClassLoadFromCookie)(struct J9VMThread *vmStruct, U_8 *clsName, UDATA clsNameLength, U_8 *romClassBytes, UDATA romClassLength) ;
#endif /* J9VM_OPT_ROM_IMAGE_SUPPORT || J9VM_IVE_ROM_IMAGE_HELPERS */
#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	void  ( *cleanUpClassLoader)(struct J9VMThread *vmThread, struct J9ClassLoader* classLoader) ;
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */
	UDATA  ( *iterateStackTrace)(struct J9VMThread * vmThread, j9object_t* exception,  UDATA  (*callback) (struct J9VMThread * vmThread, void * userData, UDATA bytecodeOffset, struct J9ROMClass * romClass, struct J9ROMMethod * romMethod, J9UTF8 * fileName, UDATA lineNumber, struct J9ClassLoader* classLoader, struct J9Class* ramClass), void * userData, UDATA pruneConstructors) ;
	char*  ( *getNPEMessage)(struct J9NPEMessageData *npeMsgData);
	void  ( *internalReleaseVMAccessNoMutex)(struct J9VMThread * vmThread) ;
	struct J9HookInterface**  ( *getVMHookInterface)(struct J9JavaVM* vm) ;
	IDATA  ( *internalAttachCurrentThread)(struct J9JavaVM * vm, struct J9VMThread ** p_env, struct J9JavaVMAttachArgs * thr_args, UDATA threadType, void * osThread) ;
	void  ( *setHaltFlag)(struct J9VMThread * vmThread, UDATA flag) ;
	void  ( *returnFromJNI)(struct J9VMThread *env, void * bp) ;
	IDATA  ( *j9stackmap_StackBitsForPC)(J9PortLibrary * portLib, UDATA pc, struct J9ROMClass * romClass, struct J9ROMMethod * romMethod, U_32 * resultArrayBase, UDATA resultArraySize, void * userData, UDATA * (* getBuffer) (void * userData), void (* releaseBuffer) (void * userData)) ;
	struct J9HookInterface**  ( *getJITHookInterface)(struct J9JavaVM* vm) ;
	void  ( *haltThreadForInspection)(struct J9VMThread * currentThread, struct J9VMThread * vmThread) ;
	void  ( *resumeThreadForInspection)(struct J9VMThread * currentThread, struct J9VMThread * vmThread) ;
	void  ( *threadCleanup)(struct J9VMThread * vmThread, UDATA forkedByVM) ;
	j9object_t  ( *walkStackForExceptionThrow)(struct J9VMThread * currentThread, j9object_t exception, UDATA walkOnly) ;
	IDATA  ( *postInitLoadJ9DLL)(struct J9JavaVM* vm, const char* dllName, void* argData) ;
	struct J9UTF8*  ( *annotationElementIteratorNext)(struct J9AnnotationState *state, void **data) ;
	struct J9UTF8*  ( *annotationElementIteratorStart)(struct J9AnnotationState *state, struct J9AnnotationInfoEntry *annotation, void **data) ;
	void*  ( *elementArrayIteratorNext)(struct J9AnnotationState *state) ;
	void*  ( *elementArrayIteratorStart)(struct J9AnnotationState *state, UDATA start, U_32 count) ;
	UDATA  ( *getAllAnnotationsFromAnnotationInfo)(struct J9AnnotationInfo *annInfo, struct J9AnnotationInfoEntry **annotations) ;
	struct J9AnnotationInfoEntry*  ( *getAnnotationDefaultsForAnnotation)(struct J9VMThread *currentThread, struct J9Class *containingClass, struct J9AnnotationInfoEntry *annotation, UDATA flags) ;
	struct J9AnnotationInfoEntry*  ( *getAnnotationDefaultsForNamedAnnotation)(struct J9VMThread *currentThread, struct J9Class *containingClass, char *className, U_32 classNameLength, UDATA flags) ;
	struct J9AnnotationInfo*  ( *getAnnotationInfoFromClass)(struct J9JavaVM *vm, struct J9Class *clazz) ;
	UDATA  ( *getAnnotationsFromAnnotationInfo)(struct J9AnnotationInfo *annInfo, UDATA annotationType, char *memberName, U_32 memberNameLength, char *memberSignature, U_32 memberSignatureLength, struct J9AnnotationInfoEntry **annotations) ;
	struct J9AnnotationInfoEntry*  ( *getAnnotationFromAnnotationInfo)(struct J9AnnotationInfo *annInfo, UDATA annotationType, char *memberName, U_32 memberNameLength, char *memberSignature, U_32 memberSignatureLength, char *annotationName, U_32 annotationNameLength) ;
	void*  ( *getNamedElementFromAnnotation)(struct J9AnnotationInfoEntry *annotation, char *name, U_32 nameLength) ;
	UDATA  ( *registerNativeLibrary)(struct J9VMThread * vmThread, struct J9ClassLoader * classLoader, const char * libName, char * libraryPath, struct J9NativeLibrary** libraryPtr, char* errorBuffer, UDATA bufferLength) ;
	UDATA  ( *registerBootstrapLibrary)(struct J9VMThread * vmThread, const char * libName, struct J9NativeLibrary** libraryPtr, UDATA suppressError) ;
	UDATA  ( *startJavaThread)(struct J9VMThread * currentThread, j9object_t threadObject, UDATA privateFlags,  UDATA osStackSize, UDATA priority, omrthread_entrypoint_t entryPoint, void * entryArg, j9object_t schedulingParameters) ;
	j9object_t  ( *createCachedOutOfMemoryError)(struct J9VMThread * currentThread, j9object_t threadObject) ;
	IDATA  ( *internalTryAcquireVMAccess)(struct J9VMThread * currentThread) ;
	IDATA  ( *internalTryAcquireVMAccessWithMask)(struct J9VMThread * currentThread, UDATA haltFlags) ;
	UDATA  ( *structuredSignalHandler)(struct J9PortLibrary* portLibrary, U_32 gpType, void* gpInfo, void* userData) ;
	UDATA  ( *structuredSignalHandlerVM)(struct J9PortLibrary* portLibrary, U_32 gpType, void* gpInfo, void* userData) ;
	UDATA  ( *addHiddenInstanceField)(struct J9JavaVM *vm, const char *className, const char *fieldName, const char *fieldSignature, UDATA *offsetReturn) ;
	void  ( *reportHotField)(struct J9JavaVM *javaVM, int32_t reducedCpuUtil, J9Class* clazz, uint8_t fieldOffset,  uint32_t reducedFrequency) ;
#ifdef J9VM_OPT_VALHALLA_VALUE_TYPES
	struct J9ROMFieldOffsetWalkResult*  ( *fieldOffsetsStartDo)(struct J9JavaVM *vm, struct J9ROMClass *romClass, struct J9Class *superClazz, struct J9ROMFieldOffsetWalkState *state, U_32 flags, J9FlattenedClassCache *flattenedClassCache) ;
#else
	struct J9ROMFieldOffsetWalkResult*  ( *fieldOffsetsStartDo)(struct J9JavaVM *vm, struct J9ROMClass *romClass, struct J9Class *superClazz, struct J9ROMFieldOffsetWalkState *state, U_32 flags) ;
#endif
	void ( *defaultValueWithUnflattenedFlattenables)(struct J9VMThread *currentThread, struct J9Class *clazz, j9object_t instance) ;
	struct J9ROMFieldOffsetWalkResult*  ( *fieldOffsetsNextDo)(struct J9ROMFieldOffsetWalkState *state) ;
	struct J9ROMFieldShape*  ( *fullTraversalFieldOffsetsStartDo)(struct J9JavaVM *vm, struct J9Class *clazz, struct J9ROMFullTraversalFieldOffsetWalkState *state, U_32 flags) ;
	struct J9ROMFieldShape*  ( *fullTraversalFieldOffsetsNextDo)(struct J9ROMFullTraversalFieldOffsetWalkState *state) ;
	void  ( *setClassCastException)(struct J9VMThread *currentThread, struct J9Class * instanceClass, struct J9Class * castClass) ;
	UDATA  ( *compareStrings)(struct J9VMThread * vmThread, j9object_t string1, j9object_t string2) ;
	UDATA  ( *compareStringToUTF8)(struct J9VMThread * vmThread, j9object_t stringObject, UDATA stringFlags, const U_8 * utfData, UDATA utfLength) ;
	void  ( *prepareForExceptionThrow)(struct J9VMThread * currentThread) ;
	UDATA  ( *verifyQualifiedName)(struct J9VMThread *vmThread, U_8 *className, UDATA classNameLength, UDATA allowedBitsForClassName) ;
	UDATA ( *copyStringToUTF8Helper)(struct J9VMThread *vmThread, j9object_t string, UDATA stringFlags, UDATA stringOffset, UDATA stringLength, U_8 *utf8Data, UDATA utf8DataLength);
	void  (JNICALL *sendCompleteInitialization)(struct J9VMThread *vmContext) ;
	IDATA  ( *J9RegisterAsyncEvent)(struct J9JavaVM * vm, J9AsyncEventHandler eventHandler, void * userData) ;
	IDATA  ( *J9UnregisterAsyncEvent)(struct J9JavaVM * vm, IDATA handlerKey) ;
	IDATA  ( *J9SignalAsyncEvent)(struct J9JavaVM * vm, struct J9VMThread * targetThread, IDATA handlerKey) ;
	IDATA  ( *J9SignalAsyncEventWithoutInterrupt)(struct J9JavaVM * vm, struct J9VMThread * targetThread, IDATA handlerKey) ;
	IDATA  ( *J9CancelAsyncEvent)(struct J9JavaVM * vm, struct J9VMThread * targetThread, IDATA handlerKey) ;
	struct J9Class*  ( *hashClassTableStartDo)(struct J9ClassLoader *classLoader, struct J9HashTableState* walkState, UDATA flags) ;
	struct J9Class*  ( *hashClassTableNextDo)(struct J9HashTableState* walkState) ;
	struct J9PackageIDTableEntry* (*hashPkgTableAt)(J9ClassLoader* classLoader, J9ROMClass* romClass);
	struct J9PackageIDTableEntry*  ( *hashPkgTableStartDo)(struct J9ClassLoader *classLoader, struct J9HashTableState* walkState) ;
	struct J9PackageIDTableEntry*  ( *hashPkgTableNextDo)(struct J9HashTableState* walkState) ;
	void**  ( *ensureJNIIDTable)(struct J9VMThread *currentThread, struct J9Class* clazz) ;
	void  ( *initializeMethodID)(struct J9VMThread * currentThread, J9JNIMethodID * methodID, struct J9Method * method) ;
	IDATA  ( *objectMonitorDestroy)(struct J9JavaVM *vm, struct J9VMThread *vmThread, omrthread_monitor_t monitor) ;
	void  ( *objectMonitorDestroyComplete)(struct J9JavaVM *vm, struct J9VMThread *vmThread) ;
	U_8*  ( *buildNativeFunctionNames)(struct J9JavaVM * javaVM, struct J9Method* ramMethod, struct J9Class* ramClass, UDATA nameOffset) ;
	IDATA  ( *resolveInstanceFieldRefInto)(struct J9VMThread *vmStruct, J9Method *method, J9ConstantPool *constantPool, UDATA fieldIndex, UDATA resolveFlags, struct J9ROMFieldShape **resolvedField, J9RAMFieldRef *ramCPEntry) ;
	struct J9Method*  ( *resolveInterfaceMethodRefInto)(struct J9VMThread *vmStruct, J9ConstantPool *constantPool, UDATA cpIndex, UDATA resolveFlags, struct J9RAMInterfaceMethodRef *ramCPEntry) ;
	struct J9Method*  ( *resolveSpecialMethodRefInto)(struct J9VMThread *vmStruct, J9ConstantPool *constantPool, UDATA cpIndex, UDATA resolveFlags, struct J9RAMSpecialMethodRef *ramCPEntry) ;
	void*  ( *resolveStaticFieldRefInto)(struct J9VMThread *vmStruct, J9Method *method, J9ConstantPool *constantPool, UDATA fieldIndex, UDATA resolveFlags, struct J9ROMFieldShape **resolvedField, struct J9RAMStaticFieldRef *ramCPEntry) ;
	struct J9Method*  ( *resolveStaticMethodRefInto)(struct J9VMThread *vmStruct, J9ConstantPool *constantPool, UDATA cpIndex, UDATA resolveFlags, struct J9RAMStaticMethodRef *ramCPEntry) ;
	UDATA  ( *resolveVirtualMethodRefInto)(struct J9VMThread *vmStruct, J9ConstantPool *constantPool, UDATA cpIndex, UDATA resolveFlags, struct J9Method **resolvedMethod, struct J9RAMVirtualMethodRef *ramCPEntry) ;
	IDATA  ( *findObjectDeadlockedThreads)(struct J9VMThread *currentThread, j9object_t **pDeadlockedThreads, j9object_t **pBlockingObjects, UDATA flags) ;
	struct J9ROMClass*  ( *findROMClassFromPC)(struct J9VMThread *vmThread, UDATA methodPC, struct J9ClassLoader **resultClassLoader) ;
	IDATA  ( *j9localmap_LocalBitsForPC)(struct J9PortLibrary * portLib, struct J9ROMClass * romClass, struct J9ROMMethod * romMethod, UDATA pc, U_32 * resultArrayBase, void * userData, UDATA * (* getBuffer) (void * userData), void (* releaseBuffer) (void * userData)) ;
	int  ( *fillInDgRasInterface)(struct DgRasInterface *dri) ;
	void  ( *rasStartDeferredThreads)(struct J9JavaVM* vm) ;
	int  ( *initJVMRI)(struct J9JavaVM* vm) ;
	int  ( *shutdownJVMRI)(struct J9JavaVM* vm) ;
	IDATA  ( *getOwnedObjectMonitors)(struct J9VMThread *currentThread, struct J9VMThread *targetThread, struct J9ObjectMonitorInfo *info, IDATA infoLen) ;
#if !defined(J9VM_SIZE_SMALL_CODE)
	void  ( *fieldIndexTableRemove)(struct J9JavaVM* javaVM, struct J9Class *ramClass) ;
#endif /* J9VM_SIZE_SMALL_CODE */
	UDATA  ( *getJavaThreadPriority)(struct J9JavaVM* vm, struct J9VMThread* thread) ;
	void  ( *atomicOrIntoConstantPool)(struct J9JavaVM *vm, struct J9Method *method, UDATA bits) ;
	void  ( *atomicAndIntoConstantPool)(struct J9JavaVM *vm, struct J9Method *method, UDATA bits) ;
	void  ( *setNativeOutOfMemoryError)(struct J9VMThread * vmThread, U_32 moduleName, U_32 messageNumber) ;
	char*  ( *illegalAccessMessage)(struct J9VMThread *currentThread, IDATA badMemberModifier, J9Class *senderClass, J9Class *targetClass, IDATA errorType) ;
	void  ( *setThreadForkOutOfMemoryError)(struct J9VMThread * vmThread, U_32 moduleName, U_32 messageNumber) ;
	UDATA  ( *initializeNativeLibrary)(struct J9JavaVM * javaVM, struct J9NativeLibrary* library) ;
	void*  ( *addStatistic)(struct J9JavaVM* javaVM, U_8 * name, U_8 dataType) ;
	void*  ( *getStatistic)(struct J9JavaVM* javaVM, U_8 * name) ;
	IDATA  ( *setVMThreadNameFromString)(struct J9VMThread *currentThread, struct J9VMThread *vmThread, j9object_t nameObject) ;
	struct J9Method*  ( *findJNIMethod)(struct J9VMThread* currentThread, J9Class* clazz, char* name, char* signature) ;
	const char*  ( *getJ9VMVersionString)(struct J9JavaVM * vm) ;
	j9object_t  ( *resolveMethodTypeRef)(struct J9VMThread *vmThread, J9ConstantPool *ramCP, UDATA cpIndex, UDATA resolveFlags) ;
	void  (JNICALL *sendFromMethodDescriptorString)(struct J9VMThread *vmThread, J9UTF8 *descriptor, J9ClassLoader *classLoader, J9Class *appendArgType) ;
	UDATA  ( *addToBootstrapClassLoaderSearch)(struct J9JavaVM * vm, const char * pathSegment, UDATA classLoaderType, BOOLEAN enforceJarRestriction) ;
	UDATA  ( *addToSystemClassLoaderSearch)(struct J9JavaVM * vm, const char * pathSegment, UDATA classLoaderType, BOOLEAN enforceJarRestriction) ;
	UDATA  ( *queryLogOptions)(struct J9JavaVM *vm, I_32 buffer_size, void *options_buffer, I_32 *data_size) ;
	UDATA  ( *setLogOptions)(struct J9JavaVM *vm, char *options) ;
	void  ( *exitJavaThread)(struct J9JavaVM * vm) ;
	void  ( *cacheObjectMonitorForLookup)(struct J9JavaVM* vm, struct J9VMThread* vmStruct, struct J9ObjectMonitor* objectMonitor) ;
	void*  ( *jniArrayAllocateMemoryFromThread)(struct J9VMThread* vmThread, UDATA sizeInBytes) ;
	void  ( *jniArrayFreeMemoryFromThread)(struct J9VMThread* vmThread, void* location) ;
	void  (JNICALL *sendForGenericInvoke)(struct J9VMThread *vmThread, j9object_t methodHandle, j9object_t methodType, UDATA dropFirstArg) ;
	void  (JNICALL *jitFillOSRBuffer)(struct J9VMThread *vmContext, void *osrBlock) ;
	void  (JNICALL *sendResolveMethodHandle)(struct J9VMThread *vmThread, UDATA cpIndex, J9ConstantPool *ramCP, J9Class *definingClass, J9ROMNameAndSignature* nameAndSig) ;
	j9object_t ( *resolveOpenJDKInvokeHandle)(struct J9VMThread *vmThread, J9ConstantPool *ramCP, UDATA cpIndex, UDATA resolveFlags) ;
	j9object_t ( *resolveConstantDynamic)(struct J9VMThread *vmThread, J9ConstantPool *ramCP, UDATA cpIndex, UDATA resolveFlags) ;
	j9object_t  ( *resolveInvokeDynamic)(struct J9VMThread *vmThread, J9ConstantPool *ramCP, UDATA cpIndex, UDATA resolveFlags) ;
	void  (JNICALL *sendResolveOpenJDKInvokeHandle)(struct J9VMThread *vmThread, J9ConstantPool *ramCP, UDATA cpIndex, I_32 refKind, J9Class *resolvedClass, J9ROMNameAndSignature* nameAndSig) ;
	void  (JNICALL *sendResolveConstantDynamic)(struct J9VMThread *vmThread, J9ConstantPool *ramCP, UDATA cpIndex, J9ROMNameAndSignature* nameAndSig, U_16* bsmData) ;
	void  (JNICALL *sendResolveInvokeDynamic)(struct J9VMThread *vmThread, J9ConstantPool *ramCP, UDATA callSiteIndex, J9ROMNameAndSignature* nameAndSig, U_16* bsmData) ;
	j9object_t  ( *resolveMethodHandleRef)(struct J9VMThread *vmThread, J9ConstantPool *ramCP, UDATA cpIndex, UDATA resolveFlags) ;
	UDATA  ( *resolveNativeAddress)(struct J9VMThread *currentThread, J9Method *nativeMethod, UDATA runtimeBind) ;
	void  ( *clearHaltFlag)(struct J9VMThread * vmThread, UDATA flag) ;
	void  ( *setHeapOutOfMemoryError)(struct J9VMThread * currentThread) ;
	jint  ( *initializeHeapOOMMessage)(struct J9VMThread *currentThread) ;
	void  ( *threadAboutToStart)(struct J9VMThread *currentThread) ;
	void  ( *mustHaveVMAccess)(struct J9VMThread * vmThread) ;
#if defined(J9VM_PORT_ZOS_CEEHDLRSUPPORT)
	void  ( *javaAndCStacksMustBeInSync)(struct J9VMThread * vmThread, BOOLEAN fromJIT) ;
#endif /* J9VM_PORT_ZOS_CEEHDLRSUPPORT */
	struct J9Class*  ( *findFieldSignatureClass)(struct J9VMThread *vmStruct, J9ConstantPool *ramCP, UDATA fieldRefCpIndex) ;
	void  ( *walkBytecodeFrameSlots)(J9StackWalkState *walkState, struct J9Method *method, UDATA offsetPC, UDATA *pendingBase, UDATA pendingStackHeight, UDATA *localBase, UDATA numberOfLocals, UDATA alwaysLocalMap) ;
	void*  ( *jniNativeMethodProperties)(struct J9VMThread *currentThread, struct J9Method *jniNativeMethod, UDATA *properties) ;
	void  ( *invalidJITReturnAddress)(J9StackWalkState *walkState) ;
	struct J9ClassLoader*  ( *internalAllocateClassLoader)(struct J9JavaVM *javaVM, j9object_t classLoaderObject) ;
	void  ( *initializeClass)(struct J9VMThread *currentThread, struct J9Class *clazz) ;
	void  ( *threadParkImpl)(struct J9VMThread* vmThread, IDATA timeoutIsEpochRelative, I_64 timeout) ;
	void  ( *threadUnparkImpl)(struct J9VMThread* vmThread, j9object_t threadObject) ;
	IDATA  ( *monitorWaitImpl)(struct J9VMThread* vmThread, j9object_t object, I_64 millis, I_32 nanos, UDATA interruptable) ;
	IDATA  ( *threadSleepImpl)(struct J9VMThread* vmThread, I_64 millis, I_32 nanos) ;
	omrthread_monitor_t  ( *getMonitorForWait)(struct J9VMThread* vmThread, j9object_t object) ;
	void  ( *jvmPhaseChange)(struct J9JavaVM* vm, UDATA phase) ;
	void  ( *prepareClass)(struct J9VMThread *currentThread, struct J9Class *clazz) ;
	struct J9SFMethodTypeFrame*  ( *buildMethodTypeFrame)(struct J9VMThread *currentThread, j9object_t methodType) ;
	void  ( *fatalRecursiveStackOverflow)(struct J9VMThread *currentThread) ;
	void  ( *setIllegalAccessErrorNonPublicInvokeInterface)(struct J9VMThread *currentThread, J9Method *method) ;
	IDATA ( *createThreadWithCategory)(omrthread_t* handle, UDATA stacksize, UDATA priority, UDATA suspend, omrthread_entrypoint_t entrypoint, void* entryarg, U_32 category) ;
	IDATA ( *attachThreadWithCategory)(omrthread_t* handle, U_32 category) ;
	struct J9Method* ( *searchClassForMethod)(J9Class *clazz, U_8 *name, UDATA nameLength, U_8 *sig, UDATA sigLength) ;
#if defined(J9VM_INTERP_ATOMIC_FREE_JNI)
	void  ( *internalEnterVMFromJNI)(struct J9VMThread * currentThread) ;
	void  ( *internalExitVMToJNI)(struct J9VMThread * currentThread) ;
#endif /* J9VM_INTERP_ATOMIC_FREE_JNI */
	struct J9HashTable* ( *hashModuleNameTableNew)(struct J9JavaVM * vm, U_32 initialSize) ;
	struct J9HashTable* ( *hashModulePointerTableNew)(struct J9JavaVM * vm, U_32 initialSize) ;
	struct J9HashTable* ( *hashPackageTableNew)(struct J9JavaVM * vm, U_32 initialSize) ;
	struct J9HashTable* ( *hashModuleExtraInfoTableNew)(struct J9JavaVM * vm, U_32 initialSize) ;
	struct J9HashTable* ( *hashClassLocationTableNew)(struct J9JavaVM * vm, U_32 initialSize) ;
	struct J9Module* ( *findModuleForPackageUTF8)(struct J9VMThread *currentThread, struct J9ClassLoader *classLoader, struct J9UTF8 *packageName);
	struct J9Module* ( *findModuleForPackage)(struct J9VMThread *currentThread, struct J9ClassLoader *classLoader, U_8 *packageName, U_32 packageNameLen);
	struct J9ModuleExtraInfo* ( *findModuleInfoForModule)(struct J9VMThread *currentThread, struct J9ClassLoader *classLoader, J9Module *j9module);
	struct J9ClassLocation* ( *findClassLocationForClass)(struct J9VMThread *currentThread, struct J9Class *clazz);
	jclass ( *getJimModules) (struct J9VMThread *currentThread);
	UDATA ( *initializeClassPath)(struct J9JavaVM *vm, char *classPath, U_8 classPathSeparator, U_16 cpFlags, BOOLEAN initClassPathEntry, struct J9ClassPathEntry **classPathEntries);
	IDATA ( *initializeClassPathEntry) (struct J9JavaVM * javaVM, struct J9ClassPathEntry *cpEntry);
	BOOLEAN ( *setBootLoaderModulePatchPaths)(struct J9JavaVM * javaVM, struct J9Module * j9module, const char * moduleName);
	BOOLEAN (*isAnyClassLoadedFromPackage)(struct J9ClassLoader* classLoader, U_8 *pkgName, UDATA pkgNameLength);
	void (*freeJ9Module)(struct J9JavaVM *javaVM, struct J9Module *j9module);
	void ( *acquireSafePointVMAccess)(struct J9VMThread * vmThread);
	void ( *releaseSafePointVMAccess)(struct J9VMThread * vmThread);
	void ( *setIllegalAccessErrorReceiverNotSameOrSubtypeOfCurrentClass)(struct J9VMThread *vmStruct, struct J9Class *receiverClass, struct J9Class *currentClass);
	U_32 ( *getVMRuntimeState)(struct J9JavaVM *vm);
	BOOLEAN ( *updateVMRuntimeState)(struct J9JavaVM *vm, U_32 newState);
	U_32 ( *getVMMinIdleWaitTime)(struct J9JavaVM *vm);
#if defined(J9VM_RAS_EYECATCHERS)
	void ( *rasSetServiceLevel)(struct J9JavaVM *vm, const char *runtimeVersion);
#endif /* J9VM_RAS_EYECATCHERS */
#if defined(J9VM_INTERP_ATOMIC_FREE_JNI_USES_FLUSH)
	void ( *flushProcessWriteBuffers)(struct J9JavaVM *vm);
#endif /* J9VM_INTERP_ATOMIC_FREE_JNI_USES_FLUSH */
	IDATA ( *registerPredefinedHandler)(struct J9JavaVM *vm, U_32 signal, void **oldOSHandler);
	IDATA ( *registerOSHandler)(struct J9JavaVM *vm, U_32 signal, void *newOSHandler, void **oldOSHandler);
	void ( *throwNativeOOMError)(JNIEnv *env, U_32 moduleName, U_32 messageNumber);
	void ( *throwNewJavaIoIOException)(JNIEnv *env, const char *message);
#if JAVA_SPEC_VERSION >= 11
	UDATA ( *loadAndVerifyNestHost)(struct J9VMThread *vmThread, struct J9Class *clazz, UDATA options);
	void ( *setNestmatesError)(struct J9VMThread *vmThread, struct J9Class *nestMember, struct J9Class *nestHost, IDATA errorCode);
#endif /* JAVA_SPEC_VERSION >= 11 */
	BOOLEAN ( *areValueTypesEnabled)(struct J9JavaVM *vm);
	J9Class* ( *peekClassHashTable)(struct J9VMThread* currentThread, J9ClassLoader* classLoader, U_8* className, UDATA classNameLength);
#if defined(J9VM_OPT_JITSERVER)
	BOOLEAN ( *isJITServerEnabled )(struct J9JavaVM *vm);
#endif /* J9VM_OPT_JITSERVER */
	IDATA ( *createJoinableThreadWithCategory)(omrthread_t* handle, UDATA stacksize, UDATA priority, UDATA suspend, omrthread_entrypoint_t entrypoint, void* entryarg, U_32 category) ;
	BOOLEAN ( *valueTypeCapableAcmp)(struct J9VMThread *currentThread, j9object_t lhs, j9object_t rhs) ;
	BOOLEAN ( *isNameOrSignatureQtype)(J9UTF8 *utfWrapper) ;
	BOOLEAN ( *isClassRefQtype)(struct J9Class *cpContextClass, U_16 cpIndex) ;
	UDATA ( *getFlattenableFieldOffset)(struct J9Class *fieldOwner, J9ROMFieldShape *field);
	BOOLEAN ( *isFlattenableFieldFlattened)(J9Class *fieldOwner, J9ROMFieldShape *field);
	struct J9Class* ( *getFlattenableFieldType)(J9Class *fieldOwner, J9ROMFieldShape *field);
	UDATA ( *getFlattenableFieldSize)(struct J9VMThread* currentThread, J9Class *fieldOwner, J9ROMFieldShape *field);
	UDATA ( *arrayElementSize)(J9ArrayClass* arrayClass);
	j9object_t ( *getFlattenableField)(struct J9VMThread *currentThread, J9RAMFieldRef *cpEntry, j9object_t receiver, BOOLEAN fastPath);
	j9object_t ( *cloneValueType)(struct J9VMThread *currentThread, struct J9Class *receiverClass, j9object_t original, BOOLEAN fastPath);
	void ( *putFlattenableField)(struct J9VMThread *currentThread, struct J9RAMFieldRef *cpEntry, j9object_t receiver, j9object_t paramObject);
#if JAVA_SPEC_VERSION >= 15
	I_32 (*checkClassBytes) (struct J9VMThread *currentThread, U_8* classBytes, UDATA classBytesLength, U_8* segment, U_32 segmentLength);
#endif /* JAVA_SPEC_VERSION >= 15 */
	void ( *storeFlattenableArrayElement)(struct J9VMThread *currentThread, j9object_t receiverObject, U_32 index, j9object_t paramObject);
	j9object_t ( *loadFlattenableArrayElement)(struct J9VMThread *currentThread, j9object_t receiverObject, U_32 index, BOOLEAN fast);
	UDATA ( *jniIsInternalClassRef)(struct J9JavaVM *vm, jobject ref);
	BOOLEAN (*objectIsBeingWaitedOn)(struct J9VMThread *currentThread, struct J9VMThread *targetThread, j9object_t obj);
	BOOLEAN (*areValueBasedMonitorChecksEnabled)(struct J9JavaVM *vm);
	BOOLEAN (*fieldContainsRuntimeAnnotation)(struct J9VMThread *currentThread, J9Class *clazz, UDATA cpIndex, J9UTF8 *annotationName);
	BOOLEAN (*methodContainsRuntimeAnnotation)(struct J9VMThread *currentThread, J9Method *method, J9UTF8 *annotationName);
	J9ROMFieldShape* (*findFieldExt)(struct J9VMThread *vmStruct, J9Class *clazz, U_8 *fieldName, UDATA fieldNameLength, U_8 *signature, UDATA signatureLength, J9Class **definingClass, UDATA *offsetOrAddress, UDATA options);
#if defined(J9VM_OPT_CRIU_SUPPORT)
	BOOLEAN (*jvmCheckpointHooks)(struct J9VMThread *currentThread);
	BOOLEAN (*jvmRestoreHooks)(struct J9VMThread *currentThread);
	BOOLEAN (*isCRIUSupportEnabled)(struct J9VMThread *currentThread);
	BOOLEAN (*isCheckpointAllowed)(struct J9VMThread *currentThread);
#endif /* defined(J9VM_OPT_CRIU_SUPPORT) */
} J9InternalVMFunctions;

/* Jazz 99339: define a new structure to replace JavaVM so as to pass J9NativeLibrary to JVMTIEnv  */
typedef struct J9InvocationJavaVM {
	const struct JNIInvokeInterface_ * functions;
	struct J9JavaVM * j9vm;
	void * reserved1_identifier;
	J9NativeLibrary * reserved2_library;
} J9InvocationJavaVM;

/* @ddr_namespace: map_to_type=J9VMThread */

typedef struct J9VMThread {
	struct JNINativeInterface_* functions;
	struct J9JavaVM* javaVM;
	UDATA* arg0EA;
	UDATA* bytecodes;
	UDATA* sp;
	U_8* pc;
	struct J9Method* literals;
	UDATA jitStackFrameFlags;
	j9object_t jitException;
	j9object_t currentException;
	UDATA* stackOverflowMark;
	UDATA* stackOverflowMark2;
	U_8* heapAlloc;
	U_8* heapTop;
	IDATA tlhPrefetchFTA;
	U_8* nonZeroHeapAlloc;
	U_8* nonZeroHeapTop;
	IDATA nonZeroTlhPrefetchFTA;
	omrthread_monitor_t publicFlagsMutex;
	UDATA publicFlags;
#if defined(J9VM_ENV_DATA64) /* The ifdeffed field does not affect the aligmment check below */
#if defined(OMR_GC_COMPRESSED_POINTERS) && defined(OMR_GC_FULL_POINTERS)
	UDATA compressObjectReferences;
#endif /* defined(OMR_GC_COMPRESSED_POINTERS) && defined(OMR_GC_FULL_POINTERS) */
#endif /* defined(J9VM_ENV_DATA64) */
	j9object_t threadObject;
	void* lowTenureAddress;
	void* highTenureAddress;
	void* heapBaseForActiveCardTable;
	void* activeCardTableBase;
	UDATA heapSizeForActiveCardTable;
	void* heapBaseForBarrierRange0;
	UDATA heapSizeForBarrierRange0;
	UDATA* jniLocalReferences;
	/**
	 * tempSlot is an overloaded field used for multiple purposes.
	 *
	 * Note: Listed below is one such use, and the other uses of this field still need to be
	 * documented.
	 *
	 * 1. OpenJDK MethodHandle implementation
	 *		For signature-polymorphic INL calls from compiled code for the following methods:
	 *		* java/lang/invoke/MethodHandle.invokeBasic
	 *		* java/lang/invoke/MethodHandle.linkToStatic
	 *		* java/lang/invoke/MethodHandle.linkToSpecial
	 *		* java/lang/invoke/MethodHandle.linkToVirtual
	 *		* java/lang/invoke/MethodHandle.linkToInterface
	 *		the compiled code performs a store to this field right before the INL call. The
	 *		stored value represents the number of stack slots occupied by the args, and the
	 *		interpreter uses the value to locate the beginning of the arguments on the stack.
	 */
	UDATA tempSlot;
	void* jitReturnAddress;
	/* floatTemp1 must be 8-aligned */
#if 1 && !defined(J9VM_ENV_DATA64) /* Change to 0 or 1 based on number of fields above */
	U_32 paddingToAlignFloatTemp;
#endif
	/**
	 * floatTemp1 is an overloaded field used for multiple purposes.
	 *
	 * Note: Listed below is one such use, and the other uses of this field still need to be
	 * documented.
	 *
	 * 1. OpenJDK MethodHandle implementation
	 *		For signature-polymorphic INL calls from compiled code for the following methods:
	 *		* java/lang/invoke/MethodHandle.linkToStatic
	 *		* java/lang/invoke/MethodHandle.linkToSpecial
	 *		the compiled code performs a store to this field right before the INL call. The
	 *		stored value represents the number of args for the unresolved invokedynamic/invokehandle
	 *		call. This is required because linkToStatic calls are generated for unresolved
	 *		invokedynamic/invokehandle, where the JIT cannot determine whether the appendix object of
	 *		the callSite/invokeCache table entry is valid. As a result, the JIT code may push NULL
	 *		appendix objects on the stack which the interpreter must remove. To determine that, the
	 *		interpreter would need to know the number of arguments of method.
	 */
	void* floatTemp1;
	void* floatTemp2;
	void* floatTemp3;
	void* floatTemp4;
	UDATA returnValue;
	UDATA returnValue2;
	UDATA* objectFlagSpinLockAddress; /* used by JIT. Do not remove, even though NO VM code uses it see matching comment in J9VMThread.st */
	struct J9JavaStack* stackObject;
	omrthread_t osThread;
	UDATA inspectionSuspendCount;
	UDATA inspectorCount;
	U_32 eventFlags;
	U_32 osrFrameIndex;
	void* codertTOC;
	U_8* cardTableVirtualStart;
	j9object_t allocateObjectSavePrivate1; /* deprecated -- use OMRVMThread::savedObject1 */
	j9object_t allocateObjectSavePrivate2; /* deprecated -- use OMRVMThread::savedObject2 */
	j9object_t stopThrowable;
	j9object_t outOfMemoryError;
	UDATA* jniCurrentReference;
	UDATA* jniLimitReference;
	struct J9VMThread* linkNext;
	struct J9VMThread* linkPrevious;
	UDATA privateFlags;
	UDATA jitTOC;
	UDATA ferReturnType;
	/* U_64 fields should be 8-aligned */
	U_64 ferReturnValue;
	U_64 mgmtBlockedTimeTotal;
	U_64 mgmtBlockedTimeStart;
	U_64 mgmtWaitedTimeTotal;
	U_64 mgmtWaitedTimeStart;
	UDATA jniVMAccessCount;
	UDATA debugEventData1;
	UDATA debugEventData2;
	UDATA debugEventData3;
	UDATA debugEventData4;
	UDATA debugEventData5;
	UDATA debugEventData6;
	UDATA debugEventData7;
	UDATA debugEventData8;
	struct J9StackElement* classLoadingStack;
#if defined(J9VM_OPT_VALHALLA_VALUE_TYPES)
	struct J9StackElement* verificationStack;
#endif /* defined(J9VM_OPT_VALHALLA_VALUE_TYPES) */
	UDATA jitTransitionJumpSlot;
	omrthread_monitor_t gcClassUnloadingMutex;
	struct J9VMThread* gcClassUnloadingThreadPrevious;
	struct J9VMThread* gcClassUnloadingThreadNext;
	struct J9StackWalkState* stackWalkState;
	struct J9VMEntryLocalStorage* entryLocalStorage;
	UDATA gpProtected;
	struct J9VMGCSublistFragment gcRememberedSet;
	struct MM_GCRememberedSetFragment sATBBarrierRememberedSetFragment;
	void* gcTaskListPtr;
	UDATA* dropBP;
	UDATA dropFlags;
	struct J9Pool* monitorEnterRecordPool;
	struct J9MonitorEnterRecord* monitorEnterRecords;
	UDATA* jniArrayCache;
	UDATA* jniArrayCache2;
	struct J9StackWalkState inlineStackWalkState;
	struct J9JITDecompilationInfo* decompilationStack;
	struct J9ModronThreadLocalHeap allocateThreadLocalHeap;
	struct J9ModronThreadLocalHeap nonZeroAllocateThreadLocalHeap;
	void* sidecarEvent;
	struct PortlibPTBuffers_struct* ptBuffers;
	j9object_t blockingEnterObject;
	void* gcExtensions;
	void* gpInfo;
	void* jitVMwithThreadInfo;
	U_8* profilingBufferEnd;
	U_8* profilingBufferCursor;
	UDATA* j2iFrame;
	UDATA currentOSStackFree;
	UDATA mgmtBlockedCount;
	UDATA mgmtWaitedCount;
	UDATA mgmtBlockedStart;
	UDATA mgmtWaitedStart;
	UDATA cardTableShiftSize;
	void* aotVMwithThreadInfo;
	UDATA asyncEventFlags;
	j9object_t forceEarlyReturnObjectSlot;
	struct J9MonitorEnterRecord* jniMonitorEnterRecords;
	struct J9DLTInformationBlock dltBlock;
	struct J9VMGCSegregatedAllocationCacheEntry *segregatedAllocationCache;
	struct J9StackWalkState* activeWalkState;
	void* jniCalloutArgs;
	struct J9VMThread* exclusiveVMAccessQueueNext;
	struct J9VMThread* exclusiveVMAccessQueuePrevious;
	j9object_t javaLangThreadLocalCache;
	UDATA jitCountDelta;
	UDATA maxProfilingCount;
	j9objectmonitor_t objectMonitorLookupCache[J9VM_OBJECT_MONITOR_CACHE_SIZE];
	UDATA jniCriticalCopyCount;
	UDATA jniCriticalDirectCount;
	struct J9Pool* jniReferenceFrames;
	U_32 ludclInlineDepth;
	U_32 ludclBPOffset;
#if defined(J9VM_JIT_FREE_SYSTEM_STACK_POINTER)
	UDATA systemStackPointer;
#if 0 && !defined(J9VM_ENV_DATA64) /* Change to 0 or 1 based on number of fields above */
	U_32 zosPadTo8;
#endif /* !J9VM_ENV_DATA64 */
#endif /* J9VM_JIT_FREE_SYSTEM_STACK_POINTER */
#if defined(J9VM_PORT_ZOS_CEEHDLRSUPPORT)
	UDATA leConditionConvertedToJavaException;
	UDATA percolatedConditionAndTriggeredDiagnostics;
#endif /* J9VM_PORT_ZOS_CEEHDLRSUPPORT */
#if defined(J9VM_JIT_RUNTIME_INSTRUMENTATION)
	UDATA jitCurrentRIFlags;
	UDATA jitPendingRIFlags;
	struct J9RIParameters *riParameters;
#if !defined(J9VM_ENV_DATA64) && defined(J9VM_JIT_FREE_SYSTEM_STACK_POINTER)
	U_32 riPadTo8;
#endif /* !J9VM_ENV_DATA64 */
#endif /* J9VM_JIT_RUNTIME_INSTRUMENTATION */
#if defined(J9VM_ZOS_3164_INTEROPERABILITY)
	U_32 jniEnv31;
	U_32 jniEnvPadTo8; /* Possible to optimize with future guarded U_32 member in ENV_DATA64. */
#endif /* J9VM_ZOS_3164_INTEROPERABILITY */
	UDATA* osrJittedFrameCopy;
	struct J9OSRBuffer* osrBuffer;
	void* osrReturnAddress;
	void* osrScratchBuffer;
	void* jitArtifactSearchCache;
	void* jitExceptionHandlerCache;
	void* jitPrivateData;
	struct J9Method* jitMethodToBeCompiled;
	UDATA privateFlags2;
	struct OMR_VMThread* omrVMThread;
#if defined(J9VM_OPT_JAVA_OFFLOAD_SUPPORT)
	UDATA javaOffloadState;
	UDATA invokedCalldisp;
#endif /* J9VM_OPT_JAVA_OFFLOAD_SUPPORT */
	void *gpuInfo;
	void *startOfMemoryBlock;
	UDATA inNative;
	struct J9JITDecompilationInfo* lastDecompilation;
#if 0 && !defined(J9VM_ENV_DATA64) /* Change to 0 or 1 based on number of fields above */
	U_32 paddingToAlignTo8;
#endif
#if defined(J9VM_JIT_TRANSACTION_DIAGNOSTIC_THREAD_BLOCK)
	U_8 transactionDiagBlock[256];
#endif /* J9VM_JIT_TRANSACTION_DIAGNOSTIC_THREAD_BLOCK */
#if defined(OMR_GC_CONCURRENT_SCAVENGER)
	struct J9GSParameters gsParameters;
	UDATA readBarrierRangeCheckBase;
	UDATA readBarrierRangeCheckTop;
#if defined(OMR_GC_COMPRESSED_POINTERS)
	U_32 readBarrierRangeCheckBaseCompressed;
	U_32 readBarrierRangeCheckTopCompressed;
#endif /* OMR_GC_COMPRESSED_POINTERS */
#endif /* OMR_GC_CONCURRENT_SCAVENGER */
	UDATA safePointCount;
} J9VMThread;

#define J9VMTHREAD_ALIGNMENT  0x100
#define J9VMTHREAD_RESERVED_C_STACK_FRACTION  8
#define J9VMTHREAD_STATE_RUNNING  1
#define J9VMTHREAD_STATE_BLOCKED  2
#define J9VMTHREAD_STATE_WAITING  4
#define J9VMTHREAD_STATE_SLEEPING  8
#define J9VMTHREAD_STATE_SUSPENDED  16
#define J9VMTHREAD_STATE_DEAD  32
#define J9VMTHREAD_STATE_WAITING_TIMED  64
#define J9VMTHREAD_STATE_PARKED  0x80
#define J9VMTHREAD_STATE_PARKED_TIMED  0x100
#define J9VMTHREAD_STATE_INTERRUPTED  0x200
#define J9VMTHREAD_STATE_UNKNOWN  0x400
#define J9VMTHREAD_STATE_UNREADABLE  0x800
#define J9VMSTATE_MAJOR  0xFFFF0000
#define J9VMSTATE_MINOR  0xFFFF
#define J9VMSTATE_INTERPRETER  0x10000
#define J9VMSTATE_GC  0x20000
#define J9VMSTATE_GROW_STACK  0x30000
#define J9VMSTATE_JNI  0x40000
#define J9VMSTATE_JNI_FROM_JIT  0x40001
#define J9VMSTATE_JIT  0x50000
#define J9VMSTATE_JIT_CODEGEN  0x5FF00
#define J9VMSTATE_JIT_OPTIMIZER  0x500FF
#define J9VMSTATE_BCVERIFY  0x60000
#define J9VMSTATE_RTVERIFY  0x70000
#define J9VMSTATE_SHAREDCLASS_FIND  0x80001
#define J9VMSTATE_SHAREDCLASS_STORE  0x80002
#define J9VMSTATE_SHAREDCLASS_MARKSTALE  0x80003
#define J9VMSTATE_SHAREDAOT_FIND  0x80004
#define J9VMSTATE_SHAREDAOT_STORE  0x80005
#define J9VMSTATE_SHAREDDATA_FIND  0x80006
#define J9VMSTATE_SHAREDDATA_STORE  0x80007
#define J9VMSTATE_SHAREDCHARARRAY_FIND  0x80008
#define J9VMSTATE_SHAREDCHARARRAY_STORE  0x80009
#define J9VMSTATE_ATTACHEDDATA_STORE  0x8000A
#define J9VMSTATE_ATTACHEDDATA_FIND  0x8000B
#define J9VMSTATE_ATTACHEDDATA_UPDATE  0x8000C
#define J9VMSTATE_SNW_STACK_VALIDATE  0x110000
#define J9VMSTATE_GP  0xFFFF0000
#define J9VMTHREAD_OBJECT_MONITOR_CACHE_SIZE  J9VM_OBJECT_MONITOR_CACHE_SIZE

#define J9VMTHREAD_BLOCKINGENTEROBJECT(currentThread, targetThread) J9VMTHREAD_JAVAVM(targetThread)->memoryManagerFunctions->j9gc_objaccess_readObjectFromInternalVMSlot((currentThread), J9VMTHREAD_JAVAVM(targetThread), (j9object_t*)&((targetThread)->blockingEnterObject))
#define J9VMTHREAD_SET_BLOCKINGENTEROBJECT(currentThread, targetThread, value) J9VMTHREAD_JAVAVM(targetThread)->memoryManagerFunctions->j9gc_objaccess_storeObjectToInternalVMSlot((currentThread), (j9object_t*)&((targetThread)->blockingEnterObject), (value))

#define J9VMTHREAD_REFERENCE_SIZE(vmThread) (J9VMTHREAD_COMPRESS_OBJECT_REFERENCES(vmThread) ? sizeof(U_32) : sizeof(UDATA))
#define J9VMTHREAD_OBJECT_HEADER_SIZE(vmThread) (J9VMTHREAD_COMPRESS_OBJECT_REFERENCES(vmThread) ? sizeof(J9ObjectCompressed) : sizeof(J9ObjectFull))
#define J9VMTHREAD_CONTIGUOUS_HEADER_SIZE(vmThread) (J9VMTHREAD_COMPRESS_OBJECT_REFERENCES(vmThread) ? sizeof(J9IndexableObjectContiguousCompressed) : sizeof(J9IndexableObjectContiguousFull))
#define J9VMTHREAD_DISCONTIGUOUS_HEADER_SIZE(vmThread) (J9VMTHREAD_COMPRESS_OBJECT_REFERENCES(vmThread) ? sizeof(J9IndexableObjectDiscontiguousCompressed) : sizeof(J9IndexableObjectDiscontiguousFull))

typedef struct J9CRIUCheckpointState {
	BOOLEAN isCheckPointAllowed;
	BOOLEAN isNonPortableRestoreMode;
} J9CRIUCheckpointState;

typedef struct J9ReflectFunctionTable {
	jobject  ( *idToReflectMethod)(struct J9VMThread* vmThread, jmethodID methodID) ;
	jobject  ( *idToReflectField)(struct J9VMThread* vmThread, jfieldID fieldID) ;
	jmethodID  ( *reflectMethodToID)(struct J9VMThread *vmThread, jobject reflectMethod) ;
	jfieldID  ( *reflectFieldToID)(struct J9VMThread *vmThread, jobject reflectField) ;
	j9object_t  ( *createConstructorObject)(struct J9Method *ramMethod, struct J9Class *declaringClass, j9array_t parameterTypes, struct J9VMThread* vmThread) ;
	j9object_t  ( *createDeclaredConstructorObject)(struct J9Method *ramMethod, struct J9Class *declaringClass, j9array_t parameterTypes, struct J9VMThread* vmThread) ;
	j9object_t  ( *createDeclaredMethodObject)(struct J9Method *ramMethod, struct J9Class *declaringClass, j9array_t parameterTypes, struct J9VMThread* vmThread) ;
	j9object_t  ( *createMethodObject)(struct J9Method *ramMethod, struct J9Class *declaringClass, j9array_t parameterTypes, struct J9VMThread* vmThread) ;
	void  ( *fillInReflectMethod)(j9object_t methodObject, struct J9Class *declaringClass, jmethodID methodID, struct J9VMThread *vmThread) ;
	struct J9JNIFieldID*  ( *idFromFieldObject)(struct J9VMThread* vmStruct, j9object_t declaringClassObject, j9object_t fieldObject) ;
	struct J9JNIMethodID*  ( *idFromMethodObject)(struct J9VMThread* vmStruct, j9object_t methodObject) ;
	struct J9JNIMethodID*  ( *idFromConstructorObject)(struct J9VMThread* vmStruct, j9object_t constructorObject) ;
	j9object_t  ( *createFieldObject)(struct J9VMThread *vmThread, struct J9ROMFieldShape *romField, struct J9Class *declaringClass, BOOLEAN isStaticField) ;
} J9ReflectFunctionTable;

/* @ddr_namespace: map_to_type=J9VMRuntimeStateListener */

typedef struct J9VMRuntimeStateListener {
	U_32 vmRuntimeState;
	U_32 runtimeStateListenerState;
	struct J9VMThread *runtimeStateListenerVMThread;
	omrthread_monitor_t runtimeStateListenerMutex;
	U_32 minIdleWaitTime;
	UDATA idleMinFreeHeap;
	UDATA idleTuningFlags;
} J9VMRuntimeStateListener;

/* Values for J9VMRuntimeStateListener.vmRuntimeState
 * These values are reflected in the Java class library code(RuntimeMXBean)
 */
#define J9VM_RUNTIME_STATE_ACTIVE 1
#define J9VM_RUNTIME_STATE_IDLE 2

/* Values for J9VMRuntimeStateListener.runtimeStateListenerState */
#define J9VM_RUNTIME_STATE_LISTENER_UNINITIALIZED 0
#define J9VM_RUNTIME_STATE_LISTENER_STARTED 1
#define J9VM_RUNTIME_STATE_LISTENER_STOP 2
#define J9VM_RUNTIME_STATE_LISTENER_ABORT 3
#define J9VM_RUNTIME_STATE_LISTENER_TERMINATED 4

/* @ddr_namespace: map_to_type=J9JavaVM */

typedef struct J9JavaVM {
	struct J9InternalVMFunctions* internalVMFunctions;
	struct J9JavaVM* javaVM;
	void* reserved1_identifier;
	struct J9NativeLibrary *reserved2_library;
	struct J9PortLibrary * portLibrary;
	UDATA j2seVersion;
	void* zipCachePool;
	struct J9VMInterface vmInterface;
	UDATA dynamicLoadClassAllocationIncrement;
	struct J9TranslationBufferSet* dynamicLoadBuffers;
	struct J9JImageIntf *jimageIntf;
	struct J9VMInitArgs* vmArgsArray;
	struct J9Pool* dllLoadTable;
	char* j2seRootDirectory;
	char* j9libvmDirectory;
	struct J9Pool* systemProperties;
	omrthread_monitor_t systemPropertiesMutex;
	U_8* javaHome;
	void* cInterpreter;
	UDATA (*bytecodeLoop)(struct J9VMThread *currentThread);
	struct J9CudaGlobals* cudaGlobals;
	struct J9SharedClassConfig* sharedClassConfig;
	U_8* bootstrapClassPath;
	U_32 runtimeFlags;
	U_32 extendedRuntimeFlags;
	U_32 extendedRuntimeFlags2;
	UDATA zeroOptions;
	struct J9Pool* hotFieldClassInfoPool;
	omrthread_monitor_t hotFieldClassInfoPoolMutex;
	omrthread_monitor_t globalHotFieldPoolMutex;
	struct J9ClassLoader* systemClassLoader;
	UDATA sigFlags;
	void* vmLocalStorageFunctions;
	omrthread_monitor_t unsafeMemoryTrackingMutex;
	struct J9UnsafeMemoryBlock* unsafeMemoryListHead;
	UDATA pathSeparator;
	struct J9JavaVM* linkPrevious;
	struct J9JavaVM* linkNext;
	struct J9InternalVMLabels* internalVMLabels;
	struct J9MemoryManagerFunctions* memoryManagerFunctions;
	struct J9MemorySegmentList* memorySegments;
	struct J9MemorySegmentList* objectMemorySegments;
	struct J9MemorySegmentList* classMemorySegments;
	UDATA stackSize;
	UDATA ramClassAllocationIncrement;
	UDATA romClassAllocationIncrement;
	UDATA memoryMax;
	UDATA directByteBufferMemoryMax;
	omrthread_monitor_t exclusiveAccessMutex;
	struct J9Pool* jniGlobalReferences;
	struct J9Pool* jniWeakGlobalReferences;
	omrthread_monitor_t vmThreadListMutex;
	j9object_t selectorHashTable;
	struct J9Pool* classLoaderBlocks;
	struct J9Class* voidReflectClass;
	struct J9Class* booleanReflectClass;
	struct J9Class* charReflectClass;
	struct J9Class* floatReflectClass;
	struct J9Class* doubleReflectClass;
	struct J9Class* byteReflectClass;
	struct J9Class* shortReflectClass;
	struct J9Class* intReflectClass;
	struct J9Class* longReflectClass;
	struct J9Class* booleanArrayClass;
	struct J9Class* charArrayClass;
	struct J9Class* floatArrayClass;
	struct J9Class* doubleArrayClass;
	struct J9Class* byteArrayClass;
	struct J9Class* shortArrayClass;
	struct J9Class* intArrayClass;
	struct J9Class* longArrayClass;
	struct J9RAMConstantPoolItem jclConstantPool[J9VM_VMCONSTANTPOOL_SIZE];
	struct J9VMThread* mainThread;
	struct J9VMThread* deadThreadList;
	UDATA exclusiveAccessState;
	omrthread_monitor_t classTableMutex;
	UDATA anonClassCount;
	UDATA totalThreadCount;
	UDATA daemonThreadCount;
	omrthread_t finalizeMainThread;
	omrthread_monitor_t finalizeMainMonitor;
	omrthread_monitor_t processReferenceMonitor; /* the monitor for synchronizing between reference process and j9gc_wait_for_reference_processing() (only for Java 9 and later) */
	UDATA processReferenceActive;
	IDATA finalizeMainFlags;
	UDATA exclusiveAccessResponseCount;
	j9object_t destroyVMState;
	omrthread_monitor_t segmentMutex;
	omrthread_monitor_t jniFrameMutex;
	UDATA verboseLevel;
	UDATA finalizeFlags;
	UDATA rsOverflow;
	UDATA maxStackUse;
	UDATA maxCStackUse;
	/* extensionClassLoader holds the platform class loader in Java 11+ */
	struct J9ClassLoader* extensionClassLoader;
	struct J9ClassLoader* applicationClassLoader;
	UDATA doPrivilegedMethodID1;
	UDATA doPrivilegedMethodID2;
	UDATA doPrivilegedWithContextMethodID1;
	UDATA doPrivilegedWithContextMethodID2;
	void* defaultMemorySpace;
	j9object_t* systemThreadGroupRef;
	omrthread_monitor_t classLoaderBlocksMutex;
	UDATA methodHandleCompileCount;
	struct J9JITConfig* jitConfig;
	j9object_t tenureAge;
	struct J9Pool* classLoadingStackPool;
	omrthread_monitor_t finalizeRunFinalizationMutex;
	UDATA finalizeRunFinalizationCount;
	void* vmLocalStorage[256];
	void  (JNICALL *exitHook)(jint exitCode) ;
	void  (JNICALL *abortHook)(void) ;
	UDATA finalizeForceClassLoaderUnloadCount;
	UDATA classLoaderAllocationCount;
	j9object_t scvTenureRatioHigh;
	j9object_t scvTenureRatioLow;
#if defined(J9VM_ENV_SHARED_LIBS_USE_GLOBAL_TABLE) || defined(J9VM_ENV_CALL_VIA_TABLE)
	UDATA jitTOC;
#endif /* J9VM_ENV_SHARED_LIBS_USE_GLOBAL_TABLE || J9VM_ENV_CALL_VIA_TABLE */
	UDATA stackWalkCount;
	omrthread_monitor_t aotRuntimeInitMutex;
	UDATA  ( *aotFindAndInitializeMethodEntryPoint)(struct J9JavaVM *javaVM, struct J9ConstantPool *ramCP, struct J9Method *method, struct J9JXEDescription *jxeDescription) ;
	UDATA  ( *aotInitializeJxeEntryPoint)(struct J9JavaVM *javaVM, struct J9JXEDescription *description, UDATA beforeBootstrap) ;
	void  ( *freeAotRuntimeInfo)(struct J9JavaVM *javaVM, void * aotRuntimeInfo) ;
	UDATA aotDllHandle;
	struct J9ROMImageHeader* arrayROMClasses;
	struct J9BytecodeVerificationData* bytecodeVerificationData;
	char* jclDLLName;
	UDATA defaultOSStackSize;
#if defined(J9VM_ENV_SHARED_LIBS_USE_GLOBAL_TABLE) || defined(J9VM_ENV_CALL_VIA_TABLE)
	UDATA magicLinkageValue;
#endif /* J9VM_ENV_SHARED_LIBS_USE_GLOBAL_TABLE || J9VM_ENV_CALL_VIA_TABLE */
	void*  ( *hookVMEvent)(struct J9JavaVM *javaVM, UDATA eventNumber, void * newHandler) ;
	void* jniFunctionTable;
	void* jniSendTarget;
	UDATA thrMaxSpins1BeforeBlocking;
	UDATA thrMaxSpins2BeforeBlocking;
	UDATA thrMaxYieldsBeforeBlocking;
	UDATA thrMaxTryEnterSpins1BeforeBlocking;
	UDATA thrMaxTryEnterSpins2BeforeBlocking;
	UDATA thrMaxTryEnterYieldsBeforeBlocking;
	UDATA thrNestedSpinning;
	UDATA thrTryEnterNestedSpinning;
	UDATA thrDeflationPolicy;
	UDATA gcOptions;
	UDATA  ( *unhookVMEvent)(struct J9JavaVM *javaVM, UDATA eventNumber, void * currentHandler, void * oldHandler) ;
	UDATA classLoadingMaxStack;
	U_8* callInReturnPC;
#if defined(J9VM_OPT_METHOD_HANDLE)
	U_8* impdep1PC;
#endif /* defined(J9VM_OPT_METHOD_HANDLE) */
	UDATA  ( *jitExceptionHandlerSearch)(struct J9VMThread *currentThread, struct J9StackWalkState *walkState) ;
	UDATA  ( *walkStackFrames)(struct J9VMThread *currentThread, struct J9StackWalkState *walkState) ;
	UDATA  ( *walkFrame)(struct J9StackWalkState *walkState) ;
	UDATA  ( *jitWalkStackFrames)(struct J9StackWalkState *walkState) ;
	UDATA  ( *jitGetOwnedObjectMonitors)(struct J9StackWalkState *walkState) ;
	UDATA jniArrayCacheMaxSize;
#if defined(J9VM_ENV_SHARED_LIBS_USE_GLOBAL_TABLE) || defined(J9VM_ENV_CALL_VIA_TABLE)
	UDATA jclTOC;
	UDATA hookTOC;
#endif /* J9VM_ENV_SHARED_LIBS_USE_GLOBAL_TABLE || J9VM_ENV_CALL_VIA_TABLE */
	UDATA initialStackSize;
	struct J9VerboseStruct* verboseStruct;
	void* codertOldAboutToBootstrap;
	void* codertOldVMShutdown;
	void* jitOldAboutToBootstrap;
	void* jitOldVMShutdown;
	struct J9GCVMInfo gcInfo;
	void* gcExtensions;
	UDATA gcAllocationType;
	UDATA gcWriteBarrierType;
	UDATA gcReadBarrierType;
	UDATA gcPolicy;
	void  ( *J9SigQuitShutdown)(struct J9JavaVM *vm) ;
	U_32 globalEventFlags;
	void  ( *sidecarInterruptFunction)(struct J9VMThread * vmThread) ;
	struct J9ReflectFunctionTable reflectFunctions;
	omrthread_monitor_t bindNativeMutex;
	J9SidecarExitHook sidecarExitHook;
	J9SidecarExitFunction * sidecarExitFunctions;
	struct J9HashTable** monitorTables;
	UDATA monitorTableCount;
	omrthread_monitor_t monitorTableMutex;
	struct J9MonitorTableListEntry* monitorTableList;
	struct J9Pool* monitorTableListPool;
	UDATA thrStaggerStep;
	UDATA thrStaggerMax;
	UDATA thrStagger;
	struct JNINativeInterface_* EsJNIFunctions;
	struct J9RAS* j9ras;
	struct J9RASdumpFunctions* j9rasDumpFunctions;
	void* floatJITExitInterpreter;
	void* doubleJITExitInterpreter;
	char* sigquitToFileDir;
	UDATA initializeSlotsOnTLHAllocate;
	UDATA stackWalkVerboseLevel;
	UDATA whackedPointerCounter;
	void* j9rasGlobalStorage;
	UDATA jclFlags;
	char* jclSysPropBuffer;
#if defined(J9VM_OPT_JAVA_OFFLOAD_SUPPORT)
	void  ( *javaOffloadSwitchOnFunc)(omrthread_t thr) ;
	void  ( *javaOffloadSwitchOffFunc)(omrthread_t thr) ;
	void  ( *javaOffloadSwitchOnWithMethodFunc)(struct J9VMThread *vmThread, struct J9Method *methodWraper) ;
	void  ( *javaOffloadSwitchOffWithMethodFunc)(struct J9VMThread *vmThread, struct J9Method *methodWraper) ;
	void  ( *javaOffloadSwitchJDBCWithMethodFunc)(struct J9VMThread *vmThread, struct J9Method *methodWraper) ;
	void  ( *javaOffloadSwitchOnWithReasonFunc)(struct J9VMThread *vmThread, UDATA reason) ;
	void  ( *javaOffloadSwitchOffWithReasonFunc)(struct J9VMThread *vmThread, UDATA reason) ;
	void  ( *javaOffloadSwitchOnNoEnvWithReasonFunc)(struct J9JavaVM *vm, omrthread_t thr, UDATA reason) ;
	void  ( *javaOffloadSwitchOffNoEnvWithReasonFunc)(struct J9JavaVM *vm, omrthread_t thr, UDATA reason) ;
#endif /* J9VM_OPT_JAVA_OFFLOAD_SUPPORT */
	UDATA maxInvariantLocalTableNodeCount;
	JVMExt jvmExtensionInterface;
	jclass srMethodAccessor;
	jclass srConstructorAccessor;
	struct J9Method* jlrMethodInvoke;
	struct J9Method* jliMethodHandleInvokeWithArgs;
	struct J9Method* jliMethodHandleInvokeWithArgsList;
	jclass jliArgumentHelper;
	void  ( *verboseStackDump)(struct J9VMThread *walkThread, const char *msg) ;
	jclass java_nio_Buffer;
	jclass sun_nio_ch_DirectBuffer;
	jfieldID java_nio_Buffer_address;
	jfieldID java_nio_Buffer_capacity;
	void* jvmtiData;
	struct J9VMHookInterface hookInterface;
	UDATA requiredDebugAttributes;
	UDATA stackSizeIncrement;
	struct J9JavaLangManagementData *managementData;
	IDATA  ( *setVerboseState)(struct J9JavaVM *vm, struct J9VerboseSettings *verboseOptions, char **errorString) ;
	omrthread_monitor_t verboseStateMutex;
	struct J9SharedClassPreinitConfig* sharedClassPreinitConfig;
	omrthread_monitor_t runtimeFlagsMutex;
	omrthread_monitor_t extendedMethodFlagsMutex;
	jclass java_nio_DirectByteBuffer;
	jmethodID java_nio_DirectByteBuffer_init;
	void* heapBase;
	void* heapTop;
	omrthread_monitor_t asyncEventMutex;
	struct J9AsyncEventRecord asyncEventHandlers[J9VM_ASYNC_MAX_HANDLERS];
	struct J9HashTable* classLoadingConstraints;
	UDATA* vTableScratch;
	UDATA vTableScratchSize;
#if defined(J9VM_JIT_CLASS_UNLOAD_RWMONITOR)
	omrthread_rwmutex_t classUnloadMutex;
#else
	omrthread_monitor_t classUnloadMutex;
#endif
	UDATA java2J9ThreadPriorityMap[11];
	UDATA j9Thread2JavaPriorityMap[12];
	UDATA priorityAsyncEventDispatch;
	UDATA priorityAsyncEventDispatchNH;
	UDATA priorityTimerDispatch;
	UDATA priorityTimerDispatchNH;
	UDATA priorityMetronomeAlarm;
	UDATA priorityMetronomeTrace;
	UDATA priorityJitSample;
	UDATA priorityJitCompile;
	UDATA priorityPosixSignalDispatch;
	UDATA priorityPosixSignalDispatchNH;
	IDATA priorityRealtimePriorityShift;
	struct J9CheckJNIData checkJNIData;
	void* j9rasdumpGlobalStorage;
	struct J9HashTable* contendedLoadTable;
#if defined(OMR_GC_COMPRESSED_POINTERS)
	UDATA compressedPointersShift;
#endif /* OMR_GC_COMPRESSED_POINTERS */
#if defined(OMR_GC_CONCURRENT_SCAVENGER) && defined(J9VM_ARCH_S390)
	void ( *invokeJ9ReadBarrier)(struct J9VMThread *currentThread);
#endif
	UDATA objectAlignmentInBytes;
	UDATA objectAlignmentShift;
	struct J9InitializerMethods initialMethods;
	struct J9HashTable* fieldIndexTable;
	UDATA fieldIndexThreshold;
	omrthread_monitor_t fieldIndexMutex;
	IDATA  ( *localMapFunction)(struct J9PortLibrary * portLib, struct J9ROMClass * romClass, struct J9ROMMethod * romMethod, UDATA pc, U_32 * resultArrayBase, void * userData, UDATA * (* getBuffer) (void * userData), void (* releaseBuffer) (void * userData)) ;
	UDATA realtimeHeapMapBasePageRounded;
	UDATA* realtimeHeapMapBits;
	struct J9VMGCSizeClasses *realtimeSizeClasses;
	struct J9HashTable* nativeMethodBindTable;
	U_8* mapMemoryBuffer;
	U_8* mapMemoryResultsBuffer;
	UDATA mapMemoryBufferSize;
	omrthread_monitor_t mapMemoryBufferMutex;
	omrthread_monitor_t jclCacheMutex;
	UDATA arrayletLeafSize;
	UDATA arrayletLeafLogSize;
	struct J9VMThread* exclusiveVMAccessQueueHead;
	struct J9VMThread* exclusiveVMAccessQueueTail;
	omrthread_monitor_t statisticsMutex;
	struct J9Statistic* nextStatistic;
	I_32  (JNICALL *loadAgentLibraryOnAttach)(struct J9JavaVM * vm, const char * library, const char *options, UDATA decorate) ;
	struct J9AttachContext attachContext;
	UDATA hotSwapCount;
	UDATA zombieThreadCount;
	struct J9HiddenInstanceField* hiddenInstanceFields;
	omrthread_monitor_t hiddenInstanceFieldsMutex;
	struct J9ROMFieldShape* hiddenLockwordFieldShape;
	struct J9ROMFieldShape* hiddenFinalizeLinkFieldShape;
	UDATA modulePointerOffset;
	omrthread_monitor_t jniCriticalLock;
	UDATA jniCriticalResponseCount;
	struct J9SharedInvariantInternTable* sharedInvariantInternTable;
	struct J9SharedCacheAPI* sharedCacheAPI;
	UDATA lockwordMode;
	struct J9HashTable* lockwordExceptions;
	void  ( *sidecarClearInterruptFunction)(struct J9VMThread * vmThread) ;
	UDATA phase;
#if defined(J9VM_PORT_ZOS_CEEHDLRSUPPORT)
	UDATA leConditionConvertedToJavaException;
#endif /* J9VM_PORT_ZOS_CEEHDLRSUPPORT */
	void* originalSIGPIPESignalAction;
	void* finalizeWorkerData;
	j9object_t* heapOOMStringRef;
	UDATA strCompEnabled;
	struct J9IdentityHashData* identityHashData;
	UDATA minimumSuperclassArraySize;
#if defined(J9VM_JIT_RUNTIME_INSTRUMENTATION)
	IDATA jitRIHandlerKey;
#endif /* J9VM_JIT_RUNTIME_INSTRUMENTATION */
	UDATA osrGlobalBufferSize;
	void* osrGlobalBuffer;
	omrthread_monitor_t osrGlobalBufferLock;
	UDATA  ( *collectJitPrivateThreadData)(struct J9VMThread *currentThread, struct J9StackWalkState *walkState) ;
	char* alternateJitDir;
	UDATA debugField1;
	UDATA segregatedAllocationCacheSize;
	struct OMR_VM* omrVM;
	struct OMR_Runtime* omrRuntime;
	UDATA vmThreadSize;
	omrthread_monitor_t nativeLibraryMonitor;
	UDATA freePreviousClassLoaders;
	struct J9ClassLoader* anonClassLoader;
	UDATA doPrivilegedWithContextPermissionMethodID1;
	UDATA doPrivilegedWithContextPermissionMethodID2;
	UDATA nativeLibrariesLoadMethodID;
#if defined(J9VM_INTERP_CUSTOM_SPIN_OPTIONS)
	struct J9Pool *customSpinOptions;
#endif /* J9VM_INTERP_CUSTOM_SPIN_OPTIONS */
	UDATA romMethodSortThreshold;
#if defined(J9VM_THR_ASYNC_NAME_UPDATE)
	IDATA threadNameHandlerKey;
#endif /* J9VM_THR_ASYNC_NAME_UPDATE */
	char *decompileName;
	omrthread_monitor_t classLoaderModuleAndLocationMutex;
	struct J9Pool* modularityPool;
	struct J9Module *javaBaseModule;
	struct J9Module *unamedModuleForSystemLoader;
	J9ClassPathEntry *modulesPathEntry;
	jclass jimModules;
	jmethodID addReads;
	jmethodID addExports;
	jmethodID addOpens;
	jmethodID addUses;
	jmethodID addProvides;
	UDATA addModulesCount;
	UDATA safePointState;
	UDATA safePointResponseCount;
	struct J9VMRuntimeStateListener vmRuntimeStateListener;
#if defined(J9VM_INTERP_ATOMIC_FREE_JNI_USES_FLUSH)
#if defined(J9UNIX) || defined(AIXPPC)
	J9PortVmemIdentifier exclusiveGuardPage;
	omrthread_monitor_t flushMutex;
#elif defined(WIN32) /* J9UNIX || AIXPPC  */
	void *flushFunction;
#endif /* WIN32 */
#endif /* J9VM_INTERP_ATOMIC_FREE_JNI_USES_FLUSH */
	omrthread_monitor_t constantDynamicMutex;
#ifdef J9VM_OPT_VALHALLA_VALUE_TYPES
	UDATA valueFlatteningThreshold;
	omrthread_monitor_t valueTypeVerificationMutex;
	struct J9Pool* valueTypeVerificationStackPool;
	UDATA verificationMaxStack;
#endif /* defined(J9VM_OPT_VALHALLA_VALUE_TYPES) */
	UDATA dCacheLineSize;
	/* Indicates processor support for committing cache lines to memory. On X86,
	 * examples would be CLFLUSH or CLWB instructions. This field takes the value
	 * of the feature constants listed in j9port.h
	 */
	U_32 cpuCacheWritebackCapabilities;
	U_32 enableGlobalLockReservation;
	U_32 reservedTransitionThreshold;
	U_32 reservedAbsoluteThreshold;
	U_32 minimumReservedRatio;
	U_32 cancelAbsoluteThreshold;
	U_32 minimumLearningRatio;
#ifdef J9VM_OPT_OPENJDK_METHODHANDLE
	UDATA vmindexOffset;
	UDATA vmtargetOffset;
	UDATA mutableCallSiteInvalidationCookieOffset;
#endif /* defined(J9VM_OPT_OPENJDK_METHODHANDLE) */
#if defined(J9VM_ZOS_3164_INTEROPERABILITY)
	U_32 javaVM31;
	U_32 javaVM31PadTo8; /* Possible to optimize with future guarded U_32 member in ENV_DATA64. */
#endif /* defined(J9VM_ZOS_3164_INTEROPERABILITY) */
#if defined(J9VM_OPT_CRIU_SUPPORT)
	jclass criuResultTypeClass;
	jfieldID criuSupportSuccess;
	jfieldID criuSupportUnsupportedOperation;
	jfieldID criuSupportInvalidArguments;
	jfieldID criuSupportSystemCheckpointFailure;
	jfieldID criuSupportJVMCheckpointFailure;
	jfieldID criuSupportJVMRestoreFailure;
	jclass criuResultClass;
	jmethodID criuResultInit;
#endif /* defined(J9VM_OPT_CRIU_SUPPORT) */
	J9CRIUCheckpointState *checkpointState;
} J9JavaVM;

#define J9VM_PHASE_NOT_STARTUP  2
#define J9VM_DEBUG_ATTRIBUTE_CAN_POP_FRAMES  0x20000
#define J9VM_PHASE_STARTUP  1
#define J9VM_DEBUG_ATTRIBUTE_CAN_ACCESS_LOCALS  16
#define J9VM_DEBUG_ATTRIBUTE_ALLOW_USER_HEAP_WALK  0x100000
#define J9VM_DEBUG_ATTRIBUTE_ALLOW_RETRANSFORM  0x400000
#define J9VM_DEBUG_ATTRIBUTE_LOCAL_VARIABLE_TABLE  4
#define J9VM_DEFLATION_POLICY_ASAP  1
#define J9VM_IDENTIFIER  (UDATA)0x4A39564D
#define J9VM_DEFLATION_POLICY_SMART  2
#define J9VM_DEBUG_ATTRIBUTE_RECORD_ALL  0x8000
#define J9VM_DEBUG_ATTRIBUTE_CAN_REDEFINE_CLASSES  0x10000
#define J9VM_DEBUG_ATTRIBUTE_UNUSED_0x80000  0x80000
#define J9VM_DEBUG_ATTRIBUTE_MAINTAIN_ORIGINAL_METHOD_ORDER  0x1000000
#define J9VM_DEBUG_ATTRIBUTE_LINE_NUMBER_TABLE  1
#define J9VM_DEBUG_ATTRIBUTE_SOURCE_FILE  2
#define J9VM_ZERO_DEFAULT_OPTIONS  1
#define J9VM_ZERO_SHAREBOOTZIPCACHE  1
#define J9VM_DEBUG_ATTRIBUTE_SOURCE_DEBUG_EXTENSION  8
#define J9VM_DEBUG_ATTRIBUTE_UNUSED_0x200000  0x200000
#define J9VM_PHASE_EARLY_STARTUP  0
#define J9VM_DEBUG_ATTRIBUTE_MAINTAIN_FULL_INLINE_MAP  0x40000
#define J9VM_DEBUG_ATTRIBUTE_UNUSED_0x800000  0x800000
#define J9VM_DEFLATION_POLICY_NEVER  0

/* objectMonitorEnterNonBlocking return codes */
#define J9_OBJECT_MONITOR_OOM 0
#define J9_OBJECT_MONITOR_VALUE_TYPE_IMSE 1
/*
 * Currently, not needed but reserving it for future use
 *
 * #define J9_OBJECT_MONITOR_PRIMITIVE_WRAPPER_IMSE 2
 */
#define J9_OBJECT_MONITOR_BLOCKING 3

#if JAVA_SPEC_VERSION >= 16
#define J9_OBJECT_MONITOR_ENTER_FAILED(rc) ((rc) < J9_OBJECT_MONITOR_BLOCKING)
#else /* JAVA_SPEC_VERSION >= 16 */
#define J9_OBJECT_MONITOR_ENTER_FAILED(rc) ((rc) == J9_OBJECT_MONITOR_OOM)
#endif /* JAVA_SPEC_VERSION >= 16 */
#define J9JAVAVM_REFERENCE_SIZE(vm) (J9JAVAVM_COMPRESS_OBJECT_REFERENCES(vm) ? sizeof(U_32) : sizeof(UDATA))
#define J9JAVAVM_OBJECT_HEADER_SIZE(vm) (J9JAVAVM_COMPRESS_OBJECT_REFERENCES(vm) ? sizeof(J9ObjectCompressed) : sizeof(J9ObjectFull))
#define J9JAVAVM_CONTIGUOUS_HEADER_SIZE(vm) (J9JAVAVM_COMPRESS_OBJECT_REFERENCES(vm) ? sizeof(J9IndexableObjectContiguousCompressed) : sizeof(J9IndexableObjectContiguousFull))
#define J9JAVAVM_DISCONTIGUOUS_HEADER_SIZE(vm) (J9JAVAVM_COMPRESS_OBJECT_REFERENCES(vm) ? sizeof(J9IndexableObjectDiscontiguousCompressed) : sizeof(J9IndexableObjectDiscontiguousFull))

/* Data block for JIT instance field watch reporting */

typedef struct J9JITWatchedInstanceFieldData {
	J9Method *method;		/* Currently executing method */
	UDATA location;			/* Bytecode PC index */
	UDATA offset;			/* Field offset (not including header) */
} J9JITWatchedInstanceFieldData;

/* Data block for JIT instance field watch reporting */

typedef struct J9JITWatchedStaticFieldData {
	J9Method *method;		/* Currently executing method */
	UDATA location;			/* Bytecode PC index */
	void *fieldAddress;		/* Address of static field storage */
	J9Class *fieldClass;	/* Declaring class of static field */
} J9JITWatchedStaticFieldData;

/* The C stack frame in which compiled code runs */

#if J9_INLINE_JNI_MAX_ARG_COUNT != 32
#error Math is depending on J9_INLINE_JNI_MAX_ARG_COUNT being 32
#endif

typedef struct J9CInterpreterStackFrame {
#if defined(J9VM_ARCH_S390)
#if defined(J9ZOS390)
	/* Z/OS
	 *
	 * Always use "small frame" - stack must always be 32-byte aligned.
	 * The system stack pointer is always 2048 bytes lower than the
	 * actual top of stack (referred to as "stack bias").
	 */
	UDATA preservedGPRs[12]; /* r4-r15 preserved by callee */
	UDATA reserved1[4];
	UDATA argRegisterSave[3]; /* r1-r3, set by callee as necessary */
	UDATA reserved2;
	UDATA outgoingArguments[J9_INLINE_JNI_MAX_ARG_COUNT];
	UDATA extraArgSlot; /* 31-bit requires extra argument slot */
	UDATA align[2]; /* Padding to maintain 16-byte alignment */
	UDATA fpcCEEHDLR; /* FPC save slot for CEEHDLR on 31-bit */
	U_8 preservedFPRs[8 * 8]; /* Save area for fp8-fp15 */
	U_8 preservedVRs[8 * 16]; /* Save area for v16-v23 */
#if defined(J9VM_PORT_ZOS_CEEHDLRSUPPORT)
	U_8 gprCEEHDLR[16 * 8]; /* r0-r15 full 8-byte save for CEEHDLR */
#endif /* J9VM_PORT_ZOS_CEEHDLRSUPPORT */
#else /* J9ZOS390 */
	/* z/Linux
	 *
	 * Stack must always be 8-byte aligned.
	 */
	UDATA backChain; /* caller SP */
	UDATA reserved;
	UDATA argRegisterSave[4]; /* r2-r5 - callee saves in caller's frame */
	UDATA preservedGPRs[10]; /* r6-r15 - callee saves in caller's frame */
	U_8 argumentFPRs[4 * 8]; /* fp0/2/4/6 - callee saves in caller's frame */
#if defined(J9ZTPF)
#if !defined(J9VM_ENV_DATA64)
#error zTPF not supported on 32 bit
#endif /* !J9VM_ENV_DATA64 */
	UDATA zTPF[36];
#endif /* J9ZTPF */
	UDATA outgoingArguments[J9_INLINE_JNI_MAX_ARG_COUNT];
	U_8 preservedFPRs[8 * 8]; /* fp8-fp15 - callee saves in own frame */
#endif /* J9ZOS390 */
	U_8 jitGPRs[16 * 8]; /* r0-r15 full 8-byte */
	U_8 jitFPRs[16 * 8]; /* f0-f15 */
	U_8 jitVRs[32 * 16]; /* v0-v31 */
#elif defined(J9VM_ARCH_POWER) /* J9VM_ARCH_S390 */
#if defined(AIXPPC)
	/* AIX
	 *
	 * Stack must be 16-byte aligned on 32-bit, 64-byte aligned on 64-bit.
	 */
	UDATA backChain; /* caller SP */
	UDATA preservedCR; /* callee saves in caller frame */
	UDATA preservedLR; /* callee saves in caller frame */
	UDATA tocSave;
	UDATA reserved;
	UDATA currentTOC; /* callee saves incoming TOC in own frame */
	UDATA outgoingArguments[J9_INLINE_JNI_MAX_ARG_COUNT];
	UDATA jitGPRs[32]; /* r0-r31 */
	UDATA jitCR;
	UDATA jitLR;
	U_8 jitFPRs[32 * 8]; /* fp0-fp31 */
#if defined(J9VM_ENV_DATA64)
	U_8 jitVRs[52 * 16]; /* vsr0-vsr51 */
	UDATA align[3];
#else /* J9VM_ENV_DATA64 */
	UDATA align[1];
#endif /* J9VM_ENV_DATA64 */
	UDATA preservedGPRs[19]; /* r13-r31 */
	U_8 preservedFPRs[18 * 8]; /* fp14-31 */
#if defined(J9VM_ENV_DATA64)
	U_8 preservedVRs[12 * 16]; /* vsr52-vsr63 */
#endif /* J9VM_ENV_DATA64 */
#elif defined(J9VM_ENV_DATA64) /* AIXPPC */
#if defined(J9VM_ENV_LITTLE_ENDIAN)
	/* Linux PPC 64 LE
	 *
	 * Stack must be 64-byte aligned.
	 */
	UDATA backChain; /* caller SP */
	UDATA preservedCR; /* callee saves in caller frame */
	UDATA preservedLR; /* callee saves in caller frame */
	UDATA currentTOC; /* callee saves own TOC in own frame */
	UDATA outgoingArguments[J9_INLINE_JNI_MAX_ARG_COUNT];
	UDATA jitGPRs[32]; /* r0-r31 */
	UDATA jitCR;
	UDATA jitLR;
	U_8 jitFPRs[32 * 8]; /* fp0-fp31 */
	U_8 jitVRs[52 * 16]; /* vsr0-vsr51 */
	UDATA align[6];
	UDATA preservedGPRs[18]; /* r14-r31 */
	U_8 preservedFPRs[18 * 8]; /* fp14-31 */
	U_8 preservedVRs[12 * 16]; /* vsr52-vsr63 */
#else /* J9VM_ENV_LITTLE_ENDIAN */
	/* Linux PPC 64 BE
	 *
	 * Stack must be 64-byte aligned.
	 */
	UDATA backChain; /* caller SP */
	UDATA preservedCR; /* callee saves in caller frame */
	UDATA preservedLR; /* callee saves in caller frame */
	UDATA tocSave;
	UDATA reserved;
	UDATA currentTOC; /* callee saves own TOC in own frame */
	UDATA outgoingArguments[J9_INLINE_JNI_MAX_ARG_COUNT];
	UDATA jitGPRs[32]; /* r0-r31 */
	UDATA jitCR;
	UDATA jitLR;
	U_8 jitFPRs[32 * 8]; /* fp0-fp31 */
	U_8 jitVRs[52 * 16]; /* vsr0-vsr51 */
	UDATA align[4];
	UDATA preservedGPRs[18]; /* r14-r31 */
	U_8 preservedFPRs[18 * 8]; /* fp14-31 */
	U_8 preservedVRs[12 * 16]; /* vsr52-vsr63 */
#endif /* J9VM_ENV_LITTLE_ENDIAN */
#else /* J9VM_ENV_DATA64 */
#if defined(J9VM_ENV_LITTLE_ENDIAN)
	/* Linux PPC 32 LE */
#error Linux PPC 32 LE unsupported
#else /* J9VM_ENV_LITTLE_ENDIAN */
	/* Linux PPC 32 BE
	 *
	 * Stack must be 16-byte aligned.
	 */
	UDATA backChain; /* caller SP */
	UDATA preservedLR; /* callee saves in caller frame */
	UDATA outgoingArguments[J9_INLINE_JNI_MAX_ARG_COUNT];
	UDATA jitGPRs[32]; /* r0-r31 */
	UDATA jitCR;
	UDATA jitLR;
	U_8 jitFPRs[32 * 8]; /* fp0-fp31 */
	UDATA preservedCR; /* callee saves in own frame */
	UDATA preservedGPRs[19]; /* r13-r31 */
	U_8 preservedFPRs[18 * 8]; /* fp14-31 */
#endif /* J9VM_ENV_LITTLE_ENDIAN */
#endif /* J9VM_ENV_DATA64 */
#elif defined(J9VM_ARCH_ARM) /* J9VM_ARCH_POWER */
#if defined(J9VM_ENV_DATA64)
	/* ARM 64 */
#error ARM 64 unsupported
#else /* J9VM_ENV_DATA64 */
	/* ARM 32 */
	UDATA preservedGPRs[9]; /* r4-r11 and r14 */
	UDATA align[1];
	U_8 preservedFPRs[8 * 8]; /* fpr8-15 */
	UDATA jitGPRs[16]; /* r0-r15 */
	U_8 jitFPRs[16 * 8]; /* fpr0-15 */
#endif /* J9VM_ENV_DATA64 */
#elif defined(J9VM_ARCH_AARCH64) /* J9VM_ARCH_ARM */
	UDATA preservedGPRs[12]; /* x19-x30 */
	U_8 preservedFPRs[8 * 8]; /* v8-15 */
	UDATA jitGPRs[32]; /* x0-x31 */
	U_8 jitFPRs[32 * 8]; /* v0-v31 */
#elif defined(J9VM_ARCH_RISCV) /* J9VM_ARCH_ARM */
	UDATA preservedGPRs[11]; /* x2, x8, x9, and x18-x25  */
	U_8 preservedFPRs[32 * 8]; /* f0-f31 */
	UDATA jitGPRs[32]; /* x0-x31 */
	U_8 jitFPRs[32 * 8]; /* f0-f31 */
	U_8 padding[8]; /* padding to 16-byte boundary */
#elif defined(J9VM_ARCH_X86) /* J9VM_ARCH_AARCH64 */
#if defined(J9VM_ENV_DATA64) && defined(WIN32)
	UDATA arguments[4]; /* outgoing arguments shadow */
#endif /*J9VM_ENV_DATA64 && WIN32*/
	UDATA vmStruct;
	UDATA machineBP;
#if defined(J9VM_ENV_DATA64)
	union {
		UDATA numbered[16];
		struct {
			UDATA rax;
			UDATA rbx;
			UDATA rcx;
			UDATA rdx;
			UDATA rdi;
			UDATA rsi;
			UDATA rbp;
			UDATA rsp;
			UDATA r8;
			UDATA r9;
			UDATA r10;
			UDATA r11;
			UDATA r12;
			UDATA r13;
			UDATA r14;
			UDATA r15;
		} named;
	} jitGPRs;
#if defined(WIN32)
	/* Windows x86-64
	 *
	 * Stack must be 16-byte aligned.
	 */
	U_8 jitFPRs[6 * 16]; /* xmm0-5 128-bit OR xmm0-7 64-bit */
	U_8 preservedFPRs[10 * 16]; /* xmm6-15 128-bit */
	UDATA align[1];
	/* r15,r14,r13,r12,rdi,rsi,rbx,rbp,return address
	 * RSP is 16-byte aligned at this point
	 */
#else /* WIN32 */
	/* Linux x86-64
	 *
	 * Stack must be 16-byte aligned.
	 */
	U_8 jitFPRs[16 * 16]; /* xmm0-15 128-bit OR xmm0-7 64-bit */
	UDATA align[1];
	/* r15,r14,r13,r12,rbx,rbp,return address
	 * RSP is 16-byte aligned at this point
	 */
#endif /* WIN32 */
#else /* J9VM_ENV_DATA64 */
	/* Windows x86-32 and Linux x86-32
	 *
	 * Stack is forcibly aligned to 16 after pushing EBP
	 */
	union {
		UDATA numbered[8];
		struct {
			UDATA rax;
			UDATA rbx;
			UDATA rcx;
			UDATA rdx;
			UDATA rdi;
			UDATA rsi;
			UDATA rbp;
			UDATA rsp;
		} named;
	} jitGPRs;
	UDATA align1[2];
	U_8 jitFPRs[8 * 16]; /* xmm0-7 128-bit */
	UDATA align2[1];
	/* ebx,edi,esi
	 * ESP is forcibly 16-byte aligned at this point
	 * possible alignment
	 * ebp,return address
	 */
#endif /* J9VM_ENV_DATA64 */
#else /* J9VM_ARCH_X86 */
#error Unknown architecture
#endif /* J9VM_ARCH_X86 */
} J9CInterpreterStackFrame;

#include "objectreferencesmacros_define.inc"

#endif /* J9NONBUILDER_H */
