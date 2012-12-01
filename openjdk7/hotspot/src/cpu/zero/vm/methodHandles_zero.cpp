/*
 * Copyright (c) 1997, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright 2009, 2010, 2011 Red Hat, Inc.
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
 *
 */

#include "precompiled.hpp"
#include "interpreter/interpreter.hpp"
#include "memory/allocation.inline.hpp"
#include "prims/methodHandles.hpp"

int MethodHandles::adapter_conversion_ops_supported_mask() {
  return ((1<<java_lang_invoke_AdapterMethodHandle::OP_RETYPE_ONLY)
         |(1<<java_lang_invoke_AdapterMethodHandle::OP_RETYPE_RAW)
         |(1<<java_lang_invoke_AdapterMethodHandle::OP_CHECK_CAST)
         |(1<<java_lang_invoke_AdapterMethodHandle::OP_PRIM_TO_PRIM)
         |(1<<java_lang_invoke_AdapterMethodHandle::OP_REF_TO_PRIM)
         |(1<<java_lang_invoke_AdapterMethodHandle::OP_SWAP_ARGS)
         |(1<<java_lang_invoke_AdapterMethodHandle::OP_ROT_ARGS)
         |(1<<java_lang_invoke_AdapterMethodHandle::OP_DUP_ARGS)
         |(1<<java_lang_invoke_AdapterMethodHandle::OP_DROP_ARGS)
         //|(1<<java_lang_invoke_AdapterMethodHandle::OP_SPREAD_ARGS) //BUG!
         );
  // FIXME: MethodHandlesTest gets a crash if we enable OP_SPREAD_ARGS.
}

void MethodHandles::generate_method_handle_stub(MacroAssembler*          masm,
                                                MethodHandles::EntryKind ek) {
  init_entry(ek, (MethodHandleEntry *) ek);
}
