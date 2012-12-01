/*
 * Copyright (c) 2004, Oracle and/or its affiliates. All rights reserved.
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

package com.sun.mirror.apt;

/**
 * Represents the status of a completed round of annotation processing.
 *
 * @deprecated All components of this API have been superseded by the
 * standardized annotation processing API.  The replacement for the
 * functionality of this interface is {@link
 * javax.annotation.processing.RoundEnvironment}.
 *
 * @author Joseph D. Darcy
 * @author Scott Seligman
 * @since 1.5
 */
@Deprecated
@SuppressWarnings("deprecation")
public interface RoundState {
    /**
     * Returns <tt>true</tt> if this was the last round of annotation
     * processing; returns <tt>false</tt> if there will be a subsequent round.
     */
    boolean finalRound();

    /**
     * Returns <tt>true</tt> if an error was raised in this round of processing;
     * returns <tt>false</tt> otherwise.
     */
    boolean errorRaised();

    /**
     * Returns <tt>true</tt> if new source files were created in this round of
     * processing; returns <tt>false</tt> otherwise.
     */
    boolean sourceFilesCreated();

    /**
     * Returns <tt>true</tt> if new class files were created in this round of
     * processing; returns <tt>false</tt> otherwise.
     */
    boolean classFilesCreated();
}
