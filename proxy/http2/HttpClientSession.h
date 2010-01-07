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

   HttpClientSession.h

   Description:

   
 ****************************************************************************/

#ifndef _HTTP_CLIENT_SESSION_H_
#define _HTTP_CLIENT_SESSION_H_

#include "inktomi++.h"
#include "P_Net.h"
#include "InkAPIInternal.h"
#include "HTTP.h"

extern ink_mutex debug_cs_list_mutex;

class HttpSM;
class HttpServerSession;

class SecurityContext;

class HttpClientSession:public VConnection
{
public:
  HttpClientSession();
  void cleanup();
  virtual void destroy();

public:
  static HttpClientSession *allocate();

public:
  void new_connection(NetVConnection * new_vc, bool backdoor);

  virtual VIO *do_io_read(Continuation * c, ink64 nbytes = INK64_MAX, MIOBuffer * buf = 0);

  virtual VIO *do_io_write(Continuation * c = NULL, ink64 nbytes = INK64_MAX, IOBufferReader * buf = 0, bool owner = false);

  virtual void do_io_close(int lerrno = -1);
  virtual void do_io_shutdown(ShutdownHowTo_t howto);
  virtual void reenable(VIO * vio);

  void set_half_close_flag()
  {
    half_close = true;
  };
  virtual void release(IOBufferReader * r);
  NetVConnection *get_netvc()
  {
    return client_vc;
  };

  virtual void attach_server_session(HttpServerSession * ssession, bool transaction_done = true);
  HttpServerSession *get_server_session()
  {
    return bound_ss;
  };

  // Used to retreive a request from NCA
  virtual HTTPHdr *get_request();

  // Used for the cache authenticated HTTP content feature
  HttpServerSession *get_bound_ss();

  // Functions for manipulating api hooks
  void ssn_hook_append(INKHttpHookID id, INKContInternal * cont);
  void ssn_hook_prepend(INKHttpHookID id, INKContInternal * cont);
  APIHook *ssn_hook_get(INKHttpHookID id);

  // Used to verify we are recording the current
  //   client transaction stat properly
  int client_trans_stat;
#ifndef VxWorks
  ink64 con_id;
#else
  ink32 con_id;
#endif

private:
  HttpClientSession(HttpClientSession &);

  int state_keep_alive(int event, void *data);
  int state_slave_keep_alive(int event, void *data);
  int state_wait_for_close(int event, void *data);

  void handle_api_return(int event);
  int state_api_callout(int event, void *data);
  void do_api_callout(INKHttpHookID id);

  virtual void new_transaction();

  enum C_Read_State
  {
    HCS_INIT,
    HCS_ACTIVE_READER,
    HCS_KEEP_ALIVE,
    HCS_HALF_CLOSED,
    HCS_CLOSED
  };

  NetVConnection *client_vc;
  int magic;
  int transact_count;
  bool half_close;
  bool conn_decrease;

  HttpServerSession *bound_ss;

  MIOBuffer *read_buffer;
  IOBufferReader *sm_reader;
  HttpSM *current_reader;
  C_Read_State read_state;

  VIO *ka_vio;
  VIO *slave_ka_vio;

  Link<HttpClientSession> debug_link;

  INKHttpHookID cur_hook_id;
  APIHook *cur_hook;
  int cur_hooks;

  // api_hooks must not be changed directly  
  //  Use ssn_hook_{ap,pre}pend so hooks_set is
  //  updated
  HttpAPIHooks api_hooks;

public:
  bool backdoor_connect;
  int hooks_set;

  bool session_based_auth;

  bool m_bAuthComplete;
  SecurityContext *secCtx;

  // for DI. An active connection is one that a request has 
  // been successfully parsed (PARSE_DONE) and it remains to
  // be active until the transaction goes through or the client
  // aborts.
  bool m_active;
};

inline APIHook *
HttpClientSession::ssn_hook_get(INKHttpHookID id)
{
  return api_hooks.get(id);
}

extern ClassAllocator<HttpClientSession> httpClientSessionAllocator;

#endif
