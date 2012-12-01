/*
 * Copyright (c) 1998, 2010, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_INTERPRETER_REWRITER_HPP
#define SHARE_VM_INTERPRETER_REWRITER_HPP

#include "memory/allocation.hpp"
#include "runtime/handles.inline.hpp"
#include "utilities/growableArray.hpp"

// The Rewriter adds caches to the constant pool and rewrites bytecode indices
// pointing into the constant pool for better interpreter performance.

class Rewriter: public StackObj {
 private:
  instanceKlassHandle _klass;
  constantPoolHandle  _pool;
  objArrayHandle      _methods;
  intArray            _cp_map;
  intStack            _cp_cache_map;
  bool                _have_invoke_dynamic;

  void init_cp_map(int length) {
    _cp_map.initialize(length, -1);
    // Choose an initial value large enough that we don't get frequent
    // calls to grow().
    _cp_cache_map.initialize(length / 2);
  }
  int  cp_entry_to_cp_cache(int i) { assert(has_cp_cache(i), "oob"); return _cp_map[i]; }
  bool has_cp_cache(int i) { return (uint)i < (uint)_cp_map.length() && _cp_map[i] >= 0; }
  int maybe_add_cp_cache_entry(int i) { return has_cp_cache(i) ? _cp_map[i] : add_cp_cache_entry(i); }
  int add_cp_cache_entry(int cp_index) {
    assert((cp_index & _secondary_entry_tag) == 0, "bad tag");
    assert(_cp_map[cp_index] == -1, "not twice on same cp_index");
    int cache_index = _cp_cache_map.append(cp_index);
    _cp_map.at_put(cp_index, cache_index);
    assert(cp_entry_to_cp_cache(cp_index) == cache_index, "");
    return cache_index;
  }
  int add_secondary_cp_cache_entry(int main_cpc_entry) {
    assert(main_cpc_entry < _cp_cache_map.length(), "must be earlier CP cache entry");
    int cache_index = _cp_cache_map.append(main_cpc_entry | _secondary_entry_tag);
    return cache_index;
  }

  // Access the contents of _cp_cache_map to determine CP cache layout.
  int cp_cache_entry_pool_index(int cache_index) {
    int cp_index = _cp_cache_map[cache_index];
    if ((cp_index & _secondary_entry_tag) != 0)
      return -1;
    else
      return cp_index;
  }
  int cp_cache_secondary_entry_main_index(int cache_index) {
    int cp_index = _cp_cache_map[cache_index];
    if ((cp_index & _secondary_entry_tag) == 0)
      return -1;
    else
      return (cp_index - _secondary_entry_tag);
  }

  // All the work goes in here:
  Rewriter(instanceKlassHandle klass, constantPoolHandle cpool, objArrayHandle methods, TRAPS);

  void compute_index_maps();
  void make_constant_pool_cache(TRAPS);
  void scan_method(methodOop m, bool reverse = false);
  void rewrite_Object_init(methodHandle m, TRAPS);
  void rewrite_member_reference(address bcp, int offset, bool reverse = false);
  void rewrite_invokedynamic(address bcp, int offset, bool reverse = false);
  void maybe_rewrite_ldc(address bcp, int offset, bool is_wide, bool reverse = false);
  // Revert bytecodes in case of an exception.
  void restore_bytecodes();

  static methodHandle rewrite_jsrs(methodHandle m, TRAPS);
 public:
  // Driver routine:
  static void rewrite(instanceKlassHandle klass, TRAPS);
  static void rewrite(instanceKlassHandle klass, constantPoolHandle cpool, objArrayHandle methods, TRAPS);

  enum {
    _secondary_entry_tag = nth_bit(30)
  };

  // Second pass, not gated by is_rewritten flag
  static void relocate_and_link(instanceKlassHandle klass, TRAPS);
  // JSR292 version to call with it's own methods.
  static void relocate_and_link(instanceKlassHandle klass,
                                objArrayHandle methods, TRAPS);

};

#endif // SHARE_VM_INTERPRETER_REWRITER_HPP
