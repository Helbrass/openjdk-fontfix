/*
 * Copyright (c) 2003, 2010, Oracle and/or its affiliates. All rights reserved.
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

package com.sun.tools.doclets.internal.toolkit.builders;

import com.sun.tools.doclets.internal.toolkit.*;
import com.sun.tools.doclets.internal.toolkit.util.*;
import java.util.*;

/**
 * The superclass for all member builders.  Member builders are only executed
 * within Class Builders.  They essentially build sub-components.  For example,
 * method documentation is a sub-component of class documentation.
 *
 * This code is not part of an API.
 * It is implementation that is subject to change.
 * Do not use it as an API
 *
 * @author Jamie Ho
 * @since 1.5
 */
public abstract class AbstractMemberBuilder extends AbstractBuilder {

    /**
     * Construct a SubBuilder.
     * @param configuration the configuration used in this run
     *        of the doclet.
     */
    public AbstractMemberBuilder(Configuration configuration) {
        super(configuration);
    }

    /**
     * This method is not supported by sub-builders.
     *
     * @throws DocletAbortException this method will always throw a
     * DocletAbortException because it is not supported.
     */
    public void build() throws DocletAbortException {
        //You may not call the build method in a subbuilder.
        throw new DocletAbortException();
    }


    /**
     * Build the sub component if there is anything to document.
     *
     * @param node the XML element that specifies which components to document.
     * @param contentTree content tree to which the documentation will be added
     */
    @Override
    public void build(XMLNode node, Content contentTree) {
        if (hasMembersToDocument()) {
            super.build(node, contentTree);
        }
    }

    /**
     * Return true if this subbuilder has anything to document.
     *
     * @return true if this subbuilder has anything to document.
     */
    public abstract boolean hasMembersToDocument();
}
