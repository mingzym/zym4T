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

//////////////////////////////////////////////////////////////////////
//
// The EThread Class
//
/////////////////////////////////////////////////////////////////////
#include "ink_unused.h"      /* MAGIC_EDITING_TAG */
#include "P_EventSystem.h"

struct AIOCallback;

#define MAX_HEARTBEATS_MISSED         	10
#define NO_HEARTBEAT                  	-1
#define THREAD_MAX_HEARTBEAT_MSECONDS	60
#define NO_ETHREAD_ID                   -1

EThread::EThread()
:generator(time(NULL) ^ (long) this),
ethreads_to_be_signalled(NULL),
n_ethreads_to_be_signalled(0),
main_accept_index(-1),
id(NO_ETHREAD_ID), event_types(0), tt(REGULAR), eventsem(NULL)
{
  memset(thread_private, 0, PER_THREAD_DATA);
}

EThread::EThread(ThreadType att, int anid)
  :
generator(time(NULL) ^ (long) this),
ethreads_to_be_signalled(NULL),
n_ethreads_to_be_signalled(0),
main_accept_index(-1),
id(anid),
event_types(0),
tt(att),
eventsem(NULL)
{
  ethreads_to_be_signalled = (EThread **) xmalloc(MAX_EVENT_THREADS * sizeof(EThread *));
  memset((char *) ethreads_to_be_signalled, 0, MAX_EVENT_THREADS * sizeof(EThread *));
  memset(thread_private, 0, PER_THREAD_DATA);
}

EThread::EThread(ThreadType att, Event * e, ink_sem * sem)
:generator(time(NULL) ^ (long) this),
ethreads_to_be_signalled(NULL),
n_ethreads_to_be_signalled(0),
main_accept_index(-1),
id(NO_ETHREAD_ID), event_types(0), tt(att), oneevent(e), eventsem(sem)
{
  ink_assert(att == DEDICATED);
  memset(thread_private, 0, PER_THREAD_DATA);
}


// Provide a destructor so that SDK functions which create and destroy
// threads won't have to deal with EThread memory deallocation.
EThread::~EThread()
{
  if (n_ethreads_to_be_signalled > 0)
    flush_signals(this);
  if (ethreads_to_be_signalled)
    xfree(ethreads_to_be_signalled);
}

bool
EThread::is_event_type(EventType et)
{
  return !!(event_types & (1 << (int) et));
}

void
EThread::set_event_type(EventType et)
{
  event_types |= (1 << (int) et);
}

void
EThread::process_event(Event * e, int calling_code)
{
  ink_assert((!e->in_the_prot_queue && !e->in_the_priority_queue));
  MUTEX_TRY_LOCK_FOR(lock, e->mutex.m_ptr, this, e->continuation);
  if (!lock) {
    e->timeout_at = cur_time + DELAY_FOR_RETRY;
    EventQueueExternal.enqueue_local(e);
  } else {
    if (e->cancelled) {
      free_event(e);
      return;
    }
    Continuation *c_temp = e->continuation;
    e->continuation->handleEvent(calling_code, e);
    ink_assert(!e->in_the_priority_queue);
    ink_assert(c_temp == e->continuation);
    MUTEX_RELEASE(lock);
    if (e->period) {
      if (!e->in_the_prot_queue && !e->in_the_priority_queue) {
        if (e->period < 0)
          e->timeout_at = e->period;
        else {
          cur_time = ink_get_based_hrtime();
          e->timeout_at = cur_time + e->period;
          if (e->timeout_at < cur_time)
            e->timeout_at = cur_time;
        }
        EventQueueExternal.enqueue_local(e);
      }
    } else if (!e->in_the_prot_queue && !e->in_the_priority_queue)
      free_event(e);
  }
}

//
// void  EThread::execute()
//
// Execute loops forever on:
// Find the earliest event.
// Sleep until the event time or until an earlier event is inserted
// When its time for the event, try to get the appropriate continuation
// lock. If successful, call the continuation, otherwise put the event back
// into the queue.
//
#ifdef ENABLE_TIME_TRACE
int immediate_events_time_dist[TIME_DIST_BUCKETS_SIZE];
int cnt_immediate_events;
#endif

extern struct EventProcessor eventProcessor;
void
EThread::execute()
{
  Event *e;
  ink_hrtime next_time = 0, sleep_time;
  Que(Event, link) NegativeQueue;
  switch (tt) {

  case REGULAR:{
      // Try to give priority to immediate events.

      for (;;) {
        // Execute all the available external events that have
        // already been dequeued
        cur_time = ink_get_based_hrtime_internal();
        while ((e = EventQueueExternal.dequeue_local())) {
          if (!e->timeout_at) {
            ink_assert(e->period == 0);
            process_event(e, EVENT_IMMEDIATE);
          } else {

            if (e->timeout_at < 0) {
#ifdef FIXME
              if (-e->timeout_at < min_neg_timeout)
                min_neg_timeout = -e->timeout_at;
#endif
              Event *p = NULL;
              Event *a = NegativeQueue.head;
              while (a && a->timeout_at > e->timeout_at) {
                p = a;
                a = a->link.next;
              }
              if (!a)
                NegativeQueue.enqueue(e);
              else
                NegativeQueue.insert(e, p);
            } else
              EventQueue.enqueue(e, cur_time);
          }
        }
        bool done_one;
        do {
          done_one = false;
          // Execute all the eligible internal events
          EventQueue.check_ready(cur_time, this);
          while ((e = EventQueue.dequeue_ready(cur_time))) {
            ink_assert(e);
            ink_assert(e->timeout_at > 0);
            if (e->cancelled)
              free_event(e);
            else {
              done_one = true;
              process_event(e, e->immediate ? EVENT_IMMEDIATE : EVENT_INTERVAL);
            }
          }
        } while (done_one);

        if (NegativeQueue.head) {
          if (n_ethreads_to_be_signalled)
            flush_signals(this);


          // dequeue all the external events and put them in a local
          // queue. If there are no external events available, don't
          // do a cond_timedwait.
          if (!INK_ATOMICLIST_EMPTY(EventQueueExternal.al))
            EventQueueExternal.dequeue_timed(cur_time, next_time, false);
          while ((e = EventQueueExternal.dequeue_local())) {
            if (!e->timeout_at)
              process_event(e, EVENT_IMMEDIATE);
            else {
              if (e->cancelled)
                free_event(e);
              else {
                // If its a negative event, it must be a result of
                // a negative event, which has been turned into a 
                // timed-event (because of a missed lock), executed 
                // before the poll. So, it must
                // be executed in this round (because you can't have
                // more than one poll between two executions of a
                // negative event)
                if (e->timeout_at < 0) {
                  Event *p = NULL;
                  Event *a = NegativeQueue.head;
                  while (a && a->timeout_at > e->timeout_at) {
                    p = a;
                    a = a->link.next;
                  }
                  if (!a)
                    NegativeQueue.enqueue(e);
                  else
                    NegativeQueue.insert(e, p);
                } else
                  EventQueue.enqueue(e, cur_time);
              }
            }
          }
          // execute poll events
          while ((e = NegativeQueue.dequeue()) != NULL) {
            process_event(e, EVENT_POLL);
          }
          // fixme min_neg_timeout = HRTIME_FOREVER;
          if (!INK_ATOMICLIST_EMPTY(EventQueueExternal.al))
            EventQueueExternal.dequeue_timed(cur_time, next_time, false);
        } else {                // Means there are no negative events
          next_time = EventQueue.earliest_timeout();
          sleep_time = next_time - cur_time;
          if (sleep_time > THREAD_MAX_HEARTBEAT_MSECONDS * HRTIME_MSECOND) {
            next_time = cur_time + THREAD_MAX_HEARTBEAT_MSECONDS * HRTIME_MSECOND;
            sleep_time = THREAD_MAX_HEARTBEAT_MSECONDS * HRTIME_MSECOND;
          }
          // dequeue all the external events and put them in a local
          // queue. If there are no external events available, do a
          // cond_timedwait.
          if (n_ethreads_to_be_signalled)
            flush_signals(this);
          EventQueueExternal.dequeue_timed(cur_time, next_time, true);
        }
      }

    }
  case DEDICATED:{
      // if (DEBUG) cout << "Inside execute (dedicated thread)\n";
      // coverity[lock]
      if (eventsem)
        ink_sem_wait(eventsem);
      MUTEX_TAKE_LOCK_FOR(oneevent->mutex, this, oneevent->continuation);
      oneevent->continuation->handleEvent(EVENT_IMMEDIATE, oneevent);
      MUTEX_UNTAKE_LOCK(oneevent->mutex, this);
      free_event(oneevent);
      break;
    }
  default:
    ink_assert(!"bad case value (execute)");
    break;
  }                             /* End switch */
  // coverity[missing_unlock]
}
