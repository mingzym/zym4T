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


#include "ink_config.h"
#include <stdio.h>
#include <string.h>

#include "P_EventSystem.h"
// #include "I_Disk.h" unused
// #include "I_NNTP.h" unavailable
// #include "I_FTP.h" unavailable
#include "I_Cache.h"
#include "I_Net.h"
#include "P_Cluster.h"
#include "I_HostDB.h"
#include "BaseManager.h"
#include "P_MultiCache.h"

/*------------------------------------------------------------------------- 
  event_int_to_string

  This routine will translate an integer event number to a string
  identifier based on a brute-force search of a switch tag.  If the event
  cannot be located in the switch table, the routine will construct and
  return a string of the integer identifier.
  -------------------------------------------------------------------------*/

char *
event_int_to_string(int event, char buffer[32])
{
  switch (event) {
    case -1: return "<no event>";
    case VC_EVENT_READ_READY: return "VC_EVENT_READ_READY";
    case VC_EVENT_WRITE_READY: return "VC_EVENT_WRITE_READY";
    case VC_EVENT_READ_COMPLETE: return "VC_EVENT_READ_COMPLETE";
    case VC_EVENT_WRITE_COMPLETE: return "VC_EVENT_WRITE_COMPLETE";
    case VC_EVENT_EOS: return "VC_EVENT_EOS";
    case VC_EVENT_INACTIVITY_TIMEOUT: return "VC_EVENT_INACTIVITY_TIMEOUT";
    case VC_EVENT_ACTIVE_TIMEOUT: return "VC_EVENT_ACTIVE_TIMEOUT";

    case NET_EVENT_OPEN: return "NET_EVENT_OPEN";
    case NET_EVENT_OPEN_FAILED: return "NET_EVENT_OPEN_FAILED";
    case NET_EVENT_ACCEPT: return "NET_EVENT_ACCEPT";
    case NET_EVENT_ACCEPT_SUCCEED: return "NET_EVENT_ACCEPT_SUCCEED";
    case NET_EVENT_ACCEPT_FAILED: return "NET_EVENT_ACCEPT_FAILED";

#if 0
    case DISK_EVENT_OPEN: return "DISK_EVENT_OPEN";
    case DISK_EVENT_OPEN_FAILED: return "DISK_EVENT_OPEN_FAILED";
    case DISK_EVENT_CLOSE_COMPLETE: return "DISK_EVENT_CLOSE_COMPLETE";
    case DISK_EVENT_STAT_COMPLETE: return "DISK_EVENT_STAT_COMPLETE";
    case DISK_EVENT_SEEK_COMPLETE: return "DISK_EVENT_SEEK_COMPLETE";
#endif

    case CLUSTER_EVENT_CHANGE: return "CLUSTER_EVENT_CHANGE";
    case CLUSTER_EVENT_CONFIGURATION: return "CLUSTER_EVENT_CONFIGURATION";
    case CLUSTER_EVENT_OPEN: return "CLUSTER_EVENT_OPEN";
    case CLUSTER_EVENT_OPEN_FAILED: return "CLUSTER_EVENT_OPEN_FAILED";
    case CLUSTER_EVENT_STEAL_THREAD: return "CLUSTER_EVENT_STEAL_THREAD";

    case EVENT_HOST_DB_LOOKUP: return "EVENT_HOST_DB_LOOKUP";
    case EVENT_HOST_DB_GET_RESPONSE: return "EVENT_HOST_DB_GET_RESPONSE";

    case DNS_EVENT_EVENTS_START: return "DNS_EVENT_EVENTS_START";

#if 0
    case FTP_EVENT_OPEN: return "FTP_EVENT_OPEN";
    case FTP_EVENT_ACCEPT: return "FTP_EVENT_ACCEPT";
    case FTP_EVENT_OPEN_FAILED: return "FTP_EVENT_OPEN_FAILED";

    case MANAGEMENT_EVENT: return "MANAGEMENT_EVENT";

    case LOGIO_FINISHED: return "LOGIO_FINISHED";
    case LOGIO_WRITE: return "LOGIO_WRITE";
    case LOGIO_COUNTEDWRITE: return "LOGIO_COUNTEDWRITE";
    case LOGIO_HAVENETIO: return "LOGIO_HAVENETIO";
    case LOGIO_RECONFIG: return "LOGIO_RECONFIG";
    case LOGIO_RECONFIG_FILE: return "LOGIO_RECONFIG_FILE";
    case LOGIO_RECONFIG_FILEREAD: return "LOGIO_RECONFIG_FILEREAD";
    case LOGIO_PULSE: return "LOGIO_PULSE";
    case LOGIO_STARTUP: return "LOGIO_STARTUP";
#endif

    case MULTI_CACHE_EVENT_SYNC: return "MULTI_CACHE_EVENT_SYNC";

    case CACHE_EVENT_LOOKUP: return "CACHE_EVENT_LOOKUP";
    case CACHE_EVENT_LOOKUP_FAILED: return "CACHE_EVENT_LOOKUP_FAILED";
    case CACHE_EVENT_OPEN_READ: return "CACHE_EVENT_OPEN_READ";
    case CACHE_EVENT_OPEN_READ_FAILED: return "CACHE_EVENT_OPEN_READ_FAILED";
    case CACHE_EVENT_OPEN_WRITE: return "CACHE_EVENT_OPEN_WRITE";
    case CACHE_EVENT_OPEN_WRITE_FAILED: return "CACHE_EVENT_OPEN_WRITE_FAILED";
    case CACHE_EVENT_REMOVE: return "CACHE_EVENT_REMOVE";
    case CACHE_EVENT_REMOVE_FAILED: return "CACHE_EVENT_REMOVE_FAILED";
    case CACHE_EVENT_UPDATE: return "CACHE_EVENT_UPDATE";
    case CACHE_EVENT_UPDATE_FAILED: return "CACHE_EVENT_UPDATE_FAILED";
    case CACHE_EVENT_LINK: return "CACHE_EVENT_LINK";
    case CACHE_EVENT_LINK_FAILED: return "CACHE_EVENT_LINK_FAILED";
    case CACHE_EVENT_DEREF: return "CACHE_EVENT_DEREF";
    case CACHE_EVENT_DEREF_FAILED: return "CACHE_EVENT_DEREF_FAILED";
    case CACHE_EVENT_RESPONSE: return "CACHE_EVENT_RESPONSE";
    case CACHE_EVENT_RESPONSE_MSG: return "CACHE_EVENT_RESPONSE_MSG";

#if 0
    case CACHE_DB_EVENT_POOL_SYNC: return "CACHE_DB_EVENT_POOL_SYNC";
    case CACHE_DB_EVENT_ITERATE_VECVEC: return "CACHE_DB_EVENT_ITERATE_VECVEC";
    case CACHE_DB_EVENT_ITERATE_FRAG_HDR: return "CACHE_DB_EVENT_ITERATE_FRAG_HDR";
    case CACHE_DB_EVENT_ITERATE_DONE: return "CACHE_DB_EVENT_ITERATE_DONE";

    case HTTP_EVENT_CONNECTION_OPEN: return "HTTP_EVENT_CONNECTION_OPEN";
    case HTTP_EVENT_CONNECTION_OPEN_ERROR: return "HTTP_EVENT_CONNECTION_OPEN_ERROR";
    case HTTP_EVENT_READ_HEADER_COMPLETE: return "HTTP_EVENT_READ_HEADER_COMPLETE";
    case HTTP_EVENT_READ_HEADER_ERROR: return "HTTP_EVENT_READ_HEADER_ERROR";
    case HTTP_EVENT_READ_BODY_READY: return "HTTP_EVENT_READ_BODY_READY";
    case HTTP_EVENT_READ_BODY_COMPLETE: return "HTTP_EVENT_READ_BODY_COMPLETE";
    case HTTP_EVENT_WRITE_READY: return "HTTP_EVENT_WRITE_READY";
    case HTTP_EVENT_WRITE_COMPLETE: return "HTTP_EVENT_WRITE_COMPLETE";
    case HTTP_EVENT_EOS: return "HTTP_EVENT_EOS";
    case HTTP_EVENT_CLOSED: return "HTTP_EVENT_CLOSED";

    case NNTP_EVENT_CMD: return "NNTP_EVENT_CMD";
    case NNTP_EVENT_CALL: return "NNTP_EVENT_CALL";
    case NNTP_EVENT_CALL_DONE: return "NNTP_EVENT_CALL_DONE";
    case NNTP_EVENT_ACQUIRE: return "NNTP_EVENT_ACQUIRE";
    case NNTP_EVENT_ACQUIRE_FAILED: return "NNTP_EVENT_ACQUIRE_FAILED";
    case NNTP_EVENT_SLAVE_RESPONSE: return "NNTP_EVENT_SLAVE_RESPONSE";
    case NNTP_EVENT_SLAVE_INITIAL_ERROR: return "NNTP_EVENT_SLAVE_INITIAL_ERROR";
    case NNTP_EVENT_SLAVE_ERROR: return "NNTP_EVENT_SLAVE_ERROR";
    case NNTP_EVENT_SLAVE_DONE: return "NNTP_EVENT_SLAVE_DONE";
    case NNTP_EVENT_TUNNEL_DONE: return "NNTP_EVENT_TUNNEL_DONE";
    case NNTP_EVENT_TUNNEL_ERROR: return "NNTP_EVENT_TUNNEL_ERROR";
    case NNTP_EVENT_TUNNEL_CONT: return "NNTP_EVENT_TUNNEL_CONT";
    case NNTP_EVENT_CLUSTER_MSG: return "NNTP_EVENT_CLUSTER_MSG";
#endif

    case MGMT_EVENT_SHUTDOWN: return "MGMT_EVENT_SHUTDOWN";
    case MGMT_EVENT_RESTART: return "MGMT_EVENT_RESTART";
    case MGMT_EVENT_BOUNCE: return "MGMT_EVENT_BOUNCE";
    case MGMT_EVENT_CONFIG_FILE_UPDATE: return "MGMT_EVENT_CONFIG_FILE_UPDATE";
    case MGMT_EVENT_CLEAR_STATS: return "MGMT_EVENT_CLEAR_STATS";

  default:
    if (buffer != NULL) {
      snprintf(buffer, sizeof(buffer), "%d", event);
      return buffer;
    } else {
      return "UNKNOWN_EVENT";
    }
  }
}
