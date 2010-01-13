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

#include "P_Net.h"

ink_hrtime last_throttle_warning;
ink_hrtime last_shedding_warning;
ink_hrtime emergency_throttle_time;
int net_connections_throttle;
int fds_throttle;
int fds_limit = 8000;
ink_hrtime last_transient_accept_error;

extern "C" void fd_reify(struct ev_loop *);


#ifndef INACTIVITY_TIMEOUT
// INKqa10496
// One Inactivity cop runs on each thread once every second and
// loops through the list of NetVCs and calls the timeouts
struct InactivityCop:public Continuation
{
  InactivityCop(ProxyMutex *m):Continuation(m)
  {
    SET_HANDLER(&InactivityCop::check_inactivity);
  }
  int check_inactivity(int event, Event *e)
  {
    (void) event;
    ink_hrtime now = ink_get_hrtime();
    NetHandler *nh = get_NetHandler(this_ethread());
    UnixNetVConnection *vc = nh->open_list.head, *vc_next = 0;
    while (vc) {
      vc_next = (UnixNetVConnection*)vc->link.next;
      if (vc->inactivity_timeout_in && vc->next_inactivity_timeout_at && vc->next_inactivity_timeout_at < now)
        vc->handleEvent(EVENT_IMMEDIATE, e);
      else
        if (vc->closed)
          close_UnixNetVConnection(vc, e->ethread);
      vc = vc_next;
    }
    return 0;
  }
};
#endif

PollCont::PollCont(ProxyMutex *m):Continuation(m), net_handler(NULL), poll_timeout(DEFAULT_POLL_TIMEOUT)
{
  pollDescriptor = NEW(new PollDescriptor);
  pollDescriptor->init();
  SET_HANDLER(&PollCont::pollEvent);
}

PollCont::PollCont(ProxyMutex *m, NetHandler *nh):Continuation(m), net_handler(nh),
poll_timeout(DEFAULT_POLL_TIMEOUT)
{
  pollDescriptor = NEW(new PollDescriptor);
  pollDescriptor->init();
  SET_HANDLER(&PollCont::pollEvent);
}

PollCont::~PollCont()
{
  delete pollDescriptor;
}

//
// Changed by YTS Team, yamsat
// PollCont continuation which does the epoll_wait
// and stores the resultant events in ePoll_Triggered_Events
//
int
PollCont::pollEvent(int event, Event *e)
{
  (void) event;
  (void) e;

  if (likely(net_handler)) {
    /* checking to see whether there are connections on the ready_queue (either read or write) that need processing [ebalsa] */
    if (likely
        (!net_handler->read_ready_list.empty() || !net_handler->read_ready_list.empty() ||
         !net_handler->read_enable_list.empty() || !net_handler->write_enable_list.empty())) {
      Debug("epoll", "rrq: %d, wrq: %d, rel: %d, wel: %d", net_handler->read_ready_list.empty(),
            net_handler->write_ready_list.empty(), net_handler->read_enable_list.empty(),
            net_handler->write_enable_list.empty());
      poll_timeout = 0;         //poll immediately returns -- we have triggered stuff to process right now
    } else {
      poll_timeout = DEFAULT_POLL_TIMEOUT;
    }
  }
  // wait for fd's to tigger, or don't wait if timeout is 0
#if defined(USE_LIBEV)
  struct ev_loop *eio = pollDescriptor->eio;
  fd_reify(eio);
  eio->backend_poll(eio, (double)poll_timeout/1000.0);
  pollDescriptor->result = eio->pendingcnt[0];
#elif defined(USE_EPOLL)
  pollDescriptor->result = epoll_wait(pollDescriptor->epoll_fd,
                                      pollDescriptor->ePoll_Triggered_Events, POLL_DESCRIPTOR_SIZE, poll_timeout);
  Debug("epoll", "[PollCont::pollEvent] epoll_fd: %d, timeout: %d, results: %d", pollDescriptor->epoll_fd, poll_timeout,
        pollDescriptor->result);
  Debug("iocore_net", "pollEvent : Epoll fd %d active\n", pollDescriptor->epoll_fd);
#elif defined(USE_KQUEUE)
  struct timespec tv;
  tv.tv_sec = poll_timeout / 1000;
  tv.tv_nsec = 1000000 * (poll_timeout % 1000);
  pollDescriptor->result = kevent(pollDescriptor->kqueue_fd, NULL, 0,
                                  pollDescriptor->kq_Triggered_Events,
                                  POLL_DESCRIPTOR_SIZE,
                                  &tv);
  Debug("kqueue", "[PollCont::pollEvent] kueue_fd: %d, timeout: %d, results: %d", pollDescriptor->kqueue_fd, poll_timeout,
        pollDescriptor->result);
  Debug("iocore_net", "pollEvent : KQueue fd %d active\n", pollDescriptor->kqueue_fd);
#else
#error port me
#endif
  return EVENT_CONT;
}

static void
net_signal_hook_callback(EThread *thread) {
#if HAVE_EVENTFD
  inku64 counter;
  read(thread->evfd, &counter, sizeof(inku64));
#else
  char dummy;
  read(thread->evpipe[0], &dummy, 1);
#endif  
}

static void
net_signal_hook_function(EThread *thread) {
#if HAVE_EVENTFD
  inku64 counter = 1;
  write(thread->evfd, &counter, sizeof(inku64));
#else
  char dummy;
  write(thread->evpipe[0], &dummy, 1);
#endif  
}

void
initialize_thread_for_net(EThread *thread, int thread_index)
{
  static bool poll_delay_read = false;
  int max_poll_delay;           // max poll delay in milliseconds
  if (!poll_delay_read) {
    IOCORE_ReadConfigInteger(max_poll_delay, "proxy.config.net.max_poll_delay");
    if ((max_poll_delay & (max_poll_delay - 1)) ||
        (max_poll_delay<NET_PRIORITY_MSEC) || (max_poll_delay> MAX_NET_BUCKETS * NET_PRIORITY_MSEC)) {
      IOCORE_SignalWarning(REC_SIGNAL_SYSTEM_ERROR, "proxy.config.net.max_poll_delay range is [4-1024]");
    }
    poll_delay_read = true;
  }

  new((ink_dummy_for_new *) get_NetHandler(thread)) NetHandler();
  new((ink_dummy_for_new *) get_PollCont(thread)) PollCont(thread->mutex, get_NetHandler(thread));
  get_NetHandler(thread)->mutex = new_ProxyMutex();
  PollCont *pc = get_PollCont(thread);
  PollDescriptor *pd = pc->pollDescriptor;
#if defined(USE_LIBEV)
  if (!thread_index)
    pd->eio = ev_default_loop(0);
  else
    pd->eio = ev_loop_new(0);
#endif
  thread->schedule_imm(get_NetHandler(thread));

#ifndef INACTIVITY_TIMEOUT
  InactivityCop *inactivityCop = NEW(new InactivityCop(get_NetHandler(thread)->mutex));
  thread->schedule_every(inactivityCop, HRTIME_SECONDS(1));
#endif

  thread->signal_hook = net_signal_hook_function;
  thread->ep = (EventIO*)malloc(sizeof(EventIO));
  thread->ep->type = EVENTIO_ASYNC_SIGNAL;
#if HAVE_EVENTFD
  thread->ep->start(pd, thread->evfd, 0, EVENTIO_READ);
#else
  thread->ep->start(pd, thread->evpipe[0], 0, EVENTIO_READ);
#endif
}

// NetHandler method definitions

NetHandler::NetHandler():Continuation(NULL), trigger_event(0)
{
  SET_HANDLER((NetContHandler) & NetHandler::startNetEvent);
}

//
// Initialization here, in the thread in which we will be executing
// from now on.
//
int
NetHandler::startNetEvent(int event, Event *e)
{
  (void) event;
  SET_HANDLER((NetContHandler) & NetHandler::mainNetEvent);
  e->schedule_every(NET_PERIOD);
  trigger_event = e;
  return EVENT_CONT;
}

//
// Move VC's enabled on a different thread to the ready list
//
void
NetHandler::process_enabled_list(NetHandler *nh, EThread *t)
{
  UnixNetVConnection *vc = NULL;

  SList(UnixNetVConnection, read.enable_link) rq(nh->read_enable_list.popall());
  while ((vc = rq.pop())) {
    vc->ep.modify(EVENTIO_READ);
    vc->read.in_enabled_list = 0;
    if ((vc->read.enabled && vc->read.triggered) || vc->closed)
      nh->read_ready_list.in_or_enqueue(vc);
  }

  SList(UnixNetVConnection, write.enable_link) wq(nh->write_enable_list.popall());
  while ((vc = wq.pop())) {
    vc->ep.modify(EVENTIO_WRITE);
    vc->write.in_enabled_list = 0;
    if ((vc->write.enabled && vc->write.triggered) || vc->closed)
      nh->write_ready_list.in_or_enqueue(vc);
  }
}


//
// The main event for NetHandler
// This is called every NET_PERIOD, and handles all IO operations scheduled
// for this period.
//
int
NetHandler::mainNetEvent(int event, Event *e)
{
  ink_assert(trigger_event == e && (event == EVENT_INTERVAL || event == EVENT_POLL));
  (void) event;
  (void) e;
  EventIO *epd = NULL;
  int poll_timeout = DEFAULT_POLL_TIMEOUT;

  NET_INCREMENT_DYN_STAT(net_handler_run_stat);

  process_enabled_list(this, e->ethread);
  if (likely(!read_ready_list.empty() || !write_ready_list.empty() ||
             !read_enable_list.empty() || !write_enable_list.empty()))
    poll_timeout = 0; // poll immediately returns -- we have triggered stuff to process right now
  else
    poll_timeout = DEFAULT_POLL_TIMEOUT;

  PollDescriptor *pd = get_PollDescriptor(trigger_event->ethread);
  UnixNetVConnection *vc = NULL;
#if defined(USE_LIBEV)
  struct ev_loop *eio = pd->eio;
  fd_reify(eio);
  eio->backend_poll(eio, (double)poll_timeout/1000.0);
  pd->result = eio->pendingcnt[0];
#elif defined(USE_EPOLL)
  pd->result = epoll_wait(pd->epoll_fd, pd->ePoll_Triggered_Events, POLL_DESCRIPTOR_SIZE, poll_timeout);
#elif defined(USE_KQUEUE)
  struct timespec tv;
  tv.tv_sec = poll_timeout / 1000;
  tv.tv_nsec = 1000000 * (poll_timeout % 1000);
  pd->result = kevent(pd->kqueue_fd, NULL, 0,
                      pd->kq_Triggered_Events, POLL_DESCRIPTOR_SIZE,
                      &tv);

#else
#error port me
#endif

  vc = NULL;
  for (int x = 0; x < pd->result; x++) {
    epd = (EventIO*) get_ev_data(pd,x);
    if (epd->type == EVENTIO_READWRITE_VC) {
      vc = epd->data.vc;
      if (get_ev_events(pd,x) & (EVENTIO_READ)) {
        vc->read.triggered = 1;
        vc->addLogMessage("read triggered");
        if (!read_ready_list.in(vc))
          read_ready_list.enqueue(vc);
        else if (get_ev_events(pd,x) & EVENTIO_ERROR) {
          // check for unhandled epoll events that should be handled
          Debug("epoll_miss", "Unhandled epoll event on read: 0x%04x read.enabled=%d closed=%d read.netready_queue=%d",
                get_ev_events(pd,x), vc->read.enabled, vc->closed, read_ready_list.in(vc));
        }
      }
      vc = epd->data.vc;
      if (get_ev_events(pd,x) & EVENTIO_WRITE) {
        vc->write.triggered = 1;
        vc->addLogMessage("write triggered");
        if (!write_ready_list.in(vc))
          write_ready_list.enqueue(vc);
        else if (get_ev_events(pd,x) & EVENTIO_ERROR) {
          // check for unhandled epoll events that should be handled
          Debug("epoll_miss",
                "Unhandled epoll event on write: 0x%04x write.enabled=%d closed=%d write.netready_queue=%d",
                get_ev_events(pd,x), vc->write.enabled, vc->closed, write_ready_list.in(vc));
        }
      } else if (!(get_ev_events(pd,x) & EVENTIO_READ) && get_ev_events(pd,x) & EVENTIO_ERROR) {
        Debug("epoll_miss", "Unhandled epoll event: 0x%04x", get_ev_events(pd,x));
      }
    } else if (epd->type == EVENTIO_DNS_CONNECTION) {
      if (epd->data.dnscon != NULL)
        dnsqueue.enqueue(epd->data.dnscon);
    } else if (epd->type == EVENTIO_ASYNC_SIGNAL)
      net_signal_hook_callback(trigger_event->ethread);
    ev_next_event(pd);
  }

  pd->result = 0;

#if !defined(USE_LIBEV) && defined(USE_EPOLL) && defined(USE_EDGE_TRIGGER_EPOLL)
  UnixNetVConnection *next_vc = NULL;
  vc = read_ready_list.head;
  while (vc) {
    next_vc = vc->read.ready_link.next;
#else
  while ((vc = read_ready_list.dequeue())) {
#endif
    if (vc->closed)
      close_UnixNetVConnection(vc, trigger_event->ethread);
    else if (vc->read.enabled && vc->read.triggered)
      vc->net_read_io(this, trigger_event->ethread);
    else if (!vc->read.enabled)
#if !defined(USE_LIBEV) && defined(USE_EPOLL) && defined(USE_EDGE_TRIGGER_EPOLL)
      read_ready_list.remove(vc);
#else
      vc->ep.modify(-EVENTIO_READ);
#endif
#if defined(USE_EPOLL) && defined(USE_EDGE_TRIGGER_EPOLL)
    vc = next_vc;
  }
#else
  }
#endif

#if !defined(USE_LIBEV) && defined(USE_EPOLL) && defined(USE_EDGE_TRIGGER_EPOLL)
  next_vc = NULL;
  vc = write_ready_list.head;
  while (vc) {
    next_vc = vc->write.ready_link.next;
#else
  while ((vc = write_ready_list.dequeue())) {
#endif
    if (vc->closed)
      close_UnixNetVConnection(vc, trigger_event->ethread);
    else if (vc->write.enabled && vc->write.triggered)
      write_to_net(this, vc, pd, trigger_event->ethread);
    else if (!vc->write.enabled)
#if !defined(USE_LIBEV) && defined(USE_EPOLL) && defined(USE_EDGE_TRIGGER_EPOLL)
      write_ready_list.remove(vc);
#else
      vc->ep.modify(-EVENTIO_WRITE);
#endif
#if defined(USE_EPOLL) && defined(USE_EDGE_TRIGGER_EPOLL)
    vc = next_vc;
  }
#else
  }
#endif

  return EVENT_CONT;
}

