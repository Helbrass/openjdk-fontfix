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

#ifndef SHARE_VM_GC_IMPLEMENTATION_G1_HEAPREGION_HPP
#define SHARE_VM_GC_IMPLEMENTATION_G1_HEAPREGION_HPP

#include "gc_implementation/g1/g1BlockOffsetTable.inline.hpp"
#include "gc_implementation/g1/g1_specialized_oop_closures.hpp"
#include "gc_implementation/g1/survRateGroup.hpp"
#include "gc_implementation/shared/ageTable.hpp"
#include "gc_implementation/shared/spaceDecorator.hpp"
#include "memory/space.inline.hpp"
#include "memory/watermark.hpp"

#ifndef SERIALGC

// A HeapRegion is the smallest piece of a G1CollectedHeap that
// can be collected independently.

// NOTE: Although a HeapRegion is a Space, its
// Space::initDirtyCardClosure method must not be called.
// The problem is that the existence of this method breaks
// the independence of barrier sets from remembered sets.
// The solution is to remove this method from the definition
// of a Space.

class CompactibleSpace;
class ContiguousSpace;
class HeapRegionRemSet;
class HeapRegionRemSetIterator;
class HeapRegion;
class HeapRegionSetBase;

#define HR_FORMAT "%d:["PTR_FORMAT","PTR_FORMAT","PTR_FORMAT"]"
#define HR_FORMAT_PARAMS(_hr_) (_hr_)->hrs_index(), (_hr_)->bottom(), \
                               (_hr_)->top(), (_hr_)->end()

// A dirty card to oop closure for heap regions. It
// knows how to get the G1 heap and how to use the bitmap
// in the concurrent marker used by G1 to filter remembered
// sets.

class HeapRegionDCTOC : public ContiguousSpaceDCTOC {
public:
  // Specification of possible DirtyCardToOopClosure filtering.
  enum FilterKind {
    NoFilterKind,
    IntoCSFilterKind,
    OutOfRegionFilterKind
  };

protected:
  HeapRegion* _hr;
  FilterKind _fk;
  G1CollectedHeap* _g1;

  void walk_mem_region_with_cl(MemRegion mr,
                               HeapWord* bottom, HeapWord* top,
                               OopClosure* cl);

  // We don't specialize this for FilteringClosure; filtering is handled by
  // the "FilterKind" mechanism.  But we provide this to avoid a compiler
  // warning.
  void walk_mem_region_with_cl(MemRegion mr,
                               HeapWord* bottom, HeapWord* top,
                               FilteringClosure* cl) {
    HeapRegionDCTOC::walk_mem_region_with_cl(mr, bottom, top,
                                                       (OopClosure*)cl);
  }

  // Get the actual top of the area on which the closure will
  // operate, given where the top is assumed to be (the end of the
  // memory region passed to do_MemRegion) and where the object
  // at the top is assumed to start. For example, an object may
  // start at the top but actually extend past the assumed top,
  // in which case the top becomes the end of the object.
  HeapWord* get_actual_top(HeapWord* top, HeapWord* top_obj) {
    return ContiguousSpaceDCTOC::get_actual_top(top, top_obj);
  }

  // Walk the given memory region from bottom to (actual) top
  // looking for objects and applying the oop closure (_cl) to
  // them. The base implementation of this treats the area as
  // blocks, where a block may or may not be an object. Sub-
  // classes should override this to provide more accurate
  // or possibly more efficient walking.
  void walk_mem_region(MemRegion mr, HeapWord* bottom, HeapWord* top) {
    Filtering_DCTOC::walk_mem_region(mr, bottom, top);
  }

public:
  HeapRegionDCTOC(G1CollectedHeap* g1,
                  HeapRegion* hr, OopClosure* cl,
                  CardTableModRefBS::PrecisionStyle precision,
                  FilterKind fk);
};


// The complicating factor is that BlockOffsetTable diverged
// significantly, and we need functionality that is only in the G1 version.
// So I copied that code, which led to an alternate G1 version of
// OffsetTableContigSpace.  If the two versions of BlockOffsetTable could
// be reconciled, then G1OffsetTableContigSpace could go away.

// The idea behind time stamps is the following. Doing a save_marks on
// all regions at every GC pause is time consuming (if I remember
// well, 10ms or so). So, we would like to do that only for regions
// that are GC alloc regions. To achieve this, we use time
// stamps. For every evacuation pause, G1CollectedHeap generates a
// unique time stamp (essentially a counter that gets
// incremented). Every time we want to call save_marks on a region,
// we set the saved_mark_word to top and also copy the current GC
// time stamp to the time stamp field of the space. Reading the
// saved_mark_word involves checking the time stamp of the
// region. If it is the same as the current GC time stamp, then we
// can safely read the saved_mark_word field, as it is valid. If the
// time stamp of the region is not the same as the current GC time
// stamp, then we instead read top, as the saved_mark_word field is
// invalid. Time stamps (on the regions and also on the
// G1CollectedHeap) are reset at every cleanup (we iterate over
// the regions anyway) and at the end of a Full GC. The current scheme
// that uses sequential unsigned ints will fail only if we have 4b
// evacuation pauses between two cleanups, which is _highly_ unlikely.

class G1OffsetTableContigSpace: public ContiguousSpace {
  friend class VMStructs;
 protected:
  G1BlockOffsetArrayContigSpace _offsets;
  Mutex _par_alloc_lock;
  volatile unsigned _gc_time_stamp;
  // When we need to retire an allocation region, while other threads
  // are also concurrently trying to allocate into it, we typically
  // allocate a dummy object at the end of the region to ensure that
  // no more allocations can take place in it. However, sometimes we
  // want to know where the end of the last "real" object we allocated
  // into the region was and this is what this keeps track.
  HeapWord* _pre_dummy_top;

 public:
  // Constructor.  If "is_zeroed" is true, the MemRegion "mr" may be
  // assumed to contain zeros.
  G1OffsetTableContigSpace(G1BlockOffsetSharedArray* sharedOffsetArray,
                           MemRegion mr, bool is_zeroed = false);

  void set_bottom(HeapWord* value);
  void set_end(HeapWord* value);

  virtual HeapWord* saved_mark_word() const;
  virtual void set_saved_mark();
  void reset_gc_time_stamp() { _gc_time_stamp = 0; }

  // See the comment above in the declaration of _pre_dummy_top for an
  // explanation of what it is.
  void set_pre_dummy_top(HeapWord* pre_dummy_top) {
    assert(is_in(pre_dummy_top) && pre_dummy_top <= top(), "pre-condition");
    _pre_dummy_top = pre_dummy_top;
  }
  HeapWord* pre_dummy_top() {
    return (_pre_dummy_top == NULL) ? top() : _pre_dummy_top;
  }
  void reset_pre_dummy_top() { _pre_dummy_top = NULL; }

  virtual void initialize(MemRegion mr, bool clear_space, bool mangle_space);
  virtual void clear(bool mangle_space);

  HeapWord* block_start(const void* p);
  HeapWord* block_start_const(const void* p) const;

  // Add offset table update.
  virtual HeapWord* allocate(size_t word_size);
  HeapWord* par_allocate(size_t word_size);

  // MarkSweep support phase3
  virtual HeapWord* initialize_threshold();
  virtual HeapWord* cross_threshold(HeapWord* start, HeapWord* end);

  virtual void print() const;

  void reset_bot() {
    _offsets.zero_bottom_entry();
    _offsets.initialize_threshold();
  }

  void update_bot_for_object(HeapWord* start, size_t word_size) {
    _offsets.alloc_block(start, word_size);
  }

  void print_bot_on(outputStream* out) {
    _offsets.print_on(out);
  }
};

class HeapRegion: public G1OffsetTableContigSpace {
  friend class VMStructs;
 private:

  enum HumongousType {
    NotHumongous = 0,
    StartsHumongous,
    ContinuesHumongous
  };

  // The next filter kind that should be used for a "new_dcto_cl" call with
  // the "traditional" signature.
  HeapRegionDCTOC::FilterKind _next_fk;

  // Requires that the region "mr" be dense with objects, and begin and end
  // with an object.
  void oops_in_mr_iterate(MemRegion mr, OopClosure* cl);

  // The remembered set for this region.
  // (Might want to make this "inline" later, to avoid some alloc failure
  // issues.)
  HeapRegionRemSet* _rem_set;

  G1BlockOffsetArrayContigSpace* offsets() { return &_offsets; }

 protected:
  // If this region is a member of a HeapRegionSeq, the index in that
  // sequence, otherwise -1.
  int  _hrs_index;

  HumongousType _humongous_type;
  // For a humongous region, region in which it starts.
  HeapRegion* _humongous_start_region;
  // For the start region of a humongous sequence, it's original end().
  HeapWord* _orig_end;

  // True iff the region is in current collection_set.
  bool _in_collection_set;

  // Is this or has it been an allocation region in the current collection
  // pause.
  bool _is_gc_alloc_region;

  // True iff an attempt to evacuate an object in the region failed.
  bool _evacuation_failed;

  // A heap region may be a member one of a number of special subsets, each
  // represented as linked lists through the field below.  Currently, these
  // sets include:
  //   The collection set.
  //   The set of allocation regions used in a collection pause.
  //   Spaces that may contain gray objects.
  HeapRegion* _next_in_special_set;

  // next region in the young "generation" region set
  HeapRegion* _next_young_region;

  // Next region whose cards need cleaning
  HeapRegion* _next_dirty_cards_region;

  // Fields used by the HeapRegionSetBase class and subclasses.
  HeapRegion* _next;
#ifdef ASSERT
  HeapRegionSetBase* _containing_set;
#endif // ASSERT
  bool _pending_removal;

  // For parallel heapRegion traversal.
  jint _claimed;

  // We use concurrent marking to determine the amount of live data
  // in each heap region.
  size_t _prev_marked_bytes;    // Bytes known to be live via last completed marking.
  size_t _next_marked_bytes;    // Bytes known to be live via in-progress marking.

  // See "sort_index" method.  -1 means is not in the array.
  int _sort_index;

  // <PREDICTION>
  double _gc_efficiency;
  // </PREDICTION>

  enum YoungType {
    NotYoung,                   // a region is not young
    Young,                      // a region is young
    Survivor                    // a region is young and it contains
                                // survivor
  };

  volatile YoungType _young_type;
  int  _young_index_in_cset;
  SurvRateGroup* _surv_rate_group;
  int  _age_index;

  // The start of the unmarked area. The unmarked area extends from this
  // word until the top and/or end of the region, and is the part
  // of the region for which no marking was done, i.e. objects may
  // have been allocated in this part since the last mark phase.
  // "prev" is the top at the start of the last completed marking.
  // "next" is the top at the start of the in-progress marking (if any.)
  HeapWord* _prev_top_at_mark_start;
  HeapWord* _next_top_at_mark_start;
  // If a collection pause is in progress, this is the top at the start
  // of that pause.

  // We've counted the marked bytes of objects below here.
  HeapWord* _top_at_conc_mark_count;

  void init_top_at_mark_start() {
    assert(_prev_marked_bytes == 0 &&
           _next_marked_bytes == 0,
           "Must be called after zero_marked_bytes.");
    HeapWord* bot = bottom();
    _prev_top_at_mark_start = bot;
    _next_top_at_mark_start = bot;
    _top_at_conc_mark_count = bot;
  }

  void set_young_type(YoungType new_type) {
    //assert(_young_type != new_type, "setting the same type" );
    // TODO: add more assertions here
    _young_type = new_type;
  }

  // Cached attributes used in the collection set policy information

  // The RSet length that was added to the total value
  // for the collection set.
  size_t _recorded_rs_length;

  // The predicted elapsed time that was added to total value
  // for the collection set.
  double _predicted_elapsed_time_ms;

  // The predicted number of bytes to copy that was added to
  // the total value for the collection set.
  size_t _predicted_bytes_to_copy;

 public:
  // If "is_zeroed" is "true", the region "mr" can be assumed to contain zeros.
  HeapRegion(G1BlockOffsetSharedArray* sharedOffsetArray,
             MemRegion mr, bool is_zeroed);

  static int LogOfHRGrainBytes;
  static int LogOfHRGrainWords;
  // The normal type of these should be size_t. However, they used to
  // be members of an enum before and they are assumed by the
  // compilers to be ints. To avoid going and fixing all their uses,
  // I'm declaring them as ints. I'm not anticipating heap region
  // sizes to reach anywhere near 2g, so using an int here is safe.
  static int GrainBytes;
  static int GrainWords;
  static int CardsPerRegion;

  // It sets up the heap region size (GrainBytes / GrainWords), as
  // well as other related fields that are based on the heap region
  // size (LogOfHRGrainBytes / LogOfHRGrainWords /
  // CardsPerRegion). All those fields are considered constant
  // throughout the JVM's execution, therefore they should only be set
  // up once during initialization time.
  static void setup_heap_region_size(uintx min_heap_size);

  enum ClaimValues {
    InitialClaimValue     = 0,
    FinalCountClaimValue  = 1,
    NoteEndClaimValue     = 2,
    ScrubRemSetClaimValue = 3,
    ParVerifyClaimValue   = 4,
    RebuildRSClaimValue   = 5
  };

  inline HeapWord* par_allocate_no_bot_updates(size_t word_size) {
    assert(is_young(), "we can only skip BOT updates on young regions");
    return ContiguousSpace::par_allocate(word_size);
  }
  inline HeapWord* allocate_no_bot_updates(size_t word_size) {
    assert(is_young(), "we can only skip BOT updates on young regions");
    return ContiguousSpace::allocate(word_size);
  }

  // If this region is a member of a HeapRegionSeq, the index in that
  // sequence, otherwise -1.
  int hrs_index() const { return _hrs_index; }
  void set_hrs_index(int index) { _hrs_index = index; }

  // The number of bytes marked live in the region in the last marking phase.
  size_t marked_bytes()    { return _prev_marked_bytes; }
  size_t live_bytes() {
    return (top() - prev_top_at_mark_start()) * HeapWordSize + marked_bytes();
  }

  // The number of bytes counted in the next marking.
  size_t next_marked_bytes() { return _next_marked_bytes; }
  // The number of bytes live wrt the next marking.
  size_t next_live_bytes() {
    return
      (top() - next_top_at_mark_start()) * HeapWordSize + next_marked_bytes();
  }

  // A lower bound on the amount of garbage bytes in the region.
  size_t garbage_bytes() {
    size_t used_at_mark_start_bytes =
      (prev_top_at_mark_start() - bottom()) * HeapWordSize;
    assert(used_at_mark_start_bytes >= marked_bytes(),
           "Can't mark more than we have.");
    return used_at_mark_start_bytes - marked_bytes();
  }

  // An upper bound on the number of live bytes in the region.
  size_t max_live_bytes() { return used() - garbage_bytes(); }

  void add_to_marked_bytes(size_t incr_bytes) {
    _next_marked_bytes = _next_marked_bytes + incr_bytes;
    guarantee( _next_marked_bytes <= used(), "invariant" );
  }

  void zero_marked_bytes()      {
    _prev_marked_bytes = _next_marked_bytes = 0;
  }

  bool isHumongous() const { return _humongous_type != NotHumongous; }
  bool startsHumongous() const { return _humongous_type == StartsHumongous; }
  bool continuesHumongous() const { return _humongous_type == ContinuesHumongous; }
  // For a humongous region, region in which it starts.
  HeapRegion* humongous_start_region() const {
    return _humongous_start_region;
  }

  // Makes the current region be a "starts humongous" region, i.e.,
  // the first region in a series of one or more contiguous regions
  // that will contain a single "humongous" object. The two parameters
  // are as follows:
  //
  // new_top : The new value of the top field of this region which
  // points to the end of the humongous object that's being
  // allocated. If there is more than one region in the series, top
  // will lie beyond this region's original end field and on the last
  // region in the series.
  //
  // new_end : The new value of the end field of this region which
  // points to the end of the last region in the series. If there is
  // one region in the series (namely: this one) end will be the same
  // as the original end of this region.
  //
  // Updating top and end as described above makes this region look as
  // if it spans the entire space taken up by all the regions in the
  // series and an single allocation moved its top to new_top. This
  // ensures that the space (capacity / allocated) taken up by all
  // humongous regions can be calculated by just looking at the
  // "starts humongous" regions and by ignoring the "continues
  // humongous" regions.
  void set_startsHumongous(HeapWord* new_top, HeapWord* new_end);

  // Makes the current region be a "continues humongous'
  // region. first_hr is the "start humongous" region of the series
  // which this region will be part of.
  void set_continuesHumongous(HeapRegion* first_hr);

  // Unsets the humongous-related fields on the region.
  void set_notHumongous();

  // If the region has a remembered set, return a pointer to it.
  HeapRegionRemSet* rem_set() const {
    return _rem_set;
  }

  // True iff the region is in current collection_set.
  bool in_collection_set() const {
    return _in_collection_set;
  }
  void set_in_collection_set(bool b) {
    _in_collection_set = b;
  }
  HeapRegion* next_in_collection_set() {
    assert(in_collection_set(), "should only invoke on member of CS.");
    assert(_next_in_special_set == NULL ||
           _next_in_special_set->in_collection_set(),
           "Malformed CS.");
    return _next_in_special_set;
  }
  void set_next_in_collection_set(HeapRegion* r) {
    assert(in_collection_set(), "should only invoke on member of CS.");
    assert(r == NULL || r->in_collection_set(), "Malformed CS.");
    _next_in_special_set = r;
  }

  // True iff it is or has been an allocation region in the current
  // collection pause.
  bool is_gc_alloc_region() const {
    return _is_gc_alloc_region;
  }
  void set_is_gc_alloc_region(bool b) {
    _is_gc_alloc_region = b;
  }
  HeapRegion* next_gc_alloc_region() {
    assert(is_gc_alloc_region(), "should only invoke on member of CS.");
    assert(_next_in_special_set == NULL ||
           _next_in_special_set->is_gc_alloc_region(),
           "Malformed CS.");
    return _next_in_special_set;
  }
  void set_next_gc_alloc_region(HeapRegion* r) {
    assert(is_gc_alloc_region(), "should only invoke on member of CS.");
    assert(r == NULL || r->is_gc_alloc_region(), "Malformed CS.");
    _next_in_special_set = r;
  }

  // Methods used by the HeapRegionSetBase class and subclasses.

  // Getter and setter for the next field used to link regions into
  // linked lists.
  HeapRegion* next()              { return _next; }

  void set_next(HeapRegion* next) { _next = next; }

  // Every region added to a set is tagged with a reference to that
  // set. This is used for doing consistency checking to make sure that
  // the contents of a set are as they should be and it's only
  // available in non-product builds.
#ifdef ASSERT
  void set_containing_set(HeapRegionSetBase* containing_set) {
    assert((containing_set == NULL && _containing_set != NULL) ||
           (containing_set != NULL && _containing_set == NULL),
           err_msg("containing_set: "PTR_FORMAT" "
                   "_containing_set: "PTR_FORMAT,
                   containing_set, _containing_set));

    _containing_set = containing_set;
  }

  HeapRegionSetBase* containing_set() { return _containing_set; }
#else // ASSERT
  void set_containing_set(HeapRegionSetBase* containing_set) { }

  // containing_set() is only used in asserts so there's no reason
  // to provide a dummy version of it.
#endif // ASSERT

  // If we want to remove regions from a list in bulk we can simply tag
  // them with the pending_removal tag and call the
  // remove_all_pending() method on the list.

  bool pending_removal() { return _pending_removal; }

  void set_pending_removal(bool pending_removal) {
    if (pending_removal) {
      assert(!_pending_removal && containing_set() != NULL,
             "can only set pending removal to true if it's false and "
             "the region belongs to a region set");
    } else {
      assert( _pending_removal && containing_set() == NULL,
              "can only set pending removal to false if it's true and "
              "the region does not belong to a region set");
    }

    _pending_removal = pending_removal;
  }

  HeapRegion* get_next_young_region() { return _next_young_region; }
  void set_next_young_region(HeapRegion* hr) {
    _next_young_region = hr;
  }

  HeapRegion* get_next_dirty_cards_region() const { return _next_dirty_cards_region; }
  HeapRegion** next_dirty_cards_region_addr() { return &_next_dirty_cards_region; }
  void set_next_dirty_cards_region(HeapRegion* hr) { _next_dirty_cards_region = hr; }
  bool is_on_dirty_cards_region_list() const { return get_next_dirty_cards_region() != NULL; }

  // Allows logical separation between objects allocated before and after.
  void save_marks();

  // Reset HR stuff to default values.
  void hr_clear(bool par, bool clear_space);
  void par_clear();

  void initialize(MemRegion mr, bool clear_space, bool mangle_space);

  // Get the start of the unmarked area in this region.
  HeapWord* prev_top_at_mark_start() const { return _prev_top_at_mark_start; }
  HeapWord* next_top_at_mark_start() const { return _next_top_at_mark_start; }

  // Apply "cl->do_oop" to (the addresses of) all reference fields in objects
  // allocated in the current region before the last call to "save_mark".
  void oop_before_save_marks_iterate(OopClosure* cl);

  // This call determines the "filter kind" argument that will be used for
  // the next call to "new_dcto_cl" on this region with the "traditional"
  // signature (i.e., the call below.)  The default, in the absence of a
  // preceding call to this method, is "NoFilterKind", and a call to this
  // method is necessary for each such call, or else it reverts to the
  // default.
  // (This is really ugly, but all other methods I could think of changed a
  // lot of main-line code for G1.)
  void set_next_filter_kind(HeapRegionDCTOC::FilterKind nfk) {
    _next_fk = nfk;
  }

  DirtyCardToOopClosure*
  new_dcto_closure(OopClosure* cl,
                   CardTableModRefBS::PrecisionStyle precision,
                   HeapRegionDCTOC::FilterKind fk);

#if WHASSUP
  DirtyCardToOopClosure*
  new_dcto_closure(OopClosure* cl,
                   CardTableModRefBS::PrecisionStyle precision,
                   HeapWord* boundary) {
    assert(boundary == NULL, "This arg doesn't make sense here.");
    DirtyCardToOopClosure* res = new_dcto_closure(cl, precision, _next_fk);
    _next_fk = HeapRegionDCTOC::NoFilterKind;
    return res;
  }
#endif

  //
  // Note the start or end of marking. This tells the heap region
  // that the collector is about to start or has finished (concurrently)
  // marking the heap.
  //

  // Note the start of a marking phase. Record the
  // start of the unmarked area of the region here.
  void note_start_of_marking(bool during_initial_mark) {
    init_top_at_conc_mark_count();
    _next_marked_bytes = 0;
    if (during_initial_mark && is_young() && !is_survivor())
      _next_top_at_mark_start = bottom();
    else
      _next_top_at_mark_start = top();
  }

  // Note the end of a marking phase. Install the start of
  // the unmarked area that was captured at start of marking.
  void note_end_of_marking() {
    _prev_top_at_mark_start = _next_top_at_mark_start;
    _prev_marked_bytes = _next_marked_bytes;
    _next_marked_bytes = 0;

    guarantee(_prev_marked_bytes <=
              (size_t) (prev_top_at_mark_start() - bottom()) * HeapWordSize,
              "invariant");
  }

  // After an evacuation, we need to update _next_top_at_mark_start
  // to be the current top.  Note this is only valid if we have only
  // ever evacuated into this region.  If we evacuate, allocate, and
  // then evacuate we are in deep doodoo.
  void note_end_of_copying() {
    assert(top() >= _next_top_at_mark_start, "Increase only");
    _next_top_at_mark_start = top();
  }

  // Returns "false" iff no object in the region was allocated when the
  // last mark phase ended.
  bool is_marked() { return _prev_top_at_mark_start != bottom(); }

  // If "is_marked()" is true, then this is the index of the region in
  // an array constructed at the end of marking of the regions in a
  // "desirability" order.
  int sort_index() {
    return _sort_index;
  }
  void set_sort_index(int i) {
    _sort_index = i;
  }

  void init_top_at_conc_mark_count() {
    _top_at_conc_mark_count = bottom();
  }

  void set_top_at_conc_mark_count(HeapWord *cur) {
    assert(bottom() <= cur && cur <= end(), "Sanity.");
    _top_at_conc_mark_count = cur;
  }

  HeapWord* top_at_conc_mark_count() {
    return _top_at_conc_mark_count;
  }

  void reset_during_compaction() {
    guarantee( isHumongous() && startsHumongous(),
               "should only be called for humongous regions");

    zero_marked_bytes();
    init_top_at_mark_start();
  }

  // <PREDICTION>
  void calc_gc_efficiency(void);
  double gc_efficiency() { return _gc_efficiency;}
  // </PREDICTION>

  bool is_young() const     { return _young_type != NotYoung; }
  bool is_survivor() const  { return _young_type == Survivor; }

  int  young_index_in_cset() const { return _young_index_in_cset; }
  void set_young_index_in_cset(int index) {
    assert( (index == -1) || is_young(), "pre-condition" );
    _young_index_in_cset = index;
  }

  int age_in_surv_rate_group() {
    assert( _surv_rate_group != NULL, "pre-condition" );
    assert( _age_index > -1, "pre-condition" );
    return _surv_rate_group->age_in_group(_age_index);
  }

  void record_surv_words_in_group(size_t words_survived) {
    assert( _surv_rate_group != NULL, "pre-condition" );
    assert( _age_index > -1, "pre-condition" );
    int age_in_group = age_in_surv_rate_group();
    _surv_rate_group->record_surviving_words(age_in_group, words_survived);
  }

  int age_in_surv_rate_group_cond() {
    if (_surv_rate_group != NULL)
      return age_in_surv_rate_group();
    else
      return -1;
  }

  SurvRateGroup* surv_rate_group() {
    return _surv_rate_group;
  }

  void install_surv_rate_group(SurvRateGroup* surv_rate_group) {
    assert( surv_rate_group != NULL, "pre-condition" );
    assert( _surv_rate_group == NULL, "pre-condition" );
    assert( is_young(), "pre-condition" );

    _surv_rate_group = surv_rate_group;
    _age_index = surv_rate_group->next_age_index();
  }

  void uninstall_surv_rate_group() {
    if (_surv_rate_group != NULL) {
      assert( _age_index > -1, "pre-condition" );
      assert( is_young(), "pre-condition" );

      _surv_rate_group = NULL;
      _age_index = -1;
    } else {
      assert( _age_index == -1, "pre-condition" );
    }
  }

  void set_young() { set_young_type(Young); }

  void set_survivor() { set_young_type(Survivor); }

  void set_not_young() { set_young_type(NotYoung); }

  // Determine if an object has been allocated since the last
  // mark performed by the collector. This returns true iff the object
  // is within the unmarked area of the region.
  bool obj_allocated_since_prev_marking(oop obj) const {
    return (HeapWord *) obj >= prev_top_at_mark_start();
  }
  bool obj_allocated_since_next_marking(oop obj) const {
    return (HeapWord *) obj >= next_top_at_mark_start();
  }

  // For parallel heapRegion traversal.
  bool claimHeapRegion(int claimValue);
  jint claim_value() { return _claimed; }
  // Use this carefully: only when you're sure no one is claiming...
  void set_claim_value(int claimValue) { _claimed = claimValue; }

  // Returns the "evacuation_failed" property of the region.
  bool evacuation_failed() { return _evacuation_failed; }

  // Sets the "evacuation_failed" property of the region.
  void set_evacuation_failed(bool b) {
    _evacuation_failed = b;

    if (b) {
      init_top_at_conc_mark_count();
      _next_marked_bytes = 0;
    }
  }

  // Requires that "mr" be entirely within the region.
  // Apply "cl->do_object" to all objects that intersect with "mr".
  // If the iteration encounters an unparseable portion of the region,
  // or if "cl->abort()" is true after a closure application,
  // terminate the iteration and return the address of the start of the
  // subregion that isn't done.  (The two can be distinguished by querying
  // "cl->abort()".)  Return of "NULL" indicates that the iteration
  // completed.
  HeapWord*
  object_iterate_mem_careful(MemRegion mr, ObjectClosure* cl);

  // filter_young: if true and the region is a young region then we
  // skip the iteration.
  // card_ptr: if not NULL, and we decide that the card is not young
  // and we iterate over it, we'll clean the card before we start the
  // iteration.
  HeapWord*
  oops_on_card_seq_iterate_careful(MemRegion mr,
                                   FilterOutOfRegionClosure* cl,
                                   bool filter_young,
                                   jbyte* card_ptr);

  // A version of block start that is guaranteed to find *some* block
  // boundary at or before "p", but does not object iteration, and may
  // therefore be used safely when the heap is unparseable.
  HeapWord* block_start_careful(const void* p) const {
    return _offsets.block_start_careful(p);
  }

  // Requires that "addr" is within the region.  Returns the start of the
  // first ("careful") block that starts at or after "addr", or else the
  // "end" of the region if there is no such block.
  HeapWord* next_block_start_careful(HeapWord* addr);

  size_t recorded_rs_length() const        { return _recorded_rs_length; }
  double predicted_elapsed_time_ms() const { return _predicted_elapsed_time_ms; }
  size_t predicted_bytes_to_copy() const   { return _predicted_bytes_to_copy; }

  void set_recorded_rs_length(size_t rs_length) {
    _recorded_rs_length = rs_length;
  }

  void set_predicted_elapsed_time_ms(double ms) {
    _predicted_elapsed_time_ms = ms;
  }

  void set_predicted_bytes_to_copy(size_t bytes) {
    _predicted_bytes_to_copy = bytes;
  }

#define HeapRegion_OOP_SINCE_SAVE_MARKS_DECL(OopClosureType, nv_suffix)  \
  virtual void oop_since_save_marks_iterate##nv_suffix(OopClosureType* cl);
  SPECIALIZED_SINCE_SAVE_MARKS_CLOSURES(HeapRegion_OOP_SINCE_SAVE_MARKS_DECL)

  CompactibleSpace* next_compaction_space() const;

  virtual void reset_after_compaction();

  void print() const;
  void print_on(outputStream* st) const;

  // use_prev_marking == true  -> use "prev" marking information,
  // use_prev_marking == false -> use "next" marking information
  // NOTE: Only the "prev" marking information is guaranteed to be
  // consistent most of the time, so most calls to this should use
  // use_prev_marking == true. Currently, there is only one case where
  // this is called with use_prev_marking == false, which is to verify
  // the "next" marking information at the end of remark.
  void verify(bool allow_dirty, bool use_prev_marking, bool *failures) const;

  // Override; it uses the "prev" marking information
  virtual void verify(bool allow_dirty) const;
};

// HeapRegionClosure is used for iterating over regions.
// Terminates the iteration when the "doHeapRegion" method returns "true".
class HeapRegionClosure : public StackObj {
  friend class HeapRegionSeq;
  friend class G1CollectedHeap;

  bool _complete;
  void incomplete() { _complete = false; }

 public:
  HeapRegionClosure(): _complete(true) {}

  // Typically called on each region until it returns true.
  virtual bool doHeapRegion(HeapRegion* r) = 0;

  // True after iteration if the closure was applied to all heap regions
  // and returned "false" in all cases.
  bool complete() { return _complete; }
};

#endif // SERIALGC

#endif // SHARE_VM_GC_IMPLEMENTATION_G1_HEAPREGION_HPP
