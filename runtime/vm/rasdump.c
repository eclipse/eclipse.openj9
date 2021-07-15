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

#include <string.h>

#include "j9protos.h"
#include "vm_internal.h"
#include "j9vmnls.h"
#include "portsock.h"
#include "j2sever.h"
#include "omrlinkedlist.h"
#include "j9version.h"

#if defined(J9ZOS390)
#include "atoe.h"
#endif

#if defined(WIN32) || defined(WIN64)
#include <WinBase.h>
#endif /* defined(WIN32) || defined(WIN64) */

#if defined(OSX)
#include <crt_externs.h>
#endif /* defined(OSX) */

#define PRIMORDIAL_DUMP_ATTACHED_THREAD 0x01

#define RAS_NETWORK_WARNING_TIME 60000

#if (defined(J9VM_RAS_DUMP_AGENTS)) 
static omr_error_t primordialTriggerDumpAgents (struct J9JavaVM *vm, struct J9VMThread *self, UDATA eventFlags, struct J9RASdumpEventData *eventData);
static omr_error_t primordialSeekDumpAgent (struct J9JavaVM *vm, struct J9RASdumpAgent **agentPtr, J9RASdumpFn dumpFn);
static omr_error_t primordialInsertDumpAgent (struct J9JavaVM *vm, struct J9RASdumpAgent *agent);
static omr_error_t primordialTriggerOneOffDump(struct J9JavaVM *vm, char *optionString, char *caller, char *fileName, size_t fileNameLength);
static omr_error_t primordialSetDumpOption (struct J9JavaVM *vm, char *optionString);
static omr_error_t primordialResetDumpOption (struct J9JavaVM *vm);
static omr_error_t primordialRemoveDumpAgent (struct J9JavaVM *vm, struct J9RASdumpAgent *agent);
static omr_error_t primordialQueryVmDump(struct J9JavaVM *vm, int buffer_size, void* options_buffer, int* data_size);
#endif /* J9VM_RAS_DUMP_AGENTS */

#if defined(J9VM_RAS_EYECATCHERS)
void J9RASInitialize (J9JavaVM* javaVM);
void J9RASShutdown (J9JavaVM* javaVM);
void J9RASCheckDump(J9JavaVM* javaVM);
void populateRASNetData (J9JavaVM *javaVM, J9RAS *rasStruct);
static J9RAS* allocateRASStruct(J9JavaVM *javaVM);

JNIEXPORT struct {
	union {
		double align;
		struct J9RAS ras;
	} j9ras;
} _j9ras_;

typedef struct J9AllocatedRAS {
	struct J9RAS ras;
	struct J9PortVmemIdentifier vmemid;
} J9AllocatedRAS;

#endif /* J9VM_RAS_EYECATCHERS */

#ifdef J9VM_RAS_DUMP_AGENTS

/* Basic dump facade */
static const J9RASdumpFunctions
primordialDumpFacade = {
	NULL,
	primordialTriggerOneOffDump,
	primordialInsertDumpAgent,
	primordialRemoveDumpAgent,
	primordialSeekDumpAgent,
	primordialTriggerDumpAgents,
	primordialSetDumpOption,
	primordialResetDumpOption,
	primordialQueryVmDump
};

#endif /* J9VM_RAS_DUMP_AGENTS */

#if (defined(J9VM_RAS_DUMP_AGENTS)) 
static omr_error_t
primordialTriggerOneOffDump(struct J9JavaVM *vm, char *optionString, char *caller, char *fileName, size_t fileNameLength)
{
	PORT_ACCESS_FROM_JAVAVM(vm);

	if (strcmp(optionString, "console") == 0 ) {
		printThreadInfo(vm, currentVMThread(vm), NULL, TRUE);
	} else {
		j9nls_printf(PORTLIB, J9NLS_WARNING, J9NLS_VM_MISSING_DUMP_DLL_TRIGGERONEOFFDUMP, optionString, J9_RAS_DUMP_DLL_NAME);
	}

	return OMR_ERROR_INTERNAL;
}
#endif /* J9VM_RAS_DUMP_AGENTS */


#if (defined(J9VM_RAS_DUMP_AGENTS)) 
static omr_error_t
primordialInsertDumpAgent(struct J9JavaVM *vm, struct J9RASdumpAgent *agent)
{
	PORT_ACCESS_FROM_JAVAVM(vm);

	j9nls_printf(PORTLIB, J9NLS_WARNING, J9NLS_VM_MISSING_DUMP_DLL_INSERTDUMPAGENT, J9_RAS_DUMP_DLL_NAME);

	return OMR_ERROR_INTERNAL;
}
#endif /* J9VM_RAS_DUMP_AGENTS */


#if (defined(J9VM_RAS_DUMP_AGENTS)) 
static omr_error_t
primordialRemoveDumpAgent(struct J9JavaVM *vm, struct J9RASdumpAgent *agent)
{
	PORT_ACCESS_FROM_JAVAVM(vm);

	j9nls_printf(PORTLIB, J9NLS_WARNING, J9NLS_VM_MISSING_DUMP_DLL_REMOVEDUMPAGENT, J9_RAS_DUMP_DLL_NAME);

	return OMR_ERROR_INTERNAL;
}
#endif /* J9VM_RAS_DUMP_AGENTS */


#if (defined(J9VM_RAS_DUMP_AGENTS)) 
static omr_error_t
primordialSeekDumpAgent(struct J9JavaVM *vm, struct J9RASdumpAgent **agentPtr, J9RASdumpFn dumpFn)
{
	PORT_ACCESS_FROM_JAVAVM(vm);

	j9nls_printf(PORTLIB, J9NLS_WARNING, J9NLS_VM_MISSING_DUMP_DLL_SEEKDUMPAGENT, J9_RAS_DUMP_DLL_NAME);

	return OMR_ERROR_INTERNAL;
}
#endif /* J9VM_RAS_DUMP_AGENTS */


#if (defined(J9VM_RAS_DUMP_AGENTS)) 
static omr_error_t
primordialTriggerDumpAgents(struct J9JavaVM *vm, struct J9VMThread *self, UDATA eventFlags, struct J9RASdumpEventData *eventData)
{
	UDATA state = 0;
	 
	if (!self){
		/* ensure that this thread is attached to the VM */
		JavaVMAttachArgs attachArgs;

		attachArgs.version = JNI_VERSION_1_2;
		attachArgs.name = "Triggered DumpAgent Thread";
		attachArgs.group = NULL;

		vm->internalVMFunctions->AttachCurrentThreadAsDaemon((JavaVM *)vm, (void **)&self, &attachArgs);
		state = PRIMORDIAL_DUMP_ATTACHED_THREAD;
	}
	
	if ( (eventFlags & J9RAS_DUMP_ON_GP_FAULT) ) {
		gpThreadDump( vm, self );
	} else if ( (eventFlags & J9RAS_DUMP_ON_USER_SIGNAL) ) {
		printThreadInfo(vm, self, NULL, TRUE);
	}
	
	if (state & PRIMORDIAL_DUMP_ATTACHED_THREAD) {
		/* if the thread was attached in this function, detach it */
		(*((JavaVM *)vm))->DetachCurrentThread((JavaVM *)vm);
	}

	return OMR_ERROR_NONE;
}
#endif /* J9VM_RAS_DUMP_AGENTS */


#if (defined(J9VM_RAS_DUMP_AGENTS)) 
static omr_error_t
primordialSetDumpOption(struct J9JavaVM *vm, char *optionString)
{
	PORT_ACCESS_FROM_JAVAVM(vm);

	j9nls_printf(PORTLIB, J9NLS_WARNING, J9NLS_VM_MISSING_DUMP_DLL_SETDUMPOPTION, J9_RAS_DUMP_DLL_NAME);

	return OMR_ERROR_INTERNAL;
}

static omr_error_t
primordialResetDumpOption(struct J9JavaVM *vm)
{
	PORT_ACCESS_FROM_JAVAVM(vm);

	j9nls_printf(PORTLIB, J9NLS_WARNING, J9NLS_VM_MISSING_DUMP_DLL_RESETDUMPOPTION, J9_RAS_DUMP_DLL_NAME);

	return OMR_ERROR_INTERNAL;
}

static omr_error_t
primordialQueryVmDump(struct J9JavaVM *vm, int buffer_size, void* options_buffer, int* data_size)
{
	PORT_ACCESS_FROM_JAVAVM(vm);

	j9nls_printf(PORTLIB, J9NLS_WARNING, J9NLS_VM_MISSING_DUMP_DLL_QUERYVMDUMP, J9_RAS_DUMP_DLL_NAME);
	
	/* set data_size to zero and don't update the buffer */
	if (NULL != data_size) {
		*data_size = 0;
	}

	return OMR_ERROR_NONE;
}

#endif /* J9VM_RAS_DUMP_AGENTS */



IDATA 
gpThreadDump(struct J9JavaVM *vm, struct J9VMThread *currentThread)
{
	/* basic thread dump code copied (almost) verbatim from gphandle.c ... */

	J9VMThread *firstThread = NULL;
	int gpThread = 0;
	U_32 numThreads = (U_32) vm->totalThreadCount;
	U_32 numThreadsDumped = 0;

	/* should dump all VMs in multi-VM */
	if (vm) {
		firstThread = currentThread;
		if (firstThread == NULL) {
			firstThread = vm->mainThread;
		} else {
			gpThread = 1;
		}
	}

	if (firstThread) {
		PORT_ACCESS_FROM_PORT(vm->portLibrary);
		currentThread = firstThread;
		do {
			if (currentThread->threadObject) {
				j9object_t threadObject = currentThread->threadObject;
				UDATA priority = vm->internalVMFunctions->getJavaThreadPriority(vm, currentThread);
				UDATA isDaemon = J9VMJAVALANGTHREAD_ISDAEMON(currentThread, threadObject);
				char* name = getOMRVMThreadName(currentThread->omrVMThread);

				j9tty_printf(
					PORTLIB,
					"\nThread: %s (priority %d)%s%s\n",
					name,
					priority,
					(isDaemon ? " (daemon)" : ""),
					(gpThread ? " (LOCATION OF ERROR)" : ""));

				releaseOMRVMThreadName(currentThread->omrVMThread);
			} else {
				j9tty_printf(PORTLIB, "\n(no Thread object associated with thread)\n");
			}
			dumpStackTrace(currentThread);
			gpThread = 0;
			currentThread = currentThread->linkNext;
			numThreadsDumped ++;
		} while (currentThread != firstThread && numThreadsDumped <= numThreads);
	}

	return JNI_OK;
}

#if (defined(J9VM_RAS_DUMP_AGENTS)) 
IDATA 
configureRasDump(J9JavaVM *vm)
{
	/* Setup initial dump facade */
	vm->j9rasDumpFunctions = (J9RASdumpFunctions *)&primordialDumpFacade;

	return JNI_OK;
}
#endif /* J9VM_RAS_DUMP_AGENTS */

#if defined(J9VM_RAS_EYECATCHERS)
void
J9RASInitialize(J9JavaVM* javaVM)
{
#if defined(OSX)
	char **environ = *_NSGetEnviron();
#else /* defined(OSX) */
	extern char **environ;
#endif /* defined(OSX) */

	PORT_ACCESS_FROM_JAVAVM(javaVM);
	const char *osarch = j9sysinfo_get_CPU_architecture();
	const char *osname = j9sysinfo_get_OS_type();
	const char *osversion = j9sysinfo_get_OS_version();
	J9RAS *rasStruct = allocateRASStruct(javaVM);

	memset(rasStruct, 0, sizeof(J9RAS));
	strcpy((char*)rasStruct->eyecatcher, "J9VMRAS");
	rasStruct->bitpattern1 = 0xaa55aa55;
	rasStruct->bitpattern2 = 0xaa55aa55;
	rasStruct->version = J9RASVersion;
	rasStruct->length = sizeof(J9RAS);
	rasStruct->vm = (UDATA)javaVM;
	rasStruct->mainThreadOffset = offsetof(J9JavaVM, mainThread);
	rasStruct->omrthreadNextOffset = offsetof(J9VMThread, linkNext);
	rasStruct->osthreadOffset = offsetof(J9VMThread, osThread);
	rasStruct->idOffset = offsetof(J9AbstractThread, tid);
	rasStruct->typedefs = (UDATA)0;
	rasStruct->env = (UDATA)0;
	rasStruct->buildID = J9UniqueBuildID;

	/* provide a link to the C runtime provided list of environment variables */
#if defined(WIN32) || defined(WIN64)
	rasStruct->environment = GetEnvironmentStringsA();
#elif defined(J9ZTPF)
	/* z/TPF will not dump static storage so use malloc. */
	rasStruct->environment = j9mem_allocate_memory32(sizeof(void *), J9MEM_CATEGORY_VM);
	*((char ***)(rasStruct->environment)) = atoe_environ();
#else
	rasStruct->environment = GLOBAL_DATA(environ);
#endif /* defined(WIN32) || defined(WIN64) */

	rasStruct->cpus = (U_32) j9sysinfo_get_number_CPUs_by_type(J9PORT_CPU_ONLINE);
	rasStruct->memory = j9sysinfo_get_physical_memory();

	strncpy((char *)rasStruct->osarch, (NULL == osarch) ? "unknown" : osarch, sizeof(rasStruct->osarch));
	rasStruct->osarch[sizeof(rasStruct->osarch) - 1] = '\0';

	strncpy((char *)rasStruct->osname, (NULL == osname) ? "unknown" : osname, sizeof(rasStruct->osname));
	rasStruct->osname[sizeof(rasStruct->osname) - 1] = '\0';

	strncpy((char *)rasStruct->osversion, (NULL == osversion) ? "unknown" : osversion, sizeof(rasStruct->osversion));
	rasStruct->osversion[sizeof(rasStruct->osversion) - 1] = '\0';

	rasStruct->nextStatistic = &(javaVM->nextStatistic);
	rasStruct->pid = j9sysinfo_get_pid();

	rasStruct->systemInfo = NULL;

	/* Initialize the dump timestamp fields (JVM start time and dump generation times) */
	rasStruct->startTimeMillis = j9time_current_time_millis();
	rasStruct->startTimeNanos = j9time_nano_time();
	rasStruct->dumpTimeMillis = 0;
	rasStruct->dumpTimeNanos = 0;

	javaVM->j9ras = rasStruct;

	/* Set the basic service level which may be updated later by java.lang.System.rasInitializeVersion. */
	j9rasSetServiceLevel(javaVM, NULL);
}

void
j9rasSetServiceLevel(J9JavaVM *vm, const char *runtimeVersion) {
	/*
	 * This creates one of the following two strings (the latter if runtimeVersion is not NULL).
	 *   JRE 1.8.0 Linux amd64-64
	 *   JRE 1.8.0 Linux amd64-64 (build pxa6480sr4-20170125_01(SR4))
	 *   ^         ^     ^     ^ ^       ^                          ^
	 *   |         |     |     | |       |                          |
	 *   |         |     |     | |       |                          closeBracket
	 *   |         |     |     | |       runtimeVersion
	 *   |         |     |     | openBracket
	 *   |         |     |     ossize
	 *   |         |     osarch
	 *   |         osname
	 *   javaVersion
	 */
	const char *formatString = "%s %s %s-%s%s%s%s";
	size_t size = strlen(formatString) - (7 * 2); /* exclude 7 copies of "%s" */
	const char *javaVersion = "";
	const char *osname = (const char *)(vm->j9ras->osname);
	const char *osarch = (const char *)(vm->j9ras->osarch);
#if defined(J9VM_ENV_DATA64)
	const char *ossize = "64";
#elif defined(J9ZOS390) || defined(S390)
	const char *ossize = "31";
#else
	const char *ossize = "32";
#endif
	const char *openBracket = "";
	const char *closeBracket = "";
	char *serviceLevel = NULL;
	PORT_ACCESS_FROM_JAVAVM(vm);

#if JAVA_SPEC_VERSION == 8
	javaVersion = RUNTIME_INFO " 1.8.0";
#else /* JAVA_SPEC_VERSION == 8 */
	javaVersion = RUNTIME_INFO " " JAVA_SPEC_VERSION_STRING;
#endif /* JAVA_SPEC_VERSION == 8 */

	if ((NULL == runtimeVersion) || ('\0' == *runtimeVersion)) {
		runtimeVersion = "";
	} else {
		openBracket = " (build ";
		closeBracket = ")";
	}

	size += strlen(javaVersion);
	size += strlen(osname);
	size += strlen(osarch);
	size += strlen(ossize);
	size += strlen(openBracket);
	size += strlen(runtimeVersion);
	size += strlen(closeBracket);

	serviceLevel = j9mem_allocate_memory((UDATA)(size + 1), OMRMEM_CATEGORY_VM);
	if (NULL != serviceLevel) {
		j9str_printf(PORTLIB, serviceLevel, size + 1, formatString,
				javaVersion, osname, osarch, ossize, openBracket, runtimeVersion, closeBracket);
		serviceLevel[size] = '\0';

		if (NULL != vm->j9ras->serviceLevel) {
			/* free old storage */
			j9mem_free_memory(vm->j9ras->serviceLevel);
		}

		vm->j9ras->serviceLevel = serviceLevel;
	}
}

#if !defined(J9ZOS390) && !defined(J9ZTPF)
#define ALLOCATE_RAS_DATA_IN_SUBALLOCATOR TRUE
#else /* !defined(J9ZOS390) && !defined(J9ZTPF) */
#define ALLOCATE_RAS_DATA_IN_SUBALLOCATOR FALSE
#endif /* !defined(J9ZOS390) && !defined(J9ZTPF) */

#if defined(J9ZOS390) || (defined (AIXPPC) && !defined (J9VM_ENV_DATA64))
#define USE_STATIC_RAS_STRUCT
#endif /* defined(J9ZOS390) || (defined (AIXPPC) && !defined (J9VM_ENV_DATA64)) */

static J9RAS*
allocateRASStruct(J9JavaVM *javaVM)
{
	J9RAS* candidate = (J9RAS*)GLOBAL_DATA(_j9ras_);
	/*
	 * z/OS: For zOS we need to use the _j9ras_ symbol because a zOS dump may contain more than one JVM
	 * in a single address space, and going in via the eyecatcher would not allow us to
	 * correlate the JVM structures with native information in the dump.
	 *
	 * AIX32: AIX uses 256MB segments for thread stacks. If we pollute one of these segments
	 * with the eyecatchers, we significantly reduce the maximum number of threads which can be created
	 * (from 1000+ to 811). Always use the static variable to avoid wasting virtual memory addresses.
	 *
	 * Compressed references: the RAS data is relocated to the JVM suballocator once the latter is created.
	 */
#if !defined(USE_STATIC_RAS_STRUCT)
	if (!(ALLOCATE_RAS_DATA_IN_SUBALLOCATOR && J9JAVAVM_COMPRESS_OBJECT_REFERENCES(javaVM))) {
		/* if not z/OS or AIX32 */

		J9PortVmemParams params;
		J9PortVmemIdentifier identifier;
		J9AllocatedRAS* result = NULL;
		UDATA pageSize;
		UDATA roundedSize;

		PORT_ACCESS_FROM_JAVAVM(javaVM);

		pageSize = j9vmem_supported_page_sizes()[0];
		if (0 == pageSize) { /* problems in the port library */
			return candidate;
		}
		j9vmem_vmem_params_init(&params);
#if defined (AIXPPC)
		/* the low 768MB is out of bounds on AIX. Don't even bother trying */
		params.startAddress = (void *)0x30000000;
#else /* defined (AIXPPC) */
		params.startAddress = (void *)pageSize;
#endif /* defined (AIXPPC) */
		params.endAddress = OMR_MIN((void *) candidate, (void *) (UDATA) 0xffffffff); /* don't bother allocating after the static */
		params.byteAmount = sizeof(J9AllocatedRAS);
		params.pageSize = pageSize;
		/* round up byteAmount to pageSize */
		roundedSize = params.byteAmount + params.pageSize - 1;
		params.byteAmount = roundedSize - (roundedSize % params.pageSize);
		params.mode = J9PORT_VMEM_MEMORY_MODE_READ | J9PORT_VMEM_MEMORY_MODE_WRITE | J9PORT_VMEM_MEMORY_MODE_COMMIT;
		params.options = J9PORT_VMEM_ALLOC_DIR_BOTTOM_UP;
#if defined(J9ZTPF)
		params.options |= J9PORT_VMEM_ZTPF_USE_31BIT_MALLOC;
#endif
		params.category = OMRMEM_CATEGORY_VM;

		result = j9vmem_reserve_memory_ex(&identifier, &params);
		if (NULL != result) {
			memcpy(&result->vmemid, &identifier, sizeof(identifier));
			candidate = &result->ras;
		}
	}
#endif /* !defined(USE_STATIC_RAS_STRUCT) */
	return candidate;
}

void J9RelocateRASData(J9JavaVM* javaVM) {
	/* See comments for allocateRASStruct concerning compressed references and z/OS */
#if !defined(USE_STATIC_RAS_STRUCT)
	if (ALLOCATE_RAS_DATA_IN_SUBALLOCATOR && J9JAVAVM_COMPRESS_OBJECT_REFERENCES(javaVM)) {
		PORT_ACCESS_FROM_JAVAVM(javaVM);
		J9RAS * result = j9mem_allocate_memory32(sizeof(J9RAS), OMRMEM_CATEGORY_VM);

		if (NULL != result) {
			memcpy(result, (J9RAS*)GLOBAL_DATA(_j9ras_), sizeof(J9RAS));
			javaVM->j9ras = result;
			memset((J9RAS*)GLOBAL_DATA(_j9ras_), 0, sizeof(J9RAS));
		}
	}
#endif /* defined(USE_STATIC_RAS_STRUCT) */
	return;
}

static void
freeRASStruct(J9JavaVM *javaVM, J9RAS* rasStruct)
{
#if !defined(USE_STATIC_RAS_STRUCT)
	if (rasStruct != GLOBAL_DATA(_j9ras_)) { /* dynamic allocation may have failed */
		PORT_ACCESS_FROM_JAVAVM(javaVM);

		if (ALLOCATE_RAS_DATA_IN_SUBALLOCATOR && J9JAVAVM_COMPRESS_OBJECT_REFERENCES(javaVM)) {
			j9mem_free_memory32(rasStruct);
		} else {
			J9AllocatedRAS* allocatedStruct = (J9AllocatedRAS*)rasStruct;
			J9PortVmemIdentifier identifier;

#ifdef J9ZTPF
			/* Release malloc'd pointer holding environ pointer which allows jdmpview to */
			/* reference it because static storage isn't dumped. */
			j9mem_free_memory32(rasStruct->environment);
#endif

			memcpy(&identifier, &allocatedStruct->vmemid, sizeof(identifier));
			j9vmem_free_memory(allocatedStruct, sizeof(J9AllocatedRAS), &identifier);
		}
	}
#endif /* defined(USE_STATIC_RAS_STRUCT) */
}

void
J9RASShutdown(J9JavaVM *javaVM)
{
	J9RASSystemInfo *systemInfo = NULL;
	PORT_ACCESS_FROM_JAVAVM(javaVM);

	if (NULL == javaVM->j9ras) {
		return;
	}

	j9mem_free_memory(javaVM->j9ras->ddrData);
	javaVM->j9ras->ddrData = NULL;

	j9mem_free_memory(javaVM->j9ras->serviceLevel);
	javaVM->j9ras->serviceLevel = NULL;

	while (!J9_LINKED_LIST_IS_EMPTY(javaVM->j9ras->systemInfo)) {
		/* Assume that systemInfo->data either doesn't need freeing or was
		 * allocated by the same j9mem_allocate_memory call that created
		 * systemInfo itself.
		 * See initSystemInfo and appendSystemInfoFromFile in dmpsup.c
		 */
		J9_LINKED_LIST_REMOVE_FIRST(javaVM->j9ras->systemInfo, systemInfo);
		j9mem_free_memory(systemInfo);
	}

	freeRASStruct(javaVM, javaVM->j9ras);
	javaVM->j9ras = NULL;
}

void
populateRASNetData(J9JavaVM *javaVM, J9RAS *rasStruct)
{
	j9addrinfo_struct addrinfo;
	j9addrinfo_t hints;
	U_64 startTime, endTime;
	PORT_ACCESS_FROM_JAVAVM(javaVM);
	OMRPORT_ACCESS_FROM_J9PORT(PORTLIB);

	/* measure the time taken to call the socket APIs, so we can issue a warning */
	startTime = j9time_current_time_millis();

	/* get the host name and IP addresses */
	if (0 != omrsysinfo_get_hostname((char*)rasStruct->hostname,  sizeof(rasStruct->hostname) )) {
		/* error so null the buffer so we don't try to work with it on the other side */
		memset(rasStruct->hostname, 0, sizeof(rasStruct->hostname));
	}
	/* ensure that the string is properly terminated */
	rasStruct->hostname[sizeof(rasStruct->hostname)-1] = '\0';

#if defined(J9OS_I5)
	/* set AI_ADDRCONFIG(0x08) to fix low performance of inactive ipv6 resolving */
	j9sock_getaddrinfo_create_hints( &hints, (I_16) J9ADDR_FAMILY_UNSPEC, 0, J9PROTOCOL_FAMILY_UNSPEC, 0x08 ); 
#else
	/* create the hints structure for both IPv4 and IPv6 */
	j9sock_getaddrinfo_create_hints( &hints, (I_16) J9ADDR_FAMILY_UNSPEC, 0, J9PROTOCOL_FAMILY_UNSPEC, 0 );
#endif
	/* rasStruct->hostname can't simply be localhost since that is always 127.0.0.1 */
	if (0 !=  j9sock_getaddrinfo((char*)rasStruct->hostname, hints, &addrinfo )) {
		/* error so null the buffer so we don't try to work with it on the other side */
		memset(rasStruct->ipAddresses, 0, sizeof(rasStruct->ipAddresses));
	} else {
		/* extract the IPs from the structure */
		I_32 length = 0;
		I_32 i = 0;
		U_32 binaryIPCursor = 0;

		j9sock_getaddrinfo_length(&addrinfo, &length);
		for (i = 0; i < length; i++) {
			U_32 scopeID = 0;
			I_32 family = 0;
			U_8 addressType = 0;
			I_32 bytesToCopy = 0;

			j9sock_getaddrinfo_family(&addrinfo, &family, i );
			bytesToCopy = (J9ADDR_FAMILY_AFINET4 == family) ? J9SOCK_INADDR_LEN : J9SOCK_INADDR6_LEN;
			addressType = (J9ADDR_FAMILY_AFINET4 == family) ? 4 : 6;
			if ((binaryIPCursor + 1 + bytesToCopy) < sizeof(rasStruct->ipAddresses)) {
				rasStruct->ipAddresses[binaryIPCursor++] = addressType;
				j9sock_getaddrinfo_address(&addrinfo, &(rasStruct->ipAddresses[binaryIPCursor]), i, &scopeID);
				binaryIPCursor += bytesToCopy;
			} else {
				/* ran out of space so break */
				break;
			}
		}
		/* free mem */
		j9sock_freeaddrinfo( &addrinfo );
	}

	endTime = j9time_current_time_millis();
	if (endTime - startTime > RAS_NETWORK_WARNING_TIME) {
		j9nls_printf(PORTLIB, J9NLS_INFO, J9NLS_VM_RAS_SLOW_NEWORK_RESPONSE, (int)(endTime - startTime)/1000);
	}
}

/**
 * Function J9RASCheckDump(), implementation for -Xcheck:dump support
 * 
 * @param J9JavaVM* javaVM, JVM top-level structure pointer
 * @returns void
 */
void
J9RASCheckDump(J9JavaVM* javaVM)
{
#if defined(LINUX) || defined(AIXPPC)
	IDATA rc;
	U_64 limit;
	IDATA fd = -1;

	PORT_ACCESS_FROM_JAVAVM(javaVM);

	/* Check the UNIX core-size hard ulimit */
	rc = j9sysinfo_get_limit(J9PORT_RESOURCE_CORE_FILE | J9PORT_LIMIT_HARD, &limit);
	if (rc == J9PORT_LIMIT_LIMITED) {
		/* Issue NLS message warning, with force routing to syslog as well */
		j9nls_printf(PORTLIB, J9NLS_WARNING | J9NLS_VITAL, J9NLS_VM_CHECK_DUMP_ULIMIT, limit);
	}

#if defined(LINUX)
	/* Check if /proc/sys/kernel/core_pattern is set. */
	fd = j9file_open("/proc/sys/kernel/core_pattern", EsOpenRead, 0);
	if (fd != -1) {
		/* Files in /proc report length as 0 but we don't expect the contents to be more than 1 line. */
		char buf[80];
		char* read = NULL;
		read = j9file_read_text(fd, &buf[0], 80);
		if( read == &buf[0] ) {
			size_t bufLen = 0;
			char *cursor;
			/* Make sure the string is only one line and null terminated. */
			for( bufLen = 0; bufLen < 80; bufLen++) {
				if( read[bufLen] == '\n') {
					read[bufLen] = '\0';
					break;
				}
			}
			buf[79] = '\0';
			if( buf[0] == '|') {
				/* Check for core dumps being piped to an external program. */
				j9nls_printf(PORTLIB, J9NLS_WARNING | J9NLS_VITAL, J9NLS_VM_CHECK_DUMP_COREPATTERN_PIPE, buf);
			} else if( buf[0] != '\0') {
				/* Check for core dumps being renamed via % format specifiers. */
				cursor = &buf[0];
				while( *cursor !='\0' ) {
					/* % means a replacement char unless it's followed by another % */
					if( *cursor == '%' ) {
						/* Found a %, issue a warning if it's not followed by % or the end of the string.*/
						cursor++;
						if( *cursor != '\0' && *cursor != '%' ) {
							j9nls_printf(PORTLIB, J9NLS_WARNING | J9NLS_VITAL, J9NLS_VM_CHECK_DUMP_COREPATTERN_FORMAT, buf);
							break; /* Found one specifier, don't worry about any others. */
						} else if( *cursor != '\0' ) {
							cursor++; /* Skip over a %% */
						}
					} else {
						cursor++;
					}
				}
			}
		}
		j9file_close(fd);
	}
#endif
#if defined(AIXPPC)

	/* Check the AIX fullcore setting */
	rc = j9sysinfo_get_limit(J9PORT_RESOURCE_CORE_FLAGS, &limit);
	if (rc == J9PORT_LIMIT_LIMITED) {
		/* Issue NLS message warning, with force routing to syslog as well */
		j9nls_printf(PORTLIB, J9NLS_WARNING | J9NLS_VITAL, J9NLS_VM_CHECK_DUMP_FULLCORE);
	}

#endif
#endif
}

#endif /* J9VM_RAS_EYECATCHERS */
