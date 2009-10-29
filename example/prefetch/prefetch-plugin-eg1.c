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

/*
  prefetch-plugin-eg1.c : an example plugin which interacts with
                          Traffic Server's prefetch feature

*/

#include <stdio.h>
#include <string.h>
#include "InkAPI.h"
#include "InkAPIPrivate.h"

/* We will register the following two hooks */

int my_preparse_hook(int hook, INKPrefetchInfo * info);
int my_embedded_url_hook(int hook, INKPrefetchInfo * info);

void
INKPluginInit(int argc, const char *argv[])
{
  INKPluginRegistrationInfo info;

  info.plugin_name = "prefetch_plugin_eg1";
  info.vendor_name = "MyCompany";
  info.support_email = "ts-api-support@MyCompany.com";

  if (!INKPluginRegister(INK_SDK_VERSION_5_2, &info)) {
    INKError("Plugin registration failed.\n");
  }

  /* register our hooks */
  INKPrefetchHookSet(INK_PREFETCH_PRE_PARSE_HOOK, &my_preparse_hook);
  INKPrefetchHookSet(INK_PREFETCH_EMBEDDED_URL_HOOK, &my_embedded_url_hook);
}

int
my_preparse_hook(int hook, INKPrefetchInfo * info)
{
  unsigned char *ip = (unsigned char *) &info->client_ip;

  printf("preparese hook (%d): request from child %u.%u.%u.%u\n", hook, ip[0], ip[1], ip[2], ip[3]);


  /* we will let TS parese the page */
  return INK_PREFETCH_CONTINUE;
}

int
my_embedded_url_hook(int hook, INKPrefetchInfo * info)
{

  unsigned char *ip = (unsigned char *) &info->client_ip;

  printf("url hook (%d): url: %s %s child: %u.%u.%u.%u\n",
         hook, info->embedded_url, (info->present_in_cache) ? "(present in cache)" : "", ip[0], ip[1], ip[2], ip[3]);

  /*
     We will select UDP for sending url and TCP for sending object
   */

  info->url_proto = INK_PREFETCH_PROTO_UDP;
  info->url_response_proto = INK_PREFETCH_PROTO_TCP;

  /* we can return INK_PREFETCH_DISCONTINUE if we dont want TS to prefetch
     this url */

  return INK_PREFETCH_CONTINUE;
}
