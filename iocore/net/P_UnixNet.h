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

#ifndef __P_UNIXNET_H__
#define __P_UNIXNET_H__

#include "inktomi++.h"

#define USE_EDGE_TRIGGER_EPOLL  1
#define USE_EDGE_TRIGGER_KQUEUE  1
#define USE_EDGE_TRIGGER_PORT  1


#define EVENTIO_NETACCEPT               1
#define EVENTIO_READWRITE_VC		2
#define EVENTIO_DNS_CONNECTION		3
#define EVENTIO_UDP_CONNECTION		4
#define EVENTIO_ASYNC_SIGNAL		5

#if defined(USE_LIBEV)
#define EVENTIO_READ EV_READ
#define EVENTIO_WRITE EV_WRITE
#define EVENTIO_ERROR EV_ERROR
#elif defined(USE_EPOLL)
#ifdef USE_EDGE_TRIGGER_EPOLL
#define USE_EDGE_TRIGGER 1
#define EVENTIO_READ (EPOLLIN|EPOLLET)
#define EVENTIO_WRITE (EPOLLOUT|EPOLLET)
#else
#define EVENTIO_READ EPOLLIN
#define EVENTIO_WRITE EPOLLOUT
#endif
#define EVENTIO_ERROR (EPOLLERR|EPOLLPRI|EPOLLHUP)
#elif defined(USE_KQUEUE)
#ifdef USE_EDGE_TRIGGER_KQUEUE
#define USE_EDGE_TRIGGER 1
#define INK_EV_EDGE_TRIGGER EV_CLEAR
#else
#define INK_EV_EDGE_TRIGGER 0
#endif
#define EVENTIO_READ INK_EVP_IN
#define EVENTIO_WRITE INK_EVP_OUT
#define EVENTIO_ERROR (0x010|0x002|0x020) // ERR PRI HUP
#elif defined(USE_PORT)
#ifdef USE_EDGE_TRIGGER_PORT
#define USE_EDGE_TRIGGER 1
#endif
#define EVENTIO_READ  POLLIN
#define EVENTIO_WRITE POLLOUT
#define EVENTIO_ERROR (POLLERR|POLLPRI|POLLHUP)
#else
#error port me
#endif

#ifdef USE_LIBEV
#define EV_MINPRI 0
#define EV_MAXPRI 0
#include "ev.h"
typedef void (*eio_cb_t)(struct ev_loop*, struct ev_io*, int);
// expose libev internals
#define NUMPRI (EV_MAXPRI - EV_MINPRI + 1)
typedef void ANFD;
typedef struct {
  ev_watcher *w;
  int events;
} ANPENDING;
typedef void ANHE;
typedef ev_watcher *W;
struct ev_loop
{
  ev_tstamp ev_rt_now;
#define ev_rt_now ((loop)->ev_rt_now)
#define VAR(name,decl) decl;
#include "ev_vars.h"
#undef VAR
};
extern "C" void fd_change(struct ev_loop *, int fd, int flags);
#endif /* USE_LIBEV */

class PollDescriptor;
typedef PollDescriptor *EventLoop;

class UnixNetVConnection;
class DNSConnection;
class NetAccept;
class UnixUDPConnection;
struct EventIO
{
#ifdef USE_LIBEV
  ev_io eio;
#define evio_get_port(e) ((e)->eio.fd)
#else
  int fd;
#define evio_get_port(e) ((e)->fd)
#endif
#if defined(USE_KQUEUE) || (defined(USE_EPOLL) && !defined(USE_EDGE_TRIGGER)) || defined(USE_PORT)
  int events;
#endif
  EventLoop event_loop;
  int type;
  union
  {
    Continuation *c;
    UnixNetVConnection *vc;
    DNSConnection *dnscon;
    NetAccept *na;
    UnixUDPConnection *uc;
#if defined(USE_OLD_EVENTFD)
    int fd;
#endif
  } data;
  int start(EventLoop l, DNSConnection *vc, int events);
  int start(EventLoop l, NetAccept *vc, int events);
  int start(EventLoop l, UnixNetVConnection *vc, int events);
  int start(EventLoop l, UnixUDPConnection *vc, int events);
  int start(EventLoop l, int fd, Continuation *c, int events);
  // Change the existing events by adding modify(EVENTIO_READ)
  // or removing modify(-EVENTIO_READ), for level triggered I/O
  int modify(int events);
  // Refresh the existing events (i.e. KQUEUE EV_CLEAR), for edge triggered I/O
  int refresh(int events);
  int stop();
  int close();
  EventIO() { 
#ifndef USE_LIBEV
    fd = 0;
#endif
    type = 0;
    data.c = 0;
  }
};

#include "P_UnixNetProcessor.h"
#include "P_UnixNetVConnection.h"
#include "P_NetAccept.h"
#include "P_DNSConnection.h"
#include "P_UnixUDPConnection.h"
#include "P_UnixPollDescriptor.h"


struct UnixNetVConnection;
struct NetHandler;
typedef int (NetHandler::*NetContHandler) (int, void *);
typedef unsigned int inku32;

extern ink_hrtime last_throttle_warning;
extern ink_hrtime last_shedding_warning;
extern ink_hrtime emergency_throttle_time;
extern int net_connections_throttle;
extern int fds_throttle;
extern bool throttle_enabled;
extern int fds_limit;
extern ink_hrtime last_transient_accept_error;
extern int http_accept_port_number;


//#define INACTIVITY_TIMEOUT
//
// Configuration Parameter had to move here to share 
// between UnixNet and UnixUDPNet or SSLNet modules.
// Design notes are in Memo.NetDesign
//

#define THROTTLE_FD_HEADROOM                      (128 + 64)    // CACHE_DB_FDS + 64

#define TRANSIENT_ACCEPT_ERROR_MESSAGE_EVERY      HRTIME_HOURS(24)
#define ACCEPT_THREAD_POLL_TIMEOUT                100   // msecs
#define NET_PRIORITY_MSEC                         4
#define NET_PRIORITY_PERIOD                       HRTIME_MSECONDS(NET_PRIORITY_MSEC)
// p' = p + (p / NET_PRIORITY_LOWER)
#define NET_PRIORITY_LOWER                        2
// p' = p / NET_PRIORITY_HIGHER
#define NET_PRIORITY_HIGHER                       2
#define NET_RETRY_DELAY                           HRTIME_MSECONDS(10)
#define MAX_ACCEPT_PERIOD                         HRTIME_MSECONDS(100)

// also the 'throttle connect headroom'
#define THROTTLE_AT_ONCE                          5
#define EMERGENCY_THROTTLE                        16
#define HYPER_EMERGENCY_THROTTLE                  6

#define NET_THROTTLE_ACCEPT_HEADROOM              1.1   // 10%
#define NET_THROTTLE_CONNECT_HEADROOM             1.0   // 0%
#define NET_THROTTLE_MESSAGE_EVERY                HRTIME_MINUTES(10)
#define NET_PERIOD                                -HRTIME_MSECONDS(5)
#define ACCEPT_PERIOD                             -HRTIME_MSECONDS(4)
#define NET_INITIAL_PRIORITY                      0
#define MAX_NET_BUCKETS                           256
#define MAX_EPOLL_ARRAY_SIZE                      (1024*16)
#define MAX_EPOLL_TIMEOUT                         50    /* mseconds */
/* NOTE: moved DEFAULT_POLL_TIMEOUT to I_NetConfig.h */
#define NET_THROTTLE_DELAY                        50    /* mseconds */
#define INK_MIN_PRIORITY                          0

#define PRINT_IP(x) ((inku8*)&(x))[0],((inku8*)&(x))[1], ((inku8*)&(x))[2],((inku8*)&(x))[3]


// function prototype needed for SSLUnixNetVConnection
unsigned int net_next_connection_number();

struct PollCont:public Continuation
{
  NetHandler *net_handler;
  PollDescriptor *pollDescriptor;
  PollDescriptor *nextPollDescriptor;
  int poll_timeout;

  PollCont(ProxyMutex * m, int pt = net_config_poll_timeout);
  PollCont(ProxyMutex * m, NetHandler * nh, int pt = net_config_poll_timeout);
  ~PollCont();
  int pollEvent(int event, Event * e);
};


//
// NetHandler
//
// A NetHandler handles the Network IO operations.  It maintains
// lists of operations at multiples of it's periodicity.
//
class NetHandler:public Continuation
{
public:
  Event *trigger_event;
  Que(UnixNetVConnection, read.ready_link) read_ready_list;
  Que(UnixNetVConnection, write.ready_link) write_ready_list;
  Que(UnixNetVConnection, link) open_list;
  Que(DNSConnection, link) dnsqueue;
  ASLL(UnixNetVConnection, read.enable_link) read_enable_list;
  ASLL(UnixNetVConnection, write.enable_link) write_enable_list;

  time_t sec;
  int cycles;

  int startNetEvent(int event, Event * data);
  int mainNetEvent(int event, Event * data);
  int mainNetEventExt(int event, Event * data);
  void process_enabled_list(NetHandler *, EThread *);

  NetHandler();
};

static inline NetHandler *
get_NetHandler(EThread * t)
{
  return (NetHandler *) ETHREAD_GET_PTR(t, unix_netProcessor.netHandler_offset);
}
static inline PollCont *
get_PollCont(EThread * t)
{
  return (PollCont *) ETHREAD_GET_PTR(t, unix_netProcessor.pollCont_offset);
}
static inline PollDescriptor *
get_PollDescriptor(EThread * t)
{
  PollCont *p = get_PollCont(t);
  return p->pollDescriptor;
}


enum ThrottleType
{ ACCEPT, CONNECT };
inline int
net_connections_to_throttle(ThrottleType t)
{

  double headroom = t == ACCEPT ? NET_THROTTLE_ACCEPT_HEADROOM : NET_THROTTLE_CONNECT_HEADROOM;
  ink64 sval = 0, cval = 0;

#ifdef HTTP_NET_THROTTLE
  NET_READ_DYN_STAT(http_current_client_connections_stat, cval, sval);
  int http_user_agents = sval;
  // one origin server connection for each user agent
  int http_use_estimate = http_user_agents * 2;
  // be conservative, bound by number currently open
  if (http_use_estimate > currently_open)
    return (int) (http_use_estimate * headroom);
#endif
  NET_READ_DYN_STAT(net_connections_currently_open_stat, cval, sval);
  int currently_open = (int) sval;
  // deal with race if we got to multiple net threads
  if (currently_open < 0)
    currently_open = 0;
  return (int) (currently_open * headroom);
}

inline void
check_shedding_warning()
{
  ink_hrtime t = ink_get_hrtime();
  if (t - last_shedding_warning > NET_THROTTLE_MESSAGE_EVERY) {
    last_shedding_warning = t;
    IOCORE_SignalWarning(REC_SIGNAL_SYSTEM_ERROR, "number of connections reaching shedding limit");
  }
}

inline int
emergency_throttle(ink_hrtime now)
{
  return emergency_throttle_time > now;
}

inline int
check_net_throttle(ThrottleType t, ink_hrtime now)
{
  if(throttle_enabled == false) {
    // added by Vijay to disable throttle. This is done find out if
    // there any other problem other than the stats problem -- bug 3040824
    return false;
  }
  int connections = net_connections_to_throttle(t);
  if (connections >= net_connections_throttle)
    return true;

  if (emergency_throttle(now))
    return true;

  return false;
}

inline void
check_throttle_warning()
{
  ink_hrtime t = ink_get_hrtime();
  if (t - last_throttle_warning > NET_THROTTLE_MESSAGE_EVERY) {
    last_throttle_warning = t;
    IOCORE_SignalWarning(REC_SIGNAL_SYSTEM_ERROR, "too many connections, throttling");

  }
}

//
// Emergency throttle when we are close to exhausting file descriptors.
// Block all accepts or connects for N seconds where N
// is the amount into the emergency fd stack squared
// (e.g. on the last file descriptor we have 14 * 14 = 196 seconds
// of emergency throttle).
//
// Hyper Emergency throttle when we are very close to exhausting file
// descriptors.  Close the connection immediately, the upper levels
// will recover.
//
inline int
check_emergency_throttle(Connection & con)
{
  int fd = con.fd;
  int emergency = fds_limit - EMERGENCY_THROTTLE;
  if (fd > emergency) {
    int over = fd - emergency;
    emergency_throttle_time = ink_get_hrtime() + (over * over) * HRTIME_SECOND;
    IOCORE_SignalWarning(REC_SIGNAL_SYSTEM_ERROR, "too many open file descriptors, emergency throttling");
    int hyper_emergency = fds_limit - HYPER_EMERGENCY_THROTTLE;
    if (fd > hyper_emergency)
      con.close();
    return true;
  }
  return false;
}


inline int
change_net_connections_throttle(const char *token, RecDataT data_type, RecData value, void *data)
{
  (void) token;
  (void) data_type;
  (void) value;
  (void) data;
  int throttle = fds_limit - THROTTLE_FD_HEADROOM;
  if (fds_throttle < 0)
    net_connections_throttle = throttle;
  else {
    net_connections_throttle = fds_throttle;
    if (net_connections_throttle > throttle)
      net_connections_throttle = throttle;
  }
  return 0;
}


// 1  - transient
// 0  - report as warning
// -1 - fatal
inline int
accept_error_seriousness(int res)
{
  switch (res) {
  case -EAGAIN:
  case -ECONNABORTED:
  case -ECONNRESET:            // for Linux
  case -EPIPE:                 // also for Linux
    return 1;
  case -EMFILE:
  case -ENOMEM:
#if (HOST_OS != freebsd)
  case -ENOSR:
#endif
    ink_assert(!"throttling misconfigured: set too high");
#ifdef ENOBUFS
  case -ENOBUFS:
#endif
#ifdef ENFILE
  case -ENFILE:
#endif
    return 0;
  case -EINTR:
    ink_assert(!"should be handled at a lower level");
    return 0;
#if (HOST_OS != freebsd)
  case -EPROTO:
#endif
  case -EOPNOTSUPP:
  case -ENOTSOCK:
  case -ENODEV:
  case -EBADF:
  default:
    return -1;
  }
}

inline void
check_transient_accept_error(int res)
{
  ink_hrtime t = ink_get_hrtime();
  if (!last_transient_accept_error || t - last_transient_accept_error > TRANSIENT_ACCEPT_ERROR_MESSAGE_EVERY) {
    last_transient_accept_error = t;
    Warning("accept thread received transient error: errno = %d", -res);
#if (HOST_OS == linux)
    if (res == -ENOBUFS || res == -ENFILE)
      Warning("errno : %d consider a memory upgrade", -res);
#endif
  }
}

//
// Disable a UnixNetVConnection
//
static inline void
read_disable(NetHandler * nh, UnixNetVConnection * vc)
{
#ifdef INACTIVITY_TIMEOUT
  if (vc->inactivity_timeout) {
    if (!vc->write.enabled) {
      vc->inactivity_timeout->cancel_action();
      vc->inactivity_timeout = NULL;
    }
  }
#else
  if (vc->next_inactivity_timeout_at)
    if (!vc->write.enabled)
      vc->next_inactivity_timeout_at = 0;
#endif
  vc->read.enabled = 0;
  nh->read_ready_list.remove(vc);
  vc->ep.modify(-EVENTIO_READ);
}

static inline void
write_disable(NetHandler * nh, UnixNetVConnection * vc)
{
#ifdef INACTIVITY_TIMEOUT
  if (vc->inactivity_timeout) {
    if (!vc->read.enabled) {
      vc->inactivity_timeout->cancel_action();
      vc->inactivity_timeout = NULL;
    }
  }
#else
  if (vc->next_inactivity_timeout_at) 
    if (!vc->read.enabled)
      vc->next_inactivity_timeout_at = 0;
#endif
  vc->write.enabled = 0;
  nh->write_ready_list.remove(vc);
  vc->ep.modify(-EVENTIO_WRITE);
}

inline int EventIO::start(EventLoop l, DNSConnection *vc, int events) {
  type = EVENTIO_DNS_CONNECTION;
  return start(l, vc->fd, (Continuation*)vc, events);
}
inline int EventIO::start(EventLoop l, NetAccept *vc, int events) {
  type = EVENTIO_NETACCEPT;
  return start(l, vc->server.fd, (Continuation*)vc, events);
}
inline int EventIO::start(EventLoop l, UnixNetVConnection *vc, int events) {
  type = EVENTIO_READWRITE_VC;
  return start(l, vc->con.fd, (Continuation*)vc, events);
}
inline int EventIO::start(EventLoop l, UnixUDPConnection *vc, int events) {
  type = EVENTIO_UDP_CONNECTION;
  return start(l, vc->fd, (Continuation*)vc, events);
}
inline int EventIO::close() {
  stop();
  switch (type) {
    default: ink_assert(!"case");
    case EVENTIO_DNS_CONNECTION: return data.dnscon->close(); break;
    case EVENTIO_NETACCEPT: return data.na->server.close(); break;
    case EVENTIO_READWRITE_VC: return data.vc->con.close(); break;
  }
  return -1;
}

#ifdef USE_LIBEV

inline int EventIO::start(EventLoop l, int afd, Continuation *c, int e) {
  event_loop = l;
  data.c = c;
  ev_init(&eio, (eio_cb_t)this);
  ev_io_set(&eio, afd, e);
  ev_io_start(l->eio, &eio);
  return 0;
}

inline int EventIO::modify(int e) {
  ink_assert(event_loop);
  int ee = eio.events;
  if (e < 0)
    ee &= ~(-e);
  else
    ee |= e;
  if (ee != eio.events) {
    eio.events = ee;
    fd_change(event_loop->eio, eio.fd, 0);
  }
  return 0;
}

inline int EventIO::refresh(int e) {
  return 0;
}

inline int EventIO::stop() {
  if (event_loop) {
    ev_io_stop(event_loop->eio, &eio);
    event_loop = 0;
  }
  return 0;
}

#else /* !USE_LIBEV */

inline int EventIO::start(EventLoop l, int afd, Continuation *c, int e) {
  data.c = c;
  fd = afd;
  event_loop = l;
#if defined(USE_EPOLL)
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));
  ev.events = e;
  ev.data.ptr = this;
#ifndef USE_EDGE_TRIGGER
  events = e;
#endif
  return epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_ADD, fd, &ev);
#elif defined(USE_KQUEUE)
  events = e;
  struct kevent ev[2];
  int n = 0;
  if (e & EVENTIO_READ)
    EV_SET(&ev[n++], fd, EVFILT_READ, EV_ADD|INK_EV_EDGE_TRIGGER, 0, 0, this);
  if (e & EVENTIO_WRITE)
    EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_ADD|INK_EV_EDGE_TRIGGER, 0, 0, this);
  return kevent(l->kqueue_fd, &ev[0], n, NULL, 0, NULL);
#elif defined(USE_PORT)
  events = e; 
  int retval = port_associate(event_loop->port_fd, PORT_SOURCE_FD, fd, events, this); 
  NetDebug("iocore_eventio", "[EventIO::start] e(%d), events(%d), %d[%s]=port_associate(%d,%d,%d,%d,%p)", e, events, retval, retval<0? strerror(errno) : "ok", event_loop->port_fd, PORT_SOURCE_FD, fd, events, this);
  return retval;
#else
#error port me
#endif
}

inline int EventIO::modify(int e) {
#if defined(USE_EPOLL) && !defined(USE_EDGE_TRIGGER)
  struct epoll_event ev;
  memset(&ev, 0, sizeof(ev));
  int new_events = events, old_events = events;
  if (e < 0)
    new_events &= ~(-e);
  else
    new_events |= e;
  events = new_events;
  ev.events = new_events;
  ev.data.ptr = this;
  if (!new_events)
    return epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_DEL, fd, &ev);
  else if (!old_events)
    return epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_ADD, fd, &ev);
  else
    return epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_MOD, fd, &ev);
#elif defined(USE_KQUEUE) && !defined(USE_EDGE_TRIGGER)
  int n = 0;
  struct kevent ev[2];
  int ee = events;
  if (e < 0) {
    ee &= ~(-e);
    if ((-e) & EVENTIO_READ)
      EV_SET(&ev[n++], fd, EVFILT_READ, EV_DELETE, 0, 0, this);
    if ((-e) & EVENTIO_WRITE)
      EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, this);
  } else {
    ee |= e;
    if (e & EVENTIO_READ)
      EV_SET(&ev[n++], fd, EVFILT_READ, EV_ADD|INK_EV_EDGE_TRIGGER, 0, 0, this);
    if (e & EVENTIO_WRITE)
      EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_ADD|INK_EV_EDGE_TRIGGER, 0, 0, this);
  }
  events = ee;
  if (n)
    return kevent(event_loop->kqueue_fd, &ev[0], n, NULL, 0, NULL);
  else
    return 0;
#elif defined(USE_PORT)
  int n = 0;
  int ne = e;
  if (e < 0) {
    if (((-e) & events)) {
      ne = ~(-e) & events;
      if ((-e) & EVENTIO_READ)
	n++;
      if ((-e) & EVENTIO_WRITE)
	n++;
    } 
  } else {
    if (!(e & events)) {
      ne = events | e;
      if (e & EVENTIO_READ)
	n++;
      if (e & EVENTIO_WRITE)
	n++;
    } 
  }
  if (n && ne && event_loop) {
    events = ne;
    int retval = port_associate(event_loop->port_fd, PORT_SOURCE_FD, fd, events, this); 
    NetDebug("iocore_eventio", "[EventIO::modify] e(%d), ne(%d), events(%d), %d[%s]=port_associate(%d,%d,%d,%d,%p)", e, ne, events, retval, retval<0? strerror(errno) : "ok", event_loop->port_fd, PORT_SOURCE_FD, fd, events, this);
    return retval;
  }
  return 0;
#else
  return 0;
#endif
}

inline int EventIO::refresh(int e) {
#if defined(USE_KQUEUE) && defined(USE_EDGE_TRIGGER)
  e = e & events;
  struct kevent ev[2];
  int n = 0;
  if (e & EVENTIO_READ)
    EV_SET(&ev[n++], fd, EVFILT_READ, EV_ADD|INK_EV_EDGE_TRIGGER, 0, 0, this);
  if (e & EVENTIO_WRITE)
    EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_ADD|INK_EV_EDGE_TRIGGER, 0, 0, this);
  if (n)
    return kevent(event_loop->kqueue_fd, &ev[0], n, NULL, 0, NULL);
  else
    return 0;
#elif defined(USE_PORT)
  int n = 0;
  int ne = e;
  if ((e & events)) {
    ne = events | e;
    if (e & EVENTIO_READ)
      n++;
    if (e & EVENTIO_WRITE)
      n++;
    if (n && ne && event_loop) {
      events = ne;
      int retval = port_associate(event_loop->port_fd, PORT_SOURCE_FD, fd, events, this); 
      NetDebug("iocore_eventio", "[EventIO::refresh] e(%d), ne(%d), events(%d), %d[%s]=port_associate(%d,%d,%d,%d,%p)", e, ne, events, retval, retval<0? strerror(errno) : "ok", event_loop->port_fd, PORT_SOURCE_FD, fd, events, this);
      return retval;
    } 
  } 
  return 0;
#else
  return 0;
#endif
}


inline int EventIO::stop() {
  if (event_loop) {
#if defined(USE_EPOLL)
    struct epoll_event ev;
    memset(&ev, 0, sizeof(struct epoll_event));
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    return epoll_ctl(event_loop->epoll_fd, EPOLL_CTL_DEL, fd, &ev);
#elif defined(USE_KQUEUE)
#if 0
    // this is not necessary and may result in a race if
    // a file descriptor is reused between polls
    int n = 0;
    struct kevent ev[2];
    if (events & EVENTIO_READ)
      EV_SET(&ev[n++], fd, EVFILT_READ, EV_DELETE, 0, 0, this);
    if (events & EVENTIO_WRITE)
      EV_SET(&ev[n++], fd, EVFILT_WRITE, EV_DELETE, 0, 0, this);
    return kevent(event_loop->kqueue_fd, &ev[0], n, NULL, 0, NULL);
#endif
#elif defined(USE_PORT)
    int retval = port_dissociate(event_loop->port_fd, PORT_SOURCE_FD, fd);
    NetDebug("iocore_eventio", "[EventIO::stop] %d[%s]=port_dissociate(%d,%d,%d)", retval, retval<0? strerror(errno) : "ok", event_loop->port_fd, PORT_SOURCE_FD, fd);
    return retval;
#else
#error port me
#endif
    event_loop = 0;
  }
  return 0;
}

#endif /* !USE_LIBEV */

#endif
