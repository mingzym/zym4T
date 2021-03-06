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

/*****************************************************************************
 * Filename: CoreAPIShared.h
 * Purpose: This file contains functions that are same for local and remote API
 * Created: 01/29/00
 * Created by: Lan Tran
 *
 * 
 ***************************************************************************/

#ifndef _CORE_API_SHARED_H_
#define _CORE_API_SHARED_H_

#include "INKMgmtAPI.h"

#define NUM_EVENTS          19  // number of predefined TM events
#define MAX_EVENT_NAME_SIZE 100 // max length for an event name
#define MAX_RECORD_SIZE     20  // max length of buffer to hold record values

// LAN - BAD BHACK; copied from Alarms.h !!!!!
/* Must be same as defined in Alarms.h; the reason we had to 
 * redefine them here is because the remote client also needs
 * access to these values for its event handling 
 */
#define MGMT_ALARM_UNDEFINED                     0
#define MGMT_ALARM_PROXY_PROCESS_DIED            1
#define MGMT_ALARM_PROXY_PROCESS_BORN            2
#define MGMT_ALARM_PROXY_PEER_BORN               3
#define MGMT_ALARM_PROXY_PEER_DIED               4
#define MGMT_ALARM_PROXY_CONFIG_ERROR            5
#define MGMT_ALARM_PROXY_SYSTEM_ERROR            6
#define MGMT_ALARM_PROXY_LOG_SPACE_CRISIS        7
#define MGMT_ALARM_PROXY_CACHE_ERROR             8
#define MGMT_ALARM_PROXY_CACHE_WARNING           9
#define MGMT_ALARM_PROXY_LOGGING_ERROR           10
#define MGMT_ALARM_PROXY_LOGGING_WARNING         11
#define MGMT_ALARM_PROXY_NNTP_ERROR	         12
#define MGMT_ALARM_MGMT_TEST                     13
#define MGMT_ALARM_CONFIG_UPDATE_FAILED          14
#define MGMT_ALARM_WEB_ERROR                     15
#define MGMT_ALARM_PING_FAILURE	                 16
#define MGMT_ALARM_MGMT_CONFIG_ERROR             17
#define MGMT_ALARM_ADD_ALARM                     18
#define MGMT_ALARM_PROXY_FTP_ERROR	         22

// used by INKReadFromUrl
#define HTTP_DIVIDER "\r\n\r\n"
#define URL_BUFSIZE  65536      // the max. lenght of URL obtainable (in bytes)
#define URL_TIMEOUT  5000       // the timeout value for send/recv HTTP in ms
#define HTTP_PORT    80
#define BUFSIZE      1024

// used by INKReadFromUrl 
INKError parseHTTPResponse(char *buffer, char **header, int *hdr_size, char **body, int *bdy_size);
INKError readHTTPResponse(int sock, char *buffer, int bufsize, inku64 timeout);
INKError sendHTTPRequest(int sock, char *request, inku64 timeout);
int connectDirect(char *host, int port, inku64 timeout);

// used by NetworkUtilRemote.cc and NetworkUtilLocal.cc
int socket_read_timeout(int fd, int sec, int usec);
int socket_write_timeout(int fd, int sec, int usec);

// used for Events
int get_event_id(const char *event_name);
char *get_event_name(int id);

#endif
