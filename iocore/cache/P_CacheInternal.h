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


#ifndef _P_CACHE_INTERNAL_H__
#define _P_CACHE_INTERNAL_H__

#ifdef HTTP_CACHE
#include "HTTP.h"
#include "P_CacheHttp.h"
#endif

#include "inktomi++.h"
#include "api/include/ts.h"

class EvacuationBlock;

// Compilation Options

//#define HIT_EVACUATE                    1
#define ALTERNATES                      1
// #define CACHE_LOCK_FAIL_RATE         0.001
// #define CACHE_AGG_FAIL_RATE          0.005
// #define CACHE_INSPECTOR_PAGES
#define MAX_CACHE_VCS_PER_THREAD        500

#define INTEGRAL_FRAGS                  4

#ifdef CACHE_INSPECTOR_PAGES
#ifdef DEBUG
#define CACHE_STAT_PAGES
#endif
#endif

#ifdef DEBUG
#define DDebug Debug
#else
#define DDebug if (0) dummy_debug
#endif

#define AIO_SOFT_FAILURE                -100000
// retry read from writer delay
#define WRITER_RETRY_DELAY  HRTIME_MSECONDS(50)

#ifndef CACHE_LOCK_FAIL_RATE
#define CACHE_TRY_LOCK(_l, _m, _t) MUTEX_TRY_LOCK(_l, _m, _t)
#else
#define CACHE_TRY_LOCK(_l, _m, _t)                             \
  MUTEX_TRY_LOCK(_l, _m, _t);                                  \
  if ((inku32)_t->generator.random() <                         \
     (inku32)(UINT_MAX *CACHE_LOCK_FAIL_RATE))                 \
    CACHE_MUTEX_RELEASE(_l)
#endif


#define VC_LOCK_RETRY_EVENT() \
  do { \
    trigger = mutex->thread_holding->schedule_in_local(this,MUTEX_RETRY_DELAY,event); \
    return EVENT_CONT; \
  } while (0)

#define VC_SCHED_LOCK_RETRY() \
  do { \
    trigger = mutex->thread_holding->schedule_in_local(this,MUTEX_RETRY_DELAY); \
    return EVENT_CONT; \
  } while (0)

#define CONT_SCHED_LOCK_RETRY_RET(_c) \
  do { \
    _c->mutex->thread_holding->schedule_in_local(_c, MUTEX_RETRY_DELAY); \
    return EVENT_CONT; \
  } while (0)

#define CONT_SCHED_LOCK_RETRY(_c) \
  _c->mutex->thread_holding->schedule_in_local(_c, MUTEX_RETRY_DELAY)

#define VC_SCHED_WRITER_RETRY() \
  do { \
    ink_assert(!trigger); \
    writer_lock_retry++; \
    ink_hrtime _t = WRITER_RETRY_DELAY; \
    if (writer_lock_retry > 2) \
      _t = WRITER_RETRY_DELAY * 2; \
    else if (writer_lock_retry > 5) \
      _t = WRITER_RETRY_DELAY * 10; \
    else if (writer_lock_retry > 10) \
      _t = WRITER_RETRY_DELAY * 100; \
    trigger = mutex->thread_holding->schedule_in_local(this, _t); \
    return EVENT_CONT; \
  } while (0)


  // cache stats definitions
enum
{
  cache_bytes_used_stat,
  cache_bytes_total_stat,
  cache_ram_cache_bytes_stat,
  cache_ram_cache_bytes_total_stat,
  cache_direntries_total_stat,
  cache_direntries_used_stat,
  cache_ram_cache_hits_stat,
  cache_ram_cache_misses_stat,
  cache_pread_count_stat,
  cache_percent_full_stat,
  cache_lookup_active_stat,
  cache_lookup_success_stat,
  cache_lookup_failure_stat,
  cache_read_active_stat,
  cache_read_success_stat,
  cache_read_failure_stat,
  cache_write_active_stat,
  cache_write_success_stat,
  cache_write_failure_stat,
  cache_write_backlog_failure_stat,
  cache_update_active_stat,
  cache_update_success_stat,
  cache_update_failure_stat,
  cache_remove_active_stat,
  cache_remove_success_stat,
  cache_remove_failure_stat,
  cache_evacuate_active_stat,
  cache_evacuate_success_stat,
  cache_evacuate_failure_stat,
  cache_scan_active_stat,
  cache_scan_success_stat,
  cache_scan_failure_stat,
  cache_directory_collision_count_stat,
  cache_single_fragment_document_count_stat,
  cache_two_fragment_document_count_stat,
  cache_three_plus_plus_fragment_document_count_stat,
  cache_read_busy_success_stat,
  cache_read_busy_failure_stat,
  cache_gc_bytes_evacuated_stat,
  cache_gc_frags_evacuated_stat,
  cache_write_bytes_stat,
  cache_hdr_vector_marshal_stat,
  cache_hdr_marshal_stat,
  cache_hdr_marshal_bytes_stat,
  cache_stat_count
};


extern RecRawStatBlock *cache_rsb;

#define GLOBAL_CACHE_SET_DYN_STAT(x,y) \
	RecSetGlobalRawStatSum(cache_rsb, (x), (y))

#define CACHE_SET_DYN_STAT(x,y) \
	RecSetGlobalRawStatSum(cache_rsb, (x), (y)) \
	RecSetGlobalRawStatSum(part->cache_part->part_rsb, (x), (y))

#define CACHE_INCREMENT_DYN_STAT(x) \
	RecIncrRawStat(cache_rsb, mutex->thread_holding, (int) (x), 1); \
	RecIncrRawStat(part->cache_part->part_rsb, mutex->thread_holding, (int) (x), 1);

#define CACHE_DECREMENT_DYN_STAT(x) \
	RecIncrRawStat(cache_rsb, mutex->thread_holding, (int) (x), -1); \
	RecIncrRawStat(part->cache_part->part_rsb, mutex->thread_holding, (int) (x), -1);

#define CACHE_PART_SUM_DYN_STAT(x,y) \
        RecIncrRawStat(part->cache_part->part_rsb, mutex->thread_holding, (int) (x), (int) y);

#define CACHE_SUM_DYN_STAT(x, y) \
	RecIncrRawStat(cache_rsb, mutex->thread_holding, (int) (x), (int) (y)); \
	RecIncrRawStat(part->cache_part->part_rsb, mutex->thread_holding, (int) (x), (int) (y));

#define GLOBAL_CACHE_SUM_GLOBAL_DYN_STAT(x, y) \
	RecIncrGlobalRawStatSum(cache_rsb,(x),(y))

#define CACHE_SUM_GLOBAL_DYN_STAT(x, y) \
	RecIncrGlobalRawStatSum(cache_rsb,(x),(y)) \
	RecIncrGlobalRawStatSum(part->cache_part->part_rsb,(x),(y))

#define CACHE_CLEAR_DYN_STAT(x) \
do { \
	RecSetRawStatSum(cache_rsb, (x), 0); \
	RecSetRawStatCount(cache_rsb, (x), 0); \
	RecSetRawStatSum(part->cache_part->part_rsb, (x), 0); \
	RecSetRawStatCount(part->cache_part->part_rsb, (x), 0); \
} while (0);

// Configuration
extern int cache_config_dir_sync_frequency;
extern int cache_config_http_max_alts;
extern int cache_config_permit_pinning;
extern int cache_config_select_alternate;
extern int cache_config_vary_on_user_agent;
extern int cache_config_max_doc_size;
extern int cache_config_min_average_object_size;
extern int cache_config_agg_write_backlog;
extern int cache_config_enable_checksum;
extern int cache_config_alt_rewrite_max_size;
extern int cache_config_read_while_writer;
extern char cache_system_config_directory[PATH_NAME_MAX + 1];
extern int cache_clustering_enabled;
#ifdef HIT_EVACUATE
extern int cache_config_hit_evacuate_percent;
extern int cache_config_hit_evacuate_size_limit;
#endif

// CacheVC
struct CacheVC:CacheVConnection
{
  CacheVC();

  VIO *do_io_read(Continuation *c, ink64 nbytes, MIOBuffer *buf);
  VIO *do_io_pread(Continuation *c, ink64 nbytes, MIOBuffer *buf, ink64 offset);
  VIO *do_io_write(Continuation *c, ink64 nbytes, IOBufferReader *buf, bool owner = false);
  void do_io_close(int lerrno = -1);
  void reenable(VIO *avio);
  void reenable_re(VIO *avio);
  bool get_data(int i, void *data);
  bool set_data(int i, void *data);
  Action *action()
  {
    return &_action;
  }
  bool is_ram_cache_hit()
  {
    ink_assert(vio.op == VIO::READ);
    return !f.not_from_ram_cache;
  }
  int get_header(void **ptr, int *len) 
  {
    Doc *doc = (Doc*)first_buf->data();
    *ptr = doc->hdr();
    *len = doc->hlen;   
    return 0;
  }
  int set_header(void *ptr, int len) 
  {
    header_to_write = ptr;
    header_to_write_len = len;
    return 0;
  }

  bool writer_done();
  int calluser(int event);
  int callcont(int event);
  int die();
  int dead(int event, Event *e);

  int handleReadDone(int event, Event *e);
  int handleRead(int event, Event *e);
  int do_read_call(CacheKey *akey);
  int handleWrite(int event, Event *e);
  int handleWriteLock(int event, Event *e);
  int do_write_call();
  int do_write_lock();
  int do_write_lock_call();

  int openReadClose(int event, Event *e);
  int openReadReadDone(int event, Event *e);
  int openReadMain(int event, Event *e);
  int openReadStartEarliest(int event, Event *e);
#ifdef HTTP_CACHE
  int openReadVecWrite(int event, Event *e);
#endif
  int openReadStartHead(int event, Event *e);
  int openReadFromWriter(int event, Event *e);
  int openReadFromWriterMain(int event, Event *e);
  int openReadFromWriterFailure(int event, Event *);
  int openReadChooseWriter(int event, Event *e);

  int openWriteCloseDir(int event, Event *e);
  int openWriteCloseHeadDone(int event, Event *e);
  int openWriteCloseHead(int event, Event *e);
  int openWriteCloseDataDone(int event, Event *e);
  int openWriteClose(int event, Event *e);
  int openWriteRemoveVector(int event, Event *e);
  int openWriteWriteDone(int event, Event *e);
  int openWriteOverwrite(int event, Event *e);
  int openWriteMain(int event, Event *e);
  int openWriteStartDone(int event, Event *e);
  int openWriteStartBegin(int event, Event *e);

  int updateVector(int event, Event *e);
  int updateReadDone(int event, Event *e);
  int updateVecWrite(int event, Event *e);

  int removeEvent(int event, Event *e);

  int linkWrite(int event, Event *e);
  int derefRead(int event, Event *e);

  int scanPart(int event, Event *e);
  int scanObject(int event, Event *e);
  int scanUpdateDone(int event, Event *e);
  int scanOpenWrite(int event, Event *e);
  int scanRemoveDone(int event, Event *e);

  int is_io_in_progress()
  {
    return io.aiocb.aio_fildes != AIO_NOT_IN_PROGRESS;
  }
  void set_io_not_in_progress()
  {
    io.aiocb.aio_fildes = AIO_NOT_IN_PROGRESS;
  }
  void set_agg_write_in_progress()
  {
    io.aiocb.aio_fildes = AIO_AGG_WRITE_IN_PROGRESS;
  }
  int evacuateDocDone(int event, Event *e);
  int evacuateReadHead(int event, Event *e);

  void cancel_trigger();
  virtual int get_object_size();
#ifdef HTTP_CACHE
  virtual void set_http_info(CacheHTTPInfo *info);
  virtual void get_http_info(CacheHTTPInfo ** info);
#endif
  virtual bool set_pin_in_cache(time_t time_pin);
  virtual time_t get_pin_in_cache();
  virtual bool set_disk_io_priority(int priority);
  virtual int get_disk_io_priority();

  // offsets from the base stat
#define CACHE_STAT_ACTIVE  0
#define CACHE_STAT_SUCCESS 1
#define CACHE_STAT_FAILURE 2

  // number of bytes to memset to 0 in the CacheVC when we free
  // it. All member variables starting from vio are memset to 0.
  // This variable is initialized in CacheVC constructor.
  static int size_to_init;

  // Start Region A
  // This set of variables are not reset when the cacheVC is freed.
  // A CacheVC must set these to the correct values whenever needed
  // These are variables that are always set to the correct values
  // before being used by the CacheVC
  CacheKey key, first_key, earliest_key, update_key;
  Dir dir, earliest_dir, overwrite_dir, first_dir;
  // end Region A

  // Start Region B
  // These variables are individually cleared or reset when the
  // CacheVC is freed. All these variables must be reset/cleared
  // in free_CacheVC.
  Action _action;
#ifdef HTTP_CACHE
  CacheHTTPHdr request;
#endif
  CacheHTTPInfoVector vector;
  CacheHTTPInfo alternate;
  Ptr<IOBufferData> buf;
  Ptr<IOBufferData> first_buf;
  Ptr<IOBufferBlock> blocks; // data available to write
  Ptr<IOBufferBlock> writer_buf;

  OpenDirEntry *od;
  AIOCallbackInternal io;
  int alternate_index;          // preferred position in vector
  Link<CacheVC> opendir_link;
#ifdef CACHE_STAT_PAGES
  Link<CacheVC> stat_link;
#endif
  // end Region B

  // Start Region C
  // These variables are memset to 0 when the structure is freed.
  // The size of this region is size_to_init which is initialized
  // in the CacheVC constuctor. It assumes that vio is the start
  // of this region. 
  // NOTE: NOTE: NOTE: If vio is NOT the start, then CHANGE the 
  // size_to_init initialization
  VIO vio;
  EThread *initial_thread;  // initial thread open_XX was called on
  CacheFragType frag_type;
  CacheHTTPInfo *info;
  CacheHTTPInfoVector *write_vector;
#ifdef HTTP_CACHE
  CacheLookupHttpConfig *params;
#endif
  int header_len;       // for communicating with agg_copy
  int frag_len;         // for communicating with agg_copy
  inku32 write_len;     // for communicating with agg_copy
  inku32 agg_len;       // for communicating with aggWrite
  Frag *frag;           // arraylist of fragment offset
  Frag integral_frags[INTEGRAL_FRAGS];
  Part *part;
  Dir *last_collision;
  Event *trigger;
  CacheKey *read_key;
  ContinuationHandler save_handler;
  inku32 pin_in_cache;
  ink_hrtime start_time;
  int base_stat;
  int recursive;
  int closed;
  ink64 seek_to;                // pread offset
  ink64 offset;                 // offset into 'blocks' of data to write
  ink64 writer_offset;          // offset of the writer for reading from a writer
  ink64 length;                 // length of data available to write
  ink64 doc_pos;                // read position in 'buf'
  inku64 write_pos;             // length written
  inku64 total_len;             // total length written and available to write
  inku64 doc_len;               // total_length (of the selected alternate for HTTP)
  inku64 update_len;
  int fragment;
  int scan_msec_delay;
  CacheVC *write_vc;
  char *hostname;
  int host_len;
  int header_to_write_len;  
  void *header_to_write;
  short writer_lock_retry;

  union
  {
    inku32 flags;
    struct
    {
      unsigned int use_first_key:1;
      unsigned int overwrite:1; // overwrite first_key Dir if it exists
      unsigned int evacuator:1;
      unsigned int single_fragment:1;
      unsigned int evac_vector:1;
      unsigned int lookup:1;
#ifdef HIT_EVACUATE
      unsigned int hit_evacuate:1;
#endif
      unsigned int update:1;
      unsigned int remove:1;
      unsigned int remove_aborted_writers:1;
      unsigned int open_read_timeout:1; // UNUSED
      unsigned int data_done:1;
      unsigned int read_from_writer_called:1;
      unsigned int not_from_ram_cache:1;        // entire doc was from ram cache
      unsigned int rewrite_resident_alt:1;
      unsigned int readers:1;
    } f;
  };
  //end region C
};

#define PUSH_HANDLER(_x) do {                                           \
    ink_assert(handler != (ContinuationHandler)(&CacheVC::dead));       \
    save_handler = handler; handler = (ContinuationHandler)(_x);        \
} while (0)

#define POP_HANDLER do {                                          \
    handler = save_handler;                                       \
    ink_assert(handler != (ContinuationHandler)(&CacheVC::dead)); \
  } while (0) 

struct CacheRemoveCont:Continuation
{
  int event_handler(int event, void *data);
  
  CacheRemoveCont():Continuation(NULL) { }
};


// Global Data

extern ClassAllocator<CacheVC> cacheVConnectionAllocator;
extern CacheKey zero_key;
extern CacheSync *cacheDirSync;
// Function Prototypes
#ifdef HTTP_CACHE
int cache_write(CacheVC *, CacheHTTPInfoVector *);
int get_alternate_index(CacheHTTPInfoVector *cache_vector, CacheKey key);
#endif
int evacuate_segments(CacheKey *key, int force, Part *part);
CacheVC *new_DocEvacuator(int nbytes, Part *d);

// inline Functions

inline CacheVC *
new_CacheVC(Continuation *cont)
{
  EThread *t = cont->mutex->thread_holding;
  CacheVC *c = THREAD_ALLOC(cacheVConnectionAllocator, t);
#ifdef HTTP_CACHE
  c->vector.data.data = &c->vector.data.fast_data[0];
#endif
  c->_action = cont;
  c->initial_thread = t;
  c->mutex = cont->mutex;
  c->start_time = ink_get_hrtime();
  ink_assert(c->trigger == NULL);
  Debug("cache_new", "new %lX", (long) c);
#ifdef CACHE_STAT_PAGES
  ink_assert(!c->stat_link.next);
  ink_assert(!c->stat_link.prev);
#endif
  dir_clear(&c->dir);
  return c;
}

inline int
free_CacheVC(CacheVC *cont)
{
  Debug("cache_free", "free %lX", (long) cont);
  ProxyMutex *mutex = cont->mutex;
  Part *part = cont->part;
  CACHE_DECREMENT_DYN_STAT(cont->base_stat + CACHE_STAT_ACTIVE);
  if (cont->closed > 0) {
    CACHE_INCREMENT_DYN_STAT(cont->base_stat + CACHE_STAT_SUCCESS);
  }                             // else abort,cancel
  ink_debug_assert(mutex->thread_holding == this_ethread());
  if (cont->trigger)
    cont->trigger->cancel();
  ink_assert(!cont->is_io_in_progress());
  ink_assert(!cont->od);
  /* calling cont->io.action = NULL causes compile problem on 2.6 solaris
     release build....wierd??? For now, null out continuation and mutex
     of the action separately */
  cont->io.action.continuation = NULL;
  cont->io.action.mutex = NULL;
  cont->io.mutex.clear();
  cont->io.aio_result = 0;
  cont->io.aiocb.aio_nbytes = 0;
  cont->io.aiocb.aio_reqprio = AIO_DEFAULT_PRIORITY;
#ifdef HTTP_CACHE
  cont->request.reset();
  cont->vector.clear();
#endif
  cont->vio.buffer.clear();
  cont->vio.mutex.clear();
#ifdef HTTP_CACHE
  if (cont->vio.op == VIO::WRITE && cont->alternate_index == CACHE_ALT_INDEX_DEFAULT)
    cont->alternate.destroy();
  else
    cont->alternate.clear();
#endif
  cont->_action.cancelled = 0;
  cont->_action.mutex.clear();
  cont->mutex.clear();
  cont->buf.clear();
  cont->first_buf.clear();
  cont->blocks.clear();
  cont->writer_buf.clear();
  cont->alternate_index = CACHE_ALT_INDEX_DEFAULT;
  if (cont->frag && cont->frag != cont->integral_frags)
    xfree(cont->frag);
  memset((char *) &cont->vio, 0, cont->size_to_init);
#ifdef CACHE_STAT_PAGES
  ink_assert(!cont->stat_link.next && !cont->stat_link.prev);
#endif
#ifdef DEBUG
  SET_CONTINUATION_HANDLER(cont, &CacheVC::dead);
#endif
  THREAD_FREE_TO(cont, cacheVConnectionAllocator, this_ethread(), MAX_CACHE_VCS_PER_THREAD);
  return EVENT_DONE;
}

inline int
CacheVC::calluser(int event)
{
  recursive++;
  ink_debug_assert(!part || this_ethread() != part->mutex->thread_holding);
  vio._cont->handleEvent(event, (void *) &vio);
  recursive--;
  if (closed) {
    die();
    return EVENT_DONE;
  }
  return EVENT_CONT;
}

inline int
CacheVC::callcont(int event)
{
  recursive++;
  ink_debug_assert(!part || this_ethread() != part->mutex->thread_holding);
  _action.continuation->handleEvent(event, this);
  recursive--;
  if (closed)
    die();
  else if (vio.vc_server)
    handleEvent(EVENT_IMMEDIATE, 0);
  return EVENT_DONE;
}

inline int
CacheVC::do_read_call(CacheKey *akey)
{
  doc_pos = 0;
  read_key = akey;
  io.aiocb.aio_nbytes = dir_approx_size(&dir);
  PUSH_HANDLER(&CacheVC::handleRead);
  return handleRead(EVENT_CALL, 0);
}

inline int
CacheVC::do_write_call()
{
  PUSH_HANDLER(&CacheVC::handleWrite);
  return handleWrite(EVENT_CALL, 0);
}

inline void
CacheVC::cancel_trigger()
{
  if (trigger) {
    trigger->cancel_action();
    trigger = NULL;
  }
}

inline int
CacheVC::die()
{
  if (vio.op == VIO::WRITE) {
#ifdef HTTP_CACHE
    if (f.update && total_len) {
      alternate.object_key_set(earliest_key);
    }
#endif
    if (!is_io_in_progress()) {
      SET_HANDLER(&CacheVC::openWriteClose);
      if (!recursive)
        openWriteClose(EVENT_NONE, NULL);
    }                           // else catch it at the end of openWriteWriteDone
    return EVENT_CONT;
  } else {
    if (is_io_in_progress())
      save_handler = (ContinuationHandler) & CacheVC::openReadClose;
    else {
      SET_HANDLER(&CacheVC::openReadClose);
      if (!recursive)
        openReadClose(EVENT_NONE, NULL);
    }
    return EVENT_CONT;
  }
}

inline int
CacheVC::handleWriteLock(int event, Event *e)
{
  cancel_trigger();
  int ret = 0;
  {
    CACHE_TRY_LOCK(lock, part->mutex, mutex->thread_holding);
    if (!lock) {
      set_agg_write_in_progress();
      trigger = mutex->thread_holding->schedule_in_local(this, MUTEX_RETRY_DELAY);
      return EVENT_CONT;
    }
    ret = handleWrite(event, e);
  }
  if (ret == EVENT_RETURN)
    return handleEvent(AIO_EVENT_DONE, 0);
  return EVENT_CONT;
}

inline int
CacheVC::do_write_lock()
{
  PUSH_HANDLER(&CacheVC::handleWriteLock);
  return handleWriteLock(EVENT_NONE, 0);
}

inline int
CacheVC::do_write_lock_call()
{
  PUSH_HANDLER(&CacheVC::handleWriteLock);
  return handleWriteLock(EVENT_CALL, 0);
}

inline bool
CacheVC::writer_done()
{
  OpenDirEntry *cod = od;
  if (!cod)
    cod = part->open_read(&first_key);
  CacheVC *w = (cod) ? cod->writers.head : NULL;
  // If the write vc started after the reader, then its not the
  // original writer, since we never choose a writer that started
  // after the reader. The original writer was deallocated and then
  // reallocated for the same first_key
  for (; w && (w != write_vc || w->start_time > start_time); w = (CacheVC *) w->opendir_link.next);
  if (!w)
    return true;
  return false;
}

inline int
Part::close_write(CacheVC *cont)
{

#ifdef CACHE_STAT_PAGES
  ink_assert(stat_cache_vcs.head);
  stat_cache_vcs.remove(cont, cont->stat_link);
  ink_assert(!cont->stat_link.next && !cont->stat_link.prev);
#endif
  return open_dir.close_write(cont);
}

// Returns 0 on success or a positive error code on failure
inline int
Part::open_write(CacheVC *cont, int allow_if_writers, int max_writers)
{
  Part *part = this;
  bool agg_error = false;
  if (!cont->f.remove) {
    agg_error = (!cont->f.update && agg_todo_size > cache_config_agg_write_backlog);
#ifdef CACHE_AGG_FAIL_RATE
    agg_error = agg_error || ((inku32) mutex->thread_holding->generator.random() <
                              (inku32) (UINT_MAX * CACHE_AGG_FAIL_RATE));
#endif
  }
  if (agg_error) {
    CACHE_INCREMENT_DYN_STAT(cache_write_backlog_failure_stat);
    return ECACHE_WRITE_FAIL;
  }
  if (open_dir.open_write(cont, allow_if_writers, max_writers)) {
#ifdef CACHE_STAT_PAGES
    ink_debug_assert(cont->mutex->thread_holding == this_ethread());
    ink_assert(!cont->stat_link.next && !cont->stat_link.prev);
    stat_cache_vcs.enqueue(cont, cont->stat_link);
#endif
    return 0;
  }
  return ECACHE_DOC_BUSY;
}

inline int
Part::close_write_lock(CacheVC *cont)
{
  EThread *t = cont->mutex->thread_holding;
  CACHE_TRY_LOCK(lock, mutex, t);
  if (!lock)
    return -1;
  return close_write(cont);
}

inline int
Part::open_write_lock(CacheVC *cont, int allow_if_writers, int max_writers)
{
  EThread *t = cont->mutex->thread_holding;
  CACHE_TRY_LOCK(lock, mutex, t);
  if (!lock)
    return -1;
  return open_write(cont, allow_if_writers, max_writers);
}

inline OpenDirEntry *
Part::open_read_lock(INK_MD5 *key, EThread *t)
{
  CACHE_TRY_LOCK(lock, mutex, t);
  if (!lock)
    return NULL;
  return open_dir.open_read(key);
}

inline int
Part::begin_read_lock(CacheVC *cont)
{
  // no need for evacuation as the entire document is already in memory
#ifndef  CACHE_STAT_PAGES
  if (cont->f.single_fragment)
    return 0;
#endif
  // VC is enqueued in stat_cache_vcs in the begin_read call
  EThread *t = cont->mutex->thread_holding;
  CACHE_TRY_LOCK(lock, mutex, t);
  if (!lock)
    return -1;
  return begin_read(cont);
}

inline int
Part::close_read_lock(CacheVC *cont)
{
  EThread *t = cont->mutex->thread_holding;
  CACHE_TRY_LOCK(lock, mutex, t);
  if (!lock)
    return -1;
  return close_read(cont);
}

inline int
dir_delete_lock(CacheKey *key, Part *d, ProxyMutex *m, Dir *del)
{
  EThread *thread = m->thread_holding;
  CACHE_TRY_LOCK(lock, d->mutex, thread);
  if (!lock)
    return -1;
  return dir_delete(key, d, del);
}

inline int
dir_insert_lock(CacheKey *key, Part *d, Dir *to_part, ProxyMutex *m)
{
  EThread *thread = m->thread_holding;
  CACHE_TRY_LOCK(lock, d->mutex, thread);
  if (!lock)
    return -1;
  return dir_insert(key, d, to_part);
}

inline int
dir_overwrite_lock(CacheKey *key, Part *d, Dir *to_part, ProxyMutex *m, Dir *overwrite, bool must_overwrite = true)
{
  EThread *thread = m->thread_holding;
  CACHE_TRY_LOCK(lock, d->mutex, thread);
  if (!lock)
    return -1;
  return dir_overwrite(key, d, to_part, overwrite, must_overwrite);
}

void inline
rand_CacheKey(CacheKey *next_key, ProxyMutex *mutex)
{
  inku32 *b = (inku32 *) & next_key->b[0];
  InkRand & g = mutex->thread_holding->generator;
  for (int i = 0; i < 4; i++)
    b[i] = (inku32) g.random();
}

extern inku8 CacheKey_next_table[];
void inline
next_CacheKey(CacheKey *next_key, CacheKey *key)
{
  inku8 *b = (inku8 *) next_key;
  inku8 *k = (inku8 *) key;
  b[0] = CacheKey_next_table[k[0]];
  for (int i = 1; i < 16; i++)
    b[i] = CacheKey_next_table[(b[i - 1] + k[i]) & 0xFF];
}
extern inku8 CacheKey_prev_table[];
void inline
prev_CacheKey(CacheKey *prev_key, CacheKey *key)
{
  inku8 *b = (inku8 *) prev_key;
  inku8 *k = (inku8 *) key;
  for (int i = 15; i > 0; i--)
    b[i] = 256 + CacheKey_prev_table[k[i]] - k[i - 1];
  b[0] = CacheKey_prev_table[k[0]];
}

inline unsigned int
next_rand(unsigned int *p)
{
  unsigned int seed = *p;
  seed = 1103515145 * seed + 12345;
  *p = seed;
  return seed;
}

extern ClassAllocator<CacheRemoveCont> cacheRemoveContAllocator;

inline CacheRemoveCont *
new_CacheRemoveCont()
{
  CacheRemoveCont *cache_rm = cacheRemoveContAllocator.alloc();

  cache_rm->mutex = new_ProxyMutex();
  SET_CONTINUATION_HANDLER(cache_rm, &CacheRemoveCont::event_handler);
  return cache_rm;
}

inline void
free_CacheRemoveCont(CacheRemoveCont *cache_rm)
{
  cache_rm->mutex = NULL;
  cacheRemoveContAllocator.free(cache_rm);
}

inline int
CacheRemoveCont::event_handler(int event, void *data)
{
  (void) event;
  (void) data;
  free_CacheRemoveCont(this);
  return EVENT_DONE;
}

ink64 cache_bytes_used(void);
ink64 cache_bytes_total(void);

#ifdef DEBUG
#define CACHE_DEBUG_INCREMENT_DYN_STAT(_x) CACHE_INCREMENT_DYN_STAT(_x)
#define CACHE_DEBUG_SUM_DYN_STAT(_x,_y) CACHE_SUM_DYN_STAT(_x,_y)
#else
#define CACHE_DEBUG_INCREMENT_DYN_STAT(_x)
#define CACHE_DEBUG_SUM_DYN_STAT(_x,_y)
#endif

struct CacheHostRecord;
class Part;
class CacheHostTable;

struct Cache
{
  volatile int cache_read_done;
  volatile int total_good_npart;
  int total_npart;
  volatile int ready;
  ink64 cache_size;             //in store block size
  CacheHostTable *hosttable;
  volatile int total_initialized_part;
  int scheme;

  int open(bool reconfigure, bool fix);
  int close();

  Action *lookup(Continuation *cont, CacheKey *key, CacheFragType type, char *hostname, int host_len);
  inkcoreapi Action *open_read(Continuation *cont, CacheKey *key, CacheFragType type, char *hostname, int len);
  inkcoreapi Action *open_write(Continuation *cont, CacheKey *key,
                                CacheFragType frag_type, bool overwrite = false,
                                time_t pin_in_cache = (time_t) 0, char *hostname = 0, int host_len = 0);
  inkcoreapi Action *remove(Continuation *cont, CacheKey *key,
                            CacheFragType type = CACHE_FRAG_TYPE_HTTP, 
                            bool user_agents = true, bool link = false,
                            char *hostname = 0, int host_len = 0);
  Action *scan(Continuation *cont, char *hostname = 0, int host_len = 0, int KB_per_second = 2500);

#ifdef HTTP_CACHE
  Action *lookup(Continuation *cont, URL *url, CacheFragType type);
  inkcoreapi Action *open_read(Continuation *cont, CacheKey *key,
                               CacheHTTPHdr *request,
                               CacheLookupHttpConfig *params, CacheFragType type, char *hostname, int host_len);
  Action *open_read(Continuation *cont, URL *url, CacheHTTPHdr *request,
                    CacheLookupHttpConfig *params, CacheFragType type);
  Action *open_write(Continuation *cont, CacheKey *key,
                     CacheHTTPInfo *old_info, time_t pin_in_cache = (time_t) 0,
                     CacheKey *key1 = NULL,
                     CacheFragType type = CACHE_FRAG_TYPE_HTTP, char *hostname = 0, int host_len = 0);
  Action *open_write(Continuation *cont, URL *url, CacheHTTPHdr *request,
                     CacheHTTPInfo *old_info, time_t pin_in_cache = (time_t) 0,
                     CacheFragType type = CACHE_FRAG_TYPE_HTTP);
  Action *remove(Continuation *cont, URL *url, CacheFragType type);
  static void generate_key(INK_MD5 *md5, URL *url, CacheHTTPHdr *request);
#endif

  Action *link(Continuation *cont, CacheKey *from, CacheKey *to, CacheFragType type, char *hostname, int host_len);
  Action *deref(Continuation *cont, CacheKey *key, CacheFragType type, char *hostname, int host_len);

  void part_initialized(bool result);

  int open_done();
  static void print_stats(FILE *fp, int verbose = 0);

  Part *key_to_part(CacheKey *key, char *hostname, int host_len);

  Cache():cache_read_done(0), total_good_npart(0), total_npart(0), ready(CACHE_INITIALIZING), cache_size(0),  // in store block size
          hosttable(NULL), total_initialized_part(0), scheme(CACHE_NONE_TYPE)
    {
    }
};

extern Cache *theCache;
extern Cache *theStreamCache;
inkcoreapi extern Cache *caches[1 << NumCacheFragTypes];
extern int cache_config_vary_on_user_agent;


#ifdef HTTP_CACHE
inline Action *
Cache::open_read(Continuation *cont, CacheURL *url, CacheHTTPHdr *request,
                 CacheLookupHttpConfig *params, CacheFragType type)
{
  INK_MD5 md5;
  int len;
  url->MD5_get(&md5);
  const char *hostname = url->host_get(&len);
  return open_read(cont, &md5, request, params, type, (char *) hostname, len);
}

inline void
Cache::generate_key(INK_MD5 *md5, URL *url, CacheHTTPHdr *request)
{
#ifdef BROKEN_HACK_FOR_VARY_ON_UA
  // We probably should make this configurable, both enabling it and what
  // MIME types we want to treat differently. // Leif

  if (cache_config_vary_on_user_agent && request) {
    // If we are varying on user-agent, we only want to do
    //  this for text content-types since expirence says
    //  images do not vary.   Varying on images decimiates
    //  the hitrate (INKqa04820)

    // HDR FIX ME - mimeTable needs to be updated to support
    //   ptr/len pairs

    // Note: if 'proxy.config.http.global_user_agent_header' is set, we
    // should ignore 'cache_config_vary_on_user_agent' flag because
    // all requests to OS were sent with one so-called 'global user agent'
    // header instead of original client's 'user-agent'

    MimeTableEntry *url_mime_type_entry =
//      mimeTable.get_entry_path(url->path_get());
      NULL;

    if (url_mime_type_entry && strstr(url_mime_type_entry->mime_type, "text")) {
      url->MD5_get(md5);
      int ua_len;
      const char *value = request->value_get(MIME_FIELD_USER_AGENT,
                                             MIME_LEN_USER_AGENT, &ua_len);
      if (value) {
        INK_DIGEST_CTX context;
        // Mix the user-agent and URL INK_MD5's
        ink_code_incr_md5_init(&context);
        ink_code_incr_md5_update(&context, value, ua_len);
        ink_code_incr_md5_update(&context, (char *) md5, sizeof(INK_MD5));
        ink_code_incr_md5_final((char *) md5, &context);
      }
      return;
    }
  }
#endif /* BROKEN_HACK_FOR_VARY_ON_UA */
  url->MD5_get(md5);
}

inline Action *
Cache::open_write(Continuation *cont, CacheURL *url, CacheHTTPHdr *request,
                  CacheHTTPInfo *old_info, time_t pin_in_cache, CacheFragType type)
{
  (void) request;
  INK_MD5 url_md5;
  url->MD5_get(&url_md5);
  int len;
  const char *hostname = url->host_get(&len);

  return open_write(cont, &url_md5, old_info, pin_in_cache, NULL, type, (char *) hostname, len);
}
#endif

inline unsigned int
cache_hash(INK_MD5 & md5)
{
  inku64 f = md5.fold();
  unsigned int mhash = (unsigned int) (f >> 32);
  return mhash;
}

#ifdef HTTP_CACHE
#define CLUSTER_CACHE
#endif
#ifdef CLUSTER_CACHE
#include "P_Net.h"
#include "P_ClusterInternal.h"
// Note: This include must occur here in order to avoid numerous forward
//       reference problems.
#include "P_ClusterInline.h"
#endif

inline Action *
CacheProcessor::lookup(Continuation *cont, CacheKey *key, bool local_only,
                       CacheFragType frag_type, char *hostname, int host_len)
{
  (void) local_only;
#ifdef CLUSTER_CACHE
  // Try to send remote, if not possible, handle locally
  if ((cache_clustering_enabled > 0) && !local_only) {
    Action *a = Cluster_lookup(cont, key, frag_type, hostname, host_len);
    if (a) {
      return a;
    }
  }
#endif
  return caches[frag_type]->lookup(cont, key, frag_type, hostname, host_len);
}

inline inkcoreapi Action *
CacheProcessor::open_read(Continuation *cont, CacheKey *key, CacheFragType frag_type, char *hostname, int host_len)
{
#ifdef CLUSTER_CACHE
  if (cache_clustering_enabled > 0) {
    return open_read_internal(CACHE_OPEN_READ, cont, (MIOBuffer *) 0,
                              (CacheURL *) 0, (CacheHTTPHdr *) 0,
                              (CacheLookupHttpConfig *) 0, key, 0, frag_type, hostname, host_len);
  }
#endif
  return caches[frag_type]->open_read(cont, key, frag_type, hostname, host_len);
}

inline Action *
CacheProcessor::open_read_buffer(Continuation *cont, MIOBuffer *buf, CacheKey *key, CacheFragType frag_type,
                                 char *hostname, int host_len)
{
  (void) buf;
#ifdef CLUSTER_CACHE
  if (cache_clustering_enabled > 0) {
    return open_read_internal(CACHE_OPEN_READ_BUFFER, cont, buf,
                              (CacheURL *) 0, (CacheHTTPHdr *) 0,
                              (CacheLookupHttpConfig *) 0, key, 0, frag_type, hostname, host_len);
  }
#endif
  return caches[frag_type]->open_read(cont, key, frag_type, hostname, host_len);
}


inline inkcoreapi Action *
CacheProcessor::open_write(Continuation *cont, CacheKey *key, CacheFragType frag_type, 
                           int expected_size, bool overwrite, time_t pin_in_cache, 
                           char *hostname, int host_len)
{
  (void) expected_size;
#ifdef CLUSTER_CACHE
  ClusterMachine *m = cluster_machine_at_depth(cache_hash(*key));

  if (m && (cache_clustering_enabled > 0)) {
    return Cluster_write(cont, expected_size, (MIOBuffer *) 0, m,
                         key, frag_type, overwrite, pin_in_cache,
                         CACHE_OPEN_WRITE, key, (CacheURL *) 0,
                         (CacheHTTPHdr *) 0, (CacheHTTPInfo *) 0, hostname, host_len);
  }
#endif
  return caches[frag_type]->open_write(cont, key, frag_type, overwrite, pin_in_cache, hostname, host_len);

}

inline Action *
CacheProcessor::open_write_buffer(Continuation *cont, MIOBuffer *buf,
                                  CacheKey *key, 
                                  CacheFragType frag_type, 
                                  bool overwrite, time_t pin_in_cache,
                                  char *hostname, int host_len) 
{
  (void)cont;
  (void)buf;
  (void)key;
  (void)frag_type;
  (void)overwrite;
  (void)hostname;
  (void)host_len;
  ink_assert(!"implemented");
  return NULL;
}

inline Action *
CacheProcessor::remove(Continuation *cont, CacheKey *key, CacheFragType frag_type,
                       bool rm_user_agents, bool rm_link, char *hostname, int host_len)
{
#ifdef CLUSTER_CACHE
  if (cache_clustering_enabled > 0) {
    ClusterMachine *m = cluster_machine_at_depth(cache_hash(*key));

    if (m) {
      return Cluster_remove(m, cont, key, rm_user_agents, rm_link, frag_type, hostname, host_len);
    }
  }
#endif
  return caches[frag_type]->remove(cont, key, frag_type, rm_user_agents, rm_link, hostname, host_len);
}

inline Action *
scan(Continuation *cont, char *hostname = 0, int host_len = 0, int KB_per_second = 2500)
{
  return caches[CACHE_FRAG_TYPE_HTTP]->scan(cont, hostname, host_len, KB_per_second);
}

#ifdef HTTP_CACHE
inline Action *
CacheProcessor::lookup(Continuation *cont, URL *url, bool local_only, CacheFragType frag_type)
{
  (void) local_only;
  INK_MD5 md5;
  url->MD5_get(&md5);
  int host_len = 0;
  const char *hostname = url->host_get(&host_len);

  return lookup(cont, &md5, local_only, frag_type, (char *) hostname, host_len);
}

inline Action *
CacheProcessor::open_read_buffer(Continuation *cont, MIOBuffer *buf,
                                 URL *url, CacheHTTPHdr *request, CacheLookupHttpConfig *params, CacheFragType type)
{
  (void) buf;
#ifdef CLUSTER_CACHE
  if (cache_clustering_enabled > 0) {
    return open_read_internal(CACHE_OPEN_READ_BUFFER_LONG, cont, buf, url,
                              request, params, (CacheKey *) 0, 0, type, (char *) 0, 0);
  }
#endif
  return caches[type]->open_read(cont, url, request, params, type);
}

inline Action *
CacheProcessor::open_write_buffer(Continuation * cont, MIOBuffer * buf, URL * url,
                                  CacheHTTPHdr * request, CacheHTTPHdr * response, CacheFragType type)
{
  (void) cont;
  (void) buf;
  (void) url;
  (void) request;
  (void) response;
  (void) type;
  ink_assert(!"implemented");
  return NULL;
}

#endif


#ifdef CLUSTER_CACHE
inline Action *
CacheProcessor::open_read_internal(int opcode,
                                   Continuation *cont, MIOBuffer *buf,
                                   CacheURL *url,
                                   CacheHTTPHdr *request,
                                   CacheLookupHttpConfig *params,
                                   CacheKey *key,
                                   time_t pin_in_cache, CacheFragType frag_type, char *hostname, int host_len)
{
  INK_MD5 url_md5;
  if ((opcode == CACHE_OPEN_READ_LONG) || (opcode == CACHE_OPEN_READ_BUFFER_LONG)) {
    Cache::generate_key(&url_md5, url, request);
  } else {
    url_md5 = *key;
  }
  ClusterMachine *m = cluster_machine_at_depth(cache_hash(url_md5));
  ClusterMachine *owner_machine = m ? m : this_cluster_machine();

  if (owner_machine != this_cluster_machine()) {
    return Cluster_read(owner_machine, opcode, cont, buf, url,
                        request, params, key, pin_in_cache, frag_type, hostname, host_len);
  } else {
    if ((opcode == CACHE_OPEN_READ_LONG)
        || (opcode == CACHE_OPEN_READ_BUFFER_LONG)) {
      return caches[frag_type]->open_read(cont, &url_md5, request, params, frag_type, hostname, host_len);
    } else {
      return caches[frag_type]->open_read(cont, key, frag_type, hostname, host_len);
    }
  }
}
#endif

#ifdef CLUSTER_CACHE
inline Action *
CacheProcessor::link(Continuation *cont, CacheKey *from, CacheKey *to,
                     CacheFragType type, char *hostname, int host_len)
{
  if (cache_clustering_enabled > 0) {
    // Use INK_MD5 in "from" to determine target machine
    ClusterMachine *m = cluster_machine_at_depth(cache_hash(*from));
    if (m) {
      return Cluster_link(m, cont, from, to, type, hostname, host_len);
    }
  }
  return caches[type]->link(cont, from, to, type, hostname, host_len);
}

inline Action *
CacheProcessor::deref(Continuation *cont, CacheKey *key, CacheFragType type, char *hostname, int host_len)
{
  if (cache_clustering_enabled > 0) {
    ClusterMachine *m = cluster_machine_at_depth(cache_hash(*key));
    if (m) {
      return Cluster_deref(m, cont, key, type, hostname, host_len);
    }
  }
  return caches[type]->deref(cont, key, type, hostname, host_len);
}
#endif

inline Action *
CacheProcessor::scan(Continuation *cont, char *hostname, int host_len, int KB_per_second)
{
  return caches[CACHE_FRAG_TYPE_HTTP]->scan(cont, hostname, host_len, KB_per_second);
}

inline int
CacheProcessor::IsCacheEnabled()
{
  return CacheProcessor::initialized;
}

inline unsigned int
CacheProcessor::IsCacheReady(CacheFragType type)
{
  if (IsCacheEnabled() != CACHE_INITIALIZED)
    return 0;
  return (cache_ready & type);
}

inline Cache *
local_cache()
{
  return theCache;
}

#endif /* _P_CACHE_INTERNAL_H__ */
