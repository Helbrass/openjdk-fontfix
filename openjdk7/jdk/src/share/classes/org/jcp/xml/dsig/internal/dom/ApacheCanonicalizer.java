/*
 * reserved comment block
 * DO NOT REMOVE OR ALTER!
 */
/*
 * Copyright 2005 The Apache Software Foundation.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 */
/*
 * Copyright (c) 2005, 2008, Oracle and/or its affiliates. All rights reserved.
 */
/*
 * $Id: ApacheCanonicalizer.java,v 1.2 2008/07/24 15:20:32 mullan Exp $
 */
package org.jcp.xml.dsig.internal.dom;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.security.spec.AlgorithmParameterSpec;
import java.security.InvalidAlgorithmParameterException;
import java.util.Set;
import java.util.logging.Logger;
import java.util.logging.Level;
import javax.xml.crypto.*;
import javax.xml.crypto.dom.DOMCryptoContext;
import javax.xml.crypto.dsig.TransformException;
import javax.xml.crypto.dsig.TransformService;
import javax.xml.crypto.dsig.XMLSignatureException;
import javax.xml.crypto.dsig.spec.C14NMethodParameterSpec;

import com.sun.org.apache.xml.internal.security.c14n.Canonicalizer;
import com.sun.org.apache.xml.internal.security.c14n.InvalidCanonicalizerException;
import com.sun.org.apache.xml.internal.security.signature.XMLSignatureInput;
import com.sun.org.apache.xml.internal.security.transforms.Transform;
import org.w3c.dom.Document;
import org.w3c.dom.Element;
import org.w3c.dom.NodeList;

public abstract class ApacheCanonicalizer extends TransformService {

    static {
        com.sun.org.apache.xml.internal.security.Init.init();
    }

    private static Logger log = Logger.getLogger("org.jcp.xml.dsig.internal.dom");
    protected Canonicalizer apacheCanonicalizer;
    private Transform apacheTransform;
    protected String inclusiveNamespaces;
    protected C14NMethodParameterSpec params;
    protected Document ownerDoc;
    protected Element transformElem;

    public final AlgorithmParameterSpec getParameterSpec() {
        return params;
    }

    public void init(XMLStructure parent, XMLCryptoContext context)
        throws InvalidAlgorithmParameterException {
        if (context != null && !(context instanceof DOMCryptoContext)) {
            throw new ClassCastException
                ("context must be of type DOMCryptoContext");
        }
        transformElem = (Element)
            ((javax.xml.crypto.dom.DOMStructure) parent).getNode();
        ownerDoc = DOMUtils.getOwnerDocument(transformElem);
    }

    public void marshalParams(XMLStructure parent, XMLCryptoContext context)
        throws MarshalException {
        if (context != null && !(context instanceof DOMCryptoContext)) {
            throw new ClassCastException
                ("context must be of type DOMCryptoContext");
        }
        transformElem = (Element)
            ((javax.xml.crypto.dom.DOMStructure) parent).getNode();
        ownerDoc = DOMUtils.getOwnerDocument(transformElem);
    }

    public Data canonicalize(Data data, XMLCryptoContext xc)
        throws TransformException {
        return canonicalize(data, xc, null);
    }

    public Data canonicalize(Data data, XMLCryptoContext xc, OutputStream os)
        throws TransformException {

        if (apacheCanonicalizer == null) {
            try {
                apacheCanonicalizer = Canonicalizer.getInstance(getAlgorithm());
                if (log.isLoggable(Level.FINE)) {
                    log.log(Level.FINE, "Created canonicalizer for algorithm: "
                        + getAlgorithm());
                }
            } catch (InvalidCanonicalizerException ice) {
                throw new TransformException
                    ("Couldn't find Canonicalizer for: " + getAlgorithm() +
                        ": " + ice.getMessage(), ice);
            }
        }

        if (os != null) {
            apacheCanonicalizer.setWriter(os);
        } else {
            apacheCanonicalizer.setWriter(new ByteArrayOutputStream());
        }

        try {
            Set nodeSet = null;
            if (data instanceof ApacheData) {
                XMLSignatureInput in =
                    ((ApacheData) data).getXMLSignatureInput();
                if (in.isElement()) {
                    if (inclusiveNamespaces != null) {
                        return new OctetStreamData(new ByteArrayInputStream
                            (apacheCanonicalizer.canonicalizeSubtree
                                (in.getSubNode(), inclusiveNamespaces)));
                    } else {
                        return new OctetStreamData(new ByteArrayInputStream
                            (apacheCanonicalizer.canonicalizeSubtree
                                (in.getSubNode())));
                    }
                } else if (in.isNodeSet()) {
                    nodeSet = in.getNodeSet();
                } else {
                    return new OctetStreamData(new ByteArrayInputStream(
                        apacheCanonicalizer.canonicalize(
                            Utils.readBytesFromStream(in.getOctetStream()))));
                }
            } else if (data instanceof DOMSubTreeData) {
                DOMSubTreeData subTree = (DOMSubTreeData) data;
                if (inclusiveNamespaces != null) {
                    return new OctetStreamData(new ByteArrayInputStream
                        (apacheCanonicalizer.canonicalizeSubtree
                         (subTree.getRoot(), inclusiveNamespaces)));
                } else {
                    return new OctetStreamData(new ByteArrayInputStream
                        (apacheCanonicalizer.canonicalizeSubtree
                         (subTree.getRoot())));
                }
            } else if (data instanceof NodeSetData) {
                NodeSetData nsd = (NodeSetData) data;
                // convert Iterator to Set
                nodeSet = Utils.toNodeSet(nsd.iterator());
                if (log.isLoggable(Level.FINE)) {
                    log.log(Level.FINE, "Canonicalizing " + nodeSet.size()
                        + " nodes");
                }
            } else {
                return new OctetStreamData(new ByteArrayInputStream(
                    apacheCanonicalizer.canonicalize(
                        Utils.readBytesFromStream(
                        ((OctetStreamData)data).getOctetStream()))));
            }
            if (inclusiveNamespaces != null) {
                return new OctetStreamData(new ByteArrayInputStream(
                    apacheCanonicalizer.canonicalizeXPathNodeSet
                        (nodeSet, inclusiveNamespaces)));
            } else {
                return new OctetStreamData(new ByteArrayInputStream(
                    apacheCanonicalizer.canonicalizeXPathNodeSet(nodeSet)));
            }
        } catch (Exception e) {
            throw new TransformException(e);
        }
    }

    public Data transform(Data data, XMLCryptoContext xc, OutputStream os)
        throws TransformException {
        if (data == null) {
            throw new NullPointerException("data must not be null");
        }
        if (os == null) {
            throw new NullPointerException("output stream must not be null");
        }

        if (ownerDoc == null) {
            throw new TransformException("transform must be marshalled");
        }

        if (apacheTransform == null) {
            try {
                apacheTransform = Transform.getInstance
                    (ownerDoc, getAlgorithm(), transformElem.getChildNodes());
                apacheTransform.setElement(transformElem, xc.getBaseURI());
                if (log.isLoggable(Level.FINE)) {
                    log.log(Level.FINE, "Created transform for algorithm: "
                        + getAlgorithm());
                }
            } catch (Exception ex) {
                throw new TransformException
                    ("Couldn't find Transform for: " + getAlgorithm(), ex);
            }
        }

        XMLSignatureInput in;
        if (data instanceof ApacheData) {
            if (log.isLoggable(Level.FINE)) {
                log.log(Level.FINE, "ApacheData = true");
            }
            in = ((ApacheData) data).getXMLSignatureInput();
        } else if (data instanceof NodeSetData) {
            if (log.isLoggable(Level.FINE)) {
                log.log(Level.FINE, "isNodeSet() = true");
            }
            if (data instanceof DOMSubTreeData) {
                DOMSubTreeData subTree = (DOMSubTreeData) data;
                in = new XMLSignatureInput(subTree.getRoot());
                in.setExcludeComments(subTree.excludeComments());
            } else {
                Set nodeSet =
                    Utils.toNodeSet(((NodeSetData) data).iterator());
                in = new XMLSignatureInput(nodeSet);
            }
        } else {
            if (log.isLoggable(Level.FINE)) {
                log.log(Level.FINE, "isNodeSet() = false");
            }
            try {
                in = new XMLSignatureInput
                    (((OctetStreamData)data).getOctetStream());
            } catch (Exception ex) {
                throw new TransformException(ex);
            }
        }

        try {
            in = apacheTransform.performTransform(in, os);
            if (!in.isNodeSet() && !in.isElement()) {
                return null;
            }
            if (in.isOctetStream()) {
                return new ApacheOctetStreamData(in);
            } else {
                return new ApacheNodeSetData(in);
            }
        } catch (Exception ex) {
            throw new TransformException(ex);
        }
    }

    public final boolean isFeatureSupported(String feature) {
        if (feature == null) {
            throw new NullPointerException();
        } else {
            return false;
        }
    }
}
