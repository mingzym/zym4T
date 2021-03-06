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

#ifndef _WEB_RECONFIG_H_
#define _WEB_RECONFIG_H_

/****************************************************************************
 *
 *  WebReconfig.h - code to handle config vars that can change on the fly
 *  
 * 
 ****************************************************************************/

#include "P_RecCore.h"

void setUpWebCB();
void updateWebConfig();

void markMgmtIpAllowChange();
void markAuthOtherUsersChange();

void configAuthEnabled();
void configAuthAdminUser();
void configAuthAdminPasswd();
void configAuthOtherUsers();
void configLangDict();
void configLoadFactor();
void configMgmtIpAllow();
void configRefreshRate();
void configSSLenable();
void configUI();

extern int webConfigChanged;

#endif // _WEB_RECONFIG_H_
