/*
 * Copyright (c) 1997, 2011, Oracle and/or its affiliates. All rights reserved.
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
#include "classfile/javaClasses.hpp"
#include "gc_implementation/shared/markSweep.inline.hpp"
#include "gc_interface/collectedHeap.inline.hpp"
#include "memory/oopFactory.hpp"
#include "memory/permGen.hpp"
#include "memory/universe.inline.hpp"
#include "oops/constantPoolKlass.hpp"
#include "oops/constantPoolOop.hpp"
#include "oops/oop.inline.hpp"
#include "oops/oop.inline2.hpp"
#include "oops/symbol.hpp"
#include "runtime/handles.inline.hpp"
#ifdef TARGET_OS_FAMILY_linux
# include "thread_linux.inline.hpp"
#endif
#ifdef TARGET_OS_FAMILY_solaris
# include "thread_solaris.inline.hpp"
#endif
#ifdef TARGET_OS_FAMILY_windows
# include "thread_windows.inline.hpp"
#endif
#ifndef SERIALGC
#include "gc_implementation/parNew/parOopClosures.inline.hpp"
#include "gc_implementation/parallelScavenge/psPromotionManager.inline.hpp"
#include "gc_implementation/parallelScavenge/psScavenge.inline.hpp"
#include "memory/cardTableRS.hpp"
#include "oops/oop.pcgc.inline.hpp"
#endif

constantPoolOop constantPoolKlass::allocate(int length, bool is_conc_safe, TRAPS) {
  int size = constantPoolOopDesc::object_size(length);
  KlassHandle klass (THREAD, as_klassOop());
  assert(klass()->is_oop(), "Can't be null, else handlizing of c below won't work");
  constantPoolHandle pool;
  {
    constantPoolOop c =
      (constantPoolOop)CollectedHeap::permanent_obj_allocate(klass, size, CHECK_NULL);
    assert(c->klass_or_null() != NULL, "Handlizing below won't work");
    pool = constantPoolHandle(THREAD, c);
  }

  pool->set_length(length);
  pool->set_tags(NULL);
  pool->set_cache(NULL);
  pool->set_operands(NULL);
  pool->set_pool_holder(NULL);
  pool->set_flags(0);
  // only set to non-zero if constant pool is merged by RedefineClasses
  pool->set_orig_length(0);
  // if constant pool may change during RedefineClasses, it is created
  // unsafe for GC concurrent processing.
  pool->set_is_conc_safe(is_conc_safe);
  // all fields are initialized; needed for GC

  // Note: because we may be in this "conc_unsafe" state when allocating
  // t_oop below, which may in turn cause a GC, it is imperative that our
  // size be correct, consistent and henceforth stable, at this stage.
  assert(pool->is_oop() && pool->is_parsable(), "Else size() below is unreliable");
  assert(size == pool->size(), "size() is wrong");

  // initialize tag array
  typeArrayOop t_oop = oopFactory::new_permanent_byteArray(length, CHECK_NULL);
  typeArrayHandle tags (THREAD, t_oop);
  for (int index = 0; index < length; index++) {
    tags()->byte_at_put(index, JVM_CONSTANT_Invalid);
  }
  pool->set_tags(tags());

  // Check that our size was stable at its old value.
  assert(size == pool->size(), "size() changed");
  return pool();
}

klassOop constantPoolKlass::create_klass(TRAPS) {
  constantPoolKlass o;
  KlassHandle h_this_klass(THREAD, Universe::klassKlassObj());
  KlassHandle k = base_create_klass(h_this_klass, header_size(), o.vtbl_value(), CHECK_NULL);
  // Make sure size calculation is right
  assert(k()->size() == align_object_size(header_size()), "wrong size for object");
  java_lang_Class::create_mirror(k, CHECK_NULL); // Allocate mirror
  return k();
}

int constantPoolKlass::oop_size(oop obj) const {
  assert(obj->is_constantPool(), "must be constantPool");
  return constantPoolOop(obj)->object_size();
}


void constantPoolKlass::oop_follow_contents(oop obj) {
  assert (obj->is_constantPool(), "obj must be constant pool");
  constantPoolOop cp = (constantPoolOop) obj;
  // Performance tweak: We skip iterating over the klass pointer since we
  // know that Universe::constantPoolKlassObj never moves.

  // If the tags array is null we are in the middle of allocating this constant pool
  if (cp->tags() != NULL) {
    // gc of constant pool contents
    oop* base = (oop*)cp->base();
    for (int i = 0; i < cp->length(); i++) {
      if (cp->is_pointer_entry(i)) {
        if (*base != NULL) MarkSweep::mark_and_push(base);
      }
      base++;
    }
    // gc of constant pool instance variables
    MarkSweep::mark_and_push(cp->tags_addr());
    MarkSweep::mark_and_push(cp->cache_addr());
    MarkSweep::mark_and_push(cp->operands_addr());
    MarkSweep::mark_and_push(cp->pool_holder_addr());
  }
}

#ifndef SERIALGC
void constantPoolKlass::oop_follow_contents(ParCompactionManager* cm,
                                            oop obj) {
  assert (obj->is_constantPool(), "obj must be constant pool");
  constantPoolOop cp = (constantPoolOop) obj;
  // Performance tweak: We skip iterating over the klass pointer since we
  // know that Universe::constantPoolKlassObj never moves.

  // If the tags array is null we are in the middle of allocating this constant
  // pool.
  if (cp->tags() != NULL) {
    // gc of constant pool contents
    oop* base = (oop*)cp->base();
    for (int i = 0; i < cp->length(); i++) {
      if (cp->is_pointer_entry(i)) {
        if (*base != NULL) PSParallelCompact::mark_and_push(cm, base);
      }
      base++;
    }
    // gc of constant pool instance variables
    PSParallelCompact::mark_and_push(cm, cp->tags_addr());
    PSParallelCompact::mark_and_push(cm, cp->cache_addr());
    PSParallelCompact::mark_and_push(cm, cp->operands_addr());
    PSParallelCompact::mark_and_push(cm, cp->pool_holder_addr());
  }
}
#endif // SERIALGC


int constantPoolKlass::oop_adjust_pointers(oop obj) {
  assert (obj->is_constantPool(), "obj must be constant pool");
  constantPoolOop cp = (constantPoolOop) obj;
  // Get size before changing pointers.
  // Don't call size() or oop_size() since that is a virtual call.
  int size = cp->object_size();
  // Performance tweak: We skip iterating over the klass pointer since we
  // know that Universe::constantPoolKlassObj never moves.

  // If the tags array is null we are in the middle of allocating this constant
  // pool.
  if (cp->tags() != NULL) {
    oop* base = (oop*)cp->base();
    for (int i = 0; i< cp->length();  i++) {
      if (cp->is_pointer_entry(i)) {
        MarkSweep::adjust_pointer(base);
      }
      base++;
    }
  }
  MarkSweep::adjust_pointer(cp->tags_addr());
  MarkSweep::adjust_pointer(cp->cache_addr());
  MarkSweep::adjust_pointer(cp->operands_addr());
  MarkSweep::adjust_pointer(cp->pool_holder_addr());
  return size;
}


int constantPoolKlass::oop_oop_iterate(oop obj, OopClosure* blk) {
  assert (obj->is_constantPool(), "obj must be constant pool");
  // Performance tweak: We skip iterating over the klass pointer since we
  // know that Universe::constantPoolKlassObj never moves.
  constantPoolOop cp = (constantPoolOop) obj;
  // Get size before changing pointers.
  // Don't call size() or oop_size() since that is a virtual call.
  int size = cp->object_size();

  // If the tags array is null we are in the middle of allocating this constant
  // pool.
  if (cp->tags() != NULL) {
    oop* base = (oop*)cp->base();
    for (int i = 0; i < cp->length(); i++) {
      if (cp->is_pointer_entry(i)) {
        blk->do_oop(base);
      }
      base++;
    }
  }
  blk->do_oop(cp->tags_addr());
  blk->do_oop(cp->cache_addr());
  blk->do_oop(cp->operands_addr());
  blk->do_oop(cp->pool_holder_addr());
  return size;
}


int constantPoolKlass::oop_oop_iterate_m(oop obj, OopClosure* blk, MemRegion mr) {
  assert (obj->is_constantPool(), "obj must be constant pool");
  // Performance tweak: We skip iterating over the klass pointer since we
  // know that Universe::constantPoolKlassObj never moves.
  constantPoolOop cp = (constantPoolOop) obj;
  // Get size before changing pointers.
  // Don't call size() or oop_size() since that is a virtual call.
  int size = cp->object_size();

  // If the tags array is null we are in the middle of allocating this constant
  // pool.
  if (cp->tags() != NULL) {
    oop* base = (oop*)cp->base();
    for (int i = 0; i < cp->length(); i++) {
      if (mr.contains(base)) {
        if (cp->is_pointer_entry(i)) {
          blk->do_oop(base);
        }
      }
      base++;
    }
  }
  oop* addr;
  addr = cp->tags_addr();
  if (mr.contains(addr)) blk->do_oop(addr);
  addr = cp->cache_addr();
  if (mr.contains(addr)) blk->do_oop(addr);
  addr = cp->operands_addr();
  if (mr.contains(addr)) blk->do_oop(addr);
  addr = cp->pool_holder_addr();
  if (mr.contains(addr)) blk->do_oop(addr);
  return size;
}

bool constantPoolKlass::oop_is_conc_safe(oop obj) const {
  assert(obj->is_constantPool(), "must be constantPool");
  return constantPoolOop(obj)->is_conc_safe();
}

#ifndef SERIALGC
int constantPoolKlass::oop_update_pointers(ParCompactionManager* cm, oop obj) {
  assert (obj->is_constantPool(), "obj must be constant pool");
  constantPoolOop cp = (constantPoolOop) obj;

  // If the tags array is null we are in the middle of allocating this constant
  // pool.
  if (cp->tags() != NULL) {
    oop* base = (oop*)cp->base();
    for (int i = 0; i < cp->length(); ++i, ++base) {
      if (cp->is_pointer_entry(i)) {
        PSParallelCompact::adjust_pointer(base);
      }
    }
  }
  PSParallelCompact::adjust_pointer(cp->tags_addr());
  PSParallelCompact::adjust_pointer(cp->cache_addr());
  PSParallelCompact::adjust_pointer(cp->operands_addr());
  PSParallelCompact::adjust_pointer(cp->pool_holder_addr());
  return cp->object_size();
}

void constantPoolKlass::oop_push_contents(PSPromotionManager* pm, oop obj) {
  assert(obj->is_constantPool(), "should be constant pool");
  constantPoolOop cp = (constantPoolOop) obj;
  if (cp->tags() != NULL) {
    for (int i = 1; i < cp->length(); ++i) {
      if (cp->is_pointer_entry(i)) {
        oop* base = cp->obj_at_addr_raw(i);
        if (PSScavenge::should_scavenge(base)) {
          pm->claim_or_forward_depth(base);
        }
      }
    }
  }
}
#endif // SERIALGC

// Printing

void constantPoolKlass::oop_print_on(oop obj, outputStream* st) {
  EXCEPTION_MARK;
  oop anObj;
  assert(obj->is_constantPool(), "must be constantPool");
  Klass::oop_print_on(obj, st);
  constantPoolOop cp = constantPoolOop(obj);
  if (cp->flags() != 0) {
    st->print(" - flags: 0x%x", cp->flags());
    if (cp->has_pseudo_string()) st->print(" has_pseudo_string");
    if (cp->has_invokedynamic()) st->print(" has_invokedynamic");
    if (cp->has_preresolution()) st->print(" has_preresolution");
    st->cr();
  }
  if (cp->pool_holder() != NULL) {
    bool extra = (instanceKlass::cast(cp->pool_holder())->constants() != cp);
    st->print_cr(" - holder: " INTPTR_FORMAT "%s", cp->pool_holder(), (extra? " (extra)" : ""));
  }
  st->print_cr(" - cache: " INTPTR_FORMAT, cp->cache());
  for (int index = 1; index < cp->length(); index++) {      // Index 0 is unused
    st->print(" - %3d : ", index);
    cp->tag_at(index).print_on(st);
    st->print(" : ");
    switch (cp->tag_at(index).value()) {
      case JVM_CONSTANT_Class :
        { anObj = cp->klass_at(index, CATCH);
          anObj->print_value_on(st);
          st->print(" {0x%lx}", (address)anObj);
        }
        break;
      case JVM_CONSTANT_Fieldref :
      case JVM_CONSTANT_Methodref :
      case JVM_CONSTANT_InterfaceMethodref :
        st->print("klass_index=%d", cp->uncached_klass_ref_index_at(index));
        st->print(" name_and_type_index=%d", cp->uncached_name_and_type_ref_index_at(index));
        break;
      case JVM_CONSTANT_UnresolvedString :
      case JVM_CONSTANT_String :
        if (cp->is_pseudo_string_at(index)) {
          anObj = cp->pseudo_string_at(index);
        } else {
          anObj = cp->string_at(index, CATCH);
        }
        anObj->print_value_on(st);
        st->print(" {0x%lx}", (address)anObj);
        break;
      case JVM_CONSTANT_Object :
        anObj = cp->object_at(index);
        anObj->print_value_on(st);
        st->print(" {0x%lx}", (address)anObj);
        break;
      case JVM_CONSTANT_Integer :
        st->print("%d", cp->int_at(index));
        break;
      case JVM_CONSTANT_Float :
        st->print("%f", cp->float_at(index));
        break;
      case JVM_CONSTANT_Long :
        st->print_jlong(cp->long_at(index));
        index++;   // Skip entry following eigth-byte constant
        break;
      case JVM_CONSTANT_Double :
        st->print("%lf", cp->double_at(index));
        index++;   // Skip entry following eigth-byte constant
        break;
      case JVM_CONSTANT_NameAndType :
        st->print("name_index=%d", cp->name_ref_index_at(index));
        st->print(" signature_index=%d", cp->signature_ref_index_at(index));
        break;
      case JVM_CONSTANT_Utf8 :
        cp->symbol_at(index)->print_value_on(st);
        break;
      case JVM_CONSTANT_UnresolvedClass :               // fall-through
      case JVM_CONSTANT_UnresolvedClassInError: {
        // unresolved_klass_at requires lock or safe world.
        CPSlot entry = cp->slot_at(index);
        if (entry.is_oop()) {
          entry.get_oop()->print_value_on(st);
        } else {
          entry.get_symbol()->print_value_on(st);
        }
        }
        break;
      case JVM_CONSTANT_MethodHandle :
        st->print("ref_kind=%d", cp->method_handle_ref_kind_at(index));
        st->print(" ref_index=%d", cp->method_handle_index_at(index));
        break;
      case JVM_CONSTANT_MethodType :
        st->print("signature_index=%d", cp->method_type_index_at(index));
        break;
      case JVM_CONSTANT_InvokeDynamic :
        {
          st->print("bootstrap_method_index=%d", cp->invoke_dynamic_bootstrap_method_ref_index_at(index));
          st->print(" name_and_type_index=%d", cp->invoke_dynamic_name_and_type_ref_index_at(index));
          int argc = cp->invoke_dynamic_argument_count_at(index);
          if (argc > 0) {
            for (int arg_i = 0; arg_i < argc; arg_i++) {
              int arg = cp->invoke_dynamic_argument_index_at(index, arg_i);
              st->print((arg_i == 0 ? " arguments={%d" : ", %d"), arg);
            }
            st->print("}");
          }
        }
        break;
      default:
        ShouldNotReachHere();
        break;
    }
    st->cr();
  }
  st->cr();
}

void constantPoolKlass::oop_print_value_on(oop obj, outputStream* st) {
  assert(obj->is_constantPool(), "must be constantPool");
  constantPoolOop cp = constantPoolOop(obj);
  st->print("constant pool [%d]", cp->length());
  if (cp->has_pseudo_string()) st->print("/pseudo_string");
  if (cp->has_invokedynamic()) st->print("/invokedynamic");
  if (cp->has_preresolution()) st->print("/preresolution");
  if (cp->operands() != NULL)  st->print("/operands[%d]", cp->operands()->length());
  cp->print_address_on(st);
  st->print(" for ");
  cp->pool_holder()->print_value_on(st);
  if (cp->pool_holder() != NULL) {
    bool extra = (instanceKlass::cast(cp->pool_holder())->constants() != cp);
    if (extra)  st->print(" (extra)");
  }
  if (cp->cache() != NULL) {
    st->print(" cache=" PTR_FORMAT, cp->cache());
  }
}

const char* constantPoolKlass::internal_name() const {
  return "{constant pool}";
}

// Verification

void constantPoolKlass::oop_verify_on(oop obj, outputStream* st) {
  Klass::oop_verify_on(obj, st);
  guarantee(obj->is_constantPool(), "object must be constant pool");
  constantPoolOop cp = constantPoolOop(obj);
  guarantee(cp->is_perm(), "should be in permspace");
  if (!cp->partially_loaded()) {
    for (int i = 0; i< cp->length();  i++) {
      constantTag tag = cp->tag_at(i);
      CPSlot entry = cp->slot_at(i);
      if (tag.is_klass()) {
        if (entry.is_oop()) {
          guarantee(entry.get_oop()->is_perm(),     "should be in permspace");
          guarantee(entry.get_oop()->is_klass(),    "should be klass");
        }
      } else if (tag.is_unresolved_klass()) {
        if (entry.is_oop()) {
          guarantee(entry.get_oop()->is_perm(),     "should be in permspace");
          guarantee(entry.get_oop()->is_klass(),    "should be klass");
        }
      } else if (tag.is_symbol()) {
        guarantee(entry.get_symbol()->refcount() != 0, "should have nonzero reference count");
      } else if (tag.is_unresolved_string()) {
        if (entry.is_oop()) {
          guarantee(entry.get_oop()->is_perm(),     "should be in permspace");
          guarantee(entry.get_oop()->is_instance(), "should be instance");
        }
        else {
          guarantee(entry.get_symbol()->refcount() != 0, "should have nonzero reference count");
        }
      } else if (tag.is_string()) {
        if (!cp->has_pseudo_string()) {
          if (entry.is_oop()) {
            guarantee(!JavaObjectsInPerm || entry.get_oop()->is_perm(),
                      "should be in permspace");
            guarantee(entry.get_oop()->is_instance(), "should be instance");
          }
        } else {
          // can be non-perm, can be non-instance (array)
        }
      } else if (tag.is_object()) {
        assert(entry.get_oop()->is_oop(), "should be some valid oop");
      } else {
        assert(!cp->is_pointer_entry(i), "unhandled oop type in constantPoolKlass::verify_on");
      }
    }
    guarantee(cp->tags()->is_perm(),         "should be in permspace");
    guarantee(cp->tags()->is_typeArray(),    "should be type array");
    if (cp->cache() != NULL) {
      // Note: cache() can be NULL before a class is completely setup or
      // in temporary constant pools used during constant pool merging
      guarantee(cp->cache()->is_perm(),              "should be in permspace");
      guarantee(cp->cache()->is_constantPoolCache(), "should be constant pool cache");
    }
    if (cp->operands() != NULL) {
      guarantee(cp->operands()->is_perm(),  "should be in permspace");
      guarantee(cp->operands()->is_typeArray(), "should be type array");
    }
    if (cp->pool_holder() != NULL) {
      // Note: pool_holder() can be NULL in temporary constant pools
      // used during constant pool merging
      guarantee(cp->pool_holder()->is_perm(),  "should be in permspace");
      guarantee(cp->pool_holder()->is_klass(), "should be klass");
    }
  }
}

bool constantPoolKlass::oop_partially_loaded(oop obj) const {
  assert(obj->is_constantPool(), "object must be constant pool");
  constantPoolOop cp = constantPoolOop(obj);
  return cp->tags() == NULL || cp->pool_holder() == (klassOop) cp;   // Check whether pool holder points to self
}


void constantPoolKlass::oop_set_partially_loaded(oop obj) {
  assert(obj->is_constantPool(), "object must be constant pool");
  constantPoolOop cp = constantPoolOop(obj);
  assert(cp->pool_holder() == NULL, "just checking");
  cp->set_pool_holder((klassOop) cp);   // Temporarily set pool holder to point to self
}

#ifndef PRODUCT
// CompileTheWorld support. Preload all classes loaded references in the passed in constantpool
void constantPoolKlass::preload_and_initialize_all_classes(oop obj, TRAPS) {
  guarantee(obj->is_constantPool(), "object must be constant pool");
  constantPoolHandle cp(THREAD, (constantPoolOop)obj);
  guarantee(!cp->partially_loaded(), "must be fully loaded");

  for (int i = 0; i< cp->length();  i++) {
    if (cp->tag_at(i).is_unresolved_klass()) {
      // This will force loading of the class
      klassOop klass = cp->klass_at(i, CHECK);
      if (klass->is_instance()) {
        // Force initialization of class
        instanceKlass::cast(klass)->initialize(CHECK);
      }
    }
  }
}

#endif
