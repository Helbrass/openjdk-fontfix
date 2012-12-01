/*
 * Copyright (c) 2002, 2003, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 *
 */

package sun.jvm.hotspot.asm.x86;

import sun.jvm.hotspot.asm.*;
import sun.jvm.hotspot.utilities.*;

public class ConditionalJmpDecoder extends InstructionDecoder {

   public ConditionalJmpDecoder(String name, int addrMode1, int operandType1) {
      super(name, addrMode1, operandType1);
   }

   protected Instruction decodeInstruction(byte[] bytesArray, boolean operandSize, boolean addrSize, X86InstructionFactory factory) {
      Operand addr = getOperand1(bytesArray, operandSize, addrSize);
      if (Assert.ASSERTS_ENABLED) {
        Assert.that(addr instanceof X86PCRelativeAddress, "Address should be PC Relative!");
      }
      return factory.newCondJmpInstruction(name, (X86PCRelativeAddress)addr, byteIndex-instrStartIndex, prefixes);
   }
}
