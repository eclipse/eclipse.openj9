/*[INCLUDE-IF Sidecar18-SE]*/
/*******************************************************************************
 * Copyright (c) 2004, 2021 IBM Corp. and others
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
package com.ibm.jvm.j9.dump.extract;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.PrintWriter;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collection;
import java.util.Collections;
import java.util.HashSet;
import java.util.Iterator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Properties;
import java.util.Set;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

import com.ibm.dtfj.addressspace.IAbstractAddressSpace;
import com.ibm.dtfj.corereaders.Builder;
import com.ibm.dtfj.corereaders.ClosingFileReader;
import com.ibm.dtfj.corereaders.DumpFactory;
import com.ibm.dtfj.corereaders.ICoreFileReader;

public class Main {

	private static class DummyBuilder implements Builder {

		private static class Register {
			final String name;
			final long value;

			Register(String name, long value) {
				this.name = name;
				this.value = value;
			}
		}

		/**
		 * Support for the "-p" option.  If this is non-null, absolute paths
		 * will be resolved against this path as their root.
		 */
		private File _virtualRootDirectory = null;

		/**
		 * Used to get around a problem on AIX where the core specifies the main
		 * binary by name only.  Any paths where we successfully find files will
		 * be added to this list and it will be consulted for future resolutions
		 * of files which we can't find.
		 *
		 * The members are File objects representing the directories to search.
		 */
		private List<File> _successfulSearchPaths = new ArrayList<>();

		private final long _environmentPointer;

		/**
		 * @param rootDirectory The path to prepend to all absolute path file look-ups.
		 * Can be null if we are to use the given paths verbatim.
		 * @param environment Address of environment section. Can be 0.
		 */
		public DummyBuilder(File rootDirectory, long environment) {
			_virtualRootDirectory = rootDirectory;

			// See comment about AIX above. In Java 6 the JVM libraries have moved to platform directories in sdk/jre/lib
			// and the main binary is still in sdk/jre/bin, so fix is to prime the extra search paths with sdk/jre/bin.
			String jre_bin = System.getProperty("java.home") + File.separator + "bin"; //$NON-NLS-1$ //$NON-NLS-2$
			File javaPath = new File(jre_bin);
			_successfulSearchPaths.add(javaPath);
			this._environmentPointer = environment;
		}

		@Override
		public Object buildProcess(Object addressSpace, String pid, String commandLine, Properties environment, Object currentThread, Iterator threads, Object executable, Iterator libraries, int addressSize) {
			return new Object();
		}

		@Override
		public Object buildRegister(String name, Number value) {
			return new Register(name, value.longValue());
		}

		@Override
		public Object buildStackSection(Object addressSpace, long stackStart, long stackEnd) {
			return new Object();
		}

		@Override
		public Object buildThread(String name, Iterator registers, Iterator stackSections, Iterator stackFrames,
				Properties properties, int signalNumber) {
			return new Object();
		}

		@Override
		public Object buildModuleSection(Object addressSpace, String name, long imageStart, long imageEnd) {
			return new Object();
		}

		@Override
		public Object buildModule(String name, Properties properties, Iterator sections, Iterator symbols,
				long loadAddress) {
			return new Object();
		}

		@Override
		public long getEnvironmentAddress() {
			return _environmentPointer;
		}

		@Override
		public long getValueOfNamedRegister(List registers, String name) {
			for (Iterator<?> iter = registers.iterator(); iter.hasNext();) {
				Register register = (Register) iter.next();
				if (name.equals(register.name))
					return register.value;
			}
			return -1;
		}

		@Override
		public Object buildStackFrame(Object addressSpace, long stackBasePointer, long pc) {
			return new Object();
		}

		@Override
		public ClosingFileReader openFile(String nameOrPath) throws IOException {
			File fileRep = new File(nameOrPath);

			if ((null != _virtualRootDirectory) && fileRep.isAbsolute()) {
				//this is to allow system files (referenced via absolute paths) from the target machine to be put into a different directory as to not conflict with the system files on the host machine
				fileRep = sysFileRelative(_virtualRootDirectory, fileRep);
			} else if (fileRep.exists()) {
				ClosingFileReader reader = new ClosingFileReader(fileRep);
				File parent = fileRep.getParentFile();
				if (null != parent) {
					File absolute = parent.getAbsoluteFile();
					//we managed to open the file here so add this to our list of search paths if it isn't already one
					if (!_successfulSearchPaths.contains(absolute)) {
						_successfulSearchPaths.add(absolute);
					}
				}
				return reader;
			} else if (!fileRep.isAbsolute()) {
				//the file is relative and doesn't exist so try looking for it in the other locations where we have seen files
				Iterator<File> paths = _successfulSearchPaths.iterator();
				String filename = fileRep.getName();
				while (paths.hasNext()) {
					File path = paths.next();
					File nextPath = new File(path, filename);
					if (nextPath.exists()) {
						return new ClosingFileReader(nextPath);
					}
				}
			}

			return new ClosingFileReader(fileRep);
		}

		/**
		 * Used to fix the look-up location of system files listed as absolute paths.
		 * Note that relative paths will be unchanged but absolute paths will inherit
		 * virtualRoot as there actual root (note that virtualRoot can be relative).
		 * @param virtualRoot
		 * @param originalPath
		 * @return A File which represents the file at originalPath, re-rooting at
		 * virtualRoot if it was absolute.
		 */
		private static File sysFileRelative(File virtualRoot, File originalPath) {
			File temp = originalPath;
			while (null != temp.getParentFile()) {
				temp = temp.getParentFile();
			}
			File result = originalPath;
			if (originalPath.isAbsolute()) {
				String middle = originalPath.getAbsolutePath().substring(temp.getAbsolutePath().length());
				result = new File(virtualRoot, middle);
			}
			return result;
		}

		@Override
		public Object buildSymbol(Object addressSpace, String functionName, long relocatedFunctionAddress) {
			return new Object();
		}

		@Override
		public void setExecutableUnavailable(String description) {
			// Do nothing
		}

		@Override
		public Object buildAddressSpace(String name, int id) {
			return new Object();
		}

		@Override
		public void setOSType(String osType) {
			// Do nothing
		}

		@Override
		public void setCPUType(String cpuType) {
			// Do nothing
		}

		@Override
		public void setCPUSubType(String subType) {
			// Do nothing
		}

		@Override
		public void setCreationTime(long millis) {
			// Do nothing
		}

		@Override
		public Object buildCorruptData(Object addressSpace, String message, long address) {
			return new Object();
		}
	}

	private final String _dumpName;
	private final File _virtualRootDirectory;
	private final boolean _verbose;
	private final boolean _excludeCoreFile;
	private static boolean _throwExceptions;
	private ICoreFileReader _dump;
	private static final String J9_LIB_NAME = "j9jextract"; //$NON-NLS-1$
	/* This is the buffer size used when copying files into the ZIP stream.
	 * Making it bigger should improve performance on z/OS and there is only
	 * one instance of this, anyway.
	 */
	private static final int ZIP_BUFFER_SIZE = 8 * 4096;
	private final DummyBuilder _builder;

	// jextract return codes
	private static int JEXTRACT_SUCCESS = 0;
	private static int JEXTRACT_SYNTAX_ERROR = 1;
	private static int JEXTRACT_FILE_ERROR = 2;
	private static int JEXTRACT_NOJVM_ERROR = 3;
	private static int JEXTRACT_ADDRESS_ERROR = 4;
	private static int JEXTRACT_VERSION_ERROR = 5;
	private static int JEXTRACT_INTERNAL_ERROR = 6;

	public static void main(String[] args) {
		String dumpName = null;
		String outputName = null;
		File virtualRootDirectory = null;
		boolean ignoreOptions = false;
		boolean interactive = false;
		boolean verbose = false;
		boolean throwExceptions = false;
		boolean zip = true;
		boolean disableBuildIdCheck = false;
		boolean excludeCoreFile = false;

		if (args.length == 0) {
			usageMessage(null, JEXTRACT_SUCCESS);
		}

		for (int i = 0; i < args.length; i++) {
			if (!ignoreOptions && args[i].startsWith("-")) { //$NON-NLS-1$
				if ("--".equals(args[i])) { //$NON-NLS-1$
					ignoreOptions = true;
				} else if ("-interactive".equals(args[i])) { //$NON-NLS-1$
					interactive = true;
				} else if ("-help".equals(args[i])) { //$NON-NLS-1$
					usageMessage(null, JEXTRACT_SUCCESS);
				} else if ("-f".equals(args[i])) { //$NON-NLS-1$
					// The next argument is the name of the executable
					i++;
					ensure(i < args.length && !args[i].startsWith("-"), //$NON-NLS-1$
							"Syntax error: -f option specified with no filename following"); //$NON-NLS-1$
					File file = new File(args[i]);
					ensure(file.exists() && file.canRead(),
							"File specified using -f option (\"" + args[i] + "\") not found."); //$NON-NLS-1$ //$NON-NLS-2$
					// Pass the executable name into the core readers via a system property
					System.setProperty("com.ibm.dtfj.corereaders.executable", args[i]); //$NON-NLS-1$
				} else if ("-p".equals(args[i])) { //$NON-NLS-1$
					// The next argument is the root directory which we should prepend to all paths (relative or absolute)
					i++;
					ensure(i < args.length && !args[i].startsWith("-"), //$NON-NLS-1$
							"Syntax error: -p option specified but no virtual root directory given"); //$NON-NLS-1$
					File file = new File(args[i]);
					ensure(file.exists() && file.canRead() && file.isDirectory(),
							"Virtual directory specified using -p option (\"" + args[i] + "\") does not exist as a readable directory."); //$NON-NLS-1$ //$NON-NLS-2$
					virtualRootDirectory = new File(args[i]);
				} else if (args[i].equals("-v")) { //$NON-NLS-1$
					verbose = true;
				} else if (args[i].equals("-e")) { //$NON-NLS-1$
					throwExceptions = true;
				} else if (args[i].equals("-r")) { //$NON-NLS-1$
					disableBuildIdCheck = true;
				} else if (args[i].equals("-x")) { //$NON-NLS-1$
					excludeCoreFile = true;
				} else {
					usageMessage("Unrecognized option: " + args[i], JEXTRACT_SYNTAX_ERROR); //$NON-NLS-1$
				}
			} else if (dumpName == null) {
				dumpName = args[i];
			} else if (outputName == null) {
				outputName = args[i];
			} else {
				usageMessage("Too many file arguments: " + args[i], JEXTRACT_SYNTAX_ERROR); //$NON-NLS-1$
			}
		}

		ensure(null != dumpName, "No dump file specified"); //$NON-NLS-1$

		Main dumper = new Main(dumpName, virtualRootDirectory, verbose, throwExceptions, disableBuildIdCheck, excludeCoreFile);

		if (interactive) {
			dumper.runInteractive();
		} else {
			if (zip) {
				dumper.runZip(outputName);
			}
			report("jextract complete."); //$NON-NLS-1$
		}

		if (dumper._dump != null) {
			try {
				dumper._dump.releaseResources();
			} catch (IOException e) {
				// ignore
			}
		}
	}

	private static void ensure(boolean condition, String errorMessage) {
		if (false == condition) {
			usageMessage(errorMessage, JEXTRACT_SYNTAX_ERROR);
		}
	}

	private static void usageMessage(String message, int code) {
		report("Usage: jextract [options] dump_name [output_filename]"); //$NON-NLS-1$
		report("  output filename defaults to dump_name.zip"); //$NON-NLS-1$
		report("  options:"); //$NON-NLS-1$
		report("    -e                  throw exceptions instead of calling System.exit()"); //$NON-NLS-1$
		report("    -f executable_name  override executable name"); //$NON-NLS-1$
		report("    -help               print this usage message"); //$NON-NLS-1$
		report("    -p prefix           prefix for all paths (absolute or relative)"); //$NON-NLS-1$
		report("    -r                  relax version checking"); //$NON-NLS-1$
		report("    -v                  enable verbose output"); //$NON-NLS-1$
		report("    -x                  exclude dump_name from output file"); //$NON-NLS-1$
		report("    --                  mark end of options"); //$NON-NLS-1$
		errorMessage(message, code);
	}

	private static void errorMessage(String message, int code) {
		if (_throwExceptions) {
			throw new JExtractFatalException(message, code);
		} else {
			if (message != null) {
				report(message);
			}
			System.exit(code);
		}
	}

	private static void errorMessage(String message, int code, Throwable throwable) {
		throwable.printStackTrace();
		if (_throwExceptions) {
			throw new JExtractFatalException(message, code);
		} else {
			if (message != null) {
				report(message);
			}
			System.exit(code);
		}
	}

	private Main(String dumpName, File virtualRootDirectory, boolean verbose, boolean throwExceptions,
			boolean disableBuildIdCheck, boolean excludeCoreFile) {
		// System.err.println("Main.Main entered dn=" + dumpName + " vrd=" + virtualRootDirectory + " v=" + verbose);

		_dumpName = dumpName;
		_virtualRootDirectory = virtualRootDirectory;
		_verbose = verbose;
		_throwExceptions = throwExceptions;
		_excludeCoreFile = excludeCoreFile;

		try {
			System.loadLibrary(J9_LIB_NAME);
		} catch (SecurityException e) {
			errorMessage("Error. Security permissions don't allow required native library to be loaded.", JEXTRACT_INTERNAL_ERROR); //$NON-NLS-1$
		} catch (UnsatisfiedLinkError e) {
			errorMessage("Error. Native library " + J9_LIB_NAME + " cannot be found. Please check your path.", JEXTRACT_INTERNAL_ERROR); //$NON-NLS-1$ //$NON-NLS-2$
		} catch (Exception e) {
			errorMessage("Error. Unexpected exception occurred loading: " + J9_LIB_NAME, JEXTRACT_INTERNAL_ERROR, e); //$NON-NLS-1$
		}

		report("Loading dump file..."); //$NON-NLS-1$

		File dumpFile = new File(dumpName);
		ClosingFileReader reader = null;
		try {
			reader = new ClosingFileReader(dumpFile);
		} catch (FileNotFoundException e) {
			if (false == dumpFile.exists()) {
				errorMessage("Error. Could not find dump file: " + dumpName, JEXTRACT_FILE_ERROR); //$NON-NLS-1$
			} else if (false == dumpFile.canRead()) {
				errorMessage("Error. Unable to read dump file (check permission): " + dumpName, JEXTRACT_FILE_ERROR); //$NON-NLS-1$
			} else {
				errorMessage("Error. Unexpected FileNotFoundException occurred opening: " + dumpName, JEXTRACT_FILE_ERROR, e); //$NON-NLS-1$
			}
		} catch (IOException e) {
			errorMessage(e.getMessage(), JEXTRACT_FILE_ERROR);
		}

		try {
			_dump = DumpFactory.createDumpForCore(reader, _verbose);
		} catch (Exception e) {
			errorMessage("Error. Unexpected Exception occurred opening: " + dumpName, JEXTRACT_FILE_ERROR, e); //$NON-NLS-1$
		}

		if (null == _dump) {
			errorMessage("Error. Dump type not recognised, file: " + dumpName, JEXTRACT_FILE_ERROR); //$NON-NLS-1$
		}
		if (_dump.isTruncated()) {
			report("Warning: dump file is truncated. Extracted information may be incomplete."); //$NON-NLS-1$
		}

		long environmentPointer = 0;
		try {
			environmentPointer = getEnvironmentPointer(_dump.getAddressSpace(), disableBuildIdCheck);
		} catch (Throwable t) {
			errorMessage("Error. Unable to locate executable for " + dumpName, JEXTRACT_INTERNAL_ERROR, t); //$NON-NLS-1$
		}

		_builder = new DummyBuilder(_virtualRootDirectory, environmentPointer);
		// extract does the bulk of core-reader processing
		_dump.extract(_builder);

		report("Read memory image from " + _dumpName); //$NON-NLS-1$
	}

	private void runZip(String outputName) {
		// Zip up the dump, libraries and (optionally) XML file
		Set<String> files = new LinkedHashSet<>();
		files.add(_dumpName);

		for (Iterator<?> iter = _dump.getAdditionalFileNames(); iter.hasNext();) {
			files.add((String) iter.next());
		}

		if (_excludeCoreFile) {
			files.remove(_dumpName);
		}

		try {
			// Add trace formatting template files (Traceformat.dat, J9TraceFormat.dat and OMRTraceFormat.dat) into the archive.
			String lib_dir = System.getProperty("java.home") + File.separator + "lib" + File.separator; //$NON-NLS-1$ //$NON-NLS-2$
			String trace = lib_dir + "TraceFormat.dat"; //$NON-NLS-1$
			if (new File(trace).exists()) {
				files.add(trace);
			}
			String j9trace = lib_dir + "J9TraceFormat.dat"; //$NON-NLS-1$
			if (new File(j9trace).exists()) {
				files.add(j9trace);
			}
			String omrtrace = lib_dir + "OMRTraceFormat.dat"; //$NON-NLS-1$
			if (new File(omrtrace).exists()) {
				files.add(omrtrace);
			}

			// Add the debugger extension library into the zip, available only on AIX
			String osName = System.getProperty("os.name"); //$NON-NLS-1$
			if ("AIX".equalsIgnoreCase(osName)) { //$NON-NLS-1$
				files.add("libdbx_j9.so"); //$NON-NLS-1$
			}
		} catch (Exception e) {
			// Ignore
		}

		String zipFileName = (null != outputName) ? outputName : _dumpName.concat(".zip"); //$NON-NLS-1$
		Set<String> excluded = _excludeCoreFile ? Collections.singleton(_dumpName) : Collections.emptySet();
		try {
			createZipFromFileNames(zipFileName, files, excluded, _builder);
		} catch (Exception e) {
			errorMessage(e.getMessage(), JEXTRACT_INTERNAL_ERROR, e);
		}
	}

	private void runInteractive() {
		report("Jextract interactive mode."); //$NON-NLS-1$
		report("Type '!j9help' for help."); //$NON-NLS-1$
		report("Type 'quit' to quit."); //$NON-NLS-1$
		report("(Commands must be prefixed with '!')"); //$NON-NLS-1$

		IAbstractAddressSpace addressSpace = _dump.getAddressSpace();

		if (addressSpace == null) {
			report("Error. Address space not found in dump: " + _dumpName //$NON-NLS-1$
					+ ". Dump is truncated, corrupted or does not contain a supported JVM."); //$NON-NLS-1$
			return;
		}

		BufferedReader input = new BufferedReader(new InputStreamReader(System.in));

		try {
			while (true) {
				report("> "); //$NON-NLS-1$
				String command = input.readLine().trim();
				if ("quit".equalsIgnoreCase(command) || "q".equalsIgnoreCase(command)) { //$NON-NLS-1$ //$NON-NLS-2$
					break;
				}
				try {
					doCommand(addressSpace, command);
				} catch (Throwable e) {
					report(e.getMessage());
					report("Failure detected during command execution, see previous message(s)."); //$NON-NLS-1$
				}
			}
		} catch (IOException e) {
			report("Error reading input."); //$NON-NLS-1$
		}
	}

	private static void report(String message) {
		System.err.println(message);
	}

	private static void createZipFromFileNames(String zipFileName, Collection<String> fileNames,
			Collection<String> excludedNames, Builder fileResolver) throws Exception {
		report("Creating archive file: " + zipFileName); //$NON-NLS-1$
		try {
			ZipOutputStream zip = new ZipOutputStream(new FileOutputStream(zipFileName));
			if (!excludedNames.isEmpty()) {
				final String excludedFilesFileName = "excluded-files.txt"; //$NON-NLS-1$

				report("Adding \"" + excludedFilesFileName + "\""); //$NON-NLS-1$ //$NON-NLS-2$

				ZipEntry zipEntry = new ZipEntry(excludedFilesFileName);

				zipEntry.setTime(System.currentTimeMillis());

				zip.putNextEntry(zipEntry);

				try {
					@SuppressWarnings("resource" /* we can't close noteWriter */)
					PrintWriter noteWriter = new PrintWriter(new OutputStreamWriter(zip, StandardCharsets.UTF_8));

					noteWriter.println("Files omitted from archive"); //$NON-NLS-1$
					noteWriter.println("=========================="); //$NON-NLS-1$

					for (String excludedName : excludedNames) {
						noteWriter.println(excludedName);
					}

					noteWriter.flush();
				} finally {
					zip.closeEntry();
				}
			}
			byte[] buffer = new byte[ZIP_BUFFER_SIZE];
			Set<String> filesInZip = new HashSet<>();
			for (String name : fileNames) {
				try (ClosingFileReader in = fileResolver.openFile(name)) {
					boolean mvsfile = in.isMVSFile();
					String absolute = in.getAbsolutePath();
					if (mvsfile || absolute.equals(new File(name).getAbsolutePath())) {
						report("Adding \"" + name + "\""); //$NON-NLS-1$ //$NON-NLS-2$
					} else {
						report("Adding \"" + name + "\" (found at \"" + absolute + "\")"); //$NON-NLS-1$ //$NON-NLS-2$ //$NON-NLS-3$
					}
					if (mvsfile) {
						// mvs files exist in a different filespace, so read the file from
						// mvs and insert it into the zip.
						zip.putNextEntry(new ZipEntry(name));
						filesInZip.add(name);
						copy(in, zip, buffer);
					} else {
						// Add files by absolute name so they have the right path in the zip.
						// Guard against two names in fileNames mapping to the same absolute path.
						if (!filesInZip.contains(absolute)) {
							// note that we can't just use the file name, we have to use
							// the full path since they may share a name
							// note also that we will use the original path and not the
							// path with a virtual root prepended to it
							InputStream fileStream = in.streamFromFile();
							ZipEntry zipEntry = new ZipEntry(absolute);
							filesInZip.add(absolute);
							zipEntry.setTime(new File(absolute).lastModified());
							zip.putNextEntry(zipEntry);

							copy(fileStream, zip, buffer);
							fileStream.close();
						}
					}
				} catch (FileNotFoundException e) {
					report("Warning:  Could not find file \"" + name + "\" for inclusion in archive"); //$NON-NLS-1$ //$NON-NLS-2$
				} catch (IOException e) {
					throw new Exception("Failure adding file " + name + " to archive", e); //$NON-NLS-1$ //$NON-NLS-2$
				} finally {
					zip.closeEntry();
				}
			}
			try {
				zip.close();
			} catch (IOException e) {
				throw new Exception("Failure closing archive file (" + zipFileName + ") : " + e.getMessage()); //$NON-NLS-1$ //$NON-NLS-2$
			}
		} catch (FileNotFoundException e) {
			throw new Exception("Could not find archive file to output to: " + e.getMessage()); //$NON-NLS-1$
		}
	}

	/**
	 * Copies from the given input stream to the given output stream using the buffer provided (this is for file copying, zipping, etc)
	 *
	 * @param from The stream to read the data from
	 * @param to The stream to write the data to
	 * @param buffer The buffer to use to hold intermediate data (this buffer may be quite large so re-using it from the callsite allows for less re-allocation of the same large buffer)
	 * @throws IOException
	 */
	private static void copy(InputStream from, OutputStream to, byte[] buffer) throws IOException {
		for (;;) {
			int count = from.read(buffer);

			if (count == -1) {
				break;
			}

			to.write(buffer, 0, count);
		}
	}

	/**
	 * Copies from the given ClosingFileReader to the given output stream using the buffer provided (this is for file copying, zipping, etc)
	 *
	 * @param from The ClosingFileReader to read from
	 * @param to The stream to write the data to
	 * @param buffer The buffer to use to hold intermediate data (this buffer may be quite large so re-using it from the callsite allows for less re-allocation of the same large buffer)
	 * @throws IOException
	 */
	private static void copy(ClosingFileReader from, OutputStream to, byte[] buffer) throws IOException {
		for (;;) {
			int count = from.read(buffer);

			if (count == -1) {
				break;
			}

			to.write(buffer, 0, count);
		}
	}

	private native long getEnvironmentPointer(IAbstractAddressSpace dump, boolean disableBuildIdCheck) throws Exception;

	private native void doCommand(IAbstractAddressSpace dump, String command) throws Exception;

}
