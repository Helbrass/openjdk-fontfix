/*
 * Copyright (c) 2002, 2011, Oracle and/or its affiliates. All rights reserved.
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

#ifndef SHARE_VM_COMPILER_COMPILELOG_HPP
#define SHARE_VM_COMPILER_COMPILELOG_HPP

#include "utilities/xmlstream.hpp"

class ciObject;
class ciSymbol;

// CompileLog
//
// An open stream for logging information about activities in a
// compiler thread.  There is exactly one per CompilerThread,
// if the +LogCompilation switch is enabled.
class CompileLog : public xmlStream {
 private:
  const char*   _file;           // name of file where XML goes
  julong        _file_end;       // last good end of file
  intx          _thread_id;      // which compile thread

  stringStream  _context;        // optional, killable context marker
  char          _context_buffer[100];

  char*         _identities;     // array of boolean
  int           _identities_limit;
  int           _identities_capacity;

  CompileLog*   _next;           // static chain of all logs

  static CompileLog* _first;     // head of static chain

  void va_tag(bool push, const char* format, va_list ap);

 public:
  CompileLog(const char* file, FILE* fp, intx thread_id);
  ~CompileLog();

  intx          thread_id()                      { return _thread_id; }
  const char*   file()                           { return _file; }
  stringStream* context()                        { return &_context; }

  void          name(ciSymbol* s);               // name='s'
  void          name(Symbol* s)                  { xmlStream::name(s); }

  // Output an object description, return obj->ident().
  int           identify(ciObject* obj);
  void          clear_identities();

  // virtuals
  virtual void see_tag(const char* tag, bool push);
  virtual void pop_tag(const char* tag);

  // make a provisional end of log mark
  void mark_file_end() { _file_end = out()->count(); }

  // copy all logs to the given stream
  static void finish_log(outputStream* out);
  static void finish_log_on_error(outputStream* out, char *buf, int buflen);
};

#endif // SHARE_VM_COMPILER_COMPILELOG_HPP
