/*
 * Copyright (c) 2011, Oracle and/or its affiliates. All rights reserved.
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

package com.sun.tools.javac.code;

import java.util.HashMap;
import java.util.Map;

import com.sun.tools.javac.util.Assert;
import com.sun.tools.javac.util.Context;
import com.sun.tools.javac.util.ListBuffer;
import com.sun.tools.javac.util.JCDiagnostic.DiagnosticPosition;

/**
 *
 * <p><b>This is NOT part of any supported API.
 * If you write code that depends on this, you do so at your own risk.
 * This code and its internal interfaces are subject to change or
 * deletion without notice.</b>
 */
public class DeferredLintHandler {
    protected static final Context.Key<DeferredLintHandler> deferredLintHandlerKey =
        new Context.Key<DeferredLintHandler>();

    public static DeferredLintHandler instance(Context context) {
        DeferredLintHandler instance = context.get(deferredLintHandlerKey);
        if (instance == null)
            instance = new DeferredLintHandler(context);
        return instance;
    }

    protected DeferredLintHandler(Context context) {
        context.put(deferredLintHandlerKey, this);
    }

    private DeferredLintHandler() {}

    public interface LintLogger {
        void report();
    }

    private DiagnosticPosition currentPos;
    private Map<DiagnosticPosition, ListBuffer<LintLogger>> loggersQueue = new HashMap<DiagnosticPosition, ListBuffer<LintLogger>>();

    public void report(LintLogger logger) {
        ListBuffer<LintLogger> loggers = loggersQueue.get(currentPos);
        Assert.checkNonNull(loggers);
        loggers.append(logger);
    }

    public void flush(DiagnosticPosition pos) {
        ListBuffer<LintLogger> loggers = loggersQueue.get(pos);
        if (loggers != null) {
            for (LintLogger lintLogger : loggers) {
                lintLogger.report();
            }
            loggersQueue.remove(pos);
        }
    }

    public DeferredLintHandler setPos(DiagnosticPosition currentPos) {
        this.currentPos = currentPos;
        loggersQueue.put(currentPos, ListBuffer.<LintLogger>lb());
        return this;
    }

    public static final DeferredLintHandler immediateHandler = new DeferredLintHandler() {
        @Override
        public void report(LintLogger logger) {
            logger.report();
        }
    };
}
