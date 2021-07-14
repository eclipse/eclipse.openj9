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
 
/**
 * @file
 * @ingroup GC_Modron_Startup
 */

#include "j9.h"
#include "j9cfg.h"
#include "j9protos.h"
#include "j9consts.h"
#include "j9argscan.h"
#include "jni.h"
#include "jvminit.h"
#include "j9port.h"
#include "modronnls.h"
#include "gcutils.h"
#include "ModronAssertions.h"

#include "mmparse.h"

#include "Configuration.hpp"
#include "GCExtensions.hpp"
#if defined(J9VM_GC_MODRON_TRACE)
#include "Tgc.hpp"
#endif /* defined(J9VM_GC_MODRON_TRACE) */

/**
 * @name GC command line options
 * Memory parameter command line options.
 * @note The order of parsing is important, as substrings occur in some parameters.
 * \anchor gcOptions
 * @{
 */
#define OPT_XGC_COLON "-Xgc:"
#define OPT_XXGC_COLON "-XXgc:"
#define OPT_XTGC_COLON "-Xtgc:"
#define OPT_XMX "-Xmx"
#define OPT_XMCA "-Xmca"
#define OPT_XMCO "-Xmco"
#define OPT_XMCRS "-Xmcrs"
#define OPT_XMN "-Xmn"
#define OPT_XMNS "-Xmns"
#define OPT_XMNX "-Xmnx"
#define OPT_XMOI "-Xmoi"
#define OPT_XMO "-Xmo"
#define OPT_XMOS "-Xmos"
#define OPT_XMOX "-Xmox"
#define OPT_XMS "-Xms"
#define OPT_XMRX "-Xmrx"
#define OPT_XMR "-Xmr"
#define OPT_SOFTMX "-Xsoftmx"
#define OPT_NUMA_NONE "-Xnuma:none"
#define OPT_XXMAXRAMPERCENT "-XX:MaxRAMPercentage="
#define OPT_XXINITIALRAMPERCENT "-XX:InitialRAMPercentage="

/**
 * @}
 */

static char OPTION_SET_GROUP_UNUSED[] = "";

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
/**
 * @name Class unloading argument group
 * Command line option group indexes for class unloading options.
 * @note The ordering of the enums must match the char array.
 *
 * @{
 */
enum {
	optionGroupClassGC_index_noclassgc = 0,
	optionGroupClassGC_index_classgc,
	optionGroupClassGC_index_alwaysclassgc
};

static const char *optionGroupClassGC[] = {
	"-Xnoclassgc",
	"-Xclassgc",
	"-Xalwaysclassgc",
	NULL
};
/**
 * @}
 */
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */

#if defined(J9VM_GC_LARGE_OBJECT_AREA)
/**
 * @name Concurrent metering argument group
 * Command line option group indexes for concurrent metering options.
 * @note The ordering of the enums must match the char array.
 *
 * @{
 */
enum {
	optionGroupConMeter_index_soa = 0,
	optionGroupConMeter_index_loa,
	optionGroupConMeter_index_dynamic
};

static const char *optionGroupConMeter[] = {
	"-Xconmeter:soa",
	"-Xconmeter:loa",
	"-Xconmeter:dynamic",
	NULL
};
/**
 * @}
 */
#endif /* J9VM_GC_LARGE_OBJECT_AREA) */

/**
 * @name Xlp error state group.
 *
 * @{
 */
struct XlpError {
	const char *xlpOptionErrorString;
	size_t xlpOptionErrorStringSize;
	const char *xlpMissingOptionString;
	bool extraCommaWarning;
};

typedef enum {
	XLP_NO_ERROR = 0,
	XLP_OPTION_NOT_SUPPORTED,
	XLP_MEM_NAN,
	XLP_MEM_OVERFLOW,
	XLP_PAGE_SIZE_INCORRECT,
	XLP_INCOMPLETE_OPTION,
	XLP_LARGE_PAGE_NOT_SUPPORTED,
	XLP_PARAMETER_NOT_RECOGNIZED
} XlpErrorState;

typedef enum {
	PARSING_FIRST_OPTION = 1,
	PARSING_OPTION,
	PARSING_COMMA,
	PARSING_ERROR
} parsingStates;
/**
 * @}
 */

/**
 * Find, consume and record an option from the argument list.
 * Given an option string and the match type, find the argument in the to be consumed list.
 * If found, consume it.
 * 
 * @return -1 if the argument was not consumed properly, otherwise the index position of the argument (>=0)
 */
static IDATA
option_set(J9JavaVM* vm, const char* option, IDATA match)
{
	return FIND_AND_CONSUME_ARG2(match, option, NULL);
}

/**
 * Find, consume and record an option from the argument list.
 * Given an option string and the match type, find the argument in the to be consumed list.
 * If found, consume it, verify the memory value.
 * 
 * @return OPTION_OK if option is found and consumed or option not present, OPTION_MALFORMED  if the option was malformed, OPTION_OVERFLOW if the option overflowed.
 * @note value stored at address is invalid if failure returned
 * @note optionIndex contains position of argument on command line if success returned, else -1
 */
static IDATA
option_set_to_opt(J9JavaVM* vm, const char* option, IDATA* optionIndex, IDATA match, UDATA* address)
{
	IDATA element;
	IDATA returnCode = OPTION_OK;
	UDATA value;

	element = FIND_AND_CONSUME_ARG2(match, option, NULL);
	*optionIndex = element;

	if (element >= 0) {
		returnCode = GET_MEMORY_VALUE(element, option, value);
		if (OPTION_OK == returnCode){
			*address = value;
		}
	}
	return returnCode;
}

/**
 * Find, consume and record an option from the argument list.
 * Given an option string and the match type, find the argument in the to be consumed list.
 * If found, consume it, verify the memory value.
 * 
 * @return OPTION_OK if option is found and consumed or option not present, OPTION_MALFORMED  if the option was malformed, OPTION_OVERFLOW if the option overflowed.
 * @note value stored at address is invalid if failure returned
 * @note optionIndex contains position of argument on command line if success returned, else -1
 */
static IDATA
option_set_to_opt_percent(J9JavaVM* vm, const char* option, IDATA* optionIndex, IDATA match, UDATA* address)
{
	IDATA element;
	IDATA returnCode = OPTION_OK;
	UDATA value;

	element = FIND_AND_CONSUME_ARG2(match, option, NULL);
	*optionIndex = element;

	if (element >= 0) {
		returnCode = GET_PERCENT_VALUE(element, option, value);
		if (OPTION_OK == returnCode) {
			*address = value;
		}
	} 
	return returnCode;
}

/**
 * Find, consume and record an option from the argument list.
 * Given an option string and the match type, find the argument in the to be consumed list.
 * If not found, return success.
 * If found, consume it, verify the memory value.
 * 
 * @return OPTION_OK if option is found and consumed or option not present, OPTION_MALFORMED  if the option was malformed, OPTION_OVERFLOW if the option overflowed.
 * @note value stored at address is invalid if failure returned
 * @note optionIndex contains position of argument on command line if success returned, else -1
 */
static IDATA
option_set_to_opt_integer(J9JavaVM* vm, const char* option, IDATA* optionIndex, IDATA match, UDATA* address)
{
	IDATA element;
	IDATA returnCode = OPTION_OK;
	IDATA value;

	element = FIND_AND_CONSUME_ARG2(match, option, NULL);
	*optionIndex = element;

	if (element >= 0) {
		returnCode = GET_INTEGER_VALUE(element, option, value);
		if (OPTION_OK == returnCode) {
			*address = value;
		}
	} 
	return returnCode;
}

/**
 * Find, consume and record an option from the argument list.
 * Given an option string and the match type, find the argument in the to be consumed list.
 * If not found, return success.
 * If found, consume it, verify the memory value.
 * 
 * @return OPTION_OK if option is found and consumed or option not present, OPTION_MALFORMED if the option was malformed, OPTION_OVERFLOW if the option overflowed.
 * @note value stored at address is invalid if failure returned
 * @note optionIndex contains position of argument on command line if success returned, else -1
 */
static IDATA
option_set_to_opt_double(J9JavaVM* vm, const char* option, IDATA* optionIndex, IDATA match, double* address)
{
	IDATA element = -1;
	IDATA returnCode = OPTION_OK;
	double value = 0.0;

	element = FIND_AND_CONSUME_ARG2(match, option, NULL);
	*optionIndex = element;

	if (element >= 0) {
		returnCode = GET_DOUBLE_VALUE(element, option, value);
		if (OPTION_OK == returnCode) {
			*address = value;
		}
	} 
	return returnCode;
}

/**
 * Find, consume and record a pair of options from the argument list.
 * Consumes the options option1 and option2 and return which option was rightmost.
 *
 * @return -1 if the arguments were not consumed properly, otherwise the index position of the rightmost argument (>=0)
 * @note if successful consumed optionIndex contains 0 if the rightmost argument is option1, 1 if the rightmost argument is option2, else -1
 */
static IDATA
option_set_pair(J9JavaVM *vm, const char *option1, const char *option2, IDATA *optionPairIndex) {
	IDATA index1, index2;
	index1 = option_set(vm, option1, EXACT_MATCH);
	index2 = option_set(vm, option2, EXACT_MATCH);

	if (index1 > index2) {
		*optionPairIndex = 0;
		return index1;
	}

	if (-1 != index2) {
		*optionPairIndex = 1;
		return index2;
	}
	
	*optionPairIndex = -1;
	return -1;
}

/**
 * Find, consume and record a group of options from the argument list.
 * Consumes the options optionGroup and return which option was rightmost.
 *
 * @return -1 if the arguments were not consumed properly, otherwise the index position of the rightmost argument (>=0)
 * @note if successful consumed optionIndex contains the index into optionGroup of the rightmost argument, else -1
 */
static IDATA
option_set_group(J9JavaVM *vm, const char **optionGroup, IDATA *optionGroupIndex) {
	IDATA rightMostIndex, currentIndex, currentOption;

	/* default return values */
	*optionGroupIndex = -1;
	rightMostIndex = -1;

	currentOption = 0;
	while(NULL != *optionGroup) {
		/* ensure that we skip over entries in the table which aren't supported on this configuration (but preserved for shape consistency) */
		if (OPTION_SET_GROUP_UNUSED != *optionGroup) {
			if (-1 != (currentIndex = option_set(vm, *optionGroup, EXACT_MATCH))) {
				if (currentIndex > rightMostIndex) {
					rightMostIndex = currentIndex;
					*optionGroupIndex = currentOption;
				}
			}
		}
		currentOption += 1;
		optionGroup++;
	}

	return rightMostIndex;
}

/**
 * Parse sub options for -Xlp:xxxxx:
 */
static XlpErrorState
xlpSubOptionsParser(J9JavaVM *vm, IDATA xlpIndex, XlpError *xlpError, UDATA *requestedPageSize, UDATA *requestedPageFlags, bool *strict, bool *warn)
{
	/* -Xlp:objectheap: found and it is most right option, so it is not overwritten by -Xlp<size> */
	char *optionsString = NULL;
	char *scan_limit = NULL;

	/* start parsing with option */
	parsingStates parsingState = PARSING_FIRST_OPTION;
	UDATA optionNumber = 1;
	char *previousOption = NULL;
	char *errorString = NULL;

	UDATA pageSizeHowMany = 0;
#if	defined(J9ZOS390)
	UDATA pageableHowMany = 0;
	UDATA pageableOptionNumber = 0;
	UDATA nonPageableHowMany = 0;
	UDATA nonPageableOptionNumber = 0;
#endif /* defined(J9ZOS390) */

	/* Reset error state from parsing of previous -Xlp<size> option */
	XlpErrorState xlpErrorState = XLP_NO_ERROR;

	xlpError->xlpOptionErrorString = NULL;
	xlpError->xlpOptionErrorStringSize = 0;
	xlpError->xlpMissingOptionString = NULL;
	xlpError->extraCommaWarning  = false;

	/* Get pointer to entire option */
	GET_OPTION_OPTION(xlpIndex, ':', ':', &optionsString);

	/* optionsString can not be NULL here, though it may point to null ('\0') character */
	scan_limit = optionsString + strlen(optionsString);

	/*
	 * parsing -Xlp:objectheap: for options
	 *
	 * reporting general parsing problems (bad formed and unknown options)
	 * recognize cases where extra commas are entered to print warning after if necessary
	 */

	while (optionsString < scan_limit) {
		if (try_scan(&optionsString, ",")) {
			/* Comma separator is discovered */
			switch (parsingState) {
			case PARSING_FIRST_OPTION:
				/* leading comma - ignored but warning required */
				xlpError->extraCommaWarning = true;
				parsingState = PARSING_OPTION;
				break;
			case PARSING_OPTION:
				/* more then one comma - ignored but warning required */
				xlpError->extraCommaWarning = true;
				break;
			case PARSING_COMMA:
				/* expecting for comma here, next should be an option*/
				parsingState = PARSING_OPTION;
				/* next option number */
				optionNumber += 1;
				break;
			case PARSING_ERROR:
			default:
				/* must be unreachable states */
				Assert_MM_unreachable();
				break;
			}
		} else {
			/* Comma separator has not been found. so */
			switch (parsingState) {
			case PARSING_FIRST_OPTION:
				/* still looking for parsing of first option - nothing to do */
				parsingState = PARSING_OPTION;
				break;
			case PARSING_OPTION:
				/* Can not recognize an option case */
				Assert_MM_true(previousOption == optionsString);
				errorString = optionsString;
				parsingState = PARSING_ERROR;
				break;
			case PARSING_COMMA:
				/* can not find comma after option - so this is something unrecognizable at the end of known option */
				errorString = previousOption;
				parsingState = PARSING_ERROR;
				break;
			case PARSING_ERROR:
			default:
				/* must be unreachable states */
				Assert_MM_unreachable();
				break;
			}
		}

		if (PARSING_ERROR == parsingState) {
			Assert_MM_true(NULL != errorString);

			xlpErrorState =  XLP_PARAMETER_NOT_RECOGNIZED;
			xlpError->xlpOptionErrorString = errorString;

			/* try to find comma to isolate unrecognized option */
			char *commaLocation = strchr(errorString, ',');
			if (NULL != commaLocation) {
				/* comma found */
				xlpError->xlpOptionErrorStringSize = (size_t)(commaLocation - errorString);
			} else {
				/* comma not found - print to the end of the string */
				xlpError->xlpOptionErrorStringSize = strlen(errorString);
			}

			return xlpErrorState;
		}

		/* check that something was parsed or previousOption still NULL, otherwise we are in dead loop */
		Assert_MM_true((NULL == previousOption) || (previousOption != optionsString));

		previousOption = optionsString;

		if (try_scan(&optionsString, "pagesize=")) {
			/* try to get memory value */
			if (!scan_udata_memory_size_helper(vm, &optionsString, requestedPageSize, "pagesize=")) {
				/* size is not formed properly */
				xlpErrorState = XLP_PAGE_SIZE_INCORRECT;
				return xlpErrorState;
			}

			pageSizeHowMany += 1;

			parsingState = PARSING_COMMA;
		} else if (try_scan(&optionsString, "pageable")) {
#if	defined(J9ZOS390)
			pageableHowMany += 1;
			pageableOptionNumber = optionNumber;
#endif /* defined(J9ZOS390) */
			parsingState = PARSING_COMMA;
		} else if (try_scan(&optionsString, "nonpageable")) {
#if	defined(J9ZOS390)
			nonPageableHowMany += 1;
			nonPageableOptionNumber = optionNumber;
#endif /* defined(J9ZOS390) */
			parsingState = PARSING_COMMA;
		} else if ((NULL != strict) && try_scan(&optionsString, "strict")) {
			*strict = true;
			parsingState = PARSING_COMMA;
		} else if ((NULL != warn) && try_scan(&optionsString, "warn")) {
			*warn = true;
			parsingState = PARSING_COMMA;
		}
	}

	/*
	 * post-parse check for trailing comma(s)
	 */
	switch (parsingState) {
	/* if loop ended in one of these two states extra comma warning required */
	case PARSING_FIRST_OPTION:
	case PARSING_OPTION:
		/* trailing comma(s) or comma(s) alone */
		xlpError->extraCommaWarning = true;
		break;
	case PARSING_COMMA:
		/* loop ended at comma search state - do nothing */
		break;
	case PARSING_ERROR:
	default:
		/* must be unreachable states */
		Assert_MM_unreachable();
		break;
	}

	/* --- analyze correctness of entered options --- */
	/*
	 * pagesize = <size>
	 *  - this options must be specified for all platforms
	 */
	if (0 == pageSizeHowMany) {
		/* error: pagesize= must be specified */
		xlpErrorState = XLP_INCOMPLETE_OPTION;
		xlpError->xlpOptionErrorString = "-Xlp:objectheap:";
		xlpError->xlpMissingOptionString = "pagesize=";
		return xlpErrorState;
	}

#if defined(J9ZOS390)
	/*
	 *  [non]pageable
	 *  - this option must be specified for Z platforms
	 */
	if ((0 == pageableHowMany) && (0 == nonPageableHowMany)) {
		/* error: [non]pageable not found */
		xlpErrorState = XLP_INCOMPLETE_OPTION;
		xlpError->xlpOptionErrorString = "-Xlp:objectheap:";
		xlpError->xlpMissingOptionString = "[non]pageable";
		return xlpErrorState;
	}

	if (pageableOptionNumber > nonPageableOptionNumber) {
		/* pageable is most right */
		*requestedPageFlags = J9PORT_VMEM_PAGE_FLAG_PAGEABLE;
	} else {
		/* nonpageable is most right */
		*requestedPageFlags = J9PORT_VMEM_PAGE_FLAG_FIXED;
	}
#endif /* defined(J9ZOS390) */

	return xlpErrorState;
}

/**
 * Find and consume -Xlp option(s) from the argument list.
 *
 * @param vm pointer to Java VM structure
 *
 * @return false if the option(s) are not consumed properly, true on success.
 */
static bool
gcParseXlpOption(J9JavaVM *vm)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(vm);
	bool rc = false;
	XlpErrorState xlpErrorState = XLP_NO_ERROR;
	XlpError xlpError = {NULL,		/* xlpOptionErrorString */
			                0,		/* xlpOptionErrorStringSize */
			                NULL,	/* xlpMissingOptionString */
			                false};	/* extraCommaWarning */
	IDATA xlpIndex = -1;
	IDATA xlpMemIndex = -1;
	IDATA xlpObjectHeapIndex = -1;
	IDATA xlpGCIndex = -1;
	UDATA requestedPageSize = 0;
	UDATA requestedPageFlags = J9PORT_VMEM_PAGE_FLAG_NOT_USED;
	PORT_ACCESS_FROM_JAVAVM(vm);

	/* Parse -Xlp option. 
	 * -Xlp option enables large pages with the default large page size, but will not
	 * override any -Xlp<size> or -Xlp:objectheap:pagesize=<size> option.
	 */
	xlpIndex = option_set(vm, "-Xlp", EXACT_MATCH);
	if (-1 != xlpIndex) {
		UDATA defaultLargePageSize = 0;
		UDATA defaultLargePageFlags = J9PORT_VMEM_PAGE_FLAG_NOT_USED;
		j9vmem_default_large_page_size_ex(0, &defaultLargePageSize, &defaultLargePageFlags);
		if (0 != defaultLargePageSize) {
			extensions->requestedPageSize = defaultLargePageSize;
			extensions->requestedPageFlags = defaultLargePageFlags;
		} else {
			xlpErrorState = XLP_OPTION_NOT_SUPPORTED;
			xlpError.xlpOptionErrorString = "-Xlp";
			/* Cannot report error message here,
			 * as we may find a valid "-Xlp:objectheap" that overwrites this option
			 */
		}
	}

	/* Parse -Xlp<size> option. It overrides -Xlp option. */
	xlpMemIndex = FIND_AND_CONSUME_ARG(EXACT_MEMORY_MATCH, "-Xlp", NULL);
	if (-1 != xlpMemIndex) {
		/* Reset error state from parsing of previous -Xlp option */
		xlpErrorState = XLP_NO_ERROR;

		/* No need to set requestedPageFlags explicitly. We use the default value J9PORT_VMEM_PAGE_FLAG_NOT_USED */

		/* If the machine does not support large pages, we may fail.
		 * Page flags for default large page size is not required, just pass NULL.
		 */
		j9vmem_default_large_page_size_ex(0, &requestedPageSize, NULL);
		if (0 != requestedPageSize) {
			IDATA result = option_set_to_opt(vm, "-Xlp", &xlpMemIndex, EXACT_MEMORY_MATCH, &requestedPageSize);
			if (OPTION_OK != result) {
				/* this -Xlp option must be formed properly even it might be overwritten by "-Xlp:objectheap" */
				if (OPTION_MALFORMED == result) {
					xlpErrorState = XLP_MEM_NAN;
				} else {
					xlpErrorState = XLP_MEM_OVERFLOW;
				}
				goto _reportXlpError;
			}
		} else {
			xlpErrorState = XLP_OPTION_NOT_SUPPORTED;
			xlpError.xlpOptionErrorString = "-Xlp";
			/* Cannot report error message here, as we may find a valid "-Xlp:objectheap" that overwrites this option */
		}
	}

	/* Parse -Xlp:objectheap:pagesize=<size> option.
	 * It overrides -Xlp option.
	 * It also overrides -Xlp<size> option if it appears to the right of -Xlp<size>
	 *
	 * The proper formed -Xlp:objectheap: option must be (in strict order):
	 * 	For all non-Z platforms:
	 * 		-Xlp:objectheap:pagesize=<size> or
	 * 		-Xlp:objectheap:pagesize=<size>,pageable or
	 * 		-Xlp:objectheap:pagesize=<size>,nonpageable
	 *
	 * 	For Z platforms
	 *		-Xlp:objectheap:pagesize=<size>,pageable or
	 *		-Xlp:objectheap:pagesize=<size>,nonpageable
	 */
	xlpObjectHeapIndex = FIND_AND_CONSUME_ARG(STARTSWITH_MATCH, "-Xlp:objectheap:", NULL);

	/* so if -Xlp:objectheap: is specified */
	if ((-1 != xlpObjectHeapIndex) && (xlpObjectHeapIndex > xlpMemIndex)) {
		/*
		 * Parse sub options for -Xlp:objectheap:
		 */
		xlpErrorState = xlpSubOptionsParser(vm, xlpObjectHeapIndex, &xlpError, &requestedPageSize, &requestedPageFlags, &extensions->largePageFailOnError, &extensions->largePageWarnOnError);

		if (xlpError.extraCommaWarning) {
			/* print extra comma ignored warning */
			j9nls_printf(PORTLIB, J9NLS_INFO, J9NLS_GC_OPTIONS_XLP_EXTRA_COMMA);
		}
	}

	/* If there is a pending error state, report it now */
	if (XLP_NO_ERROR != xlpErrorState) {
		goto _reportXlpError;
	}

	/* If a valid -Xlp<size> or -Xlp:objectheap:pagesize=<size> is present, check if the requested page size is supported */
	/* We don't need to check error state here - we did goto for all errors */
	if ((-1 != xlpMemIndex) || (-1 != xlpObjectHeapIndex)) {
		UDATA pageSize = requestedPageSize;
		UDATA pageFlags = requestedPageFlags;
		BOOLEAN isRequestedSizeSupported = FALSE;

		IDATA result = j9vmem_find_valid_page_size(0, &pageSize, &pageFlags, &isRequestedSizeSupported);

		/*
		 * j9vmem_find_valid_page_size happened to be changed to always return 0
		 * However formally the function type still be IDATA so assert if it returns anything else
		 */
		Assert_MM_true(0 == result);

		extensions->requestedPageSize = pageSize;
		extensions->requestedPageFlags = pageFlags;

		if (!isRequestedSizeSupported) {
			/* Print a message indicating requested page size is not supported and a different page size will be used */
			const char *oldQualifier, *newQualifier;
			const char *oldPageType = NULL;
			const char *newPageType = NULL;
			UDATA oldSize = requestedPageSize;
			UDATA newSize = pageSize;
			qualifiedSize(&oldSize, &oldQualifier);
			qualifiedSize(&newSize, &newQualifier);
			if (0 == (J9PORT_VMEM_PAGE_FLAG_NOT_USED & requestedPageFlags)) {
				oldPageType = getPageTypeString(requestedPageFlags);
			}
			if (0 == (J9PORT_VMEM_PAGE_FLAG_NOT_USED & pageFlags)) {
				newPageType = getPageTypeString(pageFlags);
			}
			if (NULL == oldPageType) {
				if (NULL == newPageType) {
					j9nls_printf(PORTLIB, J9NLS_INFO, J9NLS_GC_OPTIONS_LARGE_PAGE_SIZE_NOT_SUPPORTED, oldSize, oldQualifier, newSize, newQualifier);
				} else {
					j9nls_printf(PORTLIB, J9NLS_INFO, J9NLS_GC_OPTIONS_LARGE_PAGE_SIZE_NOT_SUPPORTED_WITH_NEW_PAGETYPE, oldSize, oldQualifier, newSize, newQualifier, newPageType);
				}
			} else {
				if (NULL == newPageType) {
					j9nls_printf(PORTLIB, J9NLS_INFO, J9NLS_GC_OPTIONS_LARGE_PAGE_SIZE_NOT_SUPPORTED_WITH_REQUESTED_PAGETYPE, oldSize, oldQualifier, oldPageType, newSize, newQualifier);
				} else {
					j9nls_printf(PORTLIB, J9NLS_INFO, J9NLS_GC_OPTIONS_LARGE_PAGE_SIZE_NOT_SUPPORTED_WITH_PAGETYPE, oldSize, oldQualifier, oldPageType, newSize, newQualifier, newPageType);
				}
			}
		}
	}

	/*
	 * Check for -Xlp:gc: and handle it if necessary
	 */
	xlpGCIndex = FIND_AND_CONSUME_ARG(STARTSWITH_MATCH, "-Xlp:gcmetadata:", NULL);

	if (-1 != xlpGCIndex) {
		UDATA gcmetadataPageSize = 0;
		UDATA gcmetadataPageFlags = J9PORT_VMEM_PAGE_FLAG_NOT_USED;
		UDATA *pageSizes;
		UDATA *pageFlags;
		bool found = false;
		/*
		 * Parse sub options for -Xlp:gc:
		 */
		xlpErrorState = xlpSubOptionsParser(vm, xlpGCIndex, &xlpError, &gcmetadataPageSize, &gcmetadataPageFlags, NULL, NULL);

		if (XLP_NO_ERROR != xlpErrorState) {
			goto _reportXlpError;
		}

		if (xlpError.extraCommaWarning) {
			/* print extra comma ignored warning */
			j9nls_printf(PORTLIB, J9NLS_INFO, J9NLS_GC_OPTIONS_XLP_EXTRA_COMMA);
		}

		/*
		 * Update values in case of exact match only
		 */
		pageSizes = j9vmem_supported_page_sizes();
		pageFlags = j9vmem_supported_page_flags();

		for (UDATA pageIndex = 0; 0 != pageSizes[pageIndex]; ++pageIndex) {
			if ((pageSizes[pageIndex] == gcmetadataPageSize) && (pageFlags[pageIndex] == gcmetadataPageFlags)) {
				found = true;
				extensions->gcmetadataPageSize = gcmetadataPageSize;
				extensions->gcmetadataPageFlags = gcmetadataPageFlags;
 				break;
			}
		}

		if (!found) {
			const char *oldQualifier, *newQualifier;
			UDATA oldSize = gcmetadataPageSize;
			UDATA newSize = extensions->gcmetadataPageSize;
			const char *oldPageType = getPageTypeStringWithLeadingSpace(oldSize);
			const char *newPageType = getPageTypeStringWithLeadingSpace(newSize);
			qualifiedSize(&oldSize, &oldQualifier);
			qualifiedSize(&newSize, &newQualifier);

			j9nls_printf(PORTLIB, J9NLS_INFO, J9NLS_GC_OPTIONS_XLP_PAGE_NOT_SUPPORTED, "gcmetadata", oldSize, oldQualifier, oldPageType, newSize, newQualifier, newPageType);
		}
	}

_reportXlpError:
	/* If error occurred during parsing of -Xlp options, report it here. */
	if (XLP_NO_ERROR != xlpErrorState) {
		/* parsing failed, return false */
		rc = false;

		switch(xlpErrorState) {
		case XLP_OPTION_NOT_SUPPORTED:
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_SYSTEM_CONFIG_OPTION_NOT_SUPPORTED, xlpError.xlpOptionErrorString);
			break;
		case XLP_MEM_NAN:
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, "-Xlp");
			break;
		case XLP_MEM_OVERFLOW:
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, "-Xlp");
			break;
		case XLP_PAGE_SIZE_INCORRECT:
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INCORRECT_MEMORY_SIZE, "-Xlp:objectheap:pagesize=<size>");
			break;
		case XLP_INCOMPLETE_OPTION:
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_XLP_INCOMPLETE_OPTION, xlpError.xlpOptionErrorString, xlpError.xlpMissingOptionString);
			break;
		case XLP_LARGE_PAGE_NOT_SUPPORTED:
		{
			const char *qualifier = NULL;
			const char *pageType = NULL;
			UDATA pageSize = requestedPageSize;
			qualifiedSize(&pageSize, &qualifier);
			if (0 == (J9PORT_VMEM_PAGE_FLAG_NOT_USED & requestedPageFlags)) {
				pageType = getPageTypeString(requestedPageFlags);
			}
			if (NULL == pageType) {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_XLP_LARGE_PAGE_NOT_SUPPORTED, pageSize, qualifier);
			} else {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_XLP_LARGE_PAGE_NOT_SUPPORTED_WITH_PAGETYPE, pageSize, qualifier, pageType);
			}

			break;
		}
		case XLP_PARAMETER_NOT_RECOGNIZED:
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_XLP_UNRECOGNIZED_OPTION, xlpError.xlpOptionErrorStringSize, xlpError.xlpOptionErrorString);
			break;
		default:
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_XLP_PARSING_ERROR);
			break;
		}
	} else {
		rc = true;
	}

	return rc;
}
/**
 * Consume Sovereign arguments.
 *
 * @return 1 if parsing was successful, 0 otherwise.
 */
static UDATA
gcParseSovereignArguments(J9JavaVM *vm)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(vm);
	IDATA result = 0;
	UDATA inputValue = 0;
#if defined(J9VM_GC_LARGE_OBJECT_AREA)
	IDATA groupIndex = 0;
	IDATA indexMeter = 0;
#endif /* J9VM_GC_LARGE_OBJECT_AREA */
	const char *optionFound = NULL;
	IDATA index = -1;
	PORT_ACCESS_FROM_JAVAVM(vm);

	if (!gcParseXlpOption(vm)) {
		goto _error;
	}

	/* Check for explicit specification of GC policy */
	gcParseXgcpolicy(extensions);

	if (-1 != option_set_pair(vm, "-Xenableexplicitgc", "-Xdisableexplicitgc", &index)) {
		extensions->disableExplicitGC = (index != 0);
	}

#if defined(J9VM_GC_MODRON_COMPACTION)
	/* These arguments aren't done as mutual exclusive pairs because their effects are not opposites */
	if (-1 != option_set(vm, "-Xnocompactexplicitgc", EXACT_MATCH)) {
		extensions->nocompactOnSystemGC = 1;
		extensions->compactOnSystemGC = 0;
	}

	if (-1 != option_set(vm, "-Xcompactexplicitgc", EXACT_MATCH)) {
		extensions->compactOnSystemGC = 1;
	}

	/* These arguments aren't done as mutual exclusive pairs because their effects are not opposites */
	if (-1 != option_set(vm, "-Xcompactgc", EXACT_MATCH)) {
		extensions->compactOnGlobalGC = 1;
		extensions->compactOnSystemGC = 1;
	}

	if (-1 != option_set(vm, "-Xnocompactgc", EXACT_MATCH)) {
		extensions->noCompactOnGlobalGC = 1;
	}

	if (-1 != option_set_pair(vm, "-Xnopartialcompactgc", "-Xpartialcompactgc", &index)) {
		/* Incremental compaction is no longer supported.
		 * Options are supported but deprecated.
		 */
	}
#endif /* J9VM_GC_MODRON_COMPACTION */

	result = option_set_to_opt_percent(vm, "-Xminf", &index, EXACT_MEMORY_MATCH, &extensions->heapFreeMinimumRatioMultiplier);
	if (OPTION_OK != result) {
		if (OPTION_MALFORMED == result) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, "-Xminf");
		} else {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_PERCENT_OUT_OF_RANGE, "-Xminf", 0.0, 1.0);
		}
		goto _error;
	}
	result = option_set_to_opt_percent(vm, "-Xmaxf", &index, EXACT_MEMORY_MATCH, &extensions->heapFreeMaximumRatioMultiplier);
	if (OPTION_OK != result) {
		if (OPTION_MALFORMED == result) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, "-Xmaxf");
		} else {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_PERCENT_OUT_OF_RANGE, "-Xmaxf", 0.0, 1.0);
		}
		goto _error;
	}
	result = option_set_to_opt(vm, "-Xmine", &index, EXACT_MEMORY_MATCH, &extensions->heapExpansionMinimumSize);
	if (OPTION_OK != result) {
		if (OPTION_MALFORMED == result) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, "-Xmine");
		} else {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, "-Xmine");
		}
		goto _error;
	}
	result = option_set_to_opt(vm, "-Xmaxe", &index, EXACT_MEMORY_MATCH, &extensions->heapExpansionMaximumSize);
	if (OPTION_OK != result) {
		if (OPTION_MALFORMED == result) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, "-Xmaxe");
		} else {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, "-Xmaxe");
		}
		goto _error;
	}

	
	if (-1 != FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, "-Xmaxt", NULL)) {
		extensions->heapExpansionGCTimeThreshold._wasSpecified = true;
		result =  option_set_to_opt_percent(vm, "-Xmaxt", &index, EXACT_MEMORY_MATCH, &extensions->heapExpansionGCTimeThreshold._valueSpecified);
		if (OPTION_OK != result) {
			if (OPTION_MALFORMED == result) {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, "-Xmaxt");
			} else {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_PERCENT_OUT_OF_RANGE, "-Xmaxt", 0.0, 1.0);
			}
			goto _error;
		}
	} 


	if (-1 != FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, "-Xmint", NULL)) {
		extensions->heapContractionGCTimeThreshold._wasSpecified = true;
		result =  option_set_to_opt_percent(vm, "-Xmint", &index, EXACT_MEMORY_MATCH, &extensions->heapContractionGCTimeThreshold._valueSpecified);
		if (OPTION_OK != result) {
			if (OPTION_MALFORMED == result) {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, "-Xmint");
			} else {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_PERCENT_OUT_OF_RANGE, "-Xmint", 0.0, 1.0);
			}
			goto _error;
		}
	}

	if(-1 != FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, VMOPT_XGCTHREADS, NULL)) {
		result = option_set_to_opt_integer(vm, VMOPT_XGCTHREADS, &index, EXACT_MEMORY_MATCH, &extensions->gcThreadCount);
		if (OPTION_OK != result) {
			if (OPTION_MALFORMED == result) {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, VMOPT_XGCTHREADS);
			} else {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, VMOPT_XGCTHREADS);
			}
			goto _error;
		}

		if(0 == extensions->gcThreadCount) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_MUST_BE_ABOVE, VMOPT_XGCTHREADS, (UDATA)0);
			goto _error;
		}

		extensions->gcThreadCountForced = true;
	}

	/* Handling VMOPT_XGCMAXTHREADS is equivalent to VMOPT_XGCTHREADS (above), except it sets gcThreadCountForced to false rather than true. */
	if (-1 != FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, VMOPT_XGCMAXTHREADS, NULL)) {
		result = option_set_to_opt_integer(vm, VMOPT_XGCMAXTHREADS, &index, EXACT_MEMORY_MATCH, &extensions->gcThreadCount);
		if (OPTION_OK != result) {
			if (OPTION_MALFORMED == result) {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, VMOPT_XGCMAXTHREADS);
			} else {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, VMOPT_XGCMAXTHREADS);
			}
			goto _error;
		}

		if (0 == extensions->gcThreadCount) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_MUST_BE_ABOVE, VMOPT_XGCMAXTHREADS, (UDATA)0);
			goto _error;
		}

		extensions->gcThreadCountForced = false;
	}

	if(-1 != FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, "-Xgcworkpackets", NULL)) {
		result = option_set_to_opt_integer(vm, "-Xgcworkpackets", &index, EXACT_MEMORY_MATCH, &extensions->workpacketCount);
		if (OPTION_OK != result) {
			if (OPTION_MALFORMED == result) {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, "-Xgcworkpackets");
			} else {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, "-Xgcworkpackets");
			}
			goto _error;
		}
		if(0 == extensions->workpacketCount) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_MUST_BE_ABOVE, "-Xgcworkpackets", (UDATA)0);
			goto _error;
		}
	}

#if defined(J9VM_GC_DYNAMIC_CLASS_UNLOADING)
	if (-1 != option_set_group(vm, optionGroupClassGC, &index)) {
		switch (index) {
			case optionGroupClassGC_index_noclassgc:
				extensions->dynamicClassUnloading = MM_GCExtensions::DYNAMIC_CLASS_UNLOADING_NEVER;
				break;

			case optionGroupClassGC_index_classgc:
				extensions->dynamicClassUnloading = MM_GCExtensions::DYNAMIC_CLASS_UNLOADING_ON_CLASS_LOADER_CHANGES;
				break;

			case optionGroupClassGC_index_alwaysclassgc:
				extensions->dynamicClassUnloading = MM_GCExtensions::DYNAMIC_CLASS_UNLOADING_ALWAYS;
				break;
		}
		extensions->dynamicClassUnloadingSet = true;
	}
#endif /* J9VM_GC_DYNAMIC_CLASS_UNLOADING */
	if (-1 != option_set_pair(vm, "-Xdisablestringconstantgc", "-Xenablestringconstantgc", &index)) {
		extensions->collectStringConstants = (0 != index);
	}

#if defined(OMR_GC_MODRON_CONCURRENT_MARK)
	result = option_set_to_opt_integer(vm, VMOPT_XCONCURRENTBACKGROUND, &index, EXACT_MEMORY_MATCH, &extensions->concurrentBackground);
	if (OPTION_OK != result) {
		if (OPTION_MALFORMED == result) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, VMOPT_XCONCURRENTBACKGROUND);
		} else {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, VMOPT_XCONCURRENTBACKGROUND);
		}
		goto _error;
	}
	
	result = option_set_to_opt_integer(vm, "-Xconcurrentlevel", &index, EXACT_MEMORY_MATCH, &extensions->concurrentLevel);
	if (OPTION_OK != result) {
		if (OPTION_MALFORMED == result) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, "-Xconcurrentlevel");
		} else {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, "-Xconcurrentlevel");
		}
		goto _error;
	}

	if (0 == extensions->concurrentLevel) {
		/* LIR 1396: -Xconcurrentlevel0 should mean -Xgc:noConcurrentMark. */
		extensions->configurationOptions._forceOptionConcurrentMark = true;
		extensions->concurrentMark = false;
	}

	result = option_set_to_opt(vm, "-Xconcurrentslack", &index, EXACT_MEMORY_MATCH, &extensions->concurrentSlack);
	if (OPTION_OK != result) {
		if (OPTION_MALFORMED == result) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, "-Xconcurrentslack");
		} else {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, "-Xconcurrentslack");
		}
		goto _error;
	}

#endif /* OMR_GC_MODRON_CONCURRENT_MARK */

#if defined(J9VM_GC_LARGE_OBJECT_AREA)
	/* parse this before -Xloamaximum, as -Xloamaximum0 will turn loa off */
	if (-1 != option_set_pair(vm, "-Xnoloa", "-Xloa", &index)) {
		extensions->configurationOptions._forceOptionLargeObjectArea = true;
		extensions->largeObjectArea = (1 == index);
	}
	
	if(-1 != FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, "-Xloainitial", NULL)) {
		result = option_set_to_opt_percent(vm, "-Xloainitial", &index, EXACT_MEMORY_MATCH, &inputValue);
		if (OPTION_OK != result) {
			if (OPTION_MALFORMED == result) {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, "-Xloainitial");
			} else {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_PERCENT_OUT_OF_RANGE, "-Xloainitial", 0.0, 0.95);
			}
			goto _error;
		}
		if (inputValue >  95) { 
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_PERCENT_OUT_OF_RANGE, "-Xloainitial", 0.0, 0.95);
			goto _error;
		}	
		extensions->largeObjectAreaInitialRatio = (double)inputValue / (double)100;
	} 
	
	if(-1 != FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, "-Xloamaximum", NULL)) {
		result = option_set_to_opt_percent(vm, "-Xloamaximum", &index, EXACT_MEMORY_MATCH, &inputValue);
		if (OPTION_OK != result) {
			if (OPTION_MALFORMED == result) {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, "-Xloamaximum");
			} else {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_PERCENT_OUT_OF_RANGE, "-Xloamaximum", 0.0, 0.95);
			}
			goto _error;
		}
		if (inputValue >  95) { 
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_PERCENT_OUT_OF_RANGE, "-Xloamaximum", 0.0, 0.95);
			goto _error;
		}	
		extensions->largeObjectAreaMaximumRatio = (double)inputValue / (double)100;
		if(0 == extensions->largeObjectAreaMaximumRatio) {
			/* Implies -Xnoloa */
			extensions->configurationOptions._forceOptionLargeObjectArea = true;
			extensions->largeObjectArea = false;
		}
	} 
	
	if(-1 != FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, "-Xloaminimum", NULL)) {
		result = option_set_to_opt_percent(vm, "-Xloaminimum", &index, EXACT_MEMORY_MATCH, &inputValue);
		if (OPTION_OK != result) {
			if (OPTION_MALFORMED == result) {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, "-Xloaminimum");
			} else {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_PERCENT_OUT_OF_RANGE, "-Xloaminimum", 0.0, 0.95);
			}
			goto _error;
		}
		if (inputValue >  95) { 
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_PERCENT_OUT_OF_RANGE, "-Xloaminimum", 0.0, 0.95);
			goto _error;
		}	
		extensions->largeObjectAreaMinimumRatio = (double)inputValue / (double)100;
		if(-1 == FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, "-Xloainitial", NULL)) {
			/* -Xloainitial wasn't specified, so we need to override it to match the -Xloaminimum we've just set */
			extensions->largeObjectAreaInitialRatio = extensions->largeObjectAreaMinimumRatio;
		}
	} 
	
	indexMeter = option_set_group(vm, optionGroupConMeter, &groupIndex);
	if (-1 != indexMeter) {
		switch(groupIndex) {
			case optionGroupConMeter_index_soa:
				extensions->concurrentMetering = MM_GCExtensions::METER_BY_SOA;
				break;

			case optionGroupConMeter_index_loa:
				extensions->concurrentMetering = MM_GCExtensions::METER_BY_LOA;
				break;

			case optionGroupConMeter_index_dynamic:
				extensions->concurrentMetering = MM_GCExtensions::METER_DYNAMIC;
				break;
		}
	}
	
	/* Try to match SOV alternative syntax of -Xconmeter(0|1|2) */
	if(-1 != FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, "-Xconmeter", NULL)) {
		result = option_set_to_opt_integer(vm, "-Xconmeter", &index, EXACT_MEMORY_MATCH, &inputValue);
		if (OPTION_OK != result) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, "-Xconmeter");
			goto _error;
		} 
		
		switch(inputValue) {
		case 0:
			extensions->concurrentMetering = MM_GCExtensions::METER_BY_SOA;
			break;
		case 1:
			extensions->concurrentMetering = MM_GCExtensions::METER_BY_LOA;
			break;
		case 2:
			extensions->concurrentMetering = MM_GCExtensions::METER_DYNAMIC;
			break;
		default: 
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_INTEGER_OUT_OF_RANGE, "-Xconmeter", (UDATA)0, (UDATA)2);
			goto _error;
			break;
		}	
	}
	
#endif /* J9VM_GC_LARGE_OBJECT_AREA) */

	/* If user has specified any of the following SOV options  then we just silently ignore them 
	 * 
	 * -Xparroot
	 * -XloratioN 
	 * -XloincrN  
	 * -XlorsrvN 
	 * All these options (except -Xparoot) take a float value between 0 and 1.0.  
	 * 
	 */
	option_set(vm, "-Xparroot", EXACT_MATCH); 
	option_set_to_opt_percent(vm, "-Xloratio", &index, EXACT_MEMORY_MATCH, &inputValue);
	option_set_to_opt_percent(vm, "-Xloincr", &index, EXACT_MEMORY_MATCH, &inputValue);
	option_set_to_opt_percent(vm, "-Xlorsrv", &index, EXACT_MEMORY_MATCH, &inputValue);

	/* options to enable/disable excessivegc */
	if (-1 != option_set_pair(vm, "-Xdisableexcessivegc", "-Xenableexcessivegc", &index)) {
		extensions->excessiveGCEnabled._wasSpecified = true;
		extensions->excessiveGCEnabled._valueSpecified = (1 == index) ? true : false;
	}

	if((-1 != FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, "-XSoftRefThreshold", NULL))) {
		optionFound = "-XSoftRefThreshold";
	} else {
		if((-1 != FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, "-Xsoftrefthreshold", NULL))) {
			optionFound = "-Xsoftrefthreshold";
		}	
	}		
		
	if (NULL != optionFound) {
		result = option_set_to_opt_integer(vm, optionFound, &index, EXACT_MEMORY_MATCH, &extensions->maxSoftReferenceAge);
		if (OPTION_OK != result) {
			if (OPTION_MALFORMED == result) {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, optionFound);
			} else {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, optionFound);
			}
			goto _error;
		}
		
		if ((IDATA)extensions->maxSoftReferenceAge < 0) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, optionFound);
			goto _error;
		}
	}	

	if(-1 != FIND_ARG_IN_VMARGS(EXACT_MEMORY_MATCH, "-XX:stringTableListToTreeThreshold=", NULL)) {
		UDATA threshold = 0;
		result = option_set_to_opt_integer(vm, "-XX:stringTableListToTreeThreshold=", &index, EXACT_MEMORY_MATCH, &threshold);
		if (OPTION_OK != result) {
			if (OPTION_MALFORMED == result) {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, "-XX:stringTableListToTreeThreshold=");
			} else {
				j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, "-XX:stringTableListToTreeThreshold=");
			}
			goto _error;
		}

		if (threshold <= U_32_MAX) {
			extensions->_stringTableListToTreeThreshold = (U_32)threshold;
		} else {
			extensions->_stringTableListToTreeThreshold = U_32_MAX;
		}
	}

	return 1;

_error:
	return 0;

}

static UDATA
gcParseXXArguments(J9JavaVM *vm)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(vm);

	{
		IDATA heapManagementMXBeanCompatibilityIndex = FIND_ARG_IN_VMARGS(EXACT_MATCH, "-XX:+HeapManagementMXBeanCompatibility", NULL);
		IDATA noHheapManagementMXBeanCompatibilityIndex = FIND_ARG_IN_VMARGS(EXACT_MATCH, "-XX:-HeapManagementMXBeanCompatibility", NULL);
		if (heapManagementMXBeanCompatibilityIndex != noHheapManagementMXBeanCompatibilityIndex) {
			/* At least one option is set. Find the right most one. */
			if (heapManagementMXBeanCompatibilityIndex > noHheapManagementMXBeanCompatibilityIndex) {
				extensions->_HeapManagementMXBeanBackCompatibilityEnabled = true;
			} else {
				extensions->_HeapManagementMXBeanBackCompatibilityEnabled = false;
			}
		}
	}

	{
		IDATA useGCStartupHintsIndex = FIND_ARG_IN_VMARGS(EXACT_MATCH, "-XX:+UseGCStartupHints", NULL);
		IDATA noUseGCStartupHintsIndex = FIND_ARG_IN_VMARGS(EXACT_MATCH, "-XX:-UseGCStartupHints", NULL);
		if (useGCStartupHintsIndex != noUseGCStartupHintsIndex) {
			/* At least one option is set. Find the right most one. */
			if (useGCStartupHintsIndex > noUseGCStartupHintsIndex) {
				extensions->useGCStartupHints = true;
			} else {
				extensions->useGCStartupHints = false;
			}
		}
	}

	{
		IDATA alwaysPreTouchIndex = FIND_ARG_IN_VMARGS(EXACT_MATCH, "-XX:+AlwaysPreTouch", NULL);
		IDATA notAlwaysPreTouchIndex = FIND_ARG_IN_VMARGS(EXACT_MATCH, "-XX:-AlwaysPreTouch", NULL);
		if (alwaysPreTouchIndex != notAlwaysPreTouchIndex) {
			/* At least one option is set. Find the right most one. */
			if (alwaysPreTouchIndex > notAlwaysPreTouchIndex) {
				extensions->pretouchHeapOnExpand = true;
			} else {
				extensions->pretouchHeapOnExpand = false;
			}
		}
	}

	{
		IDATA adaptiveGCThreadingIndex = FIND_ARG_IN_VMARGS(EXACT_MATCH, "-XX:+AdaptiveGCThreading", NULL);
		IDATA noAdaptiveGCThreadingIndex = FIND_ARG_IN_VMARGS(EXACT_MATCH, "-XX:-AdaptiveGCThreading", NULL);
		if (adaptiveGCThreadingIndex != noAdaptiveGCThreadingIndex) {
			/* At least one option is set. Find the right most one. */
			if (adaptiveGCThreadingIndex > noAdaptiveGCThreadingIndex) {
				extensions->adaptiveGCThreading = true;
			} else {
				extensions->adaptiveGCThreading = false;
			}
		}
	}

	return 1;
}

/**
 * Wrapper for scan_udata, that provides readable error messages.
 * @param cursor address of the pointer to the string to parse for the udata
 * @param value address of the storage for the udata to be read
 * @param argName string containing the argument name to be used in error reporting
 * @return true if parsing was successful, false otherwise.
 */
bool
scan_udata_helper(J9JavaVM *javaVM, char **cursor, UDATA *value, const char *argName)
{
	UDATA result;
	PORT_ACCESS_FROM_JAVAVM(javaVM);

	if (0 != (result = scan_udata(cursor, value))) {
		if (1 == result) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, argName);
		} else {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, argName);
		}
		return false;			
	}

	// TODO: should we add a delimiter check here?
	return true;
}

/**
 * Wrapper for scan_udata, that provides readable error messages.
 * @param cursor address of the pointer to the string to parse for the udata
 * @param value address of the storage for the udata to be read
 * @param argName string containing the argument name to be used in error reporting
 * @return true if parsing was successful, false otherwise.
 */
bool
scan_u32_helper(J9JavaVM *javaVM, char **cursor, U_32 *value, const char *argName)
{
	UDATA result;
	PORT_ACCESS_FROM_JAVAVM(javaVM);

	if (0 != (result = scan_u32(cursor, value))) {
		if (1 == result) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, argName);
		} else {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, argName);
		}
		return false;
	}

	// TODO: should we add a delimiter check here?
	return true;
}

/**
 * Wrapper for scan_long, that provides readable error messages.
 * @param cursor address of the pointer to the string to parse for the u_64
 * @param value address of the storage for the U_64 to be read
 * @param argName string containing the argument name to be used in error reporting
 * @return true if parsing was successful, false otherwise.
 */
bool
scan_u64_helper(J9JavaVM *javaVM, char **cursor, U_64 *value, const char *argName)
{
	U_64 result;
	PORT_ACCESS_FROM_JAVAVM(javaVM);

	if (0 != (result = scan_u64(cursor, value))) {
		if (1 == result) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, argName);
		} else {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, argName);
		}
		return false;
	}

	// TODO: should we add a delimiter check here?
	return true;
}

/**
 * Wrapper for scan_hex, that provides readable error messages.
 * @param cursor address of the pointer to the string to parse for the hex value
 * @param value address of the storage for the hex value to be read
 * @param argName string containing the argument name to be used in error reporting
 * @return true if parsing was successful, false otherwise.
 */
bool
scan_hex_helper(J9JavaVM *javaVM, char **cursor, UDATA *value, const char *argName)
{
	UDATA result;
	PORT_ACCESS_FROM_JAVAVM(javaVM);

	if (0 != (result = scan_hex(cursor, value))) {
		if (1 == result) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, argName);
		} else {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, argName);
		}
		return false;
	}

	// TODO: should we add a delimiter check here?
	return true;
}

/**
 * Wrapper for scan_udata_memory_size, that provides readable error messages.
 * @param cursor address of the pointer to the string to parse for the udata.
 * @param value address of the storage for the udata to be read.
 * @param argName string containing the argument name to be used in error reporting.
 * @return true if parsing was successful, false otherwise.
 */
bool
scan_udata_memory_size_helper(J9JavaVM *javaVM, char **cursor, uintptr_t *value, const char *argName)
{
	PORT_ACCESS_FROM_JAVAVM(javaVM);
	uintptr_t result = scan_udata_memory_size(cursor, value);

	/* Report Errors */
	if (1 == result) {
		j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, argName);
	} else if (2 == result) {
		j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, argName);
	}

	return 0 == result;
}

/**
 * Wrapper for scan_u64_helper, that provides readable error messages.
 * @param cursor address of the pointer to the string to parse for the udata.
 * @param value address of the storage for the udata to be read.
 * @param argName string containing the argument name to be used in error reporting.
 * @return true if parsing was successful, false otherwise.
 */
bool
scan_u64_memory_size_helper(J9JavaVM *javaVM, char **cursor, uint64_t *value, const char *argName)
{
	PORT_ACCESS_FROM_JAVAVM(javaVM);
	uintptr_t result = scan_u64_memory_size(cursor, value);

	/* Report Errors */
	if (1 == result) {
		j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_MUST_BE_NUMBER, argName);
	} else if (2 == result) {
		j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, argName);
	}

	return 0 == result;
}

/**
 * Initialize GC fields with values provided by user.
 * Record the parameter in the appropriate structure and record which
 * parameters were specified by the user.
 * @param memoryParameters pointer to the array of parameters to be set
 */
jint 
gcParseCommandLineAndInitializeWithValues(J9JavaVM *vm, IDATA *memoryParameters)
{
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(vm);
	UDATA optionValue = 0;
	IDATA index = 0;
	IDATA result = 0;
	UDATA userNewSpaceSize = 0;
	UDATA userOldSpaceSize = 0;
	char *xGCOptions = NULL;
	char *xxGCOptions = NULL;
	IDATA xGCColonIndex = 0;
	IDATA xxGCColonIndex = 0;
	J9VMInitArgs *vmArgs = vm->vmArgsArray;

	PORT_ACCESS_FROM_JAVAVM(vm);

	/* Parse the command line 
	 * Order is important for parameters that match as substrings (-Xmrx/-Xmr)
	 */
	{
		bool enableOriginalJDK8HeapSizeCompatibilityOption = false;
		/* only parse VMOPT_XXENABLEORIGINALJDK8HEAPSIZECOMPATIBILITY option for Java 8 and below */
		if (J2SE_18 >= J2SE_VERSION(vm)) {

			IDATA enabled = FIND_AND_CONSUME_ARG(EXACT_MATCH, VMOPT_XXENABLEORIGINALJDK8HEAPSIZECOMPATIBILITY, NULL);
			IDATA disabled = FIND_AND_CONSUME_ARG(EXACT_MATCH, VMOPT_XXDISABLEORIGINALJDK8HEAPSIZECOMPATIBILITY, NULL);
			if (enabled > disabled) {
				enableOriginalJDK8HeapSizeCompatibilityOption = true;
			}
		}
		/* set default max heap for Java */
		extensions->computeDefaultMaxHeapForJava(enableOriginalJDK8HeapSizeCompatibilityOption);
	}
	result = option_set_to_opt(vm, OPT_XMCA, &index, EXACT_MEMORY_MATCH, &vm->ramClassAllocationIncrement);
	if (OPTION_OK != result) {
		goto _error;
	}
	memoryParameters[opt_Xmca] = index;

	result = option_set_to_opt(vm, OPT_XMCO, &index, EXACT_MEMORY_MATCH, &vm->romClassAllocationIncrement);
	if (OPTION_OK != result) {
		goto _error;
	}
	memoryParameters[opt_Xmco] = index;

	result = option_set_to_opt(vm, OPT_XMCRS, &index, EXACT_MEMORY_MATCH, &optionValue);
	if (OPTION_OK != result) {
		goto _error;
	}

#if defined(OMR_GC_COMPRESSED_POINTERS)
	if (J9JAVAVM_COMPRESS_OBJECT_REFERENCES(vm)) {
		if (-1 != index) {
			extensions->suballocatorInitialSize = optionValue;
		}
		if(0 == extensions->suballocatorInitialSize) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_MUST_BE_ABOVE, OPT_XMCRS, (UDATA)0);
			return JNI_EINVAL;
		}
#define FOUR_GB ((UDATA)4 * 1024 * 1024 * 1024)
		if(extensions->suballocatorInitialSize >= FOUR_GB) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_VALUE_OVERFLOWED, OPT_XMCRS);
			return JNI_EINVAL;
		}
	}
#endif /* defined(OMR_GC_COMPRESSED_POINTERS) */

	memoryParameters[opt_Xmcrs] = index;

	result = option_set_to_opt(vm, OPT_XMX, &index, EXACT_MEMORY_MATCH, &extensions->memoryMax);
	if (OPTION_OK != result) {
		goto _error;
	}
	memoryParameters[opt_Xmx] = index;

	result = option_set_to_opt(vm, OPT_SOFTMX, &index, EXACT_MEMORY_MATCH, &extensions->softMx);
	if (OPTION_OK != result) {
		goto _error;
	}
	memoryParameters[opt_Xsoftmx] = index;

	/* -Xmns sets both newSpaceSize and minNewSpaceSize */
	result = option_set_to_opt(vm, OPT_XMNS, &index, EXACT_MEMORY_MATCH, &extensions->userSpecifiedParameters._Xmns._valueSpecified);
	if (OPTION_OK != result) {
		goto _error;
	}
	memoryParameters[opt_Xmns] = index;
	if (-1 != memoryParameters[opt_Xmns]) {
		extensions->userSpecifiedParameters._Xmns._wasSpecified = true;
		extensions->newSpaceSize = extensions->userSpecifiedParameters._Xmns._valueSpecified;
		extensions->minNewSpaceSize = extensions->newSpaceSize;
	}

	result = option_set_to_opt(vm, OPT_XMNX, &index, EXACT_MEMORY_MATCH, &extensions->userSpecifiedParameters._Xmnx._valueSpecified);
	if (OPTION_OK != result) {
		goto _error;
	}
	memoryParameters[opt_Xmnx] = index;
	if (-1 != memoryParameters[opt_Xmnx]) {
		extensions->userSpecifiedParameters._Xmnx._wasSpecified = true;
		extensions->maxNewSpaceSize = extensions->userSpecifiedParameters._Xmnx._valueSpecified;
	}

	result = option_set_to_opt(vm, OPT_XMOI, &index, EXACT_MEMORY_MATCH, &extensions->allocationIncrement);
	if (OPTION_OK != result) {
		goto _error;
	}
	memoryParameters[opt_Xmoi] = index;
	extensions->allocationIncrementSetByUser = (index != -1);

	/* -Xmos sets both oldSpaceSize and minOldSpaceSize */
	result = option_set_to_opt(vm, OPT_XMOS, &index, EXACT_MEMORY_MATCH, &extensions->oldSpaceSize);
	if (OPTION_OK != result) {
		goto _error;
	}
	memoryParameters[opt_Xmos] = index;
	if (-1 != memoryParameters[opt_Xmos]) {
		extensions->minOldSpaceSize = extensions->oldSpaceSize;
	}

	result = option_set_to_opt(vm, OPT_XMOX, &index, EXACT_MEMORY_MATCH, &extensions->maxOldSpaceSize);
	if (OPTION_OK != result) {
		goto _error;
	}
	memoryParameters[opt_Xmox] = index;

	result = option_set_to_opt(vm, OPT_XMS, &index, EXACT_MEMORY_MATCH, &extensions->initialMemorySize);
	if (OPTION_OK != result) {
		goto _error;
	}
	memoryParameters[opt_Xms] = index;

#if defined(J9VM_GC_MODRON_SCAVENGER)
	result = option_set_to_opt(vm, OPT_XMRX, &index, EXACT_MEMORY_MATCH, &optionValue);
	if (OPTION_OK != result) {
		goto _error;
	}
	memoryParameters[opt_Xmrx] = index;
	if(memoryParameters[opt_Xmrx] != -1) {
		extensions->rememberedSet.setMaxSize(optionValue);
	}

	result = option_set_to_opt(vm, OPT_XMR, &index, EXACT_MEMORY_MATCH, &optionValue);
	if (OPTION_OK != result) {
		goto _error;
	}
	memoryParameters[opt_Xmr] = index;
	if (memoryParameters[opt_Xmr] != -1) {
		extensions->rememberedSet.setGrowSize(optionValue);
	}
#endif /* J9VM_GC_MODRON_SCAVENGER */

	result = option_set_to_opt(vm, OPT_XMN, &index, EXACT_MEMORY_MATCH, &extensions->userSpecifiedParameters._Xmn._valueSpecified);
	if (OPTION_OK != result) {
		goto _error;
	}
	memoryParameters[opt_Xmn] = index;
	if (memoryParameters[opt_Xmn] != -1) {
		extensions->userSpecifiedParameters._Xmn._wasSpecified = true;
		userNewSpaceSize = extensions->userSpecifiedParameters._Xmn._valueSpecified;
		/* check that -Xmns/-Xmnx AND -Xmn weren't specified */
		if (memoryParameters[opt_Xmns] != -1) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_EXCLUSIVE, OPT_XMN, OPT_XMNS);
			return JNI_EINVAL;
		}
		if (memoryParameters[opt_Xmnx] != -1) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_EXCLUSIVE, OPT_XMN, OPT_XMNX);
			return JNI_EINVAL;
		}

		extensions->minNewSpaceSize = userNewSpaceSize;
		extensions->newSpaceSize = userNewSpaceSize;
		extensions->maxNewSpaceSize = userNewSpaceSize;
		
		/* Hack table to appear that -Xmns and -Xmnx _were_ specified */
		memoryParameters[opt_Xmns] = memoryParameters[opt_Xmn];
		memoryParameters[opt_Xmnx] = memoryParameters[opt_Xmn];
	}

	result = option_set_to_opt(vm, OPT_XMO, &index, EXACT_MEMORY_MATCH, &userOldSpaceSize);
	if (OPTION_OK != result) {
		goto _error;
	}
	memoryParameters[opt_Xmo] = index;
	if (memoryParameters[opt_Xmo] != -1) {
		/* check that -Xmos/-Xmox AND -Xmo weren't specified */
		if(memoryParameters[opt_Xmox] != -1) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_EXCLUSIVE, OPT_XMO, OPT_XMOX);
			return JNI_EINVAL;
		}
		if(memoryParameters[opt_Xmos] != -1) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_EXCLUSIVE, OPT_XMO, OPT_XMOS);
			return JNI_EINVAL;
		}
		
		extensions->minOldSpaceSize = userOldSpaceSize;
		extensions->oldSpaceSize = userOldSpaceSize;
		extensions->maxOldSpaceSize = userOldSpaceSize;

		/* Hack table to appear that -Xmos and -Xmox _were_ specified */		
		memoryParameters[opt_Xmos] = memoryParameters[opt_Xmo];
		memoryParameters[opt_Xmox] = memoryParameters[opt_Xmo];
	}

	result = option_set_to_opt_double(vm, OPT_XXMAXRAMPERCENT, &index, EXACT_MEMORY_MATCH, &extensions->maxRAMPercent);
	if (OPTION_OK != result) {
		goto _error;
	}
	memoryParameters[opt_maxRAMPercent] = index;
	if (memoryParameters[opt_maxRAMPercent] != -1) {
		if ((extensions->maxRAMPercent < 0.0) || (extensions->maxRAMPercent > 100.0)) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_PERCENT_OUT_OF_RANGE, "-XX:MaxRAMPercentage", 0.0, 100.0);
			return JNI_EINVAL;
		}
	}

	result = option_set_to_opt_double(vm, OPT_XXINITIALRAMPERCENT, &index, EXACT_MEMORY_MATCH, &extensions->initialRAMPercent);
	if (OPTION_OK != result) {
		goto _error;
	}
	memoryParameters[opt_initialRAMPercent] = index;
	if (memoryParameters[opt_initialRAMPercent] != -1) {
		if ((extensions->initialRAMPercent < 0.0) || (extensions->initialRAMPercent > 100.0)) {
			j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTIONS_PERCENT_OUT_OF_RANGE, "-XX:InitialRAMPercentage", 0.0, 100.0);
			return JNI_EINVAL;
		}
	}

	/* Parse the option to disable NUMA-awareness.  This is parsed on all platforms but only Balanced currently does anything
	 * with the result.
	 * Note that this option does NOT disable heap interleaving since that is considered a harmless and "passive" optimization.
	 */
	index = option_set(vm, OPT_NUMA_NONE, EXACT_MATCH);
	if (-1 != index) {
#if defined(J9VM_GC_VLHGC) || defined(J9VM_GC_GENERATIONAL)
		/* this is currently the same behaviour as our internal -Xgc:nonuma option */
		extensions->_numaManager.shouldEnablePhysicalNUMA(false);
		extensions->numaForced = true;		
#endif /* defined(J9VM_GC_VLHGC) || defined(J9VM_GC_GENERATIONAL) */
	}
	/* Since the user is not specifying a value, ensure that -Xmdx is set to the same value
	 * as -Xmx.  It does not matter whether -Xmx was specified or not.
	 */
	extensions->maxSizeDefaultMemorySpace = extensions->memoryMax;

	/* Parse for recognized Sovereign command line options.  Any duplication with an Xgc option
	 * will be overwritten.  This currently must be done after check for resource management so we can
	 * easily disallow -Xgcpolicy options.
	 */
	if (0 == gcParseSovereignArguments(vm)) {
		return JNI_EINVAL;
	}

	/* parse -XX: option that logicially belong to GC */
	if (0 == gcParseXXArguments(vm)) {
		return JNI_EINVAL;
	}

	/* Parse XXgc options */
	xxGCColonIndex = FIND_ARG_IN_VMARGS_FORWARD( STARTSWITH_MATCH, OPT_XXGC_COLON, NULL );
	while (xxGCColonIndex >= 0) {
		CONSUME_ARG(vmArgs, xxGCColonIndex);
		GET_OPTION_VALUE( xxGCColonIndex, ':', &xxGCOptions);
	
		/* Parse xxGCOptions arguments (if any) */
		if (NULL != xxGCOptions) {
			jint retCode;
		    if (JNI_OK != (retCode = gcParseXXgcArguments(vm, xxGCOptions))) {
				return retCode;
		    }
		}
		xxGCColonIndex = FIND_NEXT_ARG_IN_VMARGS_FORWARD( STARTSWITH_MATCH, OPT_XXGC_COLON, NULL, xxGCColonIndex);
	}		

	xGCColonIndex = FIND_ARG_IN_VMARGS_FORWARD( STARTSWITH_MATCH, OPT_XGC_COLON, NULL );
	while (xGCColonIndex >= 0) {
		CONSUME_ARG(vmArgs, xGCColonIndex);
		GET_OPTION_VALUE( xGCColonIndex, ':', &xGCOptions);
	
		/* Parse xGCOptions arguments (if any) */
		if (xGCOptions != NULL) {
			jint retCode;
		    if (JNI_OK != (retCode = gcParseXgcArguments(vm, xGCOptions))) {
				return retCode;
		    }			
		} else {
			return JNI_OK;
		}
		
        xGCColonIndex = FIND_NEXT_ARG_IN_VMARGS_FORWARD(STARTSWITH_MATCH, OPT_XGC_COLON, NULL, xGCColonIndex);
	}

#if defined(J9VM_GC_GENERATIONAL)
	/*
	 * If Split Heap is requested, -Xms, -Xmns and -Xmos will be overwritten
	 * so ignore them even specified
	 */
	if (extensions->enableSplitHeap) {
		memoryParameters[opt_Xms]  = -1;
		memoryParameters[opt_Xmns] = -1;
		memoryParameters[opt_Xmos] = -1;
	}
#endif /* defined(J9VM_GC_GENERATIONAL) */

	return JNI_OK;

_error:
	char *errorString = VMARGS_OPTION(index);

	/* show something meaningful about why parsing failed */
	switch(result) {
	case OPTION_MALFORMED:
		j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTION_MALFORMED, errorString);
		break;
	case OPTION_OVERFLOW:
		j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTION_OVERFLOW, errorString);
		break;
	case OPTION_ERROR:
		j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTION_ERROR, errorString);
		break;
	case OPTION_BUFFER_OVERFLOW:
		j9nls_printf(PORTLIB, J9NLS_ERROR, J9NLS_GC_OPTION_OVERFLOW, errorString);
		break;
	default:
		scan_failed(PORTLIB, "GC", errorString);
		break;
	}
	
	return JNI_EINVAL;
}

bool 
gcParseTGCCommandLine(J9JavaVM *vm)
{
	bool parseSuccess = true;
	MM_GCExtensions *extensions = MM_GCExtensions::getExtensions(vm);
	
	/* note that realtime currently doesn't support TGC so only standard and VLHGC should try to consume TGC arguments */
	if (extensions->isStandardGC() || extensions->isVLHGC() || extensions->isSegregatedHeap()) {
#if defined(J9VM_GC_MODRON_TRACE)
		J9VMInitArgs *vmArgs = vm->vmArgsArray;
		/* Parse Xtgc options */
		IDATA xTgcColonIndex = FIND_ARG_IN_VMARGS_FORWARD( STARTSWITH_MATCH, OPT_XTGC_COLON, NULL );
		while (parseSuccess && (xTgcColonIndex >= 0)) {
			char* xTgcOptions = NULL;
			CONSUME_ARG(vmArgs, xTgcColonIndex);
			GET_OPTION_VALUE( xTgcColonIndex, ':', &xTgcOptions);
	
			/* Parse xTgcOptions arguments (if any) */
			if (NULL != xTgcOptions) {
				if (!tgcParseArgs(vm, xTgcOptions) || !tgcInitializeRequestedOptions(vm)) {
					parseSuccess = false;
				}
			}
			xTgcColonIndex = FIND_NEXT_ARG_IN_VMARGS_FORWARD( STARTSWITH_MATCH, OPT_XTGC_COLON, NULL, xTgcColonIndex);
		}
#endif /* defined(J9VM_GC_MODRON_TRACE) */
	}

	return parseSuccess;
}
