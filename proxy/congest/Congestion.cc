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
 *  Congestion.cc - Content and User Access Control
 *
 *
 ****************************************************************************/
#include "Main.h"
#include "Error.h"
#include "Config.h"
//#include "Lock.h"
#include "P_Net.h"
//#include "Freer.h"
#include "CongestionDB.h"
#include "Congestion.h"
#include "ControlMatcher.h"

RecRawStatBlock *congest_rsb;

InkRand CongestionRand(123);

static const char *congestPrefix = "[CongestionControl]";
static char *congestVar = "proxy.config.http.congestion_control.filename";
static char *congestEnabledVar = "proxy.config.http.congestion_control.enabled";
static char *congestTimeVar = "proxy.config.http.congestion_control.localtime";

static const matcher_tags congest_dest_tags = {
  "dest_host",
  "dest_domain",
  "dest_ip",
  NULL,
  "host_regex",
  true
};

#define CONGESTION_CONTROL_CONFIG_TIMEOUT 120
/* default congestion control values */

static char *default_max_connection_failures_var =
  "proxy.config.http.congestion_control.default.max_connection_failures";
static char *default_fail_window_var = "proxy.config.http.congestion_control.default.fail_window";
static char *default_proxy_retry_interval_var = "proxy.config.http.congestion_control.default.proxy_retry_interval";
static char *default_client_wait_interval_var = "proxy.config.http.congestion_control.default.client_wait_interval";
static char *default_wait_interval_alpha_var = "proxy.config.http.congestion_control.default.wait_interval_alpha";
static char *default_live_os_conn_timeout_var = "proxy.config.http.congestion_control.default.live_os_conn_timeout";
static char *default_live_os_conn_retries_var = "proxy.config.http.congestion_control.default.live_os_conn_retries";
static char *default_dead_os_conn_timeout_var = "proxy.config.http.congestion_control.default.dead_os_conn_timeout";
static char *default_dead_os_conn_retries_var = "proxy.config.http.congestion_control.default.dead_os_conn_retries";
static char *default_max_connection_var = "proxy.config.http.congestion_control.default.max_connection";
static char *default_error_page_var = "proxy.config.http.congestion_control.default.error_page";
static char *default_congestion_scheme_var = "proxy.config.http.congestion_control.default.congestion_scheme";
static char *default_snmp_var = "proxy.config.http.congestion_control.default.snmp";

char *DEFAULT_error_page = xstrdup("congestion#retryAfter");
int DEFAULT_max_connection_failures = 5;
int DEFAULT_fail_window = 120;
int DEFAULT_proxy_retry_interval = 10;
int DEFAULT_client_wait_interval = 300;
int DEFAULT_wait_interval_alpha = 30;
int DEFAULT_live_os_conn_timeout = 60;
int DEFAULT_live_os_conn_retries = 2;
int DEFAULT_dead_os_conn_timeout = 15;
int DEFAULT_dead_os_conn_retries = 1;
int DEFAULT_max_connection = -1;
char *DEFAULT_congestion_scheme_str = xstrdup("per_ip");
int DEFAULT_congestion_scheme = PER_IP;
char *DEFAULT_snmp_str = xstrdup("on");
int DEFAULT_snmp = 1;

/* congestion control limits */
#define CONG_RULE_MAX_max_connection_failures \
             (1<<(sizeof(cong_hist_t) * 8))

#define CONG_RULE_ULIMITED_max_connection_failures -1
#define CONG_RULE_ULIMITED_mac_connection -1

static Ptr<ProxyMutex> reconfig_mutex;
CongestionMatcherTable *CongestionMatcher = NULL;
int congestionControlEnabled = 0;
int congestionControlLocalTime = 0;

CongestionControlRecord::CongestionControlRecord(const CongestionControlRecord & rec)
{
  prefix = xstrdup(rec.prefix);
  prefix_len = rec.prefix_len;
  port = rec.port;
  congestion_scheme = rec.congestion_scheme;
  error_page = xstrdup(rec.error_page);
  max_connection_failures = rec.max_connection_failures;
  fail_window = rec.fail_window;
  proxy_retry_interval = rec.proxy_retry_interval;
  client_wait_interval = rec.client_wait_interval;
  wait_interval_alpha = rec.wait_interval_alpha;
  live_os_conn_timeout = rec.live_os_conn_timeout;
  live_os_conn_retries = rec.live_os_conn_retries;
  dead_os_conn_timeout = rec.dead_os_conn_timeout;
  dead_os_conn_retries = rec.dead_os_conn_retries;
  max_connection = rec.max_connection;
  snmp_enabled = rec.snmp_enabled;
  pRecord = NULL;
  ref_count = 1;
  line_num = rec.line_num;
  rank = 0;
}

void
CongestionControlRecord::setdefault()
{
  cleanup();
  congestion_scheme = DEFAULT_congestion_scheme;
  port = 0;
  prefix_len = 0;
  rank = 0;
  max_connection_failures = DEFAULT_max_connection_failures;
  fail_window = DEFAULT_fail_window;
  proxy_retry_interval = DEFAULT_proxy_retry_interval;
  client_wait_interval = DEFAULT_client_wait_interval;
  wait_interval_alpha = DEFAULT_wait_interval_alpha;
  live_os_conn_timeout = DEFAULT_live_os_conn_timeout;
  live_os_conn_retries = DEFAULT_live_os_conn_retries;
  dead_os_conn_timeout = DEFAULT_dead_os_conn_timeout;
  dead_os_conn_retries = DEFAULT_dead_os_conn_retries;
  max_connection = DEFAULT_max_connection;
  snmp_enabled = DEFAULT_snmp;
}

char *
CongestionControlRecord::validate()
{
  char *error_buf = NULL;
  int error_len = 1024;

#define IsGt0(var)\
  if ( var < 1 ) { \
    error_buf = (char*) xmalloc(error_len); \
    snprintf(error_buf, error_len, "line %d: invalid %s = %d, %s must > 0", \
	    line_num, #var, var, #var); \
    cleanup(); \
    return error_buf; \
  }

  if (error_page == NULL)
    error_page = xstrdup(DEFAULT_error_page);
  if (max_connection_failures >= CONG_RULE_MAX_max_connection_failures ||
      (max_connection_failures <= 0 && max_connection_failures != CONG_RULE_ULIMITED_max_connection_failures)
    ) {
    error_buf = (char *) xmalloc(error_len);
    snprintf(error_buf, error_len, "line %d: invalid %s = %d not in [1, %d) range",
             line_num, "max_connection_failures", max_connection_failures, CONG_RULE_MAX_max_connection_failures);
    cleanup();
    return error_buf;
  }

  IsGt0(fail_window);
  IsGt0(proxy_retry_interval);
  IsGt0(client_wait_interval);
  IsGt0(wait_interval_alpha);
  IsGt0(live_os_conn_timeout);
  IsGt0(live_os_conn_retries);
  IsGt0(dead_os_conn_timeout);
  IsGt0(dead_os_conn_retries);
  // max_connection_failures <= 0  no failure num control
  // max_connection == -1 no max_connection control
  // max_connection_failures <= 0 && max_connection == -1 no congestion control for the rule
  // max_connection == 0, no connection allow to the origin server for the rule
#undef IsGt0
  return error_buf;
}

char *
CongestionControlRecord::Init(matcher_line * line_info)
{
  char *errBuf;
  const int errBufLen = 1024;
  const char *tmp;
  char *label;
  char *val;
  line_num = line_info->line_num;

  /* initialize the rule to defaults */
  setdefault();

  for (int i = 0; i < MATCHER_MAX_TOKENS; i++) {
    label = line_info->line[0][i];
    val = line_info->line[1][i];

    if (label == NULL) {
      continue;
    }
    if (strcasecmp(label, "max_connection_failures") == 0) {
      max_connection_failures = atoi(val);
    } else if (strcasecmp(label, "fail_window") == 0) {
      fail_window = atoi(val);
    } else if (strcasecmp(label, "proxy_retry_interval") == 0) {
      proxy_retry_interval = atoi(val);
    } else if (strcasecmp(label, "client_wait_interval") == 0) {
      client_wait_interval = atoi(val);
    } else if (strcasecmp(label, "wait_interval_alpha") == 0) {
      wait_interval_alpha = atoi(val);
    } else if (strcasecmp(label, "live_os_conn_timeout") == 0) {
      live_os_conn_timeout = atoi(val);
    } else if (strcasecmp(label, "live_os_conn_retries") == 0) {
      live_os_conn_retries = atoi(val);
    } else if (strcasecmp(label, "dead_os_conn_timeout") == 0) {
      dead_os_conn_timeout = atoi(val);
    } else if (strcasecmp(label, "dead_os_conn_retries") == 0) {
      dead_os_conn_retries = atoi(val);
    } else if (strcasecmp(label, "max_connection") == 0) {
      max_connection = atoi(val);
    } else if (strcasecmp(label, "congestion_scheme") == 0) {
      if (!strcasecmp(val, "per_ip")) {
        congestion_scheme = PER_IP;
      } else if (!strcasecmp(val, "per_host")) {
        congestion_scheme = PER_HOST;
      } else {
        congestion_scheme = PER_IP;
      }
    } else if (strcasecmp(label, "error_page") == 0) {
      error_page = xstrdup(val);
    } else if (strcasecmp(label, "snmp") == 0) {
      if (!strcasecmp(val, "on")) {
        snmp_enabled = 1;
      } else {
        snmp_enabled = 0;
      }
    } else if (strcasecmp(label, "prefix") == 0) {
      prefix = xstrdup(val);
      prefix_len = strlen(prefix);
      rank += 1;
      // prefix will be used in the ControlBase 
      continue;
    } else if (strcasecmp(label, "port") == 0) {
      port = atoi(val);
      rank += 2;
      // port will be used in the ControlBase;
      continue;
    } else
      continue;
    // Consume the label/value pair we used
    line_info->line[0][i] = NULL;
    line_info->num_el--;
  }
  if (line_info->num_el > 0) {
    tmp = ProcessModifiers(line_info);

    if (tmp != NULL) {
      errBuf = (char *) xmalloc(errBufLen * sizeof(char));
      ink_snprintf(errBuf, errBufLen, "%s %s at line %d in congestion.config", congestPrefix, tmp, line_num);
      return errBuf;
    }

  }

  char *err_msg = validate();
  if (err_msg == NULL) {
    pRecord = new CongestionControlRecord(*this);
  }
  return err_msg;
}

void
CongestionControlRecord::UpdateMatch(CongestionControlRule * pRule, RD * rdata)
{
/*
 * Select the first matching rule specified in congestion.config
 * rank     Matches
 *   3       dest && prefix && port
 *   2       dest && port
 *   1       dest && prefix
 *   0       dest
 */
  if (pRule->record == 0 ||
      pRule->record->rank < rank || (pRule->record->line_num > line_num && pRule->record->rank == rank)) {
    if (rank > 0) {
      if (rdata->data_type() == RequestData::RD_CONGEST_ENTRY) {
        // Enforce the same port and prefix
        if (port != 0 && port != ((CongestionEntry *) rdata)->pRecord->port)
          return;
        if (prefix != NULL && ((CongestionEntry *) rdata)->pRecord->prefix == NULL)
          return;
        if (prefix != NULL && strncmp(prefix, ((CongestionEntry *) rdata)->pRecord->prefix, prefix_len))
          return;
      } else if (!this->CheckModifiers((HttpRequestData *) rdata)) {
        return;
      }
    }
    pRule->record = this;
    Debug("congestion_config", "Matched with record 0x%x at line %d", this, line_num);
  }
}

void
CongestionControlRecord::Print()
{
#define PrintNUM(var) \
  Debug("congestion_config", "%30s = %d", #var, var);
#define PrintSTR(var) \
  Debug("congestion_config", "%30s = %s", #var, (var == NULL? "NULL" : var));

  PrintNUM(line_num);
  PrintSTR(prefix);
  PrintNUM(congestion_scheme);
  PrintSTR(error_page);
  PrintNUM(max_connection_failures);
  PrintNUM(fail_window);
  PrintNUM(proxy_retry_interval);
  PrintNUM(client_wait_interval);
  PrintNUM(wait_interval_alpha);
  PrintNUM(live_os_conn_timeout);
  PrintNUM(live_os_conn_retries);
  PrintNUM(dead_os_conn_timeout);
  PrintNUM(dead_os_conn_retries);
  PrintNUM(max_connection);
  PrintNUM(snmp_enabled);
#undef PrintNUM
#undef PrintSTR
}

struct CongestionControl_UpdateContinuation:Continuation
{
  int congestion_update_handler(int etype, void *data)
  {
    NOWARN_UNUSED(etype);
    NOWARN_UNUSED(data);
    reloadCongestionControl();
    delete this;
      return EVENT_DONE;
  }
  CongestionControl_UpdateContinuation(ProxyMutex * m):Continuation(m)
  {
    SET_HANDLER(&CongestionControl_UpdateContinuation::congestion_update_handler);
  }
};

extern void initCongestionDB();

// place holder for congestion control enable config 
static int
CongestionControlEnabledChanged(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  if (congestionControlEnabled == 1 || congestionControlEnabled == 2) {
    revalidateCongestionDB();
  }
  return 0;
}

static int
CongestionControlFile_CB(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  NOWARN_UNUSED(data);
  eventProcessor.schedule_imm(new CongestionControl_UpdateContinuation(reconfig_mutex), ET_NET);
  return 0;
}

static int
CongestionControlDefaultSchemeChanged(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  if (strcasecmp(DEFAULT_congestion_scheme_str, "per_host") == 0) {
    DEFAULT_congestion_scheme = PER_HOST;
  } else {
    DEFAULT_congestion_scheme = PER_IP;
  }
  return 0;
}

static int
CongestionControlDefaultSNMPChanged(const char *name, RecDataT data_type, RecData data, void *cookie)
{
  if (strcasecmp(DEFAULT_snmp_str, "on") == 0) {
    DEFAULT_snmp = 1;
  } else {
    DEFAULT_snmp = 0;
  }
  return 0;
}

//-----------------------------------------------
// hack for link the RegressionTest into the
//  TS binary
//-----------------------------------------------
extern void init_CongestionRegressionTest();

#define CC_EstablishStaticConfigInteger(v, n) \
  REC_EstablishStaticConfigInt32(v, n)
#define CC_EstablishStaticConfigStringAlloc(v, n) \
  REC_EstablishStaticConfigStringAlloc(v, n)

void
initCongestionControl()
{
  init_CongestionRegressionTest();
  ink_assert(CongestionMatcher == NULL);
// register the stats variables
  register_congest_stats();
// you must grab this mutex before reconfig the congestion control matcher table
  reconfig_mutex = new_ProxyMutex();

// register config variables
  CC_EstablishStaticConfigInteger(congestionControlEnabled, congestEnabledVar);
  CC_EstablishStaticConfigInteger(DEFAULT_max_connection_failures, default_max_connection_failures_var);
  CC_EstablishStaticConfigInteger(DEFAULT_fail_window, default_fail_window_var);
  CC_EstablishStaticConfigInteger(DEFAULT_proxy_retry_interval, default_proxy_retry_interval_var);
  CC_EstablishStaticConfigInteger(DEFAULT_client_wait_interval, default_client_wait_interval_var);
  CC_EstablishStaticConfigInteger(DEFAULT_wait_interval_alpha, default_wait_interval_alpha_var);
  CC_EstablishStaticConfigInteger(DEFAULT_live_os_conn_timeout, default_live_os_conn_timeout_var);
  CC_EstablishStaticConfigInteger(DEFAULT_live_os_conn_retries, default_live_os_conn_retries_var);
  CC_EstablishStaticConfigInteger(DEFAULT_dead_os_conn_timeout, default_dead_os_conn_timeout_var);
  CC_EstablishStaticConfigInteger(DEFAULT_dead_os_conn_retries, default_dead_os_conn_retries_var);
  CC_EstablishStaticConfigInteger(DEFAULT_max_connection, default_max_connection_var);
  CC_EstablishStaticConfigStringAlloc(DEFAULT_congestion_scheme_str, default_congestion_scheme_var);
  CC_EstablishStaticConfigStringAlloc(DEFAULT_error_page, default_error_page_var);
  CC_EstablishStaticConfigStringAlloc(DEFAULT_snmp_str, default_snmp_var);
  CC_EstablishStaticConfigInteger(congestionControlEnabled, congestEnabledVar);
  CC_EstablishStaticConfigInteger(congestionControlLocalTime, congestTimeVar);
  {
    RecData recdata;
    recdata.rec_int = 0;
    CongestionControlDefaultSchemeChanged(NULL, RECD_NULL, recdata, NULL);
    CongestionControlDefaultSNMPChanged(NULL, RECD_NULL, recdata, NULL);
  }

  CongestionMatcher = NEW(new CongestionMatcherTable(congestVar, congestPrefix, &congest_dest_tags));
#ifdef DEBUG_CONGESTION_MACTHER
  CongestionMatcher->Print();
#endif

  if (congestionControlEnabled) {
    Debug("congestion_config", "congestion control enabled");
    initCongestionDB();
  } else {
    Debug("congestion_config", "congestion control disabled");
  }
  RecRegisterConfigUpdateCb(default_congestion_scheme_var, &CongestionControlDefaultSchemeChanged, NULL);
  RecRegisterConfigUpdateCb(default_snmp_var, &CongestionControlDefaultSNMPChanged, NULL);
  RecRegisterConfigUpdateCb(congestEnabledVar, &CongestionControlEnabledChanged, NULL);
  RecRegisterConfigUpdateCb(congestVar, &CongestionControlFile_CB, NULL);
}

void
reloadCongestionControl()
{
  CongestionMatcherTable *newTable;
  Debug("congestion_config", "congestion control config changed, reloading");
  ink_hrtime t = CONGESTION_CONTROL_CONFIG_TIMEOUT;
  newTable = NEW(new CongestionMatcherTable(congestVar, congestPrefix, &congest_dest_tags));
#ifdef DEBUG_CONGESTION_MACTHER
  newTable->Print();
#endif
  new_Deleter(CongestionMatcher, t);
  ink_atomic_swap_ptr(&CongestionMatcher, newTable);
  if (congestionControlEnabled) {
    revalidateCongestionDB();
  }
}

CongestionControlRecord *
CongestionControlled(RD * rdata)
{
  if (congestionControlEnabled) {
    CongestionControlRule result;
    CongestionMatcher->Match(rdata, &result);
    if (result.record) {
      return result.record->pRecord;
    }
  } else {
    return NULL;
  }
  return NULL;
}

inku64
make_key(char *hostname, unsigned long ip, CongestionControlRecord * record)
{
  int host_len = 0;
  if (hostname) {
    host_len = strlen(hostname);
  }
  return make_key(hostname, host_len, ip, record);
}

inku64
make_key(char *hostname, int len, unsigned long ip, CongestionControlRecord * record)
{
  INK_MD5 md5;
#ifdef USE_MMH
  MMH_CTX ctx;
  ink_code_incr_MMH_init(&ctx);
  if (record->congestion_scheme == PER_HOST && len > 0)
    ink_code_incr_MMH_update(&ctx, hostname, len);
  else
    ink_code_incr_MMH_update(&ctx, (char *) &ip, sizeof(ip));
  if (record->port != 0) {
    unsigned short p = port;
    p = htons(p);
    ink_code_incr_MMH_update(&ctx, (char *) &p, 2);
  }
  if (record->prefix != NULL) {
    ink_code_incr_MMH_update(&ctx, record->prefix, record->prefix_len);
  }
  ink_code_incr_MMH_final((char *) &md5, &ctx);
#else
  INK_DIGEST_CTX ctx;
  ink_code_incr_md5_init(&ctx);
  if (record->congestion_scheme == PER_HOST && len > 0)
    ink_code_incr_md5_update(&ctx, hostname, len);
  else
    ink_code_incr_md5_update(&ctx, (char *) &ip, sizeof(ip));
  if (record->port != 0) {
    unsigned short p = record->port;
    p = htons(p);
    ink_code_incr_md5_update(&ctx, (char *) &p, 2);
  }
  if (record->prefix != NULL) {
    ink_code_incr_md5_update(&ctx, record->prefix, record->prefix_len);
  }
  ink_code_incr_md5_final((char *) &md5, &ctx);
#endif
  return md5.fold();
}

inku64
make_key(char *hostname, int len, unsigned long ip, char *prefix, int prelen, short port)
{
  /* if the hostname != NULL, use hostname, else, use ip */
  INK_MD5 md5;
#ifdef USE_MMH
  MMH_CTX ctx;
  ink_code_incr_MMH_init(&ctx);
  if (hostname && len > 0)
    ink_code_incr_MMH_update(&ctx, hostname, len);
  else
    ink_code_incr_MMH_update(&ctx, (char *) &ip, sizeof(ip));
  if (port != 0) {
    unsigned short p = port;
    p = htons(p);
    ink_code_incr_MMH_update(&ctx, (char *) &p, 2);
  }
  if (prefix != NULL) {
    ink_code_incr_MMH_update(&ctx, prefix, prelen);
  }
  ink_code_incr_MMH_final((char *) &md5, &ctx);
#else
  INK_DIGEST_CTX ctx;
  ink_code_incr_md5_init(&ctx);
  if (hostname && len > 0)
    ink_code_incr_md5_update(&ctx, hostname, len);
  else
    ink_code_incr_md5_update(&ctx, (char *) &ip, sizeof(ip));
  if (port != 0) {
    unsigned short p = port;
    p = htons(p);
    ink_code_incr_md5_update(&ctx, (char *) &p, 2);
  }
  if (prefix != NULL) {
    ink_code_incr_md5_update(&ctx, prefix, prelen);
  }
  ink_code_incr_md5_final((char *) &md5, &ctx);
#endif
  return md5.fold();
}

//----------------------------------------------------------
// FailHistory Implementation
//----------------------------------------------------------
void
FailHistory::init(int window)
{
  bin_len = (window + CONG_HIST_ENTRIES) / CONG_HIST_ENTRIES;
  if (bin_len <= 0)
    bin_len = 1;
  length = bin_len * CONG_HIST_ENTRIES;
  for (int i = 0; i < CONG_HIST_ENTRIES; i++) {
    bins[i] = 0;
  }
  last_event = 0;
  cur_index = 0;
  events = 0;
  start = 0;
}

void
FailHistory::init_event(long t, int n)
{
  last_event = t;
  cur_index = 0;
  events = n;
  bins[0] = n;
  for (int i = 1; i < CONG_HIST_ENTRIES; i++) {
    bins[i] = 0;
  }
  start = (last_event + bin_len) - last_event % bin_len - length;
}

int
FailHistory::regist_event(long t, int n)
{
  if (t < start)
    return events;
  if (t > last_event + length) {
    init_event(t, n);
    return events;
  }
  if (t < start + length) {
    bins[((t - start) / bin_len + 1 + cur_index) % CONG_HIST_ENTRIES] += n;
  } else {
    do {
      start += bin_len;
      cur_index++;
      if (cur_index == CONG_HIST_ENTRIES)
        cur_index = 0;
      events -= bins[cur_index];
      bins[cur_index] = 0;
    } while (start + length < t);
    bins[cur_index] = n;
  }
  events += n;
  if (last_event < t)
    last_event = t;
  return events;
}

//----------------------------------------------------------
// CongestionEntry Implementation
//----------------------------------------------------------
CongestionEntry::CongestionEntry(const char *hostname, ip_addr_t ip, CongestionControlRecord * rule, inku64 key)
:m_key(key),
m_ip(ip),
m_last_congested(0),
m_congested(0),
m_stat_congested_conn_failures(0),
m_M_congested(0), m_last_M_congested(0), m_num_connections(0), m_stat_congested_max_conn(0), m_ref_count(1)
{
  m_hostname = xstrdup(hostname);
  rule->get();
  pRecord = rule;
  clearFailHistory();
  m_hist_lock = new_ProxyMutex();
}

void
CongestionEntry::init(CongestionControlRecord * rule)
{
  if (pRecord)
    pRecord->put();
  rule->get();
  pRecord = rule;
  clearFailHistory();
  if ((pRecord->max_connection > m_num_connections)
      && ink_atomic_swap(&m_M_congested, 0)) {
    snmp_sig_alive();
  }
}

bool
CongestionEntry::validate()
{
  CongestionControlRecord *p = CongestionControlled(this);
  if (p == NULL) {
    return false;
  }

  inku64 key = make_key(m_hostname,
                        m_ip,
                        p);
  if (key != m_key) {
    return false;
  }
  applyNewRule(p);
  return true;
}

void
CongestionEntry::applyNewRule(CongestionControlRecord * rule)
{
  if (pRecord->fail_window != rule->fail_window) {
    init(rule);
    return;
  }
  int mcf = pRecord->max_connection_failures;
  pRecord->put();
  rule->get();
  pRecord = rule;
  if (((pRecord->max_connection < 0)
       || (pRecord->max_connection > m_num_connections))
      && ink_atomic_swap(&m_M_congested, 0)) {
    snmp_sig_alive();
  }
  if (pRecord->max_connection_failures < 0) {
    if (ink_atomic_swap(&m_congested, 0)) {
      snmp_sig_alive();
    }
    return;
  }
  if (mcf < pRecord->max_connection_failures) {
    if (ink_atomic_swap(&m_congested, 0)) {
      snmp_sig_alive();
    }
  } else if (mcf > pRecord->max_connection_failures && m_history.events >= pRecord->max_connection_failures) {
    if (!ink_atomic_swap(&m_congested, 1))
      snmp_sig_congested();
  }
}

int
CongestionEntry::sprint(char *buf, int buflen, int format)
{
  struct in_addr addr;
  char str_time[100] = " ";
  int len = 0;
  ink_hrtime timestamp = 0;
  char state;
  if (pRecord->max_connection >= 0 && m_num_connections >= pRecord->max_connection) {
    timestamp = ink_hrtime_to_sec(ink_get_hrtime());
    state = 'M';
  } else {
    timestamp = m_last_congested;
    state = (m_congested ? 'F' : ' ');
  }
  len += ink_snprintf(buf + len, buflen - len, "%lld|%d|%s|%s",
                      timestamp,
                      pRecord->line_num,
                      (m_hostname ? m_hostname : " "), (m_ip ? (addr.s_addr = htonl(m_ip), inet_ntoa(addr)) : " "));

  len += ink_snprintf(buf + len, buflen - len, "|%s|%s|%c",
                      (pRecord->congestion_scheme == PER_IP ? "per_ip" : "per_host"),
                      (pRecord->prefix ? pRecord->prefix : " "), state);

  len += ink_snprintf(buf + len, buflen - len, "|%d|%d", m_stat_congested_conn_failures, m_stat_congested_max_conn);

  if (format > 0) {
    if (m_congested) {
      struct tm time;
      time_t seconds = m_last_congested;
      if (congestionControlLocalTime) {
        ink_localtime_r(&seconds, &time);
      } else {
        ink_gmtime_r(&seconds, &time);
      }
      ink_snprintf(str_time, sizeof(str_time), "%04d/%02d/%02d %02d:%02d:%02d",
                   time.tm_year + 1900, time.tm_mon + 1, time.tm_mday, time.tm_hour, time.tm_min, time.tm_sec);
    }
    len += ink_snprintf(buf + len, buflen - len, "|%s", str_time);

    if (format > 1) {
      len += ink_snprintf(buf + len, buflen - len, "|%llu", m_key);

      if (format > 2) {
        len += ink_snprintf(buf + len, buflen - len, "|%ld", m_history.last_event);

        if (format > 3) {
          len += ink_snprintf(buf + len, buflen - len, "|%d|%d|%d", m_history.events, m_ref_count, m_num_connections);
        }
      }
    }
  }
  len += ink_snprintf(buf + len, buflen - len, "\n");
  return len;
}

//-------------------------------------------------------------
// When a connection failure happened, try to get the lock
//  first and change register the event, if we can not get 
//  the lock, discard the event
//-------------------------------------------------------------
void
CongestionEntry::failed_at(ink_hrtime t)
{
  if (pRecord->max_connection_failures == -1)
    return;
  // long time = ink_hrtime_to_sec(t);
  long time = t;
  Debug("congestion_control", "failed_at: %d", time);
  MUTEX_TRY_LOCK(lock, m_hist_lock, this_ethread());
  if (lock) {
    m_history.regist_event(time);
    if (!m_congested) {
      ink32 new_congested = compCongested();
      if (new_congested && !ink_atomic_swap(&m_congested, 1)) {
        m_last_congested = m_history.last_event;
        snmp_sig_congested();
      }
    }
  } else {
    Debug("congestion_control", "failure info lost due to lock contention(Entry: 0x%x, Time: %d)", (void *) this, time);
  }
}

void
CongestionEntry::go_alive()
{
  if (ink_atomic_swap(&m_congested, 0)) {
    snmp_sig_alive();
  }
}

#define SERVER_CONGESTED_SIG  REC_SIGNAL_HTTP_CONGESTED_SERVER
#define SERVER_ALLEVIATED_SIG REC_SIGNAL_HTTP_ALLEVIATED_SERVER
#define CC_SignalWarning(sig, msg) \
     REC_SignalWarning(sig, msg)

void
CongestionEntry::snmp_sig_congested()
{
  if (pRecord->snmp_enabled) {
    char msg[1024];
    struct in_addr addr;
    snprintf(msg, sizeof(msg), "[%d] server %s/%s congested",
             pRecord->line_num,
             (m_hostname ? m_hostname : (addr.s_addr = htonl(m_ip), inet_ntoa(addr))),
             (pRecord->prefix ? pRecord->prefix : ""));
    CC_SignalWarning(SERVER_CONGESTED_SIG, msg);
  }
}

void
CongestionEntry::snmp_sig_alive()
{
  if (pRecord->snmp_enabled) {
    char msg[1024];
    struct in_addr addr;
    snprintf(msg, sizeof(msg), "[%d] congested server %s/%s alleviated",
             pRecord->line_num,
             (m_hostname ? m_hostname : (addr.s_addr = htonl(m_ip), inet_ntoa(addr))),
             (pRecord->prefix ? pRecord->prefix : ""));
    CC_SignalWarning(SERVER_ALLEVIATED_SIG, msg);
  }
}
