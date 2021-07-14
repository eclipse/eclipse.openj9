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

#if !defined(VERBOSEHANDLEROUTPUTVLHGC_HPP_)
#define VERBOSEHANDLEROUTPUTVLHGC_HPP_

#include "j9.h"
#include "j9cfg.h"

#include "VerboseHandlerOutput.hpp"

#include "GCExtensions.hpp"

class MM_EnvironmentBase;
class MM_InterRegionRememberedSetStats;
class MM_MarkVLHGCStats;
class MM_ReferenceStats;
class MM_WorkPacketStats;

class MM_VerboseHandlerOutputVLHGC : public MM_VerboseHandlerOutput
{
private:
	
protected:
	J9HookInterface** _mmHooks;  /**< Pointers to the Hook interface */

public:

private:
	/**
	 * Output unfinalized processing summary.
	 * @param env GC thread used for output.
	 * @param indent base level of indentation for the summary.
	 * @param unfinalizedCandidates number of unfinalized candidates encountered.
	 * @param unfinalizedCount number of candidates that transitioned from unfinalized.
	 */
	void outputUnfinalizedInfo(MM_EnvironmentBase *env, UDATA indent, UDATA unfinalizedCandidates, UDATA unfinalizedCount);

	/**
	 * Output unfinalized processing summary.
	 * @param env GC thread used for output.
	 * @param indent base level of indentation for the summary.
	 * @param ownableSynchronizerCandidates number of ownable synchronizer candidates encountered.
	 * @param ownableSynchronizerCleared number of ownable synchronizer candidates cleared.
	 */
	void outputOwnableSynchronizerInfo(MM_EnvironmentBase *env, UDATA indent, UDATA ownableSynchronizerCandidates, UDATA ownableSynchronizerCleared);

	/**
	 * Output reference processing summary.
	 * @param env GC thread used for output.
	 * @param indent base level of indentation for the summary.
	 * @param referenceType character string representation of the reference type.
	 * @param referenceStats summary stats data of the processing.
	 * @param dynamicThreshold dynamic threshold value for reference
	 * @param maxThreshold maximum threshold value for reference. If it equal 0 thresholds should not be included to output line
	 */
	void outputReferenceInfo(MM_EnvironmentBase *env, UDATA indent, const char *referenceType, MM_ReferenceStats *referenceStats, UDATA dynamicThreshold, UDATA maxThreshold);

	/**
	 * Output a mark gc operation summary stanza.
	 * @param env GC thread performing output.
	 * @param markType String name representation of the mark type.
	 * @param markStats mark stats used for summary.
	 * @param workPacketStats work packet stats used for summary.
	 */
	void outputMarkSummary(MM_EnvironmentBase *env, const char *markType, MM_MarkVLHGCStats *markStats, MM_WorkPacketStats *workPacketStats, MM_InterRegionRememberedSetStats *irrsStats);

	/**
	 * Output info on remembered set clearing (from-CS-region clearing pass)
	 * @param env GC thread performing output.
	 * @param irrsStats Intra region remembered set stats.
	 */
	void outputRememberedSetClearedInfo(MM_EnvironmentBase *env, MM_InterRegionRememberedSetStats *irrsStats);


protected:
	virtual void outputInitializedInnerStanza(MM_EnvironmentBase *env, MM_VerboseBuffer *buffer);

	/**
	 * @returns true if memory-info staza is multiline. Caller than format first and last line accordingly.
	 */
	virtual bool hasOutputMemoryInfoInnerStanza();
	/**
	 * The actual body (excluding first and last line) of memory-info stanza.
	 */
	virtual void outputMemoryInfoInnerStanza(MM_EnvironmentBase *env, UDATA indent, MM_CollectionStatistics *stats);

	virtual const char *getSubSpaceType(uintptr_t typeFlags);
	
	/* Print out allocations statistics
	 * @param current Env
	 */
	virtual void printAllocationStats(MM_EnvironmentBase* env);
	
	/**
	 * Answer a string representation of a given cycle type.
	 * @param[IN] cycle type
	 * @return string representing the human readable "type" of the cycle.
	 */	
	virtual const char *getCycleType(UDATA type);

	virtual bool initialize(MM_EnvironmentBase *env, MM_VerboseManager *manager);
	virtual void tearDown(MM_EnvironmentBase *env);

	virtual bool getThreadName(char *buf, UDATA bufLen, OMR_VMThread *vmThread);
	virtual void writeVmArgs(MM_EnvironmentBase* env, MM_VerboseBuffer* buffer);

	MM_VerboseHandlerOutputVLHGC(MM_GCExtensions *extensions)
		: MM_VerboseHandlerOutput(extensions)
		, _mmHooks(NULL)
	{};

public:
	static MM_VerboseHandlerOutput *newInstance(MM_EnvironmentBase *env, MM_VerboseManager *manager);

	/**
	 * Write the verbose stanza allocation taxation trigger point (preceding PGCs and GMP increments).
	 * @param hook Hook interface used by the JVM.
	 * @param eventNum The hook event number.
	 * @param eventData hook specific event data.
	 */
	void handleTaxationEntryPoint(J9HookInterface** hook, UDATA eventNum, void* eventData);

	/**
	 * Write the verbose stanza for the copy forward start event.
	 * @param hook Hook interface used by the JVM.
	 * @param eventNum The hook event number.
	 * @param eventData hook specific event data.
	 */
	void handleCopyForwardStart(J9HookInterface** hook, UDATA eventNum, void* eventData);

	/**
	 * Write the verbose stanza for the copy forward end event.
	 * @param hook Hook interface used by the JVM.
	 * @param eventNum The hook event number.
	 * @param eventData hook specific event data.
	 */
	void handleCopyForwardEnd(J9HookInterface** hook, UDATA eventNum, void* eventData);
	
	virtual	void handleConcurrentStartInternal(J9HookInterface** hook, UDATA eventNum, void* eventData);
	virtual void handleConcurrentEndInternal(J9HookInterface** hook, UDATA eventNum, void* eventData);
	virtual const char *getConcurrentTypeString(uintptr_t type) { return "GMP work packet processing"; }

	/**
	 * Write the verbose stanza for the GMP mark start event.
	 * @param hook Hook interface used by the JVM.
	 * @param eventNum The hook event number.
	 * @param eventData hook specific event data.
	 */
	void handleGMPMarkStart(J9HookInterface** hook, UDATA eventNum, void* eventData);

	/**
	 * Write the verbose stanza for the GMP mark end event.
	 * @param hook Hook interface used by the JVM.
	 * @param eventNum The hook event number.
	 * @param eventData hook specific event data.
	 */
	void handleGMPMarkEnd(J9HookInterface** hook, UDATA eventNum, void* eventData);

	/**
	 * Write the verbose stanza for the global gc mark start event.
	 * @param hook Hook interface used by the JVM.
	 * @param eventNum The hook event number.
	 * @param eventData hook specific event data.
	 */
	void handleGlobalGCMarkStart(J9HookInterface** hook, UDATA eventNum, void* eventData);

	/**
	 * Write the verbose stanza for the global gc mark end event.
	 * @param hook Hook interface used by the JVM.
	 * @param eventNum The hook event number.
	 * @param eventData hook specific event data.
	 */
	void handleGlobalGCMarkEnd(J9HookInterface** hook, UDATA eventNum, void* eventData);

	/**
	 * Write the verbose stanza for the PGC mark start event.
	 * @param hook Hook interface used by the JVM.
	 * @param eventNum The hook event number.
	 * @param eventData hook specific event data.
	 */
	void handlePGCMarkStart(J9HookInterface** hook, UDATA eventNum, void* eventData);

	/**
	 * Write the verbose stanza for the PGC mark end event.
	 * @param hook Hook interface used by the JVM.
	 * @param eventNum The hook event number.
	 * @param eventData hook specific event data.
	 */
	void handlePGCMarkEnd(J9HookInterface** hook, UDATA eventNum, void* eventData);

	/**
	 * Write the verbose stanza for the reclaim sweep start event.
	 * @param hook Hook interface used by the JVM.
	 * @param eventNum The hook event number.
	 * @param eventData hook specific event data.
	 */
	void handleReclaimSweepStart(J9HookInterface** hook, UDATA eventNum, void* eventData);

	/**
	 * Write the verbose stanza for the reclaim sweep end event.
	 * @param hook Hook interface used by the JVM.
	 * @param eventNum The hook event number.
	 * @param eventData hook specific event data.
	 */
	void handleReclaimSweepEnd(J9HookInterface** hook, UDATA eventNum, void* eventData);

	/**
	 * Write the verbose stanza for the reclaim compact start event.
	 * @param hook Hook interface used by the JVM.
	 * @param eventNum The hook event number.
	 * @param eventData hook specific event data.
	 */
	void handleReclaimCompactStart(J9HookInterface** hook, UDATA eventNum, void* eventData);

	/**
	 * Write the verbose stanza for the reclaim compact end event.
	 * @param hook Hook interface used by the JVM.
	 * @param eventNum The hook event number.
	 * @param eventData hook specific event data.
	 */
	void handleReclaimCompactEnd(J9HookInterface** hook, UDATA eventNum, void* eventData);

	/**
	 * Write verbose stanza for a class unload end event.
	 * @param hook Hook interface used by the JVM.
	 * @param eventNum The hook event number.
	 * @param eventData hook specific event data.
	 */
	void handleClassUnloadEnd(J9HookInterface** hook, UDATA eventNum, void* eventData);

	virtual void enableVerbose();
	virtual void disableVerbose();

	/**
	 * Return termination reason for concurrent collection.
	 * @param stats concurrent stats
	 * @return string representing the reason for termination
	 */ 
	const char *getConcurrentTerminationReason(MM_ConcurrentPhaseStatsBase *stats);
};

#endif /* VERBOSEHANDLEROUTPUTVLHGC_HPP_ */
