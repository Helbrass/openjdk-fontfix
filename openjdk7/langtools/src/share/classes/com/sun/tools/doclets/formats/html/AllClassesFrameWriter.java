/*
 * Copyright (c) 1998, 2010, Oracle and/or its affiliates. All rights reserved.
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

package com.sun.tools.doclets.formats.html;

import java.io.*;
import java.util.*;

import com.sun.javadoc.*;
import com.sun.tools.doclets.internal.toolkit.*;
import com.sun.tools.doclets.internal.toolkit.util.*;
import com.sun.tools.doclets.formats.html.markup.*;

/**
 * Generate the file with list of all the classes in this run. This page will be
 * used in the left-hand bottom frame, when "All Classes" link is clicked in
 * the left-hand top frame. The name of the generated file is
 * "allclasses-frame.html".
 *
 * @author Atul M Dambalkar
 * @author Doug Kramer
 * @author Bhavesh Patel (Modified)
 */
public class AllClassesFrameWriter extends HtmlDocletWriter {

    /**
     * The name of the output file with frames
     */
    public static final String OUTPUT_FILE_NAME_FRAMES = "allclasses-frame.html";

    /**
     * The name of the output file without frames
     */
    public static final String OUTPUT_FILE_NAME_NOFRAMES = "allclasses-noframe.html";

    /**
     * Index of all the classes.
     */
    protected IndexBuilder indexbuilder;

    /**
     * BR tag to be used within a document tree.
     */
    final HtmlTree BR = new HtmlTree(HtmlTag.BR);

    /**
     * Construct AllClassesFrameWriter object. Also initilises the indexbuilder
     * variable in this class.
     * @throws IOException
     * @throws DocletAbortException
     */
    public AllClassesFrameWriter(ConfigurationImpl configuration,
                                 String filename, IndexBuilder indexbuilder)
                              throws IOException {
        super(configuration, filename);
        this.indexbuilder = indexbuilder;
    }

    /**
     * Create AllClassesFrameWriter object. Then use it to generate the
     * "allclasses-frame.html" file. Generate the file in the current or the
     * destination directory.
     *
     * @param indexbuilder IndexBuilder object for all classes index.
     * @throws DocletAbortException
     */
    public static void generate(ConfigurationImpl configuration,
                                IndexBuilder indexbuilder) {
        AllClassesFrameWriter allclassgen;
        String filename = OUTPUT_FILE_NAME_FRAMES;
        try {
            allclassgen = new AllClassesFrameWriter(configuration,
                                                    filename, indexbuilder);
            allclassgen.buildAllClassesFile(true);
            allclassgen.close();
            filename = OUTPUT_FILE_NAME_NOFRAMES;
            allclassgen = new AllClassesFrameWriter(configuration,
                                                    filename, indexbuilder);
            allclassgen.buildAllClassesFile(false);
            allclassgen.close();
        } catch (IOException exc) {
            configuration.standardmessage.
                     error("doclet.exception_encountered",
                           exc.toString(), filename);
            throw new DocletAbortException();
        }
    }

    /**
     * Print all the classes in the file.
     * @param wantFrames True if we want frames.
     */
    protected void buildAllClassesFile(boolean wantFrames) throws IOException {
        String label = configuration.getText("doclet.All_Classes");
        Content body = getBody(false, getWindowTitle(label));
        Content heading = HtmlTree.HEADING(HtmlConstants.TITLE_HEADING,
                HtmlStyle.bar, allclassesLabel);
        body.addContent(heading);
        Content ul = new HtmlTree(HtmlTag.UL);
        // Generate the class links and add it to the tdFont tree.
        addAllClasses(ul, wantFrames);
        Content div = HtmlTree.DIV(HtmlStyle.indexContainer, ul);
        body.addContent(div);
        printHtmlDocument(null, false, body);
    }

    /**
     * Use the sorted index of all the classes and add all the classes to the
     * content list.
     *
     * @param content HtmlTree content to which all classes information will be added
     * @param wantFrames True if we want frames.
     */
    protected void addAllClasses(Content content, boolean wantFrames) {
        for (int i = 0; i < indexbuilder.elements().length; i++) {
            Character unicode = (Character)((indexbuilder.elements())[i]);
            addContents(indexbuilder.getMemberList(unicode), wantFrames, content);
        }
    }

    /**
     * Given a list of classes, generate links for each class or interface.
     * If the class kind is interface, print it in the italics font. Also all
     * links should target the right-hand frame. If clicked on any class name
     * in this page, appropriate class page should get opened in the right-hand
     * frame.
     *
     * @param classlist Sorted list of classes.
     * @param wantFrames True if we want frames.
     * @param content HtmlTree content to which the links will be added
     */
    protected void addContents(List<Doc> classlist, boolean wantFrames,
            Content content) {
        for (int i = 0; i < classlist.size(); i++) {
            ClassDoc cd = (ClassDoc)classlist.get(i);
            if (!Util.isCoreClass(cd)) {
                continue;
            }
            String label = italicsClassName(cd, false);
            Content linkContent;
            if(wantFrames){
                linkContent = new RawHtml(getLink(new LinkInfoImpl(
                        LinkInfoImpl.ALL_CLASSES_FRAME, cd, label, "classFrame")));
            } else {
                linkContent = new RawHtml(getLink(new LinkInfoImpl(cd, label)));
            }
            Content li = HtmlTree.LI(linkContent);
            content.addContent(li);
        }
    }
}
