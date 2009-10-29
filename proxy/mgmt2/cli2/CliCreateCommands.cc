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

/****************************************************************
 * Filename: CliCreateCommands.cc
 * Purpose: This file contains the CLI command creation function.
 *
 * 
 ****************************************************************/

#include "CliCreateCommands.h"
#include "createCommand.h"
#include "ShowCmd.h"
#include "ConfigCmd.h"
#include "UtilCmds.h"

////////////////////////////////////////////////////////////////
// Called during Tcl_AppInit, this function creates the CLI commands
//
int
CliCreateCommands()
{
  createCommand("config:root", Cmd_ConfigRoot, NULL, CLI_COMMAND_EXTERNAL, "config:root", "Switch to root user");

  createCommand("show", Cmd_Show, NULL, CLI_COMMAND_EXTERNAL, "show", "Show command");

  createCommand("config", Cmd_Config, NULL, CLI_COMMAND_EXTERNAL, "config", "Config command");

  createCommand("show:status", Cmd_ShowStatus, NULL, CLI_COMMAND_EXTERNAL, "show:status", "Proxy status");

  createCommand("show:version", Cmd_ShowVersion, NULL, CLI_COMMAND_EXTERNAL, "show:version", "Version information");

#if 0
  // Tech Support wants us to handle all ports in their protocol or function area
  createCommand("show:ports", Cmd_ShowPorts, NULL, CLI_COMMAND_EXTERNAL, "show:ports", "Port value assignments");
#endif

  createCommand("show:security", Cmd_ShowSecurity, NULL, CLI_COMMAND_EXTERNAL, "show:security", "Security information");

  createCommand("show:http", Cmd_ShowHttp, NULL, CLI_COMMAND_EXTERNAL, "show:http", "HTTP protocol configuration");

  createCommand("show:nntp", Cmd_ShowNntp, CmdArgs_ShowNntp, CLI_COMMAND_EXTERNAL,
                "show:nntp [config-xml]", "NNTP protocol configuration");

  createCommand("show:ftp", Cmd_ShowFtp, NULL, CLI_COMMAND_EXTERNAL, "show:ftp", "FTP protocol configuration");

  createCommand("show:icp", Cmd_ShowIcp, CmdArgs_ShowIcp, CLI_COMMAND_EXTERNAL,
                "show:icp [peer]", "ICP protocol configuration");

  createCommand("show:proxy", Cmd_ShowProxy, NULL, CLI_COMMAND_EXTERNAL, "show:proxy", "Proxy configuration");

  createCommand("show:cache", Cmd_ShowCache, CmdArgs_ShowCache, CLI_COMMAND_EXTERNAL,
                "show:cache [rules|storage]", "Cache configuration");

  createCommand("show:virtual-ip", Cmd_ShowVirtualIp, NULL, CLI_COMMAND_EXTERNAL,
                "show:virtual-ip", "Virtual-ip configuration");

  createCommand("show:hostdb", Cmd_ShowHostDb, NULL, CLI_COMMAND_EXTERNAL,
                "show:hostdb", "Host database configuration");

  createCommand("show:dns-resolver", Cmd_ShowDnsResolver, NULL, CLI_COMMAND_EXTERNAL,
                "show:dns-resolver", "DNS resolver configuration");

  createCommand("show:logging", Cmd_ShowLogging, NULL, CLI_COMMAND_EXTERNAL, "show:logging", "Logging configuration");

  createCommand("show:ssl", Cmd_ShowSsl, NULL, CLI_COMMAND_EXTERNAL, "show:ssl", "SSL configuration");

  createCommand("show:filter", Cmd_ShowFilter, NULL, CLI_COMMAND_EXTERNAL, "show:filter", "Filter configuration");

  createCommand("show:parent", Cmd_ShowParents, CmdArgs_ShowParents, CLI_COMMAND_EXTERNAL,
                "show:parent", "Parent configuration");

  createCommand("show:remap", Cmd_ShowRemap, NULL, CLI_COMMAND_EXTERNAL, "show:remap", "Remap configuration");

  createCommand("show:snmp", Cmd_ShowSnmp, NULL, CLI_COMMAND_EXTERNAL, "show:snmp", "SNMP configuration");

  createCommand("show:ldap", Cmd_ShowLdap, CmdArgs_ShowLdap, CLI_COMMAND_EXTERNAL, "show:ldap", "LDAP configuration");

  createCommand("show:ldap-stats", Cmd_ShowLdapStats, NULL, CLI_COMMAND_EXTERNAL,
                "show:ldap-stats", "LDAP statistics configuration");


  createCommand("show:socks", Cmd_ShowSocks, CmdArgs_ShowSocks, CLI_COMMAND_EXTERNAL,
                "show:socks", "SOCKS configuration");

  createCommand("show:port-tunnels", Cmd_ShowPortTunnels, NULL, CLI_COMMAND_EXTERNAL,
                "show:port-tunnels", "Port tunnels configuration");

  createCommand("show:scheduled-update", Cmd_ShowScheduledUpdate, CmdArgs_ShowScheduledUpdate, CLI_COMMAND_EXTERNAL,
                "show:scheduled-update", "Scheduled update configuration");

  createCommand("show:proxy-stats", Cmd_ShowProxyStats, NULL, CLI_COMMAND_EXTERNAL,
                "show:proxy-stats", "Proxy statistics");

  createCommand("show:http-trans-stats", Cmd_ShowHttpTransStats, NULL, CLI_COMMAND_EXTERNAL,
                "show:http-trans-stats", "HTTP transaction statistics");

  createCommand("show:http-stats", Cmd_ShowHttpStats, NULL, CLI_COMMAND_EXTERNAL, "show:http-stats", "HTTP statistics");

  createCommand("show:nntp-stats", Cmd_ShowNntpStats, NULL, CLI_COMMAND_EXTERNAL, "show:nntp-stats", "NNTP statistics");

  createCommand("show:ftp-stats", Cmd_ShowFtpStats, NULL, CLI_COMMAND_EXTERNAL, "show:ftp-stats", "FTP statistics");

  createCommand("show:icp-stats", Cmd_ShowIcpStats, NULL, CLI_COMMAND_EXTERNAL, "show:icp-stats", "ICP statistics");

  createCommand("show:cache-stats", Cmd_ShowCacheStats, NULL, CLI_COMMAND_EXTERNAL,
                "show:cache-stats", "Cache statistics");

  createCommand("show:hostdb-stats", Cmd_ShowHostDbStats, NULL, CLI_COMMAND_EXTERNAL,
                "show:hostdb-stats", "Host database statistics");

  createCommand("show:dns-stats", Cmd_ShowDnsStats, NULL, CLI_COMMAND_EXTERNAL, "show:dns-stats", "DNS statistics");

  createCommand("show:logging-stats", Cmd_ShowLoggingStats, NULL, CLI_COMMAND_EXTERNAL,
                "show:logging-stats", "Logging statistics");

  createCommand("show:alarms", Cmd_ShowAlarms, NULL, CLI_COMMAND_EXTERNAL, "show:alarms", "Show active alarms");

  createCommand("show:network", Cmd_ShowNetwork, NULL, CLI_COMMAND_EXTERNAL, "show:network", "Show Network Settings");

  createCommand("show:cluster", Cmd_ShowCluster, NULL, CLI_COMMAND_EXTERNAL,
                "show:cluster", "Show Cluster Ports Settings");

  createCommand("show:radius", Cmd_ShowRadius, NULL, CLI_COMMAND_EXTERNAL, "show:radius", "Show Radius Configuration");

  createCommand("show:ntlm", Cmd_ShowNtlm, NULL, CLI_COMMAND_EXTERNAL, "show:ntlm", "Show NTLM Configuration");

  createCommand("show:ntlm-stats", Cmd_ShowNtlmStats, NULL, CLI_COMMAND_EXTERNAL,
                "show:ntlm-stats", "Show NTLM Statistics");

  createCommand("config:get", Cmd_ConfigGet, NULL, CLI_COMMAND_EXTERNAL,
                "config:get <variable>", "Display a variable value");

  createCommand("config:set", Cmd_ConfigSet, NULL, CLI_COMMAND_EXTERNAL,
                "config:set <variable> <value>", "Set variable to specified value");

  createCommand("config:name", Cmd_ConfigName, NULL, CLI_COMMAND_EXTERNAL,
                "config:name <string>", "Set proxy name <string>");

  createCommand("config:start", Cmd_ConfigStart, NULL, CLI_COMMAND_EXTERNAL, "config:start", "Start proxy software");

  createCommand("config:stop", Cmd_ConfigStop, NULL, CLI_COMMAND_EXTERNAL, "config:stop", "Stop proxy software");

  createCommand("config:hard-restart", Cmd_ConfigHardRestart, NULL, CLI_COMMAND_EXTERNAL,
                "config:hard-restart", "Perform Hard Restart of all software components");

  createCommand("config:restart", Cmd_ConfigRestart, CmdArgs_ConfigRestart, CLI_COMMAND_EXTERNAL,
                "config:restart [cluster]", "Perform Restart of proxy software");

  createCommand("config:ssl", Cmd_ConfigSsl, CmdArgs_ConfigSsl, CLI_COMMAND_EXTERNAL,
                "config:ssl status <on | off>\n" "config:ssl ports <int>", "Configure ssl");

  createCommand("config:filter", Cmd_ConfigFilter, NULL, CLI_COMMAND_EXTERNAL,
                "config:filter <url>", "Update filter.config rules <url>");

  createCommand("config:parent", Cmd_ConfigParents, CmdArgs_ConfigParents, CLI_COMMAND_EXTERNAL,
                "config:parent status <on | off>\n"
                "config:parent name <parent>\n" "config:parent rules <url>", "Update parent configuration");


  createCommand("config:remap", Cmd_ConfigRemap, NULL, CLI_COMMAND_EXTERNAL,
                "config:remap <url>", "Update remap configuration file <url>");

#if 0
  // Tech Support wants us to handle all ports in their protocol or function area
  createCommand("config:ports", Cmd_ConfigPorts, CmdArgs_ConfigPorts, CLI_COMMAND_EXTERNAL,
                "config:ports <port> <value>", "Configure port number assignments");
#endif

  createCommand("config:snmp", Cmd_ConfigSnmp, CmdArgs_ConfigSnmp, CLI_COMMAND_EXTERNAL,
                "config:snmp status <on|off>", "Configure SNMP <on|off>");

  createCommand("config:ldap", Cmd_ConfigLdap, CmdArgs_ConfigLdap, CLI_COMMAND_EXTERNAL,
                "config:ldap status <on|off>\n"
                "config:ldap cache-size <cache>\n"
                "config:ldap ttl <ttl>\n"
                "config:ldap purge-fail <on | off>\n"
                "config:ldap multiple <on | off>\n"
                "config:ldap bypass <on | off>\n" "config:ldap file <url>", "Configure LDAP configuration");

  createCommand("config:clock", Cmd_ConfigClock, CmdArgs_ConfigClock, CLI_COMMAND_EXTERNAL,
                "config:clock date <mm/dd/yyyy>\n"
                "config:clock time <hh:mm:ss>\n"
                "config:clock timezone <number from list | list>", "Configure date, time, timezone");

  createCommand("config:security", Cmd_ConfigSecurity, CmdArgs_ConfigSecurity, CLI_COMMAND_EXTERNAL,
                "config:security <ip-allow | mgmt-allow | admin> <url-config-file>\n"
                "config:security password", "Update security configuration");

  createCommand("config:http", Cmd_ConfigHttp, CmdArgs_ConfigHttp, CLI_COMMAND_EXTERNAL,
                "config:http status <on | off>\n"
                "config:http <keep-alive-timeout-in | keep-alive-timeout-out> <seconds>\n"
                "config:http <inactive-timeout-in | inactive-timeout-out> <seconds>\n"
                "config:http <active-timeout-in | active-timeout-out> <seconds>\n"
                "config:http <remove-from | remove-referer> <on | off>\n"
                "config:http <remove-user | remove-cookie> <on | off>\n"
                "config:http <remove-header> <string>\n"
                "config:http <insert-ip | remove-ip> <on | off>\n"
                "config:http proxy <fwd | rev | fwd-rev>", "Configure HTTP");

  createCommand("config:ftp", Cmd_ConfigFtp, CmdArgs_ConfigFtp, CLI_COMMAND_EXTERNAL,
                "config:ftp mode <pasv-port | port | pasv>\n"
                "config:ftp <inactivity-timeout | expire-after> <seconds>\n"
                "config:ftp anonymous-password <string>\n" "config:ftp proxy <fwd | rev | fwd-rev>", "Configure FTP");

  createCommand("config:icp", Cmd_ConfigIcp, CmdArgs_ConfigIcp, CLI_COMMAND_EXTERNAL,
                "config:icp mode <disabled | receive | send-receive>\n"
                "config:icp port <int>\n"
                "config:icp multicast <on | off>\n"
                "config:icp query-timeout <seconds>\n" "config:icp peers <url-config-file>", "Configure ICP");

  createCommand("config:port-tunnels", Cmd_ConfigPortTunnels, CmdArgs_ConfigPortTunnels, CLI_COMMAND_EXTERNAL,
                "config:port-tunnels server-other-ports <port>", "Configure Port Tunnels");

  createCommand("config:scheduled-update", Cmd_ConfigScheduledUpdate, CmdArgs_ConfigScheduledUpdate,
                CLI_COMMAND_EXTERNAL,
                "config:scheduled-update status <on | off>\n" "config:scheduled-update retry-count <int>\n"
                "config:scheduled-update retry-interval <sec>\n" "config:scheduled-update max-concurrent <int>\n"
                "config:scheduled-update force-immediate <on | off>\n"
                "config:scheduled-update rules <url-config-file>", "Configure Scheduled Update");

  createCommand("config:socks", Cmd_ConfigSocks, CmdArgs_ConfigSocks, CLI_COMMAND_EXTERNAL,
                "config:socks status <on | off>\n"
                "config:socks version <version>\n"
                "config:socks default-servers <string>\n"
                "config:socks accept <on | off>\n" "config:socks accept-port <int>", "Configure Socks");


  createCommand("config:cache", Cmd_ConfigCache, CmdArgs_ConfigCache, CLI_COMMAND_EXTERNAL,
                "config:cache <http | nntp | ftp> <on | off>\n"
                "config:cache ignore-bypass <on | off>\n"
                "config:cache <max-object-size | max-alternates> <int>\n"
                "config:cache file <url-config-file>\n"
                "config:cache freshness verify <when-expired | no-date | always | never>\n"
                "config:cache freshness minimum <explicit | last-modified | nothing>\n"
                "config:cache freshness no-expire-limit greater-than <sec> less-than <sec>\n"
                "config:cache <dynamic | alternates> <on | off>\n"
                "config:cache vary <text | images | other> <string>\n"
                "config:cache cookies <none | all | images | non-text>\n" "config:cache clear", "Configure Cache");


  createCommand("config:hostdb", Cmd_ConfigHostdb, CmdArgs_ConfigHostdb, CLI_COMMAND_EXTERNAL,
                "config:hostdb <lookup-timeout | foreground-timeout> <seconds>\n"
                "config:hostdb <background-timeout | invalid-host-timeout> <seconds>\n"
                "config:hostdb <re-dns-on-reload> <on | off>\n" "config:hostdb clear", "Configure Host Database");

  createCommand("config:logging", Cmd_ConfigLogging, CmdArgs_ConfigLogging, CLI_COMMAND_EXTERNAL,
                "config:logging event <enabled | trans-only | error-only | disabled>\n"
                "config:logging mgmt-directory <string>\n"
                "config:logging <space-limit | space-headroom> <megabytes>\n"
                "config:logging collation-status <inactive | host | send-standard |\n"
                "                                 send-custom | send-all>\n"
                "config:logging collation-host <string>\n"
                "config:logging collation secret <string> tagged <on | off> orphan-limit <int>\n"
                "config:logging format <squid | netscape-common | netscape-ext | netscape-ext2> <on | off>\n"
                "               type <ascii | binary> file <string> header <string>\n"
                "config:logging splitting <nntp | icp | http> <on | off>\n"
                "config:logging custom <on | off> format <traditional | xml>\n"
                "config:logging rolling <on | off> offset <hour> interval <hours>\n"
                "               auto-delete <on | off>", "Configure Logging");


  createCommand("config:dns", Cmd_ConfigDns, CmdArgs_ConfigDns, CLI_COMMAND_EXTERNAL,
                "config:dns proxy <on | off>\n"
                "config:dns proxy-port <int>\n"
                "config:dns resolve-timeout <seconds>\n" "config:dns retries <int>", "Configure DNS");

  createCommand("config:virtual-ip", Cmd_ConfigVirtualip, CmdArgs_ConfigVirtualip, CLI_COMMAND_EXTERNAL,
                "config:virtual-ip status <on | off>\n"
                "config:virtual-ip list\n"
                "config:virtual-ip add <x.x.x.x> device <string> sub-intf <int>\n"
                "config:virtual-ip delete <virtual ip number>", "Configure virtual-ip");

  createCommand("config:network", Cmd_ConfigNetwork, CmdArgs_ConfigNetwork, CLI_COMMAND_EXTERNAL,
                "config:network hostname <string>\n"
                "config:network defaultrouter <x.x.x.x>\n"
                "config:network search-domain <string>\n"
                "config:network dns \"a.a.a.a b.b.b.b c.c.c.c\"\n"
                "config:network int <interface>\n"
                "config:network int <interface> down\n"
                "config:network int <interface> up <onboot | not-onboot>\n"
                "               <static | dhcp> <ip> <netmask> <gateway>\n"
                "config:network int <interface> <onboot | not-onboot>\n"
                "config:network int <interface> <static | dhcp>\n"
                "config:network int <interface> ip <x.x.x.x>\n"
                "config:network int <interface> netmask <x.x.x.x>\n"
                "config:network int <interface> gateway <x.x.x.x | default>\n", "Configure Network Settings");

  createCommand("config:nntp", Cmd_ConfigNNTP, CmdArgs_ConfigNNTP, CLI_COMMAND_EXTERNAL,
                "config:nntp status <on | off>\n"
#if 0
                "config:nntp port <int>\n"
                "config:nntp connect-msg <posting | non-posting> <string>\n"
                "config:nntp <posting-status | access-control | v2-auth | local-auth> <on | off>\n"
                "config:nntp <clustering | allow-feeds | background-posting> <on | off>\n"
#endif
                "config:nntp <obey-cancel | obey-newgroups | obey-rmgroups> <on | off>\n"
                "config:nntp <inactive-timeout | check-new-groups | check-cancelled> <seconds>\n"
                "config:nntp <check-parent | check-pull> <seconds>\n"
#if 0
                "config:nntp <check-parent | check-cluster | check-pull> <seconds>\n"
                "config:nntp auth-server <string>\n"
                "config:nntp auth-port <seconds>\n"
                "config:nntp auth-timeout <seconds>\n" "config:nntp client-throttle <bytes per second | 0>\n"
#endif
                "config:nntp <nntp-servers | nntp-access> <url-config-file>", "NNTP Configuration");

  createCommand("config:alarms", Cmd_ConfigAlarm, CmdArgs_ConfigAlarm, CLI_COMMAND_EXTERNAL,
                "config:alarms resolve-name <string>\n"
                "config:alarms resolve-number <int>\n"
                "config:alarms resolve-all\n"
                "config:alarms notify <on | off>", "Resolve Alarms, Turn notification on/off");

  createCommand("config:ntlm", Cmd_ConfigNtlm, CmdArgs_ConfigNtlm, CLI_COMMAND_EXTERNAL,
                "config:ntlm status <on | off>\n"
                "config:ntlm domain-controller <hostnames>\n"
                "config:ntlm nt-domain <domain>\n" "config:ntlm load-balancing <on | off>", "NTLM Configuration");

  createCommand("config:radius", Cmd_ConfigRadius, CmdArgs_ConfigRadius, CLI_COMMAND_EXTERNAL,
                "config:radius status <on | off>\n"
                "config:radius primary-hostname <hostname>\n"
                "config:radius primary-port <port>\n"
                "config:radius primary-key\n"
                "config:radius secondary-hostname <hostname>\n"
                "config:radius secondary-port <port>\n" "config:radius secondary-key\n", "Radius Configuration");

  createCommand("enable", Cmd_Enable, CmdArgs_Enable, CLI_COMMAND_EXTERNAL,
                "enable \n" "enable status ", "Enable Restricted Commands");

  createCommand("disable", Cmd_Disable, NULL, CLI_COMMAND_EXTERNAL, "disable", "Disable Restricted Commands");


  createCommand("debug", DebugCmd, DebugCmdArgs, CLI_COMMAND_EXTERNAL,
                "debug <on|off>", "Turn debugging print statements on/off");

  return CLI_OK;
}
