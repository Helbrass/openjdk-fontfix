/*
 * Copyright (c) 2009, 2011, Oracle and/or its affiliates. All rights reserved.
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
#include "ci/ciCPCache.hpp"
#include "ci/ciUtilities.hpp"
#include "oops/cpCacheOop.hpp"

// ciCPCache

// ------------------------------------------------------------------
// ciCPCache::get_f1_offset
size_t ciCPCache::get_f1_offset(int index) {
  // Calculate the offset from the constantPoolCacheOop to the f1
  // field.
  ByteSize f1_offset =
    constantPoolCacheOopDesc::entry_offset(index) +
    ConstantPoolCacheEntry::f1_offset();

  return in_bytes(f1_offset);
}


// ------------------------------------------------------------------
// ciCPCache::is_f1_null_at
bool ciCPCache::is_f1_null_at(int index) {
  VM_ENTRY_MARK;
  return entry_at(index)->is_f1_null();
}


// ------------------------------------------------------------------
// ciCPCache::get_pool_index
int ciCPCache::get_pool_index(int index) {
  VM_ENTRY_MARK;
  ConstantPoolCacheEntry* e = entry_at(index);
  if (e->is_secondary_entry())
    e = entry_at(e->main_entry_index());
  return e->constant_pool_index();
}


// ------------------------------------------------------------------
// ciCPCache::print
//
// Print debugging information about the cache.
void ciCPCache::print() {
  Unimplemented();
}
