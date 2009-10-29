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

/*****************************************************************************
 *
 * P_SplitDNSProcessor.h - Interface to DNS server selection
 *
 * 
 ****************************************************************************/

#ifndef _P_SPLIT_DNSProcessor_H_
#define _P_SPLIT_DNSProcessor_H_

/*
#include "P_DNS.h"
#include "I_SplitDNS.h"
#include "I_Lock.h"
#include "ControlBase.h"
#include "ControlMatcher.h"
*/

/* ---------------------------
   forward declarations ...
   --------------------------- */


void ink_split_dns_init(ModuleVersion version);

#define MAX_CONFIGS  100
struct RequestData;
typedef RequestData RD;

struct matcher_line;

class SplitDNSRecord;
class SplitDNSResult;

struct DNSServer;

enum DNSResultType
{ DNS_SRVR_UNDEFINED = 0,
  DNS_SRVR_SPECIFIED,
  DNS_SRVR_DEFAULT,
  DNS_SRVR_FAIL
};

typedef ControlMatcher<SplitDNSRecord, SplitDNSResult> DNS_table;




/* --------------------------------------------------------------
   **                struct SplitDNSResult
   -------------------------------------------------------------- */

struct SplitDNSResult
{

  SplitDNSResult();

  /* ------------ 
     public 
     ------------ */

  DNSResultType r;

  DNSServer *get_dns_record();
  int get_dns_srvr_count();

  /* ------------ 
     private 
     ------------ */
  int m_line_number;

  SplitDNSRecord *m_rec;
  bool m_wrap_around;
};


/* --------------------------------------------------------------
   -------------------------------------------------------------- */
struct SplitDNSConfigInfo
{
  volatile int m_refcount;

    virtual ~ SplitDNSConfigInfo()
  {
  }
};


class SplitDNSConfigProcessor
{
public:
  SplitDNSConfigProcessor();

  unsigned int set(unsigned int id, SplitDNSConfigInfo * info);
  SplitDNSConfigInfo *get(unsigned int id);
  void release(unsigned int id, SplitDNSConfigInfo * data);

public:
  volatile SplitDNSConfigInfo *infos[MAX_CONFIGS];
  volatile int ninfos;
};


extern SplitDNSConfigProcessor SplitDNSconfigProcessor;

/* --------------------------------------------------------------
   **                struct SplitDNS
   -------------------------------------------------------------- */

struct SplitDNS:public SplitDNSConfigInfo
{
  SplitDNS();
  ~SplitDNS();


  void *getDNSRecord(char *hostname);
  void findServer(RD * rdata, SplitDNSResult * result);


  DNS_table *m_DNSSrvrTable;
  SplitDNSRecord *m_DefaultDNSSrvr;

  ink32 m_SplitDNSlEnable;

  char m_def_domain[MAXDNAME];
  int m_def_domain_len;

  /* ----------------------------
     required by the alleged fast
     path
     ---------------------------- */
  bool m_bEnableFastPath;
  void *m_pxLeafArray;
  int m_numEle;
};
/* --------------------------------------------------------------
   -------------------------------------------------------------- */



/* --------------------------------------------------------------
   SplitDNSConfig::isSplitDNSEnabled()
   -------------------------------------------------------------- */
inline bool SplitDNSConfig::isSplitDNSEnabled()
{
  return (gsplit_dns_enabled ? true : false);
}


/* --------------------------------------------------------------
   -------------------------------------------------------------- */

//
// End API to outside world
//



/* --------------------------------------------------------------
   **                struct DNSServer

   A record for an single server
   -------------------------------------------------------------- */


struct DNSServer
{
  unsigned long
    x_server_ip[MAXNS];
  char
    x_dns_ip_line[MAXDNAME * 2];

  char
    x_def_domain[MAXDNAME];
  char
    x_domain_srch_list[MAXDNAME];
  int
    x_dns_server_port[MAXNS];

  DNSHandler *
    x_dnsH;

  DNSServer():
  x_dnsH(NULL)
  {
    memset(x_server_ip, 0, sizeof(x_server_ip));
    memset(x_dns_server_port, 0, sizeof(x_dns_server_port));

    memset(x_def_domain, 0, MAXDNAME);
    memset(x_domain_srch_list, 0, MAXDNAME);
    memset(x_dns_ip_line, 0, MAXDNAME * 2);
  }
};


/* --------------------------------------------------------------
   **                class DNSRequestData

   A record for an single server
   -------------------------------------------------------------- */


class
  DNSRequestData:
  public
  RequestData
{
public:

  DNSRequestData();

  char *
  get_string();

  const char *
  get_host();

  ip_addr_t
  get_ip();

  ip_addr_t
  get_client_ip();


  const char *
    m_pHost;
};


/* --------------------------------------------------------------
   DNSRequestData::get_string()
   -------------------------------------------------------------- */
inline DNSRequestData::DNSRequestData()
:m_pHost(0)
{
}


/* --------------------------------------------------------------
   DNSRequestData::get_string()
   -------------------------------------------------------------- */

inline char *
DNSRequestData::get_string()
{
  return xstrdup((char *) m_pHost);
}


/* --------------------------------------------------------------
   DNSRequestData::get_host()
   -------------------------------------------------------------- */

inline const char *
DNSRequestData::get_host()
{
  return m_pHost;
}


/* --------------------------------------------------------------
   DNSRequestData::get_ip()
   -------------------------------------------------------------- */

inline ip_addr_t DNSRequestData::get_ip()
{
  return (ip_addr_t) 0;
}


/* --------------------------------------------------------------
   DNSRequestData::get_client_ip()
   -------------------------------------------------------------- */

inline ip_addr_t DNSRequestData::get_client_ip()
{
  return (ip_addr_t) 0;
}



/* --------------------------------------------------------------
   -------------------------------------------------------------- */



/* --------------------------------------------------------------
   *                 class SplitDNSRecord

   A record for a configuration line in the splitdns.config file
   -------------------------------------------------------------- */

class
  SplitDNSRecord:
  public
  ControlBase
{
public:

  SplitDNSRecord();
  ~
  SplitDNSRecord();

  char *
  Init(matcher_line * line_info);

  const char *
  ProcessDNSHosts(char *val);
  const char *
  ProcessDomainSrchList(char *val);
  const char *
  ProcessDefDomain(char *val);

  void
  UpdateMatch(SplitDNSResult * result, RD * rdata);
  void
  Print();


  DNSServer
    m_servers;
  int
    m_dnsSrvr_cnt;
  int
    m_domain_srch_list;
};


/* --------------------------------------------------------------
   SplitDNSRecord::SplitDNSRecord()
   -------------------------------------------------------------- */

inline SplitDNSRecord::SplitDNSRecord()
:m_dnsSrvr_cnt(0), m_domain_srch_list(0)
{
}


/* --------------------------------------------------------------
   SplitDNSRecord::~SplitDNSRecord()
   -------------------------------------------------------------- */

inline SplitDNSRecord::~SplitDNSRecord()
{
}



/* --------------------------------------------------------------
   -------------------------------------------------------------- */


/* --------------------------------------------------------------
   struct SDNS_UpdateContinuation
   Used to handle parent.conf or default parent updates after the
   manager signals a change
   -------------------------------------------------------------- */

struct SDNS_UpdateContinuation:
  Continuation
{

  int
  handle_event(int event, void *data);
  SDNS_UpdateContinuation(ProxyMutex * m);

};


/* --------------------------------------------------------------
   SDNS_UpdateContinuation::SDNS_UpdateContinuation()
   -------------------------------------------------------------- */

inline SDNS_UpdateContinuation::SDNS_UpdateContinuation(ProxyMutex * m)
:Continuation(m)
{
  SET_HANDLER(&SDNS_UpdateContinuation::handle_event);
}


/* --------------------------------------------------------------
   SDNS_UpdateContinuation::handle_event()
   -------------------------------------------------------------- */

inline int
SDNS_UpdateContinuation::handle_event(int event, void *data)
{
  NOWARN_UNUSED(event);
  NOWARN_UNUSED(data);

  SplitDNSConfig::reconfigure();
  delete this;

  return EVENT_DONE;
}


/* --------------------------------------------------------------
   -------------------------------------------------------------- */





/* ------------------
   Helper Functions
   ------------------ */

SplitDNSRecord *createDefaultServer();
void reloadDefaultParent(char *val);
void reloadParentFile();
void *splitDNS_CB(void *token, void *data);

#endif
