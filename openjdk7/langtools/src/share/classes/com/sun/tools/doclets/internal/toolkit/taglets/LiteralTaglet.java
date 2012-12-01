/*
 * Copyright (c) 2003, 2008, Oracle and/or its affiliates. All rights reserved.
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
package com.sun.tools.doclets.internal.toolkit.taglets;

import java.util.Map;
import com.sun.javadoc.Tag;
import com.sun.tools.doclets.Taglet;


/**
 * An inline Taglet used to denote literal text.
 * The enclosed text is interpreted as not containing HTML markup or
 * nested javadoc tags.
 * For example, the text:
 * <blockquote>  {@code {@literal a<B>c}}  </blockquote>
 * displays as:
 * <blockquote>  {@literal a<B>c}  </blockquote>
 *
 * @author Scott Seligman
 * @since 1.5
 */

public class LiteralTaglet implements Taglet {

    private static final String NAME = "literal";

    public static void register(Map<String,Taglet> map) {
           map.remove(NAME);
           map.put(NAME, new LiteralTaglet());
    }

    public String getName() {
        return NAME;
    }

    public String toString(Tag tag) {
        return textToString(tag.text());
    }

    public String toString(Tag[] tags) { return null; }

    public boolean inField() { return false; }

    public boolean inConstructor() { return false; }

    public boolean inMethod() { return false; }

    public boolean inOverview() { return false; }

    public boolean inPackage() { return false; }

    public boolean inType() { return false; }

    public boolean isInlineTag() { return true; }

    /*
     * Replace occurrences of the following characters:  < > &
     */
    protected static String textToString(String text) {
           StringBuffer buf = new StringBuffer();
           for (int i = 0; i < text.length(); i++) {
               char c = text.charAt(i);
               switch (c) {
                   case '<':
                          buf.append("&lt;");
                          break;
                   case '>':
                          buf.append("&gt;");
                          break;
                   case '&':
                          buf.append("&amp;");
                          break;
                   default:
                          buf.append(c);
               }
           }
           return buf.toString();
    }
}
