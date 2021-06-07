/*******************************************************************************
 * Copyright (c) 2001, 2015 IBM Corp. and others
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

import java.util.ArrayList;
import java.util.HashMap;
import java.util.StringTokenizer;

import com.ibm.j9ddr.CorruptDataException;
import com.ibm.j9ddr.vm29.pointer.generated.J9BuildFlags;
import com.ibm.j9ddr.vm29.pointer.generated.J9JavaVMPointer;

import static com.ibm.j9ddr.vm29.tools.ddrinteractive.gccheck.CheckBase.*;

@SuppressWarnings("unchecked")
class CheckCycle
{
	private static final String[] checkNames;
	private static final Class<Check>[] checkClasses;

//	private J9JavaVMPointer _javaVM;
	private int _checkFlags;
	private int _miscFlags;
	private int _errorCount;
//	GCCheckInvokedBy _invokedBy;
//	UDATA _manualCheckInvocation;
	private Check[] _checks;
//	J9JavaVM *_javaVM;
//	J9PortLibrary *_portLibrary;
	private CheckEngine _engine;
	private boolean _printHelp;
	
	static 
	{
		ArrayList<String> names = new ArrayList<String>();
		ArrayList<Class<?>> classes = new ArrayList<Class<?>>();
	
		names.add("objectheap");
		classes.add(CheckObjectHeap.class);

		names.add("classheap");
		classes.add(CheckClassHeap.class);
		
		if(J9BuildFlags.gc_generational) {
			names.add("rememberedset");
			classes.add(CheckRememberedSet.class);			
		}
		
		if(J9BuildFlags.gc_finalization) {
			names.add("unfinalized");
			classes.add(CheckUnfinalizedList.class);
			
			names.add("finalizable");
			classes.add(CheckFinalizableList.class);
		}

		names.add("ownablesynchronizer");
		classes.add(CheckOwnableSynchronizerList.class);
		
		names.add("stringtable");
		classes.add(CheckStringTable.class);

		names.add("classloaders");
		classes.add(CheckClassLoaders.class);

		names.add("jniglobalrefs");
		classes.add(CheckJNIGlobalReferences.class);

		names.add("jniweakglobalrefs");
		classes.add(CheckJNIWeakGlobalReferences.class);

		if(J9BuildFlags.opt_jvmti) {
			names.add("jvmtiobjecttagtables");
			classes.add(CheckJVMTIObjectTagTables.class);			
		}
				
		names.add("vmclassslots");
		classes.add(CheckVMClassSlots.class);

		names.add("monitortable");
		classes.add(CheckMonitorTable.class);	
				
		names.add("vmthreads");
		classes.add(CheckVMThreads.class);

		names.add("threadstacks");
		classes.add(CheckVMThreadStacks.class);

		checkNames = names.toArray(new String[names.size()]);
		checkClasses = classes.toArray(new Class[classes.size()]);
	}
	
	public CheckCycle(J9JavaVMPointer javaVM, CheckEngine engine, String options)
	{
//		_javaVM = javaVM;
//		_errorCount = 0;
		_checks = null;
		_engine = engine;
		initialize(options);
	}
	
	private void printHelp()
	{
		CheckReporter reporter = _engine.getReporter();
		reporter.println("GC Check for J9, Version 2.7");
		reporter.println();
		reporter.println("Usage: -Xcheck:gc[:scanOption,...][:verifyOption,...][:miscOption,...]");

		reporter.println("scan options (default is all):");
		reporter.println("  all");
		reporter.println("  none");
		for (int i = 0; i < checkNames.length; i++) {
			reporter.println("  [no]" + checkNames[i]);
		}
		reporter.println("  heap");
		reporter.println("  help");
		reporter.println();

		reporter.println("verify options (default is all):");
		reporter.println("  all");
		reporter.println("  none");
		reporter.println("  classslot");
		reporter.println("  range");
		reporter.println("  flags");		
		reporter.println();
		
		reporter.println("misc options (default is verbose,check):");
		reporter.println("  verbose");
		reporter.println("  quiet");
		reporter.println("  [no]scan");
		reporter.println("  [no]check");
		reporter.println("  maxErrors=X");
		reporter.println("  darkmatter");
		reporter.println("  midscavenge");
		reporter.println("  scavengerbackout");
		reporter.println("  ownablesynchronizerconsistency");
		reporter.println();
	}

	private void initialize(String options)
	{
		HashMap<String, Boolean> checksToRun = new HashMap<String, Boolean>();
		ArrayList<ArrayList<String>> separatedOptions = new ArrayList<ArrayList<String>>();
		ArrayList<Check> checks = new ArrayList<Check>();
		int checkFlags = 0;
		int miscFlags = J9MODRON_GCCHK_VERBOSE | J9MODRON_GCCHK_MISC_CHECK;
		
		for(int i = 0; i < checkNames.length; i++) {
			checksToRun.put(checkNames[i], false);
		}
		
		if(options.equalsIgnoreCase("help")) {
			_printHelp = true;
		} else {
	
			StringTokenizer colonSeparated = new StringTokenizer(options, ":");
			while(colonSeparated.hasMoreTokens()) {
				StringTokenizer commaSeparated = new StringTokenizer(colonSeparated.nextToken(), ",");
				ArrayList<String> separated = new ArrayList<String>();
				while(commaSeparated.hasMoreTokens()) {
					separated.add(commaSeparated.nextToken());
				}
				separatedOptions.add(separated);
			}
	
			if(separatedOptions.size() > 0) {	
				// scan options
				ArrayList<String> scanOptions = separatedOptions.get(0);
				if(scanOptions.size() == 0) {
					/* Set defaults if user did not specify */
					for(int i = 0; i < checkNames.length; i++) {
						checksToRun.put(checkNames[i], true);
					}
				} else {
					for (String scanOption : scanOptions) {
						if(scanOption.equals("all")) {
							for(int i = 0; i < checkNames.length; i++) {
								checksToRun.put(checkNames[i], true);
							}
							continue;
						}
						
						if(scanOption.equals("none")) {
							for(int i = 0; i < checkNames.length; i++) {
								checksToRun.put(checkNames[i], false);
							}
							continue;
						}
						
						/* search for a supported check */
						if(checksToRun.containsKey(scanOption)) {
							checksToRun.put(scanOption, true);
							continue;
						}
						
						/* now do the special compound options (that affect more than one check) */
						if(scanOption.equals("heap")) {
							checksToRun.put("objectheap", true);
							checksToRun.put("classheap", true);
							continue;
						}
						
						/* Enhanced functionality: support no* for all known checks */
						if(scanOption.startsWith("no")) {
							String disableOption = scanOption.substring(2);
							if(checksToRun.containsKey(disableOption)) {
								checksToRun.put(disableOption, false);
								continue;
							}
							
							if(disableOption.equals("heap")) {
								checksToRun.put("objectheap", false);
								checksToRun.put("classheap", false);
								continue;
							}
						}
		
						_printHelp = true;
						_engine.getReporter().println("GC Check: unrecognized option '" + scanOption + "'");
					}
				}
			} else {
				/* Set defaults if user did not specify */
				for(int i = 0; i < checkNames.length; i++) {
					checksToRun.put(checkNames[i], true);
				}
			}
			
			if(separatedOptions.size() > 1) {
				ArrayList<String> checkOptions = separatedOptions.get(1);
				if(checkOptions.size() == 0) {
					checkFlags = J9MODRON_GCCHK_VERIFY_ALL;
				} else {
					for (String checkOption : checkOptions) {
						if(checkOption.equals("all")) {
							checkFlags |= J9MODRON_GCCHK_VERIFY_ALL;
							continue;
						}
						
						if(checkOption.equals("none")) {
							checkFlags |= J9MODRON_GCCHK_VERIFY_ALL;
							continue;
						}
		
						if(checkOption.equals("all")) {
							checkFlags &= ~J9MODRON_GCCHK_VERIFY_ALL;
							continue;
						}
		
						if(checkOption.equals("classslot")) {
							checkFlags |= J9MODRON_GCCHK_VERIFY_CLASS_SLOT;
							continue;
						}
		
						if(checkOption.equals("range")) {
							checkFlags |= J9MODRON_GCCHK_VERIFY_RANGE;
							continue;
						}
		
						if(checkOption.equals("flags")) {
							checkFlags |= J9MODRON_GCCHK_VERIFY_FLAGS;
							continue;
						}
						
						_printHelp = true;
						_engine.getReporter().println("GC Check: unrecognized option '" + checkOption + "'");
					}
				}
			} else {
				checkFlags = J9MODRON_GCCHK_VERIFY_ALL;
			}
			
			if(separatedOptions.size() > 2) {
				for (String miscOption : separatedOptions.get(2)) {
					if(miscOption.equals("verbose")) {
						miscFlags |= J9MODRON_GCCHK_VERBOSE;
						continue;
					}
	
					if(miscOption.equals("manual")) {
						miscFlags |= J9MODRON_GCCHK_MANUAL;
						continue;
					}
	
					if(miscOption.equals("quiet")) {
						miscFlags &= ~J9MODRON_GCCHK_VERBOSE;
						miscFlags |= J9MODRON_GCCHK_MISC_QUIET;
						continue;
					}
	
					if(miscOption.equals("scan")) {
						miscFlags |= J9MODRON_GCCHK_MISC_SCAN;
						continue;
					}
	
					if(miscOption.equals("noscan")) {
						miscFlags &= ~J9MODRON_GCCHK_MISC_SCAN;
						continue;
					}
	
					if(miscOption.equals("check")) {
						miscFlags |= J9MODRON_GCCHK_MISC_CHECK;
						continue;
					}
	
					if(miscOption.equals("nocheck")) {
						miscFlags &= ~J9MODRON_GCCHK_MISC_CHECK;
						continue;
					}
	
					if(miscOption.startsWith("maxerrors=")) {
						int max = Integer.parseInt(miscOption.substring("maxerrors=".length()));
						_engine.setMaxErrorsToReport(max);
						continue;
					}
	
					if(miscOption.equals("darkmatter")) {
						miscFlags |= J9MODRON_GCCHK_MISC_DARKMATTER;
						continue;
					}
	
					if(J9BuildFlags.gc_modronScavenger || J9BuildFlags.gc_vlhgc) {
						if(miscOption.equals("midscavenge")) {
							miscFlags |= J9MODRON_GCCHK_MISC_MIDSCAVENGE;
							continue;
						}

						if(miscOption.equals("indexabledataaddress")) {
							miscFlags |= J9MODRON_GCCHK_VALID_INDEXABLE_DATA_ADDRESS;
							continue;
						}
					}	
	
					/* Most of these options are specific to a running process */					
					if(miscOption.equals("abort")) {
						miscFlags |= J9MODRON_GCCHK_MISC_ABORT;
						continue;
					}
	
					if(miscOption.equals("noabort")) {
						miscFlags &= ~J9MODRON_GCCHK_MISC_ABORT;
						continue;
					}
					
					if(miscOption.equals("dumpstack")) {
						miscFlags |= J9MODRON_GCCHK_MISC_ALWAYS_DUMP_STACK;
						continue;
					}
	
					if(miscOption.equals("nodumpstack")) {
						miscFlags &= ~J9MODRON_GCCHK_MISC_ALWAYS_DUMP_STACK;
						continue;
					}
	
					if(miscOption.startsWith("interval=")) {
						//scan_udata(&scan_start, &extensions->gcInterval);
						miscFlags |= J9MODRON_GCCHK_INTERVAL;
						continue;
					}
					
					if(J9BuildFlags.gc_modronScavenger) {
						if(miscOption.startsWith("localinterval=")) {
							//scan_udata(&scan_start, &extensions->localGcInterval);
							miscFlags |= J9MODRON_GCCHK_LOCAL_INTERVAL;
							continue;
						}
					}
	
					if(miscOption.startsWith("globalinterval=")) {
						//scan_udata(&scan_start, &extensions->globalGcInterval);
						miscFlags |= J9MODRON_GCCHK_GLOBAL_INTERVAL;
						continue;
					}
	
					if(miscOption.startsWith("startindex=")) {
						//scan_udata(&scan_start, &extensions->gcStartIndex);
						miscFlags |= J9MODRON_GCCHK_START_INDEX;
						continue;
					}
	
					if(J9BuildFlags.gc_modronScavenger) {
						if(miscOption.equals("scavengerbackout")) {
							miscFlags |= J9MODRON_GCCHK_SCAVENGER_BACKOUT;
							continue;
						}
		
						if(miscOption.equals("suppresslocal")) {
							miscFlags |= J9MODRON_GCCHK_SUPPRESS_LOCAL;
							continue;
						}
					}
	
					if(miscOption.equals("suppressglobal")) {
						miscFlags |= J9MODRON_GCCHK_SUPPRESS_GLOBAL;
						continue;
					}
	
					if(J9BuildFlags.gc_generational) {
						if(miscOption.equals("rememberedsetoverflow")) {
							miscFlags |= J9MODRON_GCCHK_REMEMBEREDSET_OVERFLOW;
							continue;
						}
					}

					if(miscOption.equals("ownablesynchronizerconsistency")) {
						/* try to match the count of ownableSynchronizerObjects on Heap and on the lists */
						miscFlags |= J9MODRON_GCCHK_MISC_OWNABLESYNCHRONIZER_CONSISTENCY;
						continue;
					}
					
					_printHelp = true;
					_engine.getReporter().println("GC Check: unrecognized option '" + miscOption + "'");
				}
			}
		}
		
		for(int i = 0; i < checkNames.length; i++) {
			if(checksToRun.get(checkNames[i])) {
				try {
					Check check = checkClasses[i].newInstance();
					check.initialize(_engine);
					checks.add(check);
				} catch (IllegalAccessException e) {
					e.printStackTrace();
				} catch (InstantiationException e) {
					e.printStackTrace();
				}
			}
		}
		
		_checks = checks.toArray(new Check[checks.size()]);
		_checkFlags = checkFlags;
		_miscFlags = miscFlags;
		
		if (checksToRun.get("objectheap")) {
			/* initialize OwnableSynchronizerCount On Object Heap for ownableSynchronizer consistency check */
			_engine.initializeOwnableSynchronizerCountOnHeap();
		}
		if (checksToRun.get("ownablesynchronizer")) {
			/* initialize OwnableSynchronizerCount On Lists for ownableSynchronizer consistency check */
			_engine.initializeOwnableSynchronizerCountOnList();
		}
		
	}

	public void run() throws CorruptDataException
	{
		if(_printHelp) {
			printHelp();
		} else {
			_engine.startCheckCycle(this);
			for (int i = 0; i < _checks.length; i++) {
				boolean check = (J9MODRON_GCCHK_MISC_CHECK == (_miscFlags & J9MODRON_GCCHK_MISC_CHECK));
				boolean scan = (J9MODRON_GCCHK_MISC_SCAN == (_miscFlags & J9MODRON_GCCHK_MISC_SCAN));
				_checks[i].run(check, scan);
			}
			_engine.endCheckCycle();
		}
	}

	public int nextErrorCount()
	{
		return ++_errorCount;
	}

	public int getCheckFlags()
	{
		return _checkFlags;
	}

	public int getMiscFlags()
	{
		return _miscFlags;
	}
}
