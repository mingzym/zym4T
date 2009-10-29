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

/* blacklist-1.c:  an example program that denies client access                 
 *                 to blacklisted sites. This plugin illustrates
 *                 how to use configuration information from a 
 *                 configuration file (blacklist.txt) that can be
 *                 updated through the Traffic Manager UI.
 *
 *
 *	Usage:	
 *	(NT) : BlackList.dll 
 *	(Solaris) : blacklist-1.so 
 *
 *
 */

#include <stdio.h>
#include <string.h>
#include "InkAPI.h"

#define MAX_NSITES 500
#define RETRY_TIME 10

#define PLUGIN_NAME "blacklist-1-neg"
#define LOG_SET_FUNCTION_NAME(NAME) const char * FUNCTION_NAME = NAME

#define LOG_ERROR_NEG(API_NAME) { \
    INKDebug(PLUGIN_NAME, "%s: %s %s %s File %s, line number %d",PLUGIN_NAME, API_NAME, "NEGAPIFAIL", \
             FUNCTION_NAME, __FILE__, __LINE__); \
}

static char *sites[MAX_NSITES];
static int nsites;
static INKMutex sites_mutex;
static INKTextLogObject log;
static INKCont global_contp;

static void handle_txn_start(INKCont contp, INKHttpTxn txnp);

typedef struct contp_data
{

  enum calling_func
  {
    HANDLE_DNS,
    HANDLE_RESPONSE,
    READ_BLACKLIST
  } cf;

  INKHttpTxn txnp;

} cdata;


static void
handle_dns(INKHttpTxn txnp, INKCont contp)
{
  INKMBuffer bufp;
  INKMLoc hdr_loc;
  INKMLoc url_loc;
  const char *host;
  int i;
  int host_length;
  cdata *cd;

  if (!INKHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    INKError("couldn't retrieve client request header\n");
    goto done;
  }

  url_loc = INKHttpHdrUrlGet(bufp, hdr_loc);
  if (!url_loc) {
    INKError("couldn't retrieve request url\n");
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    goto done;
  }

  host = INKUrlHostGet(bufp, url_loc, &host_length);
  if (!host) {
    INKError("couldn't retrieve request hostname\n");
    INKHandleMLocRelease(bufp, hdr_loc, url_loc);
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    goto done;
  }

  /*  the continuation we are dealing here is created for each transaction.
     since the transactions themselves have a mutex associated to them, 
     we don't need to lock that mutex explicitly. */
  for (i = 0; i < nsites; i++) {
    if (strncmp(host, sites[i], host_length) == 0) {
      if (log) {
        INKTextLogObjectWrite(log, "blacklisting site: %s", sites[i]);
      } else {
        INKDebug("blacklist-1", "blacklisting site: %s\n", sites[i]);
      }
      INKHttpTxnHookAdd(txnp, INK_HTTP_SEND_RESPONSE_HDR_HOOK, contp);
      INKHandleStringRelease(bufp, url_loc, host);
      INKHandleMLocRelease(bufp, hdr_loc, url_loc);
      INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
      INKHttpTxnReenable(txnp, INK_EVENT_HTTP_ERROR);
      INKMutexUnlock(sites_mutex);
      return;
    }
  }

  INKHandleStringRelease(bufp, url_loc, host);
  INKHandleMLocRelease(bufp, hdr_loc, url_loc);
  INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);

done:
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
  /* If not a blacklist site, then destroy the continuation created for 
     this transaction */
  cd = (cdata *) INKContDataGet(contp);
  INKfree(cd);
  INKContDestroy(contp);
}

static void
handle_response(INKHttpTxn txnp, INKCont contp)
{
  INKMBuffer bufp;
  INKMLoc hdr_loc;
  INKMLoc url_loc;
  char *url_str;
  char *buf;
  int url_length;
  cdata *cd;

  LOG_SET_FUNCTION_NAME("handle_response");

  if (!INKHttpTxnClientRespGet(txnp, &bufp, &hdr_loc)) {
    INKError("couldn't retrieve client response header\n");
    goto done;
  }

  INKHttpHdrStatusSet(bufp, hdr_loc, INK_HTTP_STATUS_FORBIDDEN);
  INKHttpHdrReasonSet(bufp, hdr_loc,
                      INKHttpHdrReasonLookup(INK_HTTP_STATUS_FORBIDDEN),
                      strlen(INKHttpHdrReasonLookup(INK_HTTP_STATUS_FORBIDDEN)));

  if (!INKHttpTxnClientReqGet(txnp, &bufp, &hdr_loc)) {
    INKError("couldn't retrieve client request header\n");
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    goto done;
  }

  url_loc = INKHttpHdrUrlGet(bufp, hdr_loc);
  if (!url_loc) {
    INKError("couldn't retrieve request url\n");
    INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);
    goto done;
  }

  buf = (char *) INKmalloc(4096);

  url_str = INKUrlStringGet(bufp, url_loc, &url_length);
  sprintf(buf, "You are forbidden from accessing \"%s\"\n", url_str);
  INKfree(url_str);
  INKHandleMLocRelease(bufp, hdr_loc, url_loc);
  INKHandleMLocRelease(bufp, INK_NULL_MLOC, hdr_loc);

  INKHttpTxnErrorBodySet(txnp, buf, strlen(buf), NULL);

  /* negative test for INKHttpTxnErrorBodySet */
#ifdef DEBUG
  if (INKHttpTxnErrorBodySet(NULL, buf, strlen(buf), NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKHttpTxnErrorBodySet");
  }
  if (INKHttpTxnErrorBodySet(txnp, NULL, 10, NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKHttpTxnErrorBodySet");
  }
#endif

done:
  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
  /* After everything's done, Destroy the continuation 
     created for this transaction */
  cd = (cdata *) INKContDataGet(contp);
  INKfree(cd);
  INKContDestroy(contp);
}

static void
read_blacklist(INKCont contp)
{
  char blacklist_file[1024];
  INKFile file;
  int lock;
  INKReturnCode ret_code;

  LOG_SET_FUNCTION_NAME("read_blacklist");
  sprintf(blacklist_file, "%s/blacklist.txt", INKPluginDirGet());
  file = INKfopen(blacklist_file, "r");

  ret_code = INKMutexLockTry(sites_mutex, &lock);

  if (ret_code == INK_ERROR) {
    INKError("Failed to lock mutex. Cannot read new blacklist file. Exiting ...\n");
    return;
  }
  nsites = 0;

  /* If the Mutext lock is not successful try again in RETRY_TIME */
  if (!lock) {
    INKContSchedule(contp, RETRY_TIME);
    return;
  }

  if (file != NULL) {
    char buffer[1024];

    while (INKfgets(file, buffer, sizeof(buffer) - 1) != NULL && nsites < MAX_NSITES) {
      char *eol;
      if ((eol = strstr(buffer, "\r\n")) != NULL) {
        /* To handle newlines on Windows */
        *eol = '\0';
      } else if ((eol = strchr(buffer, '\n')) != NULL) {
        *eol = '\0';
      } else {
        /* Not a valid line, skip it */
        continue;
      }
      if (sites[nsites] != NULL) {
        INKfree(sites[nsites]);
      }
      sites[nsites] = INKstrdup(buffer);
      nsites++;
    }

    INKfclose(file);
  } else {
    INKError("unable to open %s\n", blacklist_file);
    INKError("all sites will be allowed\n", blacklist_file);
  }

  INKMutexUnlock(sites_mutex);

  /* negative test for INKContSchedule */
#ifdef DEBUG
  if (INKContSchedule(NULL, 10) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKContSchedule");
  }
#endif

}

static int
blacklist_plugin(INKCont contp, INKEvent event, void *edata)
{
  INKHttpTxn txnp;
  cdata *cd;

  switch (event) {
  case INK_EVENT_HTTP_TXN_START:
    txnp = (INKHttpTxn) edata;
    handle_txn_start(contp, txnp);
    return 0;
  case INK_EVENT_HTTP_OS_DNS:
    if (contp != global_contp) {
      cd = (cdata *) INKContDataGet(contp);
      cd->cf = HANDLE_DNS;
      handle_dns(cd->txnp, contp);
      return 0;
    } else {
      break;
    }
  case INK_EVENT_HTTP_SEND_RESPONSE_HDR:
    if (contp != global_contp) {
      cd = (cdata *) INKContDataGet(contp);
      cd->cf = HANDLE_RESPONSE;
      handle_response(cd->txnp, contp);
      return 0;
    } else {
      break;
    }
  case INK_EVENT_MGMT_UPDATE:
    if (contp == global_contp) {
      read_blacklist(contp);
      return 0;
    } else {
      break;
    }
  case INK_EVENT_TIMEOUT:
    /* when mutex lock is not acquired and continuation is rescheduled, 
       the plugin is called back with INK_EVENT_TIMEOUT with a NULL 
       edata. We need to decide, in which function did the MutexLock 
       failed and call that function again */
    if (contp != global_contp) {
      cd = (cdata *) INKContDataGet(contp);
      switch (cd->cf) {
      case HANDLE_DNS:
        handle_dns(cd->txnp, contp);
        return 0;
      case HANDLE_RESPONSE:
        handle_response(cd->txnp, contp);
        return 0;
      default:
        break;
      }
    } else {
      read_blacklist(contp);
      return 0;
    }
  default:
    break;
  }
  return 0;
}

static void
handle_txn_start(INKCont contp, INKHttpTxn txnp)
{
  INKCont txn_contp;
  cdata *cd;

  txn_contp = INKContCreate((INKEventFunc) blacklist_plugin, INKMutexCreate());
  /* create the data that'll be associated with the continuation */
  cd = (cdata *) INKmalloc(sizeof(cdata));
  INKContDataSet(txn_contp, cd);

  cd->txnp = txnp;

  INKHttpTxnHookAdd(txnp, INK_HTTP_OS_DNS_HOOK, txn_contp);

  INKHttpTxnReenable(txnp, INK_EVENT_HTTP_CONTINUE);
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

    /* Since this is an TS-SDK 2.0 plugin, we need at
       least Traffic Server 3.5.2 to run */
    if (major_ts_version > 3) {
      result = 1;
    } else if (major_ts_version == 3) {
      if (minor_ts_version > 5) {
        result = 1;
      } else if (minor_ts_version == 5) {
        if (patch_ts_version >= 2) {
          result = 1;
        }
      }
    }
  }

  return result;
}

void
INKPluginInit(int argc, const char *argv[])
{
  int i;
  INKPluginRegistrationInfo info;
  INKReturnCode error;

  LOG_SET_FUNCTION_NAME("INKPluginInit");
  info.plugin_name = "blacklist-1";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_2_0, &info)) {
    INKError("Plugin registration failed.\n");
  }

  if (!check_ts_version()) {
    INKError("Plugin requires Traffic Server 3.5.2 or later\n");
    return;
  }

  /* create an INKTextLogObject to log blacklisted requests to */
  error = INKTextLogObjectCreate("blacklist", INK_LOG_MODE_ADD_TIMESTAMP, &log);
  if (!log || error == INK_ERROR) {
    INKDebug("blacklist-1", "error while creating log");
  }

  sites_mutex = INKMutexCreate();

  nsites = 0;
  for (i = 0; i < MAX_NSITES; i++) {
    sites[i] = NULL;
  }

  global_contp = INKContCreate(blacklist_plugin, sites_mutex);
  read_blacklist(global_contp);

  /*INKHttpHookAdd (INK_HTTP_OS_DNS_HOOK, contp); */
  INKHttpHookAdd(INK_HTTP_TXN_START_HOOK, global_contp);

  INKMgmtUpdateRegister(global_contp, "Inktomi Blacklist Plugin", "blacklist.cgi");

#ifdef DEBUG
  /* negative test for INKMgmtUpdateRegister */
  if (INKMgmtUpdateRegister(NULL, "Inktomi Blacklist Plugin", "blacklist.cgi") != INK_ERROR) {
    LOG_ERROR_NEG("INKMgmtUpdateRegister");
  }
  if (INKMgmtUpdateRegister(global_contp, NULL, "blacklist.cgi") != INK_ERROR) {
    LOG_ERROR_NEG("INKMgmtUpdateRegister");
  }
  if (INKMgmtUpdateRegister(global_contp, "Inktomi Blacklist Plugin", NULL) != INK_ERROR) {
    LOG_ERROR_NEG("INKMgmtUpdateRegister");
  }

  /* negative test for INKIOBufferReaderClone & INKVConnAbort */
  if (INKIOBufferReaderClone(NULL) != INK_ERROR_PTR) {
    LOG_ERROR_NEG("INKIOBufferReaderClone");
  }

  if (INKVConnAbort(NULL, 1) != INK_ERROR) {
    LOG_ERROR_NEG("INKVConnAbort");
  }
#endif
}
