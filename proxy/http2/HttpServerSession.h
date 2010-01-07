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

   HttpServerSession.h

   Description:

   
 ****************************************************************************/

#ifndef _HTTP_SERVER_SESSION_H_
#define _HTTP_SERVER_SESSION_H_
/* Enable LAZY_BUF_ALLOC to delay allocation of buffers until they
* are actually required.
* Enabling LAZY_BUF_ALLOC, stop Http code from allocation space
* for header buffer and tunnel buffer. The allocation is done by
* the net code in read_from_net when data is actually written into
* the buffer. By allocating memory only when it is required we can
* reduce the memory consumed by TS process.
* 
* IMPORTANT NOTE: enable/disable LAZY_BUF_ALLOC in HttpSM.h as well.
*/
#define LAZY_BUF_ALLOC

#include "P_Net.h"

#include "HttpConnectionCount.h"

class HttpSM;
class MIOBuffer;
class IOBufferReader;

enum HSS_State
{
  HSS_INIT,
  HSS_ACTIVE,
  HSS_KA_CLIENT_SLAVE,
  HSS_KA_SHARED
};

class HttpServerSession:public VConnection
{
public:
  HttpServerSession();
  void destroy();

public:
  static HttpServerSession *allocate();

public:
  void new_connection(NetVConnection * new_vc);

  void reset_read_buffer(void)
  {
    ink_debug_assert(read_buffer->_writer);
    ink_debug_assert(buf_reader != NULL);
    read_buffer->dealloc_all_readers();
    read_buffer->_writer = NULL;
    buf_reader = read_buffer->alloc_reader();
  }

  IOBufferReader *get_reader()
  {
    return buf_reader;
  };

  virtual VIO *do_io_read(Continuation * c, ink64 nbytes = INK64_MAX, MIOBuffer * buf = 0);

  virtual VIO *do_io_write(Continuation * c = NULL, ink64 nbytes = INK64_MAX, IOBufferReader * buf = 0, bool owner = false);

  virtual void do_io_close(int lerrno = -1);
  virtual void do_io_shutdown(ShutdownHowTo_t howto);

  virtual void reenable(VIO * vio);

  void release();
  void attach_hostname(const char *hostname);
  NetVConnection *get_netvc()
  {
    return server_vc;
  };

  // Keys for matching hostnames
  unsigned int server_ip;
  int server_port;
  INK_MD5 hostname_hash;
  bool host_hash_computed;

  ink64 con_id;
  int transact_count;
  HSS_State state;

  // Used to determine whether the session is for parent proxy
  // it is session to orgin server
  // We need to determine whether a closed connection was to
  // close parent proxy to update the 
  // proxy.process.http.current_parent_proxy_connections
  bool to_parent_proxy;

  // Used to verify we are recording the server
  //   transaction stat properly
  int server_trans_stat;

  // Sessions become if authenication headers
  //  are sent over them
  bool private_session;
  //bool www_auth_content;

  Link<HttpServerSession> lru_link;
  Link<HttpServerSession> hash_link;

  // Keep track of connection limiting and a pointer to the
  // singleton that keeps track of the connection counts.
  bool enable_origin_connection_limiting;
  ConnectionCount *connection_count;

  // The ServerSession owns the following buffer which use
  //   for parsing the headers.  The server session needs to
  //   own the buffer so we can go from a keep-alive state
  //   to being acquired and parsing the header without
  //   changing the buffer we are doing I/O on.  We can
  //   not change the buffer for I/O without issuing a
  //   an asyncronous cancel on NT
  MIOBuffer *read_buffer;

private:
  HttpServerSession(HttpServerSession &);

  NetVConnection *server_vc;
  int magic;

  IOBufferReader *buf_reader;
};

extern ClassAllocator<HttpServerSession> httpServerSessionAllocator;

inline void
HttpServerSession::attach_hostname(const char *hostname)
{
  if (!host_hash_computed) {
    ink_code_MMH((unsigned char *) hostname, strlen(hostname), (unsigned char *) &hostname_hash);
    host_hash_computed = true;
  }
}
#endif
