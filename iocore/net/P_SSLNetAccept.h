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

  P_SSLNetAccept.h

  
   NetAccept is a generalized facility which allows
   Connections of different classes to be accepted either
   from a blockable thread or by adaptive polling.
  
   It is used by the NetProcessor and the ClusterProcessor
   and should be considered PRIVATE to processor implementations.


  
 ****************************************************************************/
#if !defined (_SSLNetAccept_h_)
#define _SSLNetAccept_h_

#include "inktomi++.h"
#include "P_Connection.h"
#include "P_NetAccept.h"

struct Event;
struct UnixNetVConnection;

//
// NetAccept
// Handles accepting connections.
//
struct SSLNetAccept:NetAccept
{

#ifndef _IOCORE_WIN32_WINNT
  virtual UnixNetVConnection *allocateThread(EThread * t);
  virtual void freeThread(UnixNetVConnection * vc, EThread * t);
  virtual EventType getEtype();
#endif
  virtual void init_accept_per_thread();

    SSLNetAccept()
  {
  };

  virtual ~ SSLNetAccept() {
  };

};
#endif
