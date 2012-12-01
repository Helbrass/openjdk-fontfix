/*
 * Copyright (c) 1999, 2011, Oracle and/or its affiliates. All rights reserved.
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

package com.sun.tools.javac.util;

import java.io.*;
import java.util.Arrays;
import java.util.EnumSet;
import java.util.HashSet;
import java.util.Map;
import java.util.Queue;
import java.util.Set;
import javax.tools.DiagnosticListener;
import javax.tools.JavaFileObject;

import com.sun.tools.javac.api.DiagnosticFormatter;
import com.sun.tools.javac.main.OptionName;
import com.sun.tools.javac.tree.JCTree;
import com.sun.tools.javac.util.JCDiagnostic.DiagnosticPosition;
import com.sun.tools.javac.util.JCDiagnostic.DiagnosticType;

import static com.sun.tools.javac.main.OptionName.*;

/** A class for error logs. Reports errors and warnings, and
 *  keeps track of error numbers and positions.
 *
 *  <p><b>This is NOT part of any supported API.
 *  If you write code that depends on this, you do so at your own risk.
 *  This code and its internal interfaces are subject to change or
 *  deletion without notice.</b>
 */
public class Log extends AbstractLog {
    /** The context key for the log. */
    public static final Context.Key<Log> logKey
        = new Context.Key<Log>();

    /** The context key for the output PrintWriter. */
    public static final Context.Key<PrintWriter> outKey =
        new Context.Key<PrintWriter>();

    //@Deprecated
    public final PrintWriter errWriter;

    //@Deprecated
    public final PrintWriter warnWriter;

    //@Deprecated
    public final PrintWriter noticeWriter;

    /** The maximum number of errors/warnings that are reported.
     */
    public final int MaxErrors;
    public final int MaxWarnings;

    /** Switch: prompt user on each error.
     */
    public boolean promptOnError;

    /** Switch: emit warning messages.
     */
    public boolean emitWarnings;

    /** Switch: suppress note messages.
     */
    public boolean suppressNotes;

    /** Print stack trace on errors?
     */
    public boolean dumpOnError;

    /** Print multiple errors for same source locations.
     */
    public boolean multipleErrors;

    /**
     * Diagnostic listener, if provided through programmatic
     * interface to javac (JSR 199).
     */
    protected DiagnosticListener<? super JavaFileObject> diagListener;

    /**
     * Formatter for diagnostics.
     */
    private DiagnosticFormatter<JCDiagnostic> diagFormatter;

    /**
     * Keys for expected diagnostics.
     */
    public Set<String> expectDiagKeys;

    /**
     * JavacMessages object used for localization.
     */
    private JavacMessages messages;

    /**
     * Deferred diagnostics
     */
    public boolean deferDiagnostics;
    public Queue<JCDiagnostic> deferredDiagnostics = new ListBuffer<JCDiagnostic>();

    /** Construct a log with given I/O redirections.
     */
    @Deprecated
    protected Log(Context context, PrintWriter errWriter, PrintWriter warnWriter, PrintWriter noticeWriter) {
        super(JCDiagnostic.Factory.instance(context));
        context.put(logKey, this);
        this.errWriter = errWriter;
        this.warnWriter = warnWriter;
        this.noticeWriter = noticeWriter;

        Options options = Options.instance(context);
        this.dumpOnError = options.isSet(DOE);
        this.promptOnError = options.isSet(PROMPT);
        this.emitWarnings = options.isUnset(XLINT_CUSTOM, "none");
        this.suppressNotes = options.isSet("suppressNotes");
        this.MaxErrors = getIntOption(options, XMAXERRS, getDefaultMaxErrors());
        this.MaxWarnings = getIntOption(options, XMAXWARNS, getDefaultMaxWarnings());

        boolean rawDiagnostics = options.isSet("rawDiagnostics");
        messages = JavacMessages.instance(context);
        this.diagFormatter = rawDiagnostics ? new RawDiagnosticFormatter(options) :
                                              new BasicDiagnosticFormatter(options, messages);
        @SuppressWarnings("unchecked") // FIXME
        DiagnosticListener<? super JavaFileObject> dl =
            context.get(DiagnosticListener.class);
        this.diagListener = dl;

        String ek = options.get("expectKeys");
        if (ek != null)
            expectDiagKeys = new HashSet<String>(Arrays.asList(ek.split(", *")));
    }
    // where
        private int getIntOption(Options options, OptionName optionName, int defaultValue) {
            String s = options.get(optionName);
            try {
                if (s != null) {
                    int n = Integer.parseInt(s);
                    return (n <= 0 ? Integer.MAX_VALUE : n);
                }
            } catch (NumberFormatException e) {
                // silently ignore ill-formed numbers
            }
            return defaultValue;
        }

        /** Default value for -Xmaxerrs.
         */
        protected int getDefaultMaxErrors() {
            return 100;
        }

        /** Default value for -Xmaxwarns.
         */
        protected int getDefaultMaxWarnings() {
            return 100;
        }

    /** The default writer for diagnostics
     */
    static final PrintWriter defaultWriter(Context context) {
        PrintWriter result = context.get(outKey);
        if (result == null)
            context.put(outKey, result = new PrintWriter(System.err));
        return result;
    }

    /** Construct a log with default settings.
     */
    protected Log(Context context) {
        this(context, defaultWriter(context));
    }

    /** Construct a log with all output redirected.
     */
    protected Log(Context context, PrintWriter defaultWriter) {
        this(context, defaultWriter, defaultWriter, defaultWriter);
    }

    /** Get the Log instance for this context. */
    public static Log instance(Context context) {
        Log instance = context.get(logKey);
        if (instance == null)
            instance = new Log(context);
        return instance;
    }

    /** The number of errors encountered so far.
     */
    public int nerrors = 0;

    /** The number of warnings encountered so far.
     */
    public int nwarnings = 0;

    /** A set of all errors generated so far. This is used to avoid printing an
     *  error message more than once. For each error, a pair consisting of the
     *  source file name and source code position of the error is added to the set.
     */
    private Set<Pair<JavaFileObject, Integer>> recorded = new HashSet<Pair<JavaFileObject,Integer>>();

    public boolean hasDiagnosticListener() {
        return diagListener != null;
    }

    public void setEndPosTable(JavaFileObject name, Map<JCTree, Integer> table) {
        name.getClass(); // null check
        getSource(name).setEndPosTable(table);
    }

    /** Return current sourcefile.
     */
    public JavaFileObject currentSourceFile() {
        return source == null ? null : source.getFile();
    }

    /** Get the current diagnostic formatter.
     */
    public DiagnosticFormatter<JCDiagnostic> getDiagnosticFormatter() {
        return diagFormatter;
    }

    /** Set the current diagnostic formatter.
     */
    public void setDiagnosticFormatter(DiagnosticFormatter<JCDiagnostic> diagFormatter) {
        this.diagFormatter = diagFormatter;
    }

    /** Flush the logs
     */
    public void flush() {
        errWriter.flush();
        warnWriter.flush();
        noticeWriter.flush();
    }

    /** Returns true if an error needs to be reported for a given
     * source name and pos.
     */
    protected boolean shouldReport(JavaFileObject file, int pos) {
        if (multipleErrors || file == null)
            return true;

        Pair<JavaFileObject,Integer> coords = new Pair<JavaFileObject,Integer>(file, pos);
        boolean shouldReport = !recorded.contains(coords);
        if (shouldReport)
            recorded.add(coords);
        return shouldReport;
    }

    /** Prompt user after an error.
     */
    public void prompt() {
        if (promptOnError) {
            System.err.println(localize("resume.abort"));
            char ch;
            try {
                while (true) {
                    switch (System.in.read()) {
                    case 'a': case 'A':
                        System.exit(-1);
                        return;
                    case 'r': case 'R':
                        return;
                    case 'x': case 'X':
                        throw new AssertionError("user abort");
                    default:
                    }
                }
            } catch (IOException e) {}
        }
    }

    /** Print the faulty source code line and point to the error.
     *  @param pos   Buffer index of the error position, must be on current line
     */
    private void printErrLine(int pos, PrintWriter writer) {
        String line = (source == null ? null : source.getLine(pos));
        if (line == null)
            return;
        int col = source.getColumnNumber(pos, false);

        printLines(writer, line);
        for (int i = 0; i < col - 1; i++) {
            writer.print((line.charAt(i) == '\t') ? "\t" : " ");
        }
        writer.println("^");
        writer.flush();
    }

    /** Print the text of a message, translating newlines appropriately
     *  for the platform.
     */
    public static void printLines(PrintWriter writer, String msg) {
        int nl;
        while ((nl = msg.indexOf('\n')) != -1) {
            writer.println(msg.substring(0, nl));
            msg = msg.substring(nl+1);
        }
        if (msg.length() != 0) writer.println(msg);
    }

    /** Print the text of a message to the errWriter stream,
     *  translating newlines appropriately for the platform.
     */
    public void printErrLines(String key, Object... args) {
        printLines(errWriter, localize(key, args));
    }

    /** Print the text of a message to the noticeWriter stream,
     *  translating newlines appropriately for the platform.
     */
    public void printNoteLines(String key, Object... args) {
        printLines(noticeWriter, localize(key, args));
    }

    /**
     * Print the localized text of a "verbose" message to the
     * noticeWriter stream.
     */
    public void printVerbose(String key, Object... args) {
        printLines(noticeWriter, localize("verbose." + key, args));
    }

    protected void directError(String key, Object... args) {
        printErrLines(key, args);
        errWriter.flush();
    }

    /** Report a warning that cannot be suppressed.
     *  @param pos    The source position at which to report the warning.
     *  @param key    The key for the localized warning message.
     *  @param args   Fields of the warning message.
     */
    public void strictWarning(DiagnosticPosition pos, String key, Object ... args) {
        writeDiagnostic(diags.warning(source, pos, key, args));
        nwarnings++;
    }

    /** Report all deferred diagnostics, and clear the deferDiagnostics flag. */
    public void reportDeferredDiagnostics() {
        reportDeferredDiagnostics(EnumSet.allOf(JCDiagnostic.Kind.class));
    }

    /** Report selected deferred diagnostics, and clear the deferDiagnostics flag. */
    public void reportDeferredDiagnostics(Set<JCDiagnostic.Kind> kinds) {
        deferDiagnostics = false;
        JCDiagnostic d;
        while ((d = deferredDiagnostics.poll()) != null) {
            if (kinds.contains(d.getKind()))
                report(d);
        }
    }

    /**
     * Common diagnostic handling.
     * The diagnostic is counted, and depending on the options and how many diagnostics have been
     * reported so far, the diagnostic may be handed off to writeDiagnostic.
     */
    public void report(JCDiagnostic diagnostic) {
        if (deferDiagnostics) {
            deferredDiagnostics.add(diagnostic);
            return;
        }

        if (expectDiagKeys != null)
            expectDiagKeys.remove(diagnostic.getCode());

        switch (diagnostic.getType()) {
        case FRAGMENT:
            throw new IllegalArgumentException();

        case NOTE:
            // Print out notes only when we are permitted to report warnings
            // Notes are only generated at the end of a compilation, so should be small
            // in number.
            if ((emitWarnings || diagnostic.isMandatory()) && !suppressNotes) {
                writeDiagnostic(diagnostic);
            }
            break;

        case WARNING:
            if (emitWarnings || diagnostic.isMandatory()) {
                if (nwarnings < MaxWarnings) {
                    writeDiagnostic(diagnostic);
                    nwarnings++;
                }
            }
            break;

        case ERROR:
            if (nerrors < MaxErrors
                && shouldReport(diagnostic.getSource(), diagnostic.getIntPosition())) {
                writeDiagnostic(diagnostic);
                nerrors++;
            }
            break;
        }
    }

    /**
     * Write out a diagnostic.
     */
    protected void writeDiagnostic(JCDiagnostic diag) {
        if (diagListener != null) {
            diagListener.report(diag);
            return;
        }

        PrintWriter writer = getWriterForDiagnosticType(diag.getType());

        printLines(writer, diagFormatter.format(diag, messages.getCurrentLocale()));

        if (promptOnError) {
            switch (diag.getType()) {
            case ERROR:
            case WARNING:
                prompt();
            }
        }

        if (dumpOnError)
            new RuntimeException().printStackTrace(writer);

        writer.flush();
    }

    @Deprecated
    protected PrintWriter getWriterForDiagnosticType(DiagnosticType dt) {
        switch (dt) {
        case FRAGMENT:
            throw new IllegalArgumentException();

        case NOTE:
            return noticeWriter;

        case WARNING:
            return warnWriter;

        case ERROR:
            return errWriter;

        default:
            throw new Error();
        }
    }

    /** Find a localized string in the resource bundle.
     *  Because this method is static, it ignores the locale.
     *  Use localize(key, args) when possible.
     *  @param key    The key for the localized string.
     *  @param args   Fields to substitute into the string.
     */
    public static String getLocalizedString(String key, Object ... args) {
        return JavacMessages.getDefaultLocalizedString("compiler.misc." + key, args);
    }

    /** Find a localized string in the resource bundle.
     *  @param key    The key for the localized string.
     *  @param args   Fields to substitute into the string.
     */
    public String localize(String key, Object... args) {
        return messages.getLocalizedString("compiler.misc." + key, args);
    }

/***************************************************************************
 * raw error messages without internationalization; used for experimentation
 * and quick prototyping
 ***************************************************************************/

    /** print an error or warning message:
     */
    private void printRawError(int pos, String msg) {
        if (source == null || pos == Position.NOPOS) {
            printLines(errWriter, "error: " + msg);
        } else {
            int line = source.getLineNumber(pos);
            JavaFileObject file = source.getFile();
            if (file != null)
                printLines(errWriter,
                           file.getName() + ":" +
                           line + ": " + msg);
            printErrLine(pos, errWriter);
        }
        errWriter.flush();
    }

    /** report an error:
     */
    public void rawError(int pos, String msg) {
        if (nerrors < MaxErrors && shouldReport(currentSourceFile(), pos)) {
            printRawError(pos, msg);
            prompt();
            nerrors++;
        }
        errWriter.flush();
    }

    /** report a warning:
     */
    public void rawWarning(int pos, String msg) {
        if (nwarnings < MaxWarnings && emitWarnings) {
            printRawError(pos, "warning: " + msg);
        }
        prompt();
        nwarnings++;
        errWriter.flush();
    }

    public static String format(String fmt, Object... args) {
        return String.format((java.util.Locale)null, fmt, args);
    }

}
