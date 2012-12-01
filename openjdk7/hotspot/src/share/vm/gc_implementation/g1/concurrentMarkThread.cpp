/*
 * Copyright (c) 2001, 2011, Oracle and/or its affiliates. All rights reserved.
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
#include "gc_implementation/g1/concurrentMarkThread.inline.hpp"
#include "gc_implementation/g1/g1CollectedHeap.inline.hpp"
#include "gc_implementation/g1/g1CollectorPolicy.hpp"
#include "gc_implementation/g1/g1MMUTracker.hpp"
#include "gc_implementation/g1/vm_operations_g1.hpp"
#include "memory/resourceArea.hpp"
#include "runtime/vmThread.hpp"

// ======= Concurrent Mark Thread ========

// The CM thread is created when the G1 garbage collector is used

SurrogateLockerThread*
     ConcurrentMarkThread::_slt = NULL;

ConcurrentMarkThread::ConcurrentMarkThread(ConcurrentMark* cm) :
  ConcurrentGCThread(),
  _cm(cm),
  _started(false),
  _in_progress(false),
  _vtime_accum(0.0),
  _vtime_mark_accum(0.0),
  _vtime_count_accum(0.0)
{
  create_and_start();
}

class CMCheckpointRootsInitialClosure: public VoidClosure {

  ConcurrentMark* _cm;
public:

  CMCheckpointRootsInitialClosure(ConcurrentMark* cm) :
    _cm(cm) {}

  void do_void(){
    _cm->checkpointRootsInitial();
  }
};

class CMCheckpointRootsFinalClosure: public VoidClosure {

  ConcurrentMark* _cm;
public:

  CMCheckpointRootsFinalClosure(ConcurrentMark* cm) :
    _cm(cm) {}

  void do_void(){
    _cm->checkpointRootsFinal(false); // !clear_all_soft_refs
  }
};

class CMCleanUp: public VoidClosure {
  ConcurrentMark* _cm;
public:

  CMCleanUp(ConcurrentMark* cm) :
    _cm(cm) {}

  void do_void(){
    _cm->cleanup();
  }
};



void ConcurrentMarkThread::run() {
  initialize_in_thread();
  _vtime_start = os::elapsedVTime();
  wait_for_universe_init();

  G1CollectedHeap* g1h = G1CollectedHeap::heap();
  G1CollectorPolicy* g1_policy = g1h->g1_policy();
  G1MMUTracker *mmu_tracker = g1_policy->mmu_tracker();
  Thread *current_thread = Thread::current();

  while (!_should_terminate) {
    // wait until started is set.
    sleepBeforeNextCycle();
    {
      ResourceMark rm;
      HandleMark   hm;
      double cycle_start = os::elapsedVTime();
      double mark_start_sec = os::elapsedTime();
      char verbose_str[128];

      if (PrintGC) {
        gclog_or_tty->date_stamp(PrintGCDateStamps);
        gclog_or_tty->stamp(PrintGCTimeStamps);
        gclog_or_tty->print_cr("[GC concurrent-mark-start]");
      }

      if (!g1_policy->in_young_gc_mode()) {
        // this ensures the flag is not set if we bail out of the marking
        // cycle; normally the flag is cleared immediately after cleanup
        g1h->set_marking_complete();

        if (g1_policy->adaptive_young_list_length()) {
          double now = os::elapsedTime();
          double init_prediction_ms = g1_policy->predict_init_time_ms();
          jlong sleep_time_ms = mmu_tracker->when_ms(now, init_prediction_ms);
          os::sleep(current_thread, sleep_time_ms, false);
        }

        // We don't have to skip here if we've been asked to restart, because
        // in the worst case we just enqueue a new VM operation to start a
        // marking.  Note that the init operation resets has_aborted()
        CMCheckpointRootsInitialClosure init_cl(_cm);
        strcpy(verbose_str, "GC initial-mark");
        VM_CGC_Operation op(&init_cl, verbose_str);
        VMThread::execute(&op);
      }

      int iter = 0;
      do {
        iter++;
        if (!cm()->has_aborted()) {
          _cm->markFromRoots();
        }

        double mark_end_time = os::elapsedVTime();
        double mark_end_sec = os::elapsedTime();
        _vtime_mark_accum += (mark_end_time - cycle_start);
        if (!cm()->has_aborted()) {
          if (g1_policy->adaptive_young_list_length()) {
            double now = os::elapsedTime();
            double remark_prediction_ms = g1_policy->predict_remark_time_ms();
            jlong sleep_time_ms = mmu_tracker->when_ms(now, remark_prediction_ms);
            os::sleep(current_thread, sleep_time_ms, false);
          }

          if (PrintGC) {
            gclog_or_tty->date_stamp(PrintGCDateStamps);
            gclog_or_tty->stamp(PrintGCTimeStamps);
            gclog_or_tty->print_cr("[GC concurrent-mark-end, %1.7lf sec]",
                                      mark_end_sec - mark_start_sec);
          }

          CMCheckpointRootsFinalClosure final_cl(_cm);
          sprintf(verbose_str, "GC remark");
          VM_CGC_Operation op(&final_cl, verbose_str);
          VMThread::execute(&op);
        }
        if (cm()->restart_for_overflow() &&
            G1TraceMarkStackOverflow) {
          gclog_or_tty->print_cr("Restarting conc marking because of MS overflow "
                                 "in remark (restart #%d).", iter);
        }

        if (cm()->restart_for_overflow()) {
          if (PrintGC) {
            gclog_or_tty->date_stamp(PrintGCDateStamps);
            gclog_or_tty->stamp(PrintGCTimeStamps);
            gclog_or_tty->print_cr("[GC concurrent-mark-restart-for-overflow]");
          }
        }
      } while (cm()->restart_for_overflow());
      double counting_start_time = os::elapsedVTime();

      // YSR: These look dubious (i.e. redundant) !!! FIX ME
      slt()->manipulatePLL(SurrogateLockerThread::acquirePLL);
      slt()->manipulatePLL(SurrogateLockerThread::releaseAndNotifyPLL);

      if (!cm()->has_aborted()) {
        double count_start_sec = os::elapsedTime();
        if (PrintGC) {
          gclog_or_tty->date_stamp(PrintGCDateStamps);
          gclog_or_tty->stamp(PrintGCTimeStamps);
          gclog_or_tty->print_cr("[GC concurrent-count-start]");
        }

        _sts.join();
        _cm->calcDesiredRegions();
        _sts.leave();

        if (!cm()->has_aborted()) {
          double count_end_sec = os::elapsedTime();
          if (PrintGC) {
            gclog_or_tty->date_stamp(PrintGCDateStamps);
            gclog_or_tty->stamp(PrintGCTimeStamps);
            gclog_or_tty->print_cr("[GC concurrent-count-end, %1.7lf]",
                                   count_end_sec - count_start_sec);
          }
        }
      }
      double end_time = os::elapsedVTime();
      _vtime_count_accum += (end_time - counting_start_time);
      // Update the total virtual time before doing this, since it will try
      // to measure it to get the vtime for this marking.  We purposely
      // neglect the presumably-short "completeCleanup" phase here.
      _vtime_accum = (end_time - _vtime_start);
      if (!cm()->has_aborted()) {
        if (g1_policy->adaptive_young_list_length()) {
          double now = os::elapsedTime();
          double cleanup_prediction_ms = g1_policy->predict_cleanup_time_ms();
          jlong sleep_time_ms = mmu_tracker->when_ms(now, cleanup_prediction_ms);
          os::sleep(current_thread, sleep_time_ms, false);
        }

        CMCleanUp cl_cl(_cm);
        sprintf(verbose_str, "GC cleanup");
        VM_CGC_Operation op(&cl_cl, verbose_str);
        VMThread::execute(&op);
      } else {
        g1h->set_marking_complete();
      }

      // Check if cleanup set the free_regions_coming flag. If it
      // hasn't, we can just skip the next step.
      if (g1h->free_regions_coming()) {
        // The following will finish freeing up any regions that we
        // found to be empty during cleanup. We'll do this part
        // without joining the suspendible set. If an evacuation pause
        // takes place, then we would carry on freeing regions in
        // case they are needed by the pause. If a Full GC takes
        // place, it would wait for us to process the regions
        // reclaimed by cleanup.

        double cleanup_start_sec = os::elapsedTime();
        if (PrintGC) {
          gclog_or_tty->date_stamp(PrintGCDateStamps);
          gclog_or_tty->stamp(PrintGCTimeStamps);
          gclog_or_tty->print_cr("[GC concurrent-cleanup-start]");
        }

        // Now do the remainder of the cleanup operation.
        _cm->completeCleanup();
        // Notify anyone who's waiting that there are no more free
        // regions coming. We have to do this before we join the STS,
        // otherwise we might deadlock: a GC worker could be blocked
        // waiting for the notification whereas this thread will be
        // blocked for the pause to finish while it's trying to join
        // the STS, which is conditional on the GC workers finishing.
        g1h->reset_free_regions_coming();

        _sts.join();
        g1_policy->record_concurrent_mark_cleanup_completed();
        _sts.leave();

        double cleanup_end_sec = os::elapsedTime();
        if (PrintGC) {
          gclog_or_tty->date_stamp(PrintGCDateStamps);
          gclog_or_tty->stamp(PrintGCTimeStamps);
          gclog_or_tty->print_cr("[GC concurrent-cleanup-end, %1.7lf]",
                                 cleanup_end_sec - cleanup_start_sec);
        }
      }
      guarantee(cm()->cleanup_list_is_empty(),
                "at this point there should be no regions on the cleanup list");

      if (cm()->has_aborted()) {
        if (PrintGC) {
          gclog_or_tty->date_stamp(PrintGCDateStamps);
          gclog_or_tty->stamp(PrintGCTimeStamps);
          gclog_or_tty->print_cr("[GC concurrent-mark-abort]");
        }
      }

      // we now want to allow clearing of the marking bitmap to be
      // suspended by a collection pause.
      _sts.join();
      _cm->clearNextBitmap();
      _sts.leave();
    }

    // Update the number of full collections that have been
    // completed. This will also notify the FullGCCount_lock in case a
    // Java thread is waiting for a full GC to happen (e.g., it
    // called System.gc() with +ExplicitGCInvokesConcurrent).
    _sts.join();
    g1h->increment_full_collections_completed(true /* concurrent */);
    _sts.leave();
  }
  assert(_should_terminate, "just checking");

  terminate();
}


void ConcurrentMarkThread::yield() {
  _sts.yield("Concurrent Mark");
}

void ConcurrentMarkThread::stop() {
  // it is ok to take late safepoints here, if needed
  MutexLockerEx mu(Terminator_lock);
  _should_terminate = true;
  while (!_has_terminated) {
    Terminator_lock->wait();
  }
}

void ConcurrentMarkThread::print() const {
  print_on(tty);
}

void ConcurrentMarkThread::print_on(outputStream* st) const {
  st->print("\"G1 Main Concurrent Mark GC Thread\" ");
  Thread::print_on(st);
  st->cr();
}

void ConcurrentMarkThread::sleepBeforeNextCycle() {
  // We join here because we don't want to do the "shouldConcurrentMark()"
  // below while the world is otherwise stopped.
  assert(!in_progress(), "should have been cleared");

  MutexLockerEx x(CGC_lock, Mutex::_no_safepoint_check_flag);
  while (!started()) {
    CGC_lock->wait(Mutex::_no_safepoint_check_flag);
  }
  set_in_progress();
  clear_started();
}

// Note: this method, although exported by the ConcurrentMarkSweepThread,
// which is a non-JavaThread, can only be called by a JavaThread.
// Currently this is done at vm creation time (post-vm-init) by the
// main/Primordial (Java)Thread.
// XXX Consider changing this in the future to allow the CMS thread
// itself to create this thread?
void ConcurrentMarkThread::makeSurrogateLockerThread(TRAPS) {
  assert(_slt == NULL, "SLT already created");
  _slt = SurrogateLockerThread::make(THREAD);
}
