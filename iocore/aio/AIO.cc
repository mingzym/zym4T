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

/****************************************************************************

  Async Disk IO operations.

  
  
 ****************************************************************************/

#include "P_AIO.h"
// globals
int ts_config_with_inkdiskio = 0;
#define MAX_DISKS_POSSIBLE 100
#define SLEEP_TIME 100


#define MAX_DISKS_POSSIBLE 100
#define SLEEP_TIME 100

/* structure to hold information about each file descriptor */
AIO_Reqs *aio_reqs[MAX_DISKS_POSSIBLE];

RecRawStatBlock *aio_rsb = NULL;
// acquire this mutex before inserting a new entry in the aio_reqs array.
// Don't need to acquire this for searching the array
static ink_mutex insert_mutex;

RecInt cache_config_threads_per_disk = 12;
static RecInt cache_config_aio_sleep_time = SLEEP_TIME;

// AIO Stats
inku64 aio_num_read = 0;
inku64 aio_bytes_read = 0;
inku64 aio_num_write = 0;
inku64 aio_bytes_written = 0;



/* number of unique file descriptors in the aio_reqs array */
volatile int num_filedes = 0;

Continuation *aio_err_callbck = 0;


//////////////////////////////////////////////////////////////////////////
///////////////                    INKIO                 /////////////////
//////////////////////////////////////////////////////////////////////////

#ifdef INKDISKAIO
#undef AIO_MODE
#define AIO_MODE                         AIO_MODE_THREAD
#define AIO_PENDING_WATERMARK   400
#define AIO_PENDING_WARNING     1000
ink32 write_thread_index = 0;
#define NOS_WRITE_THREADS	   10
int nos_write_threads = NOS_WRITE_THREADS;

#define MY_AIO_THREAD		   (get_dmid(this_ethread()))
#define DISK_WRITE_THREAD	   ((this_ethread()->tt == DEDICATED))


inline int
my_aio_thread(void)
{
  ink_assert(!DISK_WRITE_THREAD);
  int ret = write_thread_index % nos_write_threads;
  ink_atomic_increment(&write_thread_index, 1);
  ink_atomic_swap(&write_thread_index, write_thread_index % nos_write_threads);
  return (this_ethread()->id * nos_write_threads + ret);
}


unsigned int last_pending_report = 0;
int pending_report_interval = 10000000;
int inkdiskio_watermark = AIO_PENDING_WATERMARK;
volatile int aio_pending = 0;
volatile int pending_size = 0;
int aio_queued = 0;
volatile int max_aio_queued = 0;


#include "inkaio.h"


#define DISK_PERIOD                 -HRTIME_MSECONDS(11)
static void aio_insert(AIOCallback * op, AIO_Reqs * req);
int
disk_io_handler(kcall_t * ioep)
{
  AIOCallback *op = (AIOCallback *) ioep->cookie;

  struct ink_aiocb_t *a = &op->aiocb;
  ink_assert(op->action.mutex);
  ink_assert(a->aio_nbytes > 0);
  if (op->aiocb.aio_lio_opcode == LIO_READ || op->aiocb.aio_lio_opcode == LIO_WRITE) {
    ink_atomic_increment(&pending_size, -op->aiocb.aio_nbytes);
    AIO_Reqs *req = aio_reqs[MY_AIO_THREAD];
    ink_atomic_increment(&req->pending, -1);
  }
  if (ioep->type == INKAIO_FLUSH) {
    Warning("AIO ERROR \n");
    op->aio_result = -1;
  } else
    op->aio_result = op->aiocb.aio_nbytes;      // aio_result is not used here!
  op->link.prev = NULL;
  op->link.next = NULL;
  op->mutex = op->action.mutex;
  EThread *thread = this_ethread();
  if (a->aio_lio_opcode == LIO_READ) {
    eventProcessor.schedule_imm(op);
  } else {
    MUTEX_TRY_LOCK(lock, op->action.mutex, thread);
    if (lock)
      op->action.continuation->handleEvent(AIO_EVENT_DONE, op);
    else {
      eventProcessor.schedule_imm(NEW(new AIOMissEvent(op->action.mutex, op)));
    }
  }
  return 0;
}


class DiskWriteMonitor:public Continuation
{
public:
  int my_id;
  int monitor_main(int event, Event * e);
    DiskWriteMonitor(ProxyMutex * m, int id):Continuation(m), my_id(id)
  {
    SET_HANDLER(&DiskWriteMonitor::monitor_main);
  };
};


inline void
drain_thread_queue(int thread_id)
{
  AIO_Reqs *req = aio_reqs[thread_id];
  MUTEX_TRY_LOCK(lock, req->list_mutex, this_ethread());
  if (!lock)
    return;
  bool run_once = false;
  while (req->pending < inkdiskio_watermark && (req->http_aio_todo.tail)) {
    AIOCallback *op1;
    struct ink_aiocb_t *a1;
    op1 = (AIOCallback *) req->http_aio_todo.pop();
    if (!op1)
      break;
    a1 = &op1->aiocb;
    if (op1->aiocb.aio_lio_opcode == LIO_READ) {
      ink_atomic_increment(&pending_size, op1->aiocb.aio_nbytes);
      if (inkaio_aioread(get_kcb(this_ethread()), (void *) op1, a1->aio_fildes,
                         ((char *) a1->aio_buf), a1->aio_nbytes, a1->aio_offset)) {
        // FIXME
        ink_release_assert(false);
      }
    } else {
      ink_atomic_increment(&pending_size, op1->aiocb.aio_nbytes);
      if (inkaio_aiowrite(get_kcb(this_ethread()), (void *) op1, a1->aio_fildes,
                          ((char *) a1->aio_buf), a1->aio_nbytes, a1->aio_offset)) {
        // FIXME
        ink_release_assert(false);
      }
    }
    if (!run_once)
      run_once = true;
    ink_atomic_increment(&req->pending, 1);
    ink_atomic_increment(&req->queued, -1);
  }
  if (run_once)
    inkaio_submit(get_kcb(this_ethread()));
}

#if 0
inline void
print_thread_counters(Event * e, AIO_Reqs * req)
{
  Warning
    ("Thread[%d] aio_pending: %d, aio_queued: %d, pending_size: %d, max_aio_queued: %d\naioread[%d] aioread_done[%d] aiowrite[%d] aiowritedone[%d] events_len[%d] events_len_done[%d]",
     e->ethread->id, req->pending, req->queued, pending_size, max_aio_queued, (get_kcb(e->ethread))->aioread,
     (get_kcb(e->ethread))->aioread_done, (get_kcb(e->ethread))->aiowrite, (get_kcb(e->ethread))->aiowrite_done,
     (get_kcb(e->ethread))->events_len, (get_kcb(e->ethread))->events_len_done);
}
#endif
static void aio_move(AIO_Reqs * req);
int
DiskWriteMonitor::monitor_main(int event, Event * e)
{
  static struct pollfd pfd;
  static int poll_ret;
  static AIO_Reqs *req = aio_reqs[my_id];
  EThread *t = this_ethread();
  // create a queue for kcalls
  if (get_dm(this_ethread()) != this) {
    *((Continuation **) ETHREAD_GET_PTR(t, dm_offset)) = this;
    *((int *) ETHREAD_GET_PTR(t, dmid_offset)) = my_id;
  }
  *((INKAIOCB **) ETHREAD_GET_PTR(t, kcb_offset)) = inkaio_create(0, disk_io_handler);
  if (get_kcb(t) == 0) {
    Error("inkaio_create : could not create");
    exit(-1);
  } else
    printf("Created writed thread %d \n", my_id);

  pfd.fd = inkaio_filno(get_kcb(this_ethread()));
  pfd.events = POLLIN;
  for (;;) {
    poll_ret = poll(NULL, 0, 10);
    {
      MUTEX_TRY_LOCK(lock, req->list_mutex, this_ethread());
      if (lock) {
        if (!INK_ATOMICLIST_EMPTY(req->aio_temp_list)) {
          aio_move(req);
        }
        inkaio_dispatch((get_kcb(e->ethread)));
        drain_thread_queue(my_id);
      }
    }
  }
  return 0;
}

void
initialize_thread_for_diskaio(EThread * thread)
{
  AIO_Reqs *request;
  if (ts_config_with_inkdiskio) {
    for (int i = 0; i < nos_write_threads; i++) {
      request = (AIO_Reqs *) xmalloc(sizeof(AIO_Reqs));
      memset(request, 0, sizeof(AIO_Reqs));

      INK_WRITE_MEMORY_BARRIER;

      // ink_cond_init(&request->aio_cond);
      // ink_mutex_init(&request->aio_mutex, NULL);
      request->list_mutex = new_ProxyMutex();
      ink_atomiclist_init(&request->aio_temp_list, "temp_list", (unsigned) &((AIOCallback *) 0)->link);

      request->index = 0;
      request->filedes = -1;
      aio_reqs[nos_write_threads * thread->id + i] = request;
      DiskWriteMonitor *dm = new DiskWriteMonitor(new_ProxyMutex(),
                                                  nos_write_threads * thread->id + i);
      eventProcessor.spawn_thread(dm);
    }
  }
}
#endif



////////////////////////////////////////////////////////////////////////
////////        Stats            Stuff                            //////
////////////////////////////////////////////////////////////////////////

static int
aio_stats_cb(const char *name, RecDataT data_type, RecData * data, RecRawStatBlock * rsb, int id)
{
  (void) data_type;
  (void) rsb;
  ink64 new_val = 0;
  ink64 diff = 0;
  ink64 count, sum;
  ink_hrtime now = ink_get_hrtime();
  // The RecGetGlobalXXX stat functions are cheaper than the
  // RecGetXXX functions. The Global ones are expensive
  // for increments and decrements. But for AIO stats we
  // only do Sets and Gets, so they are cheaper in our case. 
  RecGetGlobalRawStatSum(aio_rsb, id, &sum);
  RecGetGlobalRawStatCount(aio_rsb, id, &count);


  ink64 time_diff = ink_hrtime_to_msec(now - count);
  if (time_diff == 0) {
    data->rec_float = 0.0;
    return 0;
  }
  switch (id) {
  case AIO_STAT_READ_PER_SEC:
    new_val = aio_num_read;
    break;

  case AIO_STAT_WRITE_PER_SEC:
    new_val = aio_num_write;
    break;

  case AIO_STAT_KB_READ_PER_SEC:
    new_val = aio_bytes_read >> 10;
    break;
  case AIO_STAT_KB_WRITE_PER_SEC:
    new_val = aio_bytes_written >> 10;
    break;

  default:
    ink_assert(0);
  }
  diff = new_val - sum;
  RecSetGlobalRawStatSum(aio_rsb, id, new_val);
  RecSetGlobalRawStatCount(aio_rsb, id, now);
  data->rec_float = (float) diff *1000.00 / (float) time_diff;
  return 0;
}


#ifdef AIO_STATS
/* total number of requests received - for debugging */
static int num_requests = 0;
/* performance results */
static AIOTestData *data;

int
AIOTestData::ink_aio_stats(int event, void *d)
{

  ink_hrtime now = ink_get_hrtime();
  double time_msec = (double) (now - start) / (double) HRTIME_MSECOND;
  for (int i = 0; i < num_filedes; i++) {

    printf("%0.2f\t%i\t%i\t%i\n", time_msec, aio_reqs[i]->filedes, aio_reqs[i]->pending, aio_reqs[i]->queued);
  }
  printf("Num Requests: %i Num Queued: %i num Moved: %i\n\n", data->num_req, data->num_queue, data->num_temp);
  eventProcessor.schedule_in(this, HRTIME_MSECONDS(50), ET_CALL);
  return EVENT_DONE;
}

#endif // AIO_STATS

////////////////////////////////////////////////////////////////////////
////////        Common           Stuff                            //////
////////////////////////////////////////////////////////////////////////
AIOCallback *
new_AIOCallback(void)
{
  return new AIOCallbackInternal;
};

void
ink_aio_set_callback(Continuation * callback)
{
  aio_err_callbck = callback;
}

void
ink_aio_init(ModuleVersion v)
{
  ink_release_assert(!checkModuleVersion(v, AIO_MODULE_VERSION));

  aio_rsb = RecAllocateRawStatBlock((int) AIO_STAT_COUNT);
  RecRegisterRawStat(aio_rsb, RECT_PROCESS, "proxy.process.cache.read_per_sec",
                     RECD_FLOAT, RECP_NULL, (int) AIO_STAT_READ_PER_SEC, aio_stats_cb);
  RecRegisterRawStat(aio_rsb, RECT_PROCESS, "proxy.process.cache.write_per_sec",
                     RECD_FLOAT, RECP_NULL, (int) AIO_STAT_WRITE_PER_SEC, aio_stats_cb);
  RecRegisterRawStat(aio_rsb, RECT_PROCESS,
                     "proxy.process.cache.KB_read_per_sec",
                     RECD_FLOAT, RECP_NULL, (int) AIO_STAT_KB_READ_PER_SEC, aio_stats_cb);
  RecRegisterRawStat(aio_rsb, RECT_PROCESS,
                     "proxy.process.cache.KB_write_per_sec",
                     RECD_FLOAT, RECP_NULL, (int) AIO_STAT_KB_WRITE_PER_SEC, aio_stats_cb);

#ifdef INKDISKAIO
  kcb_offset = eventProcessor.allocate(sizeof(INKAIOCB *));
  dm_offset = eventProcessor.allocate(sizeof(Continuation *));
  ink_assert(dm_offset - kcb_offset >= sizeof(Continuation *));
  dmid_offset = eventProcessor.allocate(sizeof(int));
  ink_assert(dmid_offset - dm_offset >= sizeof(int));
  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.net.enable_ink_disk_io", 0, RECU_RESTART_TS, RECC_NULL, NULL);

  IOCORE_ReadConfigInteger(ts_config_with_inkdiskio, "proxy.config.net.enable_ink_disk_io");

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.net.ink_disk_io_watermark", 400, RECU_NULL, RECC_NULL, NULL);
  IOCORE_ReadConfigInteger(inkdiskio_watermark, "proxy.config.net.ink_disk_io_watermark");

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.net.ink_aio_write_threads", 10, RECU_NULL, RECC_NULL, NULL);
  IOCORE_ReadConfigInteger(nos_write_threads, "proxy.config.net.ink_aio_write_threads");

  if (ts_config_with_inkdiskio) {
    if (FILE * fp = fopen("/dev/inkaio", "rw")) {
      fclose(fp);
    } else {
      Warning("Inkio for disk cannot be enabled: /dev/kcalls does not exist");
      ts_config_with_inkdiskio = 0;
    }
  }
#endif
  if (!ts_config_with_inkdiskio) {
    memset(&aio_reqs, 0, MAX_DISKS_POSSIBLE * sizeof(AIO_Reqs *));
    ink_mutex_init(&insert_mutex, NULL);
    IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.cache.threads_per_disk", 4, RECU_NULL, RECC_NULL, NULL);
    IOCORE_ReadConfigInteger(cache_config_threads_per_disk, "proxy.config.cache.threads_per_disk");

    IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.cache.aio_sleep_time", 100, RECU_DYNAMIC, RECC_NULL, NULL);
    IOCORE_ReadConfigInteger(cache_config_aio_sleep_time, "proxy.config.cache.aio_sleep_time");
  }


}


int
ink_aio_start()
{
#ifdef AIO_STATS
  data = new AIOTestData();
  eventProcessor.schedule_in(data, HRTIME_MSECONDS(100), ET_CALL);
#endif
  return 0;
}


////////////////////////////////////////////////////////////////////////
////////        Unix             Stuff                            //////
////////////////////////////////////////////////////////////////////////

static void *aio_thread_main(void *arg);

struct AIOThreadInfo:public Continuation
{

  AIO_Reqs *req;
  int sleep_wait;

  int start(int event, Event * e)
  {
    (void) event;
    (void) e;
    aio_thread_main(this);
    return EVENT_DONE;
  }

  AIOThreadInfo(AIO_Reqs * thr_req, int sleep):Continuation(new_ProxyMutex()), req(thr_req), sleep_wait(sleep)
  {
    SET_HANDLER(&AIOThreadInfo::start);
  }

};

/* priority scheduling */
/* Have 2 queues per file descriptor - A queue for http requests and another
   for non-http (streaming) request. Each file descriptor has a lock 
   and condition variable associated with it. A dedicated number of threads 
   (THREADS_PER_DISK) wait on the condition variable associated with the 
   file descriptor. The cache threads try to put the request in the 
   appropriate queue. If they fail to acquire the lock, they put the 
   request in the atomic list. Requests are served in the order of 
   highest priority first. If both the queues are empty, the aio threads 
   check if there is any request on the other disks */


/* insert  an entry for file descriptor fildes into aio_reqs */
static AIO_Reqs *
aio_init_fildes(int fildes)
{
  int i;
  AIO_Reqs *request = (AIO_Reqs *) malloc(sizeof(AIO_Reqs));
  memset(request, 0, sizeof(AIO_Reqs));

  INK_WRITE_MEMORY_BARRIER;

  ink_cond_init(&request->aio_cond);
  ink_mutex_init(&request->aio_mutex, NULL);
  ink_atomiclist_init(&request->aio_temp_list, "temp_list", (uintptr_t) &((AIOCallback *) 0)->link);

  request->index = num_filedes;
  request->filedes = fildes;

  aio_reqs[num_filedes] = request;

  if (!ts_config_with_inkdiskio) {
    /* create the main thread */
    AIOThreadInfo *thr_info;
    for (i = 0; i < cache_config_threads_per_disk; i++) {
      if (i == (cache_config_threads_per_disk - 1))
        thr_info = new AIOThreadInfo(request, 1);
      else
        thr_info = new AIOThreadInfo(request, 0);
      ink_assert(eventProcessor.spawn_thread(thr_info));
    }
  }

  /* the num_filedes should be incremented after initializing everything.
     This prevents a thread from looking at uninitialized fields */
  num_filedes++;
  return request;
}

/* insert a request into either aio_todo or http_todo queue. aio_todo
   list is kept sorted */
static void
aio_insert(AIOCallback * op, AIO_Reqs * req)
{
#ifdef INKDISKAIO
  op->aiocb.aio_reqprio = AIO_LOWEST_PRIORITY;
#endif

#ifdef AIO_STATS
  num_requests++;
  req->queued++;
#endif
  if (op->aiocb.aio_reqprio == AIO_LOWEST_PRIORITY)     // http request 
  {
    AIOCallback *cb = (AIOCallback *) req->http_aio_todo.tail;
    if (!cb)
      req->http_aio_todo.push(op);
    else
      req->http_aio_todo.insert(op, cb);
  } else {

    AIOCallback *cb = (AIOCallback *) req->aio_todo.tail;

    for (; cb; cb = (AIOCallback *) cb->link.prev) {
      if (cb->aiocb.aio_reqprio >= op->aiocb.aio_reqprio) {
        req->aio_todo.insert(op, cb);
        return;
      }
    }

    /* Either the queue was empty or this request has the highest priority */
    req->aio_todo.push(op);

  }
  return;

}

/* move the request from the atomic list to the queue */
static void
aio_move(AIO_Reqs * req)
{
  AIOCallback *next = NULL, *prev = NULL, *cb = (AIOCallback *) ink_atomiclist_popall(&req->aio_temp_list);
  /* flip the list */

  if (!cb)
    return;

  while (cb->link.next) {
    next = (AIOCallback *) cb->link.next;
    cb->link.next = prev;
    prev = cb;
    cb = next;
  }
  /* fix the last pointer */
  cb->link.next = prev;

  for (; cb; cb = next) {
    next = (AIOCallback *) cb->link.next;
    cb->link.next = NULL;
    cb->link.prev = NULL;
    aio_insert(cb, req);
  }
}

/* queue the new request */
static void
aio_queue_req(AIOCallbackInternal * op)
{
  int thread_ndx = 0;
  AIO_Reqs *req = op->aio_req;
  op->link.next = NULL;;
  op->link.prev = NULL;
#ifdef AIO_STATS
  ink_atomic_increment((int *) &data->num_req, 1);
#endif
#ifdef INKDISKAIO
  if (ts_config_with_inkdiskio) {
    if (DISK_WRITE_THREAD) {
      req = aio_reqs[MY_AIO_THREAD];
      ink_assert(req);
    } else {
      req = aio_reqs[my_aio_thread()];
      if (!req) {
        initialize_thread_for_diskaio(this_ethread());
        req = aio_reqs[my_aio_thread()];
      }
      ink_assert(req);
    }
    goto Lacquirelock;
  }
#endif
  if (!req || req->filedes != op->aiocb.aio_fildes) {
    /* search for the matching file descriptor */
    for (; thread_ndx < num_filedes; thread_ndx++) {
      if (aio_reqs[thread_ndx]->filedes == op->aiocb.aio_fildes) {

        /* found the matching file descriptor */
        req = aio_reqs[thread_ndx];
        break;
      }
    }

    if (!req) {
      ink_mutex_acquire(&insert_mutex);
      if (thread_ndx == num_filedes) {
        /* insert a new entry */
        req = aio_init_fildes(op->aiocb.aio_fildes);

      } else {
        /* a new entry was inserted between the time we checked the 
           aio_reqs and acquired the mutex. check the aio_reqs array to 
           make sure the entry inserted does not correspond  to the current 
           file descriptor */
        for (thread_ndx = 0; thread_ndx < num_filedes; thread_ndx++) {
          if (aio_reqs[thread_ndx]->filedes == op->aiocb.aio_fildes) {
            req = aio_reqs[thread_ndx];
            break;
          }
        }
        if (!req) {
          req = aio_init_fildes(op->aiocb.aio_fildes);
        }

      }
      ink_mutex_release(&insert_mutex);

    }
    op->aio_req = req;
  }
#ifndef INKDISKAIO
  ink_assert(req->filedes == op->aiocb.aio_fildes);
#endif
  ink_atomic_increment(&req->requests_queued, 1);
#ifdef INKDISKAIO
Lacquirelock:
#endif
  if (!ink_mutex_try_acquire(&req->aio_mutex)) {
#ifdef AIO_STATS
    ink_atomic_increment(&data->num_temp, 1);
#endif
    ink_atomiclist_push(&req->aio_temp_list, op);
  } else {
    /* check if any pending requests on the atomic list */
#ifdef AIO_STATS
    ink_atomic_increment(&data->num_queue, 1);
#endif
    if (!INK_ATOMICLIST_EMPTY(req->aio_temp_list)) {
      aio_move(req);
    }
    /* now put the new request */
    aio_insert(op, req);

    ink_cond_signal(&req->aio_cond);
    ink_mutex_release(&req->aio_mutex);
  }
}

static inline int
cache_op(AIOCallbackInternal * op)
{
  bool read = (op->aiocb.aio_lio_opcode == LIO_READ) ? 1 : 0;
  for (; op; op = (AIOCallbackInternal *) op->then) {
    ink_aiocb_t *a = &op->aiocb;
    int err, res = 0;

#ifdef DEBUG
    if (op->sleep_time) {
      ink_sleep(op->sleep_time);
    }
#endif
    while (a->aio_nbytes - res > 0) {
      do {
        if (read)
          err = ink_pread(a->aio_fildes, ((char *) a->aio_buf) + res, a->aio_nbytes - res, a->aio_offset + res);
        else
          err = ink_pwrite(a->aio_fildes, ((char *) a->aio_buf) + res, a->aio_nbytes - res, a->aio_offset + res);
      } while ((err < 0) && (errno == EINTR || errno == ENOBUFS || errno == ENOMEM));
      if (err <= 0) {
#ifdef DIAGS_MODULARIZED
        Warning("cache disk operation failed %s %d %d\n",
                (a->aio_lio_opcode == LIO_READ) ? "READ" : "WRITE", err, errno);
#endif
        op->aio_result = -errno;
        return (err);
      }
      res += err;
    }
    op->aio_result = res;
    ink_assert(op->aio_result == (int) a->aio_nbytes);
  }
  return 1;
}

int
ink_aio_read(AIOCallback * op)
{
  op->aiocb.aio_lio_opcode = LIO_READ;
  switch (AIO_MODE) {
#if (AIO_MODE == AIO_MODE_AIO)
  case AIO_MODE_AIO:
    ink_debug_assert(this_ethread() == op->thread);
    op->thread->aio_ops.enqueue(op);
    if (aio_read(&op->aiocb) < 0) {
      Warning("failed aio_read: %s\n", strerror(errno));
      op->thread->aio_ops.remove(op);
      return -1;
    }
    break;
#endif
  case AIO_MODE_SYNC:
    cache_op((AIOCallbackInternal *) op);
    op->action.continuation->handleEvent(AIO_EVENT_DONE, op);
    break;
  case AIO_MODE_THREAD:{
      aio_queue_req((AIOCallbackInternal *) op);
      break;
    }
#if 0
  case AIO_MODE_INK:
    ink_disk_read(op->aiocb.aio_fildes, (char *) op->aiocb.aio_buf, op->aiocb.aio_nbytes, op->aiocb.aio_offset, op);
    break;
  default:
    ASSERT(0);
    break;
#endif
  }
  return 1;
}

int
ink_aio_write(AIOCallback * op)
{
  op->aiocb.aio_lio_opcode = LIO_WRITE;
  switch (AIO_MODE) {
#if (AIO_MODE == AIO_MODE_AIO)
  case AIO_MODE_AIO:
    ink_debug_assert(this_ethread() == op->thread);
    op->thread->aio_ops.enqueue(op);
    if (aio_write(&op->aiocb) < 0) {
      Warning("failed aio_write: %s\n", strerror(errno));
      op->thread->aio_ops.remove(op);
      return -1;
    }
    break;
#endif
  case AIO_MODE_SYNC:
    cache_op((AIOCallbackInternal *) op);
    op->action.continuation->handleEvent(AIO_EVENT_DONE, op);
    break;
  case AIO_MODE_THREAD:{
      aio_queue_req((AIOCallbackInternal *) op);
      break;
    }
#if 0
  case IO_MODE_INK:
    ink_disk_write(op->aiocb.aio_fildes, (char *) op->aiocb.aio_buf, op->aiocb.aio_nbytes, op->aiocb.aio_offset, op);
    break;
  default:
    ASSERT(0);
    break;
#endif
  }
  return 1;
}

void *
aio_thread_main(void *arg)
{
  AIOThreadInfo *thr_info = (AIOThreadInfo *) arg;
  AIO_Reqs *my_aio_req = (AIO_Reqs *) thr_info->req;
  int timed_wait = thr_info->sleep_wait;
  AIO_Reqs *current_req = NULL;
  AIOCallback *op = NULL;
  ink_mutex_acquire(&my_aio_req->aio_mutex);
  for (;;) {
    do {
      current_req = my_aio_req;

      /* check if any pending requests on the atomic list */

      if (!INK_ATOMICLIST_EMPTY(my_aio_req->aio_temp_list)) {
        aio_move(my_aio_req);
      }

      if (!(op = my_aio_req->aio_todo.pop()) && !(op = my_aio_req->http_aio_todo.pop()))
        break;

#ifdef AIO_STATS
      num_requests--;
      current_req->queued--;
      ink_atomic_increment((int *) &current_req->pending, 1);
#endif

      // update the stats;
      if (op->aiocb.aio_lio_opcode == LIO_WRITE) {
        aio_num_write++;
        aio_bytes_written += op->aiocb.aio_nbytes;
      } else {
        aio_num_read++;
        aio_bytes_read += op->aiocb.aio_nbytes;
      }
      ink_mutex_release(&current_req->aio_mutex);
      if (cache_op((AIOCallbackInternal *) op) <= 0) {
        if (aio_err_callbck) {
          AIOCallback *callback_op = new AIOCallbackInternal();
          callback_op->aiocb.aio_fildes = op->aiocb.aio_fildes;
          callback_op->action = aio_err_callbck;
          eventProcessor.schedule_imm(callback_op);
        }
      }
      ink_atomic_increment((int *) &current_req->requests_queued, -1);
#ifdef AIO_STATS
      ink_atomic_increment((int *) &current_req->pending, -1);
#endif
      op->link.prev = NULL;
      op->link.next = NULL;
      // make op continuation share op->action's mutex
      op->mutex = op->action.mutex;
      /* why do we callback on AIO thread only if its a write??
         See INKqa07855. The problem is that with a lot of users,
         the Net threads take a lot of time (as high as a second)
         to come back to ink_aio_complete. This means that the 
         partition can only issue 1 i/o per second. To 
         get around this problem, we have the aio threads callback
         the partitions directly. The partition issues another i/o
         on the same thread (assuming there is enough stuff to be 
         written). The partition is careful not to callback the VC's 
         and not schedule any events on the thread.
         It does not matter for reads because its generally the 
         CacheVC's that issue reads and they have to do a fair
         bit of computation (go through the docheader, call http
         state machine back, etc) before they can issue another
         read.
       */
      if (op->aiocb.aio_lio_opcode == LIO_WRITE) {
        MUTEX_TRY_LOCK(lock, op->mutex, thr_info->mutex->thread_holding);
        if (!lock) {
          eventProcessor.schedule_imm(op);
        } else {
          if (!op->action.cancelled)
            op->action.continuation->handleEvent(AIO_EVENT_DONE, op);
        }
      } else {
        eventProcessor.schedule_imm(op);

      }
      ink_mutex_acquire(&my_aio_req->aio_mutex);

    } while (1);
    if (timed_wait) {
      timespec ts = ink_based_hrtime_to_timespec(ink_get_hrtime() + HRTIME_MSECONDS(cache_config_aio_sleep_time));
      ink_cond_timedwait(&my_aio_req->aio_cond, &my_aio_req->aio_mutex, &ts);
    } else
      ink_cond_wait(&my_aio_req->aio_cond, &my_aio_req->aio_mutex);
  }
  return 0;
}
