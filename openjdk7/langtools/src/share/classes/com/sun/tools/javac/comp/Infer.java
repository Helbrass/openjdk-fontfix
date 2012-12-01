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

package com.sun.tools.javac.comp;

import com.sun.tools.javac.tree.JCTree;
import com.sun.tools.javac.tree.JCTree.JCTypeCast;
import com.sun.tools.javac.tree.TreeInfo;
import com.sun.tools.javac.util.*;
import com.sun.tools.javac.util.List;
import com.sun.tools.javac.code.*;
import com.sun.tools.javac.code.Type.*;
import com.sun.tools.javac.code.Type.ForAll.ConstraintKind;
import com.sun.tools.javac.code.Symbol.*;
import com.sun.tools.javac.util.JCDiagnostic;

import static com.sun.tools.javac.code.TypeTags.*;

/** Helper class for type parameter inference, used by the attribution phase.
 *
 *  <p><b>This is NOT part of any supported API.
 *  If you write code that depends on this, you do so at your own risk.
 *  This code and its internal interfaces are subject to change or
 *  deletion without notice.</b>
 */
public class Infer {
    protected static final Context.Key<Infer> inferKey =
        new Context.Key<Infer>();

    /** A value for prototypes that admit any type, including polymorphic ones. */
    public static final Type anyPoly = new Type(NONE, null);

    Symtab syms;
    Types types;
    Check chk;
    Resolve rs;
    JCDiagnostic.Factory diags;

    public static Infer instance(Context context) {
        Infer instance = context.get(inferKey);
        if (instance == null)
            instance = new Infer(context);
        return instance;
    }

    protected Infer(Context context) {
        context.put(inferKey, this);
        syms = Symtab.instance(context);
        types = Types.instance(context);
        rs = Resolve.instance(context);
        chk = Check.instance(context);
        diags = JCDiagnostic.Factory.instance(context);
        ambiguousNoInstanceException =
            new NoInstanceException(true, diags);
        unambiguousNoInstanceException =
            new NoInstanceException(false, diags);
        invalidInstanceException =
            new InvalidInstanceException(diags);

    }

    public static class InferenceException extends Resolve.InapplicableMethodException {
        private static final long serialVersionUID = 0;

        InferenceException(JCDiagnostic.Factory diags) {
            super(diags);
        }
    }

    public static class NoInstanceException extends InferenceException {
        private static final long serialVersionUID = 1;

        boolean isAmbiguous; // exist several incomparable best instances?

        NoInstanceException(boolean isAmbiguous, JCDiagnostic.Factory diags) {
            super(diags);
            this.isAmbiguous = isAmbiguous;
        }
    }

    public static class InvalidInstanceException extends InferenceException {
        private static final long serialVersionUID = 2;

        InvalidInstanceException(JCDiagnostic.Factory diags) {
            super(diags);
        }
    }

    private final NoInstanceException ambiguousNoInstanceException;
    private final NoInstanceException unambiguousNoInstanceException;
    private final InvalidInstanceException invalidInstanceException;

/***************************************************************************
 * Auxiliary type values and classes
 ***************************************************************************/

    /** A mapping that turns type variables into undetermined type variables.
     */
    Mapping fromTypeVarFun = new Mapping("fromTypeVarFun") {
            public Type apply(Type t) {
                if (t.tag == TYPEVAR) return new UndetVar(t);
                else return t.map(this);
            }
        };

    /** A mapping that returns its type argument with every UndetVar replaced
     *  by its `inst' field. Throws a NoInstanceException
     *  if this not possible because an `inst' field is null.
     *  Note: mutually referring undertvars will be left uninstantiated
     *  (that is, they will be replaced by the underlying type-variable).
     */

    Mapping getInstFun = new Mapping("getInstFun") {
            public Type apply(Type t) {
                switch (t.tag) {
                    case UNKNOWN:
                        throw ambiguousNoInstanceException
                            .setMessage("undetermined.type");
                    case UNDETVAR:
                        UndetVar that = (UndetVar) t;
                        if (that.inst == null)
                            throw ambiguousNoInstanceException
                                .setMessage("type.variable.has.undetermined.type",
                                            that.qtype);
                        return isConstraintCyclic(that) ?
                            that.qtype :
                            apply(that.inst);
                        default:
                            return t.map(this);
                }
            }

            private boolean isConstraintCyclic(UndetVar uv) {
                Types.UnaryVisitor<Boolean> constraintScanner =
                        new Types.UnaryVisitor<Boolean>() {

                    List<Type> seen = List.nil();

                    Boolean visit(List<Type> ts) {
                        for (Type t : ts) {
                            if (visit(t)) return true;
                        }
                        return false;
                    }

                    public Boolean visitType(Type t, Void ignored) {
                        return false;
                    }

                    @Override
                    public Boolean visitClassType(ClassType t, Void ignored) {
                        if (t.isCompound()) {
                            return visit(types.supertype(t)) ||
                                    visit(types.interfaces(t));
                        } else {
                            return visit(t.getTypeArguments());
                        }
                    }
                    @Override
                    public Boolean visitWildcardType(WildcardType t, Void ignored) {
                        return visit(t.type);
                    }

                    @Override
                    public Boolean visitUndetVar(UndetVar t, Void ignored) {
                        if (seen.contains(t)) {
                            return true;
                        } else {
                            seen = seen.prepend(t);
                            return visit(t.inst);
                        }
                    }
                };
                return constraintScanner.visit(uv);
            }
        };

/***************************************************************************
 * Mini/Maximization of UndetVars
 ***************************************************************************/

    /** Instantiate undetermined type variable to its minimal upper bound.
     *  Throw a NoInstanceException if this not possible.
     */
    void maximizeInst(UndetVar that, Warner warn) throws NoInstanceException {
        List<Type> hibounds = Type.filter(that.hibounds, errorFilter);
        if (that.inst == null) {
            if (hibounds.isEmpty())
                that.inst = syms.objectType;
            else if (hibounds.tail.isEmpty())
                that.inst = hibounds.head;
            else
                that.inst = types.glb(hibounds);
        }
        if (that.inst == null ||
            that.inst.isErroneous())
            throw ambiguousNoInstanceException
                .setMessage("no.unique.maximal.instance.exists",
                            that.qtype, hibounds);
    }
    //where
        private boolean isSubClass(Type t, final List<Type> ts) {
            t = t.baseType();
            if (t.tag == TYPEVAR) {
                List<Type> bounds = types.getBounds((TypeVar)t);
                for (Type s : ts) {
                    if (!types.isSameType(t, s.baseType())) {
                        for (Type bound : bounds) {
                            if (!isSubClass(bound, List.of(s.baseType())))
                                return false;
                        }
                    }
                }
            } else {
                for (Type s : ts) {
                    if (!t.tsym.isSubClass(s.baseType().tsym, types))
                        return false;
                }
            }
            return true;
        }

    private Filter<Type> errorFilter = new Filter<Type>() {
        @Override
        public boolean accepts(Type t) {
            return !t.isErroneous();
        }
    };

    /** Instantiate undetermined type variable to the lub of all its lower bounds.
     *  Throw a NoInstanceException if this not possible.
     */
    void minimizeInst(UndetVar that, Warner warn) throws NoInstanceException {
        List<Type> lobounds = Type.filter(that.lobounds, errorFilter);
        if (that.inst == null) {
            if (lobounds.isEmpty())
                that.inst = syms.botType;
            else if (lobounds.tail.isEmpty())
                that.inst = lobounds.head.isPrimitive() ? syms.errType : lobounds.head;
            else {
                that.inst = types.lub(lobounds);
            }
            if (that.inst == null || that.inst.tag == ERROR)
                    throw ambiguousNoInstanceException
                        .setMessage("no.unique.minimal.instance.exists",
                                    that.qtype, lobounds);
            // VGJ: sort of inlined maximizeInst() below.  Adding
            // bounds can cause lobounds that are above hibounds.
            List<Type> hibounds = Type.filter(that.hibounds, errorFilter);
            if (hibounds.isEmpty())
                return;
            Type hb = null;
            if (hibounds.tail.isEmpty())
                hb = hibounds.head;
            else for (List<Type> bs = hibounds;
                      bs.nonEmpty() && hb == null;
                      bs = bs.tail) {
                if (isSubClass(bs.head, hibounds))
                    hb = types.fromUnknownFun.apply(bs.head);
            }
            if (hb == null ||
                !types.isSubtypeUnchecked(hb, hibounds, warn) ||
                !types.isSubtypeUnchecked(that.inst, hb, warn))
                throw ambiguousNoInstanceException;
        }
    }

/***************************************************************************
 * Exported Methods
 ***************************************************************************/

    /** Try to instantiate expression type `that' to given type `to'.
     *  If a maximal instantiation exists which makes this type
     *  a subtype of type `to', return the instantiated type.
     *  If no instantiation exists, or if several incomparable
     *  best instantiations exist throw a NoInstanceException.
     */
    public Type instantiateExpr(ForAll that,
                                Type to,
                                Warner warn) throws InferenceException {
        List<Type> undetvars = Type.map(that.tvars, fromTypeVarFun);
        for (List<Type> l = undetvars; l.nonEmpty(); l = l.tail) {
            UndetVar uv = (UndetVar) l.head;
            TypeVar tv = (TypeVar)uv.qtype;
            ListBuffer<Type> hibounds = new ListBuffer<Type>();
            for (Type t : that.getConstraints(tv, ConstraintKind.EXTENDS)) {
                hibounds.append(types.subst(t, that.tvars, undetvars));
            }

            List<Type> inst = that.getConstraints(tv, ConstraintKind.EQUAL);
            if (inst.nonEmpty() && inst.head.tag != BOT) {
                uv.inst = inst.head;
            }
            uv.hibounds = hibounds.toList();
        }
        Type qtype1 = types.subst(that.qtype, that.tvars, undetvars);
        if (!types.isSubtype(qtype1,
                qtype1.tag == UNDETVAR ? types.boxedTypeOrType(to) : to)) {
            throw unambiguousNoInstanceException
                .setMessage("infer.no.conforming.instance.exists",
                            that.tvars, that.qtype, to);
        }
        for (List<Type> l = undetvars; l.nonEmpty(); l = l.tail)
            maximizeInst((UndetVar) l.head, warn);
        // System.out.println(" = " + qtype1.map(getInstFun));//DEBUG

        // check bounds
        List<Type> targs = Type.map(undetvars, getInstFun);
        if (Type.containsAny(targs, that.tvars)) {
            //replace uninferred type-vars
            targs = types.subst(targs,
                    that.tvars,
                    instaniateAsUninferredVars(undetvars, that.tvars));
        }
        return chk.checkType(warn.pos(), that.inst(targs, types), to);
    }
    //where
    private List<Type> instaniateAsUninferredVars(List<Type> undetvars, List<Type> tvars) {
        ListBuffer<Type> new_targs = ListBuffer.lb();
        //step 1 - create syntethic captured vars
        for (Type t : undetvars) {
            UndetVar uv = (UndetVar)t;
            Type newArg = new CapturedType(t.tsym.name, t.tsym, uv.inst, syms.botType, null);
            new_targs = new_targs.append(newArg);
        }
        //step 2 - replace synthetic vars in their bounds
        for (Type t : new_targs.toList()) {
            CapturedType ct = (CapturedType)t;
            ct.bound = types.subst(ct.bound, tvars, new_targs.toList());
            WildcardType wt = new WildcardType(ct.bound, BoundKind.EXTENDS, syms.boundClass);
            ct.wildcard = wt;
        }
        return new_targs.toList();
    }

    /** Instantiate method type `mt' by finding instantiations of
     *  `tvars' so that method can be applied to `argtypes'.
     */
    public Type instantiateMethod(final Env<AttrContext> env,
                                  List<Type> tvars,
                                  MethodType mt,
                                  final Symbol msym,
                                  final List<Type> argtypes,
                                  final boolean allowBoxing,
                                  final boolean useVarargs,
                                  final Warner warn) throws InferenceException {
        //-System.err.println("instantiateMethod(" + tvars + ", " + mt + ", " + argtypes + ")"); //DEBUG
        List<Type> undetvars = Type.map(tvars, fromTypeVarFun);
        List<Type> formals = mt.argtypes;
        //need to capture exactly once - otherwise subsequent
        //applicability checks might fail
        final List<Type> capturedArgs = types.capture(argtypes);
        List<Type> actuals = capturedArgs;
        List<Type> actualsNoCapture = argtypes;
        // instantiate all polymorphic argument types and
        // set up lower bounds constraints for undetvars
        Type varargsFormal = useVarargs ? formals.last() : null;
        if (varargsFormal == null &&
                actuals.size() != formals.size()) {
            throw unambiguousNoInstanceException
                .setMessage("infer.arg.length.mismatch");
        }
        while (actuals.nonEmpty() && formals.head != varargsFormal) {
            Type formal = formals.head;
            Type actual = actuals.head.baseType();
            Type actualNoCapture = actualsNoCapture.head.baseType();
            if (actual.tag == FORALL)
                actual = instantiateArg((ForAll)actual, formal, tvars, warn);
            Type undetFormal = types.subst(formal, tvars, undetvars);
            boolean works = allowBoxing
                ? types.isConvertible(actual, undetFormal, warn)
                : types.isSubtypeUnchecked(actual, undetFormal, warn);
            if (!works) {
                throw unambiguousNoInstanceException
                    .setMessage("infer.no.conforming.assignment.exists",
                                tvars, actualNoCapture, formal);
            }
            formals = formals.tail;
            actuals = actuals.tail;
            actualsNoCapture = actualsNoCapture.tail;
        }

        if (formals.head != varargsFormal) // not enough args
            throw unambiguousNoInstanceException.setMessage("infer.arg.length.mismatch");

        // for varargs arguments as well
        if (useVarargs) {
            Type elemType = types.elemtype(varargsFormal);
            Type elemUndet = types.subst(elemType, tvars, undetvars);
            while (actuals.nonEmpty()) {
                Type actual = actuals.head.baseType();
                Type actualNoCapture = actualsNoCapture.head.baseType();
                if (actual.tag == FORALL)
                    actual = instantiateArg((ForAll)actual, elemType, tvars, warn);
                boolean works = types.isConvertible(actual, elemUndet, warn);
                if (!works) {
                    throw unambiguousNoInstanceException
                        .setMessage("infer.no.conforming.assignment.exists",
                                    tvars, actualNoCapture, elemType);
                }
                actuals = actuals.tail;
                actualsNoCapture = actualsNoCapture.tail;
            }
        }

        // minimize as yet undetermined type variables
        for (Type t : undetvars)
            minimizeInst((UndetVar) t, warn);

        /** Type variables instantiated to bottom */
        ListBuffer<Type> restvars = new ListBuffer<Type>();

        /** Undet vars instantiated to bottom */
        final ListBuffer<Type> restundet = new ListBuffer<Type>();

        /** Instantiated types or TypeVars if under-constrained */
        ListBuffer<Type> insttypes = new ListBuffer<Type>();

        /** Instantiated types or UndetVars if under-constrained */
        ListBuffer<Type> undettypes = new ListBuffer<Type>();

        for (Type t : undetvars) {
            UndetVar uv = (UndetVar)t;
            if (uv.inst.tag == BOT) {
                restvars.append(uv.qtype);
                restundet.append(uv);
                insttypes.append(uv.qtype);
                undettypes.append(uv);
                uv.inst = null;
            } else {
                insttypes.append(uv.inst);
                undettypes.append(uv.inst);
            }
        }
        checkWithinBounds(tvars, undettypes.toList(), warn);

        mt = (MethodType)types.subst(mt, tvars, insttypes.toList());

        if (!restvars.isEmpty()) {
            // if there are uninstantiated variables,
            // quantify result type with them
            final List<Type> inferredTypes = insttypes.toList();
            final List<Type> all_tvars = tvars; //this is the wrong tvars
            return new UninferredMethodType(mt, restvars.toList()) {
                @Override
                List<Type> getConstraints(TypeVar tv, ConstraintKind ck) {
                    for (Type t : restundet.toList()) {
                        UndetVar uv = (UndetVar)t;
                        if (uv.qtype == tv) {
                            switch (ck) {
                                case EXTENDS: return uv.hibounds.appendList(types.subst(types.getBounds(tv), all_tvars, inferredTypes));
                                case SUPER: return uv.lobounds;
                                case EQUAL: return uv.inst != null ? List.of(uv.inst) : List.<Type>nil();
                            }
                        }
                    }
                    return List.nil();
                }
                @Override
                void check(List<Type> inferred, Types types) throws NoInstanceException {
                    // check that actuals conform to inferred formals
                    checkArgumentsAcceptable(env, capturedArgs, getParameterTypes(), allowBoxing, useVarargs, warn);
                    // check that inferred bounds conform to their bounds
                    checkWithinBounds(all_tvars,
                           types.subst(inferredTypes, tvars, inferred), warn);
                    if (useVarargs) {
                        chk.checkVararg(env.tree.pos(), getParameterTypes(), msym);
                    }
            }};
        }
        else {
            // check that actuals conform to inferred formals
            checkArgumentsAcceptable(env, capturedArgs, mt.getParameterTypes(), allowBoxing, useVarargs, warn);
            // return instantiated version of method type
            return mt;
        }
    }
    //where

        /**
         * A delegated type representing a partially uninferred method type.
         * The return type of a partially uninferred method type is a ForAll
         * type - when the return type is instantiated (see Infer.instantiateExpr)
         * the underlying method type is also updated.
         */
        static abstract class UninferredMethodType extends DelegatedType {

            final List<Type> tvars;

            public UninferredMethodType(MethodType mtype, List<Type> tvars) {
                super(METHOD, new MethodType(mtype.argtypes, null, mtype.thrown, mtype.tsym));
                this.tvars = tvars;
                asMethodType().restype = new UninferredReturnType(tvars, mtype.restype);
            }

            @Override
            public MethodType asMethodType() {
                return qtype.asMethodType();
            }

            @Override
            public Type map(Mapping f) {
                return qtype.map(f);
            }

            void instantiateReturnType(Type restype, List<Type> inferred, Types types) throws NoInstanceException {
                //update method type with newly inferred type-arguments
                qtype = new MethodType(types.subst(getParameterTypes(), tvars, inferred),
                                       restype,
                                       types.subst(UninferredMethodType.this.getThrownTypes(), tvars, inferred),
                                       UninferredMethodType.this.qtype.tsym);
                check(inferred, types);
            }

            abstract void check(List<Type> inferred, Types types) throws NoInstanceException;

            abstract List<Type> getConstraints(TypeVar tv, ConstraintKind ck);

            class UninferredReturnType extends ForAll {
                public UninferredReturnType(List<Type> tvars, Type restype) {
                    super(tvars, restype);
                }
                @Override
                public Type inst(List<Type> actuals, Types types) {
                    Type newRestype = super.inst(actuals, types);
                    instantiateReturnType(newRestype, actuals, types);
                    return newRestype;
                }
                @Override
                public List<Type> getConstraints(TypeVar tv, ConstraintKind ck) {
                    return UninferredMethodType.this.getConstraints(tv, ck);
                }
            }
        }

        private void checkArgumentsAcceptable(Env<AttrContext> env, List<Type> actuals, List<Type> formals,
                boolean allowBoxing, boolean useVarargs, Warner warn) {
            try {
                rs.checkRawArgumentsAcceptable(env, actuals, formals,
                       allowBoxing, useVarargs, warn);
            }
            catch (Resolve.InapplicableMethodException ex) {
                // inferred method is not applicable
                throw invalidInstanceException.setMessage(ex.getDiagnostic());
            }
        }

    /** Try to instantiate argument type `that' to given type `to'.
     *  If this fails, try to insantiate `that' to `to' where
     *  every occurrence of a type variable in `tvars' is replaced
     *  by an unknown type.
     */
    private Type instantiateArg(ForAll that,
                                Type to,
                                List<Type> tvars,
                                Warner warn) throws InferenceException {
        List<Type> targs;
        try {
            return instantiateExpr(that, to, warn);
        } catch (NoInstanceException ex) {
            Type to1 = to;
            for (List<Type> l = tvars; l.nonEmpty(); l = l.tail)
                to1 = types.subst(to1, List.of(l.head), List.of(syms.unknownType));
            return instantiateExpr(that, to1, warn);
        }
    }

    /** check that type parameters are within their bounds.
     */
    void checkWithinBounds(List<Type> tvars,
                                   List<Type> arguments,
                                   Warner warn)
        throws InvalidInstanceException {
        for (List<Type> tvs = tvars, args = arguments;
             tvs.nonEmpty();
             tvs = tvs.tail, args = args.tail) {
            if (args.head instanceof UndetVar ||
                    tvars.head.getUpperBound().isErroneous()) continue;
            List<Type> bounds = types.subst(types.getBounds((TypeVar)tvs.head), tvars, arguments);
            if (!types.isSubtypeUnchecked(args.head, bounds, warn))
                throw invalidInstanceException
                    .setMessage("inferred.do.not.conform.to.bounds",
                                args.head, bounds);
        }
    }

    /**
     * Compute a synthetic method type corresponding to the requested polymorphic
     * method signature. The target return type is computed from the immediately
     * enclosing scope surrounding the polymorphic-signature call.
     */
    Type instantiatePolymorphicSignatureInstance(Env<AttrContext> env, Type site,
                                            Name name,
                                            MethodSymbol spMethod,  // sig. poly. method or null if none
                                            List<Type> argtypes) {
        final Type restype;

        //The return type for a polymorphic signature call is computed from
        //the enclosing tree E, as follows: if E is a cast, then use the
        //target type of the cast expression as a return type; if E is an
        //expression statement, the return type is 'void' - otherwise the
        //return type is simply 'Object'. A correctness check ensures that
        //env.next refers to the lexically enclosing environment in which
        //the polymorphic signature call environment is nested.

        switch (env.next.tree.getTag()) {
            case JCTree.TYPECAST:
                JCTypeCast castTree = (JCTypeCast)env.next.tree;
                restype = (TreeInfo.skipParens(castTree.expr) == env.tree) ?
                    castTree.clazz.type :
                    syms.objectType;
                break;
            case JCTree.EXEC:
                JCTree.JCExpressionStatement execTree =
                        (JCTree.JCExpressionStatement)env.next.tree;
                restype = (TreeInfo.skipParens(execTree.expr) == env.tree) ?
                    syms.voidType :
                    syms.objectType;
                break;
            default:
                restype = syms.objectType;
        }

        List<Type> paramtypes = Type.map(argtypes, implicitArgType);
        List<Type> exType = spMethod != null ?
            spMethod.getThrownTypes() :
            List.of(syms.throwableType); // make it throw all exceptions

        MethodType mtype = new MethodType(paramtypes,
                                          restype,
                                          exType,
                                          syms.methodClass);
        return mtype;
    }
    //where
        Mapping implicitArgType = new Mapping ("implicitArgType") {
                public Type apply(Type t) {
                    t = types.erasure(t);
                    if (t.tag == BOT)
                        // nulls type as the marker type Null (which has no instances)
                        // infer as java.lang.Void for now
                        t = types.boxedClass(syms.voidType).type;
                    return t;
                }
        };
    }
