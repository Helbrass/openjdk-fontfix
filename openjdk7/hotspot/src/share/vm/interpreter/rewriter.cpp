/*
 * Copyright (c) 1998, 2011, Oracle and/or its affiliates. All rights reserved.
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
#include "interpreter/bytecodes.hpp"
#include "interpreter/interpreter.hpp"
#include "interpreter/rewriter.hpp"
#include "memory/gcLocker.hpp"
#include "memory/oopFactory.hpp"
#include "memory/resourceArea.hpp"
#include "oops/generateOopMap.hpp"
#include "oops/objArrayOop.hpp"
#include "oops/oop.inline.hpp"
#include "prims/methodComparator.hpp"

// Computes a CPC map (new_index -> original_index) for constant pool entries
// that are referred to by the interpreter at runtime via the constant pool cache.
// Also computes a CP map (original_index -> new_index).
// Marks entries in CP which require additional processing.
void Rewriter::compute_index_maps() {
  const int length  = _pool->length();
  init_cp_map(length);
  jint tag_mask = 0;
  for (int i = 0; i < length; i++) {
    int tag = _pool->tag_at(i).value();
    tag_mask |= (1 << tag);
    switch (tag) {
      case JVM_CONSTANT_InterfaceMethodref:
      case JVM_CONSTANT_Fieldref          : // fall through
      case JVM_CONSTANT_Methodref         : // fall through
      case JVM_CONSTANT_MethodHandle      : // fall through
      case JVM_CONSTANT_MethodType        : // fall through
      case JVM_CONSTANT_InvokeDynamic     : // fall through
        add_cp_cache_entry(i);
        break;
    }
  }

  guarantee((int)_cp_cache_map.length()-1 <= (int)((u2)-1),
            "all cp cache indexes fit in a u2");

  _have_invoke_dynamic = ((tag_mask & (1 << JVM_CONSTANT_InvokeDynamic)) != 0);
}

// Unrewrite the bytecodes if an error occurs.
void Rewriter::restore_bytecodes() {
  int len = _methods->length();

  for (int i = len-1; i >= 0; i--) {
    methodOop method = (methodOop)_methods->obj_at(i);
    scan_method(method, true);
  }
}

// Creates a constant pool cache given a CPC map
void Rewriter::make_constant_pool_cache(TRAPS) {
  const int length = _cp_cache_map.length();
  constantPoolCacheOop cache =
      oopFactory::new_constantPoolCache(length, CHECK);
  No_Safepoint_Verifier nsv;
  cache->initialize(_cp_cache_map);

  // Don't bother with the next pass if there is no JVM_CONSTANT_InvokeDynamic.
  if (_have_invoke_dynamic) {
    for (int i = 0; i < length; i++) {
      int pool_index = cp_cache_entry_pool_index(i);
      if (pool_index >= 0 &&
          _pool->tag_at(pool_index).is_invoke_dynamic()) {
        int bsm_index = _pool->invoke_dynamic_bootstrap_method_ref_index_at(pool_index);
        assert(_pool->tag_at(bsm_index).is_method_handle(), "must be a MH constant");
        // There is a CP cache entry holding the BSM for these calls.
        int bsm_cache_index = cp_entry_to_cp_cache(bsm_index);
        cache->entry_at(i)->initialize_bootstrap_method_index_in_cache(bsm_cache_index);
      }
    }
  }

  _pool->set_cache(cache);
  cache->set_constant_pool(_pool());
}



// The new finalization semantics says that registration of
// finalizable objects must be performed on successful return from the
// Object.<init> constructor.  We could implement this trivially if
// <init> were never rewritten but since JVMTI allows this to occur, a
// more complicated solution is required.  A special return bytecode
// is used only by Object.<init> to signal the finalization
// registration point.  Additionally local 0 must be preserved so it's
// available to pass to the registration function.  For simplicty we
// require that local 0 is never overwritten so it's available as an
// argument for registration.

void Rewriter::rewrite_Object_init(methodHandle method, TRAPS) {
  RawBytecodeStream bcs(method);
  while (!bcs.is_last_bytecode()) {
    Bytecodes::Code opcode = bcs.raw_next();
    switch (opcode) {
      case Bytecodes::_return: *bcs.bcp() = Bytecodes::_return_register_finalizer; break;

      case Bytecodes::_istore:
      case Bytecodes::_lstore:
      case Bytecodes::_fstore:
      case Bytecodes::_dstore:
      case Bytecodes::_astore:
        if (bcs.get_index() != 0) continue;

        // fall through
      case Bytecodes::_istore_0:
      case Bytecodes::_lstore_0:
      case Bytecodes::_fstore_0:
      case Bytecodes::_dstore_0:
      case Bytecodes::_astore_0:
        THROW_MSG(vmSymbols::java_lang_IncompatibleClassChangeError(),
                  "can't overwrite local 0 in Object.<init>");
        break;
    }
  }
}


// Rewrite a classfile-order CP index into a native-order CPC index.
void Rewriter::rewrite_member_reference(address bcp, int offset, bool reverse) {
  address p = bcp + offset;
  if (!reverse) {
    int  cp_index    = Bytes::get_Java_u2(p);
    int  cache_index = cp_entry_to_cp_cache(cp_index);
    Bytes::put_native_u2(p, cache_index);
  } else {
    int cache_index = Bytes::get_native_u2(p);
    int pool_index = cp_cache_entry_pool_index(cache_index);
    Bytes::put_Java_u2(p, pool_index);
  }
}


void Rewriter::rewrite_invokedynamic(address bcp, int offset, bool reverse) {
  address p = bcp + offset;
  assert(p[-1] == Bytecodes::_invokedynamic, "not invokedynamic bytecode");
  if (!reverse) {
    int cp_index = Bytes::get_Java_u2(p);
    int cpc  = maybe_add_cp_cache_entry(cp_index);  // add lazily
    int cpc2 = add_secondary_cp_cache_entry(cpc);

    // Replace the trailing four bytes with a CPC index for the dynamic
    // call site.  Unlike other CPC entries, there is one per bytecode,
    // not just one per distinct CP entry.  In other words, the
    // CPC-to-CP relation is many-to-one for invokedynamic entries.
    // This means we must use a larger index size than u2 to address
    // all these entries.  That is the main reason invokedynamic
    // must have a five-byte instruction format.  (Of course, other JVM
    // implementations can use the bytes for other purposes.)
    Bytes::put_native_u4(p, constantPoolCacheOopDesc::encode_secondary_index(cpc2));
    // Note: We use native_u4 format exclusively for 4-byte indexes.
  } else {
    int cache_index = constantPoolCacheOopDesc::decode_secondary_index(
                        Bytes::get_native_u4(p));
    int secondary_index = cp_cache_secondary_entry_main_index(cache_index);
    int pool_index = cp_cache_entry_pool_index(secondary_index);
    assert(_pool->tag_at(pool_index).is_invoke_dynamic(), "wrong index");
    // zero out 4 bytes
    Bytes::put_Java_u4(p, 0);
    Bytes::put_Java_u2(p, pool_index);
  }
}


// Rewrite some ldc bytecodes to _fast_aldc
void Rewriter::maybe_rewrite_ldc(address bcp, int offset, bool is_wide,
                                 bool reverse) {
  if (!reverse) {
    assert((*bcp) == (is_wide ? Bytecodes::_ldc_w : Bytecodes::_ldc), "not ldc bytecode");
    address p = bcp + offset;
    int cp_index = is_wide ? Bytes::get_Java_u2(p) : (u1)(*p);
    constantTag tag = _pool->tag_at(cp_index).value();
    if (tag.is_method_handle() || tag.is_method_type()) {
      int cache_index = cp_entry_to_cp_cache(cp_index);
      if (is_wide) {
        (*bcp) = Bytecodes::_fast_aldc_w;
        assert(cache_index == (u2)cache_index, "index overflow");
        Bytes::put_native_u2(p, cache_index);
      } else {
        (*bcp) = Bytecodes::_fast_aldc;
        assert(cache_index == (u1)cache_index, "index overflow");
        (*p) = (u1)cache_index;
      }
    }
  } else {
    Bytecodes::Code rewritten_bc =
              (is_wide ? Bytecodes::_fast_aldc_w : Bytecodes::_fast_aldc);
    if ((*bcp) == rewritten_bc) {
      address p = bcp + offset;
      int cache_index = is_wide ? Bytes::get_native_u2(p) : (u1)(*p);
      int pool_index = cp_cache_entry_pool_index(cache_index);
      if (is_wide) {
        (*bcp) = Bytecodes::_ldc_w;
        assert(pool_index == (u2)pool_index, "index overflow");
        Bytes::put_Java_u2(p, pool_index);
      } else {
        (*bcp) = Bytecodes::_ldc;
        assert(pool_index == (u1)pool_index, "index overflow");
        (*p) = (u1)pool_index;
      }
    }
  }
}


// Rewrites a method given the index_map information
void Rewriter::scan_method(methodOop method, bool reverse) {

  int nof_jsrs = 0;
  bool has_monitor_bytecodes = false;

  {
    // We cannot tolerate a GC in this block, because we've
    // cached the bytecodes in 'code_base'. If the methodOop
    // moves, the bytecodes will also move.
    No_Safepoint_Verifier nsv;
    Bytecodes::Code c;

    // Bytecodes and their length
    const address code_base = method->code_base();
    const int code_length = method->code_size();

    int bc_length;
    for (int bci = 0; bci < code_length; bci += bc_length) {
      address bcp = code_base + bci;
      int prefix_length = 0;
      c = (Bytecodes::Code)(*bcp);

      // Since we have the code, see if we can get the length
      // directly. Some more complicated bytecodes will report
      // a length of zero, meaning we need to make another method
      // call to calculate the length.
      bc_length = Bytecodes::length_for(c);
      if (bc_length == 0) {
        bc_length = Bytecodes::length_at(method, bcp);

        // length_at will put us at the bytecode after the one modified
        // by 'wide'. We don't currently examine any of the bytecodes
        // modified by wide, but in case we do in the future...
        if (c == Bytecodes::_wide) {
          prefix_length = 1;
          c = (Bytecodes::Code)bcp[1];
        }
      }

      assert(bc_length != 0, "impossible bytecode length");

      switch (c) {
        case Bytecodes::_lookupswitch   : {
#ifndef CC_INTERP
          Bytecode_lookupswitch bc(method, bcp);
          (*bcp) = (
            bc.number_of_pairs() < BinarySwitchThreshold
            ? Bytecodes::_fast_linearswitch
            : Bytecodes::_fast_binaryswitch
          );
#endif
          break;
        }
        case Bytecodes::_fast_linearswitch:
        case Bytecodes::_fast_binaryswitch: {
#ifndef CC_INTERP
          (*bcp) = Bytecodes::_lookupswitch;
#endif
          break;
        }
        case Bytecodes::_getstatic      : // fall through
        case Bytecodes::_putstatic      : // fall through
        case Bytecodes::_getfield       : // fall through
        case Bytecodes::_putfield       : // fall through
        case Bytecodes::_invokevirtual  : // fall through
        case Bytecodes::_invokespecial  : // fall through
        case Bytecodes::_invokestatic   :
        case Bytecodes::_invokeinterface:
          rewrite_member_reference(bcp, prefix_length+1, reverse);
          break;
        case Bytecodes::_invokedynamic:
          rewrite_invokedynamic(bcp, prefix_length+1, reverse);
          break;
        case Bytecodes::_ldc:
        case Bytecodes::_fast_aldc:
          maybe_rewrite_ldc(bcp, prefix_length+1, false, reverse);
          break;
        case Bytecodes::_ldc_w:
        case Bytecodes::_fast_aldc_w:
          maybe_rewrite_ldc(bcp, prefix_length+1, true, reverse);
          break;
        case Bytecodes::_jsr            : // fall through
        case Bytecodes::_jsr_w          : nof_jsrs++;                   break;
        case Bytecodes::_monitorenter   : // fall through
        case Bytecodes::_monitorexit    : has_monitor_bytecodes = true; break;
      }
    }
  }

  // Update access flags
  if (has_monitor_bytecodes) {
    method->set_has_monitor_bytecodes();
  }

  // The present of a jsr bytecode implies that the method might potentially
  // have to be rewritten, so we run the oopMapGenerator on the method
  if (nof_jsrs > 0) {
    method->set_has_jsrs();
    // Second pass will revisit this method.
    assert(method->has_jsrs(), "didn't we just set this?");
  }
}

// After constant pool is created, revisit methods containing jsrs.
methodHandle Rewriter::rewrite_jsrs(methodHandle method, TRAPS) {
  ResourceMark rm(THREAD);
  ResolveOopMapConflicts romc(method);
  methodHandle original_method = method;
  method = romc.do_potential_rewrite(CHECK_(methodHandle()));
  if (method() != original_method()) {
    // Insert invalid bytecode into original methodOop and set
    // interpreter entrypoint, so that a executing this method
    // will manifest itself in an easy recognizable form.
    address bcp = original_method->bcp_from(0);
    *bcp = (u1)Bytecodes::_shouldnotreachhere;
    int kind = Interpreter::method_kind(original_method);
    original_method->set_interpreter_kind(kind);
  }

  // Update monitor matching info.
  if (romc.monitor_safe()) {
    method->set_guaranteed_monitor_matching();
  }

  return method;
}

void Rewriter::rewrite(instanceKlassHandle klass, TRAPS) {
  ResourceMark rm(THREAD);
  Rewriter     rw(klass, klass->constants(), klass->methods(), CHECK);
  // (That's all, folks.)
}


void Rewriter::rewrite(instanceKlassHandle klass, constantPoolHandle cpool, objArrayHandle methods, TRAPS) {
  ResourceMark rm(THREAD);
  Rewriter     rw(klass, cpool, methods, CHECK);
  // (That's all, folks.)
}


Rewriter::Rewriter(instanceKlassHandle klass, constantPoolHandle cpool, objArrayHandle methods, TRAPS)
  : _klass(klass),
    _pool(cpool),
    _methods(methods)
{
  assert(_pool->cache() == NULL, "constant pool cache must not be set yet");

  // determine index maps for methodOop rewriting
  compute_index_maps();

  if (RegisterFinalizersAtInit && _klass->name() == vmSymbols::java_lang_Object()) {
    bool did_rewrite = false;
    int i = _methods->length();
    while (i-- > 0) {
      methodOop method = (methodOop)_methods->obj_at(i);
      if (method->intrinsic_id() == vmIntrinsics::_Object_init) {
        // rewrite the return bytecodes of Object.<init> to register the
        // object for finalization if needed.
        methodHandle m(THREAD, method);
        rewrite_Object_init(m, CHECK);
        did_rewrite = true;
        break;
      }
    }
    assert(did_rewrite, "must find Object::<init> to rewrite it");
  }

  // rewrite methods, in two passes
  int len = _methods->length();

  for (int i = len-1; i >= 0; i--) {
    methodOop method = (methodOop)_methods->obj_at(i);
    scan_method(method);
  }

  // allocate constant pool cache, now that we've seen all the bytecodes
  make_constant_pool_cache(THREAD);

  // Restore bytecodes to their unrewritten state if there are exceptions
  // rewriting bytecodes or allocating the cpCache
  if (HAS_PENDING_EXCEPTION) {
    restore_bytecodes();
    return;
  }
}

// Relocate jsr/rets in a method.  This can't be done with the rewriter
// stage because it can throw other exceptions, leaving the bytecodes
// pointing at constant pool cache entries.
// Link and check jvmti dependencies while we're iterating over the methods.
// JSR292 code calls with a different set of methods, so two entry points.
void Rewriter::relocate_and_link(instanceKlassHandle this_oop, TRAPS) {
  objArrayHandle methods(THREAD, this_oop->methods());
  relocate_and_link(this_oop, methods, THREAD);
}

void Rewriter::relocate_and_link(instanceKlassHandle this_oop,
                                 objArrayHandle methods, TRAPS) {
  int len = methods->length();
  for (int i = len-1; i >= 0; i--) {
    methodHandle m(THREAD, (methodOop)methods->obj_at(i));

    if (m->has_jsrs()) {
      m = rewrite_jsrs(m, CHECK);
      // Method might have gotten rewritten.
      methods->obj_at_put(i, m());
    }

    // Set up method entry points for compiler and interpreter    .
    m->link_method(m, CHECK);

    // This is for JVMTI and unrelated to relocator but the last thing we do
#ifdef ASSERT
    if (StressMethodComparator) {
      static int nmc = 0;
      for (int j = i; j >= 0 && j >= i-4; j--) {
        if ((++nmc % 1000) == 0)  tty->print_cr("Have run MethodComparator %d times...", nmc);
        bool z = MethodComparator::methods_EMCP(m(),
                   (methodOop)methods->obj_at(j));
        if (j == i && !z) {
          tty->print("MethodComparator FAIL: "); m->print(); m->print_codes();
          assert(z, "method must compare equal to itself");
        }
      }
    }
#endif //ASSERT
  }
}
