/*[INCLUDE-IF Sidecar16]*/
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
package java.lang.ref;

import java.security.AccessController;
import java.security.PrivilegedAction;

import com.ibm.oti.vm.VM;

/*[IF JAVA_SPEC_VERSION >= 12]*/
import jdk.internal.access.JavaLangRefAccess;
import jdk.internal.access.SharedSecrets;
/*[ELSE] JAVA_SPEC_VERSION >= 12
/*[IF Sidecar19-SE]
import jdk.internal.misc.JavaLangRefAccess;
import jdk.internal.misc.SharedSecrets;
/*[ELSE]
/*[IF Sidecar18-SE-OpenJ9]
import sun.misc.JavaLangRefAccess;
import sun.misc.SharedSecrets;
/*[ENDIF]*/
/*[ENDIF]*/
/*[ENDIF] JAVA_SPEC_VERSION >= 12 */

/**
 * Abstract class which describes behavior common to all reference objects.
 *
 * @author		OTI
 * @version		initial
 * @since		1.2
 */	
public abstract class Reference<T> extends Object {
	private static final int STATE_INITIAL = 0;
	private static final int STATE_CLEARED = 1;
	private static final int STATE_ENQUEUED = 2;
	
	private T referent;
	private ReferenceQueue queue;
	private int state;

	/*[IF Sidecar18-SE-OpenJ9 | Sidecar19-SE]*/
	/**
	 *  Wait for progress in reference processing.
	 * return false if there is no processing reference,
	 * return true after wait the notification from the reference processing thread if currently the thread is processing references. 
	 */
	static private native boolean waitForReferenceProcessingImpl();
	
	/*[IF Sidecar19-SE]*/
	static {
		SharedSecrets.setJavaLangRefAccess(new JavaLangRefAccess() {
			public boolean waitForReferenceProcessing() throws InterruptedException {
				return waitForReferenceProcessingImpl();
			}

			/*[IF JAVA_SPEC_VERSION >= 11]*/
			public void runFinalization() {
				Finalizer.runFinalization();
			}
			/*[ENDIF] JAVA_SPEC_VERSION >= 11 */
		});
	}
	
	/* jdk.lang.ref.disableClearBeforeEnqueue property allow reverting to the old behavior(non clear before enqueue) 
	 *  defer initializing the immutable variable to avoid bootstrap error 
	 */
	static class ClearBeforeEnqueue {
		@SuppressWarnings("boxing")
		static final boolean ENABLED =  AccessController.doPrivileged(new PrivilegedAction<Boolean>() {
			@Override public Boolean run() {
			    return !Boolean.getBoolean("jdk.lang.ref.disableClearBeforeEnqueue"); //$NON-NLS-1$
			  }
			});
	}
	
	/* The method waitForReferenceProcessing() is not used directly, just adapt for openjdk regression tests for TLS 1.3 */
    private static boolean waitForReferenceProcessing() throws InterruptedException {
    		return waitForReferenceProcessingImpl();
    }

	/*[ELSE]
	static {
		SharedSecrets.setJavaLangRefAccess(new JavaLangRefAccess() {
			public boolean tryHandlePendingReference() {
				return waitForReferenceProcessingImpl();
			}
		});
	}

	/*[ENDIF]*/
	/*[ENDIF]*/
	
/**
 * Make the referent null.  This does not force the reference object to be enqueued.
 */	
public void clear() {
	clearImpl();
}

/**
 * set the referent to null.
 */	
private void clearImpl() {
	synchronized(this) {
		referent = null;
		/* change the state to cleared if it's not already cleared or enqueued */
		if (STATE_INITIAL == state) {
			state = STATE_CLEARED;
		}
	}
}

/**
 * Force the reference object to be enqueued if it has been associated with a queue.
 *
 * @return	true if Reference is enqueued, false otherwise.
 */	
public boolean enqueue() {
	/*[IF Sidecar19-SE]*/
	if (ClearBeforeEnqueue.ENABLED) {
		clearImpl();
	}
	/*[ENDIF]*/
	return enqueueImpl();
}

/**
 * Return the referent of the reference object.
 *
 * @return	the referent to which reference refers,
 *			or null if object has been cleared.
 */	
public T get() {
	return getImpl();
}

private native T getImpl();

/**
 * Return whether the reference object has been enqueued.
 *
 * @return	true if Reference has been enqueued, false otherwise.
/*[IF JAVA_SPEC_VERSION >= 16]
 * 
 * @deprecated Use ReferenceQueue or Reference.refersTo(null).
/*[ENDIF] JAVA_SPEC_VERSION >= 16
 */
/*[IF JAVA_SPEC_VERSION >= 16]*/
@Deprecated(since="16")
/*[ENDIF] JAVA_SPEC_VERSION >= 16 */
public boolean isEnqueued () {
	synchronized(this) {
		return state == STATE_ENQUEUED;
	}
}

/**
 * Enqueue the reference object on the associated queue.
 *
 * @return	true if the Reference was successfully
 *			enqueued, false otherwise.
 */
boolean enqueueImpl() {
	final ReferenceQueue tempQueue;
	boolean result;
	T tempReferent = referent;
	synchronized(this) {
		/* Static order for the following code (DO NOT CHANGE) */
		tempQueue = queue;
		queue = null;
		if (state == STATE_ENQUEUED || tempQueue == null) {
			return false;
		}
		result = tempQueue.enqueue(this);
		if (result) {
			state = STATE_ENQUEUED;
			if (null != tempReferent) {
				reprocess();
			}
		}
		return result;
	}
}

private native void reprocess();

/** 
 * Constructs a new instance of this class.
 */
Reference() {
}

/**
 * Initialize a newly created reference object. Associate the
 * reference object with the referent.
 * 
 * @param r the referent
 */	
void initReference (T r) {
	state = STATE_INITIAL;
	referent = r;
}

/**
 * Initialize a newly created reference object.  Associate the
 * reference object with the referent, and the specified ReferenceQueue.
 * 
 * @param r the referent
 * @param q the ReferenceQueue
 */	
void initReference (T r, ReferenceQueue q) {
	/*[PR 101461] Reference should allow null queues */
	queue = q;
	state = STATE_INITIAL;
	referent = r;
}

/**
 * Called when a Reference has been removed from its ReferenceQueue.
 * Set the enqueued field to false.
 */
void dequeue() {
	/*[PR 112508] not synchronized, so isEnqueued() could return wrong result */
	synchronized(this) {
		state = STATE_CLEARED;
	}
}

/*[IF Sidecar19-SE]*/
/**
 * Used to keep the referenced object strongly reachable so that it is not reclaimable by garbage collection.
 * 
 * @param ref reference of the object.
 * @since		1.9
 */
public static void reachabilityFence(java.lang.Object ref) {
}
/*[ENDIF]*/

/*[IF JAVA_SPEC_VERSION >= 11]*/
/**
 * This method will always throw CloneNotSupportedException. A clone of this instance will not be returned 
 * since a Reference cannot be cloned. Workaround is to create a new Reference.
 * 
 * @throws CloneNotSupportedException always since a Reference cannot be cloned
 *
 * @since 11
 */
@Override
protected Object clone() throws CloneNotSupportedException {
	/*[MSG "K0900", "Create a new Reference, since a Reference cannot be cloned."]*/
	throw new CloneNotSupportedException(com.ibm.oti.util.Msg.getString("K0900")); //$NON-NLS-1$
}
/*[ENDIF] JAVA_SPEC_VERSION >= 11 */

/*[IF JAVA_SPEC_VERSION >= 16]*/
/**
 * Does this object refer to {@code target}?
 *
 * @param target the candidate referent
 * @return true if this object refers to {@code target}
 * @since 16
 */
public final native boolean refersTo(T target);

/*[ENDIF] JAVA_SPEC_VERSION >= 16 */
}
