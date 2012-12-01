/*
 * Copyright (c) 2002, Oracle and/or its affiliates. All rights reserved.
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

package sun.jvm.hotspot.asm.sparc;

import sun.jvm.hotspot.asm.*;

class V9MOVccDecoder extends V9CMoveDecoder {
    private static String getMoveCCName(int conditionCode, int conditionFlag) {
        return "mov" + getConditionName(conditionCode, conditionFlag);
    }

    private static int getMoveConditionFlag(int instruction) {
        boolean cc2Bit = (instruction & CMOVE_CC2_MASK) != 0;
        int conditionFlag = (instruction & CMOVE_CC0_CC1_MASK) >>> CMOVE_CC_START_BIT;
        if (cc2Bit) conditionFlag |= (0x4); // 100;
        return conditionFlag;
    }

    Instruction decode(int instruction, SPARCInstructionFactory factory) {
        SPARCV9InstructionFactory v9factory = (SPARCV9InstructionFactory) factory;
        Instruction instr = null;
        int conditionFlag = getMoveConditionFlag(instruction);
        if (conditionFlag == CFLAG_RESERVED1 || conditionFlag == CFLAG_RESERVED2) {
            instr = v9factory.newIllegalInstruction(instruction);
        } else {
            int rdNum = getDestinationRegister(instruction);
            SPARCRegister rd = SPARCRegisters.getRegister(rdNum);
            int conditionCode = getMoveConditionCode(instruction);
            ImmediateOrRegister source = getCMoveSource(instruction, 11);
            String name = getMoveCCName(conditionCode, conditionFlag);
            instr = v9factory.newV9MOVccInstruction(name, conditionCode, conditionFlag, source, rd);
        }

        return instr;
    }
}
