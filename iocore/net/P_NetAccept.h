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

  NetAccept.h

  
   NetAccept is a generalized facility which allows
   Connections of different classes to be accepted either
   from a blockable thread or by adaptive polling.
  
   It is used by the NetProcessor and the ClusterProcessor
   and should be considered PRIVATE to processor implementations.


  
 ****************************************************************************/
#ifndef __P_NETACCEPT_H__
#define __P_NETACCEPT_H__

#include "inktomi++.h"
#include "P_Connection.h"


struct NetAccept;
struct Event;
//
// Default accept function
//   Accepts as many connections as possible, returning the number accepted
//   or -1 to stop accepting.
//
typedef int (AcceptFunction) (NetAccept * na, void *e, bool blockable);
typedef AcceptFunction *AcceptFunctionPtr;
AcceptFunction net_accept;

class UnixNetVConnection;

// TODO fix race between cancel accept and call back
struct NetAcceptAction:public Action, RefCountObj
{
  Server *server;

  void cancel(Continuation * cont = NULL) {
    Action::cancel(cont);
    server->close();
  }

  Continuation *operator =(Continuation * acont)
  {
    return Action::operator=(acont);
  }

  ~NetAcceptAction() {
    Debug("net_accept", "NetAcceptAction dying\n");
  }
};


//
// NetAccept
// Handles accepting connections.
//
struct NetAccept:Continuation
{
  int port;
  ink_hrtime period;
  Server server;
  void *alloc_cache;
  AcceptFunctionPtr accept_fn;
  int ifd;
  int ifd_seq_num;
  bool callback_on_open;
  Ptr<NetAcceptAction> action_;
  int recv_bufsize;
  int send_bufsize;
  unsigned long sockopt_flags;
  EventType etype;
  UnixNetVConnection *epoll_vc; // only storage for epoll events
  EventIO ep;

  // Functions all THREAD_FREE and THREAD_ALLOC to be performed
  // for both SSL and regular NetVConnection transparent to
  // accept functions.
  virtual UnixNetVConnection *allocateThread(EThread * t);
  virtual void freeThread(UnixNetVConnection * vc, EThread * t);
  virtual EventType getEtype();

  void init_accept_loop();
  virtual void init_accept(EThread * t = NULL);
  virtual void init_accept_per_thread();
  // 0 == success
  int do_listen(bool non_blocking);

  int do_blocking_accept(NetAccept * master_na, EThread * t);
  virtual int acceptEvent(int event, void *e);
  virtual int acceptFastEvent(int event, void *e);
  int acceptLoopEvent(int event, Event * e);
  void cancel();

  NetAccept();
  virtual ~ NetAccept()
  {
    action_ = NULL;
  };
};


#endif
