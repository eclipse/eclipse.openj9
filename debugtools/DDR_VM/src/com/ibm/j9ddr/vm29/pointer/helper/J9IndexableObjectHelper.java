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
package com.ibm.j9ddr.vm29.pointer.helper;

import java.io.StringWriter;

import com.ibm.j9ddr.CorruptDataException;
import com.ibm.j9ddr.vm29.j9.ObjectModel;
import com.ibm.j9ddr.vm29.pointer.I16Pointer;
import com.ibm.j9ddr.vm29.pointer.I32Pointer;
import com.ibm.j9ddr.vm29.pointer.I64Pointer;
import com.ibm.j9ddr.vm29.pointer.ObjectReferencePointer;
import com.ibm.j9ddr.vm29.pointer.U16Pointer;
import com.ibm.j9ddr.vm29.pointer.U8Pointer;
import com.ibm.j9ddr.vm29.pointer.VoidPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9ArrayClassPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9BuildFlags;
import com.ibm.j9ddr.vm29.pointer.generated.J9ClassPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9IndexableObjectContiguousPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9IndexableObjectContiguousCompressedPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9IndexableObjectDiscontiguousPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9IndexableObjectDiscontiguousCompressedPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9IndexableObjectContiguousFullPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9IndexableObjectDiscontiguousFullPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9IndexableObjectPointer;
import com.ibm.j9ddr.vm29.pointer.generated.J9ObjectPointer;
import com.ibm.j9ddr.vm29.structure.J9IndexableObjectContiguous;
import com.ibm.j9ddr.vm29.structure.J9IndexableObjectContiguousCompressed;
import com.ibm.j9ddr.vm29.structure.J9IndexableObjectContiguousFull;
import com.ibm.j9ddr.vm29.structure.J9IndexableObjectDiscontiguous;
import com.ibm.j9ddr.vm29.structure.J9IndexableObjectDiscontiguousCompressed;
import com.ibm.j9ddr.vm29.structure.J9IndexableObjectDiscontiguousFull;
import com.ibm.j9ddr.vm29.types.U32;
import com.ibm.j9ddr.vm29.types.UDATA;

public class J9IndexableObjectHelper extends J9ObjectHelper 
{
	public static U32 flags(J9IndexableObjectPointer objPointer) throws CorruptDataException
	{		
		return J9ObjectHelper.flags(J9ObjectPointer.cast(objPointer));
	}

	public static J9ArrayClassPointer clazz(J9IndexableObjectPointer objPointer) throws CorruptDataException
	{	
		return J9ArrayClassPointer.cast(J9ObjectHelper.clazz(J9ObjectPointer.cast(objPointer)));
	}
	
	public static UDATA monitor(J9IndexableObjectPointer objPointer) throws CorruptDataException 
	{
		return J9ObjectHelper.monitor(J9ObjectPointer.cast(objPointer));
	}
	
	public static String getClassName(J9IndexableObjectPointer objPointer) throws CorruptDataException 
	{
		return J9ArrayClassHelper.getName(clazz(objPointer));
	}

	public static U32 rawSize(J9IndexableObjectPointer objPointer) throws CorruptDataException 
	{
		if (mixedReferenceMode) {
			try {
				if (compressObjectReferences) {
					return (U32)J9IndexableObjectContiguousCompressedPointer.cast(objPointer).size();
				}
				return (U32)J9IndexableObjectContiguousFullPointer.cast(objPointer).size();
			} catch (NoSuchFieldException e) {
				// the 'size' field should be present in a VM that supports mixed reference mode
				throw new CorruptDataException(e);
			}
		}
		return (U32)J9IndexableObjectContiguousPointer.cast(objPointer).size();
	}

	public static U32 size(J9IndexableObjectPointer objPointer) throws CorruptDataException 
	{
		U32 size = rawSize(objPointer);
		if (size.isZero()) {
			if (mixedReferenceMode) {
				try {
					if (compressObjectReferences) {
						size = (U32)J9IndexableObjectDiscontiguousCompressedPointer.cast(objPointer).size();
					} else {
						size = (U32)J9IndexableObjectDiscontiguousFullPointer.cast(objPointer).size();
					}
				} catch (NoSuchFieldException e) {
					// the 'size' field should be present in a VM that supports mixed reference mode
					throw new CorruptDataException(e);
				}
			} else {
				size = (U32)J9IndexableObjectDiscontiguousPointer.cast(objPointer).size();
			}
		}
		if (size.anyBitsIn(0x80000000)) {
			throw new CorruptDataException("java array size with sign bit set");
		}
		return size;
	}
	
	public static U32 size(J9ObjectPointer objPointer) throws CorruptDataException 
	{
		return size(J9IndexableObjectPointer.cast(objPointer));
	}

	public static long contiguousHeaderSize()
	{
		if (mixedReferenceMode) {
			if (compressObjectReferences) {
				return J9IndexableObjectContiguousCompressed.SIZEOF;
			}
			return J9IndexableObjectContiguousFull.SIZEOF;
		}
		return J9IndexableObjectContiguous.SIZEOF;
	}

	public static long discontiguousHeaderSize()
	{
		if (mixedReferenceMode) {
			if (compressObjectReferences) {
				return J9IndexableObjectDiscontiguousCompressed.SIZEOF;
			}
			return J9IndexableObjectDiscontiguousFull.SIZEOF;
		}
		return J9IndexableObjectDiscontiguous.SIZEOF;
	}

	/**
	 * @param objPointer array object who's elements we are outputting to dst
	 * @param index the desired index within then array
	 * @param dataSize size of the data held in the array
	 * @return the address for the desired element in the array
	 * @throws CorruptDataException 
	 */
	public static VoidPointer getElementEA(J9IndexableObjectPointer objPointer, int index, int dataSize) throws CorruptDataException
	{
		return ObjectModel.getElementAddress(objPointer, index, dataSize);
	}

	/**
	 * @param objPointer array object who's data address validity we are checking
	 * @throws CorruptDataException 
	 */
	public static boolean isCorrectDataPointer(J9IndexableObjectPointer objPointer) throws CorruptDataException
	{
		return ObjectModel.isCorrectDataPointer(objPointer);
	}
	
	/**
	 *  @param objPointer array object who's elements we are outputting to dst
	 *  @param dst destination array where we will output the elements
	 *  @param start starting index of the elements we are interested in
	 *  @param length number of elements to output 
	 *  @param destStart starting index of destination array where we will start outputting elements 
	 */
	public static void getData(J9IndexableObjectPointer objPointer, Object dst, int start, int length, int destStart) throws CorruptDataException
	{
		String className = J9IndexableObjectHelper.getClassName(objPointer);
		int arraySize = (int) J9IndexableObjectHelper.size(objPointer).longValue();
		
		if (start + length > arraySize) {
			throw new ArrayIndexOutOfBoundsException("Requested range " + start + " to " + (start + length) + " overflows array of length " + arraySize);
		}
		
		switch (className.charAt(1)) {
		case 'B':
		{
			if (! (dst instanceof byte[])) {
				throw new IllegalArgumentException("Destination array of type " + dst.getClass().getName() + " incompatible with byte array (expects byte[])");
			}
			
			getByteData(objPointer, (byte[])dst, start, length, destStart);
			break;
		}
		
		case 'C':
		{
			if (! (dst instanceof char[])) {
				throw new IllegalArgumentException("Destination array of type " + dst.getClass().getName() + " incompatible with char array (expects char[])");
			}
			
			getCharData(objPointer, (char[])dst, start, length, destStart);
			break;
		}
		
		case 'D':
		{
			if (! (dst instanceof double[])) {
				throw new IllegalArgumentException("Destination array of type " + dst.getClass().getName() + " incompatible with double array (expects double[])");
			}
			
			getDoubleData(objPointer, (double[])dst, start, length, destStart);
			break;
		}
		
		case 'F':
		{
			if (! (dst instanceof float[])) {
				throw new IllegalArgumentException("Destination array of type " + dst.getClass().getName() + " incompatible with float array (expects float[])");
			}
			
			getFloatData(objPointer,(float[])dst, start, length, destStart);
			break;
		}
		
		case 'I':
		{
			if (! (dst instanceof int[])) {
				throw new IllegalArgumentException("Destination array of type " + dst.getClass().getName() + " incompatible with int array (expects int[])");
			}
			
			getIntData(objPointer, (int[])dst, start, length, destStart);
			break;
		}
		
		case 'J':
		{
			if (! (dst instanceof long[])) {
				throw new IllegalArgumentException("Destination array of type " + dst.getClass().getName() + " incompatible with long array (expects long[])");
			}
			
			getLongData(objPointer, (long[])dst, start, length, destStart);
			break;
		}
		
		case 'S':
		{
			if (! (dst instanceof short[])) {
				throw new IllegalArgumentException("Destination array of type " + dst.getClass().getName() + " incompatible with short array (expects short[])");
			}
			
			getShortData(objPointer, (short[])dst, start, length, destStart);
			break;
		}
		
		case 'Z':
		{
			if (! (dst instanceof boolean[])) {
				throw new IllegalArgumentException("Destination array of type " + dst.getClass().getName() + " incompatible with boolean array (expects boolean[])");
			}
			
			getBooleanData(objPointer, (boolean[])dst, start, length, destStart);
			break;
		}
			
		case 'L':
		case '[':
		{
			if (! (dst instanceof J9ObjectPointer[])) {
				throw new IllegalArgumentException("Destination array of type " + dst.getClass().getName() + " incompatible with Object array (expects J9ObjectPointer[])");
			}
			
			getObjectData(objPointer, (J9ObjectPointer[])dst, start, length, destStart);
			break;
		}
		default:
			throw new CorruptDataException("The data identifier : " + className.charAt(1) + " was not recognised");
		}
	}
	
	public static void getByteData(J9IndexableObjectPointer objPointer, final byte[] dst, final int start, final int length, final int destStart) throws CorruptDataException 
	{
		if (destStart + length > dst.length) {
			throw new ArrayIndexOutOfBoundsException("Supplied destination array too small. Requires: " + destStart + length + ", was " + dst.length);
		}
		for (int i = 0; i < length; i++) {
			dst[destStart + i] = U8Pointer.cast(ObjectModel.getElementAddress(objPointer, start + i, 1)).at(0).byteValue();
		}
	}
	
	public static void getCharData(J9IndexableObjectPointer objPointer, final char[] dst, final int start, final int length, final int destStart) throws CorruptDataException 
	{
		if (destStart + length > dst.length) {
			throw new ArrayIndexOutOfBoundsException("Supplied destination array too small. Requires: " + destStart + length + ", was " + dst.length);
		}
		for (int i = 0; i < length; i++) {
			dst[destStart + i] = (char)U16Pointer.cast(ObjectModel.getElementAddress(objPointer, start + i, 2)).at(0).intValue();
		}
	}
	
	public static void getDoubleData(J9IndexableObjectPointer objPointer, final double[] dst, final int start, final int length, final int destStart) throws CorruptDataException 
	{
		if (destStart + length > dst.length) {
			throw new ArrayIndexOutOfBoundsException("Supplied destination array too small. Requires: " + destStart + length + ", was " + dst.length);
		}
		for (int i = 0; i < length; i++) {
			long bits = I64Pointer.cast(ObjectModel.getElementAddress(objPointer, start + i, 8)).at(0).longValue();
			dst[destStart + i] = Double.longBitsToDouble(bits);
		}
	}
	
	public static void getFloatData(J9IndexableObjectPointer objPointer, final float[] dst, final int start, final int length, final int destStart) throws CorruptDataException 
	{
		if (destStart + length > dst.length) {
			throw new ArrayIndexOutOfBoundsException("Supplied destination array too small. Requires: " + destStart + length + ", was " + dst.length);
		}
		for (int i = 0; i < length; i++) {
			int bits = I32Pointer.cast(ObjectModel.getElementAddress(objPointer, start + i, 4)).at(0).intValue();
			dst[destStart + i] = Float.intBitsToFloat(bits);
		}
	}
	
	public static void getIntData(J9IndexableObjectPointer objPointer, final int[] dst, final int start, final int length, final int destStart) throws CorruptDataException 
	{
		if (destStart + length > dst.length) {
			throw new ArrayIndexOutOfBoundsException("Supplied destination array too small. Requires: " + destStart + length + ", was " + dst.length);
		}
		for (int i = 0; i < length; i++) {
			dst[destStart + i] = I32Pointer.cast(ObjectModel.getElementAddress(objPointer, start + i, 4)).at(0).intValue();
		}
	}
	
	public static void getLongData(J9IndexableObjectPointer objPointer, final long[] dst, final int start, final int length, final int destStart) throws CorruptDataException 
	{
		if (destStart + length > dst.length) {
			throw new ArrayIndexOutOfBoundsException("Supplied destination array too small. Requires: " + destStart + length + ", was " + dst.length);
		}
		for (int i = 0; i < length; i++) {
			dst[destStart + i] = I64Pointer.cast(ObjectModel.getElementAddress(objPointer, start + i, 8)).at(0).longValue();
		}
	}
	
	public static void getShortData(J9IndexableObjectPointer objPointer, final short[] dst, final int start, final int length, final int destStart) throws CorruptDataException 
	{
		if (destStart + length > dst.length) {
			throw new ArrayIndexOutOfBoundsException("Supplied destination array too small. Requires: " + destStart + length + ", was " + dst.length);
		}
		for (int i = 0; i < length; i++) {
			dst[destStart + i] = I16Pointer.cast(ObjectModel.getElementAddress(objPointer, start + i, 2)).at(0).shortValue();
		}
	}
	
	public static void getBooleanData(J9IndexableObjectPointer objPointer, final boolean[] dst, final int start, final int length, final int destStart) throws CorruptDataException 
	{
		if (destStart + length > dst.length) {
			throw new ArrayIndexOutOfBoundsException("Supplied destination array too small. Requires: " + destStart + length + ", was " + dst.length);
		}
		for (int i = 0; i < length; i++) {
			dst[destStart + i] = (0 != U8Pointer.cast(ObjectModel.getElementAddress(objPointer, start + i, 1)).at(0).intValue());
		}
	}
	
	public static void getObjectData(J9IndexableObjectPointer objPointer, final J9ObjectPointer[] dst, final int start, final int length, final int destStart) throws CorruptDataException 
	{
		if (destStart + length > dst.length) {
			throw new ArrayIndexOutOfBoundsException("Supplied destination array too small. Requires: " + destStart + length + ", was " + dst.length);
		}
		for (int i = 0; i < length; i++) {
			dst[destStart + i] = ObjectReferencePointer.cast(ObjectModel.getElementAddress(objPointer, start + i, (int)ObjectReferencePointer.SIZEOF)).at(0);
		}
	}
	
	public static Object getData(J9IndexableObjectPointer objPointer) throws CorruptDataException 
	{
		
		String className = J9IndexableObjectHelper.getClassName(objPointer);
		int arraySize;
		arraySize = (int) J9IndexableObjectHelper.size(objPointer).longValue();

		switch (className.charAt(1)) {
		case 'B':
		{
			byte[] data = new byte[arraySize];
			getByteData(objPointer, data, 0, arraySize, 0);
			return data;			
		}
			
		case 'C':
		{
			char[] data = new char[arraySize];
			getCharData(objPointer, data, 0, arraySize, 0);
			return data;			
		}

		case 'D':
		{
			double[] data = new double[arraySize];
			getDoubleData(objPointer, data, 0, arraySize, 0);
			return data;			
		}
		
		case 'F':
		{
			float[] data = new float[arraySize];
			getFloatData(objPointer, data, 0, arraySize, 0);
			return data;			
		}
		
		case 'I':
		{
			int[] data = new int[arraySize];
			getIntData(objPointer, data, 0, arraySize, 0);
			return data;			
		}
		
		case 'J':
		{
			long[] data = new long[arraySize];
			getLongData(objPointer, data, 0, arraySize, 0);
			return data;			
		}
		
		case 'S':
		{
			short[] data = new short[arraySize];
			getShortData(objPointer, data, 0, arraySize, 0);
			return data;			
		}
		
		case 'Z':
		{
			boolean[] data = new boolean[arraySize];
			getBooleanData(objPointer, data, 0, arraySize, 0);
			return data;			
		}
			
		case 'L':
		case '[':
		{
			J9ObjectPointer[] data = new J9ObjectPointer[arraySize];			
			getObjectData(objPointer, data, 0, arraySize, 0);
			return data;			
		}
		
		default:
			throw new CorruptDataException("The data identifier : " + className.charAt(1) + " was not recognised");
		}
	}
	
	public static String getDataAsString(J9IndexableObjectPointer array) throws CorruptDataException {
		return getDataAsString(array, 10, 80);
	}
	
	public static String getDataAsString(J9IndexableObjectPointer array, int dumpLimit, int characterDumpLimit) throws CorruptDataException {
		StringWriter buf = new StringWriter();
		J9ClassPointer clazz = J9ObjectHelper.clazz(J9ObjectPointer.cast(array));
		char typeChar = J9ClassHelper.getName(clazz).charAt(1);
		int length = J9IndexableObjectHelper.size(array).intValue();
		int limit = 0;
		if (typeChar != 'C') {
			limit = dumpLimit;
		} else {
			limit = characterDumpLimit;
		}
		
		buf.write("{ ");
		
		int i;
		for (i = 0; i < Math.min(length, limit); i++) {
			Object data = J9IndexableObjectHelper.getData(array);
			switch (typeChar) {
			case 'B':
				buf.write("0x");
				buf.write(Integer.toHexString(((byte[])data)[i]));
				break;
				
			case 'C':
				char ch = ((char[])data)[i];
				if(ch > 31 && ch < 127) {
					buf.write(ch);
				} else {
					buf.write('?');
				}
				break;
				
			case 'D':
				buf.write(Double.toString(((double[])data)[i]));
				break;
				
			case 'F':
				buf.write(Float.toString(((float[])data)[i]));
				break;
				
			case 'I':
				buf.write("0x");
				buf.write(Integer.toHexString(((int[])data)[i]));
				break;

			case 'J':
				buf.write("0x");
				buf.write(Long.toHexString(((long[])data)[i]));
				break;
				
			case 'S':
				buf.write("0x");
				buf.write(Integer.toHexString(((short[])data)[i]));
				break;	
				
			case 'Z':
				buf.write(((boolean[])data)[i] ? "true" : "false");
				break;								

			case 'L':
			case '[':
			{
				J9ObjectPointer item = ((J9ObjectPointer[])data)[i];
				if(null == item) {
					buf.write("null");
				} else {
					buf.write("0x");
					buf.write(Long.toHexString(item.longValue()));
				}
				break;
			}
				
			default:
				buf.write("?");
			}
			
			if (typeChar != 'C' && (i != length-1)) {
				/* We aren't printing out a character and we aren't the last element */
				buf.write(", ");
			}
		}
		
		if (i != length) {
			/* We hit the limit */
			buf.write("... ");
		}
		buf.write(" }");
	
		return buf.toString();
	}

}
