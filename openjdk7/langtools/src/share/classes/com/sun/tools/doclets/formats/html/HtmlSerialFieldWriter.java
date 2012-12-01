/*
 * Copyright (c) 1998, 2011, Oracle and/or its affiliates. All rights reserved.
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

import java.util.*;

import com.sun.javadoc.*;
import com.sun.tools.doclets.internal.toolkit.*;
import com.sun.tools.doclets.internal.toolkit.taglets.*;
import com.sun.tools.doclets.formats.html.markup.*;

/**
 * Generate serialized form for serializable fields.
 * Documentation denoted by the tags <code>serial</code> and
 * <code>serialField</code> is processed.
 *
 * @author Joe Fialli
 * @author Bhavesh Patel (Modified)
 */
public class HtmlSerialFieldWriter extends FieldWriterImpl
    implements SerializedFormWriter.SerialFieldWriter {
    ProgramElementDoc[] members = null;

    private boolean printedOverallAnchor = false;

    public HtmlSerialFieldWriter(SubWriterHolderWriter writer,
                                    ClassDoc classdoc) {
        super(writer, classdoc);
    }

    public List<FieldDoc> members(ClassDoc cd) {
        return Arrays.asList(cd.serializableFields());
    }

    protected void printTypeLinkNoDimension(Type type) {
        ClassDoc cd = type.asClassDoc();
        //Linking to package private classes in serialized for causes
        //broken links.  Don't link to them.
        if (type.isPrimitive() || cd.isPackagePrivate()) {
            print(type.typeName());
        } else {
            writer.printLink(new LinkInfoImpl(
                LinkInfoImpl.CONTEXT_SERIAL_MEMBER, type));
        }
    }

    /**
     * Return the header for serializable fields section.
     *
     * @return a content tree for the header
     */
    public Content getSerializableFieldsHeader() {
        HtmlTree ul = new HtmlTree(HtmlTag.UL);
        ul.addStyle(HtmlStyle.blockList);
        return ul;
    }

    /**
     * Return the header for serializable fields content section.
     *
     * @param isLastContent true if the cotent being documented is the last content.
     * @return a content tree for the header
     */
    public Content getFieldsContentHeader(boolean isLastContent) {
        HtmlTree li = new HtmlTree(HtmlTag.LI);
        if (isLastContent)
            li.addStyle(HtmlStyle.blockListLast);
        else
            li.addStyle(HtmlStyle.blockList);
        return li;
    }

    /**
     * Add serializable fields.
     *
     * @param heading the heading for the section
     * @param serializableFieldsTree the tree to be added to the serializable fileds
     *        content tree
     * @return a content tree for the serializable fields content
     */
    public Content getSerializableFields(String heading, Content serializableFieldsTree) {
        HtmlTree li = new HtmlTree(HtmlTag.LI);
        li.addStyle(HtmlStyle.blockList);
        if (serializableFieldsTree.isValid()) {
            if (!printedOverallAnchor) {
                li.addContent(writer.getMarkerAnchor("serializedForm"));
                printedOverallAnchor = true;
            }
            Content headingContent = new StringContent(heading);
            Content serialHeading = HtmlTree.HEADING(HtmlConstants.SERIALIZED_MEMBER_HEADING,
                    headingContent);
            li.addContent(serialHeading);
            li.addContent(serializableFieldsTree);
        }
        return li;
    }

    /**
     * Add the member header.
     *
     * @param fieldsType the class document to be listed
     * @param fieldTypeStr the string for the filed type to be documented
     * @param fieldDimensions the dimensions of the field string to be added
     * @param firldName name of the field to be added
     * @param contentTree the content tree to which the member header will be added
     */
    public void addMemberHeader(ClassDoc fieldType, String fieldTypeStr,
            String fieldDimensions, String fieldName, Content contentTree) {
        Content nameContent = new RawHtml(fieldName);
        Content heading = HtmlTree.HEADING(HtmlConstants.MEMBER_HEADING, nameContent);
        contentTree.addContent(heading);
        Content pre = new HtmlTree(HtmlTag.PRE);
        if (fieldType == null) {
            pre.addContent(fieldTypeStr);
        } else {
            Content fieldContent = new RawHtml(writer.getLink(new LinkInfoImpl(
                    LinkInfoImpl.CONTEXT_SERIAL_MEMBER, fieldType)));
            pre.addContent(fieldContent);
        }
        pre.addContent(fieldDimensions + " ");
        pre.addContent(fieldName);
        contentTree.addContent(pre);
    }

    /**
     * Add the deprecated information for this member.
     *
     * @param field the field to document.
     * @param contentTree the tree to which the deprecated info will be added
     */
    public void addMemberDeprecatedInfo(FieldDoc field, Content contentTree) {
        addDeprecatedInfo(field, contentTree);
    }

    /**
     * Add the description text for this member.
     *
     * @param field the field to document.
     * @param contentTree the tree to which the deprecated info will be added
     */
    public void addMemberDescription(FieldDoc field, Content contentTree) {
        if (field.inlineTags().length > 0) {
            writer.addInlineComment(field, contentTree);
        }
        Tag[] tags = field.tags("serial");
        if (tags.length > 0) {
            writer.addInlineComment(field, tags[0], contentTree);
        }
    }

    /**
     * Add the description text for this member represented by the tag.
     *
     * @param serialFieldTag the field to document (represented by tag)
     * @param contentTree the tree to which the deprecated info will be added
     */
    public void addMemberDescription(SerialFieldTag serialFieldTag, Content contentTree) {
        String serialFieldTagDesc = serialFieldTag.description().trim();
        if (!serialFieldTagDesc.isEmpty()) {
            Content serialFieldContent = new RawHtml(serialFieldTagDesc);
            Content div = HtmlTree.DIV(HtmlStyle.block, serialFieldContent);
            contentTree.addContent(div);
        }
    }

    /**
     * Add the tag information for this member.
     *
     * @param field the field to document.
     * @param contentTree the tree to which the member tags info will be added
     */
    public void addMemberTags(FieldDoc field, Content contentTree) {
        TagletOutputImpl output = new TagletOutputImpl("");
        TagletWriter.genTagOuput(configuration().tagletManager, field,
                configuration().tagletManager.getCustomTags(field),
                writer.getTagletWriterInstance(false), output);
        String outputString = output.toString().trim();
        Content dlTags = new HtmlTree(HtmlTag.DL);
        if (!outputString.isEmpty()) {
            Content tagContent = new RawHtml(outputString);
            dlTags.addContent(tagContent);
        }
        contentTree.addContent(dlTags);
    }

    /**
     * Check to see if overview details should be printed. If
     * nocomment option set or if there is no text to be printed
     * for deprecation info, comment or tags, do not print overview details.
     *
     * @param field the field to check overview details for.
     * @return true if overview details need to be printed
     */
    public boolean shouldPrintOverview(FieldDoc field) {
        if (!configuration().nocomment) {
            if(!field.commentText().isEmpty() ||
                    writer.hasSerializationOverviewTags(field))
                return true;
        }
        if (field.tags("deprecated").length > 0)
            return true;
        return false;
    }
}
