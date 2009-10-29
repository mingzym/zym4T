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

/***************************************/
/****************************************************************************
 *  WebConfig.cc - code to process Config File Editor requests, and
 *                 create responses
 *
 *
 ****************************************************************************/

#include "ink_unused.h"       /* MAGIC_EDITING_TAG */

#include "ink_platform.h"
#include "inktomi++.h"

#include "WebConfig.h"
#include "WebGlobals.h"
#include "WebHttp.h"

#include "CfgContextUtils.h"


//-------------------------------------------------------------------------
// defines
//-------------------------------------------------------------------------
#define CFG_RULE_DELIMITER "^"  // used for Javascript rules
#define HTML_DELIM         "&nbsp;&nbsp;&nbsp;&nbsp;&nbsp"
#define MAX_RULE_LENGTH    1024

//-------------------------------------------------------------------------
// convertRules
//------------------------------------------------------------------------- 
// "list" contains the indices of the invalid rules in the "rules" array
// Need to convert all the rules into displayable format and put it
// into a buffer. 
// Return an allocated buffer containing the HTML of the invalid rules
// that will appear in "Rule" format. Returns NULL if error. 
// Note, that it will dequeue the elements from errRules list, but will 
// not free it (it is up to the caller to free the INKIntList)
//
char *
convertRules(INKFileNameT file, INKIntList errRules, char *rules[])
{
  char *rule = NULL;
  int *index;
  textBuffer buf(4096);
  char num[10];

  while (!INKIntListIsEmpty(errRules)) {
    index = INKIntListDequeue(errRules);
    switch (file) {
    case INK_FNAME_CACHE_OBJ:
      rule = formatCacheRule(rules[*index]);
      break;
    case INK_FNAME_FILTER:
      rule = formatFilterRule(rules[*index]);
      break;
    case INK_FNAME_FTP_REMAP:
      rule = formatFtpRemapRule(rules[*index]);
      break;
    case INK_FNAME_HOSTING:
      rule = formatHostingRule(rules[*index]);
      break;
    case INK_FNAME_ICP_PEER:
      rule = formatIcpRule(rules[*index]);
      break;
    case INK_FNAME_IP_ALLOW:
      rule = formatIpAllowRule(rules[*index]);
      break;
    case INK_FNAME_MGMT_ALLOW:
      rule = formatMgmtAllowRule(rules[*index]);
      break;
    case INK_FNAME_NNTP_ACCESS:
      rule = formatNntpAccessRule(rules[*index]);
      break;
    case INK_FNAME_NNTP_SERVERS:
      rule = formatNntpServersRule(rules[*index]);
      break;
    case INK_FNAME_PARENT_PROXY:
      rule = formatParentRule(rules[*index]);
      break;
    case INK_FNAME_PARTITION:
      rule = formatPartitionRule(rules[*index]);
      break;
    case INK_FNAME_REMAP:
      rule = formatRemapRule(rules[*index]);
      break;
    case INK_FNAME_SOCKS:
      rule = formatSocksRule(rules[*index]);
      break;
    case INK_FNAME_SPLIT_DNS:
      rule = formatSplitDnsRule(rules[*index]);
      break;
    case INK_FNAME_UPDATE_URL:
      rule = formatUpdateRule(rules[*index]);
      break;
    case INK_FNAME_VADDRS:
      rule = formatVaddrsRule(rules[*index]);
      break;
    default:                   // UH-OH!!! 
      goto Lerror;
    }
    if (rule) {
      memset(num, 0, 10);
      ink_snprintf(num, 10, "[%d] ", *index);
      buf.copyFrom(num, strlen(num));
      buf.copyFrom(rule, strlen(rule));
      buf.copyFrom("<BR>", strlen("<BR>"));
      xfree(index);
      xfree(rule);
    }
  }
  if (buf.bufPtr())
    return xstrdup(buf.bufPtr());

Lerror:
  return NULL;
}

//-------------------------------------------------------------------------
// formatArmSecurityRule
//-------------------------------------------------------------------------
//
char *
formatArmSecurityRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);

  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Rule Type=%s%s", tokens[0], HTML_DELIM);
  }
  if (strlen(tokens[1]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Connection Type=%s%s", tokens[1], HTML_DELIM);
  }
  if (strlen(tokens[2]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Source IP=%s%s", tokens[2], HTML_DELIM);
  }
  if (strlen(tokens[3]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Destination IP=%s%s", tokens[3], HTML_DELIM);
  }
  if (strlen(tokens[4]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Open Ports=%s%s", tokens[4], HTML_DELIM);
  }
  if (strlen(tokens[5]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Dest Ports=%s%s", tokens[5], HTML_DELIM);
  }
  if (strlen(tokens[6]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Source Ports=%s", tokens[6]);
  }

  return xstrdup(buf);
}

//-------------------------------------------------------------------------
// formatBypassRule
//-------------------------------------------------------------------------
// rule.rule_type + delim + rule.src_ip + delim + rule.dest_ip + delim
//
char *
formatBypassRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);

  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Rule Type=%s%s", tokens[0], HTML_DELIM);
  }
  if (strlen(tokens[1]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Source IP=%s%s", tokens[1], HTML_DELIM);
  }
  if (strlen(tokens[2]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Destination IP=%s", tokens[2]);
  }

  return xstrdup(buf);
}

//-------------------------------------------------------------------------
// formatCacheRule
//-------------------------------------------------------------------------
//
char *
formatCacheRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);
  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Rule Type=%s%s", tokens[0], HTML_DELIM);
  }
  if (strlen(tokens[1]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s=", tokens[1]);
  }
  if (strlen(tokens[2]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s%s", tokens[2], HTML_DELIM);
  } else {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s", HTML_DELIM);
  }
  if (strlen(tokens[10]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Time Period=%s%s", tokens[10], HTML_DELIM);
  }
  if (strlen(tokens[3]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Time=%s%s", tokens[3], HTML_DELIM);
  }
  if (strlen(tokens[4]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Source IP=%s%s", tokens[4], HTML_DELIM);
  }
  if (strlen(tokens[5]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Prefix=%s%s", tokens[5], HTML_DELIM);
  }
  if (strlen(tokens[6]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Suffix=%s%s", tokens[6], HTML_DELIM);
  }
  if (strlen(tokens[7]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Port=%s%s", tokens[7], HTML_DELIM);
  }
  if (strlen(tokens[8]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Method=%s%s", tokens[8], HTML_DELIM);
  }
  if (strlen(tokens[9]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Scheme=%s%s", tokens[9], HTML_DELIM);
  }
  if (strlen(tokens[11]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "MIXT Scheme=%s", tokens[11]);
  }

  return xstrdup(buf);
}

//-------------------------------------------------------------------------
// formatFilterRule
//-------------------------------------------------------------------------
//
char *
formatFilterRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);

  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Rule Type=%s%s", tokens[0], HTML_DELIM);
  }
  if (strlen(tokens[1]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s=", tokens[1]);
  }
  if (strlen(tokens[2]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s%s", tokens[2], HTML_DELIM);
  } else {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s", HTML_DELIM);
  }

  if (strlen(tokens[10]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Header Type=%s%s", tokens[10], HTML_DELIM);
  }
  if (strlen(tokens[11]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Server Name=%s%s", tokens[11], HTML_DELIM);
  }
  if (strlen(tokens[12]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "DN=%s%s", tokens[12], HTML_DELIM);
  }
  if (strlen(tokens[14]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "UID Filter=%s%s", tokens[14], HTML_DELIM);
  }
  if (strlen(tokens[15]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Attribute Name=%s%s", tokens[15], HTML_DELIM);
  }
  if (strlen(tokens[16]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Attribute Value=%s%s", tokens[16], HTML_DELIM);
  }
  if (strlen(tokens[13]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Realm=%s%s", tokens[13], HTML_DELIM);
  }
  if (strlen(tokens[17]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Redirect URL=%s%s", tokens[17], HTML_DELIM);
  }
  if (strlen(tokens[18]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Bind DN=%s%s", tokens[18], HTML_DELIM);
  }
  if (strlen(tokens[19]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Bind Password=%s%s", tokens[19], HTML_DELIM);
  }
  if (strlen(tokens[20]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Bind Password File=%s%s", tokens[20], HTML_DELIM);
  }


  if (strlen(tokens[3]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Time=%s%s", tokens[3], HTML_DELIM);
  }
  if (strlen(tokens[4]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Source IP=%s%s", tokens[4], HTML_DELIM);
  }
  if (strlen(tokens[5]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Prefix=%s%s", tokens[5], HTML_DELIM);
  }
  if (strlen(tokens[6]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Suffix=%s%s", tokens[6], HTML_DELIM);
  }
  if (strlen(tokens[7]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Port=%s%s", tokens[7], HTML_DELIM);
  }
  if (strlen(tokens[8]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Method=%s%s", tokens[8], HTML_DELIM);
  }
  if (strlen(tokens[9]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Scheme=%s%s", tokens[9], HTML_DELIM);
  }
  if (strlen(tokens[21]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "MIXT Scheme=%s", tokens[17]);
  }

  return xstrdup(buf);
}

//-------------------------------------------------------------------------
// formatFtpRemapRule
//-------------------------------------------------------------------------
//
char *
formatFtpRemapRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);

  ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Traffic Server=");
  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s", tokens[0], HTML_DELIM);
  }
  if (strlen(tokens[1]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), ":%s%s", tokens[1], HTML_DELIM);
  } else {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s", HTML_DELIM);
  }
  ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "FTP Server=");
  if (strlen(tokens[2]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s", tokens[2], HTML_DELIM);
  }
  if (strlen(tokens[3]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), ":%s", tokens[3]);
  }

  return xstrdup(buf);
}

//-------------------------------------------------------------------------
// formatHostingRule
//-------------------------------------------------------------------------
//
char *
formatHostingRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);

  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s=", tokens[0], HTML_DELIM);
  }
  if (strlen(tokens[1]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s%s", tokens[1], HTML_DELIM);
  } else {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s", HTML_DELIM);
  }
  if (strlen(tokens[2]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Partitions=%s", tokens[2]);
  }

  return xstrdup(buf);
}

//-------------------------------------------------------------------------
// formatIcpRule
//-------------------------------------------------------------------------
//
char *
formatIcpRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);

  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Peer Hostname=%s%s", tokens[0], HTML_DELIM);
  }
  if (strlen(tokens[1]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Peer IP=%s%s", tokens[1], HTML_DELIM);
  }
  if (strlen(tokens[2]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Peer Type=%s%s", tokens[2], HTML_DELIM);
  }
  if (strlen(tokens[3]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Proxy Port=%s%s", tokens[3], HTML_DELIM);
  }
  if (strlen(tokens[4]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "ICP Port=%s%s", tokens[4], HTML_DELIM);
  }
  if (strlen(tokens[5]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Multicast=%s%s", tokens[5], HTML_DELIM);
  }
  if (strlen(tokens[6]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Multicast IP=%s%s", tokens[6], HTML_DELIM);
  }
  if (strlen(tokens[7]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Multicast TTL=%s", tokens[7]);
  }

  return xstrdup(buf);
}

//-------------------------------------------------------------------------
// formatIpAllowRule
//-------------------------------------------------------------------------
//
char *
formatIpAllowRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);

  if (strlen(tokens[1]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "IP Action=%s%s", tokens[1], HTML_DELIM);
  }
  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Source IP=%s", tokens[0]);
  }

  return xstrdup(buf);
}

//-------------------------------------------------------------------------
// formatIpnatRule
//-------------------------------------------------------------------------
//
char *
formatIpnatRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);

  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Interface=%s%s", tokens[0], HTML_DELIM);
  }
  if (strlen(tokens[6]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Connection Type=%s%s", tokens[6], HTML_DELIM);
  }
  if (strlen(tokens[1]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Source=%s", tokens[1]);
  }
  if (strlen(tokens[2]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "/%s", tokens[2]);
  }
  if (strlen(tokens[3]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), ":%s", tokens[3]);
  }
  ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s", HTML_DELIM);
  if (strlen(tokens[4]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Destination=%s", tokens[4]);
  }
  if (strlen(tokens[5]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), ":%s", tokens[5]);
  }
  ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s", HTML_DELIM);
  if (strlen(tokens[7]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Protocol=%s", tokens[7]);
  }

  return xstrdup(buf);
}

//-------------------------------------------------------------------------
// formatMgmtAllowRule
//-------------------------------------------------------------------------
//
char *
formatMgmtAllowRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);

  if (strlen(tokens[1]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "IP Action=%s%s", tokens[1], HTML_DELIM);
  }
  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Source IP=%s", tokens[0]);
  }


  return xstrdup(buf);
}

//-------------------------------------------------------------------------
// formatNntpAccessRule
//-------------------------------------------------------------------------
//
char *
formatNntpAccessRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);

  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s=", tokens[0], HTML_DELIM);
  }
  if (strlen(tokens[1]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s%s", tokens[1], HTML_DELIM);
  } else {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s", HTML_DELIM);
  }
  if (strlen(tokens[2]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Access Type=%s%s", tokens[2], HTML_DELIM);
  }
  if (strlen(tokens[3]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Authenticator=%s%s", tokens[3], HTML_DELIM);
  }
  if (strlen(tokens[4]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Username=%s%s", tokens[4], HTML_DELIM);
  }
  if (strlen(tokens[5]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Password=%s%s", tokens[5], HTML_DELIM);
  }
  if (strlen(tokens[6]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Newsgroups=%s%s", tokens[6], HTML_DELIM);
  }
  if (strlen(tokens[7]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Posting=%s", tokens[7]);
  }

  return xstrdup(buf);
}

//-------------------------------------------------------------------------
// formatNntpServersRule
//-------------------------------------------------------------------------
//
char *
formatNntpServersRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);


  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Hostname=%s%s", tokens[0], HTML_DELIM);
  }
  if (strlen(tokens[1]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Newsgroups=%s%s", tokens[1], HTML_DELIM);
  }
  if (strlen(tokens[2]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Treatment=%s%s", tokens[2], HTML_DELIM);
  }
  if (strlen(tokens[3]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Priority=%s%s", tokens[3], HTML_DELIM);
  }
  if (strlen(tokens[4]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Interface=%s", tokens[4]);
  }

  return xstrdup(buf);
}

//-------------------------------------------------------------------------
// formatParentRule
//-------------------------------------------------------------------------
//
char *
formatParentRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);

  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s=", tokens[0]);
  }
  if (strlen(tokens[1]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s%s", tokens[1], HTML_DELIM);
  } else {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s", HTML_DELIM);
  }

  if (strlen(tokens[10]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Parents=%s%s", tokens[10], HTML_DELIM);
  }
  if (strlen(tokens[11]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Round Robin=%s%s", tokens[11], HTML_DELIM);
  }
  if (strlen(tokens[12]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Go Direct=%s%s", tokens[12], HTML_DELIM);
  }

  if (strlen(tokens[2]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Time=%s%s", tokens[2], HTML_DELIM);
  }
  if (strlen(tokens[3]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Source IP=%s%s", tokens[3], HTML_DELIM);
  }
  if (strlen(tokens[4]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Prefix=%s%s", tokens[4], HTML_DELIM);
  }
  if (strlen(tokens[5]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Suffix=%s%s", tokens[5], HTML_DELIM);
  }
  if (strlen(tokens[6]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Port=%s%s", tokens[6], HTML_DELIM);
  }
  if (strlen(tokens[7]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Method=%s%s", tokens[7], HTML_DELIM);
  }
  if (strlen(tokens[8]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Scheme=%s%s", tokens[8], HTML_DELIM);
  }
  if (strlen(tokens[9]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "MIXT Scheme=%s", tokens[9]);
  }

  return xstrdup(buf);
}

//-------------------------------------------------------------------------
// formatPartitionRule
//-------------------------------------------------------------------------
//
char *
formatPartitionRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);

  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Partition=%s%s", tokens[0], HTML_DELIM);
  }
  if (strlen(tokens[1]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Scheme=%s%s", tokens[1], HTML_DELIM);
  }
  if (strlen(tokens[2]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Size=%s", tokens[2]);
  }
  if (strcmp(tokens[3], "absolute") == 0)
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), " MB");
  else if (strcmp(tokens[3], "percent") == 0)
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), " %%");

  return xstrdup(buf);
}

//-------------------------------------------------------------------------
// formatRemapRule
//-------------------------------------------------------------------------
//
char *
formatRemapRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);

  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Rule Type=%s%s", tokens[0], HTML_DELIM);
  }
  ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "From URL=");
  if (strlen(tokens[1]) > 0) {  // scheme
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s://", tokens[1]);
  }
  if (strlen(tokens[2]) > 0) {  // from path
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s", tokens[2]);
  }
  if (strlen(tokens[3]) > 0) {  // from port
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), ":%s", tokens[3]);
  }
  if (strlen(tokens[4]) > 0) {  // from path
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "/%s", tokens[4]);
  }
  ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s", HTML_DELIM);

  ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "To URL=");
  if (strlen(tokens[1]) > 0) {  // scheme
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s://", tokens[1]);
  }
  if (strlen(tokens[5]) > 0) {  // to host
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s", tokens[5]);
  }
  if (strlen(tokens[6]) > 0) {  // to port
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), ":%s", tokens[6]);
  }
  if (strlen(tokens[7]) > 0) {  // to path
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "/%s", tokens[7]);
  }

  if (strlen(tokens[8]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "MIXT Scheme=%s", tokens[8]);
  }

  return xstrdup(buf);
}

//-------------------------------------------------------------------------
// formatSocksRule
//-------------------------------------------------------------------------
//
char *
formatSocksRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);

  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Rule Type=%s%s", tokens[0], HTML_DELIM);
  }
  if (strlen(tokens[2]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "User=%s%s", tokens[2], HTML_DELIM);
  }
  if (strlen(tokens[3]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Password=%s%s", tokens[3], HTML_DELIM);
  }
  if (strlen(tokens[1]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Destination IP=%s%s", tokens[1], HTML_DELIM);
  }
  if (strlen(tokens[4]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Socsk Servers=%s%s", tokens[4], HTML_DELIM);
  }
  if (strlen(tokens[5]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Round Robin=%s", tokens[5]);
  }

  return xstrdup(buf);
}

//-------------------------------------------------------------------------
// formatSplitDnsRule
//-------------------------------------------------------------------------
//
char *
formatSplitDnsRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);

  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s=", tokens[0]);
  }
  if (strlen(tokens[1]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s%s", tokens[1], HTML_DELIM);
  } else {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "%s", HTML_DELIM);
  }
  if (strlen(tokens[2]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "DNS Server IP(s)=%s%s", tokens[2], HTML_DELIM);
  }
  if (strlen(tokens[3]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Default Domain Name=%s%s", tokens[3], HTML_DELIM);
  }
  if (strlen(tokens[4]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Domain Search List=%s", tokens[4]);
  }

  return xstrdup(buf);
}

//-------------------------------------------------------------------------
// formatUpdateRule
//-------------------------------------------------------------------------
//
char *
formatUpdateRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);

  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "URL=%s%s", tokens[0], HTML_DELIM);
  }
  if (strlen(tokens[1]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Headers=%s%s", tokens[1], HTML_DELIM);
  }
  if (strlen(tokens[2]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Offset Hour=%s%s", tokens[2], HTML_DELIM);
  }
  if (strlen(tokens[3]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Interval=%s%s", tokens[3], HTML_DELIM);
  }
  if (strlen(tokens[4]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Recursion Depth=%s", tokens[4]);
  }

  return xstrdup(buf);
}

//-------------------------------------------------------------------------
// formatVaddrsRule
//-------------------------------------------------------------------------
//
char *
formatVaddrsRule(char *rule)
{
  Tokenizer tokens(CFG_RULE_DELIMITER);
  tokens.Initialize(rule, ALLOW_EMPTY_TOKS);
  char buf[MAX_RULE_LENGTH];

  if (!rule)
    return NULL;
  memset(buf, 0, MAX_RULE_LENGTH);

  if (strlen(tokens[0]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Virtual IP=%s%s", tokens[0], HTML_DELIM);
  }
  if (strlen(tokens[1]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Ethernet Interface=%s%s", tokens[1], HTML_DELIM);
  }
  if (strlen(tokens[2]) > 0) {
    ink_snprintf(buf + strlen(buf), MAX_RULE_LENGTH - strlen(buf), "Sub-Interface=%s", tokens[2]);
  }

  return xstrdup(buf);
}




//-------------------------------------------------------------------------
// updateCacheConfig
//-------------------------------------------------------------------------
int
updateCacheConfig(char *rules[], int numRules, char **errBuff)
{
  INKCfgContext ctx = NULL;
  INKCacheEle *ele;
  Tokenizer tokens(CFG_RULE_DELIMITER);
  INKPdSsFormat *pdss;
  INKActionNeedT action_need;
  INKError response;
  int i, err = WEB_HTTP_ERR_OKAY;
  INKIntList errRules = NULL;

  ctx = INKCfgContextCreate(INK_FNAME_CACHE_OBJ);
  if (!ctx) {
    Debug("config", "[updateCacheConfig] can't allocate ctx memory");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // since we want to preserve comments, we need to read in the 
  // file using INKCfgContextGet and remove all the rules; starting from scratch
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY || INKCfgContextRemoveAll(ctx) != INK_ERR_OKAY) {
    Debug("config", "[updateCacheConfig] Failed to Get and Clear CfgContext");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // create Ele's by parsing the rules in the rules array 
  // insert the Ele's into a Cfg Context; if get invalid formatted rule, just skip it
  for (i = 0; i < numRules; i++) {
    tokens.Initialize(rules[i], ALLOW_EMPTY_TOKS);
    // we know there should be 13 tokens for cache.config rule
    ele = INKCacheEleCreate(INK_TYPE_UNDEFINED);
    pdss = &(ele->cache_info);

    // rule type
    if (strcmp(tokens[0], "never-cache") == 0) {
      ele->cfg_ele.type = INK_CACHE_NEVER;
    } else if (strcmp(tokens[0], "ignore-no-cache") == 0) {
      ele->cfg_ele.type = INK_CACHE_IGNORE_NO_CACHE;
    } else if (strcmp(tokens[0], "ignore-client-no-cache") == 0) {
      ele->cfg_ele.type = INK_CACHE_IGNORE_CLIENT_NO_CACHE;
    } else if (strcmp(tokens[0], "ignore-server-no-cache") == 0) {
      ele->cfg_ele.type = INK_CACHE_IGNORE_SERVER_NO_CACHE;
    } else if (strcmp(tokens[0], "pin-in-cache") == 0) {
      ele->cfg_ele.type = INK_CACHE_PIN_IN_CACHE;
    } else if (strcmp(tokens[0], "revalidate") == 0) {
      ele->cfg_ele.type = INK_CACHE_REVALIDATE;
    } else if (strcmp(tokens[0], "ttl-in-cache") == 0) {
      ele->cfg_ele.type = INK_CACHE_TTL_IN_CACHE;
    } else if (strcmp(tokens[0], "cache-auth-content") == 0) {
      ele->cfg_ele.type = INK_CACHE_AUTH_CONTENT;
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateCacheConfig] invalid rule - SKIP");
    }

    // pd type
    if (strcmp(tokens[1], "dest_domain") == 0) {
      pdss->pd_type = INK_PD_DOMAIN;
    } else if (strcmp(tokens[1], "dest_host") == 0) {
      pdss->pd_type = INK_PD_HOST;
    } else if (strcmp(tokens[1], "dest_ip") == 0) {
      pdss->pd_type = INK_PD_IP;
    } else if (strcmp(tokens[1], "url_regex") == 0) {
      pdss->pd_type = INK_PD_URL_REGEX;
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateCacheConfig] invalid rule - SKIP");
    }

    // pd value - Required field!
    if (strlen(tokens[2]) <= 0) {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateCacheConfig] invalid rule - SKIP");
    }
    pdss->pd_val = xstrdup(tokens[2]);

    // FIXME: lots of parsing and conversion to do - similar to CfgContextUtils.cc
    // secondary specifiers

    // time    
    if (strlen(tokens[3]) > 0) {
      if (string_to_time_struct(tokens[3], &(pdss->sec_spec)) != INK_ERR_OKAY) {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateCacheConfig] invalid time sec spec. - SKIP");
      }
    }
    // src_ip
    if (strlen(tokens[4]) > 0) {
      pdss->sec_spec.src_ip = string_to_ip_addr(tokens[4]);
      if (!pdss->sec_spec.src_ip) {     // invalid IP
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateCacheConfig] invalid src_ip - SKIP");
      }
    }
    // prefix
    if (strlen(tokens[5]) > 0) {
      pdss->sec_spec.prefix = xstrdup(tokens[5]);
    }
    // suffix
    if (strlen(tokens[6]) > 0) {
      pdss->sec_spec.suffix = xstrdup(tokens[6]);
    }
    // port
    if (strlen(tokens[7]) > 0) {
      pdss->sec_spec.port = string_to_port_ele(tokens[7]);
      if (!pdss->sec_spec.port) {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateCacheConfig] invalid port - SKIP");
      }
    }
    // method
    if (strlen(tokens[8]) > 0) {
      pdss->sec_spec.method = string_to_method_type(tokens[8]);
    }
    // scheme
    if (strlen(tokens[9]) > 0) {
      pdss->sec_spec.scheme = string_to_scheme_type(tokens[9]);
    }
    // time_period
    if (strlen(tokens[10]) > 0) {
      if (string_to_hms_time(tokens[10], &(ele->time_period)) != INK_ERR_OKAY) {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateCacheConfig] invalid hms time - SKIP");
      }
    }
    // mixt tag
    if (strlen(tokens[11]) > 0) {
      pdss->sec_spec.mixt = string_to_mixt_type(tokens[11]);
    }

    INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);     // add new ele to end of list

  }

  // commit the CfgContext to write a new version of the file
  errRules = INKIntListCreate();
  response = INKCfgContextCommit(ctx, &action_need, errRules);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    err = WEB_HTTP_ERR_INVALID_CFG_RULE;
    *errBuff = convertRules(INK_FNAME_CACHE_OBJ, errRules, rules);
  } else if (response != INK_ERR_OKAY) {
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }

Lerror:
  if (errRules)
    INKIntListDestroy(errRules);
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}

//-------------------------------------------------------------------------
// updateFilterConfig
//-------------------------------------------------------------------------
int
updateFilterConfig(char *rules[], int numRules, char **errBuff)
{
  INKCfgContext ctx = NULL;
  INKFilterEle *ele;
  Tokenizer tokens(CFG_RULE_DELIMITER);
  INKPdSsFormat *pdss;
  INKActionNeedT action_need;
  INKError response;
  int i, err = WEB_HTTP_ERR_OKAY;
  INKIntList errRules = NULL;

  ctx = INKCfgContextCreate(INK_FNAME_FILTER);
  if (!ctx) {
    Debug("config", "[updateFilterConfig] can't allocate ctx memory");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // since we want to preserve comments, we need to read in the 
  // file using INKCfgContextGet and remove all the rules; starting from scratch
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY || INKCfgContextRemoveAll(ctx) != INK_ERR_OKAY) {
    Debug("config", "[updateFilterConfig] Failed to Get and Clear CfgContext");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // create Ele's by parsing the rules in the rules array 
  // insert the Ele's into a Cfg Context; if get invalid formatted rule, just skip it
  for (i = 0; i < numRules; i++) {
    tokens.Initialize(rules[i], ALLOW_EMPTY_TOKS);
    ele = INKFilterEleCreate(INK_TYPE_UNDEFINED);
    pdss = &(ele->filter_info);

    // rule type
    if (strcmp(tokens[0], "allow") == 0) {
      ele->cfg_ele.type = INK_FILTER_ALLOW;
    } else if (strcmp(tokens[0], "deny") == 0) {
      ele->cfg_ele.type = INK_FILTER_DENY;
    } else if (strcmp(tokens[0], "ldap") == 0) {
      ele->cfg_ele.type = INK_FILTER_LDAP;
    } else if (strcmp(tokens[0], "ntlm") == 0) {
      ele->cfg_ele.type = INK_FILTER_NTLM;
    } else if (strcmp(tokens[0], "radius") == 0) {
      ele->cfg_ele.type = INK_FILTER_RADIUS;
    } else if (strcmp(tokens[0], "keep_hdr") == 0) {
      ele->cfg_ele.type = INK_FILTER_KEEP_HDR;
    } else if (strcmp(tokens[0], "strip_hdr") == 0) {
      ele->cfg_ele.type = INK_FILTER_STRIP_HDR;
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateFilterConfig] invalid rule - SKIP");
    }

    // pd type
    if (strcmp(tokens[1], "dest_domain") == 0) {
      pdss->pd_type = INK_PD_DOMAIN;
    } else if (strcmp(tokens[1], "dest_host") == 0) {
      pdss->pd_type = INK_PD_HOST;
    } else if (strcmp(tokens[1], "dest_ip") == 0) {
      pdss->pd_type = INK_PD_IP;
    } else if (strcmp(tokens[1], "url_regex") == 0) {
      pdss->pd_type = INK_PD_URL_REGEX;
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateFilterConfig] invalid rule - SKIP");
    }

    // pd value - Required field!
    if (strlen(tokens[2]) <= 0) {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateFilterConfig] invalid rule - SKIP");
    }
    pdss->pd_val = xstrdup(tokens[2]);

    // FIXME: lots of parsing and conversion to do - similar to CfgContextUtils.cc
    // secondary specifiers

    // time    
    if (strlen(tokens[3]) > 0) {
      if (string_to_time_struct(tokens[3], &(pdss->sec_spec)) != INK_ERR_OKAY) {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateFilterConfig] invalid time sec spec. - SKIP");
      }
    }
    // src_ip
    if (strlen(tokens[4]) > 0) {
      pdss->sec_spec.src_ip = string_to_ip_addr(tokens[4]);
      if (!pdss->sec_spec.src_ip) {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateFilterConfig] invalid src_ip - SKIP");
      }
    }
    // prefix
    if (strlen(tokens[5]) > 0) {
      pdss->sec_spec.prefix = xstrdup(tokens[5]);
    }
    // suffix
    if (strlen(tokens[6]) > 0) {
      pdss->sec_spec.suffix = xstrdup(tokens[6]);
    }
    // port
    if (strlen(tokens[7]) > 0) {
      pdss->sec_spec.port = string_to_port_ele(tokens[7]);
      if (!pdss->sec_spec.port) {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateFilterConfig] invalid port - SKIP");
      }
    }
    // method
    if (strlen(tokens[8]) > 0) {
      pdss->sec_spec.method = string_to_method_type(tokens[8]);
    }
    // scheme
    if (strlen(tokens[9]) > 0) {
      pdss->sec_spec.scheme = string_to_scheme_type(tokens[9]);
    }
    // header type
    if (strlen(tokens[10]) > 0) {
      ele->hdr = string_to_header_type(tokens[10]);
    }
    //  LDAP/NTLM/radius optional parameters
    if (strlen(tokens[11]) > 0) {
      ele->server = xstrdup(tokens[11]);
    }
    if (strlen(tokens[12]) > 0) {
      ele->dn = xstrdup(tokens[12]);
    }
    if (strlen(tokens[13]) > 0) {
      ele->realm = xstrdup(tokens[13]);
    }
    if (strlen(tokens[14]) > 0) {
      ele->uid_filter = xstrdup(tokens[14]);
    }
    if (strlen(tokens[15]) > 0) {
      ele->attr = xstrdup(tokens[15]);
    }
    if (strlen(tokens[16]) > 0) {
      ele->attr_val = xstrdup(tokens[16]);
    }
    if (strlen(tokens[17]) > 0) {
      ele->redirect_url = xstrdup(tokens[17]);
    }
    if (strlen(tokens[18]) > 0) {
      ele->bind_dn = xstrdup(tokens[18]);
    }
    // ignore the bind_pwd token; only track the bind_pwd_file token
    if (strlen(tokens[20]) > 0) {
      ele->bind_pwd_file = xstrdup(tokens[20]);
    }
    // mixt tag
    if (strlen(tokens[21]) > 0) {
      pdss->sec_spec.mixt = string_to_mixt_type(tokens[21]);
    }

    INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);     // add new ele to end of list

  }

  // commit the CfgContext to write a new version of the file
  errRules = INKIntListCreate();
  response = INKCfgContextCommit(ctx, &action_need, errRules);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    err = WEB_HTTP_ERR_INVALID_CFG_RULE;
    *errBuff = convertRules(INK_FNAME_FILTER, errRules, rules);
  } else if (response != INK_ERR_OKAY) {
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }

Lerror:
  if (errRules)
    INKIntListDestroy(errRules);
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}

//-------------------------------------------------------------------------
// updateFilterConfigPassword
//-------------------------------------------------------------------------
// Create new rule by reading the values from the form which are specified in 
// WebConfigRender::writeFilterConfigForm(). Be sure to encrypt the password
// and create a bind_pwd_file. 
int
updateFilterConfigPassword(WebHttpContext * whc, char **errBuff)
{
  char *rule_type, *pd_type, *pd_val, *time, *src_ip, *prefix, *suffix, *port, *method, *scheme, *mixt, *hdr_type,
    *server, *dn, *realm, *uid_filter, *attr_name, *attr_val, *redirect_url, *bind_dn, *bind_pwd;
  char ruleStr[MAX_RULE_LENGTH];
  INKFilterEle *ele;
  INKPdSsFormat *pdss;
  INKActionNeedT action_need;
  INKCfgContext ctx = NULL;
  INKIntList errRules = NULL;
  INKError response = INK_ERR_OKAY;
  int err = WEB_HTTP_ERR_OKAY;
  bool valid = true;
  int table_lookup_value = 0;

  memset(ruleStr, 0, MAX_RULE_LENGTH);

  // make sure all required fields are specified 
  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "rule_type", (void **) &rule_type);
  if (!rule_type) {
    valid = false;              // ERROR: this must be specified! 
  } else {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "Rule Type=%s%s", rule_type, HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "pd_type", (void **) &pd_type);
  if (table_lookup_value && !pd_type) {
    valid = false;              // ERROR: this must be specified! 
  } else {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "%s=", pd_type);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "pd_val", (void **) &pd_val);
  if (table_lookup_value && !pd_val) {
    valid = false;              // ERROR: this must be specified! 
  } else {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "%s%s", pd_val, HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "time", (void **) &time);
  if (table_lookup_value && time) {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "Time=%s%s", time, HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "src_ip", (void **) &src_ip);
  if (table_lookup_value && src_ip) {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "Source IP=%s%s", src_ip, HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "prefix", (void **) &prefix);
  if (table_lookup_value && prefix) {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "Prefix=%s%s", prefix, HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "suffix", (void **) &suffix);
  if (table_lookup_value && suffix) {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "Suffix=%s%s", suffix, HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "port", (void **) &port);
  if (table_lookup_value && port) {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "Port=%s%s", port, HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "method", (void **) &method);
  if (table_lookup_value && method) {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "Method=%s%s", method, HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "scheme", (void **) &scheme);
  if (table_lookup_value && scheme) {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "Scheme=%s%s", scheme, HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "mixt", (void **) &mixt);
  if (table_lookup_value && mixt) {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "MIXT Scheme=%s%s", mixt, HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "hdr_type", (void **) &hdr_type);
  // coverity[DEADCODE]
  if (table_lookup_value && hdr_type) {
    valid = false;              // ERROR: this should not be specified! 
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "Header Type=%s%s", hdr_type,
                 HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "server", (void **) &server);
  if (table_lookup_value && server) {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "Server Name=%s%s", server, HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "dn", (void **) &dn);
  if (table_lookup_value && dn) {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "DN=%s%s", dn, HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "realm", (void **) &realm);
  if (table_lookup_value && realm) {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "Realm=%s%s", realm, HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "uid_filter", (void **) &uid_filter);
  if (table_lookup_value && uid_filter) {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "UID Filter=%s%s", uid_filter,
                 HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "attr_name", (void **) &attr_name);
  if (table_lookup_value && attr_name) {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "Attribute Name=%s%s", attr_name,
                 HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "attr_val", (void **) &attr_val);
  if (table_lookup_value && attr_val) {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "Attribute Value=%s%s", attr_val,
                 HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "redirect_url", (void **) &redirect_url);
  if (table_lookup_value && redirect_url) {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "Redirect URL=%s%s", redirect_url,
                 HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "bind_dn", (void **) &bind_dn);
  if (table_lookup_value && !bind_dn) {
    valid = false;              // ERROR: should have 
  } else {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "Bind DN=%s%s", bind_dn, HTML_DELIM);
  }

  table_lookup_value = ink_hash_table_lookup(whc->post_data_ht, "bind_pwd", (void **) &bind_pwd);
  if (table_lookup_value && !bind_pwd) {
    valid = false;              // ERROR: should have    
  } else {
    ink_snprintf(ruleStr + strlen(ruleStr), MAX_RULE_LENGTH - strlen(ruleStr), "Bind Password=%s%s", bind_pwd,
                 HTML_DELIM);
  }

  if (!valid)
    goto Lerror_commit;

  // add the rule to end of the file 
  ctx = INKCfgContextCreate(INK_FNAME_FILTER);
  if (!ctx) {
    Debug("config", "[updateFilterConfigPassword] can't allocate ctx memory");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror_commit;
  }

  if (INKCfgContextGet(ctx) != INK_ERR_OKAY) {
    Debug("config", "[updateFilterConfigPassword] Failed to get CfgContext");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror_commit;
  }

  ele = INKFilterEleCreate(INK_TYPE_UNDEFINED);
  pdss = &(ele->filter_info);

  // rule type
  if (strcmp(rule_type, "allow") == 0) {
    ele->cfg_ele.type = INK_FILTER_ALLOW;
  } else if (strcmp(rule_type, "deny") == 0) {
    ele->cfg_ele.type = INK_FILTER_DENY;
  } else if (strcmp(rule_type, "ldap") == 0) {
    ele->cfg_ele.type = INK_FILTER_LDAP;
  } else if (strcmp(rule_type, "ntlm") == 0) {
    ele->cfg_ele.type = INK_FILTER_NTLM;
  } else if (strcmp(rule_type, "radius") == 0) {
    ele->cfg_ele.type = INK_FILTER_RADIUS;
  } else if (strcmp(rule_type, "keep_hdr") == 0) {
    ele->cfg_ele.type = INK_FILTER_KEEP_HDR;
  } else if (strcmp(rule_type, "strip_hdr") == 0) {
    ele->cfg_ele.type = INK_FILTER_STRIP_HDR;
  } else {
    ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    Debug("config", "[updateFilterConfigPassword] invalid rule - SKIP");
  }

  // pd type
  if (strcmp(pd_type, "dest_domain") == 0) {
    pdss->pd_type = INK_PD_DOMAIN;
  } else if (strcmp(pd_type, "dest_host") == 0) {
    pdss->pd_type = INK_PD_HOST;
  } else if (strcmp(pd_type, "dest_ip") == 0) {
    pdss->pd_type = INK_PD_IP;
  } else if (strcmp(pd_type, "url_regex") == 0) {
    pdss->pd_type = INK_PD_URL_REGEX;
  } else {
    ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    Debug("config", "[updateFilterConfigPassword] invalid rule - SKIP");
  }

  // pd value - Required field!
  if (pd_val) {
    pdss->pd_val = xstrdup(pd_val);
  } else {
    ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
    Debug("config", "[updateFilterConfigPassword] invalid rule - SKIP");
  }

  // time    
  if (time) {
    if (string_to_time_struct(time, &(pdss->sec_spec)) != INK_ERR_OKAY) {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateFilterConfigPassword] invalid time sec spec. - SKIP");
    }
  }
  // src_ip
  if (src_ip) {
    pdss->sec_spec.src_ip = string_to_ip_addr(src_ip);
    if (!pdss->sec_spec.src_ip) {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateFilterConfigPassword] invalid src_ip - SKIP");
    }
  }
  // prefix
  if (prefix) {
    pdss->sec_spec.prefix = xstrdup(prefix);
  }
  // suffix
  if (suffix) {
    pdss->sec_spec.suffix = xstrdup(suffix);
  }
  // port
  if (port) {
    pdss->sec_spec.port = string_to_port_ele(port);
    if (!pdss->sec_spec.port) {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateFilterConfig] invalid port - SKIP");
    }
  }
  // method
  if (method) {
    pdss->sec_spec.method = string_to_method_type(method);
  }
  // scheme
  if (scheme) {
    pdss->sec_spec.scheme = string_to_scheme_type(scheme);
  }
  // mixt tag
  if (mixt) {
    pdss->sec_spec.mixt = string_to_mixt_type(mixt);
  }
  // header type
  if (hdr_type) {
    ele->hdr = string_to_header_type(hdr_type);
  }
  //  LDAP/NTLM/radius optional parameters
  if (server) {
    ele->server = xstrdup(server);
  }
  if (dn) {
    ele->dn = xstrdup(dn);
  }
  if (realm) {
    ele->realm = xstrdup(realm);
  }
  if (uid_filter) {
    ele->uid_filter = xstrdup(uid_filter);
  }
  if (attr_name) {
    ele->attr = xstrdup(attr_name);
  }
  if (attr_val) {
    ele->attr_val = xstrdup(attr_val);
  }
  if (redirect_url) {
    ele->redirect_url = xstrdup(redirect_url);
  }
  if (bind_dn) {
    ele->bind_dn = xstrdup(bind_dn);
  }
  if (bind_pwd) {               // encrypt the password 
    char *pwd_file = encryptToFileAuth_malloc(bind_pwd);
    if (pwd_file) {
      ele->bind_pwd_file = pwd_file;
    } else {
      Debug("config", "[updateFilterConfigPassword] password encryption failed");
      goto Lerror_encrypt;
    }
  }

  INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);       // add new ele to end of list

  // commit the CfgContext to write a new version of the file
  errRules = INKIntListCreate();
  response = INKCfgContextCommit(ctx, &action_need, errRules);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    goto Lerror_commit;
  } else if (response != INK_ERR_OKAY) {
    err = WEB_HTTP_ERR_FAIL;
    goto Lend;
  } else {
    err = WEB_HTTP_ERR_OKAY;
    goto Lend;
  }

Lerror_encrypt:
  err = WEB_HTTP_ERR_FAIL;
  *errBuff = xstrdup("Password encryption failed. The rule could not be committed.");
  goto Lend;

Lerror_commit:
  err = WEB_HTTP_ERR_INVALID_CFG_RULE;
  *errBuff = xstrdup(ruleStr);

Lend:
  if (errRules)
    INKIntListDestroy(errRules);
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}

//-------------------------------------------------------------------------
// updateFtpRemapConfig
//-------------------------------------------------------------------------
int
updateFtpRemapConfig(char *rules[], int numRules, char **errBuff)
{
  INKCfgContext ctx = NULL;
  INKFtpRemapEle *ele;
  Tokenizer tokens(CFG_RULE_DELIMITER);
  INKActionNeedT action_need;
  INKError response;
  int i, err = WEB_HTTP_ERR_OKAY;
  INKIntList errRules = NULL;

  ctx = INKCfgContextCreate(INK_FNAME_FTP_REMAP);
  if (!ctx) {
    Debug("config", "[updateFtpRemapConfig] can't allocate ctx memory");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // since we want to preserve comments, we need to read in the 
  // file using INKCfgContextGet and remove all the rules; starting from scratch
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY || INKCfgContextRemoveAll(ctx) != INK_ERR_OKAY) {
    Debug("config", "[updateFtpRemapConfig] Failed to Get and Clear CfgContext");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // create Ele's by parsing the rules in the rules array 
  // insert the Ele's into a Cfg Context; if invalid formatted rule, just skip it
  for (i = 0; i < numRules; i++) {
    tokens.Initialize(rules[i], ALLOW_EMPTY_TOKS);

    ele = INKFtpRemapEleCreate();

    if (strlen(tokens[0]) <= 0 || strlen(tokens[1]) <= 0 ||
        strlen(tokens[2]) <= 0 || strlen(tokens[3]) <= 0 || !isNumber(tokens[1]) || !isNumber(tokens[3])) {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateFtpRemapConfig] Invalid rule - SKIP");
    } else {
      ele->from_val = xstrdup(tokens[0]);
      ele->from_port = ink_atoi(tokens[1]);
      ele->to_val = xstrdup(tokens[2]);
      ele->to_port = ink_atoi(tokens[3]);
    }
    INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);     // add new ele to end of list     
  }

  // commit the CfgContext to write a new version of the file
  errRules = INKIntListCreate();
  response = INKCfgContextCommit(ctx, &action_need, errRules);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    err = WEB_HTTP_ERR_INVALID_CFG_RULE;
    *errBuff = convertRules(INK_FNAME_FTP_REMAP, errRules, rules);
  } else if (response != INK_ERR_OKAY) {
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }

Lerror:
  if (errRules)
    INKIntListDestroy(errRules);
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}

//-------------------------------------------------------------------------
// updateHostingConfig
//-------------------------------------------------------------------------
int
updateHostingConfig(char *rules[], int numRules, char **errBuff)
{
  INKCfgContext ctx = NULL;
  INKHostingEle *ele;
  Tokenizer tokens(CFG_RULE_DELIMITER);
  INKActionNeedT action_need;
  INKError response;
  int i, err = WEB_HTTP_ERR_OKAY;
  INKIntList errRules = NULL;

  ctx = INKCfgContextCreate(INK_FNAME_HOSTING);
  if (!ctx) {
    Debug("config", "[updateHostingConfig] can't allocate ctx memory");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // since we want to preserve comments, we need to read in the 
  // file using INKCfgContextGet and remove all the rules; starting from scratch
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY || INKCfgContextRemoveAll(ctx) != INK_ERR_OKAY) {
    Debug("config", "[updateHostingConfig] Failed to Get and Clear CfgContext");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // create Ele's by parsing the rules in the rules array 
  // insert the Ele's into a Cfg Context; if get invalid formatted rule, just skip it
  for (i = 0; i < numRules; i++) {
    tokens.Initialize(rules[i], ALLOW_EMPTY_TOKS);
    ele = INKHostingEleCreate();

    // pd type
    if (strcmp(tokens[0], "domain") == 0) {
      ele->pd_type = INK_PD_DOMAIN;
    } else if (strcmp(tokens[0], "hostname") == 0) {
      ele->pd_type = INK_PD_HOST;
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateHostingConfig] invalid rule - SKIP");
    }

    // pd value
    if (strlen(tokens[1]) > 0) {
      ele->pd_val = xstrdup(tokens[1]);
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateHostingConfig] invalid rule - SKIP");
    }

    // partitions
    if (strlen(tokens[2]) > 0) {
      ele->partitions = string_to_int_list(tokens[2], ",");
    } else {                    // a required field
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateHostingConfig] invalid rule - SKIP");
    }

    INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);     // add new ele to end of list     
  }

  // commit the CfgContext to write a new version of the file
  errRules = INKIntListCreate();
  response = INKCfgContextCommit(ctx, &action_need, errRules);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    err = WEB_HTTP_ERR_INVALID_CFG_RULE;
    *errBuff = convertRules(INK_FNAME_HOSTING, errRules, rules);
  } else if (response != INK_ERR_OKAY) {
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }

Lerror:
  if (errRules)
    INKIntListDestroy(errRules);
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}

//-------------------------------------------------------------------------
// updateIcpConfig
//-------------------------------------------------------------------------
int
updateIcpConfig(char *rules[], int numRules, char **errBuff)
{
  INKCfgContext ctx = NULL;
  INKIcpEle *ele;
  Tokenizer tokens(CFG_RULE_DELIMITER);
  INKActionNeedT action_need;
  INKError response;
  int i, err = WEB_HTTP_ERR_OKAY;
  INKIntList errRules = NULL;

  ctx = INKCfgContextCreate(INK_FNAME_ICP_PEER);
  if (!ctx) {
    Debug("config", "[updateIcpConfig] can't allocate ctx memory");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // since we want to preserve comments, we need to read in the 
  // file using INKCfgContextGet and remove all the rules; starting from scratch
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY || INKCfgContextRemoveAll(ctx) != INK_ERR_OKAY) {
    Debug("config", "[updateIcpConfig] Failed to Get and Clear CfgContext");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // create Ele's by parsing the rules in the rules array 
  // insert the Ele's into a Cfg Context; if get invalid formatted rule, just skip it
  for (i = 0; i < numRules; i++) {
    tokens.Initialize(rules[i], ALLOW_EMPTY_TOKS);
    ele = INKIcpEleCreate();

    if (strlen(tokens[0]) <= 0 && strlen(tokens[1]) <= 0) {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateIcpConfig] invalid rule - SKIP");
    }
    // hostname
    if (strlen(tokens[0]) > 0) {
      ele->peer_hostname = xstrdup(tokens[0]);
    }
    // host_ip
    if (strlen(tokens[1]) > 0) {
      ele->peer_host_ip_addr = string_to_ip_addr(tokens[1]);
      if (!ele->peer_host_ip_addr) {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateIcpConfig] invalid host IP - SKIP");
      }
    }
    // peer type
    if (strcmp(tokens[2], "parent") == 0) {
      ele->peer_type = INK_ICP_PARENT;
    } else if (strcmp(tokens[2], "sibling") == 0) {
      ele->peer_type = INK_ICP_SIBLING;
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateIcpConfig] invalid cache type - SKIP");
    }

    // proxy_port
    if (strlen(tokens[3]) > 0 && isNumber(tokens[3])) {
      ele->peer_proxy_port = ink_atoi(tokens[3]);
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateIcpConfig]invalid proxy_port - SKIP");
    }

    // icp_port
    if (strlen(tokens[4]) > 0 && isNumber(tokens[4])) {
      ele->peer_icp_port = ink_atoi(tokens[4]);
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateIcpConfig]invalid icp_port - SKIP");
    }

    // mc_state
    if (strlen(tokens[5]) > 0) {
      if (strcmp(tokens[5], "on") == 0) {
        ele->is_multicast = true;
      } else {
        ele->is_multicast = false;
      }
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateIcpConfig] invalid mc state - SKIP");
    }

    // mc_ip
    if (strlen(tokens[6]) > 0) {
      ele->mc_ip_addr = string_to_ip_addr(tokens[6]);
      if (!ele->mc_ip_addr) {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateIcpConfig] invalid mc_ip - SKIP");
      }
    }
    // mc_ttl
    if (strlen(tokens[7]) > 0) {
      if (strcmp(tokens[7], "single subnet") == 0) {
        ele->mc_ttl = INK_MC_TTL_SINGLE_SUBNET;
      } else if (strcmp(tokens[7], "multiple subnets") == 0) {
        ele->mc_ttl = INK_MC_TTL_MULT_SUBNET;
      } else {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateIcpConfig] invalid mc_Ttl - SKIP");
      }
    }

    INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);     // add new ele to end of list     
  }

  // commit the CfgContext to write a new version of the file
  errRules = INKIntListCreate();
  response = INKCfgContextCommit(ctx, &action_need, errRules);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    err = WEB_HTTP_ERR_INVALID_CFG_RULE;
    *errBuff = convertRules(INK_FNAME_ICP_PEER, errRules, rules);
  } else if (response != INK_ERR_OKAY) {
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }

Lerror:
  if (errRules)
    INKIntListDestroy(errRules);
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}


//-------------------------------------------------------------------------
// updateIpAllowConfig
//-------------------------------------------------------------------------
int
updateIpAllowConfig(char *rules[], int numRules, char **errBuff)
{
  INKCfgContext ctx = NULL;
  INKIpAllowEle *ele;
  Tokenizer tokens(CFG_RULE_DELIMITER);
  INKActionNeedT action_need;
  INKError response;
  int i, err = WEB_HTTP_ERR_OKAY;
  INKIntList errRules = NULL;

  ctx = INKCfgContextCreate(INK_FNAME_IP_ALLOW);
  if (!ctx) {
    Debug("config", "[updateIpAllowConfig] can't allocate ctx memory");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // since we want to preserve comments, we need to read in the 
  // file using INKCfgContextGet and remove all the rules; starting from scratch
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY || INKCfgContextRemoveAll(ctx) != INK_ERR_OKAY) {
    Debug("config", "[updateIpAllowConfig] Failed to Get and Clear CfgContext");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // create Ele's by parsing the rules in the rules array 
  // insert the Ele's into a Cfg Context; if get invalid formatted rule, just skip it
  for (i = 0; i < numRules; i++) {
    tokens.Initialize(rules[i], ALLOW_EMPTY_TOKS);
    ele = INKIpAllowEleCreate();

    // src_ip
    if (strlen(tokens[0]) > 0) {
      ele->src_ip_addr = string_to_ip_addr_ele(tokens[0]);
    }
    // ip action 
    if (strlen(tokens[1]) > 0) {
      if (strcmp(tokens[1], "ip_allow") == 0) {
        ele->action = INK_IP_ALLOW_ALLOW;
      } else if (strcmp(tokens[1], "ip_deny") == 0) {
        ele->action = INK_IP_ALLOW_DENY;
      } else {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateIpAllowConfig] invalid rule - SKIP");
      }
    }

    INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);     // add new ele to end of list     
  }

  // commit the CfgContext to write a new version of the file
  errRules = INKIntListCreate();
  response = INKCfgContextCommit(ctx, &action_need, errRules);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    err = WEB_HTTP_ERR_INVALID_CFG_RULE;
    *errBuff = convertRules(INK_FNAME_IP_ALLOW, errRules, rules);
  } else if (response != INK_ERR_OKAY) {
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }

Lerror:
  if (errRules)
    INKIntListDestroy(errRules);
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}


//-------------------------------------------------------------------------
// updateMgmtAllowConfig
//-------------------------------------------------------------------------
int
updateMgmtAllowConfig(char *rules[], int numRules, char **errBuff)
{
  INKCfgContext ctx = NULL;
  INKMgmtAllowEle *ele;
  Tokenizer tokens(CFG_RULE_DELIMITER);
  INKActionNeedT action_need;
  INKError response;
  int i, err = WEB_HTTP_ERR_OKAY;
  INKIntList errRules = NULL;

  ctx = INKCfgContextCreate(INK_FNAME_MGMT_ALLOW);
  if (!ctx) {
    Debug("config", "[updateMgmtAllowConfig] can't allocate ctx memory");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // since we want to preserve comments, we need to read in the 
  // file using INKCfgContextGet and remove all the rules; starting from scratch
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY || INKCfgContextRemoveAll(ctx) != INK_ERR_OKAY) {
    Debug("config", "[updateMgmtAllowConfig] Failed to Get and Clear CfgContext");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // create Ele's by parsing the rules in the rules array 
  // insert the Ele's into a Cfg Context; if get invalid formatted rule, just skip it
  for (i = 0; i < numRules; i++) {
    tokens.Initialize(rules[i], ALLOW_EMPTY_TOKS);
    ele = INKMgmtAllowEleCreate();

    // src_ip
    if (strlen(tokens[0]) > 0) {
      ele->src_ip_addr = string_to_ip_addr_ele(tokens[0]);
    }
    // ip action 
    if (strlen(tokens[1]) > 0) {
      if (strcmp(tokens[1], "ip_allow") == 0) {
        ele->action = INK_MGMT_ALLOW_ALLOW;
      } else if (strcmp(tokens[1], "ip_deny") == 0) {
        ele->action = INK_MGMT_ALLOW_DENY;
      } else {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateMgmtAllowConfig] invalid rule - SKIP");
      }
    }

    INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);     // add new ele to end of list     
  }

  // commit the CfgContext to write a new version of the file
  errRules = INKIntListCreate();
  response = INKCfgContextCommit(ctx, &action_need, errRules);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    err = WEB_HTTP_ERR_INVALID_CFG_RULE;
    *errBuff = convertRules(INK_FNAME_MGMT_ALLOW, errRules, rules);
  } else if (response != INK_ERR_OKAY) {
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }

Lerror:
  if (errRules)
    INKIntListDestroy(errRules);
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}

//-------------------------------------------------------------------------
// updateNntpAccessConfig
//-------------------------------------------------------------------------
int
updateNntpAccessConfig(char *rules[], int numRules, char **errBuff)
{
  INKCfgContext ctx = NULL;
  INKNntpAccessEle *ele;
  Tokenizer tokens(CFG_RULE_DELIMITER);
  INKActionNeedT action_need;
  INKError response;
  int i, err = WEB_HTTP_ERR_OKAY;
  INKIntList errRules = NULL;

  ctx = INKCfgContextCreate(INK_FNAME_NNTP_ACCESS);
  if (!ctx) {
    Debug("config", "[updateNntpAccessConfig] can't allocate ctx memory");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // since we want to preserve comments, we need to read in the 
  // file using INKCfgContextGet and remove all the rules; starting from scratch
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY || INKCfgContextRemoveAll(ctx) != INK_ERR_OKAY) {
    Debug("config", "[updateNntpAccessConfig] Failed to Get and Clear CfgContext");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // create Ele's by parsing the rules in the rules array 
  // insert the Ele's into a Cfg Context; if get invalid formatted rule, just skip it
  for (i = 0; i < numRules; i++) {
    tokens.Initialize(rules[i], ALLOW_EMPTY_TOKS);
    ele = INKNntpAccessEleCreate();

    // group type
    if (strcmp(tokens[0], "ip") == 0) {
      ele->client_t = INK_CLIENT_GRP_IP;
    } else if (strcmp(tokens[0], "domain") == 0) {
      ele->client_t = INK_CLIENT_GRP_DOMAIN;
    } else if (strcmp(tokens[0], "hostname") == 0) {
      ele->client_t = INK_CLIENT_GRP_HOSTNAME;
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateNntpAccessConfig] invalid group type %s - SKIP", tokens[0]);
    }

    // clients
    if (strlen(tokens[1]) > 0) {
      ele->clients = xstrdup(tokens[1]);
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateNntpAccesConfig] invalid clients - SKIP");
    }

    // access type
    if (strcmp(tokens[2], "allow") == 0) {
      ele->access = INK_NNTP_ACC_ALLOW;
    } else if (strcmp(tokens[2], "deny") == 0) {
      ele->access = INK_NNTP_ACC_DENY;
    } else if (strcmp(tokens[2], "basic") == 0) {
      ele->access = INK_NNTP_ACC_BASIC;
    } else if (strcmp(tokens[2], "generic") == 0) {
      ele->access = INK_NNTP_ACC_GENERIC;
    } else if (strcmp(tokens[2], "custom") == 0) {
      ele->access = INK_NNTP_ACC_CUSTOM;
    } else {                    // a required field
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateNntpAccessConfig] invalid access type %s - SKIP", tokens[2]);
    }

    // authenticator
    if (strlen(tokens[3]) > 0) {
      ele->authenticator = xstrdup(tokens[3]);
    }
    // user
    if (strlen(tokens[4]) > 0) {
      ele->user = xstrdup(tokens[4]);
    }
    // pass
    if (strlen(tokens[5]) > 0) {
      ele->pass = xstrdup(tokens[5]);
    }
    // group wildmat list
    if (strlen(tokens[6]) > 0) {
      ele->group_wildmat = string_to_string_list(tokens[6], ",");
    }
    // deny posting
    if (strlen(tokens[7]) > 0) {
      ele->deny_posting = true;
    } else {
      ele->deny_posting = false;
    }

    INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);     // add new ele to end of list     
  }

  // commit the CfgContext to write a new version of the file
  errRules = INKIntListCreate();
  response = INKCfgContextCommit(ctx, &action_need, errRules);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    err = WEB_HTTP_ERR_INVALID_CFG_RULE;
    *errBuff = convertRules(INK_FNAME_NNTP_ACCESS, errRules, rules);
  } else if (response != INK_ERR_OKAY) {
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }

Lerror:
  if (errRules)
    INKIntListDestroy(errRules);
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}

//-------------------------------------------------------------------------
// updateNntpServersConfig
//-------------------------------------------------------------------------
int
updateNntpServersConfig(char *rules[], int numRules, char **errBuff)
{
  INKCfgContext ctx = NULL;
  INKNntpSrvrEle *ele;
  Tokenizer tokens(CFG_RULE_DELIMITER);
  INKActionNeedT action_need;
  INKError response;
  int i, err = WEB_HTTP_ERR_OKAY;
  INKIntList errRules = NULL;

  ctx = INKCfgContextCreate(INK_FNAME_NNTP_SERVERS);
  if (!ctx) {
    Debug("config", "[updateNntpServersConfig] can't allocate ctx memory");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // since we want to preserve comments, we need to read in the 
  // file using INKCfgContextGet and remove all the rules; starting from scratch
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY || INKCfgContextRemoveAll(ctx) != INK_ERR_OKAY) {
    Debug("config", "[updateNntpServersConfig] Failed to Get and Clear CfgContext");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // create Ele's by parsing the rules in the rules array 
  // insert the Ele's into a Cfg Context; if get invalid formatted rule, just skip it
  for (i = 0; i < numRules; i++) {
    tokens.Initialize(rules[i], ALLOW_EMPTY_TOKS);
    ele = INKNntpSrvrEleCreate();

    // hostname
    if (strlen(tokens[0]) > 0) {
      ele->hostname = xstrdup(tokens[0]);
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateNntpAccessConfig] invalid hostname - SKIP");
    }

    // group wildmat list
    if (strlen(tokens[1]) > 0) {
      ele->group_wildmat = string_to_string_list(tokens[1], ",");
    }
    // treatment type
    if (strcmp(tokens[2], "feed") == 0) {
      ele->treatment = INK_NNTP_TRMT_FEED;
    } else if (strcmp(tokens[2], "push") == 0) {
      ele->treatment = INK_NNTP_TRMT_PUSH;
    } else if (strcmp(tokens[2], "pull") == 0) {
      ele->treatment = INK_NNTP_TRMT_PULL;
    } else if (strcmp(tokens[2], "pullover") == 0) {
      ele->treatment = INK_NNTP_TRMT_PULLOVER;
    } else if (strcmp(tokens[2], "dynamic") == 0) {
      ele->treatment = INK_NNTP_TRMT_DYNAMIC;
    } else if (strcmp(tokens[2], "post") == 0) {
      ele->treatment = INK_NNTP_TRMT_POST;
    } else {
      ele->treatment = INK_NNTP_TRMT_UNDEFINED;
    }

    // priority
    if (strlen(tokens[3]) > 0) {
      if (isNumber(tokens[3])) {
        ele->priority = ink_atoi(tokens[3]);
      } else {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateNntpServersConfig] invalid treatment/priority - SKIP");
      }
      if (ele->treatment != INK_NNTP_TRMT_UNDEFINED && ele->priority > 0) {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateNntpServersConfig] invalid treatment/priority - SKIP");
      }
    } else {
      ele->priority = 0;
    }

    // interface
    if (strlen(tokens[4]) > 0) {
      ele->interface = xstrdup(tokens[4]);
    }

    INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);     // add new ele to end of list     
  }

  // commit the CfgContext to write a new version of the file
  errRules = INKIntListCreate();
  response = INKCfgContextCommit(ctx, &action_need, errRules);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    err = WEB_HTTP_ERR_INVALID_CFG_RULE;
    *errBuff = convertRules(INK_FNAME_NNTP_SERVERS, errRules, rules);
  } else if (response != INK_ERR_OKAY) {
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }

Lerror:
  if (errRules)
    INKIntListDestroy(errRules);
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}

//-------------------------------------------------------------------------
// updateParentConfig
//-------------------------------------------------------------------------
int
updateParentConfig(char *rules[], int numRules, char **errBuff)
{
  INKCfgContext ctx = NULL;
  INKParentProxyEle *ele;
  Tokenizer tokens(CFG_RULE_DELIMITER);
  INKPdSsFormat *pdss;
  INKActionNeedT action_need;
  INKError response;
  int i, err = WEB_HTTP_ERR_OKAY;
  INKIntList errRules = NULL;

  ctx = INKCfgContextCreate(INK_FNAME_PARENT_PROXY);
  if (!ctx) {
    Debug("config", "[updateParentConfig] can't allocate ctx memory");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // since we want to preserve comments, we need to read in the 
  // file using INKCfgContextGet and remove all the rules; starting from scratch
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY || INKCfgContextRemoveAll(ctx) != INK_ERR_OKAY) {
    Debug("config", "[updateParentConfig] Failed to Get and Clear CfgContext");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // create Ele's by parsing the rules in the rules array 
  // insert the Ele's into a Cfg Context; if get invalid formatted rule, just skip it
  for (i = 0; i < numRules; i++) {
    tokens.Initialize(rules[i], ALLOW_EMPTY_TOKS);
    ele = INKParentProxyEleCreate(INK_TYPE_UNDEFINED);
    pdss = &(ele->parent_info);


    // pd type
    if (strcmp(tokens[0], "dest_domain") == 0) {
      pdss->pd_type = INK_PD_DOMAIN;
    } else if (strcmp(tokens[0], "dest_host") == 0) {
      pdss->pd_type = INK_PD_HOST;
    } else if (strcmp(tokens[0], "dest_ip") == 0) {
      pdss->pd_type = INK_PD_IP;
    } else if (strcmp(tokens[0], "url_regex") == 0) {
      pdss->pd_type = INK_PD_URL_REGEX;
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateParentConfig] invalid prime dest type - SKIP");
    }

    // pd value - Required field!
    if (strlen(tokens[1]) > 0) {
      pdss->pd_val = xstrdup(tokens[1]);
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateParentConfig] invalid prime dest value - SKIP");
    }


    // FIXME: lots of parsing and conversion to do - similar to CfgContextUtils.cc
    // secondary specifiers

    // time    
    if (strlen(tokens[2]) > 0) {
      if (string_to_time_struct(tokens[2], &(pdss->sec_spec)) != INK_ERR_OKAY) {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateParentConfig] invalid time sec spec. - SKIP");
      }
    }
    // src_ip
    if (strlen(tokens[3]) > 0) {
      pdss->sec_spec.src_ip = string_to_ip_addr(tokens[3]);
      if (!pdss->sec_spec.src_ip) {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateParentConfig] invalid src_ip - SKIP");
      }
    }
    // prefix
    if (strlen(tokens[4]) > 0) {
      pdss->sec_spec.prefix = xstrdup(tokens[4]);
    }
    // suffix
    if (strlen(tokens[5]) > 0) {
      pdss->sec_spec.suffix = xstrdup(tokens[5]);
    }
    // port
    if (strlen(tokens[6]) > 0) {
      pdss->sec_spec.port = string_to_port_ele(tokens[6]);
      if (!pdss->sec_spec.port) {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateParentConfig] invalid port - SKIP");
      }
    }
    // method
    if (strlen(tokens[7]) > 0) {
      pdss->sec_spec.method = string_to_method_type(tokens[7]);
    }
    // scheme
    if (strlen(tokens[8]) > 0) {
      pdss->sec_spec.scheme = string_to_scheme_type(tokens[8]);
    }
    // mixt tag
    if (strlen(tokens[9]) > 0) {
      pdss->sec_spec.mixt = string_to_mixt_type(tokens[9]);
    }
    // parents
    if (strlen(tokens[10]) > 0) {
      ele->proxy_list = string_to_domain_list(tokens[10], ";");
      if (!ele->proxy_list) {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateParentConfig] invalid parent proxies - SKIP");
      }
      ele->cfg_ele.type = INK_PP_PARENT;
    }
    // round robin type
    if (strlen(tokens[11]) > 0) {
      if (strcmp(tokens[11], "true") == 0) {
        ele->rr = INK_RR_TRUE;
      } else if (strcmp(tokens[11], "strict") == 0) {
        ele->rr = INK_RR_STRICT;
      } else if (strcmp(tokens[11], "false") == 0) {
        ele->rr = INK_RR_FALSE;
      } else {
        ele->rr = INK_RR_NONE;
      }
    } else {
      ele->rr = INK_RR_NONE;
    }

    // go direct
    if (strlen(tokens[12]) > 0) {
      if (strcmp(tokens[12], "true") == 0) {
        ele->direct = true;
      } else {
        ele->direct = false;
      }
    }
    // if no parents specified, must be a GO_DIRECT rule type
    if (ele->proxy_list) {
      ele->cfg_ele.type = INK_PP_PARENT;
    } else {
      ele->cfg_ele.type = INK_PP_GO_DIRECT;
    }

    INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);     // add new ele to end of list

  }

  // commit the CfgContext to write a new version of the file
  errRules = INKIntListCreate();
  response = INKCfgContextCommit(ctx, &action_need, errRules);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    err = WEB_HTTP_ERR_INVALID_CFG_RULE;
    *errBuff = convertRules(INK_FNAME_PARENT_PROXY, errRules, rules);
  } else if (response != INK_ERR_OKAY) {
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }

Lerror:
  if (errRules)
    INKIntListDestroy(errRules);
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}

//-------------------------------------------------------------------------
// updatePartitionConfig
//-------------------------------------------------------------------------
int
updatePartitionConfig(char *rules[], int numRules, char **errBuff)
{
  INKCfgContext ctx = NULL;
  INKPartitionEle *ele;
  Tokenizer tokens(CFG_RULE_DELIMITER);
  INKActionNeedT action_need;
  INKError response = INK_ERR_OKAY;
  int i, err = WEB_HTTP_ERR_OKAY;
  INKIntList errRules = NULL;

  ctx = INKCfgContextCreate(INK_FNAME_PARTITION);
  if (!ctx) {
    Debug("config", "[updatePartitionConfig] can't allocate ctx memory");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // since we want to preserve comments, we need to read in the 
  // file using INKCfgContextGet and remove all the rules; starting from scratch
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY || INKCfgContextRemoveAll(ctx) != INK_ERR_OKAY) {
    Debug("config", "[updatePartitionConfig] Failed to Get and Clear CfgContext");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // create Ele's by parsing the rules in the rules array 
  // insert the Ele's into a Cfg Context; if get invalid formatted rule, just skip it
  for (i = 0; i < numRules; i++) {
    tokens.Initialize(rules[i], ALLOW_EMPTY_TOKS);
    ele = INKPartitionEleCreate();

    // partition number
    if (strlen(tokens[0]) > 0 && isNumber(tokens[0])) {
      ele->partition_num = ink_atoi(tokens[0]);
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updatePartitionConfig] invalid partition number - SKIP");
    }

    // scheme
    if (strcmp(tokens[1], "http") == 0) {
      ele->scheme = INK_PARTITION_HTTP;
    } else if (strcmp(tokens[1], "mixt") == 0) {
      ele->scheme = INK_PARTITION_MIXT;
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updatePartitionConfig] invalid scheme - SKIP");
    }

    // size
    if (strlen(tokens[2]) > 0 && isNumber(tokens[2])) {
      ele->partition_size = ink_atoi(tokens[2]);
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updatePartitionConfig] invalid size - SKIP");
    }

    // size format
    if (strcmp(tokens[3], "percent") == 0) {
      ele->size_format = INK_SIZE_FMT_PERCENT;
    } else if (strcmp(tokens[3], "absolute") == 0) {
      ele->size_format = INK_SIZE_FMT_ABSOLUTE;
    } else {                    // a required field
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updatePartitionConfig] invalid size format - SKIP");
    }

    INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);     // add new ele to end of list     
  }

  // commit the CfgContext to write a new version of the file
  errRules = INKIntListCreate();
  response = INKCfgContextCommit(ctx, &action_need, errRules);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    err = WEB_HTTP_ERR_INVALID_CFG_RULE;
    *errBuff = convertRules(INK_FNAME_PARTITION, errRules, rules);
  } else if (response != INK_ERR_OKAY) {
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }

Lerror:
  if (errRules)
    INKIntListDestroy(errRules);
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}

//-------------------------------------------------------------------------
// updateRemapConfig
//-------------------------------------------------------------------------
int
updateRemapConfig(char *rules[], int numRules, char **errBuff)
{
  INKCfgContext ctx = NULL;
  INKRemapEle *ele;
  Tokenizer tokens(CFG_RULE_DELIMITER);
  INKActionNeedT action_need;
  INKError response;
  int i, err = WEB_HTTP_ERR_OKAY;
  INKIntList errRules = NULL;

  ctx = INKCfgContextCreate(INK_FNAME_REMAP);
  if (!ctx) {
    Debug("config", "[updateRemapConfig] can't allocate ctx memory");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // since we want to preserve comments, we need to read in the 
  // file using INKCfgContextGet and remove all the rules; starting from scratch
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY || INKCfgContextRemoveAll(ctx) != INK_ERR_OKAY) {
    Debug("config", "[updateRemapConfig] Failed to Get and Clear CfgContext");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // create Ele's by parsing the rules in the rules array 
  // insert the Ele's into a Cfg Context; if get invalid formatted rule, just skip it
  for (i = 0; i < numRules; i++) {
    tokens.Initialize(rules[i], ALLOW_EMPTY_TOKS);
    ele = INKRemapEleCreate(INK_TYPE_UNDEFINED);

    // rule type
    if (strcmp(tokens[0], "map") == 0) {
      ele->cfg_ele.type = INK_REMAP_MAP;
    } else if (strcmp(tokens[0], "reverse_map") == 0) {
      ele->cfg_ele.type = INK_REMAP_REVERSE_MAP;
    } else if (strcmp(tokens[0], "redirect") == 0) {
      ele->cfg_ele.type = INK_REMAP_REDIRECT;
    } else if (strcmp(tokens[0], "redirect_temporary") == 0) {
      ele->cfg_ele.type = INK_REMAP_REDIRECT_TEMP;
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateRemapConfig] invalid rule type - SKIP");
    }

    // from scheme
    if (strlen(tokens[1]) > 0) {
      ele->from_scheme = string_to_scheme_type(tokens[1]);
      if (ele->from_scheme == INK_SCHEME_UNDEFINED) {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateRemapConfig] invalid scheme - SKIP");
      }
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateRemapConfig] invalid scheme - SKIP");
    }

    // from host 
    if (strlen(tokens[2]) > 0) {
      ele->from_host = xstrdup(tokens[2]);
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateRemapConfig] invalid src host - SKIP");
    }

    // from port
    if (strlen(tokens[3]) > 0) {
      if (isNumber(tokens[3])) {
        ele->from_port = ink_atoi(tokens[3]);
      } else {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateRemapConfig] invalid src port - SKIP");
      }
    }
    // from path prefix
    if (strlen(tokens[4]) > 0) {
      ele->from_path_prefix = xstrdup(tokens[4]);
    }
    // to scheme
    if (strlen(tokens[5]) > 0) {
      ele->to_scheme = string_to_scheme_type(tokens[5]);
      if (ele->to_scheme == INK_SCHEME_UNDEFINED) {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateRemapConfig] invalid scheme - SKIP");
      }
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateRemapConfig] invalid scheme - SKIP");
    }

    // to host 
    if (strlen(tokens[6]) > 0) {
      ele->to_host = xstrdup(tokens[6]);
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateRemapConfig] invalid dest host - SKIP");
    }

    // to port
    if (strlen(tokens[7]) > 0) {
      if (isNumber(tokens[7])) {
        ele->to_port = ink_atoi(tokens[7]);
      } else {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateRemapConfig] invalid dest port - SKIP");
      }
    }
    // to path prefix
    if (strlen(tokens[8]) > 0) {
      ele->to_path_prefix = xstrdup(tokens[8]);
    }
    // mixt tag
    if (strlen(tokens[9]) > 0) {
      ele->mixt = string_to_mixt_type(tokens[9]);
    }

    INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);     // add new ele to end of list
  }

  // commit the CfgContext to write a new version of the file
  errRules = INKIntListCreate();
  response = INKCfgContextCommit(ctx, &action_need, errRules);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    err = WEB_HTTP_ERR_INVALID_CFG_RULE;
    *errBuff = convertRules(INK_FNAME_REMAP, errRules, rules);
  } else if (response != INK_ERR_OKAY) {
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }

Lerror:
  if (errRules)
    INKIntListDestroy(errRules);
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}

//-------------------------------------------------------------------------
// updateSocksConfig
//-------------------------------------------------------------------------
int
updateSocksConfig(char *rules[], int numRules, char **errBuff)
{
  INKCfgContext ctx = NULL;
  INKSocksEle *ele;
  Tokenizer tokens(CFG_RULE_DELIMITER);
  INKActionNeedT action_need;
  INKError response;
  int i, err = WEB_HTTP_ERR_OKAY;
  INKIntList errRules = NULL;

  ctx = INKCfgContextCreate(INK_FNAME_SOCKS);
  if (!ctx) {
    Debug("config", "[updateSocksConfig] can't allocate ctx memory");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // since we want to preserve comments, we need to read in the 
  // file using INKCfgContextGet and remove all the rules; starting from scratch
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY || INKCfgContextRemoveAll(ctx) != INK_ERR_OKAY) {
    Debug("config", "[updateSocksConfig] Failed to Get and Clear CfgContext");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // create Ele's by parsing the rules in the rules array 
  // insert the Ele's into a Cfg Context; if get invalid formatted rule, just skip it
  for (i = 0; i < numRules; i++) {
    tokens.Initialize(rules[i], ALLOW_EMPTY_TOKS);
    ele = INKSocksEleCreate(INK_TYPE_UNDEFINED);

    // rule type
    if (strcmp(tokens[0], "no_socks") == 0) {
      ele->cfg_ele.type = INK_SOCKS_BYPASS;
    } else if (strcmp(tokens[0], "auth") == 0) {
      ele->cfg_ele.type = INK_SOCKS_AUTH;
    } else if (strcmp(tokens[0], "multiple_socks") == 0) {
      ele->cfg_ele.type = INK_SOCKS_MULTIPLE;
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateSocksConfig] invalid rule type - SKIP");
    }

    // dest_ip
    if (strlen(tokens[1]) > 0) {
      switch (ele->cfg_ele.type) {
      case INK_SOCKS_BYPASS:
        ele->ip_addrs = string_to_ip_addr_list(tokens[1], ",");
        break;
      case INK_SOCKS_MULTIPLE:
        ele->dest_ip_addr = string_to_ip_addr_ele(tokens[1]);
        break;
      default:
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateSocksConfig] invalid rule - SKIP");
      }
    }
    // username
    if (strlen(tokens[2]) > 0) {
      ele->username = xstrdup(tokens[2]);
    }
    // password
    if (strlen(tokens[3]) > 0) {
      ele->password = xstrdup(tokens[3]);
    }
    // socks servers
    if (strlen(tokens[4]) > 0) {
      ele->socks_servers = string_to_domain_list(tokens[4], ";");
    }
    // round robin
    if (strlen(tokens[5]) > 0) {
      if (strcmp(tokens[5], "true") == 0) {
        ele->rr = INK_RR_TRUE;
      } else if (strcmp(tokens[5], "strict") == 0) {
        ele->rr = INK_RR_STRICT;
      } else if (strcmp(tokens[5], "false") == 0) {
        ele->rr = INK_RR_FALSE;
      } else {
        ele->rr = INK_RR_NONE;
      }
    } else {
      ele->rr = INK_RR_NONE;
    }

    INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);     // add new ele to end of list
  }

  // commit the CfgContext to write a new version of the file
  errRules = INKIntListCreate();
  response = INKCfgContextCommit(ctx, &action_need, errRules);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    err = WEB_HTTP_ERR_INVALID_CFG_RULE;
    *errBuff = convertRules(INK_FNAME_SOCKS, errRules, rules);
  } else if (response != INK_ERR_OKAY) {
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }

Lerror:
  if (errRules)
    INKIntListDestroy(errRules);
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}


//-------------------------------------------------------------------------
// updateSplitDnsConfig
//-------------------------------------------------------------------------
int
updateSplitDnsConfig(char *rules[], int numRules, char **errBuff)
{
  INKCfgContext ctx = NULL;
  INKSplitDnsEle *ele;
  Tokenizer tokens(CFG_RULE_DELIMITER);
  INKActionNeedT action_need;
  INKError response;
  int i, err = WEB_HTTP_ERR_OKAY;
  INKIntList errRules = NULL;

  ctx = INKCfgContextCreate(INK_FNAME_SPLIT_DNS);
  if (!ctx) {
    Debug("config", "[updateSplitDnsConfig] can't allocate ctx memory");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // since we want to preserve comments, we need to read in the 
  // file using INKCfgContextGet and remove all the rules; starting from scratch
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY || INKCfgContextRemoveAll(ctx) != INK_ERR_OKAY) {
    Debug("config", "[updateSplitDnsConfig] Failed to Get and Clear CfgContext");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // create Ele's by parsing the rules in the rules array 
  // insert the Ele's into a Cfg Context; if get invalid formatted rule, just skip it
  for (i = 0; i < numRules; i++) {
    tokens.Initialize(rules[i], ALLOW_EMPTY_TOKS);
    ele = INKSplitDnsEleCreate();

    // pd type
    if (strcmp(tokens[0], "dest_domain") == 0) {
      ele->pd_type = INK_PD_DOMAIN;
    } else if (strcmp(tokens[0], "dest_host") == 0) {
      ele->pd_type = INK_PD_HOST;
    } else if (strcmp(tokens[0], "dest_ip") == 0) {
      ele->pd_type = INK_PD_IP;
    } else if (strcmp(tokens[0], "url_regex") == 0) {
      ele->pd_type = INK_PD_URL_REGEX;
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateSplitDnsConfig] invalid rule - SKIP");
    }

    // pd value
    if (strlen(tokens[1]) > 0) {
      ele->pd_val = xstrdup(tokens[1]);
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateSplitDnsConfig] invalid rule - SKIP");
    }

    // dns servers ip's
    if (strlen(tokens[2]) > 0) {
      ele->dns_servers_addrs = string_to_domain_list(tokens[2], "; ");
    } else {                    // a required field
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateSplitDnsConfig] invalid rule - SKIP");
    }

    // def_domain
    if (strlen(tokens[3]) > 0) {
      ele->def_domain = xstrdup(tokens[3]);
    }
    // search list
    if (strlen(tokens[4]) > 0) {
      ele->search_list = string_to_domain_list(tokens[4], "; ");
      if (!ele->search_list) {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateSplitDnsConfig] invalid rule - SKIP");
      }
    }

    INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);     // add new ele to end of list     
  }

  // commit the CfgContext to write a new version of the file
  errRules = INKIntListCreate();
  response = INKCfgContextCommit(ctx, &action_need, errRules);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    err = WEB_HTTP_ERR_INVALID_CFG_RULE;
    *errBuff = convertRules(INK_FNAME_SPLIT_DNS, errRules, rules);
  } else if (response != INK_ERR_OKAY) {
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }

Lerror:
  if (errRules)
    INKIntListDestroy(errRules);
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}

//-------------------------------------------------------------------------
// updateUpdateConfig
//-------------------------------------------------------------------------
int
updateUpdateConfig(char *rules[], int numRules, char **errBuff)
{
  INKCfgContext ctx = NULL;
  INKUpdateEle *ele;
  Tokenizer tokens(CFG_RULE_DELIMITER);
  INKActionNeedT action_need;
  INKError response;
  int i, err = WEB_HTTP_ERR_OKAY;
  INKIntList errRules = NULL;

  ctx = INKCfgContextCreate(INK_FNAME_UPDATE_URL);
  if (!ctx) {
    Debug("config", "[updateUpdateConfig] can't allocate ctx memory");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // since we want to preserve comments, we need to read in the 
  // file using INKCfgContextGet and remove all the rules; starting from scratch
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY || INKCfgContextRemoveAll(ctx) != INK_ERR_OKAY) {
    Debug("config", "[updateUpdateConfig] Failed to Get and Clear CfgContext");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // create Ele's by parsing the rules in the rules array 
  // insert the Ele's into a Cfg Context; if get invalid formatted rule, just skip it
  for (i = 0; i < numRules; i++) {
    tokens.Initialize(rules[i], ALLOW_EMPTY_TOKS);
    ele = INKUpdateEleCreate();

    // url
    if (strlen(tokens[0]) > 0) {
      ele->url = xstrdup(tokens[0]);
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateUpdateConfig] invalid url - SKIP");
    }

    // headers
    if (strlen(tokens[1]) > 0) {
      ele->headers = string_to_string_list(tokens[1], ";");
    }
    // offset hour
    if (strlen(tokens[2]) > 0 && isNumber(tokens[2])) {
      ele->offset_hour = ink_atoi(tokens[2]);
    } else {                    // a required field
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateUpdateConfig] invalid offset hour- SKIP");
    }

    // interval
    if (strlen(tokens[3]) > 0 && isNumber(tokens[3])) {
      ele->interval = ink_atoi(tokens[3]);
    } else {                    // a required field
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateUpdateConfig] invalid interval - SKIP");
    }

    // recursion depth
    if (strlen(tokens[4]) > 0) {
      if (isNumber(tokens[4])) {
        ele->recursion_depth = ink_atoi(tokens[4]);
      } else {
        ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
        Debug("config", "[updateRemapConfig] invalid recursion depth - SKIP");
      }
    }

    INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);     // add new ele to end of list     
  }

  // commit the CfgContext to write a new version of the file
  errRules = INKIntListCreate();
  response = INKCfgContextCommit(ctx, &action_need, errRules);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    err = WEB_HTTP_ERR_INVALID_CFG_RULE;
    *errBuff = convertRules(INK_FNAME_UPDATE_URL, errRules, rules);
  } else if (response != INK_ERR_OKAY) {
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }

Lerror:
  if (errRules)
    INKIntListDestroy(errRules);
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}


//-------------------------------------------------------------------------
// updateVaddrsConfig
//-------------------------------------------------------------------------
int
updateVaddrsConfig(char *rules[], int numRules, char **errBuff)
{
  INKCfgContext ctx = NULL;
  INKVirtIpAddrEle *ele;
  Tokenizer tokens(CFG_RULE_DELIMITER);
  INKActionNeedT action_need;
  INKError response;
  int i, err = WEB_HTTP_ERR_OKAY;
  INKIntList errRules = NULL;

  ctx = INKCfgContextCreate(INK_FNAME_VADDRS);
  if (!ctx) {
    Debug("config", "[updateVaddrsConfig] can't allocate ctx memory");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // since we want to preserve comments, we need to read in the 
  // file using INKCfgContextGet and remove all the rules; starting from scratch
  if (INKCfgContextGet(ctx) != INK_ERR_OKAY || INKCfgContextRemoveAll(ctx) != INK_ERR_OKAY) {
    Debug("config", "[updateVaddrsConfig] Failed to Get and Clear CfgContext");
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }
  // create Ele's by parsing the rules in the rules array 
  // insert the Ele's into a Cfg Context; if get invalid formatted rule, just skip it
  for (i = 0; i < numRules; i++) {
    tokens.Initialize(rules[i], ALLOW_EMPTY_TOKS);
    ele = INKVirtIpAddrEleCreate();

    // virtual IP
    if (strlen(tokens[0]) > 0) {
      ele->ip_addr = string_to_ip_addr(tokens[0]);
    } else {
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateVaddrsConfig] invalid Virtual Ip Addr - SKIP");
    }

    // interface
    if (strlen(tokens[1]) > 0) {
      ele->intr = xstrdup(tokens[1]);
    } else {                    // a required field
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateVaddrsConfig] invalid interface- SKIP");
    }

    // sub-interface
    if (strlen(tokens[2]) > 0 && isNumber(tokens[2])) {
      ele->sub_intr = ink_atoi(tokens[2]);
    } else {                    // a required field
      ele->cfg_ele.error = INK_ERR_INVALID_CONFIG_RULE;
      Debug("config", "[updateVaddrsConfig] invalid sub-interface - SKIP");
    }

    INKCfgContextAppendEle(ctx, (INKCfgEle *) ele);     // add new ele to end of list     
  }

  // commit the CfgContext to write a new version of the file
  errRules = INKIntListCreate();
  response = INKCfgContextCommit(ctx, &action_need, errRules);
  if (response == INK_ERR_INVALID_CONFIG_RULE) {
    err = WEB_HTTP_ERR_INVALID_CFG_RULE;
    *errBuff = convertRules(INK_FNAME_VADDRS, errRules, rules);
  } else if (response != INK_ERR_OKAY) {
    err = WEB_HTTP_ERR_FAIL;
    goto Lerror;
  }

Lerror:
  if (errRules)
    INKIntListDestroy(errRules);
  if (ctx)
    INKCfgContextDestroy(ctx);
  return err;
}
