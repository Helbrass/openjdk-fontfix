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

#ifndef SHARE_VM_GC_IMPLEMENTATION_G1_CONCURRENTG1REFINE_HPP
#define SHARE_VM_GC_IMPLEMENTATION_G1_CONCURRENTG1REFINE_HPP

#include "memory/allocation.hpp"
#include "memory/cardTableModRefBS.hpp"
#include "runtime/thread.hpp"
#include "utilities/globalDefinitions.hpp"

// Forward decl
class ConcurrentG1RefineThread;
class G1RemSet;

class ConcurrentG1Refine: public CHeapObj {
  ConcurrentG1RefineThread** _threads;
  int _n_threads;
  int _n_worker_threads;
 /*
  * The value of the update buffer queue length falls into one of 3 zones:
  * green, yellow, red. If the value is in [0, green) nothing is
  * done, the buffers are left unprocessed to enable the caching effect of the
  * dirtied cards. In the yellow zone [green, yellow) the concurrent refinement
  * threads are gradually activated. In [yellow, red) all threads are
  * running. If the length becomes red (max queue length) the mutators start
  * processing the buffers.
  *
  * There are some interesting cases (when G1UseAdaptiveConcRefinement
  * is turned off):
  * 1) green = yellow = red = 0. In this case the mutator will process all
  *    buffers. Except for those that are created by the deferred updates
  *    machinery during a collection.
  * 2) green = 0. Means no caching. Can be a good way to minimize the
  *    amount of time spent updating rsets during a collection.
  */
  int _green_zone;
  int _yellow_zone;
  int _red_zone;

  int _thread_threshold_step;

  // Reset the threshold step value based of the current zone boundaries.
  void reset_threshold_step();

  // The cache for card refinement.
  bool   _use_cache;
  bool   _def_use_cache;

  size_t _n_periods;    // Used as clearing epoch

  // An evicting cache of the number of times each card
  // is accessed. Reduces, but does not eliminate, the amount
  // of duplicated processing of dirty cards.

  enum SomePrivateConstants {
    epoch_bits           = 32,
    card_num_shift       = epoch_bits,
    epoch_mask           = AllBits,
    card_num_mask        = AllBits,

    // The initial cache size is approximately this fraction
    // of a maximal cache (i.e. the size needed for all cards
    // in the heap)
    InitialCacheFraction = 512
  };

  const static julong card_num_mask_in_place =
                        (julong) card_num_mask << card_num_shift;

  typedef struct {
    julong _value;      // |  card_num   |  epoch   |
  } CardEpochCacheEntry;

  julong make_epoch_entry(unsigned int card_num, unsigned int epoch) {
    assert(0 <= card_num && card_num < _max_cards, "Bounds");
    assert(0 <= epoch && epoch <= _n_periods, "must be");

    return ((julong) card_num << card_num_shift) | epoch;
  }

  unsigned int extract_epoch(julong v) {
    return (v & epoch_mask);
  }

  unsigned int extract_card_num(julong v) {
    return (v & card_num_mask_in_place) >> card_num_shift;
  }

  typedef struct {
    unsigned char _count;
    unsigned char _evict_count;
  } CardCountCacheEntry;

  CardCountCacheEntry* _card_counts;
  CardEpochCacheEntry* _card_epochs;

  // The current number of buckets in the card count cache
  size_t _n_card_counts;

  // The number of cards for the entire reserved heap
  size_t _max_cards;

  // The max number of buckets for the card counts and epochs caches.
  // This is the maximum that the counts and epochs will grow to.
  // It is specified as a fraction or percentage of _max_cards using
  // G1MaxHotCardCountSizePercent.
  size_t _max_n_card_counts;

  // Possible sizes of the cache: odd primes that roughly double in size.
  // (See jvmtiTagMap.cpp).
  enum {
    MAX_CC_CACHE_INDEX = 15    // maximum index into the cache size array.
  };

  static size_t _cc_cache_sizes[MAX_CC_CACHE_INDEX];

  // The index in _cc_cache_sizes corresponding to the size of
  // _card_counts.
  int _cache_size_index;

  bool _expand_card_counts;

  const jbyte* _ct_bot;

  jbyte**      _hot_cache;
  int          _hot_cache_size;
  int          _n_hot;
  int          _hot_cache_idx;

  int          _hot_cache_par_chunk_size;
  volatile int _hot_cache_par_claimed_idx;

  // Needed to workaround 6817995
  CardTableModRefBS* _ct_bs;
  G1CollectedHeap*   _g1h;

  // Helper routine for expand_card_count_cache().
  // The arrays used to hold the card counts and the epochs must have
  // a 1:1 correspondence. Hence they are allocated and freed together.
  // Returns true if the allocations of both the counts and epochs
  // were successful; false otherwise.
  bool allocate_card_count_cache(size_t n,
                                 CardCountCacheEntry** counts,
                                 CardEpochCacheEntry** epochs);

  // Expands the arrays that hold the card counts and epochs
  // to the cache size at index. Returns true if the expansion/
  // allocation was successful; false otherwise.
  bool expand_card_count_cache(int index);

  // hash a given key (index of card_ptr) with the specified size
  static unsigned int hash(size_t key, size_t size) {
    return (unsigned int) (key % size);
  }

  // hash a given key (index of card_ptr)
  unsigned int hash(size_t key) {
    return hash(key, _n_card_counts);
  }

  unsigned int ptr_2_card_num(jbyte* card_ptr) {
    return (unsigned int) (card_ptr - _ct_bot);
  }

  jbyte* card_num_2_ptr(unsigned int card_num) {
    return (jbyte*) (_ct_bot + card_num);
  }

  // Returns the count of this card after incrementing it.
  jbyte* add_card_count(jbyte* card_ptr, int* count, bool* defer);

  // Returns true if this card is in a young region
  bool is_young_card(jbyte* card_ptr);

 public:
  ConcurrentG1Refine();
  ~ConcurrentG1Refine();

  void init(); // Accomplish some initialization that has to wait.
  void stop();

  void reinitialize_threads();

  // Iterate over the conc refine threads
  void threads_do(ThreadClosure *tc);

  // If this is the first entry for the slot, writes into the cache and
  // returns NULL.  If it causes an eviction, returns the evicted pointer.
  // Otherwise, its a cache hit, and returns NULL.
  jbyte* cache_insert(jbyte* card_ptr, bool* defer);

  // Process the cached entries.
  void clean_up_cache(int worker_i, G1RemSet* g1rs, DirtyCardQueue* into_cset_dcq);

  // Set up for parallel processing of the cards in the hot cache
  void clear_hot_cache_claimed_index() {
    _hot_cache_par_claimed_idx = 0;
  }

  // Discard entries in the hot cache.
  void clear_hot_cache() {
    _hot_cache_idx = 0; _n_hot = 0;
  }

  bool hot_cache_is_empty() { return _n_hot == 0; }

  bool use_cache() { return _use_cache; }
  void set_use_cache(bool b) {
    if (b) _use_cache = _def_use_cache;
    else   _use_cache = false;
  }

  void clear_and_record_card_counts();

  static int thread_num();

  void print_worker_threads_on(outputStream* st) const;

  void set_green_zone(int x)  { _green_zone = x;  }
  void set_yellow_zone(int x) { _yellow_zone = x; }
  void set_red_zone(int x)    { _red_zone = x;    }

  int green_zone() const      { return _green_zone;  }
  int yellow_zone() const     { return _yellow_zone; }
  int red_zone() const        { return _red_zone;    }

  int total_thread_num() const  { return _n_threads;        }
  int worker_thread_num() const { return _n_worker_threads; }

  int thread_threshold_step() const { return _thread_threshold_step; }
};

#endif // SHARE_VM_GC_IMPLEMENTATION_G1_CONCURRENTG1REFINE_HPP
