/*
 * Copyright (c) 2003, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.  Oracle designates this
 * particular file as subject to the "Classpath" exception as provided
 * by Oracle in the LICENSE file that accompanied this code.
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
 */


package sun.io;

import sun.nio.cs.ext.JIS_X_0212_Solaris_Decoder;

/**
 * Tables and data to convert JIS0212_Solaris to Unicode
 * Solaris ==> vendor defined characters -- NEC row 13 + IBM extended
 *         selected character set.
 * @author  ConverterGenerator tool
 */

public class ByteToCharJIS0212_Solaris extends ByteToCharDoubleByte {

    public String getCharacterEncoding() {
        return "JIS0212_Solaris";
    }

    protected char convSingleByte(int b) {
        //Fix bug#4179800 - JIS0212 is 7bit,double-byte encoding
        return REPLACE_CHAR;
    }

    public ByteToCharJIS0212_Solaris() {
        super.index1 = JIS_X_0212_Solaris_Decoder.getIndex1();
        super.index2 = JIS_X_0212_Solaris_Decoder.getIndex2();
        start = 0x21;
        end = 0x7E;
    }
}
