/*[INCLUDE-IF Sidecar16]*/

package com.ibm.oti.vm;
/*******************************************************************************
 * Copyright (c) 1998, 2021 IBM Corp. and others
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

import com.ibm.oti.util.Msg;
import com.ibm.oti.util.Util;

/*[IF Sidecar19-SE]
import jdk.internal.reflect.CallerSensitive;
/*[ELSE]*/
import sun.reflect.CallerSensitive;
/*[ENDIF]*/
 
/**
 * Represents the running virtual machine. All VM specific API
 * are implemented on this class.
 * <p>
 * Note that all methods in VM are static. There is no singleton
 * instance which represents the actively running VM.
 */
@SuppressWarnings("javadoc")
public final class VM {

	public static final boolean PACKED_SUPPORT_ENABLED = false;  /* TODO delete this when ARM JCL is updated */

	public static final int J9_JAVA_CLASS_RAM_SHAPE_SHIFT;
	public static final int OBJECT_HEADER_SHAPE_MASK;
	public static final int OBJECT_HEADER_SIZE;
	public static final int FJ9OBJECT_SIZE;
	
	public static final int J9_GC_WRITE_BARRIER_TYPE;
	public static final int J9_GC_WRITE_BARRIER_TYPE_NONE;
	public static final int J9_GC_WRITE_BARRIER_TYPE_ALWAYS;
	public static final int J9_GC_WRITE_BARRIER_TYPE_OLDCHECK;
	public static final int J9_GC_WRITE_BARRIER_TYPE_CARDMARK;
	public static final int J9_GC_WRITE_BARRIER_TYPE_CARDMARK_INCREMENTAL;
	public static final int J9_GC_WRITE_BARRIER_TYPE_CARDMARK_AND_OLDCHECK;
	public static final int J9_GC_WRITE_BARRIER_TYPE_SATB;
	public static final int J9_GC_WRITE_BARRIER_TYPE_SATB_AND_OLDCHECK;

	public static final int J9_GC_ALLOCATION_TYPE;
	public static final int J9_GC_ALLOCATION_TYPE_TLH;
	public static final int J9_GC_ALLOCATION_TYPE_SEGREGATED;

	public static final int J9_GC_POLICY;
	public static final int J9_GC_POLICY_OPTTHRUPUT;
	public static final int J9_GC_POLICY_OPTAVGPAUSE;
	public static final int J9_GC_POLICY_GENCON;
	public static final int J9_GC_POLICY_BALANCED;
	public static final int J9_GC_POLICY_METRONOME;
	public static final int J9_GC_POLICY_NOGC;
	
	public static final int J9CLASS_INSTANCESIZE_OFFSET;
	public static final int J9CLASS_INSTANCE_DESCRIPTION_OFFSET;
	public static final int J9CLASS_LOCK_OFFSET_OFFSET;
	public static final int J9CLASS_LOCK_RESERVATION_HISTORY_RESERVED_COUNTER_OFFSET;
	public static final int J9CLASS_LOCK_RESERVATION_HISTORY_CANCEL_COUNTER_OFFSET;
	public static final int J9CLASS_INITIALIZE_STATUS_OFFSET;
	public static final int J9CLASS_CLASS_DEPTH_AND_FLAGS_OFFSET;
	public static final int J9CLASS_SUPERCLASSES_OFFSET;
	public static final int J9CLASS_ROMCLASS_OFFSET;
	public static final int J9CLASS_SIZE;
	
	public static final int J9_JAVA_CLASS_DEPTH_MASK;
	public static final int J9_JAVA_CLASS_MASK;
	
	public static final int J9ROMCLASS_MODIFIERS_OFFSET;
	
	public static final int ADDRESS_SIZE;

	public static final int J9_ACC_CLASS_ARRAY;
	
	public static final int J9_ACC_CLASS_INTERNAL_PRIMITIVE_TYPE;

	public static final int J9CLASS_INIT_SUCCEEDED;

	/* Valid types for J9_JIT_STRING_DEDUP_POLICY are:
	 *  - J9_JIT_STRING_DEDUP_POLICY_DISABLED
	 *  - J9_JIT_STRING_DEDUP_POLICY_FAVOUR_HIGHER
	 *  - J9_JIT_STRING_DEDUP_POLICY_FAVOUR_LOWER
	 */	
	public static final int J9_JIT_STRING_DEDUP_POLICY;
	
	/* Hint values for J9_JIT_STRING_DEDUP_POLICY */
	public static final int J9_JIT_STRING_DEDUP_POLICY_DISABLED = 0;
	public static final int J9_JIT_STRING_DEDUP_POLICY_FAVOUR_LOWER = 1;
	public static final int J9_JIT_STRING_DEDUP_POLICY_FAVOUR_HIGHER = 2;
	
	/* Determines whether String compression is enabled at VM startup */
	public static final boolean J9_STRING_COMPRESSION_ENABLED;
	
	public static final int OBJECT_HEADER_HAS_BEEN_MOVED_IN_CLASS;
	
	public static final boolean IS_BIG_ENDIAN;
	
	public static final int J9_CLASSLOADER_TYPE_OTHERS;
	public static final int J9_CLASSLOADER_TYPE_BOOT;
	public static final int J9_CLASSLOADER_TYPE_PLATFORM;
	
	/* Lock reservation */
	public static final int J9CLASS_RESERVABLE_LOCK_WORD_INIT;
	public static final int OBJECT_HEADER_LOCK_RESERVED;
	public static final int OBJECT_HEADER_LOCK_LEARNING;

	public static final int GLR_ENABLE_GLOBAL_LOCK_RESERVATION;
	public static final int GLR_RESERVED_ABSOLUTE_THRESHOLD;
	public static final int GLR_MINIMUM_RESERVED_RATIO;
	public static final int GLR_CANCEL_ABSOLUTE_THRESHOLD;
	public static final int GLR_MINIMUM_LEARNING_RATIO;

	private static String[] cachedVMArgs;
	/*[PR CMVC 189091] Perf: EnumSet.allOf() is slow */
	/*[PR CMVC 191554] Provide access to ClassLoader methods to improve performance */
	private static VMLangAccess javalangVMaccess;

	static {
		/* Note this is never called - the VM marks this class as initialized immediately after loading.
		 * The initializer is here solely to trick the compiler into letting us have static final
		 * variables that are initialized by the VM.
		 */
		J9_JAVA_CLASS_RAM_SHAPE_SHIFT = 0;
		OBJECT_HEADER_SHAPE_MASK = 0;
		OBJECT_HEADER_SIZE = 0;
		FJ9OBJECT_SIZE = 0;
		
		J9_GC_WRITE_BARRIER_TYPE = 0;
		J9_GC_WRITE_BARRIER_TYPE_NONE = 0;
		J9_GC_WRITE_BARRIER_TYPE_ALWAYS = 0;
		J9_GC_WRITE_BARRIER_TYPE_OLDCHECK = 0;
		J9_GC_WRITE_BARRIER_TYPE_CARDMARK = 0;
		J9_GC_WRITE_BARRIER_TYPE_CARDMARK_INCREMENTAL = 0;
		J9_GC_WRITE_BARRIER_TYPE_CARDMARK_AND_OLDCHECK = 0;
		J9_GC_WRITE_BARRIER_TYPE_SATB = 0;
		J9_GC_WRITE_BARRIER_TYPE_SATB_AND_OLDCHECK = 0;

		J9_GC_ALLOCATION_TYPE = 0;
		J9_GC_ALLOCATION_TYPE_TLH = 0;
		J9_GC_ALLOCATION_TYPE_SEGREGATED = 0;

		J9_GC_POLICY = 0;
		J9_GC_POLICY_OPTTHRUPUT = 0;
		J9_GC_POLICY_OPTAVGPAUSE = 0;
		J9_GC_POLICY_GENCON = 0;
		J9_GC_POLICY_BALANCED = 0;
		J9_GC_POLICY_METRONOME = 0;
		J9_GC_POLICY_NOGC = 0;
		
		J9CLASS_INSTANCESIZE_OFFSET = 0;
		J9CLASS_INSTANCE_DESCRIPTION_OFFSET = 0;
		J9CLASS_LOCK_OFFSET_OFFSET = 0;
		J9CLASS_LOCK_RESERVATION_HISTORY_RESERVED_COUNTER_OFFSET = 0;
		J9CLASS_LOCK_RESERVATION_HISTORY_CANCEL_COUNTER_OFFSET = 0;
		J9CLASS_INITIALIZE_STATUS_OFFSET = 0;
		J9CLASS_CLASS_DEPTH_AND_FLAGS_OFFSET = 0;
		J9CLASS_SUPERCLASSES_OFFSET = 0;
		J9CLASS_ROMCLASS_OFFSET = 0;
		J9CLASS_SIZE = 0;
		
		J9_JAVA_CLASS_DEPTH_MASK = 0;
		J9_JAVA_CLASS_MASK = 0;
		
		J9ROMCLASS_MODIFIERS_OFFSET = 0;
		
		ADDRESS_SIZE = 0;
		
		J9_ACC_CLASS_INTERNAL_PRIMITIVE_TYPE = 0;
		J9_ACC_CLASS_ARRAY = 0;
		J9CLASS_INIT_SUCCEEDED = 0;
		
		J9_JIT_STRING_DEDUP_POLICY = 0;
		
		J9_STRING_COMPRESSION_ENABLED = false;
		
		OBJECT_HEADER_HAS_BEEN_MOVED_IN_CLASS = 0;
		
		IS_BIG_ENDIAN = false;

		J9_CLASSLOADER_TYPE_OTHERS = 0;
		J9_CLASSLOADER_TYPE_BOOT = 0;
		J9_CLASSLOADER_TYPE_PLATFORM = 0;

		J9CLASS_RESERVABLE_LOCK_WORD_INIT = 0;
		OBJECT_HEADER_LOCK_RESERVED = 0;
		OBJECT_HEADER_LOCK_LEARNING = 0;

		GLR_ENABLE_GLOBAL_LOCK_RESERVATION = 0;
		GLR_RESERVED_ABSOLUTE_THRESHOLD = 0;
		GLR_MINIMUM_RESERVED_RATIO = 0;
		GLR_CANCEL_ABSOLUTE_THRESHOLD = 0;
		GLR_MINIMUM_LEARNING_RATIO = 0;
}
/**
 * Prevents this class from being instantiated.
 */
private VM() {
}

/**
 * Answer the class at depth.
 *
 * Notes:
 * 	 1) This method operates on the defining classes of methods on stack.
 *		NOT the classes of receivers.
 *
 *	 2) The item at index zero describes the caller of this method.
 *
 */
@CallerSensitive
static final native Class getStackClass(int depth);

/**
 * Returns the ClassLoader of the method (including natives) at the
 * specified depth on the stack of the calling thread. Frames representing
 * the VM implementation of java.lang.reflect are not included in the list.
 * 
 * This is not a public method as it can return the bootstrap class
 * loader, which should not be accessed by non-bootstrap classes.
 *
 * Notes: <ul>
 * 	 <li> This method operates on the defining classes of methods on stack.
 *		NOT the classes of receivers. </li>
 *
 *	 <li> The item at depth zero is the caller of this method </li>
 * </ul>
 *
 * @param depth the stack depth of the requested ClassLoader 
 * @return the ClassLoader at the specified depth
 * 
 * @see java.lang.ClassLoader#getStackClassLoader
 */
@CallerSensitive
static final native ClassLoader getStackClassLoader(int depth);

/**
 * Initialize the classloader.
 *
 * @param loader
 *            ClassLoader the ClassLoader instance
 * @param loaderType
 *            J9_CLASSLOADER_TYPE_BOOT     - bootstrap classloader 
 *            J9_CLASSLOADER_TYPE_PLATFORM - platform classloader 
 *            J9_CLASSLOADER_TYPE_OTHERS   - other classloader
 * @param parallelCapable
 *            true if the loader has registered as parallel capable
 */
public final static native void initializeClassLoader(ClassLoader loader, int loaderType, boolean parallelCapable);

public final static native long getProcessId(); 
public final static native long getUid(); 
/**
 * Return whether or not a ClassLoader is the bootstrap class loader.
 *
 * @param 		loader the ClassLoader instance to test
 * @return	true if the specified ClassLoader is the bootstrap ClassLoader
 */
static private final native boolean isBootstrapClassLoader(ClassLoader loader); 

/**
 * Ensures that the caller of the method where this check is made is a bootstrap class. 
 * 
 * @throws SecurityException if the caller of the method where this check is made is not a bootstrap class 
 */
@CallerSensitive
public static void ensureCalledFromBootstrapClass() {
	ClassLoader callerLoader = getStackClassLoader(2);
	if (!isBootstrapClassLoader(callerLoader)) {
		throw new SecurityException();
	}
}

/**
 * Native used to dump a string to the system console for debugging.
 *
 * @param 		str String
 *					the String to display
 */
public static native void dumpString(String str);

/**
 * Native used to set the classpath for an OTI implemented classloader
 *
 * @param 		classLoader
 *					the classloader to give the path to.
 *				classPath
 *					the string used as the class path.
 */
public static void setClassPathImpl(ClassLoader classLoader, String classPath) {
/*[IF]*/
	// Note Eclipse 2.0 and earlier calls this method
	// So don't delete it otherwise Eclipse will not work
/*[ENDIF]*/
}

/**
 * Leaving this method around for older Eclipse installations which may call it.
 * 
 * @deprecated	No longer used for hot-swap
 */
@Deprecated
public static void enableClassHotSwap(Class hotSwapClass) {
/*[IF]*/
	// Note Eclipse 2.0 and earlier calls this method
	// Leaving the method around.
/*[ENDIF]*/
}

/**
 * Get the classpath entry that was used to load the class that is the arg.
 * <p>
 * This method is for internal use only.
 *
 * @param		targetClass Class
 *					the class to set the classpath of.
 *
 * @see			java.lang.Class
 */
static native int getCPIndexImpl(Class targetClass);

/**
 * Does internal initialization required by VM.
 */
public static void initializeVM() {
	/*[IF]
	 * Currently there are no initialization routines but leaving this method in place for future possible use
	 */
	/*[ENDIF]*/
}

/**
 * Internal use only. Shutdown the JCL. Do not create new Threads.
 * If shutting down without calling Runtime.exit(),
 * or Runtime.halt(), 
 * daemon threads will still be running. If shutting down from a call
 * to System.exit(), JNI DestroyJavaVM(),
 * or Runtime.halt(),
 * all threads will still be running.
 * Called after java.lang.Runtime shutdownHooks have been run,
 * and after any finalization.  
 * 
 * @see #deleteOnExit()
 * @see #closeJars()
 */
private static void shutdown() {
}

static final int CPE_TYPE_UNKNOWN = 0;
static final int CPE_TYPE_DIRECTORY = 1;
static final int CPE_TYPE_JAR = 2;
static final int CPE_TYPE_JIMAGE = 3;
static final int CPE_TYPE_UNUSABLE = 5;

/**
 * Return the type of the specified entry on the class path for a ClassLoader.
 * Valid types are:
 * 		CPE_TYPE_UNKNOWN
 *		CPE_TYPE_DIRECTORY
 * 		CPE_TYPE_JAR
 * 		CPE_TYPE_JIMAGE
 *		CPE_TYPE_UNUSABLE
 * 
 * @param classLoader the ClassLoader
 * @param cpIndex the index on the class path
 * 
 * @return a int which specifies the class path entry type
 */
static final native int getClassPathEntryType(Object classLoader, int cpIndex);

/**
 * Returns command line arguments passed to the VM. Internally these are broken into
 * optionString and extraInfo. This only returns the optionString part.
 * <p>
 * @return		a String array containing the optionString part of command line arguments
 */
public static String [] getVMArgs() {
	/*[PR CMVC 112580] Java cannot handle non-ascii character arguments */
	if (cachedVMArgs == null) {
		byte[][] byteArgs = getVMArgsImpl();
		if (byteArgs == null) return null;
		String[] result = new String[byteArgs.length];
		for (int i=0; i<byteArgs.length; i++) {
			if (byteArgs[i] == null) {
				result[i] = null;
			} else {
				result[i] = Util.toString(byteArgs[i]);
			}
		}
		cachedVMArgs = result;
	}
	return (String[])cachedVMArgs.clone();
}

private static native byte[][] getVMArgsImpl();

/**
 * Return the number of entries on the bootclasspath.
 * 
 * @return an int which is the number of entries on the bootclasspath
 */
static native int getClassPathCount();

/**
 * Return the specified bootclasspath entry. Directory entries
 * are terminated with a file separator character.
 * 
 * @param index the index of the bootclasspath entry 
 * 
 * @return a byte array containing the bootclasspath entry
 * 			specified in the vm options
 */
static native byte[] getPathFromClassPath(int index);

/**
 * This method will cause the GC to do a local collection.
 */
public static native void localGC();  

/**
 * This method will cause the GC to do a global collection.
 */
public static native void globalGC();  

/**
 * Answer if native implementations should be used.
 */
/*[PR 118415] Add -includeMethod com.ibm.oti.vm.VM.useNatives()Z */
/*[PR 103225] Must implement for com.ibm.oti.io.CharacterConverterSimple */
public final static boolean useNatives() {
	return false;
}

/**
 * Returns a count of the number of instances of clazz in the heap.
 * Optionally fills in an array with references to the instances. If the
 * array parameter is NULL. This primitive may return 0 if there are no
 * instances or the operation is determined to be invalid during due to
 * specific vm states.
 * <p>
 * 
 * @return a integer with a count of all the instances of clazz in the heal
 * 
 * @param clazz
 *            the class which is being looked for
 * @param all
 *            an array to contain all the instances found or NULL. If the
 *            array is NULL, do not collect instances. Fill only up to full
 *            array. Extra instances are ignored if they don't fit in the
 *            array. If array is larger than the instance count then leave
 *            the extra slots in the array untouched.
 */
static private native int allInstances(Class clazz, Object[] all);

/**
 * setCommonData The string that keeps its original bytes is string1.
 * String2 has its bytes set to be string1-> bytes if the offsets already
 * match and the bytes are not already set to the same value The bytes being
 * set already could happen frequently as this primitive will be used
 * repeatedly to remerge strings in the runtime
 * 
 * @param s1
 *            the original string, this string is unmodified by the
 *            primitive
 * @param s2
 *            the duplicateString string, this string is modified by the
 *            primitive if the offsets match.
 * 
 * 
 */
static private native int setCommonData(String s1, String s2);

/**
 * Find allInstances of String in the system and return an array containing
 * them.
 * 
 * @return return an array containing all the strings in the system. The
 *         array may contain NULL references.
 */
static private String[] allStrings() { 
	int count = allInstances(String.class, null); 
	String[] result = new String[count];
	/*
	 * if the number of strings is zero then don't bother with a second
	 * scan. The number can be zero if the primitive prematurely returned
	 * due to strings being found in the system as they are being
	 * constructed.
	 */
	if (count != 0) {
		allInstances(String.class, result); 
	}
	return result;
}

/**
 * Request the VM perform a string merging operation.
 * This call is a hint to the VM that it should attempt to optimized String
 * footprint.
 * If the VM is in a state where the merging is not possible, the operation may be
 * quietly declined in which case the number of strings merged is 0. 
 * 
 * @return a count of number of merged duplicate strings performed by the merging call removeStringDuplicates.
 */
public static int removeStringDuplicates() {
	String heap[] = allStrings();
	int stringCount = heap.length;
	int mergedCount = 0;
	java.util.HashMap uniqueStrings = new java.util.HashMap();
	for (int n = 0; n < stringCount; n++) {
		String possibleDuplicate = heap[n];
		heap[n] = null; // don't hold on to the strings
		if (possibleDuplicate != null) {
			String originalString = (String) uniqueStrings
				.get(possibleDuplicate);
			if (originalString != null) {
				if (setCommonData(originalString, possibleDuplicate) == 1) {
					mergedCount++;
				}
			} else {
				uniqueStrings.put(possibleDuplicate, possibleDuplicate);
			}
		}
	}
	return mergedCount;
}

/*[PR 126182] Do not intern bootstrap class names when loading */
public static native String getClassNameImpl(Class aClass);

/*[PR CMVC 189091] Perf: EnumSet.allOf() is slow */
/*[PR CMVC 191554] Provide access to ClassLoader methods to improve performance */
public static void setVMLangAccess(VMLangAccess access) {
	/*[MSG "K05ba", "Cannot set access twice"]*/
	if (javalangVMaccess != null) throw new SecurityException(Msg.getString("K05ba")); //$NON-NLS-1$
	javalangVMaccess = access;
}

/*[PR CMVC 189091] Perf: EnumSet.allOf() is slow */
/*[PR CMVC 191554] Provide access to ClassLoader methods to improve performance */
public static VMLangAccess getVMLangAccess() {
	return javalangVMaccess;
}

/**
 * Set the current thread as a JVM System Thread
 * @return 0 on success, -1 on failure
 */
@CallerSensitive
public static int markCurrentThreadAsSystem()
{
	ensureCalledFromBootstrapClass();
	return markCurrentThreadAsSystemImpl();
}

private static native int markCurrentThreadAsSystemImpl();

}
