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
 *
 *  WebConfigRender.h - html rendering and assembly for the Configuration
 *                      File Editor
 *
 * 
 ****************************************************************************/

#ifndef _WEB_CONFIG_RENDER_H_
#define _WEB_CONFIG_RENDER_H_

#include "TextBuffer.h"
#include "WebHttpContext.h"

#include "P_RecCore.h"

int writeCacheConfigTable(WebHttpContext * whc);
int writeCacheRuleList(textBuffer * output);
int writeCacheConfigForm(WebHttpContext * whc);

int writeFilterConfigTable(WebHttpContext * whc);
int writeFilterRuleList(textBuffer * output);
int writeFilterConfigForm(WebHttpContext * whc);

int writeFtpRemapConfigTable(WebHttpContext * whc);
int writeFtpRemapRuleList(textBuffer * output);
int writeFtpRemapConfigForm(WebHttpContext * whc);

int writeHostingConfigTable(WebHttpContext * whc);
int writeHostingRuleList(textBuffer * output);
int writeHostingConfigForm(WebHttpContext * whc);

int writeIcpConfigTable(WebHttpContext * whc);
int writeIcpRuleList(textBuffer * output);
int writeIcpConfigForm(WebHttpContext * whc);

int writeIpAllowConfigTable(WebHttpContext * whc);
int writeIpAllowRuleList(textBuffer * output);
int writeIpAllowConfigForm(WebHttpContext * whc);

int writeMgmtAllowConfigTable(WebHttpContext * whc);
int writeMgmtAllowRuleList(textBuffer * output);
int writeMgmtAllowConfigForm(WebHttpContext * whc);

int writeNntpAccessConfigTable(WebHttpContext * whc);
int writeNntpAccessRuleList(textBuffer * output);
int writeNntpAccessConfigForm(WebHttpContext * whc);

int writeNntpServersConfigTable(WebHttpContext * whc);
int writeNntpServersRuleList(textBuffer * output);
int writeNntpServersConfigForm(WebHttpContext * whc);

int writeParentConfigTable(WebHttpContext * whc);
int writeParentRuleList(textBuffer * output);
int writeParentConfigForm(WebHttpContext * whc);

int writePartitionConfigTable(WebHttpContext * whc);
int writePartitionRuleList(textBuffer * output);
int writePartitionConfigForm(WebHttpContext * whc);

int writeRemapConfigTable(WebHttpContext * whc);
int writeRemapRuleList(textBuffer * output);
int writeRemapConfigForm(WebHttpContext * whc);

int writeSocksConfigTable(WebHttpContext * whc);
int writeSocksRuleList(textBuffer * output);
int writeSocksConfigForm(WebHttpContext * whc);

int writeSplitDnsConfigTable(WebHttpContext * whc);
int writeSplitDnsRuleList(textBuffer * output);
int writeSplitDnsConfigForm(WebHttpContext * whc);

int writeUpdateConfigTable(WebHttpContext * whc);
int writeUpdateRuleList(textBuffer * output);
int writeUpdateConfigForm(WebHttpContext * whc);

int writeVaddrsConfigTable(WebHttpContext * whc);
int writeVaddrsRuleList(textBuffer * output);
int writeVaddrsConfigForm(WebHttpContext * whc);

int writeSecondarySpecsForm(WebHttpContext * whc, INKFileNameT file);
int writeSecondarySpecsTableElem(textBuffer * output, char *time, char *src_ip, char *prefix, char *suffix, char *port,
                                 char *method, char *scheme, char *mixt);


// -------------------- CONVERSION FUNCTIONS ------------------------------

int convert_cache_ele_to_html_format(INKCacheEle * ele,
                                     char *ruleType,
                                     char *pdType,
                                     char *time,
                                     char *src_ip,
                                     char *prefix,
                                     char *suffix,
                                     char *port, char *method, char *scheme, char *time_period, char *mixt);

int convert_filter_ele_to_html_format(INKFilterEle * ele,
                                      char *ruleType,
                                      char *pdType,
                                      char *time,
                                      char *src_ip,
                                      char *prefix,
                                      char *suffix,
                                      char *port,
                                      char *method,
                                      char *scheme,
                                      char *hdr_type, char *server, char *dn, char *realm, char *uid_filter,
                                      char *attr_name, char *attr_val, char *redirect_url, char *bind_dn,
                                      char *bind_pwd_file, char *mixt);

int convert_ftp_remap_ele_to_html_format(INKFtpRemapEle * ele, char *from_port, char *to_port);

int convert_hosting_ele_to_html_format(INKHostingEle * ele, char *pdType, char *partitions);

int convert_icp_ele_to_html_format(INKIcpEle * ele,
                                   char *name,
                                   char *host_ip,
                                   char *peer_type,
                                   char *proxy_port, char *icp_port, char *mc_state, char *mc_ip, char *mc_ttl);

int convert_ip_allow_ele_to_html_format(INKIpAllowEle * ele, char *src_ip, char *action);

int convert_mgmt_allow_ele_to_html_format(INKMgmtAllowEle * ele, char *src_ip, char *action);

int convert_nntp_access_ele_to_html_format(INKNntpAccessEle * ele,
                                           char *grp_type,
                                           char *access, char *auth, char *user, char *pass, char *groups, char *post);

int convert_nntp_srvr_ele_to_html_format(INKNntpSrvrEle * ele,
                                         char *groups, char *treatment, char *priority, char *intr);

int convert_parent_ele_to_html_format(INKParentProxyEle * ele,
                                      char *pdType,
                                      char *time,
                                      char *src_ip,
                                      char *prefix,
                                      char *suffix,
                                      char *port,
                                      char *method,
                                      char *scheme, char *mixt, char *parents, char *round_robin, char *direct);

int convert_partition_ele_to_html_format(INKPartitionEle * ele,
                                         char *part_num, char *scheme, char *size, char *size_fmt);

int convert_remap_ele_to_html_format(INKRemapEle * ele,
                                     char *rule_type,
                                     char *from_scheme, char *from_port, char *from_path,
                                     char *to_scheme, char *to_port, char *to_path, char *mixt);

int convert_socks_ele_to_html_format(INKSocksEle * ele,
                                     char *rule_type, char *dest_ip, char *user, char *passwd, char *servers, char *rr);

int convert_split_dns_ele_to_html_format(INKSplitDnsEle * ele,
                                         char *pdType, char *dns_server, char *def_domain, char *search_list);

int convert_update_ele_to_html_format(INKUpdateEle * ele, char *hdrs, char *offset, char *interval, char *depth);

int convert_virt_ip_addr_ele_to_html_format(INKVirtIpAddrEle * ele, char *ip, char *sub_intr);

int convert_pdss_to_html_format(INKPdSsFormat info,
                                char *pdType,
                                char *time,
                                char *src_ip,
                                char *prefix, char *suffix, char *port, char *method, char *scheme, char *mixt);

//------------------------- SELECT FUNCTIONS ------------------------------

void writeRuleTypeSelect_arm(textBuffer * html, char *listName);
void writeRuleTypeSelect_cache(textBuffer * html, char *listName);
void writeRuleTypeSelect_filter(textBuffer * html, char *listName);
void writeRuleTypeSelect_remap(textBuffer * html, char *listName);
void writeRuleTypeSelect_socks(textBuffer * html, char *listName);
void writeRuleTypeSelect_bypass(textBuffer * html, char *listName);
void writeConnTypeSelect(textBuffer * html, char *listName);
void writeIpActionSelect(textBuffer * html, char *listName);
void writePdTypeSelect(textBuffer * html, char *listName);
void writePdTypeSelect_hosting(textBuffer * html, char *listName);
void writePdTypeSelect_splitdns(textBuffer * html, char *listName);
void writeMethodSelect(textBuffer * html, char *listName);
void writeMethodSelect_push(textBuffer * html, char *listName);
void writeSchemeSelect(textBuffer * html, char *listName);
void writeSchemeSelect_partition(textBuffer * html, char *listName);
void writeSchemeSelect_remap(textBuffer * html, char *listName);
void writeMixtSelect(textBuffer * html, char *listName);
void writeHeaderTypeSelect(textBuffer * html, char *listName);
void writeCacheTypeSelect(textBuffer * html, char *listName);
void writeMcTtlSelect(textBuffer * html, char *listName);
void writeOnOffSelect(textBuffer * html, char *listName);
void writeDenySelect(textBuffer * html, char *listName);
void writeClientGroupTypeSelect(textBuffer * html, char *listName);
void writeAccessTypeSelect(textBuffer * html, char *listName);
void writeTreatmentTypeSelect(textBuffer * html, char *listName);
void writeRoundRobinTypeSelect(textBuffer * html, char *listName);
void writeRoundRobinTypeSelect_notrue(textBuffer * html, char *listName);
void writeTrueFalseSelect(textBuffer * html, char *listName);
void writeSizeFormatSelect(textBuffer * html, char *listName);
void writeProtocolSelect(textBuffer * html, char *listName);

#endif // _WEB_CONFIG_RENDER_H_
