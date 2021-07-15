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
package j9vm.test.ddrext;

import j9vm.test.ddrext.util.DDROutputStream;

import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Properties;
import java.util.Scanner;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import junit.framework.TestCase;

import org.testng.log4testng.Logger;

import com.ibm.j9ddr.tools.ddrinteractive.CommandUtils;
import com.ibm.j9ddr.tools.ddrinteractive.DDRInteractive;

public class DDRExtTesterBase extends TestCase {
	private static Logger log = Logger.getLogger(DDRExtTesterBase.class);
	public static ArrayList<String> coveredStructures = new ArrayList<String>();
	private String currentCommand = null;
	private String[] coreinfoOutput = null;
	private int javaVersion = -1;
	private int javaSr = -1;
	private String vmVersion = null;

	/**
	 * Executes a given DDR extension and returns its output 
	 * @param ddrExtCmd - DDR command to execute 
	 * @param arg - Argument of the DDR command 
	 * @return - Output of the DDR command 
	 */
	protected String exec(String ddrExtCmd, String[] arg) {
		String output = null;
		if (SetupConfig.getDDRContxt() != null) { // this means we are
			// running from ddr
			// plugin
			DDROutputStream mps = SetupConfig.getPrintStream();
			String argStr = "";
			for (int i = 0; i < arg.length; i++) {
				argStr = argStr + " " + arg[i];
			}
			log.debug("Execute command: !" + ddrExtCmd + " " + argStr);
			if (ddrExtCmd.startsWith("!") == false) {
				ddrExtCmd = "!" + ddrExtCmd;
			}
			currentCommand = "'" + ddrExtCmd + " " + argStr + "'";
			SetupConfig.getDDRContxt().execute(ddrExtCmd, arg,
					SetupConfig.getPrintStream());
			output = mps.getOutBuffer().toString();
			mps.clear();
		} else {
			DDRInteractive app = SetupConfig.getDDRInstance();
			// if app is null, then we try to initialize it using the dump
			// in the ddrext.properties file
			if (app == null) {
				String coreFilePath = getCoreFilePath();
				if (coreFilePath != null) {
					if (SetupConfig.init(coreFilePath)) {
						app = SetupConfig.getDDRInstance();
					} else {
						System.exit(-2);
					}
				}
			}
			if (app != null) {
				DDROutputStream mps = SetupConfig.getPrintStream();
				String argStr = "";
				for (int i = 0; i < arg.length; i++) {
					argStr = argStr + " " + arg[i];
				}
				log.debug("Execute command: !" + ddrExtCmd + " " + argStr);
				if (ddrExtCmd.startsWith("!") == false) {
					ddrExtCmd = "!" + ddrExtCmd;
				}
				currentCommand = "'" + ddrExtCmd + " " + argStr + "'";
				app.execute(ddrExtCmd, arg);
				output = mps.getOutBuffer().toString();
				mps.clear();
			} else {
				fail("DDRInterative is null. Please check CoreFilePath in ddrext.properties file.");
				return null;
			}
		}
		return output;
	}

	/**
	 * Validation method for DDR extension output
	 * Performs basic validation on a given DDR extension output. Basic validation includes
	 * checking if output is null, empty or contains 'unrecognized command' error.
	 * @param output - The DDR extension output string to validate
	 * @return - True if validation passes,false otherwise
	 */
	private boolean basicValidation(String output) {
		if (output == null || output.isEmpty()) {
			log.error(currentCommand + " output is null or empty");
			return false;
		} else if (output.contains(Constants.UNRECOGNIZED_CMD)) {
			log.error(currentCommand + " is not recognized");
			return false;
		}
		return true;
	}
	
	/**
	 * Validation method for DDR extension output. Check that successKey appears expectedMatches number of times
	 * 
	 * @param output - DDR extension output to validate.
	 * @param successKey - Comma separated list of tokens that must be available in the output for it to be valid.
	 * @param expectedMatches - number of times successKey must be seen
	 * @return True if validation passes, false otherwise. 
	 */
	protected boolean validate(String output, String successKey, int expectedMatches) {
		
		int matchesFound = 0;
		
		Pattern pattern = Pattern.compile(successKey.trim(), Pattern.CASE_INSENSITIVE);
		Matcher matcher = pattern.matcher(output);
		
		while (matcher.find()) {
			matchesFound = matchesFound + 1;
		}
		
		return (matchesFound == expectedMatches);
	}
	
	/**
	 * Validation method for DDR extension output
	 * @param output - DDR extension output to validate.
	 * @param successKeys - Comma separated list of tokens that must be available in the output for it to be valid.
	 * @param failureKeys - Comma separated list of tokens that must NOT be available in the output for it to be valid.
	 * @return True if validation passes, false otherwise. 
	 */
	protected boolean validate(String output, String successKeys,
			String failureKeys) {
		return validate(output, successKeys, failureKeys, false, false);
	}

	/**
	 * Validation method for DDR extension output
	 * @param output - DDR extension output to validate.
	 * @param successKeys - Comma separated list of tokens that must be available in the output for it to be valid.
	 * @param failureKeys - Comma separated list of tokens that must NOT be available in the output for it to be valid.
	 * @param goNextStep - Flag indicating whether or not we will perform exhaustive validation using all extension outputs available in this output.
	 * @return True if validation passes, false otherwise. 
	 */
	protected boolean validate(String output, String successKeys,
			String failureKeys, boolean goNextStep) {
		return validate(output, successKeys, failureKeys, goNextStep, false);
	}

	/**
	 * Validation method for DDR extension output
	 * @param output - DDR extension output to validate.
	 * @param successKeys - Comma separated list of tokens that must be available in the output for it to be valid.
	 * @param failureKeys - Comma separated list of tokens that must NOT be available in the output for it to be valid.
	 * @param goNextStep - Flag indicating whether or not we will perform exhaustive validation using all extension outputs available in this output.
	 * @param isNegativeTest - Flag indicating whether or not this is a negative test. If set, it will not fail validation if it sees a failure key in output.
	 * @return True if validation passes, false otherwise. 
	 */
	protected boolean validate(String output, String successKeys,
			String failureKeys, boolean goNextStep, boolean isNegativeTest) {
		log.info("Validation started for : " + currentCommand);
		if (basicValidation(output) == false) {
			return false;
		}

		if (!isNegativeTest) {
			if (runCommonFailureTest(output) == false) {
				return false;
			}
		}

		if (successKeys != null && successKeys.length() != 0) {
			String[] sKeys = successKeys.split(",");
			for (String successKey : sKeys) {
				Pattern pattern = Pattern.compile(successKey.trim(),
						Pattern.CASE_INSENSITIVE);
				Matcher matcher = pattern.matcher(output);
				if (matcher.find() == false) {
					log.error(currentCommand
							+ " output does not contain success key : "
							+ successKey);
					log.error(currentCommand + " output :"
							+ output);
					return false;
				}
			}
		}

		if (failureKeys != null && failureKeys.length() != 0) {
			String[] fKeys = failureKeys.split(",");
			for (String failKey : fKeys) {
				Pattern pattern = Pattern.compile(failKey.trim(),
						Pattern.CASE_INSENSITIVE);
				Matcher matcher = pattern.matcher(output);
				if (matcher.find()) {
					log.error(currentCommand
							+ " output contains failure key : " + failKey);
					log.error(currentCommand + " output :" + Constants.NL
							+ output);
					return false;
				}
			}
		}

		/*Performing exhaustive validation using all available structures (subcommands) in the output*/
		if (goNextStep) {
			int firstIndex = output.indexOf("!");
			String parentCmd = currentCommand;
			boolean allNextCmdAlreadyCovered = true;
			if (firstIndex != -1) { // if this output contains any !subcommand
				ArrayList<String> structuresToRun = new ArrayList<String>();
				output = output.substring(output.indexOf("!") + 1); // chop off
				Scanner scanner = new Scanner(output).useDelimiter("!");
				String structToRun = null;
				String structAddress = null;
				while (scanner.hasNext()) {
					String nextToken = scanner.next();
					String tokens[] = nextToken.split(" ");
					if (tokens.length > 1) {
						structToRun = tokens[0].trim();
						if (coveredStructures.contains(structToRun) == false) {
							if (structToRun.equals("void") == false) {
								coveredStructures.add(structToRun);
								structAddress = cleanse(tokens[1].trim());
								if (structToRun != null
										&& structAddress != null) {
									structuresToRun.add(structToRun + " "
											+ structAddress);
								}
								allNextCmdAlreadyCovered = false;
							}
						}
					}
				}
				if (allNextCmdAlreadyCovered == false) {
					log.info("Starting exhaustive DDR structure test for parent command : "
							+ parentCmd);
					for (int i = 0; i < structuresToRun.size(); i++) {
						currentCommand = structuresToRun.get(i);
						log.info("Runing structure test with : !"
								+ currentCommand);
						String[] tokens = currentCommand.trim().split(" ");
						String cmd = tokens[0];
						String argument = tokens[1];

						String nxtOutput = exec(cmd, new String[] { argument });

						if (basicValidation(nxtOutput) == false) {
							log.error("Structure validation failed");
							log.error(nxtOutput);
							continue;
						}

						if (nxtOutput.contains("{")
								&& nxtOutput.contains("}")
								&& nxtOutput.indexOf("{") < nxtOutput
								.indexOf("}")) {
							String structPropertyPattern = ".*=.* 0x.*|<FAULT>";
							Pattern pattern = Pattern
							.compile(structPropertyPattern);
							Matcher matcher = pattern.matcher(nxtOutput);
							if (matcher.find()
									&& runCommonFailureTestForSubCmd(nxtOutput)) {
								log.info("Structure validation passed");
								log.debug(nxtOutput);
							} else {
								log.error("Structure validation failed");
								log.error(nxtOutput);
							}
						} else {
							log.info("Can not validate: " + currentCommand
									+ " output is not a structure");
							log.debug(nxtOutput);
						}
					}
					currentCommand = parentCmd;
				}
			}
		}
		log.debug(output);
		log.info("Validation passed for : " + currentCommand);
		return true;
	}

	/**
	 * Helper method to perform cleanup on a parsed address string
	 * @param address
	 * @return
	 */
	private String cleanse(String address) {
		if (address.contains(Constants.NL)) {
			address = address.substring(0, address.indexOf(Constants.NL));
		}

		/* We make sure we are in fact dealing with a hex value string */
		if (address.startsWith("0x") == false) {
			return null;
		}

		if (address.contains(",")) {
			address = address.split(",")[0];
		}

		if (address.contains("\t")) {
			address = address.split("\t")[0];
		}
	
		if (address.endsWith(")") || address.endsWith(">")) {
			address = address.substring(0, address.length() - 1);
		}
		return address;
	}

	/**
	 * Method to run validation using the Constants.COMMON_FAILURE_KEYS
	 * @param output
	 * @return
	 */
	private boolean runCommonFailureTest(String output) {
		String[] cFailKeys = Constants.COMMON_FAILURE_KEYS.split(",");
		for (String cfKey : cFailKeys) {
			Pattern pattern = Pattern.compile(cfKey, Pattern.CASE_INSENSITIVE
					| Pattern.LITERAL);
			Matcher matcher = pattern.matcher(output);
			if (matcher.find()) {
				log.error(currentCommand
						+ " output contains common failure key : " + cfKey);
				log.error(currentCommand + " output :" + Constants.NL + output);
				return false;
			}
		}
		return true;
	}

	/**
	 * Common failure test for subcommands. Used during exhaustive validation of a ddr extension. 
	 * @param output
	 * @return
	 */
	private boolean runCommonFailureTestForSubCmd(String output) {
		String[] cFailKeys = Constants.COMMON_FAILURE_KEYS.split(",");
		for (String cfKey : cFailKeys) {
			if (output.contains(cfKey.trim())) {
				if (cfKey.trim().equals("<FAULT>")) {
					log.warn(currentCommand
							+ " output contains <FAULT> references");
					// log.warn(currentCommand+ " output :" + Constants.NL +
					// output);
					return true;
				}
				log.error(currentCommand
						+ " output contains common failure key : " + cfKey);
				log.error(currentCommand + " output :" + Constants.NL + output);
				return false;
			}
		}
		return true;
	}

	
	/**
	 * Get the address corresponding to the cmd in threadOutput
	 * 
	 * For example, if we wanted the address 0x03596100 corresponding to j9vmthread in the following string:
	 * 
	 * !stack 0x03596100	!j9vmthread 0x03596100	!j9thread 0x2aaab7c72b10	tid 0x5221 (21025) // (Thread-3)
	 * 
	 * 	we would call getAddressFor with:
	 * 
	 *  	threadOutput = the string
	 *  	eyeCatcher 	= "(Thread-3"
	 *  	cmd  		= "j9vmthread"
	 *  	cmdDelimiter= "\t"
	 *  
	 * @param threadOutput	the output to parses
	 * @param eyeCatcher	only search this string if eyeCatcher is present
	 * @param cmd			the cmd who's address we are after
	 * @param cmdDelimiter	the delimiter that separates multiples commands in the output
	 * @return 	the string representation of the address as it appears in threadOutput 
	 */
	public String getAddressFor(String threadOutput, String eyeCatcher, String cmd, String cmdDelimiter) {
		String[] lines = threadOutput.split(Constants.NL);
		for (int i = 0; i < lines.length; i++) {
			if ((null == eyeCatcher) || (lines[i].contains(eyeCatcher))) {
				String[] tokens = lines[i].split(cmdDelimiter);
				for (int j = 0; j < tokens.length; j++) {
					if (tokens[j].contains(cmd)) {
						return tokens[j].trim().split(" ")[1];
					}
				}
			}
		}
		return null;
	}


	/**
	 * Helper function that looks for the address cmd in threadOutput
	 * 
	 * See {@link DDRExtTesterBase#getAddressFor(String, String, String, String)}
	 * 
	 * @param cmd
	 * @param threadOutput
	 * @return the address corresponding to the command
	 */
	public String getAddressForThreads(String cmd, String threadOutput) {
		return getAddressFor(threadOutput, "(Thread-", cmd, "\t");
	}
	
	/**Used to get the full core file path from the ddrext.properties file
	 * @return
	 */
	private String getCoreFilePath() {
		String path = null;
		Properties ddrExtPros = new Properties();
		String key = "CoreFilePath";
		InputStream in;
		try {
			in = getClass().getResourceAsStream(
					"/" + Constants.DDREXT_PROPERTIES);
			log.debug("Loading File" + Constants.DDREXT_PROPERTIES + "...");
			ddrExtPros.load(in);
			path = ddrExtPros.getProperty(key);
			log.debug("key : " + key);
			log.debug("value : " + path);
			in.close();
		} catch (FileNotFoundException e) {
			log.error("Not able to find file " + Constants.DDREXT_PROPERTIES);
			log.error(e.toString());
		} catch (IOException e) {
			log.error("Not able to read file " + Constants.DDREXT_PROPERTIES);
			log.error(e.toString());
		}
		if (path.equals("")) {
			log.error("Please provide your dump location in "
					+ Constants.DDREXT_PROPERTIES);
			return null;
		}
		return path;
	}

	public static void resetList() {
		coveredStructures.clear();
	}
	
	/* Check whether core is generated on 64 bit platform or not. 
	 * Run setvm with the address that can exist only on 64 bit platform 
	 */
	public boolean is64BitPlatform(){
		String setVMOutput = exec(Constants.SETVM_CMD, new String[] {CommandUtils.UDATA_MAX_64BIT.toString()});
		boolean is64BitPlatform = true;
		/* If it is 32 bit platform, than it will complain about address being too large */
		if (validate(setVMOutput, "is larger than the max available memory address: 0xFFFFFFFF", null)) {
			is64BitPlatform = false;
		} 
		return is64BitPlatform;
	}
	
	/**
	 * get and save the output from !coreinfo
	 * @return array of strings
	 */
	protected String[] getCoreInfo() {
		if (null == coreinfoOutput) {
			String execOutput = exec(Constants.COREINFO_CMD, new String[] {});
			/* split on any kind of line-end: CR-LF, LF or CR */
			coreinfoOutput = execOutput.split("\r\n|\n|\r");
		}
		return coreinfoOutput;
	}

	/**
	 * @return the service release number.  GA versions returns 0, unknown returns -2
	 */
	protected int getJavaSr() {
		if (javaSr != -1) {
			return javaSr;
		}
		String[] coreInfo = getCoreInfo();
		Pattern versionMatcher = Pattern.compile(".*\\(SR(\\d+)\\).*"); /* look for (SRn), where "n" is a string of digits */
		for (int i = 0; i < (coreInfo.length - 1); ++i) {
			if (coreInfo[i].matches("JAVA SERVICE LEVEL INFO.*")) {
				Matcher m = versionMatcher.matcher(coreInfo[i + 1]);
				if (m.matches()) {
					String version = m.group(1); /* get the "n" described above */
					javaSr = Integer.parseInt(version);
				} else {
					javaSr = 0;
				}
				return javaSr;
			}
		}
		javaSr = -2;
		return javaSr;
	}

	/**
	 * Does lazy evaluation and returns the result.
	 * Returns 0 if the version cannot be determined, e.g. for Java 5, 6 (R24).
	 * @return the java version, i.e. Java 6, 7, etc.
	 */
	protected int getJavaVersion() {
		if (javaVersion != -1) {
			return javaVersion;
		}
		String[] coreInfo = getCoreInfo();
		Pattern versionMatcher = Pattern.compile("JRE|RUNTIME\\s+1\\.(\\d+).*"); /* look for JRE 1.n, where "n" is a string of digits */
		Pattern versionMatcherForJava9 = Pattern.compile("JRE|RUNTIME\\s(\\d+).*"); /* look for JRE 9 */

		for (int i = 0; i < (coreInfo.length - 1); ++i) {
			if (coreInfo[i].matches("JAVA VERSION INFO.*")) {
				Matcher m = versionMatcher.matcher(coreInfo[i + 1]); /* look on the next line */
				if (m.matches()) {
					String version = m.group(1); /* get the "n" described above */
					javaVersion = Integer.parseInt(version);
					return javaVersion;
				} else {
					m = versionMatcherForJava9.matcher(coreInfo[i + 1]);
					if (m.matches()) {
						String version = m.group(1); /* get the "n" described above */
						javaVersion = Integer.parseInt(version);
						return javaVersion;
					}
				}
				break; /* cannot get Java version */
			}
		}
		javaVersion = 0;
		return javaVersion;
	}
	
	/**
	 * @return the version string for the J9 VM, e.g. "2.6", or "unknown"
	 */
	protected String getVmVersion() {
		if (vmVersion != null) {
			return vmVersion;
		}
		String[] coreInfo = getCoreInfo();
		Pattern versionMatcher = Pattern.compile("JAVA VM VERSION\\s*-\\s*(.*)");
		for (int i = 0; i < coreInfo.length; ++i) {
			if (coreInfo[i].matches("JAVA VM VERSION.*")) {
				Matcher m = versionMatcher.matcher(coreInfo[i]);
				if (m.matches()) {
					vmVersion = m.group(1); /* get the "n" described above */
					return vmVersion;
				}
				break;
			}
		}
		vmVersion = "unknown";
		return vmVersion;
	}

	/**
	 * @return a human-friendly description of the Java version, release, and VM stream.
	 */
	protected String getVersionString() {
		String javaVer = (javaVersion > 0) ? ("Java " + javaVersion) : "unknown Java version";
		String sr;
		if (javaSr < 0) {
			sr = "unknown release";
		} else if (0 == javaSr) {
			sr = "GA";
		} else {
			sr = "SR" + javaSr;
		}
		String vmVer = (null != vmVersion) ? ("VM R" + vmVersion) : "unknown VM";
		return javaVer + " " + sr + " " + vmVer;
	}
}
