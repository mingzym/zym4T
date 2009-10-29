/** @file

  A brief file description

  @section license License

  Licensed to the Apache Software Foundation (ASF) under one
  or more contributor license agreements.  See the NOTICE file
  distributed with this work for additional information
  regarding copyright ownership.  The ASF licenses this file
  to you under the Apache License, Version 2.0 (the
  "License"); you may not use this file except in compliance
  with the License.  You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

  Unless required by applicable law or agreed to in writing, software
  distributed under the License is distributed on an "AS IS" BASIS,
  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
  See the License for the specific language governing permissions and
  limitations under the License.
 */


#include "P_Cache.h"

#define LOOP_CHECK_MODE
#ifdef LOOP_CHECK_MODE
#define DIR_LOOP_THRESHOLD	      1000
#endif
#include "ink_stack_trace.h"

#define SYNC_MAX_WRITE                (256 * 1024)
#define SYNC_DELAY                    HRTIME_MSECONDS(500)

#define CACHE_INC_DIR_USED(_m) do { \
ProxyMutex *mutex = _m; \
CACHE_INCREMENT_DYN_STAT(cache_direntries_used_stat); \
} while (0) \

#define CACHE_DEC_DIR_USED(_m) do { \
ProxyMutex *mutex = _m; \
CACHE_DECREMENT_DYN_STAT(cache_direntries_used_stat); \
} while (0) \

#define CACHE_INC_DIR_COLLISIONS(_m) do {\
ProxyMutex *mutex = _m; \
CACHE_INCREMENT_DYN_STAT(cache_directory_collision_count_stat); \
} while (0);


// Debugging Options

// #define CHECK_DIR_FAST
// #define CHECK_DIR

// Globals

ClassAllocator<OpenDirEntry> openDirEntryAllocator("openDirEntry");
Dir empty_dir = { 0 };

// OpenDir

OpenDir::OpenDir()
{
  SET_HANDLER(&OpenDir::signal_readers);
}

/* 
   If allow_if_writers is false, open_write fails if there are other writers.
   max_writers sets the maximum number of concurrent writers that are 
   allowed. Only The first writer can set the max_writers. It is ignored
   for later writers.
   Returns 1 on success and 0 on failure.
   */
int
OpenDir::open_write(CacheVC * cont, int allow_if_writers, int max_writers)
{
  ink_debug_assert(cont->part->mutex->thread_holding == this_ethread());
  unsigned int h = cont->first_key.word(0);
  int b = h % OPEN_DIR_BUCKETS;
  for (OpenDirEntry * d = bucket[b].head; d; d = d->link.next) {
    if (!(d->writers.head->first_key == cont->first_key))
      continue;
    if (allow_if_writers && d->num_writers < d->max_writers) {
      d->writers.push(cont, cont->opendir_link);
      d->num_writers++;
      cont->od = d;
      cont->write_vector = &d->vector;
      return 1;
    }
    return 0;
  }
  OpenDirEntry *od = THREAD_ALLOC(openDirEntryAllocator,
                                  cont->mutex->thread_holding);
  od->readers.head = NULL;
  od->writers.push(cont, cont->opendir_link);
  od->num_writers = 1;
  od->max_writers = max_writers;
  od->vector.data.data = &od->vector.data.fast_data[0];
  od->dont_update_directory = 0;
  od->move_resident_alt = 0;
  od->reading_vec = 0;
  od->writing_vec = 0;
  dir_clear(&od->first_dir);
  cont->od = od;
  cont->write_vector = &od->vector;
  bucket[b].push(od);
  return 1;
}

int
OpenDir::signal_readers(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  Queue<CacheVC> newly_delayed_readers;
  EThread *t = mutex->thread_holding;
  CacheVC *c = NULL;
  while ((c = delayed_readers.dequeue())) {
    if (c->mutex->is_thread() && c->mutex->thread_holding != t)
      newly_delayed_readers.enqueue(c);
    else {
      CACHE_TRY_LOCK(lock, c->mutex, t);
      if (lock) {
        c->f.open_read_timeout = 0;
        c->handleEvent(EVENT_IMMEDIATE, 0);
        continue;
      }
      newly_delayed_readers.push(c);
    }
  }
  if (newly_delayed_readers.head) {
    delayed_readers = newly_delayed_readers;
    EThread *t1 = newly_delayed_readers.head->mutex->thread_holding;
    if (!t1)
      t1 = mutex->thread_holding;
    t1->schedule_in(this, MUTEX_RETRY_DELAY);
  }
  return 0;
}

int
OpenDir::close_write(CacheVC * cont)
{
  ink_debug_assert(cont->part->mutex->thread_holding == this_ethread());
  cont->od->writers.remove(cont, cont->opendir_link);
  cont->od->num_writers--;
  if (!cont->od->writers.head) {
    unsigned int h = cont->first_key.word(0);
    int b = h % OPEN_DIR_BUCKETS;
    bucket[b].remove(cont->od);
    delayed_readers.append(cont->od->readers);
    signal_readers(0, 0);
    cont->od->vector.clear();
    THREAD_FREE(cont->od, openDirEntryAllocator, cont->mutex->thread_holding);
  }
  cont->od = NULL;
  return 0;
}

OpenDirEntry *
OpenDir::open_read(INK_MD5 * key)
{
  unsigned int h = key->word(0);
  int b = h % OPEN_DIR_BUCKETS;
  for (OpenDirEntry * d = bucket[b].head; d; d = d->link.next)
    if (d->writers.head->first_key == *key)
      return d;
  return NULL;
}

int
OpenDirEntry::wait(CacheVC * cont, int msec)
{
  ink_debug_assert(cont->part->mutex->thread_holding == this_ethread());
  cont->f.open_read_timeout = 1;
  ink_assert(!cont->trigger);
  cont->trigger = cont->part->mutex->thread_holding->schedule_in_local(cont, HRTIME_MSECONDS(msec));
  readers.push(cont);
  return EVENT_CONT;
}

//
// Cache Directory
//

// return value 1 means no loop
// zero indicates loop
int
dir_bucket_loop_check(Dir * start_dir, Dir * seg)
{
  if (start_dir == NULL)
    return 1;

  Dir *p1 = start_dir;
  Dir *p2 = start_dir;

  while (p2) {
    // p1 moves by one entry per iteration
    p1 = next_dir(p1, seg);
    // p2 moves by two entries per iteration
    p2 = next_dir(p2, seg);
    if (p2)
      p2 = next_dir(p2, seg);
    else
      return 1;

    if (p2 == p1)
      return 0;                 // we have a loop
  }
  return 1;
}

// adds all the directory entries
// in a segment to the segment freelist
void
dir_init_segment(int s, Part * d)
{
  d->header->freelist[s] = 0;
  Dir *seg = dir_segment(s, d);
  int l, b;
  memset(seg, 0, SIZEOF_DIR * DIR_DEPTH * d->buckets);
  for (l = 1; l < DIR_DEPTH; l++) {
    for (b = 0; b < d->buckets; b++) {
      Dir *bucket = dir_bucket(b, seg);
      dir_free_entry(&bucket[l], s, d);
    }
  }
}


// break the infinite loop in directory entries
// Note : abuse of the token bit in dir entries
#if 0
int
dir_bucket_loop_fix(Dir * start_dir, int s, Part * d)
{
  int ret = 0;
  if (start_dir == NULL)
    return 0;

  Dir *p1 = start_dir;
  Dir *p2 = start_dir;
  Dir *seg = dir_segment(s, d);
  while (p2) {
    dir_set_token(p1, 1);
    p2 = next_dir(p2, seg);
    if (p2 && dir_token(p2)) {
      // Loop exists
      Warning("cache directory loop broken");
      ink_stack_trace_dump();
      dir_set_next(p1, 0);
      ret = 1;
      break;
    }
    p1 = p2;
  }
  for (Dir * p3 = start_dir; p3; p3 = next_dir(p3, seg))
    dir_set_token(p3, 0);
  return ret;
}
#else
int
dir_bucket_loop_fix(Dir * start_dir, int s, Part * d)
{
  if (!dir_bucket_loop_check(start_dir, dir_segment(s, d))) {
    Warning("Dir loop exists, clearing segment %d", s);
    dir_init_segment(s, d);
    return 1;
  }
  return 0;
}
#endif

int
dir_freelist_length(Part * d, int s)
{
  int free = 0;
  Dir *seg = dir_segment(s, d);
  Dir *e = dir_from_offset(d->header->freelist[s], seg);
  if (dir_bucket_loop_fix(e, s, d))
    return (DIR_DEPTH - 1) * d->buckets;
  while (e) {
    free++;
    e = next_dir(e, seg);
  }
  return free;
}

int
dir_bucket_length(Dir * b, int s, Part * d)
{
  Dir *e = b;
  int i = 0;
  Dir *seg = dir_segment(s, d);
  if (dir_bucket_loop_fix(b, s, d))
    return 1;
  while (e) {
    i++;
    if (i > 100)
      return -1;
    e = next_dir(e, seg);
  }
  return i;
}


void
check_dir(Part * d)
{
  int i, s;
  Debug("cache_check_dir", "inside check dir");
  for (s = 0; s < DIR_SEGMENTS; s++) {
    Dir *seg = dir_segment(s, d);
    ink_debug_assert(dir_bucket_loop_check(dir_from_offset(d->header->freelist[s], seg), seg));
    for (i = 0; i < d->buckets; i++) {
      Dir RELEASE_UNUSED *b = dir_bucket(i, seg);
      ink_debug_assert(dir_bucket_length(b, s, d) >= 0);
      ink_debug_assert(!dir_next(b) || dir_offset(b));
      ink_debug_assert(dir_bucket_loop_check(b, seg));
    }
  }
}

inline void
unlink_from_freelist(Dir * e, int s, Part * d)
{
  Dir *seg = dir_segment(s, d);
  Dir *p = dir_from_offset(dir_prev(e), seg);
  if (p)
    dir_set_next(p, dir_next(e));
  else
    d->header->freelist[s] = dir_next(e);
  Dir *n = dir_from_offset(dir_next(e), seg);
  if (n)
    dir_set_prev(n, dir_prev(e));
}

inline Dir *
dir_delete_entry(Dir * e, Dir * p, int s, Part * d)
{
  Dir *seg = dir_segment(s, d);
  int no = dir_next(e);
  d->header->dirty = 1;
  if (p) {
    unsigned int fo = d->header->freelist[s];
    unsigned int eo = dir_to_offset(e, seg);
    dir_clear(e);
    dir_set_next(p, no);
    dir_set_next(e, fo);
    if (fo)
      dir_set_prev(dir_from_offset(fo, seg), eo);
    d->header->freelist[s] = eo;
  } else {
    Dir *n = next_dir(e, seg);
    if (n) {
      dir_assign(e, n);
      dir_delete_entry(n, e, s, d);
      return e;
    } else {
      dir_clear(e);
      return NULL;
    }
  }
  return dir_from_offset(no, seg);
}

inline void
dir_clean_bucket(Dir * b, int s, Part * part)
{
  Dir *e = b, *p = NULL;
  Dir *seg = dir_segment(s, part);
  int loop_count = 0;
  do {
#ifdef LOOP_CHECK_MODE
    loop_count++;
    if (loop_count > DIR_LOOP_THRESHOLD) {
      if (dir_bucket_loop_fix(b, s, part))
        return;
    }
#endif
    if (!dir_valid(part, e) || !dir_offset(e)) {
      if (is_debug_tag_set("dir_clean"))
        Debug("dir_clean", "cleaning %X tag %X boffset %d b %X p %X l %d",
              (long) e, dir_tag(e), (int) dir_offset(e), (long) b, (long) p, dir_bucket_length(b, s, part));
      if (dir_offset(e))
        CACHE_DEC_DIR_USED(part->mutex);

      e = dir_delete_entry(e, p, s, part);
      continue;
    }
    p = e;
    e = next_dir(e, seg);
  } while (e);
}

void
dir_clean_segment(int s, Part * d)
{
  Dir *seg = dir_segment(s, d);
  for (int i = 0; i < d->buckets; i++) {
    dir_clean_bucket(dir_bucket(i, seg), s, d);
    ink_assert(!dir_next(dir_bucket(i, seg)) || dir_offset(dir_bucket(i, seg)));
  }
}

void
dir_clean_part(Part * d)
{
  for (int i = 0; i < DIR_SEGMENTS; i++)
    dir_clean_segment(i, d);
#if defined(DEBUG) && defined(CHECK_DIR)
  check_dir(d);
#endif
}

void
dir_clear_range(int start, int end, Part * part)
{
  for (int i = 0; i < part->buckets * DIR_DEPTH * DIR_SEGMENTS; i++) {
    Dir *e = dir_index(part, i);
    if (!dir_token(e) && (int) dir_offset(e) >= start && (int) dir_offset(e) < end) {
      CACHE_DEC_DIR_USED(part->mutex);

      dir_set_offset(e, 0);     // delete
    }
  }
  dir_clean_part(part);
}

void
check_bucket_not_contains(Dir * b, Dir * e, Dir * seg)
{
  Dir *x = b;
  do {
    if (x == e)
      break;
    x = next_dir(x, seg);
  } while (x);
  ink_assert(!x);
}

void
freelist_clean(int s, Part * part)
{
  dir_clean_segment(s, part);
  if (part->header->freelist[s])
    return;
  Warning("cache directory overflow on '%s' segment %d, purging...", part->path, s);
  int n = 0;
  Dir *seg = dir_segment(s, part);
  for (int bi = 0; bi < part->buckets; bi++) {
    Dir *b = dir_bucket(bi, seg);
    for (int l = 0; l < DIR_DEPTH; l++) {
      Dir *e = dir_bucket_row(b, l);
      if (dir_head(e) && !(n++ % 10)) {
        CACHE_DEC_DIR_USED(part->mutex);

        dir_set_offset(e, 0);   // delete
      }
    }
  }
  dir_clean_segment(s, part);
}

inline Dir *
freelist_pop(int s, Part * d)
{
  Dir *seg = dir_segment(s, d);
  Dir *e = dir_from_offset(d->header->freelist[s], seg);
  if (!e) {
    freelist_clean(s, d);
    return NULL;
  }
  d->header->freelist[s] = dir_next(e);
  // if the freelist if bad, punt.
  if (dir_offset(e)) {
    dir_init_segment(s, d);
    return NULL;
  }
  Dir *h = dir_from_offset(d->header->freelist[s], seg);
  if (h)
    dir_set_prev(h, 0);
  return e;
}

int
dir_segment_accounted(int s, Part * d, int offby, int *f, int *u, int *et, int *v, int *av, int *as)
{
  int free = dir_freelist_length(d, s);
  int used = 0, empty = 0;
  int valid = 0, agg_valid = 0;
  ink64 agg_size = 0;
  Dir *seg = dir_segment(s, d);
  for (int bi = 0; bi < d->buckets; bi++) {
    Dir *b = dir_bucket(bi, seg);
    Dir *e = b;
    while (e) {
      if (!dir_offset(e)) {
        ink_assert(e == b);
        empty++;
      } else {
        used++;
        if (dir_valid(d, e))
          valid++;
        if (dir_agg_valid(d, e))
          agg_valid++;
        agg_size += dir_approx_size(e);
      }
      e = next_dir(e, seg);
      if (!e)
        break;
    }
  }
  if (f)
    *f = free;
  if (u)
    *u = used;
  if (et)
    *et = empty;
  if (v)
    *v = valid;
  if (av)
    *av = agg_valid;
  if (as)
    *as = used ? (int) (agg_size / used) : 0;
  ink_assert(d->buckets * DIR_DEPTH - (free + used + empty) <= offby);
  return d->buckets * DIR_DEPTH - (free + used + empty) <= offby;
}

void
dir_free_entry(Dir * e, int s, Part * d)
{
  Dir *seg = dir_segment(s, d);
  unsigned int fo = d->header->freelist[s];
  unsigned int eo = dir_to_offset(e, seg);
  dir_set_next(e, fo);
  if (fo)
    dir_set_prev(dir_from_offset(fo, seg), eo);
  d->header->freelist[s] = eo;
}

int
dir_probe(CacheKey * key, Part * d, Dir * result, Dir ** last_collision)
{
  ink_debug_assert(d->mutex->thread_holding == this_ethread());
  int s = key->word(0) % DIR_SEGMENTS;
  int b = (key->word(0) / DIR_SEGMENTS) % d->buckets;
  Dir *seg = dir_segment(s, d);
  Dir *e = NULL, *p = NULL, *collision = *last_collision;
  Part *part = d;
#if defined(DEBUG) && defined(CHECK_DIR)
  check_dir(d);
#endif
#ifdef LOOP_CHECK_MODE
  if (dir_bucket_loop_fix(dir_bucket(b, seg), s, d))
    return 0;
#endif
Lagain:
  e = dir_bucket(b, seg);
  if (dir_offset(e))
    do {
      if (dir_compare_tag(e, key)) {
        ink_debug_assert(dir_offset(e));
        // Bug: 51680. Need to check collision before checking 
        // dir_valid(). In case of a collision, if !dir_valid(), we 
        // don't want to call dir_delete_entry.
        if (collision) {
          if (collision == e) {
            collision = NULL;
            // increment collison stat
            // Note: dir_probe could be called multiple times
            // for the same document and so the collision stat
            // may not accurately reflect the number of documents
            // having the same first_key
            Debug("cache_stats", "Incrementing dir collisions");
            CACHE_INC_DIR_COLLISIONS(d->mutex);
          }
          goto Lcont;
        }
        if (dir_valid(d, e)) {
          Debug("dir_probe_hit", "found %X part %d bucket %d  boffset %d", key->word(0), d->fd, b, (int) dir_offset(e));
          dir_assign(result, e);
          *last_collision = e;
          ink_assert(dir_offset(e) * INK_BLOCK_SIZE < d->len);
          return 1;
        } else {                // delete the invalid entry
          CACHE_DEC_DIR_USED(d->mutex);
          e = dir_delete_entry(e, p, s, d);
          continue;
        }
      } else
        Debug("dir_probe_tag", "tag mismatch %X %X vs expected %X", e, dir_tag(e), key->word(1));
    Lcont:
      p = e;
      e = next_dir(e, seg);
    } while (e);
  if (collision) {              // last collision no longer in the list, retry
    Debug("cache_stats", "Incrementing dir collisions");
    CACHE_INC_DIR_COLLISIONS(d->mutex);
    collision = NULL;
    goto Lagain;
  }
  Debug("dir_probe_miss", "missed %X on part %d bucket %d at %X", key->word(0), d->fd, b, (long) seg);
#if defined(DEBUG) && defined(CHECK_DIR)
  check_dir(d);
#endif
  return 0;
}

int
dir_insert(CacheKey * key, Part * d, Dir * to_part)
{
  ink_debug_assert(d->mutex->thread_holding == this_ethread());
  int s = key->word(0) % DIR_SEGMENTS, l;
  int bi = (key->word(0) / DIR_SEGMENTS) % d->buckets;
  ink_assert((unsigned int) dir_approx_size(to_part) <= (unsigned int) (MAX_FRAG_SIZE + sizeofDoc));    // XXX - size should be unsigned
  Dir *seg = dir_segment(s, d);
  Dir *e = NULL;
  Dir *b = dir_bucket(bi, seg);
  Part *part = d;
#if defined(DEBUG) && defined(CHECK_DIR_FAST)
  unsigned int t = DIR_MASK_TAG(key->word(1));
  Dir *col = b;
  while (col) {
    ink_assert((dir_tag(col) != t) || (dir_offset(col) != dir_offset(to_part)));
    col = next_dir(col, seg);
  }
#endif
#if defined(DEBUG) && defined(CHECK_DIR)
  check_dir(d);
#endif

Lagain:
  // get from this row first
  e = b;
  if (dir_is_empty(e))
    goto Lfill;
  for (l = 1; l < DIR_DEPTH; l++) {
    e = dir_bucket_row(b, l);
    if (dir_is_empty(e)) {
      unlink_from_freelist(e, s, d);
      goto Llink;
    }
  }
  // get one from the freelist
  e = freelist_pop(s, d);
  if (!e)
    goto Lagain;
Llink:
  dir_set_next(e, dir_next(b));
  dir_set_next(b, dir_to_offset(e, seg));
Lfill:
  dir_assign_data(e, to_part);
  dir_set_tag(e, key->word(1));
  ink_assert(part_offset(d, e) < (d->skip + d->len));
  Debug("dir_insert",
        "insert %X %X into part %d bucket %d at %X tag %X %X boffset %d",
        (long) e, key->word(0), d->fd, bi, (long) e, key->word(1), dir_tag(e), (int) dir_offset(e));
#if defined(DEBUG) && defined(CHECK_DIR)
  check_dir(d);
#endif
  d->header->dirty = 1;
  CACHE_INC_DIR_USED(d->mutex);
  return 1;
}

int
dir_overwrite(CacheKey * key, Part * d, Dir * dir, Dir * overwrite, bool must_overwrite)
{
  ink_debug_assert(d->mutex->thread_holding == this_ethread());
  int s = key->word(0) % DIR_SEGMENTS, l;
  int bi = (key->word(0) / DIR_SEGMENTS) % d->buckets;
  Dir *seg = dir_segment(s, d);
  Dir *e = NULL;
  Dir *b = dir_bucket(bi, seg);
  unsigned int t = DIR_MASK_TAG(key->word(1));
  int res = 1;
  int loop_count = 0;
  bool loop_possible = true;
  Part *part = d;
#if defined(DEBUG) && defined(CHECK_DIR)
  check_dir(d);
#endif

  ink_assert((unsigned int) dir_approx_size(dir) <= (unsigned int) (MAX_FRAG_SIZE + sizeofDoc));        // XXX - size should be unsigned
Lagain:
  // find entry to overwrite
  e = b;
  if (dir_offset(e))
    do {
#ifdef LOOP_CHECK_MODE
      loop_count++;
      if (loop_count > DIR_LOOP_THRESHOLD && loop_possible) {
        if (dir_bucket_loop_fix(b, s, d)) {
          loop_possible = false;
          goto Lagain;
        }
      }
#endif
      if (dir_tag(e) == t && dir_offset(e) == dir_offset(overwrite))
        goto Lfill;
      e = next_dir(e, seg);
    } while (e);
  if (must_overwrite)
    return 0;
  res = 0;
  // get from this row first
  e = b;
  if (dir_is_empty(e)) {
    CACHE_INC_DIR_USED(d->mutex);
    goto Lfill;
  }
  for (l = 1; l < DIR_DEPTH; l++) {
    e = dir_bucket_row(b, l);
    if (dir_is_empty(e)) {
      unlink_from_freelist(e, s, d);
      goto Llink;
    }
  }
  // get one from the freelist
  e = freelist_pop(s, d);
  if (!e)
    goto Lagain;
Llink:
  CACHE_INC_DIR_USED(d->mutex);
  dir_set_next(e, dir_next(b));
  dir_set_next(b, dir_to_offset(e, seg));
Lfill:
  dir_assign_data(e, dir);
  dir_set_tag(e, t);
  ink_assert(part_offset(d, e) < d->skip + d->len);
  Debug("dir_overwrite",
        "overwrite %X %X into part %d bucket %d at %X tag %X %X boffset %d",
        (long) e, key->word(0), d->fd, bi, (long) e, t, dir_tag(e), (int) dir_offset(e));
#if defined(DEBUG) && defined(CHECK_DIR)
  check_dir(d);
#endif
  d->header->dirty = 1;
  return res;
}

int
dir_delete(CacheKey * key, Part * d, Dir * del)
{
  ink_debug_assert(d->mutex->thread_holding == this_ethread());
  int s = key->word(0) % DIR_SEGMENTS;
  int b = (key->word(0) / DIR_SEGMENTS) % d->buckets;
  Dir *seg = dir_segment(s, d);
  Dir *e = NULL, *p = NULL;
  int loop_count = 0;
  Part *part = d;
#if defined(DEBUG) && defined(CHECK_DIR)
  check_dir(d);
#endif

  e = dir_bucket(b, seg);
  if (dir_offset(e))
    do {
#ifdef LOOP_CHECK_MODE
      loop_count++;
      if (loop_count > DIR_LOOP_THRESHOLD) {
        if (dir_bucket_loop_fix(dir_bucket(b, seg), s, d))
          return 0;
      }
#endif
      if (dir_compare_tag(e, key) && dir_offset(e) == dir_offset(del)) {
        CACHE_DEC_DIR_USED(d->mutex);
        dir_delete_entry(e, p, s, d);
#if defined(DEBUG) && defined(CHECK_DIR)
        check_dir(d);
#endif
        return 1;
      }
      p = e;
      e = next_dir(e, seg);
    } while (e);
#if defined(DEBUG) && defined(CHECK_DIR)
  check_dir(d);
#endif
  return 0;
}

// Lookaside Cache

int
dir_lookaside_probe(CacheKey * key, Part * d, Dir * result, EvacuationBlock ** eblock)
{
  ink_debug_assert(d->mutex->thread_holding == this_ethread());
  int i = key->word(3) % LOOKASIDE_SIZE;
  EvacuationBlock *b = d->lookaside[i].head;
  while (b) {
    if (b->evac_frags.key == *key) {
      if (dir_valid(d, &b->new_dir)) {
        *result = b->new_dir;
        Debug("dir_lookaside", "probe %X success", key->word(0));
        if (eblock)
          *eblock = b;
        return 1;
      }
    }
    b = b->link.next;
  }
  Debug("dir_lookaside", "probe %X failed", key->word(0));
  return 0;
}

int
dir_lookaside_insert(EvacuationBlock * eblock, Part * d, Dir * to)
{
  CacheKey *key = &eblock->evac_frags.earliest_key;
  Debug("dir_lookaside", "insert %X, offset %d phase %d", key->word(0), (int) dir_offset(to), (int) dir_phase(to));
  ink_debug_assert(d->mutex->thread_holding == this_ethread());
  int i = key->word(3) % LOOKASIDE_SIZE;
  EvacuationBlock *b = new_EvacuationBlock(d->mutex->thread_holding);
  b->evac_frags.key = *key;
  b->evac_frags.earliest_key = *key;
  b->earliest_evacuator = eblock->earliest_evacuator;
  ink_assert(b->earliest_evacuator);
  b->dir = eblock->dir;
  b->new_dir = *to;
  d->lookaside[i].push(b);
  return 1;
}

int
dir_lookaside_fixup(CacheKey * key, Part * d)
{
  ink_debug_assert(d->mutex->thread_holding == this_ethread());
  int i = key->word(3) % LOOKASIDE_SIZE;
  EvacuationBlock *b = d->lookaside[i].head;
  while (b) {
    if (b->evac_frags.key == *key) {
      int res = dir_overwrite(key, d, &b->new_dir, &b->dir, false);
      Debug("dir_lookaside", "fixup %X offset %d phase %d %d",
            key->word(0), dir_offset(&b->new_dir), dir_phase(&b->new_dir), res);
      d->ram_cache.fixup(key, 0, dir_offset(&b->dir), 0, dir_offset(&b->new_dir));
      d->lookaside[i].remove(b);
#if 0
      // we need to do this because in case of a small cache, the scan
      // might have occured before we inserted this directory entry (if we 
      // wrapped around fast enough)
      int part_end_offset = offset_to_part_offset(d, d->len + d->skip);
      int part_write_offset = offset_to_part_offset(d, d->header->write_pos);
      if ((dir_offset(&b->new_dir) + part_end_offset - part_write_offset)
          % part_end_offset <= offset_to_part_offset(d, EVAC_SIZE + (d->len / PIN_SCAN_EVERY)))
        d->force_evacuate_head(&b->new_dir, dir_pinned(&b->new_dir));
#endif
      free_EvacuationBlock(b, d->mutex->thread_holding);
      return res;
    }
    b = b->link.next;
  }
  Debug("dir_lookaside", "fixup %X failed", key->word(0));
  return 0;
}

void
dir_lookaside_cleanup(Part * d)
{
  ink_debug_assert(d->mutex->thread_holding == this_ethread());
  for (int i = 0; i < LOOKASIDE_SIZE; i++) {
    EvacuationBlock *b = d->lookaside[i].head;
    while (b) {
      if (!dir_valid(d, &b->new_dir)) {
        EvacuationBlock *nb = b->link.next;
        Debug("dir_lookaside", "cleanup %X cleaned up", b->evac_frags.earliest_key.word(0));
        d->lookaside[i].remove(b);
        free_CacheVC(b->earliest_evacuator);
        free_EvacuationBlock(b, d->mutex->thread_holding);
        b = nb;
        goto Lagain;
      }
      b = b->link.next;
    Lagain:;
    }
  }
}

void
dir_lookaside_remove(CacheKey * key, Part * d)
{
  ink_debug_assert(d->mutex->thread_holding == this_ethread());
  int i = key->word(3) % LOOKASIDE_SIZE;
  EvacuationBlock *b = d->lookaside[i].head;
  while (b) {
    if (b->evac_frags.key == *key) {
      Debug("dir_lookaside", "remove %X offset %d phase %d",
            key->word(0), dir_offset(&b->new_dir), dir_phase(&b->new_dir));
      d->lookaside[i].remove(b);
      free_EvacuationBlock(b, d->mutex->thread_holding);
      return;
    }
    b = b->link.next;
  }
  Debug("dir_lookaside", "remove %X failed", key->word(0));
  return;
}

// Cache Sync
//

void
dir_sync_init()
{
  cacheDirSync = NEW(new CacheSync);
  cacheDirSync->trigger = eventProcessor.schedule_in(cacheDirSync, HRTIME_SECONDS(cache_config_dir_sync_frequency));
}

void
CacheSync::aio_write(int fd, char *b, int n, ink_off_t o)
{
  io.aiocb.aio_fildes = fd;
  io.aiocb.aio_offset = o;
  io.aiocb.aio_nbytes = n;
  io.aiocb.aio_buf = b;
  io.action = this;
  io.thread = mutex->thread_holding;
  ink_assert(ink_aio_write(&io) >= 0);
}

inku64
dir_entries_used(Part * d)
{

  inku64 full = 0;
  inku64 sfull = 0;
  for (int s = 0; s < DIR_SEGMENTS; full += sfull, s++) {
    Dir *seg = dir_segment(s, d);
    sfull = 0;
    for (int b = 0; b < d->buckets; b++) {
      Dir *e = dir_bucket(b, seg);
      if (dir_bucket_loop_fix(e, s, d)) {
        sfull = 0;
        break;
      }
      while (e) {
        if (dir_offset(e))
          sfull++;

        e = next_dir(e, seg);
        if (!e)
          break;
      }
    }
  }
  return full;
}

/*
 * this function flushes the cache meta data to disk when
 * the cache is shutdown. Must *NOT* be used during regular
 * operation.
 */

void
sync_cache_dir_on_shutdown(void)
{
  Debug("cache_dir_sync", "sync started");
  char *buf = NULL;
  int buflen = 0;

  EThread *t = (EThread *) 0xdeadbeef;
  for (int i = 0; i < gnpart; i++) {
    // the process is going down, do a blocking call
    // dont release the partition's lock, there could
    // be another aggWrite in progress
    MUTEX_TAKE_LOCK(gpart[i]->mutex, t);
    Part *d = gpart[i];

    if (DISK_BAD(d->disk)) {
      Debug("cache_dir_sync", "Dir %s: ignoring -- bad disk", d->hash_id);
      continue;
    }
    // Unused variable.
    // int headerlen = ROUND_TO_BLOCK(sizeof(PartHeaderFooter));
    int dirlen = part_dirlen(d);
    if (!d->header->dirty && !d->dir_sync_in_progress) {
      Debug("cache_dir_sync", "Dir %s: ignoring -- not dirty", d->hash_id);
      continue;
    }
#ifdef HIT_EVACUATE
    // recompute hit_evacuate_window
    d->hit_evacuate_window = (d->data_blocks * cache_config_hit_evacuate_percent) / 100;
#endif


    // check if we have data in the agg buffer
    // dont worry about the cachevc s in the agg queue
    // directories have not been inserted for these writes
    if (d->agg_buf_pos) {
      Debug("cache_dir_sync", "Dir %s: flushing agg buffer first", d->hash_id);

      // set write limit
      d->header->agg_pos = d->header->write_pos + d->agg_buf_pos;

      int r = pwrite(d->fd, d->agg_buffer, d->agg_buf_pos,
                     d->header->write_pos);
      if (r != d->agg_buf_pos) {
        ink_debug_assert(!"flusing agg buffer failed");
        continue;
      }
      d->header->last_write_pos = d->header->write_pos;
      d->header->write_pos += d->agg_buf_pos;
      ink_debug_assert(d->header->write_pos == d->header->agg_pos);
      d->agg_buf_pos = 0;
      d->header->write_serial++;
    }

    if (buflen < dirlen) {
      if (buf)
        ink_memalign_free(buf);
      buf = (char *) ink_memalign(sysconf(_SC_PAGESIZE), dirlen);       // buf = (char*) valloc (dirlen);
      buflen = dirlen;
    }

    if (!d->dir_sync_in_progress) {
      d->header->sync_serial++;
    } else {
      Debug("cache_dir_sync", "Periodic dir sync in progress -- overwriting");
    }
    d->footer->sync_serial = d->header->sync_serial;
#ifdef DEBUG
    check_dir(d);
#endif
    memcpy(buf, d->raw_dir, dirlen);
    int B = d->header->sync_serial & 1;
    ink_off_t start = d->skip + (B ? dirlen : 0);

    B = pwrite(d->fd, buf, dirlen, start);
    ink_debug_assert(B == dirlen);
    Debug("cache_dir_sync", "done syncing dir for part %s", d->hash_id);
  }
  Debug("cache_dir_sync", "sync done");
  if (buf)
    ink_memalign_free(buf);
}



int
CacheSync::mainEvent(int event, Event * e)
{
  NOWARN_UNUSED(e);
  NOWARN_UNUSED(event);

  if (trigger) {
    trigger->cancel_action();
    trigger = NULL;
  }

Lrestart:
  if (part >= gnpart) {
    part = 0;
    if (buf) {
      ink_memalign_free(buf);
      buf = 0;
      buflen = 0;
    }
    Debug("cache_dir_sync", "sync done");
    trigger = eventProcessor.schedule_in(this, HRTIME_SECONDS(cache_config_dir_sync_frequency));
    return EVENT_CONT;
  }
  if (event == AIO_EVENT_DONE) {
    // AIO Thread 
    if (io.aio_result != (int) io.aiocb.aio_nbytes) {
      Warning("part write error during directory sync '%s'", gpart[part]->hash_id);
      event = EVENT_NONE;
      goto Ldone;
    }
    trigger = eventProcessor.schedule_in(this, SYNC_DELAY);
    return EVENT_CONT;
  }
  {
    CACHE_TRY_LOCK(lock, gpart[part]->mutex, mutex->thread_holding);
    if (!lock) {
      trigger = eventProcessor.schedule_in(this, MUTEX_RETRY_DELAY);
      return EVENT_CONT;
    }
    Part *d = gpart[part];

#ifdef HIT_EVACUATE
    // recompute hit_evacuate_window
    d->hit_evacuate_window = (d->data_blocks * cache_config_hit_evacuate_percent) / 100;
#endif

    if (DISK_BAD(d->disk))
      goto Ldone;

    int headerlen = ROUND_TO_BLOCK(sizeof(PartHeaderFooter));
    int dirlen = part_dirlen(d);
    if (!writepos) {
      // start
      Debug("cache_dir_sync", "sync started");
      /* Don't sync the directory to disk if its not dirty. Syncing the
         clean directory to disk is also the cause of INKqa07151. Increasing
         the serial serial causes the cache to recover more data
         than necessary.
         The dirty bit it set in dir_insert, dir_overwrite and dir_delete_entry
       */
      if (!d->header->dirty) {
        Debug("cache_dir_sync", "Dir %s not dirty", d->hash_id);
        goto Ldone;
      }
      if (d->is_io_in_progress() || d->agg_buf_pos) {
        Debug("cache_dir_sync", "Dir %s: waiting for agg buffer", d->hash_id);
        d->dir_sync_waiting = 1;
        if (!d->is_io_in_progress())
          d->aggWrite(EVENT_IMMEDIATE, 0);
        return EVENT_CONT;
      }
      Debug("cache_dir_sync", "pos: %llu Dir %s dirty...syncing to disk", d->header->write_pos, d->hash_id);
      d->header->dirty = 0;
      if (buflen < dirlen) {
        if (buf)
          ink_memalign_free(buf);
        buf = (char *) ink_memalign(sysconf(_SC_PAGESIZE), dirlen);
        buflen = dirlen;
      }
      d->header->sync_serial++;
      d->footer->sync_serial = d->header->sync_serial;
#ifdef DEBUG
      check_dir(d);
#endif
      memcpy(buf, d->raw_dir, dirlen);
      d->dir_sync_in_progress = 1;
    }
    int B = d->header->sync_serial & 1;
    ink_off_t start = d->skip + (B ? dirlen : 0);

    if (!writepos) {
      // write header
      aio_write(d->fd, buf + writepos, headerlen, start + writepos);
      writepos += headerlen;
    } else if (writepos < dirlen - headerlen) {
      // write part of body
      int l = SYNC_MAX_WRITE;
      if (writepos + l > dirlen - headerlen)
        l = dirlen - headerlen - writepos;
      aio_write(d->fd, buf + writepos, l, start + writepos);
      writepos += l;
    } else if (writepos < dirlen) {
      ink_assert(writepos == dirlen - headerlen);
      // write footer
      aio_write(d->fd, buf + writepos, headerlen, start + writepos);
      writepos += headerlen;
    } else {
      d->dir_sync_in_progress = 0;
      goto Ldone;
    }
    return EVENT_CONT;
  }
Ldone:
  // done
  writepos = 0;
  part++;
  goto Lrestart;
}

//
// Check
//

#define HIST_DEPTH 8
int
Part::dir_check(bool fix)
{
  NOWARN_UNUSED(fix);
  int hist[HIST_DEPTH + 1] = { 0 };
  int shist[DIR_SEGMENTS] = { 0 };
  int j;
  int stale = 0, full = 0, empty = 0;
  int last = 0, free = 0;
  for (int s = 0; s < DIR_SEGMENTS; s++) {
    Dir *seg = dir_segment(s, this);
    for (int b = 0; b < buckets; b++) {
      int h = 0;
      Dir *e = dir_bucket(b, seg);
      while (e) {
        if (!dir_offset(e))
          empty++;
        else {
          h++;
          if (!dir_valid(this, e))
            stale++;
          else
            full++;
        }
        e = next_dir(e, seg);
        if (!e)
          break;
      }
      if (h > HIST_DEPTH)
        h = HIST_DEPTH;
      hist[h]++;
    }
    int t = stale + full;
    shist[s] = t - last;
    last = t;
    free += dir_freelist_length(this, s);
  }
  int total = buckets * DIR_SEGMENTS * DIR_DEPTH;
  printf("    Directory for [%s]\n", hash_id);
  printf("        Bytes:     %d\n", total * SIZEOF_DIR);
  printf("        Segments:  %d\n", DIR_SEGMENTS);
  printf("        Buckets:   %d\n", buckets);
  printf("        Entries:   %d\n", total);
  printf("        Full:      %d\n", full);
  printf("        Empty:     %d\n", empty);
  printf("        Stale:     %d\n", stale);
  printf("        Free:      %d\n", free);
  printf("        Bucket Fullness:   ");
  for (j = 0; j < HIST_DEPTH; j++) {
    printf("%5d ", hist[j]);
    if ((j % 5 == 4))
      printf("\n" "                           ");
  }
  printf("\n");
  printf("        Segment Fullness:  ");
  for (j = 0; j < DIR_SEGMENTS; j++) {
    printf("%5d ", shist[j]);
    if ((j % 5 == 4))
      printf("\n" "                           ");
  }
  printf("\n");
  printf("        Freelist Fullness: ");
  for (j = 0; j < DIR_SEGMENTS; j++) {
    printf("%5d ", dir_freelist_length(this, j));
    if ((j % 5 == 4))
      printf("\n" "                           ");
  }
  printf("\n");
  return 0;
}

//
// Static Tables
//

// permutation table
inku8 CacheKey_next_table[256] = {
  21, 53, 167, 51, 255, 126, 241, 151,
  115, 66, 155, 174, 226, 215, 80, 188,
  12, 95, 8, 24, 162, 201, 46, 104,
  79, 172, 39, 68, 56, 144, 142, 217,
  101, 62, 14, 108, 120, 90, 61, 47,
  132, 199, 110, 166, 83, 125, 57, 65,
  19, 130, 148, 116, 228, 189, 170, 1,
  71, 0, 252, 184, 168, 177, 88, 229,
  242, 237, 183, 55, 13, 212, 240, 81,
  211, 74, 195, 205, 147, 93, 30, 87,
  86, 63, 135, 102, 233, 106, 118, 163,
  107, 10, 243, 136, 160, 119, 43, 161,
  206, 141, 203, 78, 175, 36, 37, 140,
  224, 197, 185, 196, 248, 84, 122, 73,
  152, 157, 18, 225, 219, 145, 45, 2,
  171, 249, 173, 32, 143, 137, 69, 41,
  35, 89, 33, 98, 179, 214, 114, 231,
  251, 123, 180, 194, 29, 3, 178, 31,
  192, 164, 15, 234, 26, 230, 91, 156,
  5, 16, 23, 244, 58, 50, 4, 67,
  134, 165, 60, 235, 250, 7, 138, 216,
  49, 139, 191, 154, 11, 52, 239, 59,
  111, 245, 9, 64, 25, 129, 247, 232,
  190, 246, 109, 22, 112, 210, 221, 181,
  92, 169, 48, 100, 193, 77, 103, 133,
  70, 220, 207, 223, 176, 204, 76, 186,
  200, 208, 158, 182, 227, 222, 131, 38,
  187, 238, 6, 34, 253, 128, 146, 44,
  94, 127, 105, 153, 113, 20, 27, 124,
  159, 17, 72, 218, 96, 149, 213, 42,
  28, 254, 202, 40, 117, 82, 97, 209,
  54, 236, 121, 75, 85, 150, 99, 198,
};

// permutation table
inku8 CacheKey_prev_table[256] = {
  57, 55, 119, 141, 158, 152, 218, 165,
  18, 178, 89, 172, 16, 68, 34, 146,
  153, 233, 114, 48, 229, 0, 187, 154,
  19, 180, 148, 230, 240, 140, 78, 143,
  123, 130, 219, 128, 101, 102, 215, 26,
  243, 127, 239, 94, 223, 118, 22, 39,
  194, 168, 157, 3, 173, 1, 248, 67,
  28, 46, 156, 175, 162, 38, 33, 81,
  179, 47, 9, 159, 27, 126, 200, 56,
  234, 111, 73, 251, 206, 197, 99, 24,
  14, 71, 245, 44, 109, 252, 80, 79,
  62, 129, 37, 150, 192, 77, 224, 17,
  236, 246, 131, 254, 195, 32, 83, 198,
  23, 226, 85, 88, 35, 186, 42, 176,
  188, 228, 134, 8, 51, 244, 86, 93,
  36, 250, 110, 137, 231, 45, 5, 225,
  221, 181, 49, 214, 40, 199, 160, 82,
  91, 125, 166, 169, 103, 97, 30, 124,
  29, 117, 222, 76, 50, 237, 253, 7,
  112, 227, 171, 10, 151, 113, 210, 232,
  92, 95, 20, 87, 145, 161, 43, 2,
  60, 193, 54, 120, 25, 122, 11, 100,
  204, 61, 142, 132, 138, 191, 211, 66,
  59, 106, 207, 216, 15, 53, 184, 170,
  144, 196, 139, 74, 107, 105, 255, 41,
  208, 21, 242, 98, 205, 75, 96, 202,
  209, 247, 189, 72, 69, 238, 133, 13,
  167, 31, 235, 116, 201, 190, 213, 203,
  104, 115, 12, 212, 52, 63, 149, 135,
  183, 84, 147, 163, 249, 65, 217, 174,
  70, 6, 64, 90, 155, 177, 185, 182,
  108, 121, 164, 136, 58, 220, 241, 4,
};

//
// Regression
//
unsigned int regress_rand_seed = 0;
void
regress_rand_init(unsigned int i)
{
  regress_rand_seed = i;
}

void
regress_rand_CacheKey(CacheKey * key)
{
  unsigned int *x = (unsigned int *) key;
  for (int i = 0; i < 4; i++)
    x[i] = next_rand(&regress_rand_seed);
}

void
dir_corrupt_bucket(Dir * b, int s, Part * d)
{
  // coverity[secure_coding]
  int l = ((int) (dir_bucket_length(b, s, d) * drand48()));
  Dir *e = b;
  Dir *seg = dir_segment(s, d);
  for (int i = 0; i < l; i++) {
    ink_release_assert(e);
    e = next_dir(e, seg);
  }
  dir_next(e) = dir_to_offset(e, seg);
}

struct CacheDirReg:Continuation
{
  int *status;

    CacheDirReg(int *_status):status(_status)
  {
    SET_HANDLER(&CacheDirReg::signal_reg);
    eventProcessor.schedule_in(this, 120 * HRTIME_SECOND);

  }

  int signal_reg(int event, Event * e)
  {
    NOWARN_UNUSED(e);
    NOWARN_UNUSED(event);

    *status = REGRESSION_TEST_PASSED;
    return EVENT_DONE;
  }

};

EXCLUSIVE_REGRESSION_TEST(Cache_dir) (RegressionTest * t, int atype, int *status) {
  NOWARN_UNUSED(atype);
  ink_hrtime ttime;
  int ret = REGRESSION_TEST_PASSED;

  if ((CacheProcessor::IsCacheEnabled() != CACHE_INITIALIZED) || gnpart < 1) {
    rprintf(t, "cache not ready/configured");
    *status = REGRESSION_TEST_FAILED;
    return;
  }
  Part *d = gpart[0];
  EThread *thread = this_ethread();
  MUTEX_TRY_LOCK(lock, d->mutex, thread);
  ink_release_assert(lock);
  rprintf(t, "clearing part 0\n", free);
  part_dir_clear(d);

  // coverity[var_decl]
  Dir dir;
  dir_clear(&dir);
  dir_set_phase(&dir, 0);
  dir_set_head(&dir, true);
  dir_set_offset(&dir, 1);

  d->header->agg_pos = d->header->write_pos += 1024;

  CacheKey key;
  rand_CacheKey(&key, thread->mutex);

  int s = key.word(0) % DIR_SEGMENTS, i, j;
  Dir *seg = dir_segment(s, d);

  // test insert
  rprintf(t, "insert test\n", free);
  int inserted = 0;
  int free = dir_freelist_length(d, s);
  int n = free;
  rprintf(t, "free: %d\n", free);
  while (n--) {
    if (!dir_insert(&key, d, &dir))
      break;
    inserted++;
  }
  rprintf(t, "inserted: %d\n", inserted);
  if ((unsigned int) (inserted - free) > 1)
    ret = REGRESSION_TEST_FAILED;

  // test delete
  rprintf(t, "delete test\n");
  for (i = 0; i < d->buckets; i++)
    for (j = 0; j < DIR_DEPTH; j++)
      dir_set_offset(dir_bucket_row(dir_bucket(i, seg), j), 0); // delete
  dir_clean_segment(s, d);
  int newfree = dir_freelist_length(d, s);
  rprintf(t, "newfree: %d\n", newfree);
  if ((unsigned int) (newfree - free) > 1)
    ret = REGRESSION_TEST_FAILED;

  // test insert-delete
  rprintf(t, "insert-delete test\n");
  regress_rand_init(13);
  ttime = ink_get_hrtime_internal();
  for (i = 0; i < newfree; i++) {
    regress_rand_CacheKey(&key);
    dir_insert(&key, d, &dir);
  }
  inku64 us = (ink_get_hrtime_internal() - ttime) / HRTIME_USECOND;
  //On windows us is sometimes 0. I don't know why.
  //printout the insert rate only if its not 0
  if (us)
    rprintf(t, "insert rate = %d / second\n", (int) ((newfree * (inku64) 1000000) / us));
  regress_rand_init(13);
  ttime = ink_get_hrtime_internal();
  for (i = 0; i < newfree; i++) {
    Dir *last_collision = 0;
    regress_rand_CacheKey(&key);
    if (!dir_probe(&key, d, &dir, &last_collision))
      ret = REGRESSION_TEST_FAILED;
  }
  us = (ink_get_hrtime_internal() - ttime) / HRTIME_USECOND;
  //On windows us is sometimes 0. I don't know why.
  //printout the probe rate only if its not 0
  if (us)
    rprintf(t, "probe rate = %d / second\n", (int) ((newfree * (inku64) 1000000) / us));


  for (int c = 0; c < part_dirlen(d) * 0.1; c++) {
    regress_rand_CacheKey(&key);
    dir_insert(&key, d, &dir);
  }


  /* introduce loops */
  /* in bucket */
#if 1
  for (int sno = 0; sno < DIR_SEGMENTS; sno++) {
    for (int bno = 0; bno < d->buckets; bno++) {
      // coverity[secure_coding]
      if (drand48() < 0.01)
        rprintf(t, "creating loop in bucket %d, seg %d\n", bno, sno);
      dir_corrupt_bucket(dir_bucket(bno, dir_segment(sno, d)), sno, d);
    }
  }

#else
  Dir *last_collision = 0, *seg1 = 0;
  int s1, b1;

  for (int ntimes = 0; ntimes < 1000; ntimes++) {
    rprintf(t, "dir_probe in bucket with loop\n");
    rand_CacheKey(&key, thread->mutex);
    s1 = key.word(0) % DIR_SEGMENTS;
    b1 = (key.word(0) / DIR_SEGMENTS) % d->buckets;
    dir_corrupt_bucket(dir_bucket(b1, dir_segment(s1, d)), s1, d);
    dir_insert(&key, d, &dir);
    last_collision = 0;
    dir_probe(&key, d, &dir, &last_collision);


    rand_CacheKey(&key, thread->mutex);
    s1 = key.word(0) % DIR_SEGMENTS;
    b1 = (key.word(0) / DIR_SEGMENTS) % d->buckets;
    dir_corrupt_bucket(dir_bucket(b1, dir_segment(s1, d)), s1, d);

    last_collision = 0;
    dir_probe(&key, d, &dir, &last_collision);

    rprintf(t, "dir_overwrite in bucket with loop\n");
    rand_CacheKey(&key, thread->mutex);
    s1 = key.word(0) % DIR_SEGMENTS;
    b1 = (key.word(0) / DIR_SEGMENTS) % d->buckets;
    CacheKey key1;
    key1.b[1] = 127;
    Dir dir1 = dir;
    dir_set_offset(&dir1, 23);
    dir_insert(&key1, d, &dir1);
    dir_insert(&key, d, &dir);
    key1.b[1] = 80;
    dir_insert(&key1, d, &dir1);
    dir_corrupt_bucket(dir_bucket(b1, dir_segment(s1, d)), s1, d);
    dir_overwrite(&key, d, &dir, &dir, 1);

    rand_CacheKey(&key, thread->mutex);
    s1 = key.word(0) % DIR_SEGMENTS;
    b1 = (key.word(0) / DIR_SEGMENTS) % d->buckets;
    key.b[1] = 23;
    dir_insert(&key, d, &dir1);
    dir_corrupt_bucket(dir_bucket(b1, dir_segment(s1, d)), s1, d);
    dir_overwrite(&key, d, &dir, &dir, 0);


    rand_CacheKey(&key, thread->mutex);
    s1 = key.word(0) % DIR_SEGMENTS;
    seg1 = dir_segment(s1, d);
    rprintf(t, "dir_freelist_length in freelist with loop: segment %d\n", s1);
    dir_corrupt_bucket(dir_from_offset(d->header->freelist[s], seg1), s1, d);
    dir_freelist_length(d, s1);



    rand_CacheKey(&key, thread->mutex);
    s1 = key.word(0) % DIR_SEGMENTS;
    b1 = (key.word(0) / DIR_SEGMENTS) % d->buckets;
    rprintf(t, "dir_bucket_length in bucket with loop: segment %d\n", s1);
    dir_corrupt_bucket(dir_bucket(b1, dir_segment(s1, d)), s1, d);
    dir_bucket_length(dir_bucket(b1, dir_segment(s1, d)), s1, d);



    rand_CacheKey(&key, thread->mutex);
    s1 = key.word(0) % DIR_SEGMENTS;
    b1 = (key.word(0) / DIR_SEGMENTS) % d->buckets;
    rprintf(t, "check_dir in bucket with loop: segment %d %d %d\n", 3, 23, 17);
    dir_corrupt_bucket(dir_bucket(b1, dir_segment(3, d)), 3, d);
    dir_corrupt_bucket(dir_bucket(b1, dir_segment(7, d)), 7, d);
    dir_corrupt_bucket(dir_bucket(b1, dir_segment(17, d)), 17, d);
    check_dir(d);
  }
  //part_dir_clear(d);
#endif

  NEW(new CacheDirReg(status));
}
