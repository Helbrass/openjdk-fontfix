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
#include "classfile/systemDictionary.hpp"
#include "classfile/vmSymbols.hpp"
#include "compiler/compileBroker.hpp"
#include "compiler/compileLog.hpp"
#include "interpreter/linkResolver.hpp"
#include "oops/objArrayKlass.hpp"
#include "opto/callGenerator.hpp"
#include "opto/parse.hpp"
#include "runtime/handles.inline.hpp"

//=============================================================================
//------------------------------InlineTree-------------------------------------
InlineTree::InlineTree(Compile* c,
                       const InlineTree *caller_tree, ciMethod* callee,
                       JVMState* caller_jvms, int caller_bci,
                       float site_invoke_ratio, int max_inline_level) :
  C(c),
  _caller_jvms(caller_jvms),
  _caller_tree((InlineTree*) caller_tree),
  _method(callee),
  _site_invoke_ratio(site_invoke_ratio),
  _max_inline_level(max_inline_level),
  _count_inline_bcs(method()->code_size())
{
  NOT_PRODUCT(_count_inlines = 0;)
  if (_caller_jvms != NULL) {
    // Keep a private copy of the caller_jvms:
    _caller_jvms = new (C) JVMState(caller_jvms->method(), caller_tree->caller_jvms());
    _caller_jvms->set_bci(caller_jvms->bci());
    assert(!caller_jvms->should_reexecute(), "there should be no reexecute bytecode with inlining");
  }
  assert(_caller_jvms->same_calls_as(caller_jvms), "consistent JVMS");
  assert((caller_tree == NULL ? 0 : caller_tree->stack_depth() + 1) == stack_depth(), "correct (redundant) depth parameter");
  assert(caller_bci == this->caller_bci(), "correct (redundant) bci parameter");
  if (UseOldInlining) {
    // Update hierarchical counts, count_inline_bcs() and count_inlines()
    InlineTree *caller = (InlineTree *)caller_tree;
    for( ; caller != NULL; caller = ((InlineTree *)(caller->caller_tree())) ) {
      caller->_count_inline_bcs += count_inline_bcs();
      NOT_PRODUCT(caller->_count_inlines++;)
    }
  }
}

InlineTree::InlineTree(Compile* c, ciMethod* callee_method, JVMState* caller_jvms,
                       float site_invoke_ratio, int max_inline_level) :
  C(c),
  _caller_jvms(caller_jvms),
  _caller_tree(NULL),
  _method(callee_method),
  _site_invoke_ratio(site_invoke_ratio),
  _max_inline_level(max_inline_level),
  _count_inline_bcs(method()->code_size())
{
  NOT_PRODUCT(_count_inlines = 0;)
  assert(!UseOldInlining, "do not use for old stuff");
}

static bool is_init_with_ea(ciMethod* callee_method,
                            ciMethod* caller_method, Compile* C) {
  // True when EA is ON and a java constructor is called or
  // a super constructor is called from an inlined java constructor.
  return C->do_escape_analysis() && EliminateAllocations &&
         ( callee_method->is_initializer() ||
           (caller_method->is_initializer() &&
            caller_method != C->method() &&
            caller_method->holder()->is_subclass_of(callee_method->holder()))
         );
}

// positive filter: should send be inlined?  returns NULL, if yes, or rejection msg
const char* InlineTree::should_inline(ciMethod* callee_method, ciMethod* caller_method, int caller_bci, ciCallProfile& profile, WarmCallInfo* wci_result) const {
  // Allows targeted inlining
  if(callee_method->should_inline()) {
    *wci_result = *(WarmCallInfo::always_hot());
    if (PrintInlining && Verbose) {
      CompileTask::print_inline_indent(inline_level());
      tty->print_cr("Inlined method is hot: ");
    }
    return NULL;
  }

  // positive filter: should send be inlined?  returns NULL (--> yes)
  // or rejection msg
  int size = callee_method->code_size();

  // Check for too many throws (and not too huge)
  if(callee_method->interpreter_throwout_count() > InlineThrowCount &&
     size < InlineThrowMaxSize ) {
    wci_result->set_profit(wci_result->profit() * 100);
    if (PrintInlining && Verbose) {
      CompileTask::print_inline_indent(inline_level());
      tty->print_cr("Inlined method with many throws (throws=%d):", callee_method->interpreter_throwout_count());
    }
    return NULL;
  }

  if (!UseOldInlining) {
    return NULL;  // size and frequency are represented in a new way
  }

  int default_max_inline_size = C->max_inline_size();
  int inline_small_code_size  = InlineSmallCode / 4;
  int max_inline_size         = default_max_inline_size;

  int call_site_count  = method()->scale_count(profile.count());
  int invoke_count     = method()->interpreter_invocation_count();

  // Bytecoded method handle adapters do not have interpreter
  // profiling data but only made up MDO data.  Get the counter from
  // there.
  if (caller_method->is_method_handle_adapter()) {
    assert(method()->method_data_or_null(), "must have an MDO");
    ciMethodData* mdo = method()->method_data();
    ciProfileData* mha_profile = mdo->bci_to_data(caller_bci);
    assert(mha_profile, "must exist");
    CounterData* cd = mha_profile->as_CounterData();
    invoke_count = cd->count();
    call_site_count = invoke_count;  // use the same value
  }

  assert(invoke_count != 0, "require invocation count greater than zero");
  int freq = call_site_count / invoke_count;

  // bump the max size if the call is frequent
  if ((freq >= InlineFrequencyRatio) ||
      (call_site_count >= InlineFrequencyCount) ||
      is_init_with_ea(callee_method, caller_method, C)) {

    max_inline_size = C->freq_inline_size();
    if (size <= max_inline_size && TraceFrequencyInlining) {
      CompileTask::print_inline_indent(inline_level());
      tty->print_cr("Inlined frequent method (freq=%d count=%d):", freq, call_site_count);
      CompileTask::print_inline_indent(inline_level());
      callee_method->print();
      tty->cr();
    }
  } else {
    // Not hot.  Check for medium-sized pre-existing nmethod at cold sites.
    if (callee_method->has_compiled_code() &&
        callee_method->instructions_size(CompLevel_full_optimization) > inline_small_code_size)
      return "already compiled into a medium method";
  }
  if (size > max_inline_size) {
    if (max_inline_size > default_max_inline_size)
      return "hot method too big";
    return "too big";
  }
  return NULL;
}


// negative filter: should send NOT be inlined?  returns NULL, ok to inline, or rejection msg
const char* InlineTree::should_not_inline(ciMethod *callee_method, ciMethod* caller_method, WarmCallInfo* wci_result) const {
  // negative filter: should send NOT be inlined?  returns NULL (--> inline) or rejection msg
  if (!UseOldInlining) {
    const char* fail = NULL;
    if (callee_method->is_abstract())               fail = "abstract method";
    // note: we allow ik->is_abstract()
    if (!callee_method->holder()->is_initialized()) fail = "method holder not initialized";
    if (callee_method->is_native())                 fail = "native method";

    if (fail) {
      *wci_result = *(WarmCallInfo::always_cold());
      return fail;
    }

    if (callee_method->has_unloaded_classes_in_signature()) {
      wci_result->set_profit(wci_result->profit() * 0.1);
    }

    // don't inline exception code unless the top method belongs to an
    // exception class
    if (callee_method->holder()->is_subclass_of(C->env()->Throwable_klass())) {
      ciMethod* top_method = caller_jvms() ? caller_jvms()->of_depth(1)->method() : method();
      if (!top_method->holder()->is_subclass_of(C->env()->Throwable_klass())) {
        wci_result->set_profit(wci_result->profit() * 0.1);
      }
    }

    if (callee_method->has_compiled_code() && callee_method->instructions_size(CompLevel_full_optimization) > InlineSmallCode) {
      wci_result->set_profit(wci_result->profit() * 0.1);
      // %%% adjust wci_result->size()?
    }

    return NULL;
  }

  // Always inline MethodHandle methods and generated MethodHandle adapters.
  if (callee_method->is_method_handle_invoke() || callee_method->is_method_handle_adapter())
    return NULL;

  // First check all inlining restrictions which are required for correctness
  if (callee_method->is_abstract())               return "abstract method";
  // note: we allow ik->is_abstract()
  if (!callee_method->holder()->is_initialized()) return "method holder not initialized";
  if (callee_method->is_native())                 return "native method";
  if (callee_method->has_unloaded_classes_in_signature()) return "unloaded signature classes";

  if (callee_method->should_inline()) {
    // ignore heuristic controls on inlining
    return NULL;
  }

  // Now perform checks which are heuristic

  if( callee_method->has_compiled_code() && callee_method->instructions_size(CompLevel_full_optimization) > InlineSmallCode )
    return "already compiled into a big method";

  // don't inline exception code unless the top method belongs to an
  // exception class
  if (caller_tree() != NULL &&
      callee_method->holder()->is_subclass_of(C->env()->Throwable_klass())) {
    const InlineTree *top = this;
    while (top->caller_tree() != NULL) top = top->caller_tree();
    ciInstanceKlass* k = top->method()->holder();
    if (!k->is_subclass_of(C->env()->Throwable_klass()))
      return "exception method";
  }

  // use frequency-based objections only for non-trivial methods
  if (callee_method->code_size() <= MaxTrivialSize) return NULL;

  // don't use counts with -Xcomp or CTW
  if (UseInterpreter && !CompileTheWorld) {

    if (!callee_method->has_compiled_code() &&
        !callee_method->was_executed_more_than(0)) {
      return "never executed";
    }

    if (is_init_with_ea(callee_method, caller_method, C)) {

      // Escape Analysis: inline all executed constructors

    } else if (!callee_method->was_executed_more_than(MIN2(MinInliningThreshold,
                                                           CompileThreshold >> 1))) {
      return "executed < MinInliningThreshold times";
    }
  }

  if (callee_method->should_not_inline()) {
    return "disallowed by CompilerOracle";
  }

  if (UseStringCache) {
    // Do not inline StringCache::profile() method used only at the beginning.
    if (callee_method->name() == ciSymbol::profile_name() &&
        callee_method->holder()->name() == ciSymbol::java_lang_StringCache()) {
      return "profiling method";
    }
  }

  return NULL;
}

//-----------------------------try_to_inline-----------------------------------
// return NULL if ok, reason for not inlining otherwise
// Relocated from "InliningClosure::try_to_inline"
const char* InlineTree::try_to_inline(ciMethod* callee_method, ciMethod* caller_method, int caller_bci, ciCallProfile& profile, WarmCallInfo* wci_result) {

  // Old algorithm had funny accumulating BC-size counters
  if (UseOldInlining && ClipInlining
      && (int)count_inline_bcs() >= DesiredMethodLimit) {
    return "size > DesiredMethodLimit";
  }

  const char *msg = NULL;
  msg = should_inline(callee_method, caller_method, caller_bci, profile, wci_result);
  if (msg != NULL)
    return msg;

  msg = should_not_inline(callee_method, caller_method, wci_result);
  if (msg != NULL)
    return msg;

  if (InlineAccessors && callee_method->is_accessor()) {
    // accessor methods are not subject to any of the following limits.
    return NULL;
  }

  // suppress a few checks for accessors and trivial methods
  if (callee_method->code_size() > MaxTrivialSize) {

    // don't inline into giant methods
    if (C->unique() > (uint)NodeCountInliningCutoff) {
      return "NodeCountInliningCutoff";
    }

    if ((!UseInterpreter || CompileTheWorld) &&
        is_init_with_ea(callee_method, caller_method, C)) {

      // Escape Analysis stress testing when running Xcomp or CTW:
      // inline constructors even if they are not reached.

    } else if (profile.count() == 0) {
      // don't inline unreached call sites
      return "call site not reached";
    }
  }

  if (!C->do_inlining() && InlineAccessors) {
    return "not an accessor";
  }
  if (inline_level() > _max_inline_level) {
    return "inlining too deep";
  }

  // detect direct and indirect recursive inlining
  {
    // count the current method and the callee
    int inline_level = (method() == callee_method) ? 1 : 0;
    if (inline_level > MaxRecursiveInlineLevel)
      return "recursively inlining too deep";
    // count callers of current method and callee
    JVMState* jvms = caller_jvms();
    while (jvms != NULL && jvms->has_method()) {
      if (jvms->method() == callee_method) {
        inline_level++;
        if (inline_level > MaxRecursiveInlineLevel)
          return "recursively inlining too deep";
      }
      jvms = jvms->caller();
    }
  }

  int size = callee_method->code_size();

  if (UseOldInlining && ClipInlining
      && (int)count_inline_bcs() + size >= DesiredMethodLimit) {
    return "size > DesiredMethodLimit";
  }

  // ok, inline this method
  return NULL;
}

//------------------------------pass_initial_checks----------------------------
bool pass_initial_checks(ciMethod* caller_method, int caller_bci, ciMethod* callee_method) {
  ciInstanceKlass *callee_holder = callee_method ? callee_method->holder() : NULL;
  // Check if a callee_method was suggested
  if( callee_method == NULL )            return false;
  // Check if klass of callee_method is loaded
  if( !callee_holder->is_loaded() )      return false;
  if( !callee_holder->is_initialized() ) return false;
  if( !UseInterpreter || CompileTheWorld /* running Xcomp or CTW */ ) {
    // Checks that constant pool's call site has been visited
    // stricter than callee_holder->is_initialized()
    ciBytecodeStream iter(caller_method);
    iter.force_bci(caller_bci);
    Bytecodes::Code call_bc = iter.cur_bc();
    // An invokedynamic instruction does not have a klass.
    if (call_bc != Bytecodes::_invokedynamic) {
      int index = iter.get_index_u2_cpcache();
      if (!caller_method->is_klass_loaded(index, true)) {
        return false;
      }
      // Try to do constant pool resolution if running Xcomp
      if( !caller_method->check_call(index, call_bc == Bytecodes::_invokestatic) ) {
        return false;
      }
    }
  }
  // We will attempt to see if a class/field/etc got properly loaded.  If it
  // did not, it may attempt to throw an exception during our probing.  Catch
  // and ignore such exceptions and do not attempt to compile the method.
  if( callee_method->should_exclude() )  return false;

  return true;
}

//------------------------------print_inlining---------------------------------
// Really, the failure_msg can be a success message also.
void InlineTree::print_inlining(ciMethod* callee_method, int caller_bci, const char* failure_msg) const {
  CompileTask::print_inlining(callee_method, inline_level(), caller_bci, failure_msg ? failure_msg : "inline");
  if (callee_method == NULL)  tty->print(" callee not monotonic or profiled");
  if (Verbose && callee_method) {
    const InlineTree *top = this;
    while( top->caller_tree() != NULL ) { top = top->caller_tree(); }
    tty->print("  bcs: %d+%d  invoked: %d", top->count_inline_bcs(), callee_method->code_size(), callee_method->interpreter_invocation_count());
  }
}

//------------------------------ok_to_inline-----------------------------------
WarmCallInfo* InlineTree::ok_to_inline(ciMethod* callee_method, JVMState* jvms, ciCallProfile& profile, WarmCallInfo* initial_wci) {
  assert(callee_method != NULL, "caller checks for optimized virtual!");
#ifdef ASSERT
  // Make sure the incoming jvms has the same information content as me.
  // This means that we can eventually make this whole class AllStatic.
  if (jvms->caller() == NULL) {
    assert(_caller_jvms == NULL, "redundant instance state");
  } else {
    assert(_caller_jvms->same_calls_as(jvms->caller()), "redundant instance state");
  }
  assert(_method == jvms->method(), "redundant instance state");
#endif
  const char *failure_msg   = NULL;
  int         caller_bci    = jvms->bci();
  ciMethod   *caller_method = jvms->method();

  if( !pass_initial_checks(caller_method, caller_bci, callee_method)) {
    if( PrintInlining ) {
      failure_msg = "failed_initial_checks";
      print_inlining( callee_method, caller_bci, failure_msg);
    }
    return NULL;
  }

  // Check if inlining policy says no.
  WarmCallInfo wci = *(initial_wci);
  failure_msg = try_to_inline(callee_method, caller_method, caller_bci, profile, &wci);
  if (failure_msg != NULL && C->log() != NULL) {
    C->log()->begin_elem("inline_fail reason='");
    C->log()->text("%s", failure_msg);
    C->log()->end_elem("'");
  }

#ifndef PRODUCT
  if (UseOldInlining && InlineWarmCalls
      && (PrintOpto || PrintOptoInlining || PrintInlining)) {
    bool cold = wci.is_cold();
    bool hot  = !cold && wci.is_hot();
    bool old_cold = (failure_msg != NULL);
    if (old_cold != cold || (Verbose || WizardMode)) {
      tty->print("   OldInlining= %4s : %s\n           WCI=",
                 old_cold ? "cold" : "hot", failure_msg ? failure_msg : "OK");
      wci.print();
    }
  }
#endif
  if (UseOldInlining) {
    if (failure_msg == NULL)
      wci = *(WarmCallInfo::always_hot());
    else
      wci = *(WarmCallInfo::always_cold());
  }
  if (!InlineWarmCalls) {
    if (!wci.is_cold() && !wci.is_hot()) {
      // Do not inline the warm calls.
      wci = *(WarmCallInfo::always_cold());
    }
  }

  if (!wci.is_cold()) {
    // In -UseOldInlining, the failure_msg may also be a success message.
    if (failure_msg == NULL)  failure_msg = "inline (hot)";

    // Inline!
    if( PrintInlining ) print_inlining( callee_method, caller_bci, failure_msg);
    if (UseOldInlining)
      build_inline_tree_for_callee(callee_method, jvms, caller_bci);
    if (InlineWarmCalls && !wci.is_hot())
      return new (C) WarmCallInfo(wci);  // copy to heap
    return WarmCallInfo::always_hot();
  }

  // Do not inline
  if (failure_msg == NULL)  failure_msg = "too cold to inline";
  if( PrintInlining ) print_inlining( callee_method, caller_bci, failure_msg);
  return NULL;
}

//------------------------------compute_callee_frequency-----------------------
float InlineTree::compute_callee_frequency( int caller_bci ) const {
  int count  = method()->interpreter_call_site_count(caller_bci);
  int invcnt = method()->interpreter_invocation_count();
  float freq = (float)count/(float)invcnt;
  // Call-site count / interpreter invocation count, scaled recursively.
  // Always between 0.0 and 1.0.  Represents the percentage of the method's
  // total execution time used at this call site.

  return freq;
}

//------------------------------build_inline_tree_for_callee-------------------
InlineTree *InlineTree::build_inline_tree_for_callee( ciMethod* callee_method, JVMState* caller_jvms, int caller_bci) {
  float recur_frequency = _site_invoke_ratio * compute_callee_frequency(caller_bci);
  // Attempt inlining.
  InlineTree* old_ilt = callee_at(caller_bci, callee_method);
  if (old_ilt != NULL) {
    return old_ilt;
  }
  int max_inline_level_adjust = 0;
  if (caller_jvms->method() != NULL) {
    if (caller_jvms->method()->is_method_handle_adapter())
      max_inline_level_adjust += 1;  // don't count actions in MH or indy adapter frames
    else if (callee_method->is_method_handle_invoke()) {
      max_inline_level_adjust += 1;  // don't count method handle calls from java.lang.invoke implem
    }
    if (max_inline_level_adjust != 0 && PrintInlining && (Verbose || WizardMode)) {
      CompileTask::print_inline_indent(inline_level());
      tty->print_cr(" \\-> discounting inline depth");
    }
    if (max_inline_level_adjust != 0 && C->log()) {
      int id1 = C->log()->identify(caller_jvms->method());
      int id2 = C->log()->identify(callee_method);
      C->log()->elem("inline_level_discount caller='%d' callee='%d'", id1, id2);
    }
  }
  InlineTree* ilt = new InlineTree(C, this, callee_method, caller_jvms, caller_bci, recur_frequency, _max_inline_level + max_inline_level_adjust);
  _subtrees.append(ilt);

  NOT_PRODUCT( _count_inlines += 1; )

  return ilt;
}


//---------------------------------------callee_at-----------------------------
InlineTree *InlineTree::callee_at(int bci, ciMethod* callee) const {
  for (int i = 0; i < _subtrees.length(); i++) {
    InlineTree* sub = _subtrees.at(i);
    if (sub->caller_bci() == bci && callee == sub->method()) {
      return sub;
    }
  }
  return NULL;
}


//------------------------------build_inline_tree_root-------------------------
InlineTree *InlineTree::build_inline_tree_root() {
  Compile* C = Compile::current();

  // Root of inline tree
  InlineTree* ilt = new InlineTree(C, NULL, C->method(), NULL, -1, 1.0F, MaxInlineLevel);

  return ilt;
}


//-------------------------find_subtree_from_root-----------------------------
// Given a jvms, which determines a call chain from the root method,
// find the corresponding inline tree.
// Note: This method will be removed or replaced as InlineTree goes away.
InlineTree* InlineTree::find_subtree_from_root(InlineTree* root, JVMState* jvms, ciMethod* callee, bool create_if_not_found) {
  InlineTree* iltp = root;
  uint depth = jvms && jvms->has_method() ? jvms->depth() : 0;
  for (uint d = 1; d <= depth; d++) {
    JVMState* jvmsp  = jvms->of_depth(d);
    // Select the corresponding subtree for this bci.
    assert(jvmsp->method() == iltp->method(), "tree still in sync");
    ciMethod* d_callee = (d == depth) ? callee : jvms->of_depth(d+1)->method();
    InlineTree* sub = iltp->callee_at(jvmsp->bci(), d_callee);
    if (!sub) {
      if (create_if_not_found && d == depth) {
        return iltp->build_inline_tree_for_callee(d_callee, jvmsp, jvmsp->bci());
      }
      assert(sub != NULL, "should be a sub-ilt here");
      return NULL;
    }
    iltp = sub;
  }
  return iltp;
}
