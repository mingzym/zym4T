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

#ifndef _P_UnixEventProcessor_h_
#define _P_UnixEventProcessor_h_
#include "I_EventProcessor.h"

const int LOAD_BALANCE_INTERVAL = 1;


inline
EventProcessor::EventProcessor():
n_ethreads(0),
n_thread_groups(0),
n_dthreads(0),
thread_data_used(0)
{
  memset(all_ethreads, 0, sizeof(all_ethreads));
  memset(dthreads, 0, sizeof(dthreads));
  memset(n_threads_for_type, 0, sizeof(n_threads_for_type));
  memset(next_thread_for_type, 0, sizeof(next_thread_for_type));
}

inline off_t
EventProcessor::allocate(int size)
{
  static off_t start = (ink_offsetof(EThread, thread_private) + 7) & ~7;
  static off_t loss = start - ink_offsetof(EThread, thread_private);
  size = (size + 7) & ~7;       // 8 byte alignment

  int old;
  do {
    old = thread_data_used;
    if (old + loss + size > PER_THREAD_DATA)
      return -1;
  } while (!ink_atomic_cas(&thread_data_used, old, old + size));

  return (off_t) (old + start);
}

inline EThread *
EventProcessor::assign_thread(EventType etype)
{
  int next;
  if (n_threads_for_type[etype] > 1)
    next = next_thread_for_type[etype]++ % n_threads_for_type[etype];
  else
    next = 0;
  return (eventthread[etype][next]);
}

inline Event *
EventProcessor::schedule(Event * e, EventType etype)
{
  e->ethread = assign_thread(etype);
  if (e->continuation->mutex)
    e->mutex = e->continuation->mutex;
  else
    e->mutex = e->continuation->mutex = e->ethread->mutex;
  e->ethread->EventQueueExternal.enqueue(e);
  return e;
}

#if 0
/* getting rid of this, i dont see anybody using this */
inline Event *
EventProcessor::schedule_spawn(Continuation * cont)
{
  Event *e = eventAllocator.alloc();
  return schedule(e->init(cont, 0, 0), ET_SPAWN);
}
#endif

inline Event *
EventProcessor::schedule_imm(Continuation * cont, EventType et, int callback_event, void *cookie)
{
  Event *e = eventAllocator.alloc();
#ifdef ENABLE_TIME_TRACE
  e->start_time = ink_get_hrtime();
#endif
  e->callback_event = callback_event;
  e->cookie = cookie;
  return schedule(e->init(cont, 0, 0), et);
}

inline Event *
EventProcessor::schedule_at(Continuation * cont, ink_hrtime t, EventType et, int callback_event, void *cookie)
{
  ink_assert(t > 0);
  Event *e = eventAllocator.alloc();
  e->callback_event = callback_event;
  e->cookie = cookie;
  return schedule(e->init(cont, t, 0), et);
}

inline Event *
EventProcessor::schedule_in(Continuation * cont, ink_hrtime t, EventType et, int callback_event, void *cookie)
{
  Event *e = eventAllocator.alloc();
  e->callback_event = callback_event;
  e->cookie = cookie;
  return schedule(e->init(cont, ink_get_based_hrtime() + t, 0), et);
}

inline Event *
EventProcessor::schedule_every(Continuation * cont, ink_hrtime t, EventType et, int callback_event, void *cookie)
{
  ink_assert(t != 0);
  Event *e = eventAllocator.alloc();
  e->callback_event = callback_event;
  e->cookie = cookie;
  if (t < 0)
    return schedule(e->init(cont, t, t), et);
  else
    return schedule(e->init(cont, ink_get_based_hrtime() + t, t), et);
}


#endif
