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
  P_UDPNet.h
  Private header for UDPNetProcessor
  
  
 ****************************************************************************/

#ifndef __P_UDPNET_H_
#define __P_UDPNET_H_

extern EventType ET_UDP;

#include "I_UDPNet.h"
#include "P_UDPPacket.h"

//added by YTS Team, yamsat
static inline PollCont *get_UDPPollCont(EThread *);

#include "P_UnixUDPConnection.h"
#include "P_UDPIOEvent.h"

class UDPNetHandler;

struct UDPNetProcessorInternal : public UDPNetProcessor
{
  virtual int start(int n_udp_threads);
#if defined (_IOCORE_WIN32)
  SOCKET create_dgram_socket_internal();
#else
  void udp_read_from_net(UDPNetHandler * nh, UDPConnection * uc, PollDescriptor * pd, EThread * thread);
  int udp_callback(UDPNetHandler * nh, UDPConnection * uc, EThread * thread);
#endif

#if defined (_IOCORE_WIN32)
  EThread *ethread;
  UDPNetHandler *udpNetHandler;
#else
  ink_off_t pollCont_offset;
  ink_off_t udpNetHandler_offset;
#endif

public:
  virtual void UDPNetProcessor_is_abstract() {  }
};

extern UDPNetProcessorInternal udpNetInternal;

class PacketQueue;

class UDPQueue
{
public:

  void service(UDPNetHandler *);
  // these are internal APIs
  // BulkIOSend uses the BulkIO kernel module for bulk data transfer
  void BulkIOSend();
  // In the absence of bulk-io, we are down sending packet after packet
  void SendPackets();
  void SendUDPPacket(UDPPacketInternal * p, ink32 pktLen);

  // Interface exported to the outside world
  void send(UDPPacket * p);

  Que(UDPPacketInternal, link) reliabilityPktQueue;
  InkAtomicList atomicQueue;
  ink_hrtime last_report;
  ink_hrtime last_service;
  ink_hrtime last_byteperiod;
  int bytesSent;
  int packets;
  int added;

  UDPQueue();
  ~UDPQueue();
};

#ifdef PACKETQUEUE_IMPL_AS_RING

// 20 ms slots; 2048 slots  => 40 sec. into the future
#define SLOT_TIME_MSEC 20
#define SLOT_TIME HRTIME_MSECONDS(SLOT_TIME_MSEC)
#define N_SLOTS 2048

extern inku64 g_udp_bytesPending;

class PacketQueue
{
public:
  PacketQueue()
  :nPackets(0)
  , now_slot(0)
  {
    lastPullLongTermQ = 0;
    init();
  }

  virtual ~ PacketQueue()
  {
  }
  int nPackets;

  ink_hrtime lastPullLongTermQ;
  Que(UDPPacketInternal, link) longTermQ;
  Que(UDPPacketInternal, link) bucket[N_SLOTS];
  ink_hrtime delivery_time[N_SLOTS];
  int now_slot;

  void init(void)
  {
    now_slot = 0;
    ink_hrtime now = ink_get_hrtime_internal();
    int i = now_slot;
    int j = 0;
    while (j < N_SLOTS) {
      delivery_time[i] = now + j * SLOT_TIME;
      i = (i + 1) % N_SLOTS;
      j++;
    }
  }

  void addPacket(UDPPacketInternal * e, ink_hrtime now = 0) {
    int before = 0;
    int slot;

    if (IsCancelledPacket(e)) {
      g_udp_bytesPending -= e->getPktLength();
      e->free();
      return;
    }

    nPackets++;

    ink_assert(delivery_time[now_slot]);

    if (e->delivery_time < now)
      e->delivery_time = now;

    ink_hrtime s = e->delivery_time - delivery_time[now_slot];

    if (s < 0) {
      before = 1;
      s = 0;
    }
    s = s / SLOT_TIME;
    // if s >= N_SLOTS, either we are *REALLY* behind or someone is trying
    // queue packets *WAY* too far into the future.
    // need a thingy to hold packets in a "long-term" slot; then, pull packets
    // from long-term slot whenever you advance.
    if (s >= N_SLOTS - 1) {
      longTermQ.enqueue(e);
      e->in_heap = 0;
      e->in_the_priority_queue = 1;
      return;
    }
    slot = (s + now_slot) % N_SLOTS;

    // so that slot+1 is still "in future".
    ink_assert((before || delivery_time[slot] <= e->delivery_time) &&
               (delivery_time[(slot + 1) % N_SLOTS] >= e->delivery_time));
    e->in_the_priority_queue = 1;
    e->in_heap = slot;
    bucket[slot].enqueue(e);
  };
  UDPPacketInternal *firstPacket(ink_hrtime t)
  {
    if (t > delivery_time[now_slot]) {
      return bucket[now_slot].head;
    } else {
      return NULL;
    }
  };
  UDPPacketInternal *getFirstPacket()
  {
    nPackets--;
    return dequeue_ready(0);
  };
  int size()
  {
    ink_assert(nPackets >= 0);
    return nPackets;
  };
  void invariant();
  bool IsCancelledPacket(UDPPacketInternal * p)
  {
    // discard packets that'll never get sent...
    return ((p->conn->shouldDestroy()) || (p->conn->GetSendGenerationNumber() != p->reqGenerationNum));
  };

  void FreeCancelledPackets(int numSlots)
  {
    UDPPacketInternal *p;
    Que(UDPPacketInternal, link) tempQ;
    int i, s;

    for (i = 0; i < numSlots; i++) {
      s = (now_slot + i) % N_SLOTS;
      while (NULL != (p = bucket[s].dequeue())) {
        if (IsCancelledPacket(p)) {
          g_udp_bytesPending -= p->getPktLength();
          p->free();
          continue;
        }
        tempQ.enqueue(p);
      }
      // remove and flip it over
      while (NULL != (p = tempQ.dequeue())) {
        bucket[s].enqueue(p);
      }
    }
  };

  void advanceNow(ink_hrtime t)
  {
    int s = now_slot;
    int prev;

    if (ink_hrtime_to_msec(t - lastPullLongTermQ) >= SLOT_TIME_MSEC * ((N_SLOTS - 1) / 2)) {
      Que(UDPPacketInternal, link) tempQ;
      UDPPacketInternal *p;
      // pull in all the stuff from long-term slot
      lastPullLongTermQ = t;
      // this is to handle wierdoness where someone is trying to queue a
      // packet to be sent in SLOT_TIME_MSEC * N_SLOTS * (2+)---the packet
      // will get back to longTermQ and we'll have an infinite loop.
      while ((p = longTermQ.dequeue()) != NULL)
        tempQ.enqueue(p);
      while ((p = tempQ.dequeue()) != NULL)
        addPacket(p);
    }

    while (!bucket[s].head && (t > delivery_time[s] + SLOT_TIME)) {
      prev = (s + N_SLOTS - 1) % N_SLOTS;
      delivery_time[s] = delivery_time[prev] + SLOT_TIME;
      s = (s + 1) % N_SLOTS;
      prev = (s + N_SLOTS - 1) % N_SLOTS;
      ink_assert(delivery_time[prev] > delivery_time[s]);

      if (s == now_slot) {
        init();
        s = 0;
        break;
      }
    }

    if (s != now_slot)
      Debug("udpnet-service", "Advancing by (%d slots): behind by %lld ms",
            s - now_slot, ink_hrtime_to_msec(t - delivery_time[now_slot]));
    now_slot = s;
  };
private:
  void remove(UDPPacketInternal * e)
  {
    nPackets--;
    ink_assert(e->in_the_priority_queue);
    e->in_the_priority_queue = 0;
    bucket[e->in_heap].remove(e);
  }

public:
  UDPPacketInternal * dequeue_ready(ink_hrtime t) {
    (void) t;
    UDPPacketInternal *e = bucket[now_slot].dequeue();
    if (e) {
      ink_assert(e->in_the_priority_queue);
      e->in_the_priority_queue = 0;
    }
    advanceNow(t);
    return e;
  }

  void check_ready(ink_hrtime now)
  {
    (void) now;
  }

  ink_hrtime earliest_timeout()
  {
    int s = now_slot;
    for (int i = 0; i < N_SLOTS; i++) {
      if (bucket[s].head) {
        return delivery_time[s];
      }
      s = (s + 1) % N_SLOTS;
    }
    return HRTIME_FOREVER;
  }

private:
  void kill_cancelled_events()
  {
  }
};
#endif

#if !defined (_IOCORE_WIN32)

void initialize_thread_for_udp_net(EThread * thread);

struct UDPNetHandler:Continuation
{
public:
  // to be polled for read
  Que(UnixUDPConnection, polling_link) udp_polling;
  // to be called back with data
  Que(UnixUDPConnection, callback_link) udp_callbacks;
  // outgoing packets
  InkAtomicList udpAtomicQueue;
  UDPQueue udpOutQueue;
  // to hold the newly created descriptors before scheduling them on
  // the servicing buckets.
  // atomically added to by a thread creating a new connection with
  // UDPBind
  InkAtomicList udpNewConnections;
  Event *trigger_event;
  ink_hrtime nextCheck;
  ink_hrtime lastCheck;

  int startNetEvent(int event, Event * data);
  int mainNetEvent(int event, Event * data);
  PollDescriptor *build_poll(PollDescriptor *);
  PollDescriptor *build_one_udpread_poll(int fd, UnixUDPConnection *, PollDescriptor * pd);

  UDPNetHandler();
};
#endif

#if defined(_IOCORE_WIN32)
#include "NTUDPConnection.h"
void initialize_thread_for_udp_net(EThread * thread);

class UDPQueue;

class UDPNetHandler:Continuation
{
public:
  // to be polled for read
  Que(UnixUDPConnection, polling_link) udp_polling;
  // to be called back with data
  Que(UnixUDPConnection, callback_link) udp_callbacks;
  // outgoing packets
  InkAtomicList udpAtomicQueue;
  UDPQueue udpOutQueue;
  // to hold the newly created descriptors before scheduling them on
  // the servicing buckets.
  // atomically added to by a thread creating a new connection with
  // UDPBind
  InkAtomicList udpNewConnections;
  Event *trigger_event;
  ink_hrtime nextCheck;
  ink_hrtime lastCheck;

  int startNetEvent(int event, Event * data);
  int mainNetEvent(int event, Event * data);

  UDPNetHandler();
  virtual ~ UDPNetHandler();
};
#endif

struct PollCont;
static inline PollCont *
get_UDPPollCont(EThread * t)
{
  return (PollCont *) ETHREAD_GET_PTR(t, udpNetInternal.pollCont_offset);
}

static inline UDPNetHandler *
get_UDPNetHandler(EThread * t)
{
  return (UDPNetHandler *)
    ETHREAD_GET_PTR(t, udpNetInternal.udpNetHandler_offset);
}

// All of this stuff is for UDP egress b/w management
struct InkSinglePipeInfo
{
  InkSinglePipeInfo()
  {
    wt = 0.0;
    bwLimit = 0;
    destIP = 0;
    count = 0;
    bytesSent = pktsSent = 0;
    bwAlloc = 0;
    bwUsed = 0.0;
    queue = NEW(new PacketQueue());
  };

  ~InkSinglePipeInfo() {
    delete queue;
  }

  double wt;
  // all are in bps (bits per sec.) so that we can do ink_atomic_increment
  ink64 bwLimit;
  ink64 bwAlloc;
  // this is in Mbps
  double bwUsed;
  ink32 destIP;
  inku32 count;
  inku64 bytesSent;
  inku64 pktsSent;
  PacketQueue *queue;
};

struct InkPipeInfo
{
  int numPipes;
  double interfaceMbps;
  double reliabilityMbps;
  InkSinglePipeInfo *perPipeInfo;
};

extern InkPipeInfo G_inkPipeInfo;

class UDPWorkContinuation:public Continuation
{
public:
  UDPWorkContinuation():cont(NULL), numPairs(0), myIP(0), destIP(0),
    sendbufsize(0), recvbufsize(0), udpConns(NULL), resultCode(NET_EVENT_DATAGRAM_OPEN)
  {
  };
  ~UDPWorkContinuation() {
  };
  void init(Continuation * c, int numPairs, unsigned int my_ip, unsigned int dest_ip, int s_bufsize, int r_bufsize);
  int StateCreatePortPairs(int event, void *data);
  int StateDoCallback(int event, void *data);

  Action action;

private:
  Continuation * cont;
  int numPairs;
  unsigned int myIP, destIP;
  int sendbufsize, recvbufsize;
  UnixUDPConnection **udpConns;
  int resultCode;
};

typedef int (UDPWorkContinuation::*UDPWorkContinuation_Handler) (int, void *);

inkcoreapi extern ClassAllocator<UDPWorkContinuation> udpWorkContinuationAllocator;

#endif //__P_UDPNET_H_
