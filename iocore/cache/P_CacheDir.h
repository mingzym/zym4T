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


#ifndef _P_CACHE_DIR_H__
#define _P_CACHE_DIR_H__

#include "P_CacheHttp.h"

struct Part;
struct CacheVC;

/*
  Directory layout
*/

// Constants

#define DIR_TAG_WIDTH			12
#define DIR_MASK_TAG(_t)                ((_t) & ((1 << DIR_TAG_WIDTH) - 1))
#define SIZEOF_DIR           		10
#define ESTIMATED_OBJECT_SIZE           8000

#define MAX_DIR_SEGMENTS                (32 * (1<<16))
#define DIR_DEPTH                       4
#define DIR_SIZE_WIDTH                  6
#define DIR_BLOCK_SIZES                 4
#define DIR_BLOCK_SHIFT(_i)             (3*(_i))
#define DIR_BLOCK_SIZE(_i)              (INK_BLOCK_SIZE << DIR_BLOCK_SHIFT(_i))
#define DIR_SIZE_WITH_BLOCK(_i)         ((1<<DIR_SIZE_WIDTH) * DIR_BLOCK_SIZE(_i))
#define DIR_OFFSET_BITS                 40
#define DIR_OFFSET_MAX                  ((((ink_off_t)1) << DIR_OFFSET_BITS) - 1)
#define MAX_DOC_SIZE                    ((1<<DIR_SIZE_WIDTH)*(1<<B8K_SHIFT)) // 1MB

#define SYNC_MAX_WRITE                  (2 * 1024 * 1024)
#define SYNC_DELAY                      HRTIME_MSECONDS(500)

// Debugging Options

//#define DO_CHECK_DIR_FAST
//#define DO_CHECK_DIR

// Macros

#ifdef DO_CHECK_DIR
#define CHECK_DIR(_d) ink_debug_assert(check_dir(_d))
#else
#define CHECK_DIR(_d) ((void)0)
#endif

#define dir_index(_e, _i) ((Dir*)((char*)(_e)->dir + (SIZEOF_DIR * (_i))))
#define dir_assign(_e,_x) do {                \
    (_e)->w[0] = (_x)->w[0];                  \
    (_e)->w[1] = (_x)->w[1];                  \
    (_e)->w[2] = (_x)->w[2];                  \
    (_e)->w[3] = (_x)->w[3];                  \
    (_e)->w[4] = (_x)->w[4];                  \
} while (0)
#define dir_assign_data(_e,_x) do {           \
  unsigned short next = dir_next(_e);         \
  dir_assign(_e, _x);		              \
  dir_set_next(_e, next);                     \
} while(0)
// entry is valid
#define dir_valid(_d, _e)                                               \
  (_d->header->phase == dir_phase(_e) ? part_in_phase_valid(_d, _e) :   \
                                        part_out_of_phase_valid(_d, _e))
// entry is valid and outside of write aggregation region
#define dir_agg_valid(_d, _e)                                            \
  (_d->header->phase == dir_phase(_e) ? part_in_phase_valid(_d, _e) :    \
                                        part_out_of_phase_agg_valid(_d, _e))
// entry may be valid or overwritten in the last aggregated write
#define dir_write_valid(_d, _e)                                         \
  (_d->header->phase == dir_phase(_e) ? part_in_phase_valid(_d, _e) :   \
                                        part_out_of_phase_write_valid(_d, _e))
#define dir_agg_buf_valid(_d, _e)                                          \
  (_d->header->phase == dir_phase(_e) && part_in_phase_agg_buf_valid(_d, _e))
#define dir_is_empty(_e) (!dir_offset(_e))
#define dir_clear(_e) do { \
    (_e)->w[0] = 0; \
    (_e)->w[1] = 0; \
    (_e)->w[2] = 0; \
    (_e)->w[3] = 0; \
    (_e)->w[4] = 0; \
} while (0)
#define dir_clean(_e) dir_set_offset(_e,0)
#define dir_segment(_s, _d) part_dir_segment(_d, _s)

// OpenDir

#define OPEN_DIR_BUCKETS           256

struct EvacuationBlock;
typedef inku32 DirInfo;
// Global Data

// Cache Directory

// INTERNAL: do not access these members directly, use the
// accessors below (e.g. dir_offset, dir_set_offset).
// These structures are stored in memory 2 byte aligned.
// The accessors prevent unaligned memory access which
// is often either less efficient or unsupported depending
// on the processor.
struct Dir
{
#if 0
  // bits are numbered from lowest in u16 to highest
  // always index as u16 to avoid byte order issues
  unsigned int offset:24;       // (0,1:0-7) 16M * 512 = 8GB
  unsigned int big:2;           // (1:8-9) 512 << (3 * big)
  unsigned int size:6;          // (1:10-15) 6**2 = 64, 64*512 = 32768 .. 64*256=16MB
  unsigned int tag:12;          // (2:0-11) 2048 / 8 entries/bucket = .4%
  unsigned int phase:1;         // (2:12)
  unsigned int head:1;          // (2:13) first segment in a document
  unsigned int pinned:1;        // (2:14)
  unsigned int token:1;         // (2:15)
  unsigned int next:16;         // (3)
  inku16 offset_high;           // 8GB * 65k = 0.5PB (4)
#else
  inku16 w[5];
  Dir() { dir_clear(this); }
#endif
};

// INTERNAL: do not access these members directly, use the
// accessors below (e.g. dir_offset, dir_set_offset)
struct FreeDir
{
#if 0
  unsigned int offset:24;       // 0: empty
  unsigned int reserved:8;
  unsigned int prev:16;         // (2)
  unsigned int next:16;         // (3)
  inku16 offset_high;           // 0: empty
#else
  inku16 w[5];
  FreeDir() { dir_clear(this); }
#endif
};

#define dir_bit(_e, _w, _b) (((_e)->w[_w] >> (_b)) & 1)
#define dir_set_bit(_e, _w, _b, _v) (_e)->w[_w] = (inku16)(((_e)->w[_w] & ~(1<<(_b))) | (((_v)?1:0)<<(_b)))
#define dir_offset(_e) ((ink64)                                         \
                         (((inku64)(_e)->w[0]) |                        \
                          (((inku64)((_e)->w[1] & 0xFF)) << 16) |       \
                          (((inku64)(_e)->w[4]) << 24)))
#define dir_set_offset(_e,_o) do { \
    (_e)->w[0] = (inku16)_o;                                                    \
    (_e)->w[1] = (inku16)((((_o) >> 16) & 0xFF) | ((_e)->w[1] & 0xFF00));       \
    (_e)->w[4] = (inku16)((_o) >> 24);                                          \
} while (0)
#define dir_big(_e) ((inku32)((((_e)->w[1]) >> 8)&0x3))
#define dir_set_big(_e, _v) (_e)->w[1] = (inku16)(((_e)->w[1] & 0xFCFF) | (((inku16)(_v))&0x3)<<8)
#define dir_size(_e) ((inku32)(((_e)->w[1]) >> 10))
#define dir_set_size(_e, _v) (_e)->w[1] = (inku16)(((_e)->w[1] & ((1<<10)-1)) | ((_v)<<10))
#define dir_set_approx_size(_e, _s) do {                         \
  if ((_s) <= DIR_SIZE_WITH_BLOCK(0)) {                          \
    dir_set_big(_e,0);                                           \
    dir_set_size(_e,((_s)-1) / DIR_BLOCK_SIZE(0));               \
  } else if ((_s) <= DIR_SIZE_WITH_BLOCK(1)) {                   \
    dir_set_big(_e,1);                                           \
    dir_set_size(_e,((_s)-1) / DIR_BLOCK_SIZE(1));               \
  } else if ((_s) <= DIR_SIZE_WITH_BLOCK(2)) {                   \
    dir_set_big(_e,2);                                           \
    dir_set_size(_e,((_s)-1) / DIR_BLOCK_SIZE(2));               \
  } else {                                                       \
    dir_set_big(_e,3);                                           \
    dir_set_size(_e,((_s)-1) / DIR_BLOCK_SIZE(3));               \
  }                                                              \
} while (0)
#define dir_approx_size(_e) ((dir_size(_e) + 1) * DIR_BLOCK_SIZE(dir_big(_e)))
#define round_to_approx_size(_s) (_s <= DIR_SIZE_WITH_BLOCK(0) ? ROUND_TO(_s, DIR_BLOCK_SIZE(0)) : \
                                  (_s <= DIR_SIZE_WITH_BLOCK(1) ? ROUND_TO(_s, DIR_BLOCK_SIZE(1)) : \
                                   (_s <= DIR_SIZE_WITH_BLOCK(2) ? ROUND_TO(_s, DIR_BLOCK_SIZE(2)) : \
                                    ROUND_TO(_s, DIR_BLOCK_SIZE(3)))))
#define dir_tag(_e) ((_e)->w[2]&((1<<DIR_TAG_WIDTH)-1))
#define dir_set_tag(_e,_t) (_e)->w[2] = (inku16)(((_e)->w[2]&~((1<<DIR_TAG_WIDTH)-1)) | ((_t)&((1<<DIR_TAG_WIDTH)-1)))
#define dir_phase(_e) dir_bit(_e,2,12)
#define dir_set_phase(_e,_v) dir_set_bit(_e,2,12,_v)
#define dir_head(_e) dir_bit(_e,2,13)
#define dir_set_head(_e, _v) dir_set_bit(_e,2,13,_v)
#define dir_pinned(_e) dir_bit(_e,2,14)
#define dir_set_pinned(_e, _v) dir_set_bit(_e,2,14,_v)
#define dir_token(_e) dir_bit(_e,2,15)
#define dir_set_token(_e, _v) dir_set_bit(_e,2,15,_v)
#define dir_next(_e) (_e)->w[3]
#define dir_set_next(_e, _o) (_e)->w[3] = (inku16)(_o)
#define dir_prev(_e) (_e)->w[2]
#define dir_set_prev(_e,_o) (_e)->w[2] = (inku16)(_o)

// INKqa11166 - Cache can not store 2 HTTP alternates simultaneously.
// To allow this, move the vector from the CacheVC to the OpenDirEntry.
// Each CacheVC now maintains a pointer to this vector. Adding/Deleting
// alternates from this vector is done under the Part::lock. The alternate
// is deleted/inserted into the vector just before writing the vector disk 
// (CacheVC::updateVector). 
struct OpenDirEntry
{
  DLL<CacheVC> writers;         // list of all the current writers
  DLL<CacheVC> readers;         // list of all the current readers - not used
  CacheHTTPInfoVector vector;   // Vector for the http document. Each writer 
                                // maintains a pointer to this vector and 
                                // writes it down to disk. 
  CacheKey single_doc_key;      // Key for the resident alternate. 
  Dir single_doc_dir;           // Directory for the resident alternate
  Dir first_dir;                // Dir for the vector. If empty, a new dir is 
                                // inserted, otherwise this dir is overwritten
  inku16 num_writers;           // num of current writers
  inku16 max_writers;           // max number of simultaneous writers allowed
  bool dont_update_directory;   // if set, the first_dir is not updated.
  bool move_resident_alt;       // if set, single_doc_dir is inserted.
  volatile bool reading_vec;    // somebody is currently reading the vector
  volatile bool writing_vec;    // somebody is currently writing the vector

  Link<OpenDirEntry> link;

  int wait(CacheVC *c, int msec);

  bool has_multiple_writers()
  {
    return num_writers > 1;
  }
};

struct OpenDir:Continuation
{
  Queue<CacheVC> delayed_readers;
  DLL<OpenDirEntry> bucket[OPEN_DIR_BUCKETS];

  int open_write(CacheVC *c, int allow_if_writers, int max_writers);
  int close_write(CacheVC *c);
  OpenDirEntry *open_read(INK_MD5 *key);
  int signal_readers(int event, Event *e);

  OpenDir();
};

struct CacheSync:Continuation
{
  int part;
  char *buf;
  int buflen;
  int writepos;
  AIOCallbackInternal io;
  Event *trigger;
  int mainEvent(int event, Event *e);
  void aio_write(int fd, char *b, int n, ink_off_t o);

  CacheSync():Continuation(new_ProxyMutex()), part(0), buf(0), buflen(0), writepos(0), trigger(0)
  {
    SET_HANDLER(&CacheSync::mainEvent);
  }
};

// Global Functions

void part_init_dir(Part *d);
int dir_token_probe(CacheKey *, Part *, Dir *);
int dir_probe(CacheKey *, Part *, Dir *, Dir **);
int dir_insert(CacheKey *key, Part *d, Dir *to_part);
int dir_overwrite(CacheKey *key, Part *d, Dir *to_part, Dir *overwrite, bool must_overwrite = true);
int dir_delete(CacheKey *key, Part *d, Dir *del);
int dir_lookaside_probe(CacheKey *key, Part *d, Dir *result, EvacuationBlock ** eblock);
int dir_lookaside_insert(EvacuationBlock *b, Part *d, Dir *to);
int dir_lookaside_fixup(CacheKey *key, Part *d);
void dir_lookaside_cleanup(Part *d);
void dir_lookaside_remove(CacheKey *key, Part *d);
void dir_free_entry(Dir *e, int s, Part *d);
void dir_sync_init();
int check_dir(Part *d);
void dir_clean_part(Part *d);
void dir_clear_range(ink_off_t start, ink_off_t end, Part *d);
int dir_segment_accounted(int s, Part *d, int offby = 0,
                          int *free = 0, int *used = 0,
                          int *empty = 0, int *valid = 0, int *agg_valid = 0, int *avg_size = 0);
inku64 dir_entries_used(Part *d);
void sync_cache_dir_on_shutdown();

// Global Data

extern Dir empty_dir;

// Inline Funtions

#define dir_in_seg(_s, _i) ((Dir*)(((char*)(_s)) + (SIZEOF_DIR *(_i))))

inline bool
dir_compare_tag(Dir *e, CacheKey *key)
{
  return (dir_tag(e) == DIR_MASK_TAG(key->word(2)));
}

inline Dir *
dir_from_offset(int i, Dir *seg)
{
#if DIR_DEPTH < 5
  if (!i)
    return 0;
  return dir_in_seg(seg, i);
#else
  i = i + ((i - 1) / (DIR_DEPTH - 1));
  return dir_in_seg(seg, i);
#endif
}
inline Dir *
next_dir(Dir *d, Dir *seg)
{
  int i = dir_next(d);
  return dir_from_offset(i, seg);
}
inline int
dir_to_offset(Dir *d, Dir *seg)
{
#if DIR_DEPTH < 5
  return (((char*)d) - ((char*)seg))/SIZEOF_DIR;
#else
  int i = (int)((((char*)d) - ((char*)seg))/SIZEOF_DIR);
  i = i - (i / DIR_DEPTH);
  return i;
#endif
}
inline Dir *
dir_bucket(int b, Dir *seg)
{
  return dir_in_seg(seg, b * DIR_DEPTH);
}
inline Dir *
dir_bucket_row(Dir *b, int i)
{
  return dir_in_seg(b, i);
}

#endif /* _P_CACHE_DIR_H__ */
