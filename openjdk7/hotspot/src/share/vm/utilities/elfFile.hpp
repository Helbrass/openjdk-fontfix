/*
 * Copyright (c) 1997, 2010, Oracle and/or its affiliates. All rights reserved.
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

#ifndef __ELF_FILE_HPP
#define __ELF_FILE_HPP

#ifndef _WINDOWS

#include <elf.h>
#include <stdio.h>

#ifdef _LP64

typedef Elf64_Half      Elf_Half;
typedef Elf64_Word      Elf_Word;
typedef Elf64_Off       Elf_Off;
typedef Elf64_Addr      Elf_Addr;

typedef Elf64_Ehdr      Elf_Ehdr;
typedef Elf64_Shdr      Elf_Shdr;
typedef Elf64_Sym       Elf_Sym;

#define ELF_ST_TYPE ELF64_ST_TYPE

#else

typedef Elf32_Half      Elf_Half;
typedef Elf32_Word      Elf_Word;
typedef Elf32_Off       Elf_Off;
typedef Elf32_Addr      Elf_Addr;


typedef Elf32_Ehdr      Elf_Ehdr;
typedef Elf32_Shdr      Elf_Shdr;
typedef Elf32_Sym       Elf_Sym;

#define ELF_ST_TYPE ELF32_ST_TYPE
#endif

#include "globalDefinitions.hpp"
#include "memory/allocation.hpp"
#include "utilities/decoder.hpp"


class ElfStringTable;
class ElfSymbolTable;


// On Solaris/Linux platforms, libjvm.so does contain all private symbols.
// ElfFile is basically an elf file parser, which can lookup the symbol
// that is the nearest to the given address.
// Beware, this code is called from vm error reporting code, when vm is already
// in "error" state, so there are scenarios, lookup will fail. We want this
// part of code to be very defensive, and bait out if anything went wrong.

class ElfFile: public CHeapObj {
  friend class Decoder;
 public:
  ElfFile(const char* filepath);
  ~ElfFile();

  const char* decode(address addr, int* offset);
  const char* filepath() {
    return m_filepath;
  }

  bool same_elf_file(const char* filepath) {
    assert(filepath, "null file path");
    assert(m_filepath, "already out of memory");
    return (m_filepath && !strcmp(filepath, m_filepath));
  }

  Decoder::decoder_status get_status() {
    return m_status;
  }

 private:
  // sanity check, if the file is a real elf file
  bool is_elf_file(Elf_Ehdr&);

  // load string tables from the elf file
  bool load_tables();

  // string tables are stored in a linked list
  void add_string_table(ElfStringTable* table);

  // symbol tables are stored in a linked list
  void add_symbol_table(ElfSymbolTable* table);

  // return a string table at specified section index
  ElfStringTable* get_string_table(int index);

  // look up an address and return the nearest symbol
  const char* look_up(Elf_Shdr shdr, address addr, int* offset);

 protected:
    ElfFile*         m_next;

 private:
  // file
  const char* m_filepath;
  FILE* m_file;

  // Elf header
  Elf_Ehdr            m_elfHdr;

  // symbol tables
  ElfSymbolTable*     m_symbol_tables;

  // string tables
  ElfStringTable*     m_string_tables;

  Decoder::decoder_status  m_status;
};

#endif // _WINDOWS

#endif // __ELF_FILE_HPP

