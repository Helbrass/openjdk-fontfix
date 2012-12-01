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
#include "classfile/symbolTable.hpp"
#include "classfile/systemDictionary.hpp"
#include "classfile/vmSymbols.hpp"
#include "interpreter/linkResolver.hpp"
#include "memory/oopFactory.hpp"
#include "memory/universe.inline.hpp"
#include "oops/constantPoolOop.hpp"
#include "oops/instanceKlass.hpp"
#include "oops/objArrayKlass.hpp"
#include "oops/oop.inline.hpp"
#include "runtime/fieldType.hpp"
#include "runtime/init.hpp"
#include "runtime/signature.hpp"
#include "runtime/vframe.hpp"

void constantPoolOopDesc::set_flag_at(FlagBit fb) {
  const int MAX_STATE_CHANGES = 2;
  for (int i = MAX_STATE_CHANGES + 10; i > 0; i--) {
    int oflags = _flags;
    int nflags = oflags | (1 << (int)fb);
    if (Atomic::cmpxchg(nflags, &_flags, oflags) == oflags)
      return;
  }
  assert(false, "failed to cmpxchg flags");
  _flags |= (1 << (int)fb);     // better than nothing
}

klassOop constantPoolOopDesc::klass_at_impl(constantPoolHandle this_oop, int which, TRAPS) {
  // A resolved constantPool entry will contain a klassOop, otherwise a Symbol*.
  // It is not safe to rely on the tag bit's here, since we don't have a lock, and the entry and
  // tag is not updated atomicly.
  CPSlot entry = this_oop->slot_at(which);
  if (entry.is_oop()) {
    assert(entry.get_oop()->is_klass(), "must be");
    // Already resolved - return entry.
    return (klassOop)entry.get_oop();
  }

  // Acquire lock on constant oop while doing update. After we get the lock, we check if another object
  // already has updated the object
  assert(THREAD->is_Java_thread(), "must be a Java thread");
  bool do_resolve = false;
  bool in_error = false;

  Symbol* name = NULL;
  Handle       loader;
  { ObjectLocker ol(this_oop, THREAD);

    if (this_oop->tag_at(which).is_unresolved_klass()) {
      if (this_oop->tag_at(which).is_unresolved_klass_in_error()) {
        in_error = true;
      } else {
        do_resolve = true;
        name   = this_oop->unresolved_klass_at(which);
        loader = Handle(THREAD, instanceKlass::cast(this_oop->pool_holder())->class_loader());
      }
    }
  } // unlocking constantPool


  // The original attempt to resolve this constant pool entry failed so find the
  // original error and throw it again (JVMS 5.4.3).
  if (in_error) {
    Symbol* error = SystemDictionary::find_resolution_error(this_oop, which);
    guarantee(error != (Symbol*)NULL, "tag mismatch with resolution error table");
    ResourceMark rm;
    // exception text will be the class name
    const char* className = this_oop->unresolved_klass_at(which)->as_C_string();
    THROW_MSG_0(error, className);
  }

  if (do_resolve) {
    // this_oop must be unlocked during resolve_or_fail
    oop protection_domain = Klass::cast(this_oop->pool_holder())->protection_domain();
    Handle h_prot (THREAD, protection_domain);
    klassOop k_oop = SystemDictionary::resolve_or_fail(name, loader, h_prot, true, THREAD);
    KlassHandle k;
    if (!HAS_PENDING_EXCEPTION) {
      k = KlassHandle(THREAD, k_oop);
      // Do access check for klasses
      verify_constant_pool_resolve(this_oop, k, THREAD);
    }

    // Failed to resolve class. We must record the errors so that subsequent attempts
    // to resolve this constant pool entry fail with the same error (JVMS 5.4.3).
    if (HAS_PENDING_EXCEPTION) {
      ResourceMark rm;
      Symbol* error = PENDING_EXCEPTION->klass()->klass_part()->name();

      bool throw_orig_error = false;
      {
        ObjectLocker ol (this_oop, THREAD);

        // some other thread has beaten us and has resolved the class.
        if (this_oop->tag_at(which).is_klass()) {
          CLEAR_PENDING_EXCEPTION;
          entry = this_oop->resolved_klass_at(which);
          return (klassOop)entry.get_oop();
        }

        if (!PENDING_EXCEPTION->
              is_a(SystemDictionary::LinkageError_klass())) {
          // Just throw the exception and don't prevent these classes from
          // being loaded due to virtual machine errors like StackOverflow
          // and OutOfMemoryError, etc, or if the thread was hit by stop()
          // Needs clarification to section 5.4.3 of the VM spec (see 6308271)
        }
        else if (!this_oop->tag_at(which).is_unresolved_klass_in_error()) {
          SystemDictionary::add_resolution_error(this_oop, which, error);
          this_oop->tag_at_put(which, JVM_CONSTANT_UnresolvedClassInError);
        } else {
          // some other thread has put the class in error state.
          error = SystemDictionary::find_resolution_error(this_oop, which);
          assert(error != NULL, "checking");
          throw_orig_error = true;
        }
      } // unlocked

      if (throw_orig_error) {
        CLEAR_PENDING_EXCEPTION;
        ResourceMark rm;
        const char* className = this_oop->unresolved_klass_at(which)->as_C_string();
        THROW_MSG_0(error, className);
      }

      return 0;
    }

    if (TraceClassResolution && !k()->klass_part()->oop_is_array()) {
      // skip resolving the constant pool so that this code get's
      // called the next time some bytecodes refer to this class.
      ResourceMark rm;
      int line_number = -1;
      const char * source_file = NULL;
      if (JavaThread::current()->has_last_Java_frame()) {
        // try to identify the method which called this function.
        vframeStream vfst(JavaThread::current());
        if (!vfst.at_end()) {
          line_number = vfst.method()->line_number_from_bci(vfst.bci());
          Symbol* s = instanceKlass::cast(vfst.method()->method_holder())->source_file_name();
          if (s != NULL) {
            source_file = s->as_C_string();
          }
        }
      }
      if (k() != this_oop->pool_holder()) {
        // only print something if the classes are different
        if (source_file != NULL) {
          tty->print("RESOLVE %s %s %s:%d\n",
                     instanceKlass::cast(this_oop->pool_holder())->external_name(),
                     instanceKlass::cast(k())->external_name(), source_file, line_number);
        } else {
          tty->print("RESOLVE %s %s\n",
                     instanceKlass::cast(this_oop->pool_holder())->external_name(),
                     instanceKlass::cast(k())->external_name());
        }
      }
      return k();
    } else {
      ObjectLocker ol (this_oop, THREAD);
      // Only updated constant pool - if it is resolved.
      do_resolve = this_oop->tag_at(which).is_unresolved_klass();
      if (do_resolve) {
        this_oop->klass_at_put(which, k());
      }
    }
  }

  entry = this_oop->resolved_klass_at(which);
  assert(entry.is_oop() && entry.get_oop()->is_klass(), "must be resolved at this point");
  return (klassOop)entry.get_oop();
}


// Does not update constantPoolOop - to avoid any exception throwing. Used
// by compiler and exception handling.  Also used to avoid classloads for
// instanceof operations. Returns NULL if the class has not been loaded or
// if the verification of constant pool failed
klassOop constantPoolOopDesc::klass_at_if_loaded(constantPoolHandle this_oop, int which) {
  CPSlot entry = this_oop->slot_at(which);
  if (entry.is_oop()) {
    assert(entry.get_oop()->is_klass(), "must be");
    return (klassOop)entry.get_oop();
  } else {
    assert(entry.is_metadata(), "must be either symbol or klass");
    Thread *thread = Thread::current();
    Symbol* name = entry.get_symbol();
    oop loader = instanceKlass::cast(this_oop->pool_holder())->class_loader();
    oop protection_domain = Klass::cast(this_oop->pool_holder())->protection_domain();
    Handle h_prot (thread, protection_domain);
    Handle h_loader (thread, loader);
    klassOop k = SystemDictionary::find(name, h_loader, h_prot, thread);

    if (k != NULL) {
      // Make sure that resolving is legal
      EXCEPTION_MARK;
      KlassHandle klass(THREAD, k);
      // return NULL if verification fails
      verify_constant_pool_resolve(this_oop, klass, THREAD);
      if (HAS_PENDING_EXCEPTION) {
        CLEAR_PENDING_EXCEPTION;
        return NULL;
      }
      return klass();
    } else {
      return k;
    }
  }
}


klassOop constantPoolOopDesc::klass_ref_at_if_loaded(constantPoolHandle this_oop, int which) {
  return klass_at_if_loaded(this_oop, this_oop->klass_ref_index_at(which));
}


// This is an interface for the compiler that allows accessing non-resolved entries
// in the constant pool - but still performs the validations tests. Must be used
// in a pre-parse of the compiler - to determine what it can do and not do.
// Note: We cannot update the ConstantPool from the vm_thread.
klassOop constantPoolOopDesc::klass_ref_at_if_loaded_check(constantPoolHandle this_oop, int index, TRAPS) {
  int which = this_oop->klass_ref_index_at(index);
  CPSlot entry = this_oop->slot_at(which);
  if (entry.is_oop()) {
    assert(entry.get_oop()->is_klass(), "must be");
    return (klassOop)entry.get_oop();
  } else {
    assert(entry.is_metadata(), "must be either symbol or klass");
    Symbol*  name  = entry.get_symbol();
    oop loader = instanceKlass::cast(this_oop->pool_holder())->class_loader();
    oop protection_domain = Klass::cast(this_oop->pool_holder())->protection_domain();
    Handle h_loader(THREAD, loader);
    Handle h_prot  (THREAD, protection_domain);
    KlassHandle k(THREAD, SystemDictionary::find(name, h_loader, h_prot, THREAD));

    // Do access check for klasses
    if( k.not_null() ) verify_constant_pool_resolve(this_oop, k, CHECK_NULL);
    return k();
  }
}


methodOop constantPoolOopDesc::method_at_if_loaded(constantPoolHandle cpool,
                                                   int which, Bytecodes::Code invoke_code) {
  assert(!constantPoolCacheOopDesc::is_secondary_index(which), "no indy instruction here");
  if (cpool->cache() == NULL)  return NULL;  // nothing to load yet
  int cache_index = which - CPCACHE_INDEX_TAG;
  if (!(cache_index >= 0 && cache_index < cpool->cache()->length())) {
    if (PrintMiscellaneous && (Verbose||WizardMode)) {
      tty->print_cr("bad operand %d for %d in:", which, invoke_code); cpool->print();
    }
    return NULL;
  }
  ConstantPoolCacheEntry* e = cpool->cache()->entry_at(cache_index);
  if (invoke_code != Bytecodes::_illegal)
    return e->get_method_if_resolved(invoke_code, cpool);
  Bytecodes::Code bc;
  if ((bc = e->bytecode_1()) != (Bytecodes::Code)0)
    return e->get_method_if_resolved(bc, cpool);
  if ((bc = e->bytecode_2()) != (Bytecodes::Code)0)
    return e->get_method_if_resolved(bc, cpool);
  return NULL;
}


Symbol* constantPoolOopDesc::impl_name_ref_at(int which, bool uncached) {
  int name_index = name_ref_index_at(impl_name_and_type_ref_index_at(which, uncached));
  return symbol_at(name_index);
}


Symbol* constantPoolOopDesc::impl_signature_ref_at(int which, bool uncached) {
  int signature_index = signature_ref_index_at(impl_name_and_type_ref_index_at(which, uncached));
  return symbol_at(signature_index);
}


int constantPoolOopDesc::impl_name_and_type_ref_index_at(int which, bool uncached) {
  int i = which;
  if (!uncached && cache() != NULL) {
    if (constantPoolCacheOopDesc::is_secondary_index(which)) {
      // Invokedynamic index.
      int pool_index = cache()->main_entry_at(which)->constant_pool_index();
      pool_index = invoke_dynamic_name_and_type_ref_index_at(pool_index);
      assert(tag_at(pool_index).is_name_and_type(), "");
      return pool_index;
    }
    // change byte-ordering and go via cache
    i = remap_instruction_operand_from_cache(which);
  } else {
    if (tag_at(which).is_invoke_dynamic()) {
      int pool_index = invoke_dynamic_name_and_type_ref_index_at(which);
      assert(tag_at(pool_index).is_name_and_type(), "");
      return pool_index;
    }
  }
  assert(tag_at(i).is_field_or_method(), "Corrupted constant pool");
  assert(!tag_at(i).is_invoke_dynamic(), "Must be handled above");
  jint ref_index = *int_at_addr(i);
  return extract_high_short_from_int(ref_index);
}


int constantPoolOopDesc::impl_klass_ref_index_at(int which, bool uncached) {
  guarantee(!constantPoolCacheOopDesc::is_secondary_index(which),
            "an invokedynamic instruction does not have a klass");
  int i = which;
  if (!uncached && cache() != NULL) {
    // change byte-ordering and go via cache
    i = remap_instruction_operand_from_cache(which);
  }
  assert(tag_at(i).is_field_or_method(), "Corrupted constant pool");
  jint ref_index = *int_at_addr(i);
  return extract_low_short_from_int(ref_index);
}



int constantPoolOopDesc::remap_instruction_operand_from_cache(int operand) {
  int cpc_index = operand;
  DEBUG_ONLY(cpc_index -= CPCACHE_INDEX_TAG);
  assert((int)(u2)cpc_index == cpc_index, "clean u2");
  int member_index = cache()->entry_at(cpc_index)->constant_pool_index();
  return member_index;
}


void constantPoolOopDesc::verify_constant_pool_resolve(constantPoolHandle this_oop, KlassHandle k, TRAPS) {
 if (k->oop_is_instance() || k->oop_is_objArray()) {
    instanceKlassHandle holder (THREAD, this_oop->pool_holder());
    klassOop elem_oop = k->oop_is_instance() ? k() : objArrayKlass::cast(k())->bottom_klass();
    KlassHandle element (THREAD, elem_oop);

    // The element type could be a typeArray - we only need the access check if it is
    // an reference to another class
    if (element->oop_is_instance()) {
      LinkResolver::check_klass_accessability(holder, element, CHECK);
    }
  }
}


int constantPoolOopDesc::name_ref_index_at(int which_nt) {
  jint ref_index = name_and_type_at(which_nt);
  return extract_low_short_from_int(ref_index);
}


int constantPoolOopDesc::signature_ref_index_at(int which_nt) {
  jint ref_index = name_and_type_at(which_nt);
  return extract_high_short_from_int(ref_index);
}


klassOop constantPoolOopDesc::klass_ref_at(int which, TRAPS) {
  return klass_at(klass_ref_index_at(which), CHECK_NULL);
}


Symbol* constantPoolOopDesc::klass_name_at(int which) {
  assert(tag_at(which).is_unresolved_klass() || tag_at(which).is_klass(),
         "Corrupted constant pool");
  // A resolved constantPool entry will contain a klassOop, otherwise a Symbol*.
  // It is not safe to rely on the tag bit's here, since we don't have a lock, and the entry and
  // tag is not updated atomicly.
  CPSlot entry = slot_at(which);
  if (entry.is_oop()) {
    // Already resolved - return entry's name.
    assert(entry.get_oop()->is_klass(), "must be");
    return klassOop(entry.get_oop())->klass_part()->name();
  } else {
    assert(entry.is_metadata(), "must be either symbol or klass");
    return entry.get_symbol();
  }
}

Symbol* constantPoolOopDesc::klass_ref_at_noresolve(int which) {
  jint ref_index = klass_ref_index_at(which);
  return klass_at_noresolve(ref_index);
}

Symbol* constantPoolOopDesc::uncached_klass_ref_at_noresolve(int which) {
  jint ref_index = uncached_klass_ref_index_at(which);
  return klass_at_noresolve(ref_index);
}

char* constantPoolOopDesc::string_at_noresolve(int which) {
  // Test entry type in case string is resolved while in here.
  CPSlot entry = slot_at(which);
  if (entry.is_metadata()) {
    return (entry.get_symbol())->as_C_string();
  } else if (java_lang_String::is_instance(entry.get_oop())) {
    return java_lang_String::as_utf8_string(entry.get_oop());
  } else {
    return (char*)"<pseudo-string>";
  }
}


BasicType constantPoolOopDesc::basic_type_for_signature_at(int which) {
  return FieldType::basic_type(symbol_at(which));
}


void constantPoolOopDesc::resolve_string_constants_impl(constantPoolHandle this_oop, TRAPS) {
  for (int index = 1; index < this_oop->length(); index++) { // Index 0 is unused
    if (this_oop->tag_at(index).is_unresolved_string()) {
      this_oop->string_at(index, CHECK);
    }
  }
}

// A resolved constant value in the CP cache is represented as a non-null
// value.  As a special case, this value can be a 'systemObjArray'
// which masks an exception object to throw.
// This allows a MethodHandle constant reference to throw a consistent
// exception every time, if it fails to resolve.
static oop decode_exception_from_f1(oop result_oop, TRAPS) {
  if (result_oop->klass() != Universe::systemObjArrayKlassObj())
    return result_oop;

  // Special cases here:  Masked null, saved exception.
  objArrayOop sys_array = (objArrayOop) result_oop;
  assert(sys_array->length() == 1, "bad system array");
  if (sys_array->length() == 1) {
    THROW_OOP_(sys_array->obj_at(0), NULL);
  }
  return NULL;
}

oop constantPoolOopDesc::resolve_constant_at_impl(constantPoolHandle this_oop, int index, int cache_index, TRAPS) {
  oop result_oop = NULL;
  Handle throw_exception;

  if (cache_index == _possible_index_sentinel) {
    // It is possible that this constant is one which is cached in the CP cache.
    // We'll do a linear search.  This should be OK because this usage is rare.
    assert(index > 0, "valid index");
    constantPoolCacheOop cache = this_oop()->cache();
    for (int i = 0, len = cache->length(); i < len; i++) {
      ConstantPoolCacheEntry* cpc_entry = cache->entry_at(i);
      if (!cpc_entry->is_secondary_entry() && cpc_entry->constant_pool_index() == index) {
        // Switch the query to use this CPC entry.
        cache_index = i;
        index = _no_index_sentinel;
        break;
      }
    }
    if (cache_index == _possible_index_sentinel)
      cache_index = _no_index_sentinel;  // not found
  }
  assert(cache_index == _no_index_sentinel || cache_index >= 0, "");
  assert(index == _no_index_sentinel || index >= 0, "");

  if (cache_index >= 0) {
    assert(index == _no_index_sentinel, "only one kind of index at a time");
    ConstantPoolCacheEntry* cpc_entry = this_oop->cache()->entry_at(cache_index);
    result_oop = cpc_entry->f1();
    if (result_oop != NULL) {
      return decode_exception_from_f1(result_oop, THREAD);
      // That was easy...
    }
    index = cpc_entry->constant_pool_index();
  }

  jvalue prim_value;  // temp used only in a few cases below

  int tag_value = this_oop->tag_at(index).value();
  switch (tag_value) {

  case JVM_CONSTANT_UnresolvedClass:
  case JVM_CONSTANT_UnresolvedClassInError:
  case JVM_CONSTANT_Class:
    {
      klassOop resolved = klass_at_impl(this_oop, index, CHECK_NULL);
      // ldc wants the java mirror.
      result_oop = resolved->java_mirror();
      break;
    }

  case JVM_CONSTANT_String:
  case JVM_CONSTANT_UnresolvedString:
    if (this_oop->is_pseudo_string_at(index)) {
      result_oop = this_oop->pseudo_string_at(index);
      break;
    }
    result_oop = string_at_impl(this_oop, index, CHECK_NULL);
    break;

  case JVM_CONSTANT_Object:
    result_oop = this_oop->object_at(index);
    break;

  case JVM_CONSTANT_MethodHandle:
    {
      int ref_kind                 = this_oop->method_handle_ref_kind_at(index);
      int callee_index             = this_oop->method_handle_klass_index_at(index);
      Symbol*  name =      this_oop->method_handle_name_ref_at(index);
      Symbol*  signature = this_oop->method_handle_signature_ref_at(index);
      if (PrintMiscellaneous)
        tty->print_cr("resolve JVM_CONSTANT_MethodHandle:%d [%d/%d/%d] %s.%s",
                      ref_kind, index, this_oop->method_handle_index_at(index),
                      callee_index, name->as_C_string(), signature->as_C_string());
      KlassHandle callee;
      { klassOop k = klass_at_impl(this_oop, callee_index, CHECK_NULL);
        callee = KlassHandle(THREAD, k);
      }
      KlassHandle klass(THREAD, this_oop->pool_holder());
      Handle value = SystemDictionary::link_method_handle_constant(klass, ref_kind,
                                                                   callee, name, signature,
                                                                   THREAD);
      if (HAS_PENDING_EXCEPTION) {
        throw_exception = Handle(THREAD, PENDING_EXCEPTION);
        CLEAR_PENDING_EXCEPTION;
        break;
      }
      result_oop = value();
      assert(result_oop != NULL, "");
      break;
    }

  case JVM_CONSTANT_MethodType:
    {
      Symbol*  signature = this_oop->method_type_signature_at(index);
      if (PrintMiscellaneous)
        tty->print_cr("resolve JVM_CONSTANT_MethodType [%d/%d] %s",
                      index, this_oop->method_type_index_at(index),
                      signature->as_C_string());
      KlassHandle klass(THREAD, this_oop->pool_holder());
      bool ignore_is_on_bcp = false;
      Handle value = SystemDictionary::find_method_handle_type(signature,
                                                               klass,
                                                               false,
                                                               ignore_is_on_bcp,
                                                               THREAD);
      if (HAS_PENDING_EXCEPTION) {
        throw_exception = Handle(THREAD, PENDING_EXCEPTION);
        CLEAR_PENDING_EXCEPTION;
        break;
      }
      result_oop = value();
      assert(result_oop != NULL, "");
      break;
    }

  case JVM_CONSTANT_Integer:
    prim_value.i = this_oop->int_at(index);
    result_oop = java_lang_boxing_object::create(T_INT, &prim_value, CHECK_NULL);
    break;

  case JVM_CONSTANT_Float:
    prim_value.f = this_oop->float_at(index);
    result_oop = java_lang_boxing_object::create(T_FLOAT, &prim_value, CHECK_NULL);
    break;

  case JVM_CONSTANT_Long:
    prim_value.j = this_oop->long_at(index);
    result_oop = java_lang_boxing_object::create(T_LONG, &prim_value, CHECK_NULL);
    break;

  case JVM_CONSTANT_Double:
    prim_value.d = this_oop->double_at(index);
    result_oop = java_lang_boxing_object::create(T_DOUBLE, &prim_value, CHECK_NULL);
    break;

  default:
    DEBUG_ONLY( tty->print_cr("*** %p: tag at CP[%d/%d] = %d",
                              this_oop(), index, cache_index, tag_value) );
    assert(false, "unexpected constant tag");
    break;
  }

  if (cache_index >= 0) {
    // Cache the oop here also.
    if (throw_exception.not_null()) {
      objArrayOop sys_array = oopFactory::new_system_objArray(1, CHECK_NULL);
      sys_array->obj_at_put(0, throw_exception());
      result_oop = sys_array;
      throw_exception = Handle();  // be tidy
    }
    Handle result_handle(THREAD, result_oop);
    result_oop = NULL;  // safety
    ObjectLocker ol(this_oop, THREAD);
    ConstantPoolCacheEntry* cpc_entry = this_oop->cache()->entry_at(cache_index);
    result_oop = cpc_entry->f1();
    // Benign race condition:  f1 may already be filled in while we were trying to lock.
    // The important thing here is that all threads pick up the same result.
    // It doesn't matter which racing thread wins, as long as only one
    // result is used by all threads, and all future queries.
    // That result may be either a resolved constant or a failure exception.
    if (result_oop == NULL) {
      result_oop = result_handle();
      cpc_entry->set_f1(result_oop);
    }
    return decode_exception_from_f1(result_oop, THREAD);
  } else {
    if (throw_exception.not_null()) {
      THROW_HANDLE_(throw_exception, NULL);
    }
    return result_oop;
  }
}

oop constantPoolOopDesc::string_at_impl(constantPoolHandle this_oop, int which, TRAPS) {
  oop str = NULL;
  CPSlot entry = this_oop->slot_at(which);
  if (entry.is_metadata()) {
    ObjectLocker ol(this_oop, THREAD);
    if (this_oop->tag_at(which).is_unresolved_string()) {
      // Intern string
      Symbol* sym = this_oop->unresolved_string_at(which);
      str = StringTable::intern(sym, CHECK_(constantPoolOop(NULL)));
      this_oop->string_at_put(which, str);
    } else {
      // Another thread beat us and interned string, read string from constant pool
      str = this_oop->resolved_string_at(which);
    }
  } else {
    str = entry.get_oop();
  }
  assert(java_lang_String::is_instance(str), "must be string");
  return str;
}


bool constantPoolOopDesc::is_pseudo_string_at(int which) {
  CPSlot entry = slot_at(which);
  if (entry.is_metadata())
    // Not yet resolved, but it will resolve to a string.
    return false;
  else if (java_lang_String::is_instance(entry.get_oop()))
    return false; // actually, it might be a non-interned or non-perm string
  else
    // truly pseudo
    return true;
}


bool constantPoolOopDesc::klass_name_at_matches(instanceKlassHandle k,
                                                int which) {
  // Names are interned, so we can compare Symbol*s directly
  Symbol* cp_name = klass_name_at(which);
  return (cp_name == k->name());
}


int constantPoolOopDesc::pre_resolve_shared_klasses(TRAPS) {
  ResourceMark rm;
  int count = 0;
  for (int index = 1; index < tags()->length(); index++) { // Index 0 is unused
    if (tag_at(index).is_unresolved_string()) {
      // Intern string
      Symbol* sym = unresolved_string_at(index);
      oop entry = StringTable::intern(sym, CHECK_(-1));
      string_at_put(index, entry);
    }
  }
  return count;
}

// Iterate over symbols and decrement ones which are Symbol*s.
// This is done during GC so do not need to lock constantPool unless we
// have per-thread safepoints.
// Only decrement the UTF8 symbols. Unresolved classes and strings point to
// these symbols but didn't increment the reference count.
void constantPoolOopDesc::unreference_symbols() {
  for (int index = 1; index < length(); index++) { // Index 0 is unused
    constantTag tag = tag_at(index);
    if (tag.is_symbol()) {
      symbol_at(index)->decrement_refcount();
    }
  }
}

// Iterate over symbols which are used as class, field, method names and
// signatures (in preparation for writing to the shared archive).

void constantPoolOopDesc::shared_symbols_iterate(SymbolClosure* closure) {
  for (int index = 1; index < length(); index++) { // Index 0 is unused
    switch (tag_at(index).value()) {

    case JVM_CONSTANT_UnresolvedClass:
    case JVM_CONSTANT_UnresolvedString:
    case JVM_CONSTANT_Utf8:
      assert(slot_at(index).is_metadata(), "must be symbol");
      closure->do_symbol(symbol_at_addr(index));
      break;

    case JVM_CONSTANT_NameAndType:
      {
        int i = *int_at_addr(index);
        closure->do_symbol(symbol_at_addr((unsigned)i >> 16));
        closure->do_symbol(symbol_at_addr((unsigned)i & 0xffff));
      }
      break;

    case JVM_CONSTANT_Class:
    case JVM_CONSTANT_InterfaceMethodref:
    case JVM_CONSTANT_Fieldref:
    case JVM_CONSTANT_Methodref:
    case JVM_CONSTANT_Integer:
    case JVM_CONSTANT_Float:
      // Do nothing!  Not an oop.
      // These constant types do not reference symbols at this point.
      break;

    case JVM_CONSTANT_String:
      // Do nothing!  Not a symbol.
      break;

    case JVM_CONSTANT_Long:
    case JVM_CONSTANT_Double:
      // Do nothing!  Not an oop. (But takes two pool entries.)
      ++index;
      break;

    default:
      ShouldNotReachHere();
      break;
    }
  }
}


// Iterate over the [one] tags array (in preparation for writing to the
// shared archive).

void constantPoolOopDesc::shared_tags_iterate(OopClosure* closure) {
  closure->do_oop(tags_addr());
  closure->do_oop(operands_addr());
}


// Iterate over String objects (in preparation for writing to the shared
// archive).

void constantPoolOopDesc::shared_strings_iterate(OopClosure* closure) {
  for (int index = 1; index < length(); index++) { // Index 0 is unused
    switch (tag_at(index).value()) {

    case JVM_CONSTANT_UnresolvedClass:
    case JVM_CONSTANT_NameAndType:
      // Do nothing!  Not a String.
      break;

    case JVM_CONSTANT_Class:
    case JVM_CONSTANT_InterfaceMethodref:
    case JVM_CONSTANT_Fieldref:
    case JVM_CONSTANT_Methodref:
    case JVM_CONSTANT_Integer:
    case JVM_CONSTANT_Float:
      // Do nothing!  Not an oop.
      // These constant types do not reference symbols at this point.
      break;

    case JVM_CONSTANT_String:
      closure->do_oop(obj_at_addr_raw(index));
      break;

    case JVM_CONSTANT_UnresolvedString:
    case JVM_CONSTANT_Utf8:
      // These constants are symbols, but unless these symbols are
      // actually to be used for something, we don't want to mark them.
      break;

    case JVM_CONSTANT_Long:
    case JVM_CONSTANT_Double:
      // Do nothing!  Not an oop. (But takes two pool entries.)
      ++index;
      break;

    default:
      ShouldNotReachHere();
      break;
    }
  }
}


// Compare this constant pool's entry at index1 to the constant pool
// cp2's entry at index2.
bool constantPoolOopDesc::compare_entry_to(int index1, constantPoolHandle cp2,
       int index2, TRAPS) {

  jbyte t1 = tag_at(index1).value();
  jbyte t2 = cp2->tag_at(index2).value();


  // JVM_CONSTANT_UnresolvedClassInError is equal to JVM_CONSTANT_UnresolvedClass
  // when comparing
  if (t1 == JVM_CONSTANT_UnresolvedClassInError) {
    t1 = JVM_CONSTANT_UnresolvedClass;
  }
  if (t2 == JVM_CONSTANT_UnresolvedClassInError) {
    t2 = JVM_CONSTANT_UnresolvedClass;
  }

  if (t1 != t2) {
    // Not the same entry type so there is nothing else to check. Note
    // that this style of checking will consider resolved/unresolved
    // class pairs and resolved/unresolved string pairs as different.
    // From the constantPoolOop API point of view, this is correct
    // behavior. See constantPoolKlass::merge() to see how this plays
    // out in the context of constantPoolOop merging.
    return false;
  }

  switch (t1) {
  case JVM_CONSTANT_Class:
  {
    klassOop k1 = klass_at(index1, CHECK_false);
    klassOop k2 = cp2->klass_at(index2, CHECK_false);
    if (k1 == k2) {
      return true;
    }
  } break;

  case JVM_CONSTANT_ClassIndex:
  {
    int recur1 = klass_index_at(index1);
    int recur2 = cp2->klass_index_at(index2);
    bool match = compare_entry_to(recur1, cp2, recur2, CHECK_false);
    if (match) {
      return true;
    }
  } break;

  case JVM_CONSTANT_Double:
  {
    jdouble d1 = double_at(index1);
    jdouble d2 = cp2->double_at(index2);
    if (d1 == d2) {
      return true;
    }
  } break;

  case JVM_CONSTANT_Fieldref:
  case JVM_CONSTANT_InterfaceMethodref:
  case JVM_CONSTANT_Methodref:
  {
    int recur1 = uncached_klass_ref_index_at(index1);
    int recur2 = cp2->uncached_klass_ref_index_at(index2);
    bool match = compare_entry_to(recur1, cp2, recur2, CHECK_false);
    if (match) {
      recur1 = uncached_name_and_type_ref_index_at(index1);
      recur2 = cp2->uncached_name_and_type_ref_index_at(index2);
      match = compare_entry_to(recur1, cp2, recur2, CHECK_false);
      if (match) {
        return true;
      }
    }
  } break;

  case JVM_CONSTANT_Float:
  {
    jfloat f1 = float_at(index1);
    jfloat f2 = cp2->float_at(index2);
    if (f1 == f2) {
      return true;
    }
  } break;

  case JVM_CONSTANT_Integer:
  {
    jint i1 = int_at(index1);
    jint i2 = cp2->int_at(index2);
    if (i1 == i2) {
      return true;
    }
  } break;

  case JVM_CONSTANT_Long:
  {
    jlong l1 = long_at(index1);
    jlong l2 = cp2->long_at(index2);
    if (l1 == l2) {
      return true;
    }
  } break;

  case JVM_CONSTANT_NameAndType:
  {
    int recur1 = name_ref_index_at(index1);
    int recur2 = cp2->name_ref_index_at(index2);
    bool match = compare_entry_to(recur1, cp2, recur2, CHECK_false);
    if (match) {
      recur1 = signature_ref_index_at(index1);
      recur2 = cp2->signature_ref_index_at(index2);
      match = compare_entry_to(recur1, cp2, recur2, CHECK_false);
      if (match) {
        return true;
      }
    }
  } break;

  case JVM_CONSTANT_String:
  {
    oop s1 = string_at(index1, CHECK_false);
    oop s2 = cp2->string_at(index2, CHECK_false);
    if (s1 == s2) {
      return true;
    }
  } break;

  case JVM_CONSTANT_StringIndex:
  {
    int recur1 = string_index_at(index1);
    int recur2 = cp2->string_index_at(index2);
    bool match = compare_entry_to(recur1, cp2, recur2, CHECK_false);
    if (match) {
      return true;
    }
  } break;

  case JVM_CONSTANT_UnresolvedClass:
  {
    Symbol* k1 = unresolved_klass_at(index1);
    Symbol* k2 = cp2->unresolved_klass_at(index2);
    if (k1 == k2) {
      return true;
    }
  } break;

  case JVM_CONSTANT_MethodType:
  {
    int k1 = method_type_index_at(index1);
    int k2 = cp2->method_type_index_at(index2);
    bool match = compare_entry_to(k1, cp2, k2, CHECK_false);
    if (match) {
      return true;
    }
  } break;

  case JVM_CONSTANT_MethodHandle:
  {
    int k1 = method_handle_ref_kind_at(index1);
    int k2 = cp2->method_handle_ref_kind_at(index2);
    if (k1 == k2) {
      int i1 = method_handle_index_at(index1);
      int i2 = cp2->method_handle_index_at(index2);
      bool match = compare_entry_to(i1, cp2, i2, CHECK_false);
      if (match) {
        return true;
      }
    }
  } break;

  case JVM_CONSTANT_InvokeDynamic:
  {
    int k1 = invoke_dynamic_bootstrap_method_ref_index_at(index1);
    int k2 = cp2->invoke_dynamic_bootstrap_method_ref_index_at(index2);
    bool match = compare_entry_to(k1, cp2, k2, CHECK_false);
    if (!match)  return false;
    k1 = invoke_dynamic_name_and_type_ref_index_at(index1);
    k2 = cp2->invoke_dynamic_name_and_type_ref_index_at(index2);
    match = compare_entry_to(k1, cp2, k2, CHECK_false);
    if (!match)  return false;
    int argc = invoke_dynamic_argument_count_at(index1);
    if (argc == cp2->invoke_dynamic_argument_count_at(index2)) {
      for (int j = 0; j < argc; j++) {
        k1 = invoke_dynamic_argument_index_at(index1, j);
        k2 = cp2->invoke_dynamic_argument_index_at(index2, j);
        match = compare_entry_to(k1, cp2, k2, CHECK_false);
        if (!match)  return false;
      }
      return true;           // got through loop; all elements equal
    }
  } break;

  case JVM_CONSTANT_UnresolvedString:
  {
    Symbol* s1 = unresolved_string_at(index1);
    Symbol* s2 = cp2->unresolved_string_at(index2);
    if (s1 == s2) {
      return true;
    }
  } break;

  case JVM_CONSTANT_Utf8:
  {
    Symbol* s1 = symbol_at(index1);
    Symbol* s2 = cp2->symbol_at(index2);
    if (s1 == s2) {
      return true;
    }
  } break;

  // Invalid is used as the tag for the second constant pool entry
  // occupied by JVM_CONSTANT_Double or JVM_CONSTANT_Long. It should
  // not be seen by itself.
  case JVM_CONSTANT_Invalid: // fall through

  default:
    ShouldNotReachHere();
    break;
  }

  return false;
} // end compare_entry_to()


// Copy this constant pool's entries at start_i to end_i (inclusive)
// to the constant pool to_cp's entries starting at to_i. A total of
// (end_i - start_i) + 1 entries are copied.
void constantPoolOopDesc::copy_cp_to_impl(constantPoolHandle from_cp, int start_i, int end_i,
       constantPoolHandle to_cp, int to_i, TRAPS) {

  int dest_i = to_i;  // leave original alone for debug purposes

  for (int src_i = start_i; src_i <= end_i; /* see loop bottom */ ) {
    copy_entry_to(from_cp, src_i, to_cp, dest_i, CHECK);

    switch (from_cp->tag_at(src_i).value()) {
    case JVM_CONSTANT_Double:
    case JVM_CONSTANT_Long:
      // double and long take two constant pool entries
      src_i += 2;
      dest_i += 2;
      break;

    default:
      // all others take one constant pool entry
      src_i++;
      dest_i++;
      break;
    }
  }

  int from_oplen = operand_array_length(from_cp->operands());
  int old_oplen  = operand_array_length(to_cp->operands());
  if (from_oplen != 0) {
    // append my operands to the target's operands array
    if (old_oplen == 0) {
      to_cp->set_operands(from_cp->operands());  // reuse; do not merge
    } else {
      int old_len  = to_cp->operands()->length();
      int from_len = from_cp->operands()->length();
      int old_off  = old_oplen * sizeof(u2);
      int from_off = from_oplen * sizeof(u2);
      typeArrayHandle new_operands = oopFactory::new_permanent_shortArray(old_len + from_len, CHECK);
      int fillp = 0, len = 0;
      // first part of dest
      Copy::conjoint_memory_atomic(to_cp->operands()->short_at_addr(0),
                                   new_operands->short_at_addr(fillp),
                                   (len = old_off) * sizeof(u2));
      fillp += len;
      // first part of src
      Copy::conjoint_memory_atomic(to_cp->operands()->short_at_addr(0),
                                   new_operands->short_at_addr(fillp),
                                   (len = from_off) * sizeof(u2));
      fillp += len;
      // second part of dest
      Copy::conjoint_memory_atomic(to_cp->operands()->short_at_addr(old_off),
                                   new_operands->short_at_addr(fillp),
                                   (len = old_len - old_off) * sizeof(u2));
      fillp += len;
      // second part of src
      Copy::conjoint_memory_atomic(to_cp->operands()->short_at_addr(from_off),
                                   new_operands->short_at_addr(fillp),
                                   (len = from_len - from_off) * sizeof(u2));
      fillp += len;
      assert(fillp == new_operands->length(), "");

      // Adjust indexes in the first part of the copied operands array.
      for (int j = 0; j < from_oplen; j++) {
        int offset = operand_offset_at(new_operands(), old_oplen + j);
        assert(offset == operand_offset_at(from_cp->operands(), j), "correct copy");
        offset += old_len;  // every new tuple is preceded by old_len extra u2's
        operand_offset_at_put(new_operands(), old_oplen + j, offset);
      }

      // replace target operands array with combined array
      to_cp->set_operands(new_operands());
    }
  }

} // end copy_cp_to()


// Copy this constant pool's entry at from_i to the constant pool
// to_cp's entry at to_i.
void constantPoolOopDesc::copy_entry_to(constantPoolHandle from_cp, int from_i,
                                        constantPoolHandle to_cp, int to_i,
                                        TRAPS) {

  int tag = from_cp->tag_at(from_i).value();
  switch (tag) {
  case JVM_CONSTANT_Class:
  {
    klassOop k = from_cp->klass_at(from_i, CHECK);
    to_cp->klass_at_put(to_i, k);
  } break;

  case JVM_CONSTANT_ClassIndex:
  {
    jint ki = from_cp->klass_index_at(from_i);
    to_cp->klass_index_at_put(to_i, ki);
  } break;

  case JVM_CONSTANT_Double:
  {
    jdouble d = from_cp->double_at(from_i);
    to_cp->double_at_put(to_i, d);
    // double takes two constant pool entries so init second entry's tag
    to_cp->tag_at_put(to_i + 1, JVM_CONSTANT_Invalid);
  } break;

  case JVM_CONSTANT_Fieldref:
  {
    int class_index = from_cp->uncached_klass_ref_index_at(from_i);
    int name_and_type_index = from_cp->uncached_name_and_type_ref_index_at(from_i);
    to_cp->field_at_put(to_i, class_index, name_and_type_index);
  } break;

  case JVM_CONSTANT_Float:
  {
    jfloat f = from_cp->float_at(from_i);
    to_cp->float_at_put(to_i, f);
  } break;

  case JVM_CONSTANT_Integer:
  {
    jint i = from_cp->int_at(from_i);
    to_cp->int_at_put(to_i, i);
  } break;

  case JVM_CONSTANT_InterfaceMethodref:
  {
    int class_index = from_cp->uncached_klass_ref_index_at(from_i);
    int name_and_type_index = from_cp->uncached_name_and_type_ref_index_at(from_i);
    to_cp->interface_method_at_put(to_i, class_index, name_and_type_index);
  } break;

  case JVM_CONSTANT_Long:
  {
    jlong l = from_cp->long_at(from_i);
    to_cp->long_at_put(to_i, l);
    // long takes two constant pool entries so init second entry's tag
    to_cp->tag_at_put(to_i + 1, JVM_CONSTANT_Invalid);
  } break;

  case JVM_CONSTANT_Methodref:
  {
    int class_index = from_cp->uncached_klass_ref_index_at(from_i);
    int name_and_type_index = from_cp->uncached_name_and_type_ref_index_at(from_i);
    to_cp->method_at_put(to_i, class_index, name_and_type_index);
  } break;

  case JVM_CONSTANT_NameAndType:
  {
    int name_ref_index = from_cp->name_ref_index_at(from_i);
    int signature_ref_index = from_cp->signature_ref_index_at(from_i);
    to_cp->name_and_type_at_put(to_i, name_ref_index, signature_ref_index);
  } break;

  case JVM_CONSTANT_String:
  {
    oop s = from_cp->string_at(from_i, CHECK);
    to_cp->string_at_put(to_i, s);
  } break;

  case JVM_CONSTANT_StringIndex:
  {
    jint si = from_cp->string_index_at(from_i);
    to_cp->string_index_at_put(to_i, si);
  } break;

  case JVM_CONSTANT_UnresolvedClass:
  {
    // Can be resolved after checking tag, so check the slot first.
    CPSlot entry = from_cp->slot_at(from_i);
    if (entry.is_oop()) {
      assert(entry.get_oop()->is_klass(), "must be");
      // Already resolved
      to_cp->klass_at_put(to_i, (klassOop)entry.get_oop());
    } else {
      to_cp->unresolved_klass_at_put(to_i, entry.get_symbol());
    }
  } break;

  case JVM_CONSTANT_UnresolvedClassInError:
  {
    Symbol* k = from_cp->unresolved_klass_at(from_i);
    to_cp->unresolved_klass_at_put(to_i, k);
    to_cp->tag_at_put(to_i, JVM_CONSTANT_UnresolvedClassInError);
  } break;


  case JVM_CONSTANT_UnresolvedString:
  {
    // Can be resolved after checking tag, so check the slot first.
    CPSlot entry = from_cp->slot_at(from_i);
    if (entry.is_oop()) {
      // Already resolved (either string or pseudo-string)
      to_cp->string_at_put(to_i, entry.get_oop());
    } else {
      to_cp->unresolved_string_at_put(to_i, entry.get_symbol());
    }
  } break;

  case JVM_CONSTANT_Utf8:
  {
    Symbol* s = from_cp->symbol_at(from_i);
    to_cp->symbol_at_put(to_i, s);
    // This constantPool has the same lifetime as the original, so don't
    // increase reference counts for the copy.
  } break;

  case JVM_CONSTANT_MethodType:
  {
    jint k = from_cp->method_type_index_at(from_i);
    to_cp->method_type_index_at_put(to_i, k);
  } break;

  case JVM_CONSTANT_MethodHandle:
  {
    int k1 = from_cp->method_handle_ref_kind_at(from_i);
    int k2 = from_cp->method_handle_index_at(from_i);
    to_cp->method_handle_index_at_put(to_i, k1, k2);
  } break;

  case JVM_CONSTANT_InvokeDynamic:
  {
    int k1 = from_cp->invoke_dynamic_bootstrap_specifier_index(from_i);
    int k2 = from_cp->invoke_dynamic_name_and_type_ref_index_at(from_i);
    k1 += operand_array_length(to_cp->operands());  // to_cp might already have operands
    to_cp->invoke_dynamic_at_put(to_i, k1, k2);
  } break;

  // Invalid is used as the tag for the second constant pool entry
  // occupied by JVM_CONSTANT_Double or JVM_CONSTANT_Long. It should
  // not be seen by itself.
  case JVM_CONSTANT_Invalid: // fall through

  default:
  {
    ShouldNotReachHere();
  } break;
  }
} // end copy_entry_to()


// Search constant pool search_cp for an entry that matches this
// constant pool's entry at pattern_i. Returns the index of a
// matching entry or zero (0) if there is no matching entry.
int constantPoolOopDesc::find_matching_entry(int pattern_i,
      constantPoolHandle search_cp, TRAPS) {

  // index zero (0) is not used
  for (int i = 1; i < search_cp->length(); i++) {
    bool found = compare_entry_to(pattern_i, search_cp, i, CHECK_0);
    if (found) {
      return i;
    }
  }

  return 0;  // entry not found; return unused index zero (0)
} // end find_matching_entry()


#ifndef PRODUCT

const char* constantPoolOopDesc::printable_name_at(int which) {

  constantTag tag = tag_at(which);

  if (tag.is_unresolved_string() || tag.is_string()) {
    return string_at_noresolve(which);
  } else if (tag.is_klass() || tag.is_unresolved_klass()) {
    return klass_name_at(which)->as_C_string();
  } else if (tag.is_symbol()) {
    return symbol_at(which)->as_C_string();
  }
  return "";
}

#endif // PRODUCT


// JVMTI GetConstantPool support

// For temporary use until code is stable.
#define DBG(code)

static const char* WARN_MSG = "Must not be such entry!";

static void print_cpool_bytes(jint cnt, u1 *bytes) {
  jint size = 0;
  u2   idx1, idx2;

  for (jint idx = 1; idx < cnt; idx++) {
    jint ent_size = 0;
    u1   tag  = *bytes++;
    size++;                       // count tag

    printf("const #%03d, tag: %02d ", idx, tag);
    switch(tag) {
      case JVM_CONSTANT_Invalid: {
        printf("Invalid");
        break;
      }
      case JVM_CONSTANT_Unicode: {
        printf("Unicode      %s", WARN_MSG);
        break;
      }
      case JVM_CONSTANT_Utf8: {
        u2 len = Bytes::get_Java_u2(bytes);
        char str[128];
        if (len > 127) {
           len = 127;
        }
        strncpy(str, (char *) (bytes+2), len);
        str[len] = '\0';
        printf("Utf8          \"%s\"", str);
        ent_size = 2 + len;
        break;
      }
      case JVM_CONSTANT_Integer: {
        u4 val = Bytes::get_Java_u4(bytes);
        printf("int          %d", *(int *) &val);
        ent_size = 4;
        break;
      }
      case JVM_CONSTANT_Float: {
        u4 val = Bytes::get_Java_u4(bytes);
        printf("float        %5.3ff", *(float *) &val);
        ent_size = 4;
        break;
      }
      case JVM_CONSTANT_Long: {
        u8 val = Bytes::get_Java_u8(bytes);
        printf("long         "INT64_FORMAT, *(jlong *) &val);
        ent_size = 8;
        idx++; // Long takes two cpool slots
        break;
      }
      case JVM_CONSTANT_Double: {
        u8 val = Bytes::get_Java_u8(bytes);
        printf("double       %5.3fd", *(jdouble *)&val);
        ent_size = 8;
        idx++; // Double takes two cpool slots
        break;
      }
      case JVM_CONSTANT_Class: {
        idx1 = Bytes::get_Java_u2(bytes);
        printf("class        #%03d", idx1);
        ent_size = 2;
        break;
      }
      case JVM_CONSTANT_String: {
        idx1 = Bytes::get_Java_u2(bytes);
        printf("String       #%03d", idx1);
        ent_size = 2;
        break;
      }
      case JVM_CONSTANT_Fieldref: {
        idx1 = Bytes::get_Java_u2(bytes);
        idx2 = Bytes::get_Java_u2(bytes+2);
        printf("Field        #%03d, #%03d", (int) idx1, (int) idx2);
        ent_size = 4;
        break;
      }
      case JVM_CONSTANT_Methodref: {
        idx1 = Bytes::get_Java_u2(bytes);
        idx2 = Bytes::get_Java_u2(bytes+2);
        printf("Method       #%03d, #%03d", idx1, idx2);
        ent_size = 4;
        break;
      }
      case JVM_CONSTANT_InterfaceMethodref: {
        idx1 = Bytes::get_Java_u2(bytes);
        idx2 = Bytes::get_Java_u2(bytes+2);
        printf("InterfMethod #%03d, #%03d", idx1, idx2);
        ent_size = 4;
        break;
      }
      case JVM_CONSTANT_NameAndType: {
        idx1 = Bytes::get_Java_u2(bytes);
        idx2 = Bytes::get_Java_u2(bytes+2);
        printf("NameAndType  #%03d, #%03d", idx1, idx2);
        ent_size = 4;
        break;
      }
      case JVM_CONSTANT_ClassIndex: {
        printf("ClassIndex  %s", WARN_MSG);
        break;
      }
      case JVM_CONSTANT_UnresolvedClass: {
        printf("UnresolvedClass: %s", WARN_MSG);
        break;
      }
      case JVM_CONSTANT_UnresolvedClassInError: {
        printf("UnresolvedClassInErr: %s", WARN_MSG);
        break;
      }
      case JVM_CONSTANT_StringIndex: {
        printf("StringIndex: %s", WARN_MSG);
        break;
      }
      case JVM_CONSTANT_UnresolvedString: {
        printf("UnresolvedString: %s", WARN_MSG);
        break;
      }
    }
    printf(";\n");
    bytes += ent_size;
    size  += ent_size;
  }
  printf("Cpool size: %d\n", size);
  fflush(0);
  return;
} /* end print_cpool_bytes */


// Returns size of constant pool entry.
jint constantPoolOopDesc::cpool_entry_size(jint idx) {
  switch(tag_at(idx).value()) {
    case JVM_CONSTANT_Invalid:
    case JVM_CONSTANT_Unicode:
      return 1;

    case JVM_CONSTANT_Utf8:
      return 3 + symbol_at(idx)->utf8_length();

    case JVM_CONSTANT_Class:
    case JVM_CONSTANT_String:
    case JVM_CONSTANT_ClassIndex:
    case JVM_CONSTANT_UnresolvedClass:
    case JVM_CONSTANT_UnresolvedClassInError:
    case JVM_CONSTANT_StringIndex:
    case JVM_CONSTANT_UnresolvedString:
    case JVM_CONSTANT_MethodType:
      return 3;

    case JVM_CONSTANT_MethodHandle:
      return 4; //tag, ref_kind, ref_index

    case JVM_CONSTANT_Integer:
    case JVM_CONSTANT_Float:
    case JVM_CONSTANT_Fieldref:
    case JVM_CONSTANT_Methodref:
    case JVM_CONSTANT_InterfaceMethodref:
    case JVM_CONSTANT_NameAndType:
      return 5;

    case JVM_CONSTANT_InvokeDynamic:
      // u1 tag, u2 bsm, u2 nt
      return 5;

    case JVM_CONSTANT_Long:
    case JVM_CONSTANT_Double:
      return 9;
  }
  assert(false, "cpool_entry_size: Invalid constant pool entry tag");
  return 1;
} /* end cpool_entry_size */


// SymbolHashMap is used to find a constant pool index from a string.
// This function fills in SymbolHashMaps, one for utf8s and one for
// class names, returns size of the cpool raw bytes.
jint constantPoolOopDesc::hash_entries_to(SymbolHashMap *symmap,
                                          SymbolHashMap *classmap) {
  jint size = 0;

  for (u2 idx = 1; idx < length(); idx++) {
    u2 tag = tag_at(idx).value();
    size += cpool_entry_size(idx);

    switch(tag) {
      case JVM_CONSTANT_Utf8: {
        Symbol* sym = symbol_at(idx);
        symmap->add_entry(sym, idx);
        DBG(printf("adding symbol entry %s = %d\n", sym->as_utf8(), idx));
        break;
      }
      case JVM_CONSTANT_Class:
      case JVM_CONSTANT_UnresolvedClass:
      case JVM_CONSTANT_UnresolvedClassInError: {
        Symbol* sym = klass_name_at(idx);
        classmap->add_entry(sym, idx);
        DBG(printf("adding class entry %s = %d\n", sym->as_utf8(), idx));
        break;
      }
      case JVM_CONSTANT_Long:
      case JVM_CONSTANT_Double: {
        idx++; // Both Long and Double take two cpool slots
        break;
      }
    }
  }
  return size;
} /* end hash_utf8_entries_to */


// Copy cpool bytes.
// Returns:
//    0, in case of OutOfMemoryError
//   -1, in case of internal error
//  > 0, count of the raw cpool bytes that have been copied
int constantPoolOopDesc::copy_cpool_bytes(int cpool_size,
                                          SymbolHashMap* tbl,
                                          unsigned char *bytes) {
  u2   idx1, idx2;
  jint size  = 0;
  jint cnt   = length();
  unsigned char *start_bytes = bytes;

  for (jint idx = 1; idx < cnt; idx++) {
    u1   tag      = tag_at(idx).value();
    jint ent_size = cpool_entry_size(idx);

    assert(size + ent_size <= cpool_size, "Size mismatch");

    *bytes = tag;
    DBG(printf("#%03hd tag=%03hd, ", idx, tag));
    switch(tag) {
      case JVM_CONSTANT_Invalid: {
        DBG(printf("JVM_CONSTANT_Invalid"));
        break;
      }
      case JVM_CONSTANT_Unicode: {
        assert(false, "Wrong constant pool tag: JVM_CONSTANT_Unicode");
        DBG(printf("JVM_CONSTANT_Unicode"));
        break;
      }
      case JVM_CONSTANT_Utf8: {
        Symbol* sym = symbol_at(idx);
        char*     str = sym->as_utf8();
        // Warning! It's crashing on x86 with len = sym->utf8_length()
        int       len = (int) strlen(str);
        Bytes::put_Java_u2((address) (bytes+1), (u2) len);
        for (int i = 0; i < len; i++) {
            bytes[3+i] = (u1) str[i];
        }
        DBG(printf("JVM_CONSTANT_Utf8: %s ", str));
        break;
      }
      case JVM_CONSTANT_Integer: {
        jint val = int_at(idx);
        Bytes::put_Java_u4((address) (bytes+1), *(u4*)&val);
        break;
      }
      case JVM_CONSTANT_Float: {
        jfloat val = float_at(idx);
        Bytes::put_Java_u4((address) (bytes+1), *(u4*)&val);
        break;
      }
      case JVM_CONSTANT_Long: {
        jlong val = long_at(idx);
        Bytes::put_Java_u8((address) (bytes+1), *(u8*)&val);
        idx++;             // Long takes two cpool slots
        break;
      }
      case JVM_CONSTANT_Double: {
        jdouble val = double_at(idx);
        Bytes::put_Java_u8((address) (bytes+1), *(u8*)&val);
        idx++;             // Double takes two cpool slots
        break;
      }
      case JVM_CONSTANT_Class:
      case JVM_CONSTANT_UnresolvedClass:
      case JVM_CONSTANT_UnresolvedClassInError: {
        *bytes = JVM_CONSTANT_Class;
        Symbol* sym = klass_name_at(idx);
        idx1 = tbl->symbol_to_value(sym);
        assert(idx1 != 0, "Have not found a hashtable entry");
        Bytes::put_Java_u2((address) (bytes+1), idx1);
        DBG(printf("JVM_CONSTANT_Class: idx=#%03hd, %s", idx1, sym->as_utf8()));
        break;
      }
      case JVM_CONSTANT_String: {
        unsigned int hash;
        char *str = string_at_noresolve(idx);
        TempNewSymbol sym = SymbolTable::lookup_only(str, (int) strlen(str), hash);
        if (sym == NULL) {
          // sym can be NULL if string refers to incorrectly encoded JVM_CONSTANT_Utf8
          // this can happen with JVM TI; see CR 6839599 for more details
          oop string = *(obj_at_addr_raw(idx));
          assert(java_lang_String::is_instance(string),"Not a String");
          DBG(printf("Error #%03hd tag=%03hd\n", idx, tag));
          idx1 = 0;
          for (int j = 0; j < tbl->table_size() && idx1 == 0; j++) {
            for (SymbolHashMapEntry* cur = tbl->bucket(j); cur != NULL; cur = cur->next()) {
              int length;
              Symbol* s = cur->symbol();
              jchar* chars = s->as_unicode(length);
              if (java_lang_String::equals(string, chars, length)) {
                idx1 = cur->value();
                DBG(printf("Index found: %d\n",idx1));
                break;
              }
            }
          }
        } else {
          idx1 = tbl->symbol_to_value(sym);
        }
        assert(idx1 != 0, "Have not found a hashtable entry");
        Bytes::put_Java_u2((address) (bytes+1), idx1);
        DBG(printf("JVM_CONSTANT_String: idx=#%03hd, %s", idx1, str));
        break;
      }
      case JVM_CONSTANT_UnresolvedString: {
        *bytes = JVM_CONSTANT_String;
        Symbol* sym = unresolved_string_at(idx);
        idx1 = tbl->symbol_to_value(sym);
        assert(idx1 != 0, "Have not found a hashtable entry");
        Bytes::put_Java_u2((address) (bytes+1), idx1);
        DBG(char *str = sym->as_utf8());
        DBG(printf("JVM_CONSTANT_UnresolvedString: idx=#%03hd, %s", idx1, str));
        break;
      }
      case JVM_CONSTANT_Fieldref:
      case JVM_CONSTANT_Methodref:
      case JVM_CONSTANT_InterfaceMethodref: {
        idx1 = uncached_klass_ref_index_at(idx);
        idx2 = uncached_name_and_type_ref_index_at(idx);
        Bytes::put_Java_u2((address) (bytes+1), idx1);
        Bytes::put_Java_u2((address) (bytes+3), idx2);
        DBG(printf("JVM_CONSTANT_Methodref: %hd %hd", idx1, idx2));
        break;
      }
      case JVM_CONSTANT_NameAndType: {
        idx1 = name_ref_index_at(idx);
        idx2 = signature_ref_index_at(idx);
        Bytes::put_Java_u2((address) (bytes+1), idx1);
        Bytes::put_Java_u2((address) (bytes+3), idx2);
        DBG(printf("JVM_CONSTANT_NameAndType: %hd %hd", idx1, idx2));
        break;
      }
      case JVM_CONSTANT_ClassIndex: {
        *bytes = JVM_CONSTANT_Class;
        idx1 = klass_index_at(idx);
        Bytes::put_Java_u2((address) (bytes+1), idx1);
        DBG(printf("JVM_CONSTANT_ClassIndex: %hd", idx1));
        break;
      }
      case JVM_CONSTANT_StringIndex: {
        *bytes = JVM_CONSTANT_String;
        idx1 = string_index_at(idx);
        Bytes::put_Java_u2((address) (bytes+1), idx1);
        DBG(printf("JVM_CONSTANT_StringIndex: %hd", idx1));
        break;
      }
      case JVM_CONSTANT_MethodHandle: {
        *bytes = JVM_CONSTANT_MethodHandle;
        int kind = method_handle_ref_kind_at(idx);
        idx1 = method_handle_index_at(idx);
        *(bytes+1) = (unsigned char) kind;
        Bytes::put_Java_u2((address) (bytes+2), idx1);
        DBG(printf("JVM_CONSTANT_MethodHandle: %d %hd", kind, idx1));
        break;
      }
      case JVM_CONSTANT_MethodType: {
        *bytes = JVM_CONSTANT_MethodType;
        idx1 = method_type_index_at(idx);
        Bytes::put_Java_u2((address) (bytes+1), idx1);
        DBG(printf("JVM_CONSTANT_MethodType: %hd", idx1));
        break;
      }
      case JVM_CONSTANT_InvokeDynamic: {
        *bytes = tag;
        idx1 = extract_low_short_from_int(*int_at_addr(idx));
        idx2 = extract_high_short_from_int(*int_at_addr(idx));
        assert(idx2 == invoke_dynamic_name_and_type_ref_index_at(idx), "correct half of u4");
        Bytes::put_Java_u2((address) (bytes+1), idx1);
        Bytes::put_Java_u2((address) (bytes+3), idx2);
        DBG(printf("JVM_CONSTANT_InvokeDynamic: %hd %hd", idx1, idx2));
        break;
      }
    }
    DBG(printf("\n"));
    bytes += ent_size;
    size  += ent_size;
  }
  assert(size == cpool_size, "Size mismatch");

  // Keep temorarily for debugging until it's stable.
  DBG(print_cpool_bytes(cnt, start_bytes));
  return (int)(bytes - start_bytes);
} /* end copy_cpool_bytes */


void SymbolHashMap::add_entry(Symbol* sym, u2 value) {
  char *str = sym->as_utf8();
  unsigned int hash = compute_hash(str, sym->utf8_length());
  unsigned int index = hash % table_size();

  // check if already in map
  // we prefer the first entry since it is more likely to be what was used in
  // the class file
  for (SymbolHashMapEntry *en = bucket(index); en != NULL; en = en->next()) {
    assert(en->symbol() != NULL, "SymbolHashMapEntry symbol is NULL");
    if (en->hash() == hash && en->symbol() == sym) {
        return;  // already there
    }
  }

  SymbolHashMapEntry* entry = new SymbolHashMapEntry(hash, sym, value);
  entry->set_next(bucket(index));
  _buckets[index].set_entry(entry);
  assert(entry->symbol() != NULL, "SymbolHashMapEntry symbol is NULL");
}

SymbolHashMapEntry* SymbolHashMap::find_entry(Symbol* sym) {
  assert(sym != NULL, "SymbolHashMap::find_entry - symbol is NULL");
  char *str = sym->as_utf8();
  int   len = sym->utf8_length();
  unsigned int hash = SymbolHashMap::compute_hash(str, len);
  unsigned int index = hash % table_size();
  for (SymbolHashMapEntry *en = bucket(index); en != NULL; en = en->next()) {
    assert(en->symbol() != NULL, "SymbolHashMapEntry symbol is NULL");
    if (en->hash() == hash && en->symbol() == sym) {
      return en;
    }
  }
  return NULL;
}
