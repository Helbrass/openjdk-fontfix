/*
 * Copyright (c) 2001, Oracle and/or its affiliates. All rights reserved.
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
 */

/**
 * @test
 * @bug 4485208
 * @summary  file: and ftp: URL handlers need to throw NPE in setRequestProperty
 */

import java.net.*;

public class RequestProperties {
    public static void main (String args[]) throws Exception {
        URL url0 = new URL ("http://foo.com/bar/");
        URL url1 = new URL ("file:/etc/passwd");
        URL url2 = new URL ("ftp://foo:bar@foobar.com/etc/passwd");
        URL url3 = new URL ("jar:http://foo.com/bar.html!/foo/bar");
        URLConnection urlc0 = url0.openConnection ();
        URLConnection urlc1 = url1.openConnection ();
        URLConnection urlc2 = url2.openConnection ();
        URLConnection urlc3 = url3.openConnection ();
        int count = 0;
        String s = null;
        try {
            urlc0.setRequestProperty (null, null);
            System.out.println ("http: setRequestProperty (null,) did not throw NPE");
        } catch (NullPointerException e) {
            count ++;
        }
        try {
            urlc0.addRequestProperty (null, null);
            System.out.println ("http: addRequestProperty (null,) did not throw NPE");
        } catch (NullPointerException e) {
            count ++;
        }
        try {
            urlc1.setRequestProperty (null, null);
            System.out.println ("file: setRequestProperty (null,) did not throw NPE");
        } catch (NullPointerException e) {
            count ++;
        }
        try {
            urlc1.addRequestProperty (null, null);
            System.out.println ("file: addRequestProperty (null,) did not throw NPE");
        } catch (NullPointerException e) {
            count ++;
        }
        try {
            urlc2.setRequestProperty (null, null);
            System.out.println ("ftp: setRequestProperty (null,) did not throw NPE");
        } catch (NullPointerException e) {
            count ++;
        }
        try {
            urlc2.addRequestProperty (null, null);
            System.out.println ("ftp: addRequestProperty (null,) did not throw NPE");
        } catch (NullPointerException e) {
            count ++;
        }
        try {
            urlc3.setRequestProperty (null, null);
            System.out.println ("jar: setRequestProperty (null,) did not throw NPE");
        } catch (NullPointerException e) {
            count ++;
        }
        try {
            urlc3.addRequestProperty (null, null);
            System.out.println ("jar: addRequestProperty (null,) did not throw NPE");
        } catch (NullPointerException e) {
            count ++;
        }
        if (urlc0.getRequestProperty (null) != null) {
            System.out.println ("http: getRequestProperty (null,) did not return null");
        } else {
            count ++;
        }
        if (urlc1.getRequestProperty (null) != null) {
            System.out.println ("file: getRequestProperty (null,) did not return null");
        } else {
            count ++;
        }
        if (urlc2.getRequestProperty (null) != null) {
            System.out.println ("ftp: getRequestProperty (null,) did not return null");
        } else {
            count ++;
        }
        if (urlc2.getRequestProperty (null) != null) {
            System.out.println ("jar: getRequestProperty (null,) did not return null");
        } else {
            count ++;
        }

        if (count != 12) {
            throw new RuntimeException ((12 -count) + " errors") ;
        }
    }
}
