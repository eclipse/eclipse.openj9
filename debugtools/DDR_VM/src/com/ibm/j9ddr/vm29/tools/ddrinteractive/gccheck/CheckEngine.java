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
package com.ibm.j9ddr.vm29.tools.ddrinteractive.gccheck;

import static com.ibm.j9ddr.vm29.structure.J9JavaAccessFlags.*;

import java.util.Arrays;
import java.util.Iterator;

import com.ibm.j9ddr.CorruptDataException;
import com.ibm.j9ddr.InvalidDataTypeException;
import com.ibm.j9ddr.vm29.structure.J9Class;
import com.ibm.j9ddr.vm29.structure.J9JavaClassFlags;
import com.ibm.j9ddr.vm29.structure.J9Object;
import com.ibm.j9ddr.vm29.j9.J9ObjectFieldOffset;
import com.ibm.j9ddr.vm29.j9.J9ObjectFieldOffsetIterator;
import com.ibm.j9ddr.vm29.j9.ObjectAccessBarrier;
import com.ibm.j9ddr.vm29.j9.ObjectModel;
import com.ibm.j9ddr.vm29.j9.gc.GCClassIterator;
import com.ibm.j9ddr.vm29.j9.gc.GCClassIteratorClassSlots;
import com.ibm.j9ddr.vm29.j9.gc.GCExtensions;
import com.ibm.j9ddr.vm29.j9.gc.GCHeapLinkedFreeHeader;
import com.ibm.j9ddr.vm29.j9.gc.GCHeapRegionDescriptor;
import com.ibm.j9ddr.vm29.j9.gc.GCHeapRegionIterator;
import com.ibm.j9ddr.vm29.j9.gc.GCHeapRegionManager;
import com.ibm.j9ddr.vm29.j9.gc.GCObjectIterator;
import com.ibm.j9ddr.vm29.j9.gc.GCScavengerForwardedHeader;
import com.ibm.j9ddr.vm29.j9.gc.GCVMThreadIterator;
import com.ibm.j9ddr.vm29.j9.gc.GCVMThreadListIterator;
import com.ibm.j9ddr.vm29.pointer.AbstractPointer;
import com.ibm.j9ddr.vm29.pointer.ObjectReferencePointer;
import com.ibm.j9ddr.vm29.pointer.PointerPointer;
import com.ibm.j9ddr.vm29.pointer.U8Pointer;
import com.ibm.j9ddr.vm29.pointer.VoidPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9BuildFlags;
import com.ibm.j9ddr.vm29.pointer.generated.J9ClassPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9IndexableObjectPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9JavaStackPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9JavaVMPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9MemorySegmentPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9ObjectPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9ROMClassPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9VMThreadPointer;
import com.ibm.j9ddr.vm29.pointer.generated.MM_GCExtensionsPointer;
import com.ibm.j9ddr.vm29.pointer.generated.MM_HeapRegionManagerPointer;
import com.ibm.j9ddr.vm29.pointer.generated.MM_OwnableSynchronizerObjectListPointer;
import com.ibm.j9ddr.vm29.pointer.generated.MM_SublistPuddlePointer;
import com.ibm.j9ddr.vm29.pointer.generated.MM_UnfinalizedObjectListPointer;
import com.ibm.j9ddr.vm29.pointer.helper.J9ClassHelper;
import com.ibm.j9ddr.vm29.pointer.helper.J9ObjectHelper;
import com.ibm.j9ddr.vm29.structure.J9IndexableObjectContiguous;
import com.ibm.j9ddr.vm29.structure.J9MemorySegment;
import com.ibm.j9ddr.vm29.types.IDATA;
import com.ibm.j9ddr.vm29.types.U32;
import com.ibm.j9ddr.vm29.types.U64;
import com.ibm.j9ddr.vm29.types.UDATA;
import com.ibm.j9ddr.vm29.pointer.helper.J9IndexableObjectHelper;

import static com.ibm.j9ddr.vm29.tools.ddrinteractive.gccheck.CheckBase.*;
import static com.ibm.j9ddr.vm29.structure.J9MemorySegment.*;
import static com.ibm.j9ddr.vm29.structure.J9Object.*;
import static com.ibm.j9ddr.vm29.structure.J9ROMFieldOffsetWalkState.J9VM_FIELD_OFFSET_WALK_INCLUDE_STATIC;
import static com.ibm.j9ddr.vm29.structure.J9ROMFieldOffsetWalkState.J9VM_FIELD_OFFSET_WALK_ONLY_OBJECT_SLOTS;
import static com.ibm.j9ddr.vm29.structure.J9JavaAccessFlags.J9AccClassDying;

class CheckEngine
{
	private J9JavaVMPointer _javaVM;
	private CheckReporter _reporter;
	private CheckCycle _cycle;
	private Check _currentCheck;
	private CheckElement _lastHeapObject1 = new CheckElement();
	private CheckElement _lastHeapObject2 = new CheckElement();
	private CheckElement _lastHeapObject3 = new CheckElement();
	private SegmentTree _classSegmentsTree;
	private static final int CLASS_CACHE_SIZE = 19;
	private J9ClassPointer[] _checkedClassCache = new J9ClassPointer[CLASS_CACHE_SIZE];
	private J9ClassPointer[] _checkedClassCacheAllowUndead = new J9ClassPointer[CLASS_CACHE_SIZE];
	private static final int OBJECT_CACHE_SIZE = 61;
	private J9ObjectPointer[] _checkedObjectCache = new J9ObjectPointer[OBJECT_CACHE_SIZE];

	private static final int UNINITIALIZED_SIZE = -1;
	private int _ownableSynchronizerObjectCountOnList=UNINITIALIZED_SIZE; /**< the count of ownableSynchronizerObjects on the ownableSynchronizerLists, =UNINITIALIZED_SIZE indicates that the count has not been calculated */
	private int _ownableSynchronizerObjectCountOnHeap=UNINITIALIZED_SIZE; /**< the count of ownableSynchronizerObjects on the heap, =UNINITIALIZED_SIZE indicates that the count has not been calculated */
	private boolean _needVerifyOwnableSynchronizerConsistency = false;

	private GCHeapRegionManager _hrm;
		
	public CheckEngine(J9JavaVMPointer vm, CheckReporter reporter) throws CorruptDataException
	{
		_javaVM = vm;
		_reporter = reporter;

		/*
		 * Even if hrm is null, all helpers that use it will null check it and
		 * attempt to allocate it and handle the failure
		 */
		MM_HeapRegionManagerPointer hrmPtr = MM_GCExtensionsPointer.cast(_javaVM.gcExtensions()).heapRegionManager();
		_hrm = GCHeapRegionManager.fromHeapRegionManager(hrmPtr);
	}
	
	public J9JavaVMPointer getJavaVM()
	{
		return _javaVM;
	}

	public CheckReporter getReporter()
	{
		return _reporter;
	}
	
	public void clearPreviousObjects()
	{
		_lastHeapObject1.setNone();
		_lastHeapObject2.setNone();
		_lastHeapObject3.setNone();
	}
	
	public void pushPreviousObject(J9ObjectPointer object)
	{
		_lastHeapObject3.copyFrom(_lastHeapObject2);
		_lastHeapObject2.copyFrom(_lastHeapObject1);
		_lastHeapObject1.setObject(object);
	}
	
	public void pushPreviousClass(J9ClassPointer clazz)
	{
		_lastHeapObject3.copyFrom( _lastHeapObject2);
		_lastHeapObject2.copyFrom(_lastHeapObject1);
		_lastHeapObject1.setClazz(clazz);
	}

	public boolean isMidscavengeFlagSet()
	{
		return (_cycle.getMiscFlags() & J9MODRON_GCCHK_MISC_MIDSCAVENGE) != 0;
	}

	public boolean isIndexableDataAddressFlagSet()
	{
		return (_cycle.getMiscFlags() & J9MODRON_GCCHK_VALID_INDEXABLE_DATA_ADDRESS) != 0;
	}
	
	public boolean isScavengerBackoutFlagSet()
	{
		return (_cycle.getMiscFlags() & J9MODRON_GCCHK_SCAVENGER_BACKOUT) != 0;
	}
	
	public void reportForwardedObject(J9ObjectPointer object, J9ObjectPointer forwardedObject)
	{
		if((_cycle.getMiscFlags() & J9MODRON_GCCHK_VERBOSE) != 0) {
			_reporter.reportForwardedObject(object, forwardedObject);
		}
	}
	
	public int checkObjectHeap(J9ObjectPointer object, GCHeapRegionDescriptor regionDesc)
	{
		int result = J9MODRON_SLOT_ITERATOR_OK;
		
		boolean isDead = false;
		boolean isIndexable = false;
		J9ClassPointer clazz = null;
		
		try {
			if(ObjectModel.isDeadObject(object)) {
				/* this is a hole */
				result = checkJ9LinkedFreeHeader(GCHeapLinkedFreeHeader.fromJ9Object(object), regionDesc, _cycle.getCheckFlags());
				if (J9MODRON_GCCHK_RC_OK != result) {
					CheckError error = new CheckError(object, _cycle, _currentCheck, "Object", result, _cycle.nextErrorCount());
					_reporter.report(error);
					/* There are some error cases would not prevent further iteration */
					if (!((J9MODRON_GCCHK_RC_DEAD_OBJECT_NEXT_IS_NOT_HOLE == result) ||
						(J9MODRON_GCCHK_RC_DEAD_OBJECT_NEXT_IS_NOT_IN_REGION == result) ||
						(J9MODRON_GCCHK_RC_DEAD_OBJECT_NEXT_IS_POINTED_INSIDE == result))) {
						_reporter.reportHeapWalkError(error, _lastHeapObject1, _lastHeapObject2, _lastHeapObject3);
						return J9MODRON_SLOT_ITERATOR_UNRECOVERABLE_ERROR;
					}
				}		
				return J9MODRON_SLOT_ITERATOR_OK;
			}
		} catch (CorruptDataException e) {
			// TODO : cde should be part of the error
			CheckError error = new CheckError(object, _cycle, _currentCheck, "Object ", J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION, _cycle.nextErrorCount());
			_reporter.report(error);
			return J9MODRON_SLOT_ITERATOR_UNRECOVERABLE_ERROR;
		}
		
		try {
			// Prefetch this data to make CDE handling easy
			isIndexable = ObjectModel.isIndexable(object);
			clazz = J9ObjectHelper.clazz(object);
			result = checkJ9Object(object, regionDesc, _cycle.getCheckFlags());
		} catch (CorruptDataException cde) {
			// TODO : cde should be part of the error
			CheckError error = new CheckError(object, _cycle, _currentCheck, "Object ", J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION, _cycle.nextErrorCount());
			_reporter.report(error);
			return J9MODRON_SLOT_ITERATOR_UNRECOVERABLE_ERROR;
		}
		if (J9MODRON_GCCHK_RC_OK != result) {
			String elementName = isIndexable ? "IObject " : "Object ";
			CheckError error = new CheckError(object, _cycle, _currentCheck, elementName, result, _cycle.nextErrorCount());
			_reporter.report(error);
			/* There are some error cases would not prevent further iteration */
			if (!(J9MODRON_GCCHK_RC_CLASS_IS_UNLOADED == result)) {
				_reporter.reportHeapWalkError(error, _lastHeapObject1, _lastHeapObject2, _lastHeapObject3);
				return J9MODRON_SLOT_ITERATOR_UNRECOVERABLE_ERROR;
			} else {
				return J9MODRON_SLOT_ITERATOR_OK;
			}
		}		
		
		try {
			/* check Ownable Synchronizer Object consistency */
			if (needVerifyOwnableSynchronizerConsistency()) {
				if (J9Object.OBJECT_HEADER_SHAPE_MIXED == ObjectModel.getClassShape(clazz).intValue() && !J9ClassHelper.classFlags(clazz).bitAnd(J9AccClassOwnableSynchronizer).eq(0)) {
					if (ObjectAccessBarrier.isObjectInOwnableSynchronizerList(object).isNull()) {
						CheckError error = new CheckError(object, _cycle, _currentCheck, "Object ", J9MODRON_GCCHK_OWNABLE_SYNCHRONIZER_OBJECT_IS_NOT_ATTACHED_TO_THE_LIST, _cycle.nextErrorCount());
						_reporter.report(error);
					} else {
						_ownableSynchronizerObjectCountOnHeap += 1;
					}
				}
			}
		} catch (CorruptDataException cde) {
			// TODO : cde should be part of the error
			CheckError error = new CheckError(object, _cycle, _currentCheck, "Object ", J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION, _cycle.nextErrorCount());
			_reporter.report(error);
			return J9MODRON_SLOT_ITERATOR_UNRECOVERABLE_ERROR;
		}
				
		if (J9MODRON_GCCHK_RC_OK == result) {
			GCObjectIterator fieldIterator;
			GCObjectIterator addressIterator;

			try {
				fieldIterator = GCObjectIterator.fromJ9Object(object, true);
				addressIterator = GCObjectIterator.fromJ9Object(object, true);
			} catch (CorruptDataException e) {
				// TODO : cde should be part of the error
				CheckError error = new CheckError(object, _cycle, _currentCheck, "Object ", J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION, _cycle.nextErrorCount());
				_reporter.report(error);
				return J9MODRON_SLOT_ITERATOR_UNRECOVERABLE_ERROR;
			}
			while (fieldIterator.hasNext()) {
				J9ObjectPointer field = fieldIterator.next();
				VoidPointer address = addressIterator.nextAddress();
				result = checkSlotObjectHeap(field, ObjectReferencePointer.cast(address), regionDesc, object);
				if(J9MODRON_SLOT_ITERATOR_OK != result) {
					break;
				}
			}
		}
		
		if (J9MODRON_GCCHK_RC_OK == result) {
			/* this heap object is OK. Record it in the cache in case we find a pointer to it soon */
			int cacheIndex = (int) (object.getAddress() % OBJECT_CACHE_SIZE);
			_checkedObjectCache[cacheIndex] = object;
		}
		
		return result;
	}
	
	public int checkSlotObjectHeap(J9ObjectPointer object, ObjectReferencePointer objectIndirect, GCHeapRegionDescriptor regionDesc, J9ObjectPointer objectIndirectBase)
	{
		if(object.isNull()) {
			return J9MODRON_SLOT_ITERATOR_OK;
		}
		
		int result = checkObjectIndirect(object);
		
		/* might the heap include dark matter? If so, ignore most errors */
		if ((_cycle.getMiscFlags() & J9MODRON_GCCHK_MISC_DARKMATTER) != 0) {
			/* only report a subset of errors -- the rest are expected to be found in dark matter */
			switch (result) {
			case J9MODRON_GCCHK_RC_OK:
			case J9MODRON_GCCHK_RC_UNALIGNED:
			case J9MODRON_GCCHK_RC_STACK_OBJECT:
				break;
			
			/* These errors are unlikely, but not impossible to find in dark matter. 
			 * Leave them enabled because they can help find real corruption
			 */
			case J9MODRON_GCCHK_RC_NOT_FOUND: /* can happen due to contraction */
				break;
				
			/* other errors in possible dark matter are expected, so ignore them and don't 
			 * investigate this pointer any further 
			 */
			default:
				return J9MODRON_GCCHK_RC_OK;
			}
		}
		
		boolean isIndexable = false;
		boolean scavengerEnabled = false;
		try {
			isIndexable = ObjectModel.isIndexable(objectIndirectBase);
			scavengerEnabled = GCExtensions.scavengerEnabled();
		} catch (CorruptDataException e) {
			// TODO : cde should be part of the error
			CheckError error = new CheckError(object, _cycle, _currentCheck, "Object ", J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION, _cycle.nextErrorCount());
			_reporter.report(error);
			return J9MODRON_SLOT_ITERATOR_UNRECOVERABLE_ERROR;
		}
		
		if (J9MODRON_GCCHK_RC_OK != result) {
			String elementName = isIndexable ? "IObject " : "Object ";
			CheckError error = new CheckError(objectIndirectBase, objectIndirect, _cycle, _currentCheck, elementName, result, _cycle.nextErrorCount());
			_reporter.report(error);
			return J9MODRON_SLOT_ITERATOR_OK;
		}
		
		if(J9BuildFlags.gc_generational) {
			if(scavengerEnabled) {
				GCHeapRegionDescriptor objectRegion = ObjectModel.findRegionForPointer(_javaVM, _hrm, object, regionDesc);
				if(objectRegion == null) {
					/* should be impossible, since checkObjectIndirect() already verified that the object exists */
					return J9MODRON_GCCHK_RC_NOT_FOUND;
				}
				if(object.notNull()) {
					UDATA regionType;
					UDATA objectRegionType;
					boolean isRemembered;
					boolean isOld;
					try {
						regionType = regionDesc.getTypeFlags();
						objectRegionType = objectRegion.getTypeFlags();
						isRemembered = ObjectModel.isRemembered(objectIndirectBase);
						isOld = ObjectModel.isOld(object);
					} catch (CorruptDataException e) {
						// TODO : cde should be part of the error
						CheckError error = new CheckError(objectIndirectBase, _cycle, _currentCheck, "Object ", J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION, _cycle.nextErrorCount());
						_reporter.report(error);
						return J9MODRON_SLOT_ITERATOR_UNRECOVERABLE_ERROR;
					}
					
					/* Old objects that point to new objects should have remembered bit ON */
					if(regionType.allBitsIn(MEMORY_TYPE_OLD) && objectRegionType.allBitsIn(MEMORY_TYPE_NEW) && !isRemembered) {
						String elementName = isIndexable ? "IObject " : "Object ";
						CheckError error = new CheckError(objectIndirectBase, objectIndirect, _cycle, _currentCheck, elementName, J9MODRON_GCCHK_RC_NEW_POINTER_NOT_REMEMBERED, _cycle.nextErrorCount());
						_reporter.report(error);
						return J9MODRON_SLOT_ITERATOR_OK;
					}
					
					/* Old objects that point to objects with old bit OFF should have remembered bit ON */
					if(regionType.allBitsIn(MEMORY_TYPE_OLD) && !isOld && !isRemembered) {
						String elementName = isIndexable ? "IObject " : "Object ";
						CheckError error = new CheckError(objectIndirectBase, objectIndirect, _cycle, _currentCheck, elementName, J9MODRON_GCCHK_RC_REMEMBERED_SET_OLD_OBJECT, _cycle.nextErrorCount());
						_reporter.report(error);
						return J9MODRON_SLOT_ITERATOR_OK;						
					}
				}
			}
		}
		
		return J9MODRON_SLOT_ITERATOR_OK;		
	}

	private int checkObjectIndirect(J9ObjectPointer object)
	{
		if(object.isNull()) {
			return J9MODRON_GCCHK_RC_OK;
		}
		
		/* Short circuit if we've recently checked this object. */ 
		int cacheIndex = (int) Math.abs((object.getAddress() % OBJECT_CACHE_SIZE));
		if (_checkedObjectCache[cacheIndex] == object) {
			return J9MODRON_GCCHK_RC_OK;
		}
		
		/* Check if reference to J9Object */
		J9ObjectPointer[] newObject = new J9ObjectPointer[] { J9ObjectPointer.NULL };
		GCHeapRegionDescriptor[] objectRegion = new GCHeapRegionDescriptor[1];
		int result;
		try {
			result = checkJ9ObjectPointer(object, newObject, objectRegion);

			if (J9MODRON_GCCHK_RC_OK == result) {
				result = checkJ9Object(newObject[0], objectRegion[0], _cycle.getCheckFlags());
			}
		} catch (CorruptDataException e) {
			// TODO : preserve the CDE somehow?
			result = J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION;
		}
		
		if (J9MODRON_GCCHK_RC_OK == result) {
			/* Object is OK. Record it in the cache so we can short circuit if we see it again */
			_checkedObjectCache[cacheIndex] = object;
		}

		return result;
	}

	private int checkJ9ObjectPointer(J9ObjectPointer object, J9ObjectPointer[] newObject, GCHeapRegionDescriptor[] regionDesc) throws CorruptDataException
	{
		newObject[0] = object;
		
		if(object.isNull()) {
			return J9MODRON_GCCHK_RC_OK;
		}
		
		regionDesc[0] = ObjectModel.findRegionForPointer(_javaVM, _hrm, object, regionDesc[0]);
		if(regionDesc[0] == null) {
			/* Is the object on the stack? */
			GCVMThreadListIterator threadListIterator = GCVMThreadListIterator.from();
			while(threadListIterator.hasNext()) {
				J9VMThreadPointer vmThread = threadListIterator.next();
				if (isObjectOnStack(object, vmThread.stackObject())) {
					return J9MODRON_GCCHK_RC_STACK_OBJECT;
				}
			}
			
			UDATA classSlot = J9ObjectHelper.rawClazz(object);
			if (classSlot.eq(J9MODRON_GCCHK_J9CLASS_EYECATCHER)) {
				return J9MODRON_GCCHK_RC_OBJECT_SLOT_POINTS_TO_J9CLASS;
			}

			return J9MODRON_GCCHK_RC_NOT_FOUND;
		}
		
		// Can't do this check verbatim  
		//	if (0 == regionDesc->objectAlignment) {
		if(!regionDesc[0].containsObjects()) {
			/* this is a heap region, but it's not intended for objects (could be free or an arraylet leaf) */
			return J9MODRON_GCCHK_RC_NOT_IN_OBJECT_REGION;
		}
		
		/* Now we know object is not on stack we can check that it's correctly aligned
		 * for a J9Object.
		 */
		if(object.anyBitsIn(J9MODRON_GCCHK_J9OBJECT_ALIGNMENT_MASK)) {
			return J9MODRON_GCCHK_RC_UNALIGNED;
		}
		
		if(isMidscavengeFlagSet()) {
			if(GCExtensions.isVLHGC() || (regionDesc[0].getTypeFlags().allBitsIn(MEMORY_TYPE_NEW))) {
				// TODO: ideally, we should only check this in the evacuate segment
				// TODO: do some safety checks first -- is there enough room in the segment?
				GCScavengerForwardedHeader scavengerForwardedHeader = GCScavengerForwardedHeader.fromJ9Object(object);
				if(scavengerForwardedHeader.isForwardedPointer()) {
					newObject[0] = scavengerForwardedHeader.getForwardedObject();
					
					reportForwardedObject(object, newObject[0]);
					
					// Replace the object and resume
					object = newObject[0];

					regionDesc[0] = ObjectModel.findRegionForPointer(_javaVM, _hrm, object, regionDesc[0]);
					if(regionDesc[0] == null) {
						/* Is the object on the stack? */
						GCVMThreadListIterator threadListIterator = GCVMThreadListIterator.from();
						while(threadListIterator.hasNext()) {
							J9VMThreadPointer vmThread = threadListIterator.next();
							if (isObjectOnStack(object, vmThread.stackObject())) {
								return J9MODRON_GCCHK_RC_STACK_OBJECT;
							}
						}
						return J9MODRON_GCCHK_RC_NOT_FOUND;
					}
					
					// Can't do this check verbatim  
					//	if (0 == regionDesc->objectAlignment) {
					if(!regionDesc[0].containsObjects()) {
						/* this is a heap region, but it's not intended for objects (could be free or an arraylet leaf) */
						return J9MODRON_GCCHK_RC_NOT_IN_OBJECT_REGION;
					}
					
					/* make sure the forwarded pointer is also aligned */
					if(object.anyBitsIn(J9MODRON_GCCHK_J9OBJECT_ALIGNMENT_MASK)) {
						return J9MODRON_GCCHK_RC_UNALIGNED;
					}
				}
			}
		}
		
		if(isScavengerBackoutFlagSet()) {
			GCScavengerForwardedHeader scavengerForwardedHeader = GCScavengerForwardedHeader.fromJ9Object(object);
			if(scavengerForwardedHeader.isReverseForwardedPointer()) {
				newObject[0] = scavengerForwardedHeader.getReverseForwardedPointer();
				
				reportForwardedObject(object, newObject[0]);
				
				// Replace the object and resume
				object = newObject[0];

				regionDesc[0] = ObjectModel.findRegionForPointer(_javaVM, _hrm, object, regionDesc[0]);
				if(regionDesc[0] == null) {
					return J9MODRON_GCCHK_RC_NOT_FOUND;
				}
				
				if(!regionDesc[0].containsObjects()) {
					/* this is a heap region, but it's not intended for objects (could be free or an arraylet leaf) */
					return J9MODRON_GCCHK_RC_NOT_IN_OBJECT_REGION;
				}

				if(!regionDesc[0].getTypeFlags().allBitsIn(MEMORY_TYPE_NEW)) {
					/* reversed forwarded should point to Evacuate */
					return J9MODRON_GCCHK_RC_REVERSED_FORWARDED_OUTSIDE_EVACUATE;
				}
				
				/* make sure the forwarded pointer is also aligned */
				if(object.anyBitsIn(J9MODRON_GCCHK_J9OBJECT_ALIGNMENT_MASK)) {
					return J9MODRON_GCCHK_RC_UNALIGNED;
				}
			}
		}
		
		/* Check that elements of a double array are aligned on an 8-byte boundary.  For continuous
		 * arrays, verifying that the J9Indexable object is aligned on an 8-byte boundary is sufficient.
		 * For arraylets, depending on the layout, elements of the array may be stored on arraylet leafs
		 * or on the spine.  Arraylet leafs should always be aligned on 8-byte boundaries.  Checking both 
		 * the first and last element will ensure that we are always checking that elements are aligned 
		 * on the spine.
		 *  */ 
		long classShape = -1;
		try {
			classShape = ObjectModel.getClassShape(J9ObjectHelper.clazz(object)).longValue();
		} catch(CorruptDataException cde) {
			/* don't bother to report an error yet -- a later step will catch this. */
		}
		if(classShape == OBJECT_HEADER_SHAPE_DOUBLES) {
			J9IndexableObjectPointer array = J9IndexableObjectPointer.cast(object);
			int size = 0;
			VoidPointer elementPtr = VoidPointer.NULL;
			
			try {
				size = ObjectModel.getSizeInElements(object).intValue();
			} catch(InvalidDataTypeException ex) {
				// size in elements can not be larger then 2G but it is...
				
				// We could report an error at this point, but the C version
				// doesn't -- we'll catch it later
			} catch(IllegalArgumentException ex) {
				// We could report an error at this point, but the C version
				// doesn't -- we'll catch it later
			}
			
			if(0 != size) {
				elementPtr = ObjectModel.getElementAddress(array, 0, U64.SIZEOF);
				if(elementPtr.anyBitsIn(U64.SIZEOF - 1)) {
					return J9MODRON_GCCHK_RC_DOUBLE_ARRAY_UNALIGNED;
				}
				
				elementPtr = ObjectModel.getElementAddress(array, size - 1, U64.SIZEOF);
				if(elementPtr.anyBitsIn(U64.SIZEOF - 1)) {
					return J9MODRON_GCCHK_RC_DOUBLE_ARRAY_UNALIGNED;
				}
			}
		}

		return J9MODRON_GCCHK_RC_OK;
	}

	public int checkSlot(PointerPointer objectIndirect, VoidPointer objectIndirectBase, int objectType)
	{
		J9ObjectPointer object;
		try {
			object = J9ObjectPointer.cast(objectIndirect.at(0));
			int result = checkObjectIndirect(object);
			if(J9MODRON_GCCHK_RC_OK != result) {
				CheckError error = new CheckError(objectIndirectBase, objectIndirect, _cycle, _currentCheck, result, _cycle.nextErrorCount(), objectType);
				_reporter.report(error);
			}
		} catch (CorruptDataException e) {
			// TODO : cde should be part of the error
			CheckError error = new CheckError(objectIndirectBase, objectIndirect, _cycle, _currentCheck, J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION, _cycle.nextErrorCount(), objectType);
			_reporter.report(error);
		}
		return J9MODRON_SLOT_ITERATOR_OK;
	}
	
	public int checkSlotVMThread(PointerPointer objectIndirect, VoidPointer objectIndirectBase, int objectType, int iteratorState)
	{
		J9ObjectPointer object;
		try {
			object = J9ObjectPointer.cast(objectIndirect.at(0));
			int result = checkObjectIndirect(object);
			if ((GCVMThreadIterator.state_monitor_records == iteratorState) && (J9MODRON_GCCHK_RC_STACK_OBJECT == result)) {
				result = checkStackObject(object);
			}
			if(J9MODRON_GCCHK_RC_OK != result) {
				CheckError error = new CheckError(objectIndirectBase, objectIndirect, _cycle, _currentCheck, result, _cycle.nextErrorCount(), objectType);
				_reporter.report(error);
			}
		} catch (CorruptDataException e) {
			// TODO : cde should be part of the error
			CheckError error = new CheckError(objectIndirectBase, objectIndirect, _cycle, _currentCheck, J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION, _cycle.nextErrorCount(), objectType);
			_reporter.report(error);
		}
		return J9MODRON_SLOT_ITERATOR_OK;
	}
	
	public int checkSlotStack(PointerPointer objectIndirect, J9VMThreadPointer vmThread, VoidPointer stackLocation)
	{
		J9ObjectPointer object;
		try {
			object = J9ObjectPointer.cast(objectIndirect.at(0));
			int result = checkObjectIndirect(object);
			if (J9MODRON_GCCHK_RC_STACK_OBJECT == result) {
				result = checkStackObject(object);
			}
			if(J9MODRON_GCCHK_RC_OK != result) {
				CheckError error = new CheckError(vmThread, objectIndirect, stackLocation, _cycle, _currentCheck, result, _cycle.nextErrorCount());
				_reporter.report(error);
				return J9MODRON_SLOT_ITERATOR_RECOVERABLE_ERROR;
			}
		} catch (CorruptDataException e) {
			// TODO : cde should be part of the error
			CheckError error = new CheckError(vmThread, objectIndirect, stackLocation, _cycle, _currentCheck, J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION, _cycle.nextErrorCount());
			_reporter.report(error);
			return J9MODRON_SLOT_ITERATOR_RECOVERABLE_ERROR;
		}
		return J9MODRON_SLOT_ITERATOR_OK;
	}
	
	private int checkStackObject(J9ObjectPointer object)
	{
		if(object.isNull()) {
			return J9MODRON_GCCHK_RC_OK;
		}
		
		if(object.anyBitsIn(J9MODRON_GCCHK_ONSTACK_ALIGNMENT_MASK)) {
			return J9MODRON_GCCHK_RC_UNALIGNED;
		}
		
		if((_cycle.getCheckFlags() & J9MODRON_GCCHK_VERIFY_CLASS_SLOT) != 0) {
			/* Check that the class pointer points to the class heap, etc. */
			try {
				int ret = checkJ9ClassPointer(J9ObjectHelper.clazz(object));
				if (J9MODRON_GCCHK_RC_OK != ret) {
					return ret;
				}
			} catch (CorruptDataException e) {
				return J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION;
			}
			
		}
		
		if((_cycle.getCheckFlags() & J9MODRON_GCCHK_VERIFY_FLAGS) != 0) {

			try {
				if(!checkIndexableFlag(object)) {
					return J9MODRON_GCCHK_RC_INVALID_FLAGS;
				}
			} catch (CorruptDataException e) {
				return J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION;
			}
		}

		return J9MODRON_GCCHK_RC_OK;
	}

	public int checkSlotRememberedSet(PointerPointer objectIndirect, MM_SublistPuddlePointer puddle)
	{
		J9ObjectPointer object;
		try {
			object = J9ObjectPointer.cast(objectIndirect.at(0));
			
			if(isMidscavengeFlagSet()) {
				/* during a scavenge, some RS entries may be tagged -- remove the tag */
				if(object.anyBitsIn(DEFERRED_RS_REMOVE_FLAG )) {
					object = object.untag(DEFERRED_RS_REMOVE_FLAG);
				}
			}
			
			int result = checkObjectIndirect(object);
			if(J9MODRON_GCCHK_RC_OK != result) {
				CheckError error = new CheckError(puddle, objectIndirect, _cycle, _currentCheck, result, _cycle.nextErrorCount());
				_reporter.report(error);
				return J9MODRON_SLOT_ITERATOR_OK;
			}
			
			/* Additional checks for the remembered set */
			if(object.notNull()) {
				GCHeapRegionDescriptor objectRegion = ObjectModel.findRegionForPointer(_javaVM, _hrm, object, null);
				
				if (objectRegion == null) {
					/* shouldn't happen, since checkObjectIndirect() already verified this object */
					CheckError error = new CheckError(puddle, objectIndirect, _cycle, _currentCheck, J9MODRON_GCCHK_RC_NOT_FOUND, _cycle.nextErrorCount());
					_reporter.report(error);
					return J9MODRON_SLOT_ITERATOR_OK;
				}

				/* we shouldn't have newspace references in the remembered set */
				if(objectRegion.getTypeFlags().allBitsIn(MEMORY_TYPE_NEW)) {
					CheckError error = new CheckError(puddle, objectIndirect, _cycle, _currentCheck, J9MODRON_GCCHK_RC_REMEMBERED_SET_WRONG_SEGMENT, _cycle.nextErrorCount());
					_reporter.report(error);
					return J9MODRON_SLOT_ITERATOR_OK;
				}

				boolean skipObject = false;

				if(isScavengerBackoutFlagSet()) {
					GCScavengerForwardedHeader scavengerForwardedHeader = GCScavengerForwardedHeader.fromJ9Object(object);
					if(scavengerForwardedHeader.isReverseForwardedPointer()) {
						/* There is no reason to check object - is gone */
						skipObject = true;
					}
				}
				
				if(!skipObject) {
					/* content of Remembered Set should be Old and Remembered */
					if (!ObjectModel.isOld(object) || !ObjectModel.isRemembered(object)) {
						CheckError error = new CheckError(puddle, objectIndirect, _cycle, _currentCheck, J9MODRON_GCCHK_RC_REMEMBERED_SET_FLAGS, _cycle.nextErrorCount());
						_reporter.report(error);
						_reporter.reportObjectHeader(error, object, null);
						return J9MODRON_SLOT_ITERATOR_OK;
					}
				}
			}
			
		} catch (CorruptDataException e) {
			// TODO : cde should be part of the error
			CheckError error = new CheckError(puddle, objectIndirect, _cycle, _currentCheck, J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION, _cycle.nextErrorCount());
			_reporter.report(error);
		}
		return J9MODRON_SLOT_ITERATOR_OK;
	}

	public int checkSlotPool(PointerPointer objectIndirect, VoidPointer objectIndirectBase)
	{
		J9ObjectPointer object;
		try {
			object = J9ObjectPointer.cast(objectIndirect.at(0));
			int result = checkObjectIndirect(object);
			if(J9MODRON_GCCHK_RC_OK != result) {
				CheckError error = new CheckError(objectIndirectBase, objectIndirect, _cycle, _currentCheck, result, _cycle.nextErrorCount(), CheckError.check_type_other);
				_reporter.report(error);
			}
		} catch (CorruptDataException e) {
			// TODO : cde should be part of the error
			CheckError error = new CheckError(objectIndirectBase, objectIndirect, _cycle, _currentCheck, J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION, _cycle.nextErrorCount(), CheckError.check_type_other);
			_reporter.report(error);
		}
		return J9MODRON_SLOT_ITERATOR_OK;
	}

	public int checkClassHeap(J9ClassPointer clazz, J9MemorySegmentPointer segment)
	{
		int result;
		
		/*
		 * Verify that this is, in fact, a class
		 */
		try {
			result = checkJ9Class(clazz, segment, _cycle.getCheckFlags());
			if (J9MODRON_GCCHK_RC_OK != result) {
				CheckError error = new CheckError(clazz, _cycle, _currentCheck, "Class ", result, _cycle.nextErrorCount());
				_reporter.report(error);
			}
		
			/*
			 * Process object slots in the class
			 */
			GCClassIterator classIterator = GCClassIterator.fromJ9Class(clazz);
			while(classIterator.hasNext()) {
				PointerPointer slotPtr = PointerPointer.cast(classIterator.nextAddress());
				J9ObjectPointer object = J9ObjectPointer.cast(slotPtr.at(0));
				
				result = checkObjectIndirect(object);
				if (J9MODRON_GCCHK_RC_OK != result) {
					String elementName = "";
					
					switch(classIterator.getState()) {
					case GCClassIterator.state_statics:
						elementName = "static ";
						break;
					case GCClassIterator.state_constant_pool:
						elementName = "constant ";
						break;
					case GCClassIterator.state_slots:
						elementName = "slots "; 
						break;
					}
					CheckError error = new CheckError(clazz, slotPtr, _cycle, _currentCheck, elementName, result, _cycle.nextErrorCount());
					_reporter.report(error);
					return J9MODRON_SLOT_ITERATOR_OK;
				}
				
				if(GCExtensions.isStandardGC()) {
					/* If the slot has its old bit OFF, the class's remembered bit should be ON */
					if(object.notNull() && !ObjectModel.isOld(object)) {
						if(!ObjectModel.isRemembered(clazz.classObject())) {
							CheckError error = new CheckError(clazz, slotPtr, _cycle, _currentCheck, "Class ", J9MODRON_GCCHK_RC_REMEMBERED_SET_OLD_OBJECT, _cycle.nextErrorCount());
							_reporter.report(error);
							return J9MODRON_SLOT_ITERATOR_OK;
						}
					}
				}
				
			}
			
			if (J9MODRON_GCCHK_RC_OK != checkClassStatics(clazz)) {
				return J9MODRON_SLOT_ITERATOR_OK;
			}
			
			J9ClassPointer replaced = clazz.replacedClass();
			if (replaced.notNull()) {
				if (!J9ClassHelper.isSwappedOut(replaced)) {
					CheckError error = new CheckError(clazz, clazz.replacedClassEA(), _cycle, _currentCheck, "Class ", J9MODRON_GCCHK_RC_REPLACED_CLASS_HAS_NO_HOTSWAP_FLAG, _cycle.nextErrorCount());
					_reporter.report(error);
					return J9MODRON_SLOT_ITERATOR_OK;
				}
			}
			
			/*
			 * Process class slots in the class
			 */
			GCClassIteratorClassSlots classIteratorClassSlots = GCClassIteratorClassSlots.fromJ9Class(clazz);
			while(classIteratorClassSlots.hasNext()) {
				PointerPointer classSlotPtr = PointerPointer.cast(classIteratorClassSlots.nextAddress());
				J9ClassPointer classSlot = J9ClassPointer.cast(classSlotPtr.at(0));
				String elementName = "";
				result = J9MODRON_GCCHK_RC_OK;
				
				switch(classIteratorClassSlots.getState()) {
				case GCClassIteratorClassSlots.state_constant_pool:
					/* may be NULL */
					if (classSlot.notNull()) {
						result = checkJ9ClassPointer(classSlot);
					}
					elementName = "constant ";
					break;
					
				case GCClassIteratorClassSlots.state_superclasses:
					/* must not be NULL */
					result = checkJ9ClassPointer(classSlot);
					elementName = "superclass ";
					break;
					
				case GCClassIteratorClassSlots.state_interfaces:
					/* must not be NULL */
					result = checkJ9ClassPointer(classSlot);
					elementName = "interface ";
					break;
					
				case GCClassIteratorClassSlots.state_array_class_slots:
					/* may be NULL */
					if (classSlot.notNull()) {
						result = checkJ9ClassPointer(classSlot);
					}
					elementName = "array class ";
				}
				
				if (J9MODRON_GCCHK_RC_OK != result) {
					CheckError error = new CheckError(clazz, classSlotPtr, _cycle, _currentCheck, elementName, result, _cycle.nextErrorCount());
					_reporter.report(error);
					return J9MODRON_SLOT_ITERATOR_OK;
				}
			}
					
		} catch (CorruptDataException e) {
			// TODO : cde should be part of the error
			CheckError error = new CheckError(clazz, _cycle, _currentCheck, "Class ", J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION, _cycle.nextErrorCount());
			_reporter.report(error);
		}
		
		return J9MODRON_SLOT_ITERATOR_OK;
	}

	private int checkClassStatics(J9ClassPointer clazz)
	{
		int result = J9MODRON_GCCHK_RC_OK;
		
		try {
			boolean validationRequired = true;
			
			if(J9ClassHelper.isSwappedOut(clazz)) {
				/* if class has been hot swapped (J9AccClassHotSwappedOut bit is set) in Fast HCR,
				 * the ramStatics of the existing class may be reused.  The J9ClassReusedStatics
				 * bit in J9Class->extendedClassFlags will be set if that's the case.
				 * In Extended HCR mode ramStatics might be not NULL and must be valid
				 * NOTE: If class is hot swapped and the value in ramStatics is NULL it is valid 
				 * to have the correspondent ROM Class value in objectStaticCount field greater then 0
				 */
				if(J9ClassHelper.isArrayClass(clazz)) {
					/* j9arrayclass should not be hot swapped */
					result = J9MODRON_GCCHK_RC_CLASS_HOT_SWAPPED_FOR_ARRAY;
					CheckError error = new CheckError(clazz, _cycle, _currentCheck, "Class ", result, _cycle.nextErrorCount());
					_reporter.report(error);
					return result;
				}
				
				if(J9ClassHelper.areExtensionsEnabled()) {
					/* This is Extended HSR mode so hot swapped class might have NULL in ramStatics field */
					if(clazz.ramStatics().isNull()) {
						validationRequired = false;
					}
				}
				try {
					/* This case can also occur when running -Xint with extensions enabled */
					 if (J9ClassHelper.extendedClassFlags(clazz).allBitsIn(J9JavaClassFlags.J9ClassReusedStatics)) {
						 validationRequired = false;
					 }
				} catch (NoSuchFieldError e) {
					/* Flag must be missing from the core. */
				}
			}
		
			if (validationRequired) {
				// J9ClassLoaderPointer classLoader = clazz.classLoader();
				J9ROMClassPointer romClazz = clazz.romClass();
	
				UDATA numberOfReferences = new UDATA(0);
				PointerPointer sectionStart = PointerPointer.NULL;
				PointerPointer sectionEnd = PointerPointer.NULL;
	
				/*
				 * Note: we have no special recognition for J9ArrayClass here
				 * J9ArrayClass does not have a ramStatics field but something else at this place
				 * so direct check (NULL != clazz->ramStatics) would not be correct,
				 * however romClazz->objectStaticCount must be 0 for this case
				 */
				if (!romClazz.objectStaticCount().eq(0)) {
					sectionStart = PointerPointer.cast(clazz.ramStatics());
					sectionEnd = sectionStart.add(romClazz.objectStaticCount());
				}
	
				/* Iterate all fields of ROM Class looking to statics fields pointed to java objects */
				Iterator<J9ObjectFieldOffset> objectFieldOffsetIterator = J9ObjectFieldOffsetIterator.J9ObjectFieldOffsetIteratorFor(clazz, J9ClassHelper.superclass(clazz), new U32(J9VM_FIELD_OFFSET_WALK_INCLUDE_STATIC | J9VM_FIELD_OFFSET_WALK_ONLY_OBJECT_SLOTS));
				while (objectFieldOffsetIterator.hasNext()) {
					J9ObjectFieldOffset fieldOffset = objectFieldOffsetIterator.next();
					// J9ROMFieldShapePointer field = fieldOffset.getField();
					numberOfReferences = numberOfReferences.add(1);
	
					/* get address of next field */
					PointerPointer address = sectionStart.addOffset(fieldOffset.getOffsetOrAddress());
					
					/* an address must be in gc scan range */
					if(!(address.gte(sectionStart) && address.lt(sectionEnd))) {
						result = J9MODRON_GCCHK_RC_CLASS_STATICS_REFERENCE_IS_NOT_IN_SCANNING_RANGE;
						CheckError error = new CheckError(clazz, address, _cycle, _currentCheck, "Class ", result, _cycle.nextErrorCount());
						_reporter.report(error);
					}
					
					/* check only if we have an object */
					
					// TODO kmt : can't easily implement this part of the check
					//		J9Class* classToCast = vm->internalVMFunctions->internalFindClassUTF8(currentThread, toSearchString, toSearchLength, classLoader, J9_FINDCLASS_FLAG_EXISTING_ONLY);
					//		if ((NULL == classToCast) || (0 == instanceOfOrCheckCast(J9GC_J9OBJECT_CLAZZ(*address), classToCast))) {
					// The issue is that we can't simply call "internalFindClassUTF8" in DDR.
					// We could guess at the behaviour of the ClassLoader, but that makes 
					// distinguishing a real problem from a weird ClassLoader delegation
					// model difficult.
				}
	
				if (!numberOfReferences.eq(romClazz.objectStaticCount())) {
					result = J9MODRON_GCCHK_RC_CLASS_STATICS_WRONG_NUMBER_OF_REFERENCES;
					CheckError error = new CheckError(clazz, _cycle, _currentCheck, "Class ", result, _cycle.nextErrorCount());
					_reporter.report(error);
				}
			}
			
		} catch (CorruptDataException e) {
			// TODO : cde should be part of the error
			CheckError error = new CheckError(clazz, _cycle, _currentCheck, "Class ", J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION, _cycle.nextErrorCount());
			_reporter.report(error);
		}		
		
		return result;
	}

	private int checkJ9Class(J9ClassPointer clazz, J9MemorySegmentPointer segment, int checkFlags) throws CorruptDataException
	{
		if(clazz.isNull()) {
			return J9MODRON_GCCHK_RC_OK;
		}
		
		if (clazz.anyBitsIn(J9MODRON_GCCHK_J9CLASS_ALIGNMENT_MASK)) {
			return J9MODRON_GCCHK_RC_CLASS_POINTER_UNALIGNED;
		}
		
		/* Check that the class header contains the expected values */
		int ret = checkJ9ClassHeader(clazz);
		if (J9MODRON_GCCHK_RC_OK != ret) {
			return ret;
		}
		
		/* Check that class is not unloaded */
		ret = checkJ9ClassIsNotUnloaded(clazz);
		if (J9MODRON_GCCHK_RC_OK != ret) {
			return ret;
		}
		
		if((checkFlags & J9MODRON_GCCHK_VERIFY_RANGE) != J9MODRON_GCCHK_VERIFY_RANGE) {
			UDATA delta = UDATA.cast(segment.heapAlloc()).sub(UDATA.cast(clazz));
			
			/* Basic check that there is enough room for the object header */
			if (delta.lt(J9Class.SIZEOF)) {
				return J9MODRON_GCCHK_RC_CLASS_INVALID_RANGE;
			}
		}
		
		return J9MODRON_GCCHK_RC_OK;
	}

	public int checkJ9ClassPointer(J9ClassPointer clazz) throws CorruptDataException
	{
		return checkJ9ClassPointer(clazz, false);
	}

	public int checkJ9ClassPointer(J9ClassPointer clazz, boolean allowUndead) throws CorruptDataException
	{
		// Java-ism. Need to check this first before doing compares etc.
		if(clazz == null || clazz.isNull()) {
			return J9MODRON_GCCHK_RC_NULL_CLASS_POINTER;
		}
		
		// Short circuit if we've recently checked this class. 
		int cacheIndex = (int) (clazz.longValue() % CLASS_CACHE_SIZE);
		if(allowUndead && clazz.eq(_checkedClassCacheAllowUndead[cacheIndex])) {
			return J9MODRON_GCCHK_RC_OK;
		} else if(clazz.eq(_checkedClassCache[cacheIndex])) {
			return J9MODRON_GCCHK_RC_OK;
		}
		
		if(UDATA.cast(clazz).anyBitsIn(J9MODRON_GCCHK_J9CLASS_ALIGNMENT_MASK)) {
			return J9MODRON_GCCHK_RC_CLASS_POINTER_UNALIGNED;
		}
		
		J9MemorySegmentPointer segment = findSegmentForClass(clazz);
		if(segment == null) {
			return J9MODRON_GCCHK_RC_CLASS_NOT_FOUND;
		}
		if(!allowUndead) {
			if((segment.type().longValue() & MEMORY_TYPE_UNDEAD_CLASS) != 0) {
				return J9MODRON_GCCHK_RC_CLASS_IS_UNDEAD;
			}
		}
		
		/* Check to ensure J9Class header has the correct eyecatcher. */
		int result = checkJ9ClassHeader(clazz);
		if ( J9MODRON_GCCHK_RC_OK != result) {
			return result;
		}
		
		/* Check to ensure J9Class is not unloaded */
		result = checkJ9ClassIsNotUnloaded(clazz);
		if ( J9MODRON_GCCHK_RC_OK != result) {
			return result;
		}
		
		if((_cycle.getCheckFlags() & J9MODRON_GCCHK_VERIFY_RANGE) != 0) {
			IDATA delta = segment.heapAlloc().sub(U8Pointer.cast(clazz));

			/* Basic check that there is enough room for the class header */
			if (delta.lt(J9Class.SIZEOF)) {
				return J9MODRON_GCCHK_RC_CLASS_INVALID_RANGE;
			}
		}		
		/* class checked out. Record it in the cache so we don't need to check it again. */
		if (allowUndead) {
			_checkedClassCacheAllowUndead[cacheIndex] = clazz;
		} else {
			_checkedClassCache[cacheIndex] = clazz;
		}
		return J9MODRON_GCCHK_RC_OK;
	}

	private int checkJ9ClassHeader(J9ClassPointer clazz) throws CorruptDataException
	{
		if(!clazz.eyecatcher().eq(J9MODRON_GCCHK_J9CLASS_EYECATCHER)) {
			return J9MODRON_GCCHK_RC_J9CLASS_HEADER_INVALID;
		}
		return J9MODRON_GCCHK_RC_OK;
	}
	
	private int checkJ9ClassIsNotUnloaded(J9ClassPointer clazz) throws CorruptDataException
	{
		if(!clazz.classDepthAndFlags().bitAnd(J9AccClassDying).eq(0)) {
			return J9MODRON_GCCHK_RC_CLASS_IS_UNLOADED;
		}
		return J9MODRON_GCCHK_RC_OK;
	}
	
	/**
	 * Verify the integrity of an GCHeapLinkedFreeHeader (hole) on the heap.
	 * Checks various aspects of hole integrity.
	 *
	 * @param hole Pointer to the hole
	 * @param segment The segment containing the pointer
	 * @param checkFlags Type/level of verification
	 *
	 * @return @ref GCCheckWalkStageErrorCodes
	 * @throws CorruptDataException 
	 *
	 * @see @ref checkFlags
	 */
	private int checkJ9LinkedFreeHeader(GCHeapLinkedFreeHeader hole, GCHeapRegionDescriptor regionDesc, int checkFlags) throws CorruptDataException
	{
		J9ObjectPointer object = hole.getObject();
		
		if(ObjectModel.isSingleSlotDeadObject(object)) {
			/* Nothing to check for single slot hole */
			/* TODO: we can add warning here if header of single slot hole is not standard (0s or fs) */
			return J9MODRON_GCCHK_RC_OK;
		}
		
		UDATA holeSize = hole.getSize();
		/* Hole size can not be 0 */
		if(holeSize.eq(0)) {
			return J9MODRON_GCCHK_RC_DEAD_OBJECT_SIZE;
		}

		/* Hole size must be aligned */
		if(holeSize.anyBitsIn(J9MODRON_GCCHK_J9OBJECT_ALIGNMENT_MASK)) {
			return J9MODRON_GCCHK_RC_DEAD_OBJECT_SIZE_NOT_ALIGNED;
		}
		
		UDATA regionStart = UDATA.cast(regionDesc.getLowAddress());
		UDATA regionEnd = regionStart.add(regionDesc.getSize());
		UDATA delta = regionEnd.sub(UDATA.cast(object));
		
		/* Hole does not fit region */
		if(delta.lt(holeSize)) {
			return J9MODRON_GCCHK_RC_INVALID_RANGE;
		}
		
		GCHeapLinkedFreeHeader nextHole = hole.getNext();
		J9ObjectPointer nextObject = nextHole.getObject();

		if(!nextObject.isNull()) {
			/* Next must be a hole */
			if (!ObjectModel.isDeadObject(nextObject)) {
				return J9MODRON_GCCHK_RC_DEAD_OBJECT_NEXT_IS_NOT_HOLE;
			}
			
			/* Next should point to the same region */
			if(regionStart.gt(UDATA.cast(nextObject)) || regionEnd.lte(UDATA.cast(nextObject))) {
				return J9MODRON_GCCHK_RC_DEAD_OBJECT_NEXT_IS_NOT_IN_REGION;
			}
	
			/* next should not point to inside the hole */
			if(holeSize.add(UDATA.cast(object)).gt(UDATA.cast(nextObject)) && object.lt(nextObject)) {
				return J9MODRON_GCCHK_RC_DEAD_OBJECT_NEXT_IS_POINTED_INSIDE;
			}
		}
		return J9MODRON_GCCHK_RC_OK;
	}

	/**
	 * Verify the integrity of an object on the heap.
	 * Checks various aspects of object integrity based on the checkFlags.
	 *
	 * @param objectPtr Pointer to the object
	 * @param segment The segment containing the pointer
	 * @param checkFlags Type/level of verification
	 *
	 * @return @ref GCCheckWalkStageErrorCodes
	 * @throws CorruptDataException 
	 *
	 * @see @ref checkFlags
	 */
	private int checkJ9Object(J9ObjectPointer object, GCHeapRegionDescriptor regionDesc, int checkFlags) throws CorruptDataException
	{
		if(object.isNull()) {
			return J9MODRON_GCCHK_RC_OK; 
		}
		
		// Can't do this check verbatim  
		//	if (0 == regionDesc->objectAlignment) {
		if(!regionDesc.containsObjects()) {
			/* this is a heap region, but it's not intended for objects (could be free or an arraylet leaf) */
			return J9MODRON_GCCHK_RC_NOT_IN_OBJECT_REGION;
		}
		
		if(object.anyBitsIn(J9MODRON_GCCHK_J9OBJECT_ALIGNMENT_MASK)) {
			return J9MODRON_GCCHK_RC_UNALIGNED;
		}
		
		if((checkFlags & J9MODRON_GCCHK_VERIFY_CLASS_SLOT) != 0) {
			/* Check that the class pointer points to the class heap, etc. */
			int ret = checkJ9ClassPointer(J9ObjectHelper.clazz(object), true);

			if (J9MODRON_GCCHK_RC_OK != ret) {
				return ret;
			}
		}

		if (J9BuildFlags.arch_x86 && J9BuildFlags.env_data64 && isIndexableDataAddressFlagSet() && ObjectModel.isIndexable(object)) {
			J9IndexableObjectPointer array = J9IndexableObjectPointer.cast(object);
			if(false == J9IndexableObjectHelper.isCorrectDataPointer(array)) {
				return J9MODRON_GCCHK_RC_INVALID_INDEXABLE_DATA_ADDRESS;
			}
		}
		
		if((checkFlags & J9MODRON_GCCHK_VERIFY_RANGE) != 0) {
			UDATA regionEnd = UDATA.cast(regionDesc.getLowAddress()).add(regionDesc.getSize());
			long delta = regionEnd.sub(UDATA.cast(object)).longValue();
			
			/* Basic check that there is enough room for the object header */
			if (delta < J9ObjectHelper.headerSize()) {
				return J9MODRON_GCCHK_RC_INVALID_RANGE;
			}

			/* TODO: find out what the indexable header size should really be */
			if (ObjectModel.isIndexable(object) && (delta < J9IndexableObjectHelper.contiguousHeaderSize())) {
				return J9MODRON_GCCHK_RC_INVALID_RANGE;
			}

			if (delta < ObjectModel.getSizeInBytesWithHeader(object).longValue()) {
				return J9MODRON_GCCHK_RC_INVALID_RANGE;
			}

			if((checkFlags & J9MODRON_GCCHK_VERIFY_FLAGS) != 0) {
				// TODO : fix this test
				if (!checkIndexableFlag(object)) {
					return J9MODRON_GCCHK_RC_INVALID_FLAGS;
				}

				if(GCExtensions.isStandardGC()) {
					UDATA regionFlags = regionDesc.getTypeFlags();
					if(regionFlags.allBitsIn(MEMORY_TYPE_OLD)) {
						/* All objects in an old segment must have old bit set */
						if (!ObjectModel.isOld(object)) {
							return J9MODRON_GCCHK_RC_OLD_SEGMENT_INVALID_FLAGS;
						}
					} else {
						if(regionFlags.allBitsIn(MEMORY_TYPE_NEW)) {
				 			/* Object in a new segment can't have old bit or remembered bit set */
							if(ObjectModel.isOld(object)) {
								return J9MODRON_GCCHK_RC_NEW_SEGMENT_INVALID_FLAGS;
							}
						}
					}
				}
			}
		}
		return J9MODRON_GCCHK_RC_OK;
	}

	private boolean checkIndexableFlag(J9ObjectPointer object) throws CorruptDataException
	{
		UDATA classShape = ObjectModel.getClassShape(J9ObjectHelper.clazz(object));
		boolean isIndexable = ObjectModel.isIndexable(object);
		if(classShape.eq(OBJECT_HEADER_SHAPE_POINTERS)) {
			return isIndexable; 
		}
		if(classShape.eq(OBJECT_HEADER_SHAPE_BYTES)) {
			return isIndexable; 
		}
		if(classShape.eq(OBJECT_HEADER_SHAPE_WORDS)) {
			return isIndexable; 
		}
		if(classShape.eq(OBJECT_HEADER_SHAPE_LONGS)) {
			return isIndexable; 
		}
		if(classShape.eq(OBJECT_HEADER_SHAPE_DOUBLES)) {
			return isIndexable; 
		}
		return !isIndexable;
	}

//	int checkScopeInternalPointers(MM_MemorySpacePointer scopedMemorySpace, IterateRegionDescriptor regionDesc)
//	{
//		return -1;
//	}
//
//	void formatScopeInternalPointersOutput(MM_MemorySpacePointer scopedMemorySpace, GC_ScanFormatter* formatter)
//	{
//	}


	/**
	 * Start of a check
	 * This function should be called before any of the check functions in
	 * the engine. It ensures that the heap is walkable and TLHs are flushed
	 * @throws CorruptDataException 
	 */
	public void startCheckCycle(CheckCycle checkCycle) throws CorruptDataException
	{
		_cycle = checkCycle;
		_currentCheck = null;
//#if defined(J9VM_GC_MODRON_SCAVENGER)
//		_scavengerBackout = false;
//		_rsOverflowState = false;
//#endif /* J9VM_GC_MODRON_SCAVENGER */
		clearPreviousObjects();
//		clearRegionDescription(null); // &_regionDesc
		clearCheckedCache();
		
		if (CheckBase.J9MODRON_GCCHK_MISC_OWNABLESYNCHRONIZER_CONSISTENCY == (_cycle.getMiscFlags() & CheckBase.J9MODRON_GCCHK_MISC_OWNABLESYNCHRONIZER_CONSISTENCY) ) {
			_needVerifyOwnableSynchronizerConsistency = true;
		}
		else {
			_needVerifyOwnableSynchronizerConsistency = false;
		}
		clearCountsForOwnableSynchronizerObjects();
		prepareForHeapWalk();
	}

	public void prepareForHeapWalk() throws CorruptDataException
	{
		_classSegmentsTree = new SegmentTree(_javaVM.classMemorySegments());
	}

	/**
	 * End of a check
	 * This function should be called at the end of a check. It triggers
	 * the hook to inform interested parties that a heap walk has occurred.
	 */
	public void endCheckCycle()
	{
	}

	/**
	 * Advance to the next stage of checking.
	 * Sets the context for the next stage of checking.  Called every time verification
	 * moves to a new structure.
	 *
	 * @param check Pointer to the check we're about to start
	 */
	public void startNewCheck(Check check)
	{
		_currentCheck = check;
		clearPreviousObjects();
	}
	
	private void clearCheckedCache() 
	{
		Arrays.fill(_checkedClassCache, null);
		Arrays.fill(_checkedClassCacheAllowUndead, null);
		Arrays.fill(_checkedObjectCache, null);
	}
	
	private boolean isPointerInSegment(AbstractPointer pointer, J9MemorySegmentPointer segment)
	{
		try {
			return pointer.gte(segment.heapBase()) && pointer.lt(segment.heapAlloc());
		} catch (CorruptDataException e) {
			return false;
		}
	}
	
	private boolean isObjectOnStack(J9ObjectPointer object, J9JavaStackPointer stack)
	{
		try {
			return object.lt(stack.end()) && object.gte(stack.add(1));
		} catch (CorruptDataException e) {
			return false;
		}
	}
	
	private J9MemorySegmentPointer findSegmentForClass(J9ClassPointer clazz)
	{
		J9MemorySegmentPointer segment = _classSegmentsTree.findSegment(clazz);
		if(segment != null) {
			if(!isPointerInSegment(clazz, segment)) {
				return null;
			}
			try {
				if(segment.type().anyBitsIn(J9MemorySegment.MEMORY_TYPE_RAM_CLASS | J9MemorySegment.MEMORY_TYPE_UNDEAD_CLASS)) {
					return segment;
				}
			} catch (CorruptDataException e) {
				return null;
			}
		}
		return null;
	}

	public int checkSlotUnfinalizedList(J9ObjectPointer object, MM_UnfinalizedObjectListPointer currentList)
	{
		int result = checkObjectIndirect(object);
		if(J9MODRON_GCCHK_RC_OK != result) {
			CheckError error = new CheckError(currentList, object, _cycle, _currentCheck, result, _cycle.nextErrorCount());
			_reporter.report(error);
		}
		return J9MODRON_SLOT_ITERATOR_OK;
	}
	
	public int checkSlotOwnableSynchronizerList(J9ObjectPointer object, MM_OwnableSynchronizerObjectListPointer currentList)
	{
		if (needVerifyOwnableSynchronizerConsistency()) {
			_ownableSynchronizerObjectCountOnList += 1;
		}
		try {
			int result = checkObjectIndirect(object);
			if(J9MODRON_GCCHK_RC_OK != result) {
				CheckError error = new CheckError(currentList, object, _cycle, _currentCheck, result, _cycle.nextErrorCount());
				_reporter.report(error);
				_reporter.reportHeapWalkError(error, _lastHeapObject1, _lastHeapObject2, _lastHeapObject3);
				return J9MODRON_SLOT_ITERATOR_UNRECOVERABLE_ERROR;
			} 
			
			J9ClassPointer instanceClass = J9ObjectHelper.clazz(object);
			if (J9ClassHelper.classFlags(instanceClass).bitAnd(J9AccClassOwnableSynchronizer).eq(0)) {
				CheckError error = new CheckError(currentList, object, _cycle, _currentCheck, J9MODRON_GCCHK_RC_INVALID_FLAGS, _cycle.nextErrorCount());
				_reporter.report(error);
			}			
		} catch (CorruptDataException e) {
			CheckError error = new CheckError(currentList, object, _cycle, _currentCheck, J9MODRON_GCCHK_RC_CORRUPT_DATA_EXCEPTION, _cycle.nextErrorCount());
			_reporter.report(error);			
			_reporter.reportHeapWalkError(error, _lastHeapObject1, _lastHeapObject2, _lastHeapObject3);
			return J9MODRON_SLOT_ITERATOR_UNRECOVERABLE_ERROR;
		}
		return J9MODRON_SLOT_ITERATOR_OK;
	}
	

	public int checkSlotFinalizableList(J9ObjectPointer object)
	{
		int result = checkObjectIndirect(object);
		if (J9MODRON_GCCHK_RC_OK != result) {
			CheckError error = new CheckError(object, null, _cycle, _currentCheck, result, _cycle.nextErrorCount(), CheckError.check_type_finalizable);
			_reporter.report(error);
		}
		return J9MODRON_SLOT_ITERATOR_OK;
	}

	public void setMaxErrorsToReport(int count)
	{
		_reporter.setMaxErrorsToReport(count);
	}
	
	public void clearCountsForOwnableSynchronizerObjects()
	{
		_ownableSynchronizerObjectCountOnList = UNINITIALIZED_SIZE;
		_ownableSynchronizerObjectCountOnHeap = UNINITIALIZED_SIZE;
	}
	
	public boolean verifyOwnableSynchronizerObjectCounts()
	{
		boolean ret = true;
		if ((UNINITIALIZED_SIZE != _ownableSynchronizerObjectCountOnList) && (UNINITIALIZED_SIZE != _ownableSynchronizerObjectCountOnHeap)) {
			if (_ownableSynchronizerObjectCountOnList != _ownableSynchronizerObjectCountOnHeap) {
				_reporter.println(String.format("<gc check: found count=%d of OwnableSynchronizerObjects on Heap doesn't match count=%d on lists>", _ownableSynchronizerObjectCountOnHeap, _ownableSynchronizerObjectCountOnList));
				ret = false;
			}
		}

		return ret;		
	}

	public void reportOwnableSynchronizerCircularReferenceError(J9ObjectPointer object, MM_OwnableSynchronizerObjectListPointer currentList)
	{
		CheckError error = new CheckError(currentList, object, _cycle, _currentCheck, J9MODRON_GCCHK_OWNABLE_SYNCHRONIZER_LIST_HAS_CIRCULAR_REFERENCE, _cycle.nextErrorCount());
		_reporter.report(error);
		_reporter.reportHeapWalkError(error, _lastHeapObject1, _lastHeapObject2, _lastHeapObject3);
	}
	
	public void initializeOwnableSynchronizerCountOnList() 
	{ 
		_ownableSynchronizerObjectCountOnList = 0; 
	}
	
	public void initializeOwnableSynchronizerCountOnHeap() 
	{ 
		_ownableSynchronizerObjectCountOnHeap = 0; 
	}
	
	public boolean needVerifyOwnableSynchronizerConsistency()
	{
		return _needVerifyOwnableSynchronizerConsistency;
	}
}
