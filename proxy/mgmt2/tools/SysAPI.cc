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

#if (HOST_OS == linux)

#include "SysAPI.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <stdarg.h>
#include <string.h>
#include <ink_string.h>
#include <regex.h>
#include <time.h>
#include <grp.h>

#include <ctype.h>
#include "../api2/include/INKMgmtAPI.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>
#include <sys/stat.h>

#define NETCONFIG_HOSTNAME  0
#define NETCONFIG_GATEWAY   1
#define NETCONFIG_DOMAIN    2
#define NETCONFIG_DNS       3
#define NETCONFIG_INTF_UP   4
#define NETCONFIG_INTF_DOWN 5
#define NETCONFIG_SNMP_UP   7
#define NETCONFIG_INTF_DISABLE 8

#define TIMECONFIG_ALL	    0
#define TIMECONFIG_TIME     1
#define TIMECONFIG_DATE     2
#define TIMECONFIG_TIMEZONE 3
#define TIMECONFIG_NTP      4

#ifdef DEBUG_SYSAPI
#define DPRINTF(x)  printf x
#else
#define DPRINTF(x)
#endif

int NetConfig_Action(int index, ...);
int TimeConfig_Action(int index, bool restart, ...);
int Net_GetNIC_Values(char *interface, char *status, char *onboot, char *static_ip, char *ip, char *netmask,
                      char *gateway);
int find_value(char *pathname, char *key, char *value, size_t value_len, char *delim, int no);
static bool recordRegexCheck(const char *pattern, const char *value);
static int getTSdirectory(char *ts_path, size_t ts_path_len);

int
Net_GetHostname(char *hostname, size_t hostname_len)
{
  ink_strncpy(hostname, "", hostname_len);
  return (gethostname(hostname, 256));
}

int
Net_SetHostname(char *hostname)
{
  int status;
  char old_hostname[256], protocol[80];
  char ip_addr[80], name[80], nic_status[80];
  bool found = false;

  old_hostname[0] = '\0';

  DPRINTF(("Net_SetHostname: hostname %s\n", hostname));

  if (!Net_IsValid_Hostname(hostname)) {
    DPRINTF(("Net_SetHostname: invalid hostname\n"));
    return -1;
  }

  Net_GetHostname(old_hostname, sizeof(old_hostname));
  if (!strlen(old_hostname)) {
    DPRINTF(("Net_SetHostname: failed to get old_hostname\n"));
    return -1;
  }
  //Fix for BZ48925 - adding the correct ip to /etc/hosts
  //First get an IP of a valid interface - we don't care so much which one as we don't
  //use it in TS - it is just a place holder for Real Proxy with no DNS server (see BZ38199)

  ip_addr[0] = '\0';
  int count = Net_GetNetworkIntCount();
  if (count == 0) {             //this means we didn't find any interface
    ip_addr[0] = '\0';
  } else {
    name[0] = '\0';
    nic_status[0] = '\0';
    protocol[0] = '\0';
    for (int i = 0; i < count; i++) {   //since we are looping - we will get the "last" available IP - doesn't matter to us
      Net_GetNetworkInt(i, name, sizeof(name)); //we know we have at least one
      if (strlen(name) > 0) {
        Net_GetNIC_Status(name, nic_status, sizeof(nic_status));
        Net_GetNIC_Protocol(name, protocol, sizeof(protocol));
        if ((strcmp("up", nic_status) == 0) && (!found) && (strcasecmp(protocol, "dhcp") != 0)) {
          //we can use this interface
          Net_GetNIC_IP(name, ip_addr, sizeof(ip_addr));
          found = true;
        }
      }
    }
  }
  DPRINTF(("Net_SetHostname: calling INKSetHostname \"%s %s %s\"\n", hostname, old_hostname, ip_addr));
  status = NetConfig_Action(NETCONFIG_HOSTNAME, hostname, old_hostname, ip_addr);

  return status;
}

// return true if the line is commented out, or if line is blank
// return false otherwise
bool
isLineCommented(char *line)
{
  char *p = line;
  while (*p) {
    if (*p == '#')
      return true;
    if (!isspace(*p) && *p != '#')
      return false;
    p++;
  }
  return true;
}


int
Net_GetDefaultRouter(char *router, size_t router_len)
{

  int value;
  router[0] = '\0';
  value = find_value("/etc/sysconfig/network", "GATEWAY", router, router_len, "=", 0);
  DPRINTF(("[Net_GetDefaultRouter] Find returned %d\n", value));
  if (value) {
    return !value;
  } else {
    char command[80];
    char *tmp_file = "/tmp/route_status", buffer[256];
    FILE *fp;

    ink_strncpy(command, "/sbin/route -n > /tmp/route_status", sizeof(command));
    remove(tmp_file);
    if (system(command) == (-1)) {
      DPRINTF(("[Net_GetDefaultRouter] run route -n\n"));
      return -1;
    }
        
    fp = fopen(tmp_file, "r");
    if (fp == NULL) {
      DPRINTF(("[Net_GetDefaultRouter] can not open the temp file\n"));
      return -1;
    }

    char *gw_start;
    bool find_UG = false;
    NOWARN_UNUSED_RETURN(fgets(buffer, 256, fp));
    while (!feof(fp)) {
      if (strstr(buffer, "UG") != NULL) {
        find_UG = true;
        strtok(buffer, " \t");
        gw_start = strtok(NULL, " \t");
        break;
      }
      NOWARN_UNUSED_RETURN(fgets(buffer, 256, fp));
    }
    if (find_UG) {
      ink_strncpy(router, gw_start, router_len);
      fclose(fp);
      return 0;
    }
    fclose(fp);
  }
  return 1;

}

int
Net_SetDefaultRouter(char *router)
{
  int status;
  char old_router[80];

  DPRINTF(("Net_SetDefaultRouter: router %s\n", router));

  if (!Net_IsValid_IP(router)) {
    DPRINTF(("Net_SetDefaultRouter: invalid IP\n"));
    return -1;
  }


  Net_GetDefaultRouter(old_router, sizeof(old_router));
  if (!strlen(old_router)) {
    DPRINTF(("Net_SetHostname: failed to get old_router\n"));
    return -1;
  }

  status = NetConfig_Action(NETCONFIG_GATEWAY, router, old_router);
  DPRINTF(("Net_SetDefaultRouter: NetConfig_Action returned %d\n", status));
  if (status) {
    return status;
  }

  return status;
}

int
Net_GetDomain(char *domain, size_t domain_len)
{
  //  domain can be defined using search or domain keyword
  ink_strncpy(domain, "", domain_len);
  return !find_value("/etc/resolv.conf", "search", domain, domain_len, " ", 0);

  /**
  if (!find_value("/etc/resolv.conf", "search", domain, " ", 0)) {
    return (!find_value("/etc/resolv.conf", "domain", domain, " ", 0));
  }else
    return 1; 
  **/
}

int
Net_SetDomain(char *domain)
{
  int status;

  DPRINTF(("Net_SetDomain: domain %s\n", domain));

  status = NetConfig_Action(NETCONFIG_DOMAIN, domain);
  if (status) {
    return status;
  }

  return status;
}

int
Net_GetDNS_Servers(char *dns, size_t dns_len)
{
  char ip[80];
  ink_strncpy(dns, "", dns_len);
  int i = 0;
  while (find_value("/etc/resolv.conf", "nameserver", ip, sizeof(ip), " ", i++)) {
    strncat(dns, ip, dns_len - strlen(dns) - 1);
    strncat(dns, " ", dns_len - strlen(dns) - 1);
  }
  return 0;
}

int
Net_SetDNS_Servers(char *dns)
{
  int status;
  char buff[512];
  char *tmp1, *tmp2;
  memset(buff, 0, 512);

  DPRINTF(("Net_SetDNS_Servers: dns %s\n", dns));

  if (dns == NULL) {
    return -1;
  }
  // check all IP addresses for validity
  strncpy(buff, dns, 512);
  buff[511] = '\0';
  tmp1 = buff;
  while ((tmp2 = strtok(tmp1, " \t")) != NULL) {
    DPRINTF(("Net_SetDNS_Servers: tmp2 %s\n", tmp2));
    if (!Net_IsValid_IP(tmp2)) {
      return -1;
    }
    tmp1 = NULL;
  }
  DPRINTF(("Net_SetDNS_Servers: dns %s\n", dns));
  status = NetConfig_Action(NETCONFIG_DNS, dns);
  if (status) {
    return status;
  }

  return status;
}

int
Net_GetDNS_Server(char *server, size_t server_len, int no)
{
  ink_strncpy(server, "", server_len);
  return (!find_value("/etc/resolv.conf", "nameserver", server, server_len, " ", no));
}

int
Net_GetNetworkIntCount()
{

  FILE *net_device;
  char buffer[200];
  int count = 0;

  // for each NIC
  net_device = fopen("/proc/net/dev", "r");

  while (!feof(net_device)) {
    NOWARN_UNUSED_RETURN(fgets(buffer, 200, net_device));
    if (strstr(buffer, "eth"))  // only counts eth interface
      count++;
  }
  fclose(net_device);
  return count;
}

int
Net_GetNetworkInt(int int_num, char *interface, size_t interface_len)
{
  ink_strncpy(interface, "", interface_len);

  FILE *net_device;
  char buffer[200];
  int space_len;
  char *pos, *tmp;

  net_device = fopen("/proc/net/dev", "r");

  int i = 0;
  while (!feof(net_device) && i < int_num) {
    NOWARN_UNUSED_RETURN(fgets(buffer, 200, net_device));
    if (strstr(buffer, "eth"))  // only counts the eth interface
      i++;
  }
  fclose(net_device);

  if (i < int_num - 1)
    return -1;

  pos = strchr(buffer, ':');
  *pos = '\0';
  space_len = strspn(buffer, " ");
  tmp = buffer + space_len;

  ink_strncpy(interface, tmp, interface_len);

  return 0;

}

int
Net_GetNIC_Status(char *interface, char *status, size_t status_len)
{

  char ifcfg[80], command[80];

  ink_strncpy(status, "", status_len);

  ink_strncpy(ifcfg, "/etc/sysconfig/network-scripts/ifcfg-", sizeof(ifcfg));
  strncat(ifcfg, interface, (sizeof(ifcfg) - strlen(ifcfg) - 1));

  ink_strncpy(command, "/sbin/ifconfig | grep ", sizeof(command));
  strncat(command, interface, (sizeof(command) - strlen(command) - 1));
  strncat(command, " >/dev/null 2>&1", (sizeof(command) - strlen(command) - 1));

  if (system(command) == 0) {
    ink_strncpy(status, "up", status_len);
  } else {
    ink_strncpy(status, "down", status_len);
  }
  return 0;
}

int
Net_GetNIC_Start(char *interface, char *start, size_t start_len)
{

  char ifcfg[80], value[80];

  ink_strncpy(start, "", start_len);

  ink_strncpy(ifcfg, "/etc/sysconfig/network-scripts/ifcfg-", sizeof(ifcfg));
  strncat(ifcfg, interface, (sizeof(ifcfg) - strlen(ifcfg) - 1));

  if (find_value(ifcfg, "ONBOOT", value, sizeof(value), "=", 0)) {
    if (strcasecmp(value, "yes") == 0) {
      ink_strncpy(start, "onboot", start_len);
    } else {
      ink_strncpy(start, "not-onboot", start_len);
    }
    return 0;
  } else {
    return 1;
  }
}

int
Net_GetNIC_Protocol(char *interface, char *protocol, size_t protocol_len)
{

  char ifcfg[80], value[80];

  ink_strncpy(protocol, "", protocol_len);

  ink_strncpy(ifcfg, "/etc/sysconfig/network-scripts/ifcfg-", sizeof(ifcfg));
  strncat(ifcfg, interface, (sizeof(ifcfg) - strlen(ifcfg) - 1));

  if (find_value(ifcfg, "BOOTPROTO", value, sizeof(value), "=", 0)) {
    if ((strcasecmp(value, "none") == 0) || (strcasecmp(value, "static") == 0) || (strcasecmp(value, "dhcp") == 0)) {
      ink_strncpy(protocol, value, protocol_len);
    } else {
      ink_strncpy(protocol, "none", protocol_len);
    }
    return 0;
  } else {
    //if there is no BOOTPROTO, assume the default is "none" now
    ink_strncpy(protocol, "none", protocol_len);
    return 1;
  }
}

int
Net_GetNIC_IP(char *interface, char *ip, size_t ip_len)
{

  char ifcfg[80], protocol[80], status[80];
  char command[80];

  ink_strncpy(ip, "", ip_len);
  Net_GetNIC_Protocol(interface, protocol, sizeof(protocol));
  if (strcmp(protocol, "none") == 0 || strcmp(protocol, "static") == 0) {
    ink_strncpy(ifcfg, "/etc/sysconfig/network-scripts/ifcfg-", sizeof(ifcfg));
    strncat(ifcfg, interface, (sizeof(ifcfg) - strlen(ifcfg) - 1));

    return (!find_value(ifcfg, "IPADDR", ip, ip_len, "=", 0));
  } else {
    Net_GetNIC_Status(interface, status, sizeof(status));
    if (strcmp(status, "up") == 0) {
      char *tmp_file = "/tmp/dhcp_status", buffer[256];
      FILE *fp;

      ink_strncpy(command, "/sbin/ifconfig ", sizeof(command));
      strncat(command, interface, (sizeof(command) - strlen(command) - 1));
      strncat(command, " >", (sizeof(command) - strlen(command) - 1));
      strncat(command, tmp_file, (sizeof(command) - strlen(command) - 1));

      remove(tmp_file);
      if (system(command) == (-1)) {
        DPRINTF(("[Net_GetNIC_IP] can not run ifconfig\n"));
        return -1;
      }
      fp = fopen(tmp_file, "r");
      if (fp == NULL) {
        DPRINTF(("[Net_GetNIC_IP] can not open the temp file\n"));
        return -1;
      }

      char *pos, *addr_end, *addr_start = NULL;
      NOWARN_UNUSED_RETURN(fgets(buffer, 256, fp));
      while (!feof(fp)) {
        if (strstr(buffer, "inet addr:") != NULL) {
          pos = strchr(buffer, ':');
          addr_start = pos + 1;
          addr_end = strchr(addr_start, ' ');
          *addr_end = '\0';
          break;
        }
        NOWARN_UNUSED_RETURN(fgets(buffer, 256, fp));
      }

      if (addr_start)
        ink_strncpy(ip, addr_start, ip_len);
      fclose(fp);
      return 0;
    }
  }
  return 1;
}

int
Net_GetNIC_Netmask(char *interface, char *netmask, size_t netmask_len)
{

  char ifcfg[80], protocol[80], status[80];
  char command[80];

  ink_strncpy(netmask, "", netmask_len);
  Net_GetNIC_Protocol(interface, protocol, sizeof(protocol));
  if (strcmp(protocol, "none") == 0 || strcmp(protocol, "static") == 0) {
    ink_strncpy(ifcfg, "/etc/sysconfig/network-scripts/ifcfg-", sizeof(ifcfg));
    strncat(ifcfg, interface, (sizeof(ifcfg) - strlen(ifcfg) - 1));
    return (!find_value(ifcfg, "NETMASK", netmask, netmask_len, "=", 0));
  } else {
    Net_GetNIC_Status(interface, status, sizeof(status));
    if (strcmp(status, "up") == 0) {
      char *tmp_file = "/tmp/dhcp_status", buffer[256];
      FILE *fp;

      ink_strncpy(command, "/sbin/ifconfig ", sizeof(command));
      strncat(command, interface, (sizeof(command) - strlen(command) - 1));
      strncat(command, " >", (sizeof(command) - strlen(command) - 1));
      strncat(command, tmp_file, (sizeof(command) - strlen(command) - 1));

      remove(tmp_file);
      if (system(command) == (-1)) {
        DPRINTF(("[Net_GetNIC_Netmask] can not run ifconfig\n"));
        return -1;
      }
      fp = fopen(tmp_file, "r");
      if (fp == NULL) {
        DPRINTF(("[Net_GetNIC_Netmask] can not open the temp file\n"));
        return -1;
      }

      char *pos, *mask_end, *mask_start = NULL;
      NOWARN_UNUSED_RETURN(fgets(buffer, 256, fp));
      while (!feof(fp)) {
        if (strstr(buffer, "Mask:") != NULL) {
          pos = strstr(buffer, "Mask:");
          mask_start = pos + 5;
          mask_end = strchr(mask_start, '\n');
          *mask_end = '\0';
          break;
        }
        NOWARN_UNUSED_RETURN(fgets(buffer, 256, fp));
      }

      if (mask_start)
        ink_strncpy(netmask, mask_start, netmask_len);
      fclose(fp);
      return 0;
    }
  }
  return 1;

}

int
Net_GetNIC_Gateway(char *interface, char *gateway, size_t gateway_len)
{

  char ifcfg[80];

  ink_strncpy(gateway, "", gateway_len);
  ink_strncpy(ifcfg, "/etc/sysconfig/network-scripts/ifcfg-", sizeof(ifcfg));
  strncat(ifcfg, interface, (sizeof(ifcfg) - strlen(ifcfg) - 1));

  return (!find_value(ifcfg, "GATEWAY", gateway, gateway_len, "=", 0));
}

int
Net_SetNIC_Down(char *interface)
{
  int status;
  char ip[80];

  if (!Net_IsValid_Interface(interface))
    return -1;

  status = NetConfig_Action(NETCONFIG_INTF_DOWN, interface);
  if (status) {
    return status;
  }

  Net_GetNIC_IP(interface, ip, sizeof(ip));

  return status;
}

int
Net_SetNIC_StartOnBoot(char *interface, char *onboot)
{
  char nic_protocol[80], nic_ip[80], nic_netmask[80], nic_gateway[80];

  Net_GetNIC_Protocol(interface, nic_protocol, sizeof(nic_protocol));
  Net_GetNIC_IP(interface, nic_ip, sizeof(nic_ip));
  Net_GetNIC_Netmask(interface, nic_netmask, sizeof(nic_netmask));
  Net_GetNIC_Gateway(interface, nic_gateway, sizeof(nic_gateway));

  return (Net_SetNIC_Up(interface, onboot, nic_protocol, nic_ip, nic_netmask, nic_gateway));
}

int
Net_SetNIC_BootProtocol(char *interface, char *nic_protocol)
{
  char nic_boot[80], nic_ip[80], nic_netmask[80], nic_gateway[80];

  Net_GetNIC_Start(interface, nic_boot, sizeof(nic_boot));
  Net_GetNIC_IP(interface, nic_ip, sizeof(nic_ip));
  Net_GetNIC_Netmask(interface, nic_netmask, sizeof(nic_netmask));
  Net_GetNIC_Gateway(interface, nic_gateway, sizeof(nic_gateway));

  return (Net_SetNIC_Up(interface, nic_boot, nic_protocol, nic_ip, nic_netmask, nic_gateway));
}

int
Net_SetNIC_IP(char *interface, char *nic_ip)
{
  //int status;
  char nic_boot[80], nic_protocol[80], nic_netmask[80], nic_gateway[80], old_ip[80];

  Net_GetNIC_IP(interface, old_ip, sizeof(old_ip));
  Net_GetNIC_Start(interface, nic_boot, sizeof(nic_boot));
  Net_GetNIC_Protocol(interface, nic_protocol, sizeof(nic_protocol));
  Net_GetNIC_Netmask(interface, nic_netmask, sizeof(nic_netmask));
  Net_GetNIC_Gateway(interface, nic_gateway, sizeof(nic_gateway));

  return (Net_SetNIC_Up(interface, nic_boot, nic_protocol, nic_ip, nic_netmask, nic_gateway));
}

int
Net_SetNIC_Netmask(char *interface, char *nic_netmask)
{
  char nic_boot[80], nic_protocol[80], nic_ip[80], nic_gateway[80];

  Net_GetNIC_Start(interface, nic_boot, sizeof(nic_boot));
  Net_GetNIC_Protocol(interface, nic_protocol, sizeof(nic_protocol));
  Net_GetNIC_IP(interface, nic_ip, sizeof(nic_ip));
  Net_GetNIC_Gateway(interface, nic_gateway, sizeof(nic_gateway));

  return (Net_SetNIC_Up(interface, nic_boot, nic_protocol, nic_ip, nic_netmask, nic_gateway));
}

int
Net_SetNIC_Gateway(char *interface, char *nic_gateway)
{
  char nic_boot[80], nic_protocol[80], nic_ip[80], nic_netmask[80];

  Net_GetNIC_Start(interface, nic_boot, sizeof(nic_boot));
  Net_GetNIC_Protocol(interface, nic_protocol, sizeof(nic_protocol));
  Net_GetNIC_IP(interface, nic_ip, sizeof(nic_ip));
  Net_GetNIC_Netmask(interface, nic_netmask, sizeof(nic_netmask));
  DPRINTF(("Net_SetNIC_Gateway:: interface %s onboot %s protocol %s ip %s netmask %s gateway %s\n", interface, nic_boot,
           nic_protocol, nic_ip, nic_netmask, nic_gateway));

  return (Net_SetNIC_Up(interface, nic_boot, nic_protocol, nic_ip, nic_netmask, nic_gateway));
}

int
Net_SetNIC_Up(char *interface, char *onboot, char *protocol, char *ip, char *netmask, char *gateway)
{
  int status;

  DPRINTF(("Net_SetNIC_Up:: interface %s onboot %s protocol %s ip %s netmask %s gateway %s\n", interface, onboot,
           protocol, ip, netmask, gateway));

  if (!Net_IsValid_Interface(interface))
    return -1;

  char onboot_bool[8], protocol_bool[8], old_ip[80];
  //char *new_gateway;

  if (strcmp(onboot, "onboot") == 0) {
    ink_strncpy(onboot_bool, "1", sizeof(onboot_bool));
  } else {
    ink_strncpy(onboot_bool, "0", sizeof(onboot_bool));
  }

  if (strcmp(protocol, "dhcp") == 0) {
    ink_strncpy(protocol_bool, "0", sizeof(protocol_bool));
  } else {
    ink_strncpy(protocol_bool, "1", sizeof(protocol_bool));
  }

  if (!Net_IsValid_IP(ip))
    return -1;

  if (!Net_IsValid_IP(netmask))
    return -1;

  Net_GetNIC_IP(interface, old_ip, sizeof(old_ip));

  //DPRINTF(("Net_SetNIC_Up: int %s prot %s ip %s net %s onboot %s gw %s\n",
  //interface, protocol_bool, ip, netmask, onboot_bool, new_gateway));

  //status = NetConfig_Action(NETCONFIG_INTF_UP, interface, protocol, ip, netmask, onboot, gateway);
  status = NetConfig_Action(NETCONFIG_INTF_UP, interface, protocol_bool, ip, netmask, onboot_bool, gateway);
  if (status) {
    DPRINTF(("Net_SetNIC_Up: NetConfig_Action returned %d\n", status));
    return status;
  }

  return status;
}


#if (HOST_OS == linux)

// Disable all previously disabled, and now active interfaces
int
Net_DisableInterface(char *interface)
{
  int status;

  DPRINTF(("Net_DisableInterface:: interface %s\n", interface));

  //if (!Net_IsValid_Interface(interface))
  //return -1;

  status = NetConfig_Action(NETCONFIG_INTF_DISABLE, interface);
  if (status) {
    DPRINTF(("Net_DisableInterface: NetConfig_Action returned %d\n", status));
    return status;
  }

  return status;
}

#endif


int
Net_IsValid_Interface(char *interface)
{
  char name[80];

  if (interface == NULL) {
    return 0;
  }
  int count = Net_GetNetworkIntCount();
  for (int i = 0; i < count; i++) {
    Net_GetNetworkInt(i, name, sizeof(name));
    if (strcmp(name, interface) == 0)
      return 1;
  }
  return 0;
}

int
Sys_User_Root(int *old_euid)
{

  *old_euid = getuid();
  seteuid(0);
  setreuid(0, 0);

  return 0;
}

int
Sys_User_Inktomi(int euid)
{
// bug 50394 - preserve saved uid as root, 
//             while changing effiective and real uid to input parameter value
  setreuid(euid, 0);
  seteuid(euid);
  return 0;
}

int
Sys_Grp_Root(int *old_egid)
{
  *old_egid = getegid();
  setregid(0, *old_egid);
  return 0;
}

int
Sys_Grp_Inktomi(int egid)
{
  setregid(egid, egid);
  return 0;
}


int
find_value(char *pathname, char *key, char *value, size_t value_len, char *delim, int no)
{
  char buffer[1024];
  char *pos;
  char *open_quot, *close_quot;
  FILE *fp;
  int find = 0;
  int counter = 0;

  int str_len = strlen(key);

  ink_strncpy(value, "", value_len);
  // coverity[fs_check_call]
  if (access(pathname, R_OK)) {
    return find;
  }
  // coverity[toctou]
  if ((fp = fopen(pathname, "r")) != NULL) {
    NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));

    while (!feof(fp)) {
      if (!isLineCommented(buffer) &&   // skip if line is commented
          (strstr(buffer, key) != NULL) && ((strncmp((buffer + str_len), "=", 1) == 0) ||
                                            (strncmp((buffer + str_len), " ", 1) == 0) ||
                                            (strncmp((buffer + str_len), "\t", 1) == 0))) {
        if (counter != no) {
          counter++;
        } else {
          find = 1;

          pos = strstr(buffer, delim);
          if (pos == NULL && (strcmp(delim, " ") == 0)) {       // anniec - give tab a try
            pos = strstr(buffer, "\t");
          }
          if (pos != NULL) {
            pos++;
            if ((open_quot = strchr(pos, '"')) != NULL) {
              pos = open_quot + 1;
              close_quot = strrchr(pos, '"');
              *close_quot = '\0';
            }
            //Bug49159, Dell use "'" in the ifcfg-ethx file
            else if ((open_quot = strchr(pos, '\'')) != NULL) {
              pos = open_quot + 1;
              close_quot = strrchr(pos, '\'');
              *close_quot = '\0';
            }
            //filter the comment on the same line
            char *cur;
            cur = pos;
            while (*cur != '#' && *cur != '\0') {
              cur++;
            }
            *cur = '\0';

            ink_strncpy(value, pos, value_len);

            if (value[strlen(value) - 1] == '\n') {
              value[strlen(value) - 1] = '\0';
            }
          }
          break;
        }
      }
      NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
    }
    fclose(fp);
  }
  return find;
}

int
Net_IsValid_Hostname(char *hostname)
{

  if (hostname == NULL) {
    return 0;
  } else if (strstr(hostname, " ") != NULL || hostname[strlen(hostname) - 1] == '.') {
    return 0;
  } else if (!recordRegexCheck(".+\\..+\\..+", hostname)) {
    return 0;
  }
  return 1;
}

// return 1 if the IP addr valid, return 0 if invalid
//    valid IP address is four decimal numbers (0-255) separated by dots
int
Net_IsValid_IP(char *ip_addr)
{
  char addr[80];
  char octet1[80], octet2[80], octet3[80], octet4[80];
  int byte1, byte2, byte3, byte4;
  char junk[256];

  if (ip_addr == NULL) {
    return 1;
  }

  ink_strncpy(addr, ip_addr, sizeof(addr));

  octet1[0] = '\0';
  octet2[0] = '\0';
  octet3[0] = '\0';
  octet4[0] = '\0';
  junk[0] = '\0';

  // each of the octet? is not shorter than addr, so no overflow will happen
  // disable coverity check in this case
  // coverity[secure_coding]
  int matches = sscanf(addr, "%[0-9].%[0-9].%[0-9].%[0-9]%[^0-9]",
                       octet1, octet2, octet3, octet4, junk);

  if (matches != 4) {
    return 0;
  }

  byte1 = octet1[0] ? atoi(octet1) : 0;
  byte2 = octet2[0] ? atoi(octet2) : 0;
  byte3 = octet3[0] ? atoi(octet3) : 0;
  byte4 = octet4[0] ? atoi(octet4) : 0;

  if (byte1<0 || byte1> 255 || byte2<0 || byte2> 255 || byte3<0 || byte3> 255 || byte4<0 || byte4> 255) {
    return 0;
  }

  if (strlen(junk)) {
    return 0;
  }

  return 1;
}

int
NetConfig_Action(int index, ...)
{
  char *argv[10];
  pid_t pid;
  int status;

  va_list ap;
  va_start(ap, index);

  argv[0] = "net_config";

  switch (index) {
  case NETCONFIG_HOSTNAME:
    argv[1] = "0";
    argv[2] = va_arg(ap, char *);
    argv[3] = va_arg(ap, char *);
    argv[4] = va_arg(ap, char *);
    argv[5] = NULL;
    break;
  case NETCONFIG_GATEWAY:
    argv[1] = "1";
    argv[2] = va_arg(ap, char *);
    argv[3] = va_arg(ap, char *);
    argv[4] = NULL;
    break;
  case NETCONFIG_DOMAIN:
    argv[1] = "2";
    argv[2] = va_arg(ap, char *);
    argv[3] = NULL;
    break;
  case NETCONFIG_DNS:
    argv[1] = "3";
    argv[2] = va_arg(ap, char *);
    argv[3] = NULL;
    break;
  case NETCONFIG_INTF_UP:
    argv[1] = "4";
    argv[2] = va_arg(ap, char *);       // nic_name
    argv[3] = va_arg(ap, char *);       // static_ip (1/0)
    argv[4] = va_arg(ap, char *);       // ip
    argv[5] = va_arg(ap, char *);       // netmask
    argv[6] = va_arg(ap, char *);       // onboot (1/0)
    argv[7] = va_arg(ap, char *);       // gateway_ip
    argv[8] = NULL;
    break;
  case NETCONFIG_INTF_DOWN:
    argv[1] = "5";
    argv[2] = va_arg(ap, char *);
    argv[3] = NULL;
    break;
  case NETCONFIG_INTF_DISABLE:
    argv[1] = "8";
    argv[2] = va_arg(ap, char *);
    argv[3] = NULL;
    break;
  }

  va_end(ap);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res;

    close(1);                   // close STDOUT
    close(2);                   // close STDERR

    char ts_path[256];
    char command_path[512];

    if (getTSdirectory(ts_path, sizeof(ts_path))) {
      DPRINTF(("[SysAPI] unable to determine install directory\n"));
      _exit(-1);
    }

    snprintf(command_path, sizeof(command_path), "%s/bin/net_config", ts_path);

    res = execv(command_path, argv);

    if (res != 0) {
      DPRINTF(("[SysAPI] fail to call net_config"));
    }
    _exit(res);
  }

  return 0;
}

bool
recordRegexCheck(const char *pattern, const char *value)
{
  regex_t regex;
  int result;
  if (regcomp(&regex, pattern, REG_NOSUB | REG_EXTENDED) != 0) {
    return false;
  }
  result = regexec(&regex, value, 0, NULL, 0);
  regfree(&regex);
  return (result == 0) ? true : false;
}

int
Time_SortTimezone()
{
  FILE *fp, *tmp;
  const char *zonetable = "/usr/share/zoneinfo/zone.tab";
  char buffer[1024];
  char *zone;

  fp = fopen(zonetable, "r");
  tmp = fopen("/tmp/zonetab.tmp", "w");
  if (fp == NULL || tmp == NULL) {
    DPRINTF(("[Time_SortTimezone] Can not open the file\n"));
    return -1;
  }
  NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
  while (!feof(fp)) {
    if (buffer[0] != '#') {
      strtok(buffer, " \t");
      strtok(NULL, " \t");
      zone = strtok(NULL, " \t");
      if (zone[strlen(zone) - 1] == '\n') {
        zone[strlen(zone) - 1] = '\0';
      }
      fprintf(tmp, "%s\n", zone);
    }
    NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
  }
  fclose(fp);
  fclose(tmp);
  remove("/tmp/zonetab");
  NOWARN_UNUSED_RETURN(system("/bin/sort /tmp/zonetab.tmp > /tmp/zonetab"));
  remove("/tmp/zonetab.tmp");

  return 0;
}

int
Time_GetTimezone(char *timezone, size_t timezone_len)
{
  //const char *zonetable="/usr/share/zoneinfo/zone.tab";
  //char buffer[1024];

  return (!find_value("/etc/sysconfig/clock", "ZONE", timezone, timezone_len, "=", 0));
}

int
Time_SetTimezone(bool restart, char *timezone)
{
  int status;

  status = TimeConfig_Action(3, restart, timezone);

  return status;
}

int
Net_GetEncryptedRootPassword(char **password)
{
  char shadowPasswd[1024];
  //int status = 0;
  int old_euid;
  int find = 0;
  //char *passwd = NULL;

  old_euid = getuid();
  seteuid(0);
  setreuid(0, 0);

  find = find_value("/etc/shadow", "root", shadowPasswd, sizeof(shadowPasswd), ":", 0);
  if (find == 0)
    *password = NULL;
  else
    *password = strtok(strdup(shadowPasswd), ":");
  setreuid(old_euid, old_euid);
  return 0;
}

int
getTSdirectory(char *ts_path, size_t ts_path_len)
{
  FILE *fp;
  char *env_path;

  // INST will set ROOT and INST_ROOT properly, try ROOT first
  if ((env_path = getenv("ROOT")) || (env_path = getenv("INST_ROOT"))) {
    ink_strncpy(ts_path, env_path, ts_path_len);
    return 0;
  }

  if ((fp = fopen("/etc/traffic_server", "r")) == NULL) {
    ink_strncpy(ts_path, "/home/trafficserver", ts_path_len);
    return 0;
  }

  if (fgets(ts_path, ts_path_len, fp) == NULL) {
    fclose(fp);
    return -1;
  }
  // strip newline if it exists
  int len = strlen(ts_path);
  if (ts_path[len - 1] == '\n') {
    ts_path[len - 1] = '\0';
  }
  // strip trailing "/" if it exists
  len = strlen(ts_path);
  if (ts_path[len - 1] == '/') {
    ts_path[len - 1] = '\0';
  }

  fclose(fp);
  return 0;
}

int
Time_GetTime(char *hour, const size_t hourSize, char *minute, const size_t minuteSize, char *second,
             const size_t secondSize)
{
  int status;
  struct tm *my_tm;
  struct timeval tv;

  status = gettimeofday(&tv, NULL);
  if (status != 0) {
    return status;
  }
  my_tm = localtime(&(tv.tv_sec));

  snprintf(hour, hourSize, "%d", my_tm->tm_hour);
  snprintf(minute, minuteSize, "%d", my_tm->tm_min);
  snprintf(second, secondSize, "%d", my_tm->tm_sec);

  return status;
}

int
Time_SetTime(bool restart, char *hour, char *minute, char *second)
{
  int status;

  status = TimeConfig_Action(TIMECONFIG_TIME, restart, hour, minute, second);

  return status;
}


int
Time_GetDate(char *month, const size_t monthSize, char *day, const size_t daySize, char *year, const size_t yearSize)
{
  int status;
  struct tm *my_tm;
  struct timeval tv;

  status = gettimeofday(&tv, NULL);
  if (status != 0) {
    return status;
  }
  my_tm = localtime(&(tv.tv_sec));

  snprintf(month, monthSize, "%d", my_tm->tm_mon + 1);
  snprintf(day, daySize, "%d", my_tm->tm_mday);
  snprintf(year, yearSize, "%d", my_tm->tm_year + 1900);

  return status;
}

int
Time_SetDate(bool restart, char *month, char *day, char *year)
{
  int status;

  status = TimeConfig_Action(TIMECONFIG_DATE, restart, month, day, year);

  return status;
}

int
Time_GetNTP_Servers(char *server, size_t server_len)
{
  ink_strncpy(server, "", server_len);
  return (!find_value("/etc/ntp.conf", "server", server, server_len, " ", 0));
}

int
Time_SetNTP_Servers(bool restart, char *server)
{
  int status;



  status = TimeConfig_Action(TIMECONFIG_NTP, restart, server);

  return status;
}

int
Time_GetNTP_Server(char *server, size_t server_len, int no)
{
  ink_strncpy(server, "", server_len);
  return (!find_value("/etc/ntp.conf", "server", server, server_len, " ", no));
}


int
Time_GetNTP_Status(char *status, size_t status_len)
{
  FILE *fp;
  char buffer[1024];

  ink_strncpy(status, "", status_len);

  fp = popen("/etc/init.d/ntpd status", "r");
  NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
  if (strstr(buffer, "running") != NULL) {
    ink_strncpy(status, "on", status_len);
  } else {
    ink_strncpy(status, "off", status_len);
  }

  pclose(fp);
  return 0;
}

int
Time_SetNTP_Off()
{
  int status;

  status = system("/etc/init.d/ntpd stop");
  status = system("/sbin/chkconfig --level 2345 ntpd off");
  return status;
}

int
TimeConfig_Action(int index, bool restart ...)
{
  char *argv[20];
  pid_t pid;
  int status;

  va_list ap;
  va_start(ap, restart);

  argv[0] = "time_config";
  if (restart) {
    argv[1] = "1";
  } else {
    argv[1] = "0";
  }

  switch (index) {
  case TIMECONFIG_TIME:
    argv[2] = "1";
    argv[3] = va_arg(ap, char *);
    argv[4] = va_arg(ap, char *);
    argv[5] = va_arg(ap, char *);
    argv[6] = NULL;
    break;
  case TIMECONFIG_DATE:
    argv[2] = "2";
    argv[3] = va_arg(ap, char *);
    argv[4] = va_arg(ap, char *);
    argv[5] = va_arg(ap, char *);
    argv[6] = NULL;
    break;
  case TIMECONFIG_TIMEZONE:
    argv[2] = "3";
    argv[3] = va_arg(ap, char *);
    argv[4] = NULL;
    break;
  case TIMECONFIG_NTP:
    argv[2] = "4";
    argv[3] = va_arg(ap, char *);
    argv[4] = NULL;
    break;
  }
  va_end(ap);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res;

    //close(1);  // close STDOUT
    //close(2);  // close STDERR

    char ts_path[256];
    char command_path[512];

    if (getTSdirectory(ts_path, sizeof(ts_path))) {
      DPRINTF(("[SysAPI] unable to determine install directory\n"));
      _exit(-1);
    }
    snprintf(command_path, sizeof(command_path), "%s/bin/time_config", ts_path);

    res = execv(command_path, argv);

    if (res != 0) {
      DPRINTF(("[SysAPI] fail to call time_config"));
    }
    _exit(res);
  }
  return 0;
}



int
Net_SetEncryptedRootPassword(char *password)
{


  char *remainingTokens;
  FILE *fp, *tmp;
  char buffer[1025];
  int old_euid;

  old_euid = getuid();
  seteuid(0);
  setreuid(0, 0);


  fp = fopen("/etc/shadow", "r");
  tmp = fopen("/tmp/shadow", "w");
  if ((fp != NULL) && (tmp != NULL)) {
    NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
    while (!feof(fp)) {
      if (strncmp(buffer, "root", 4) != 0) {
        fputs(buffer, tmp);
      } else {
        char *buf;
        if ((buf = strdup(buffer)) != NULL) {
          strtok_r(buf, ":", &remainingTokens);
          strtok_r(NULL, ":", &remainingTokens);
          fprintf(tmp, "root:%s:%s", password, remainingTokens);
          free(buf);
        }
      }
      NOWARN_UNUSED_RETURN(fgets(buffer, 1024, fp));
    }

    fclose(fp);
    fclose(tmp);
    NOWARN_UNUSED_RETURN(system("/bin/mv -f /tmp/shadow /etc/shadow"));
  }
  setreuid(old_euid, old_euid);
  return 0;
}

int
Net_SetSMTP_Server(char *server)
{
  return 0;
}

int
Net_GetSMTP_Server(char *server)
{
  return 0;
}

#define MV_BINARY "/bin/mv"
#define SNMP_PATH "/etc/snmp/snmpd.conf"
#define TS_SNMP_PATH "snmpd.cnf"


//main SNMP setup funciton
int
setSNMP(char *sys_location, char *sys_contact, char *sys_name, char *authtrapenable, char *trap_community,
        char *trap_host)
{
  char snmp_path[1024], ts_snmp_path[1024], snmp_path_new[1024], ts_snmp_path_new[1024], buffer[1024],
    ts_base_dir[1024], buf[1024];
  char *tmp, tmp1[1024];
  FILE *fp, *ts_file, *fp1, *fp_ts, *fp1_ts, *snmppass_fp, *snmppass_fp1;
  pid_t pid;
  int i, status;
  char *mv_binary = MV_BINARY;

  bool sys_location_flag = false;
  bool sys_contact_flag = false;
  bool sys_name_flag = false;
  bool authtrapenable_flag = false;
  bool trap_community_flag = false;
  bool trap_host_flag = false;
  char *env_path;

  DPRINTF(("setSNMP(): sys_location: %s, sys_contact: %s, sys_name: %s, authtrapenable: %s, trap_community: %s, trap_host: %s\n", sys_location, sys_contact, sys_name, authtrapenable, trap_community, trap_host));

  //first open the snmp files
  snprintf(snmp_path, sizeof(snmp_path), "%s", SNMP_PATH);
  if ((fp = fopen(snmp_path, "r")) == NULL && (fp = fopen(snmp_path, "a+")) == NULL) {
    DPRINTF(("[SysAPI] failed to open snmp configuration file"));
    return 1;
  }

  if ((env_path = getenv("ROOT")) || (env_path = getenv("INST_ROOT"))) {
    ink_strncpy(ts_base_dir, env_path, sizeof(ts_base_dir));
  } else {
    if ((ts_file = fopen("/etc/traffic_server", "r")) == NULL) {
      ink_strncpy(ts_base_dir, "/home/trafficserver", sizeof(ts_base_dir));
    } else {
      NOWARN_UNUSED_RETURN(fgets(buffer, sizeof(buf), ts_file));
      fclose(ts_file);

      for (i = 0; !isspace(buffer[i]); i++)
        ts_base_dir[i] = buffer[i];
      ts_base_dir[i] = '\0';
    }
  }

  snprintf(ts_snmp_path, sizeof(ts_snmp_path), "%s/conf/yts/%s", ts_base_dir, TS_SNMP_PATH);
  if ((fp_ts = fopen(ts_snmp_path, "r")) == NULL && (fp_ts = fopen(ts_snmp_path, "a+")) == NULL) {
    DPRINTF(("[SysAPI] failed to open ts snmp configuration file"));
    fclose(fp);
    return 1;
  }
  snprintf(snmp_path_new, sizeof(snmp_path_new), "%s.new", SNMP_PATH);
  if ((fp1 = fopen(snmp_path_new, "w")) == NULL) {
    DPRINTF(("[SysAPI] failed to open new snmp configuration file"));
    return 1;
  }
  snprintf(ts_snmp_path_new, sizeof(ts_snmp_path_new), "%s/%s.new", ts_base_dir, TS_SNMP_PATH);
  if ((fp1_ts = fopen(ts_snmp_path_new, "w")) == NULL) {
    DPRINTF(("[SysAPI] failed to open new ts snmp configuration file"));
    return 1;
  }
  //first handle the Linux snmp
  buf[0] = 0;                   /* lv: == strcpy(buf, ""); */
  NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp));

  while (!feof(fp)) {
    if (!isLineCommented(buf)) {
      if ((strncasecmp(buf, "syslocation", 11) == 0) && (sys_location != NULL)) {
        fprintf(fp1, "syslocation %s\n", sys_location);
        sys_location_flag = true;
      } else if ((strncasecmp(buf, "syscontact", 10) == 0) && (sys_contact != NULL)) {
        fprintf(fp1, "syscontact %s\n", sys_contact);
        sys_contact_flag = true;
      } else if ((strncasecmp(buf, "sysName", 7) == 0) && (sys_name != NULL)) {
        fprintf(fp1, "sysname %s\n", sys_name);
        sys_name_flag = true;
      } else if ((strncasecmp(buf, "authtrapenable", 14) == 0) && (authtrapenable != NULL)) {
        fprintf(fp1, "authtrapenable %s\n", authtrapenable);
        authtrapenable_flag = true;
      } else if ((strncasecmp(buf, "com2sec", 7) == 0) && (trap_community != NULL)) {
        fprintf(fp1, "com2sec notConfigUser default %s\n", trap_community);
        trap_community_flag = true;
      } else if ((strncasecmp(buf, "trapsink", 8) == 0) && (trap_host != NULL)) {
        fprintf(fp1, "trapsink %s\n", trap_host);
        trap_host_flag = true;
      } else                    // leave the old value as it is
        fputs(buf, fp1);
    } else {                    //if isLineCommented
      fputs(buf, fp1);          //only in case of a comment
    }
    NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp));
  }

  //now check which of the options is not set, and set it...
  if ((!sys_location_flag) && (sys_location != NULL))
    fprintf(fp1, "syslocation %s\n", sys_location);
  if ((!sys_contact_flag) && sys_contact != NULL)
    fprintf(fp1, "syscontact %s\n", sys_contact);
  if ((!sys_name_flag) && sys_name != NULL)
    fprintf(fp1, "sysname %s\n", sys_name);
  if ((!authtrapenable_flag) && (authtrapenable != NULL))
    fprintf(fp1, "authtrapenable %s\n", authtrapenable);
  if ((!trap_community_flag) && (trap_community != NULL))
    fprintf(fp1, "com2sec notConfigUser default %s\n", trap_community);
  if ((!trap_host_flag) && (trap_host != NULL))
    fprintf(fp1, "trapsink %s\n", trap_host);
  fclose(fp);
  fclose(fp1);

  //now handle TS snmp
  buf[0] = 0;
  NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp_ts));
  while (!feof(fp_ts)) {
    if (!isLineCommented(buf)) {
      if ((strncasecmp(buf, "sysLocation", 11) == 0) && (sys_location != NULL)) {
        fprintf(fp1_ts, "sysLocation %s\n", sys_location);
        sys_location_flag = true;
      } else if ((strncasecmp(buf, "sysContact", 10) == 0) && (sys_contact != NULL)) {
        fprintf(fp1_ts, "sysContact %s\n", sys_contact);
        sys_contact_flag = true;
      } else if ((strncasecmp(buf, "sysName", 7) == 0) && (sys_name != NULL)) {
        fprintf(fp1_ts, "sysName %s\n", sys_name);
        sys_name_flag = true;
      } else if ((strncasecmp(buf, "snmpEnableAuthenTraps", 21) == 0) && (authtrapenable != NULL)) {
        fprintf(fp1_ts, "snmpEnableAuthenTraps %s\n", authtrapenable);
        authtrapenable_flag = true;
      } else if ((strncasecmp(buf, "snmpCommunityEntry", 18) == 0) && (trap_community != NULL)) {
        fprintf(fp1_ts, "snmpCommunityEntry t0000000 %s public localSnmpID - - nonVolatile\n", trap_community);
        trap_community_flag = true;
      } else if ((strncasecmp(buf, "snmpTargetAddrEntry", 19) == 0) && (trap_host != NULL)) {
        tmp1[0] = 0;
        int i = 0;
        tmp = strtok(buf, "  ");
        strncat(tmp1, tmp, (sizeof(tmp1) - strlen(tmp1) - 1));
        strncat(tmp1, " ", (sizeof(tmp1) - strlen(tmp1) - 1));
        while ((tmp = strtok(NULL, "  ")) != NULL) {
          i++;
          if (i != 3) {
            strncat(tmp1, tmp, (sizeof(tmp1) - strlen(tmp1) - 1));
            strncat(tmp1, " ", (sizeof(tmp1) - strlen(tmp1) - 1));
          } else {
            strncat(tmp1, trap_host, (sizeof(tmp1) - strlen(tmp1) - 1));
            strncat(tmp1, ":0 ", (sizeof(tmp1) - strlen(tmp1) - 1));
          }
        }
        fprintf(fp1_ts, "%s", tmp1);
        trap_host_flag = true;
      } else                    // leave the old value as it is
        fputs(buf, fp1_ts);
    } else {                    //if isLineCommented
      fputs(buf, fp1_ts);       //only in case of a comment
    }
    NOWARN_UNUSED_RETURN(fgets(buf, sizeof(buf), fp_ts));
  }

  //now check which of the options is not set, and set it...
  if ((!sys_location_flag) && (sys_location != NULL))
    fprintf(fp1_ts, "sysLocation %s\n", sys_location);
  if ((!sys_contact_flag) && sys_contact != NULL)
    fprintf(fp1_ts, "sysContact %s\n", sys_contact);
  if ((!sys_name_flag) && sys_name != NULL)
    fprintf(fp1_ts, "sysName %s\n", sys_name);
  if ((!authtrapenable_flag) && (authtrapenable != NULL))
    fprintf(fp1_ts, "snmpEnableAuthenTraps %s\n", authtrapenable);
  if ((!trap_community_flag) && (trap_community != NULL))
    fprintf(fp1_ts, "snmpCommunityEntry t0000000 %s public localSnmpID - - nonVolatile\n ", trap_community);
  //if ((!trap_host_flag) &&(trap_host!=NULL)) - This can't be added as it is ID dependant
  fclose(fp_ts);
  fclose(fp1_ts);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
    {
      // Determine wheel's GID.
      struct group *gr;
      int wheel_gid = 0;
      setgrent();
      while ((gr = getgrent()) != NULL) {
        if (!strcasecmp(gr->gr_name, "wheel")) {
          wheel_gid = gr->gr_gid;
          break;
        }
      }
      endgrent();
      chmod(snmp_path, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH);
      // cast type in order to get rid of compiling error. disable coverity check here
      // coverity[cast_to_qualified_type]
      if (chown(snmp_path, (const int) -1, wheel_gid) == (-1)) {
        DPRINTF(("[SysAPI] can not chown new ts_snmp cfg file"));
      }
    }
  } else {
    int res;
    res = execl(mv_binary, "mv", snmp_path_new, snmp_path, NULL);
    if (res != 0) {
      DPRINTF(("[SysAPI] mv of new snmp cfg file failed "));
    }
    _exit(res);
  }

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    wait(&status);
  } else {
    int res;
    res = execl(mv_binary, "mv", ts_snmp_path_new, ts_snmp_path, NULL);
    if (res != 0) {
      DPRINTF(("[SysAPI] mv of new ts_snmp cfg file failed "));
    }
    _exit(res);
  }

  //now we need to change snmppass.sh or the community strings won't match

  char community_string[1024], snmp_pass[1024], snmp_pass_new[1024];
  const char *community1 = "result=`$COMMAND $VERSION -O fn -p $PORT localhost";
  const char *community2 = "$OID.0`;";

  if (trap_community != NULL) {
    snprintf(community_string, sizeof(community_string), "%s %s %s\n", community1, trap_community, community2);

    //now open the file for modification.

    snprintf(snmp_pass, sizeof(snmp_pass), "%s/bin/snmppass.sh", ts_base_dir);
    if ((snmppass_fp = fopen(snmp_pass, "r")) == NULL) {
      if ((fp = fopen(snmp_pass, "a+")) == NULL) {
        DPRINTF(("[SysAPI] failed to open ts snmp script file"));
        return 1;
      }
      fclose(fp);
    }
    snprintf(snmp_pass_new, sizeof(snmp_pass_new), "%s/bin/snmppass.sh.new", ts_base_dir);
    if ((snmppass_fp1 = fopen(snmp_pass_new, "w")) == NULL) {
      DPRINTF(("[SysAPI] failed to open new snmp script file"));
      return 1;
    }

    buf[0] = 0;
    NOWARN_UNUSED_RETURN(fgets(buf, 1024, snmppass_fp));
    while (!feof(snmppass_fp)) {
      if (!isLineCommented(buf)) {
        if (strncasecmp(buf, "result=", 7) == 0) {
          fprintf(snmppass_fp1, "%s\n", community_string);
        } else                  // leave the old value as it is
          fputs(buf, snmppass_fp1);
      } else {                  //if isLineCommented
        fputs(buf, snmppass_fp1);       //only in case of a comment
      }
      NOWARN_UNUSED_RETURN(fgets(buf, 1024, snmppass_fp));
    }
    fclose(snmppass_fp);
    fclose(snmppass_fp1);


    if ((pid = fork()) < 0) {
      exit(1);
    } else if (pid > 0) {
      wait(&status);
    } else {
      int res;
      res = execl(mv_binary, "mv", snmp_pass_new, snmp_pass, NULL);
      if (res != 0) {
        DPRINTF(("[SysAPI] mv of new snmpass.sh file failed "));
      }
      _exit(res);
    }
    //now change the file permissions BZ50331
    int flag_status;
    flag_status = chmod(snmp_pass, S_IRUSR | S_IWUSR | S_IXUSR | S_IRGRP | S_IWGRP | S_IXGRP | S_IROTH | S_IXOTH);
    if (flag_status)
      DPRINTF(("[SYSAPI]: Failed to change permissions of snmpass.sh"));
  }

  return 0;
}


int
Net_SNMPSetUp(char *sys_location, char *sys_contact, char *sys_name, char *authtrapenable, char *trap_community,
              char *trap_host)
{
  int status;
  pid_t pid;

  DPRINTF(("Net_SNMPSetUp: sys_location: %s, sys_contact: %s, sys_name: %s, authtrapenable: %s, trap_community: %s, trap_host: %s\n", sys_location, sys_contact, sys_name, authtrapenable, trap_community, trap_host));

  status = setSNMP(sys_location, sys_contact, sys_name, authtrapenable, trap_community, trap_host);
  if (status) {
    DPRINTF(("Net_SNMPSetUp: NetConfig_Action returned %d\n", status));
    return status;
  }
  //now we need to stop and start snmp daemons. We assume encapsulation so we can use TS snmp script
  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    //wait(&status);
  } else {
    int res;

    char ts_path[256];
    char command_path[512];

    if (getTSdirectory(ts_path, sizeof(ts_path))) {
      DPRINTF(("[SysAPI] unable to determine install directory\n"));
      _exit(-1);
    }
    snprintf(command_path, sizeof(command_path), "%s/bin/stop_snmp && %s/bin/start_snmp", ts_path, ts_path);

    sleep(30);
    res = execv(command_path, NULL);

    if (res != 0) {
      DPRINTF(("[SysAPI] failed to execute stop_snmp && start_snmp"));
    }
    _exit(res);
  }

  return status;
}

//location - 1 is always the location of the named parameter in the buffer
int
Get_Value(char *buffer, size_t buffer_len, char *buf, int location)
{
  char *tmp_buffer;
  int i = 1;

  ink_strncpy(buffer, strtok(buf, " "), buffer_len);

  if (buffer[0] == '"') {
    tmp_buffer = strtok(NULL, "\"");
    strncat(buffer, tmp_buffer, (buffer_len - strlen(buffer) - 1));
    strncat(buffer, "\"", (buffer_len - strlen(buffer) - 1));
  }

  if (location == 1) {
    char *ptr = strchr(buffer, '\n');
    if (ptr != NULL)
      *ptr = '\0';
    return 0;
  }
  // Fix for bz50331 - read in quotes from SNMP files
  while ((tmp_buffer = strtok(NULL, " ")) != NULL) {
    if (tmp_buffer[0] == '"') {
      ink_strncpy(buffer, tmp_buffer, buffer_len);
      tmp_buffer = strtok(NULL, "\"");
      strncat(buffer, " ", (buffer_len - strlen(buffer) - 1));
      strncat(buffer, tmp_buffer, (buffer_len - strlen(buffer) - 1));
      strncat(buffer, "\"", (buffer_len - strlen(buffer) - 1));
    } else
      ink_strncpy(buffer, tmp_buffer, buffer_len);
    i++;
    if (i == location) {
      char *ptr = strchr(buffer, '\n');
      if (ptr != NULL)
        *ptr = '\0';
      return 0;
    }
  }
  //if we are - we didn't get the value...
  return -1;
}


// we are assuming ts snmp configuration is identical to the Linux one, therefore, we will retrieve the info from TS snmp config file
int
Net_SNMPGetInfo(char *sys_location, size_t sys_location_len, char *sys_contact, size_t sys_contact_len, char *sys_name,
                size_t sys_name_len, char *authtrapenable, size_t authtrapenable_len, char *trap_community,
                size_t trap_community_len, char *trap_host, size_t trap_host_len)
{
  char ts_path[256], snmp_config[256], buf[1024];
  FILE *fp;
  int status;

  if (getTSdirectory(ts_path, sizeof(ts_path))) {
    DPRINTF(("[SysAPI]:SNMPGetInfo: unable to determine TS install directory\n"));
    return -1;
  }

  snprintf(snmp_config, sizeof(snmp_config), "%s/conf/yts/snmpd.cnf", ts_path);
  if ((fp = fopen(snmp_config, "r")) == NULL && (fp = fopen(snmp_config, "a+")) == NULL) {
    DPRINTF(("[SysAPI] failed to open ts snmp script file"));
    return 1;
  }

  NOWARN_UNUSED_RETURN(fgets(buf, 1024, fp));
  while (!feof(fp)) {
    if (!isLineCommented(buf)) {
      if (strncasecmp(buf, "sysLocation", 11) == 0) {
        status = Get_Value(sys_location, sys_location_len, buf, 2);
        if (status) {           //this means we didn't find the value - set it to NULL
          sys_location[0] = '\0';
        }
      } else if (strncasecmp(buf, "syscontact", 10) == 0) {
        status = Get_Value(sys_contact, sys_contact_len, buf, 2);
        if (status) {
          sys_contact[0] = '\0';
        }
      } else if (strncasecmp(buf, "sysName", 7) == 0) {
        status = Get_Value(sys_name, sys_name_len, buf, 2);
        if (status) {
          sys_name[0] = '\0';
        }
      } else if (strncasecmp(buf, "snmpEnableAuthenTraps", 21) == 0) {
        status = Get_Value(authtrapenable, authtrapenable_len, buf, 2);
        if (status) {
          authtrapenable[0] = '\0';
        }
      } else if (strncasecmp(buf, "snmpCommunityEntry", 18) == 0) {
        status = Get_Value(trap_community, trap_community_len, buf, 3);
        if (status) {
          trap_community[0] = '\0';
        }
      } else if (strncasecmp(buf, "snmpTargetAddrEntry", 19) == 0) {
        status = Get_Value(trap_host, trap_host_len, buf, 4);
        if (status) {
          trap_host[0] = '\0';
        } else {
          char *trap_temp = strtok(trap_host, ":");
          ink_strncpy(trap_host, trap_temp, trap_host_len);
        }
      }
    }
    NOWARN_UNUSED_RETURN(fgets(buf, 1024, fp));
  }

  fclose(fp);
  return 0;
}

#endif

#if (HOST_OS == sunos)

#include "SysAPI.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <stdarg.h>
#include <string.h>
#include <regex.h>

#include <ctype.h>
#include "../api2/include/INKMgmtAPI.h"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/types.h>

#include "ParseRules.h"

#define NETCONFIG_HOSTNAME  0
#define NETCONFIG_GATEWAY   1
#define NETCONFIG_DOMAIN    2
#define NETCONFIG_DNS       3
#define NETCONFIG_INTF_UP   4
#define NETCONFIG_INTF_DOWN 5

#define TIMECONFIG_ALL	    0
#define TIMECONFIG_TIME     1
#define TIMECONFIG_DATE     2
#define TIMECONFIG_TIMEZONE 3
#define TIMECONFIG_NTP      4

//#define DEBUG_SYSAPI
#ifdef DEBUG_SYSAPI
#define DPRINTF(x)  printf x
#else
#define DPRINTF(x)
#endif

const char *
strcasestr(char *container, char *substr)
{
  return ParseRules::strcasestr(container, substr);
}

// go through string, ane terminate it with \0
void
makeStr(char *str)
{
  char *p = str;
  while (*p) {
    if (*p == '\n') {
      *p = '\0';
      return;
    }
    p++;
  }
}

int NetConfig_Action(int index, ...);
int TimeConfig_Action(int index, bool restart, ...);
int Net_GetNIC_Values(char *interface, char *status, char *onboot, char *static_ip, char *ip, char *netmask,
                      char *gateway);
int find_value(char *pathname, char *key, char *value, char *delim, int no);
static bool recordRegexCheck(const char *pattern, const char *value);
static int getTSdirectory(char *ts_path, size_t ts_path_len);

int
Net_GetHostname(char *hostname)
{
  strcpy(hostname, "");
  return (gethostname(hostname, 256));
}

int
Net_SetHostname(char *hostname)
{
  int status;
  char old_hostname[256], protocol[80];
  char ip_addr[80], name[80], nic_status[80];
  bool found = false;

  old_hostname[0] = '\0';

  DPRINTF(("Net_SetHostname: hostname %s\n", hostname));

  if (!Net_IsValid_Hostname(hostname)) {
    DPRINTF(("Net_SetHostname: invalid hostname\n"));
    return -1;
  }

  Net_GetHostname(old_hostname);
  if (!strlen(old_hostname)) {
    DPRINTF(("Net_SetHostname: failed to get old_hostname\n"));
    return -1;
  }
  //Fix for BZ48925 - adding the correct ip to /etc/hosts
  //First get an IP of a valid interface - we don't care so much which one as we don't
  //use it in TS - it is just a place holder for Real Proxy with no DNS server (see BZ38199)

  ip_addr[0] = 0;
  int count = Net_GetNetworkIntCount();
  if (count == 0) {             //this means we didn't find any interface
    ip_addr[0] = 0;
  } else {
    name[0] = 0;
    nic_status[0] = 0;
    protocol[0] = 0;
    for (int i = 0; i < count; i++) {   //since we are looping - we will get the "last" available IP - doesn't matter to us
      Net_GetNetworkInt(i, name);       //we know we have at least one
      if (name != NULL) {
        Net_GetNIC_Status(name, nic_status);
        Net_GetNIC_Protocol(name, protocol);
        if ((strcmp("up", nic_status) == 0) && (!found)) {
          //we can use this interface
          Net_GetNIC_IP(name, ip_addr);
          found = true;
        }
      }
    }
  }
  DPRINTF(("Net_SetHostname: calling INKSetHostname \"%s %s %s\"\n", hostname, old_hostname, ip_addr));
  status = NetConfig_Action(NETCONFIG_HOSTNAME, hostname, old_hostname, ip_addr);

  return status;
}

// return true if the line is commented out, or if line is blank
// return false otherwise
bool
isLineCommented(char *line)
{
  char *p = line;
  while (*p) {
    if (*p == '#')
      return true;
    if (!isspace(*p) && *p != '#')
      return false;
    p++;
  }
  return true;
}

int
getDefaultRouterViaNetstat(char *gateway)
{
  const int BUFLEN = 256;
  char command[BUFLEN], buffer[BUFLEN];
  int found_status = 1;

  // hme for UltraSpark5 le for Spark10
  snprintf(command, sizeof(command), "/usr/bin/netstat -rn | grep default | grep -v hme | grep -v le");

  FILE *fd = popen(command, "r");
  if (fd && fgets(buffer, BUFLEN, fd)) {
    char *p = buffer;
    char *gateway_ptr = gateway;
    while (*p && !isspace(*p))
      p++;                      // reach first white space
    while (*p && isspace(*p))
      p++;                      // skip white space
    while (*p && !isspace(*p))
      *(gateway_ptr++) = *(p++);
    *gateway_ptr = 0;
    found_status = 0;           // success 
  }
  pclose(fd);
  return found_status;
}

// if error, return -1
// if found return 0
// if not found and no error, return 1
int
Net_GetDefaultRouter(char *router, size_t router_len)
{

  router[0] = '\0';
  FILE *fd;
  const int BUFFLEN = 80;
  char buffer[BUFFLEN];
  char command[BUFFLEN];
  const char *GATEWAY_CONFIG = "/etc/defaultrouter";

  if ((fd = fopen(GATEWAY_CONFIG, "r")) == NULL) {
    DPRINTF(("[Net_GetDefaultRouter] failed to open file \"%s\"\n", GATEWAY_CONFIG));
    return -1;
  }

  char *state = fgets(buffer, BUFFLEN, fd);
  if (state == NULL)            // empty file, try netstat, this may be because of DHCP as primary
    return getDefaultRouterViaNetstat(router);

  /* healthy /etc/defaultrouter file, parse through this */

  bool found = false;
  while (!feof(fd) && !found) {
    if (!isLineCommented(buffer))       // skip # and blank
      found = true;
    else
      fgets(buffer, BUFFLEN, fd);
  }
  fclose(fd);

  if (found) {                  // a healthy line
    makeStr(buffer);
    // found, but not in IP format, need to look up in /etc/inet/hosts
    if (!Net_IsValid_IP(buffer)) {
      snprintf(command, sizeof(command), "grep %s /etc/inet/hosts", buffer);
      FILE *fd = popen(command, "r");
      if (fd == NULL) {
        pclose(fd);
        DPRINTF(("[Net_GetDefaultRouter] failed to open pipe\n"));
        return -1;
      }
      if (fgets(buffer, BUFFLEN, fd)) {
        char *p = buffer;
        while (*p && !isspace(*p) && (p - buffer) < router_len)
          *(router++) = *(p++);
        *router = 0;
      }
      fclose(fd);
    } else {                    // found, already in ip format, just need to cpy it
      char *p = buffer;
      while (*p && !isspace(*p) && (p - buffer) < router_len)
        *(router++) = *(p++);
      *router = 0;
    }
    return 0;
  }
  return 1;                     // follow Linux behavior, return 1 if not found, with no error
}

int
Net_SetDefaultRouter(char *router)
{
  int status;
  char old_router[80];

  DPRINTF(("Net_SetDefaultRouter: router %s\n", router));

  if (!Net_IsValid_IP(router)) {
    DPRINTF(("Net_SetDefaultRouter: invalid IP\n"));
    return -1;
  }


  Net_GetDefaultRouter(old_router, sizeof(old_router));
  if (!strlen(old_router)) {
    DPRINTF(("Net_SetHostname: failed to get old_router\n"));
    return -1;
  }

  status = NetConfig_Action(NETCONFIG_GATEWAY, router, old_router);
  DPRINTF(("Net_SetDefaultRouter: NetConfig_Action returned %d\n", status));
  if (status) {
    return status;
  }

  return status;
}

int
Net_GetDomain(char *domain)
{
  //  domain can be defined using search or domain keyword
  strcpy(domain, "");
  return !find_value("/etc/resolv.conf", "search", domain, " ", 0);
  /*  if there is bug file against this, we should search for domain keyword as well
     if (!find_value("/etc/resolv.conf", "search", domain, " ", 0)) {
     return (!find_value("/etc/resolv.conf", "domain", domain, " ", 0));
     }else
     return 0; 
   */
}

int
Net_SetDomain(char *domain)
{
  int status;

  DPRINTF(("Net_SetDomain: domain %s\n", domain));

  status = NetConfig_Action(NETCONFIG_DOMAIN, domain);
  if (status) {
    return status;
  }

  return status;
}

int
Net_GetDNS_Servers(char *dns)
{
  char ip[80];
  strcpy(dns, "");
  int i = 0;
  while (find_value("/etc/resolv.conf", "nameserver", ip, " ", i++)) {
    strcat(dns, ip);
    strcat(dns, " ");
  }
  return 0;
}

int
Net_SetDNS_Servers(char *dns)
{
  int status;
  char buff[512];
  char *tmp1, *tmp2;

  DPRINTF(("Net_SetDNS_Servers: dns %s\n", dns));

  if (dns == NULL) {
    return -1;
  }
  // check all IP addresses for validity
  strncpy(buff, dns, 512);
  tmp1 = buff;
  while ((tmp2 = strtok(tmp1, " \t")) != NULL) {
    DPRINTF(("Net_SetDNS_Servers: tmp2 %s\n", tmp2));
    if (!Net_IsValid_IP(tmp2)) {
      return -1;
    }
    tmp1 = NULL;
  }
  DPRINTF(("Net_SetDNS_Servers: dns %s\n", dns));
  status = NetConfig_Action(NETCONFIG_DNS, dns);
  if (status) {
    return status;
  }

  return status;
}

int
Net_GetDNS_Server(char *server, int no)
{
  strcpy(server, "");
  return (!find_value("/etc/resolv.conf", "nameserver", server, " ", no));
}

int
Net_GetNetworkIntCount()
{
  const int BUFFLEN = 80;
  char buffer[BUFFLEN];
  /* 
     get the list of network interfaces using pattern ^/etc/hostname.*[0-9]$
     need to get rid of any virtual IP definition which is defined with ':'
     Example of virtual IP definition is /etc/hostname.hme0:1
   */
  FILE *fd = popen("/bin/ls /etc/*hostname.*[0-9] | grep -v : | wc -l", "r");
  if (fd == NULL || fgets(buffer, BUFFLEN, fd) == NULL) {
    pclose(fd);
    DPRINTF(("[Net_GetNetworkIntCount] failed to open pipe\n"));
    return -1;
  }
  pclose(fd);
  return atoi(buffer);
}

int
Net_GetNetworkInt(int int_num, char *interface)
{
  strcpy(interface, "");

  const int BUFFLEN = 200;
  char buffer[BUFFLEN];

  /* 
     get the list of network interfaces using pattern ^/etc/hostname.*[0-9]$
     need to get rid of any virtual IP definition which is defined with ':'
     Example of virtual IP definition is /etc/hostname.hme0:1
   */

  FILE *fd = popen("/bin/ls /etc/*hostname.*[0-9] | grep -v :", "r");

  int i = 0;

  if (fd == NULL) {
    pclose(fd);
    DPRINTF(("[Net_GetNetworkInt] failed to open pipe\n"));
    return -1;
  }

  if (fgets(buffer, BUFFLEN, fd)) {
    while (!feof(fd) && i < int_num) {
      fgets(buffer, BUFFLEN, fd);
      i++;
    }

    if (i < int_num - 1) {
      pclose(fd);
      DPRINTF(("[Net_GetNetworkInt] failed to retrieved the interface\n"));
      return -1;
    }

    char *pos;
    if (strstr(buffer, "inkt")) // one of the inktomi backup file
      pos = buffer + strlen("/etc/inkt.save.hostname.");
    else
      pos = buffer + strlen("/etc/hostname.");

    if (pos != NULL) {
      strcpy(interface, pos);
      if (interface[strlen(interface) - 1] == '\n') {
        interface[strlen(interface) - 1] = '\0';
      }
    }
  }
  pclose(fd);
  return 0;
}

int
Net_GetNIC_Status(char *interface, char *status)
{
  const int BUFFLEN = 80;
  char buffer[BUFFLEN];
  char command[BUFFLEN];
  /* ifconfig -au shows all "up" interfaces in the system */
  snprintf(command, sizeof(command), "ifconfig -au | grep %s | wc -l", interface);
  FILE *fd = popen(command, "r");
  if (fd == NULL || fgets(buffer, BUFFLEN, fd) == NULL) {
    pclose(fd);
    DPRINTF(("[Net_GetNIC_Status] failed to open pipe\n"));
    return -1;
  }
  pclose(fd);
  if (atoi(buffer) == 1)
    strcpy(status, "up");
  else
    strcpy(status, "down");
  return 0;
}

int
Net_GetNIC_Start(char *interface, char *start)
{
  const int PATHLEN = 200;
  char hostnamefile[PATHLEN];
  FILE *fd;
  snprintf(hostnamefile, sizeof(hostnamefile), "/etc/hostname.%s", interface);

  // if /etc/hostname.<interface> file exist, return true, else return false
  if ((fd = fopen(hostnamefile, "r")) != NULL)
    strcpy(start, "onboot");
  else
    strcpy(start, "not-onboot");
  return 0;
}

int
Net_GetNIC_Protocol(char *interface, char *protocol)
{
  const int PATHLEN = 200;
  char dhcp_filename[PATHLEN];
  FILE *fd;
  snprintf(dhcp_filename, sizeof(dhcp_filename), "/etc/dhcp.%s", interface);

  if ((fd = fopen(dhcp_filename, "r")) == NULL)
    strcpy(protocol, "static");
  else
    strcpy(protocol, "dhcp");

  return 0;
}

int
parseIfconfig(char *interface, char *keyword, char *value)
{

  const int BUFFLEN = 200;
  char buffer[BUFFLEN];
  char command[BUFFLEN];
  FILE *fd;

  // first check if the interface is attached 
  snprintf(command, sizeof(command), "/sbin/ifconfig -a | grep %s", interface);
  fd = popen(command, "r");
  if (fd == NULL) {
    pclose(fd);
    DPRINTF(("[parseIfconfig ] failed to open pipe\n"));
    return -1;
  }
  if (fgets(buffer, BUFFLEN, fd) == NULL) {     // interface not found
    pclose(fd);
    return -1;
  }

  snprintf(command, sizeof(command), "/sbin/ifconfig %s", interface);
  fd = popen(command, "r");
  // can the first line
  if (fd == NULL || fgets(buffer, BUFFLEN, fd) == NULL) {
    pclose(fd);
    DPRINTF(("[parseIfconfig ] failed to open pipe\n"));
    return -1;
  }
  fgets(buffer, BUFFLEN, fd);
  char *pos = strstr(buffer, keyword);
  if (pos) {
    pos += strlen(keyword);
    while (*pos && !isspace(*pos))
      *(value++) = *(pos++);
    *value = 0;
  }
  return 0;
}


/*
  return number of heading matching bits for network address
 */
int
getMatchingBits(char *network, char *ip)
{

  unsigned int network_array[4];
  unsigned int ip_array[4];
  int count = 0, i = 0;

  sscanf(ip, "%u.%u.%u.%u", &ip_array[0], &ip_array[1], &ip_array[2], &ip_array[3]);
  sscanf(network, "%u.%u.%u.%u", &network_array[0], &network_array[1], &network_array[2], &network_array[3]);

  for (i = 0; i < 4; i++) {
    if (network_array[i] == ip_array[i])
      count += 8;
    else
      break;
  }

  if (count < 8 * 4) {
    unsigned char network_byte = network_array[i];
    unsigned char ip_byte = ip_array[i];

    int n = 8;                  // 8 bits
    unsigned char temp_mask = 1 << (n - 1);
    for (i = 0; i < n; i++) {
      if ((temp_mask & network_byte) == (temp_mask & ip_byte)) {
        count++;
      } else
        break;
      network_byte <<= 1;
      ip_byte <<= 1;
    }
  }
  return count;
}

int
Net_GetNIC_IP(char *interface, char *ip)
{
  strcpy(ip, "");               // bug 50628, initialize for null value
  int status = parseIfconfig(interface, "inet ", ip);
  if (status != 0) {            // in case of network down
    const int BUFFLEN = 1024;
    const int PATHLEN = 200;
    char command[PATHLEN], buffer[BUFFLEN], hostname_path[PATHLEN];
    char hostname[PATHLEN];
    FILE *fd;

    /* get hostname related to the nic */
    snprintf(hostname_path, sizeof(hostname_path), "/etc/hostname.%s", interface);
    if ((fd = fopen(hostname_path, "r")) == NULL) {     // could be in the backup file
      snprintf(hostname_path, sizeof(hostname_path), "/etc/inkt.save.hostname.%s", interface);
      if ((fd = fopen(hostname_path, "r")) == NULL) {
        DPRINTF(("[NET_GETNIC_IP] failed to open hostname configuration file"));
        return -1;
      }
    }
    if (fgets(hostname, PATHLEN, fd) == NULL) {
      DPRINTF(("[NET_GETNIC_IP] has empty %s file\n", hostname_path));
      return -1;
    }
    while (!feof(fd) && isLineCommented(hostname))      // skip # and blank
      fgets(hostname, PATHLEN, fd);

    if (!hostname) {
      DPRINTF(("[NET_GETNIC_IP] failed to get hostname"));
      return -1;
    }
    fclose(fd);

    /* lookup ip address entry in /etc/hosts file */
    makeStr(hostname);
    snprintf(command, sizeof(command), "grep %s /etc/inet/hosts", hostname);
    fd = popen(command, "r");
    if (fd && fgets(buffer, BUFFLEN, fd)) {
      char *p = buffer;
      while (*p && isspace(*p))
        p++;                    // skip white space
      char *tmp = ip;
      while (*p && !isspace(*p))
        *(tmp++) = *(p++);
      *tmp = 0;                 // finish filling ip
    }
    pclose(fd);
    status = 0;
  }
  return status;

}

int
Net_GetNIC_Netmask(char *interface, char *netmask, size_t netmask_len)
{
  strcpy(netmask, "");          // bug 50628, initialize for null value
  int status = parseIfconfig(interface, "netmask ", netmask);

  if (status != 0) {            // when network interface is down
    const int BUFFLEN = 1024;
    const int PATHLEN = 80;
    char ip_addr[PATHLEN], cur_network[PATHLEN], cur_netmask[PATHLEN];
    char buffer[BUFFLEN];
    char winnerMask[PATHLEN];
    int maxMatchingBits = 0;
    int curMatchingBits = 0;
    FILE *fd;


    status = Net_GetNIC_IP(interface, ip_addr);
    if (status != 0) {
      DPRINTF(("[NET_GETNIC_NETMASK] failed to obtain ip address"));
      return -1;
    }
    // go through /etc/inet/netmasks
    if ((fd = fopen("/etc/inet/netmasks", "r")) == NULL) {
      DPRINTF(("[NET_GETNIC_NETMASK] failed to open netmasks file"));
      return -1;
    }

    if (fgets(buffer, BUFFLEN, fd) == NULL) {
      DPRINTF(("[NET_GETNIC_NETMASK] empty config file"));
      return -1;
    }

    while (!feof(fd)) {
      if (!isLineCommented(buffer)) {
        // parse network and netmask
        char *p = buffer;
        while (*p && isspace(*p))
          p++;                  // skip white space
        char *tmp = cur_network;
        while (*p && !isspace(*p)) {
          *(tmp++) = *(p++);
        }
        *tmp = 0;               // finish filling cur_network
        while (*p && isspace(*p))
          p++;                  // skip white space
        tmp = cur_netmask;
        while (*p && !isspace(*p)) {
          *(tmp++) = *(p++);
        }
        *tmp = 0;               // finish filling cur_netmask

        // find best match
        curMatchingBits = getMatchingBits(cur_network, ip_addr);
        if (curMatchingBits > maxMatchingBits) {
          maxMatchingBits = curMatchingBits;
          ink_strncpy(winnerMask, cur_netmask, sizeof(winnerMask));
        }
      }
      fgets(buffer, BUFFLEN, fd);
    }
    if (maxMatchingBits > 0)
      strcpy(netmask, winnerMask);
    fclose(fd);
    status = 0;
  }

  if (!strstr(netmask, ".")) {  // not dotted format
    char temp[3];
    int oct[4];
    int j = 0;
    for (int i = 0; i < 4; i++, j += 2) {
      //       temp[0] = netmask[j+1];
      //temp[1] = netmask[j];
      temp[0] = netmask[j];
      temp[1] = netmask[j + 1];
      temp[2] = '\0';
      oct[i] = (int) strtol(temp, (char **) NULL, 16);
    }
    sprintf(netmask, "%d.%d.%d.%d", oct[0], oct[1], oct[2], oct[3]);
  }
  return status;

}

int
Net_GetNIC_Gateway(char *interface, char *gateway)
{
  // command is netstat -rn | grep <interface name> | grep G
  // the 2nd column is the Gateway
  strcpy(gateway, "");
  const int BUFFLEN = 200;
  char command[BUFFLEN];
  char buffer[BUFFLEN];
  snprintf(command, sizeof(command), "/usr/bin/netstat -rn | grep %s | grep G", interface);
  FILE *fd = popen(command, "r");

  if (fd && fgets(buffer, BUFFLEN, fd)) {       // gateway found
    char *p = buffer;
    while (*p && !isspace(*p))
      p++;                      // reach first white space
    while (*p && isspace(*p))
      p++;                      // skip white space
    while (*p && !isspace(*p))
      *(gateway++) = *(p++);
    *gateway = 0;
    return 0;
  } else                        // gateway not found
    return -1;
}

int
Net_SetNIC_Down(char *interface)
{
  int status;
  char ip[80];

  if (!Net_IsValid_Interface(interface))
    return -1;

  status = NetConfig_Action(NETCONFIG_INTF_DOWN, interface);
  if (status) {
    return status;
  }

  Net_GetNIC_IP(interface, ip);

  return status;
}

int
Net_SetNIC_StartOnBoot(char *interface, char *onboot)
{
  char nic_protocol[80], nic_ip[80], nic_netmask[80], nic_gateway[80];

  Net_GetNIC_Protocol(interface, nic_protocol);
  Net_GetNIC_IP(interface, nic_ip);
  Net_GetNIC_Netmask(interface, nic_netmask);
  Net_GetNIC_Gateway(interface, nic_gateway);

  return (Net_SetNIC_Up(interface, onboot, nic_protocol, nic_ip, nic_netmask, nic_gateway));
}

int
Net_SetNIC_BootProtocol(char *interface, char *nic_protocol)
{
  char nic_boot[80], nic_ip[80], nic_netmask[80], nic_gateway[80];

  Net_GetNIC_Start(interface, nic_boot);
  Net_GetNIC_IP(interface, nic_ip);
  Net_GetNIC_Netmask(interface, nic_netmask);
  Net_GetNIC_Gateway(interface, nic_gateway);

  return (Net_SetNIC_Up(interface, nic_boot, nic_protocol, nic_ip, nic_netmask, nic_gateway));
}

int
Net_SetNIC_IP(char *interface, char *nic_ip)
{
  //int status;
  char nic_boot[80], nic_protocol[80], nic_netmask[80], nic_gateway[80], old_ip[80];

  Net_GetNIC_IP(interface, old_ip);
  Net_GetNIC_Start(interface, nic_boot);
  Net_GetNIC_Protocol(interface, nic_protocol);
  Net_GetNIC_Netmask(interface, nic_netmask);
  Net_GetNIC_Gateway(interface, nic_gateway);

  return (Net_SetNIC_Up(interface, nic_boot, nic_protocol, nic_ip, nic_netmask, nic_gateway));
}

int
Net_SetNIC_Netmask(char *interface, char *nic_netmask)
{
  char nic_boot[80], nic_protocol[80], nic_ip[80], nic_gateway[80];

  Net_GetNIC_Start(interface, nic_boot);
  Net_GetNIC_Protocol(interface, nic_protocol);
  Net_GetNIC_IP(interface, nic_ip);
  Net_GetNIC_Gateway(interface, nic_gateway);

  return (Net_SetNIC_Up(interface, nic_boot, nic_protocol, nic_ip, nic_netmask, nic_gateway));
}

int
Net_SetNIC_Gateway(char *interface, char *nic_gateway)
{
  char nic_boot[80], nic_protocol[80], nic_ip[80], nic_netmask[80];

  Net_GetNIC_Start(interface, nic_boot);
  Net_GetNIC_Protocol(interface, nic_protocol);
  Net_GetNIC_IP(interface, nic_ip);
  Net_GetNIC_Netmask(interface, nic_netmask);
  //   DPRINTF(("Net_SetNIC_Gateway:: interface %s onboot %s protocol %s ip %s netmask %s gateway %s\n", interface, nic_boot, nic_protocol, nic_ip, nic_netmask, nic_gateway));

  return (Net_SetNIC_Up(interface, nic_boot, nic_protocol, nic_ip, nic_netmask, nic_gateway));
}

int
Net_SetNIC_Up(char *interface, char *onboot, char *protocol, char *ip, char *netmask, char *gateway)
{
  int status;

  if (gateway == NULL)
    gateway = "";

  //   DPRINTF(("Net_SetNIC_Up:: interface %s onboot %s protocol %s ip %s netmask %s gateway %s\n", interface, onboot, protocol, ip, netmask, gateway));

  if (!Net_IsValid_Interface(interface))
    return -1;

  if (!Net_IsValid_IP(ip))
    return -1;

  if (!Net_IsValid_IP(netmask))
    return -1;

  const int BUFFLEN = 200;
  char old_ip[BUFFLEN], old_mask[BUFFLEN], old_gateway[BUFFLEN], default_gateway[BUFFLEN];

  Net_GetNIC_IP(interface, old_ip);
  Net_GetNIC_Netmask(interface, old_mask);
  Net_GetNIC_Gateway(interface, old_gateway);
  Net_GetDefaultRouter(default_gateway, sizeof(default_gateway));

  if (strcmp(protocol, "static") == 0)
    strcpy(protocol, "1");
  else if (strcmp(protocol, "dhcp") == 0)
    strcpy(protocol, "0");


  if (strcmp(onboot, "onboot") == 0)
    strcpy(onboot, "1");
  else if (strcmp(onboot, "not-onboot") == 0)
    strcpy(onboot, "0");

  status = NetConfig_Action(NETCONFIG_INTF_UP, interface, protocol, ip, netmask, onboot, gateway,
                            old_ip, old_mask, old_gateway, default_gateway);

  if (status) {
    DPRINTF(("Net_SetNIC_Up: NetConfig_Action returned %d\n", status));
    return status;
  }

  return status;
}

int
Net_IsValid_Interface(char *interface)
{
  char name[80];

  if (interface == NULL) {
    return 0;
  }
  int count = Net_GetNetworkIntCount();
  for (int i = 0; i < count; i++) {
    Net_GetNetworkInt(i, name);
    if (strcmp(name, interface) == 0)
      return 1;
  }
  return 0;
}

int
find_value(char *pathname, char *key, char *value, char *delim, int no)
{
  char buffer[1024];
  char *pos;
  char *open_quot, *close_quot;
  FILE *fp;
  int find = 0;
  int counter = 0;

  strcpy(value, "");
  if (access(pathname, R_OK)) {
    return find;
  }


  fp = fopen(pathname, "r");

  char *state = fgets(buffer, 1024, fp);
  if (state == NULL) {          // empty file
    DPRINTF(("[find_value] has empty config file\n"));
    return -1;
  }

  while (!feof(fp)) {
    if (!isLineCommented(buffer) &&     // skip if line is commented
        strstr(buffer, key) != NULL) {
      if (counter != no) {
        counter++;
      } else {
        find = 1;

        pos = strstr(buffer, delim);
        if (pos == NULL && (strcmp(delim, " ") == 0)) { // anniec - give tab a try
          pos = strstr(buffer, "\t");
        }
        if (pos != NULL) {
          pos++;
          if ((open_quot = strchr(pos, '"')) != NULL) {
            pos = open_quot + 1;
            close_quot = strrchr(pos, '"');
            *close_quot = '\0';
          }
          strcpy(value, pos);


          if (value[strlen(value) - 1] == '\n') {
            value[strlen(value) - 1] = '\0';
          }
        }
        break;
      }
    }
    fgets(buffer, 80, fp);
  }
  return find;
}

int
Net_IsValid_Hostname(char *hostname)
{

  if (hostname == NULL) {
    return 0;
  } else if (strstr(hostname, " ") != NULL || hostname[strlen(hostname) - 1] == '.') {
    return 0;
  } else if (!recordRegexCheck(".+\\..+\\..+", hostname)) {
    return 0;
  }
  return 1;
}

// return 1 if the IP addr valid, return 0 if invalid
//    valid IP address is four decimal numbers (0-255) separated by dots
int
Net_IsValid_IP(char *ip_addr)
{
  char addr[80];
  char octet1[16], octet2[16], octet3[16], octet4[16];
  int byte1, byte2, byte3, byte4;
  char junk[256];

  if (ip_addr == NULL) {
    return 1;
  }

  ink_strncpy(addr, ip_addr, sizeof(addr));

  octet1[0] = '\0';
  octet2[0] = '\0';
  octet3[0] = '\0';
  octet4[0] = '\0';
  junk[0] = '\0';

  int matches = sscanf(addr, "%[0-9].%[0-9].%[0-9].%[0-9]%[^0-9]",
                       octet1, octet2, octet3, octet4, junk);

  if (matches != 4) {
    return 0;
  }

  if (octet1[0])
    byte1 = atoi(octet1);
  if (octet2[0])
    byte2 = atoi(octet2);
  if (octet3[0])
    byte3 = atoi(octet3);
  if (octet4[0])
    byte4 = atoi(octet4);

  if (byte1<0 || byte1> 255 || byte2<0 || byte2> 255 || byte3<0 || byte3> 255 || byte4<0 || byte4> 255) {
    return 0;
  }

  if (strlen(junk)) {
    return 0;
  }

  return 1;
}

int
NetConfig_Action(int index, ...)
{
  char *argv[20];
  pid_t pid;
  int status;

  va_list ap;
  va_start(ap, index);

  char ts_path[256];
  char command_path[512];


  if (getTSdirectory(ts_path, sizeof(ts_path))) {
    DPRINTF(("[SysAPI] unable to determine install directory\n"));
    _exit(-1);
  }

  snprintf(command_path, sizeof(command_path), "%s/bin/net_config", ts_path);

  argv[0] = command_path;

  switch (index) {
  case NETCONFIG_HOSTNAME:
    argv[1] = "0";
    argv[2] = va_arg(ap, char *);
    argv[3] = va_arg(ap, char *);
    argv[4] = va_arg(ap, char *);
    argv[5] = NULL;
    break;
  case NETCONFIG_GATEWAY:
    argv[1] = "1";
    argv[2] = va_arg(ap, char *);
    argv[3] = va_arg(ap, char *);
    argv[4] = NULL;
    break;
  case NETCONFIG_DOMAIN:
    argv[1] = "2";
    argv[2] = va_arg(ap, char *);
    argv[3] = NULL;
    break;
  case NETCONFIG_DNS:
    argv[1] = "3";
    argv[2] = va_arg(ap, char *);
    argv[3] = NULL;
    break;
  case NETCONFIG_INTF_UP:
    argv[1] = "4";
    argv[2] = va_arg(ap, char *);       // nic_name
    argv[3] = va_arg(ap, char *);       // static_ip (1/0)
    argv[4] = va_arg(ap, char *);       // ip
    argv[5] = va_arg(ap, char *);       // netmask
    argv[6] = va_arg(ap, char *);       // onboot (1/0)
    argv[7] = va_arg(ap, char *);       // gateway_ip
    argv[8] = va_arg(ap, char *);       // old_ip
    argv[9] = va_arg(ap, char *);       // old_mask
    argv[10] = va_arg(ap, char *);      // old_gateway
    argv[11] = va_arg(ap, char *);      // default_gateway
    argv[12] = NULL;
    break;
  case NETCONFIG_INTF_DOWN:
    argv[1] = "5";
    argv[2] = va_arg(ap, char *);
    argv[3] = NULL;
    break;
  }

  va_end(ap);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {
    int res;

    close(1);                   // close STDOUT
    close(2);                   // close STDERR
    seteuid(0);
    setreuid(0, 0);

    res = execv(command_path, argv);

    if (res != 0) {
      DPRINTF(("[SysAPI] fail to call net_config"));
    }
    _exit(res);
  }

  return 0;
}

bool
recordRegexCheck(const char *pattern, const char *value)
{
  regex_t regex;
  int result;
  if (regcomp(&regex, pattern, REG_NOSUB | REG_EXTENDED) != 0) {
    return false;
  }
  result = regexec(&regex, value, 0, NULL, 0);
  regfree(&regex);
  return (result == 0) ? true : false;
}

int
Time_SortTimezone()
{
  FILE *fp, *tmp;
  const char *zonetable = "/usr/share/zoneinfo/zone.tab";
  char buffer[1024];
  char *zone;

  fp = fopen(zonetable, "r");
  tmp = fopen("/tmp/zonetab.tmp", "w");
  if (fp == NULL || tmp == NULL) {
    DPRINTF(("[Time_SortTimezone] Can not open the file\n"));
    return -1;
  }
  fgets(buffer, 1024, fp);
  while (!feof(fp)) {
    if (buffer[0] != '#') {
      strtok(buffer, " \t");
      strtok(NULL, " \t");
      zone = strtok(NULL, " \t");
      if (zone[strlen(zone) - 1] == '\n') {
        zone[strlen(zone) - 1] = '\0';
      }
      fprintf(tmp, "%s\n", zone);
    }
    fgets(buffer, 1024, fp);
  }
  fclose(fp);
  fclose(tmp);
  remove("/tmp/zonetab");
  system("/bin/sort /tmp/zonetab.tmp > /tmp/zonetab");
  remove("/tmp/zonetab.tmp");

  return 0;
}

int
Time_GetTimezone(char *timezone)
{
  //const char *zonetable="/usr/share/zoneinfo/zone.tab";
  //char buffer[1024];

  return (!find_value("/etc/sysconfig/clock", "ZONE", timezone, "=", 0));
}



int
Time_SetTimezone(bool restart, char *timezone)
{
  int status;

  status = TimeConfig_Action(3, restart, timezone);

  return status;
}

int
Net_GetEncryptedRootPassword(char **password)
{
  char shadowPasswd[1024];
  //int status = 0;
  int old_euid;
  int find = 0;
  //char *passwd = NULL;

  old_euid = getuid();
  seteuid(0);
  setreuid(0, 0);

  find = find_value("/etc/shadow", "root", shadowPasswd, ":", 0);
  if (find == 0)
    *password = NULL;
  else
    *password = strtok(strdup(shadowPasswd), ":");
  setreuid(old_euid, old_euid);
  return 0;
}

int
getTSdirectory(char *ts_path, size_t ts_path_len)
{
  FILE *fp;
  char *env_path;

  if ((env_path = getenv("ROOT")) || (env_path = getenv("INST_ROOT"))) {
    ink_strcnpy(ts_path, env_path, ts_path_len);
    return 0;
  }
  if ((fp = fopen("/etc/traffic_server", "r")) == NULL) {
    ink_strncpy(ts_path, "/home/trafficserver", ts_path_len);
    return 0;
  }

  if (fgets(ts_path, ts_path_len, fp) == NULL) {
    fclose(fp);
    return -1;
  }
  // strip newline if it exists
  int len = strlen(ts_path);
  if (ts_path[len - 1] == '\n') {
    ts_path[len - 1] = '\0';
  }
  // strip trailing "/" if it exists
  len = strlen(ts_path);
  if (ts_path[len - 1] == '/') {
    ts_path[len - 1] = '\0';
  }

  fclose(fp);
  return 0;
}

int
Time_GetTime(char *hour, char *minute, char *second)
{
  int status;
  struct tm *my_tm;
  struct timeval tv;

  status = gettimeofday(&tv, NULL);
  if (status != 0) {
    return status;
  }
  my_tm = localtime(&(tv.tv_sec));

  sprintf(hour, "%d", my_tm->tm_hour);
  sprintf(minute, "%d", my_tm->tm_min);
  sprintf(second, "%d", my_tm->tm_sec);

  return status;
}

int
Time_SetTime(bool restart, char *hour, char *minute, char *second)
{
  int status;

  status = TimeConfig_Action(TIMECONFIG_TIME, restart, hour, minute, second);

  return status;
}


int
Time_GetDate(char *month, char *day, char *year)
{
  int status;
  struct tm *my_tm;
  struct timeval tv;

  status = gettimeofday(&tv, NULL);
  if (status != 0) {
    return status;
  }
  my_tm = localtime(&(tv.tv_sec));

  sprintf(month, "%d", my_tm->tm_mon + 1);
  sprintf(day, "%d", my_tm->tm_mday);
  sprintf(year, "%d", my_tm->tm_year + 1900);

  return status;
}

int
Time_SetDate(bool restart, char *month, char *day, char *year)
{
  int status;

  status = TimeConfig_Action(TIMECONFIG_DATE, restart, month, day, year);

  return status;
}

int
Time_GetNTP_Servers(char *server)
{
  FILE *fp;
  const char *ntpconf = "/etc/ntp.conf";
  char buffer[1024];
  char *option, *server_name;

  fp = fopen(ntpconf, "r");
  if (fp == NULL) {
    DPRINTF(("[Net_GetNTP_Servers] can not open the file /etc/net.conf\n"));
    return -1;
  }
  fgets(buffer, 1024, fp);
  while (!feof(fp)) {
    if (buffer[0] != '#') {
      option = strtok(buffer, " \t");
      if (strcmp(option, "server") == 0) {
        server_name = strtok(NULL, " \t");
        break;                  //Assume only one ntp server is in the ntp.conf
      }
    }
    fgets(buffer, 1024, fp);
  }
  if (server_name != NULL) {
    strcpy(server, server_name);
  }

  return 0;
}

int
Time_SetNTP_Servers(bool restart, char *server)
{
  int status;

  DPRINTF(("[Time_SetNTP_Servers] restart %d, server %s\n", restart, server));
  status = TimeConfig_Action(TIMECONFIG_NTP, restart, server);

  return status;
}

int
Time_GetNTP_Server(char *server, int no)
{
  return 0;
}

int
TimeConfig_Action(int index, bool restart ...)
{
  char *argv[20];
  pid_t pid;
  int status;

  va_list ap;
  va_start(ap, restart);

  argv[0] = "time_config";
  if (restart) {
    argv[1] = "1";
  } else {
    argv[1] = "0";
  }

  switch (index) {
  case TIMECONFIG_TIME:
    argv[2] = "1";
    argv[3] = va_arg(ap, char *);
    argv[4] = va_arg(ap, char *);
    argv[5] = va_arg(ap, char *);
    argv[6] = NULL;
    break;
  case TIMECONFIG_DATE:
    argv[2] = "2";
    argv[3] = va_arg(ap, char *);
    argv[4] = va_arg(ap, char *);
    argv[5] = va_arg(ap, char *);
    argv[6] = NULL;
    break;
  case TIMECONFIG_TIMEZONE:
    argv[2] = "3";
    argv[3] = va_arg(ap, char *);
    argv[4] = NULL;
    break;
  case TIMECONFIG_NTP:
    argv[2] = "4";
    argv[3] = va_arg(ap, char *);
    argv[4] = NULL;
    break;
  }
  va_end(ap);

  if ((pid = fork()) < 0) {
    exit(1);
  } else if (pid > 0) {
    waitpid(pid, &status, 0);
  } else {
    int res;

    //close(1);  // close STDOUT
    //close(2);  // close STDERR

    char ts_path[256];
    char command_path[512];

    if (getTSdirectory(ts_path, sizeof(ts_path))) {
      DPRINTF(("[SysAPI] unable to determine install directory\n"));
      _exit(-1);
    }
    snprintf(command_path, sizeof(command_path), "%s/bin/time_config", ts_path);

    res = execv(command_path, argv);

    if (res != 0) {
      DPRINTF(("[SysAPI] fail to call time_config"));
    }
    _exit(res);
  }

  return 0;
}

int
Net_SetEncryptedRootPassword(char *password)
{
  char *remainingTokens;
  FILE *fp, *tmp;
  char buffer[1025];
  int old_euid;

  old_euid = getuid();
  seteuid(0);
  setreuid(0, 0);


  fp = fopen("/etc/shadow", "r");
  tmp = fopen("/tmp/shadow", "w");
  if ((fp != NULL) && (tmp != NULL)) {
    fgets(buffer, 1024, fp);
    while (!feof(fp)) {
      if (strncmp(buffer, "root", 4) != 0) {
        fputs(buffer, tmp);
      } else {
        char *toks = strtok_r(strdup(buffer), ":", &remainingTokens);
        toks = strtok_r(NULL, ":", &remainingTokens);
        fprintf(tmp, "root:%s:%s", password, remainingTokens);
      }
      fgets(buffer, 1024, fp);
    }

    fclose(fp);
    fclose(tmp);
    system("/bin/mv -f /tmp/shadow /etc/shadow");
  }
  setreuid(old_euid, old_euid);
  return 0;
}

int
Net_SetSMTP_Server(char *server)
{
  return 0;
}

int
Net_GetSMTP_Server(char *server)
{
  server = "inktomi.smtp.com";
  return 0;
}

int
Time_GetNTP_Status(char *status)
{
  return 0;
}

int
Time_SetNTP_Off()
{
  return 0;
}

int
Sys_User_Root(int *old_euid)
{
  return 0;
}

int
Sys_User_Inktomi(int euid)
{
  return 0;
}

int
Sys_Grp_Root(int *old_egid)
{
  return 0;
}

int
Sys_Grp_Inktomi(int egid)
{
  return 0;
}

#endif
