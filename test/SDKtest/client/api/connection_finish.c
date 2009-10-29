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

#include "ClientAPI.h"
#include <stdio.h>

void
INKPluginInit(int clientid)
{
  fprintf(stderr, "connection finish!!!\n");
  INKFuncRegister(INK_FID_CONNECTION_FINISH);
}

void
INKConnectionFinish(void *rid, INKConnectionStatus status)
{
  switch (status) {
  case INK_CONN_COMPLETE:
    fprintf(stderr, "c");
    break;
  case INK_TIME_EXPIRE:
    fprintf(stderr, "x");
    break;
  case INK_CONN_ERR:
    fprintf(stderr, "e");
    break;
  case INK_READ_ERR:
    fprintf(stderr, "r");
    break;
  case INK_WRITE_ERR:
    fprintf(stderr, "w");
    break;
  default:
    fprintf(stderr, "u");
    break;
  }
}
