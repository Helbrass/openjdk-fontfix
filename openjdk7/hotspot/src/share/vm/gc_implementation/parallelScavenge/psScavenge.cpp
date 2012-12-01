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

#include "precompiled.hpp"
#include "classfile/symbolTable.hpp"
#include "gc_implementation/parallelScavenge/cardTableExtension.hpp"
#include "gc_implementation/parallelScavenge/gcTaskManager.hpp"
#include "gc_implementation/parallelScavenge/generationSizer.hpp"
#include "gc_implementation/parallelScavenge/parallelScavengeHeap.hpp"
#include "gc_implementation/parallelScavenge/psAdaptiveSizePolicy.hpp"
#include "gc_implementation/parallelScavenge/psMarkSweep.hpp"
#include "gc_implementation/parallelScavenge/psParallelCompact.hpp"
#include "gc_implementation/parallelScavenge/psScavenge.inline.hpp"
#include "gc_implementation/parallelScavenge/psTasks.hpp"
#include "gc_implementation/shared/isGCActiveMark.hpp"
#include "gc_implementation/shared/spaceDecorator.hpp"
#include "gc_interface/gcCause.hpp"
#include "memory/collectorPolicy.hpp"
#include "memory/gcLocker.inline.hpp"
#include "memory/referencePolicy.hpp"
#include "memory/referenceProcessor.hpp"
#include "memory/resourceArea.hpp"
#include "oops/oop.inline.hpp"
#include "oops/oop.psgc.inline.hpp"
#include "runtime/biasedLocking.hpp"
#include "runtime/fprofiler.hpp"
#include "runtime/handles.inline.hpp"
#include "runtime/threadCritical.hpp"
#include "runtime/vmThread.hpp"
#include "runtime/vm_operations.hpp"
#include "services/memoryService.hpp"
#include "utilities/stack.inline.hpp"


HeapWord*                  PSScavenge::_to_space_top_before_gc = NULL;
int                        PSScavenge::_consecutive_skipped_scavenges = 0;
ReferenceProcessor*        PSScavenge::_ref_processor = NULL;
CardTableExtension*        PSScavenge::_card_table = NULL;
bool                       PSScavenge::_survivor_overflow = false;
int                        PSScavenge::_tenuring_threshold = 0;
HeapWord*                  PSScavenge::_young_generation_boundary = NULL;
elapsedTimer               PSScavenge::_accumulated_time;
Stack<markOop>             PSScavenge::_preserved_mark_stack;
Stack<oop>                 PSScavenge::_preserved_oop_stack;
CollectorCounters*         PSScavenge::_counters = NULL;
bool                       PSScavenge::_promotion_failed = false;

// Define before use
class PSIsAliveClosure: public BoolObjectClosure {
public:
  void do_object(oop p) {
    assert(false, "Do not call.");
  }
  bool do_object_b(oop p) {
    return (!PSScavenge::is_obj_in_young((HeapWord*) p)) || p->is_forwarded();
  }
};

PSIsAliveClosure PSScavenge::_is_alive_closure;

class PSKeepAliveClosure: public OopClosure {
protected:
  MutableSpace* _to_space;
  PSPromotionManager* _promotion_manager;

public:
  PSKeepAliveClosure(PSPromotionManager* pm) : _promotion_manager(pm) {
    ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
    assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");
    _to_space = heap->young_gen()->to_space();

    assert(_promotion_manager != NULL, "Sanity");
  }

  template <class T> void do_oop_work(T* p) {
    assert (!oopDesc::is_null(*p), "expected non-null ref");
    assert ((oopDesc::load_decode_heap_oop_not_null(p))->is_oop(),
            "expected an oop while scanning weak refs");

    // Weak refs may be visited more than once.
    if (PSScavenge::should_scavenge(p, _to_space)) {
      PSScavenge::copy_and_push_safe_barrier(_promotion_manager, p);
    }
  }
  virtual void do_oop(oop* p)       { PSKeepAliveClosure::do_oop_work(p); }
  virtual void do_oop(narrowOop* p) { PSKeepAliveClosure::do_oop_work(p); }
};

class PSEvacuateFollowersClosure: public VoidClosure {
 private:
  PSPromotionManager* _promotion_manager;
 public:
  PSEvacuateFollowersClosure(PSPromotionManager* pm) : _promotion_manager(pm) {}

  virtual void do_void() {
    assert(_promotion_manager != NULL, "Sanity");
    _promotion_manager->drain_stacks(true);
    guarantee(_promotion_manager->stacks_empty(),
              "stacks should be empty at this point");
  }
};

class PSPromotionFailedClosure : public ObjectClosure {
  virtual void do_object(oop obj) {
    if (obj->is_forwarded()) {
      obj->init_mark();
    }
  }
};

class PSRefProcTaskProxy: public GCTask {
  typedef AbstractRefProcTaskExecutor::ProcessTask ProcessTask;
  ProcessTask & _rp_task;
  uint          _work_id;
public:
  PSRefProcTaskProxy(ProcessTask & rp_task, uint work_id)
    : _rp_task(rp_task),
      _work_id(work_id)
  { }

private:
  virtual char* name() { return (char *)"Process referents by policy in parallel"; }
  virtual void do_it(GCTaskManager* manager, uint which);
};

void PSRefProcTaskProxy::do_it(GCTaskManager* manager, uint which)
{
  PSPromotionManager* promotion_manager =
    PSPromotionManager::gc_thread_promotion_manager(which);
  assert(promotion_manager != NULL, "sanity check");
  PSKeepAliveClosure keep_alive(promotion_manager);
  PSEvacuateFollowersClosure evac_followers(promotion_manager);
  PSIsAliveClosure is_alive;
  _rp_task.work(_work_id, is_alive, keep_alive, evac_followers);
}

class PSRefEnqueueTaskProxy: public GCTask {
  typedef AbstractRefProcTaskExecutor::EnqueueTask EnqueueTask;
  EnqueueTask& _enq_task;
  uint         _work_id;

public:
  PSRefEnqueueTaskProxy(EnqueueTask& enq_task, uint work_id)
    : _enq_task(enq_task),
      _work_id(work_id)
  { }

  virtual char* name() { return (char *)"Enqueue reference objects in parallel"; }
  virtual void do_it(GCTaskManager* manager, uint which)
  {
    _enq_task.work(_work_id);
  }
};

class PSRefProcTaskExecutor: public AbstractRefProcTaskExecutor {
  virtual void execute(ProcessTask& task);
  virtual void execute(EnqueueTask& task);
};

void PSRefProcTaskExecutor::execute(ProcessTask& task)
{
  GCTaskQueue* q = GCTaskQueue::create();
  for(uint i=0; i<ParallelGCThreads; i++) {
    q->enqueue(new PSRefProcTaskProxy(task, i));
  }
  ParallelTaskTerminator terminator(
                 ParallelScavengeHeap::gc_task_manager()->workers(),
                 (TaskQueueSetSuper*) PSPromotionManager::stack_array_depth());
  if (task.marks_oops_alive() && ParallelGCThreads > 1) {
    for (uint j=0; j<ParallelGCThreads; j++) {
      q->enqueue(new StealTask(&terminator));
    }
  }
  ParallelScavengeHeap::gc_task_manager()->execute_and_wait(q);
}


void PSRefProcTaskExecutor::execute(EnqueueTask& task)
{
  GCTaskQueue* q = GCTaskQueue::create();
  for(uint i=0; i<ParallelGCThreads; i++) {
    q->enqueue(new PSRefEnqueueTaskProxy(task, i));
  }
  ParallelScavengeHeap::gc_task_manager()->execute_and_wait(q);
}

// This method contains all heap specific policy for invoking scavenge.
// PSScavenge::invoke_no_policy() will do nothing but attempt to
// scavenge. It will not clean up after failed promotions, bail out if
// we've exceeded policy time limits, or any other special behavior.
// All such policy should be placed here.
//
// Note that this method should only be called from the vm_thread while
// at a safepoint!
void PSScavenge::invoke() {
  assert(SafepointSynchronize::is_at_safepoint(), "should be at safepoint");
  assert(Thread::current() == (Thread*)VMThread::vm_thread(), "should be in vm thread");
  assert(!Universe::heap()->is_gc_active(), "not reentrant");

  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");

  PSAdaptiveSizePolicy* policy = heap->size_policy();
  IsGCActiveMark mark;

  bool scavenge_was_done = PSScavenge::invoke_no_policy();

  PSGCAdaptivePolicyCounters* counters = heap->gc_policy_counters();
  if (UsePerfData)
    counters->update_full_follows_scavenge(0);
  if (!scavenge_was_done ||
      policy->should_full_GC(heap->old_gen()->free_in_bytes())) {
    if (UsePerfData)
      counters->update_full_follows_scavenge(full_follows_scavenge);
    GCCauseSetter gccs(heap, GCCause::_adaptive_size_policy);
    CollectorPolicy* cp = heap->collector_policy();
    const bool clear_all_softrefs = cp->should_clear_all_soft_refs();

    if (UseParallelOldGC) {
      PSParallelCompact::invoke_no_policy(clear_all_softrefs);
    } else {
      PSMarkSweep::invoke_no_policy(clear_all_softrefs);
    }
  }
}

// This method contains no policy. You should probably
// be calling invoke() instead.
bool PSScavenge::invoke_no_policy() {
  assert(SafepointSynchronize::is_at_safepoint(), "should be at safepoint");
  assert(Thread::current() == (Thread*)VMThread::vm_thread(), "should be in vm thread");

  assert(_preserved_mark_stack.is_empty(), "should be empty");
  assert(_preserved_oop_stack.is_empty(), "should be empty");

  TimeStamp scavenge_entry;
  TimeStamp scavenge_midpoint;
  TimeStamp scavenge_exit;

  scavenge_entry.update();

  if (GC_locker::check_active_before_gc()) {
    return false;
  }

  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  GCCause::Cause gc_cause = heap->gc_cause();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");

  // Check for potential problems.
  if (!should_attempt_scavenge()) {
    return false;
  }

  bool promotion_failure_occurred = false;

  PSYoungGen* young_gen = heap->young_gen();
  PSOldGen* old_gen = heap->old_gen();
  PSPermGen* perm_gen = heap->perm_gen();
  PSAdaptiveSizePolicy* size_policy = heap->size_policy();
  heap->increment_total_collections();

  AdaptiveSizePolicyOutput(size_policy, heap->total_collections());

  if ((gc_cause != GCCause::_java_lang_system_gc) ||
       UseAdaptiveSizePolicyWithSystemGC) {
    // Gather the feedback data for eden occupancy.
    young_gen->eden_space()->accumulate_statistics();
  }

  if (ZapUnusedHeapArea) {
    // Save information needed to minimize mangling
    heap->record_gen_tops_before_GC();
  }

  if (PrintHeapAtGC) {
    Universe::print_heap_before_gc();
  }

  assert(!NeverTenure || _tenuring_threshold == markOopDesc::max_age + 1, "Sanity");
  assert(!AlwaysTenure || _tenuring_threshold == 0, "Sanity");

  size_t prev_used = heap->used();
  assert(promotion_failed() == false, "Sanity");

  // Fill in TLABs
  heap->accumulate_statistics_all_tlabs();
  heap->ensure_parsability(true);  // retire TLABs

  if (VerifyBeforeGC && heap->total_collections() >= VerifyGCStartAt) {
    HandleMark hm;  // Discard invalid handles created during verification
    gclog_or_tty->print(" VerifyBeforeGC:");
    Universe::verify(true);
  }

  {
    ResourceMark rm;
    HandleMark hm;

    gclog_or_tty->date_stamp(PrintGC && PrintGCDateStamps);
    TraceCPUTime tcpu(PrintGCDetails, true, gclog_or_tty);
    TraceTime t1("GC", PrintGC, !PrintGCDetails, gclog_or_tty);
    TraceCollectorStats tcs(counters());
    TraceMemoryManagerStats tms(false /* not full GC */,gc_cause);

    if (TraceGen0Time) accumulated_time()->start();

    // Let the size policy know we're starting
    size_policy->minor_collection_begin();

    // Verify the object start arrays.
    if (VerifyObjectStartArray &&
        VerifyBeforeGC) {
      old_gen->verify_object_start_array();
      perm_gen->verify_object_start_array();
    }

    // Verify no unmarked old->young roots
    if (VerifyRememberedSets) {
      CardTableExtension::verify_all_young_refs_imprecise();
    }

    if (!ScavengeWithObjectsInToSpace) {
      assert(young_gen->to_space()->is_empty(),
             "Attempt to scavenge with live objects in to_space");
      young_gen->to_space()->clear(SpaceDecorator::Mangle);
    } else if (ZapUnusedHeapArea) {
      young_gen->to_space()->mangle_unused_area();
    }
    save_to_space_top_before_gc();

    NOT_PRODUCT(reference_processor()->verify_no_references_recorded());
    COMPILER2_PRESENT(DerivedPointerTable::clear());

    reference_processor()->enable_discovery();
    reference_processor()->setup_policy(false);

    // We track how much was promoted to the next generation for
    // the AdaptiveSizePolicy.
    size_t old_gen_used_before = old_gen->used_in_bytes();

    // For PrintGCDetails
    size_t young_gen_used_before = young_gen->used_in_bytes();

    // Reset our survivor overflow.
    set_survivor_overflow(false);

    // We need to save the old/perm top values before
    // creating the promotion_manager. We pass the top
    // values to the card_table, to prevent it from
    // straying into the promotion labs.
    HeapWord* old_top = old_gen->object_space()->top();
    HeapWord* perm_top = perm_gen->object_space()->top();

    // Release all previously held resources
    gc_task_manager()->release_all_resources();

    PSPromotionManager::pre_scavenge();

    // We'll use the promotion manager again later.
    PSPromotionManager* promotion_manager = PSPromotionManager::vm_thread_promotion_manager();
    {
      // TraceTime("Roots");
      ParallelScavengeHeap::ParStrongRootsScope psrs;

      GCTaskQueue* q = GCTaskQueue::create();

      for(uint i=0; i<ParallelGCThreads; i++) {
        q->enqueue(new OldToYoungRootsTask(old_gen, old_top, i));
      }

      q->enqueue(new SerialOldToYoungRootsTask(perm_gen, perm_top));

      q->enqueue(new ScavengeRootsTask(ScavengeRootsTask::universe));
      q->enqueue(new ScavengeRootsTask(ScavengeRootsTask::jni_handles));
      // We scan the thread roots in parallel
      Threads::create_thread_roots_tasks(q);
      q->enqueue(new ScavengeRootsTask(ScavengeRootsTask::object_synchronizer));
      q->enqueue(new ScavengeRootsTask(ScavengeRootsTask::flat_profiler));
      q->enqueue(new ScavengeRootsTask(ScavengeRootsTask::management));
      q->enqueue(new ScavengeRootsTask(ScavengeRootsTask::system_dictionary));
      q->enqueue(new ScavengeRootsTask(ScavengeRootsTask::jvmti));
      q->enqueue(new ScavengeRootsTask(ScavengeRootsTask::code_cache));

      ParallelTaskTerminator terminator(
                  gc_task_manager()->workers(),
                  (TaskQueueSetSuper*) promotion_manager->stack_array_depth());
      if (ParallelGCThreads>1) {
        for (uint j=0; j<ParallelGCThreads; j++) {
          q->enqueue(new StealTask(&terminator));
        }
      }

      gc_task_manager()->execute_and_wait(q);
    }

    scavenge_midpoint.update();

    // Process reference objects discovered during scavenge
    {
      reference_processor()->setup_policy(false); // not always_clear
      PSKeepAliveClosure keep_alive(promotion_manager);
      PSEvacuateFollowersClosure evac_followers(promotion_manager);
      if (reference_processor()->processing_is_mt()) {
        PSRefProcTaskExecutor task_executor;
        reference_processor()->process_discovered_references(
          &_is_alive_closure, &keep_alive, &evac_followers, &task_executor);
      } else {
        reference_processor()->process_discovered_references(
          &_is_alive_closure, &keep_alive, &evac_followers, NULL);
      }
    }

    // Enqueue reference objects discovered during scavenge.
    if (reference_processor()->processing_is_mt()) {
      PSRefProcTaskExecutor task_executor;
      reference_processor()->enqueue_discovered_references(&task_executor);
    } else {
      reference_processor()->enqueue_discovered_references(NULL);
    }

    if (!JavaObjectsInPerm) {
      // Unlink any dead interned Strings
      StringTable::unlink(&_is_alive_closure);
      // Process the remaining live ones
      PSScavengeRootsClosure root_closure(promotion_manager);
      StringTable::oops_do(&root_closure);
    }

    // Finally, flush the promotion_manager's labs, and deallocate its stacks.
    PSPromotionManager::post_scavenge();

    promotion_failure_occurred = promotion_failed();
    if (promotion_failure_occurred) {
      clean_up_failed_promotion();
      if (PrintGC) {
        gclog_or_tty->print("--");
      }
    }

    // Let the size policy know we're done.  Note that we count promotion
    // failure cleanup time as part of the collection (otherwise, we're
    // implicitly saying it's mutator time).
    size_policy->minor_collection_end(gc_cause);

    if (!promotion_failure_occurred) {
      // Swap the survivor spaces.


      young_gen->eden_space()->clear(SpaceDecorator::Mangle);
      young_gen->from_space()->clear(SpaceDecorator::Mangle);
      young_gen->swap_spaces();

      size_t survived = young_gen->from_space()->used_in_bytes();
      size_t promoted = old_gen->used_in_bytes() - old_gen_used_before;
      size_policy->update_averages(_survivor_overflow, survived, promoted);

      // A successful scavenge should restart the GC time limit count which is
      // for full GC's.
      size_policy->reset_gc_overhead_limit_count();
      if (UseAdaptiveSizePolicy) {
        // Calculate the new survivor size and tenuring threshold

        if (PrintAdaptiveSizePolicy) {
          gclog_or_tty->print("AdaptiveSizeStart: ");
          gclog_or_tty->stamp();
          gclog_or_tty->print_cr(" collection: %d ",
                         heap->total_collections());

          if (Verbose) {
            gclog_or_tty->print("old_gen_capacity: %d young_gen_capacity: %d"
              " perm_gen_capacity: %d ",
              old_gen->capacity_in_bytes(), young_gen->capacity_in_bytes(),
              perm_gen->capacity_in_bytes());
          }
        }


        if (UsePerfData) {
          PSGCAdaptivePolicyCounters* counters = heap->gc_policy_counters();
          counters->update_old_eden_size(
            size_policy->calculated_eden_size_in_bytes());
          counters->update_old_promo_size(
            size_policy->calculated_promo_size_in_bytes());
          counters->update_old_capacity(old_gen->capacity_in_bytes());
          counters->update_young_capacity(young_gen->capacity_in_bytes());
          counters->update_survived(survived);
          counters->update_promoted(promoted);
          counters->update_survivor_overflowed(_survivor_overflow);
        }

        size_t survivor_limit =
          size_policy->max_survivor_size(young_gen->max_size());
        _tenuring_threshold =
          size_policy->compute_survivor_space_size_and_threshold(
                                                           _survivor_overflow,
                                                           _tenuring_threshold,
                                                           survivor_limit);

       if (PrintTenuringDistribution) {
         gclog_or_tty->cr();
         gclog_or_tty->print_cr("Desired survivor size %ld bytes, new threshold %d (max %d)",
                                size_policy->calculated_survivor_size_in_bytes(),
                                _tenuring_threshold, MaxTenuringThreshold);
       }

        if (UsePerfData) {
          PSGCAdaptivePolicyCounters* counters = heap->gc_policy_counters();
          counters->update_tenuring_threshold(_tenuring_threshold);
          counters->update_survivor_size_counters();
        }

        // Do call at minor collections?
        // Don't check if the size_policy is ready at this
        // level.  Let the size_policy check that internally.
        if (UseAdaptiveSizePolicy &&
            UseAdaptiveGenerationSizePolicyAtMinorCollection &&
            ((gc_cause != GCCause::_java_lang_system_gc) ||
              UseAdaptiveSizePolicyWithSystemGC)) {

          // Calculate optimial free space amounts
          assert(young_gen->max_size() >
            young_gen->from_space()->capacity_in_bytes() +
            young_gen->to_space()->capacity_in_bytes(),
            "Sizes of space in young gen are out-of-bounds");
          size_t max_eden_size = young_gen->max_size() -
            young_gen->from_space()->capacity_in_bytes() -
            young_gen->to_space()->capacity_in_bytes();
          size_policy->compute_generation_free_space(young_gen->used_in_bytes(),
                                   young_gen->eden_space()->used_in_bytes(),
                                   old_gen->used_in_bytes(),
                                   perm_gen->used_in_bytes(),
                                   young_gen->eden_space()->capacity_in_bytes(),
                                   old_gen->max_gen_size(),
                                   max_eden_size,
                                   false  /* full gc*/,
                                   gc_cause,
                                   heap->collector_policy());

        }
        // Resize the young generation at every collection
        // even if new sizes have not been calculated.  This is
        // to allow resizes that may have been inhibited by the
        // relative location of the "to" and "from" spaces.

        // Resizing the old gen at minor collects can cause increases
        // that don't feed back to the generation sizing policy until
        // a major collection.  Don't resize the old gen here.

        heap->resize_young_gen(size_policy->calculated_eden_size_in_bytes(),
                        size_policy->calculated_survivor_size_in_bytes());

        if (PrintAdaptiveSizePolicy) {
          gclog_or_tty->print_cr("AdaptiveSizeStop: collection: %d ",
                         heap->total_collections());
        }
      }

      // Update the structure of the eden. With NUMA-eden CPU hotplugging or offlining can
      // cause the change of the heap layout. Make sure eden is reshaped if that's the case.
      // Also update() will case adaptive NUMA chunk resizing.
      assert(young_gen->eden_space()->is_empty(), "eden space should be empty now");
      young_gen->eden_space()->update();

      heap->gc_policy_counters()->update_counters();

      heap->resize_all_tlabs();

      assert(young_gen->to_space()->is_empty(), "to space should be empty now");
    }

    COMPILER2_PRESENT(DerivedPointerTable::update_pointers());

    NOT_PRODUCT(reference_processor()->verify_no_references_recorded());

    // Re-verify object start arrays
    if (VerifyObjectStartArray &&
        VerifyAfterGC) {
      old_gen->verify_object_start_array();
      perm_gen->verify_object_start_array();
    }

    // Verify all old -> young cards are now precise
    if (VerifyRememberedSets) {
      // Precise verification will give false positives. Until this is fixed,
      // use imprecise verification.
      // CardTableExtension::verify_all_young_refs_precise();
      CardTableExtension::verify_all_young_refs_imprecise();
    }

    if (TraceGen0Time) accumulated_time()->stop();

    if (PrintGC) {
      if (PrintGCDetails) {
        // Don't print a GC timestamp here.  This is after the GC so
        // would be confusing.
        young_gen->print_used_change(young_gen_used_before);
      }
      heap->print_heap_change(prev_used);
    }

    // Track memory usage and detect low memory
    MemoryService::track_memory_usage();
    heap->update_counters();
  }

  if (VerifyAfterGC && heap->total_collections() >= VerifyGCStartAt) {
    HandleMark hm;  // Discard invalid handles created during verification
    gclog_or_tty->print(" VerifyAfterGC:");
    Universe::verify(false);
  }

  if (PrintHeapAtGC) {
    Universe::print_heap_after_gc();
  }

  if (ZapUnusedHeapArea) {
    young_gen->eden_space()->check_mangled_unused_area_complete();
    young_gen->from_space()->check_mangled_unused_area_complete();
    young_gen->to_space()->check_mangled_unused_area_complete();
  }

  scavenge_exit.update();

  if (PrintGCTaskTimeStamps) {
    tty->print_cr("VM-Thread " INT64_FORMAT " " INT64_FORMAT " " INT64_FORMAT,
                  scavenge_entry.ticks(), scavenge_midpoint.ticks(),
                  scavenge_exit.ticks());
    gc_task_manager()->print_task_time_stamps();
  }

#ifdef TRACESPINNING
  ParallelTaskTerminator::print_termination_counts();
#endif

  return !promotion_failure_occurred;
}

// This method iterates over all objects in the young generation,
// unforwarding markOops. It then restores any preserved mark oops,
// and clears the _preserved_mark_stack.
void PSScavenge::clean_up_failed_promotion() {
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");
  assert(promotion_failed(), "Sanity");

  PSYoungGen* young_gen = heap->young_gen();

  {
    ResourceMark rm;

    // Unforward all pointers in the young gen.
    PSPromotionFailedClosure unforward_closure;
    young_gen->object_iterate(&unforward_closure);

    if (PrintGC && Verbose) {
      gclog_or_tty->print_cr("Restoring %d marks", _preserved_oop_stack.size());
    }

    // Restore any saved marks.
    while (!_preserved_oop_stack.is_empty()) {
      oop obj      = _preserved_oop_stack.pop();
      markOop mark = _preserved_mark_stack.pop();
      obj->set_mark(mark);
    }

    // Clear the preserved mark and oop stack caches.
    _preserved_mark_stack.clear(true);
    _preserved_oop_stack.clear(true);
    _promotion_failed = false;
  }

  // Reset the PromotionFailureALot counters.
  NOT_PRODUCT(Universe::heap()->reset_promotion_should_fail();)
}

// This method is called whenever an attempt to promote an object
// fails. Some markOops will need preservation, some will not. Note
// that the entire eden is traversed after a failed promotion, with
// all forwarded headers replaced by the default markOop. This means
// it is not neccessary to preserve most markOops.
void PSScavenge::oop_promotion_failed(oop obj, markOop obj_mark) {
  _promotion_failed = true;
  if (obj_mark->must_be_preserved_for_promotion_failure(obj)) {
    // Should use per-worker private stakcs hetre rather than
    // locking a common pair of stacks.
    ThreadCritical tc;
    _preserved_oop_stack.push(obj);
    _preserved_mark_stack.push(obj_mark);
  }
}

bool PSScavenge::should_attempt_scavenge() {
  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");
  PSGCAdaptivePolicyCounters* counters = heap->gc_policy_counters();

  if (UsePerfData) {
    counters->update_scavenge_skipped(not_skipped);
  }

  PSYoungGen* young_gen = heap->young_gen();
  PSOldGen* old_gen = heap->old_gen();

  if (!ScavengeWithObjectsInToSpace) {
    // Do not attempt to promote unless to_space is empty
    if (!young_gen->to_space()->is_empty()) {
      _consecutive_skipped_scavenges++;
      if (UsePerfData) {
        counters->update_scavenge_skipped(to_space_not_empty);
      }
      return false;
    }
  }

  // Test to see if the scavenge will likely fail.
  PSAdaptiveSizePolicy* policy = heap->size_policy();

  // A similar test is done in the policy's should_full_GC().  If this is
  // changed, decide if that test should also be changed.
  size_t avg_promoted = (size_t) policy->padded_average_promoted_in_bytes();
  size_t promotion_estimate = MIN2(avg_promoted, young_gen->used_in_bytes());
  bool result = promotion_estimate < old_gen->free_in_bytes();

  if (PrintGCDetails && Verbose) {
    gclog_or_tty->print(result ? "  do scavenge: " : "  skip scavenge: ");
    gclog_or_tty->print_cr(" average_promoted " SIZE_FORMAT
      " padded_average_promoted " SIZE_FORMAT
      " free in old gen " SIZE_FORMAT,
      (size_t) policy->average_promoted_in_bytes(),
      (size_t) policy->padded_average_promoted_in_bytes(),
      old_gen->free_in_bytes());
    if (young_gen->used_in_bytes() <
        (size_t) policy->padded_average_promoted_in_bytes()) {
      gclog_or_tty->print_cr(" padded_promoted_average is greater"
        " than maximum promotion = " SIZE_FORMAT, young_gen->used_in_bytes());
    }
  }

  if (result) {
    _consecutive_skipped_scavenges = 0;
  } else {
    _consecutive_skipped_scavenges++;
    if (UsePerfData) {
      counters->update_scavenge_skipped(promoted_too_large);
    }
  }
  return result;
}

  // Used to add tasks
GCTaskManager* const PSScavenge::gc_task_manager() {
  assert(ParallelScavengeHeap::gc_task_manager() != NULL,
   "shouldn't return NULL");
  return ParallelScavengeHeap::gc_task_manager();
}

void PSScavenge::initialize() {
  // Arguments must have been parsed

  if (AlwaysTenure) {
    _tenuring_threshold = 0;
  } else if (NeverTenure) {
    _tenuring_threshold = markOopDesc::max_age + 1;
  } else {
    // We want to smooth out our startup times for the AdaptiveSizePolicy
    _tenuring_threshold = (UseAdaptiveSizePolicy) ? InitialTenuringThreshold :
                                                    MaxTenuringThreshold;
  }

  ParallelScavengeHeap* heap = (ParallelScavengeHeap*)Universe::heap();
  assert(heap->kind() == CollectedHeap::ParallelScavengeHeap, "Sanity");

  PSYoungGen* young_gen = heap->young_gen();
  PSOldGen* old_gen = heap->old_gen();
  PSPermGen* perm_gen = heap->perm_gen();

  // Set boundary between young_gen and old_gen
  assert(perm_gen->reserved().end() <= old_gen->object_space()->bottom(),
         "perm above old");
  assert(old_gen->reserved().end() <= young_gen->eden_space()->bottom(),
         "old above young");
  _young_generation_boundary = young_gen->eden_space()->bottom();

  // Initialize ref handling object for scavenging.
  MemRegion mr = young_gen->reserved();
  _ref_processor =
    new ReferenceProcessor(mr,                         // span
                           ParallelRefProcEnabled && (ParallelGCThreads > 1), // mt processing
                           (int) ParallelGCThreads,    // mt processing degree
                           true,                       // mt discovery
                           (int) ParallelGCThreads,    // mt discovery degree
                           true,                       // atomic_discovery
                           NULL,                       // header provides liveness info
                           false);                     // next field updates do not need write barrier

  // Cache the cardtable
  BarrierSet* bs = Universe::heap()->barrier_set();
  assert(bs->kind() == BarrierSet::CardTableModRef, "Wrong barrier set kind");
  _card_table = (CardTableExtension*)bs;

  _counters = new CollectorCounters("PSScavenge", 0);
}
