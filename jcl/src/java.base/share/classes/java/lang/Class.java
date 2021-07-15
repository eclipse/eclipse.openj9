/*[INCLUDE-IF JAVA_SPEC_VERSION >= 8]*/
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
package java.lang;

import java.io.InputStream;
import java.security.AccessControlContext;
import java.security.ProtectionDomain;
import java.security.Permissions;
/*[IF JAVA_SPEC_VERSION >= 12]*/
import java.lang.constant.ClassDesc;
import jdk.internal.reflect.ReflectionFactory;
/*[ENDIF] JAVA_SPEC_VERSION >= 12*/
import java.lang.reflect.*;
import java.net.URL;
import java.lang.annotation.*;
import java.util.Collection;
import java.util.HashMap;
/*[IF JAVA_SPEC_VERSION >= 16]*/
import java.util.HashSet;
/*[ENDIF] JAVA_SPEC_VERSION >= 16 */
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
/*[IF JAVA_SPEC_VERSION >= 12]*/
import java.util.Optional;
/*[ENDIF] JAVA_SPEC_VERSION >= 12 */
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;
import java.security.AccessController;
import java.security.PrivilegedExceptionAction;
import java.security.PrivilegedAction;
import java.lang.ref.*;
/*[IF JAVA_SPEC_VERSION >= 12]*/
import java.lang.constant.ClassDesc;
import java.lang.constant.Constable;
/*[ENDIF] JAVA_SPEC_VERSION >= 12 */

import sun.reflect.generics.repository.ClassRepository;
import sun.reflect.generics.factory.CoreReflectionFactory;
import sun.reflect.generics.scope.ClassScope;
import sun.reflect.annotation.AnnotationType;
import java.util.Arrays;
import com.ibm.oti.vm.VM;
/*[IF JAVA_SPEC_VERSION >= 11]*/
import static com.ibm.oti.util.Util.doesClassLoaderDescendFrom;
/*[ENDIF] JAVA_SPEC_VERSION >= 11*/

/*[IF Sidecar19-SE]
import jdk.internal.misc.Unsafe;
/*[IF JAVA_SPEC_VERSION >= 15]*/
import jdk.internal.access.SharedSecrets;
/*[ELSE] JAVA_SPEC_VERSION >= 15
import jdk.internal.misc.SharedSecrets;
/*[ENDIF] JAVA_SPEC_VERSION >= 15 */
import java.io.IOException;
import jdk.internal.reflect.Reflection;
import jdk.internal.reflect.CallerSensitive;
import jdk.internal.reflect.ConstantPool;
/*[ELSE]*/
import sun.misc.Unsafe;
import sun.misc.SharedSecrets;
import sun.reflect.Reflection;
import sun.reflect.CallerSensitive;
import sun.reflect.ConstantPool;
/*[ENDIF]*/

import java.util.ArrayList;
import java.lang.annotation.Repeatable;
import java.lang.invoke.*;
import com.ibm.oti.reflect.TypeAnnotationParser;
import java.security.PrivilegedActionException;
import sun.security.util.SecurityConstants;

/**
 * An instance of class Class is the in-image representation
 * of a Java class. There are three basic types of Classes
 * <dl>
 * <dt><em>Classes representing object types (classes or interfaces)</em></dt>
 * <dd>These are Classes which represent the class of a
 *     simple instance as found in the class hierarchy.
 *     The name of one of these Classes is simply the
 *     fully qualified class name of the class or interface
 *     that it represents. Its <em>signature</em> is
 *     the letter "L", followed by its name, followed
 *     by a semi-colon (";").</dd>
 * <dt><em>Classes representing base types</em></dt>
 * <dd>These Classes represent the standard Java base types.
 *     Although it is not possible to create new instances
 *     of these Classes, they are still useful for providing
 *     reflection information, and as the component type
 *     of array classes. There is one of these Classes for
 *     each base type, and their signatures are:
 *     <ul>
 *     <li><code>B</code> representing the <code>byte</code> base type</li>
 *     <li><code>S</code> representing the <code>short</code> base type</li>
 *     <li><code>I</code> representing the <code>int</code> base type</li>
 *     <li><code>J</code> representing the <code>long</code> base type</li>
 *     <li><code>F</code> representing the <code>float</code> base type</li>
 *     <li><code>D</code> representing the <code>double</code> base type</li>
 *     <li><code>C</code> representing the <code>char</code> base type</li>
 *     <li><code>Z</code> representing the <code>boolean</code> base type</li>
 *     <li><code>V</code> representing void function return values</li>
 *     </ul>
 *     The name of a Class representing a base type
 *     is the keyword which is used to represent the
 *     type in Java source code (i.e. "int" for the
 *     <code>int</code> base type.</dd>
 * <dt><em>Classes representing array classes</em></dt>
 * <dd>These are Classes which represent the classes of
 *     Java arrays. There is one such Class for all array 
 *     instances of a given arity (number of dimensions)
 *     and leaf component type. In this case, the name of the
 *     class is one or more left square brackets (one per 
 *     dimension in the array) followed by the signature ofP
 *     the class representing the leaf component type, which
 *     can be either an object type or a base type. The 
 *     signature of a Class representing an array type
 *     is the same as its name.</dd>
 * </dl>
 *
 * @author		OTI
 * @version		initial
 */
public final class Class<T> implements java.io.Serializable, GenericDeclaration, Type
/*[IF JAVA_SPEC_VERSION >= 12]*/
	, Constable, TypeDescriptor, TypeDescriptor.OfField<Class<?>>
/*[ENDIF] JAVA_SPEC_VERSION >= 12 */
{
	private static final long serialVersionUID = 3206093459760846163L;
	private static ProtectionDomain AllPermissionsPD;
	private static final int SYNTHETIC = 0x1000;
	private static final int ANNOTATION = 0x2000;
	private static final int ENUM = 0x4000;
	private static final int MEMBER_INVALID_TYPE = -1;

/*[IF]*/
	/**
	 * It is important that these remain static final
	 * because the VM peeks for them before running the <clinit>
	 */
/*[ENDIF]*/
	static final Class<?>[] EmptyParameters = new Class<?>[0];
	
	/*[PR VMDESIGN 485]*/
	private transient long vmRef;
	private transient ClassLoader classLoader;

	/*[IF Sidecar19-SE]*/
	private transient Module module;
	/*[ENDIF]*/

	/*[PR CMVC 125822] Move RAM class fields onto the heap to fix hotswap crash */
	private transient ProtectionDomain protectionDomain;
	private transient String classNameString;

	/* Cache filename on Class to avoid repeated lookups / allocations in stack traces */
	private transient String fileNameString;

	private static final class AnnotationVars {
		AnnotationVars() {}
		static long annotationTypeOffset = -1;
		static long valueMethodOffset = -1;

		/*[PR 66931] annotationType should be volatile because we use compare and swap */
		volatile AnnotationType annotationType;
		MethodHandle valueMethod;
	}
	private transient AnnotationVars annotationVars;
	private static long annotationVarsOffset = -1;

	/*[PR JAZZ 55717] add Java 8 new field: transient ClassValue.ClassValueMap classValueMap */
	/*[PR CMVC 200702] New field to support changes for RI defect 7030453 */
	transient ClassValue.ClassValueMap classValueMap;

	private static final class EnumVars<T> {
		EnumVars() {}
		static long enumDirOffset = -1;
		static long enumConstantsOffset = -1;

		Map<String, T> cachedEnumConstantDirectory;
		/*[PR CMVC 188840] Perf: Class.getEnumConstants() is slow */
		T[] cachedEnumConstants;
	}
	private transient EnumVars<T> enumVars;
	private static long enumVarsOffset = -1;
	
	transient J9VMInternals.ClassInitializationLock initializationLock;
	
	private transient Object methodHandleCache;
	
	/*[PR Jazz 85476] Address locking contention on classRepository in getGeneric*() methods */
	private transient ClassRepositoryHolder classRepoHolder;

/*[IF JAVA_SPEC_VERSION >= 12]*/
	private static ReflectionFactory reflectionFactory = null;
/*[ENDIF] JAVA_SPEC_VERSION >= 12 */
	
	/* Helper class to hold the ClassRepository. We use a Class with a final 
	 * field to ensure that we have both safe initialization and safe publication.
	 */
	private static final class ClassRepositoryHolder {
		static final ClassRepositoryHolder NullSingleton = new ClassRepositoryHolder(null);
		final ClassRepository classRepository;
		
		ClassRepositoryHolder(ClassRepository classRepo) {
			classRepository = classRepo;
		}
	}

	private static final class AnnotationCache {
		final LinkedHashMap<Class<? extends Annotation>, Annotation> directAnnotationMap;
		final LinkedHashMap<Class<? extends Annotation>, Annotation> annotationMap;
		AnnotationCache(
				LinkedHashMap<Class<? extends Annotation>, Annotation> directMap,
				LinkedHashMap<Class<? extends Annotation>, Annotation> annMap
		) {
			directAnnotationMap = directMap;
			annotationMap = annMap;
		}
	}
	private transient AnnotationCache annotationCache;
	private static long annotationCacheOffset = -1;
	private static boolean reflectCacheEnabled;
	private static boolean reflectCacheDebug;
	private static boolean reflectCacheAppOnly = true;

	/*
	 * This {@code ClassReflectNullPlaceHolder} class is created to indicate the cached class value is
	 * initialized to null rather than the default value null ;e.g. {@code cachedDeclaringClass}
	 * and {@code cachedEnclosingClass}. The reason default value has to be null is that
	 * j.l.Class instances are created by the VM only rather than Java, and
	 * any instance field with non-null default values have to be set by VM natives.
	 */
	private static final class ClassReflectNullPlaceHolder {}

	private static final class MetadataCache {
		MetadataCache() {}

		static long cachedCanonicalNameOffset = -1;
		static long cachedSimpleNameOffset = -1;

		SoftReference<String> cachedCanonicalName;
		SoftReference<String> cachedSimpleName;
	}

	private transient MetadataCache metadataCache;
	private static long metadataCacheOffset = -1;

	private transient Class<?>[] cachedInterfaces;
	private static long cachedInterfacesOffset = -1;

	private transient Class<?> cachedDeclaringClass;
	private static long cachedDeclaringClassOffset = -1;

	private transient Class<?> cachedEnclosingClass;
	private static long cachedEnclosingClassOffset = -1;
	
	private transient boolean cachedCheckInnerClassAttr;

	private static Annotation[] EMPTY_ANNOTATION_ARRAY = new Annotation[0];
	
	static MethodHandles.Lookup implLookup;

	private static final Unsafe unsafe = Unsafe.getUnsafe();
	
	static Unsafe getUnsafe() {
		return unsafe;
	}
	
/*[IF JAVA_SPEC_VERSION >= 11]*/
	private Class<?> nestHost;
/*[ENDIF] JAVA_SPEC_VERSION >= 11 */

/*[IF JAVA_SPEC_VERSION >= 15]*/
	private transient Object classData;
/*[ENDIF] JAVA_SPEC_VERSION >= 15*/

/*[IF JAVA_SPEC_VERSION >= 16]*/
	private transient Class<?>[] cachedPermittedSubclasses;
	private static long cachedPermittedSubclassesOffset = -1;
/*[ENDIF] JAVA_SPEC_VERSION >= 16 */

/**
 * Prevents this class from being instantiated. Instances
 * created by the virtual machine only.
 */
private Class() {}

/*
 * Ensure the caller has the requested type of access.
 * 
 * @param		security			the current SecurityManager
 * @param		callerClassLoader	the ClassLoader of the caller of the original protected API
 * @param		type				type of access, PUBLIC, DECLARED or INVALID
 * 
 */
void checkMemberAccess(SecurityManager security, ClassLoader callerClassLoader, int type) {
	if (callerClassLoader != ClassLoader.bootstrapClassLoader) {
		ClassLoader loader = getClassLoaderImpl();
		/*[PR CMVC 82311] Spec is incorrect before 1.5, RI has this behavior since 1.2 */
		/*[PR CMVC 201490] To remove CheckPackageAccess call from more Class methods */
		if (type == Member.DECLARED && callerClassLoader != loader) {
			security.checkPermission(SecurityConstants.CHECK_MEMBER_ACCESS_PERMISSION);
		}
		/*[PR CMVC 195558, 197433, 198986] Various fixes. */
		if (sun.reflect.misc.ReflectUtil.needsPackageAccessCheck(callerClassLoader, loader)) {	
			if (Proxy.isProxyClass(this)) {
				sun.reflect.misc.ReflectUtil.checkProxyPackageAccess(callerClassLoader, this.getInterfaces());
			} else {
				String packageName = this.getPackageName();
				if ((packageName != null) && (packageName != "")) { //$NON-NLS-1$
					security.checkPackageAccess(packageName);
				}
			}
		}
	}
}

/**
 * Ensure the caller has the requested type of access.
 * 
 * This helper method is only called by getClasses, and skip security.checkPackageAccess()
 * when the class is a ProxyClass and the package name is sun.proxy.
 *
 * @param		type			type of access, PUBLIC or DECLARED
 * 
 */
private void checkNonSunProxyMemberAccess(SecurityManager security, ClassLoader callerClassLoader, int type) {
	if (callerClassLoader != ClassLoader.bootstrapClassLoader) {
		ClassLoader loader = getClassLoaderImpl();
		if (type == Member.DECLARED && callerClassLoader != loader) {
			security.checkPermission(SecurityConstants.CHECK_MEMBER_ACCESS_PERMISSION);
		}
		String packageName = this.getPackageName();
		if (!(Proxy.isProxyClass(this) && packageName.equals(sun.reflect.misc.ReflectUtil.PROXY_PACKAGE)) &&
				packageName != null && packageName != "" && sun.reflect.misc.ReflectUtil.needsPackageAccessCheck(callerClassLoader, loader)) //$NON-NLS-1$	
		{
			security.checkPackageAccess(packageName);
		}
	}
}

private static void forNameAccessCheck(final SecurityManager sm, final Class<?> callerClass, final Class<?> foundClass) {
	if (null != callerClass) {
		ProtectionDomain pd = callerClass.getPDImpl();
		if (null != pd) {
			AccessController.doPrivileged(new PrivilegedAction<Object>() {
				@Override
				public Object run() {
					foundClass.checkMemberAccess(sm, callerClass.getClassLoaderImpl(), MEMBER_INVALID_TYPE);
					return null;
				}
			}, new AccessControlContext(new ProtectionDomain[]{pd}));
		}
	}
}

/**
 * Answers a Class object which represents the class
 * named by the argument. The name should be the name
 * of a class as described in the class definition of
 * java.lang.Class, however Classes representing base 
 * types can not be found using this method.
 *
 * @param		className	The name of the non-base type class to find
 * @return		the named Class
 * @throws		ClassNotFoundException If the class could not be found
 *
 * @see			java.lang.Class
 */
@CallerSensitive
public static Class<?> forName(String className) throws ClassNotFoundException {
	@SuppressWarnings("removal")
	SecurityManager sm = null;
	/**
	 * Get the SecurityManager from System.  If the VM has not yet completed bootstrapping (i.e., J9VMInternals.initialized is still false)
	 * sm is kept as null without referencing System in order to avoid loading System earlier than necessary.
	 */
	if (J9VMInternals.initialized) {
		sm = System.getSecurityManager();
	}
	if (null == sm) {
		return forNameImpl(className, true, ClassLoader.callerClassLoader());
	}
	Class<?> caller = getStackClass(1);
	ClassLoader callerClassLoader = null;
	if (null != caller) {
		callerClassLoader = caller.getClassLoaderImpl();
	}
	Class<?> c = forNameImpl(className, false, callerClassLoader);
	forNameAccessCheck(sm, caller, c);
	J9VMInternals.initialize(c);
	return c;
}

AnnotationType getAnnotationType() {
	AnnotationVars localAnnotationVars = getAnnotationVars();
	return localAnnotationVars.annotationType;
}
void setAnnotationType(AnnotationType t) {
	AnnotationVars localAnnotationVars = getAnnotationVars();
	localAnnotationVars.annotationType = t;
}
/**
 * Compare-And-Swap the AnnotationType instance corresponding to this class.
 * (This method only applies to annotation types.)
 */
boolean casAnnotationType(AnnotationType oldType, AnnotationType newType) {
	AnnotationVars localAnnotationVars = getAnnotationVars();
	long localTypeOffset = AnnotationVars.annotationTypeOffset;
	if (-1 == localTypeOffset) {
		Field field = AccessController.doPrivileged(new PrivilegedAction<Field>() {
			public Field run() {
				try {
					return AnnotationVars.class.getDeclaredField("annotationType"); //$NON-NLS-1$
				} catch (Exception e) {
					throw newInternalError(e);
				}
			}
		});
		localTypeOffset = getUnsafe().objectFieldOffset(field);
		AnnotationVars.annotationTypeOffset = localTypeOffset;
	}
/*[IF Sidecar19-SE]*/
	return getUnsafe().compareAndSetObject(localAnnotationVars, localTypeOffset, oldType, newType);
/*[ELSE]
	return getUnsafe().compareAndSwapObject(localAnnotationVars, localTypeOffset, oldType, newType);
/*[ENDIF]*/	
}

/**
 * Answers a Class object which represents the class
 * named by the argument. The name should be the name
 * of a class as described in the class definition of
 * java.lang.Class, however Classes representing base 
 * types can not be found using this method.
 * Security rules will be obeyed.
 * 
 * @param		className			The name of the non-base type class to find
 * @param		initializeBoolean	A boolean indicating whether the class should be 
 *									initialized
 * @param		classLoader			The classloader to use to load the class
 * @return		the named class.
 * @throws		ClassNotFoundException If the class could not be found
 *
 * @see			java.lang.Class
 */
@CallerSensitive
public static Class<?> forName(String className, boolean initializeBoolean, ClassLoader classLoader)
	throws ClassNotFoundException
{
	@SuppressWarnings("removal")
	SecurityManager sm = null;
	if (J9VMInternals.initialized) {
		sm = System.getSecurityManager();
	}
	if (null == sm) {
		return forNameImpl(className, initializeBoolean, classLoader);
	}
	Class<?> caller = getStackClass(1);
	/* perform security checks */
	if (null == classLoader) {
		if (null != caller) {
			ClassLoader callerClassLoader = caller.getClassLoaderImpl();
			if (callerClassLoader != ClassLoader.bootstrapClassLoader) {
				/* only allowed if caller has RuntimePermission("getClassLoader") permission */
				sm.checkPermission(SecurityConstants.GET_CLASSLOADER_PERMISSION);
			}
		}
	}
	Class<?> c = forNameImpl(className, false, classLoader);
	forNameAccessCheck(sm, caller, c);
	if (initializeBoolean) {
		J9VMInternals.initialize(c);
	}
	return c;
}

/*[IF Sidecar19-SE]*/
/**
 * Answers a Class object which represents the class
 * with the given name in the given module. 
 * The name should be the name of a class as described 
 * in the class definition of java.lang.Class, 
 * however Classes representing base 
 * types can not be found using this method.
 * It does not invoke the class initializer.
 * Note that this method does not check whether the 
 * requested class is accessible to its caller. 
 * Security rules will be obeyed.
 * 
 * @param module The name of the module
 * @param name The name of the non-base type class to find
 * 
 * @return The Class object representing the named class
 *
 * @see	java.lang.Class
 */
@CallerSensitive
public static Class<?> forName(Module module, String name)
{
	@SuppressWarnings("removal")
	SecurityManager sm = null;
	ClassLoader classLoader;
	Class<?> c;
	
	if ((null == module) || (null == name)) {
		throw new NullPointerException();
	}
	if (J9VMInternals.initialized) {
		sm = System.getSecurityManager();
	}
	Class<?> caller = getStackClass(1);
	if (null != sm) {
		/* If the caller is not the specified module and RuntimePermission("getClassLoader") permission is denied, throw SecurityException */
		if ((null != caller) && (caller.getModule() != module)) {
			sm.checkPermission(SecurityConstants.GET_CLASSLOADER_PERMISSION);
		}
		classLoader = AccessController.doPrivileged(new PrivilegedAction<ClassLoader>() {
	        public ClassLoader run() {
	        	return module.getClassLoader();
	        }
		});
	} else {
		classLoader = module.getClassLoader();
	}
	
	try {
		if (classLoader == null) {
			c = ClassLoader.bootstrapClassLoader.loadClass(module, name);
		} else {
			c = classLoader.loadClassHelper(name, false, false, module);
		}
	} catch (ClassNotFoundException e) {
		/* This method returns null on failure rather than throwing a ClassNotFoundException */
		return null;
	}
	if (null != c) {
		/* If the class loader of the given module defines other modules and 
		 * the given name is a class defined in a different module, 
		 * this method returns null after the class is loaded. 
		 */
		if (c.getModule() != module) {
			return null;
		}
	}
	return c;
}
/*[ENDIF] Sidecar19-SE */

/**
 * Answers a Class object which represents the class
 * named by the argument. The name should be the name
 * of a class as described in the class definition of
 * java.lang.Class, however Classes representing base 
 * types can not be found using this method.
 *
 * @param		className			The name of the non-base type class to find
 * @param		initializeBoolean	A boolean indicating whether the class should be 
 *									initialized
 * @param		classLoader			The classloader to use to load the class
 * @return		the named class.
 * @throws		ClassNotFoundException If the class could not be found
 *
 * @see			java.lang.Class
 */
private static native Class<?> forNameImpl(String className,
                            boolean initializeBoolean,
                            ClassLoader classLoader)
	throws ClassNotFoundException;

/**
 * Answers an array containing all public class members
 * of the class which the receiver represents and its
 * superclasses and interfaces
 *
 * @return		the class' public class members
 * @throws		SecurityException If member access is not allowed
 *
 * @see			java.lang.Class
 */
@CallerSensitive
public Class<?>[] getClasses() {
	/*[PR CMVC 82311] Spec is incorrect before 1.5, RI has this behavior since 1.2 */
	@SuppressWarnings("removal")
	SecurityManager security = System.getSecurityManager();
	if (security != null) {
		ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
		checkNonSunProxyMemberAccess(security, callerClassLoader, Member.PUBLIC);
	}

	java.util.Vector<Class<?>> publicClasses = new java.util.Vector<>();
	Class<?> current = this;
	Class<?>[] classes;
	while(current != null) {
		/*[PR 97353] Call the native directly, as the security check in getDeclaredClasses() does nothing but check the caller classloader */
		classes = current.getDeclaredClassesImpl();
		for (int i = 0; i < classes.length; i++)
			if (Modifier.isPublic(classes[i].getModifiers()))
				publicClasses.addElement(classes[i]);
		current = current.getSuperclass();
	}
	classes = new Class<?>[publicClasses.size()];
	publicClasses.copyInto(classes);
	return classes;
}

/**
 * Answers the classloader which was used to load the
 * class represented by the receiver. Answer null if the
 * class was loaded by the system class loader.
 *
 * @return		the receiver's class loader or nil
 *
 * @see			java.lang.ClassLoader
 */
@CallerSensitive
public ClassLoader getClassLoader() {
	if (null != classLoader) {
		if (classLoader == ClassLoader.bootstrapClassLoader)	{
			return null;
		}
		@SuppressWarnings("removal")
		SecurityManager security = System.getSecurityManager();
		if (null != security) {
			ClassLoader callersClassLoader = ClassLoader.callerClassLoader();
			if (ClassLoader.needsClassLoaderPermissionCheck(callersClassLoader, classLoader)) {
				security.checkPermission(SecurityConstants.GET_CLASSLOADER_PERMISSION);
			}
		}
	}
	return classLoader;
}

/**
 * Returns the classloader used to load the receiver's class.
 * Returns null if the class was loaded by the bootstrap (system) class loader.
 * This skips security checks.
 * @return the receiver's class loader or null
 * @see java.lang.ClassLoader
 */
ClassLoader internalGetClassLoader() {
	return (classLoader == ClassLoader.bootstrapClassLoader)? null: classLoader;
}

/**
 * Answers the ClassLoader which was used to load the
 * class represented by the receiver.
 *
 * @return		the receiver's class loader
 *
 * @see			java.lang.ClassLoader
 */
ClassLoader getClassLoader0() {
	ClassLoader loader = getClassLoaderImpl();
	return loader;
}


/**
 * Return the ClassLoader for this Class without doing any security
 * checks. The bootstrap ClassLoader is returned, unlike getClassLoader()
 * which returns null in place of the bootstrap ClassLoader. 
 * 
 * @return the ClassLoader
 * 
 * @see ClassLoader#isASystemClassLoader()
 */
ClassLoader getClassLoaderImpl()
{
	return classLoader;
}

/**
 * Answers a Class object which represents the receiver's
 * component type if the receiver represents an array type.
 * Otherwise answers nil. The component type of an array
 * type is the type of the elements of the array.
 *
 * @return		the component type of the receiver.
 *
 * @see			java.lang.Class
 */
public native Class<?> getComponentType();

private NoSuchMethodException newNoSuchMethodException(String name, Class<?>[] types) {
	StringBuilder error = new StringBuilder();
	error.append(getName()).append('.').append(name).append('(');
	/*[PR CMVC 80340] check for null types */
	for (int i = 0; i < types.length; ++i) {
		if (i != 0) error.append(", "); //$NON-NLS-1$
		error.append(types[i] == null ? null : types[i].getName());
	}
	error.append(')');
	return new NoSuchMethodException(error.toString());
}

/**
 * Answers a public Constructor object which represents the
 * constructor described by the arguments.
 *
 * @param		parameterTypes	the types of the arguments.
 * @return		the constructor described by the arguments.
 * @throws		NoSuchMethodException if the constructor could not be found.
 * @throws		SecurityException if member access is not allowed
 * 
 * @see			#getConstructors
 */
@CallerSensitive
public Constructor<T> getConstructor(Class<?>... parameterTypes) throws NoSuchMethodException, SecurityException {
	@SuppressWarnings("removal")
	SecurityManager security = System.getSecurityManager();
	if (security != null) {
		ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
		checkMemberAccess(security, callerClassLoader, Member.PUBLIC);
	}

	/*[PR CMVC 114820, CMVC 115873, CMVC 116166] add reflection cache */
	if (parameterTypes == null) parameterTypes = EmptyParameters;
	Constructor<T> cachedConstructor = lookupCachedConstructor(parameterTypes);
	if (cachedConstructor != null && Modifier.isPublic(cachedConstructor.getModifiers())) {
		return cachedConstructor;
	}

	/*[PR CMVC 192714,194493] prepare the class before attempting to access members */
	J9VMInternals.prepare(this);

	Constructor<T> rc;
	// Handle the default constructor case upfront
	if (parameterTypes.length == 0) {
		rc = getConstructorImpl(parameterTypes, "()V"); //$NON-NLS-1$
	} else {
		parameterTypes = parameterTypes.clone();
		// Build a signature for the requested method.
		String signature = getParameterTypesSignature(true, "<init>", parameterTypes, "V"); //$NON-NLS-1$ //$NON-NLS-2$
		rc = getConstructorImpl(parameterTypes, signature);
		if (rc != null)
			rc = checkParameterTypes(rc, parameterTypes);
	}
	if (rc == null) throw newNoSuchMethodException("<init>", parameterTypes); //$NON-NLS-1$
	return cacheConstructor(rc);
}

/**
 * Answers a public Constructor object which represents the
 * constructor described by the arguments.
 *
 * @param		parameterTypes	the types of the arguments.
 * @param		signature		the signature of the method.
 * @return		the constructor described by the arguments.
 * 
 * @see			#getConstructors
 */
private native Constructor<T> getConstructorImpl(Class<?> parameterTypes[], String signature);

/**
 * Answers an array containing Constructor objects describing
 * all constructors which are visible from the current execution
 * context.
 *
 * @return		all visible constructors starting from the receiver.
 * @throws		SecurityException if member access is not allowed
 * 
 * @see			#getMethods
 */
@CallerSensitive
public Constructor<?>[] getConstructors() throws SecurityException {
	@SuppressWarnings("removal")
	SecurityManager security = System.getSecurityManager();
	if (security != null) {
		ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
		checkMemberAccess(security, callerClassLoader, Member.PUBLIC);
	}

	/*[PR CMVC 114820, CMVC 115873, CMVC 116166] add reflection cache */
	Constructor<T>[] cachedConstructors = lookupCachedConstructors(CacheKey.PublicConstructorsKey);
	if (cachedConstructors != null) {
		return cachedConstructors;
	}

	/*[PR CMVC 192714,194493] prepare the class before attempting to access members */
	J9VMInternals.prepare(this);

	Constructor<T>[] ctors = getConstructorsImpl();

	return cacheConstructors(ctors, CacheKey.PublicConstructorsKey);
}

/**
 * Answers an array containing Constructor objects describing
 * all constructors which are visible from the current execution
 * context.
 *
 * @return		all visible constructors starting from the receiver.
 * 
 * @see			#getMethods
 */
private native Constructor<T>[] getConstructorsImpl();

/**
 * Answers an array containing all class members of the class 
 * which the receiver represents. Note that some of the fields 
 * which are returned may not be visible in the current 
 * execution context.
 *
 * @return		the class' class members
 * @throws		SecurityException if member access is not allowed
 *
 * @see			java.lang.Class
 */
@CallerSensitive
public Class<?>[] getDeclaredClasses() throws SecurityException {
	@SuppressWarnings("removal")
	SecurityManager security = System.getSecurityManager();
	if (security != null) {
		ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
		checkNonSunProxyMemberAccess(security, callerClassLoader, Member.DECLARED);
	}

	/*[PR 97353] getClasses() calls this native directly */
	return getDeclaredClassesImpl();
}

/**
 * Answers an array containing all class members of the class 
 * which the receiver represents. Note that some of the fields 
 * which are returned may not be visible in the current 
 * execution context.
 *
 * @return		the class' class members
 *
 * @see			java.lang.Class
 */
private native Class<?>[] getDeclaredClassesImpl();

/**
 * Answers a Constructor object which represents the
 * constructor described by the arguments.
 *
 * @param		parameterTypes	the types of the arguments.
 * @return		the constructor described by the arguments.
 * @throws		NoSuchMethodException if the constructor could not be found.
 * @throws		SecurityException if member access is not allowed
 * 
 * @see			#getConstructors
 */
@CallerSensitive
public Constructor<T> getDeclaredConstructor(Class<?>... parameterTypes) throws NoSuchMethodException, SecurityException {
	@SuppressWarnings("removal")
	SecurityManager security = System.getSecurityManager();
	if (security != null) {
		ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
		checkMemberAccess(security, callerClassLoader, Member.DECLARED);
	}

	/*[PR CMVC 114820, CMVC 115873, CMVC 116166] add reflection cache */
	if (parameterTypes == null) parameterTypes = EmptyParameters;
	Constructor<T> cachedConstructor = lookupCachedConstructor(parameterTypes);
	if (cachedConstructor != null) {
		return cachedConstructor;
	}

	/*[PR CMVC 192714,194493] prepare the class before attempting to access members */
	J9VMInternals.prepare(this);

	Constructor<T> rc; 
	// Handle the default constructor case upfront
	if (parameterTypes.length == 0) {
		rc = getDeclaredConstructorImpl(parameterTypes, "()V"); //$NON-NLS-1$
	} else {
		parameterTypes = parameterTypes.clone();
		// Build a signature for the requested method.
		String signature = getParameterTypesSignature(true, "<init>", parameterTypes, "V"); //$NON-NLS-1$ //$NON-NLS-2$
		rc = getDeclaredConstructorImpl(parameterTypes, signature);
		if (rc != null)
			rc = checkParameterTypes(rc, parameterTypes);
	}
	if (rc == null) throw newNoSuchMethodException("<init>", parameterTypes); //$NON-NLS-1$
	return cacheConstructor(rc);
}

/**
 * Answers a Constructor object which represents the
 * constructor described by the arguments.
 *
 * @param		parameterTypes	the types of the arguments.
 * @param		signature		the signature of the method.
 * @return		the constructor described by the arguments.
 * 
 * @see			#getConstructors
 */
private native Constructor<T> getDeclaredConstructorImpl(Class<?>[] parameterTypes, String signature);

/**
 * Answers an array containing Constructor objects describing
 * all constructor which are defined by the receiver. Note that
 * some of the fields which are returned may not be visible
 * in the current execution context.
 *
 * @return		the receiver's constructors.
 * @throws		SecurityException if member access is not allowed
 * 
 * @see			#getMethods
 */
@CallerSensitive
public Constructor<?>[] getDeclaredConstructors() throws SecurityException {
	@SuppressWarnings("removal")
	SecurityManager security = System.getSecurityManager();
	if (security != null) {
		ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
		checkMemberAccess(security, callerClassLoader, Member.DECLARED);
	}

	/*[PR CMVC 114820, CMVC 115873, CMVC 116166] add reflection cache */
	Constructor<T>[] cachedConstructors = lookupCachedConstructors(CacheKey.DeclaredConstructorsKey);
	if (cachedConstructors != null) {
		return cachedConstructors;
	}
	
	/*[PR CMVC 192714,194493] prepare the class before attempting to access members */
	J9VMInternals.prepare(this);

	Constructor<T>[] ctors = getDeclaredConstructorsImpl();
	
	return cacheConstructors(ctors, CacheKey.DeclaredConstructorsKey);
}

/**
 * Answers an array containing Constructor objects describing
 * all constructor which are defined by the receiver. Note that
 * some of the fields which are returned may not be visible
 * in the current execution context.
 *
 * @return		the receiver's constructors.
 * 
 * @see			#getMethods
 */
private native Constructor<T>[] getDeclaredConstructorsImpl();

/**
 * Answers a Field object describing the field in the receiver
 * named by the argument. Note that the Constructor may not be
 * visible from the current execution context.
 *
 * @param		name		The name of the field to look for.
 * @return		the field in the receiver named by the argument.
 * @throws		NoSuchFieldException if the requested field could not be found
 * @throws		SecurityException if member access is not allowed
 * 
 * @see			#getDeclaredFields
 */
@CallerSensitive
public Field getDeclaredField(String name) throws NoSuchFieldException, SecurityException {
	@SuppressWarnings("removal")
	SecurityManager security = System.getSecurityManager();
	if (security != null) {
		ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
		checkMemberAccess(security, callerClassLoader, Member.DECLARED);
	}
	return getDeclaredFieldInternal(name, true);
}

/**
 * A private helper method for getDeclaredField().
 * This is for internal usage, no security check required,
 * and if doCache is false, the field is to be retrieved without caching
 * which can avoid circular dependency at JVM bootstrapping phase.
 *
 * @param		name		The name of the field to look for.
 * @param		doCache		The flag to determine if caching the field.
 * @return		the field in the receiver named by the argument.
 * @throws		NoSuchFieldException if the requested field could not be found
 */
@CallerSensitive
private Field getDeclaredFieldInternal(String name, boolean doCache) throws NoSuchFieldException {
	if (doCache) {
		/*[PR CMVC 114820, CMVC 115873, CMVC 116166] add reflection cache */
		Field cachedField = lookupCachedField(name);
		if (cachedField != null && cachedField.getDeclaringClass() == this) {
			return cachedField;
		}
	}

	/*[PR CMVC 192714,194493] prepare the class before attempting to access members */
	J9VMInternals.prepare(this);

	Field field = getDeclaredFieldImpl(name);
	
	/*[PR JAZZ 102876] IBM J9VM not using Reflection.filterFields API to hide the sensitive fields */
	Field[] fields = Reflection.filterFields(this, new Field[] {field});
	if (0 == fields.length) {
		throw new NoSuchFieldException(name);
	}
	
	if (doCache) {
		return cacheField(fields[0]);
	} else {
		return fields[0];
	}
}

/**
 * Answers a Field object describing the field in the receiver
 * named by the argument. Note that the Constructor may not be
 * visible from the current execution context.
 *
 * @param		name		The name of the field to look for.
 * @return		the field in the receiver named by the argument.
 * @throws		NoSuchFieldException If the given field does not exist
 * 
 * @see			#getDeclaredFields
 */
private native Field getDeclaredFieldImpl(String name) throws NoSuchFieldException;

/**
 * Answers an array containing Field objects describing
 * all fields which are defined by the receiver. Note that
 * some of the fields which are returned may not be visible
 * in the current execution context.
 *
 * @return		the receiver's fields.
 * @throws		SecurityException If member access is not allowed
 * 
 * @see			#getFields
 */
@CallerSensitive
public Field[] getDeclaredFields() throws SecurityException {
	@SuppressWarnings("removal")
	SecurityManager security = System.getSecurityManager();
	if (security != null) {
		ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
		checkMemberAccess(security, callerClassLoader, Member.DECLARED);
	}

	/*[PR CMVC 114820, CMVC 115873, CMVC 116166] add reflection cache */
	Field[] cachedFields = lookupCachedFields(CacheKey.DeclaredFieldsKey);
	if (cachedFields != null) {
		return cachedFields;
	}

	/*[PR CMVC 192714,194493] prepare the class before attempting to access members */
	J9VMInternals.prepare(this);

	Field[] fields = getDeclaredFieldsImpl();

	return cacheFields(Reflection.filterFields(this, fields), CacheKey.DeclaredFieldsKey);
}

/**
 * Answers an array containing Field objects describing
 * all fields which are defined by the receiver. Note that
 * some of the fields which are returned may not be visible
 * in the current execution context.
 *
 * @return		the receiver's fields.
 * 
 * @see			#getFields
 */
private native Field[] getDeclaredFieldsImpl();

/*[IF JAVA_SPEC_VERSION >= 11]*/
/**
 * Answers a list of method objects which represent the public methods
 * described by the arguments. Note that the associated method may not 
 * be visible from the current execution context.
 * An empty list is returned if the method can't be found.
 *
 * @param		name			the name of the method
 * @param		parameterTypes	the types of the arguments.
 * @return		a list of methods described by the arguments.
 *
 * @see			#getMethods
 */
@CallerSensitive
List<Method> getDeclaredPublicMethods(String name, Class<?>... parameterTypes) {
	CacheKey ck = CacheKey.newDeclaredPublicMethodsKey(name, parameterTypes);
	List<Method> methodList = lookupCachedDeclaredPublicMethods(ck);
	if (methodList != null) {
		return methodList;
	}
	try {
		methodList = new ArrayList<>();
		getMethodHelper(false, true, methodList, name, parameterTypes);
	} catch (NoSuchMethodException e) {
		// no NoSuchMethodException expected
	}
	return cacheDeclaredPublicMethods(methodList, ck);
}

private List<Method> lookupCachedDeclaredPublicMethods(CacheKey cacheKey) {
	if (!reflectCacheEnabled) return null;
	if (reflectCacheDebug) {
		reflectCacheDebugHelper(null, 0, "lookup DeclaredPublicMethods in: ", getName());	//$NON-NLS-1$
	}
	ReflectCache cache = peekReflectCache();
	if (cache != null) {
		List<Method> methods = (List<Method>) cache.find(cacheKey);
		if (methods != null) {
			// assuming internal caller won't change this method list content
			return methods;
		}
	}
	return null;
}
@CallerSensitive
private List<Method> cacheDeclaredPublicMethods(List<Method> methods, CacheKey cacheKey) {
	if (!reflectCacheEnabled
		|| (reflectCacheAppOnly && ClassLoader.getStackClassLoader(2) == ClassLoader.bootstrapClassLoader) 
	) {	
		return methods;
	}
	if (reflectCacheDebug) {
		reflectCacheDebugHelper(null, 0, "cache DeclaredPublicMethods in: ", getName());	//$NON-NLS-1$
	}
	ReflectCache cache = null;
	try {
		cache = acquireReflectCache();
		for (int i = 0; i < methods.size(); i++) {
			Method method = methods.get(i);
			CacheKey key = CacheKey.newMethodKey(method.getName(), getParameterTypes(method), method.getReturnType());
			Method methodPut = cache.insertIfAbsent(key, method);
			if (method != methodPut) {
				methods.set(i, methodPut);
			}
		}
		cache.insert(cacheKey, methods);
	} finally {
		if (cache != null) {
			cache.release();
		}
	}
	return methods;
}
/*[ENDIF] JAVA_SPEC_VERSION >= 11 */

/**
 * A helper method for reflection debugging
 * 
 * @param parameters parameters[i].getName() to be appended 
 * @param posInsert parameters to be appended AFTER msgs[posInsert]
 * @param msgs a message array
 */
static void reflectCacheDebugHelper(Class<?>[] parameters, int posInsert, String... msgs) {
	StringBuilder output = new StringBuilder(200);
	for (int i = 0; i < msgs.length; i++) {
		output.append(msgs[i]);
		if ((parameters != null) && (i == posInsert)) {
			output.append('(');
			for (int j = 0; j < parameters.length; j++) {
				if (j != 0) {
					output.append(", "); //$NON-NLS-1$
				}
				output.append(parameters[i].getName());
			}
			output.append(')');
		}
	}
	System.err.println(output);
}

/**
 * Answers a Method object which represents the method
 * described by the arguments. Note that the associated
 * method may not be visible from the current execution
 * context.
 *
 * @param		name			the name of the method
 * @param		parameterTypes	the types of the arguments.
 * @return		the method described by the arguments.
 * @throws		NoSuchMethodException if the method could not be found.
 * @throws		SecurityException If member access is not allowed
 *
 * @see			#getMethods
 */
@CallerSensitive
public Method getDeclaredMethod(String name, Class<?>... parameterTypes) throws NoSuchMethodException, SecurityException {
	@SuppressWarnings("removal")
	SecurityManager security = System.getSecurityManager();
	if (security != null) {
		ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
		checkMemberAccess(security, callerClassLoader, Member.DECLARED);
	}
	return getMethodHelper(true, true, null, name, parameterTypes);
}

/**
 * This native iterates over methods matching the provided name and signature
 * in the receiver class. The startingPoint parameter is passed the last
 * method returned (or null on the first use), and the native returns the next
 * matching method or null if there are no more matches. 
 * Note that the associated method may not be visible from the
 * current execution context.
 *
 * @param		name				the name of the method
 * @param		parameterTypes		the types of the arguments.
 * @param		partialSignature	the signature of the method, without return type.
 * @param		startingPoint		the method to start searching after, or null to start at the beginning
 * @return		the next Method described by the arguments
 * 
 * @see			#getMethods
 */
private native Method getDeclaredMethodImpl(String name, Class<?>[] parameterTypes, String partialSignature, Method startingPoint);

/**
 * Answers an array containing Method objects describing
 * all methods which are defined by the receiver. Note that
 * some of the methods which are returned may not be visible
 * in the current execution context.
 *
 * @throws		SecurityException	if member access is not allowed
 * @return		the receiver's methods.
 *
 * @see			#getMethods
 */
@CallerSensitive
public Method[] getDeclaredMethods() throws SecurityException {
	@SuppressWarnings("removal")
	SecurityManager security = System.getSecurityManager();
	if (security != null) {
		ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
		checkMemberAccess(security, callerClassLoader, Member.DECLARED);
	}

	/*[PR CMVC 114820, CMVC 115873, CMVC 116166] add reflection cache */
	Method[] cachedMethods = lookupCachedMethods(CacheKey.DeclaredMethodsKey);
	if (cachedMethods != null) {
		return cachedMethods;
	}

	/*[PR CMVC 192714,194493] prepare the class before attempting to access members */
	J9VMInternals.prepare(this);

	/*[PR CMVC 194301] do not allow reflect access to sun.misc.Unsafe.getUnsafe() */
	return cacheMethods(Reflection.filterMethods(this, getDeclaredMethodsImpl()), CacheKey.DeclaredMethodsKey);
}

/**
 * Answers an array containing Method objects describing
 * all methods which are defined by the receiver. Note that
 * some of the methods which are returned may not be visible
 * in the current execution context.
 *
 * @return		the receiver's methods.
 *
 * @see			#getMethods
 */
private native Method[] getDeclaredMethodsImpl();

/**
 * Answers the class which declared the class represented
 * by the receiver. This will return null if the receiver
 * is not a member of another class.
 *
 * @return		the declaring class of the receiver.
 */
@CallerSensitive
public Class<?> getDeclaringClass() {
	if (cachedDeclaringClassOffset == -1) {
		cachedDeclaringClassOffset = getFieldOffset("cachedDeclaringClass"); //$NON-NLS-1$
	}
	if (cachedDeclaringClass == null) {
		Class<?> localDeclaringClass = getDeclaringClassImpl();
		if (localDeclaringClass == null) {
			localDeclaringClass = ClassReflectNullPlaceHolder.class;
		}
		writeFieldValue(cachedDeclaringClassOffset, localDeclaringClass);
	}

	/**
	 * ClassReflectNullPlaceHolder.class means the value of cachedDeclaringClass is null
	 * @see ClassReflectNullPlaceHolder.class
	 */
	Class<?> declaringClass = cachedDeclaringClass == ClassReflectNullPlaceHolder.class ? null : cachedDeclaringClass;
	if (declaringClass == null) {
		if (cachedCheckInnerClassAttr == false) {
			/* Check whether the enclosing class has an valid inner class entry to the current class.
			 * Note: the entries are populated with the InnerClass attribute when creating ROM class. 
			 */
			checkInnerClassAttrOfEnclosingClass();
			cachedCheckInnerClassAttr = true;
		}
		return declaringClass;
	}
	if (declaringClass.isClassADeclaredClass(this)) {
		@SuppressWarnings("removal")
		SecurityManager security = System.getSecurityManager();
		if (security != null) {
			ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
			declaringClass.checkMemberAccess(security, callerClassLoader, MEMBER_INVALID_TYPE);
		}
		return declaringClass;
	} else if (this.isClassADeclaredClass(declaringClass) || this.isClassADeclaredClass(this) || this.isCircularDeclaringClass()) {
		/* The execution of VM shouldn't be interrupted by corrupted InnerClasses attributes such as circular entries.
		 * To be specific, Class A is the declaringClass of B, Class B is the declaringClass of C, ..., Class Z is the
		 * declaringClass of A, which forms a circle in the entries, whether or not the relationships between two classes
		 * literally exist. e.g.
		 * The specified declaringClass is fake one (intentionally set in the class file) which actually has no
		 * relationship with current class but the current class is the declaring class of the specified declaringClass.
		 * Thus, there are 3 cases:
		 * 1) A --> B (real/fake) --> A: B is the declaring class of A and is one of the inner classes of A.
		 * 2) B --> A --> A: B is the declaring class of A while A is one of the inner classes of A itself.
		 * 3) A --> B --> C --> ... Z --> A: keep fetching the declaring class from A ends up wit A itself.
		 */
		return null;
	}
	
	/*[MSG "K0555", "incompatible InnerClasses attribute between \"{0}\" and \"{1}\""]*/
	throw new IncompatibleClassChangeError(
			com.ibm.oti.util.Msg.getString("K0555", this.getName(), declaringClass.getName())); //$NON-NLS-1$
}

/**
 * Checks whether the current class exists in the InnerClass attribute of the specified enclosing class
 * when this class is not defined directly inside the enclosing class (e.g. defined inside a method).
 * 
 * Note: The direct inner classes of the declaring class is already checked in getDeclaringClass()
 * when the enclosing class is the declaring class.
 */
private void checkInnerClassAttrOfEnclosingClass() {
	Class<?> enclosingClass = getEnclosingObjectClass();
	if ((enclosingClass != null) && !enclosingClass.isClassAEnclosedClass(this)) {
		/*[MSG "K0555", "incompatible InnerClasses attribute between \"{0}\" and \"{1}\""]*/
		throw new IncompatibleClassChangeError(
				com.ibm.oti.util.Msg.getString("K0555", this.getName(), enclosingClass.getName())); //$NON-NLS-1$
	}
}

/**
 * Returns true if a cycle exists from the current class to itself by repeatedly searching the declaring classes.
 *
 * @return		true if the cycle exists, false otherwise.
 *
 */
private native boolean isCircularDeclaringClass();

/**
 * Returns true if the class passed in to the method is a declared class of
 * this class. 
 *
 * @param		aClass		The class to validate
 * @return		true if aClass a declared class of this class
 * 				false otherwise.
 *
 */
private native boolean isClassADeclaredClass(Class<?> aClass);

/**
 * Returns true if the class passed in to the method is an enclosed class of
 * this class, which include both the declared classes and the classes defined
 * inside a method of this class.
 *
 * @param		aClass		The class to validate
 * @return		true if aClass an enclosed class of this class
 * 				false otherwise.
 *
 */
private native boolean isClassAEnclosedClass(Class<?> aClass);

/**
 * Answers the class which declared the class represented
 * by the receiver. This will return null if the receiver
 * is a member of another class.
 *
 * @return		the declaring class of the receiver.
 */
private native Class<?> getDeclaringClassImpl();

/**
 * Answers a Field object describing the field in the receiver
 * named by the argument which must be visible from the current 
 * execution context.
 *
 * @param		name		The name of the field to look for.
 * @return		the field in the receiver named by the argument.
 * @throws		NoSuchFieldException If the given field does not exist
 * @throws		SecurityException If access is denied
 * 
 * @see			#getDeclaredFields
 */
@CallerSensitive
public Field getField(String name) throws NoSuchFieldException, SecurityException {
	@SuppressWarnings("removal")
	SecurityManager security = System.getSecurityManager();
	if (security != null) {
		ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
		checkMemberAccess(security, callerClassLoader, Member.PUBLIC);
	}

	/*[PR CMVC 114820, CMVC 115873, CMVC 116166] add reflection cache */
	Field cachedField = lookupCachedField(name);
	if (cachedField != null && Modifier.isPublic(cachedField.getModifiers())) {
		return cachedField;
	}

	/*[PR CMVC 192714,194493] prepare the class before attempting to access members */
	J9VMInternals.prepare(this);

	Field field = getFieldImpl(name);
	
	if (0 == Reflection.filterFields(this, new Field[] {field}).length) {
		throw new NoSuchFieldException(name);
	}

	return cacheField(field);
}

/**
 * Answers a Field object describing the field in the receiver
 * named by the argument which must be visible from the current 
 * execution context.
 *
 * @param		name		The name of the field to look for.
 * @return		the field in the receiver named by the argument.
 * @throws		NoSuchFieldException If the given field does not exist
 *
 * @see			#getDeclaredFields
 */
private native Field getFieldImpl(String name) throws NoSuchFieldException;

/**
 * Answers an array containing Field objects describing
 * all fields which are visible from the current execution
 * context.
 *
 * @return		all visible fields starting from the receiver.
 * @throws		SecurityException If member access is not allowed
 *
 * @see			#getDeclaredFields
 */
@CallerSensitive
public Field[] getFields() throws SecurityException {
	@SuppressWarnings("removal")
	SecurityManager security = System.getSecurityManager();
	if (security != null) {
		ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
		checkMemberAccess(security, callerClassLoader, Member.PUBLIC);
	}

	/*[PR CMVC 114820, CMVC 115873, CMVC 116166] add reflection cache */
	Field[] cachedFields = lookupCachedFields(CacheKey.PublicFieldsKey);
	if (cachedFields != null) {
		return cachedFields;
	}

	/*[PR CMVC 192714,194493] prepare the class before attempting to access members */
	J9VMInternals.prepare(this);
	
	Field[] fields = getFieldsImpl();
	
	return cacheFields(Reflection.filterFields(this, fields), CacheKey.PublicFieldsKey);
}

/**
 * Answers an array containing Field objects describing
 * all fields which are visible from the current execution
 * context.
 *
 * @return		all visible fields starting from the receiver.
 *
 * @see			#getDeclaredFields
 */
private native Field[] getFieldsImpl();

/**
 * Answers an array of Class objects which match the interfaces
 * specified in the receiver classes <code>implements</code>
 * declaration
 *
 * @return		{@code Class<?>[]}
 *					the interfaces the receiver claims to implement.
 */
public Class<?>[] getInterfaces() {
	if (cachedInterfacesOffset == -1) {
		cachedInterfacesOffset = getFieldOffset("cachedInterfaces"); //$NON-NLS-1$
	}
	if (cachedInterfaces == null) {
		writeFieldValue(cachedInterfacesOffset, J9VMInternals.getInterfaces(this));
	}
	Class<?>[] newInterfaces = cachedInterfaces.length == 0 ? cachedInterfaces: cachedInterfaces.clone();
	return newInterfaces;
}

/**
 * Answers a Method object which represents the method
 * described by the arguments.
 *
 * @param		name String
 *					the name of the method
 * @param		parameterTypes {@code Class<?>[]}
 *					the types of the arguments.
 * @return		Method
 *					the method described by the arguments.
 * @throws	NoSuchMethodException
 *					if the method could not be found.
 * @throws	SecurityException
 *					if member access is not allowed
 *
 * @see			#getMethods
 */
@CallerSensitive
public Method getMethod(String name, Class<?>... parameterTypes) throws NoSuchMethodException, SecurityException {
	@SuppressWarnings("removal")
	SecurityManager security = System.getSecurityManager();
	if (security != null) {
		ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
		checkMemberAccess(security, callerClassLoader, Member.PUBLIC);
	}
	return getMethodHelper(true, false, null, name, parameterTypes);
}

/**
 * Helper method throws NoSuchMethodException when throwException is true, otherwise returns null. 
 */
private Method throwExceptionOrReturnNull(boolean throwException, String name, Class<?>... parameterTypes) throws NoSuchMethodException {
	if (throwException) {
		throw newNoSuchMethodException(name, parameterTypes);
	} else {
		return null;
	}
}
/**
 * Helper method for
 *	public Method getDeclaredMethod(String name, Class<?>... parameterTypes)
 *	public Method getMethod(String name, Class<?>... parameterTypes)
 *	List<Method> getDeclaredPublicMethods(String name, Class<?>... parameterTypes)
 * without going thorough security checking
 *
 * @param	throwException boolean
 *				true - throw exception in this helper;
 *				false - return null instead without throwing NoSuchMethodException
 * @param	forDeclaredMethod boolean
 *				true - for getDeclaredMethod(String name, Class<?>... parameterTypes)
 *						& getDeclaredPublicMethods(String name, Class<?>... parameterTypes);
 *				false - for getMethod(String name, Class<?>... parameterTypes);
 * @param	name String					the name of the method
 * @param	parameterTypes Class<?>[]	the types of the arguments
 * @param	methodList List<Method>		a list to store the methods described by the arguments
 * 										for getDeclaredPublicMethods()
 * 										or null for getDeclaredMethod() & getMethod()
 * @return	Method						the method described by the arguments.
 * @throws	NoSuchMethodException		if the method could not be found.
 */
@CallerSensitive
Method getMethodHelper(
	boolean throwException, boolean forDeclaredMethod, List<Method> methodList, String name, Class<?>... parameterTypes)
	throws NoSuchMethodException {
	Method result;
	Method bestCandidate;
	String strSig;
	boolean candidateFromInterface = false;
	
	/*[PR CMVC 114820, CMVC 115873, CMVC 116166] add reflection cache */
	if (parameterTypes == null) {
		parameterTypes = EmptyParameters;
	}
	if (methodList == null) {
		// getDeclaredPublicMethods() has to go through all methods anyway
		Method cachedMethod = lookupCachedMethod(name, parameterTypes);
		if ((cachedMethod != null) 
			&& ((forDeclaredMethod && (cachedMethod.getDeclaringClass() == this))
			|| (!forDeclaredMethod && Modifier.isPublic(cachedMethod.getModifiers())))) {
			return cachedMethod;
		}
	}

	/*[PR CMVC 192714,194493] prepare the class before attempting to access members */
	J9VMInternals.prepare(this);

	// Handle the no parameter case upfront
	/*[PR 103441] should throw NullPointerException when name is null */
	if (name == null || parameterTypes.length == 0) {
		strSig = "()"; //$NON-NLS-1$
		parameterTypes = EmptyParameters;
	} else {
		parameterTypes = parameterTypes.clone();
		// Build a signature for the requested method.
		strSig = getParameterTypesSignature(throwException, name, parameterTypes, ""); //$NON-NLS-1$
		if (strSig == null) {
			return null;
		}
	}
	
	if (forDeclaredMethod) {
		result = getDeclaredMethodImpl(name, parameterTypes, strSig, null);
	} else {
		if (this.isInterface()) {
			/* if the result is not in the current class, all superinterfaces will need to be searched */
			result = getDeclaredMethodImpl(name, parameterTypes, strSig, null);
			if (null == result) {
				result = getMostSpecificMethodFromAllInterfacesOfCurrentClass(null, null, name, parameterTypes);
				candidateFromInterface = true;
			}
		} else {
			result = getMethodImpl(name, parameterTypes, strSig);
			/* Retrieve the specified method implemented by the superclass from the top to the bottom. */
			if ((result != null) && result.getDeclaringClass().isInterface()) {
				HashMap<Class<?>, HashMap<MethodInfo, MethodInfo>> infoCache = new HashMap<>(16);
				result = getMostSpecificMethodFromAllInterfacesOfAllSuperclasses(infoCache, name, parameterTypes);
				candidateFromInterface = true;
			}
		}
	}
	
	if (result == null) {
		return throwExceptionOrReturnNull(throwException, name, parameterTypes);
	}
	if (0 == Reflection.filterMethods(this, new Method[] {result}).length) {
		return throwExceptionOrReturnNull(throwException, name, parameterTypes);
	}	

	/*[PR 127079] Use declaring classloader for Methods */
	/*[PR CMVC 104523] ensure parameter types are visible in the receiver's class loader */
	if (parameterTypes.length > 0) {
		ClassLoader loader = forDeclaredMethod ? getClassLoaderImpl() : result.getDeclaringClass().getClassLoaderImpl();
		for (int i = 0; i < parameterTypes.length; ++i) {
			Class<?> parameterType = parameterTypes[i];
			if (!parameterType.isPrimitive()) {
				try {
					if (Class.forName(parameterType.getName(), false, loader) != parameterType) {
						return throwExceptionOrReturnNull(throwException, name, parameterTypes);
					}
				} catch(ClassNotFoundException e) {
					return throwExceptionOrReturnNull(throwException, name, parameterTypes);
				}
			}
		}
	}
	if ((methodList != null) && ((result.getModifiers() & Modifier.PUBLIC) != 0)) {
		methodList.add(result);
	}
	
	/* [PR 113003] The native is called repeatedly until it returns null,
	 * as each call returns another match if one exists. The first call uses
	 * getMethodImpl which searches across superclasses and interfaces, but
	 * since the spec requires that we only weigh multiple matches against
	 * each other if they are in the same class, on subsequent calls we call
	 * getDeclaredMethodImpl on the declaring class of the first hit.
	 * If more than one match is found, more specific method is selected.
	 * For methods with same signature (name, parameter types) but different return types,
	 * Method N with return type S is more specific than M with return type R if:
	 * S is the same as or a subtype of R.
	 * Otherwise, the result method is chosen arbitrarily from specific methods.
	 */
	bestCandidate = result;
	if (!candidateFromInterface) {
		Class<?> declaringClass = forDeclaredMethod ? this : result.getDeclaringClass();
		while (true) {
			result = declaringClass.getDeclaredMethodImpl(name, parameterTypes, strSig, result);
			if (result == null) {
				break;
			}
			boolean publicMethod = ((result.getModifiers() & Modifier.PUBLIC) != 0);
			if ((methodList != null) && publicMethod) {
				methodList.add(result);
			}
			if (forDeclaredMethod || publicMethod) {
				// bestCandidate and result have same declaringClass.
				Class<?> candidateRetType = bestCandidate.getReturnType();
				Class<?> resultRetType = result.getReturnType();
				if ((candidateRetType != resultRetType) && candidateRetType.isAssignableFrom(resultRetType)) {
					bestCandidate = result;
				}
			}
		}
	}
	return cacheMethod(bestCandidate);
}

/**
 * Helper method searches all interfaces implemented by superclasses from the top to the bottom
 * for the most specific method declared in one of these interfaces.
 *
 * @param infoCache
 * @param name the specified method's name
 * @param parameterTypes the types of the arguments of the specified method
 * @return the most specific method selected from all interfaces from each superclass of the current class;
 *         otherwise, return the method of the first interface from the top superclass
 *         if the return types of all specified methods are identical.
 */
private Method getMostSpecificMethodFromAllInterfacesOfAllSuperclasses(HashMap<Class<?>, HashMap<MethodInfo, MethodInfo>> infoCache, 
	String name, Class<?>... parameterTypes) 
{
	Method candidateMethod = null;
	if (this != Object.class) {
		/* get to the top superclass first. if all return types end up being the same the interfaces from this superclass have priority. */
		Class superclz = getSuperclass();
		candidateMethod = superclz.getMostSpecificMethodFromAllInterfacesOfAllSuperclasses(infoCache, name, parameterTypes);
		
		/* search all interfaces of current class, comparing against result from previous superclass. */
		candidateMethod = getMostSpecificMethodFromAllInterfacesOfCurrentClass(infoCache, candidateMethod, name, parameterTypes);
	}
	return candidateMethod;
}

/**
 * Helper method searches all interfaces implemented by the current class or interface 
 * for the most specific method declared in one of these interfaces.
 *
 * @param infoCache
 * @param potentialCandidate potential candidate from superclass, null if currentClass is an interface
 * @param name the specified method's name
 * @param parameterTypes the types of the arguments of the specified method
 * @return the most specific method selected from all interfaces;
 *         otherwise if return types from all qualifying methods are identical, return an arbitrary method.
 */
private Method getMostSpecificMethodFromAllInterfacesOfCurrentClass(HashMap<Class<?>, HashMap<MethodInfo, MethodInfo>> infoCache,
	Method potentialCandidate, String name, Class<?>... parameterTypes) 
{
	Method bestMethod = potentialCandidate;
	/* if infoCache is passed in, reuse from superclass */
	if (null == infoCache) {
		infoCache = new HashMap<>(16);
	}
	HashMap<MethodInfo, MethodInfo> methodCandidates = getMethodSet(infoCache, false, true);

	for (MethodInfo mi : methodCandidates.values()) {
		if (null == mi.jlrMethods) {
			bestMethod = getMostSpecificInterfaceMethod(name, parameterTypes, bestMethod, mi.me);
		} else {
			for (Method m: mi.jlrMethods) {
				bestMethod = getMostSpecificInterfaceMethod(name, parameterTypes, bestMethod, m);
				/*[IF JAVA_SPEC_VERSION == 8]*/
				// Java 8 returns any matching method found
				if (bestMethod != null) {
					break;
				}
				/*[ENDIF] JAVA_SPEC_VERSION == 8 */
			}
		}
		/*[IF JAVA_SPEC_VERSION == 8]*/
		if (bestMethod != null) {
			break;
		}
		/*[ENDIF] JAVA_SPEC_VERSION == 8 */
	}

	return bestMethod;

}

private static Method getMostSpecificInterfaceMethod(String name, Class<?>[] parameterTypes, Method bestMethod, Method candidateMethod) {
	if (candidateMethod == bestMethod) {
		return bestMethod;
	}

	/* match name and parameters to user specification */
	if (!candidateMethod.getDeclaringClass().isInterface() 
		|| !candidateMethod.getName().equals(name) 
		|| !doParameterTypesMatch(candidateMethod.getParameterTypes(), parameterTypes)
	) {
		return bestMethod;
	}

	if (null == bestMethod) {
		bestMethod = candidateMethod;
		return bestMethod;
	}

	Class<?> bestRetType = bestMethod.getReturnType();
	Class<?> candidateRetType = candidateMethod.getReturnType();

	if (bestRetType == candidateRetType) {
		int bestModifiers = bestMethod.getModifiers();
		int candidateModifiers = candidateMethod.getModifiers();
		Class<?> bestDeclaringClass = bestMethod.getDeclaringClass();
		Class<?> candidateDeclaringClass = candidateMethod.getDeclaringClass();
		/* if all return types end up being the same, non-static methods take priority over static methods and sub-interfaces take
			priority over superinterface */
			if ((Modifier.isStatic(bestModifiers) && !Modifier.isStatic(candidateModifiers))
				|| methodAOverridesMethodB(candidateDeclaringClass, Modifier.isAbstract(candidateModifiers), candidateDeclaringClass.isInterface(), 
				bestDeclaringClass, Modifier.isAbstract(bestModifiers), bestDeclaringClass.isInterface())
		) {
			bestMethod = candidateMethod;
		}
	} else {
		/* resulting method should have the most specific return type */
		if (bestRetType.isAssignableFrom(candidateRetType)) {
			bestMethod = candidateMethod;
		}
	}

	return bestMethod;
}

private static boolean doParameterTypesMatch(Class<?>[] paramList1, Class<?>[] paramList2) {
	if (paramList1.length != paramList2.length) return false;

	for (int index = 0; index < paramList1.length; index++) {
		if (!paramList1[index].equals(paramList2[index])) {
			return false;
		}
	}

	return true;
}

/**
 * Answers a Method object which represents the first method found matching
 * the arguments.
 *
 * @param		name String
 *					the name of the method
 * @param		parameterTypes Class<?>[]
 *					the types of the arguments.
 * @param		partialSignature String
 *					the signature of the method, without return type.
 * @return		Object
 *					the first Method found matching the arguments
 *
 * @see			#getMethods
 */
private native Method getMethodImpl(String name, Class<?>[] parameterTypes, String partialSignature);

/**
 * Answers an array containing Method objects describing
 * all methods which are visible from the current execution
 * context.
 *
 * @return		Method[]
 *					all visible methods starting from the receiver.
 * @throws	SecurityException
 *					if member access is not allowed
 *
 * @see			#getDeclaredMethods
 */
@CallerSensitive
public Method[] getMethods() throws SecurityException {
	@SuppressWarnings("removal")
	SecurityManager security = System.getSecurityManager();
	if (security != null) {
		ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
		checkMemberAccess(security, callerClassLoader, Member.PUBLIC);
	}

	Method[] methods;

	/*[PR CMVC 114820, CMVC 115873, CMVC 116166] add reflection cache */
	methods = lookupCachedMethods(CacheKey.PublicMethodsKey);
	if (methods != null) {
		return methods;
	}

	if(isPrimitive()) {
		return new Method[0];
	}

	/*[PR CMVC 192714,194493] prepare the class before attempting to access members */
	J9VMInternals.prepare(this);
	HashMap<Class<?>, HashMap<MethodInfo, MethodInfo>> infoCache = new HashMap<>(16);
	HashMap<MethodInfo, MethodInfo> myMethods = getMethodSet(infoCache, false, false);
	ArrayList<Method> myMethodList = new ArrayList<>(16);
	for (MethodInfo mi: myMethods.values()) { /* don't know how big this will be at the start */
		if (null == mi.jlrMethods) {
			myMethodList.add(mi.me);
		} else {
			for (Method m: mi.jlrMethods) {
				myMethodList.add(m);
			}
		}
	}
	methods = myMethodList.toArray(new Method[myMethodList.size()]);
	return cacheMethods(Reflection.filterMethods(this, methods), CacheKey.PublicMethodsKey);	
}

private HashMap<MethodInfo, MethodInfo> getMethodSet(
		HashMap<Class<?>, HashMap<MethodInfo, MethodInfo>> infoCache, 
		boolean virtualOnly, boolean localInterfacesOnly) {
	/* virtualOnly must be false only for the bottom class of the hierarchy */
	HashMap<MethodInfo, MethodInfo> myMethods = infoCache.get(this);
	if (null == myMethods) { 
		/* haven't visited this class.  Initialize with the methods from the VTable which take priority */
		myMethods = new HashMap<>(16);
		if (!isInterface() && !localInterfacesOnly) {
			int vCount = 0;
			int sCount = 0;
			Method methods[] = null; /* this includes the superclass's virtual and static methods. */
			Set<MethodInfo> methodFilter = null;
			boolean noHotswap = true;
			do { 
				/* atomically get the list of methods, iterate if a hotswap occurred */
				vCount = getVirtualMethodCountImpl(); /* returns only public methods */
				sCount = getStaticMethodCountImpl();
				methods = (Method[])Method.class.allocateAndFillArray(vCount + sCount);
				if (null == methods) {
					throw new Error("Error retrieving class methods"); //$NON-NLS-1$
				}
				noHotswap = (getVirtualMethodsImpl(methods, 0, vCount) && getStaticMethodsImpl(methods, vCount, sCount));
			} while (!noHotswap);
			/* if we are here, this is the target class, so return static and virtual methods */
			boolean scanInterfaces = false;
			for (Method m: methods) {
				Class<?> mDeclaringClass = m.getDeclaringClass();
				MethodInfo mi = new MethodInfo(m);
				MethodInfo prevMI = myMethods.put(mi, mi);
				if (prevMI != null) {
					/* As per Java spec:
					 * For methods with same signature (name, parameter types) and return type,
					 * only the most specific method should be selected. 
					 * Method N is more specific than M if:
					 * N is declared by a class and M is declared by an interface; or
					 * N and M are both declared by either classes or interfaces and N's
					 * declaring type is the same as or a subtype of M's declaring type.
					 */
					Class<?> prevMIDeclaringClass = prevMI.me.getDeclaringClass();
					if ((mDeclaringClass.isInterface() && !prevMIDeclaringClass.isInterface())
						|| (mDeclaringClass.isAssignableFrom(prevMIDeclaringClass))
					) {
						myMethods.put(prevMI, prevMI);
					}
				}
				if (mDeclaringClass.isInterface()) {
					scanInterfaces = true;
					/* Add all the interfaces at once to preserve ordering */
					myMethods.remove(mi, mi);
				}
			}
			if (scanInterfaces) {
				/* methodFilter is guaranteed to be non-null at this point */
				addInterfaceMethods(infoCache, methodFilter, myMethods, localInterfacesOnly);
			}
		} else {
			if (!localInterfacesOnly || isInterface()) {
				/* this is an interface and doesn't have a vTable, but may have static or private methods */
				for (Method m: getDeclaredMethods()) { 
					int methodModifiers = m.getModifiers();
					if ((virtualOnly && Modifier.isStatic(methodModifiers)) || !Modifier.isPublic(methodModifiers)){
						continue;
					}
					MethodInfo mi = new MethodInfo(m);
					myMethods.put(mi, mi);
				}
			}
			addInterfaceMethods(infoCache, null, myMethods, localInterfacesOnly);
		}
		infoCache.put(this, myMethods); /* save results for future use */
	}
	return myMethods;
}

/**
 * Add methods defined in this class's interfaces or those of superclasses
 * @param infoCache Cache of previously visited method lists
 * @param methodFilter List of methods to include.  If null, include all
 * @param myMethods non-null if you want to update an existing list
 * @return list of methods with their various declarations
 */
private HashMap<MethodInfo, MethodInfo> addInterfaceMethods(
		HashMap<Class<?>, HashMap<MethodInfo, MethodInfo>> infoCache, 
		Set<MethodInfo> methodFilter, 
		HashMap<MethodInfo, MethodInfo> myMethods, boolean localInterfacesOnly) {
	boolean addToCache = false;
	boolean updateList = (null != myMethods);
	if (!updateList) {
		myMethods = infoCache.get(this);
	}
	if (null == myMethods) { 
		/* haven't visited this class */
		myMethods = new HashMap<>();	
		addToCache = true;
		updateList = true;
	}
	if (updateList) {
		Class mySuperclass = getSuperclass();
		if (!isInterface() && (Object.class != mySuperclass)) { 
			/* some interface methods are visible via the superclass */
			HashMap<MethodInfo, MethodInfo> superclassMethods = mySuperclass.addInterfaceMethods(infoCache, methodFilter, null, localInterfacesOnly);
			for (MethodInfo otherInfo: superclassMethods.values()) {
				if ((null == methodFilter) || methodFilter.contains(otherInfo)) {
					addMethod(myMethods, otherInfo);
				}
			}
		}
		for (Class intf: getInterfaces()) {
			HashMap<MethodInfo, MethodInfo> intfMethods = intf.getMethodSet(infoCache, true, localInterfacesOnly);
			for (MethodInfo otherInfo: intfMethods.values()) {
				if ((null == methodFilter) || methodFilter.contains(otherInfo)) {
					addMethod(myMethods, otherInfo);
				}
			}
		}
	}
	if (addToCache) {
		infoCache.put(this, myMethods); 
		/* save results for future use */
	}
	return myMethods;
}

/* this is called only to add methods from implemented interfaces of a class or superinterfaces of an interface */
private void addMethod(HashMap<MethodInfo,  MethodInfo>  myMethods, MethodInfo otherMi) {
	 MethodInfo oldMi = myMethods.get(otherMi);
	if (null == oldMi) { 
		/* haven't seen this method's name & sig */
		oldMi = new MethodInfo(otherMi); 
		/* create a new MethodInfo object and add mi's Method objects to it */
		myMethods.put(oldMi, oldMi);
	} else { 
		/* NB: the vTable has an abstract method for each method declared in the implemented interfaces */
		oldMi.update(otherMi); /* add the new method as appropriate */
	}
}

private native int getVirtualMethodCountImpl();
private native boolean getVirtualMethodsImpl(Method[] array, int start, int count);
private native int getStaticMethodCountImpl();
private native boolean getStaticMethodsImpl(Method[] array, int start, int count);
private native Object[] allocateAndFillArray(int size);

/**
 * Answers an integer which is the receiver's modifiers. 
 * Note that the constants which describe the bits which are
 * returned are implemented in class java.lang.reflect.Modifier
 * which may not be available on the target.
 *
 * @return		the receiver's modifiers
 */
public int getModifiers() {
	/*[PR CMVC 89071, 89373] Return SYNTHETIC, ANNOTATION, ENUM modifiers */
	int rawModifiers = getModifiersImpl();
	if (isArray()) {
		rawModifiers &= Modifier.PUBLIC | Modifier.PRIVATE | Modifier.PROTECTED |
				Modifier.ABSTRACT | Modifier.FINAL;	
	} else {
		rawModifiers &= Modifier.PUBLIC | Modifier.PRIVATE | Modifier.PROTECTED |
					Modifier.STATIC | Modifier.FINAL | Modifier.INTERFACE |
					Modifier.ABSTRACT | SYNTHETIC | ENUM | ANNOTATION;
	}
	return rawModifiers;
}

private native int getModifiersImpl();

/*[IF Sidecar19-SE]*/
/**
 * Answers the module to which the receiver belongs.
 * If this class doesn't belong to a named module, the unnamedModule of the classloader 
 * loaded this class is returned;
 * If this class represents an array type, the module for the element type is returned;
 * If this class represents a primitive type or void, module java.base is returned.
 * 
 * @return the module to which the receiver belongs 
 */
public Module getModule()
{
	return module;
}
/*[ENDIF] Sidecar19-SE */

/**
 * Answers the name of the class which the receiver represents.
 * For a description of the format which is used, see the class
 * definition of java.lang.Class.
 *
 * @return		the receiver's name.
 *
 * @see			java.lang.Class
 */
public String getName() {
	/*[PR CMVC 105714] Remove classNameMap (PR 115275) and always use getClassNameStringImpl() */
	String name = classNameString;
	if (name != null){
		return name;
	}
	//must have been null to set it
	name = VM.getClassNameImpl(this).intern();
	classNameString = name;
	return name;
}

/**
 * Answers the ProtectionDomain of the receiver. 
 * <p>
 * Note: In order to conserve space in embedded targets, we allow this
 * method to answer null for classes in the system protection domain 
 * (i.e. for system classes). System classes are always given full 
 * permissions (i.e. AllPermission). This is not changeable via the 
 * java.security.Policy.
 *
 * @return		ProtectionDomain
 *					the receiver's ProtectionDomain.
 *
 * @see			java.lang.Class
 */
public ProtectionDomain getProtectionDomain() {
	@SuppressWarnings("removal")
	SecurityManager security = System.getSecurityManager();
	if (security != null) {
		security.checkPermission(sun.security.util.SecurityConstants.GET_PD_PERMISSION);
	}
	return getProtectionDomainInternal();
}

ProtectionDomain getProtectionDomainInternal() {
	ProtectionDomain result = getPDImpl();
	if (result != null) {
		return result;
	}
	if (AllPermissionsPD == null) {
		allocateAllPermissionsPD();
	}
	return AllPermissionsPD;
}

private void allocateAllPermissionsPD() {
	Permissions collection = new Permissions();
	collection.add(sun.security.util.SecurityConstants.ALL_PERMISSION);
	AllPermissionsPD = new ProtectionDomain(null, collection);
}

/**
 * Answers the ProtectionDomain of the receiver.
 * <p>
 * This method is for internal use only.
 *
 * @return		ProtectionDomain
 *					the receiver's ProtectionDomain.
 *
 * @see			java.lang.Class
 */
ProtectionDomain getPDImpl() {
	/*[PR CMVC 125822] Move RAM class fields onto the heap to fix hotswap crash */
	return protectionDomain;
}

/**
 * Helper method to get the name of the package of incoming non-array class.
 * returns an empty string if the class is in an unnamed package.
 *
 * @param		clz a non-array class.
 * @return		the package name of incoming non-array class.
 */
private static String getNonArrayClassPackageName(Class<?> clz) {
	String name = clz.getName();
	int index = name.lastIndexOf('.');
	if (index >= 0) {
		return name.substring(0, index).intern();
	}
	return ""; //$NON-NLS-1$
}

/**
 * Answers the name of the package to which the receiver belongs.
 * For example, Object.class.getPackageName() returns "java.lang".
 * Returns "java.lang" if this class represents a primitive type or void,
 * and the element type's package name in the case of an array type.
 *
 * @return String the receiver's package name
 *
 * @see			#getPackage
 */
/*[IF Sidecar19-SE]*/
public 
/*[ENDIF] Sidecar19-SE */
String getPackageName() {
/*[IF Sidecar19-SE]*/
	if (isPrimitive()) {
		return "java.lang"; //$NON-NLS-1$
	}
	if (isArray()) {
		Class<?> componentType = getComponentType();
		while (componentType.isArray()) {
			componentType = componentType.getComponentType();
		}
		if (componentType.isPrimitive()) {
			return "java.lang"; //$NON-NLS-1$
		} else {
			return getNonArrayClassPackageName(componentType);
		}
	}
/*[ENDIF] Sidecar19-SE */
	return getNonArrayClassPackageName(this);
}

/**
 * Answers a URL referring to the
 * resource specified by resName. The mapping between
 * the resource name and the URL is managed by the
 * class's class loader.
 *
 * @param		resName 	the name of the resource.
 * @return		a stream on the resource.
 *
 * @see			java.lang.ClassLoader
 */
/*[IF Sidecar19-SE]*/
@CallerSensitive
/*[ENDIF] Sidecar19-SE */
public URL getResource(String resName) {
	ClassLoader loader = this.getClassLoaderImpl();
	String absoluteResName = this.toResourceName(resName);
	URL result = null;
	/*[IF Sidecar19-SE]*/
	Module thisModule = getModule();
	if (useModularSearch(absoluteResName, thisModule, System.getCallerClass())) {
		try {
			result = loader.findResource(thisModule.getName(), absoluteResName);
		} catch (IOException e) {
			return null;
		}
	}
	if (null == result)
		/*[ENDIF] Sidecar19-SE */
	{
		if (loader == ClassLoader.bootstrapClassLoader) {
			result =ClassLoader.getSystemResource(absoluteResName);
		} else {
			result =loader.getResource(absoluteResName);
		}
	}
	return result;
}

/**
 * Answers a read-only stream on the contents of the
 * resource specified by resName. The mapping between
 * the resource name and the stream is managed by the
 * class's class loader.
 *
 * @param		resName		the name of the resource.
 * @return		a stream on the resource.
 *
 * @see			java.lang.ClassLoader
 */
/*[IF Sidecar19-SE]*/
@CallerSensitive
/*[ENDIF] Sidecar19-SE */
public InputStream getResourceAsStream(String resName) {
	ClassLoader loader = this.getClassLoaderImpl();
	String absoluteResName = this.toResourceName(resName);
	InputStream result = null;
/*[IF Sidecar19-SE]*/
	Module thisModule = getModule();
	
	if (useModularSearch(absoluteResName, thisModule, System.getCallerClass())) {
		try {
			result = thisModule.getResourceAsStream(absoluteResName);
		} catch (IOException e) {
			return null;
		}
	}
	if (null == result)
/*[ENDIF] Sidecar19-SE */
	{
		if (loader == ClassLoader.bootstrapClassLoader) {
			result = ClassLoader.getSystemResourceAsStream(absoluteResName);
		} else {
			result = loader.getResourceAsStream(absoluteResName);
		}
	}
	return result;
}

/*[IF Sidecar19-SE]*/
/**
 * Indicate if the package should be looked up in a module or via the class path.
 * Look up the resource in the module if the module is named 
 * and is the same module as the caller or the package is open to the caller.
 * The default package (i.e. resources at the root of the module) is considered open.
 * 
 * @param absoluteResName name of resource, including package
 * @param thisModule module of the current class
 * @param callerClass class of method calling getResource() or getResourceAsStream()
 * @return true if modular lookup should be used.
 */
private boolean useModularSearch(String absoluteResName, Module thisModule, Class<?> callerClass) {
	boolean visible = false;

	if (thisModule.isNamed()) {
		// When the caller class is null, assuming it is loaded by module java.base.
		// See https://github.com/eclipse-openj9/openj9/issues/8993 for more info.
		final Module callerModule = callerClass == null ? Class.class.getModule() : callerClass.getModule();
		visible = (thisModule == callerModule);
		if (!visible) {
			visible = absoluteResName.endsWith(".class"); //$NON-NLS-1$
			if (!visible) {
				// extract the package name
				int lastSlash = absoluteResName.lastIndexOf('/');
				if (-1 == lastSlash) { // no package name
					visible = true;
				} else {
					String result = absoluteResName.substring(0, lastSlash).replace('/', '.');
					visible = thisModule.isOpen(result, callerModule);
				}
			}
		}
	}
	return visible;
}
/*[ENDIF] Sidecar19-SE */


/**
 * Answers a String object which represents the class's
 * signature, as described in the class definition of
 * java.lang.Class. 
 *
 * @return		the signature of the class.
 *
 * @see			java.lang.Class
 */
private String getSignature() {
	if(isArray()) return getName(); // Array classes are named with their signature
	if(isPrimitive()) {
		// Special cases for each base type.
		if(this == void.class) return "V"; //$NON-NLS-1$
		if(this == boolean.class) return "Z"; //$NON-NLS-1$
		if(this == byte.class) return "B"; //$NON-NLS-1$
		if(this == char.class) return "C"; //$NON-NLS-1$
		if(this == short.class) return "S"; //$NON-NLS-1$
		if(this == int.class) return "I"; //$NON-NLS-1$
		if(this == long.class) return "J"; //$NON-NLS-1$
		if(this == float.class) return "F"; //$NON-NLS-1$
		if(this == double.class) return "D"; //$NON-NLS-1$
	}

	// General case.
	// Create a buffer of the correct size
	String name = getName();
	return new StringBuilder(name.length() + 2).
		append('L').append(name).append(';').toString();
}

/**
 * Answers the signers for the class represented by the
 * receiver, or null if there are no signers.
 *
 * @return		the signers of the receiver.
 * 
 * @see			#getMethods
 */
public Object[] getSigners() {
	/*[PR CMVC 93861] allow setSigners() for bootstrap classes */
	 return getClassLoaderImpl().getSigners(this);
}
 
/**
 * Answers the Class which represents the receiver's
 * superclass. For Classes which represent base types,
 * interfaces, and for java.lang.Object the method
 * answers null.
 *
 * @return		the receiver's superclass.
 */
public Class<? super T> getSuperclass()
{
	return J9VMInternals.getSuperclass(this);
}

/**
 * Answers true if the receiver represents an array class.
 *
 * @return		<code>true</code>
 *					if the receiver represents an array class
 *              <code>false</code>
 *                  if it does not represent an array class
 */
public native boolean isArray();

/**
 * Answers true if the type represented by the argument
 * can be converted via an identity conversion or a widening
 * reference conversion (i.e. if either the receiver or the 
 * argument represent primitive types, only the identity 
 * conversion applies).
 *
 * @return		<code>true</code>
 *					the argument can be assigned into the receiver
 *              <code>false</code> 
 *					the argument cannot be assigned into the receiver
 * @param		cls	Class
 *					the class to test
 * @throws	NullPointerException
 *					if the parameter is null
 *					
 */
public native boolean isAssignableFrom(Class<?> cls);

/**
 * Answers true if the argument is non-null and can be
 * cast to the type of the receiver. This is the runtime
 * version of the <code>instanceof</code> operator.
 *
 * @return		<code>true</code>
 *					the argument can be cast to the type of the receiver
 *              <code>false</code> 
 *					the argument is null or cannot be cast to the 
 *					type of the receiver 
 *
 * @param		object Object
 *					the object to test
 */
public native boolean isInstance(Object object);

/**
 * Answers true if the receiver represents an interface.
 *
 * @return		<code>true</code>
 *					if the receiver represents an interface
 *              <code>false</code>
 *                  if it does not represent an interface
 */
public boolean isInterface() {
	// This code has been inlined in toGenericString. toGenericString 
	// must be modified to reflect any changes to this implementation.
	return !isArray() && (getModifiersImpl() & 512 /* AccInterface */) != 0;
}

/**
 * Answers true if the receiver represents a base type.
 *
 * @return		<code>true</code>
 *					if the receiver represents a base type
 *              <code>false</code>
 *                  if it does not represent a base type
 */
public native boolean isPrimitive();

/*[IF INLINE-TYPES]*/
/**
 * Answers true if the receiver represents a primitive class type. Array classes
 * return false.
 *
 * @return	true if reciever is primitive class type, and false otherwise.
 */
public native boolean isPrimitiveClass();
/*[ENDIF] INLINE-TYPES */

/**
 * Answers a new instance of the class represented by the
 * receiver, created by invoking the default (i.e. zero-argument)
 * constructor. If there is no such constructor, or if the
 * creation fails (either because of a lack of available memory or
 * because an exception is thrown by the constructor), an
 * InstantiationException is thrown. If the default constructor
 * exists, but is not accessible from the context where this
 * message is sent, an IllegalAccessException is thrown.
 *
 * @return		a new instance of the class represented by the receiver.
 * @throws		IllegalAccessException if the constructor is not visible to the sender.
 * @throws		InstantiationException if the instance could not be created.
 */
@CallerSensitive
/*[IF Sidecar19-SE]*/
@Deprecated(forRemoval=false, since="9")
/*[ENDIF]*/
public T newInstance() throws IllegalAccessException, InstantiationException {
	@SuppressWarnings("removal")
	SecurityManager security = System.getSecurityManager();
	if (security != null) {
		ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
		checkNonSunProxyMemberAccess(security, callerClassLoader, Member.PUBLIC);
	}

/*[IF JAVA_SPEC_VERSION >= 12]*/
	Class<?> callerClazz = getStackClass(1);
	if (callerClazz.classLoader == ClassLoader.bootstrapClassLoader) {
		/* newInstanceImpl() is required for all bootstrap classes to avoid an infinite loop
		 * when calling copyConstructor.invoke() in cacheConstructor() at the bootstrap stage
		 * as the constructors of bootstrap classes are not yet cached for use at that time.
		 */
		return (T)J9VMInternals.newInstanceImpl(this);
	} else {
		try {
			Constructor<?> ctr = getDeclaredConstructor();
			if (reflectionFactory == null) {
				reflectionFactory = AccessController.doPrivileged(new ReflectionFactory.GetReflectionFactoryAction());
			}
			return (T)reflectionFactory.newInstance(ctr, null, callerClazz);
		} catch (NoSuchMethodException  e) {
			InstantiationException instantiationEx = new InstantiationException();
			throw (InstantiationException)instantiationEx.initCause(e);
		} catch (InvocationTargetException e) {
			getUnsafe().throwException(e.getCause());
			return null;  //unreachable but required in compilation
		}
	}
/*[ELSE] JAVA_SPEC_VERSION >= 12*/
	return (T)J9VMInternals.newInstanceImpl(this);
/*[ENDIF] JAVA_SPEC_VERSION >= 12*/
}

/**
 * Used as a prototype for the jit.
 * 
 * @param 		callerClass
 * @return		the object
 * @throws 		InstantiationException
 */
/*[PR CMVC 114139]InstantiationException has wrong detail message */
private Object newInstancePrototype(Class<?> callerClass) throws InstantiationException {
	/*[PR 96623]*/
	throw new InstantiationException(this);
}


/**
 * Answers a string describing a path to the receiver's appropriate
 * package specific subdirectory, with the argument appended if the
 * argument did not begin with a slash. If it did, answer just the
 * argument with the leading slash removed.
 *
 * @return		String
 *					the path to the resource.
 * @param		resName	String
 *					the name of the resource.
 *
 * @see			#getResource
 * @see			#getResourceAsStream
 */
private String toResourceName(String resName) {
	// Turn package name into a directory path
	if (resName.length() > 0 && resName.charAt(0) == '/')
		return resName.substring(1);
	
	Class<?> thisObject = this;
	while (thisObject.isArray()) {
		thisObject = thisObject.getComponentType();
	}

	String qualifiedClassName = thisObject.getName();
	int classIndex = qualifiedClassName.lastIndexOf('.');
	if (classIndex == -1) return resName; // from a default package
	return qualifiedClassName.substring(0, classIndex + 1).replace('.', '/') + resName;
}

/**
 * Answers a string containing a concise, human-readable
 * description of the receiver.
 *
 * @return		a printable representation for the receiver.
 */
@Override
public String toString() {
	// Note change from 1.1.7 to 1.2: For primitive types,
	// return just the type name.
	if (isPrimitive()) return getName();
	return (isInterface() ? "interface " : "class ") + getName(); //$NON-NLS-1$ //$NON-NLS-2$
}

/**
 * Returns a formatted string describing this Class. The string has
 * the following format:
 * <i>modifier1 modifier2 ... kind name&lt;typeparam1, typeparam2, ...&gt;</i>.
 * kind is one of <code>class</code>, <code>enum</code>, <code>interface</code>,
 * <code>&#64;interface</code>, or
 * the empty string for primitive types. The type parameter list is
 * omitted if there are no type parameters.
/*[IF Sidecar19-SE]
 * For array classes, the string has the following format instead:
 * <i>name&lt;typeparam1, typeparam2, ...&gt;</i> followed by a number of
 * <code>[]</code> pairs, one pair for each dimension of the array.
/*[ENDIF]
 *
 * @return a formatted string describing this class
 * @since 1.8
 */
public String toGenericString() {
	if (isPrimitive()) return getName();
	
	StringBuilder result = new StringBuilder(); 
	int modifiers = getModifiers();
	
	// Checks for isInterface, isAnnotation and isEnum have been inlined
	// in order to avoid multiple calls to isArray and getModifiers
	boolean isArray = isArray();
	boolean isInterface = !isArray && (0 != (modifiers & Modifier.INTERFACE));
	
	// Get kind of type before modifying the modifiers
	String kindOfType;
	if ((!isArray) && ((modifiers & ANNOTATION) != 0)) {
		kindOfType = "@interface "; //$NON-NLS-1$
	} else if (isInterface) {
		kindOfType = "interface "; //$NON-NLS-1$
	} else if ((!isArray) && ((modifiers & ENUM) != 0) && (getSuperclass() == Enum.class)) {
		kindOfType = "enum "; //$NON-NLS-1$
/*[IF JAVA_SPEC_VERSION >= 14]*/
	} else if (isRecord()) {
		kindOfType = "record "; //$NON-NLS-1$
/*[ENDIF] JAVA_SPEC_VERSION >= 14 */
	} else {
		kindOfType = "class "; //$NON-NLS-1$	
	}
	
	// Remove "interface" from modifiers (is included as kind of type)
	if (isInterface) {
		modifiers -= Modifier.INTERFACE;
	}
	
	// Build generic string
/*[IF Sidecar19-SE]*/
	if (isArray) {
		int depth = 0;
		Class inner = this;
		Class component = this;
		do {
			inner = inner.getComponentType();
			if (inner != null) {
				component = inner;
				depth += 1;
			}
		} while (inner != null);
		result.append(component.getName());
		component.appendTypeParameters(result);
		for (int i = 0; i < depth; i++) {
			result.append('[').append(']');
		}
		return result.toString();
	}
/*[ENDIF]*/
	result.append(Modifier.toString(modifiers));
	if (result.length() > 0) {
		result.append(' ');
	}
	result.append(kindOfType);
	result.append(getName());
	
	appendTypeParameters(result);
	return result.toString();
}

// Add type parameters to stringbuilder if present
private void appendTypeParameters(StringBuilder nameBuilder) {
	TypeVariable<?>[] typeVariables = getTypeParameters();
	if (0 != typeVariables.length) {
		nameBuilder.append('<');
		boolean comma = false;
		for (TypeVariable<?> t : typeVariables) {
			if (comma) nameBuilder.append(',');
			nameBuilder.append(t);
			comma = true;
/*[IF JAVA_SPEC_VERSION >= 12]*/
			Type[] types = t.getBounds();
			if (types.length == 1 && types[0].equals(Object.class)) {
				// skip in case the only bound is java.lang.Object
			} else {
				String prefix = " extends "; //$NON-NLS-1$
				for (Type type : types) {
					nameBuilder.append(prefix).append(type.getTypeName());
					prefix = " & "; //$NON-NLS-1$
				}
			}
/*[ENDIF] JAVA_SPEC_VERSION >= 12 */
		}
		nameBuilder.append('>');
	}
}

/**
 * Returns the Package of which this class is a member.
 * A class has a Package iff it was loaded from a SecureClassLoader.
 *
 * @return Package the Package of which this class is a member
 * or null in the case of primitive or array types
 */
public Package getPackage() {
	if (isArray() || isPrimitive()) {
		return null;
	}
	String packageName = getPackageName();
	if (null == packageName) {
		return null;
	} else {
/*[IF Sidecar19-SE]*/
		if (this.classLoader == ClassLoader.bootstrapClassLoader) {
			return jdk.internal.loader.BootLoader.getDefinedPackage(packageName);
		} else {
			return getClassLoaderImpl().getDefinedPackage(packageName);
		}
/*[ELSE]
		return getClassLoaderImpl().getPackage(packageName);
/*[ENDIF]*/
	}
}

static Class<?> getPrimitiveClass(String name) {
	if (name.equals("float")) //$NON-NLS-1$
		return new float[0].getClass().getComponentType();
	if (name.equals("double")) //$NON-NLS-1$
		return new double[0].getClass().getComponentType();
	if (name.equals("int")) //$NON-NLS-1$
		return new int[0].getClass().getComponentType();
	if (name.equals("long")) //$NON-NLS-1$
		return new long[0].getClass().getComponentType();
	if (name.equals("char")) //$NON-NLS-1$
		return new char[0].getClass().getComponentType();
	if (name.equals("byte")) //$NON-NLS-1$
		return new byte[0].getClass().getComponentType();
	if (name.equals("boolean")) //$NON-NLS-1$
		return new boolean[0].getClass().getComponentType();
	if (name.equals("short")) //$NON-NLS-1$
		return new short[0].getClass().getComponentType();
	if (name.equals("void")) { //$NON-NLS-1$
		try {
			java.lang.reflect.Method method = Runnable.class.getMethod("run", EmptyParameters); //$NON-NLS-1$
			return method.getReturnType();
		} catch (Exception e) {
			com.ibm.oti.vm.VM.dumpString("Cannot initialize Void.TYPE\n"); //$NON-NLS-1$
		}
	}
	throw new Error("Unknown primitive type: " + name); //$NON-NLS-1$
}

/**
 * Returns the assertion status for this class.
 * Assertion is enabled/disabled based on 
 * classloader default, package or class default at runtime
 * 
 * @since 1.4
 *
 * @return		the assertion status for this class
 */
public boolean desiredAssertionStatus() {
	ClassLoader cldr = getClassLoaderImpl();
	if (cldr != null) {
		/*[PR CMVC 80253] package assertion status not checked */
		return cldr.getClassAssertionStatus(getName());
	}
	return false;
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
 * @param 		depth
 * @return		the class at the given depth
 */
@CallerSensitive
static final native Class<?> getStackClass(int depth);

/**
 * Walk the stack and answer an array containing the maxDepth
 * most recent classes on the stack of the calling thread.
 * 
 * Starting with the caller of the caller of getStackClasses(), return an
 * array of not more than maxDepth Classes representing the classes of
 * running methods on the stack (including native methods).  Frames
 * representing the VM implementation of java.lang.reflect are not included
 * in the list.  If stopAtPrivileged is true, the walk will terminate at any
 * frame running one of the following methods:
 * 
 * <code><ul>
 * <li>java/security/AccessController.doPrivileged(Ljava/security/PrivilegedAction;)Ljava/lang/Object;</li>
 * <li>java/security/AccessController.doPrivileged(Ljava/security/PrivilegedExceptionAction;)Ljava/lang/Object;</li>
 * <li>java/security/AccessController.doPrivileged(Ljava/security/PrivilegedAction;Ljava/security/AccessControlContext;)Ljava/lang/Object;</li>
 * <li>java/security/AccessController.doPrivileged(Ljava/security/PrivilegedExceptionAction;Ljava/security/AccessControlContext;)Ljava/lang/Object;</li>
 * </ul></code>
 * 
 * If one of the doPrivileged methods is found, the walk terminate and that frame is NOT included in the returned array.
 *
 * Notes: <ul>
 * 	 <li> This method operates on the defining classes of methods on stack.
 *		NOT the classes of receivers. </li>
 *
 *	 <li> The item at index zero in the result array describes the caller of 
 *		the caller of this method. </li>
 *</ul>
 *
 * @param 		maxDepth			maximum depth to walk the stack, -1 for the entire stack
 * @param 		stopAtPrivileged	stop at privileged classes
 * @return		the array of the most recent classes on the stack
 */
@CallerSensitive
static final native Class<?>[] getStackClasses(int maxDepth, boolean stopAtPrivileged);


/**
 * Called from JVM_ClassDepth.
 * Answers the index in the stack of the first method which
 * is contained in a class called <code>name</code>. If no
 * methods from this class are in the stack, return -1.
 *
 * @param		name String
 *					the name of the class to look for.
 * @return		int
 *					the depth in the stack of a the first
 *					method found.
 */
@CallerSensitive
static int classDepth (String name) {
	Class<?>[] classes = getStackClasses(-1, false);
	for (int i=1; i<classes.length; i++)
		if (classes[i].getName().equals(name))
			return i - 1;
	return -1;
}

/**
 * Called from JVM_ClassLoaderDepth.
 * Answers the index in the stack of the first class
 * whose class loader is not a system class loader.
 *
 * @return		the frame index of the first method whose class was loaded by a non-system class loader.
 */
@CallerSensitive
static int classLoaderDepth() {
	// Now, check if there are any non-system class loaders in
	// the stack up to the first privileged method (or the end
	// of the stack.
	Class<?>[] classes = getStackClasses(-1, true);
	for (int i=1; i<classes.length; i++) {
		ClassLoader cl = classes[i].getClassLoaderImpl();
		if (!cl.isASystemClassLoader()) return i - 1;
	}
	return -1;
}

/**
 * Called from JVM_CurrentClassLoader.
 * Answers the class loader of the first class in the stack
 * whose class loader is not a system class loader.
 *
 * @return		the most recent non-system class loader.
 */
@CallerSensitive
static ClassLoader currentClassLoader() {
	// Now, check if there are any non-system class loaders in
	// the stack up to the first privileged method (or the end
	// of the stack.
	Class<?>[] classes = getStackClasses(-1, true);
	for (int i=1; i<classes.length; i++) {
		ClassLoader cl = classes[i].getClassLoaderImpl();
		if (!cl.isASystemClassLoader()) return cl;
	}
	return null;
}

/**
 * Called from JVM_CurrentLoadedClass.
 * Answers the first class in the stack which was loaded
 * by a class loader which is not a system class loader.
 *
 * @return		the most recent class loaded by a non-system class loader.
 */
@CallerSensitive
static Class<?> currentLoadedClass() {
	// Now, check if there are any non-system class loaders in
	// the stack up to the first privileged method (or the end
	// of the stack.
	Class<?>[] classes = getStackClasses(-1, true);
	for (int i=1; i<classes.length; i++) {
		ClassLoader cl = classes[i].getClassLoaderImpl();
		if (!cl.isASystemClassLoader()) return classes[i];
	}
	return null;
}

/**
 * Return the specified Annotation for this Class. Inherited Annotations
 * are searched.
 * 
 * @param annotation the Annotation type
 * @return the specified Annotation or null
 * 
 * @since 1.5
 */
public <A extends Annotation> A getAnnotation(Class<A> annotation) {
	if (annotation == null) throw new NullPointerException();
	LinkedHashMap<Class<? extends Annotation>, Annotation> map = getAnnotationCache().annotationMap;
	if (map != null) {
		return (A)map.get(annotation);
	}
	return null;
}

/**
 * Return the directly declared Annotations for this Class, including the Annotations
 * inherited from superclasses. 
 * If an annotation type has been included before, then next occurrences will not be included. 
 * 
 * Repeated annotations are not included since they will be stored in their container annotation. 
 * But container annotations are included. (If a container annotation is repeatable and it is repeated, 
 * then these container annotations' container annotation is included. ) 
 * @return an array of Annotation 
 * 
 * @since 1.5
 */
public Annotation[] getAnnotations() {
	LinkedHashMap<Class<? extends Annotation>, Annotation> map = getAnnotationCache().annotationMap;
	if (map != null) {
		Collection<Annotation> annotations = map.values();
		return annotations.toArray(new Annotation[annotations.size()]);
	}
	return EMPTY_ANNOTATION_ARRAY;
}

/**
 * Looks through directly declared annotations for this class, not including Annotations inherited from superclasses. 
 * 
 * @param annotation the Annotation to search for
 * @return directly declared annotation of specified annotation type.
 * 
 * @since 1.8
 */
public <A extends Annotation> A getDeclaredAnnotation(Class<A> annotation) {
	if (annotation == null) throw new NullPointerException();
	LinkedHashMap<Class<? extends Annotation>, Annotation> map = getAnnotationCache().directAnnotationMap;
	if (map != null) {
		return (A)map.get(annotation);
	}
	return null;
}

/**
 * Return the annotated types for the implemented interfaces.
 * @return array, possibly empty, of AnnotatedTypes
 */
public AnnotatedType[] getAnnotatedInterfaces() {
	return TypeAnnotationParser.buildAnnotatedInterfaces(this);
}

/**
 * Return the annotated superclass of this class.
 * @return null if this class is Object, an interface, a primitive type, or an array type.  Otherwise return (possibly empty) AnnotatedType. 
 */
public AnnotatedType getAnnotatedSuperclass() {
	if (this.equals(Object.class) || this.isInterface() || this.isPrimitive() || this.isArray()) {
		return null;
	}
	return TypeAnnotationParser.buildAnnotatedSupertype(this);
}

/**
 * Answers the type name of the class which the receiver represents.
 * 
 * @return the fully qualified type name, with brackets if an array class
 * 
 * @since 1.8
 */
@Override
public String getTypeName() {
	if (isArray()) {
		StringBuilder nameBuffer = new StringBuilder("[]"); //$NON-NLS-1$
		Class<?> componentType = getComponentType();
		while (componentType.isArray()) {
			nameBuffer.append("[]"); //$NON-NLS-1$
			componentType = componentType.getComponentType();
		}
		nameBuffer.insert(0, componentType.getName());
		return nameBuffer.toString();
	} else {
		return getName();
	}
}

/**
 * Returns the annotations only for this Class, not including Annotations inherited from superclasses.
 * It includes all the directly declared annotations. 
 * Repeated annotations are not included but their container annotation does. 
 * 
 * @return an array of declared annotations
 *
 * 
 * @since 1.5
 */
public Annotation[] getDeclaredAnnotations() {
	LinkedHashMap<Class<? extends Annotation>, Annotation> map = getAnnotationCache().directAnnotationMap;
	if (map != null) {
		Collection<Annotation> annotations = map.values();
		return annotations.toArray(new Annotation[annotations.size()]);
	}
	return EMPTY_ANNOTATION_ARRAY;
}

/**
 * Gets the specified type annotations of this class. 
 * <br>
 * Terms used for annotations :<br><br>
 * Repeatable Annotation :
 * 		<p>An annotation which can be used more than once for the same class declaration.
 * 		Repeatable annotations are annotated with Repeatable annotation which tells the
 * 		container annotation for this repeatable annotation.</p>
 * 		Example
 * 		<pre><code>
 * 		{@literal @}interface ContainerAnnotation {RepeatableAnn[] value();}
 * 		{@literal @}Repeatable(ContainerAnnotation.class)
 * 		</code></pre>
 * Container Annotation:
 * 		<p>Container annotation stores the repeated annotations in its array-valued element.
 * 		Using repeatable annotations more than once makes them stored in their container annotation.
 * 		In this case, container annotation is visible directly on class declaration, but not the repeated annotations.</p>
 * Repeated Annotation:
 * 		<p>A repeatable annotation which is used more than once for the same class.</p>
 * Directly Declared Annotation :
 * 		<p>All non repeatable annotations are directly declared annotations.
 * 		As for repeatable annotations, they can be directly declared annotation if and only if they are used once.
 * 		Repeated annotations are not directly declared in class declaration, but their container annotation does.</p>
 *
 * -------------------------------------------------------------------------------------------------------
 *
 * <p>If the specified type is not repeatable annotation, then returned array size will be 0 or 1.
 * If specified type is repeatable annotation, then all the annotations of that type will be returned. Array size might be 0, 1 or more.</p>
 *
 * It does not search through super classes. 
 * 
 * @param annotationClass the annotation type to search for
 * @return array of declared annotations in the specified annotation type
 * 
 * @since 1.8
 */
public <A extends Annotation> A[] getDeclaredAnnotationsByType(Class<A> annotationClass) {
	ArrayList<A> annotationsList = internalGetDeclaredAnnotationsByType(annotationClass);
	return annotationsList.toArray((A[])Array.newInstance(annotationClass, annotationsList.size()));
}

private <A extends Annotation> ArrayList<A> internalGetDeclaredAnnotationsByType(Class<A> annotationClass) {
	AnnotationCache currentAnnotationCache = getAnnotationCache();
	ArrayList<A> annotationsList = new ArrayList<>();
	
	LinkedHashMap<Class<? extends Annotation>, Annotation> map = currentAnnotationCache.directAnnotationMap;
	if (map != null) {
		Repeatable repeatable = annotationClass.getDeclaredAnnotation(Repeatable.class);
		if (repeatable == null) {
			A annotation = (A)map.get(annotationClass);
			if (annotation != null) {
				annotationsList.add(annotation);
			}
		} else {
			Class<? extends Annotation> containerType = repeatable.value();
			// if the annotation and its container are both present, the order must be maintained
			for (Map.Entry<Class<? extends Annotation>, Annotation> entry : map.entrySet()) {
				Class<? extends Annotation> annotationType = entry.getKey();
				if (annotationType == annotationClass) {
					annotationsList.add((A)entry.getValue());
				} else if (annotationType == containerType) {
					A[] containedAnnotations = (A[])getAnnotationsArrayFromValue(entry.getValue(), containerType, annotationClass);
					if (containedAnnotations != null) {
						annotationsList.addAll(Arrays.asList(containedAnnotations));
					}
				}
			}
		}
	}

	return annotationsList;
}

/**
 * Gets the specified type annotations of this class. 
 * If the specified type is not repeatable annotation, then returned array size will be 0 or 1.
 * If specified type is repeatable annotation, then all the annotations of that type will be returned. Array size might be 0, 1 or more.
 * 
 * It searches through superclasses until it finds the inherited specified annotationClass. 
 * 
 * @param annotationClass the annotation type to search for
 * @return array of declared annotations in the specified annotation type
 * 
 * @since 1.8
 */
public <A extends Annotation> A[] getAnnotationsByType(Class<A> annotationClass)
{
	ArrayList<A> annotationsList = internalGetDeclaredAnnotationsByType(annotationClass);
	
	if (annotationClass.isInheritedAnnotationType()) {
		Class<?> sc = this;
		while (0 == annotationsList.size()) {
			sc = sc.getSuperclass();
			if (null == sc) break;
			ArrayList<A> superAnnotations = sc.internalGetDeclaredAnnotationsByType(annotationClass);
			if (superAnnotations != null) {
				annotationsList.addAll(superAnnotations);
			}
		}
	}
	return annotationsList.toArray((A[])Array.newInstance(annotationClass, annotationsList.size()));
}

AnnotationVars getAnnotationVars() {
	AnnotationVars tempAnnotationVars = annotationVars;
	if (tempAnnotationVars == null) {
		if (annotationVarsOffset == -1) {
			try {
				Field annotationVarsField = Class.class.getDeclaredField("annotationVars"); //$NON-NLS-1$
				annotationVarsOffset = getUnsafe().objectFieldOffset(annotationVarsField);
			} catch (NoSuchFieldException e) {
				throw newInternalError(e);
			}
		}
		tempAnnotationVars = new AnnotationVars();
		synchronized (this) {
			if (annotationVars == null) {
				// Lazy initialization of a non-volatile field. Ensure the Object is initialized
				// and flushed to memory before assigning to the annotationVars field.
				/*[IF Sidecar19-SE]
				getUnsafe().putObjectRelease(this, annotationVarsOffset, tempAnnotationVars);
				/*[ELSE]*/
				getUnsafe().putOrderedObject(this, annotationVarsOffset, tempAnnotationVars);
				/*[ENDIF]*/
			} else {
				tempAnnotationVars = annotationVars;
			}
		}
	}
	return tempAnnotationVars;
}

private MethodHandle getValueMethod(final Class<? extends Annotation> containedType) {
	final AnnotationVars localAnnotationVars = getAnnotationVars();
	MethodHandle valueMethod = localAnnotationVars.valueMethod;
	if (valueMethod == null) {
		final MethodType methodType = MethodType.methodType(Array.newInstance(containedType, 0).getClass());
		valueMethod = AccessController.doPrivileged(new PrivilegedAction<MethodHandle>() {
		    @Override
		    public MethodHandle run() {
		    	try {
		    		MethodHandles.Lookup localImplLookup = implLookup;
		    		if (localImplLookup == null) {
		    			Field privilegedLookupField = MethodHandles.Lookup.class.getDeclaredField("IMPL_LOOKUP"); //$NON-NLS-1$
		    			privilegedLookupField.setAccessible(true);
		    			localImplLookup = (MethodHandles.Lookup)privilegedLookupField.get(MethodHandles.Lookup.class);
		    			Field implLookupField = Class.class.getDeclaredField("implLookup"); //$NON-NLS-1$
		    			long implLookupOffset = getUnsafe().staticFieldOffset(implLookupField);
			    		// Lazy initialization of a non-volatile field. Ensure the Object is initialized
			    		// and flushed to memory before assigning to the implLookup field.
						/*[IF Sidecar19-SE]
						getUnsafe().putObjectRelease(Class.class, implLookupOffset, localImplLookup);
						/*[ELSE]*/
		    			getUnsafe().putOrderedObject(Class.class, implLookupOffset, localImplLookup);
						/*[ENDIF]*/
		    		}
		    		MethodHandle handle = localImplLookup.findVirtual(Class.this, "value", methodType); //$NON-NLS-1$
		    		if (AnnotationVars.valueMethodOffset == -1) {
		    			Field valueMethodField = AnnotationVars.class.getDeclaredField("valueMethod"); //$NON-NLS-1$
		    			AnnotationVars.valueMethodOffset = getUnsafe().objectFieldOffset(valueMethodField);
		    		}
		    		// Lazy initialization of a non-volatile field. Ensure the Object is initialized
		    		// and flushed to memory before assigning to the valueMethod field.
					/*[IF Sidecar19-SE]
					getUnsafe().putObjectRelease(localAnnotationVars, AnnotationVars.valueMethodOffset, handle);
					/*[ELSE]*/
		    		getUnsafe().putOrderedObject(localAnnotationVars, AnnotationVars.valueMethodOffset, handle);
					/*[ENDIF]*/
		    		return handle;
		    	} catch (NoSuchMethodException e) {
		    		return null;
		    	} catch (IllegalAccessException | NoSuchFieldException e) {
		    		throw newInternalError(e);
				}
		    }
		});
	}
	return valueMethod;
}

private MetadataCache getMetadataCache() {
	if (metadataCache == null) {
		/*[IF JAVA_SPEC_VERSION >= 11]
		metadataCacheOffset = getUnsafe().objectFieldOffset(Class.class, "metadataCache"); //$NON-NLS-1$
		/*[ELSE] JAVA_SPEC_VERSION >= 11 */
		try {
			Field field = Class.class.getDeclaredFieldInternal("metadataCache", false); //$NON-NLS-1$
			metadataCacheOffset = getUnsafe().objectFieldOffset(field);
		} catch (NoSuchFieldException e) {
			throw newInternalError(e);
		}
		/*[ENDIF] JAVA_SPEC_VERSION >= 11 */
		writeFieldValue(metadataCacheOffset, new MetadataCache());
	}
	return metadataCache;
}

private String cacheSimpleName(String simpleName) {
	MetadataCache cache = getMetadataCache();

	if (cache.cachedSimpleName == null || cache.cachedSimpleName.get() == null) {
		MetadataCache.cachedSimpleNameOffset = getFieldOffset(
			MetadataCache.class, "cachedSimpleName", MetadataCache.cachedSimpleNameOffset); //$NON-NLS-1$

		writeFieldValue(cache, MetadataCache.cachedSimpleNameOffset, new SoftReference<>(simpleName));
	}

	return simpleName;
}

private String cacheCanonicalName(String canonicalName) {
	MetadataCache cache = getMetadataCache();

	if (cache.cachedCanonicalName == null || cache.cachedCanonicalName.get() == null) {
		MetadataCache.cachedCanonicalNameOffset = getFieldOffset(
				MetadataCache.class, "cachedCanonicalName", MetadataCache.cachedCanonicalNameOffset); //$NON-NLS-1$

		writeFieldValue(cache, MetadataCache.cachedCanonicalNameOffset, new SoftReference<>(canonicalName));
	}

	return canonicalName;
}

/**
 * This helper method atomically writes the given {@code fieldValue} to the
 * field specified by the {@code fieldOffset} of the {@code target} object
 */
private static void writeFieldValue(Object target, long fieldOffset, Object fieldValue) {
	/*[IF JAVA_SPEC_VERSION >= 11]
	getUnsafe().putObjectRelease(target, fieldOffset, fieldValue);
	/*[ELSE] JAVA_SPEC_VERSION >= 11 */
	getUnsafe().putOrderedObject(target, fieldOffset, fieldValue);
	/*[ENDIF] JAVA_SPEC_VERSION >= 11 */
}

private void writeFieldValue(long fieldOffset, Object fieldValue) {
	writeFieldValue(this, fieldOffset, fieldValue);
}

private static long getFieldOffset(Class<?> hostClass, String fieldName, long initialOffset) {
	if (initialOffset == -1) {
		try {
			Field field = hostClass.getDeclaredField(fieldName);
			return getUnsafe().objectFieldOffset(field);
		} catch (NoSuchFieldException e) {
			throw newInternalError(e);
		}
	}
	return initialOffset;
}

private static long getFieldOffset(String fieldName) {
	return getFieldOffset(Class.class, fieldName, -1);
}

/**
 * Gets the array of containedType from the value() method.
 * 
 * @param container the annotation which is the container of the repeated annotation
 * @param containerType the annotationType() of the container. This implements the value() method.
 * @param containedType the annotationType() stored in the container
 * @return Annotation array if the given annotation has a value() method which returns an array of the containedType. Otherwise, return null.
 */
private static Annotation[] getAnnotationsArrayFromValue(Annotation container, Class<? extends Annotation> containerType, Class<? extends Annotation> containedType) {
	try {
		MethodHandle valueMethod = containerType.getValueMethod(containedType);
		if (valueMethod != null) {
			Object children = valueMethod.invoke(container);
			/*  
			 * Check whether value is Annotation array or not
			 */
			if (children instanceof Annotation[]) {
				return (Annotation[])children;
			}
		}
		return null;
	} catch (Error | RuntimeException e) {
		throw e;
	} catch (Throwable t) {
		throw new RuntimeException(t);
	}	
}

private boolean isInheritedAnnotationType() {
	LinkedHashMap<Class<? extends Annotation>, Annotation> map = getAnnotationCache().directAnnotationMap;
	if (map != null) {
		return map.get(Inherited.class) != null;
	}
	return false;
}

private LinkedHashMap<Class<? extends Annotation>, Annotation> buildAnnotations(LinkedHashMap<Class<? extends Annotation>, Annotation> directAnnotationsMap) {
	Class<?> superClass = getSuperclass();
	if (superClass == null) {
		return directAnnotationsMap;
	}
	LinkedHashMap<Class<? extends Annotation>, Annotation> superAnnotations = superClass.getAnnotationCache().annotationMap;
	LinkedHashMap<Class<? extends Annotation>, Annotation> annotationsMap = null;
	if (superAnnotations != null) {
		for (Map.Entry<Class<? extends Annotation>, Annotation> entry : superAnnotations.entrySet()) {
			Class<? extends Annotation> annotationType = entry.getKey();
			// if the annotation is Inherited store the annotation 
			if (annotationType.isInheritedAnnotationType()) {
				if (annotationsMap == null) {
					annotationsMap = new LinkedHashMap<>((superAnnotations.size() + (directAnnotationsMap != null ? directAnnotationsMap.size() : 0)) * 4 / 3);
				}
				annotationsMap.put(annotationType, entry.getValue());
			}
		}
	}
	if (annotationsMap == null) {
		return directAnnotationsMap;
	}
	if (directAnnotationsMap != null) {
		annotationsMap.putAll(directAnnotationsMap);
	}
	return annotationsMap;
}

/**
 * Gets all the direct annotations. 
 * It does not include repeated annotations for this class, it includes their container annotation(s).
 * 
 * @return array of all the direct annotations.
 */
private AnnotationCache getAnnotationCache() {
	AnnotationCache annotationCacheResult = annotationCache;
	
	if (annotationCacheResult == null) {
		byte[] annotationsData = getDeclaredAnnotationsData();
		if (annotationsData == null) {
			annotationCacheResult = new AnnotationCache(null, buildAnnotations(null));
		} else {
			Annotation[] directAnnotations = sun.reflect.annotation.AnnotationParser.toArray(
						sun.reflect.annotation.AnnotationParser.parseAnnotations(
								annotationsData,
								getConstantPool(),
								this));
			
			LinkedHashMap<Class<? extends Annotation>, Annotation> directAnnotationsMap = new LinkedHashMap<>(directAnnotations.length * 4 / 3);
			for (Annotation annotation : directAnnotations) {
				Class<? extends Annotation> annotationType = annotation.annotationType();
				directAnnotationsMap.put(annotationType, annotation);
			}
			annotationCacheResult = new AnnotationCache(directAnnotationsMap, buildAnnotations(directAnnotationsMap));
		}
		
		// Don't bother with synchronization. Since it is just a cache, it doesn't matter if it gets overwritten
		// because multiple threads create the cache at the same time
		long localAnnotationCacheOffset = annotationCacheOffset;
		if (localAnnotationCacheOffset == -1) {
			try {
				Field annotationCacheField = Class.class.getDeclaredField("annotationCache"); //$NON-NLS-1$
				localAnnotationCacheOffset = getUnsafe().objectFieldOffset(annotationCacheField);
				annotationCacheOffset = localAnnotationCacheOffset;
			} catch (NoSuchFieldException e) {
				throw newInternalError(e);
			}
		}
		// Lazy initialization of a non-volatile field. Ensure the Object is initialized
		// and flushed to memory before assigning to the annotationCache field.
		/*[IF Sidecar19-SE]*/
		getUnsafe().putObjectRelease(this, localAnnotationCacheOffset, annotationCacheResult);
		/*[ELSE]*/
		getUnsafe().putOrderedObject(this, localAnnotationCacheOffset, annotationCacheResult);
		/*[ENDIF]*/
	}
	return annotationCacheResult;
}


private native byte[] getDeclaredAnnotationsData();

/**
 * Answer if this class is an Annotation.
 * 
 * @return true if this class is an Annotation 
 * 
 * @since 1.5
 */
public boolean isAnnotation() {
	// This code has been inlined in toGenericString. toGenericString 
	// must be modified to reflect any changes to this implementation.
	/*[PR CMVC 89373] Ensure Annotation subclass is not annotation */
	return !isArray() && (getModifiersImpl() & ANNOTATION) != 0;
}

/**
 * Answer if the specified Annotation exists for this Class. Inherited
 * Annotations are searched.
 * 
 * @param annotation the Annotation type
 * @return true if the specified Annotation exists
 * 
 * @since 1.5
 */
public boolean isAnnotationPresent(Class<? extends Annotation> annotation) {
	if (annotation == null) throw new NullPointerException();
	return getAnnotation(annotation) != null;
}

/**
 * Cast this Class to a subclass of the specified Class. 
 * @param <U> the type for casting to
 * @param cls the Class to cast to
 * @return this Class, cast to a subclass of the specified Class
 * 
 * @throws ClassCastException if this Class is not the same or a subclass
 *		of the specified Class
 * 
 * @since 1.5
 */
public <U> Class<? extends U> asSubclass(Class<U> cls) {
	if (!cls.isAssignableFrom(this))
		throw new ClassCastException(this.toString());
	return (Class<? extends U>)this;
}

/**
 * Cast the specified object to this Class. 
 * 
 * @param object the object to cast
 * 
 * @return the specified object, cast to this Class
 * 
 * @throws ClassCastException if the specified object cannot be cast
 *		to this Class
 * 
 * @since 1.5
 */
public T cast(Object object) {
	if (object != null && !this.isInstance(object))
/*[MSG "K0336", "Cannot cast {0} to {1}"]*/
		throw new ClassCastException(com.ibm.oti.util.Msg.getString("K0336", object.getClass(), this)); //$NON-NLS-1$
	return (T)object;
}

/**
 * Answer if this Class is an enum.
 * 
 * @return true if this Class is an enum
 * 
 * @since 1.5
 */
public boolean isEnum() {
	// This code has been inlined in toGenericString. toGenericString 
	// must be modified to reflect any changes to this implementation.
	/*[PR CMVC 89071] Ensure class with enum access flag (modifier) !isEnum() */
	return !isArray() && (getModifiersImpl() & ENUM) != 0 &&
		getSuperclass() == Enum.class;
}

private EnumVars<T> getEnumVars() {
	EnumVars<T> tempEnumVars = enumVars;
	if (tempEnumVars == null) {
		long localEnumVarsOffset = enumVarsOffset;
		if (localEnumVarsOffset == -1) {
			Field enumVarsField;
			try {
				enumVarsField = Class.class.getDeclaredField("enumVars"); //$NON-NLS-1$
				localEnumVarsOffset = getUnsafe().objectFieldOffset(enumVarsField);
				enumVarsOffset = localEnumVarsOffset;
			} catch (NoSuchFieldException e) {
				throw newInternalError(e);
			}
		}
		// Don't bother with synchronization to determine if the field is already assigned. Since it is just a cache,
		// it doesn't matter if it gets overwritten because multiple threads create the cache at the same time
		tempEnumVars = new EnumVars<>();
		// Lazy initialization of a non-volatile field. Ensure the Object is initialized
		// and flushed to memory before assigning to the enumVars field.
		/*[IF Sidecar19-SE]
		getUnsafe().putObjectRelease(this, localEnumVarsOffset, tempEnumVars);
		/*[ELSE]*/
		getUnsafe().putOrderedObject(this, localEnumVarsOffset, tempEnumVars);
		/*[ENDIF]*/
	}
	return tempEnumVars;
}

/**
 * 
 * @return Map keyed by enum name, of uncloned and cached enum constants in this class
 */
Map<String, T> enumConstantDirectory() {
	EnumVars<T> localEnumVars = getEnumVars();
	Map<String, T> map = localEnumVars.cachedEnumConstantDirectory;
	if (null == map) {
		/*[PR CMVC 189091] Perf: EnumSet.allOf() is slow */
		T[] enums = getEnumConstantsShared();
		if (enums == null) {
			/*[PR CMVC 189257] Class#valueOf throws NPE instead of IllegalArgEx for nonEnum Classes */
			/*
			 * Class#valueOf() is the caller of this method,
			 * according to the spec it throws IllegalArgumentException if the class is not an Enum.
			 */
			/*[MSG "K0564", "{0} is not an Enum"]*/			
			throw new IllegalArgumentException(com.ibm.oti.util.Msg.getString("K0564", getName())); //$NON-NLS-1$
		}
		map = new HashMap<>(enums.length * 4 / 3);
		for (int i = 0; i < enums.length; i++) {
			map.put(((Enum<?>) enums[i]).name(), enums[i]);
		}
		
		if (EnumVars.enumDirOffset == -1) {
			try {
				Field enumDirField = EnumVars.class.getDeclaredField("cachedEnumConstantDirectory"); //$NON-NLS-1$
				EnumVars.enumDirOffset = getUnsafe().objectFieldOffset(enumDirField);
			} catch (NoSuchFieldException e) {
				throw newInternalError(e);
			}
		}
		// Lazy initialization of a non-volatile field. Ensure the Object is initialized
		// and flushed to memory before assigning to the cachedEnumConstantDirectory field.
		/*[IF Sidecar19-SE]
		getUnsafe().putObjectRelease(localEnumVars, EnumVars.enumDirOffset, map);
		/*[ELSE]*/
		getUnsafe().putOrderedObject(localEnumVars, EnumVars.enumDirOffset, map);
		/*[ENDIF]*/
	}
	return map;
}

/**
 * Answer the shared uncloned array of enum constants for this Class. Returns null if
 * this class is not an enum.
 * 
 * @return the array of enum constants, or null
 * 
 * @since 1.5
 */
/*[PR CMVC 189091] Perf: EnumSet.allOf() is slow */
T[] getEnumConstantsShared() {
	/*[PR CMVC 188840] Perf: Class.getEnumConstants() is slow */
	EnumVars<T> localEnumVars = getEnumVars();
	T[] enums = localEnumVars.cachedEnumConstants;
	if (null == enums && isEnum()) {
		try {
			final PrivilegedExceptionAction<Method> privilegedAction = new PrivilegedExceptionAction<Method>() {
				@Override
				public Method run() throws Exception {
					Method method = getMethod("values"); //$NON-NLS-1$
					/*[PR CMVC 83171] caused ClassCastException: <enum class> not an enum]*/
					// the enum class may not be visible
					method.setAccessible(true);
					return method;
				}
			};
			
			Method values = AccessController.doPrivileged(privilegedAction);
			enums = (T[])values.invoke(this);
			
			long localEnumConstantsOffset = EnumVars.enumConstantsOffset;
			if (localEnumConstantsOffset == -1) {
				try {
					Field enumConstantsField = EnumVars.class.getDeclaredField("cachedEnumConstants"); //$NON-NLS-1$
					localEnumConstantsOffset = getUnsafe().objectFieldOffset(enumConstantsField);
					EnumVars.enumConstantsOffset = localEnumConstantsOffset;
				} catch (NoSuchFieldException e) {
					throw newInternalError(e);
				}
			}
			// Lazy initialization of a non-volatile field. Ensure the Object is initialized
			// and flushed to memory before assigning to the cachedEnumConstants field.
			/*[IF Sidecar19-SE]
			getUnsafe().putObjectRelease(localEnumVars, localEnumConstantsOffset, enums);
			/*[ELSE]*/
			getUnsafe().putOrderedObject(localEnumVars, localEnumConstantsOffset, enums);
			/*[ENDIF]*/
		} catch (IllegalAccessException | IllegalArgumentException | InvocationTargetException | PrivilegedActionException e) {
			enums = null;
		}
	}
	
	return enums;	 
}

/**
 * Answer the array of enum constants for this Class. Returns null if
 * this class is not an enum.
 * 
 * @return the array of enum constants, or null
 * 
 * @since 1.5
 */
public T[] getEnumConstants() {
	/*[PR CMVC 188840] Perf: Class.getEnumConstants() is slow */
	/*[PR CMVC 189091] Perf: EnumSet.allOf() is slow */
	/*[PR CMVC 192837] JAVA8:JCK: NPE at j.l.Class.getEnumConstants */
	T[] enumConstants = getEnumConstantsShared();
	if (null != enumConstants) {
		return enumConstants.clone();
	} else {
		return null;
	}
}

/**
 * Answer if this Class is synthetic. A synthetic Class is created by
 * the compiler.
 * 
 * @return true if this Class is synthetic.
 * 
 * @since 1.5
 */
public boolean isSynthetic() {
	return !isArray() && (getModifiersImpl() & SYNTHETIC) != 0;
}

private native String getGenericSignature();

private CoreReflectionFactory getFactory() {
	return CoreReflectionFactory.make(this, ClassScope.make(this));
}

private ClassRepositoryHolder getClassRepositoryHolder() {
	ClassRepositoryHolder localClassRepositoryHolder = classRepoHolder;
	if (localClassRepositoryHolder == null) {
		synchronized(this) {
			localClassRepositoryHolder = classRepoHolder;
			if (localClassRepositoryHolder == null) {
				String signature = getGenericSignature();
				if (signature == null) {
					localClassRepositoryHolder = ClassRepositoryHolder.NullSingleton;
				} else {
					ClassRepository classRepo = ClassRepository.make(signature, getFactory());
					localClassRepositoryHolder = new ClassRepositoryHolder(classRepo);
				}
				classRepoHolder = localClassRepositoryHolder;
			}
		}
	}
	return localClassRepositoryHolder;
}


/**
 * Answers an array of TypeVariable for the generic parameters declared
 * on this Class.
 *
 * @return		the TypeVariable[] for the generic parameters
 * 
 * @since 1.5
 */
@SuppressWarnings("unchecked")
public TypeVariable<Class<T>>[] getTypeParameters() {
	ClassRepositoryHolder holder = getClassRepositoryHolder();
	ClassRepository repository = holder.classRepository;
	if (repository == null) return new TypeVariable[0];
	return (TypeVariable<Class<T>>[])repository.getTypeParameters();
}

/**
 * Answers an array of Type for the Class objects which match the
 * interfaces specified in the receiver classes <code>implements</code>
 * declaration.
 *
 * @return		Type[]
 *					the interfaces the receiver claims to implement.
 *
 * @since 1.5
 */
public Type[] getGenericInterfaces() {
	ClassRepositoryHolder holder = getClassRepositoryHolder();
	ClassRepository repository = holder.classRepository;
	if (repository == null) return getInterfaces();
	return repository.getSuperInterfaces();
}

/**
 * Answers the Type for the Class which represents the receiver's
 * superclass. For classes which represent base types,
 * interfaces, and for java.lang.Object the method
 * answers null.
 *
 * @return		the Type for the receiver's superclass.
 * 
 * @since 1.5
 */
public Type getGenericSuperclass() {
	ClassRepositoryHolder holder = getClassRepositoryHolder();
	ClassRepository repository = holder.classRepository;
	if (repository == null) return getSuperclass();
	if (isInterface()) return null;
	return repository.getSuperclass();
}

private native Object getEnclosingObject();

/**
 * If this Class is defined inside a constructor, return the Constructor.
 * 
 * @return the enclosing Constructor or null
 * @throws SecurityException if declared member access or package access is not allowed
 * 
 * @since 1.5
 * 
 * @see #isAnonymousClass()
 * @see #isLocalClass() 
 */
@CallerSensitive
public Constructor<?> getEnclosingConstructor() throws SecurityException {
	Constructor<?> constructor = null;
	Object enclosing = getEnclosingObject();
	if (enclosing instanceof Constructor<?>) {
		constructor = (Constructor<?>) enclosing;
		@SuppressWarnings("removal")
		SecurityManager security = System.getSecurityManager();
		if (security != null) {
			ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
			constructor.getDeclaringClass().checkMemberAccess(security, callerClassLoader, Member.DECLARED);
		}
		/*[PR CMVC 201439] To remove CheckPackageAccess call from getEnclosingMethod of J9 */
	}
	return constructor;
}

/**
 * If this Class is defined inside a method, return the Method.
 * 
 * @return the enclosing Method or null
 * @throws SecurityException if declared member access or package access is not allowed
 * 
 * @since 1.5
 * 
 * @see #isAnonymousClass()
 * @see #isLocalClass() 
 */
@CallerSensitive
public Method getEnclosingMethod() throws SecurityException {
	Method method = null;
	Object enclosing = getEnclosingObject();
	if (enclosing instanceof Method) {
		method = (Method)enclosing;
		@SuppressWarnings("removal")
		SecurityManager security = System.getSecurityManager();
		if (security != null) {
			ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
			method.getDeclaringClass().checkMemberAccess(security, callerClassLoader, Member.DECLARED);
		}
		/*[PR CMVC 201439] To remove CheckPackageAccess call from getEnclosingMethod of J9 */
	}
	return method;
}

private native Class<?> getEnclosingObjectClass();

/**
 * Return the enclosing Class of this Class. Unlike getDeclaringClass(),
 * this method works on any nested Class, not just classes nested directly
 * in other classes.
 * 
 * @return the enclosing Class or null
 * @throws SecurityException if package access is not allowed
 * 
 * @since 1.5
 * 
 * @see #getDeclaringClass()
 * @see #isAnonymousClass()
 * @see #isLocalClass()
 * @see #isMemberClass()
 */
@CallerSensitive
public Class<?> getEnclosingClass() throws SecurityException {
	Class<?> enclosingClass = getDeclaringClass();
	if (enclosingClass == null) {
		if (cachedEnclosingClassOffset == -1) {
			cachedEnclosingClassOffset = getFieldOffset("cachedEnclosingClass"); //$NON-NLS-1$
		}
		if (cachedEnclosingClass == null) {
			Class<?> localEnclosingClass = getEnclosingObjectClass();
			if (localEnclosingClass == null){
				localEnclosingClass = ClassReflectNullPlaceHolder.class;
			}
			writeFieldValue(cachedEnclosingClassOffset, localEnclosingClass);
		}
		/**
		 * ClassReflectNullPlaceHolder.class means the value of cachedEnclosingClass is null
		 * @see ClassReflectNullPlaceHolder.class
		 */
		enclosingClass = cachedEnclosingClass == ClassReflectNullPlaceHolder.class ? null: cachedEnclosingClass;
	}
	if (enclosingClass != null) {
		@SuppressWarnings("removal")
		SecurityManager security = System.getSecurityManager();
		if (security != null) {
			ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
			enclosingClass.checkMemberAccess(security, callerClassLoader, MEMBER_INVALID_TYPE);
		}
	}
	
	return enclosingClass;
}

private native String getSimpleNameImpl();

/**
 * Return the simple name of this Class. The simple name does not include
 * the package or the name of the enclosing class. The simple name of an
 * anonymous class is "".
 * 
 * @return the simple name
 * 
 * @since 1.5
 * 
 * @see #isAnonymousClass()
 */
public String getSimpleName() {
	MetadataCache cache = getMetadataCache();
	if (cache.cachedSimpleName != null) {
		String cachedSimpleName = cache.cachedSimpleName.get();
		if (cachedSimpleName != null) {
			return cachedSimpleName;
		}
	}

	int arrayCount = 0;
	Class<?> baseType = this;
	if (isArray()) {
		arrayCount = 1;
		while ((baseType = baseType.getComponentType()).isArray()) {
			arrayCount++;
		}
	}
	String simpleName = baseType.getSimpleNameImpl();
	String fullName = baseType.getName();
	if (simpleName == null) {
		/** 
		 * It is a base class, an anonymous class, or a hidden class.
		 * Call getEnclosingClass() instead of getEnclosingObjectClass() to check getDeclaringClass() first. Hidden class test expects
		 * NoClassDefFoundError from getDeclaringClass(). 
		 */
		Class<?> parent = baseType.getEnclosingClass();
		if (parent != null) {
			simpleName = ""; //$NON-NLS-1$
		} else {
			// remove the package name
			int index = fullName.lastIndexOf('.');
			if (index != -1) {
				simpleName = fullName.substring(index+1);
			} else {
				// no periods in fully qualified name, thus simple name is also the full name
				simpleName = fullName;
			}
		}
	}
	/*[IF !Sidecar19-SE]*/
	/* In Java 8, the simple name needs to match the full name*/
	else if (!fullName.endsWith(simpleName)) {
		Class<?> parent = baseType.getEnclosingObjectClass();
		int index = fullName.lastIndexOf('.') + 1;
		if (parent == null) {
			parent = getDeclaringClassImpl();
		}
		if (parent != null) {
			/* Nested classes have names which consist of the parent class name followed by a '$', followed by
			 * the simple name. Some nested classes have additional characters between the parent class name
			 * and the simple name of the nested class.
			 */
			String parentName = parent.getName();
			if (fullName.startsWith(parentName) && (fullName.charAt(parentName.length()) == '$')) {
				index = fullName.lastIndexOf('$') + 1;
				// a local class simple name is preceded by a sequence of digits
				while (index < fullName.length() && !Character.isJavaIdentifierStart(fullName.charAt(index))) {
					index++;
				}
			}
		}
		if (index != -1) {
			simpleName = fullName.substring(index);
		}
	}
	/*[ENDIF] !Sidecar19-SE*/
	if (arrayCount > 0) {
		StringBuilder result = new StringBuilder(simpleName);
		for (int i=0; i<arrayCount; i++) {
			result.append("[]"); //$NON-NLS-1$
		}
		return result.toString();
	}
	return cacheSimpleName(simpleName);
}

/**
 * Return the canonical name of this Class. The canonical name is null
 * for a local or anonymous class. The canonical name includes the package
 * and the name of the enclosing class.
 * 
 * @return the canonical name or null
 * 
 * @since 1.5
 * 
 * @see #isAnonymousClass()
 * @see #isLocalClass()
 */
public String getCanonicalName() {
	MetadataCache cache = getMetadataCache();
	if (cache.cachedCanonicalName != null) {
		String cachedCanonicalName = cache.cachedCanonicalName.get();
		if (cachedCanonicalName != null) {
			return cachedCanonicalName;
		}
	}

	int arrayCount = 0;
	Class<?> baseType = this;
	if (isArray()) {
		arrayCount = 1;
		while ((baseType = baseType.getComponentType()).isArray()) {
			arrayCount++;
		}
	}
/*[IF JAVA_SPEC_VERSION >= 15]*/
	if (baseType.isHidden()) {
		/* Canonical name is always null for hidden classes. */
		return null;
	}
/*[ENDIF] JAVA_SPEC_VERSION >= 15 */
	if (baseType.getEnclosingObjectClass() != null) {
		// local or anonymous class
		return null;
	}
	String canonicalName;
	Class<?> declaringClass = baseType.getDeclaringClass();
	if (declaringClass == null) {
		canonicalName = baseType.getName();
	} else {
		/*[PR 119256] The canonical name of a member class of a local class should be null */
		String declaringClassCanonicalName = declaringClass.getCanonicalName();
		if (declaringClassCanonicalName == null) return null;
		// remove the enclosingClass from the name, including the $
		String simpleName = baseType.getName().substring(declaringClass.getName().length() + 1);
		canonicalName = declaringClassCanonicalName + '.' + simpleName;
	}
	
	if (arrayCount > 0) {
		StringBuilder result = new StringBuilder(canonicalName);
		for (int i=0; i<arrayCount; i++) {
			result.append("[]"); //$NON-NLS-1$
		}
		return result.toString();
	}
	return cacheCanonicalName(canonicalName);
}

/**
 * Answer if this Class is anonymous. An unnamed Class defined
 * inside a method.
 * 
 * @return true if this Class is anonymous.
 * 
 * @since 1.5
 * 
 * @see #isLocalClass()
 */
public boolean isAnonymousClass() {
	return getSimpleNameImpl() == null && getEnclosingObjectClass() != null;
}

/**
 * Answer if this Class is local. A named Class defined inside
 * a method.
 * 
 * @return true if this Class is local.
 * 
 * @since 1.5
 * 
 * @see #isAnonymousClass()
 */
public boolean isLocalClass() {
	return getEnclosingObjectClass() != null && getSimpleNameImpl() != null;
}

/**
 * Answer if this Class is a member Class. A Class defined inside another
 * Class.
 * 
 * @return true if this Class is local.
 * 
 * @since 1.5
 * 
 * @see #isLocalClass()
 */
public boolean isMemberClass() {
	return getEnclosingObjectClass() == null && getDeclaringClass() != null;
}

/**
 * Compute the signature for get*Method()
 * 
 * @param		throwException  if NoSuchMethodException is thrown 
 * @param		name			the name of the method
 * @param		parameterTypes	the types of the arguments
 * @return 		the signature string
 * @throws		NoSuchMethodException if one of the parameter types cannot be found in the local class loader
 * 
 * @see #getDeclaredMethod
 * @see #getMethod
 */
private String getParameterTypesSignature(boolean throwException, String name, Class<?>[] parameterTypes, String returnTypeSignature) throws NoSuchMethodException {
	int total = 2;
	String[] sigs = new String[parameterTypes.length];	
	for(int i = 0; i < parameterTypes.length; i++) {
		Class<?> parameterType = parameterTypes[i];
		/*[PR 103441] should throw NoSuchMethodException */
		if (parameterType != null) {
			sigs[i] = parameterType.getSignature();
			total += sigs[i].length();
		} else {
			if (throwException) {
				throw newNoSuchMethodException(name, parameterTypes);
			} else {
				return null;
			}
		}
	}
	total += returnTypeSignature.length();
	StringBuilder signature = new StringBuilder(total);
	signature.append('(');
	for(int i = 0; i < parameterTypes.length; i++)
		signature.append(sigs[i]);
	signature.append(')').append(returnTypeSignature);
	return signature.toString();
}

/*[PR CMVC 114820, CMVC 115873, CMVC 116166] add reflection cache */
private static Method copyMethod, copyField, copyConstructor;
private static Field methodParameterTypesField;
private static Field constructorParameterTypesField;
private static final Object[] NoArgs = new Object[0];

/*[PR JAZZ 107786] constructorParameterTypesField should be initialized regardless of reflectCacheEnabled or not */
static void initCacheIds(boolean cacheEnabled, boolean cacheDebug) {
	reflectCacheEnabled = cacheEnabled;
	reflectCacheDebug = cacheDebug;
	AccessController.doPrivileged(new PrivilegedAction<Void>() {
		@Override
		public Void run() {
			doInitCacheIds();
			return null;
		}
	});
}
static void setReflectCacheAppOnly(boolean cacheAppOnly) {
	reflectCacheAppOnly = cacheAppOnly;
}
@SuppressWarnings("nls")
static void doInitCacheIds() {
	/*
	 * We cannot just call getDeclaredField() because that method includes a call
	 * to Reflection.filterFields() which will remove the fields needed here.
	 * The remaining required behavior of getDeclaredField() is inlined here.
	 * The security checks are omitted (they would be redundant). Caching is
	 * not done (we're in the process of initializing the caching mechanisms).
	 * We must ensure the classes that own the fields of interest are prepared.
	 */
	J9VMInternals.prepare(Constructor.class);
	J9VMInternals.prepare(Method.class);
	try {
		constructorParameterTypesField = Constructor.class.getDeclaredFieldImpl("parameterTypes");
		methodParameterTypesField = Method.class.getDeclaredFieldImpl("parameterTypes");
	} catch (NoSuchFieldException e) {
		throw newInternalError(e);
	}
	constructorParameterTypesField.setAccessible(true);
	methodParameterTypesField.setAccessible(true);
	if (reflectCacheEnabled) {
		copyConstructor = getAccessibleMethod(Constructor.class, "copy");
		copyMethod = getAccessibleMethod(Method.class, "copy");
		copyField = getAccessibleMethod(Field.class, "copy");
	}
}
private static Method getAccessibleMethod(Class<?> cls, String name) {
	try {
		Method method = cls.getDeclaredMethod(name, EmptyParameters);
		method.setAccessible(true);
		return method;
	} catch (NoSuchMethodException e) {
		throw newInternalError(e);
	}
}

/*[PR RTC 104994 redesign getMethods]*/
/**
 * represents all methods of a given name and signature visible from a given class or interface.
 *
 */
private class MethodInfo {
	ArrayList<Method> jlrMethods;
	Method me;
	private final int myHash;
	private Class<?>[] paramTypes;
	private Class<?> returnType;
	private java.lang.String methodName;
	
	public MethodInfo(Method myMethod) {
		me = myMethod;
		methodName = myMethod.getName();
		myHash = methodName.hashCode();
		this.paramTypes = null;
		this.returnType = null;
		jlrMethods = null;
	}

	public MethodInfo(MethodInfo otherMi) {
		this.me = otherMi.me;
		this.methodName = otherMi.methodName;
		this.paramTypes = otherMi.paramTypes;
		this.returnType = otherMi.returnType;
		this.myHash = otherMi.myHash;
	
		if (null != otherMi.jlrMethods) {
			jlrMethods = (ArrayList<Method>) otherMi.jlrMethods.clone();
		} else {
			jlrMethods = null;
		}

	}

	private void initializeTypes() {
		paramTypes = getParameterTypes(me);
		returnType = me.getReturnType();
	}

	/** (non-Javadoc)
	 * @param that another MethodInfo object
	 * @return true if the methods have the same name and signature
	 * @note does not compare the defining class, permissions, exceptions, etc.
	 */
	@Override
	public boolean equals(Object that) {
		if (this == that) {
			return true;
		}
		if (!that.getClass().equals(this.getClass())) {
			return false;
		}
		@SuppressWarnings("unchecked")
		MethodInfo otherMethod = (MethodInfo) that;
		if (!methodName.equals(otherMethod.methodName)) {
			return false;
		}
		if (null == returnType) {
			initializeTypes();
		}
		if (null == otherMethod.returnType) {
			otherMethod.initializeTypes();
		}
		if (!returnType.equals(otherMethod.returnType)) {
			return false;
		}
		Class<?>[] m1Parms = paramTypes;
		Class<?>[] m2Parms = otherMethod.paramTypes;
		if (m1Parms.length != m2Parms.length) {
			return false;
		}
		for (int i = 0; i < m1Parms.length; i++) {
			if (m1Parms[i] != m2Parms[i]) {
				return false;
			}
		}	
		return true;
	}
	
	/**
	 * Add a method to the list.  newMethod may be discarded if it is masked by an incumbent method in the list.
	 * Also, an incumbent method may be removed if newMethod masks it.
	 * In general, a target class inherits a method from its direct superclass or directly implemented interfaces unless:
	 * 	- the method is static or private and the declaring class is not the target class 
	 * 	- the target class declares the method (concrete or abstract)
	 * 	- the method is default and a superclass of the target class contains a concrete implementation of the method
	 * 	- a more specific implemented interface contains a concrete implementation
	 * @param newMethod method to be added.
	 */
	void update(Method newMethod) {
		int newModifiers = newMethod.getModifiers();
		if (!Modifier.isPublic(newModifiers)) { /* can't see the method */ 
			return;
		}
		Class<?> newMethodClass = newMethod.getDeclaringClass();
		boolean newMethodIsAbstract = Modifier.isAbstract(newModifiers);
		boolean newMethodClassIsInterface = newMethodClass.isInterface();
		
		if (null == jlrMethods) { 
			/* handle the common case of a single declaration */
			if (!newMethod.equals(me)) {
				Class<?> incumbentMethodClass = me.getDeclaringClass();
				if (Class.this != incumbentMethodClass) {
					boolean incumbentIsAbstract = Modifier.isAbstract(me.getModifiers());
					boolean incumbentClassIsInterface = incumbentMethodClass.isInterface();
					if (methodAOverridesMethodB(newMethodClass, newMethodIsAbstract, newMethodClassIsInterface,
							incumbentMethodClass, incumbentIsAbstract, incumbentClassIsInterface)
					) {
						me = newMethod; 
					} else if (!methodAOverridesMethodB(incumbentMethodClass, incumbentIsAbstract, incumbentClassIsInterface,
							newMethodClass, newMethodIsAbstract, newMethodClassIsInterface)
					) { 
						/* we need to store both */
						jlrMethods = new ArrayList<>(2);
						jlrMethods.add(me);
						jlrMethods.add(newMethod);				
					}
				}
			}
		} else {
			int methodCursor = 0;
			boolean addMethod = true;
			boolean replacedMethod = false;
			while (methodCursor < jlrMethods.size()) {
				int increment = 1;
				Method m = jlrMethods.get(methodCursor);
				if (newMethod.equals(m)) { /* already have this method */
					addMethod = false;
				} else {
					Class<?> incumbentMethodClass = m.getDeclaringClass();
					if (Class.this == incumbentMethodClass) {
						addMethod = false;
					} else {
						boolean incumbentIsAbstract = Modifier.isAbstract(m.getModifiers());
						boolean incumbentClassIsInterface = incumbentMethodClass.isInterface();
						if (methodAOverridesMethodB(newMethodClass, newMethodIsAbstract, newMethodClassIsInterface,
								incumbentMethodClass, incumbentIsAbstract, incumbentClassIsInterface)
						) {
							if (!replacedMethod) {
								/* preserve ordering by removing old and appending new instead of directly replacing. */ 
								jlrMethods.remove(methodCursor);
								jlrMethods.add(newMethod);
								increment = 0;
								replacedMethod = true;
							} else {
								jlrMethods.remove(methodCursor);
								increment = 0; 
								/* everything slid over one slot */
							}
							addMethod = false;
						} else if (methodAOverridesMethodB(incumbentMethodClass, incumbentIsAbstract, incumbentClassIsInterface,
								newMethodClass, newMethodIsAbstract, newMethodClassIsInterface)
						) {
							addMethod = false;
						}
					}
				}
				methodCursor += increment;
			}
			if (addMethod) {
				jlrMethods.add(newMethod);
			}
		}
	}

	public void update(MethodInfo otherMi) {
		if (null == otherMi.jlrMethods) {
			update(otherMi.me);
		} else for (Method m: otherMi.jlrMethods) {
			update(m);
		}		
	}
	@Override
	public int hashCode() {
		return myHash;
	}
	
}

static boolean methodAOverridesMethodB(Class<?> methodAClass,	boolean methodAIsAbstract, boolean methodAClassIsInterface,
		Class<?> methodBClass, boolean methodBIsAbstract, boolean methodBClassIsInterface) {
	return (methodBIsAbstract && methodBClassIsInterface && !methodAIsAbstract && !methodAClassIsInterface) ||
			(methodBClass.isAssignableFrom(methodAClass)
					/*[IF !Sidecar19-SE]*/
					/*
					 * In Java 8, abstract methods in subinterfaces do not hide abstract methods in superinterfaces.
					 * This is fixed in Java 9.
					 */
					&& (!methodAClassIsInterface || !methodAIsAbstract)
					/*[ENDIF]*/
					);
}

/*[PR 125873] Improve reflection cache */
private static final class ReflectRef extends SoftReference<Object> implements Runnable {
	private static final ReferenceQueue<Object> queue = new ReferenceQueue<>();
	private final ReflectCache cache;
	final CacheKey key;
	ReflectRef(ReflectCache cache, CacheKey key, Object value) {
		super(value, queue);
		this.cache = cache;
		this.key = key;
	}
	@Override
	public void run() {
		cache.handleCleared(this);
	}
}

/*[IF]*/
/*
 * Keys for constructors, fields and methods are all mutually distinct so we can
 * distinguish them in a single map. The key for a field has parameterTypes == null
 * while parameterTypes can't be null for constructors or methods. The key for a
 * constructor has an empty name which is not legal in a class file (for any feature).
 * The Public* and Declared* keys have names that can't collide with any other normal
 * key (derived from a legal class).
 */
/*[ENDIF]*/
private static final class CacheKey {
	/*[PR CMVC 163440] java.lang.Class$CacheKey.PRIME should be static */
	private static final int PRIME = 31;
	private static int hashCombine(int partial, int itemHash) {
		return partial * PRIME + itemHash;
	}
	private static int hashCombine(int partial, Object item) {
		return hashCombine(partial, item == null ? 0 : item.hashCode());
	}

	static CacheKey newConstructorKey(Class<?>[] parameterTypes) {
		return new CacheKey("", parameterTypes, null); //$NON-NLS-1$
	}
	static CacheKey newFieldKey(String fieldName, Class<?> type) {
		return new CacheKey(fieldName, null, type);
	}
	static CacheKey newMethodKey(String methodName, Class<?>[] parameterTypes, Class<?> returnType) {
		return new CacheKey(methodName, parameterTypes, returnType);
	}
/*[IF JAVA_SPEC_VERSION >= 11]*/
	static CacheKey newDeclaredPublicMethodsKey(String methodName, Class<?>[] parameterTypes) {
		return new CacheKey("#m" + methodName, parameterTypes, null);	//$NON-NLS-1$
	}
/*[ENDIF] JAVA_SPEC_VERSION >= 11 */

	static final CacheKey PublicConstructorsKey = new CacheKey("/c", EmptyParameters, null); //$NON-NLS-1$
	static final CacheKey PublicFieldsKey = newFieldKey("/f", null); //$NON-NLS-1$
	static final CacheKey PublicMethodsKey = new CacheKey("/m", EmptyParameters, null); //$NON-NLS-1$

	static final CacheKey DeclaredConstructorsKey = new CacheKey(".c", EmptyParameters, null); //$NON-NLS-1$
	static final CacheKey DeclaredFieldsKey = newFieldKey(".f", null); //$NON-NLS-1$
	static final CacheKey DeclaredMethodsKey = new CacheKey(".m", EmptyParameters, null); //$NON-NLS-1$

	private final String name;
	private final Class<?>[] parameterTypes;
	private final Class<?> returnType;
	private final int hashCode;
	private CacheKey(String name, Class<?>[] parameterTypes, Class<?> returnType) {
		super();
		int hash = hashCombine(name.hashCode(), returnType);
		if (parameterTypes != null) {
			for (Class<?> parameterType : parameterTypes) {
				hash = hashCombine(hash, parameterType);
			}
		}
		this.name = name;
		this.parameterTypes = parameterTypes;
		this.returnType = returnType;
		this.hashCode = hash;
	}
	@Override
	public boolean equals(Object obj) {
		if (this == obj) {
			return true;
		}
		CacheKey that = (CacheKey) obj;
		if (this.returnType == that.returnType
				&& sameTypes(this.parameterTypes, that.parameterTypes)) {
			return this.name.equals(that.name);
		}
		return false;
	}
	@Override
	public int hashCode() {
		return hashCode;
	}
}

private static Class<?>[] getParameterTypes(Constructor<?> constructor) {
	try {
		if (null != constructorParameterTypesField)	{
			return (Class<?>[]) constructorParameterTypesField.get(constructor);
		} else {
			return constructor.getParameterTypes();
		}
	} catch (IllegalAccessException | IllegalArgumentException e) {
		throw newInternalError(e);
	}
}

static Class<?>[] getParameterTypes(Method method) {
	try {
		if (null != methodParameterTypesField)	{
			return (Class<?>[]) methodParameterTypesField.get(method);
		} else {
			return method.getParameterTypes();
		}
	} catch (IllegalAccessException | IllegalArgumentException e) {
		throw newInternalError(e);
	}
}

/*[PR 125873] Improve reflection cache */
private static final class ReflectCache extends ConcurrentHashMap<CacheKey, ReflectRef> {
	private static final long serialVersionUID = 6551549321039776630L;

	private final Class<?> owner;
	private final AtomicInteger useCount;

	ReflectCache(Class<?> owner) {
		super();
		this.owner = owner;
		this.useCount = new AtomicInteger();
	}

	ReflectCache acquire() {
		useCount.incrementAndGet();
		return this;
	}

	void handleCleared(ReflectRef ref) {
		boolean removed = false;
		if (remove(ref.key, ref) && isEmpty()) {
			if (useCount.get() == 0) {
				owner.setReflectCache(null);
				removed = true;
			}
		}
		if (reflectCacheDebug) {
			if (removed) {
				System.err.println("Removed reflect cache for: " + this); //$NON-NLS-1$
			} else {
				System.err.println("Retained reflect cache for: " + this + ", size: " + size()); //$NON-NLS-1$ //$NON-NLS-2$
			}
		}
	}

	Object find(CacheKey key) {
		ReflectRef ref = get(key);
		return ref != null ? ref.get() : null;
	}

	void insert(CacheKey key, Object value) {
		put(key, new ReflectRef(this, key, value));
	}

	<T> T insertIfAbsent(CacheKey key, T value) {
		ReflectRef newRef = new ReflectRef(this, key, value);
		for (;;) {
			ReflectRef oldRef = putIfAbsent(key, newRef);
			if (oldRef == null) {
				return value;
			}
			T oldValue = (T) oldRef.get();
			if (oldValue != null) {
				return oldValue;
			}
			// The entry addressed by key has been cleared, but not yet removed from this map.
			// One thread will successfully replace the entry; the value stored will be shared.
			if (replace(key, oldRef, newRef)) {
				return value;
			}
		}
	}

	void release() {
		useCount.decrementAndGet();
	}

}

private transient ReflectCache reflectCache;
private static long reflectCacheOffset = -1;

private ReflectCache acquireReflectCache() {
	ReflectCache cache = reflectCache;
	if (cache == null) {
		Unsafe theUnsafe = getUnsafe();
		long cacheOffset = getReflectCacheOffset();
		ReflectCache newCache = new ReflectCache(this);
		do {
			// Some thread will insert this new cache making it available to all.
/*[IF Sidecar19-SE]*/
			if (theUnsafe.compareAndSetObject(this, cacheOffset, null, newCache)) {
/*[ELSE]
			if (theUnsafe.compareAndSwapObject(this, cacheOffset, null, newCache)) {
/*[ENDIF]*/			
				cache = newCache;
				break;
			}
			cache = (ReflectCache) theUnsafe.getObject(this, cacheOffset);
		} while (cache == null);
	}
	return cache.acquire();
}
private static long getReflectCacheOffset() {
	long cacheOffset = reflectCacheOffset;
	if (cacheOffset < 0) {
		try {
			// Bypass the reflection cache to avoid infinite recursion.
			Field reflectCacheField = Class.class.getDeclaredFieldImpl("reflectCache"); //$NON-NLS-1$
			cacheOffset = getUnsafe().objectFieldOffset(reflectCacheField);
			reflectCacheOffset = cacheOffset;
		} catch (NoSuchFieldException e) {
			throw newInternalError(e);
		}
	}
	return cacheOffset;
}
void setReflectCache(ReflectCache cache) {
	// Lazy initialization of a non-volatile field. Ensure the Object is initialized
	// and flushed to memory before assigning to the annotationCache field.
	/*[IF Sidecar19-SE]
	getUnsafe().putObjectRelease(this, getReflectCacheOffset(), cache);
	/*[ELSE]*/
	getUnsafe().putOrderedObject(this, getReflectCacheOffset(), cache);
	/*[ENDIF]*/
}

private ReflectCache peekReflectCache() {
	return reflectCache;
}

static InternalError newInternalError(Exception cause) {
	return new InternalError(cause);
}

private Method lookupCachedMethod(String methodName, Class<?>[] parameters) {
	if (!reflectCacheEnabled) return null;
	if (reflectCacheDebug) {
		reflectCacheDebugHelper(null, 0, "lookup Method: ", getName(), ".", methodName);	//$NON-NLS-1$ //$NON-NLS-2$
	}
	ReflectCache cache = peekReflectCache();
	if (cache != null) {
		// use a null returnType to find the Method with the largest depth
		Method method = (Method) cache.find(CacheKey.newMethodKey(methodName, parameters, null));
		if (method != null) {
			try {
				Class<?>[] orgParams = getParameterTypes(method);
				// ensure the parameter classes are identical
				if (sameTypes(parameters, orgParams)) {
					return (Method) copyMethod.invoke(method, NoArgs);
				}
			} catch (IllegalAccessException | IllegalArgumentException | InvocationTargetException e) {
				throw newInternalError(e);
			}
		}
	}
	return null;
}

@CallerSensitive
private Method cacheMethod(Method method) {
	if (!reflectCacheEnabled) return method;
	if (reflectCacheAppOnly && ClassLoader.getStackClassLoader(2) == ClassLoader.bootstrapClassLoader) {
		return method;
	}
	if (copyMethod == null) return method;
	if (reflectCacheDebug) {
		reflectCacheDebugHelper(null, 0, "cache Method: ", getName(), ".", method.getName());	//$NON-NLS-1$ //$NON-NLS-2$
	}
	try {
		Class<?>[] parameterTypes = getParameterTypes(method);
		CacheKey key = CacheKey.newMethodKey(method.getName(), parameterTypes, method.getReturnType());
		Class<?> declaringClass = method.getDeclaringClass();
		ReflectCache cache = declaringClass.acquireReflectCache();
		try {
			/*[PR CMVC 116493] store inherited methods in their declaringClass */
			method = cache.insertIfAbsent(key, method);
		} finally {
			if (declaringClass != this) {
				cache.release();
				cache = acquireReflectCache();
			}
		}
		try {
			// cache the Method with the largest depth with a null returnType		
			CacheKey lookupKey = CacheKey.newMethodKey(method.getName(), parameterTypes, null);
			cache.insert(lookupKey, method);
		} finally {
			cache.release();
		}
		return (Method)copyMethod.invoke(method, NoArgs);
	} catch (IllegalAccessException | IllegalArgumentException | InvocationTargetException e) {
		throw newInternalError(e);
	}
}

private Field lookupCachedField(String fieldName) {
	if (!reflectCacheEnabled) return null;
	if (reflectCacheDebug) {
		reflectCacheDebugHelper(null, 0, "lookup Field: ", getName(), ".", fieldName);	//$NON-NLS-1$ //$NON-NLS-2$
	}
	ReflectCache cache = peekReflectCache();
	if (cache != null) {
		/*[PR 124746] Field cache cannot handle same field name with multiple types */
		Field field = (Field) cache.find(CacheKey.newFieldKey(fieldName, null));
		if (field != null) {
			try {
				return (Field)copyField.invoke(field, NoArgs);
			} catch (IllegalAccessException | IllegalArgumentException | InvocationTargetException e) {
				throw newInternalError(e);
			}
		}
	}
	return null;
}

@CallerSensitive
private Field cacheField(Field field) {
	if (!reflectCacheEnabled) return field;
	if (reflectCacheAppOnly && ClassLoader.getStackClassLoader(2) == ClassLoader.bootstrapClassLoader) {
		return field;
	}
	if (copyField == null) return field;
	if (reflectCacheDebug) {
		reflectCacheDebugHelper(null, 0, "cache Field: ", getName(), ".", field.getName());	//$NON-NLS-1$ //$NON-NLS-2$
	}
	/*[PR 124746] Field cache cannot handle same field name with multiple types */
	CacheKey typedKey = CacheKey.newFieldKey(field.getName(), field.getType());  
	Class<?> declaringClass = field.getDeclaringClass();
	ReflectCache cache = declaringClass.acquireReflectCache();
	try {
		field = cache.insertIfAbsent(typedKey, field);
		/*[PR 124746] Field cache cannot handle same field name with multiple types */
		if (declaringClass == this) {
			// cache the Field returned from getField() with a null returnType
			CacheKey lookupKey = CacheKey.newFieldKey(field.getName(), null);
			cache.insert(lookupKey, field);
		}
	} finally {
		cache.release();
	}
	try {
		return (Field) copyField.invoke(field, NoArgs);
	} catch (IllegalAccessException | IllegalArgumentException | InvocationTargetException e) {
		throw newInternalError(e);
	}
}

private Constructor<T> lookupCachedConstructor(Class<?>[] parameters) {
	if (!reflectCacheEnabled) return null;

	if (reflectCacheDebug) {
		reflectCacheDebugHelper(parameters, 1, "lookup Constructor: ", getName());	//$NON-NLS-1$
	}
	ReflectCache cache = peekReflectCache();
	if (cache != null) {
		Constructor<?> constructor = (Constructor<?>) cache.find(CacheKey.newConstructorKey(parameters));
		if (constructor != null) {
			Class<?>[] orgParams = getParameterTypes(constructor);
			try {
				// ensure the parameter classes are identical
				if (sameTypes(orgParams, parameters)) {
					return (Constructor<T>) copyConstructor.invoke(constructor, NoArgs);
				}
			} catch (IllegalAccessException | IllegalArgumentException | InvocationTargetException e) {
				throw newInternalError(e);
			}
		}
	}
	return null;
}

@CallerSensitive
private Constructor<T> cacheConstructor(Constructor<T> constructor) {
	if (!reflectCacheEnabled) return constructor;
	if (reflectCacheAppOnly && ClassLoader.getStackClassLoader(2) == ClassLoader.bootstrapClassLoader) {
		return constructor;
	}
	if (copyConstructor == null) return constructor;
	if (reflectCacheDebug) {
		reflectCacheDebugHelper(constructor.getParameterTypes(), 1, "cache Constructor: ", getName());	//$NON-NLS-1$
	}
	ReflectCache cache = acquireReflectCache();
	try {
		CacheKey key = CacheKey.newConstructorKey(getParameterTypes(constructor));
		cache.insert(key, constructor);
	} finally {
		cache.release();
	}
	try {
		return (Constructor<T>) copyConstructor.invoke(constructor, NoArgs);
	} catch (IllegalAccessException | IllegalArgumentException | InvocationTargetException e) {
		throw newInternalError(e);
	}
}

private static Method[] copyMethods(Method[] methods) {
	Method[] result = new Method[methods.length];
	try {
		for (int i=0; i<methods.length; i++) {
			result[i] = (Method)copyMethod.invoke(methods[i], NoArgs);
		}
		return result;
	} catch (IllegalAccessException | IllegalArgumentException | InvocationTargetException e) {
		throw newInternalError(e);
	}
}

private Method[] lookupCachedMethods(CacheKey cacheKey) {
	if (!reflectCacheEnabled) return null;

	if (reflectCacheDebug) {
		reflectCacheDebugHelper(null, 0, "lookup Methods in: ", getName());	//$NON-NLS-1$
	}
	ReflectCache cache = peekReflectCache();
	if (cache != null) {
		Method[] methods = (Method[]) cache.find(cacheKey);
		if (methods != null) {
			return copyMethods(methods);
		}
	}
	return null;
}

@CallerSensitive
private Method[] cacheMethods(Method[] methods, CacheKey cacheKey) {
	if (!reflectCacheEnabled) return methods;
	if (reflectCacheAppOnly && ClassLoader.getStackClassLoader(2) == ClassLoader.bootstrapClassLoader) {
		return methods;
	}
	if (copyMethod == null) return methods;
	if (reflectCacheDebug) {
		reflectCacheDebugHelper(null, 0, "cache Methods in: ", getName());	//$NON-NLS-1$
	}
	ReflectCache cache = null;
	Class<?> cacheOwner = null;
	try {
		for (int i = 0; i < methods.length; ++i) {
			Method method = methods[i];
			CacheKey key = CacheKey.newMethodKey(method.getName(), getParameterTypes(method), method.getReturnType());
			Class<?> declaringClass = method.getDeclaringClass();
			if (cacheOwner != declaringClass || cache == null) {
				if (cache != null) {
					cache.release();
					cache = null;
				}
				cache = declaringClass.acquireReflectCache();
				cacheOwner = declaringClass;
			}
			methods[i] = cache.insertIfAbsent(key, method);
		}
		if (cache != null && cacheOwner != this) {
			cache.release();
			cache = null;
		}
		if (cache == null) {
			cache = acquireReflectCache();
		}
		cache.insert(cacheKey, methods);
	} finally {
		if (cache != null) {
			cache.release();
		}
	}
	return copyMethods(methods);
}

private static Field[] copyFields(Field[] fields) {
	Field[] result = new Field[fields.length];
	try {
		for (int i=0; i<fields.length; i++) {
			result[i] = (Field)copyField.invoke(fields[i], NoArgs);
		}
		return result;
	} catch (IllegalAccessException | IllegalArgumentException | InvocationTargetException e) {
		throw newInternalError(e);
	}
}

private Field[] lookupCachedFields(CacheKey cacheKey) {
	if (!reflectCacheEnabled) return null;

	if (reflectCacheDebug) {
		reflectCacheDebugHelper(null, 0, "lookup Fields in: ", getName());	//$NON-NLS-1$
	}
	ReflectCache cache = peekReflectCache();
	if (cache != null) {
		Field[] fields = (Field[]) cache.find(cacheKey);
		if (fields != null) {
			return copyFields(fields);
		}
	}
	return null;
}

@CallerSensitive
private Field[] cacheFields(Field[] fields, CacheKey cacheKey) {
	if (!reflectCacheEnabled) return fields;
	if (reflectCacheAppOnly && ClassLoader.getStackClassLoader(2) == ClassLoader.bootstrapClassLoader) {
		return fields;
	}
	if (copyField == null) return fields;
	if (reflectCacheDebug) {
		reflectCacheDebugHelper(null, 0, "cache Fields in: ", getName());	//$NON-NLS-1$
	}
	ReflectCache cache = null;
	Class<?> cacheOwner = null;
	try {
		for (int i = 0; i < fields.length; ++i) {
			/*[PR 124746] Field cache cannot handle same field name with multiple types */
			Field field = fields[i];
			Class<?> declaringClass = field.getDeclaringClass();
			if (cacheOwner != declaringClass || cache == null) {
				if (cache != null) {
					cache.release();
					cache = null;
				}
				cache = declaringClass.acquireReflectCache();
				cacheOwner = declaringClass;
			}
			fields[i] = cache.insertIfAbsent(CacheKey.newFieldKey(field.getName(), field.getType()), field);
		}
		if (cache != null && cacheOwner != this) {
			cache.release();
			cache = null;
		}
		if (cache == null) {
			cache = acquireReflectCache();
		}
		cache.insert(cacheKey, fields);
	} finally {
		if (cache != null) {
			cache.release();
		}
	}
	return copyFields(fields);
}

private static <T> Constructor<T>[] copyConstructors(Constructor<T>[] constructors) {
	Constructor<T>[] result = new Constructor[constructors.length];
	try {
		for (int i=0; i<constructors.length; i++) {
			result[i] = (Constructor<T>) copyConstructor.invoke(constructors[i], NoArgs);
		}
		return result;
	} catch (IllegalAccessException | IllegalArgumentException | InvocationTargetException e) {
		throw newInternalError(e);
	}
}

private Constructor<T>[] lookupCachedConstructors(CacheKey cacheKey) {
	if (!reflectCacheEnabled) return null;

	if (reflectCacheDebug) {
		reflectCacheDebugHelper(null, 0, "lookup Constructors in: ", getName());	//$NON-NLS-1$
	}
	ReflectCache cache = peekReflectCache();
	if (cache != null) {
		Constructor<T>[] constructors = (Constructor<T>[]) cache.find(cacheKey);
		if (constructors != null) {
			return copyConstructors(constructors);
		}
	}
	return null;
}

@CallerSensitive
private Constructor<T>[] cacheConstructors(Constructor<T>[] constructors, CacheKey cacheKey) {
	if (!reflectCacheEnabled) return constructors;
	if (reflectCacheAppOnly && ClassLoader.getStackClassLoader(2) == ClassLoader.bootstrapClassLoader) {
		return constructors;
	}
	if (copyConstructor == null) return constructors;
	if (reflectCacheDebug) {
		reflectCacheDebugHelper(null, 0, "cache Constructors in: ", getName());	//$NON-NLS-1$
	}
	ReflectCache cache = acquireReflectCache();
	try {
		for (int i=0; i<constructors.length; i++) {
			CacheKey key = CacheKey.newConstructorKey(getParameterTypes(constructors[i]));
			constructors[i] = cache.insertIfAbsent(key, constructors[i]);
		}
		cache.insert(cacheKey, constructors);
	} finally {
		cache.release();
	}
	return copyConstructors(constructors);
}

private static <T> Constructor<T> checkParameterTypes(Constructor<T> constructor, Class<?>[] parameterTypes) {
	Class<?>[] constructorParameterTypes = getParameterTypes(constructor);
	return sameTypes(constructorParameterTypes, parameterTypes) ? constructor : null;
}

static boolean sameTypes(Class<?>[] aTypes, Class<?>[] bTypes) {
	if (aTypes == null) {
		if (bTypes == null) {
			return true;
		}
	} else if (bTypes != null) {
		int length = aTypes.length;
		if (length == bTypes.length) {
			for (int i = 0; i < length; ++i) {
				if (aTypes[i] != bTypes[i]) {
					return false;
				}
			}
			return true;
		}
	}
	return false;
}

Object getMethodHandleCache() {
	return methodHandleCache;
}

Object setMethodHandleCache(Object cache) {
	Object result = methodHandleCache;
	if (null == result) {
		synchronized (this) {
			result = methodHandleCache;
			if (null == result) {
				methodHandleCache = cache;
				result = cache;
			}
		}
	}
	return result;
}

ConstantPool getConstantPool() {
	return SharedSecrets.getJavaLangAccess().getConstantPool(this);
}

/*[IF Sidecar19-SE]*/
Map<Class<? extends Annotation>, Annotation> getDeclaredAnnotationMap() {
	throw new Error("Class.getDeclaredAnnotationMap() unimplemented"); //$NON-NLS-1$
}
byte[] getRawAnnotations() {
	throw new Error("Class.getRawAnnotations() unimplemented"); //$NON-NLS-1$
}
byte[] getRawTypeAnnotations() {
	throw new Error("Class.getRawTypeAnnotations() unimplemented"); //$NON-NLS-1$
}
static byte[] getExecutableTypeAnnotationBytes(Executable exec) {
	throw new Error("Class.getExecutableTypeAnnotationBytes() unimplemented"); //$NON-NLS-1$
}
/*[ENDIF] Sidecar19-SE*/

/*[IF JAVA_SPEC_VERSION >= 11]*/
/**
 * Answers the host class of the receiver's nest.
 *
 * @return		the host class of the receiver.
 */
private native Class<?> getNestHostImpl();

/**
 * Answers the nest member classes of the receiver's nest host.
 *
 * @return		the host class of the receiver.
 * 
 * @implNote This implementation does not remove duplicate nest members if they are present.
 */
private native Class<?>[] getNestMembersImpl();

/**
 * Answers the host class of the receiver's nest.
 * 
 * @throws SecurityException if nestHost is not same as the current class, a security manager
 *	is present, the classloader of the caller is not the same or an ancestor of nestHost
 * 	class, and checkPackageAccess() denies access
 * @return the host class of the receiver.
 */
@CallerSensitive
public Class<?> getNestHost() throws SecurityException {
	if (nestHost == null) {
		nestHost = getNestHostImpl();
	}
	/* The specification requires that if:
	 *    - the returned class is not the current class
	 *    - a security manager is present
	 *    - the caller's class loader is not the same or an ancestor of the returned class
	 *    - s.checkPackageAccess() disallows access to the package of the returned class
	 * then throw a SecurityException.
	 */
	if (nestHost != this) {
		@SuppressWarnings("removal")
		SecurityManager securityManager = System.getSecurityManager();
		if (securityManager != null) {
			ClassLoader callerClassLoader = ClassLoader.getCallerClassLoader();
			ClassLoader nestHostClassLoader = nestHost.internalGetClassLoader();
			if (!doesClassLoaderDescendFrom(nestHostClassLoader, callerClassLoader)) {
				String nestHostPackageName = nestHost.getPackageName();
				if ((nestHostPackageName != null) && (nestHostPackageName != "")) {
					securityManager.checkPackageAccess(nestHostPackageName);
				}
			}
		}
	}
	return nestHost;
}

/**
 * Returns true if the class passed has the same nest top as this class.
 * 
 * @param that The class to compare
 * @return true if class is a nestmate of this class; false otherwise.
 *
 */
public boolean isNestmateOf(Class<?> that) {
	Class<?> thisNestHost = this.nestHost;
	if (thisNestHost == null) {
		thisNestHost = this.getNestHostImpl();
	}
	Class<?> thatNestHost = that.nestHost;
	if (thatNestHost == null) {
		thatNestHost = that.getNestHostImpl();
	}
	return (thisNestHost == thatNestHost);
}

/**
 * Answers the nest member classes of the receiver's nest host.
 *
 * @throws SecurityException if a SecurityManager is present and package access is not allowed
/*[IF JAVA_SPEC_VERSION < 15]
 * @throws LinkageError if there is any problem loading or validating a nest member or the nest host
/*[ENDIF]
 * @throws SecurityException if a returned class is not the current class, a security manager is enabled,
 *	the caller's class loader is not the same or an ancestor of that returned class, and the
 * 	checkPackageAccess() denies access
 * @return the host class of the receiver.
 */
@CallerSensitive
public Class<?>[] getNestMembers() throws
/*[IF JAVA_SPEC_VERSION < 15] */
LinkageError,
/*[ENDIF] JAVA_SPEC_VERSION < 15 */
SecurityException {
	if (isArray() || isPrimitive()) {
		/* By spec, Class objects representing array types or primitive types
		 * belong to the nest consisting only of itself.
		 */
		return new Class<?>[] { this };
	}

	Class<?>[] members = getNestMembersImpl();
	/* Skip security check for the Class object that belongs to the nest consisting only of itself */
	if (members.length > 1) {
		@SuppressWarnings("removal")
		SecurityManager securityManager = System.getSecurityManager();
		if (securityManager != null) {
			/* All classes in a nest must be in the same runtime package and therefore same classloader */
			ClassLoader nestMemberClassLoader = this.internalGetClassLoader();
			ClassLoader callerClassLoader = ClassLoader.getCallerClassLoader();
			if (!doesClassLoaderDescendFrom(nestMemberClassLoader, callerClassLoader)) {
				String nestMemberPackageName = this.getPackageName();
				if ((nestMemberPackageName != null) && (nestMemberPackageName != "")) { //$NON-NLS-1$
					securityManager.checkPackageAccess(nestMemberPackageName);
				}
			}
		}
	}

	return members;
}
/*[ENDIF] JAVA_SPEC_VERSION >= 11 */

/*[IF JAVA_SPEC_VERSION >= 12]*/
	/**
	 * Create class of an array. The component type will be this Class instance.
	 * 
	 * @return array class where the component type is this Class instance
	 */
	public Class<?> arrayType() {
		if (this == void.class) {
			throw new IllegalArgumentException();
		}
		return arrayTypeImpl();
	}

	private native Class<?> arrayTypeImpl();

	/**
	 * Answers a Class object which represents the receiver's component type if the receiver 
	 * represents an array type. The component type of an array type is the type of the elements 
	 * of the array.
	 *
	 * @return the component type of the receiver. Returns null if the receiver does 
	 * not represent an array.
	 */
	public Class<?> componentType() {
		return getComponentType();
	}

	/**
	 * Returns the nominal descriptor of this Class instance, or an empty Optional 
	 * if construction is not possible.
	 * 
	 * @return Optional with a nominal descriptor of Class instance
	 */
	public Optional<ClassDesc> describeConstable() {
/*[IF JAVA_SPEC_VERSION >= 15]*/
		Class<?> clazz = this;
		if (isArray()) {
			clazz = getComponentType();
			while (clazz.isArray()) {
				clazz = clazz.getComponentType();
			}
		}
		if (clazz.isHidden()) {
			/* It is always an empty Optional for hidden classes. */
			return Optional.empty(); 
		}
/*[ENDIF] JAVA_SPEC_VERSION >= 15 */

		ClassDesc classDescriptor = ClassDesc.ofDescriptor(this.descriptorString());
		return Optional.of(classDescriptor);
	}

	/**
	 * Return field descriptor of Class instance.
	 * 
	 * @return field descriptor of Class instance
	 */
	public String descriptorString() {
		/* see MethodTypeHelper.getBytecodeStringName */

		if (this.isPrimitive()) {
			if (this == int.class) {
				return "I"; //$NON-NLS-1$
			} else if (this == long.class) {
				return "J"; //$NON-NLS-1$
			} else if (this == byte.class) {
				return "B"; //$NON-NLS-1$
			} else if (this == boolean.class) {
				return "Z"; //$NON-NLS-1$
			} else if (this == void.class) {
				return "V"; //$NON-NLS-1$
			} else if (this == char.class) {
				return "C"; //$NON-NLS-1$
			} else if (this == double.class) {
				return "D"; //$NON-NLS-1$
			} else if (this == float.class) {
				return "F"; //$NON-NLS-1$
			} else if (this == short.class) {
				return "S"; //$NON-NLS-1$
			}
		}
		String name = this.getName().replace('.', '/');
/*[IF JAVA_SPEC_VERSION >= 15]*/
		Class<?> clazz = this;
		if (isArray()) {
			clazz = getComponentType();
			while (clazz.isArray()) {
				clazz = clazz.getComponentType();
			}
		}
		if (clazz.isHidden()) {
			/* descriptor String of hidden class is something like: "Lpackage/ClassName.romaddress;". */
			int index = name.lastIndexOf('/');
			name = name.substring(0, index)+ '.' + name.substring(index + 1,name.length());
		}
/*[ENDIF] JAVA_SPEC_VERSION >= 15 */
		if (this.isArray()) {
		}
/*[IF INLINE-TYPES]*/
		else if (this.isPrimitiveClass()) {
			name = new StringBuilder(name.length() + 2).
				append('Q').append(name).append(';').toString();
		}
/*[ENDIF] INLINE-TYPES */
		else {
			name = new StringBuilder(name.length() + 2).
				append('L').append(name).append(';').toString();
		}
		return name;
	}
/*[ENDIF] JAVA_SPEC_VERSION >= 12 */

/*[IF JAVA_SPEC_VERSION >= 14]*/
	/**
	 * Returns true if the class instance is a record.
	 * 
	 * @return true for a record class, false otherwise
	 */
	public boolean isRecord() {
/*[IF JAVA_SPEC_VERSION >= 16]*/
		Class superclazz = getSuperclass();
		return ((superclazz == Record.class) && isRecordImpl());
/*[ELSE] JAVA_SPEC_VERSION >= 16*/
		return isRecordImpl();
/*[ENDIF] JAVA_SPEC_VERSION >= 16*/
	}
	
	private native boolean isRecordImpl();

	/**
	 * Returns an array of RecordComponent objects for a record class.
	 * 
	 * @return array of RecordComponent objects, one for each component in the record.
	 * For a class that is not a record, null is returned.
	 * For a record with no components an empty array is returned.
	 * 
	 * @throws SecurityException if declared member access or package access is not allowed
	 */
	@CallerSensitive
	public RecordComponent[] getRecordComponents() {
		@SuppressWarnings("removal")
		SecurityManager security = System.getSecurityManager();
		if (security != null) {
			ClassLoader callerClassLoader = ClassLoader.getStackClassLoader(1);
			checkMemberAccess(security, callerClassLoader, Member.DECLARED);
		}

		if (!isRecord()) {
			return null;
		}

		return getRecordComponentsImpl();
	}

	private native RecordComponent[] getRecordComponentsImpl();
/*[ENDIF] JAVA_SPEC_VERSION >= 14 */

/*[IF JAVA_SPEC_VERSION >= 15]*/
	/**
	 * Returns true if class or interface is sealed.
	 * 
	 * @return true if class is sealed, false otherwise
	 */
	public native boolean isSealed();

	private native boolean isHiddenImpl();
	/**
	 * Returns true if the class is a hidden class.
	 * 
	 * @return true for a hidden class, false otherwise
	 */
	public boolean isHidden() {
		return isHiddenImpl(); 
	}

	/**
	 * Returns the classData stored in the class.
	 * 
	 * @return the classData (Object).
	 */
	Object getClassData() {
		return classData;
	}

	private native String[] permittedSubclassesImpl();
/*[ENDIF] JAVA_SPEC_VERSION >= 15 */

/*[IF JAVA_SPEC_VERSION >= 16]*/
	/**
	 * Returns the permitted subclasses related to the calling sealed class as an array
	 * of Class objects. If the calling class is not a sealed class, is an array class,
	 * or is primitive, then this method returns null instead.
	 * The order of any classes returned in the array is unspecified, and any classes
	 * that cannot be loaded are not included in the returned array. The returned array
	 * may be empty if there are no permitted subclasses.
	 * 
	 * @return array of Class objects if permitted subclasses exist or null if not a sealed class.
	 * 
	 * @throws SecurityException if access to any of the classes returned in the array is denied
	 * 
	 * @since 16
	 */
	@CallerSensitive
	public Class<?>[] getPermittedSubclasses() throws SecurityException {
		if (!isSealed()) {
			return null;
		}
		Class<?>[] localPermittedSubclasses = cachedPermittedSubclasses;
		if (null == localPermittedSubclasses) {
			String[] permittedSubclassesNames = permittedSubclassesImpl();
			ArrayList<Class<?>> permittedSubclasses = new ArrayList<>(permittedSubclassesNames.length);

			for (int i = 0; i < permittedSubclassesNames.length; i++) {
				String subclassName = permittedSubclassesNames[i];
				try {
					Class<?> permitted = forNameImpl(subclassName, false, this.classLoader);
					permittedSubclasses.add(permitted);
				} catch (ClassNotFoundException e) {
					// do nothing if class not found
				}
			}
			localPermittedSubclasses = permittedSubclasses.toArray(new Class<?>[0]);

			long localPermittedSubclassesCacheOffset = cachedPermittedSubclassesOffset;
			if (-1 == localPermittedSubclassesCacheOffset) {
				localPermittedSubclassesCacheOffset = getUnsafe().objectFieldOffset(Class.class, "cachedPermittedSubclasses");
				cachedPermittedSubclassesOffset = localPermittedSubclassesCacheOffset;
			}
			getUnsafe().putObjectRelease(this, localPermittedSubclassesCacheOffset, localPermittedSubclasses);
		}

		@SuppressWarnings("removal")
		SecurityManager sm = System.getSecurityManager();
		if (null != sm) {
			HashSet<String> packages = new HashSet<>();
			ClassLoader callerClassLoader = ClassLoader.getCallerClassLoader();
			for (int i = 0; i < localPermittedSubclasses.length; i++) {
				ClassLoader subClassLoader = localPermittedSubclasses[i].internalGetClassLoader();
				if (sun.reflect.misc.ReflectUtil.needsPackageAccessCheck(callerClassLoader, subClassLoader)) {
					String pkgName = localPermittedSubclasses[i].getPackageName();
					if ((pkgName != null) && (pkgName != "")) {
						packages.add(pkgName);
					}
				}
			}
			for (String pkgName : packages) {
				sm.checkPackageAccess(pkgName);
			}
		}

		return localPermittedSubclasses;
	}
/*[ENDIF] JAVA_SPEC_VERSION >= 16 */
}
