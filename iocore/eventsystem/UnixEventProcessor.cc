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

#include "P_EventSystem.h"      /* MAGIC_EDITING_TAG */



EventType
EventProcessor::spawn_event_threads(int n_threads)
{
  EventType new_thread_group_id;
  int i;

  ink_assert(n_threads > 0);
  if (n_threads <= 0)
    return -1;

  if ((n_ethreads + n_threads) > MAX_EVENT_THREADS)
    return -1;

  if (n_thread_groups >= MAX_EVENT_TYPES)
    return -1;

  new_thread_group_id = (EventType) n_thread_groups;

  for (i = 0; i < n_threads; i++) {
    EThread *t = NEW(new EThread(REGULAR, n_ethreads + i));
    all_ethreads[n_ethreads + i] = t;
    eventthread[new_thread_group_id][i] = t;
    t->set_event_type(new_thread_group_id);
  }

  n_threads_for_type[new_thread_group_id] = n_threads;
  for (i = 0; i < n_threads; i++)
    eventthread[new_thread_group_id][i]->start();

  n_thread_groups++;
  n_ethreads += n_threads;
  return new_thread_group_id;
}


#if 1
#define INK_NO_CLUSTER
#endif

struct EventProcessor eventProcessor;

int
EventProcessor::start(int n_event_threads)
{
  int i;

  // do some sanity checking.
  static int started = 0;
  ink_release_assert(!started);
  ink_release_assert(n_event_threads > 0 && n_event_threads <= MAX_EVENT_THREADS);
  started = 1;

  n_ethreads = n_event_threads;
  n_thread_groups = 1;

  int first_thread = 1;

  for (i = 0; i < n_event_threads; i++) {
    EThread *t = NEW(new EThread(REGULAR, i));
    if (first_thread && !i) {
      ink_thread_setspecific(Thread::thread_data_key, t);
      global_mutex = t->mutex;
      t->cur_time = ink_get_based_hrtime_internal();
    }
    all_ethreads[i] = t;

    eventthread[ET_CALL][i] = t;
    t->set_event_type((EventType) ET_CALL);
  }
  n_threads_for_type[ET_CALL] = n_event_threads;
  for (i = first_thread; i < n_ethreads; i++)
    all_ethreads[i]->start();

  return 0;
}

void
EventProcessor::shutdown()
{
}

Event *
EventProcessor::spawn_thread(Continuation * cont, ink_sem * sem)
{
  Event *e = eventAllocator.alloc();
  e->init(cont, 0, 0);
  dthreads[n_dthreads] = NEW(new EThread(DEDICATED, e, sem));
  e->ethread = dthreads[n_dthreads];
  e->mutex = e->continuation->mutex = dthreads[n_dthreads]->mutex;
  n_dthreads++;
  e->ethread->start();
  return e;
}
