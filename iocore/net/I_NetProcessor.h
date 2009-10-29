/** @file

  This file implements an I/O Processor for network I/O

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

#ifndef __I_NETPROCESSOR_H__
#define __I_NETPROCESSOR_H__

#include "I_EventSystem.h"
#include "I_Socks.h"
struct socks_conf_struct;
#define NET_CONNECT_TIMEOUT (30*1000)

class NetVCOptions;

/**
  This is the heart of the Net system. Provides common network APIs,
  like accept, connect etc. It performs network I/O on behalf of a
  state machine.

*/
class NetProcessor:public Processor
{
public:

  /**
    Accept connections on a port.

    Callbacks:
      - cont->handleEvent( NET_EVENT_ACCEPT, NetVConnection *) is
        called for each new connection
      - cont->handleEvent(EVENT_ERROR,-errno) on a bad error

    Re-entrant callbacks (based on callback_on_open flag):
      - cont->handleEvent(NET_EVENT_ACCEPT_SUCCEED, 0) on successful
        accept init
      - cont->handleEvent(NET_EVENT_ACCEPT_FAILED, 0) on accept
        init failure

    @param cont Continuation to be called back with events this
      continuation is not locked on callbacks and so the handler must
      be re-entrant.
    @param port port to bind for accept.
    @param frequent_accept if true, accept is done on all event
      threads and throttle limit is imposed if false, accept is done
      just on one thread and no throttling is done.
    @param accept_ip DEPRECATED.
    @param callback_on_open if true, cont is called back with
      NET_EVENT_ACCEPT_SUCCEED, or NET_EVENT_ACCEPT_FAILED on success
      and failure resp.
    @param listen_socket_in if passed, used for listening.
    @param accept_pool_size NT specific, better left unspecified.
    @param accept_only can be used to customize accept, accept a
      connection only if there is some data to be read. This works
      only on supported platforms (NT & Win2K currently).
    @param bound_sockaddr returns the sockaddr for the listen fd.
    @param bound_sockaddr_size size of the sockaddr returned.
    @param recv_bufsize used to set recv buffer size for accepted
      connections (Works only on selected platforms ??).
    @param send_bufsize used to set send buffer size for accepted
      connections (Works only on selected platforms ??).
    @param sockopt_flag can be used to define additional socket option.
    @param etype Event Thread group to accept on.
    @return Action, that can be cancelled to cancel the accept. The
      port becomes free immediately.

  */
  inkcoreapi virtual Action * accept(Continuation * cont, int port, bool frequent_accept = false,       // UNIX only ??
                                     // not used
                                     unsigned int accept_ip = INADDR_ANY, bool callback_on_open = false,        // TBD for NT
                                     SOCKET listen_socket_in = NO_FD,   // NT only
                                     int accept_pool_size = ACCEPTEX_POOL_SIZE, // NT only
                                     bool accept_only = false,
                                     sockaddr * bound_sockaddr = 0,
                                     int *bound_sockaddr_size = 0,
                                     int recv_bufsize = 0,
                                     int send_bufsize = 0, unsigned long sockopt_flag = 0, EventType etype = ET_NET);

  /**
    Accepts incoming connections on port. Accept connections on port.
    Accept is done on all net threads and throttle limit is imposed
    if frequent_accept flag is true. This is similar to the accept
    method described above. The only difference is that the list
    of parameter that is takes is limited.

    Callbacks:
      - cont->handleEvent( NET_EVENT_ACCEPT, NetVConnection *) is called for each new connection
      - cont->handleEvent(EVENT_ERROR,-errno) on a bad error

    Re-entrant callbacks (based on callback_on_open flag):
      - cont->handleEvent(NET_EVENT_ACCEPT_SUCCEED, 0) on successful accept init
      - cont->handleEvent(NET_EVENT_ACCEPT_FAILED, 0) on accept init failure

    @param cont Continuation to be called back with events this
      continuation is not locked on callbacks and so the handler must
      be re-entrant.
    @param listen_socket_in if passed, used for listening.
    @param port port to bind for accept.
    @param bound_sockaddr returns the sockaddr for the listen fd.
    @param bound_sockaddr_size size of the sockaddr returned.
    @param accept_only can be used to customize accept, accept a
      connection only if there is some data to be read. This works
      only on supported platforms (NT & Win2K currently).
    @param recv_bufsize used to set recv buffer size for accepted
      connections (Works only on selected platforms ??).
    @param send_bufsize used to set send buffer size for accepted
      connections (Works only on selected platforms ??).
    @param sockopt_flag can be used to define additional socket option.
    @param etype Event Thread group to accept on.
    @param callback_on_open if true, cont is called back with
      NET_EVENT_ACCEPT_SUCCEED, or NET_EVENT_ACCEPT_FAILED on success
      and failure resp.
    @return Action, that can be cancelled to cancel the accept. The
      port becomes free immediately.

  */
  virtual Action *main_accept(Continuation * cont, SOCKET listen_socket_in, int port, sockaddr * bound_sockaddr = NULL, int *bound_sockaddr_size = NULL, bool accept_only = false, int recv_bufsize = 0, int send_bufsize = 0, unsigned long sockopt_flag = 0, EventType etype = ET_NET, bool callback_on_open = false  // TBD for NT
    );

  /**
    Open a NetVConnection for connection oriented I/O. Connects
    through sockserver if netprocessor is configured to use socks
    or is socks parameters to the call are set.

    Re-entrant callbacks:
      - On success calls: c->handleEvent(NET_EVENT_OPEN, NetVConnection *)
      - On failure calls: c->handleEvent(NET_EVENT_OPEN_FAILED, -errno)

    @note Connection may not have been established when cont is
      call back with success. If this behaviour is desired use
      synchronous connect connet_s method.

    @see connect_s()

    @param cont Continuation to be called back with events.
    @param ip machine to connect to.
    @param port port to connect to.
    @param _interface network interface to use for connect.
    @param options @see NetVCOptions.

  */

  inkcoreapi Action *connect_re(Continuation * cont,
                                unsigned int ip, int port, unsigned int _interface = 0, NetVCOptions * options = NULL);

  /**
    Open a NetVConnection for connection oriented I/O. This call
    is simliar to connect method except that the cont is called
    back only after the connections has been established. In the
    case of connect the cont could be called back with NET_EVENT_OPEN
    event and OS could still be in the process of establishing the
    connection. Re-entrant Callbacks: same as connect. If unix
    asynchronous type connect is desired use connect_re().

    @param cont Continuation to be called back with events.
    @param ip machine to connect to.
    @param port port to connect to.
    @param _interface local interaface to bind to for the new connection.
    @param timeout for connect, the cont will get NET_EVENT_OPEN_FAILED
      if connection could not be established for timeout msecs. The
      default is 30 secs.
    @param options @see NetVCOptions.

    @see connect_re()

  */
  Action *connect_s(Continuation * cont,
                    unsigned int ip,
                    int port,
                    unsigned int _interface = 0, int timeout = NET_CONNECT_TIMEOUT, NetVCOptions * opts = NULL);

  /**
    Starts the Netprocessor. This has to be called before doing any
    other net call.

    @param number_of_net_threads is not used. The net processor
      uses the Event Processor threads for its activity.

  */
  virtual int start(int number_of_net_threads = 0 /* uses event threads */ ) = 0;

  /** Private constructor. */
    NetProcessor()
  {
  };

  /** Private destructor. */
  virtual ~ NetProcessor() {
  };

  /** This is MSS for connections we accept (client connections). */
  static int accept_mss;

  //
  // The following are required by the SOCKS protocol:
  //
  // Either the configuration variables will give your a regular
  // expression for all the names that are to go through the SOCKS
  // server, or will give you a list of domain names which should *not* go
  // through SOCKS. If the SOCKS option is set to false then, these
  // variables (regular expression or list) should be set
  // appropriately. If it is set to TRUE then, in addition to supplying
  // the regular expression or the list, the user should also give the
  // the ip address and port number for the SOCKS server (use
  // appropriate defaults)

  /* shared by regular netprocessor and ssl netprocessor */
  static socks_conf_struct *socks_conf_stuff;

private:

  /** @note Not implemented. */
  virtual int stop()
  {
    ink_release_assert(!"NetProcessor::stop not implemented");
    return 1;
  }

  NetProcessor(const NetProcessor &);
  NetProcessor & operator =(const NetProcessor &);
};


/**
  Global NetProcessor singleton object for making net calls. All
  net processor calls like connect, accept, etc are made using this
  object.

  @code
    netProcesors.accept(my_cont, ...);
    netProcessor.connect_re(my_cont, ...);
  @endcode

*/
extern inkcoreapi NetProcessor & netProcessor;

#ifdef HAVE_LIBSSL
/** 
  Global netProcessor singleton object for making ssl enabled net
  calls. As far as the SM is concerned this behaves excatly like
  netProcessor. The only difference is that the connections are
  over ssl.

*/
extern inkcoreapi NetProcessor & sslNetProcessor;
#endif

#endif
