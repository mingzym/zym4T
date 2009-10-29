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

#include "Protocol.h"

/* global variable */
INKTextLogObject protocol_plugin_log;

/* static variable */
static INKAction pending_action;
static int accept_port;
static int server_port;

/* Functions only seen in this file, should be static. */
static void protocol_init(int accept_port, int server_port);
static int accept_handler(INKCont contp, INKEvent event, void *edata);


/* When the handle is called, the net_vc is returned. */
static int
accept_handler(INKCont contp, INKEvent event, void *edata)
{
  INKCont txn_sm;
  INKMutex pmutex;
  int lock;

  switch (event) {
  case INK_EVENT_NET_ACCEPT:
    /* Create a new mutex for the TxnSM, which is going
       to handle the incoming request. */
    pmutex = (INKMutex) INKMutexCreate();
    txn_sm = (INKCont) TxnSMCreate(pmutex, (INKVConn) edata, server_port);

    /* This is no reason for not grabbing the lock.
       So skip the routine which handle LockTry failure case. */
    if (INKMutexLockTry(pmutex, &lock) == INK_SUCCESS) {
      INKContCall(txn_sm, 0, NULL);
      INKMutexUnlock(pmutex);
    }
    break;

  default:
    /* Something wrong with the network, if there are any 
       pending NetAccept, cancel them. */
    if (pending_action && !INKActionDone(pending_action))
      INKActionCancel(pending_action);

    INKContDestroy(contp);
    break;
  }

  return INK_EVENT_NONE;
}

static void
protocol_init(int accept_port, int server_port)
{
  INKCont contp;
  int ret_val;

  /* create customized log */
  ret_val = INKTextLogObjectCreate("protocol", INK_LOG_MODE_ADD_TIMESTAMP, &protocol_plugin_log);
  if (ret_val != INK_SUCCESS) {
    INKError("failed to create log");
  }

  /* format of the log entries, for caching_status, 1 for HIT and 0 for MISS */
  ret_val = INKTextLogObjectWrite(protocol_plugin_log, "timestamp filename servername caching_status\n\n");
  if (ret_val != INK_SUCCESS) {
    INKError("failed to write into log");
  }

  contp = INKContCreate(accept_handler, INKMutexCreate());

  /* Accept network traffic from the accept_port.
     When there are requests coming in, contp's handler
     should be called, in this case, contp's handler
     is accept_event, see AcceptSM.c */
  pending_action = INKNetAccept(contp, accept_port);
}

int
check_ts_version()
{

  const char *ts_version = INKTrafficServerVersionGet();
  int result = 0;

  if (ts_version) {
    int major_ts_version = 0;
    int minor_ts_version = 0;
    int patch_ts_version = 0;

    if (sscanf(ts_version, "%d.%d.%d", &major_ts_version, &minor_ts_version, &patch_ts_version) != 3) {
      return 0;
    }

    /* Need at least TS 5.2 */
    if (major_ts_version > 5) {
      result = 1;
    } else if (major_ts_version == 5) {
      if (minor_ts_version >= 2) {
        result = 1;
      }
    }
  }

  return result;
}


void
INKPluginInit(int argc, const char *argv[])
{
  INKPluginRegistrationInfo info;

  info.plugin_name = "output-header";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_5_2, &info)) {
    INKError("[PluginInit] Plugin registration failed.\n");
    goto error;
  }

  if (!check_ts_version()) {
    INKError("[PluginInit] Plugin requires Traffic Server 5.2.0 or later\n");
    goto error;
  }


  /* default value */
  accept_port = 4666;
  server_port = 4666;

  if (argc < 3) {
    INKDebug("protocol", "Usage: protocol.so accept_port server_port");
    printf("[protocol_plugin] Usage: protocol.so accept_port server_port\n");
    printf("[protocol_plugin] Wrong arguments. Using deafult ports.\n");
  } else {
    if (!isnan(atoi(argv[1]))) {
      accept_port = atoi(argv[1]);
      INKDebug("protocol", "using accept_port %d", accept_port);
      printf("[protocol_plugin] using accept_port %d\n", accept_port);
    } else {
      printf("[protocol_plugin] Wrong argument for accept_port.");
      printf("Using deafult port %d\n", accept_port);
    }

    if (!isnan(atoi(argv[2]))) {
      server_port = atoi(argv[2]);
      INKDebug("protocol", "using server_port %d", server_port);
      printf("[protocol_plugin] using server_port %d\n", server_port);
    } else {
      printf("[protocol_plugin] Wrong argument for server_port.");
      printf("Using deafult port %d\n", server_port);
    }
  }

  protocol_init(accept_port, server_port);

error:
  INKError("[PluginInit] Plugin not initialized");
}
