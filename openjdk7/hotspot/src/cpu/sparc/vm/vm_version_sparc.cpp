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

#include "precompiled.hpp"
#include "assembler_sparc.inline.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/java.hpp"
#include "runtime/stubCodeGenerator.hpp"
#include "vm_version_sparc.hpp"
#ifdef TARGET_OS_FAMILY_linux
# include "os_linux.inline.hpp"
#endif
#ifdef TARGET_OS_FAMILY_solaris
# include "os_solaris.inline.hpp"
#endif

int VM_Version::_features = VM_Version::unknown_m;
const char* VM_Version::_features_str = "";

void VM_Version::initialize() {
  _features = determine_features();
  PrefetchCopyIntervalInBytes = prefetch_copy_interval_in_bytes();
  PrefetchScanIntervalInBytes = prefetch_scan_interval_in_bytes();
  PrefetchFieldsAhead         = prefetch_fields_ahead();

  // Allocation prefetch settings
  intx cache_line_size = L1_data_cache_line_size();
  if( cache_line_size > AllocatePrefetchStepSize )
    AllocatePrefetchStepSize = cache_line_size;
  if( FLAG_IS_DEFAULT(AllocatePrefetchLines) )
    AllocatePrefetchLines = 3; // Optimistic value
  assert( AllocatePrefetchLines > 0, "invalid value");
  if( AllocatePrefetchLines < 1 ) // set valid value in product VM
    AllocatePrefetchLines = 1; // Conservative value

  AllocatePrefetchDistance = allocate_prefetch_distance();
  AllocatePrefetchStyle    = allocate_prefetch_style();

  assert(AllocatePrefetchDistance % AllocatePrefetchStepSize == 0, "invalid value");

  UseSSE = 0; // Only on x86 and x64

  _supports_cx8               = has_v9();

  if (is_niagara()) {
    // Indirect branch is the same cost as direct
    if (FLAG_IS_DEFAULT(UseInlineCaches)) {
      FLAG_SET_DEFAULT(UseInlineCaches, false);
    }
    // Align loops on a single instruction boundary.
    if (FLAG_IS_DEFAULT(OptoLoopAlignment)) {
      FLAG_SET_DEFAULT(OptoLoopAlignment, 4);
    }
    // When using CMS, we cannot use memset() in BOT updates because
    // the sun4v/CMT version in libc_psr uses BIS which exposes
    // "phantom zeros" to concurrent readers. See 6948537.
    if (FLAG_IS_DEFAULT(UseMemSetInBOT) && UseConcMarkSweepGC) {
      FLAG_SET_DEFAULT(UseMemSetInBOT, false);
    }
#ifdef _LP64
    // 32-bit oops don't make sense for the 64-bit VM on sparc
    // since the 32-bit VM has the same registers and smaller objects.
    Universe::set_narrow_oop_shift(LogMinObjAlignmentInBytes);
#endif // _LP64
#ifdef COMPILER2
    // Indirect branch is the same cost as direct
    if (FLAG_IS_DEFAULT(UseJumpTables)) {
      FLAG_SET_DEFAULT(UseJumpTables, true);
    }
    // Single-issue, so entry and loop tops are
    // aligned on a single instruction boundary
    if (FLAG_IS_DEFAULT(InteriorEntryAlignment)) {
      FLAG_SET_DEFAULT(InteriorEntryAlignment, 4);
    }
    if (is_niagara_plus()) {
      if (has_blk_init() && AllocatePrefetchStyle > 0 &&
          FLAG_IS_DEFAULT(AllocatePrefetchStyle)) {
        // Use BIS instruction for allocation prefetch.
        FLAG_SET_DEFAULT(AllocatePrefetchStyle, 3);
        if (FLAG_IS_DEFAULT(AllocatePrefetchDistance)) {
          // Use smaller prefetch distance on N2 with BIS
          FLAG_SET_DEFAULT(AllocatePrefetchDistance, 64);
        }
      }
      if (AllocatePrefetchStyle != 3 && FLAG_IS_DEFAULT(AllocatePrefetchDistance)) {
        // Use different prefetch distance without BIS
        FLAG_SET_DEFAULT(AllocatePrefetchDistance, 256);
      }
    }
#endif
  }

  // Use hardware population count instruction if available.
  if (has_hardware_popc()) {
    if (FLAG_IS_DEFAULT(UsePopCountInstruction)) {
      FLAG_SET_DEFAULT(UsePopCountInstruction, true);
    }
  }

#ifdef COMPILER2
  // Currently not supported anywhere.
  FLAG_SET_DEFAULT(UseFPUForSpilling, false);
#endif

  char buf[512];
  jio_snprintf(buf, sizeof(buf), "%s%s%s%s%s%s%s%s%s%s%s%s%s%s%s",
               (has_v8() ? ", has_v8" : ""),
               (has_v9() ? ", has_v9" : ""),
               (has_hardware_popc() ? ", popc" : ""),
               (has_vis1() ? ", has_vis1" : ""),
               (has_vis2() ? ", has_vis2" : ""),
               (has_vis3() ? ", has_vis3" : ""),
               (has_blk_init() ? ", has_blk_init" : ""),
               (is_ultra3() ? ", is_ultra3" : ""),
               (is_sun4v() ? ", is_sun4v" : ""),
               (is_niagara() ? ", is_niagara" : ""),
               (is_niagara_plus() ? ", is_niagara_plus" : ""),
               (is_sparc64() ? ", is_sparc64" : ""),
               (!has_hardware_mul32() ? ", no-mul32" : ""),
               (!has_hardware_div32() ? ", no-div32" : ""),
               (!has_hardware_fsmuld() ? ", no-fsmuld" : ""));

  // buf is started with ", " or is empty
  _features_str = strdup(strlen(buf) > 2 ? buf + 2 : buf);

#ifndef PRODUCT
  if (PrintMiscellaneous && Verbose) {
    tty->print("Allocation: ");
    if (AllocatePrefetchStyle <= 0) {
      tty->print_cr("no prefetching");
    } else {
      if (AllocatePrefetchLines > 1) {
        tty->print_cr("PREFETCH %d, %d lines of size %d bytes", AllocatePrefetchDistance, AllocatePrefetchLines, AllocatePrefetchStepSize);
      } else {
        tty->print_cr("PREFETCH %d, one line", AllocatePrefetchDistance);
      }
    }
    if (PrefetchCopyIntervalInBytes > 0) {
      tty->print_cr("PrefetchCopyIntervalInBytes %d", PrefetchCopyIntervalInBytes);
    }
    if (PrefetchScanIntervalInBytes > 0) {
      tty->print_cr("PrefetchScanIntervalInBytes %d", PrefetchScanIntervalInBytes);
    }
    if (PrefetchFieldsAhead > 0) {
      tty->print_cr("PrefetchFieldsAhead %d", PrefetchFieldsAhead);
    }
  }
#endif // PRODUCT
}

void VM_Version::print_features() {
  tty->print_cr("Version:%s", cpu_features());
}

int VM_Version::determine_features() {
  if (UseV8InstrsOnly) {
    NOT_PRODUCT(if (PrintMiscellaneous && Verbose) tty->print_cr("Version is Forced-V8");)
    return generic_v8_m;
  }

  int features = platform_features(unknown_m); // platform_features() is os_arch specific

  if (features == unknown_m) {
    features = generic_v9_m;
    warning("Cannot recognize SPARC version. Default to V9");
  }

  assert(is_T_family(features) == is_niagara(features), "Niagara should be T series");
  if (UseNiagaraInstrs) { // Force code generation for Niagara
    if (is_T_family(features)) {
      // Happy to accomodate...
    } else {
      NOT_PRODUCT(if (PrintMiscellaneous && Verbose) tty->print_cr("Version is Forced-Niagara");)
      features |= T_family_m;
    }
  } else {
    if (is_T_family(features) && !FLAG_IS_DEFAULT(UseNiagaraInstrs)) {
      NOT_PRODUCT(if (PrintMiscellaneous && Verbose) tty->print_cr("Version is Forced-Not-Niagara");)
      features &= ~(T_family_m | T1_model_m);
    } else {
      // Happy to accomodate...
    }
  }

  return features;
}

static int saved_features = 0;

void VM_Version::allow_all() {
  saved_features = _features;
  _features      = all_features_m;
}

void VM_Version::revert() {
  _features = saved_features;
}

unsigned int VM_Version::calc_parallel_worker_threads() {
  unsigned int result;
  if (is_niagara_plus()) {
    result = nof_parallel_worker_threads(5, 16, 8);
  } else {
    result = nof_parallel_worker_threads(5, 8, 8);
  }
  return result;
}
