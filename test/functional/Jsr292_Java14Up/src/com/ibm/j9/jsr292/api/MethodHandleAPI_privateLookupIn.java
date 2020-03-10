/*******************************************************************************
 * Copyright (c) 2017, 2018 IBM Corp. and others
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
package com.ibm.j9.jsr292.api;

import org.testng.annotations.Test;
import org.testng.Assert;
import java.lang.invoke.MethodHandles;
import java.lang.invoke.MethodHandles.Lookup;
import static java.lang.invoke.MethodHandles.privateLookupIn;
import mods.modulea.package1.ModuleA_Package1_Example1;
import mods.modulea.package1.ModuleA_Package1_Example2;
import mods.modulea.package2.ModuleA_Package2_Example1;
import mods.moduleb.package1.ModuleB_Package1_Example1;
import mods.modulec.package1.ModuleC_Package1_Example1;

public class MethodHandleAPI_privateLookupIn {
	final static Lookup callerLookup = ModuleA_Package1_Example1.getLookup();
	final static Lookup callerPublicLookup = ModuleA_Package1_Example1.getPublicLookup();
	final static Lookup callerLookupWithoutFullPrivilegeAccess = callerLookup.dropLookupMode(Lookup.MODULE);
	final static int FULL_ACCESS_MODE = Lookup.MODULE |Lookup.PUBLIC | Lookup.PRIVATE | Lookup.PROTECTED | Lookup.PACKAGE; //31
	final static int fullPrivilegeAccess = Lookup.MODULE | Lookup.PRIVATE;
	final static int NO_ACCESS = 0;
	
	/**
	 * privateLookupIn test for a null target class.
	 */
	@Test(expectedExceptions = NullPointerException.class, groups = { "level.sanity" })
	public static void test_privateLookupIn_NullTargetClass() throws Throwable {
		Lookup mhprivateLookupIn = privateLookupIn(null, callerLookup);
		Assert.fail("The test case failed to detect a null target class");
	}
	
	/**
	 * privateLookupIn test for a null caller lookup.
	 */
	@Test(expectedExceptions = NullPointerException.class, groups = { "level.sanity" })
	public static void test_privateLookupIn_NullcallerLookup() throws Throwable {
		Lookup mhprivateLookupIn = privateLookupIn(Object.class, null);
		Assert.fail("The test case failed to detect a null caller lookup");
	}
	
	/**
	 * privateLookupIn test for a target class with a primitive type.
	 */
	@Test(expectedExceptions = IllegalArgumentException.class, groups = { "level.sanity" })
	public static void test_privateLookupIn_TargetClass_PrimitiveType() throws Throwable {
		Lookup mhprivateLookupIn = privateLookupIn(int.class, callerLookup);
		Assert.fail("The test case failed to detect a target class with a primitive type");
	}
	
	/**
	 * privateLookupIn test for a target class with the array type.
	 */
	@Test(expectedExceptions = IllegalArgumentException.class, groups = { "level.sanity" })
	public static void test_privateLookupIn_TargetClass_ArrayType() throws Throwable {
		Lookup mhprivateLookupIn = privateLookupIn(Object[].class, callerLookup);
		Assert.fail("The test case failed to detect a target class with the array type");
	}
	
	/**
	 * privateLookupIn test for a named module containing the caller lookup
	 */
	@Test(expectedExceptions = IllegalAccessException.class, groups = { "level.sanity" })
	public static void test_privateLookupIn_CallerLookup_NamedModule() throws Throwable {
		Lookup mhprivateLookupIn = privateLookupIn(ModuleC_Package1_Example1.class, callerLookup);
		Assert.fail("The test case failed to detect that the module containing the target class is unreadable by"
				+ "the named module containing the caller lookup");
	}
	
	/**
	 * privateLookupIn test for an package (containing the target class) which is not opened to
	 * the module containing the caller lookup.
	 */
	@Test(expectedExceptions = IllegalAccessException.class, groups = { "level.sanity" })
	public static void test_privateLookupIn_TargetClass_UnOpenedPackage() throws Throwable {
		Lookup mhprivateLookupIn = privateLookupIn(Object.class, callerLookup);
		Assert.fail("The test case failed to detect the unopened package containing the target class");
	}
	
	/**
	 * privateLookupIn test for a caller lookup without the MODULE access mode
	 */
	@Test(groups = { "level.sanity" },expectedExceptions = IllegalAccessException.class)
	public static void test_privateLookupIn_Lookup_NoModuleMode() throws Throwable {
		Lookup mhprivateLookupIn = privateLookupIn(callerPublicLookup.lookupClass(), callerPublicLookup);
		Assert.fail("The test case failed to detect a caller lookup without the MODULE access mode");
	}
	
	/**
	 * privateLookupIn test for a target class which is the caller class in an unnamed module
	 */
	@Test(groups = { "level.sanity" })
	public static void test_privateLookupIn_TargetClass_Itself_UnnamedModule() throws Throwable {
		Lookup lookup = MethodHandles.lookup();
		Class<?> targetClass = lookup.lookupClass();
		Lookup mhprivateLookupIn = privateLookupIn(targetClass, lookup);
		Assert.assertEquals(mhprivateLookupIn.lookupClass(), targetClass);
		Assert.assertEquals(mhprivateLookupIn.lookupModes() & Lookup.PRIVATE, Lookup.PRIVATE);
	}
	
	/**
	 * privateLookupIn test for a target class which is the caller class in a named module
	 */
	@Test(groups = { "level.sanity" })
	public static void test_privateLookupIn_TargetClass_Itself_NamedModule() throws Throwable {
		Lookup mhprivateLookupIn = privateLookupIn(ModuleA_Package1_Example1.class, callerLookup);
		Assert.assertEquals(mhprivateLookupIn.lookupClass(), ModuleA_Package1_Example1.class);
		Assert.assertEquals(mhprivateLookupIn.lookupModes() & Lookup.PRIVATE, Lookup.PRIVATE);
	}
	
	/**
	 * privateLookupIn test for a target class and a caller lookup within the same package
	 */
	@Test(groups = { "level.sanity" })
	public static void test_privateLookupIn_TargetClass_CallerLookup_SamePackage_SameModule() throws Throwable {
		Lookup mhprivateLookupIn = privateLookupIn(ModuleA_Package1_Example2.class, callerLookup);
		Assert.assertEquals(mhprivateLookupIn.lookupClass(), ModuleA_Package1_Example2.class);
		Assert.assertEquals(mhprivateLookupIn.previousLookupClass(), null);
		Assert.assertEquals(mhprivateLookupIn.lookupModes(), FULL_ACCESS_MODE);
	}
	
	/**
	 * privateLookupIn test for a target class and a caller lookup from different packages in the same module
	 */
	@Test(groups = { "level.sanity" })
	public static void test_privateLookupIn_TargetClass_CallerLookup_DiffferentPackage_SameModule() throws Throwable {
		Lookup mhprivateLookupIn1 = privateLookupIn(ModuleA_Package2_Example1.class, callerLookup);
		Assert.assertEquals(mhprivateLookupIn1.lookupClass(), ModuleA_Package2_Example1.class);
		Assert.assertEquals(mhprivateLookupIn1.previousLookupClass(), null);
		Assert.assertEquals(mhprivateLookupIn1.lookupModes(), FULL_ACCESS_MODE);
	}
	
	/**
	 * privateLookupIn test for a target class and a caller lookup with the private access
	 * from different packages in the same module.
	 */
	@Test(groups = { "level.sanity" })
	public static void test_privateLookupIn_TargetClass_PrivateCallerLookup_DiffferentPackage_SameModule() throws Throwable {
		Lookup mhprivateLookupIn1 = privateLookupIn(ModuleA_Package2_Example1.class, callerLookup);
		Lookup mhprivateLookupIn2 = privateLookupIn(ModuleA_Package1_Example1.class, mhprivateLookupIn1);
		Assert.assertEquals(mhprivateLookupIn2.lookupClass(), ModuleA_Package1_Example1.class);
		Assert.assertEquals(mhprivateLookupIn2.previousLookupClass(), null);
		Assert.assertEquals(mhprivateLookupIn2.lookupModes(), FULL_ACCESS_MODE);
	}
	
	/**
	 * privateLookupIn test for a target class from a different module and a caller lookup with the private access
	 * from the same module as the original caller lookup.
	 */
	@Test(groups = { "level.sanity" })
	public static void test_privateLookupIn_TargetClass_DifferentModule_PrivateCallerLookup_SameModule() throws Throwable {
		int expectedAccessModes = callerLookup.lookupModes() & ~Lookup.MODULE;
		Lookup mhprivateLookupIn1 = privateLookupIn(ModuleA_Package2_Example1.class, callerLookup);
		Lookup mhprivateLookupIn2 = privateLookupIn(ModuleB_Package1_Example1.class, mhprivateLookupIn1);
		Assert.assertEquals(mhprivateLookupIn2.lookupClass(), ModuleB_Package1_Example1.class);
		Assert.assertEquals(mhprivateLookupIn2.previousLookupClass(), mhprivateLookupIn1.lookupClass());
		Assert.assertEquals(mhprivateLookupIn2.lookupModes(), expectedAccessModes);
		Assert.assertFalse(mhprivateLookupIn2.hasFullPrivilegeAccess());
	}
	
	/**
	 * privateLookupIn test for a target class and a caller lookup from different modules
	 */
	@Test(groups = { "level.sanity" })
	public static void test_privateLookupIn_TargetClass_CallerLookup_DifferentModule() throws Throwable {
		int expectedAccessModes = callerLookup.lookupModes() & ~Lookup.MODULE;
		Lookup mhprivateLookupIn = privateLookupIn(ModuleB_Package1_Example1.class, callerLookup);
		Assert.assertEquals(mhprivateLookupIn.lookupClass(), ModuleB_Package1_Example1.class);
		Assert.assertEquals(mhprivateLookupIn.previousLookupClass(), callerLookup.lookupClass());
		Assert.assertEquals(mhprivateLookupIn.lookupModes(), expectedAccessModes);
		Assert.assertFalse(mhprivateLookupIn.hasFullPrivilegeAccess());
	}
	
	/**
	 * privateLookupIn test for the original caller lookup's class as the target class
	 * and a caller lookup with the private access from different modules
	 */
	@Test(groups = { "level.sanity" }, expectedExceptions = IllegalAccessException.class)
	public static void test_privateLookupIn_CallerLookupClass_PrivateCallerLookup_DifferentModule() throws Throwable {
		Lookup mhprivateLookupIn1 = privateLookupIn(ModuleB_Package1_Example1.class, callerLookup);
		Lookup mhprivateLookupIn2 = privateLookupIn(ModuleA_Package1_Example1.class, mhprivateLookupIn1);
	}
	
	/**
	 * privateLookupIn test for a target class and a caller lookup without full privilege access
	 */
	@Test(groups = { "level.sanity" }, expectedExceptions = IllegalAccessException.class)
	public static void test_privateLookupIn_TargetClass_CallerLookup_NoFullPrivilegeAccess() throws Throwable {
		Assert.assertEquals(callerLookupWithoutFullPrivilegeAccess.lookupModes() & fullPrivilegeAccess, 0);
		Lookup mhprivateLookupIn = privateLookupIn(ModuleA_Package2_Example1.class, callerLookupWithoutFullPrivilegeAccess);
		Assert.fail("The test case failed to detect a caller lookup without full privilege access");
	}
}
