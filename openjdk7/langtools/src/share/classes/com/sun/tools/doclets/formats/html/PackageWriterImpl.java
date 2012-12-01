/*
 * Copyright (c) 1997, 2011, Oracle and/or its affiliates. All rights reserved.
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
 * Class to generate file for each package contents in the right-hand
 * frame. This will list all the Class Kinds in the package. A click on any
 * class-kind will update the frame with the clicked class-kind page.
 *
 * @author Atul M Dambalkar
 * @author Bhavesh Patel (Modified)
 */
public class PackageWriterImpl extends HtmlDocletWriter
    implements PackageSummaryWriter {

    /**
     * The prev package name in the alpha-order list.
     */
    protected PackageDoc prev;

    /**
     * The next package name in the alpha-order list.
     */
    protected PackageDoc next;

    /**
     * The package being documented.
     */
    protected PackageDoc packageDoc;

    /**
     * The name of the output file.
     */
    private static final String OUTPUT_FILE_NAME = "package-summary.html";

    /**
     * Constructor to construct PackageWriter object and to generate
     * "package-summary.html" file in the respective package directory.
     * For example for package "java.lang" this will generate file
     * "package-summary.html" file in the "java/lang" directory. It will also
     * create "java/lang" directory in the current or the destination directory
     * if it doesen't exist.
     *
     * @param configuration the configuration of the doclet.
     * @param packageDoc    PackageDoc under consideration.
     * @param prev          Previous package in the sorted array.
     * @param next            Next package in the sorted array.
     */
    public PackageWriterImpl(ConfigurationImpl configuration,
        PackageDoc packageDoc, PackageDoc prev, PackageDoc next)
    throws IOException {
        super(configuration, DirectoryManager.getDirectoryPath(packageDoc), OUTPUT_FILE_NAME,
             DirectoryManager.getRelativePath(packageDoc.name()));
        this.prev = prev;
        this.next = next;
        this.packageDoc = packageDoc;
    }

    /**
     * Return the name of the output file.
     *
     * @return the name of the output file.
     */
    public String getOutputFileName() {
        return OUTPUT_FILE_NAME;
    }

    /**
     * {@inheritDoc}
     */
    public Content getPackageHeader(String heading) {
        String pkgName = packageDoc.name();
        Content bodyTree = getBody(true, getWindowTitle(pkgName));
        addTop(bodyTree);
        addNavLinks(true, bodyTree);
        HtmlTree div = new HtmlTree(HtmlTag.DIV);
        div.addStyle(HtmlStyle.header);
        Content annotationContent = new HtmlTree(HtmlTag.P);
        addAnnotationInfo(packageDoc, annotationContent);
        div.addContent(annotationContent);
        Content tHeading = HtmlTree.HEADING(HtmlConstants.TITLE_HEADING, true,
                HtmlStyle.title, packageLabel);
        tHeading.addContent(getSpace());
        Content packageHead = new RawHtml(heading);
        tHeading.addContent(packageHead);
        div.addContent(tHeading);
        addDeprecationInfo(div);
        if (packageDoc.inlineTags().length > 0 && ! configuration.nocomment) {
            HtmlTree docSummaryDiv = new HtmlTree(HtmlTag.DIV);
            docSummaryDiv.addStyle(HtmlStyle.docSummary);
            addSummaryComment(packageDoc, docSummaryDiv);
            div.addContent(docSummaryDiv);
            Content space = getSpace();
            Content descLink = getHyperLink("", "package_description",
                    descriptionLabel, "", "");
            Content descPara = new HtmlTree(HtmlTag.P, seeLabel, space, descLink);
            div.addContent(descPara);
        }
        bodyTree.addContent(div);
        return bodyTree;
    }

    /**
     * {@inheritDoc}
     */
    public Content getContentHeader() {
        HtmlTree div = new HtmlTree(HtmlTag.DIV);
        div.addStyle(HtmlStyle.contentContainer);
        return div;
    }

    /**
     * Add the package deprecation information to the documentation tree.
     *
     * @param div the content tree to which the deprecation information will be added
     */
    public void addDeprecationInfo(Content div) {
        Tag[] deprs = packageDoc.tags("deprecated");
        if (Util.isDeprecated(packageDoc)) {
            HtmlTree deprDiv = new HtmlTree(HtmlTag.DIV);
            deprDiv.addStyle(HtmlStyle.deprecatedContent);
            Content deprPhrase = HtmlTree.SPAN(HtmlStyle.strong, deprecatedPhrase);
            deprDiv.addContent(deprPhrase);
            if (deprs.length > 0) {
                Tag[] commentTags = deprs[0].inlineTags();
                if (commentTags.length > 0) {
                    addInlineDeprecatedComment(packageDoc, deprs[0], deprDiv);
                }
            }
            div.addContent(deprDiv);
        }
    }

    /**
     * {@inheritDoc}
     */
    public Content getSummaryHeader() {
        HtmlTree ul = new HtmlTree(HtmlTag.UL);
        ul.addStyle(HtmlStyle.blockList);
        return ul;
    }

    /**
     * {@inheritDoc}
     */
    public void addClassesSummary(ClassDoc[] classes, String label,
            String tableSummary, String[] tableHeader, Content summaryContentTree) {
        if(classes.length > 0) {
            Arrays.sort(classes);
            Content caption = getTableCaption(label);
            Content table = HtmlTree.TABLE(HtmlStyle.packageSummary, 0, 3, 0,
                    tableSummary, caption);
            table.addContent(getSummaryTableHeader(tableHeader, "col"));
            Content tbody = new HtmlTree(HtmlTag.TBODY);
            for (int i = 0; i < classes.length; i++) {
                if (!Util.isCoreClass(classes[i]) ||
                    !configuration.isGeneratedDoc(classes[i])) {
                    continue;
                }
                Content classContent = new RawHtml(getLink(new LinkInfoImpl(
                        LinkInfoImpl.CONTEXT_PACKAGE, classes[i], false)));
                Content tdClass = HtmlTree.TD(HtmlStyle.colFirst, classContent);
                HtmlTree tr = HtmlTree.TR(tdClass);
                if (i%2 == 0)
                    tr.addStyle(HtmlStyle.altColor);
                else
                    tr.addStyle(HtmlStyle.rowColor);
                HtmlTree tdClassDescription = new HtmlTree(HtmlTag.TD);
                tdClassDescription.addStyle(HtmlStyle.colLast);
                if (Util.isDeprecated(classes[i])) {
                    tdClassDescription.addContent(deprecatedLabel);
                    if (classes[i].tags("deprecated").length > 0) {
                        addSummaryDeprecatedComment(classes[i],
                            classes[i].tags("deprecated")[0], tdClassDescription);
                    }
                }
                else
                    addSummaryComment(classes[i], tdClassDescription);
                tr.addContent(tdClassDescription);
                tbody.addContent(tr);
            }
            table.addContent(tbody);
            Content li = HtmlTree.LI(HtmlStyle.blockList, table);
            summaryContentTree.addContent(li);
        }
    }

    /**
     * {@inheritDoc}
     */
    public void addPackageDescription(Content packageContentTree) {
        if (packageDoc.inlineTags().length > 0) {
            packageContentTree.addContent(getMarkerAnchor("package_description"));
            Content h2Content = new StringContent(
                    configuration.getText("doclet.Package_Description",
                    packageDoc.name()));
            packageContentTree.addContent(HtmlTree.HEADING(HtmlConstants.PACKAGE_HEADING,
                    true, h2Content));
            addInlineComment(packageDoc, packageContentTree);
        }
    }

    /**
     * {@inheritDoc}
     */
    public void addPackageTags(Content packageContentTree) {
        addTagsInfo(packageDoc, packageContentTree);
    }

    /**
     * {@inheritDoc}
     */
    public void addPackageFooter(Content contentTree) {
        addNavLinks(false, contentTree);
        addBottom(contentTree);
    }

    /**
     * {@inheritDoc}
     */
    public void printDocument(Content contentTree) {
        printHtmlDocument(configuration.metakeywords.getMetaKeywords(packageDoc),
                true, contentTree);
    }

    /**
     * Get "Use" link for this pacakge in the navigation bar.
     *
     * @return a content tree for the class use link
     */
    protected Content getNavLinkClassUse() {
        Content useLink = getHyperLink("package-use.html", "",
                useLabel, "", "");
        Content li = HtmlTree.LI(useLink);
        return li;
    }

    /**
     * Get "PREV PACKAGE" link in the navigation bar.
     *
     * @return a content tree for the previous link
     */
    public Content getNavLinkPrevious() {
        Content li;
        if (prev == null) {
            li = HtmlTree.LI(prevpackageLabel);
        } else {
            String path = DirectoryManager.getRelativePath(packageDoc.name(),
                                                           prev.name());
            li = HtmlTree.LI(getHyperLink(path + "package-summary.html", "",
                prevpackageLabel, "", ""));
        }
        return li;
    }

    /**
     * Get "NEXT PACKAGE" link in the navigation bar.
     *
     * @return a content tree for the next link
     */
    public Content getNavLinkNext() {
        Content li;
        if (next == null) {
            li = HtmlTree.LI(nextpackageLabel);
        } else {
            String path = DirectoryManager.getRelativePath(packageDoc.name(),
                                                           next.name());
            li = HtmlTree.LI(getHyperLink(path + "package-summary.html", "",
                nextpackageLabel, "", ""));
        }
        return li;
    }

    /**
     * Get "Tree" link in the navigation bar. This will be link to the package
     * tree file.
     *
     * @return a content tree for the tree link
     */
    protected Content getNavLinkTree() {
        Content useLink = getHyperLink("package-tree.html", "",
                treeLabel, "", "");
        Content li = HtmlTree.LI(useLink);
        return li;
    }

    /**
     * Highlight "Package" in the navigation bar, as this is the package page.
     *
     * @return a content tree for the package link
     */
    protected Content getNavLinkPackage() {
        Content li = HtmlTree.LI(HtmlStyle.navBarCell1Rev, packageLabel);
        return li;
    }
}
