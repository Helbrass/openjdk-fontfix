/*
 * Copyright (c) 2003, 2011, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_CLASSFILE_LOADERCONSTRAINTS_HPP
#define SHARE_VM_CLASSFILE_LOADERCONSTRAINTS_HPP

#include "classfile/dictionary.hpp"
#include "classfile/placeholders.hpp"
#include "utilities/hashtable.hpp"

class LoaderConstraintEntry;

class LoaderConstraintTable : public Hashtable<klassOop> {
  friend class VMStructs;
private:

  enum Constants {
    _loader_constraint_size = 107,                     // number of entries in constraint table
    _nof_buckets            = 1009                     // number of buckets in hash table
  };

  LoaderConstraintEntry** find_loader_constraint(Symbol* name,
                                                 Handle loader);

public:

  LoaderConstraintTable(int nof_buckets);

  LoaderConstraintEntry* new_entry(unsigned int hash, Symbol* name,
                                   klassOop klass, int num_loaders,
                                   int max_loaders);
  void free_entry(LoaderConstraintEntry *entry);

  LoaderConstraintEntry* bucket(int i) {
    return (LoaderConstraintEntry*)Hashtable<klassOop>::bucket(i);
  }

  LoaderConstraintEntry** bucket_addr(int i) {
    return (LoaderConstraintEntry**)Hashtable<klassOop>::bucket_addr(i);
  }

  // GC support
  void oops_do(OopClosure* f);

  // Check class loader constraints
  bool add_entry(Symbol* name, klassOop klass1, Handle loader1,
                                    klassOop klass2, Handle loader2);

  // Note:  The main entry point for this module is via SystemDictionary.
  // SystemDictionary::check_signature_loaders(Symbol* signature,
  //                                           Handle loader1, Handle loader2,
  //                                           bool is_method, TRAPS)

  klassOop find_constrained_klass(Symbol* name, Handle loader);

  // Class loader constraints

  void ensure_loader_constraint_capacity(LoaderConstraintEntry *p, int nfree);
  void extend_loader_constraint(LoaderConstraintEntry* p, Handle loader,
                                klassOop klass);
  void merge_loader_constraints(LoaderConstraintEntry** pp1,
                                LoaderConstraintEntry** pp2, klassOop klass);

  bool check_or_update(instanceKlassHandle k, Handle loader,
                              Symbol* name);


  void purge_loader_constraints(BoolObjectClosure* is_alive);

  void verify(Dictionary* dictionary, PlaceholderTable* placeholders);
#ifndef PRODUCT
  void print();
#endif
};

class LoaderConstraintEntry : public HashtableEntry<klassOop> {
  friend class VMStructs;
private:
  Symbol*                _name;                   // class name
  int                    _num_loaders;
  int                    _max_loaders;
  oop*                   _loaders;                // initiating loaders

public:

  klassOop klass() { return literal(); }
  klassOop* klass_addr() { return literal_addr(); }
  void set_klass(klassOop k) { set_literal(k); }

  LoaderConstraintEntry* next() {
    return (LoaderConstraintEntry*)HashtableEntry<klassOop>::next();
  }

  LoaderConstraintEntry** next_addr() {
    return (LoaderConstraintEntry**)HashtableEntry<klassOop>::next_addr();
  }
  void set_next(LoaderConstraintEntry* next) {
    HashtableEntry<klassOop>::set_next(next);
  }

  Symbol* name() { return _name; }
  void set_name(Symbol* name) {
    _name = name;
    if (name != NULL) name->increment_refcount();
  }

  int num_loaders() { return _num_loaders; }
  void set_num_loaders(int i) { _num_loaders = i; }

  int max_loaders() { return _max_loaders; }
  void set_max_loaders(int i) { _max_loaders = i; }

  oop* loaders() { return _loaders; }
  void set_loaders(oop* loaders) { _loaders = loaders; }

  oop loader(int i) { return _loaders[i]; }
  oop* loader_addr(int i) { return &_loaders[i]; }
  void set_loader(int i, oop p) { _loaders[i] = p; }

};

#endif // SHARE_VM_CLASSFILE_LOADERCONSTRAINTS_HPP
