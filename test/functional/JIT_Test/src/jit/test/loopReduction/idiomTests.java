/*******************************************************************************
 * Copyright (c) 2000, 2021 IBM Corp. and others
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
package jit.test.loopReduction;

public class idiomTests
{
   public void testBitMemOp(Context c) { new bitMemOp().runTest(c); }
   public void testSimpleByteTRT(Context c) { new simpleByteTRT().runTest(c); }
   public void testSimpleCharTRT(Context c) { new simpleCharTRT().runTest(c); }
   public void testTRTNestedArray(Context c) { new TRTNestedArray().runTest(c); }
   public void testCopyingTROT(Context c) { new copyingTROT().runTest(c); }
   public void testTROTArray(Context c) { new TROTArray().runTest(c); }
   public void testCopyingTRTO(Context c) { new copyingTRTO().runTest(c); }
   public void testTRTOArray(Context c) { new TRTOArray().runTest(c); }
   public void testLongMemCpy(Context c) { new longMemCpy().runTest(c); }
   public void testByteMemCpy(Context c) { new byteMemCpy().runTest(c); }
   public void testBigEndianByte2CharMemCpy(Context c) { new bigEndianByte2CharMemCpy().runTest(c); }
   public void testLittleEndianByte2CharMemCpy(Context c) { new littleEndianByte2CharMemCpy().runTest(c); }
   public void testChar2ByteMemCpy(Context c) { new char2ByteMemCpy().runTest(c); }
   public void testMEMCPYChar2Byte2(Context c) { new MEMCPYChar2Byte2().runTest(c); }
   public void testByteMemSet(Context c) { new byteMemSet().runTest(c); }
   public void testByteMemCmp(Context c) { new byteMemCmp().runTest(c); }
   public void testCharMemCmp(Context c) { new charMemCmp().runTest(c); }
   public void testLongMemCmp(Context c) { new longMemCmp().runTest(c); }
   public void testMEMCMP2CompareTo(Context c) { new MEMCMP2CompareTo().runTest(c); }
   public void testCountDecimalDigitInt(Context c) { new countDecimalDigitInt().runTest(c, 10, 10); }
   public void testCountDecimalDigitLong(Context c) { new CountDecimalDigitLong().runTest(c, 18, 18); }
   public void testCountDecimalDigitLong2(Context c) { new CountDecimalDigitLong2().runTest(c, 18, 18); }
   public void testWriteUTF(Context c) { new writeUTF().runTest(c); }
}
