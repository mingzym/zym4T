/** @file

  Error code defines

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

  @section details Details

  Contains all the errno codes that we may need (above and beyond those
  provided by /usr/include/errno.h)

*/

#if !defined (_Errno_h_)
#define _Errno_h_
#include <errno.h>

#define INK_START_ERRNO 20000

#define SOCK_ERRNO                        INK_START_ERRNO
#define NET_ERRNO                         INK_START_ERRNO+100
#define CLUSTER_ERRNO                     INK_START_ERRNO+200
#define FTP_ERRNO                         INK_START_ERRNO+300
#define CACHE_ERRNO                       INK_START_ERRNO+400
#define NNTP_ERRNO                        INK_START_ERRNO+500
#define HTTP_ERRNO                        INK_START_ERRNO+600

#define ENET_THROTTLING                   (NET_ERRNO+1)

#define ESOCK_DENIED                      (SOCK_ERRNO+0)
#define ESOCK_TIMEOUT                     (SOCK_ERRNO+1)
#define ESOCK_NO_SOCK_SERVER_CONN         (SOCK_ERRNO+2)

// Error codes for CLUSTER_EVENT_OPEN_FAILED
#define ECLUSTER_NO_VC                    (CLUSTER_ERRNO+0)
#define ECLUSTER_NO_MACHINE               (CLUSTER_ERRNO+1)
#define ECLUSTER_OP_TIMEOUT               (CLUSTER_ERRNO+2)
#define ECLUSTER_ORB_DATA_READ            (CLUSTER_ERRNO+3)
#define ECLUSTER_ORB_EIO            	  (CLUSTER_ERRNO+4)
#define ECLUSTER_CHANNEL_INUSE       	  (CLUSTER_ERRNO+5)
#define ECLUSTER_NOMORE_CHANNELS       	  (CLUSTER_ERRNO+6)

#define EFTP_ILL_REPLY_SYNTAX             (FTP_ERRNO+0)
#define EFTP_TIMEOUT                      (FTP_ERRNO+1)
#define EFTP_CONNECTION_ERROR             (FTP_ERRNO+2)
#define EFTP_NO_CTRL_CONN                 (FTP_ERRNO+3)
#define EFTP_SEND_CMD                     (FTP_ERRNO+4)
#define EFTP_ILL_REPLY_CODE               (FTP_ERRNO+5)
#define EFTP_NO_PASV_CONN                 (FTP_ERRNO+6)
#define EFTP_NO_ACCEPT                    (FTP_ERRNO+7)
#define EFTP_LOGIN_INCORRECT              (FTP_ERRNO+8)
#define EFTP_FILE_UNAVAILABLE             (FTP_ERRNO+9)
#define EFTP_FTP_PROTOCOL_ERROR           (FTP_ERRNO+10)
#define EFTP_SERVICE_UNAVAILABLE          (FTP_ERRNO+11)
#define EFTP_NAME_TOO_LONG                (FTP_ERRNO+12)
#define EFTP_INTERNAL                     (FTP_ERRNO+13)
#define EFTP_ERROR                        (FTP_ERRNO+14)

#define ECACHE_NO_DOC                     (CACHE_ERRNO+0)
#define ECACHE_DOC_BUSY                   (CACHE_ERRNO+1)
#define ECACHE_DIR_BAD                    (CACHE_ERRNO+2)
#define ECACHE_BAD_META_DATA              (CACHE_ERRNO+3)
#define ECACHE_READ_FAIL                  (CACHE_ERRNO+4)
#define ECACHE_WRITE_FAIL                 (CACHE_ERRNO+5)
#define ECACHE_MAX_ALT_EXCEEDED           (CACHE_ERRNO+6)
#define ECACHE_NOT_READY                  (CACHE_ERRNO+7)
#define ECACHE_ALT_MISS                   (CACHE_ERRNO+8)
#define ECACHE_BAD_READ_REQUEST           (CACHE_ERRNO+9)

#define ENNTP_ERROR                       (NNTP_ERRNO+0)

#define EHTTP_ERROR                       (HTTP_ERRNO+0)

#endif
