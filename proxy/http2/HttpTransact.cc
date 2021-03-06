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

#include "inktomi++.h"

#include <strings.h>
#include <math.h>

//#include "Hash_Table.h"
#include "HttpTransact.h"
#include "HttpTransactHeaders.h"
#include "HttpSM.h"
#include "HttpCacheSM.h"        //Added to get the scope of HttpCacheSM object - YTS Team, yamsat
#include "HttpDebugNames.h"
#include "time.h"
#include "ParseRules.h"
#include "HTTP.h"
#include "HdrUtils.h"
#include "HttpMessageBody.h"
#include "MimeTable.h"
#include "logging/Log.h"
#include "logging/LogUtils.h"
#include "Error.h"
#include "CacheControl.h"
#include "ControlMatcher.h"
#include "ReverseProxy.h"
#include "HttpAssert.h"
#include "HttpBodyFactory.h"
#include "StatPages.h"
#include "HttpClientSession.h"
//#include "YAddr.h"
#define REVALIDATE_CONV_FACTOR 1000000  //Revalidation Conversion Factor to msec - YTS Team, yamsat
#include "I_Machine.h"
#ifdef USE_NCA
#include "NcaProcessor.h"
#endif

static const char *URL_MSG = "Unable to process requested URL.\n";

#define HTTP_INCREMENT_TRANS_STAT(X) update_stat(s, X, 1);
#define HTTP_SUM_TRANS_STAT(X,S) update_stat(s, X, S);

#define TRANSACT_REMEMBER(_s,_e,_d) \
{ \
    HttpSM *sm = (_s)->state_machine; \
    sm->history[sm->history_pos % HISTORY_SIZE].file  = __FILE__; \
    sm->history[sm->history_pos % HISTORY_SIZE].line  = __LINE__; \
    sm->history[sm->history_pos % HISTORY_SIZE].event = _e; \
    sm->history[sm->history_pos % HISTORY_SIZE].data = (void *)_d; \
    sm->history_pos += 1; \
}

extern HttpBodyFactory *body_factory;
extern int cache_config_vary_on_user_agent;

static const char local_host_ip_str[] = "127.0.0.1";


/////////////////////////////
// Inline Utility Routines //
/////////////////////////////
inline static bool
is_empty(char *s)
{
  return (s[0] == NUL);
}

inline static bool
is_asterisk(char *s)
{
  return ((s[0] == '*') && (s[1] == NUL));
}


// someday, reduce the amount of duplicate code between this 
// function and _process_xxx_connection_field_in_outgoing_header
inline static HTTPKeepAlive
is_header_keep_alive(const HTTPVersion & http_version, const HTTPVersion & request_http_version, MIMEField * con_hdr    /*, bool* unknown_tokens */
  )
{

  enum
  {
    CON_TOKEN_NONE = 0,
    CON_TOKEN_KEEP_ALIVE,
    CON_TOKEN_CLOSE
  };

  int con_token = CON_TOKEN_NONE;
  HTTPKeepAlive keep_alive = HTTP_NO_KEEPALIVE;
  //    *unknown_tokens = false;

  if (con_hdr) {
    int val_len;
    const char *val;

    if (!con_hdr->has_dups()) { // try fastpath first
      val = con_hdr->value_get(&val_len);
      if (ptr_len_casecmp(val, val_len, "keep-alive", 10) == 0) {
        con_token = CON_TOKEN_KEEP_ALIVE;
      } else if (ptr_len_casecmp(val, val_len, "close", 5) == 0) {
        con_token = CON_TOKEN_CLOSE;
      }
    }

    if (con_token == CON_TOKEN_NONE) {
      HdrCsvIter iter;

      val = iter.get_first(con_hdr, &val_len);

      while (val) {
        if (ptr_len_casecmp(val, val_len, "keep-alive", 10) == 0) {
          con_token = CON_TOKEN_KEEP_ALIVE;
          /*
             if (!*unknown_tokens) {
             *unknown_tokens = (iter->get_next(&val_len) == NULL);
             } */
          break;
        } else if (ptr_len_casecmp(val, val_len, "close", 5) == 0) {
          con_token = CON_TOKEN_CLOSE;
          /* 
             if (!*unknown_tokens) {
             *unknown_tokens = (iter->get_next(&val_len) == NULL);
             } */
          break;
        } else {
          //      *unknown_tokens = true;
        }
        val = iter.get_next(&val_len);
      }
    }
  }

  if (HTTPVersion(1, 0) == http_version) {
    keep_alive = (con_token == CON_TOKEN_KEEP_ALIVE) ? (HTTP_KEEPALIVE) : (HTTP_NO_KEEPALIVE);
  } else if (HTTPVersion(1, 1) == http_version) {
    // We deviate from the spec here.  If the we got a response where
    //   where there is no Connection header and the request 1.0 was
    //   1.0 don't treat this as keep-alive since Netscape-Enterprise/3.6 SP1
    //   server doesn't
    keep_alive = ((con_token == CON_TOKEN_KEEP_ALIVE) ||
                  (con_token == CON_TOKEN_NONE && HTTPVersion(1, 1) == request_http_version)) ? (HTTP_KEEPALIVE)
      : (HTTP_NO_KEEPALIVE);
  } else {
    keep_alive = HTTP_NO_KEEPALIVE;
  }

  return (keep_alive);
}

inline static bool
is_request_conditional(HTTPHdr * header)
{
  inku64 mask = (MIME_PRESENCE_IF_UNMODIFIED_SINCE |
                 MIME_PRESENCE_IF_MODIFIED_SINCE | MIME_PRESENCE_IF_RANGE |
                 MIME_PRESENCE_IF_MATCH | MIME_PRESENCE_IF_NONE_MATCH);
  return (header->presence(mask) && (header->method_get_wksidx() == HTTP_WKSIDX_GET));
}

static inline bool
is_ssl_port_ok(HttpTransact::State * s, int port)
{
  HttpConfigSSLPortRange *pr;

  pr = s->http_config_param->ssl_ports;

  while (pr) {
    if (pr->low == -1) {
      return true;
    } else if ((pr->low <= port) && (pr->high >= port)) {
      return true;
    }

    pr = pr->next;
  }

  return false;
}

inline static void
update_cache_control_information_from_config(HttpTransact::State * s)
{
  getCacheControl(&s->cache_control, &s->request_data, s->http_config_param);

  s->cache_info.directives.does_config_permit_lookup &= (s->cache_control.never_cache == false);
  s->cache_info.directives.does_config_permit_storing &= (s->cache_control.never_cache == false);

  s->cache_info.directives.does_client_permit_storing =
    HttpTransact::does_client_request_permit_storing(&(s->cache_control), &s->hdr_info.client_request);

  s->cache_info.directives.does_client_permit_lookup =
    HttpTransact::does_client_request_permit_cached_response(s->
                                                             http_config_param,
                                                             &(s->
                                                               cache_control),
                                                             &s->hdr_info.client_request, s->via_string);

  s->cache_info.directives.does_client_permit_dns_storing =
    HttpTransact::does_client_request_permit_dns_caching(&(s->cache_control), &s->hdr_info.client_request);

  if (s->client_info.http_version == HTTPVersion(0, 9)) {
    s->cache_info.directives.does_client_permit_lookup = false;
    s->cache_info.directives.does_client_permit_storing = false;
  }
}

inline bool
HttpTransact::is_server_negative_cached(State * s)
{
  if (s->host_db_info.app.http_data.last_failure != 0 &&
      s->host_db_info.app.http_data.last_failure + s->http_config_param->down_server_timeout > s->client_request_time) {
    return true;
  } else {
    // Make sure some nasty clock skew has not happened
    //  Use the server timeout to set an upperbound as to how far in the
    //   future we should tolerate bogus last failure times.  This sets
    //   the upper bound to the time that we would ever consider a server
    //   down to 2*down_server_timeout
    if (s->client_request_time + s->http_config_param->down_server_timeout < s->host_db_info.app.http_data.last_failure) {
      s->host_db_info.app.http_data.last_failure = 0;
      ink_assert(!"extreme clock skew");
      return true;
    }
    return false;
  }
}

inline static void
update_current_info(HttpTransact::CurrentInfo * into,
                    HttpTransact::ConnectionAttributes * from, HttpTransact::LookingUp_t who, int attempts)
{
  into->request_to = who;
  into->server = from;
  into->attempts = attempts;
}

inline static void
update_dns_info(HttpTransact::DNSLookupInfo * dns, HttpTransact::CurrentInfo * from, int attempts, Arena * arena)
{
  dns->looking_up = from->request_to;
  dns->lookup_name = from->server->name;
  dns->attempts = attempts;
}

inline static HTTPHdr *
find_appropriate_cached_resp(HttpTransact::State * s)
{
  HTTPHdr *c_resp = NULL;

  if (s->cache_info.object_store.valid()) {
    c_resp = s->cache_info.object_store.response_get();
    if (c_resp != NULL && c_resp->valid())
      return c_resp;
  }

  ink_assert(s->cache_info.object_read != NULL);
  return s->cache_info.object_read->response_get();
}

inline static bool
is_negative_caching_appropriate(HttpTransact::State * s)
{
  if (!s->http_config_param->negative_caching_enabled || s->no_negative_cache || !s->hdr_info.server_response.valid())
    return false;

  switch (s->hdr_info.server_response.status_get()) {
  case HTTP_STATUS_NO_CONTENT:
  case HTTP_STATUS_USE_PROXY:
  case HTTP_STATUS_BAD_REQUEST:
  case HTTP_STATUS_FORBIDDEN:
  case HTTP_STATUS_NOT_FOUND:
  case HTTP_STATUS_METHOD_NOT_ALLOWED:
  case HTTP_STATUS_REQUEST_URI_TOO_LONG:
  case HTTP_STATUS_INTERNAL_SERVER_ERROR:
  case HTTP_STATUS_NOT_IMPLEMENTED:
  case HTTP_STATUS_BAD_GATEWAY:
  case HTTP_STATUS_SERVICE_UNAVAILABLE:
  case HTTP_STATUS_GATEWAY_TIMEOUT:
    return true;
  default:
    break;
  }

  return false;
}

inline static
  HttpTransact::LookingUp_t
find_server_and_update_current_info(HttpTransact::State * s)
{
  URL *url = s->hdr_info.client_request.url_get();

  int host_len;
  const char *host = url->host_get(&host_len);

  if (ptr_len_cmp(host, host_len, local_host_ip_str, sizeof(local_host_ip_str) - 1) == 0) {
    // Do not forward requests to local_host onto a parent.
    // I just wanted to do this for cop heartbeats, someone else
    // wanted it for all requests to local_host.
    s->parent_result.r = PARENT_DIRECT;
  } else if (url->scheme_get_wksidx() == URL_WKSIDX_HTTPS) {
    // Do not forward HTTPS requests onto a parent.
    s->parent_result.r = PARENT_DIRECT;
  } else if (s->method == HTTP_WKSIDX_CONNECT && s->http_config_param->disable_ssl_parenting) {
    s->parent_result.r = PARENT_DIRECT;
  } else if (s->http_config_param->uncacheable_requests_bypass_parent &&
             s->http_config_param->no_dns_forward_to_parent == 0 &&
             !HttpTransact::is_request_cache_lookupable(s, &s->hdr_info.client_request)) {
    // request not lookupable and cacheable, so bypass parent
    // Note that the configuration of the proxy as well as the request
    // itself affects the result of is_request_cache_lookupable();
    // we are assuming both child and parent have similar configuration
    // with respect to whether a request is cacheable or not.
    // For example, the cache_urls_that_look_dynamic variable.
    Debug("http_trans", "request not cacheable, so bypass parent");
    s->parent_result.r = PARENT_DIRECT;
  } else {
    switch (s->parent_result.r) {
    case PARENT_UNDEFINED:
      s->parent_params->findParent(&s->request_data, &s->parent_result);
      break;
    case PARENT_SPECIFIED:
      s->parent_params->nextParent(&s->request_data, &s->parent_result);

      // Hack!
      // We already have a parent that failed, if we are now told
      //  to go the origin server, we can only obey this if we
      //  dns'ed the origin server
      if (s->parent_result.r == PARENT_DIRECT && s->http_config_param->no_dns_forward_to_parent != 0) {
        ink_assert(s->server_info.ip == 0);
        s->parent_result.r = PARENT_FAIL;
      }
      break;
    case PARENT_FAIL:
      // Check to see if should bypass the parent and go direct
      //   We can only do this if 
      //   1) the parent was not set from API
      //   2) the config permits us
      //   3) the config permitted us to dns the origin server
      if (!s->parent_params->apiParentExists(&s->request_data) && s->parent_result.rec->bypass_ok() &&
          s->http_config_param->no_dns_forward_to_parent == 0) {
        s->parent_result.r = PARENT_DIRECT;
      }
      break;
    default:
      ink_assert(0);
      // FALL THROUGH
    case PARENT_DIRECT:
      //              // if we have already decided to go direct
      //              // dont bother calling nextParent.
      //              // do nothing here, guy.
      break;
    }
  }

  switch (s->parent_result.r) {
  case PARENT_SPECIFIED:
    s->parent_info.name = s->arena.str_store(s->parent_result.hostname, strlen(s->parent_result.hostname));
    s->parent_info.port = s->parent_result.port;
    update_current_info(&s->current, &s->parent_info, HttpTransact::PARENT_PROXY, (s->current.attempts)++);
    update_dns_info(&s->dns_info, &s->current, 0, &s->arena);
    HTTP_DEBUG_ASSERT(s->dns_info.looking_up == HttpTransact::PARENT_PROXY);
    s->next_hop_scheme = URL_WKSIDX_HTTP;

    return HttpTransact::PARENT_PROXY;
  case PARENT_FAIL:
    // No more parents - need to return an error message
    s->current.request_to = HttpTransact::HOST_NONE;
    return HttpTransact::HOST_NONE;

  case PARENT_DIRECT:
    /* fall through */
  default:
    update_current_info(&s->current, &s->server_info, HttpTransact::ORIGIN_SERVER, (s->current.attempts)++);
    update_dns_info(&s->dns_info, &s->current, 0, &s->arena);
    HTTP_DEBUG_ASSERT(s->dns_info.looking_up == HttpTransact::ORIGIN_SERVER);
    s->next_hop_scheme = s->scheme;
    return HttpTransact::ORIGIN_SERVER;
  }
}

inline static bool
do_cookies_prevent_caching(int cookies_conf, HTTPHdr * request, HTTPHdr * response, HTTPHdr * cached_request = NULL)
{
  enum CookiesConfig
  {
    COOKIES_CACHE_NONE = 0,     // do not cache any responses to cookies
    COOKIES_CACHE_ALL = 1,      // cache for any content-type (ignore cookies)
    COOKIES_CACHE_IMAGES = 2,   // cache only for image types
    COOKIES_CACHE_ALL_BUT_TEXT = 3,     // cache for all but text content-types
    COOKIES_CACHE_ALL_BUT_TEXT_EXT = 4  // cache for all but text content-types except with OS response
      // without "Set-Cookie" or with "Cache-Control: public"
  };

  const char *content_type = NULL;
  int str_len;

#ifdef DEBUG
  HTTP_DEBUG_ASSERT(request->type_get() == HTTP_TYPE_REQUEST);
  HTTP_DEBUG_ASSERT(response->type_get() == HTTP_TYPE_RESPONSE);
  if (cached_request) {
    HTTP_DEBUG_ASSERT(cached_request->type_get() == HTTP_TYPE_REQUEST);
  }
#endif

  // Can cache all regardless of cookie header - just ignore all cookie headers
  if ((CookiesConfig) cookies_conf == COOKIES_CACHE_ALL) {
    return false;
  }
  // Do not cache if cookies option is COOKIES_CACHE_NONE
  if ((CookiesConfig) cookies_conf == COOKIES_CACHE_NONE) {
    return true;
  }
  // It is considered that Set-Cookie headers can be safely ignored
  // for non text content types if Cache-Control private is not set.
  // This enables a bigger hit rate, which currently outweighs the risk of
  // breaking origin servers that truly intend to set a cookie with other
  // objects such as images.
  // At this time, it is believed that only advertisers do this, and that
  // customers won't care about it.

  // If the response does not have a Set-Cookie header and
  // the response does not have a Cookie header and
  // the object is not cached or the request does not have a Cookie header
  // then cookies do not prevent caching.
  if (!response->presence(MIME_PRESENCE_SET_COOKIE) &&
      !request->presence(MIME_PRESENCE_COOKIE) && (cached_request == NULL
                                                   || !cached_request->presence(MIME_PRESENCE_COOKIE))) {
    return false;
  }
  // All other options depend on the Content-Type
  content_type = response->value_get(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE, &str_len);

  if ((CookiesConfig) cookies_conf == COOKIES_CACHE_IMAGES) {
    if (content_type && str_len >= 5 && memcmp(content_type, "image", 5) == 0) {
      // Images can be cached
      return false;
    }
    return true;                // do not cache if  COOKIES_CACHE_IMAGES && content_type != "image"
  }
  // COOKIES_CACHE_ALL_BUT_TEXT || COOKIES_CACHE_ALL_BUT_TEXT_EXT
  // Note: if the configuration is bad, we consider
  // COOKIES_CACHE_ALL_BUT_TEXT to be the default

  if (content_type && str_len >= 4 && memcmp(content_type, "text", 4) == 0) {   // content type  - "text"
    // Text objects cannot be cached unless the option is
    // COOKIES_CACHE_ALL_BUT_TEXT_EXT.
    // Furthermore, if there is a Set-Cookie header, then
    // Cache-Control must be set.
    if ((CookiesConfig) cookies_conf == COOKIES_CACHE_ALL_BUT_TEXT_EXT &&
        ((!response->presence(MIME_PRESENCE_SET_COOKIE)) || response->is_cache_control_set(HTTP_VALUE_PUBLIC))) {
      return false;
    }
    return true;
  }
  return false;                 // Non text objects can be cached
}


inline static bool
does_method_require_cache_copy_deletion(int method)
{
  return ((method != HTTP_WKSIDX_GET) &&
          (method == HTTP_WKSIDX_DELETE || method == HTTP_WKSIDX_PURGE ||
           method == HTTP_WKSIDX_PUT || method == HTTP_WKSIDX_POST));
}


inline static
  HttpTransact::StateMachineAction_t
how_to_open_connection(HttpTransact::State * s)
{
  HTTP_DEBUG_ASSERT(s->pending_work == NULL);

  // Originally we returned which type of server to open
  // Now, however, we may want to issue a cache
  // operation first in order to lock the cache
  // entry to prevent multiple origin server requests
  // for the same document.
  // The cache operation that we actually issue, of
  // course, depends on the specified "cache_action".
  // If there is no cache-action to be issued, just
  // connect to the server.
  switch (s->cache_info.action) {
  case HttpTransact::CACHE_PREPARE_TO_DELETE:
  case HttpTransact::CACHE_PREPARE_TO_UPDATE:
  case HttpTransact::CACHE_PREPARE_TO_WRITE:
    s->transact_return_point = HttpTransact::handle_cache_write_lock;
    return HttpTransact::CACHE_ISSUE_WRITE;
  default:
    // This covers:
    // CACHE_DO_UNDEFINED, CACHE_DO_NO_ACTION, CACHE_DO_DELETE,
    // CACHE_DO_LOOKUP, CACHE_DO_REPLACE, CACHE_DO_SERVE,
    // CACHE_DO_SERVE_AND_DELETE, CACHE_DO_SERVE_AND_UPDATE,
    // CACHE_DO_UPDATE, CACHE_DO_WRITE, TOTAL_CACHE_ACTION_TYPES
    break;
  }

  if (s->next_hop_scheme == URL_WKSIDX_FTP) {
    // disable user agent keep-alive for ftp
    s->client_info.keep_alive = HTTP_NO_KEEPALIVE;
    return HttpTransact::FTP_SERVER_OPEN;
  } else if (s->method == HTTP_WKSIDX_CONNECT && s->parent_result.r != PARENT_SPECIFIED) {
    s->cdn_saved_next_action = HttpTransact::ORIGIN_SERVER_RAW_OPEN;
  } else {
    s->cdn_saved_next_action = HttpTransact::ORIGIN_SERVER_OPEN;
  }

  // In the following, if url_remap_mode == 2 (URL_REMAP_FOR_OS)
  // then do remapping for requests to OS's.
  // Whether there is CDN remapping or not, goto DNS_LOOKUP;
  // after that, it'll goto ORIGIN_SERVER_(RAW_)OPEN as needed.

  if ((url_remap_mode == HttpTransact::URL_REMAP_FOR_OS) &&
      (s->current.request_to == HttpTransact::ORIGIN_SERVER) && !s->cdn_remap_complete) {

    Debug("cdn", "*** START CDN Remapping *** CDN mode = %d", url_remap_mode);

    char *remap_redirect = NULL;
    int host_len;
    const char *host;

    // We need to copy the client request into the server request.  Why?  BUGBUG
    s->hdr_info.server_request.url_set(s->hdr_info.client_request.url_get());

    // TODO yeah, not sure everything here is correct with redirects
    if (request_url_remap(s, &s->hdr_info.server_request, &remap_redirect, &s->unmapped_request_url)) {
      ink_assert(!remap_redirect);      // should not redirect in this code
      HttpTransact::initialize_state_variables_for_origin_server(s, &s->hdr_info.server_request, true);
      Debug("cdn", "Converting proxy request to server request");
      // Check whether a Host header field is missing from a 1.0 or 1.1 request.
      if (                      /*outgoing_version != HTTPVersion(0,9) && */
           !s->hdr_info.server_request.presence(MIME_PRESENCE_HOST)) {
        URL *url = s->hdr_info.server_request.url_get();
        host = url->host_get(&host_len);
        // Add a ':port' to the HOST header if the request is not going 
        // to the default port.
        int port = url->port_get();
        if (port != url_canonicalize_port(URL_TYPE_HTTP, 0)) {
          char *buf = (char *) xmalloc(host_len + 15);
          strncpy(buf, host, host_len);
          host_len += ink_snprintf(buf + host_len, host_len + 15, ":%d", port);
          s->hdr_info.server_request.value_set(MIME_FIELD_HOST, MIME_LEN_HOST, buf, host_len);
          xfree(buf);
        } else {
          s->hdr_info.server_request.value_set(MIME_FIELD_HOST, MIME_LEN_HOST, host, host_len);
        }
        xfree(remap_redirect);  // This apparently shouldn't happen...
      }
      // Stripping out the host name from the URL
      if (s->current.server == &s->server_info && s->next_hop_scheme == URL_WKSIDX_HTTP) {
        Debug("cdn", "Removing host name from URL");
        HttpTransactHeaders::remove_host_name_from_url(&s->hdr_info.server_request);
      }
    }                           // the URL was remapped
    if (is_debug_tag_set("cdn")) {
      char *d_url = s->hdr_info.server_request.url_get()->string_get(NULL);
      if (d_url)
        Debug("cdn", "URL: %s", d_url);
      char *d_hst = (char *) s->hdr_info.server_request.value_get(MIME_FIELD_HOST,
                                                                  MIME_LEN_HOST,
                                                                  &host_len);
      if (d_hst)
        Debug("cdn", "Host Hdr: %s", d_hst);

      if (d_url) {
        //s->arena.str_free (d_url); <- vl: incorrect one sinse it was allocated without arena
        xfree(d_url);
      }
    }
    s->cdn_remap_complete = true;       // It doesn't matter if there was an actual remap or not
    s->transact_return_point = HttpTransact::OSDNSLookup;
    ink_assert(s->next_action);
    ink_assert(s->cdn_saved_next_action);
    return HttpTransact::DNS_LOOKUP;
  }

  if (!s->already_downgraded) { //false unless downgraded previously (possibly due to HTTP 505)
    (&s->hdr_info.server_request)->version_set(HTTPVersion(1, 1));
    HttpTransactHeaders::convert_request(s->current.server->http_version, &s->hdr_info.server_request);
  }

  HTTP_DEBUG_ASSERT(s->cdn_saved_next_action ==
                    HttpTransact::ORIGIN_SERVER_OPEN ||
                    s->cdn_saved_next_action == HttpTransact::ORIGIN_SERVER_RAW_OPEN);
  return s->cdn_saved_next_action;
}

#ifdef USE_NCA
static inku64
extract_ctag_from_response(HTTPHdr * h)
{

  HTTP_DEBUG_ASSERT(h->type_get() == HTTP_TYPE_RESPONSE);
  MIMEField *ctag_field = h->field_find("@Ctag", 5);

  inku64 ctag;
  if (ctag_field != NULL) {
    int tmp;
    const char *ctag_str = ctag_field->value_get(&tmp);
    // strtoull requires NULL terminated string so
    //  create one
    if (tmp < 64) {
      char tmp_str[64];
      memcpy(tmp_str, ctag_str, tmp);
      tmp_str[tmp] = '\0';
      ctag = strtoull(tmp_str, NULL, 10);
    } else {
      // Obviously bogus, ignore
      ctag = 0;
    }
  } else {
    ctag = 0;
  }

  return ctag;
}
#endif

/*****************************************************************************
 *****************************************************************************
 ****                                                                     ****
 ****                 HttpTransact State Machine Handlers                 ****
 ****                                                                     ****
 **** What follow from here on are the state machine handlers - the code  ****
 **** which is called from HttpSM::set_next_state to specify ****
 **** what action the state machine needs to execute next. These ftns     ****
 **** take as input just the state and set the next_action variable.      ****
 *****************************************************************************
 *****************************************************************************/

void
HttpTransact::BadRequest(State * s)
{

  Debug("http_trans", "[BadRequest]" "parser marked request bad");

  bootstrap_state_variables_from_request(s, &s->hdr_info.client_request);

  build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Invalid HTTP Request",
                       "request#syntax_error", "Bad request syntax", "");
  TRANSACT_RETURN(PROXY_SEND_ERROR_CACHE_NOOP, NULL);

}

void
HttpTransact::HandleBlindTunnel(State * s)
{
  Debug("http_trans", "[HttpTransact::HandleBlindTunnel]");

  // We've received a request on a port which we blind forward
  //  For logging purposes we create a fake request
  s->hdr_info.client_request.create(HTTP_TYPE_REQUEST);
  s->hdr_info.client_request.method_set(HTTP_METHOD_CONNECT, HTTP_LEN_CONNECT);
  URL u;
  s->hdr_info.client_request.url_create(&u);
  u.scheme_set(URL_SCHEME_TUNNEL, URL_LEN_TUNNEL);
  s->hdr_info.client_request.url_set(&u);

  // We set the version to 0.9 because once we know where we are going
  //   this blind ssl tunnel is indistinguishable from a "CONNECT 0.9"
  //   except for the need to suppression error messages
  HTTPVersion ver(0, 9);
  s->hdr_info.client_request.version_set(ver);

  // Now we need to figure where the request is destined for
  //   Two options.  First, we got the request for packets
  //   sent through the arm layer.  If pick the address out that
  //   way.  Otherwise, the client connected directly which means
  //   we use our address and a remap rule is reuquired to send
  //   request to it's proper destination
  bool dest_found = false;
  // ua_session is NULL for scheduled updates.
  // Don't use req_flavor to do the test because if updated
  // urls are remapped, the req_flavor is changed to REV_PROXY.
  if (s->http_config_param->transparency_enabled && s->state_machine->ua_session != NULL) {
    dest_found = setup_transparency(s);
  }

  if (dest_found == false) {

    struct in_addr dest_addr;
    dest_addr.s_addr = s->state_machine->ua_session->get_netvc()->get_local_ip();

    char *new_host = inet_ntoa(dest_addr);
    s->hdr_info.client_request.url_get()->host_set(new_host, strlen(new_host));
    // get_local_port() returns a port number in network order
    // so it needs to be converted to host order (eg, in i386 machine)
    s->hdr_info.client_request.url_get()->port_set(ntohs(s->state_machine->ua_session->get_netvc()->get_local_port()));
  }
  // Intialize the state vars necessary to sending error responses
  bootstrap_state_variables_from_request(s, &s->hdr_info.client_request);

  if (is_debug_tag_set("http_trans")) {
    int host_len;
    const char *host = s->hdr_info.client_request.url_get()->host_get(&host_len);
    Debug("http_trans", "[HandleBlindTunnel] destination set to %.*s:%d",
          host_len, host, s->hdr_info.client_request.url_get()->port_get());
  }
  // Now we need to run the url remapping engine to find the where
  //   this request really goes since we were sent was bound for
  //   machine we are running on

  // Do request_url_remap only if url_remap_mode != URL_REMAP_FOR_OS.
  bool url_remap_success = false;
  char *remap_redirect = NULL;

  // TODO take a look at this
  if (url_remap_mode == URL_REMAP_DEFAULT || url_remap_mode == URL_REMAP_ALL) {
    url_remap_success = request_url_remap(s, &s->hdr_info.client_request, &remap_redirect, &s->unmapped_request_url);
  }
  // We must have mapping or we will self loop since the this
  //    request was addressed to us to begin with.  Remap directs
  //    are something used in the normal reverse proxy and if we
  //    get them here they indicate a very bad misconfiguration!
  if (url_remap_success == false || remap_redirect != NULL) {
    // The error message we send back will be suppressed so
    //  the only important thing in selecting the error is what
    //  status code it gets logged as
    build_error_response(s, HTTP_STATUS_INTERNAL_SERVER_ERROR, "Port Forwarding Error", "default", "");

    int host_len;
    const char *host = s->hdr_info.client_request.url_get()->host_get(&host_len);

    Log::error("Forwarded port error: request with destination %.*s:%d "
               "does not have a mapping", host_len, host, s->hdr_info.client_request.url_get()->port_get());

    TRANSACT_RETURN(PROXY_SEND_ERROR_CACHE_NOOP, NULL);
  }
  // Set the mode to tunnel so that we don't lookup the cache
  s->current.mode = TUNNELLING_PROXY;

  // Let the request work it's way through the code and
  //  we grab it again after the raw connection has been opened
  HandleRequest(s);
}

bool
HttpTransact::perform_accept_encoding_filtering(State * s)
{
  HttpUserAgent_RegxEntry *uae;
  HTTPHdr *client_request;
  MIMEField *accept_field;
  MIMEField *usragent_field;
  char tmp_ua_buf[1024], *c;
  char const *u_agent = NULL;
  int u_agent_len = 0;
  bool retcode = false;
  bool ua_match = false;

  //fprintf(stderr,"HttpTransact::perform_accept_encoding_filtering - start\n");

  client_request = &s->hdr_info.client_request;

  // Make sense to check Accept-Encoding if UserAgent is present (and matches)
  if ((usragent_field =
       client_request->field_find(MIME_FIELD_USER_AGENT,
                                  MIME_LEN_USER_AGENT)) != 0 &&
      (u_agent = usragent_field->value_get(&u_agent_len)) != 0 && u_agent_len > 0) {
    if (u_agent_len >= (int) sizeof(tmp_ua_buf))
      u_agent_len = (int) (sizeof(tmp_ua_buf) - 1);
    memcpy(tmp_ua_buf, u_agent, u_agent_len);
    tmp_ua_buf[u_agent_len] = 0;
    //fprintf(stderr,"User-Agent: \"%s\"\n",tmp_ua_buf);

    // Check hardcoded case MSIE[6-9] & Mozilla/4
    if ((c = strstr(tmp_ua_buf, "MSIE")) != NULL) {
      if (c[5] >= '6' && c[5] <= '9')
        return false;           // Don't change anything for IE > 6
      ua_match = true;
    } else if (!strncasecmp(tmp_ua_buf, "mozilla", 7)) {
      if (tmp_ua_buf[8] >= '5' && tmp_ua_buf[8] <= '9')
        return false;           // Don't change anything for Mozilla > 4
      ua_match = true;
    }
    // Check custom filters
    if (!ua_match && HttpConfig::user_agent_list) {
      for (uae = HttpConfig::user_agent_list; uae && !ua_match; uae = uae->next) {
        switch (uae->stype) {
        case HttpUserAgent_RegxEntry::STRTYPE_SUBSTR_CASE:     /* .substring, .string */
          if (u_agent_len >= uae->user_agent_str_size &&
              !memcmp(tmp_ua_buf, uae->user_agent_str, uae->user_agent_str_size))
            ua_match = true;
          break;
        case HttpUserAgent_RegxEntry::STRTYPE_SUBSTR_NCASE:    /* .substring_ncase, .string_ncase */
          if (u_agent_len >= uae->user_agent_str_size &&
              !strncasecmp(uae->user_agent_str, tmp_ua_buf, uae->user_agent_str_size))
            ua_match = true;
          break;
        case HttpUserAgent_RegxEntry::STRTYPE_REGEXP:  /* .regexp POSIX regular expression */
          if (uae->regx_valid && !regexec(&uae->regx, tmp_ua_buf, 0, NULL, 0))
            ua_match = true;
          break;
        default:               /* unknown type in the structure - bad initialization - impossible bug! */
          /* I can use ink_error() here since we should shutdown TS immediately */
          ink_error
            ("[HttpTransact::perform_accept_encoding_filtering] - get unknown User-Agent string type - bad initialization");
        };
      }
    }

    /* If we have correct User-Agent header ....
       Just set Accept-Encoding: identity or .... do nothing because
       "If no Accept-Encoding field is present in a request, the server MAY assume that the client
       will accept any content coding. In this case, if "identity" is one of the available content-codings,
       then the server SHOULD use the "identity" content-coding, unless it has additional information that
       a different content-coding is meaningful to the client." */
    if (ua_match && (accept_field = client_request->field_find(MIME_FIELD_ACCEPT_ENCODING, MIME_LEN_ACCEPT_ENCODING)) != NULL) {        // char const *a_encoding = NULL;
      // int a_encoding_len = 0;
      //if((a_encoding = accept_field->value_get(&a_encoding_len)) != 0 && a_encoding_len > 0)
      //{ fprintf(stderr,"Accept-Encoding: \"");
      //  for(int i = 0;i < a_encoding_len;i++) { fprintf(stderr,"%c",a_encoding[i]); }
      //  fprintf(stderr,"\" - before  \"");
      //}
      // Accept-Encoding: identity
      client_request->field_value_set(accept_field, "identity", 8);     // ", *;q=0"
      //if((a_encoding = accept_field->value_get(&a_encoding_len)) != 0 && a_encoding_len > 0)
      //{ for(int i = 0;i < a_encoding_len;i++) { fprintf(stderr,"%c",a_encoding[i]); }
      //  fprintf(stderr,"\" - after\n");
      //}
      retcode = true;
    }
  }                             // end of 'user-agent'
  return retcode;
}

void
HttpTransact::StartRemapRequest(State * s)
{
  Debug("http_trans", "START HttpTransact::StartRemapRequest");

        /**
	 * Check for URL remappings before checking request
	 * validity or initializing state variables since       
	 * the remappings can insert or change the destination  
	 * host, port and protocol.                             
	**/

  HTTPHdr *incoming_request = &(s->hdr_info.client_request);
  URL *url = incoming_request->url_get();
  int host_len, path_len, method;
  const char *host = url->host_get(&host_len);
  int port = url->port_get();
  const char *path = url->path_get(&path_len);

  const char syntxt[] = "synthetic.txt";

  s->cop_test_page =
    (ptr_len_cmp(host,
                 host_len,
                 local_host_ip_str,
                 sizeof(local_host_ip_str) - 1) == 0) && (ptr_len_cmp(path, path_len, syntxt, sizeof(syntxt) - 1) == 0);

  // Determine whether the incoming request is a Traffic Net request.
  s->traffic_net_req = ((method = incoming_request->method_get_wksidx()) == HTTP_WKSIDX_POST)
    &&
    (ptr_len_cmp
     (host, host_len, s->http_config_param->tn_server,
      s->http_config_param->tn_server_len) == 0) && (port == s->http_config_param->tn_port);

  //////////////////////////////////////////////////////////////////
  // FIX: this logic seems awfully convoluted and hard to follow; //
  //      seems like we could come up with a more elegant and     //
  //      comprehensible design that generalized things           //
  //////////////////////////////////////////////////////////////////

  /////////////////////////////////////////////////////////////////
  // run the remap url-rewriting engine:                         //
  //                                                             //
  // * the variable <url_remap_success> is set true if           //
  //   the url was rewritten                                     //
  //                                                             //
  // * the variable <remap_redirect> is set to non-NULL if there //
  //   is a URL provided that the proxy is supposed to redirect  //
  //   requesters of a particular URL to.                        //
  /////////////////////////////////////////////////////////////////

  if (is_debug_tag_set("http_chdr_describe") || is_debug_tag_set("http_trans")) {
    Debug("http_trans", "Before Remapping:");
    obj_describe(s->hdr_info.client_request.m_http, 1);
  }

  if (url_remap_mode == URL_REMAP_DEFAULT || url_remap_mode == URL_REMAP_ALL) {
    if (s->http_config_param->referer_filter_enabled) {
      s->filter_mask = URL_REMAP_FILTER_REFERER;
      if (s->http_config_param->referer_format_redirect)
        s->filter_mask |= URL_REMAP_FILTER_REDIRECT_FMT;
    }
  }

  Debug("http_trans", "END HttpTransact::StartRemapRequest");
  TRANSACT_RETURN(HTTP_REMAP_REQUEST, HttpTransact::EndRemapRequest);
}

void
HttpTransact::EndRemapRequest(State * s)
{
  Debug("http_trans", "START HttpTransact::EndRemapRequest");

  HTTPHdr *incoming_request = &(s->hdr_info.client_request);
  URL *url = incoming_request->url_get();
  int host_len, method;
  const char *host = url->host_get(&host_len);

  method = incoming_request->method_get_wksidx();

  ////////////////////////////////////////////////////////////////
  // if we got back a URL to redirect to, vector the user there //
  ////////////////////////////////////////////////////////////////
  if (s->remap_redirect != NULL) {
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
    if (s->http_return_code == HTTP_STATUS_MOVED_PERMANENTLY) {
      build_error_response(s, HTTP_STATUS_MOVED_PERMANENTLY,
                           "Redirect", "redirect#moved_permanently", "\"<em>%s</em>\".<p>", s->remap_redirect);
    } else {
      build_error_response(s, HTTP_STATUS_MOVED_TEMPORARILY,
                           "Redirect", "redirect#moved_temporarily", "\"<em>%s</em>\".<p>", s->remap_redirect);
    }
    s->hdr_info.client_response.value_set(MIME_FIELD_LOCATION,
                                          MIME_LEN_LOCATION, s->remap_redirect, strlen(s->remap_redirect));
    xfree(s->remap_redirect);
    s->reverse_proxy = false;
    goto done;
  }
  /////////////////////////////////////////////////////
  // Quick HTTP filtering (primary key: http method) //
  /////////////////////////////////////////////////////
  if (s->http_config_param->quick_filter_mask) {
    process_quick_http_filter(s, method);
  }
  /////////////////////////////////////////////////////////////////////////
  // We must close this connection if client_connection_enabled == false //
  /////////////////////////////////////////////////////////////////////////
  if (!s->client_connection_enabled) {
    build_error_response(s, HTTP_STATUS_FORBIDDEN,
                         "Access Denied", "access#denied", "You are not allowed to access the document.");
    s->reverse_proxy = false;
    goto done;
  }
  /////////////////////////////////////////////////////////////////
  // Check if remap plugin set HTTP return code and return body  //
  /////////////////////////////////////////////////////////////////
  if (s->http_return_code != HTTP_STATUS_NONE) {
    build_error_response(s, s->http_return_code, NULL, NULL, s->return_xbuf_size ? s->return_xbuf : NULL);
    s->reverse_proxy = false;
    goto done;
  }
  ///////////////////////////////////////////////////////////////
  // if no mapping was found, handle the cases where:          //
  //                                                           //
  // (1) reverse proxy is on, and no URL host (server request) //
  // (2) no mappings are found, but mappings strictly required //
  ///////////////////////////////////////////////////////////////

  if (!s->url_remap_success) {
    /////////////////////////////////////////////////////////
    // check for: (1) reverse proxy is on, and no URL host //
    /////////////////////////////////////////////////////////
    if (s->http_config_param->reverse_proxy_enabled && host == NULL) {
      /////////////////////////////////////////////////////////
      // the url mapping failed, reverse proxy was enabled,  //
      // and the url contains no host:                       //
      //                                                     //
      // * if there is an explanatory redirect, send there   //
      // * if there was no host header, send "no host" error //
      // * if there was a host, say "not found"
      /////////////////////////////////////////////////////////

      char *redirect_url = s->http_config_param->reverse_proxy_no_host_redirect;
      int redirect_url_len = s->http_config_param->reverse_proxy_no_host_redirect_len;
      int host_len;
      const char *host_hdr = incoming_request->value_get(MIME_FIELD_HOST, MIME_LEN_HOST,
                                                         &host_len);
      SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
      if (redirect_url) {       /* there is a redirect url */
        build_error_response(s, HTTP_STATUS_MOVED_TEMPORARILY,
                             "Redirect For Explanation", "request#no_host", "\"<em>%s</em>\".<p>", redirect_url);
        s->hdr_info.client_response.value_set(MIME_FIELD_LOCATION, MIME_LEN_LOCATION, redirect_url, redirect_url_len);
      } else if (host_hdr == NULL) {    /* no host header */
        build_error_response(s, HTTP_STATUS_BAD_REQUEST,
                             "Host Header Required", "request#no_host",
                             ("Your browser did not send a \"Host:\" HTTP header, "
                              "so the virtual host being requested could not be "
                              "determined.  To access this site you will need "
                              "to upgrade to a modern browser that supports the HTTP " "\"Host:\" header field."));
      } else {                  /* there was a host header */

        build_error_response(s, HTTP_STATUS_NOT_FOUND,
                             "Not Found on Accelerator", "urlrouting#no_mapping", "Your requested URL was not found.");
      }
      s->reverse_proxy = false;
      goto done;
    }
    ///////////////////////////////////////////////////////
    // check for: (2) no mappings, but mappings required //
    ///////////////////////////////////////////////////////
    if (s->http_config_param->url_remap_required && !s->cop_test_page && !s->traffic_net_req) {
      ///////////////////////////////////////////////////////
      // the url mapping failed, but mappings are strictly //
      // required (except for synthetic cop accesses), so  //
      // return an error message.                          //
      ///////////////////////////////////////////////////////

      SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
      build_error_response(s, HTTP_STATUS_NOT_FOUND,
                           "Not Found", "urlrouting#no_mapping", "Your requested URL was not found.");

      s->reverse_proxy = false;
      goto done;
    }
  } else {
    if (s->http_config_param->reverse_proxy_enabled) {
      s->req_flavor = REQ_FLAVOR_REVPROXY;
    }
  }
  s->reverse_proxy = true;

done:
        /**
   * Since we don't want to return 404 Not Found error if there's redirect rule,
   * the function to do redirect is moved before sending the 404 error.
  **/
  if (handleIfRedirect(s)) {
    Debug("http_trans", "END HttpTransact::RemapRequest");
    TRANSACT_RETURN(PROXY_INTERNAL_CACHE_NOOP, NULL);
  }

  if (s->reverse_proxy) {
    Debug("url_rewrite", "s->reverse_proxy is true");
  } else {
    Debug("url_rewrite", "s->reverse_proxy is false");
  }

  if (is_debug_tag_set("http_chdr_describe") || is_debug_tag_set("http_trans") || is_debug_tag_set("url_rewrite")) {
    Debug("http_trans", "After Remapping:");
    obj_describe(s->hdr_info.client_request.m_http, 1);
  }

  if (!s->reverse_proxy) {
    Debug("http_trans", "END HttpTransact::EndRemapRequest");
    HTTP_INCREMENT_TRANS_STAT(http_invalid_client_requests_stat);
    TRANSACT_RETURN(PROXY_SEND_ERROR_CACHE_NOOP, NULL);
  } else {
    Debug("http_trans", "END HttpTransact::EndRemapRequest");
    TRANSACT_RETURN(HTTP_API_READ_REQUEST_HDR, HttpTransact::HandleRequest);
  }

  ink_debug_assert(!"not reached");

}

void
HttpTransact::ModifyRequest(State * s)
{
  int scheme, hostname_len;
  const char *hostname;
  MIMEField *max_forwards_f;
  int max_forwards = -1;

  Debug("http_trans", "START HttpTransact::ModifyRequest");

  // Intialize the state vars necessary to sending error responses      
  bootstrap_state_variables_from_request(s, &s->hdr_info.client_request);

  ////////////////////////////////////////////////
  // If there is no scheme default to http      //
  ////////////////////////////////////////////////
  URL *url = s->hdr_info.client_request.url_get();

  if ((hostname = url->host_get(&hostname_len)) == NULL)
    s->hdr_info.client_req_is_server_style = true;

  s->orig_scheme = (scheme = url->scheme_get_wksidx());

  s->method = s->hdr_info.client_request.method_get_wksidx();
  if (scheme < 0 && s->method != HTTP_WKSIDX_CONNECT) {
    if (s->client_info.port_attribute == SERVER_PORT_SSL) {
      url->scheme_set(URL_SCHEME_HTTPS, URL_LEN_HTTPS);
      s->orig_scheme = URL_WKSIDX_HTTPS;
    } else {
      url->scheme_set(URL_SCHEME_HTTP, URL_LEN_HTTP);
      s->orig_scheme = URL_WKSIDX_HTTP;
    }
  }
  if (s->method == HTTP_WKSIDX_CONNECT && url->port_get() == 0)
    url->port_set(80);

  // If the incoming request is proxy-style AND contains a Host header,
  // then remove the Host header to prevent content spoofing.

  // Do not delete the Host header if Max-Forwards is 0
  max_forwards_f = s->hdr_info.client_request.field_find(MIME_FIELD_MAX_FORWARDS, MIME_LEN_MAX_FORWARDS);
  if (max_forwards_f) {
    max_forwards = max_forwards_f->value_get_int();
  }

  if ((max_forwards != 0) && !s->hdr_info.client_req_is_server_style && s->method != HTTP_WKSIDX_CONNECT) {
    MIMEField *host_field = s->hdr_info.client_request.field_find(MIME_FIELD_HOST, MIME_LEN_HOST);
    int host_val_len = hostname_len;
    const char **host_val = &hostname;
    int req_host_val_len;
    const char *req_host_val;
    int port = url->port_get_raw();
    char *buf = NULL;

    if (port > 0) {
      buf = (char *) xmalloc(host_val_len + 15);
      strncpy(buf, hostname, host_val_len);
      host_val_len += snprintf(buf + host_val_len, host_val_len + 15, ":%u", port);
      host_val = (const char**)(&buf);
    }

    if (!host_field ||
        (s->http_config_param->avoid_content_spoofing &&
         ((req_host_val = host_field->value_get(&req_host_val_len)) == NULL ||
          host_val_len != req_host_val_len || strncasecmp(*host_val, req_host_val, host_val_len) != 0))) {
      // instead of deleting the Host: header, set it to URL host for all requests (including HTTP/1.0)

      if (!host_field) {
        host_field = s->hdr_info.client_request.field_create(MIME_FIELD_HOST, MIME_LEN_HOST);
        s->hdr_info.client_request.field_attach(host_field);
      }

      s->hdr_info.client_request.field_value_set(host_field, *host_val, host_val_len);
    }

    if (buf)
      xfree(buf);
  }

  if (s->http_config_param->normalize_ae_gzip) {
    // if enabled, force Accept-Encoding header to gzip or no header
    MIMEField *ae_field = s->hdr_info.client_request.field_find(MIME_FIELD_ACCEPT_ENCODING, MIME_LEN_ACCEPT_ENCODING);

    if (ae_field) {
      if (HttpTransactCache::match_gzip(ae_field) == GZIP) {
        s->hdr_info.client_request.field_value_set(ae_field, "gzip", 4);
        Debug("http_trans", "[ModifyRequest] normalized Accept-Encoding to gzip");
      } else {
        s->hdr_info.client_request.field_delete(ae_field);
        Debug("http_trans", "[ModifyRequest] removed non-gzip Accept-Encoding");
      }
    }
  }

  ////////////////////////////////////////////////////////
  // First check for the presence of a host header or   //
  // the availability of the host name through the url. //
  ////////////////////////////////////////////////////////
  // ua_session is NULL for scheduled updates.
  // Don't use req_flavor to do the test because if updated
  // urls are remapped, the req_flavor is changed to REV_PROXY.
  if (s->http_config_param->transparency_enabled && s->state_machine->ua_session != NULL) {
    setup_transparency(s);
  }
  /////////////////////////////////////////////////////////
  // Modify Accept-Encoding for several specific User-Agent
  /////////////////////////////////////////////////////////
  if (s->http_config_param->accept_encoding_filter_enabled) {
    perform_accept_encoding_filtering(s);
  }

  Debug("http_trans", "END HttpTransact::ModifyRequest");

  TRANSACT_RETURN(HTTP_API_READ_REQUEST_PRE_REMAP, HttpTransact::StartRemapRequest);
}

// This function is supposed to figure out if this transaction is
// susceptible to a redirection as specified by remap.config
bool
HttpTransact::handleIfRedirect(State * s)
{
#ifndef INK_NO_REMAP
  int answer;
  char *remap_redirect = NULL;
  if ((answer =
       request_url_remap_redirect(&s->hdr_info.client_request, &remap_redirect, &s->unmapped_request_url)) == NONE)
    return false;

  if ((answer == PERMANENT_REDIRECT) || (answer == TEMPORARY_REDIRECT)) {
    if (answer == TEMPORARY_REDIRECT) {
      if ((s->client_info).http_version.m_version == HTTP_VERSION(1, 1)) {
        build_error_response(s, (HTTPStatus) 307
                             /* which is HTTP/1.1 for HTTP_STATUS_MOVED_TEMPORARILY */
                             ,
                             "Redirect", "redirect#moved_temporarily",
                             "%s <a href=\"%s\">%s</a>. %s",
                             "The document you requested is now",
                             remap_redirect, remap_redirect, "Please update your documents and bookmarks accordingly");
      } else {
        build_error_response(s,
                             HTTP_STATUS_MOVED_TEMPORARILY,
                             "Redirect",
                             "redirect#moved_temporarily",
                             "%s <a href=\"%s\">%s</a>. %s",
                             "The document you requested is now",
                             remap_redirect, remap_redirect, "Please update your documents and bookmarks accordingly");
      }
    } else {
      build_error_response(s,
                           HTTP_STATUS_MOVED_PERMANENTLY,
                           "Redirect",
                           "redirect#moved_permanently",
                           "%s <a href=\"%s\">%s</a>. %s",
                           "The document you requested is now",
                           remap_redirect, remap_redirect, "Please update your documents and bookmarks accordingly");
    }
    s->hdr_info.client_response.value_set(MIME_FIELD_LOCATION,
                                          MIME_LEN_LOCATION, remap_redirect, strlen(remap_redirect));
    xfree(remap_redirect);
    return true;
  }
#endif /* INK_NO_REMAP */
  return false;
}

void
HttpTransact::HandleRequest(State * s)
{
  Debug("http_trans", "START HttpTransact/HandleRequest");

  HTTP_DEBUG_ASSERT(!s->hdr_info.server_request.valid());

  HTTP_INCREMENT_TRANS_STAT(http_incoming_requests_stat);

  if (s->api_release_server_session == true) {
    s->api_release_server_session = false;
  }
  ///////////////////////////////////////////////
  // if request is bad, return error response  //
  ///////////////////////////////////////////////

  if (!(is_request_valid(s, &s->hdr_info.client_request))) {
    HTTP_INCREMENT_TRANS_STAT(http_invalid_client_requests_stat);
    Debug("http_seq", "[HttpTransact::HandleRequest] request invalid.");
    s->next_action = PROXY_SEND_ERROR_CACHE_NOOP;
    //  s->next_action = HttpTransact::PROXY_INTERNAL_CACHE_NOOP;
    return;
  }
  Debug("http_seq", "[HttpTransact::HandleRequest] request valid.");

  if (is_debug_tag_set("http_chdr_describe")) {
    obj_describe(s->hdr_info.client_request.m_http, 1);
  }
  // at this point we are guaranteed that the request is good and acceptable.
  // initialize some state variables from the request (client version,
  // client keep-alive, cache action, etc.
  initialize_state_variables_from_request(s, &s->hdr_info.client_request);

  // Cache lookup or not will be decided later at DecideCacheLookup().
  // Before it's decided to do a cache lookup,
  // assume no cache lookup and using proxy (not tunnelling)
  s->cache_info.action = CACHE_DO_NO_ACTION;
  s->current.mode = GENERIC_PROXY;

  // initialize the cache_control structure read from cache.config
  update_cache_control_information_from_config(s);

  // We still need to decide whether or not to do a cache lookup since 
  // the scheduled update code depends on this info.
  if (is_request_cache_lookupable(s, &s->hdr_info.client_request))
    s->cache_info.action = CACHE_DO_LOOKUP;

  // If the hostname is "$internal$" then this is a request for
  // internal proxy information.
  if (handle_internal_request(s, &s->hdr_info.client_request)) {
    TRANSACT_RETURN(PROXY_INTERNAL_REQUEST, NULL);
  }
  // If this an ftp request, we need to setup ftp state
  if (s->scheme == URL_WKSIDX_FTP && !setup_ftp_request(s)) {
    HTTP_INCREMENT_TRANS_STAT(http_invalid_client_requests_stat);
    return;
  }
  // this needs to be called after initializing state variables from request
  // it tries to handle the problem that MSIE only adds no-cache
  // headers to reload requests when there is an explicit proxy --- the
  // reload button does nothing in the case of transparent proxies.
  handle_msie_reload_badness(s, &s->hdr_info.client_request);

  // this needs to be called after initializing state variables from request
  // it adds the client-ip to the incoming client request.

  if (!s->cop_test_page)
    DUMP_HEADER("http_hdrs", &s->hdr_info.client_request, s->state_machine_id, "Incoming Request");

  if (s->state_machine->plugin_tunnel_type == HTTP_PLUGIN_AS_INTERCEPT) {
    setup_plugin_request_intercept(s);
    return;
  }
  // grab the username from the authorization header, if configured
  if (s->http_config_param->snarf_username_from_authorization) {
    //snarf_username_from_authorization_hdr(s);
  }
  // if the request is a trace or options request, decrement the
  // max-forwards value. if the incoming max-forwards value was 0,
  // then we have to return a response to the client with the
  // appropriate action for trace/option. in this case this routine
  // is responsible for building the response.
  if (handle_trace_and_options_requests(s, &s->hdr_info.client_request)) {
    TRANSACT_RETURN(PROXY_INTERNAL_CACHE_NOOP, NULL);
  }
  // for HTTPS requests, we must go directly to the
  // origin server. Ignore the no_dns_just_forward_to_parent setting.

  if (s->http_config_param->no_dns_forward_to_parent &&
      s->scheme != URL_WKSIDX_HTTPS && strcmp(s->server_info.name, "127.0.0.1") != 0) {
    // we need to see if the hostname is an
    //   ip address since the parent selection code result
    //   could change as a result of this ip address
    inku32 addr = ink_inet_addr(s->server_info.name);
    if ((ink32) addr != -1) {
      s->request_data.dest_ip = addr;
    }

    if (s->parent_params->parentExists(&s->request_data)) {
      // If the proxy is behind and firewall and there is no
      //  DNS service available, we just want to forward the request
      //  the parent proxy.  In this case, we never find out the
      //  origin server's ip.  So just skip past OSDNS
      s->server_info.ip = 0;
      StartAccessControl(s);
      return;
    } else if (s->http_config_param->no_origin_server_dns) {
      build_error_response(s,
                           HTTP_STATUS_BAD_GATEWAY,
                           "Next Hop Connection Failed", "connect#failed_connect", "Next Hop Connection Failed");

      TRANSACT_RETURN(HttpTransact::PROXY_SEND_ERROR_CACHE_NOOP, NULL);
    }
  }
  // Added to skip the dns if the document is in the cache.
  // DNS is requested before cache lookup only if there are rules in cache.config , parent.config or 
  // if the newly added varible doc_in_cache_skip_dns is not enabled              
  if (s->dns_info.lookup_name[0] <= '9' &&
      s->dns_info.lookup_name[0] >= '0' &&
     // (s->state_machine->authAdapter.needs_rev_dns() ||
      ( host_rule_in_CacheControlTable() || s->parent_params->ParentTable->hostMatch)) {
    s->force_dns = 1;
  }
  //YTS Team, yamsat Plugin
  //Doing a Cache Lookup in case of a Redirection to ensure that 
  //the newly requested object is not in the CACHE
  if (s->http_config_param->cache_http && s->redirect_info.redirect_in_process && s->state_machine->enable_redirection) {
    TRANSACT_RETURN(CACHE_LOOKUP, NULL);
  }


  if (s->force_dns) {
    TRANSACT_RETURN(DNS_LOOKUP, OSDNSLookup);   // After handling the request, DNS is done.
  } else {
    // After the requested is properly handled No need of requesting the DNS directly check the ACLs 
    // if the request is Authorised
    StartAccessControl(s);
  }
}

void
HttpTransact::setup_plugin_request_intercept(State * s)
{

  ink_debug_assert(s->state_machine->plugin_tunnel != NULL);

  // Plugin is incerpting the request which means
  //  that we don't do dns, cache read or cache write
  //
  // We just want to write the request straight to the plugin
  if (s->cache_info.action != HttpTransact::CACHE_DO_NO_ACTION) {
    s->cache_info.action = HttpTransact::CACHE_DO_NO_ACTION;
    s->current.mode = TUNNELLING_PROXY;
    HTTP_INCREMENT_TRANS_STAT(http_tunnels_stat);
  }
  // Regardless of the protocol we're gatewaying to
  //   we see the scheme as http
  s->scheme = s->next_hop_scheme = URL_WKSIDX_HTTP;

  // Set up a "fake" server server entry
  update_current_info(&s->current, &s->server_info, HttpTransact::ORIGIN_SERVER, 0);

  // Also "fake" the info we'd normally get from
  //   hostDB
  s->server_info.http_version.set(1, 0);
  s->server_info.keep_alive = HTTP_NO_KEEPALIVE;
  s->host_db_info.app.http_data.http_version = HostDBApplicationInfo::HTTP_VERSION_10;
  s->host_db_info.app.http_data.pipeline_max = 1;

  // Build the request to the server
  build_request(s, &s->hdr_info.client_request, &s->hdr_info.server_request, s->client_info.http_version);

  // We don't do keep alive over these impersonated
  //  NetVCs so nuke the connection header
  s->hdr_info.server_request.field_delete(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION);

  TRANSACT_RETURN(ORIGIN_SERVER_OPEN, NULL);
}

////////////////////////////////////////////////////////////////////////
// void HttpTransact::HandleApiErrorJump(State* s)
//
//   Called after an API function indicates it wished to send an
//     error to the user agent
////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleApiErrorJump(State * s)
{

  Debug("http_trans", "[HttpTransact::HandleApiErrorJump]");

  // since the READ_REQUEST_HDR_HOOK is processed before
  //   we examine the request, returning INK_EVENT_ERROR will cause
  //   the protocol in the via string to be "?"  Set it here
  //   since we know it has to be http
  // For CONNECT method, next_hop_scheme is NULL
  if (s->next_hop_scheme < 0) {
    s->next_hop_scheme = URL_WKSIDX_HTTP;
  }
  // The client response may not be empty in the
  // case the txn was reenabled in error by a plugin from hook SEND_REPONSE_HDR.
  // build_response doesn't clean the header. So clean it up before.
  // Do fields_clear() instead of clear() to prevent memory leak
  // and set the source to internal so chunking is handled correctly
  if (s->hdr_info.client_response.valid()) {
    s->hdr_info.client_response.fields_clear();
    s->source = SOURCE_INTERNAL;
  }

  build_response(s, &s->hdr_info.client_response,
                 s->client_info.http_version, HTTP_STATUS_INTERNAL_SERVER_ERROR, "INKApi Error");

  TRANSACT_RETURN(PROXY_INTERNAL_CACHE_NOOP, NULL);
  return;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : PPDNSLookup
// Description: called after DNS lookup of parent proxy name
//
// Details    :
//   
// the configuration information gave us the name of the parent proxy
// to send the request to. this function is called after the dns lookup
// for that name. it may fail, in which case we look for the next parent
// proxy to try and if none exist, then go to the origin server.
// if the lookup succeeds, we open a connection to the parent proxy.
//
//
// Possible Next States From Here:

// - HttpTransact::DNS_LOOKUP;
// - HttpTransact::ORIGIN_SERVER_RAW_OPEN;
// - HttpTransact::FTP_SERVER_OPEN;
// - HttpTransact::ORIGIN_SERVER_OPEN;
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::PPDNSLookup(State * s)
{
  ++s->dns_info.attempts;

  Debug("http_trans", "[HttpTransact::PPDNSLookup] This was attempt %d", s->dns_info.attempts);

  HTTP_DEBUG_ASSERT(s->dns_info.looking_up == PARENT_PROXY);
  if (!s->dns_info.lookup_success) {
    // DNS lookup of parent failed, find next parent or o.s.
    find_server_and_update_current_info(s);
    if (s->current.server->ip == 0) {
      if (s->current.request_to == PARENT_PROXY) {
        TRANSACT_RETURN(DNS_LOOKUP, PPDNSLookup);
      } else {
        // We could be out of parents here if all the parents
        // failed DNS lookup
        HTTP_DEBUG_ASSERT(s->current.request_to == HOST_NONE);
        handle_parent_died(s);
      }
      return;
    }
  } else {
    // lookup succeeded, open connection to p.p.
    s->parent_info.ip = s->host_db_info.ip();
    get_ka_info_from_host_db(s, &s->parent_info, &s->client_info, &s->host_db_info, s->http_config_param);
    s->parent_info.dns_round_robin = s->dns_info.round_robin;

    Debug("http_trans", "[PPDNSLookup] DNS lookup for sm_id[%d] successful "
          "IP: %u.%u.%u.%u", s->state_machine->sm_id,
          ((unsigned char *) &s->parent_info.ip)[0],
          ((unsigned char *) &s->parent_info.ip)[1],
          ((unsigned char *) &s->parent_info.ip)[2], ((unsigned char *) &s->parent_info.ip)[3]);
  }

  // Since this function can be called serveral times while retrying
  //  parents, check to see if we've already built our request
  if (!s->hdr_info.server_request.valid()) {

    build_request(s, &s->hdr_info.client_request, &s->hdr_info.server_request, s->current.server->http_version);

    // Take care of defered (issue revalidate) work in building
    //   the request
    if (s->pending_work != NULL) {
      HTTP_DEBUG_ASSERT(s->pending_work == issue_revalidate);
      (*s->pending_work) (s);
      s->pending_work = NULL;
    }
  }
  // what kind of a connection (raw, simple)
  s->next_action = how_to_open_connection(s);
}

///////////////////////////////////////////////////////////////////////////////
//
// Name       : ReDNSRoundRobin
// Description: Called after we fail to contact part of a round-robin
//              robin server set and we found a another ip address.
//
// Details    :
//
//  
//  
// Possible Next States From Here:
// - HttpTransact::ORIGIN_SERVER_RAW_OPEN;
// - HttpTransact::FTP_SERVER_OPEN;
// - HttpTransact::ORIGIN_SERVER_OPEN;
// - HttpTransact::PROXY_INTERNAL_CACHE_NOOP;
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::ReDNSRoundRobin(State * s)
{

  HTTP_DEBUG_ASSERT(s->current.server == &s->server_info);
  HTTP_DEBUG_ASSERT(s->current.server->connect_failure);

  if (s->dns_info.lookup_success) {

    // We using a new server now so clear the connection 
    //  failure mark
    s->current.server->connect_failure = 0;

    // Our ReDNS of the server succeeeded so update the necessary
    //  information and try again
    s->server_info.ip = s->host_db_info.ip();
    s->request_data.dest_ip = s->server_info.ip;
    get_ka_info_from_host_db(s, &s->server_info, &s->client_info, &s->host_db_info, s->http_config_param);
    s->server_info.dns_round_robin = s->dns_info.round_robin;

    Debug("http_trans", "[ReDNSRoundRobin] DNS lookup for O.S. successful "
          "IP: %u.%u.%u.%u",
          ((unsigned char *) &s->server_info.ip)[0],
          ((unsigned char *) &s->server_info.ip)[1],
          ((unsigned char *) &s->server_info.ip)[2], ((unsigned char *) &s->server_info.ip)[3]);

    s->next_action = how_to_open_connection(s);
  } else {

    // Our ReDNS failed so output the DNS failure error message
    build_error_response(s, HTTP_STATUS_BAD_GATEWAY, "Cannot find server.", "connect#dns_failed",
                         // The following is all one long string
                         //("Unable to locate the server named \"<em>%s</em>\" --- "
                         //"the server does not have a DNS entry.  Perhaps there is "
                         //"a misspelling in the server name, or the server no "
                         //"longer exists.  Double-check the name and try again."),
                         ("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\"><html><head>"
                          "<style>a:link {font:8pt/11pt verdana; color:red}a:visited {font:8pt/11pt verdana; color:#4e4e4e}"
                          "</style><meta HTTP-EQUIV=\"Content-Type\" Content=\"text-html; charset=Windows-1252\"><title>"
                          "Cannot find server</title></head><body bgcolor=\"white\"><table width=\"400\" cellpadding=\"3\" "
                          "cellspacing=\"5\"><tr><td id=\"tableProps2\" align=\"left\" valign=\"middle\" width=\"360\">"
                          "<h1 id=\"textSection1\"style=\"COLOR: black; FONT: 13pt/15pt verdana\"><span id=\"errorText\">"
                          "The page cannot be displayed</span></h1></td></tr><tr><td id=\"tablePropsWidth\" width=\"400\""
                          " colspan=\"2\"><font style=\"COLOR: black; FONT: 8pt/11pt verdana\">The page you are looking "
                          "for is currently unavailable. The Web site might be experiencing technical difficulties, "
                          "or you may need to adjust your browser settings.</font></td></tr><tr><td id=\"tablePropsWidth\""
                          " width=\"400\" colspan=\"2\"><font id=\"LID1\"style=\"COLOR: black; FONT: 8pt/11pt verdana\">"
                          "<hr color=\"#C0C0C0\" noshade><p id=\"LID2\">Please try the following:</p><ul>"
                          "<li id=\"instructionsText1\">Click the Refresh button, or try again later.</li><li id="
                          "\"instructionsText2\"> If you typed the page address in the Address bar, make sure that it is "
                          "spelled correctly. <br></li><li id=\"instructionsText3\">To check your connection settings, click the"
                          "<b>Tools</b> "
                          "menu, and then click <b>Internet Options</b>. On the <b>Connections</b> tab, click <b>Settings</b>."
                          " The settings should match those provided by your local area network (LAN) administrator or "
                          "Internet service provider (ISP).</li><li id=\"instructionsText5\">Some sites require 128-bit "
                          "connection security. Click the <b>Help</b> menu and then click <b> About Internet Explorer </b>"
                          " to determine what strength security you have installed.</li><li id=\"instructionsText4\">"
                          "If you are trying to reach a secure site, make sure your Security settings can support it. "
                          "Click the <B>Tools</b> menu, and then click <b>Internet Options</b>.  On the Advanced tab, "
                          "scroll to the Security section and check settings for SSL 2.0, SSL 3.0, TLS 1.0, PCT 1.0.</li>"
                          "<li id=\"list3\">Click the Back button to try another link.</li></ul><p><br></p><h2 id=\"PEText\""
                          " style=\"font:8pt/11pt verdana; color:black\">502 - Cannot find server or DNS Error</h2></font>"
                          "</td></tr> </table></body></html>"), s->server_info.name);
    s->cache_info.action = CACHE_DO_NO_ACTION;
    s->next_action = PROXY_SEND_ERROR_CACHE_NOOP;
    //  s->next_action = PROXY_INTERNAL_CACHE_NOOP;
  }

  return;
}


///////////////////////////////////////////////////////////////////////////////
// Name       : OSDNSLookup
// Description: called after the DNS lookup of origin server name
//
// Details    :
//   
// normally called after Start. may be called more than once, however,
// if the dns lookup fails. this may be if the client does not specify
// the full hostname (e.g. just cnn, instead of www.cnn.com), or because
// it was not possible to resolve the name after several attempts.
//
// the next action depends. since this function is normally called after
// a request has come in, which is valid and does not require an immediate
// response, the next action may just be to open a connection to the
// origin server, or a parent proxy, or the next action may be to do a
// cache lookup, or in the event of an error, the next action may be to
// send a response back to the client.
//
//
// Possible Next States From Here:
// - HttpTransact::PROXY_INTERNAL_CACHE_NOOP;
// - HttpTransact::CACHE_LOOKUP;
// - HttpTransact::DNS_LOOKUP;
// - HttpTransact::ORIGIN_SERVER_RAW_OPEN;
// - HttpTransact::FTP_SERVER_OPEN;
// - HttpTransact::ORIGIN_SERVER_OPEN;
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::OSDNSLookup(State * s)
{
  static int max_dns_lookups = 3 + s->http_config_param->num_url_expansions;
  ++s->dns_info.attempts;
  int return_action = 0;        // Return action - YTS Team, yamsat

  Debug("http_trans", "[HttpTransact::OSDNSLookup] This was attempt %d", s->dns_info.attempts);

  HTTP_DEBUG_ASSERT(s->dns_info.looking_up == ORIGIN_SERVER);

  // detect whether we are about to self loop. the client may have
  // specified the proxy as the origin server (badness).

  // Check if this procedure is already done - YTS Team, yamsat
  if (!s->request_will_not_selfloop) {
    if (will_this_request_self_loop(s)) {
      Debug("http_trans", "[OSDNSLookup] request will selfloop - bailing out");
      SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
      TRANSACT_RETURN(PROXY_SEND_ERROR_CACHE_NOOP, NULL);
    }
  }

  if (!s->dns_info.lookup_success) {
    // maybe the name can be expanded (e.g cnn -> www.cnn.com)
    HostNameExpansionError_t host_name_expansion = try_to_expand_host_name(s);

    switch (host_name_expansion) {
    case RETRY_EXPANDED_NAME:
      // expansion successful, do a dns lookup on expanded name
      HTTP_RELEASE_ASSERT(s->dns_info.attempts < max_dns_lookups);
      HTTP_RELEASE_ASSERT(s->http_config_param->enable_url_expandomatic);
      TRANSACT_RETURN(DNS_LOOKUP, OSDNSLookup);
      break;
    case EXPANSION_NOT_ALLOWED:
    case EXPANSION_FAILED:
    case DNS_ATTEMPTS_EXHAUSTED:
      if (host_name_expansion == EXPANSION_NOT_ALLOWED) {
        // config file doesn't allow automatic expansion of host names
        HTTP_RELEASE_ASSERT(!(s->http_config_param->enable_url_expandomatic));
        Debug("http_seq", "[HttpTransact::OSDNSLookup] DNS Lookup unsuccessful");
      } else if (host_name_expansion == EXPANSION_FAILED) {
        // not able to expand the hostname. dns lookup failed
        Debug("http_seq", "[HttpTransact::OSDNSLookup] DNS Lookup unsuccessful");
      } else if (host_name_expansion == DNS_ATTEMPTS_EXHAUSTED) {
        // retry attempts exhausted --- can't find dns entry for this host name
        HTTP_RELEASE_ASSERT(s->dns_info.attempts >= max_dns_lookups);
        Debug("http_seq", "[HttpTransact::OSDNSLookup] DNS Lookup unsuccessful");
      }
      // output the DNS failure error message
      SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
      build_error_response(s, HTTP_STATUS_BAD_GATEWAY, "Cannot find server.", "connect#dns_failed",
                           // The following is all one long string
                           //("Unable to locate the server named \"<em>%s</em>\" --- "
                           //"the server does not have a DNS entry.  Perhaps there is "
                           //"a misspelling in the server name, or the server no "
                           //"longer exists.  Double-check the name and try again."),
                           ("<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 3.2 Final//EN\"><html><head>"
                            "<style>a:link {font:8pt/11pt verdana; color:red}a:visited {font:8pt/11pt verdana; color:#4e4e4e}"
                            "</style><meta HTTP-EQUIV=\"Content-Type\" Content=\"text-html; charset=Windows-1252\"><title>"
                            "Cannot find server</title></head><body bgcolor=\"white\"><table width=\"400\" cellpadding=\"3\" "
                            "cellspacing=\"5\"><tr><td id=\"tableProps2\" align=\"left\" valign=\"middle\" width=\"360\">"
                            "<h1 id=\"textSection1\"style=\"COLOR: black; FONT: 13pt/15pt verdana\"><span id=\"errorText\">"
                            "The page cannot be displayed</span></h1></td></tr><tr><td id=\"tablePropsWidth\" width=\"400\""
                            " colspan=\"2\"><font style=\"COLOR: black; FONT: 8pt/11pt verdana\">The page you are looking "
                            "for is currently unavailable. The Web site might be experiencing technical difficulties, "
                            "or you may need to adjust your browser settings.</font></td></tr><tr><td id=\"tablePropsWidth\""
                            " width=\"400\" colspan=\"2\"><font id=\"LID1\"style=\"COLOR: black; FONT: 8pt/11pt verdana\">"
                            "<hr color=\"#C0C0C0\" noshade><p id=\"LID2\">Please try the following:</p><ul>"
                            "<li id=\"instructionsText1\">Click the Refresh button, or try again later.</li><li id="
                            "\"instructionsText2\"> If you typed the page address in the Address bar, make sure that it is "
                            "spelled correctly. <br></li><li id=\"instructionsText3\">To check your connection settings, click the"
                            "<b>Tools</b> "
                            "menu, and then click <b>Internet Options</b>. On the <b>Connections</b> tab, click <b>Settings</b>."
                            " The settings should match those provided by your local area network (LAN) administrator or "
                            "Internet service provider (ISP).</li><li id=\"instructionsText5\">Some sites require 128-bit "
                            "connection security. Click the <b>Help</b> menu and then click <b> About Internet Explorer </b>"
                            " to determine what strength security you have installed.</li><li id=\"instructionsText4\">"
                            "If you are trying to reach a secure site, make sure your Security settings can support it. "
                            "Click the <B>Tools</b> menu, and then click <b>Internet Options</b>.  On the Advanced tab, "
                            "scroll to the Security section and check settings for SSL 2.0, SSL 3.0, TLS 1.0, PCT 1.0.</li>"
                            "<li id=\"list3\">Click the Back button to try another link.</li></ul><p><br></p><h2 id=\"PEText\""
                            " style=\"font:8pt/11pt verdana; color:black\">502 - Cannot find server or DNS Error</h2></font>"
                            "</td></tr> </table></body></html>"), s->server_info.name);
      // s->cache_info.action = CACHE_DO_NO_ACTION;
      TRANSACT_RETURN(PROXY_SEND_ERROR_CACHE_NOOP, NULL);
      break;
    default:
      HTTP_DEBUG_ASSERT(!("try_to_expand_hostname returned an unsupported code"));
      break;
    }
    return;
  }
  // ok, so the dns lookup succeeded
  HTTP_DEBUG_ASSERT(s->dns_info.lookup_success);
  Debug("http_seq", "[HttpTransact::OSDNSLookup] DNS Lookup successful");

  // Check to see if can fullfill expect requests based on the cached
  // update some state variables with hostdb information that has
  // been provided.
  s->server_info.ip = s->host_db_info.ip();
  s->request_data.dest_ip = s->server_info.ip;
  get_ka_info_from_host_db(s, &s->server_info, &s->client_info, &s->host_db_info, s->http_config_param);
  s->server_info.dns_round_robin = s->dns_info.round_robin;
  Debug("http_trans", "[OSDNSLookup] DNS lookup for O.S. successful "
        "IP: %u.%u.%u.%u",
        ((unsigned char *) &s->server_info.ip)[0],
        ((unsigned char *) &s->server_info.ip)[1],
        ((unsigned char *) &s->server_info.ip)[2], ((unsigned char *) &s->server_info.ip)[3]);


  //Added By YTS Team, yamsat

  //As the object is a miss in Cache and DNS lookup done by this time
  //lets check if there is someother client requesting for the same url
  //by looking-up the HashTable.
  //The return action would further be either scheduling for connection
  //collapsing or doing a cache lookup again.
  return_action = ConnectionCollapsing(s);

  if (return_action == CONNECTION_COLLAPSING_SCHEDULED) {
    // Changing the state from DNS_LOOKUP to STATE_MACHINE_ACTION_UNDEFINED
    //As the handler has been scheduled, the event system would call back 
    //after rww_wait_time. So lets return to Zero state from here so that 
    //transaction starts afresh when piggybacked_handler is scheduled
    //YTS Team, yamsat
    TRANSACT_RETURN(STATE_MACHINE_ACTION_UNDEFINED, NULL);
  }

  else if (return_action == CACHE_RELOOKUP) {
    //This case arises if a client could not insert an entry in Hash Table
    //as someother url has inserted it.Therefore there is a possibility 
    //for this client to get the object from Cache instead of going to 
    //origin server connection -- YTS Team, yamsat
    TRANSACT_RETURN(CACHE_LOOKUP, NULL);
  }
  // so the dns lookup was a success, but the lookup succeeded on
  // a hostname which was expanded by the traffic server. we should
  // not automatically forward the request to this expanded hostname.
  // return a response to the client with the expanded host name
  // and a tasty little blurb explaining what happened.

  // if a DNS lookup succeeded on a user-defined 
  // hostname expansion, forward the request to the expanded hostname.
  // On the other hand, if the lookup succeeded on a www.<hostname>.com
  // expansion, return a 302 response.
  if (s->dns_info.attempts == max_dns_lookups && s->dns_info.looking_up == ORIGIN_SERVER) {

    if (diags->on()) {
      DebugOn("http_trans", "[OSDNSLookup] DNS name resolution on expansion");
      DebugOn("http_seq", "[OSDNSLookup] DNS name resolution on expansion - returning");
    }
    build_redirect_response(s);
    // s->cache_info.action = CACHE_DO_NO_ACTION;
    TRANSACT_RETURN(PROXY_INTERNAL_CACHE_NOOP, NULL);
  }
  // everything succeeded with the DNS lookup so do an API callout
  //   that allows for filtering.  We'll do traffic_server interal
  //   filtering after API filitering


  // After DNS_LOOKUP, goto the saved action/state ORIGIN_SERVER_(RAW_)OPEN.
  // Should we skip the StartAccessControl()? why?

  if (s->cdn_remap_complete) {
    Debug("cdn", "This is a late DNS lookup.  We are going to the OS, " "not to HandleFiltering.");
    ink_assert(s->cdn_saved_next_action == ORIGIN_SERVER_OPEN || s->cdn_saved_next_action == ORIGIN_SERVER_RAW_OPEN);
    Debug("cdn", "outgoing version -- (pre  conversion) %d", s->hdr_info.server_request.m_http->m_version);
    (&s->hdr_info.server_request)->version_set(HTTPVersion(1, 1));
    HttpTransactHeaders::convert_request(s->current.server->http_version, &s->hdr_info.server_request);
    Debug("cdn", "outgoing version -- (post conversion) %d", s->hdr_info.server_request.m_http->m_version);
    TRANSACT_RETURN(s->cdn_saved_next_action, NULL);
  } else if (s->dns_info.lookup_name[0] <= '9' &&
             s->dns_info.lookup_name[0] >= '0' &&
             //(s->state_machine->authAdapter.needs_rev_dns() ||
             ( host_rule_in_CacheControlTable() || s->parent_params->ParentTable->hostMatch)) {
    // note, broken logic: ACC fudges the OR stmt to always be true, 
    // 'AuthHttpAdapter' should do the rev-dns if needed, not here .
    TRANSACT_RETURN(REVERSE_DNS_LOOKUP, HttpTransact::StartAccessControl);
  } else {
    //(s->state_machine->authAdapter).StartLookup (s);
    // TRANSACT_RETURN(AUTH_LOOKUP, NULL);

    if (s->force_dns) {
      StartAccessControl(s);    // If skip_dns is enabled and no ip based rules in cache.config and parent.config
      // Access Control is called after DNS response
    } else {
      if ((s->cache_info.action == CACHE_DO_NO_ACTION) &&
          (s->range_setup == RANGE_NOT_SATISFIABLE || s->range_setup == RANGE_NOT_HANDLED)) {
        TRANSACT_RETURN(HttpTransact::HTTP_API_OS_DNS, HandleCacheOpenReadMiss);
      } else if (s->cache_lookup_result == HttpTransact::CACHE_LOOKUP_SKIPPED) {
        TRANSACT_RETURN(HttpTransact::HTTP_API_OS_DNS, LookupSkipOpenServer);
        // DNS Lookup is done after LOOKUP Skipped  and after we get response 
        // from the DNS we need to call LookupSkipOpenServer
      } else if (s->cache_lookup_result == CACHE_LOOKUP_HIT_FRESH ||
                 s->cache_lookup_result == CACHE_LOOKUP_HIT_WARNING ||
                 s->cache_lookup_result == CACHE_LOOKUP_HIT_STALE) {
        //DNS lookup is done if the content is state need to call handle cache open read hit
        TRANSACT_RETURN(HttpTransact::HTTP_API_OS_DNS, HandleCacheOpenReadHit);
      } else if (s->cache_lookup_result == CACHE_LOOKUP_MISS || s->cache_info.action == CACHE_DO_NO_ACTION) {
        TRANSACT_RETURN(HttpTransact::HTTP_API_OS_DNS, HandleCacheOpenReadMiss);
        //DNS lookup is done if the lookup failed and need to call Handle Cache Open Read Miss
      } else {
        build_error_response(s, HTTP_STATUS_INTERNAL_SERVER_ERROR, "Invalid Cache Lookup result", "default", "");
        Log::error("HTTP: Invalid CACHE LOOKUP RESULT : %d", s->cache_lookup_result);
        TRANSACT_RETURN(PROXY_SEND_ERROR_CACHE_NOOP, NULL);
      }
    }
  }
}

void
HttpTransact::StartAccessControl(State * s)
{
  //if (s->cop_test_page || s->traffic_net_req || (s->state_machine->authAdapter.disabled() == true)) {
    // Heartbeats should always be allowed.
    // s->content_control.access = ACCESS_ALLOW;   
    HandleRequestAuthorized(s);
  //  return;
 // }
  // ua_session is NULL for scheduled updates.
  // Don't use req_flavor to do the test because if updated
  // urls are remapped, the req_flavor is changed to REV_PROXY.
  //if (s->state_machine->ua_session == NULL) {
    // Scheduled updates should always be allowed 
   // return;
  //}
  // pass the access control logic to the ACC module.
  //(s->state_machine->authAdapter).StartLookup(s);
}

void
HttpTransact::HandleRequestAuthorized(State * s)
{
  //(s->state_machine->authAdapter).SetState(s);
  //(s->state_machine->authAdapter).UserAuthorized(NULL);
  //TRANSACT_RETURN(HTTP_API_OS_DNS, HandleFiltering);
   if (s->force_dns) {

    TRANSACT_RETURN(HttpTransact::HTTP_API_OS_DNS, HttpTransact::DecideCacheLookup);
  } else {
    HttpTransact::DecideCacheLookup(s);
  }

}

void
HttpTransact::HandleFiltering(State * s)
{
  ink_release_assert(!"Fix-Me AUTH MERGE");

  if (s->method == HTTP_WKSIDX_PUSH && s->http_config_param->push_method_enabled == 0) {
    // config file says this request is not authorized.
    // send back error response to client.
    if (diags->on()) {
      DebugOn("http_trans", "[HandleFiltering] access denied.");
      DebugOn("http_seq", "[HttpTransact::HandleFiltering] Access Denied.");
    }

    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
    // adding a comment so that cvs recognizes that I added a space in the text below
    build_error_response(s,
                         HTTP_STATUS_FORBIDDEN,
                         "Access Denied",
                         "access#denied", "You are not allowed to access the document at the URL location");
    // s->cache_info.action = CACHE_DO_NO_ACTION;
    TRANSACT_RETURN(PROXY_SEND_ERROR_CACHE_NOOP, NULL);
  }

  Debug("http_seq", "[HttpTransact::HandleFiltering] Request Authorized.");
  //////////////////////////////////////////////////////////////
  // ok, the config file says that the request is authorized. //
  //////////////////////////////////////////////////////////////

  // the config file may have specified that some headers have to
  // be removed (for example, the client-ip). we put the
  // code here for now and removed the headers from the incoming
  // request.

  // TODO do not remove the headers from the incoming request, but only from the outgoing requests

  // there is a function
  // in HttpTransactHeaders.cc (remove_privacy_headers_from_request)
  // which should actually have this code. this function is called
  // from build_request.
  //
  // strip out the headers content control says we need to remove
#if 0
  HeaderFilter *aclHdrs = s->content_control.hdrs;
  while (aclHdrs != NULL) {
    if (aclHdrs->hdr_action == ACCESS_HDR_STRIP) {
      s->hdr_info.client_request.field_delete(aclHdrs->header_name, strlen(aclHdrs->header_name));
    }
    aclHdrs = aclHdrs->next;
  }
#endif

  // request is not black listed so now decided if we ought to
  //  lookup the cache
  DecideCacheLookup(s);
}

void
HttpTransact::DecideCacheLookup(State * s)
{
  // Check if a client request is lookupable.
  if (s->redirect_info.redirect_in_process) {
    // for redirect, we want to skip cache lookup and write into 
    // the cache directly with the URL before the redirect
    s->cache_info.action = CACHE_DO_NO_ACTION;
    s->current.mode = GENERIC_PROXY;
  } else if (!s->state_machine->piggybacking_scheduled) {       //Check if this process is done - YTS Team, yamsat
    if (is_request_cache_lookupable(s, &s->hdr_info.client_request)) {
      s->cache_info.action = CACHE_DO_LOOKUP;
      s->current.mode = GENERIC_PROXY;
    } else {
      s->cache_info.action = CACHE_DO_NO_ACTION;
      s->current.mode = TUNNELLING_PROXY;
      HTTP_INCREMENT_TRANS_STAT(http_tunnels_stat);
    }
  } else {
    s->cache_info.action = CACHE_DO_LOOKUP;
    s->current.mode = GENERIC_PROXY;
    s->state_machine->piggybacking_scheduled = false;
  }

  if (service_transaction_in_proxy_only_mode(s)) {
    s->cache_info.action = CACHE_DO_NO_ACTION;
    s->current.mode = TUNNELLING_PROXY;
    HTTP_INCREMENT_TRANS_STAT(http_throttled_proxy_only_stat);
  }
  // at this point the request is ready to continue down the
  // traffic server path.

  // now decide whether the cache can even be looked up.
  if (s->cache_info.action == CACHE_DO_LOOKUP) {
    if (diags->on()) {
      DebugOn("http_trans", "[DecideCacheLookup] Will do cache lookup.");
      DebugOn("http_seq", "[DecideCacheLookup] Will do cache lookup");
    }
    HTTP_DEBUG_ASSERT(s->current.mode != TUNNELLING_PROXY);

    if (s->cache_info.lookup_url == NULL) {

      if (s->pristine_host_hdr > 0 || s->http_config_param->maintain_pristine_host_hdr || s->scheme == URL_WKSIDX_FTP) {
        s->cache_info.lookup_url_storage.create(NULL);
        s->cache_info.lookup_url_storage.copy(s->hdr_info.client_request.url_get());
        s->cache_info.lookup_url = &(s->cache_info.lookup_url_storage);
      } else {
        s->cache_info.lookup_url = s->hdr_info.client_request.url_get();
      }

      // *somebody* wants us to not hack the host header in a reverse proxy setup.
      // In addition, they want us to reverse proxy for 6000 servers, which vary
      // the stupid content on the Host header!!!!
      // We could a) have 6000 alts (barf, puke, vomit) or b) use the original
      // host header in the url before doing all cache actions (lookups, writes, etc.)
      if (s->http_config_param->maintain_pristine_host_hdr || s->pristine_host_hdr > 0) {

        // So, the host header will have the original host header.
        int host_len;
        const char *host_hdr = s->hdr_info.client_request.value_get(MIME_FIELD_HOST,
                                                                    MIME_LEN_HOST, &host_len);
        if (host_hdr) {
          char *tmp;
          int port = 0;
          tmp = (char *) memchr(host_hdr, ':', host_len);
          if (tmp) {
            s->cache_info.lookup_url->host_set(host_hdr, (tmp - host_hdr));

            port = ink_atoi(tmp + 1, host_len - (tmp + 1 - host_hdr));
          } else {
            s->cache_info.lookup_url->host_set(host_hdr, host_len);
          }
          s->cache_info.lookup_url->port_set(port);
        }
      }
      HTTP_DEBUG_ASSERT(s->cache_info.lookup_url->valid() == true);
    }
    // For ftp requests remove user name and
    // password from the url when doing lookup
    int tmp;
    if (s->scheme == URL_WKSIDX_FTP) {
      if (s->cache_info.lookup_url->user_get(&tmp))
        s->cache_info.lookup_url->user_set("", 0);
      if (s->cache_info.lookup_url->password_get(&tmp))
        s->cache_info.lookup_url->password_set("", 0);
    }

    TRANSACT_RETURN(CACHE_LOOKUP, NULL);
  } else {

    ink_assert(s->cache_info.action != CACHE_DO_LOOKUP && s->cache_info.action != CACHE_DO_SERVE);

    if (diags->on()) {
      DebugOn("http_trans", "[DecideCacheLookup] Will NOT do cache lookup.");
      DebugOn("http_seq", "[DecideCacheLookup] Will NOT do cache lookup");
    }
    // If this is a push request, we need send an error because
    //   since what ever was sent is not cachable
    if (s->method == HTTP_WKSIDX_PUSH) {
      HandlePushError(s, "Request Not Cachable");
      return;
    }
    // for redirect, we skipped cache lookup to do the automatic redirection
    if (s->redirect_info.redirect_in_process) {
      // without calling out the CACHE_LOOKUP_COMPLETE_HOOK
      s->cache_info.action = CACHE_DO_WRITE;
      LookupSkipOpenServer(s);
    } else {
      // calling out CACHE_LOOKUP_COMPLETE_HOOK even when the cache 
      // lookup is skipped
      s->cache_lookup_result = HttpTransact::CACHE_LOOKUP_SKIPPED;
      if (s->force_dns) {
        TRANSACT_RETURN(HTTP_API_CACHE_LOOKUP_COMPLETE, LookupSkipOpenServer);
      } else {
        // Returning to dns lookup as cache lookup is skipped
        TRANSACT_RETURN(HTTP_API_CACHE_LOOKUP_COMPLETE, CallOSDNSLookup);
      }
    }
  }

  return;
}

void
HttpTransact::LookupSkipOpenServer(State * s)
{
  // cache will not be looked up. open a connection
  // to a parent proxy or to the origin server.
  find_server_and_update_current_info(s);

  if (s->current.request_to == PARENT_PROXY) {
    TRANSACT_RETURN(DNS_LOOKUP, PPDNSLookup);
  }

  ink_assert(s->current.request_to == ORIGIN_SERVER);
  // ink_assert(s->current.server->ip != 0);

  build_request(s, &s->hdr_info.client_request, &s->hdr_info.server_request, s->current.server->http_version);

  StateMachineAction_t next = how_to_open_connection(s);
  s->next_action = next;
  if (next == ORIGIN_SERVER_OPEN || next == ORIGIN_SERVER_RAW_OPEN) {
    TRANSACT_RETURN(next, HttpTransact::HandleResponse);
  }
}


//////////////////////////////////////////////////////////////////////////////
// Name       : HandleCacheOpenReadPush
// Description: 
//
// Details    :
//
// Called on PUSH requests from HandleCacheOpenRead
//////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleCacheOpenReadPush(State * s, bool read_successful)
{

  if (read_successful) {
    s->cache_info.action = CACHE_PREPARE_TO_UPDATE;
  } else {
    s->cache_info.action = CACHE_PREPARE_TO_WRITE;
  }

  TRANSACT_RETURN(READ_PUSH_HDR, HandlePushResponseHdr);
}

//////////////////////////////////////////////////////////////////////////////
// Name       : HandlePushResponseHdr
// Description: 
//
// Details    :
//
// Called after reading the response header on PUSH request
//////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandlePushResponseHdr(State * s)
{

  // Verify the pushed header wasn't longer than the content length
  int body_bytes = s->hdr_info.request_content_length - s->state_machine->pushed_response_hdr_bytes;
  if (body_bytes < 0) {
    HandlePushError(s, "Bad Content Length");
    return;
  }
  // We need to create the request header storing in the cache
  s->hdr_info.server_request.create(HTTP_TYPE_REQUEST);
  s->hdr_info.server_request.copy(&s->hdr_info.client_request);
  s->hdr_info.server_request.method_set(HTTP_METHOD_GET, HTTP_LEN_GET);
  s->hdr_info.server_request.value_set("X-Inktomi-Source", 16, "http PUSH", 9);

  if (!s->cop_test_page)
    DUMP_HEADER("http_hdrs", &s->hdr_info.server_response, s->state_machine_id, "Pushed Response Header");

  if (!s->cop_test_page)
    DUMP_HEADER("http_hdrs", &s->hdr_info.server_request, s->state_machine_id, "Generated Request Header");

  s->response_received_time = s->request_sent_time = ink_cluster_time();

  if (is_response_cacheable(s, &s->hdr_info.server_request, &(s->hdr_info.server_response))) {
    ink_assert(s->cache_info.action == CACHE_PREPARE_TO_WRITE || s->cache_info.action == CACHE_PREPARE_TO_UPDATE);

    TRANSACT_RETURN(CACHE_ISSUE_WRITE, HandlePushCacheWrite);
  } else {
    HandlePushError(s, "Response Not Cachable");
  }
}

//////////////////////////////////////////////////////////////////////////////
// Name       : HandlePushCacheWrite
// Description: 
//
// Details    :
//
// Called after performing the cache write on a push request
//////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandlePushCacheWrite(State * s)
{

  switch (s->cache_info.write_lock_state) {
  case CACHE_WL_SUCCESS:
    // We were able to get the lock for the URL vector in the cache
    if (s->cache_info.action == CACHE_PREPARE_TO_WRITE) {
      s->cache_info.action = CACHE_DO_WRITE;
    } else if (s->cache_info.action == CACHE_PREPARE_TO_UPDATE) {
      s->cache_info.action = CACHE_DO_REPLACE;
    } else {
      ink_release_assert(0);
    }
    set_headers_for_cache_write(s, &s->cache_info.object_store,
                                &s->hdr_info.server_request, &s->hdr_info.server_response);

    TRANSACT_RETURN(STORE_PUSH_BODY, NULL);
    break;

  case CACHE_WL_FAIL:
  case CACHE_WL_READ_RETRY:
    // No write lock, can not complete request so bail
    HandlePushError(s, "Cache Write Failed");
    break;
  case CACHE_WL_INIT:
  default:
    ink_release_assert(0);
  }
}


void
HttpTransact::HandlePushTunnelSuccess(State * s)
{

  ink_assert(s->cache_info.action == CACHE_DO_WRITE || s->cache_info.action == CACHE_DO_REPLACE);

  // FIX ME: check PUSH spec for status codes
  HTTPStatus resp_status = (s->cache_info.action == CACHE_DO_WRITE) ? HTTP_STATUS_CREATED : HTTP_STATUS_OK;

  build_response(s, &s->hdr_info.client_response, s->client_info.http_version, resp_status);

  TRANSACT_RETURN(PROXY_INTERNAL_CACHE_NOOP, NULL);
}


void
HttpTransact::HandlePushTunnelFailure(State * s)
{
  HandlePushError(s, "Cache Error");
}

void
HttpTransact::HandleBadPushRespHdr(State * s)
{
  HandlePushError(s, "Malformed Pushed Response Header");
}

void
HttpTransact::HandlePushError(State * s, char *reason)
{
  s->client_info.keep_alive = HTTP_NO_KEEPALIVE;

  // Set half close flag to prevent TCP
  //   reset from the body still being transfered
  s->state_machine->set_ua_half_close_flag();

  build_error_response(s, HTTP_STATUS_BAD_REQUEST, reason, "default", NULL);
}


///////////////////////////////////////////////////////////////////////////////
// Name       : HandleCacheOpenRead
// Description: the cache lookup succeeded - may have been a hit or a miss
//
// Details    :
//   
// the cache lookup succeeded. first check if the lookup resulted in
// a hit or a miss, if the lookup was for an http request or an ftp
// request. this function just funnels the result into the appropriate
// functions which handle these different cases.
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleCacheOpenRead(State * s)
{
  Debug("http_trans", "[HttpTransact::HandleCacheOpenRead]");

  SET_VIA_STRING(VIA_DETAIL_CACHE_TYPE, VIA_DETAIL_CACHE);

  bool read_successful = true;

  if (s->cache_info.object_read == 0) {
    read_successful = false;
    //
    // If somebody else was writing the document, proceed just like it was
    // a normal cache miss, except don't try to write to the cache
    //
    if (s->cache_lookup_result == CACHE_LOOKUP_DOC_BUSY) {
      s->cache_lookup_result = CACHE_LOOKUP_MISS;
      s->cache_info.action = CACHE_DO_NO_ACTION;
    }
  } else {
    CacheHTTPInfo *obj = s->cache_info.object_read;
    if (obj->response_get()->type_get() == HTTP_TYPE_UNKNOWN) {
      read_successful = false;
    }
    if (obj->request_get()->type_get() == HTTP_TYPE_UNKNOWN) {
      read_successful = false;
    }
  }

  if (s->method == HTTP_WKSIDX_PUSH) {
    HandleCacheOpenReadPush(s, read_successful);
  } else if (read_successful == false) {
    // cache miss
    Debug("http_trans", "CacheOpenRead -- miss");
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_NOT_CACHED);
    //StartAccessControl(s);
    if (s->force_dns) {
      HandleCacheOpenReadMiss(s);
    } else {
      //Cache Lookup Unsuccesfull ..calling dns lookup
      TRANSACT_RETURN(DNS_LOOKUP, OSDNSLookup);
    }
  }
#ifndef INK_NO_FTP
  // cache hit - http or ftp?
  else if (s->scheme == URL_WKSIDX_FTP) {
    Debug("http_trans", "CacheOpenRead -- ftp hit");
    TRANSACT_RETURN(HTTP_API_READ_CACHE_HDR, HandleCacheOpenReadFtpFreshness);
  }
#endif
  else {
    // cache hit
    Debug("http_trans", "CacheOpenRead -- hit");
    TRANSACT_RETURN(HTTP_API_READ_CACHE_HDR, HandleCacheOpenReadHitFreshness);
  }

  return;
}

#ifndef INK_NO_FTP
void
HttpTransact::HandleCacheOpenReadFtpFreshness(State * s)
{
  if (diags->on()) {
    DebugOn("http_trans", "[HttpTransact::HandleCacheOpenReadFtpFreshness]");
    DebugOn("http_seq", "[HttpTransact::HandleCacheOpenReadFtpFreshness] Ftp hit in cache");
  }
  HTTP_DEBUG_ASSERT(s->cache_info.object_read != 0);

  if (delete_all_document_alternates_and_return(s, TRUE)) {
    Debug("http_trans", "[HandleCacheOpenReadFtpFreshness] Delete and return");
    s->cache_info.action = CACHE_DO_DELETE;
    s->next_action = HttpTransact::PROXY_INTERNAL_CACHE_DELETE;
    return;
  }
  // If the request has a user name which
  // is not anonymous treat this as a MISS.
  if (s->ftp_info->username && s->ftp_info->username[0] &&
      strcmp(s->ftp_info->username, "anonymous") != 0 && strcmp(s->ftp_info->username, "ftp") != 0) {
    s->cache_lookup_result = HttpTransact::CACHE_LOOKUP_HIT_FTP_NON_ANONYMOUS;
    Debug("http_seq", "[HttpTransact::HandleCacheOpenReadFtpFreshness] Not anon user");
  } else {
    // calculate the freshness of the document
    s->request_sent_time = s->cache_info.object_read->request_sent_time_get();
    s->response_received_time = s->cache_info.object_read->response_received_time_get();

    HTTP_DEBUG_ASSERT(s->request_sent_time <= s->response_received_time);

    if (diags->on()) {
      DebugOn("http_trans", "[HandleCacheOpenReadFtpFreshness] request_sent_time : %ld", s->request_sent_time);
      DebugOn("http_trans",
              "[HandleCacheOpenReadFtpFreshness] response_received_time : %ld", s->response_received_time);
    }
    // if the plugin hasn't decided about the freshness
    if (s->cache_lookup_result == HttpTransact::CACHE_LOOKUP_NONE) {
      HTTPHdr *resp = s->cache_info.object_read->response_get();
      int current_age = HttpTransactHeaders::calculate_document_age(s->request_sent_time,
                                                                    s->response_received_time,
                                                                    resp,
                                                                    resp->get_date(),
                                                                    s->current.now);

      // is the document fresh enough to be served to client?
      if (current_age < s->http_config_param->cache_ftp_document_lifetime) {
        s->cache_lookup_result = HttpTransact::CACHE_LOOKUP_HIT_FRESH;
      } else {
        Debug("http_seq", "[HttpTransact::HandleCacheOpenReadFtpFreshness] Not fresh enough");
        s->cache_lookup_result = HttpTransact::CACHE_LOOKUP_HIT_STALE;
      }
    }
  }

  TRANSACT_RETURN(HTTP_API_CACHE_LOOKUP_COMPLETE, HandleCacheOpenReadFtp);
}


//////////////////////////////////////////////////////////////////////////////
// Name       : HandleCacheOpenReadFtp
// Description: 
//
// Details    :
//   
//  1. If the request has a user name and the user name is not "anonymous"
//     then the document is not cached (only "anonymous" documents are
//     cached.
//  2. If the request does not have a user name or the user name is
//     "anonymous" check if the document is still fresh.
//
//
// Possible Next States From Here:
// - HttpTransact::PROXY_INTERNAL_CACHE_DELETE;
// - HttpTransact::DNS_LOOKUP;
// - HttpTransact::PROXY_INTERNAL_CACHE_NOOP;
// - HttpTransact::FTP_SERVER_OPEN;
// - HttpTransact::SERVE_FROM_CACHE;
//
//////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleCacheOpenReadFtp(State * s)
{
  ink_assert(s->cache_lookup_result ==
             HttpTransact::CACHE_LOOKUP_HIT_FTP_NON_ANONYMOUS ||
             s->cache_lookup_result == HttpTransact::CACHE_LOOKUP_HIT_STALE
             || s->cache_lookup_result == HttpTransact::CACHE_LOOKUP_HIT_FRESH);

  // cached response may not be returnable, such as if the method is PUT
  bool response_returnable = is_cache_response_returnable(s);

  // If the request has a user name which
  // is not anonymous treat this as a MISS.
  if (s->cache_lookup_result == CACHE_LOOKUP_HIT_FTP_NON_ANONYMOUS) {
    response_returnable = false;
    Debug("http_seq", "[HttpTransact::HandleCacheOpenReadFtp] Not anon user");
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_METHOD);
  }
  // If the response is returnable, determine if it is fresh
  bool response_fresh = s->cache_lookup_result == HttpTransact::CACHE_LOOKUP_HIT_FRESH ? true : false;
  if (response_returnable) {
    // set the via string to indicate expiration miss, update stat
    if (s->cache_lookup_result == HttpTransact::CACHE_LOOKUP_HIT_STALE) {
      SET_VIA_STRING(VIA_CACHE_RESULT, VIA_IN_CACHE_STALE);
      SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_EXPIRED);
    }
  }

  bool serve_from_cache = response_returnable && response_fresh;

  if (serve_from_cache) {
    // the incoming request has an anonymous user name and
    // the document is fresh enough in the cache.

    Debug("http_seq", "[HttpTransact::HandleCacheOpenReadFtp] Fresh in cache");
    if (s->cache_info.is_ram_cache_hit) {
      SET_VIA_STRING(VIA_CACHE_RESULT, VIA_IN_RAM_CACHE_FRESH);
    } else {
      SET_VIA_STRING(VIA_CACHE_RESULT, VIA_IN_CACHE_FRESH);
    }
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_HIT_SERVED);
    if (s->method == HTTP_WKSIDX_HEAD) {
      s->cache_info.action = CACHE_DO_NO_ACTION;
      s->next_action = PROXY_INTERNAL_CACHE_NOOP;
    } else {
      s->cache_info.action = CACHE_DO_SERVE;
      s->next_action = SERVE_FROM_CACHE;
    }
    if (s->next_action == SERVE_FROM_CACHE && s->state_machine->do_transform_open()) {
      set_header_for_transform(s, s->cache_info.object_read->response_get());
    } else {
      build_response(s, s->cache_info.object_read->response_get(),
                     &s->hdr_info.client_response, s->client_info.http_version);
    }
  } else {

    // incoming request did not use an anonymouse user name
    // or the document has expired in the cache. revalidate the
    //  document
    find_server_and_update_current_info(s);
    if (s->current.server->ip == 0) {

      HTTP_ASSERT(s->current.request_to == PARENT_PROXY ||
                  s->http_config_param->no_dns_forward_to_parent != 0);

      // Set ourselves up to handle pending revalidate issues
      //  after the PP DNS lookup
      HTTP_DEBUG_ASSERT(s->pending_work == NULL);
      s->pending_work = issue_revalidate;

      // We must be going a PARENT PROXY since so did
      //  origin server DNS lookup right after state Start
      //
      // If we end up here in the release case just fall
      //  through.  The request will fail because of the
      //  missing ip but we won't take down the system
      //
      if (s->current.request_to == PARENT_PROXY) {
        TRANSACT_RETURN(DNS_LOOKUP, PPDNSLookup);
      } else {
        handle_parent_died(s);
        return;
      }
    }
    build_request(s, &s->hdr_info.client_request, &s->hdr_info.server_request, s->current.server->http_version);

    issue_revalidate(s);

    // this can not be anything but a simple origin server connection.
    // in other words, we would not have looked up the cache for a
    // connect request, so the next action can not be origin_server_raw_open.
    s->next_action = how_to_open_connection(s);
  }

  return;
}
#endif //INK_NO_FTP

///////////////////////////////////////////////////////////////////////////////
// Name       : issue_revalidate
// Description:   Sets cache action and does various bookeeping
//
// Details    :
//
// The Cache Lookup was hit but the document was stale so after
//   calling build_request, we need setup up the cache action,
//   set the via code, and possibly conditionalize the request
// The paths that we take to get this code are:
//   Dircectly from HandleOpenReadHit if we are going to the origin server
//   After PPDNS if we are going to a parent proxy
//
//
// Possible Next States From Here:
// - 
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::issue_revalidate(State * s)
{
  HTTPHdr *c_resp = find_appropriate_cached_resp(s);
  SET_VIA_STRING(VIA_CACHE_RESULT, VIA_IN_CACHE_STALE);
  HTTP_DEBUG_ASSERT(GET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP) != ' ');

  if (s->www_auth_content == CACHE_AUTH_FRESH) {
    s->hdr_info.server_request.method_set(HTTP_METHOD_HEAD, HTTP_LEN_HEAD);
    // The document is fresh in cache and we just want to see if the 
    // the client has the right credentials 
    // this cache action is just to get us into the hcoofsr function
    s->cache_info.action = CACHE_DO_UPDATE;
    DUMP_HEADER("http_hdrs", &(s->hdr_info.server_request), s->state_machine_id, "Proxy's Request (Conditionalized)");
    return;
  }

  if (s->cache_info.write_lock_state == CACHE_WL_INIT) {
    // We do a cache lookup for DELETE, PUT and POST requests as well.
    // We must, however, delete the cached copy after forwarding the
    // request to the server. is_cache_response_returnable will ensure
    // that we forward the request. We now specify what the cache
    // action should be when the response is received.
    if (does_method_require_cache_copy_deletion(s->method)) {
      s->cache_info.action = CACHE_PREPARE_TO_DELETE;
      Debug("http_seq", "[HttpTransact::issue_revalidate] cache action: DELETE");
    } else {
      s->cache_info.action = CACHE_PREPARE_TO_UPDATE;
      Debug("http_seq", "[HttpTransact::issue_revalidate] cache action: UPDATE");
    }
  } else {
    // We've looped back around due to missing the write lock 
    //  for the cache.  At this point we want to forget about the cache
    ink_assert(s->cache_info.write_lock_state == CACHE_WL_READ_RETRY);
    s->cache_info.action = CACHE_DO_NO_ACTION;
    return;
  }

  // if the document is cached, just send a conditional request to the server

  // So the request does not have preconditions. It can, however
  // be a simple GET request with a Pragma:no-cache. As on 8/28/98
  // we have fixed the whole Reload/Shift-Reload cached copy
  // corruption problem. This means that we can issue a conditional
  // request to the server only if the incoming request has a conditional
  // or the incoming request does NOT have a no-cache header.
  // In other words, if the incoming request is not conditional
  // but has a no-cache header we can not issue an IMS. check for
  // that case here.
  bool no_cache_in_request = false;

  if (s->hdr_info.client_request.is_pragma_no_cache_set() ||
      s->hdr_info.client_request.is_cache_control_set(HTTP_VALUE_NO_CACHE)) {
    Debug("http_trans", "[issue_revalidate] no-cache header directive in request, folks");
    no_cache_in_request = true;
  }

  if ((!
       (s->hdr_info.client_request.
        presence(MIME_PRESENCE_IF_MODIFIED_SINCE))) &&
      (!(s->hdr_info.client_request.presence(MIME_PRESENCE_IF_NONE_MATCH)))
      && (no_cache_in_request == true) &&
      (!s->http_config_param->cache_ims_on_client_no_cache) && (s->www_auth_content == CACHE_AUTH_NONE)) {
    Debug("http_trans",
          "[issue_revalidate] Can not make this a conditional"
          "request. This is the force update of the cached copy case");
    // set cache action to update. response will be a 200 or error,
    // causing cached copy to be replaced (if 200).
    s->cache_info.action = CACHE_PREPARE_TO_UPDATE;
    return;
  }
  // do not conditionalize if the cached response is not a 200
  switch (c_resp->status_get()) {
  case HTTP_STATUS_OK:         // 200
    // don't conditionalize if we are configured to repeat the clients
    //   conditionals
    if (s->http_config_param->cache_when_to_revalidate == 4)
      break;
    // ok, request is either a conditional or does not have a no-cache.
    //   (or is method that we don't conditionalize but lookup the
    //    cache on like DELETE)
    if (c_resp->get_last_modified() > 0 &&
        s->hdr_info.server_request.method_get_wksidx() == HTTP_WKSIDX_GET && s->range_setup == RANGE_NONE) {
      // make this a conditional request
      int length;
      const char *str = c_resp->value_get(MIME_FIELD_LAST_MODIFIED, MIME_LEN_LAST_MODIFIED,
                                          &length);
      s->hdr_info.server_request.value_set(MIME_FIELD_IF_MODIFIED_SINCE, MIME_LEN_IF_MODIFIED_SINCE, str, length);
      if (!s->cop_test_page)
        DUMP_HEADER("http_hdrs", &(s->hdr_info.server_request),
                    s->state_machine_id, "Proxy's Request (Conditionalized)");
    }
    // if Etag exists, also add if-non-match header
    if (c_resp->presence(MIME_PRESENCE_ETAG) && s->hdr_info.server_request.method_get_wksidx() == HTTP_WKSIDX_GET) {
      int length;
      const char *etag = c_resp->value_get(MIME_FIELD_ETAG, MIME_LEN_ETAG, &length);
      if ((length >= 2) && (etag[0] == 'W') && (etag[1] == '/')) {
        etag += 2;
        length -= 2;
      }
      s->hdr_info.server_request.value_set(MIME_FIELD_IF_NONE_MATCH, MIME_LEN_IF_NONE_MATCH, etag, length);
      if (!s->cop_test_page)
        DUMP_HEADER("http_hdrs", &(s->hdr_info.server_request),
                    s->state_machine_id, "Proxy's Request (Conditionalized)");
    }
    break;
  case HTTP_STATUS_NON_AUTHORITATIVE_INFORMATION:      // 203
    /* fall through */
  case HTTP_STATUS_MULTIPLE_CHOICES:   // 300
    /* fall through */
  case HTTP_STATUS_MOVED_PERMANENTLY:  // 301
    /* fall through */
  case HTTP_STATUS_GONE:       // 410
    /* fall through */
  default:
    Debug("http_trans", "[issue_revalidate] cached response is" "not a 200 response so no conditionalization.");
    s->cache_info.action = CACHE_PREPARE_TO_UPDATE;
    break;
  case HTTP_STATUS_PARTIAL_CONTENT:
    ink_assert(!"unexpected status code");
    break;
  }

}


void
HttpTransact::HandleCacheOpenReadHitFreshness(State * s)
{
  CacheHTTPInfo *&obj = s->cache_info.object_read;

  HTTP_ASSERT((s->request_sent_time == UNDEFINED_TIME) && (s->response_received_time == UNDEFINED_TIME));
  Debug("http_seq", "[HttpTransact::HandleCacheOpenReadHitFreshness] Hit in cache");

  if (delete_all_document_alternates_and_return(s, TRUE)) {
    Debug("http_trans", "[HandleCacheOpenReadHitFreshness] Delete and return");
    s->cache_info.action = CACHE_DO_DELETE;
    s->next_action = HttpTransact::PROXY_INTERNAL_CACHE_DELETE;
    return;
  }

  s->request_sent_time = obj->request_sent_time_get();
  s->response_received_time = obj->response_received_time_get();

  // There may be clock skew if one of the machines
  // went down and we do not have the correct delta
  // for it. this is just to deal with the effects
  // of the skew by setting minimum and maximum times
  // so that ages are not negative, etc.
  s->request_sent_time = min(s->client_request_time, s->request_sent_time);
  s->response_received_time = min(s->client_request_time, s->response_received_time);

  HTTP_DEBUG_ASSERT(s->request_sent_time <= s->response_received_time);

  if (diags->on()) {
    DebugOn("http_trans", "[HandleCacheOpenReadHitFreshness] request_sent_time      : %ld", s->request_sent_time);
    DebugOn("http_trans", "[HandleCacheOpenReadHitFreshness] response_received_time : %ld", s->response_received_time);
  }
  // if the plugin has already decided the freshness, we don't need to 
  // do it again
  if (s->cache_lookup_result == HttpTransact::CACHE_LOOKUP_NONE) {
    // is the document still fresh enough to be served back to
    // the client without revalidation?
    Freshness_t freshness = what_is_document_freshness(s,
                                                       s->http_config_param,
                                                       &s->hdr_info.client_request,
                                                       obj->request_get(),
                                                       obj->response_get());
    switch (freshness) {
    case FRESHNESS_FRESH:
      Debug("http_seq", "[HttpTransact::HandleCacheOpenReadHitFreshness] " "Fresh copy");
      s->cache_lookup_result = HttpTransact::CACHE_LOOKUP_HIT_FRESH;
      break;
    case FRESHNESS_WARNING:
      Debug("http_seq", "[HttpTransact::HandleCacheOpenReadHitFreshness] " "Heuristic-based Fresh copy");
      s->cache_lookup_result = HttpTransact::CACHE_LOOKUP_HIT_WARNING;
      break;
    case FRESHNESS_STALE:
      Debug("http_seq", "[HttpTransact::HandleCacheOpenReadHitFreshness] " "Stale in cache");
      s->cache_lookup_result = HttpTransact::CACHE_LOOKUP_HIT_STALE;
      s->is_revalidation_necessary = true;      // to identify a revalidation occurrence
      break;
    default:
      HTTP_DEBUG_ASSERT(!("what_is_document_freshness has returned unsupported code."));
      break;
    }
  }

  ink_assert(s->cache_lookup_result != HttpTransact::CACHE_LOOKUP_MISS);
  if (s->cache_lookup_result == HttpTransact::CACHE_LOOKUP_HIT_STALE)
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_EXPIRED);

  if (!s->force_dns) {          // If DNS is not performed before 
    if (need_to_revalidate(s)) {
      TRANSACT_RETURN(HTTP_API_CACHE_LOOKUP_COMPLETE, CallOSDNSLookup); // content needs to be revalidated and we did not perform a dns ....calling DNS lookup
    } else {                    // document can be served can cache
      TRANSACT_RETURN(HTTP_API_CACHE_LOOKUP_COMPLETE, HttpTransact::HandleCacheOpenReadHit);
    }
  } else {                      // we have done dns . Its up to HandleCacheOpenReadHit to decide to go OS or serve from cache
    TRANSACT_RETURN(HTTP_API_CACHE_LOOKUP_COMPLETE, HttpTransact::HandleCacheOpenReadHit);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Name       : CallOSDNSLookup
// Description: Moves in DNS_LOOKUP state and sets the transact return to OSDNSLookup
//
// Details    :
/////////////////////////////////////////////////////////////////////////////
void
HttpTransact::CallOSDNSLookup(State * s)
{
//printf("into HttpTransact::CallOSDNSLookup **\n");
  TRANSACT_RETURN(DNS_LOOKUP, OSDNSLookup);
}

///////////////////////////////////////////////////////////////////////////////
// Name       : need_to_revalidate
// Description: Checks if a document which is in the cache needs to be revalidates
//
// Details    : Function calls AuthenticationNeeded and is_cache_response_returnable to determine 
//              if the cached document can be served 
/////////////////////////////////////////////////////////////////////////////   
bool
HttpTransact::need_to_revalidate(State * s)
{
  bool needs_revalidate, needs_authenticate = false;
  bool needs_cache_auth = false;
  CacheHTTPInfo *obj;

  if (s->api_update_cached_object == HttpTransact::UPDATE_CACHED_OBJECT_CONTINUE) {
    obj = &(s->cache_info.object_store);
    ink_assert(obj->valid());
    if (!obj->valid())
      return true;
  } else
    obj = s->cache_info.object_read;

  // do we have to authenticate with the server before
  // sending back the cached response to the client?
  Authentication_t
    authentication_needed = AuthenticationNeeded(s->http_config_param,
                                                 &s->hdr_info.client_request, obj->response_get());
  switch (authentication_needed) {
  case AUTHENTICATION_SUCCESS:
    Debug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] " "Authentication not needed");
    needs_authenticate = false;
    break;
  case AUTHENTICATION_MUST_REVALIDATE:
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_METHOD);
    Debug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] " "Authentication needed");
    needs_authenticate = true;
    break;
  case AUTHENTICATION_MUST_PROXY:
    Debug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] " "Authentication needed");
    needs_authenticate = true;
    break;
  case AUTHENTICATION_CACHE_AUTH:
    Debug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] " "Authentication needed for cache_auth_content");
    needs_authenticate = false;
    needs_cache_auth = true;
    break;
  default:
    HTTP_DEBUG_ASSERT(!("AuthenticationNeeded has returned unsupported code."));
    return true;
    break;
  }

  ink_assert(s->cache_lookup_result == CACHE_LOOKUP_HIT_FRESH ||
             s->cache_lookup_result == CACHE_LOOKUP_HIT_WARNING || s->cache_lookup_result == CACHE_LOOKUP_HIT_STALE);
  if (s->cache_lookup_result == CACHE_LOOKUP_HIT_STALE &&
      s->api_update_cached_object != HttpTransact::UPDATE_CACHED_OBJECT_CONTINUE) {
    needs_revalidate = true;
  } else
    needs_revalidate = false;


  bool
    send_revalidate = ((needs_authenticate == true) ||
                       (needs_revalidate == true) || (is_cache_response_returnable(s) == false));
  if (needs_cache_auth == true) {
    s->www_auth_content = send_revalidate ? CACHE_AUTH_STALE : CACHE_AUTH_FRESH;
    send_revalidate = true;
  }
  return send_revalidate;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : HandleCacheOpenReadHit
// Description: handle result of a cache hit
//
// Details    :
//   
// Cache lookup succeeded and resulted in a cache hit. This means
// that the Accept* and Etags fields also matched. The cache lookup
// may have resulted in a vector of alternates (since lookup may
// be based on a url). A different function (SelectFromAlternates)
// goes through the alternates and finds the best match. That is
// then returned to this function. The result may not be sent back
// to the client, still, if the document is not fresh enough, or 
// does not have enough authorization, or if the client wants a 
// reload, etc. that decision will be made in this routine.
//
//
// Possible Next States From Here:
// - HttpTransact::PROXY_INTERNAL_CACHE_DELETE;
// - HttpTransact::DNS_LOOKUP;
// - HttpTransact::ORIGIN_SERVER_OPEN;
// - HttpTransact::PROXY_INTERNAL_CACHE_NOOP;
// - HttpTransact::SERVE_FROM_CACHE;
// - result of how_to_open_connection()
//
// 
// For Range requests, we will decide to do simple tunnelling if one of the 
// following conditions hold:
// - document stale
// - cached response doesn't have Accept-Ranges and Content-Length
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleCacheOpenReadHit(State * s)
{
  bool needs_revalidate, needs_authenticate = false;
  bool needs_cache_auth = false;
  bool server_up = true;
  CacheHTTPInfo *obj;
  int return_action = 0;        // Return action for Connection Collapsing - YTS Team, yamsat

  if (s->api_update_cached_object == HttpTransact::UPDATE_CACHED_OBJECT_CONTINUE) {
    obj = &(s->cache_info.object_store);
    ink_assert(obj->valid());
  } else
    obj = s->cache_info.object_read;

  // do we have to authenticate with the server before
  // sending back the cached response to the client?
  Authentication_t authentication_needed = AuthenticationNeeded(s->http_config_param,
                                                                &s->hdr_info.client_request,
                                                                obj->response_get());
  switch (authentication_needed) {
  case AUTHENTICATION_SUCCESS:
    Debug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] " "Authentication not needed");
    needs_authenticate = false;
    break;
  case AUTHENTICATION_MUST_REVALIDATE:
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_METHOD);
    Debug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] " "Authentication needed");
    needs_authenticate = true;
    break;
  case AUTHENTICATION_MUST_PROXY:
    Debug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] " "Authentication needed");
    HandleCacheOpenReadMiss(s);
    return;
  case AUTHENTICATION_CACHE_AUTH:
    Debug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] " "Authentication needed for cache_auth_content");
    needs_authenticate = false;
    needs_cache_auth = true;
    break;
  default:
    HTTP_DEBUG_ASSERT(!("AuthenticationNeeded has returned unsupported code."));
    break;
  }

  ink_assert(s->cache_lookup_result == CACHE_LOOKUP_HIT_FRESH ||
             s->cache_lookup_result == CACHE_LOOKUP_HIT_WARNING || s->cache_lookup_result == CACHE_LOOKUP_HIT_STALE);
  if (s->cache_lookup_result == CACHE_LOOKUP_HIT_STALE &&
      s->api_update_cached_object != HttpTransact::UPDATE_CACHED_OBJECT_CONTINUE) {
    needs_revalidate = true;
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_EXPIRED);
  } else
    needs_revalidate = false;

  // the response may not be directly returnable to the client. there
  // are several reasons for this: config may force revalidation or
  // client may have forced a refresh by sending a Pragma:no-cache 
  // or a Cache-Control:no-cache, or the client may have sent a 
  // non-GET/HEAD request for a document that is cached. an example 
  // of a situation for this is when a client sends a DELETE, PUT
  // or POST request for a url that is cached. except for DELETE,
  // we may actually want to update the cached copy with the contents
  // of the PUT/POST, but the easiest, safest and most robust solution
  // is to simply delete the cached copy (in order to maintain cache
  // consistency). this is particularly true if the server does not
  // accept or conditionally accepts the PUT/POST requests.
  // anyhow, this is an overloaded function and will return false
  // if the origin server still has to be looked up.
  bool response_returnable = is_cache_response_returnable(s);

  // do we need to revalidate. in other words if the response
  // has to be authorized, is stale or can not be returned, do
  // a revalidate.
  bool send_revalidate = ((needs_authenticate == true) || (needs_revalidate == true) || (response_returnable == false));

  if (needs_cache_auth == true) {
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_EXPIRED);
    s->www_auth_content = send_revalidate ? CACHE_AUTH_STALE : CACHE_AUTH_FRESH;
    send_revalidate = true;
  }
  if (send_revalidate && s->hdr_info.client_request.presence(MIME_PRESENCE_RANGE)) {
    s->range_setup = RANGE_REVALIDATE;
  }

  if (diags->on()) {
    DebugOn("http_trans", "CacheOpenRead --- needs_auth          = %d", needs_authenticate);
    DebugOn("http_trans", "CacheOpenRead --- needs_revalidate    = %d", needs_revalidate);
    DebugOn("http_trans", "CacheOpenRead --- response_returnable = %d", response_returnable);
    DebugOn("http_trans", "CacheOpenRead --- needs_cache_auth    = %d", needs_cache_auth);
    DebugOn("http_trans", "CacheOpenRead --- send_revalidate    = %d", send_revalidate);
  }
  //Added by YTS Team, yamsat

  //At this juncture, the object needs to be revalidated. But check if some other client
  //is already in the process of revalidation. Looking-up hashtable will enable to find 
  //if someone's there.The foll cases arise
  // 1) if URL inserted and revalidation_window_period not expired, then this client will
  //    be served stale object.
  // 2) if URL not found, then insert the URL and update the window_period.
  //Both the above actions are done in ConnectionCollapsing_for_revalidation()
  //Depending on the return type, set send_revalidate to false, or retain it to true.


  //Check if hash table is enabled in config file
  if (HttpConfig::m_master.hashtable_enabled && (s->state_machine->is_cache_enabled) && (s->is_revalidation_necessary)) {

    return_action = ConnectionCollapsing_for_revalidation(s);

    if (return_action == SERVE_STALE_OBJECT) {
      //setting send_revalidate to false so as to serve stale object in cache
      send_revalidate = false;
    }
  }                             // YTS Team, yamsat

  if (send_revalidate) {
    Debug("http_trans", "CacheOpenRead --- HIT-STALE");
    s->dns_info.attempts = 0;

    Debug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] " "Revalidate document with server");

#ifndef INK_NO_ICP
    if (s->http_config_param->icp_enabled && icp_dynamic_enabled
        && s->http_config_param->stale_icp_enabled
        && needs_authenticate == false && needs_cache_auth == false &&
        !s->hdr_info.client_request.is_pragma_no_cache_set() &&
        !s->hdr_info.client_request.is_cache_control_set(HTTP_VALUE_NO_CACHE)) {
      Debug("http_trans",
            "[HandleCacheOpenReadHit] ICP is configured" " and no no-cache in request; checking ICP for a STALE hit");

      s->stale_icp_lookup = true;

      // we haven't done the ICP lookup yet. The following is to
      // fake an icp_info to cater for build_request's needs
      s->icp_info.http_version.set(1, 0);
      if (!s->http_config_param->keep_alive_enabled || s->http_config_param->origin_server_pipeline == 0) {
        s->icp_info.keep_alive = HTTP_NO_KEEPALIVE;
      } else {
        s->icp_info.keep_alive = HTTP_KEEPALIVE;
      }

      update_current_info(&s->current, &s->icp_info, HttpTransact::ICP_SUGGESTED_HOST, 1);
    }
#endif //INK_NO_ICP

    if (s->stale_icp_lookup == false) {
      find_server_and_update_current_info(s);

      // We do not want to try to revalidate documents if we think
      //  the server is down due to the something report problem
      //
      // Note: we only want to skip origin servers because 1)
      //  parent proxies have their own negative caching
      //  scheme & 2) If we skip down parents, every page
      //  we serve is potentially stale
      //
      if (s->current.request_to == ORIGIN_SERVER &&
          is_server_negative_cached(s) && response_returnable == true &&
          is_stale_cache_response_returnable(s) == true) {
        server_up = false;
        update_current_info(&s->current, NULL, UNDEFINED_LOOKUP, 0);
        Debug("http_trans", "CacheOpenReadHit - server_down, returning stale document");
      }
    }

    if (server_up || s->stale_icp_lookup) {
      if (!s->stale_icp_lookup && s->current.server->ip == 0) {

        HTTP_ASSERT(s->current.request_to == PARENT_PROXY ||
                    s->http_config_param->no_dns_forward_to_parent != 0);

        // Set ourselves up to handle pending revalidate issues
        //  after the PP DNS lookup
        HTTP_DEBUG_ASSERT(s->pending_work == NULL);
        s->pending_work = issue_revalidate;

        // We must be going a PARENT PROXY since so did
        //  origin server DNS lookup right after state Start
        //
        // If we end up here in the release case just fall
        //  through.  The request will fail because of the
        //  missing ip but we won't take down the system
        //
        if (s->current.request_to == PARENT_PROXY) {
          TRANSACT_RETURN(DNS_LOOKUP, PPDNSLookup);
        } else {
          handle_parent_died(s);
          return;
        }
      }
      build_request(s, &s->hdr_info.client_request, &s->hdr_info.server_request, s->current.server->http_version);

      issue_revalidate(s);

      // this can not be anything but a simple origin server connection.
      // in other words, we would not have looked up the cache for a
      // connect request, so the next action can not be origin_server_raw_open.
      s->next_action = how_to_open_connection(s);
      if (s->stale_icp_lookup && s->next_action == ORIGIN_SERVER_OPEN)
        s->next_action = ICP_QUERY;
      HTTP_ASSERT(s->next_action != ORIGIN_SERVER_RAW_OPEN);

      return;
    } else {                    // server is down but stale response is returnable
      SET_VIA_STRING(VIA_DETAIL_CACHE_TYPE, VIA_DETAIL_CACHE);
    }
  }
  // cache hit, document is fresh, does not authorization,
  // is valid, etc. etc. send it back to the client.
  //
  // the important thing to keep in mind is that if we are 
  // here then we found a match in the cache and the document
  // is fresh and we have enough authorization for it to send
  // it back to the client without revalidating first with the
  // origin server. we are, therefore, allowed to behave as the
  // origin server. we can, therefore, make the claim that the
  // document has not been modified since or has not been unmodified
  // since the time requested by the client. this may not be
  // the case in reality, but since the document is fresh in
  // the cache, we can make the claim that this is the truth.
  //
  // so, any decision we make at this point can be made with authority.
  // realistically, if we can not make this claim, then there
  // is no reason to cache anything.
  //
  HTTP_DEBUG_ASSERT((send_revalidate == true && server_up == false) || (send_revalidate == false && server_up == true));

  if (diags->on()) {
    DebugOn("http_trans", "CacheOpenRead --- HIT-FRESH");
    DebugOn("http_seq", "[HttpTransact::HandleCacheOpenReadHit] " "Serve from cache");
  }

  if (s->cache_info.is_ram_cache_hit) {
    SET_VIA_STRING(VIA_CACHE_RESULT, VIA_IN_RAM_CACHE_FRESH);
  } else {
    SET_VIA_STRING(VIA_CACHE_RESULT, VIA_IN_CACHE_FRESH);
  }

#ifdef USE_NCA
  if (s->client_info.port_attribute == SERVER_PORT_NCA) {

    inku64 ctag = extract_ctag_from_response(obj->response_get());
    NcaCacheUp_t *nc = s->nca_info.request_info;

    if (nc->advisory) {
      if (nc->advise_ctag != 0 && nc->advise_ctag == ctag) {

        // Since the ctags match and the object is
        //  still fresh in our cache, the advise wasn't
        //  needed and nca should return the document
        //  it has
        s->nca_info.response_info.advisory = NCA_IO_ADVISE_NONE;
        s->squid_codes.log_code = SQUID_LOG_TCP_HIT;
        TRANSACT_RETURN(NCA_IMMEDIATE, NULL);
      }
    } else if (nc->nocache == 0 && ctag != 0) {
      // No advise, so see if NCA already has the document
      //   for a different alternate but matching the
      //   cached copy's ctag against the set nca has

      for (int i = 0; i < nc->num_ctags; i++) {
        if (nc->ctag_array[i] == ctag) {
          s->nca_info.response_info.iodirect_ctag = ctag;
          s->squid_codes.log_code = SQUID_LOG_TCP_HIT;
          TRANSACT_RETURN(NCA_IMMEDIATE, NULL);
        }
      }
    }
    // No immediate NCA response so we will be
    //  returning the cached cop and thus we
    //  need to set the ctag on the way down
    s->nca_info.response_info.ctag = ctag;
  }
#endif


  if (s->cache_lookup_result == CACHE_LOOKUP_HIT_WARNING) {
    build_response_from_cache(s, HTTP_WARNING_CODE_HERUISTIC_EXPIRATION);
  } else if (s->cache_lookup_result == CACHE_LOOKUP_HIT_STALE) {
    HTTP_DEBUG_ASSERT(server_up == false);
    build_response_from_cache(s, HTTP_WARNING_CODE_REVALIDATION_FAILED);
  } else {
    build_response_from_cache(s, HTTP_WARNING_CODE_NONE);
  }

  if (s->api_update_cached_object == HttpTransact::UPDATE_CACHED_OBJECT_CONTINUE) {
    s->saved_update_next_action = s->next_action;
    s->saved_update_cache_action = s->cache_info.action;
    s->next_action = HttpTransact::PREPARE_CACHE_UPDATE;
  }
}


///////////////////////////////////////////////////////////////////////////////
// Name       : build_response_from_cache()
// Description: build a client response from cached response and client request
//
// Input      : State, warning code to be inserted into the response header
// Output     :
//
// Details    : This function is called if we decided to serve a client request
//              using a cached response.
//              It is called by handle_server_connection_not_open()
//              and HandleCacheOpenReadHit().
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::build_response_from_cache(State * s, HTTPWarningCode warning_code)
{
  HTTPHdr *client_request = &s->hdr_info.client_request;
  HTTPHdr *cached_response = NULL;
  HTTPHdr *to_warn = &s->hdr_info.client_response;
  CacheHTTPInfo *obj;

  if (s->api_update_cached_object == HttpTransact::UPDATE_CACHED_OBJECT_CONTINUE) {
    obj = &(s->cache_info.object_store);
    ink_assert(obj->valid());
  } else
    obj = s->cache_info.object_read;
  cached_response = obj->response_get();

  // If the client request is conditional, and the cached copy meets
  // the conditions, do not need to send back the full document,
  // just a NOT_MODIFIED response.
  // If the request is not conditional,
  // the function match_response_to_request_conditionals() returns
  // the code of the cached response, which means that we should send
  // back the full document.
  HTTPStatus client_response_code = HttpTransactCache::match_response_to_request_conditionals(client_request,
                                                                                              cached_response);

  switch (client_response_code) {
  case HTTP_STATUS_NOT_MODIFIED:
    // A IMS or INM GET client request with conditions being met
    // by the cached response.  Send back a NOT MODIFIED response.
    Debug("http_trans", "[build_response_from_cache] Not modified");
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_HIT_CONDITIONAL);

    build_response(s, cached_response, &s->hdr_info.client_response, s->client_info.http_version, client_response_code);
    s->cache_info.action = CACHE_DO_NO_ACTION;
    s->next_action = PROXY_INTERNAL_CACHE_NOOP;
    break;

  case HTTP_STATUS_PRECONDITION_FAILED:
    // A conditional request with conditions not being met by the cached
    // response.  Send back a PRECONDITION FAILED response.
    Debug("http_trans", "[build_response_from_cache] Precondition Failed");
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_CONDITIONAL);

    build_response(s, &s->hdr_info.client_response, s->client_info.http_version, client_response_code);
    s->cache_info.action = CACHE_DO_NO_ACTION;
    s->next_action = PROXY_INTERNAL_CACHE_NOOP;
    break;

  case HTTP_STATUS_RANGE_NOT_SATISFIABLE:
    // Check if cached response supports Range. If it does, append
    // Range transformation plugin
    // A little misnomer. HTTP_STATUS_RANGE_NOT_SATISFIABLE
    // acutally means If-Range match fails here.
    // fall through
  default:
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_HIT_SERVED);
    if (s->method == HTTP_WKSIDX_GET) {

      // send back the full document to the client.
      Debug("http_trans", "[build_response_from_cache] Match! Serving full document.");
      s->cache_info.action = CACHE_DO_SERVE;

      // Check if cached response supports Range. If it does, append
      // Range transformation plugin
      // only if the cached response is a 200 OK
      if (client_response_code == HTTP_STATUS_OK && client_request->presence(MIME_PRESENCE_RANGE) &&
          (cache_global_hooks == NULL || cache_global_hooks->hooks_set <= 0)) {
        s->state_machine->do_range_setup_if_necessary();
        if (s->range_setup == RANGE_NOT_SATISFIABLE &&
            s->http_config_param->reverse_proxy_enabled) {
          build_response(s, &s->hdr_info.client_response,
                         s->client_info.http_version, HTTP_STATUS_RANGE_NOT_SATISFIABLE);

          s->cache_info.action = CACHE_DO_NO_ACTION;
          s->next_action = PROXY_INTERNAL_CACHE_NOOP;
          break;
        } else if (s->range_setup == RANGE_NOT_SATISFIABLE || s->range_setup == RANGE_NOT_HANDLED) {
          // we switch to tunneing for Range requests either 
          // 1. we need to revalidate or
          // 2. out-of-order Range requests
          Debug("http_seq", "[HttpTransact::HandleCacheOpenReadHit] Out-oforder Range request - tunneling");
          s->cache_info.action = CACHE_DO_NO_ACTION;
          if (s->force_dns) {
            HandleCacheOpenReadMiss(s); // DNS is already completed no need of doing DNS
          } else {
            TRANSACT_RETURN(DNS_LOOKUP, OSDNSLookup);   // DNS not done before need to be done now as we are connecting to OS
          }
          return;
        }
      }

      if (s->state_machine->do_transform_open()) {
        set_header_for_transform(s, cached_response);
        to_warn = &s->hdr_info.transform_response;
      } else {
        build_response(s, cached_response, &s->hdr_info.client_response, s->client_info.http_version);
      }
      s->next_action = SERVE_FROM_CACHE;
    }
    // If the client request is a HEAD, then serve the header from cache.
    else if (s->method == HTTP_WKSIDX_HEAD) {
      Debug("http_trans", "[build_response_from_cache] Match! Serving header only.");

      build_response(s, cached_response, &s->hdr_info.client_response, s->client_info.http_version);
      s->cache_info.action = CACHE_DO_NO_ACTION;
      s->next_action = PROXY_INTERNAL_CACHE_NOOP;
    } else {
      // We handled the request but it's not GET or HEAD (eg. DELETE),
      // and server is not reacheable: 502
      //
      Debug("http_trans", "[build_response_from_cache] No match! Connection failed.");

      build_error_response(s, HTTP_STATUS_BAD_GATEWAY, "Connection Failed",
                           "connect#failed_connect", "Connection Failed");
      s->cache_info.action = CACHE_DO_NO_ACTION;
      s->next_action = PROXY_INTERNAL_CACHE_NOOP;
      warning_code = HTTP_WARNING_CODE_NONE;
    }
    break;
  }

  // After building the client response, add the given warning if provided.
  if (warning_code != HTTP_WARNING_CODE_NONE) {
    delete_warning_value(to_warn, warning_code);
    HttpTransactHeaders::insert_warning_header(s->http_config_param, to_warn, warning_code);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Name       : handle_cache_write_lock
// Description: 
//
// Details    :
//   
//
//
// Possible Next States From Here:
// - result of how_to_open_conenction
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_cache_write_lock(State * s)
{
  bool remove_ims = false;

  ink_assert(s->cache_info.action == CACHE_PREPARE_TO_DELETE ||
             s->cache_info.action == CACHE_PREPARE_TO_UPDATE || s->cache_info.action == CACHE_PREPARE_TO_WRITE);

  switch (s->cache_info.write_lock_state) {
  case CACHE_WL_SUCCESS:
    // We were able to get the lock for the URL vector in the cache
    SET_UNPREPARE_CACHE_ACTION(s->cache_info);
    break;
  case CACHE_WL_FAIL:
    // No write lock, ignore the cache and proxy only;
    // FIX: Should just serve from cache if this is a revalidate
    s->cache_info.action = CACHE_DO_NO_ACTION;
    s->cache_info.write_status = CACHE_WRITE_LOCK_MISS;
    remove_ims = true;
    break;
  case CACHE_WL_READ_RETRY:
    //  Write failed but retried and got a vector to read
    //  We need to clean up our state so that transact does
    //  not assert later on.  Then handle the open read hit
    //  
    s->request_sent_time = UNDEFINED_TIME;
    s->response_received_time = UNDEFINED_TIME;
    s->cache_info.action = CACHE_DO_LOOKUP;
    remove_ims = true;
    SET_VIA_STRING(VIA_DETAIL_CACHE_TYPE, VIA_DETAIL_CACHE);
    break;
  case CACHE_WL_INIT:
  default:
    ink_release_assert(0);
    break;
  }

  // Since we've already built the server request and we can't get the write
  //  lock we need to remove the ims field from the request since we're
  //  ignoring the cache.  If their is a client ims field, copy that since
  //  we're tunneling response anyway
  if (remove_ims) {
    s->hdr_info.server_request.field_delete(MIME_FIELD_IF_MODIFIED_SINCE, MIME_LEN_IF_MODIFIED_SINCE);

    MIMEField *c_ims = s->hdr_info.client_request.field_find(MIME_FIELD_IF_MODIFIED_SINCE,
                                                             MIME_LEN_IF_MODIFIED_SINCE);
    if (c_ims) {
      int len;
      const char *value = c_ims->value_get(&len);
      s->hdr_info.server_request.value_set(MIME_FIELD_IF_MODIFIED_SINCE, MIME_LEN_IF_MODIFIED_SINCE, value, len);
    }
  }

  if (s->cache_info.write_lock_state == CACHE_WL_READ_RETRY) {
    s->hdr_info.server_request.destroy();

    // We need to cleanup SDK handles to cached hdrs 
    //   since those headers are no longer good as we've closed 
    //   the orginal cache read vc and replaced it with a new cache
    //   read vc on the write lock read retry loop
    if (s->cache_req_hdr_heap_handle) {
      s->cache_req_hdr_heap_handle->m_sdk_alloc.free_all();
      s->arena.free(s->cache_req_hdr_heap_handle, sizeof(HdrHeapSDKHandle));
      s->cache_req_hdr_heap_handle = NULL;
    }
    if (s->cache_resp_hdr_heap_handle) {
      s->cache_resp_hdr_heap_handle->m_sdk_alloc.free_all();
      s->arena.free(s->cache_resp_hdr_heap_handle, sizeof(HdrHeapSDKHandle));
      s->cache_resp_hdr_heap_handle = NULL;
    }

    HandleCacheOpenReadHitFreshness(s);
  } else {
    StateMachineAction_t next;
    if (s->stale_icp_lookup == false) {
      next = how_to_open_connection(s);
      if (next == ORIGIN_SERVER_OPEN || next == ORIGIN_SERVER_RAW_OPEN || next == FTP_SERVER_OPEN) {
        s->next_action = next;
        TRANSACT_RETURN(next, NULL);
      } else {
        // hehe!
        s->next_action = next;
        ink_assert(s->next_action == DNS_LOOKUP);
        return;
      }
    } else
      next = HttpTransact::ICP_QUERY;
    TRANSACT_RETURN(next, NULL);
  }
}

///////////////////////////////////////////////////////////////////////////////
// Name       : HandleCacheOpenReadMiss
// Description: cache looked up, miss or hit, but needs authorization
//
// Details    :
//   
//
//
// Possible Next States From Here:
// - HttpTransact::ICP_QUERY;
// - HttpTransact::DNS_LOOKUP;
// - HttpTransact::ORIGIN_SERVER_OPEN;
// - HttpTransact::PROXY_INTERNAL_CACHE_NOOP;
// - HttpTransact::FTP_SERVER_OPEN;
// - result of how_to_open_connection()
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleCacheOpenReadMiss(State * s)
{
  if (diags->on()) {
    DebugOn("http_trans", "[HandleCacheOpenReadMiss] --- MISS");
    DebugOn("http_seq", "[HttpTransact::HandleCacheOpenReadMiss] " "Miss in cache");
  }

  if (delete_all_document_alternates_and_return(s, FALSE)) {
    Debug("http_trans", "[HandleCacheOpenReadMiss] Delete and return");
    s->cache_info.action = CACHE_DO_NO_ACTION;
    s->next_action = PROXY_INTERNAL_CACHE_NOOP;
    return;
  }
  // reinitialize some variables to reflect cache miss state.
  s->cache_info.object_read = NULL;
  s->request_sent_time = UNDEFINED_TIME;
  s->response_received_time = UNDEFINED_TIME;
  SET_VIA_STRING(VIA_CACHE_RESULT, VIA_CACHE_MISS);
  if (GET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP) == ' ') {
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_NOT_CACHED);
  }
  // We do a cache lookup for DELETE and PUT requests as well.
  // We must, however, not cache the responses to these requests.
  if (does_method_require_cache_copy_deletion(s->method)) {
    s->cache_info.action = CACHE_DO_NO_ACTION;
  } else if (s->range_setup == RANGE_NOT_SATISFIABLE || s->range_setup == RANGE_NOT_HANDLED) {
    s->cache_info.action = CACHE_DO_NO_ACTION;
  } else {
    s->cache_info.action = CACHE_PREPARE_TO_WRITE;
  }

  // We should not issue an ICP lookup if the request has a
  // no-cache header. First check if the request has a no
  // cache header. Then, if icp is enabled and the request
  // does not have a no-cache header, issue an icp lookup.

  // does the request have a no-cache?
  bool no_cache_in_request = false;

  if (s->hdr_info.client_request.is_pragma_no_cache_set() ||
      s->hdr_info.client_request.is_cache_control_set(HTTP_VALUE_NO_CACHE)) {
    no_cache_in_request = true;
  }
  // if ICP is enabled and above test indicates that request
  // does not have a no-cache, issue icp query to sibling cache.
  if (s->http_config_param->icp_enabled && icp_dynamic_enabled != 0 && (no_cache_in_request == false)) {
    Debug("http_trans", "[HandleCacheOpenReadMiss] " "ICP is configured and no no-cache in request; checking ICP");
    s->next_action = ICP_QUERY;
    return;
  }
  ///////////////////////////////////////////////////////////////
  // a normal miss would try to fetch the document from the    //
  // origin server, unless the origin server isn't resolvable, //
  // but if "CacheControl: only-if-cached" is set, then we are //
  // supposed to send a 504 (GATEWAY TIMEOUT) response.        //
  ///////////////////////////////////////////////////////////////

  HTTPHdr *h = &s->hdr_info.client_request;

  if (!h->is_cache_control_set(HTTP_VALUE_ONLY_IF_CACHED)) {
    find_server_and_update_current_info(s);
    if (s->current.server->ip == 0) {
      HTTP_ASSERT(s->current.request_to == PARENT_PROXY ||
                  s->http_config_param->no_dns_forward_to_parent != 0);
      if (s->current.request_to == PARENT_PROXY) {
        TRANSACT_RETURN(DNS_LOOKUP, HttpTransact::PPDNSLookup);
      } else {
        handle_parent_died(s);
        return;
      }
    }
    build_request(s, &s->hdr_info.client_request, &s->hdr_info.server_request, s->current.server->http_version);

    s->next_action = how_to_open_connection(s);
  } else {                      // miss, but only-if-cached is set
    build_error_response(s,
                         HTTP_STATUS_GATEWAY_TIMEOUT,
                         "Not Cached",
                         "cache#not_in_cache",
                         "%s",
                         "This document was not available in the cache, " "but the client only accepts cached copies.");
    s->next_action = PROXY_SEND_ERROR_CACHE_NOOP;
  }

  return;
}


#ifndef INK_NO_ICP
///////////////////////////////////////////////////////////////////////////////
// Name       : HandleICPLookup
// Description: 
//
// Details    :
//   
//
//
// Possible Next States From Here:
// - HttpTransact::DNS_LOOKUP;
// - HttpTransact::PROXY_INTERNAL_CACHE_NOOP;
// - result of how_to_open_connection()
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleICPLookup(State * s)
{
  SET_VIA_STRING(VIA_DETAIL_CACHE_TYPE, VIA_DETAIL_ICP);
  if (s->icp_lookup_success == true) {
    HTTP_INCREMENT_TRANS_STAT(http_icp_suggested_lookups_stat);
    Debug("http_trans", "[HandleICPLookup] Success, sending request to icp suggested host.");
    s->icp_info.ip = s->icp_ip_result.sin_addr.s_addr;
    // store the port information in native byte order
    s->icp_info.port = ntohs(s->icp_ip_result.sin_port);

    // TODO in this case we should go to the miss case
    // just a little shy about using goto's, that's all.
    HTTP_ASSERT((s->icp_info.port != s->client_info.port) || (s->icp_info.ip != this_machine()->ip));

    // Since the ICPDNSLookup is not called, these two
    //   values are not initialized.
    // Force them to be initialized
    s->icp_info.http_version.set(1, 0);
    if (!s->http_config_param->keep_alive_enabled || s->http_config_param->origin_server_pipeline == 0) {
      s->icp_info.keep_alive = HTTP_NO_KEEPALIVE;
    } else {
      s->icp_info.keep_alive = HTTP_KEEPALIVE;
    }

    s->icp_info.name = (char *) s->arena.alloc(17);
    unsigned char *p = (unsigned char *) &(s->icp_ip_result.sin_addr.s_addr);
    ink_snprintf(s->icp_info.name, 17, "%d.%d.%d.%d", p[0], p[1], p[2], p[3]);

    update_current_info(&s->current, &s->icp_info, ICP_SUGGESTED_HOST, 1);
    s->next_hop_scheme = URL_WKSIDX_HTTP;
  } else {
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_NOT_CACHED);
    Debug("http_trans", "[HandleICPLookup] Failure, sending request to forward server.");
    s->parent_info.name = NULL;
    s->parent_info.port = 0;

    find_server_and_update_current_info(s);
    if (s->current.server->ip == 0) {
      if (s->current.request_to == PARENT_PROXY) {
        TRANSACT_RETURN(DNS_LOOKUP, PPDNSLookup);
      } else {
        HTTP_ASSERT(0);
      }
      return;
    }
  }
  if (!s->stale_icp_lookup) {
    build_request(s, &s->hdr_info.client_request, &s->hdr_info.server_request, s->current.server->http_version);
  } else {
    ink_assert(s->hdr_info.server_request.valid());
    s->stale_icp_lookup = false;
  }
  s->next_action = how_to_open_connection(s);

  return;
}
#endif //INK_NO_ICP

#ifndef INK_NO_FTP
void
HttpTransact::HandleFtpPutSuccess(State * s)
{
  Debug("http_trans", "[HttpTransact::HandleFtpPutSuccess]");

  build_response(s, &s->hdr_info.client_response, s->client_info.http_version, HTTP_STATUS_OK);

  TRANSACT_RETURN(PROXY_INTERNAL_CACHE_NOOP, NULL);

  return;
}

void
HttpTransact::HandleFtpPutFailure(State * s)
{
  Debug("http_trans", "[HttpTransact::HandleFtpPutFailure]");

  build_response(s, &s->hdr_info.client_response, s->client_info.http_version, HTTP_STATUS_GATEWAY_TIMEOUT);

  TRANSACT_RETURN(PROXY_INTERNAL_CACHE_NOOP, NULL);

  return;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : HandleFtpRevalidate
// Description: 
//
// Details    :
//
//   
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleFtpRevalidate(State * s)
{
  if (diags->on()) {
    DebugOn("http_trans", "[HttpTransact::HandleFtpRevalidate]");
    DebugOn("http_seq", "[HttpTransact::HandleFtpRevalidate] " "Successful revalidation");
  }

  s->response_received_time = ink_cluster_time();
  HTTP_DEBUG_ASSERT(s->response_received_time >= s->request_sent_time);
  s->current.now = s->response_received_time;

  Debug("http_trans", "[HandleFtpRevalidate] response_received_time: %ld", s->response_received_time);

  SET_VIA_STRING(VIA_SERVER_RESULT, VIA_SERVER_NOT_MODIFIED);

  if (s->cache_info.object_read == NULL || s->cache_info.write_lock_state == CACHE_WL_FAIL) {
    Debug("http_ftp_revalidate", "Ftp revalidate succeeded - cannot update cache");
    ink_assert(s->hdr_info.client_request.presence(MIME_PRESENCE_IF_MODIFIED_SINCE));
    s->cache_info.action = CACHE_DO_NO_ACTION;
    build_response(s, &(s->hdr_info.client_response), s->client_info.http_version, HTTP_STATUS_NOT_MODIFIED);
    TRANSACT_RETURN(PROXY_INTERNAL_CACHE_NOOP, NULL);
  }
  // We need update the cached copy.  Since the ftp server doesn't
  //   send us headers, we update with the old cached headers.  The
  //   only thing that changes in the response received and request
  //   sent time
  s->cache_info.action = CACHE_DO_SERVE_AND_UPDATE;
  s->cache_info.object_store.create();
  s->cache_info.object_store.request_set(s->cache_info.object_read->request_get());
  s->cache_info.object_store.response_set(s->cache_info.object_read->response_get());

  if (s->http_config_param->wuts_enabled)
    HttpTransactHeaders::convert_wuts_code_to_normal_reason(s->cache_info.object_store.response_get());

  // Reset Date on Ftp docs which have been revalidated, in order
  // to enforce ftp_document_lifetime config value
  s->cache_info.object_store.response_get()->set_date(s->request_sent_time);

  SET_VIA_STRING(VIA_CACHE_FILL_ACTION, VIA_CACHE_UPDATED);
  HTTP_INCREMENT_TRANS_STAT(http_cache_updates_stat);

  if (s->method == HTTP_WKSIDX_HEAD) {
    s->next_action = PROXY_INTERNAL_CACHE_UPDATE_HEADERS;
  } else {
    s->next_action = SERVE_FROM_CACHE;
  }

  if (s->next_action == SERVE_FROM_CACHE && s->state_machine->do_transform_open()) {
    set_header_for_transform(s, s->cache_info.object_read->response_get());
  } else {
    if (s->hdr_info.client_request.presence(MIME_PRESENCE_IF_MODIFIED_SINCE)
        && s->hdr_info.client_request.get_if_modified_since() == s->ftp_info->last_modified) {
      s->next_action = HttpTransact::PROXY_INTERNAL_CACHE_UPDATE_HEADERS;

      build_response(s, &(s->hdr_info.client_response), s->client_info.http_version, HTTP_STATUS_NOT_MODIFIED);
    } else
      build_response(s, s->cache_info.object_read->response_get(),
                     &s->hdr_info.client_response, s->client_info.http_version);
  }
}

bool
HttpTransact::build_ftp_server_response(State * s)
{
  if (s->method == HTTP_WKSIDX_GET) {
    // Since we don't have a server response, build one here
    s->hdr_info.server_response.create(HTTP_TYPE_RESPONSE);
    build_response(s, &s->hdr_info.server_response, s->client_info.http_version, HTTP_STATUS_OK);
    if (s->ftp_info->last_modified > 0) {
      s->hdr_info.server_response.set_last_modified(s->ftp_info->last_modified);
    }
    // add mime type and content length.
    if (!s->ftp_info->is_directory) {
      HTTP_ASSERT(s->ftp_info->mime_type != 0);
      s->hdr_info.server_response.value_set(MIME_FIELD_CONTENT_TYPE,
                                            MIME_LEN_CONTENT_TYPE,
                                            s->ftp_info->mime_type->
                                            mime_type, strlen(s->ftp_info->mime_type->mime_type));
      ////////////////////////////////////////////////////////
      // do not write content length for ascii transfer     //
      // mode because the ftp server may do transformation  //
      // on the content of the file.                        //
      ////////////////////////////////////////////////////////
      if (s->ftp_info->content_length > 0 && s->ftp_info->transfer_mode == 'I') {
        s->hdr_info.server_response.set_content_length(s->ftp_info->content_length);
        s->hdr_info.response_content_length = s->ftp_info->content_length;
      } else {
        s->hdr_info.response_content_length = HTTP_UNDEFINED_CL;
      }
    } else {
      char ct[] = "text/html";
      s->hdr_info.response_content_length = HTTP_UNDEFINED_CL;
      s->hdr_info.server_response.value_set(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE, ct, sizeof(ct) - 1);

      // We don't cache directories since they are supposedly 
      //  dynamic.  As such, we must add a no-store so downstream
      //  caches don't cache them either
      if (s->http_return_code != HTTP_STATUS_MOVED_PERMANENTLY)
        s->hdr_info.server_response.value_set(MIME_FIELD_CACHE_CONTROL, MIME_LEN_CACHE_CONTROL, "no-store", 8);
      s->hdr_info.server_response.value_set(MIME_FIELD_PRAGMA, MIME_LEN_PRAGMA, "no-cache", 8);
    }
    return true;
  }
  return false;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : HandleFtpResponse
// Description: 
//
// Details    :
//
//   
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleFtpResponse(State * s)
{
  if (diags->on()) {
    DebugOn("http_trans", "[HttpTransact::HandleFtpResponse]");
    DebugOn("http_seq", "[HttpTransact::HandleFtpResponse] " "Response received");
  }

  HTTP_ASSERT(s->cache_info.action != CACHE_PREPARE_TO_DELETE);
  HTTP_ASSERT(s->cache_info.action != CACHE_PREPARE_TO_UPDATE);
  HTTP_ASSERT(s->cache_info.action != CACHE_PREPARE_TO_WRITE);

  s->source = SOURCE_FTP_ORIGIN_SERVER;
  s->response_received_time = ink_cluster_time();
  HTTP_DEBUG_ASSERT(s->response_received_time >= s->request_sent_time);
  s->current.now = s->response_received_time;

  Debug("http_trans", "[HandleFtpResponse] response_received_time: %ld", s->response_received_time);
  if (!s->cop_test_page)
    DUMP_HEADER("http_hdrs", &s->hdr_info.server_response, s->state_machine_id, "Incoming O.S. Response");

  HTTP_INCREMENT_TRANS_STAT(http_incoming_responses_stat);

  if (!is_ftp_response_valid(s)) {
    Debug("http_seq", "[HttpTransact::HandleFtpResponse] " "Response not valid");
    handle_server_died(s);
    s->next_action = PROXY_SEND_ERROR_CACHE_NOOP;
//      s->next_action = PROXY_INTERNAL_CACHE_NOOP;
    return;
  }
  Debug("http_seq", "[HttpTransact::HandleFtpResponse] " "Response valid");

  // Intialize some state variables for the ftp response
  s->current.server->transfer_encoding = NO_TRANSFER_ENCODING;
  s->hdr_info.response_content_length = HTTP_UNDEFINED_CL;
  s->current.server->keep_alive = HTTP_NO_KEEPALIVE;

  ///////////////////////////////////////////////////////////
  // open is successfull:                                  //
  // - If we opened a directory using NLST and it went ok, //
  //   let's open the directory using LIST.                //
  // - If we opened a file or a directory using LIST and   //
  //   it went ok, write the data back to the user agent.  //
  ///////////////////////////////////////////////////////////
  HTTP_ASSERT(s->current.state == CONNECTION_ALIVE);

  if (s->method == HTTP_WKSIDX_PUT) {
    if (s->cache_info.action == CACHE_DO_DELETE) {
      s->next_action = FTP_WRITE_CACHE_DELETE;
    } else {
      HTTP_DEBUG_ASSERT(s->cache_info.object_read == 0);
      HTTP_DEBUG_ASSERT(s->cache_info.action == CACHE_DO_NO_ACTION);
      s->next_action = FTP_WRITE_CACHE_NOOP;
    }
  } else if (s->method == HTTP_WKSIDX_GET) {

    ink_release_assert(s->hdr_info.server_response.valid());

    if (s->state_machine->do_transform_open()) {
      // VIA will get added back in after the transform
      s->hdr_info.server_response.field_delete(MIME_FIELD_VIA, MIME_LEN_VIA);
      set_header_for_transform(s, &s->hdr_info.server_response);
    } else {
      // No transform - just copy our server response to
      //   the ua response
      ink_assert(s->ftp_info->is_directory == false);
      s->hdr_info.client_response.create(HTTP_TYPE_RESPONSE);
      s->hdr_info.client_response.copy(&s->hdr_info.server_response);
    }

    //////////////////////////////////////////////
    // decide if to write document to the cache //
    //////////////////////////////////////////////
    if (diags->on()) {
      DebugOn("http_trans", "[HandleFtpResponse] ConfigCacheFtp:         %lld", s->http_config_param->cache_ftp);
      DebugOn("http_trans", "[HandleFtpResponse] Is this a directory:    %d", s->ftp_info->is_directory);
      DebugOn("http_trans",
              "[HandleFtpResponse] Client permits storing: %d", s->cache_info.directives.does_client_permit_storing);
    }

    if (is_response_cacheable(s,
                              &s->hdr_info.client_request,
                              &(s->hdr_info.server_response)) && s->cache_info.write_lock_state == CACHE_WL_SUCCESS) {
      Debug("http_trans", "[HandleFtpResponse] Ftp response cacheable");

      if (s->cache_info.object_read != 0) {
        Debug("http_trans", "[HandleFtpResponse] " "Deleting and caching ftp document.");
        s->cache_info.action = CACHE_DO_REPLACE;
        s->next_action = FTP_READ;
      } else {
        Debug("http_trans", "[HandleFtpResponse] Caching ftp document.");
        s->cache_info.action = CACHE_DO_WRITE;
        s->next_action = FTP_READ;
      }
      ////////////////////////////////////////////////////////
      // initialize cache object info to store the document //
      // since ftp server does not send http headers with   //
      // the document, we store our generated response      //
      // header to the user agent as the cached response.   //
      ////////////////////////////////////////////////////////
      s->cache_info.object_store.create();
      s->cache_info.object_store.request_set(&s->hdr_info.client_request);
      s->cache_info.object_store.response_set(&s->hdr_info.server_response);

      if (s->http_config_param->wuts_enabled)
        HttpTransactHeaders::convert_wuts_code_to_normal_reason(s->cache_info.object_store.response_get());

      /////////////////////////////////////////////////////////////
      // Remove the VIA field from headers for the cache since   //
      //  the proxy genrated it as part of the client response   //
      //  contruction.  Ftp servers do not send VIA headers      //
      /////////////////////////////////////////////////////////////
      s->cache_info.object_store.response_get()->field_delete(MIME_FIELD_VIA, MIME_LEN_VIA);

      HTTP_DEBUG_ASSERT(s->request_sent_time <= s->response_received_time);
    } else {
      Debug("http_trans", "[HandleFtpResponse] Ftp response not cacheable.");
      s->cache_info.action = CACHE_DO_NO_ACTION;
      s->next_action = FTP_READ;
    }
  } else {
    HTTP_DEBUG_ASSERT(!"Invalid Ftp method.");
  }

  return;
}
#endif //INK_NO_FTP

///////////////////////////////////////////////////////////////////////////////
// Name       : OriginServerRawOpen
// Description: called for ssl tunnelling
//
// Details    :
//   
// when the method is CONNECT, we open a raw connection to the origin
// server. if the open succeeds, then do ssl tunnelling from the client
// to the host.
//
//
// Possible Next States From Here:
// - HttpTransact::PROXY_INTERNAL_CACHE_NOOP;
// - HttpTransact::SSL_TUNNEL;
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::OriginServerRawOpen(State * s)
{
  Debug("http_trans", "[HttpTransact::OriginServerRawOpen]");

  switch (s->current.state) {
  case STATE_UNDEFINED:
    /* fall through */
  case OPEN_RAW_ERROR:
    /* fall through */
  case CONNECTION_ERROR:
    /* fall through */
  case CONNECTION_CLOSED:
    /* fall through */
  case CONGEST_CONTROL_CONGESTED_ON_F:
    /* fall through */
  case CONGEST_CONTROL_CONGESTED_ON_M:
    handle_server_died(s);

    HTTP_DEBUG_ASSERT(s->cache_info.action == CACHE_DO_NO_ACTION);
    s->next_action = PROXY_INTERNAL_CACHE_NOOP;
    break;
  case CONNECTION_ALIVE:
    build_response(s, &s->hdr_info.client_response, s->client_info.http_version, HTTP_STATUS_OK);

    Debug("http_trans", "[OriginServerRawOpen] connection alive. next action is ssl_tunnel");
    s->next_action = SSL_TUNNEL;
    break;
  default:
    HTTP_DEBUG_ASSERT(!("s->current.state is set to something unsupported"));
    break;
  }

  return;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : HandleResponse
// Description: called from the state machine when a response is received
//
// Details    :
//
//   This is the entry into a coin-sorting machine. There are many different
//   bins that the response can fall into. First, the response can be invalid
//   if for example it is not a response, or not complete, or the connection
//   was closed, etc. Then, the response can be from an icp-suggested-host,
//   from a parent proxy or from the origin server. The next action to take
//   differs for all three of these cases. Finally, good responses can either
//   require a cache action, be it deletion, update, or writing or may just
//   need to be tunnelled to the client. This latter case should be handled
//   with as little processing as possible, since it should represent a fast
//   path.
//
//
// Possible Next States From Here:
//
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleResponse(State * s)
{
  if (diags->on()) {
    DebugOn("http_trans", "[HttpTransact::HandleResponse]");
    DebugOn("http_seq", "[HttpTransact::HandleResponse] Response received");
  }

  s->source = SOURCE_HTTP_ORIGIN_SERVER;
  s->response_received_time = ink_cluster_time();
  HTTP_DEBUG_ASSERT(s->response_received_time >= s->request_sent_time);
  s->current.now = s->response_received_time;

  Debug("http_trans", "[HandleResponse] response_received_time: %ld", s->response_received_time);
  if (!s->cop_test_page)
    DUMP_HEADER("http_hdrs", &s->hdr_info.server_response, s->state_machine_id, "Incoming O.S. Response");

  HTTP_INCREMENT_TRANS_STAT(http_incoming_responses_stat);

  HTTP_ASSERT(s->current.request_to != UNDEFINED_LOOKUP);
  if (s->cache_info.action != CACHE_DO_WRITE) {
    HTTP_ASSERT(s->cache_info.action != CACHE_DO_LOOKUP);
    HTTP_ASSERT(s->cache_info.action != CACHE_DO_SERVE);
    HTTP_ASSERT(s->cache_info.action != CACHE_PREPARE_TO_DELETE);
    HTTP_ASSERT(s->cache_info.action != CACHE_PREPARE_TO_UPDATE);
    HTTP_ASSERT(s->cache_info.action != CACHE_PREPARE_TO_WRITE);
  }

  if (!is_response_valid(s, &s->hdr_info.server_response)) {
    Debug("http_seq", "[HttpTransact::HandleResponse] Response not valid");
  } else {
    Debug("http_seq", "[HttpTransact::HandleResponse] Response valid");
    initialize_state_variables_from_response(s, &s->hdr_info.server_response);
  }

  switch (s->current.request_to) {
#ifndef INK_NO_ICP
  case ICP_SUGGESTED_HOST:
    handle_response_from_icp_suggested_host(s);
    break;
#endif
  case PARENT_PROXY:
    handle_response_from_parent(s);
    break;
  case ORIGIN_SERVER:
    handle_response_from_server(s);
    break;
  default:
    HTTP_DEBUG_ASSERT(!("s->current.request_to is not ICP, P.P. or O.S. - hmmm."));
    break;
  }

  return;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : HandleUpdateCachedObject
// Description: called from the state machine when we are going to modify
//              headers without any server contact.
//
// Details    : this function does very little. mainly to satisfy
//              the call_transact_and_set_next format and not affect
//              the performance of the non-invalidate operations, which
//              are the majority
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleUpdateCachedObject(State * s)
{
  if (s->cache_info.write_lock_state == HttpTransact::CACHE_WL_SUCCESS) {

    ink_assert(s->cache_info.object_store.valid());
    ink_assert(s->cache_info.object_store.response_get() != NULL);
    ink_assert(s->cache_info.object_read != NULL);
    ink_assert(s->cache_info.object_read->valid());

    if (!s->cache_info.object_store.request_get())
      s->cache_info.object_store.request_set(s->cache_info.object_read->request_get());
    s->request_sent_time = s->cache_info.object_read->request_sent_time_get();
    s->response_received_time = s->cache_info.object_read->response_received_time_get();
    if (s->api_update_cached_object == UPDATE_CACHED_OBJECT_CONTINUE) {
      TRANSACT_RETURN(HttpTransact::ISSUE_CACHE_UPDATE, HttpTransact::HandleUpdateCachedObjectContinue);
    } else {
      TRANSACT_RETURN(HttpTransact::ISSUE_CACHE_UPDATE, HttpTransact::HandleApiErrorJump);
    }
  } else if (s->api_update_cached_object == UPDATE_CACHED_OBJECT_CONTINUE) {
    // even failed to update, continue to serve from cache
    HandleUpdateCachedObjectContinue(s);
  } else {
    s->api_update_cached_object = UPDATE_CACHED_OBJECT_FAIL;
    HandleApiErrorJump(s);
  }
}

void
HttpTransact::HandleUpdateCachedObjectContinue(State * s)
{
  ink_assert(s->api_update_cached_object == UPDATE_CACHED_OBJECT_CONTINUE);
  s->cache_info.action = s->saved_update_cache_action;
  s->next_action = s->saved_update_next_action;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : HandleStatPage
// Description: called from the state machine when a response is received
//
// Details    :
//
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::HandleStatPage(State * s)
{
  HTTPStatus status;

  if (s->internal_msg_buffer) {
    status = HTTP_STATUS_OK;
  } else {
    status = HTTP_STATUS_BAD_REQUEST;
  }

  build_response(s, &s->hdr_info.client_response, s->client_info.http_version, status);

  ///////////////////////////
  // insert content-length //
  ///////////////////////////
  s->hdr_info.client_response.set_content_length(s->internal_msg_buffer_size);

  if (s->internal_msg_buffer_type) {
    s->hdr_info.client_response.value_set(MIME_FIELD_CONTENT_TYPE,
                                          MIME_LEN_CONTENT_TYPE,
                                          s->internal_msg_buffer_type, strlen(s->internal_msg_buffer_type));
  }

  s->cache_info.action = CACHE_DO_NO_ACTION;
  s->next_action = PROXY_INTERNAL_CACHE_NOOP;
}

#ifndef INK_NO_ICP
///////////////////////////////////////////////////////////////////////////////
// Name       : handle_response_from_icp_suggested_host
// Description: response came from the host suggested by the icp lookup
//
// Details    :
//
//   If the response was bad (for whatever reason), may try to open a
//   connection with a parent proxy, if there are any, else the request
//   should be sent to the client.
//   If the response is good, handle_forward_server_connection_open is
//   called.
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_response_from_icp_suggested_host(State * s)
{
  Debug("http_trans", "[handle_response_from_icp_suggested_host] (hrfish)");
  HTTP_RELEASE_ASSERT(s->current.server == &(s->icp_info));

  s->icp_info.state = s->current.state;
  switch (s->current.state) {
  case CONNECTION_ALIVE:
    Debug("http_trans", "[hrfish] connection alive");
    SET_VIA_STRING(VIA_DETAIL_ICP_CONNECT, VIA_DETAIL_ICP_SUCCESS);
    handle_forward_server_connection_open(s);
    break;
  default:
    Debug("http_trans", "[hrfish] connection not alive");
    SET_VIA_STRING(VIA_DETAIL_ICP_CONNECT, VIA_DETAIL_ICP_FAILURE);

    // If the request is not retryable, bail
    if (is_request_retryable(s) == false) {
      handle_server_died(s);
      return;
    }
    // send request to parent proxy now if there is
    // one or else directly to the origin server.
    find_server_and_update_current_info(s);
    if (s->current.server->ip == 0) {
      if (s->current.request_to == PARENT_PROXY) {
        TRANSACT_RETURN(DNS_LOOKUP, PPDNSLookup);
      } else {
        HTTP_ASSERT(0);
      }
      return;
    }
    HTTP_DEBUG_ASSERT(&(s->hdr_info.server_request));
    s->next_action = how_to_open_connection(s);
    if (s->current.server == &s->server_info && s->next_hop_scheme == URL_WKSIDX_HTTP) {
      HttpTransactHeaders::remove_host_name_from_url(&s->hdr_info.server_request);
    }
    break;
  }
}
#endif //INK_NO_ICP


///////////////////////////////////////////////////////////////////////////////
// Name       : handle_response_from_parent
// Description: response came from a parent proxy
//
// Details    :
//
//   The configuration file can be used to specify more than one parent
//   proxy. If a connection to one fails, another can be looked up. This
//   function handles responses from parent proxies. If the response is
//   bad the next parent proxy (if any) is looked up. If there are no more
//   parent proxies that can be looked up, the response is sent to the
//   origin server. If the response is good handle_forward_server_connection_open
//   is called, as with handle_response_from_icp_suggested_host.
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_response_from_parent(State * s)
{
  Debug("http_trans", "[handle_response_from_parent] (hrfp)");
  HTTP_RELEASE_ASSERT(s->current.server == &(s->parent_info));

  s->parent_info.state = s->current.state;
  switch (s->current.state) {
  case CONNECTION_ALIVE:
    Debug("http_trans", "[hrfp] connection alive");
    s->current.server->connect_failure = 0;
    SET_VIA_STRING(VIA_DETAIL_PP_CONNECT, VIA_DETAIL_PP_SUCCESS);
    if (s->parent_result.retry) {
      s->parent_params->recordRetrySuccess(&s->parent_result);
    }
    handle_forward_server_connection_open(s);
    break;
  default:
    {
      LookingUp_t next_lookup = UNDEFINED_LOOKUP;
      Debug("http_trans", "[hrfp] connection not alive");
      SET_VIA_STRING(VIA_DETAIL_PP_CONNECT, VIA_DETAIL_PP_FAILURE);

      HTTP_DEBUG_ASSERT(s->hdr_info.server_request.valid());

      s->current.server->connect_failure = 1;

      Debug("http_trans", "[%d] failed to connect to parent %u.%u.%u.%u",
            s->current.attempts,
            ((unsigned char *) &s->current.server->ip)[0],
            ((unsigned char *) &s->current.server->ip)[1],
            ((unsigned char *) &s->current.server->ip)[2], ((unsigned char *) &s->current.server->ip)[3]);

      // If the request is not retryable, just give up!
      if (!is_request_retryable(s)) {
        s->parent_params->markParentDown(&s->parent_result);
        s->parent_result.r = PARENT_FAIL;
        handle_parent_died(s);
        return;
      }

      if (s->current.attempts < s->http_config_param->parent_connect_attempts) {
        s->current.attempts++;

        // Are we done with this particular parent?
        if ((s->current.attempts - 1) % s->http_config_param->per_parent_connect_attempts != 0) {

          // No we are not done with this parent so retry
          s->next_action = how_to_open_connection(s);
          Debug("http_trans", "%s Retrying parent for attempt %d, max %d",
                "[handle_response_from_parent]", s->current.attempts,
                s->http_config_param->per_parent_connect_attempts);
          return;
        } else {
          Debug("http_trans", "%s per parent attempts exhausted", "[handle_response_from_parent]", s->current.attempts);

          // Only mark the parent down if we failed to connect
          //  to the parent otherwise slow origin servers cause
          //  us to mark the parent down
          if (s->current.state == CONNECTION_ERROR) {
            s->parent_params->markParentDown(&s->parent_result);
          }
          // We are done so look for another parent if any
          next_lookup = find_server_and_update_current_info(s);
        }
      } else {
        // Done trying parents... fail over to origin server if that is
        //   appropriate
        Debug("http_trans", "[handle_response_from_parent] Error. No more retries.");
        s->parent_params->markParentDown(&s->parent_result);
        s->parent_result.r = PARENT_FAIL;
        next_lookup = find_server_and_update_current_info(s);
      }

      // We have either tried to find a new parent or failed over to the
      //   origin server
      switch (next_lookup) {
      case PARENT_PROXY:
        HTTP_DEBUG_ASSERT(s->current.request_to == PARENT_PROXY);
        TRANSACT_RETURN(DNS_LOOKUP, PPDNSLookup);
        break;
      case ORIGIN_SERVER:
        s->current.attempts = 0;
        s->next_action = how_to_open_connection(s);
        if (s->current.server == &s->server_info && s->next_hop_scheme == URL_WKSIDX_HTTP) {
          HttpTransactHeaders::remove_host_name_from_url(&s->hdr_info.server_request);
        }
        break;
      case HOST_NONE:
        handle_parent_died(s);
        break;
      default:
        // This handles:
        // UNDEFINED_LOOKUP, ICP_SUGGESTED_HOST,
        // INCOMING_ROUTER
        break;
      }

      break;
    }
  }
}


///////////////////////////////////////////////////////////////////////////////
// Name       : handle_response_from_server
// Description: response is from the origin server
//
// Details    :
//
//   response from the origin server. one of three things can happen now.
//   if the response is bad, then we can either retry (by first downgrading
//   the request, maybe making it non-keepalive, etc.), or we can give up.
//   the latter case is handled by handle_server_connection_not_open and
//   sends an error response back to the client. if the response is good
//   handle_forward_server_connection_open is called.
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_response_from_server(State * s)
{
  Debug("http_trans", "[handle_response_from_server] (hrfs)");
  HTTP_RELEASE_ASSERT(s->current.server == &(s->server_info));
  int max_connect_retries = 0;

  s->server_info.state = s->current.state;

  // plugin call
  if (s->fp_tsremap_os_response)        // && s->current.state != CONNECTION_ALIVE)
  {
    s->fp_tsremap_os_response(s->remap_plugin_instance, (rhandle) (s->state_machine), s->current.state);
  }

  switch (s->current.state) {
  case CONNECTION_ALIVE:
    Debug("http_trans", "[hrfs] connection alive");
    SET_VIA_STRING(VIA_DETAIL_SERVER_CONNECT, VIA_DETAIL_SERVER_SUCCESS);
    s->current.server->connect_failure = 0;
    handle_forward_server_connection_open(s);
    break;
  case CONGEST_CONTROL_CONGESTED_ON_F:
  case CONGEST_CONTROL_CONGESTED_ON_M:
    Debug("http_trans", "[handle_response_from_server] Error. congestion control -- congested.");
    SET_VIA_STRING(VIA_DETAIL_SERVER_CONNECT, VIA_DETAIL_SERVER_FAILURE);
    s->current.server->connect_failure = 1;
    handle_server_connection_not_open(s);
    break;
  case STATE_UNDEFINED:
    /* fall through */
  case OPEN_RAW_ERROR:
    /* fall through */
  case CONNECTION_ERROR:
    /* fall through */
  case INACTIVE_TIMEOUT:
    /* fall through */
  case PARSE_ERROR:
    /* fall through */
  case CONNECTION_CLOSED:
    /* fall through */
  case FTP_OPEN_FAILED:
    /* fall through */
  case BAD_INCOMING_RESPONSE:
    s->current.server->connect_failure = 1;
    if (is_server_negative_cached(s)) {
      max_connect_retries = s->http_config_param->connect_attempts_max_retries_dead_server;
    } else {
      // server not yet negative cached - use default number of retries
      max_connect_retries = s->http_config_param->connect_attempts_max_retries;
    }
    if (s->pCongestionEntry != NULL)
      max_connect_retries = s->pCongestionEntry->connect_retries();

    if (is_request_retryable(s) && s->current.attempts < max_connect_retries) {

      // If this is a round robin DNS entry & we're tried configured
      //    number of times, we should try another node

      bool use_srv_records = HttpConfig::m_master.srv_enabled;

      if (use_srv_records) {
        delete_srv_entry(s, max_connect_retries);
        return;
      } else if (s->server_info.dns_round_robin &&
                 (s->http_config_param->connect_attempts_rr_retries > 0) &&
                 (s->current.attempts % s->http_config_param->connect_attempts_rr_retries == 0)) {
        delete_server_rr_entry(s, max_connect_retries);
        return;
      } else {
        retry_server_connection_not_open(s, s->current.state, max_connect_retries);
        Debug("http_trans", "[handle_response_from_server] Error. Retrying...");
        s->next_action = how_to_open_connection(s);
        return;
      }
    } else {
      Debug("http_trans", "[handle_response_from_server] Error. No more retries.");
      SET_VIA_STRING(VIA_DETAIL_SERVER_CONNECT, VIA_DETAIL_SERVER_FAILURE);
      handle_server_connection_not_open(s);
    }
    break;
  case ACTIVE_TIMEOUT:
    Debug("http_trans", "[hrfs] connection not alive");
    SET_VIA_STRING(VIA_DETAIL_SERVER_CONNECT, VIA_DETAIL_SERVER_FAILURE);
    s->current.server->connect_failure = 1;
    handle_server_connection_not_open(s);
    break;
  default:
    HTTP_DEBUG_ASSERT(!("s->current.state is set to something unsupported"));
    break;
  }

  return;
}

void
HttpTransact::delete_srv_entry(State * s, int max_retries)
{
  /* we are using SRV lookups and this host failed -- lets remove it from the HostDB */
  INK_MD5 md5;
  EThread *thread = this_ethread();
  //ProxyMutex *mutex = thread->mutex;
  void *pDS = 0;

  int len;
  int port;
  ProxyMutex *bucket_mutex = hostDB.lock_for_bucket((int) (fold_md5(md5) % hostDB.buckets));

  char *hostname = s->dns_info.srv_hostname;    /* of the form: _http._tcp.host.foo.bar.com */

  s->current.attempts++;

  Debug("http_trans", "[delete_srv_entry] attempts now: %d, max: %lld", s->current.attempts, max_retries);
  Debug("dns_srv", "[delete_srv_entry] attempts now: %d, max: %lld", s->current.attempts, max_retries);

  if (!hostname) {
    TRANSACT_RETURN(OS_RR_MARK_DOWN, ReDNSRoundRobin);
  }

  len = strlen(hostname);
  port = 0;

  make_md5(md5, hostname, len, port, 0, 1);

  MUTEX_TRY_LOCK(lock, bucket_mutex, thread);
  if (lock) {
    HostDBInfo *r = probe(bucket_mutex, md5, hostname, len, 1, port, pDS, false, true);
    if (r) {
      if (r->is_srv) {
        Debug("dns_srv", "Marking SRV records for %s [Origin: %s] as bad", hostname, s->dns_info.lookup_name);

        inku64 folded_md5 = fold_md5(md5);

        HostDBInfo *new_r = NULL;

        Debug("dns_srv", "[HttpTransact::delete_srv_entry] Adding relevent entries back into HostDB");

        SRVHosts srv_hosts(r);  /* conversion constructor for SRVHosts() */
        r->set_deleted();       //delete the original HostDB
        hostDB.delete_block(r); //delete the original HostDB

        new_r = hostDB.insert_block(folded_md5, NULL, 0);       //create new entry
        new_r->md5_high = md5[1];

        SortableQueue<SRV> *q = srv_hosts.getHosts();        //get the Queue of SRV entries
        SRV *srv_entry = NULL;

        Queue<SRV> still_ok_hosts;

        new_r->srv_count = 0;
        while ((srv_entry = q->dequeue())) {    // ok to dequeue since this is the last time we are using this.
          if (strcmp(srv_entry->getHost(), s->dns_info.lookup_name) != 0) {
            still_ok_hosts.enqueue(srv_entry);
            new_r->srv_count++;
          } else {
            SRVAllocator.free(srv_entry);
          }
        }

        q = NULL;

        /* no hosts DON'T match -- max out retries and return */
        if (still_ok_hosts.empty()) {
          Debug("dns_srv", "No more SRV hosts to try that dont contain a host we just tried -- giving up");
          s->current.attempts = max_retries;
          TRANSACT_RETURN(OS_RR_MARK_DOWN, ReDNSRoundRobin);
        }

        /* 
           assert: at this point, we have (inside still_ok_hosts) those SRV records that were NOT pointing to the
           same hostname as the one that just failed; lets reenqueue those into the HostDB and perform another "lookup"
           which [hopefully] will find these records inside the HostDB and use them. 
         */

        new_r->ip_timeout_interval = 45;        /* good for 45 seconds, then lets re-validate? */
        new_r->ip_timestamp = hostdb_current_interval;
        new_r->ip() = 1;

        /* these go into the RR area */
        int n = new_r->srv_count;

        if (n < 1) {
          new_r->round_robin = 0;
        } else {
          new_r->round_robin = 1;
          int sz = HostDBRoundRobin::size(n, true);
          HostDBRoundRobin *rr_data = (HostDBRoundRobin *) hostDB.alloc(&new_r->app.rr.offset, sz);
          Debug("hostdb", "allocating %d bytes for %d RR at %lX %d", sz, n, rr_data, new_r->app.rr.offset);
          int i = 0;
          while ((srv_entry = still_ok_hosts.dequeue())) {
            Debug("dns_srv", "Re-adding %s to HostDB [as a RR] after %s failed", srv_entry->getHost(),
                  s->dns_info.lookup_name);
            rr_data->info[i].ip() = 1;
            rr_data->info[i].round_robin = 0;
            rr_data->info[i].reverse_dns = 0;

            rr_data->info[i].srv_weight = srv_entry->getWeight();
            rr_data->info[i].srv_priority = srv_entry->getPriority();
            rr_data->info[i].srv_port = srv_entry->getPort();

            ink_strncpy(rr_data->rr_srv_hosts[i], srv_entry->getHost(), MAXDNAME);
            rr_data->rr_srv_hosts[i][MAXDNAME - 1] = '\0';
            rr_data->info[i].is_srv = true;

            rr_data->info[i].md5_high = new_r->md5_high;
            rr_data->info[i].md5_low = new_r->md5_low;
            rr_data->info[i].md5_low_low = new_r->md5_low_low;
            rr_data->info[i].full = 1;
            SRVAllocator.free(srv_entry);
            i++;
          }
          rr_data->good = rr_data->n = n;
          rr_data->current = 0;
        }

      } else {
        Debug("dns_srv", "Trying to delete a bad SRV for %s and something was wonky", hostname);
      }
    } else {
      Debug("dns_srv", "No SRV data to remove. Ruh Roh Shaggy. Maxing out connection attempts...");
      s->current.attempts = max_retries;
    }
  }

  TRANSACT_RETURN(OS_RR_MARK_DOWN, ReDNSRoundRobin);
}

///////////////////////////////////////////////////////////////////////////////
// Name       : delete_server_rr_entry
// Description: 
//
// Details    :
//
//   connection to server failed mark down the server round robin entry
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::delete_server_rr_entry(State * s, int max_retries)
{
  if (diags->on()) {
    DebugOn("http_trans", "[%d] failed to connect to %u.%u.%u.%u",
            s->current.attempts,
            ((unsigned char *) &s->current.server->ip)[0],
            ((unsigned char *) &s->current.server->ip)[1],
            ((unsigned char *) &s->current.server->ip)[2], ((unsigned char *) &s->current.server->ip)[3]);
    DebugOn("http_trans", "[delete_server_rr_entry] marking rr entry " "down and finding next one");
  }
  HTTP_DEBUG_ASSERT(s->current.server->connect_failure);
  HTTP_DEBUG_ASSERT(s->current.request_to == ORIGIN_SERVER);
  HTTP_DEBUG_ASSERT(s->current.server == &s->server_info);
  update_dns_info(&s->dns_info, &s->current, 0, &s->arena);
  s->current.attempts++;
  Debug("http_trans", "[delete_server_rr_entry] attempts now: %d, max: %lld", s->current.attempts, max_retries);
  TRANSACT_RETURN(OS_RR_MARK_DOWN, ReDNSRoundRobin);
}

///////////////////////////////////////////////////////////////////////////////
// Name       : retry_server_connection_not_open
// Description: 
//
// Details    :
//
//   connection to server failed. retry.
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::retry_server_connection_not_open(State * s, ServerState_t conn_state, int max_retries)
{
  HTTP_DEBUG_ASSERT(s->current.state != CONNECTION_ALIVE);
  HTTP_DEBUG_ASSERT(s->current.state != ACTIVE_TIMEOUT);
  HTTP_DEBUG_ASSERT(s->current.attempts <= max_retries);
  HTTP_DEBUG_ASSERT(s->current.server->connect_failure != 0);

  URL *url = s->hdr_info.client_request.url_get();
  char *url_string = url->valid()? url->string_get(&s->arena) : NULL;

  Debug("http_trans", "[%d] failed to connect [%d] to %u.%u.%u.%u",
        s->current.attempts, conn_state,
        ((unsigned char *) &s->current.server->ip)[0],
        ((unsigned char *) &s->current.server->ip)[1],
        ((unsigned char *) &s->current.server->ip)[2], ((unsigned char *) &s->current.server->ip)[3]);

  //////////////////////////////////////////
  // on the first connect attempt failure //
  // record the failue                   //
  //////////////////////////////////////////

  if (!s->traffic_net_req) {
    Log::error("CONNECT:[%d] could not connect [%s] to %u.%u.%u.%u "
               "for '%s'", s->current.attempts, HttpDebugNames::get_server_state_name(conn_state),
               ((unsigned char *) &s->current.server->ip)[0],
               ((unsigned char *) &s->current.server->ip)[1],
               ((unsigned char *) &s->current.server->ip)[2],
               ((unsigned char *) &s->current.server->ip)[3], url_string ? url_string : "<none>");
  }

  if (url_string) {
    s->arena.str_free(url_string);
  }
  //////////////////////////////////////////////
  // disable keep-alive for request and retry //
  //////////////////////////////////////////////
  s->current.server->keep_alive = HTTP_NO_KEEPALIVE;
  s->current.attempts++;

  Debug("http_trans", "[retry_server_connection_not_open] attempts now: %d, max: %d", s->current.attempts, max_retries);

  return;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : handle_server_connection_not_open
// Description: 
//
// Details    :
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_server_connection_not_open(State * s)
{
  bool serve_from_cache = false;

  if (diags->on()) {
    DebugOn("http_trans", "[handle_server_connection_not_open] (hscno)");
    DebugOn("http_seq", "[HttpTransact::handle_server_connection_not_open] ");
  }
  HTTP_DEBUG_ASSERT(s->current.state != CONNECTION_ALIVE);
  HTTP_DEBUG_ASSERT(s->current.server->connect_failure != 0);

  SET_VIA_STRING(VIA_SERVER_RESULT, VIA_SERVER_ERROR);
  HTTP_INCREMENT_TRANS_STAT(http_broken_server_connections_stat);

  // Fire off a hostdb update to mark the server as down
  s->state_machine->do_hostdb_update_if_necessary();

  switch (s->cache_info.action) {
  case CACHE_DO_UPDATE:
    serve_from_cache = is_stale_cache_response_returnable(s);
    break;

  case CACHE_PREPARE_TO_DELETE:
    /* fall through */
  case CACHE_PREPARE_TO_UPDATE:
    /* fall through */
  case CACHE_PREPARE_TO_WRITE:
    HTTP_ASSERT(!"Why still preparing for cache action - " "we skipped a step somehow.");
    break;

  case CACHE_DO_LOOKUP:
    /* fall through */
  case CACHE_DO_SERVE:
    HTTP_DEBUG_ASSERT(!("Why server response? Should have been a cache operation"));
    break;

  case CACHE_DO_DELETE:
    // decisions, decisions. what should we do here?
    // we could theoretically still delete the cached
    // copy or serve it back with a warning, or easier
    // just punt and biff the user. i say: biff the user.
    /* fall through */
  case CACHE_DO_UNDEFINED:
    /* fall through */
  case CACHE_DO_NO_ACTION:
    /* fall through */
  case CACHE_DO_WRITE:
    /* fall through */
  default:
    serve_from_cache = false;
    break;
  }

  if (serve_from_cache) {
    HTTP_DEBUG_ASSERT(s->cache_info.object_read != NULL);
    HTTP_DEBUG_ASSERT(s->cache_info.action == CACHE_DO_UPDATE);
    HTTP_DEBUG_ASSERT(s->internal_msg_buffer == NULL);

    Debug("http_trans", "[hscno] serving stale doc to client");
    build_response_from_cache(s, HTTP_WARNING_CODE_REVALIDATION_FAILED);
  } else {
    handle_server_died(s);
    s->next_action = PROXY_SEND_ERROR_CACHE_NOOP;
  }

  return;
}


///////////////////////////////////////////////////////////////////////////////
// Name       : handle_forward_server_connection_open
// Description: connection to a forward server is open and good
//
// Details    :
//
//   "Forward server" includes the icp-suggested-host or the parent proxy
//   or the origin server. This function first determines if the forward
//   server uses HTTP 0.9, in which case it simply tunnels the response
//   to the client. Else, it updates
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_forward_server_connection_open(State * s)
{
  if (diags->on()) {
    DebugOn("http_trans", "[handle_forward_server_connection_open] (hfsco)");
    DebugOn("http_seq", "[HttpTransact::handle_server_connection_open] ");
  }
  HTTP_ASSERT(s->current.state == CONNECTION_ALIVE);

  if (s->hdr_info.server_response.version_get() == HTTPVersion(0, 9)) {

    Debug("http_trans", "[hfsco] server sent 0.9 response, reading...");
    build_response(s, &s->hdr_info.client_response,
                   s->client_info.http_version, HTTP_STATUS_OK, "Connection Established");

    s->client_info.keep_alive = HTTP_NO_KEEPALIVE;
    s->cache_info.action = CACHE_DO_NO_ACTION;
    s->next_action = SERVER_READ;
    return;

  }
#ifndef INK_NO_HOSTDB
  else if (s->hdr_info.server_response.version_get() == HTTPVersion(1, 0)) {

    if (s->current.server->http_version == HTTPVersion(0, 9)) {

      // update_hostdb_to_indicate_server_version_is_1_0
      s->updated_server_version = HostDBApplicationInfo::HTTP_VERSION_10;

    } else if (s->current.server->http_version == HTTPVersion(1, 1)) {

      // update_hostdb_to_indicate_server_version_is_1_0
      s->updated_server_version = HostDBApplicationInfo::HTTP_VERSION_10;

    } else {
      // dont update the hostdb. let us try again with what we currently think.
    }

  } else if (s->hdr_info.server_response.version_get() == HTTPVersion(1, 1)) {

    if (s->current.server->http_version == HTTPVersion(0, 9)) {

      // update_hostdb_to_indicate_server_version_is_1_1
      s->updated_server_version = HostDBApplicationInfo::HTTP_VERSION_11;

    } else if (s->current.server->http_version == HTTPVersion(1, 0)) {

      // update_hostdb_to_indicate_server_version_is_1_1
      s->updated_server_version = HostDBApplicationInfo::HTTP_VERSION_11;

    } else {
      // dont update the hostdb. let us try again with what we currently think.
    }

  }
#endif
  else {
    // dont update the hostdb. let us try again with what we currently think.
  }

  if (s->hdr_info.server_response.status_get() == HTTP_STATUS_CONTINUE) {
    handle_100_continue_response(s);
    return;
  }

  s->state_machine->do_hostdb_update_if_necessary();

  if (s->www_auth_content == CACHE_AUTH_FRESH) {
    // no update is needed - either to serve from cache if authorized,
    // or tunnnel the server response
    if (s->hdr_info.server_response.status_get() == HTTP_STATUS_OK) {
      // borrow a state variable used by the API function
      // this enable us to serve from cache without doing any updating
      s->api_server_response_ignore = true;
    }
    //s->cache_info.action = CACHE_PREPARE_TO_SERVE;
    // xing xing in the tunneling case, need to check when the cache_read_vc is closed, make sure the cache_read_vc is closed right away
  }

  switch (s->cache_info.action) {
  case CACHE_DO_WRITE:
    /* fall through */
  case CACHE_DO_UPDATE:
    /* fall through */
  case CACHE_DO_DELETE:
    Debug("http_trans", "[hfsco] cache action: %s", HttpDebugNames::get_cache_action_name(s->cache_info.action));
    handle_cache_operation_on_forward_server_response(s);
    break;
  case CACHE_PREPARE_TO_DELETE:
    /* fall through */
  case CACHE_PREPARE_TO_UPDATE:
    /* fall through */
  case CACHE_PREPARE_TO_WRITE:
    HTTP_ASSERT(!"Why still preparing for cache action - we skipped a step somehow.");
    break;
  case CACHE_DO_LOOKUP:
    /* fall through */
  case CACHE_DO_SERVE:
    HTTP_DEBUG_ASSERT(!("Why server response? Should have been a cache operation"));
    break;
  case CACHE_DO_UNDEFINED:
    /* fall through */
  case CACHE_DO_NO_ACTION:
    /* fall through */
  default:
    // Just tunnel?
    Debug("http_trans", "[hfsco] cache action: %s", HttpDebugNames::get_cache_action_name(s->cache_info.action));
    handle_no_cache_operation_on_forward_server_response(s);
    break;
  }

  return;
}

// void HttpTransact::handle_100_continue_response(State* s)
//
//   We've received a 100 continue response.  Determine if
//     we should just swallow the response 100 or forward it
//     the client.  http-1.1-spec-rev-06 section 8.2.3
//
void
HttpTransact::handle_100_continue_response(State * s)
{
  bool forward_100 = false;

  HTTPVersion ver = s->hdr_info.client_request.version_get();
  if (ver == HTTPVersion(1, 1)) {
    forward_100 = true;
  } else if (ver == HTTPVersion(1, 0)) {
    if (s->hdr_info.client_request.value_get_int(MIME_FIELD_EXPECT, MIME_LEN_EXPECT) == 100) {
      forward_100 = true;
    }
  }

  if (forward_100) {
    // We just want to copy the server's response.  All
    //   the other build response functions insist on
    //   adding stuff
    build_response_copy(s, &s->hdr_info.server_response, &s->hdr_info.client_response, s->client_info.http_version);
    TRANSACT_RETURN(PROXY_INTERNAL_100_RESPONSE, HandleResponse);
  } else {
    TRANSACT_RETURN(SERVER_PARSE_NEXT_HDR, HandleResponse);
  }
}

// void HttpTransact::build_response_copy
//
//   Build a response with minimal changes from the base response
//
void
HttpTransact::build_response_copy(State * s, HTTPHdr * base_response,
                                  HTTPHdr * outgoing_response, HTTPVersion outgoing_version)
{

  HttpTransactHeaders::copy_header_fields(base_response,
                                          outgoing_response,
                                          s->http_config_param->fwd_proxy_auth_to_parent, s->current.now);
  HttpTransactHeaders::convert_response(outgoing_version, outgoing_response);   // http version conversion
  HttpTransactHeaders::add_server_header_to_response(s->http_config_param, outgoing_response);

  if (!s->cop_test_page)
    DUMP_HEADER("http_hdrs", outgoing_response, s->state_machine_id, "Proxy's Response");
}

//////////////////////////////////////////////////////////////////////////
//   IMS handling table                                                 //
//       OS = Origin Server                                             //
//       IMS = A GET request w/ an If-Modified-Since header             //
//       LMs = Last modified state returned by server                   //
//       D, D' are Last modified dates returned by the origin server    //
//          and are later used for IMS                                  //
//       D < D'                                                         //
//                                                                      //
//  +------------------------------------------------------------+      //
//  | Client's | Cached    | Proxy's  | Response to client       |      //
//  | Request  |  State    | Request  |  OS 200    |   OS 304    |      //
//  |------------------------------------------------------------|      //
//  |  GET     | Fresh     | N/A      |  N/A      |  N/A         |      //
//  |------------------------------------------------------------|      //
//  |  GET     | Stale, D' | IMS  D'  | 200, new  | 200, cached  |      //
//  |------------------------------------------------------------|      //
//  |  IMS D   | None      | GET      | 200, new *|  N/A         |      //
//  |------------------------------------------------------------|      //
//  |  IMS D   | Stale, D' | IMS D'   | 200, new  | Compare      |      //
//  |---------------------------------------------| LMs & D'     |      //
//  |  IMS D'  | Stale, D' | IMS D'   | 200, new  | If match, 304|      //
//  |---------------------------------------------| If no match, |      //
//  |  IMS D'  | Stale D   | IMS D    | 200, new *|  200, cached |      //
//  +------------------------------------------------------------+      //
//                                                                      //
//  Note: * indicates a case that could be optimized to return          //
//     304 to the client but currently is not                           //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

///////////////////////////////////////////////////////////////////////////////
// Name       : handle_cache_operation_on_forward_server_response
// Description: 
//
// Details    :
//
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_cache_operation_on_forward_server_response(State * s)
{
  if (diags->on()) {
    DebugOn("http_trans", "[handle_cache_operation_on_forward_server_response] (hcoofsr)");
    DebugOn("http_seq", "[handle_cache_operation_on_forward_server_response]");
  }

  HTTPHdr *base_response = NULL;
  HTTPStatus server_response_code = HTTP_STATUS_NONE;
  HTTPStatus client_response_code = HTTP_STATUS_NONE;
  char *warn_text = NULL;
  bool cacheable = false;

  cacheable = is_response_cacheable(s, &s->hdr_info.client_request, &s->hdr_info.server_response);
  if (diags->on()) {
    if (cacheable) {
      DebugOn("http_trans", "[hcoofsr] response cacheable");
    } else {
      if (s->state_machine->request_inserted) {
        cacheProcessor.hashtable_tracker.set_response_noncacheable(s->state_machine->Hashtable_index,
                                                                   s->state_machine->RequestHeader);
      }
      DebugOn("http_trans", "[hcoofsr] response not cacheable");
    }
  }
  // set the correct next action, cache action, response code, and base response

  server_response_code = s->hdr_info.server_response.status_get();
  switch (server_response_code) {
  case HTTP_STATUS_NOT_MODIFIED:       // 304
    SET_VIA_STRING(VIA_SERVER_RESULT, VIA_SERVER_NOT_MODIFIED);

    // determine the correct cache action, next state, and response
    // precondition: s->cache_info.action should be one of the following
    // CACHE_DO_DELETE, or CACHE_DO_UPDATE; otherwise, it's an error.
    if (s->api_server_response_ignore && s->cache_info.action == CACHE_DO_UPDATE) {
      s->api_server_response_ignore = false;
      ink_assert(s->cache_info.object_read);
      base_response = s->cache_info.object_read->response_get();
      s->cache_info.action = CACHE_DO_SERVE;
      Debug("http_trans",
            "[hcoofsr] not merging, cache action changed to: %s",
            HttpDebugNames::get_cache_action_name(s->cache_info.action));
      s->next_action = HttpTransact::SERVE_FROM_CACHE;
      client_response_code = base_response->status_get();
    } else if ((s->cache_info.action == CACHE_DO_DELETE) || ((s->cache_info.action == CACHE_DO_UPDATE) && !cacheable)) {

      if (is_request_conditional(&s->hdr_info.client_request)) {
        client_response_code =
          HttpTransactCache::match_response_to_request_conditionals(&s->
                                                                    hdr_info.
                                                                    client_request,
                                                                    s->cache_info.object_read->response_get());
      } else {
        client_response_code = HTTP_STATUS_OK;
      }

      if (client_response_code != HTTP_STATUS_OK) {
        // we can just forward the not modified response
        // from the server and delete the cached copy
        base_response = &s->hdr_info.server_response;
        client_response_code = base_response->status_get();
        s->cache_info.action = CACHE_DO_DELETE;
        s->next_action = PROXY_INTERNAL_CACHE_DELETE;
      } else {
        // We got screwed. The client did not send a conditional request,
        // but we had a cached copy which we revalidated. The server has
        // now told us to delete the cached copy and sent back a 304.
        // We need to send the cached copy to the client, then delete it.
        if (s->method == HTTP_WKSIDX_HEAD) {
          s->cache_info.action = CACHE_DO_DELETE;
          s->next_action = SERVER_READ;
        } else {
          s->cache_info.action = CACHE_DO_SERVE_AND_DELETE;
          s->next_action = SERVE_FROM_CACHE;
        }
        base_response = s->cache_info.object_read->response_get();
        client_response_code = base_response->status_get();
      }

    } else if (s->cache_info.action == CACHE_DO_UPDATE && is_request_conditional(&s->hdr_info.server_request)) {
      // CACHE_DO_UPDATE and server response is cacheable

      if (is_request_conditional(&s->hdr_info.client_request)) {
        if (s->http_config_param->cache_when_to_revalidate != 4)
          client_response_code =
            HttpTransactCache::match_response_to_request_conditionals(&s->
                                                                      hdr_info.
                                                                      client_request,
                                                                      s->cache_info.object_read->response_get());
        else
          client_response_code = server_response_code;
      } else {
        client_response_code = HTTP_STATUS_OK;
      }

      if (client_response_code != HTTP_STATUS_OK) {
        // delete the cached copy unless configured to always verify IMS
        if (s->http_config_param->cache_when_to_revalidate != 4) {
          s->cache_info.action = CACHE_DO_UPDATE;
          s->next_action = PROXY_INTERNAL_CACHE_UPDATE_HEADERS;
          /* base_response will be set after updating headers below */
        } else {
          s->cache_info.action = CACHE_DO_NO_ACTION;
          s->next_action = PROXY_INTERNAL_CACHE_NOOP;
          base_response = &s->hdr_info.server_response;
        }
      } else {
        if (s->method == HTTP_WKSIDX_HEAD) {
          s->cache_info.action = CACHE_DO_UPDATE;
          s->next_action = SERVER_READ;
        } else {
          s->cache_info.action = CACHE_DO_SERVE_AND_UPDATE;
          s->next_action = SERVE_FROM_CACHE;
        }
        /* base_response will be set after updating headers below */
      }

    } else {                    // cache action != CACHE_DO_DELETE and != CACHE_DO_UPDATE

      // bogus response from server. deal by tunnelling to client.
      // server should not have sent back a 304 because our request
      // should not have been an conditional.
      Debug("http_trans", "[hcoofsr] 304 for non-conditional request");
      s->cache_info.action = CACHE_DO_NO_ACTION;
      s->next_action = PROXY_INTERNAL_CACHE_NOOP;
      client_response_code = s->hdr_info.server_response.status_get();
      base_response = &s->hdr_info.server_response;

      // since this is bad, insert warning header into client response
      // The only exception case is conditional client request,
      // cache miss, and client request being unlikely cacheable.
      // In this case, the server request is given the same
      // conditional headers as client request (see build_request()).
      // So an unexpected 304 might be received.
      // FIXME: check this case
      if (is_request_likely_cacheable(s, &s->hdr_info.client_request)) {
        warn_text = "Proxy received unexpected 304 response; " "content may be stale";
      }
    }

    break;

  case HTTP_STATUS_HTTPVER_NOT_SUPPORTED:      // 505

    bool keep_alive;
    keep_alive = ((s->current.server->keep_alive == HTTP_KEEPALIVE) ||
                  (s->current.server->keep_alive == HTTP_PIPELINE));

    s->next_action = how_to_open_connection(s);

    /* Downgrade the request level and retry */
    if (!HttpTransactHeaders::downgrade_request(&keep_alive, &s->hdr_info.server_request)) {
      build_error_response(s, HTTP_STATUS_HTTPVER_NOT_SUPPORTED,
                           "HTTP Version Not Supported", "response#bad_version", "");
      s->next_action = PROXY_SEND_ERROR_CACHE_NOOP;
      s->already_downgraded = true;
    } else {
      if (!keep_alive) {
        /* START Hack */
        (s->hdr_info.server_request).field_delete(MIME_FIELD_PROXY_CONNECTION, MIME_LEN_PROXY_CONNECTION);
        /* END   Hack */
      }
      s->already_downgraded = true;
      s->next_action = how_to_open_connection(s);
    }
    return;

  default:
    Debug("http_trans", "[hcoofsr] response code: %d", server_response_code);
    SET_VIA_STRING(VIA_SERVER_RESULT, VIA_SERVER_SERVED);
    SET_VIA_STRING(VIA_PROXY_RESULT, VIA_PROXY_SERVED);


    /* if we receive a 500, 502, 503 or 504 while revalidating
       a document, treat the response as a 304 and in effect revalidate the document for
       negative_revalidating_lifetime. (negative revalidating)
     */

    if ((server_response_code == HTTP_STATUS_INTERNAL_SERVER_ERROR ||
         server_response_code == HTTP_STATUS_GATEWAY_TIMEOUT ||
         server_response_code == HTTP_STATUS_BAD_GATEWAY ||
         server_response_code == HTTP_STATUS_SERVICE_UNAVAILABLE) &&
        s->cache_info.action == CACHE_DO_UPDATE &&
        s->http_config_param->negative_revalidating_enabled && is_stale_cache_response_returnable(s)) {

      Debug("http_trans", "[hcoofsr] negative revalidating: revalidate stale object and serve from cache");

      s->cache_info.object_store.create();
      s->cache_info.object_store.request_set(&s->hdr_info.client_request);
      s->cache_info.object_store.response_set(s->cache_info.object_read->response_get());
      base_response = s->cache_info.object_store.response_get();
      time_t exp_time = s->http_config_param->negative_revalidating_lifetime + ink_cluster_time();
      base_response->set_expires(exp_time);

      SET_VIA_STRING(VIA_CACHE_FILL_ACTION, VIA_CACHE_UPDATED);
      HTTP_INCREMENT_TRANS_STAT(http_cache_updates_stat);

      // unset Cache-control: "need-revalidate-once" (if it's set)
      // This directive is used internally by T.S. to invalidate
      // documents so that an invalidated document needs to be
      // revalidated again.
      base_response->unset_cooked_cc_need_revalidate_once();

      if (is_request_conditional(&s->hdr_info.client_request) &&
          HttpTransactCache::match_response_to_request_conditionals(&s->
                                                                    hdr_info.
                                                                    client_request,
                                                                    s->
                                                                    cache_info.
                                                                    object_read->
                                                                    response_get()) == HTTP_STATUS_NOT_MODIFIED) {
        s->next_action = HttpTransact::PROXY_INTERNAL_CACHE_UPDATE_HEADERS;
        client_response_code = HTTP_STATUS_NOT_MODIFIED;
      } else {

        if (s->method == HTTP_WKSIDX_HEAD) {
          s->cache_info.action = CACHE_DO_UPDATE;
          s->next_action = HttpTransact::PROXY_INTERNAL_CACHE_NOOP;
        } else {
          s->cache_info.action = CACHE_DO_SERVE_AND_UPDATE;
          s->next_action = HttpTransact::SERVE_FROM_CACHE;
        }

        client_response_code = HTTP_STATUS_OK;
      }

      HTTP_DEBUG_ASSERT(base_response->valid());

      if (client_response_code == HTTP_STATUS_NOT_MODIFIED) {
        HTTP_DEBUG_ASSERT(GET_VIA_STRING(VIA_CLIENT_REQUEST) != VIA_CLIENT_SIMPLE);
        SET_VIA_STRING(VIA_CLIENT_REQUEST, VIA_CLIENT_IMS);
        SET_VIA_STRING(VIA_PROXY_RESULT, VIA_PROXY_NOT_MODIFIED);
      } else {
        SET_VIA_STRING(VIA_PROXY_RESULT, VIA_PROXY_SERVED);
      }

      HTTP_DEBUG_ASSERT(client_response_code != HTTP_STATUS_NONE);

      if (s->next_action == HttpTransact::SERVE_FROM_CACHE && s->state_machine->do_transform_open()) {
        set_header_for_transform(s, base_response);
      } else {
        build_response(s, base_response, &s->hdr_info.client_response,
                       s->client_info.http_version, client_response_code);
      }

      return;
    }

    s->next_action = SERVER_READ;
    client_response_code = server_response_code;
    base_response = &s->hdr_info.server_response;

    s->negative_caching = is_negative_caching_appropriate(s);

    // determine the correct cache action given the original cache action,
    // cacheability of server response, and request method
    // precondition: s->cache_info.action is one of the following
    // CACHE_DO_UPDATE, CACHE_DO_WRITE, or CACHE_DO_DELETE
    if (s->api_server_response_no_store) {
      s->api_server_response_no_store = false;
      s->cache_info.action = CACHE_DO_NO_ACTION;
    } else if (s->api_server_response_ignore &&
               server_response_code == HTTP_STATUS_OK &&
               s->hdr_info.server_request.method_get_wksidx() == HTTP_WKSIDX_HEAD) {
      s->api_server_response_ignore = false;
      ink_assert(s->cache_info.object_read);
      base_response = s->cache_info.object_read->response_get();
      s->cache_info.action = CACHE_DO_SERVE;
      Debug("http_trans", "[hcoofsr] ignoring server response, "
            "cache action changed to: %s", HttpDebugNames::get_cache_action_name(s->cache_info.action));
      s->next_action = HttpTransact::SERVE_FROM_CACHE;
      client_response_code = base_response->status_get();
    } else if (s->cache_info.action == CACHE_DO_UPDATE) {
      if (s->www_auth_content == CACHE_AUTH_FRESH) {
        s->cache_info.action = CACHE_DO_NO_ACTION;
      } else if (s->www_auth_content == CACHE_AUTH_STALE && server_response_code == HTTP_STATUS_UNAUTHORIZED) {
        s->cache_info.action = CACHE_DO_NO_ACTION;
      } else if (!cacheable) {
        s->cache_info.action = CACHE_DO_DELETE;
      } else if (s->method == HTTP_WKSIDX_HEAD) {
        s->cache_info.action = CACHE_DO_DELETE;
      } else {
        HTTP_DEBUG_ASSERT(s->cache_info.object_read != 0);
        s->cache_info.action = CACHE_DO_REPLACE;
      }

    } else if (s->cache_info.action == CACHE_DO_WRITE) {
      if (!cacheable && !s->negative_caching) {
        s->cache_info.action = CACHE_DO_NO_ACTION;
      } else if (s->method == HTTP_WKSIDX_HEAD) {
        s->cache_info.action = CACHE_DO_NO_ACTION;
      } else {
        s->cache_info.action = CACHE_DO_WRITE;
      }

    } else if (s->cache_info.action == CACHE_DO_DELETE) {
      // do nothing

    } else {
      HTTP_DEBUG_ASSERT(!("cache action inconsistent with current state"));
    }
    // postcondition: s->cache_info.action is one of the following
    // CACHE_DO_REPLACE, CACHE_DO_WRITE, CACHE_DO_DELETE, or
    // CACHE_DO_NO_ACTION

    // Check see if we ought to serve the client a 304 based on
    //   it's IMS date.  We may gotten a 200 back from the origin
    //   server if our (the proxies's) cached copy was out of date
    //   but the client's wasn't.  However, if the response is
    //   not cacheable we ought not issue a 304 to the client so
    //   make sure we are writing the document to the cache if
    //   before issuing a 304
    if (s->cache_info.action == CACHE_DO_WRITE ||
        s->cache_info.action == CACHE_DO_NO_ACTION || s->cache_info.action == CACHE_DO_REPLACE) {
      if (s->negative_caching) {
        HTTPHdr *resp;
        s->cache_info.object_store.create();
        s->cache_info.object_store.request_set(&s->hdr_info.client_request);
        s->cache_info.object_store.response_set(&s->hdr_info.server_response);
        resp = s->cache_info.object_store.response_get();
        if (!resp->presence(MIME_PRESENCE_EXPIRES)) {
          time_t exp_time = s->http_config_param->negative_caching_lifetime + ink_cluster_time();
          resp->set_expires(exp_time);
        }
      } else if (is_request_conditional(&s->hdr_info.client_request) && server_response_code == HTTP_STATUS_OK) {
        Debug("http_trans", "[hcoofsr] conditional request, 200 " "response, send back 304 if possible");

        client_response_code =
          HttpTransactCache::match_response_to_request_conditionals(&s->
                                                                    hdr_info.
                                                                    client_request, &s->hdr_info.server_response);

        if ((client_response_code == HTTP_STATUS_NOT_MODIFIED) ||
            (client_response_code == HTTP_STATUS_PRECONDITION_FAILED)) {

          switch (s->cache_info.action) {
          case CACHE_DO_WRITE:
          case CACHE_DO_REPLACE:
            s->next_action = PROXY_INTERNAL_CACHE_WRITE;
            break;
          case CACHE_DO_DELETE:
            s->next_action = PROXY_INTERNAL_CACHE_DELETE;
            break;
          default:
            s->next_action = PROXY_INTERNAL_CACHE_NOOP;
            break;
          }
        } else {
          SET_VIA_STRING(VIA_PROXY_RESULT, VIA_PROXY_SERVER_REVALIDATED);
        }
      }
    } else if (s->negative_caching)
      s->negative_caching = false;
    break;
  }

  // update stat, set via string, etc

  switch (s->cache_info.action) {
  case CACHE_DO_SERVE_AND_DELETE:
    // fall through
  case CACHE_DO_DELETE:
    Debug("http_trans", "[hcoofsr] delete cached copy");
    SET_VIA_STRING(VIA_CACHE_FILL_ACTION, VIA_CACHE_DELETED);
    HTTP_INCREMENT_TRANS_STAT(http_cache_deletes_stat);
    break;
  case CACHE_DO_WRITE:
    Debug("http_trans", "[hcoofsr] cache write");
    SET_VIA_STRING(VIA_CACHE_FILL_ACTION, VIA_CACHE_WRITTEN);
    HTTP_INCREMENT_TRANS_STAT(http_cache_writes_stat);
    break;
  case CACHE_DO_SERVE_AND_UPDATE:
    // fall through
  case CACHE_DO_UPDATE:
    // fall through
  case CACHE_DO_REPLACE:
    Debug("http_trans", "[hcoofsr] cache update/replace");
    SET_VIA_STRING(VIA_CACHE_FILL_ACTION, VIA_CACHE_UPDATED);
    HTTP_INCREMENT_TRANS_STAT(http_cache_updates_stat);
    break;
  default:
    break;
  }

  if ((client_response_code == HTTP_STATUS_NOT_MODIFIED) && (s->cache_info.action != CACHE_DO_NO_ACTION)) {
    /* HTTP_DEBUG_ASSERT(GET_VIA_STRING(VIA_CLIENT_REQUEST)
       != VIA_CLIENT_SIMPLE); */
    Debug("http_trans", "[hcoofsr] Client request was conditional");
    SET_VIA_STRING(VIA_CLIENT_REQUEST, VIA_CLIENT_IMS);
    SET_VIA_STRING(VIA_PROXY_RESULT, VIA_PROXY_NOT_MODIFIED);
  } else {
    SET_VIA_STRING(VIA_PROXY_RESULT, VIA_PROXY_SERVED);
  }

  HTTP_DEBUG_ASSERT(client_response_code != HTTP_STATUS_NONE);

  // The correct cache action, next action, and response code are set.
  // Do the real work below.

  // first update the cached object
  if ((s->cache_info.action == CACHE_DO_UPDATE) || (s->cache_info.action == CACHE_DO_SERVE_AND_UPDATE)) {
    Debug("http_trans", "[hcoofsr] merge and update cached copy");
    merge_and_update_headers_for_cache_update(s);
    base_response = s->cache_info.object_store.response_get();
    // unset Cache-control: "need-revalidate-once" (if it's set)
    // This directive is used internally by T.S. to invalidate documents
    // so that an invalidated document needs to be revalidated again.
    base_response->unset_cooked_cc_need_revalidate_once();
    // unset warning revalidation failed header if it set
    // (potentially added by negative revalidating)
    delete_warning_value(base_response, HTTP_WARNING_CODE_REVALIDATION_FAILED);
  }
  HTTP_DEBUG_ASSERT(base_response->valid());

  if ((s->cache_info.action == CACHE_DO_WRITE) || (s->cache_info.action == CACHE_DO_REPLACE)) {
    set_headers_for_cache_write(s, &s->cache_info.object_store,
                                &s->hdr_info.server_request, &s->hdr_info.server_response);
  }
  // 304, 412, and 416 responses are handled here
  if ((client_response_code == HTTP_STATUS_NOT_MODIFIED) || (client_response_code == HTTP_STATUS_PRECONDITION_FAILED)) {

    // Because we are decoupling User-Agent validation from
    //  Traffic Server validation just build a regular 304
    //  if the exception of adding prepending the VIA
    //  header to show the revalidation path
    build_response(s, base_response, &(s->hdr_info.client_response), s->client_info.http_version, client_response_code);

    // Copy over the response via field (if any) preserving
    //  the order of the fields
    MIMEField *resp_via = s->hdr_info.server_response.field_find(MIME_FIELD_VIA, MIME_LEN_VIA);
    if (resp_via) {
      MIMEField *our_via;
      our_via = s->hdr_info.client_response.field_find(MIME_FIELD_VIA, MIME_LEN_VIA);
      if (our_via == NULL) {
        our_via = s->hdr_info.client_response.field_create(MIME_FIELD_VIA, MIME_LEN_VIA);
        s->hdr_info.client_response.field_attach(our_via);
      }
      // HDR FIX ME - Mulitple appends are VERY slow
      while (resp_via) {
        int clen;
        const char *cfield = resp_via->value_get(&clen);
        s->hdr_info.client_response.field_value_append(our_via, cfield, clen, true);
        resp_via = resp_via->m_next_dup;
      }
    }
    // a warning text is added only in the case of a NOT MODIFIED response
    if (warn_text) {
      HttpTransactHeaders::insert_warning_header(s->http_config_param,
                                                 &s->hdr_info.
                                                 client_response,
                                                 HTTP_WARNING_CODE_MISC_WARNING, warn_text, strlen(warn_text));
    }

    if (!s->cop_test_page)
      DUMP_HEADER("http_hdrs", &s->hdr_info.client_response,
                  s->state_machine_id, "Proxy's Response (Client Conditionals)");
    return;
  }
  // all other responses (not 304, 412, 416) are handled here
  else {
    if (((s->next_action == SERVE_FROM_CACHE) ||
         (s->next_action == SERVER_READ)) && s->state_machine->do_transform_open()) {
      set_header_for_transform(s, base_response);
    } else {
      build_response(s, base_response, &s->hdr_info.client_response, s->client_info.http_version, client_response_code);
    }
  }

  return;
}


///////////////////////////////////////////////////////////////////////////////
// Name       : handle_no_cache_operation_on_forward_server_response
// Description: 
//
// Details    :
//
//
//
// Possible Next States From Here:
//
///////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_no_cache_operation_on_forward_server_response(State * s)
{
  if (diags->on()) {
    DebugOn("http_trans", "[handle_no_cache_operation_on_forward_server_response] (hncoofsr)");
    DebugOn("http_seq", "[handle_no_cache_operation_on_forward_server_response]");
  }

  bool keep_alive = true;
  keep_alive = ((s->current.server->keep_alive == HTTP_KEEPALIVE) || (s->current.server->keep_alive == HTTP_PIPELINE));
  char *warn_text = NULL;

  switch (s->hdr_info.server_response.status_get()) {
  case HTTP_STATUS_OK:
    Debug("http_trans", "[hncoofsr] server sent back 200");
    SET_VIA_STRING(VIA_SERVER_RESULT, VIA_SERVER_SERVED);
    SET_VIA_STRING(VIA_PROXY_RESULT, VIA_PROXY_SERVED);
    if (s->method == HTTP_WKSIDX_CONNECT) {
      Debug("http_trans", "[hncoofsr] next action is SSL_TUNNEL");
      s->next_action = SSL_TUNNEL;
    } else {
      Debug("http_trans", "[hncoofsr] next action will be OS_READ_CACHE_NOOP");

      HTTP_DEBUG_ASSERT(s->cache_info.action == CACHE_DO_NO_ACTION);
      s->next_action = SERVER_READ;
    }
    if (s->state_machine->redirect_url == NULL) {
      s->state_machine->enable_redirection = false;
    }
    break;
  case HTTP_STATUS_NOT_MODIFIED:
    Debug("http_trans", "[hncoofsr] server sent back 304. IMS from client?");
    SET_VIA_STRING(VIA_SERVER_RESULT, VIA_SERVER_NOT_MODIFIED);
    SET_VIA_STRING(VIA_PROXY_RESULT, VIA_PROXY_NOT_MODIFIED);

    if (!is_request_conditional(&s->hdr_info.client_request)) {
      // bogus server response. not a conditional request
      // from the client and probably not a conditional
      // request from the proxy. 

      // since this is bad, insert warning header into client response
      warn_text = "Proxy received unexpected 304 response; content may be stale";
    }

    HTTP_DEBUG_ASSERT(s->cache_info.action == CACHE_DO_NO_ACTION);
    s->next_action = PROXY_INTERNAL_CACHE_NOOP;
    break;
  case HTTP_STATUS_HTTPVER_NOT_SUPPORTED:
    s->next_action = how_to_open_connection(s);

    /* Downgrade the request level and retry */
    if (!HttpTransactHeaders::downgrade_request(&keep_alive, &s->hdr_info.server_request)) {
      s->already_downgraded = true;
      build_error_response(s, HTTP_STATUS_HTTPVER_NOT_SUPPORTED,
                           "HTTP Version Not Supported", "response#bad_version", "");

      s->next_action = PROXY_SEND_ERROR_CACHE_NOOP;
    } else {
      s->already_downgraded = true;
      s->next_action = how_to_open_connection(s);
    }
    return;
  default:
    Debug("http_trans", "[hncoofsr] server sent back something other than 100,304,200");
    /* Default behavior is to pass-through response to the client */

    HTTP_DEBUG_ASSERT(s->cache_info.action == CACHE_DO_NO_ACTION);
    s->next_action = SERVER_READ;
    break;
  }

  HTTPHdr *to_warn;
  if (s->next_action == SERVER_READ && s->state_machine->do_transform_open()) {
    set_header_for_transform(s, &s->hdr_info.server_response);
    to_warn = &s->hdr_info.transform_response;
  } else {
    build_response(s, &s->hdr_info.server_response, &s->hdr_info.client_response, s->client_info.http_version);
    to_warn = &s->hdr_info.server_response;
  }

  if (warn_text) {
    HttpTransactHeaders::insert_warning_header(s->http_config_param,
                                               to_warn, HTTP_WARNING_CODE_MISC_WARNING, warn_text, strlen(warn_text));
  }

  return;
}


void
HttpTransact::merge_and_update_headers_for_cache_update(State * s)
{
  URL *s_url = NULL;
  // This is not used.
  // HTTPHdr * f_resp = NULL;

  if (!s->cache_info.object_store.valid()) {
    s->cache_info.object_store.create();
  }

  s->cache_info.object_store.request_set(&s->hdr_info.server_request);

  s_url = &(s->cache_info.store_url);
  if (!s_url->valid()) {
    if (s->redirect_info.redirect_in_process)
      s_url = &(s->redirect_info.original_url);
    else
      s_url = &(s->cache_info.original_url);
    ink_assert(s_url != NULL);
  }

  s->cache_info.object_store.request_get()->url_set(s_url->valid()? s_url : s->hdr_info.client_request.url_get());

  if (s->cache_info.object_store.request_get()->method_get_wksidx() == HTTP_WKSIDX_HEAD) {
    s->cache_info.object_store.request_get()->method_set(HTTP_METHOD_GET, HTTP_LEN_GET);
  }

  if (s->api_modifiable_cached_resp) {
    ink_assert(s->cache_info.object_store.response_get() != NULL && s->cache_info.object_store.response_get()->valid());
    s->api_modifiable_cached_resp = false;
  } else {
    s->cache_info.object_store.response_set(s->cache_info.object_read->response_get());
  }

  merge_response_header_with_cached_header(s->cache_info.object_store.response_get(), &s->hdr_info.server_response);

  // Some special processing for 304
  //
  if (s->hdr_info.server_response.status_get() == HTTP_STATUS_NOT_MODIFIED) {

    // Hack fix. If the server sends back
    // a 304 without a Date Header, use the current time
    // as the new Date value in the header to be cached.
    time_t date_value = s->hdr_info.server_response.get_date();
    HTTPHdr *cached_hdr = s->cache_info.object_store.response_get();
    if (date_value <= 0) {
      cached_hdr->set_date(s->request_sent_time);
    }
    // If the cached response has an Age: we should update it
    // We could use calculate_document_age but my guess is it's overkill
    // Just use 'now' - 304's Date: + Age: (response's Age: if there)
    //
    date_value = s->hdr_info.server_response.get_date();
    if (date_value <= 0) {
      date_value = s->request_sent_time;
    }
    date_value = max(s->current.now - date_value, (time_t) 0);
    if (s->hdr_info.server_response.presence(MIME_PRESENCE_AGE))
      date_value += s->hdr_info.server_response.get_age();
    cached_hdr->set_age(date_value);

    delete_warning_value(cached_hdr, HTTP_WARNING_CODE_REVALIDATION_FAILED);
  }

  s->cache_info.object_store.request_get()->field_delete(MIME_FIELD_VIA, MIME_LEN_VIA);

  if (s->http_config_param->wuts_enabled)
    HttpTransactHeaders::convert_wuts_code_to_normal_reason(s->cache_info.object_store.response_get());
}

void
HttpTransact::handle_transform_cache_write(State * s)
{

  ink_assert(s->cache_info.transform_action == CACHE_PREPARE_TO_WRITE);

  switch (s->cache_info.write_lock_state) {
  case CACHE_WL_SUCCESS:
    // We were able to get the lock for the URL vector in the cache
    s->cache_info.transform_action = CACHE_DO_WRITE;
    break;
  case CACHE_WL_FAIL:
    // No write lock, ignore the cache
    s->cache_info.transform_action = CACHE_DO_NO_ACTION;
    s->cache_info.transform_write_status = CACHE_WRITE_LOCK_MISS;
    break;
  default:
    ink_release_assert(0);
  }

  TRANSACT_RETURN(TRANSFORM_READ, NULL);
}

void
HttpTransact::handle_transform_ready(State * s)
{
  ink_assert(s->hdr_info.transform_response.valid() == true);

  Source_t orig_source = s->source;
  s->source = SOURCE_TRANSFORM;

  if (!s->cop_test_page)
    DUMP_HEADER("http_hdrs", &s->hdr_info.transform_response, s->state_machine_id, "Header From Transform");

  build_response(s, &s->hdr_info.transform_response, &s->hdr_info.client_response, s->client_info.http_version);

  if (s->cache_info.action != CACHE_DO_NO_ACTION &&
      s->cache_info.action != CACHE_DO_DELETE && s->api_info.cache_transformed && !s->range_setup) {
    HTTPHdr *transform_store_request = 0;
    switch (orig_source) {
    case SOURCE_CACHE:
      // If we are transforming from the cache, treat
      //  the transform as if it were virtual server
      //  use in the incoming request
      transform_store_request = &s->hdr_info.client_request;
      break;
    case SOURCE_HTTP_ORIGIN_SERVER:
      transform_store_request = &s->hdr_info.server_request;
      break;
    case SOURCE_FTP_ORIGIN_SERVER:
      transform_store_request = &s->hdr_info.client_request;
      break;
    default:
      ink_release_assert(0);
    }
    ink_assert(transform_store_request->valid() == true);

    set_headers_for_cache_write(s, &s->cache_info.transform_store,
                                transform_store_request, &s->hdr_info.transform_response);

    // For debugging
    if (is_action_tag_set("http_nullt")) {
      s->cache_info.transform_store.request_get()->value_set("InkXform", 8, "nullt", 5);
      s->cache_info.transform_store.response_get()->value_set("InkXform", 8, "nullt", 5);
    }

    s->cache_info.transform_action = CACHE_PREPARE_TO_WRITE;
    TRANSACT_RETURN(CACHE_ISSUE_WRITE_TRANSFORM, handle_transform_cache_write);
  } else {
    s->cache_info.transform_action = CACHE_DO_NO_ACTION;
    TRANSACT_RETURN(TRANSFORM_READ, NULL);
  }
}

void
HttpTransact::set_header_for_transform(State * s, HTTPHdr * base_header)
{
#ifndef INK_NO_TRANSFORM
  s->hdr_info.transform_response.create(HTTP_TYPE_RESPONSE);
  s->hdr_info.transform_response.copy(base_header);

  // Nuke the content length since 1) the transform will probably 
  //   change it.  2) it would only be valid for the first transform
  //   in the chain
  s->hdr_info.transform_response.field_delete(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);

  if (!s->cop_test_page)
    DUMP_HEADER("http_hdrs", &s->hdr_info.transform_response, s->state_machine_id, "Header To Transform");
#else
  ink_assert(!"transformation not supported\n");
#endif //INK_NO_TRANSFORM
}

void
HttpTransact::set_headers_for_cache_write(State * s, HTTPInfo * cache_info, HTTPHdr * request, HTTPHdr * response)
{
  URL *temp_url;
  HTTP_DEBUG_ASSERT(request->type_get() == HTTP_TYPE_REQUEST);
  HTTP_DEBUG_ASSERT(response->type_get() == HTTP_TYPE_RESPONSE);

  if (!cache_info->valid()) {
    cache_info->create();
  }

  /* Store the requested URI */
  //  Nasty hack. The set calls for
  //  marshalled types current do handle something being
  //  set to itself.  Make the check here for that case.
  //  Why the request url is set before a copy made is 
  //  quite beyond me.  Seems like a unsafe practice so
  //  FIX ME!

  // Logic added to restore the orignal URL for multiple cache lookup
  // and automatic redirection
  if ((temp_url = &(s->cache_info.store_url))->valid()) {
    request->url_set(temp_url);
  } else if (s->redirect_info.redirect_in_process) {
    temp_url = &(s->redirect_info.original_url);
    ink_assert(temp_url->valid());
    request->url_set(temp_url);
  } else if ((temp_url = &(s->cache_info.original_url))->valid()) {
    request->url_set(temp_url);
  } else if (request != &s->hdr_info.client_request) {
    request->url_set(s->hdr_info.client_request.url_get());
  }
  cache_info->request_set(request);
  if (!s->negative_caching)
    cache_info->response_set(response);
  else {
    ink_assert(cache_info->response_get()->valid());
  }

  if (s->api_server_request_body_set)
    cache_info->request_get()->method_set(HTTP_METHOD_GET, HTTP_LEN_GET);

  // Set-Cookie should not be put in the cache to prevent
  //  sending person A's cookie to person B
  cache_info->response_get()->field_delete(MIME_FIELD_SET_COOKIE, MIME_LEN_SET_COOKIE);
  cache_info->request_get()->field_delete(MIME_FIELD_VIA, MIME_LEN_VIA);
  // server 200 Ok for Range request
  cache_info->request_get()->field_delete(MIME_FIELD_RANGE, MIME_LEN_RANGE);

  // If we're ignoring auth, then we don't want to cache WWW-Auth
  //  headers
  if (s->http_config_param->cache_ignore_auth) {
    cache_info->response_get()->field_delete(MIME_FIELD_WWW_AUTHENTICATE, MIME_LEN_WWW_AUTHENTICATE);
  }

  //if (s->cache_control.cache_auth_content && s->www_auth_content != CACHE_AUTH_NONE) {
    // decided to cache authenticated content because of cache.config
    // add one marker to the content in cache
   // cache_info->response_get()->value_set("@WWW-Auth", 9, "true", 4);
  //}
  if (s->http_config_param->wuts_enabled)
    HttpTransactHeaders::convert_wuts_code_to_normal_reason(cache_info->response_get());

  if (!s->cop_test_page)
    DUMP_HEADER("http_hdrs", cache_info->request_get(), s->state_machine_id, "Cached Request Hdr");

#ifdef USE_NCA
  // With NCA set get a new ctag here since this is a new document
  if (s->client_info.port_attribute == SERVER_PORT_NCA) {
    inku64 new_ctag = ncaProcessor.allocate_ctag();

    char ctag_str[21];
    snprintf(ctag_str, sizeof(ctag_str), "%llu", new_ctag);

    HTTPHdr *ch = cache_info->response_get();
    ch->value_set("@Ctag", 5, ctag_str, strlen(ctag_str));

    s->nca_info.response_info.ctag = new_ctag;
  }
#endif
}

void
HttpTransact::merge_response_header_with_cached_header(HTTPHdr * cached_header, HTTPHdr * response_header)
{
  MIMEField *field;
  MIMEField *new_field;
  MIMEFieldIter fiter;
  const char *name;
  bool dups_seen = false;


  field = response_header->iter_get_first(&fiter);

  for (; field != NULL; field = response_header->iter_get_next(&fiter)) {

    int name_len;
    name = field->name_get(&name_len);

    ///////////////////////////
    // is hop-by-hop header? //
    ///////////////////////////
    if (HttpTransactHeaders::is_this_a_hop_by_hop_header(name)) {
      continue;
    }
    /////////////////////////////////////
    // dont cache content-length field //
    /////////////////////////////////////
    if (name == MIME_FIELD_CONTENT_LENGTH) {
      continue;
    }
    /////////////////////////////////////
    // dont cache Set-Cookie headers   //
    /////////////////////////////////////
    if (name == MIME_FIELD_SET_COOKIE) {
      continue;
    }
    /////////////////////////////////////////
    // dont overwrite the cached content   //
    //   type as this wreaks havoc with    //
    //   transformed content               //
    /////////////////////////////////////////
    if (name == MIME_FIELD_CONTENT_TYPE) {
      continue;
    }
    /////////////////////////////////////
    // dont delete warning.  a separate//
    //  functions merges the two in a  //
    //  complex manner                 //
    /////////////////////////////////////
    if (name == MIME_FIELD_WARNING) {
      continue;
    }
    // Copy all remaining headers with replacement      

    // Duplicate header fields cause a bug problem
    //   since we need to duplicate with replacement.
    //   Without dups, we can just nuke what is already 
    //   there in the cached header.  With dups, we
    //   can't do this because what is already there
    //   may be a dup we've already copied in.  If
    //   dups show up we look through the remaining
    //   header fields in the new reponse, nuke
    //   them in the cached response and then add in
    //   the remaining fields one by one from the
    //   response header
    //    
    if (field->m_next_dup) {
      if (dups_seen == false) {
        MIMEField *dfield;
        // use a second iterator to delete the
        // remaining response headers in the cached response,
        // so that they will be added in the next iterations.
        MIMEFieldIter fiter2 = fiter;
        const char *dname = name;
        int dlen = name_len;

        while (dname) {
          cached_header->field_delete(dname, dlen);
          dfield = response_header->iter_get_next(&fiter2);
          if (dfield) {
            dname = dfield->name_get(&dlen);
          } else {
            dname = NULL;
          }
        }
        dups_seen = true;
      }
    }

    int value_len;
    const char *value = field->value_get(&value_len);

    if (dups_seen == false) {
      cached_header->value_set(name, name_len, value, value_len);
    } else {
      new_field = cached_header->field_create(name, name_len);
      cached_header->field_attach(new_field);
      cached_header->field_value_set(new_field, value, value_len);
    }
  }

  merge_warning_header(cached_header, response_header);

  Debug("http_hdr_space", "Merged response header with %d dead bytes", cached_header->m_heap->m_lost_string_space);
}


void
HttpTransact::merge_warning_header(HTTPHdr * cached_header, HTTPHdr * response_header)
{
  //  The plan:
  //
  //    1) The cached header has it's warning codes untouched
  //         since merge_response_header_with_cached_header()
  //         doesn't deal with warning headers.
  //    2) If there are 1xx warning codes in the cached
  //         header, they need to be removed.  Removal
  //         is difficult since the hdrs don't comma 
  //         separate values, so build up a new header
  //         piecemal.  Very slow but shouldn't happen
  //         very often
  //    3) Since we keep the all the warning codes from
  //         the response header, append if to
  //         the cached header
  //
  MIMEField *c_warn = cached_header->field_find(MIME_FIELD_WARNING,
                                                MIME_LEN_WARNING);

  MIMEField *r_warn = response_header->field_find(MIME_FIELD_WARNING,
                                                  MIME_LEN_WARNING);

  MIMEField *new_cwarn = NULL;
  int move_warn_len;
  const char *move_warn;

  // Loop over the cached warning header and transfer all non 1xx
  //   warning values to a new header
  if (c_warn) {
    HdrCsvIter csv;

    move_warn = csv.get_first(c_warn, &move_warn_len);
    while (move_warn) {

      int code = ink_atoi(move_warn, move_warn_len);
      if (code<100 || code> 199) {
        bool first_move;
        if (!new_cwarn) {
          new_cwarn = cached_header->field_create();
          first_move = true;
        } else {
          first_move = false;
        }
        cached_header->field_value_append(new_cwarn, move_warn, move_warn_len, !first_move);
      }

      move_warn = csv.get_next(&move_warn_len);
    }

    // At this point we can nuke the old warning headers
    cached_header->field_delete(MIME_FIELD_WARNING, MIME_LEN_WARNING);

    // Add in the new header if it has anything in it
    if (new_cwarn) {
      new_cwarn->name_set(cached_header->m_heap, cached_header->m_mime, MIME_FIELD_WARNING, MIME_LEN_WARNING);
      cached_header->field_attach(new_cwarn);
    }
  }
  // Loop over all the dups in the response warning header and append
  //  them one by one on to the cached warning header
  while (r_warn) {
    move_warn = r_warn->value_get(&move_warn_len);

    if (new_cwarn) {
      cached_header->field_value_append(new_cwarn, move_warn, move_warn_len, true);
    } else {
      new_cwarn = cached_header->field_create(MIME_FIELD_WARNING, MIME_LEN_WARNING);
      cached_header->field_attach(new_cwarn);
      cached_header->field_value_set(new_cwarn, move_warn, move_warn_len);
    }

    r_warn = r_warn->m_next_dup;
  }
}

void
HttpTransact::get_ka_info_from_host_db(State * s,
                                       ConnectionAttributes *
                                       server_info,
                                       ConnectionAttributes *
                                       client_info, HostDBInfo * host_db_info, HttpConfigParams * config_params)
{
  ////////////////////////////////////////////////////////
  // Set the keep-alive and version flags for later use //
  // in request construction                            //
  // this is also used when opening a connection to     //
  // the origin server, and search_keepalive_to().      //
  ////////////////////////////////////////////////////////

  bool force_http11 = false;
  bool http11_if_hostdb = false;

  switch (s->http_config_param->send_http11_requests) {
  case HttpConfigParams::SEND_HTTP11_NEVER:
    // No need to do anything since above vars
    //   are defaulted false
    break;
  case HttpConfigParams::SEND_HTTP11_ALWAYS:
    force_http11 = true;
    break;
  case HttpConfigParams::SEND_HTTP11_UPGRADE_HOSTDB:
    http11_if_hostdb = true;
    break;
  default:
    ink_assert(0);
    // FALL THROUGH
  case HttpConfigParams::SEND_HTTP11_IF_REQUEST_11_AND_HOSTDB:
    if (s->hdr_info.client_request.version_get() == HTTPVersion(1, 1)) {
      http11_if_hostdb = true;
    }
    break;
  }

  if (force_http11 == true ||
      (http11_if_hostdb == true &&
       host_db_info->app.http_data.http_version == HostDBApplicationInfo::HTTP_VERSION_11)) {
    server_info->http_version.set(1, 1);
    server_info->keep_alive = HTTP_PIPELINE;
  } else if (host_db_info->app.http_data.http_version == HostDBApplicationInfo::HTTP_VERSION_10) {

    server_info->http_version.set(1, 0);
    server_info->keep_alive = HTTP_KEEPALIVE;
  } else if (host_db_info->app.http_data.http_version == HostDBApplicationInfo::HTTP_VERSION_09) {

    server_info->http_version.set(0, 9);
    server_info->keep_alive = HTTP_NO_KEEPALIVE;
  } else {
    //////////////////////////////////////////////
    // not set yet for this host. set defaults. //
    //////////////////////////////////////////////
    server_info->http_version.set(1, 0);
    server_info->keep_alive = HTTP_KEEPALIVE;
    host_db_info->app.http_data.http_version = HostDBApplicationInfo::HTTP_VERSION_10;
  }

  /////////////////////////////
  // origin server keep_alive //
  /////////////////////////////
  if ((!config_params->keep_alive_enabled) || (config_params->origin_server_pipeline == 0)) {
    server_info->keep_alive = HTTP_NO_KEEPALIVE;
  }
  ///////////////////////////////
  // keep_alive w/o pipelining  //
  ///////////////////////////////
  if ((server_info->keep_alive == HTTP_PIPELINE) && (config_params->origin_server_pipeline <= 1)) {
    server_info->keep_alive = HTTP_KEEPALIVE;
  }

  return;
}

//////////////////////////////////////////////////////////////////////////
//
// void HttpTransact::handle_msie_reload_badness(...)
//
// Microsoft Internet Explorer has a design flaw that is exposed with
// transparent proxies.  The reload button only generates no-cache
// headers when there is an explicit proxy.  When going to a reverse
// proxy or when being transparently intercepted, no no-cache header is
// added.  This means state content cannot be reloaded with MSIE.
//
// This routine attempts to provide some knobs to improve the situation
// by explicitly adding no-cache to requests in certain scenarios.  In
// the future we might want to adjust freshness lifetimes for MSIE
// browsers.  Hopefully Microsoft will fix this in future versions of
// their browser, and then we can conditionally test version numbers.
//
// Here are the options available:
//   0: never add no-cache headers to MSIE requests
//   1: add no-cache headers to IMS MSIE requests
//   2: add no-cache headers to all MSIE requests
// 
//////////////////////////////////////////////////////////////////////////

void
HttpTransact::handle_msie_reload_badness(State * s, HTTPHdr * client_request)
{
  int user_agent_value_len;
  int has_ua_msie, has_no_cache, has_ims;
  const char *user_agent_value, *c, *e;
  const HttpConfigParams *config = s->http_config_param;

  // fastpath (see INKqa09624)
  if (config->cache_when_to_add_no_cache_to_msie_requests < 0)
    return;

  //////////////////////////////////////////////
  // figure out if User-Agent contains "MSIE" //
  //////////////////////////////////////////////

  has_ua_msie = 0;
  user_agent_value = client_request->value_get(MIME_FIELD_USER_AGENT, MIME_LEN_USER_AGENT, &user_agent_value_len);
  if (user_agent_value && user_agent_value_len >= 4) {
    c = user_agent_value;
    e = c + user_agent_value_len - 4;
    while (1) {
      c = (const char *) memchr(c, 'M', e - c);
      if (c == NULL)
        break;
      if ((c[1] == 'S') && (c[2] == 'I') && (c[3] == 'E')) {
        has_ua_msie = 1;
        break;
      }
      c++;
    }
  }
  ///////////////////////////////////////
  // figure out if no-cache and/or IMS //
  ///////////////////////////////////////

  has_no_cache = (client_request->is_pragma_no_cache_set() ||
                  (client_request->is_cache_control_set(HTTP_VALUE_NO_CACHE)));
  has_ims = (client_request->presence(MIME_PRESENCE_IF_MODIFIED_SINCE) != 0);

  /////////////////////////////////////////////////////////
  // increment some stats based on these three variables //
  /////////////////////////////////////////////////////////

  switch ((has_ims ? 4 : 0) + (has_no_cache ? 2 : 0) + (has_ua_msie ? 1 : 0)) {
  case 0:
    HTTP_INCREMENT_TRANS_STAT(http_request_taxonomy_i0_n0_m0_stat);
    break;
  case 1:
    HTTP_INCREMENT_TRANS_STAT(http_request_taxonomy_i0_n0_m1_stat);
    break;
  case 2:
    HTTP_INCREMENT_TRANS_STAT(http_request_taxonomy_i0_n1_m0_stat);
    break;
  case 3:
    HTTP_INCREMENT_TRANS_STAT(http_request_taxonomy_i0_n1_m1_stat);
    break;
  case 4:
    HTTP_INCREMENT_TRANS_STAT(http_request_taxonomy_i1_n0_m0_stat);
    break;
  case 5:
    HTTP_INCREMENT_TRANS_STAT(http_request_taxonomy_i1_n0_m1_stat);
    break;
  case 6:
    HTTP_INCREMENT_TRANS_STAT(http_request_taxonomy_i1_n1_m0_stat);
    break;
  case 7:
    HTTP_INCREMENT_TRANS_STAT(http_request_taxonomy_i1_n1_m1_stat);
    break;
  }

  //////////////////////////////////////////////////////////////////
  // if MSIE no-cache addition is disabled, or this isn't an      //
  //  MSIE browser, or if no-cache is already set, get outta here //
  //////////////////////////////////////////////////////////////////

  if ((config->cache_when_to_add_no_cache_to_msie_requests == 0) || (!has_ua_msie) || has_no_cache) {
    return;
  }
  //////////////////////////////////////////////////////
  // add a no-cache if mode and circumstances warrant //
  //////////////////////////////////////////////////////

  if ((config->cache_when_to_add_no_cache_to_msie_requests == 2) ||
      ((config->cache_when_to_add_no_cache_to_msie_requests == 1) && has_ims)) {
    client_request->value_append(MIME_FIELD_PRAGMA, MIME_LEN_PRAGMA, "no-cache", 8, true);
  }
}                               /* End HttpTransact::handle_msie_reload_badness */


void
HttpTransact::add_client_ip_to_outgoing_request(State * s, HTTPHdr * request)
{
  char ip_string[32];
  size_t ip_string_size;
  bool client_ip_set;
  unsigned char *p = (unsigned char *) &(s->client_info.ip);

  if (unlikely(!p))
    return;

  // Always prepare the IP string. ip_to_str() expects host order instead of network order
  if (LogUtils::ip_to_str(ntohl(s->client_info.ip), ip_string + 1, 30, &ip_string_size) == 0) {
    ip_string[0] = ' ';         // Leading space always, in case we need to concatenate this IP
    ip_string_size += 1;
  } else {
    // Failure, omg
    ip_string_size = 0;
    ip_string[0] = 0;
  }

  ////////////////////////////////////////////////////////////////
  // if we want client-ip headers, and there isn't one, add one //
  ////////////////////////////////////////////////////////////////
  if ((s->http_config_param->anonymize_insert_client_ip) && (!s->http_config_param->anonymize_remove_client_ip)) {
    client_ip_set = request->presence(MIME_PRESENCE_CLIENT_IP);
    Debug("http_trans", "client_ip_set = %d", client_ip_set);

    if (!client_ip_set && ip_string_size > 1) {
      request->value_set(MIME_FIELD_CLIENT_IP, MIME_LEN_CLIENT_IP, ip_string + 1, ip_string_size - 1);
      Debug("http_trans", "inserted request header 'Client-ip: %s'", ip_string + 1);
    }
  }

  if (s->http_config_param->insert_squid_x_forwarded_for) {
    // Use insert an extra space in the front so we're append,
    //   everything looks ok.  If we're not appending, we'll
    //   skip over it
    if (ip_string_size > 1) {
      MIMEField *x_for;

      if (s->http_config_param->insert_squid_x_forwarded_for) {
        if ((x_for = request->field_find(MIME_FIELD_X_FORWARDED_FOR, MIME_LEN_X_FORWARDED_FOR)) != 0) {
          // My undersanding is that X-Forwarded header does
          // not use comma to separate tokens...
          // but...
          //  According to http://www.openinfo.co.uk/apache/
          //  "If a request has passed through multiple proxies then the X-Forwarded-For may
          //   contain several IPs like this: X-Forwarded-For: client1, proxy1, proxy2 "
          //
          // I am going to fix it.
          //
          //request->field_value_append(x_for, ip_string, ip_string_size, false);    // false => no comma
          request->field_value_append(x_for, ip_string, ip_string_size, true);  // true => comma must be inserted
        } else {
          request->value_set(MIME_FIELD_X_FORWARDED_FOR, MIME_LEN_X_FORWARDED_FOR, ip_string + 1, ip_string_size - 1);
        }
        Debug("http_trans",
              "[add_client_ip_to_outgoing_request] Appended connecting client's "
              "(%s) to the X-Forwards header", ip_string + 1);
      }
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
// Name       : check_request_validity()
// Description: checks to see if incoming request has necessary fields
//
// Input      : State, header (can we do this without the state?)
// Output     : enum RequestError_t of the error type, if any
//
// Details    :
//
//
///////////////////////////////////////////////////////////////////////////////
HttpTransact::RequestError_t HttpTransact::check_request_validity(State * s, HTTPHdr * incoming_hdr)
{

  if (incoming_hdr == 0) {
    return NON_EXISTANT_REQUEST_HEADER;
  }

  if (!(HttpTransactHeaders::is_request_proxy_authorized(incoming_hdr))) {
    return FAILED_PROXY_AUTHORIZATION;
  }

  int
    hostname_len;
  URL *
    incoming_url;
  incoming_url = incoming_hdr->url_get();
  const char *
    hostname = incoming_url->host_get(&hostname_len);
  if (hostname == NULL) {
    return MISSING_HOST_FIELD;
  }

  if (hostname_len >= MAXDNAME) {
    return BAD_HTTP_HEADER_SYNTAX;
  }

  int
    scheme = incoming_url->scheme_get_wksidx();
  int
    method = incoming_hdr->method_get_wksidx();

  if (!((scheme == URL_WKSIDX_HTTP) && (method == HTTP_WKSIDX_GET))) {

    if (scheme != URL_WKSIDX_HTTP && scheme != URL_WKSIDX_HTTPS &&
#ifndef INK_NO_FTP
        scheme != URL_WKSIDX_FTP &&
#endif
        method != HTTP_WKSIDX_CONNECT) {
      if (scheme < 0) {
        return NO_REQUEST_SCHEME;
      } else {
        return SCHEME_NOT_SUPPORTED;
      }
    }

    if (!HttpTransactHeaders::is_this_method_supported(scheme, method)) {
      return ((scheme == URL_WKSIDX_FTP) ? FTP_METHOD_NOT_SUPPORTED : METHOD_NOT_SUPPORTED);
    }
    if ((method == HTTP_WKSIDX_CONNECT) && (!is_ssl_port_ok(s, incoming_hdr->url_get()->port_get()))) {

      return BAD_SSL_PORT;
    }

    if ((scheme == URL_WKSIDX_HTTP || scheme == URL_WKSIDX_HTTPS) &&
        (method == HTTP_WKSIDX_POST || method == HTTP_WKSIDX_PUSH || method == HTTP_WKSIDX_PUT)) {
      if (scheme == URL_WKSIDX_HTTP && !incoming_hdr->presence(MIME_PRESENCE_CONTENT_LENGTH)) {
        bool
          chunked_encoding = false;
        if (incoming_hdr->presence(MIME_PRESENCE_TRANSFER_ENCODING)) {
          MIMEField *
            field = incoming_hdr->field_find(MIME_FIELD_TRANSFER_ENCODING,
                                             MIME_LEN_TRANSFER_ENCODING);

          HdrCsvIter
            enc_val_iter;
          int
            enc_val_len;
          const char *
            enc_value = enc_val_iter.get_first(field, &enc_val_len);

          while (enc_value) {
            const char *
              wks_value = hdrtoken_string_to_wks(enc_value, enc_val_len);
            if (wks_value == HTTP_VALUE_CHUNKED) {
              chunked_encoding = true;
              break;
            }
            enc_value = enc_val_iter.get_next(&enc_val_len);
          }
        }

        if (!chunked_encoding)
          return NO_POST_CONTENT_LENGTH;
        else
          s->client_info.transfer_encoding = CHUNKED_ENCODING;
      }
    }
  }
  // Check whether a Host header field is missing in the request.
  if (!incoming_hdr->presence(MIME_PRESENCE_HOST) && incoming_hdr->version_get() != HTTPVersion(0, 9)) {
    // Update the number of incoming 1.0 or 1.1 requests that do 
    // not contain Host header fields.  
    HTTP_INCREMENT_TRANS_STAT(http_missing_host_hdr_stat);
  }
  // Did the client send a "TE: identity;q=0"? We have to respond
  // with an error message because we only support identity
  // Transfer Encoding.

  if (incoming_hdr->presence(MIME_PRESENCE_TE)) {
    MIMEField *
      te_field = incoming_hdr->field_find(MIME_FIELD_TE, MIME_LEN_TE);
    HTTPValTE *
      te_val;

    if (te_field) {
      HdrCsvIter
        csv_iter;

      int
        te_raw_len;
      const char *
        te_raw = csv_iter.get_first(te_field, &te_raw_len);
      while (te_raw) {
        te_val = http_parse_te(te_raw, te_raw_len, &s->arena);
        if (te_val->encoding == HTTP_VALUE_IDENTITY) {
          if (te_val->qvalue <= 0.0) {
            s->arena.free(te_val, sizeof(HTTPValTE));
            return UNACCEPTABLE_TE_REQUIRED;
          }
        }
        s->arena.free(te_val, sizeof(HTTPValTE));
        te_raw = csv_iter.get_next(&te_raw_len);
      }
    }
  }

  return NO_REQUEST_HEADER_ERROR;
}

HttpTransact::ResponseError_t HttpTransact::check_ftp_response_validity(State * s)
{
  ink_assert(s->next_hop_scheme == URL_WKSIDX_FTP);

  // request may have required authorization information.
  if (s->ftp_info->last_error == EFTP_LOGIN_INCORRECT) {
    return FTP_LOGIN_INCORRECT;
  }
  // connection problem. ftp open failed. not much else
  // to do, but bail out.
  if (s->current.state == FTP_OPEN_FAILED) {
    return FTP_CONNECTION_OPEN_FAILED;

  }
  return NO_RESPONSE_HEADER_ERROR;
}

HttpTransact::ResponseError_t HttpTransact::check_response_validity(State * s, HTTPHdr * incoming_hdr)
{

  ink_assert(s->next_hop_scheme == URL_WKSIDX_HTTP || s->next_hop_scheme == URL_WKSIDX_HTTPS);

  if (incoming_hdr == 0) {
    return NON_EXISTANT_RESPONSE_HEADER;
  }

  if (incoming_hdr->type_get() != HTTP_TYPE_RESPONSE) {
    return NOT_A_RESPONSE_HEADER;
  }
  // If the response is 0.9 then there is no status
  //   code or date
  if (did_forward_server_send_0_9_response(s) == TRUE) {
    return NO_RESPONSE_HEADER_ERROR;
  }

  HTTPStatus
    incoming_status = incoming_hdr->status_get();
  if (!incoming_status) {
    return MISSING_STATUS_CODE;
  }

  if (incoming_status == HTTP_STATUS_INTERNAL_SERVER_ERROR) {
    return STATUS_CODE_SERVER_ERROR;
  }

  if (!incoming_hdr->presence(MIME_PRESENCE_DATE)) {
    incoming_hdr->set_date(s->current.now);
  }
//     if (! incoming_hdr->get_reason_phrase()) {
//      return MISSING_REASON_PHRASE;
//     }

#ifdef REALLY_NEED_TO_CHECK_DATE_VALIDITY

  if (incoming_hdr->presence(MIME_PRESENCE_DATE)) {
    time_t
      date_value = incoming_hdr->get_date();
    if (date_value <= 0) {

// following lines commented out because of performance
// concerns
//          if (s->http_config_param->errors_log_error_pages) {
//              const char *date_string =
//                    incoming_hdr->value_get(MIME_FIELD_DATE);
//              Log::error ("Incoming response has bogus date value: %d: %s",
//                          date_value, date_string ? date_string : "(null)");
//          }

      Debug("http_trans", "[check_response_validity] Bogus date in response");
      return BOGUS_OR_NO_DATE_IN_RESPONSE;
    }
  } else {
    Debug("http_trans", "[check_response_validity] No date in response");
    return BOGUS_OR_NO_DATE_IN_RESPONSE;
  }
#endif

  return NO_RESPONSE_HEADER_ERROR;
}

bool
HttpTransact::did_forward_server_send_0_9_response(State * s)
{
  if (s->hdr_info.server_response.version_get() == HTTPVersion(0, 9)) {
    s->current.server->http_version.set(0, 9);
    return TRUE;
  }
  return FALSE;
}

bool
HttpTransact::handle_internal_request(State * s, HTTPHdr * incoming_hdr)
{
#ifdef INK_NO_STAT_PAGES
  return false;
#else

  URL *url;

  HTTP_DEBUG_ASSERT(incoming_hdr->type_get() == HTTP_TYPE_REQUEST);

  if (incoming_hdr->method_get_wksidx() != HTTP_WKSIDX_GET) {
    return false;
  }

  url = incoming_hdr->url_get();

  int scheme = url->scheme_get_wksidx();
  if (scheme != URL_WKSIDX_HTTP && scheme != URL_WKSIDX_HTTPS) {
    return false;
  }

  if (!statPagesManager.is_stat_page(url)) {
    return false;
  }

  return true;
#endif //INK_NO_STAT_PAGES
}

bool
HttpTransact::handle_trace_and_options_requests(State * s, HTTPHdr * incoming_hdr)
{
  HTTP_DEBUG_ASSERT(incoming_hdr->type_get() == HTTP_TYPE_REQUEST);
  if (s->method == HTTP_WKSIDX_GET)
    return false;

  if (s->method == HTTP_WKSIDX_TRACE) {
    HTTP_INCREMENT_TRANS_STAT(http_trace_requests_stat);
  } else if (s->method == HTTP_WKSIDX_OPTIONS) {
    HTTP_INCREMENT_TRANS_STAT(http_options_requests_stat);
  } else {
    return false;
  }

  // If there is no Max-Forwards request header, just return false.
  if (!incoming_hdr->presence(MIME_PRESENCE_MAX_FORWARDS)) {
    // Trace and Options requests should not be looked up in cache.
    // s->cache_info.action = CACHE_DO_NO_ACTION;
    s->current.mode = TUNNELLING_PROXY;
    HTTP_INCREMENT_TRANS_STAT(http_tunnels_stat);
    return FALSE;
  }

  int max_forwards = incoming_hdr->get_max_forwards();
  if (max_forwards <= 0) {
    //////////////////////////////////////////////
    // if max-forward is 0 the request must not //
    // be forwarded to the origin server.       //
    //////////////////////////////////////////////
    Debug("http_trans", "[handle_trace] max-forwards: 0, building response...");
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
    build_response(s, &s->hdr_info.client_response, s->client_info.http_version, HTTP_STATUS_OK);

    ////////////////////////////////////////
    // if method is trace we should write //
    // the request header as the body.    //
    ////////////////////////////////////////
    if (s->method == HTTP_WKSIDX_TRACE) {
      Debug("http_trans", "[handle_trace] inserting request in body.");
      int req_length = incoming_hdr->length_get();
      HTTP_RELEASE_ASSERT(req_length > 0);

      s->internal_msg_buffer_index = 0;
      s->internal_msg_buffer_size = req_length * 2;
      if (s->internal_msg_buffer) {
        free_internal_msg_buffer(s->internal_msg_buffer, s->internal_msg_buffer_fast_allocator_size);
      }

      if (s->internal_msg_buffer_size <= max_iobuffer_size) {
        s->internal_msg_buffer_fast_allocator_size = buffer_size_to_index(s->internal_msg_buffer_size);
        s->internal_msg_buffer = (char *) ioBufAllocator[s->internal_msg_buffer_fast_allocator_size].alloc_void();
      } else {
        s->internal_msg_buffer_fast_allocator_size = -1;
        s->internal_msg_buffer = (char *) xmalloc(s->internal_msg_buffer_size);
      }

      // clear the stupid buffer
      memset(s->internal_msg_buffer, '\0', s->internal_msg_buffer_size);

      int offset = 0;
      int used = 0;
      int done;
      done = incoming_hdr->print(s->internal_msg_buffer, s->internal_msg_buffer_size, &used, &offset);
      HTTP_RELEASE_ASSERT(done);
      s->internal_msg_buffer_size = used;

      s->hdr_info.client_response.set_content_length(used);
    } else {
      // For OPTIONS request insert supported methods in ALLOW field
      Debug("http_trans", "[handle_options] inserting methods in Allow.");
      HttpTransactHeaders::insert_supported_methods_in_response(&s->hdr_info.client_response, s->scheme);

    }
    return TRUE;
  } else {                      /* max-forwards != 0 */

    if ((max_forwards <= 0) || (max_forwards > MAXINT)) {
      if (!s->traffic_net_req) {
        Log::error("HTTP: snapping invalid max-forwards value %d to %d", max_forwards, MAXINT);
      }
      max_forwards = MAXINT;
    }

    --max_forwards;
    Debug("http_trans", "[handle_trace_options] Decrementing max_forwards to %d", max_forwards);
    incoming_hdr->set_max_forwards(max_forwards);

    // Trace and Options requests should not be looked up in cache.
    // s->cache_info.action = CACHE_DO_NO_ACTION;
    s->current.mode = TUNNELLING_PROXY;
    HTTP_INCREMENT_TRANS_STAT(http_tunnels_stat);
  }

  return FALSE;
}

void
HttpTransact::initialize_state_variables_for_origin_server(State * s, HTTPHdr * incoming_request, bool second_time)
{
  if (s->server_info.name && !second_time) {
    ink_assert(s->server_info.port != 0);
  }

  int host_len;
  const char *host = incoming_request->url_get()->host_get(&host_len);
  s->server_info.name = s->arena.str_store(host, host_len);
  s->server_info.port = incoming_request->url_get()->port_get();

  if (second_time) {
    s->dns_info.attempts = 0;
    s->dns_info.lookup_name = s->server_info.name;
  }
}

void
HttpTransact::bootstrap_state_variables_from_request(State * s, HTTPHdr * incoming_request)
{
  s->current.now = s->client_request_time = ink_cluster_time();
  s->client_info.http_version = incoming_request->version_get();
}

void
HttpTransact::initialize_state_variables_from_request(State * s, HTTPHdr * incoming_request)
{
  // check if the request is conditional (IMS or INM)
  if (incoming_request->presence(MIME_PRESENCE_IF_MODIFIED_SINCE | MIME_PRESENCE_IF_NONE_MATCH)) {
    SET_VIA_STRING(VIA_CLIENT_REQUEST, VIA_CLIENT_IMS);
  } else {
    SET_VIA_STRING(VIA_CLIENT_REQUEST, VIA_CLIENT_SIMPLE);
  }

  // Is the user agent Keep-Alive?
  //  If we are transparent or if the user-agent is following
  //  the 1.1 spec, we will see a "Connection" header to
  //  indicate a keep-alive.  However most user-agents including
  //  MSIE3.0, Netscape4.04 and Netscape3.01 send Proxy-Connection
  //  when they are configured to use a proxy.  Proxy-Connection
  //  is not in the spec but was added to prevent problems
  //  with a dumb proxy forwarding all headers (including "Connection")
  //  to the origin server and confusing it.  However, the 
  //  "Proxy-Connection" solution breaks down with transparent
  //  backbone caches since the request could be from dumb
  //  downstream caches that are forwarding the "Proxy-Connection"
  //  header.  Therefore, we disable keep-alive if we are transparent
  //  and see "Proxy-Connection" header
  //
  MIMEField *pc = incoming_request->field_find(MIME_FIELD_PROXY_CONNECTION,
                                               MIME_LEN_PROXY_CONNECTION);

  if (!s->http_config_param->keep_alive_enabled || (s->http_config_param->transparency_enabled && pc != NULL)) {
    s->client_info.keep_alive = HTTP_NO_KEEPALIVE;

    // If we need to send a close header later,
    //   check to see if it should be "Proxy-Connection"
    if (pc != NULL) {
      s->client_info.proxy_connect_hdr = true;
    }
  } else {
    // If there is a Proxy-Connection header use that,
    //   otherwise use the Connection header
    if (pc != NULL) {
      s->client_info.keep_alive = is_header_keep_alive(s->client_info.http_version, s->client_info.http_version, pc);
      s->client_info.proxy_connect_hdr = true;
    } else {
      MIMEField *c = incoming_request->field_find(MIME_FIELD_CONNECTION,
                                                  MIME_LEN_CONNECTION);

      s->client_info.keep_alive = is_header_keep_alive(s->client_info.http_version, s->client_info.http_version, c);
    }
  }

  if (s->client_info.keep_alive == HTTP_KEEPALIVE && s->client_info.http_version == HTTPVersion(1, 1)) {
    s->client_info.pipeline_possible = true;
  }

  if (s->http_config_param->log_spider_codes) {
    HTTPVersion uver = s->client_info.http_version;
    if (uver != HTTPVersion(1, 0) && uver != HTTPVersion(1, 1) && uver != HTTPVersion(0, 9)) {
      // this probably will be overwriten later if the server accepts
      // unsupported versions
      s->squid_codes.wuts_proxy_status_code = WUTS_PROXY_STATUS_SPIDER_UNSUPPORTED_HTTP_VERSION;
    }
  }

  if (!s->server_info.name || s->redirect_info.redirect_in_process) {
    int host_len;
    const char *host = incoming_request->url_get()->host_get(&host_len);
    s->server_info.name = s->arena.str_store(host, host_len);
    s->server_info.port = incoming_request->url_get()->port_get();
  } else {
    HTTP_DEBUG_ASSERT(s->server_info.port != 0);
  }

  s->next_hop_scheme = s->scheme = incoming_request->url_get()->scheme_get_wksidx();
  s->method = incoming_request->method_get_wksidx();

  if (s->method == HTTP_WKSIDX_GET) {
    HTTP_INCREMENT_TRANS_STAT(http_get_requests_stat);
  } else if (s->method == HTTP_WKSIDX_HEAD) {
    HTTP_INCREMENT_TRANS_STAT(http_head_requests_stat);
  } else if (s->method == HTTP_WKSIDX_POST) {
    HTTP_INCREMENT_TRANS_STAT(http_post_requests_stat);
  } else if (s->method == HTTP_WKSIDX_PUT) {
    HTTP_INCREMENT_TRANS_STAT(http_put_requests_stat);
  } else if (s->method == HTTP_WKSIDX_CONNECT) {
    HTTP_INCREMENT_TRANS_STAT(http_connect_requests_stat);
  } else if (s->method == HTTP_WKSIDX_DELETE) {
    HTTP_INCREMENT_TRANS_STAT(http_delete_requests_stat);
  } else if (s->method == HTTP_WKSIDX_PURGE) {
    HTTP_INCREMENT_TRANS_STAT(http_purge_requests_stat);
  } else if (s->method == HTTP_WKSIDX_TRACE) {
    HTTP_INCREMENT_TRANS_STAT(http_trace_requests_stat);
  } else if (s->method == HTTP_WKSIDX_PUSH) {
    HTTP_INCREMENT_TRANS_STAT(http_push_requests_stat);
  } else {
    HTTP_INCREMENT_TRANS_STAT(http_extension_method_requests_stat);
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_METHOD);
    s->squid_codes.log_code = SQUID_LOG_TCP_MISS;
    s->hdr_info.extension_method = true;
  }

  ////////////////////////////////////////////////
  // get request content length for POST or PUT //
  ////////////////////////////////////////////////
  if ((s->method != HTTP_WKSIDX_GET) &&
      (s->method == HTTP_WKSIDX_POST || s->method == HTTP_WKSIDX_PUT ||
       s->method == HTTP_WKSIDX_PUSH || s->hdr_info.extension_method)) {

    int length = incoming_request->get_content_length();
    s->hdr_info.request_content_length = (length >= 0) ? length : HTTP_UNDEFINED_CL;    // content length less than zero is invalid

    Debug("http_trans", "[init_stat_vars_from_req] set req cont length to %d", s->hdr_info.request_content_length);

  } else {
    s->hdr_info.request_content_length = 0;
  }
  // if transfer encoding is chunked content length is undefined
  if (incoming_request->presence(MIME_PRESENCE_TRANSFER_ENCODING)) {
    MIMEField *field = incoming_request->field_find(MIME_FIELD_TRANSFER_ENCODING,
                                                    MIME_LEN_TRANSFER_ENCODING);

    HdrCsvIter enc_val_iter;
    int enc_val_len;
    const char *enc_value = enc_val_iter.get_first(field, &enc_val_len);

    while (enc_value) {

      const char *wks_value = hdrtoken_string_to_wks(enc_value, enc_val_len);

      if (wks_value == HTTP_VALUE_CHUNKED)
        s->hdr_info.request_content_length = HTTP_UNDEFINED_CL;
      enc_value = enc_val_iter.get_next(&enc_val_len);
    }
  }
  int host_len;
  const char *hostname = s->hdr_info.client_request.url_get()->host_get(&host_len);
  s->request_data.hdr = &s->hdr_info.client_request;
  s->request_data.hostname_str = s->arena.str_store(hostname, host_len);
  s->request_data.src_ip = s->client_info.ip;
  s->request_data.dest_ip = 0;
  if (s->state_machine->ua_session) {
    s->request_data.incoming_port = (inku16) ntohs(s->state_machine->ua_session->get_netvc()->get_local_port());
  }
  s->request_data.xact_start = s->client_request_time;
  s->request_data.api_info = &s->api_info;

  /////////////////////////////////////////////
  // Do dns lookup for the host. We need     //
  // the expanded host for cache lookup, and //
  // the host ip for reverse proxy.          //
  /////////////////////////////////////////////
  s->dns_info.looking_up = ORIGIN_SERVER;
  s->dns_info.attempts = 0;
  s->dns_info.lookup_name = s->server_info.name;
}

void
HttpTransact::initialize_state_variables_from_response(State * s, HTTPHdr * incoming_response)
{
  /* check if the server permits caching */
  s->cache_info.directives.does_server_permit_storing =
    HttpTransactHeaders::does_server_allow_response_to_be_stored(&s->hdr_info.server_response);

  /*
   * A stupid moronic broken pathetic excuse
   *   for a server may send us a keep alive response even
   *   if we sent "Connection: close"  We need check the response
   *   header regardless of what we sent to the server
   */
  MIMEField *c_hdr;
  if ((s->current.request_to != ORIGIN_SERVER) &&
      (s->current.request_to == PARENT_PROXY ||
       s->current.request_to == ICP_SUGGESTED_HOST)) {
    c_hdr = s->hdr_info.server_response.field_find(MIME_FIELD_PROXY_CONNECTION, MIME_LEN_PROXY_CONNECTION);

    // If there is a Proxy-Connection header use that,
    //   otherwise use the Connection header
    if (c_hdr == NULL) {
      c_hdr = s->hdr_info.server_response.field_find(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION);
    }
  } else {
    c_hdr = s->hdr_info.server_response.field_find(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION);
  }

  s->current.server->keep_alive =
    is_header_keep_alive(s->hdr_info.server_response.version_get(), s->hdr_info.server_request.version_get(), c_hdr);

  if (s->current.server->keep_alive == HTTP_KEEPALIVE) {
    if (!s->cop_test_page)
      Debug("http_hdrs", "[initialize_state_variables_from_response]" "Server is keep-alive.");
  }

  HTTPStatus status_code = incoming_response->status_get();
  if (is_response_body_precluded(status_code, s->method)) {

    s->hdr_info.response_content_length = 0;
    s->hdr_info.trust_response_cl = true;
  } else {
    ////////////////////////////////////////////////////////
    // this is the case that the origin server sent       //
    // us content length. Experience says that many       //
    // origin servers that do not support keep-alive      //
    // lie about the content length. to avoid truncation  //
    // of docuemnts from such server, if the server is    //
    // not keep-alive, we set to read until the server    //
    // closes the connection. We sent the maybe bogus     //
    // content length to the browser, and we will correct //
    // the cache if it turnrd out that the servers lied.  //
    ////////////////////////////////////////////////////////
    if (incoming_response->presence(MIME_PRESENCE_CONTENT_LENGTH)) {
      if (s->current.server->keep_alive == HTTP_NO_KEEPALIVE) {
        s->hdr_info.trust_response_cl = false;
        s->hdr_info.response_content_length = HTTP_UNDEFINED_CL;
      } else {
        int cl = incoming_response->get_content_length();
        s->hdr_info.response_content_length = (cl >= 0) ? cl : HTTP_UNDEFINED_CL;
        s->hdr_info.trust_response_cl = true;
      }
    } else {
      s->hdr_info.response_content_length = HTTP_UNDEFINED_CL;
      s->hdr_info.trust_response_cl = false;
    }
  }

  initialize_bypass_variables(s);

  if (incoming_response->presence(MIME_PRESENCE_TRANSFER_ENCODING)) {
    MIMEField *field = incoming_response->field_find(MIME_FIELD_TRANSFER_ENCODING,
                                                     MIME_LEN_TRANSFER_ENCODING);

    HdrCsvIter enc_val_iter;
    int enc_val_len;
    const char *enc_value = enc_val_iter.get_first(field, &enc_val_len);

    while (enc_value) {

      const char *wks_value = hdrtoken_string_to_wks(enc_value, enc_val_len);

      //   FIX ME: What is chunked appears more than once?  Old
      //     code didn't deal with this so I don't either
      if (wks_value == HTTP_VALUE_CHUNKED) {
        if (!s->cop_test_page)
          Debug("http_hdrs", "[init_state_vars_from_resp] transfer encoding: chunked!");
        s->current.server->transfer_encoding = CHUNKED_ENCODING;

        s->hdr_info.response_content_length = HTTP_UNDEFINED_CL;
        s->hdr_info.trust_response_cl = false;

        // OBJECTIVE: Since we are dechunking the request remove the 
        //   chunked value If this is the only value, we need to remove 
        //    the whole field.
        MIMEField *new_enc_field = NULL;
        HdrCsvIter new_enc_iter;
        int new_enc_len;
        const char *new_enc_val = new_enc_iter.get_first(field, &new_enc_len);

        // Loop over the all the values in existing Trans-enc header and
        //   copy the ones that aren't our chunked value to a new field
        while (new_enc_val) {
          if (new_enc_val != enc_value) {
            if (new_enc_field) {
              new_enc_field->value_append(incoming_response->m_heap,
                                          incoming_response->m_mime, new_enc_val, new_enc_len, true);
            } else {
              new_enc_field = incoming_response->field_create();
              incoming_response->field_value_set(new_enc_field, new_enc_val, new_enc_len);
            }
          }

          new_enc_val = new_enc_iter.get_next(&new_enc_len);
        }

        // We're done with the old field since we copied out everything
        //   we needed
        incoming_response->field_delete(field);

        // If there is a new field (ie: there was more than one
        //   transfer-encoding), insert it to the list
        if (new_enc_field) {
          new_enc_field->name_set(incoming_response->m_heap,
                                  incoming_response->m_mime, MIME_FIELD_TRANSFER_ENCODING, MIME_LEN_TRANSFER_ENCODING);
          incoming_response->field_attach(new_enc_field);
        }

        return;
      }                         //  if (enc_value == CHUNKED)

      enc_value = enc_val_iter.get_next(&enc_val_len);
    }
  }

  s->current.server->transfer_encoding = NO_TRANSFER_ENCODING;
}


bool
HttpTransact::is_cache_response_returnable(State * s)
{
  if (s->cache_control.never_cache) {
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_CONFIG);
    return false;
  }

  if (!s->cache_info.directives.does_client_permit_lookup) {
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_CLIENT);
    return false;
  }

  if (!HttpTransactHeaders::is_method_cacheable(s->method)) {
    SET_VIA_STRING(VIA_CACHE_RESULT, VIA_IN_CACHE_NOT_ACCEPTABLE);
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_METHOD);
    return false;
  }
  // If cookies in response and no TTL set, we do not cache the doc
  if ((s->cache_control.ttl_in_cache <= 0) &&
      do_cookies_prevent_caching((int) s->http_config_param->
                                 cache_responses_to_cookies,
                                 &s->hdr_info.client_request,
                                 s->cache_info.object_read->response_get(), s->cache_info.object_read->request_get())) {
    SET_VIA_STRING(VIA_CACHE_RESULT, VIA_IN_CACHE_NOT_ACCEPTABLE);
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_COOKIE);
    return false;
  }

  return true;
}

///////////////////////////////////////////////////////////////////////////////
// Name       : is_stale_cache_response_returnable()
// Description: check if a stale cached response is returnable to a client
//
// Input      : State
// Output     : true or false
//
// Details    :
//
///////////////////////////////////////////////////////////////////////////////
bool
HttpTransact::is_stale_cache_response_returnable(State * s)
{
  HTTPHdr *cached_response = s->cache_info.object_read->response_get();

  // First check if client allows cached response
  // Note does_client_permit_lookup was set to
  // does_client_Request_permit_cached_response()
  // in update_cache_control_information_from_config().
  if (!s->cache_info.directives.does_client_permit_lookup) {
    return false;
  }
  // Spec says that we can not serve a stale document with a
  //   "must-revalidate header"
  // How about "s-maxage" and "no-cache" directives?
  inku32 cc_mask;
  cc_mask = (MIME_COOKED_MASK_CC_MUST_REVALIDATE |
             MIME_COOKED_MASK_CC_PROXY_REVALIDATE |
             MIME_COOKED_MASK_CC_NEED_REVALIDATE_ONCE |
             MIME_COOKED_MASK_CC_NO_CACHE | MIME_COOKED_MASK_CC_NO_STORE | MIME_COOKED_MASK_CC_S_MAXAGE);
  if ((cached_response->get_cooked_cc_mask() & cc_mask) || cached_response->is_pragma_no_cache_set()) {
    Debug("http_trans", "[is_stale_cache_response_returnable] " "document headers prevent serving stale");
    return false;
  }
  // See how old the document really is.  We don't want create a
  //   stale content museum of doucments that are no longer available
  int current_age = HttpTransactHeaders::calculate_document_age(s->cache_info.object_read->request_sent_time_get(),
                                                                s->cache_info.object_read->response_received_time_get(),
                                                                cached_response,
                                                                cached_response->get_date(),
                                                                s->current.now);
  if (current_age > s->http_config_param->cache_max_stale_age) {
    Debug("http_trans", "[is_stale_cache_response_returnable] " "document age is too large %d", current_age);
    return false;
  }
  // If the stale document requires authorization, we can't return it either.
  Authentication_t
    auth_needed = AuthenticationNeeded(s->http_config_param, &s->hdr_info.client_request, cached_response);
  if (auth_needed != AUTHENTICATION_SUCCESS) {
    Debug("http_trans", "[is_stale_cache_response_returnable] " "authorization prevent serving stale");
    return false;
  }

  Debug("http_trans", "[is_stale_cache_response_returnable] can serve stale");
  return true;
}


bool
HttpTransact::url_looks_dynamic(URL * url)
{
  const char *p_start, *p, *t;
  static const char *asp = ".asp";
  const char *part;
  int part_length;

  if (url->scheme_get_wksidx() != URL_WKSIDX_HTTP && url->scheme_get_wksidx() != URL_WKSIDX_HTTPS) {
    return false;
  }
  ////////////////////////////////////////////////////////////
  // (1) If URL contains query stuff in it, call it dynamic //
  ////////////////////////////////////////////////////////////

  part = url->params_get(&part_length);
  if (part != NULL) {
    return true;
  }
  part = url->query_get(&part_length);
  if (part != NULL) {
    return true;
  }
  ///////////////////////////////////////////////
  // (2) If path ends in "asp" call it dynamic //
  ///////////////////////////////////////////////

  part = url->path_get(&part_length);
  if (part) {
    p = &part[part_length - 1];
    t = &asp[3];

    while (p != part) {
      if (ParseRules::ink_tolower(*p) == ParseRules::ink_tolower(*t)) {
        p -= 1;
        t -= 1;
        if (t == asp)
          return true;
      } else
        break;
    }
  }
  /////////////////////////////////////////////////////////////////
  // (3) If the path of the url contains "cgi", call it dynamic. //
  /////////////////////////////////////////////////////////////////

  if (part && part_length >= 3) {
    for (p_start = part; p_start <= &part[part_length - 3]; p_start++) {
      if (((p_start[0] == 'c') || (p_start[0] == 'C')) &&
          ((p_start[1] == 'g') || (p_start[1] == 'G')) && ((p_start[2] == 'i') || (p_start[2] == 'I'))) {
        return (true);
      }
    }
  }

  return (false);
}


///////////////////////////////////////////////////////////////////////////////
// Name       : is_request_cache_lookupable()
// Description: check if a request should be looked up in cache
//
// Input      : State, request header
// Output     : true or false
//
// Details    :
//
//
///////////////////////////////////////////////////////////////////////////////
bool
HttpTransact::is_request_cache_lookupable(State * s, HTTPHdr * incoming)
{
  // ummm, someone has already decided that proxy should tunnel
  if (s->current.mode == TUNNELLING_PROXY) {
    return false;
  }
  // don't bother with remaining checks if we already did a cache lookup
  if (s->cache_info.lookup_count > 0) {
    return true;
  }
  // is cache turned on?
  if (!s->http_config_param->cache_http) {
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_CACHE_OFF);
    return false;
  }
  // GET, HEAD, POST, DELETE, and PUT are all cache lookupable
  if (!HttpTransactHeaders::is_method_cache_lookupable(s->method)) {
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_METHOD);
    return false;
  }
  // don't cache page if URL "looks dynamic" and this filter is enabled
  // We can do the check in is_response_cacheable() or here.
  // It may be more efficient if we are not going to cache dynamic looking urls
  // (the default config?) since we don't even need to do cache lookup.
  // So for the time being, it'll be left here.

  // If url looks dynamic but a ttl is set, request is cache lookupable
  if ((!s->http_config_param->cache_urls_that_look_dynamic) &&
      url_looks_dynamic(s->hdr_info.client_request.url_get()) && (s->cache_control.ttl_in_cache <= 0)) {

    // We do not want to forward the request for a dynamic URL onto the
    // origin server if the value of the Max-Forwards header is zero.
    int max_forwards = -1;
    if (s->hdr_info.client_request.presence(MIME_PRESENCE_MAX_FORWARDS)) {
      MIMEField *max_forwards_f = s->hdr_info.client_request.field_find(MIME_FIELD_MAX_FORWARDS,
                                                                        MIME_LEN_MAX_FORWARDS);
      if (max_forwards_f)
        max_forwards = max_forwards_f->value_get_int();
    }

    if (max_forwards != 0) {
      SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_URL);
      return false;
    }
  }
#if 0                           // ckwong (already fixed and no need this) see build_request()
  // if the request has authorization don't look up the cache
  //  Putting this request through the cachable path will cause
  //  conditional headers to be unecessarily stripped (INKqa04463)
  //  We don't cache responses that were obtained with authorization
  //  headers anyway.  Even if they come with Cache-Control: public
  //  see is_response_cacheable()
  if (s->hdr_info.client_request.presence(MIME_PRESENCE_AUTHORIZATION) && s->http_config_param->cache_ignore_auth == 0) {
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_AUTHORIZATION);
    return false;
  }
#endif

  // Don't cache if it's a RANGE request but the cache is not enabled for RANGE.
  if (!s->http_config_param->cache_range_lookup && s->hdr_info.client_request.presence(MIME_PRESENCE_RANGE)) {
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_HEADER_FIELD);
    return false;
  }

  if (s->api_skip_cache_lookup) {
    s->api_skip_cache_lookup = false;
    return false;
  }
  // Even with "no-cache" directive, we want to do a cache lookup
  // because we need to update our cached copy.
  // Client request "no-cache" directive is handle elsewhere:
  // update_cache_control_information_from_config()

  return true;
}


///////////////////////////////////////////////////////////////////////////////
// Name       : response_cacheable_indicated_by_cc()
// Description: check if a response is cacheable as indicated by Cache-Control
//
// Input      : Response header
// Output     : -1, 0, or +1
//
// Details    :
// (1) return -1 if cache control indicates response not cacheable,
//     ie, with no-store, or private directives;
// (2) return +1 if cache control indicates response cacheable
//     ie, with public, max-age, s-maxage, must-revalidate, or proxy-revalidate;
// (3) otherwise, return 0 if cache control does not indicate.
//
///////////////////////////////////////////////////////////////////////////////
int
response_cacheable_indicated_by_cc(HTTPHdr * response)
{
  inku32 cc_mask;
  // the following directives imply not cacheable
  cc_mask = (MIME_COOKED_MASK_CC_NO_STORE | MIME_COOKED_MASK_CC_PRIVATE);
  if (response->get_cooked_cc_mask() & cc_mask) {
    return -1;
  }
  // the following directives imply cacheable
  cc_mask = (MIME_COOKED_MASK_CC_PUBLIC |
             MIME_COOKED_MASK_CC_MAX_AGE |
             MIME_COOKED_MASK_CC_S_MAXAGE | MIME_COOKED_MASK_CC_MUST_REVALIDATE | MIME_COOKED_MASK_CC_PROXY_REVALIDATE);
  if (response->get_cooked_cc_mask() & cc_mask) {
    return 1;
  }
  // otherwise, no indication
  return 0;
}


///////////////////////////////////////////////////////////////////////////////
// Name       : is_response_cacheable()
// Description: check if a response is cacheable
//
// Input      : State, request header, response header
// Output     : true or false
//
// Details    :
//
///////////////////////////////////////////////////////////////////////////////
bool
HttpTransact::is_response_cacheable(State * s, HTTPHdr * request, HTTPHdr * response)
{

  NOWARN_UNUSED(s->http_config_param);

  // if method is not GET or HEAD, do not cache.
  // Note: POST is also cacheable with Expires or Cache-control.
  // but due to INKqa11567, we are not caching POST responses.
  // Basically, the problem is the resp for POST url1 req should not
  // be served to a GET url1 request, but we just match URL not method.
  int req_method = request->method_get_wksidx();
  if (!(HttpTransactHeaders::is_method_cacheable(req_method))) {
    Debug("http_trans", "[is_response_cacheable] " "only GET, and some HEAD and POST are cachable");
    return (false);
  }
  // Debug("http_trans", "[is_response_cacheable] method is cacheable");
  // If the request was not looked up in the cache, the response
  // should not be cached (same subsequent requests will not be
  // looked up, either, so why cache this).
  if (!(is_request_cache_lookupable(s, request))) {
    Debug("http_trans", "[is_response_cacheable] " "request is not cache lookupable, response is not cachable");
    return (false);
  }
  // already has a fresh copy in the cache
  if (s->range_setup == RANGE_NOT_HANDLED)
    return (false);

  // Check whether the response is cachable based on its cookie
  // If there are cookies in response but a ttl is set, allow caching 
  if ((s->cache_control.ttl_in_cache <= 0) &&
      do_cookies_prevent_caching((int) s->http_config_param->cache_responses_to_cookies, request, response)) {
    Debug("http_trans", "[is_response_cacheable] " "response has uncachable cookies, response is not cachable");
    return (false);
  }
  // if server spits back a WWW-Authenticate
  if (response->presence(MIME_PRESENCE_WWW_AUTHENTICATE) && s->http_config_param->cache_ignore_auth == 0) {
    Debug("http_trans", "[is_response_cacheable] " "response has WWW-Authenticate, response is not cachable");
    return (false);
  }
  // does server explicitly forbid storing?
  // If OS forbids storing but a ttl is set, allow caching
  if (!s->cache_info.directives.does_server_permit_storing &&
      !s->cache_control.ignore_server_no_cache && (s->cache_control.ttl_in_cache <= 0)) {
    Debug("http_trans",
          "[is_response_cacheable] "
          "server does not permit storing and config file does not "
          "indicate that server directive should be ignored");
    return (false);
  }
  // Debug("http_trans", "[is_response_cacheable] server permits storing");

  // does config explicitly forbit storing?
  // ttl overides other config parameters
  if ((!s->cache_info.directives.does_config_permit_storing &&
       !s->cache_control.ignore_server_no_cache &&
       (s->cache_control.ttl_in_cache <= 0)) || (s->cache_control.never_cache)) {
    Debug("http_trans",
          "[is_response_cacheable] "
          "config doesn't allow storing, and cache control does not "
          "say to ignore no-cache and does not specify never-cache or a ttl");
    return (false);
  }
  // Debug("http_trans", "[is_response_cacheable] config permits storing");

  // does client explicitly forbit storing?
  if (!s->cache_info.directives.does_client_permit_storing && !s->cache_control.ignore_client_no_cache) {
    Debug("http_trans",
          "[is_response_cacheable] " "client does not permit storing, "
          "and cache control does not say to ignore client no-cache");
    return (false);
  }
  Debug("http_trans", "[is_response_cacheable] client permits storing");

  HTTPStatus response_code = response->status_get();

  // caching/not-caching based on required headers
  // only makes sense when the server sends back a
  // 200 and a document.
  if (response_code == HTTP_STATUS_OK) {

    // If a ttl is set: no header required for caching
    // otherwise: follow parameter http.cache.required_headers
    if (s->cache_control.ttl_in_cache <= 0) {
      inku32 cc_mask = (MIME_COOKED_MASK_CC_MAX_AGE | MIME_COOKED_MASK_CC_S_MAXAGE);
      // server did not send expires header or last modified
      // and we are configured to not cache without them.
      switch (s->http_config_param->cache_required_headers) {
      case HttpConfigParams::CACHE_REQUIRED_HEADERS_NONE:
        Debug("http_trans", "[is_response_cacheable] " "no response headers required");
        break;

      case HttpConfigParams::CACHE_REQUIRED_HEADERS_AT_LEAST_LAST_MODIFIED:
        if (!response->presence(MIME_PRESENCE_EXPIRES) && !(response->get_cooked_cc_mask() & cc_mask) && 
            !response->get_last_modified()) {
          Debug("http_trans", "[is_response_cacheable] " "last_modified, expires, or max-age is required");

          // Set the WUTS code to NO_DLE or NO_LE only for 200 responses.
          if (response_code == HTTP_STATUS_OK) {
            s->squid_codes.hit_miss_code =
              ((response->get_date() == 0) ? (SQUID_MISS_HTTP_NO_DLE) : (SQUID_MISS_HTTP_NO_LE));
          }
          return (false);
        }
        break;

      case HttpConfigParams::CACHE_REQUIRED_HEADERS_CACHE_CONTROL:
        if (!response->presence(MIME_PRESENCE_EXPIRES) && !(response->get_cooked_cc_mask() & cc_mask)) {
          Debug("http_trans", "[is_response_cacheable] " "expires header or max-age is required");
          return (false);
        }
        break;

      default:
        break;
      }
    }
  }
  // do not cache partial content - Range response
  if (response_code == HTTP_STATUS_PARTIAL_CONTENT) {
    Debug("http_trans", "[is_response_cacheable] " "Partial content response - don't cache");
    return false;
  }

  if (s->scheme == URL_WKSIDX_FTP) {
    bool
      request_has_username =
      (s->ftp_info->username && s->ftp_info->username[0] &&
       (strcmp(s->ftp_info->username, "anonymous") != 0) && (strcmp(s->ftp_info->username, "ftp") != 0));

    if (request_has_username) {
      return (false);
    }
    if (!s->http_config_param->cache_ftp) {
      return (false);
    }
    if (s->ftp_info->is_directory) {
      return (false);
    }
  }
  // check if cache control overrides default cacheability
  int indicator;
  indicator = response_cacheable_indicated_by_cc(response);
  if (indicator > 0) {          // cacheable indicated by cache control header
    Debug("http_trans", "[is_response_cacheable] YES by response cache control");
    // even if it is authenticated, this is cacheable based on regular rules
    s->www_auth_content = CACHE_AUTH_NONE;
    return true;
  } else if (indicator < 0) {   // not cacheable indicated by cache control header

    // If a ttl is set, allow caching even if response contains
    // Cache-Control headers to prevent caching
    if (s->cache_control.ttl_in_cache > 0) {
      Debug("http_trans",
            "[is_response_cacheable] Cache-control header directives in response " "overriden by ttl in cache.config");
    } else if (!s->cache_control.ignore_server_no_cache) {
      Debug("http_trans", "[is_response_cacheable] NO by response cache control");
      return false;
    }
  }
  // else no indication by cache control header
  // continue to determine cacheability

  // if client contains Authorization header,
  // only cache if response has proper Cache-Control
 // if (s->www_auth_content == CACHE_AUTH_FRESH) {
    // response to the HEAD request
  //  return false;
  //} else if (s->www_auth_content == CACHE_AUTH_TRUE ||
   //          (s->www_auth_content == CACHE_AUTH_NONE && request->presence(MIME_PRESENCE_AUTHORIZATION))) {
   // if (!s->cache_control.cache_auth_content || response_code != HTTP_STATUS_OK || req_method != HTTP_WKSIDX_GET)
    //  return false;
  //}
  // s->www_auth_content == CACHE_AUTH_STALE silently continues

  if (response->presence(MIME_PRESENCE_EXPIRES)) {
    Debug("http_trans", "[is_response_cacheable] YES response w/ Expires");
    return true;
  }
  // if it's a 302 or 307 and no positive indicator from cache-control, reject
  if (response_code == HTTP_STATUS_MOVED_TEMPORARILY || response_code == HTTP_STATUS_TEMPORARY_REDIRECT) {
    Debug("http_trans", "[is_response_cacheable] cache-control or expires header is required for 302");
    return false;
  }
  // if it's a POST request and no positive indicator from cache-control
  if (req_method == HTTP_WKSIDX_POST) {
    // allow caching for a POST requests w/o Expires but with a ttl
    if (s->cache_control.ttl_in_cache > 0) {
      Debug("http_trans", "[is_response_cacheable] POST method with a TTL");
    } else {
      Debug("http_trans", "[is_response_cacheable] NO POST w/o Expires or CC");
      return false;
    }
  }
  // the plugin may decide we don't want to cache the response
  if (s->api_server_response_no_store) {
    s->api_server_response_no_store = false;
    return (false);
  }
  // default cacheability
  if (!s->http_config_param->negative_caching_enabled || s->no_negative_cache) {
    if ((response_code == HTTP_STATUS_OK) ||
        (response_code == HTTP_STATUS_NOT_MODIFIED) ||
        (response_code == HTTP_STATUS_NON_AUTHORITATIVE_INFORMATION) ||
        (response_code == HTTP_STATUS_MOVED_PERMANENTLY) ||
        (response_code == HTTP_STATUS_MULTIPLE_CHOICES) || (response_code == HTTP_STATUS_GONE)) {
      Debug("http_trans", "[is_response_cacheable] YES by default ");
      return true;
    } else {
      Debug("http_trans", "[is_response_cacheable] NO by default");
      return false;
    }
  }
  // let is_negative_caching_approriate decide what to do
  return true;
/* Since we weren't caching response obtained with
   Authorization (the cache control stuff was commented out previously)
   I've moved this check to is_request_cache_lookupable().
   We should consider this matter further.  It is unclear
   how many sites actually add Cache-Control headers for Authorized content.

    // if client contains Authorization header, only cache if response
    // has proper Cache-Control flags, as in RFC2068, section 14.8.
    if (request->field_presence(MIME_PRESENCE_AUTHORIZATION)) {
//         if (! (response->is_cache_control_set(HTTP_VALUE_MUST_REVALIDATE)) &&
//             ! (response->is_cache_control_set(HTTP_VALUE_PROXY_REVALIDATE)) &&
//             ! (response->is_cache_control_set(HTTP_VALUE_PUBLIC))) {
	    
	    Debug("http_trans", "[is_response_cacheable] request has AUTHORIZATION - not cacheable");
            return(false);
//         }
// 	else {
// 	    Debug("http_trans","[is_response_cacheable] request has AUTHORIZATION, "
// 		  "but response has a cache-control that allows caching");
// 	}
    }
*/

}

bool
HttpTransact::is_request_valid(State * s, HTTPHdr * incoming_request)
{
  RequestError_t incoming_error;
  URL *url = NULL;

  if (incoming_request)
    url = incoming_request->url_get();

  incoming_error = check_request_validity(s, incoming_request);
  switch (incoming_error) {
  case NO_REQUEST_HEADER_ERROR:
    Debug("http_trans", "[is_request_valid]" "no request header errors");
    break;
  case FAILED_PROXY_AUTHORIZATION:
    Debug("http_trans", "[is_request_valid]" "failed proxy authorization");
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
    build_error_response(s,
                         HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED,
                         "Proxy Authentication Required", "access#proxy_auth_required", "");
    return FALSE;
  case NON_EXISTANT_REQUEST_HEADER:
    /* fall through */
  case BAD_HTTP_HEADER_SYNTAX:
    {
      Debug("http_trans", "[is_request_valid]" "non-existant/bad header");
      SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
      build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Invalid HTTP Request", "request#syntax_error",
                           const_cast < char *>(URL_MSG));
      return FALSE;
    }

  case MISSING_HOST_FIELD:

    ////////////////////////////////////////////////////////////////////
    // FIX: are we sure the following logic is right?  it seems that  //
    //      we shouldn't complain about the missing host header until //
    //      we know we really need one --- are we sure we need a host //
    //      header at this point?                                     //
    //                                                                //
    // FIX: also, let's clean up the transparency code to remove the  //
    //      SunOS conditionals --- we will be transparent on all      //
    //      platforms soon!  in fact, I really want a method that i   //
    //      can call for each transaction to say if the transaction   //
    //      is a forward proxy request, a transparent request, a      //
    //      reverse proxy request, etc --- the detail of how we       //
    //      determine the cases should be hidden behind the method.   //
    ////////////////////////////////////////////////////////////////////

    Debug("http_trans", "[is_request_valid] missing host field");
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
    if (s->http_config_param->transparency_enabled) {   // host header missing, and transparency on
      build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Host Header Required", "interception#no_host",
                           // This is all one long string
                           "An attempt was made to transparently proxy your request, "
                           "but this attempt failed because your browser did not "
                           "send an HTTP 'Host' header.<p>Please manually configure "
                           "your browser to use 'http://%s:%d' as an HTTP proxy. "
                           "Please refer to your browser's documentation for details. ",
                           s->http_config_param->proxy_hostname, s->http_config_param->proxy_server_port);
    } else if (s->http_config_param->reverse_proxy_enabled) {   // host header missing, and transparency off but reverse
      // proxy on
      build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Host Header Required", "request#no_host",
                           // This too is all one long string
                           "Your browser did not send \"Host:\" HTTP header field, "
                           "and therefore the virtual host being requested could "
                           "not be determined.  To access this site you will need "
                           "to upgrade to a browser that supports the HTTP " "\"Host:\" header field.");
    } else {
      // host header missing, and transparency & reverse proxy off
      build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Host Required In Request", "request#no_host",
                           // This too is all one long string
                           "Your browser did not send a hostname as part of "
                           "the requested url. The configuration of this proxy "
                           "requires a hostname to be send as part of the url");
    }

    return FALSE;
  case SCHEME_NOT_SUPPORTED:
  case NO_REQUEST_SCHEME:
    {
      Debug("http_trans", "[is_request_valid] unsupported " "or missing request scheme");
      SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
      build_error_response(s, HTTP_STATUS_BAD_REQUEST,
                           "Unsupported URL Scheme", "request#scheme_unsupported", const_cast < char *>(URL_MSG));
      return FALSE;
    }
    /* fall through */
  case METHOD_NOT_SUPPORTED:
    Debug("http_trans", "[is_request_valid]" "unsupported method");
    s->current.mode = TUNNELLING_PROXY;
    return TRUE;
  case FTP_METHOD_NOT_SUPPORTED:
    {
      Debug("http_trans", "[is_request_valid] unsupported ftp method");
      SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
      build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Unsupported FTP Method", "ftp#unsupported_method",
                           const_cast < char *>(URL_MSG));
      return FALSE;
    }
  case BAD_SSL_PORT:
    int port;
    port = url ? url->port_get() : 0;
    Debug("http_trans", "[is_request_valid]" "%d is an invalid ssl port", port);
    SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
    build_error_response(s,
                         HTTP_STATUS_FORBIDDEN,
                         "Tunnel or SSL Forbidden",
                         "access#ssl_forbidden", "%d is not an allowed port for Tunnel or SSL connections", port);
    return FALSE;
  case NO_POST_CONTENT_LENGTH:
    {
      Debug("http_trans", "[is_request_valid] post request without content length");
      SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
      build_error_response(s, HTTP_STATUS_BAD_REQUEST,
                           "request#no_content_length", "Content Length Required", const_cast < char *>(URL_MSG));
      return FALSE;
    }
  case UNACCEPTABLE_TE_REQUIRED:
    {
      Debug("http_trans", "[is_request_valid] TE required is unacceptable.");
      SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);
      build_error_response(s, HTTP_STATUS_NOT_ACCEPTABLE,
                           "Transcoding Not Available", "transcoding#unsupported", const_cast < char *>(URL_MSG));
      return FALSE;
    }
  default:
    return TRUE;
  }

  return TRUE;
}

bool
HttpTransact::is_ftp_response_valid(State * s)
{

  s->hdr_info.response_error = check_ftp_response_validity(s);

  switch (s->hdr_info.response_error) {
  case FTP_LOGIN_INCORRECT:
    HTTP_DEBUG_ASSERT(s->scheme == URL_WKSIDX_FTP);
    Debug("http_trans", "[is_ftp_response_valid] Response Error: ftp login incorrect");
    s->current.state = FTP_OPEN_FAILED;
    return false;

  case FTP_CONNECTION_OPEN_FAILED:
    HTTP_DEBUG_ASSERT(s->scheme == URL_WKSIDX_FTP);
    Debug("http_trans", "[is_ftp_response_valid] Response Error: ftp connection open failed");
    s->current.state = FTP_OPEN_FAILED;
    return false;
  default:
    Debug("http_trans", "[is_ftp_response_valid] Errors in response");
    s->current.state = BAD_INCOMING_RESPONSE;
    return false;
  case NO_RESPONSE_HEADER_ERROR:
    Debug("http_trans", "[is_response_valid] No errors in response");
    return true;
  }
}

// bool HttpTransact::is_request_retryable
//
//   If we started a POST/PUT tunnel then we can
//    not retry failed requests
//
bool
HttpTransact::is_request_retryable(State * s)
{
  if (s->hdr_info.request_body_start == true) {
    return false;
  }

  if (s->state_machine->plugin_tunnel_type != HTTP_NO_PLUGIN_TUNNEL) {
    // API can override
    if (s->state_machine->plugin_tunnel_type == HTTP_PLUGIN_AS_SERVER && s->api_info.retry_intercept_failures == true) {
      // This used to be an == comparison, which made no sense. Changed
      // to be an assignment, hoping the state is correct.
      s->state_machine->plugin_tunnel_type = HTTP_NO_PLUGIN_TUNNEL;
    } else {
      return false;
    }
  }

  return true;
}

bool
HttpTransact::is_response_valid(State * s, HTTPHdr * incoming_response)
{

  if (s->current.state != CONNECTION_ALIVE) {
    HTTP_DEBUG_ASSERT((s->current.state == CONNECTION_ERROR) ||
                      (s->current.state == OPEN_RAW_ERROR) ||
                      (s->current.state == PARSE_ERROR) ||
                      (s->current.state == CONNECTION_CLOSED) ||
                      (s->current.state == INACTIVE_TIMEOUT) ||
                      (s->current.state == ACTIVE_TIMEOUT) ||
                      (s->current.state == FTP_OPEN_FAILED) || (s->current.state == CONGEST_CONTROL_CONGESTED_ON_M)
                      || (s->current.state == CONGEST_CONTROL_CONGESTED_ON_F));

    s->hdr_info.response_error = CONNECTION_OPEN_FAILED;
    return false;
  }

  s->hdr_info.response_error = check_response_validity(s, incoming_response);

  switch (s->hdr_info.response_error) {
#ifdef REALLY_NEED_TO_CHECK_DATE_VALIDITY
  case BOGUS_OR_NO_DATE_IN_RESPONSE:
    // We could modify the response to add the date, if need be.
//          incoming_response->set_date(s->request_sent_time);
    return true;
#endif
  case NO_RESPONSE_HEADER_ERROR:
    Debug("http_trans", "[is_response_valid] No errors in response");
    return true;

  case MISSING_REASON_PHRASE:
    Debug("http_trans", "[is_response_valid] Response Error: Missing reason phrase - allowing");
    return true;

  case STATUS_CODE_SERVER_ERROR:
    Debug("http_trans", "[is_response_valid] Response Error: Origin Server returned 500 - allowing");
    return true;

  case CONNECTION_OPEN_FAILED:
    Debug("http_trans", "[is_response_valid] Response Error: connection open failed");
    s->current.state = CONNECTION_ERROR;
    return false;

  case NON_EXISTANT_RESPONSE_HEADER:
    Debug("http_trans", "[is_response_valid] Response Error: No response header");
    s->current.state = BAD_INCOMING_RESPONSE;
    return false;

  case NOT_A_RESPONSE_HEADER:
    Debug("http_trans", "[is_response_valid] Response Error: Not a response header");
    s->current.state = BAD_INCOMING_RESPONSE;
    return false;

  case MISSING_STATUS_CODE:
    if (s->next_hop_scheme == URL_WKSIDX_FTP) {
      Debug("http_trans", "[is_response_valid] Response Error: Missing status code - allowing");
      return true;
    } else {
      Debug("http_trans", "[is_response_valid] Response Error: Missing status code");
      s->current.state = BAD_INCOMING_RESPONSE;
      return false;
    }

  default:
    Debug("http_trans", "[is_response_valid] Errors in response");
    s->current.state = BAD_INCOMING_RESPONSE;
    return false;
  }
}

///////////////////////////////////////////////////////////////////////////////
// Name       : service_transaction_in_proxy_only_mode
// Description: uses some metric to force this transaction to be proxy-only
//
// Details    :
//   
// Some metric may be employed to force the traffic server to enter
// a proxy-only mode temporarily. This function is called to determine
// if the current transaction should be proxy-only. The function is
// called from initialize_state_variables_from_request and is used to
// set s->current.mode to TUNNELLING_PROXY and just for safety to set
// s->cache_info.action to CACHE_DO_NO_ACTION.
// 
// Currently the function is just a placeholder and always returns false.
//
///////////////////////////////////////////////////////////////////////////////
bool
HttpTransact::service_transaction_in_proxy_only_mode(State * s)
{
  return false;
}

//////////////////////////////////////////////////////////////////
//
//  HttpTransact::setup_ftp_request()
//
//  setup ftp path, user name and password
//////////////////////////////////////////////////////////////////
bool
HttpTransact::setup_ftp_request(State * s)
{
#ifndef INK_NO_FTP
  HTTPHdr *r = &s->hdr_info.client_request;
  MimeTableEntry *e = NULL;
  const char *t;
  int j;

  char *tmp;
  int ftp_path_len, user_name_len, password_len, ftp_filename_len;

  // Create the struct for holding ftp info
  s->ftp_info = (FtpInfo *) s->arena.alloc(sizeof(FtpInfo));
  s->ftp_info->init();

  ///////////////////////////////////////////////////////
  // get ftp path. separate the path from the filename //
  // path1/file1  => [path1, file1]
  // path1/file1  => [path1, file1]
  // file1        => [., file1]
  ///////////////////////////////////////////////////////
  t = r->url_get()->path_get(&ftp_path_len);
  if (!t)
    return false;               // This probably shouldn't happen ...

  if (t[0] == '/') {
    t++;
    ftp_path_len--;
  }                             // a bug in

  for (j = ftp_path_len - 1; j >= 0; j--) {
    if (t[j] == '/') {
      // make a copy of the path name
      bool filename_exists = (j < ftp_path_len - 1);
      ftp_filename_len = (filename_exists) ? (ftp_path_len - j - 1) : 0;

      ftp_path_len = j;
      s->ftp_info->path = URL::unescapify(&s->arena, &t[0], ftp_path_len);

      if (filename_exists) {    // not ending with '/'. There is a filename
        s->ftp_info->filename = URL::unescapify(&s->arena, &t[j + 1], ftp_filename_len);
      }
      break;
    }
  }
  if (j == -1 && s->ftp_info->path == 0) {      // no '/' found
    // don't default to "." for directory here
    s->ftp_info->path = NULL;
    if (t && t[0] != '\0')
      s->ftp_info->filename = URL::unescapify(&s->arena, t, ftp_path_len);
  }
  /////////////////////////////////////////////////////////////////
  // get user name and password.                                 //
  // If an Authorization header field exist, try to get the user //
  // name and password from there. Else try to get them from the //
  // url.                                                        //
  /////////////////////////////////////////////////////////////////
  if (r->presence(MIME_PRESENCE_AUTHORIZATION)) {
    if (!HttpTransactHeaders::
        generate_basic_authorization_from_request(&s->arena, r, &s->ftp_info->username, &s->ftp_info->password)) {
      build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Bad HTTP Request For FTP Object", "ftp#bad_request", "");
      s->next_action = PROXY_SEND_ERROR_CACHE_NOOP;;
      return false;
    }
  } else {
    ////////////////////////////
    // get user name from url //
    ////////////////////////////
    r->url_get()->user_get(&user_name_len);
    if (user_name_len == 0) {
      s->ftp_info->username = "anonymous";
      s->ftp_info->password = s->http_config_param->ftp_anonymous_passwd;
    } else {
      /////////////////////////////////////////
      // get user name and password from url //
      /////////////////////////////////////////
      char anon[] = "anonymous";
      char ftp[] = "ftp";
      tmp = (char *) r->url_get()->user_get(&user_name_len);
      s->ftp_info->username = s->arena.str_store(tmp, user_name_len);
      tmp = (char *) r->url_get()->password_get(&password_len);
      s->ftp_info->password = s->arena.str_store(tmp, password_len);
      if (password_len <= 0 &&
          ((ptr_len_cmp
            (s->ftp_info->username, user_name_len, anon,
             sizeof(anon) - 1) == 0) ||
           (ptr_len_cmp(s->ftp_info->username, user_name_len, ftp, sizeof(ftp) - 1) == 0))) {
        s->ftp_info->password = s->http_config_param->ftp_anonymous_passwd;
      }
    }
  }
  ////////////////////////
  // Get mime type info //
  ////////////////////////
  e = (s->ftp_info->filename) ? (mimeTable.get_entry_path(s->ftp_info->filename)) : (0);
  if (e && strcmp(e->name, "unknown") == 0)
    e = &unknown_ftp_mime_type;
  s->ftp_info->mime_type = e;

  /////////////////////////////////////////////
  // set ftp type. If it not explicit in the //
  // url then get it from the mime entry     //
  /////////////////////////////////////////////
  s->ftp_info->transfer_mode = r->url_get()->type_get();
  if (s->ftp_info->transfer_mode == 0) {
    if (!s->ftp_info->filename)
      s->ftp_info->transfer_mode = 'A'; // directory (ASCII)
    else if (s->http_config_param->ftp_binary_transfer_only)
      s->ftp_info->transfer_mode = 'I';
    else if (!s->ftp_info->mime_type)
      s->ftp_info->transfer_mode = 'I'; // no mime type, use default (binary)
    else if (strcmp(s->ftp_info->mime_type->mime_encoding, "7bit") == 0)
      s->ftp_info->transfer_mode = 'A'; // text files (ASCII)
    else
      s->ftp_info->transfer_mode = 'I'; // all other mime encodings (binary)
  }

  return (true);
#else
  return (false);
#endif //INK_NO_FTP
}

bool
HttpTransact::setup_transparency(State * s)
{
  bool set = false;
  /*
   * NOTE: removed ARM code from here
   */
  return set;
}

bool
HttpTransact::process_quick_http_filter(State * s, int method)
{
  int quick_filter_mask;
  bool address_ok = true;

  // vl: Please refer to proxy/mgmt2/RecordsConfig.cc file,
  // "proxy.config.http.quick_filter.mask" variable for
  // detailed information about quick_filter_mask layout.

  if (!s->client_connection_enabled) {
    return false;               // connection already disabled by previous ACL filtering
  }

  if (((quick_filter_mask = s->http_config_param->quick_filter_mask) & 0x0FFF) != 0) {
    int method_mask = (method - HTTP_WKSIDX_CONNECT);   // fastest way to do it
    if (likely(method_mask >= 0 && method_mask < HTTP_WKSIDX_METHODS_CNT)) {
      method_mask = 1 << method_mask;
    } else                      // impossible case, but we have to check it
    {
      if (method == HTTP_WKSIDX_GET)
        method_mask = 0x0004;
      else if (method == HTTP_WKSIDX_HEAD)
        method_mask = 0x0008;
      else if (method == HTTP_WKSIDX_POST)
        method_mask = 0x0040;
      else if (method == HTTP_WKSIDX_DELETE)
        method_mask = 0x0002;
      else if (method == HTTP_WKSIDX_OPTIONS)
        method_mask = 0x0020;
      else if (method == HTTP_WKSIDX_PURGE)
        method_mask = 0x0080;
      else if (method == HTTP_WKSIDX_PUT)
        method_mask = 0x0100;
      else if (method == HTTP_WKSIDX_TRACE)
        method_mask = 0x0200;
      else if (method == HTTP_WKSIDX_PUSH)
        method_mask = 0x0400;
      else if (method == HTTP_WKSIDX_CONNECT)
        method_mask = 0x0001;
      else if (method == HTTP_WKSIDX_ICP_QUERY)
        method_mask = 0x0010;
      else
        method_mask = 0x0000;
    }
    if ((quick_filter_mask & method_mask) == 0) {
      return true;              // enable request processing because method does not match
    }
  }

  if (address_ok) {
    if ((quick_filter_mask & 0x80000000) == 0)
      address_ok = false;
  }
  return (s->client_connection_enabled = address_ok);
}


// bool HttpTransact::setup_reverse_proxy(State * s, HTTPHdr * incoming_request)
// {
// }

HttpTransact::HostNameExpansionError_t HttpTransact::try_to_expand_host_name(State * s)
{
  static int
    max_dns_lookups = 2 + s->http_config_param->num_url_expansions;
  static int
    last_expansion = max_dns_lookups - 2;

  HTTP_RELEASE_ASSERT(!s->dns_info.lookup_success);

  if (s->dns_info.looking_up == ORIGIN_SERVER) {
    ///////////////////////////////////////////////////
    // if resolving dns of the origin server failed, //
    // we try to expand hostname.                    //
    ///////////////////////////////////////////////////
    if (s->http_config_param->enable_url_expandomatic) {
      int
        attempts = s->dns_info.attempts;
      HTTP_DEBUG_ASSERT(attempts >= 1 && attempts <= max_dns_lookups);
      if (attempts < max_dns_lookups) {
        // Try a URL expansion
        if (attempts <= last_expansion) {
          char *
            expansion = s->http_config_param->url_expansions[attempts - 1];
          int
            length = strlen(s->server_info.name) + strlen(expansion) + 1;
          s->dns_info.lookup_name = s->arena.str_alloc(length);
          ink_string_concatenate_strings_n(s->dns_info.lookup_name,
                                           length + 1, s->server_info.name, ".", expansion, NULL);
        } else {
          if (ParseRules::strchr(s->server_info.name, '.')) {
            // don't expand if contains '.'
            return (EXPANSION_FAILED);
          }
          // Try www.<server_name>.com
          int
            length = strlen(s->server_info.name) + 8;
          s->dns_info.lookup_name = s->arena.str_alloc(length);
          ink_string_concatenate_strings_n(s->dns_info.lookup_name,
                                           length + 1, "www.", s->server_info.name, ".com", NULL);
        }
        return (RETRY_EXPANDED_NAME);
      } else {
        return (DNS_ATTEMPTS_EXHAUSTED);
      }
    } else {
      return EXPANSION_NOT_ALLOWED;
    }
  } else {
    //////////////////////////////////////////////////////
    // we looked up dns of parent proxy, but it failed, //
    // try lookup of origin server name.                //
    //////////////////////////////////////////////////////
    HTTP_DEBUG_ASSERT(s->dns_info.looking_up == PARENT_PROXY);

    s->dns_info.lookup_name = s->server_info.name;
    s->dns_info.looking_up = ORIGIN_SERVER;
    s->dns_info.attempts = 0;

    return RETRY_EXPANDED_NAME;
  }
}

bool
HttpTransact::will_this_request_self_loop(State * s)
{
  ////////////////////////////////////////
  // check if we are about to self loop //
  ////////////////////////////////////////
  if (s->dns_info.lookup_success) {
    int dns_host_ip, host_port, local_ip, local_port;

    dns_host_ip = s->host_db_info.ip();
    local_ip = this_machine()->ip;

    if (dns_host_ip == local_ip) {
      host_port = s->hdr_info.client_request.url_get()->port_get();
      local_port = s->client_info.port;
      if (host_port == local_port) {
        switch (s->dns_info.looking_up) {
        case ORIGIN_SERVER:
          Debug("http_transact",
                "[will_this_request_self_loop] " "host ip and port same as local ip and port - bailing");
          break;
        case PARENT_PROXY:
          Debug("http_transact", "[will_this_request_self_loop] "
                "parent proxy ip and port same as local ip and port - bailing");
          break;
        default:
          Debug("http_transact", "[will_this_request_self_loop] "
                "unknown's ip and port same as local ip and port - bailing");
          break;
        }
        build_error_response(s, HTTP_STATUS_BAD_REQUEST, "Cycle Detected",
                             "request#cycle_detected", "Your request is prohibited because it would cause a cycle.");
        return TRUE;
      }
    }
    // Now check for a loop using the Via string.

    // Since we insert our ip_address (in hex) into outgoing Via strings,
    // look for our_ip address in the request's via string.
    //
    MIMEField *via_field;
    via_field = s->hdr_info.client_request.field_find(MIME_FIELD_VIA, MIME_LEN_VIA);
    if (via_field) {
      // look for hex-based ip_string in Via string (which we would have inserted)
      char proxy_ip_string[9];

      int bits = nstrhex(proxy_ip_string, this_machine()->ip);
      proxy_ip_string[bits] = '\0';

      while (via_field) {
        // No need to waste cycles comma separating the via
        //  values since we want to do a match anywhere in the
        //  in the string.  We can just loop over the dup hdr
        //  fields
        int via_len;
        const char *via_string = via_field->value_get(&via_len);
        if (via_string && ptr_len_str(via_string, via_len, proxy_ip_string)) {

          Debug("http_transact", "[will_this_request_self_loop] "
                "Incoming via: %.*s has (%s[%s] (%s))",
                via_len, via_string,
                s->http_config_param->proxy_hostname, proxy_ip_string, s->http_config_param->proxy_request_via_string);
          build_error_response(s, HTTP_STATUS_BAD_REQUEST,
                               "Multi-Hop Cycle Detected",
                               "request#cycle_detected", "Your request is prohibited because it would cause a cycle.");
          return TRUE;
        }

        via_field = via_field->m_next_dup;
      }
    }
  }
  s->request_will_not_selfloop = true;
  return FALSE;
}

/*
 * handle_content_length_header(...)
 *  Function handles the insertion of content length headers into
 * header. header CAN equal base.
 */
void
HttpTransact::handle_content_length_header(State * s, HTTPHdr * header, HTTPHdr * base)
{
  if (header->type_get() == HTTP_TYPE_RESPONSE) {
    // This isn't used.
    // int status_code = base->status_get();
    ink32 cl = HTTP_UNDEFINED_CL;
    if (base->presence(MIME_PRESENCE_CONTENT_LENGTH)) {
      cl = base->get_content_length();
      if (cl >= 0) {
        // header->set_content_length(cl);
        HTTP_DEBUG_ASSERT(header->get_content_length() == cl);

        switch (s->source) {
        case SOURCE_CACHE:
          ////////////////////////////////////////////////
          //  Make sure that the cache's object size    //
          //   agrees with the Content-Length           //
          //   Otherwise, set the state's machine view  //
          //   of c-l to undefined to turn off K-A      //
          ////////////////////////////////////////////////
          if ((ink32) s->cache_info.object_read->object_size_get() == cl) {
            s->hdr_info.trust_response_cl = true;
          } else {
            Debug("http_trans", "Content Length header and cache object size mismatch." "Disabling keep-alive");
            s->hdr_info.trust_response_cl = false;
          }
          break;
        case SOURCE_HTTP_ORIGIN_SERVER:
          // We made our decision about whether to trust the
          //   response content length in init_state_vars_from_response()
          break;
        case SOURCE_TRANSFORM:
          if (s->hdr_info.transform_response_cl == HTTP_UNDEFINED_CL) {
            s->hdr_info.trust_response_cl = false;
          } else {
            s->hdr_info.trust_response_cl = true;
          }
          break;
        default:
          ink_release_assert(0);
          break;
        }
      } else {
        header->field_delete(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);
        s->hdr_info.trust_response_cl = false;
      }
    } else {
      // No content length header
      if (s->source == SOURCE_CACHE) {

        // If there is no content-length header, we can
        //   insert one since the cache knows definately
        //   how long the object is unless we're in a
        //   read-while-write mode and object hasn't been
        //   written into a cache completely.
        cl = s->cache_info.object_read->object_size_get();
        if (cl == INT_MAX) { //INT_MAX cl in cache indicates rww in progress
          header->field_delete(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);
          s->hdr_info.trust_response_cl = false;
          s->hdr_info.request_content_length = HTTP_UNDEFINED_CL;
        } else {
          header->set_content_length(cl);
          s->hdr_info.trust_response_cl = true;
        }
      } else {

        // Check to see if there is no content length
        //  header because the response precludes a
        // body
        if (is_response_body_precluded(header->status_get(), s->method)) {
          // We want to be able to do keep-alive here since
          //   there can't be body so we don't have any
          //   issues about trusting the body length
          s->hdr_info.trust_response_cl = true;
        } else {
          s->hdr_info.trust_response_cl = false;
        }
        header->field_delete(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);

      }
    }
  } else if (header->type_get() == HTTP_TYPE_REQUEST) {

    int method = header->method_get_wksidx();
    if (method == HTTP_WKSIDX_GET || method == HTTP_WKSIDX_HEAD || method == HTTP_WKSIDX_OPTIONS || method == HTTP_WKSIDX_TRACE) {      /* No body, no content-length */

      header->field_delete(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);
      s->hdr_info.request_content_length = 0;

    } else if (base && base->presence(MIME_PRESENCE_CONTENT_LENGTH)) {
      /* Copy over the content length if its set */
      HTTP_DEBUG_ASSERT(s->hdr_info.request_content_length == base->get_content_length());
      HTTP_DEBUG_ASSERT(header->get_content_length() == base->get_content_length());
    } else {
      /*
       * Otherwise we are in a method with a potential cl, so unset
       * the header and flag the cl as undefined for the state machine.
       */
      header->field_delete(MIME_FIELD_CONTENT_LENGTH, MIME_LEN_CONTENT_LENGTH);
      s->hdr_info.request_content_length = HTTP_UNDEFINED_CL;
    }
    Debug("http_trans",
          "[handle_content_length_header] cont len in hdr is %d, stat var is %d",
          header->get_content_length(), s->hdr_info.request_content_length);
  }


  return;
}                               /* End HttpTransact::handle_content_length_header */


//////////////////////////////////////////////////////////////////////////////
//
//      void HttpTransact::handle_request_keep_alive_headers(
//          State* s, bool ka_on, HTTPVersion ver, HTTPHdr *heads)
//
//      Removes keep alive headers from user-agent from <heads>
//
//      Adds the appropriate keep alive headers (if any) to <heads>
//      for keep-alive state <ka_on>, and HTTP version <ver>.
//
//////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_request_keep_alive_headers(State * s, HTTPVersion ver, HTTPHdr * heads)
{
  enum KA_Action_t
  { KA_UNKNOWN, KA_DISABLED, KA_CLOSE, KA_CONNECTION };

  KA_Action_t ka_action = KA_UNKNOWN;

  bool upstream_ka = ((s->current.server->keep_alive == HTTP_KEEPALIVE) ||
                      (s->current.server->keep_alive == HTTP_PIPELINE));

  HTTP_DEBUG_ASSERT(heads->type_get() == HTTP_TYPE_REQUEST);

  // Check preconditions for Keep-Alive
  if (!upstream_ka) {
    ka_action = KA_DISABLED;
  } else if (HTTP_MAJOR(ver.m_version) == 0) {  /* No K-A for 0.9 apps */
    ka_action = KA_DISABLED;
  }
  // If preconditions are met, figure out what action to take
  if (ka_action == KA_UNKNOWN) {
    int method = heads->method_get_wksidx();
    if (method == HTTP_WKSIDX_GET ||
        method == HTTP_WKSIDX_HEAD ||
        method == HTTP_WKSIDX_OPTIONS ||
        method == HTTP_WKSIDX_PURGE || method == HTTP_WKSIDX_DELETE || method == HTTP_WKSIDX_TRACE) {
      // These methods do not need a content-length header
      ka_action = KA_CONNECTION;
    } else {
      // All remaining methods require a content length header
      if (heads->get_content_length() == -1) {
        ka_action = KA_CLOSE;
      } else {
        ka_action = KA_CONNECTION;
      }
    }
  }

  HTTP_DEBUG_ASSERT(ka_action != KA_UNKNOWN);

  // Since connection headers are hop-to-hop, strip the
  //  the ones we received from the user-agent
  heads->field_delete(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION);
  heads->field_delete(MIME_FIELD_PROXY_CONNECTION, MIME_LEN_PROXY_CONNECTION);


  // Insert K-A headers as necessary
  switch (ka_action) {
  case KA_CONNECTION:
    HTTP_DEBUG_ASSERT(s->current.server->keep_alive != HTTP_NO_KEEPALIVE);
    if (ver == HTTPVersion(1, 0)) {
      if (s->current.request_to == PARENT_PROXY ||
          s->current.request_to == ICP_SUGGESTED_HOST) {
        heads->value_set(MIME_FIELD_PROXY_CONNECTION, MIME_LEN_PROXY_CONNECTION, "keep-alive", 10);
      } else {
        heads->value_set(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION, "keep-alive", 10);
      }
    }
    // NOTE: if the version is 1.1 we don't need to do
    //  anything since keep-alive is assumed
    break;
  case KA_DISABLED:
  case KA_CLOSE:
    if (s->current.server->keep_alive != HTTP_NO_KEEPALIVE || (ver == HTTPVersion(1, 1))) {
      /* Had keep-alive */
      s->current.server->keep_alive = HTTP_NO_KEEPALIVE;
      if (s->current.request_to == PARENT_PROXY ||
          s->current.request_to == ICP_SUGGESTED_HOST) {
        heads->value_set(MIME_FIELD_PROXY_CONNECTION, MIME_LEN_PROXY_CONNECTION, "close", 5);
      } else {
        heads->value_set(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION, "close", 5);
      }
    }
    // Note: if we are 1.1, we always need to send the close
    //  header since persistant connnections are the default
    break;
  case KA_UNKNOWN:
  default:
    HTTP_DEBUG_ASSERT(0);
    break;
  }
}                               /* End HttpTransact::handle_request_keep_alive_headers */

//////////////////////////////////////////////////////////////////////////////
//
//      void HttpTransact::handle_response_keep_alive_headers(
//          State* s, bool ka_on, HTTPVersion ver, HTTPHdr *heads)
//
//      Removes keep alive headers from origin server from <heads>
//
//      Adds the appropriate Transfer-Encoding: chunked header.
//
//      Adds the appropriate keep alive headers (if any) to <heads>
//      for keep-alive state <ka_on>, and HTTP version <ver>.
//
//////////////////////////////////////////////////////////////////////////////
void
HttpTransact::handle_response_keep_alive_headers(State * s, HTTPVersion ver, HTTPHdr * heads)
{
  enum KA_Action_t
  { KA_UNKNOWN, KA_DISABLED, KA_CLOSE, KA_CONNECTION };
  KA_Action_t ka_action = KA_UNKNOWN;

  HTTP_DEBUG_ASSERT(heads->type_get() == HTTP_TYPE_RESPONSE);

  // Since connection headers are hop-to-hop, strip the
  //  the ones we received from upstream
  heads->field_delete(MIME_FIELD_CONNECTION, MIME_LEN_CONNECTION);
  heads->field_delete(MIME_FIELD_PROXY_CONNECTION, MIME_LEN_PROXY_CONNECTION);

#ifdef USE_NCA
  // Since NCA does all the client side keep alive,
  //   simply want to delete all the connection headers
  //   and return so as not interfere with NCA keep-alive
  if (s->client_info.port_attribute == SERVER_PORT_NCA) {
    return;
  }
#endif

  int c_hdr_field_len;
  const char *c_hdr_field_str;
  if (s->client_info.proxy_connect_hdr) {
    c_hdr_field_str = MIME_FIELD_PROXY_CONNECTION;
    c_hdr_field_len = MIME_LEN_PROXY_CONNECTION;
  } else {
    c_hdr_field_str = MIME_FIELD_CONNECTION;
    c_hdr_field_len = MIME_LEN_CONNECTION;
  }

  // Check pre-conditions for keep-alive
  if (s->client_info.keep_alive != HTTP_KEEPALIVE && s->client_info.keep_alive != HTTP_PIPELINE) {
    ka_action = KA_DISABLED;
  } else if (HTTP_MAJOR(ver.m_version) == 0) {  /* No K-A for 0.9 apps */
    ka_action = KA_DISABLED;
  }
  // some systems hang until the connection closes when receiving a 204
  //   regardless of the K-A headers
  else if (heads->status_get() == HTTP_STATUS_NO_CONTENT) {
    ka_action = KA_CLOSE;
  } else {
    // for the cache authenticated content feature
    bool session_auth_close = false;

    if (s->www_auth_content != CACHE_AUTH_NONE
        && s->state_machine->ua_session->session_based_auth
        && (s->server_info.keep_alive == HTTP_NO_KEEPALIVE ||
            (!s->http_config_param->session_auth_cache_keep_alive_enabled && s->next_action == SERVE_FROM_CACHE)))
      session_auth_close = true;

    // Note: the preceding conditions also apply for the use
    // of chunked encoding.

    // Determine if we are going to send either a server-generated or
    // proxy-generated chunked response to the client. If we cannot
    // trust the content-length, we may be able to chunk the response
    // to the client to keep the connection alive.
    // Insert a Transfer-Encoding header in the response if necessary.

    if (// check that the client is HTTP 1.1 and the conf allows chunking
        s->client_info.http_version == HTTPVersion(1, 1) &&
        ((s->http_config_param->chunking_enabled == 1 && s->remap_chunking_enabled != 0) ||
         (s->http_config_param->chunking_enabled == 0 && s->remap_chunking_enabled == 1)) &&
        // if we're not sending a body, don't set a chunked header regardless of server response
        !is_response_body_precluded(s->hdr_info.client_response.status_get(), s->method) &&
         // we do not need chunked encoding for internal error messages
         // that are sent to the client if the server response is not valid.
         ((s->source == SOURCE_HTTP_ORIGIN_SERVER &&
         s->hdr_info.server_response.valid() &&
         // if we receive a 304, we will serve the client from the
         // cache and thus do not need chunked encoding.
         s->hdr_info.server_response.status_get() != HTTP_STATUS_NOT_MODIFIED &&
         (s->current.server->transfer_encoding == HttpTransact::CHUNKED_ENCODING ||
          // we can use chunked encoding if we cannot trust the content
          // length (e.g. no Content-Length and Connection:close in HTTP/1.1 responses)
          s->hdr_info.trust_response_cl == false) && session_auth_close == false) ||
         // handle serve from cache (read-while-write) case
         (s->source == SOURCE_CACHE && s->hdr_info.trust_response_cl == false))) {

      s->client_info.receive_chunked_response = true;
      heads->value_append(MIME_FIELD_TRANSFER_ENCODING, MIME_LEN_TRANSFER_ENCODING, HTTP_VALUE_CHUNKED, HTTP_LEN_CHUNKED, true);
    } else {
      s->client_info.receive_chunked_response = false;
    }

    // If we cannot trust the content length, we will close the connection
    // unless we are going to use chunked encoding or the client issued
    // a PUSH request
    if (s->hdr_info.trust_response_cl == false &&
        !(s->client_info.receive_chunked_response == true ||
          (s->method == HTTP_WKSIDX_PUSH && s->client_info.keep_alive == HTTP_KEEPALIVE))) {
      ka_action = KA_CLOSE;
    } else if (session_auth_close) {
      ka_action = KA_CLOSE;
    } else {
      ka_action = KA_CONNECTION;
    }
  }

  // Insert K-A headers as necessary
  switch (ka_action) {
  case KA_CONNECTION:
    HTTP_DEBUG_ASSERT(s->client_info.keep_alive != HTTP_NO_KEEPALIVE);
    // This is a hack, we send the keep-alive header for both 1.0
    // and 1.1, to be "compatible" with Akamai.
    // if (ver == HTTPVersion (1, 0)) {
    heads->value_set(c_hdr_field_str, c_hdr_field_len, "keep-alive", 10);
    // NOTE: if the version is 1.1 we don't need to do
    //  anything since keep-alive is assumed
    break;
  case KA_CLOSE:
  case KA_DISABLED:
    if (s->client_info.keep_alive != HTTP_NO_KEEPALIVE || (ver == HTTPVersion(1, 1))) {
      heads->value_set(c_hdr_field_str, c_hdr_field_len, "close", 5);
      s->client_info.keep_alive = HTTP_NO_KEEPALIVE;
    }
    // Note: if we are 1.1, we always need to send the close
    //  header since persistant connnections are the default
    break;
  case KA_UNKNOWN:
  default:
    HTTP_DEBUG_ASSERT(0);
    break;
  }

}                               /* End HttpTransact::handle_response_keep_alive_headers */


bool
HttpTransact::delete_all_document_alternates_and_return(State * s, bool cache_hit)
{
  if (cache_hit == TRUE) {
    if (s->cache_info.is_ram_cache_hit) {
      SET_VIA_STRING(VIA_CACHE_RESULT, VIA_IN_RAM_CACHE_FRESH);
    } else {
      SET_VIA_STRING(VIA_CACHE_RESULT, VIA_IN_CACHE_FRESH);
    }
  } else {
    SET_VIA_STRING(VIA_DETAIL_CACHE_LOOKUP, VIA_DETAIL_MISS_NOT_CACHED);
  }

#ifdef USE_NCA
  /*  FIX ME: DELETE and NCA
     if (s->client_info.port_attribute == SERVER_PORT_NCA) {
     if (s->nca_info.request_info->advisory) {
     HTTPHdr cached_response = s->cache_info.object_read->response_get();
     ink_assert(cached_response.valid());
     inku64 ctag = extract_ctag_from_response(cached_response);
     s->nca_info.response_info.ctag = ctag;
     s->nca_info.response_info.advisory |= NCA_IO_ADVISE_FLUSH;
     s->nca_info.response_info.nocache = 1;
     }
     }
   */
#endif

  if ((s->method != HTTP_WKSIDX_GET) && (s->method == HTTP_WKSIDX_DELETE || s->method == HTTP_WKSIDX_PURGE)) {
    bool valid_max_forwards;
    int max_forwards = -1;
    MIMEField *max_forwards_f = s->hdr_info.client_request.field_find(MIME_FIELD_MAX_FORWARDS,
                                                                      MIME_LEN_MAX_FORWARDS);

    // Check the max forwards value for DELETE
    if (max_forwards_f) {
      valid_max_forwards = true;
      max_forwards = max_forwards_f->value_get_int();
    } else {
      valid_max_forwards = false;
    }

    if (s->method == HTTP_WKSIDX_PURGE || (valid_max_forwards && max_forwards <= 0)) {
      Debug("http_trans", "[delete_all_document_alternates_and_return] " "DELETE with Max-Forwards: %d", max_forwards);

      SET_VIA_STRING(VIA_DETAIL_TUNNEL, VIA_DETAIL_TUNNEL_NO_FORWARD);

      // allow deletes to be pipelined
      //   We want to allow keep-alive so trust the response content
      //    length.  There really isn't one and the SM will add the
      //    zero content length when setting up the transfer
      s->hdr_info.trust_response_cl = true;
      build_response(s,
                     &s->hdr_info.client_response,
                     s->client_info.http_version, (cache_hit == TRUE) ? HTTP_STATUS_OK : HTTP_STATUS_NOT_FOUND);

      return true;
    } else {
      if (valid_max_forwards) {
        --max_forwards;
        Debug("http_trans",
              "[delete_all_document_alternates_and_return] " "Decrementing max_forwards to %d", max_forwards);
        s->hdr_info.client_request.value_set_int(MIME_FIELD_MAX_FORWARDS, MIME_LEN_MAX_FORWARDS, max_forwards);
      }
    }
  }

  return false;
}

bool
  HttpTransact::
does_client_request_permit_cached_response(const HttpConfigParams * p,
                                           CacheControlResult * c, HTTPHdr * h, char *via_string)
{
  NOWARN_UNUSED(p);

  ////////////////////////////////////////////////////////////////////////
  // If aren't ignoring client's cache directives, meet client's wishes //
  ////////////////////////////////////////////////////////////////////////

  if (!c->ignore_client_no_cache) {
    if (h->is_cache_control_set(HTTP_VALUE_NO_CACHE))
      return (false);
    if (h->is_pragma_no_cache_set()) {
      // if we are going to send out an ims anyway,
      // no need to flag this as a no-cache.
      if (!p->cache_ims_on_client_no_cache) {
        via_string[VIA_CLIENT_REQUEST] = VIA_CLIENT_NO_CACHE;
      }
      return (false);
    }
  }

  return (true);
}

bool
HttpTransact::does_client_request_permit_dns_caching(CacheControlResult * c, HTTPHdr * h)
{
  if (h->is_pragma_no_cache_set() && h->is_cache_control_set(HTTP_VALUE_NO_CACHE) && (!c->ignore_client_no_cache)) {
    return (false);
  }
  return (true);
}

bool
HttpTransact::does_client_request_permit_storing(CacheControlResult * c, HTTPHdr * h)
{
  ////////////////////////////////////////////////////////////////////////
  // If aren't ignoring client's cache directives, meet client's wishes //
  ////////////////////////////////////////////////////////////////////////
  if (!c->ignore_client_no_cache) {
    if (h->is_cache_control_set(HTTP_VALUE_NO_STORE))
      return (false);
  }

  return (true);
}



int
HttpTransact::calculate_document_freshness_limit(Arena * arena,
                                                 HTTPHdr * response,
                                                 time_t response_date,
                                                 bool * heuristic,
                                                 time_t
                                                 request_sent_time,
                                                 int
                                                 cache_heuristic_min_lifetime,
                                                 int
                                                 cache_heuristic_max_lifetime,
                                                 double
                                                 cache_heuristic_lm_factor,
                                                 int
                                                 cache_guaranteed_min_lifetime,
                                                 int cache_guaranteed_max_lifetime,
                                                 time_t plugin_set_expire_time, State * s)
{
  bool expires_set, date_set, last_modified_set;
  time_t date_value, expires_value, last_modified_value;
  int min_freshness_bounds, max_freshness_bounds, freshness_limit = 0;

  *heuristic = false;

  inku32 cc_mask = response->get_cooked_cc_mask();
  if (cc_mask & (MIME_COOKED_MASK_CC_S_MAXAGE | MIME_COOKED_MASK_CC_MAX_AGE)) {
    if (cc_mask & MIME_COOKED_MASK_CC_S_MAXAGE) {
      freshness_limit = (int) response->get_cooked_cc_s_maxage();
      Debug("http_match",
            "calculate_document_freshness_limit --- s_max_age set, freshness_limit = %d", freshness_limit);
    } else if (cc_mask & MIME_COOKED_MASK_CC_MAX_AGE) {
      freshness_limit = (int) response->get_cooked_cc_max_age();
      Debug("http_match", "calculate_document_freshness_limit --- max_age set, freshness_limit = %d", freshness_limit);
    }
    freshness_limit = min(max(0, freshness_limit), NUM_SECONDS_IN_ONE_YEAR);
  } else {
    date_set = last_modified_set = false;

    if (plugin_set_expire_time != UNDEFINED_TIME) {
      expires_set = true;
      expires_value = plugin_set_expire_time;
    } else {
      expires_set = (response->presence(MIME_PRESENCE_EXPIRES) != 0);
      expires_value = response->get_expires();
    }

    date_value = response_date;
    if (date_value > 0) {
      date_set = true;
    } else {
      date_value = request_sent_time;
      Debug("http_match",
            "calculate_document_freshness_limit --- Expires header = %d  no date, using sent time %d",
            expires_value, date_value);
    }
    HTTP_DEBUG_ASSERT(date_value > 0);

    // Getting the cache_sm object
    HttpCacheSM & cache_sm = s->state_machine->get_cache_sm();

    //Bypassing if loop to set freshness_limit to heuristic value
    if (expires_set && !cache_sm.is_readwhilewrite_inprogress()) {
      if (expires_value == UNDEFINED_TIME || expires_value <= date_value) {
        expires_value = date_value;
        Debug("http_match", "calculate_document_freshness_limit --- no expires, using date %d", expires_value);
      }
      freshness_limit = (int) (expires_value - date_value);

      Debug("http_match",
            "calculate_document_freshness_limit --- Expires: %d,"
            "Date: %d, " "freshness_limit = %d", expires_value, date_value, freshness_limit);

      freshness_limit = min(max(0, freshness_limit), NUM_SECONDS_IN_ONE_YEAR);
    } else {
      last_modified_value = 0;
      if (response->presence(MIME_PRESENCE_LAST_MODIFIED)) {
        last_modified_set = TRUE;
        last_modified_value = response->get_last_modified();
        Debug("http_match", "calculate_document_freshness_limit --- Last Modified header = %d", last_modified_value);

        if (last_modified_value == UNDEFINED_TIME) {
          last_modified_set = FALSE;
        } else if (last_modified_value > date_value) {
          last_modified_value = date_value;
          Debug("http_match",
                "calculate_document_freshness_limit --- no last-modified, using sent time %d", last_modified_value);
        }
      }

      *heuristic = true;
      if (date_set && last_modified_set) {
        float f = cache_heuristic_lm_factor;
        HTTP_DEBUG_ASSERT((f >= 0.0) && (f <= 1.0));
        ink_time_t time_since_last_modify = date_value - last_modified_value;
        int h_freshness = (int) (time_since_last_modify * f);
        freshness_limit = max(h_freshness, 0);
        Debug("http_match",
              "calculate_document_freshness_limit --- heuristic: date=%d, lm=%d,"
              "time_since_last_modify=%ld, f=%g, freshness_limit = %d",
              date_value, last_modified_value, time_since_last_modify, f, freshness_limit);
      } else {
        freshness_limit = cache_heuristic_min_lifetime;
        Debug("http_match", "calculate_document_freshness_limit --- heuristic: freshness_limit = %d", freshness_limit);
      }
    }
  }

  // The freshness limit must always fall within the min and max guaranteed bounds.
  min_freshness_bounds = max(0, cache_guaranteed_min_lifetime);
  max_freshness_bounds = min(NUM_SECONDS_IN_ONE_YEAR, cache_guaranteed_max_lifetime);

  // Heuristic freshness can be more strict.
  if (*heuristic) {
    min_freshness_bounds = max(min_freshness_bounds, cache_heuristic_min_lifetime);
    max_freshness_bounds = min(max_freshness_bounds, cache_heuristic_max_lifetime);
  }
  // Now clip the freshness limit.
  if (freshness_limit > max_freshness_bounds)
    freshness_limit = max_freshness_bounds;
  if (freshness_limit < min_freshness_bounds)
    freshness_limit = min_freshness_bounds;

  Debug("http_match", "calculate_document_freshness_limit --- final freshness_limit = %d", freshness_limit);

  return (freshness_limit);
}



////////////////////////////////////////////////////////////////////////////////////
//  int HttpTransact::calculate_freshness_fuzz() 
//
//    This function trys to revents many, many simulatenous revalidations in
//     reverse proxy situations.  Statistically introduce a fuzz factor that
//     brings revalidation forward for a small percentage of the requests/
//     The hope is that is that the document early by a selected few, and 
//     the headers are updated in the cache before the regualr freshness
//     limit is actually reached
////////////////////////////////////////////////////////////////////////////////////

int
HttpTransact::calculate_freshness_fuzz(State * s, int fresh_limit)
{
  static double LOG_YEAR = log10(NUM_SECONDS_IN_ONE_YEAR);
  const inku32 granularity = 1000;
  int result = 0;

  inku32 random_num = this_ethread()->generator.random();
  inku32 index = random_num % granularity;
  inku32 range = (inku32) (granularity * s->http_config_param->freshness_fuzz_prob);

  if (index < range) {
    if (s->http_config_param->freshness_fuzz_min_time > 0) {
      // Complicated calculations to try to find a reasonable fuzz time between fuzz_min_time and fuzz_time
      int fresh_small = (int) rint((double) s->http_config_param->freshness_fuzz_min_time *
                                   pow(2, min((double) fresh_limit / (double) s->http_config_param->freshness_fuzz_time,
                                              sqrt(s->http_config_param->freshness_fuzz_time))));
      int fresh_large = max((int) s->http_config_param->freshness_fuzz_min_time,
                            (int) rint(s->http_config_param->freshness_fuzz_time *
                                       log10(fresh_limit - s->http_config_param->freshness_fuzz_min_time) / LOG_YEAR));
      result = min(fresh_small, fresh_large);
      Debug("http_match", "calculate_freshness_fuzz using min/max --- freshness fuzz = %d", result);
    } else {
      result = s->http_config_param->freshness_fuzz_time;
      Debug("http_match", "calculate_freshness_fuzz --- freshness fuzz = %d", result);
    }
  }

  return result;
}

//////////////////////////////////////////////////////////////////////////////
//
//
//      This function takes the request and response headers for a cached
//      object, and the current HTTP parameters, and decides if the object
//      is still "fresh enough" to serve.  One of the following values
//      is returned:
//
//          FRESHNESS_FRESH             Fresh enough, serve it
//          FRESHNESS_WARNING           Stale but client says it's okay
//          FRESHNESS_STALE             Too stale, don't use
//
//////////////////////////////////////////////////////////////////////////////
HttpTransact::Freshness_t HttpTransact::what_is_document_freshness(State *
                                                                   s,
                                                                   const
                                                                   HttpConfigParams
                                                                   *
                                                                   config,
                                                                   HTTPHdr
                                                                   *
                                                                   client_request,
                                                                   HTTPHdr
                                                                   * cached_obj_request, HTTPHdr * cached_obj_response)
{
  bool
    heuristic,
    do_revalidate = false;
  int
    age_limit;
  // These aren't used.
  //HTTPValCacheControl *cc;
  //const char *cc_val;
  int
    fresh_limit,
    current_age;
  ink_time_t
    response_date;
  inku32
    cc_mask,
    cooked_cc_mask;
  inku32
    os_specifies_revalidate;


  //////////////////////////////////////////////////////
  // If config file has a ttl-in-cache field set,     //
  // it has priority over any other http headers and  //
  // other configuration parameters.                  //
  //////////////////////////////////////////////////////
  if (s->cache_control.ttl_in_cache > 0) {
    // what matters if ttl is set is not the age of the document
    // but for how long it has been stored in the cache (resident time)
    int
      resident_time = s->current.now - s->response_received_time;
    Debug("http_match",
          "[..._document_freshness] ttl-in-cache = %d, resident time = %d",
          s->cache_control.ttl_in_cache, resident_time);
    if (resident_time > s->cache_control.ttl_in_cache) {
      return (FRESHNESS_STALE);
    } else {
      return (FRESHNESS_FRESH);
    }
  }


  cooked_cc_mask = cached_obj_response->get_cooked_cc_mask();
  os_specifies_revalidate = cooked_cc_mask &
    (MIME_COOKED_MASK_CC_MUST_REVALIDATE | MIME_COOKED_MASK_CC_PROXY_REVALIDATE);
  cc_mask = MIME_COOKED_MASK_CC_NEED_REVALIDATE_ONCE;

  // Check to see if the server forces revalidation

  if ((cooked_cc_mask & cc_mask) && s->cache_control.revalidate_after <= 0) {
    Debug("http_match", "[what_is_document_freshness] document stale due to " "server must-revalidate");
    return FRESHNESS_STALE;
  }

  response_date = cached_obj_response->get_date();

  fresh_limit = calculate_document_freshness_limit(&s->arena,
                                                   cached_obj_response,
                                                   response_date,
                                                   &heuristic,
                                                   s->request_sent_time,
                                                   config->cache_heuristic_min_lifetime,
                                                   config->cache_heuristic_max_lifetime,
                                                   config->cache_heuristic_lm_factor,
                                                   config->cache_guaranteed_min_lifetime,
                                                   config->cache_guaranteed_max_lifetime, s->plugin_set_expire_time, s);
  HTTP_DEBUG_ASSERT(fresh_limit >= 0);

  // Fuzz the freshness to prevent too many revalidates to popular 
  //  documents at the same time
  if (s->http_config_param->freshness_fuzz_time >= 0) {
    fresh_limit = fresh_limit - calculate_freshness_fuzz(s, fresh_limit);
    fresh_limit = max(0, fresh_limit);
    fresh_limit = min(NUM_SECONDS_IN_ONE_YEAR, fresh_limit);
  }

  current_age =
    HttpTransactHeaders::calculate_document_age(s->request_sent_time,
                                                s->response_received_time,
                                                cached_obj_response, response_date, s->current.now);

  HTTP_DEBUG_ASSERT(current_age >= 0);
  current_age = min(NUM_SECONDS_IN_ONE_YEAR, current_age);
  Debug("http_match", "[what_is_document_freshness] fresh_limit:  %d  current_age: %d", fresh_limit, current_age);

  /////////////////////////////////////////////////////////
  // did the admin override the expiration calculations? //
  // (used only for http).                               //
  /////////////////////////////////////////////////////////
  HTTP_DEBUG_ASSERT(client_request == &s->hdr_info.client_request);

  if (config->cache_when_to_revalidate == 0) {
    ;
    // Compute how fresh below
  } else if (client_request->url_get()->scheme_get_wksidx() == URL_WKSIDX_HTTP) {
    switch (config->cache_when_to_revalidate) {
    case 1:                    // Stale if heuristic
      if (heuristic) {
        Debug("http_match",
              "[what_is_document_freshness] config requires " "FRESHNESS_STALE because heuristic calculation");
        return (FRESHNESS_STALE);
      }
      break;
    case 2:                    // Always stale
      Debug("http_match", "[what_is_document_freshness] config " "specifies always FRESHNESS_STALE");
      return (FRESHNESS_STALE);
    case 3:                    // Never stale
      Debug("http_match", "[what_is_document_freshness] config " "specifies always FRESHNESS_FRESH");
      return (FRESHNESS_FRESH);
    case 4:                    // Stale if IMS
      if (client_request->presence(MIME_PRESENCE_IF_MODIFIED_SINCE)) {
        Debug("http_match", "[what_is_document_freshness] config " "specifies FRESHNESS_STALE if IMS present");
        return (FRESHNESS_STALE);
      }
    default:                   // Bad config, ignore
      break;
    }
  }
  //////////////////////////////////////////////////////////////////////
  // the normal expiration policy allows serving a doc from cache if: //
  //     basic:          (current_age <= fresh_limit)                 //
  //                                                                  //
  // this can be modified by client Cache-Control headers:            //
  //     max-age:        (current_age <= max_age)                     //
  //     min-fresh:      (current_age <= fresh_limit - min_fresh)     //
  //     max-stale:      (current_age <= fresh_limit + max_stale)     //
  //////////////////////////////////////////////////////////////////////
  age_limit = fresh_limit;      // basic constraint
  Debug("http_match", "[..._document_freshness] initial age limit: %d", age_limit);

  cooked_cc_mask = client_request->get_cooked_cc_mask();
  cc_mask = (MIME_COOKED_MASK_CC_MAX_STALE | MIME_COOKED_MASK_CC_MIN_FRESH | MIME_COOKED_MASK_CC_MAX_AGE);
  if (cooked_cc_mask & cc_mask) {

    /////////////////////////////////////////////////
    // if max-stale set, relax the freshness limit //
    /////////////////////////////////////////////////
    if (cooked_cc_mask & MIME_COOKED_MASK_CC_MAX_STALE) {
      if (os_specifies_revalidate) {
        Debug("http_match", "[...document_freshness] OS specifies revalidation; "
              "ignoring client's max-stale request...");
      } else {
        int max_stale_val = client_request->get_cooked_cc_max_stale();

        if (max_stale_val != MAXINT)
          age_limit += max_stale_val;
        else
          age_limit = max_stale_val;
        Debug("http_match", "[..._document_freshness] max-stale set, age limit: %d", age_limit);
      }
    }
    /////////////////////////////////////////////////////
    // if min-fresh set, constrain the freshness limit //
    /////////////////////////////////////////////////////
    if (cooked_cc_mask & MIME_COOKED_MASK_CC_MIN_FRESH) {
      age_limit = min(age_limit, fresh_limit - client_request->get_cooked_cc_min_fresh());
      Debug("http_match", "[..._document_freshness] min_fresh set, age limit: %d", age_limit);
    }
    ///////////////////////////////////////////////////
    // if max-age set, constrain the freshness limit //
    ///////////////////////////////////////////////////
    if (!s->cache_control.ignore_client_cc_max_age && (cooked_cc_mask & MIME_COOKED_MASK_CC_MAX_AGE)) {
      int age_val = client_request->get_cooked_cc_max_age();
      if (age_val == 0)
        do_revalidate = true;
      age_limit = min(age_limit, age_val);
      Debug("http_match", "[..._document_freshness] min_fresh set, age limit: %d", age_limit);
    }
  }
  /////////////////////////////////////////////////////////
  // config file may have a "revalidate_after" field set //
  /////////////////////////////////////////////////////////
  // bug fix changed ">0" to ">=0"
  if (s->cache_control.revalidate_after >= 0) {

    // if we want the minimum of the already-computed age_limit and revalidate_after
//      age_limit = mine(age_limit, s->cache_control.revalidate_after);

    // if instead the revalidate_after overrides all other variables
    age_limit = s->cache_control.revalidate_after;

    Debug("http_match", "[..._document_freshness] revalidate_after set, age limit: %d", age_limit);
  }

  if (diags->on()) {
    DebugOn("http_match", "document_freshness --- current_age = %d", current_age);
    DebugOn("http_match", "document_freshness --- age_limit   = %d", age_limit);
    DebugOn("http_match", "document_freshness --- fresh_limit = %d", fresh_limit);
    DebugOn("http_seq", "document_freshness --- current_age = %d", current_age);
    DebugOn("http_seq", "document_freshness --- age_limit   = %d", age_limit);
    DebugOn("http_seq", "document_freshness --- fresh_limit = %d", fresh_limit);
  }
  ///////////////////////////////////////////
  // now, see if the age is "fresh enough" //
  ///////////////////////////////////////////

  if (do_revalidate || current_age > age_limit) { // client-modified limit
    DebugOn("http_match", "[..._document_freshness] document needs revalidate/too old; "
            "returning FRESHNESS_STALE");
    return (FRESHNESS_STALE);
  } else if (current_age > fresh_limit) {  // original limit
    if (os_specifies_revalidate) {
      DebugOn("http_match", "[..._document_freshness] document is stale and OS specifies revalidation; "
              "returning FRESHNESS_STALE");
      return (FRESHNESS_STALE);
    }
    DebugOn("http_match", "[..._document_freshness] document is stale but no revalidation explicitly required; "
            "returning FRESHNESS_WARNING");
    return (FRESHNESS_WARNING);
  } else {
    DebugOn("http_match", "[..._document_freshness] document is fresh; returning FRESHNESS_FRESH");
    return (FRESHNESS_FRESH);
  }
}

//////////////////////////////////////////////////////////////////////////////
//
//      HttpTransact::Authentication_t HttpTransact::AuthenticationNeeded(
//          const HttpConfigParams *p,
//          HTTPHdr *client_request,
//          HTTPHdr *obj_response)
//
//      This function takes the current client request, and the headers
//      from a potential response (e.g. from cache or proxy), and decides
//      if the object needs to be authenticated with the origin server,
//      before it can be sent to the client.
//
//      The return value describes the authentication process needed.  In
//      this function, three results are possible:
//
//          AUTHENTICATION_SUCCESS              Can serve object directly
//          AUTHENTICATION_MUST_REVALIDATE      Must revalidate with server
//          AUTHENTICATION_MUST_PROXY           Must not serve object
//
//////////////////////////////////////////////////////////////////////////////

HttpTransact::Authentication_t HttpTransact::
AuthenticationNeeded(const HttpConfigParams * p, HTTPHdr * client_request, HTTPHdr * obj_response)
{
  NOWARN_UNUSED(p);

  ///////////////////////////////////////////////////////////////////////
  // from RFC2068, sec 14.8, if a client request has the Authorization //
  // header set, we can't serve it unless the response is public, or   //
  // if it has a Cache-Control revalidate flag, and we do revalidate.  //
  ///////////////////////////////////////////////////////////////////////

  if (client_request->presence(MIME_PRESENCE_AUTHORIZATION) && p->cache_ignore_auth == 0) {
    if (obj_response->is_cache_control_set(HTTP_VALUE_MUST_REVALIDATE) ||
        obj_response->is_cache_control_set(HTTP_VALUE_PROXY_REVALIDATE)) {
      return AUTHENTICATION_MUST_REVALIDATE;
    } else if (obj_response->is_cache_control_set(HTTP_VALUE_PROXY_REVALIDATE)) {
      return AUTHENTICATION_MUST_REVALIDATE;
    } else if (obj_response->is_cache_control_set(HTTP_VALUE_PUBLIC)) {
      return AUTHENTICATION_SUCCESS;
    } else {
      if (obj_response->field_find("@WWW-Auth", 9) && client_request->method_get_wksidx() == HTTP_WKSIDX_GET)
        return AUTHENTICATION_CACHE_AUTH;
      return AUTHENTICATION_MUST_PROXY;
    }
  }

  if (obj_response->field_find("@WWW-Auth", 9) && client_request->method_get_wksidx() == HTTP_WKSIDX_GET)
    return AUTHENTICATION_CACHE_AUTH;

  return (AUTHENTICATION_SUCCESS);
}

void
HttpTransact::handle_parent_died(State * s)
{
  ink_assert(s->parent_result.r == PARENT_FAIL);

  build_error_response(s,
                       HTTP_STATUS_BAD_GATEWAY,
                       "Next Hop Connection Failed", "connect#failed_connect", "Next Hop Connection Failed");

  TRANSACT_RETURN(PROXY_SEND_ERROR_CACHE_NOOP, NULL);
}

void
HttpTransact::handle_server_died(State * s)
{
  char *reason = NULL;
  char *body_type = "UNKNOWN";
  HTTPStatus status = HTTP_STATUS_BAD_GATEWAY;

  ////////////////////////////////////////////////////////
  // FIX: all the body types below need to be filled in //
  ////////////////////////////////////////////////////////


  //
  // congestion control
  //
  if (s->pCongestionEntry != NULL) {
    s->congestion_congested_or_failed = 1;
    if (s->current.state != CONGEST_CONTROL_CONGESTED_ON_F && s->current.state != CONGEST_CONTROL_CONGESTED_ON_M) {
      s->pCongestionEntry->failed_at(s->current.now);
    }
  }

  switch (s->current.state) {
  case CONNECTION_ALIVE:       /* died while alive for unknown reason */
    HTTP_ASSERT(s->hdr_info.response_error != NO_RESPONSE_HEADER_ERROR);
    status = HTTP_STATUS_BAD_GATEWAY;
    reason = "Unknown Error";
    body_type = "response#bad_response";
    break;
  case CONNECTION_ERROR:
    status = HTTP_STATUS_BAD_GATEWAY;
    reason = (char *) get_error_string(s->cause_of_death_errno);
    body_type = "connect#failed_connect";
    break;
  case OPEN_RAW_ERROR:
    status = HTTP_STATUS_BAD_GATEWAY;
    reason = "Tunnel Connection Failed";
    body_type = "connect#failed_connect";
    break;
  case FTP_OPEN_FAILED:
    status = HTTP_STATUS_BAD_GATEWAY;
    reason = "FTP Connection Failed";
    body_type = "connect#failed_connect";
    break;
  case CONNECTION_CLOSED:
    status = HTTP_STATUS_BAD_GATEWAY;
    reason = "Server Hangup";
    body_type = "connect#hangup";
    break;
  case ACTIVE_TIMEOUT:
    if (s->api_txn_active_timeout)
      Debug("http_timeout", "Maximum active time of %d msec exceeded", s->api_txn_active_timeout_value);
    status = HTTP_STATUS_GATEWAY_TIMEOUT;
    reason = "Maximum Transaction Time Exceeded";
    body_type = "timeout#activity";
    break;
  case INACTIVE_TIMEOUT:
    if (s->api_txn_connect_timeout)
      Debug("http_timeout", "Maximum connect time of %d msec exceeded", s->api_txn_connect_timeout_value);
    status = HTTP_STATUS_GATEWAY_TIMEOUT;
    reason = "Connection Timed Out";
    body_type = "timeout#inactivity";
    break;
  case PARSE_ERROR:
  case BAD_INCOMING_RESPONSE:
    status = HTTP_STATUS_BAD_GATEWAY;
    reason = "Invalid HTTP Response";
    body_type = "response#bad_response";
    break;
  case CONGEST_CONTROL_CONGESTED_ON_F:
    status = HTTP_STATUS_SERVICE_UNAVAILABLE;
    reason = "Origin server congested";
    if (s->pCongestionEntry)
      body_type = s->pCongestionEntry->getErrorPage();
    else
      body_type = "congestion#retryAfter";
    s->hdr_info.response_error = TOTAL_RESPONSE_ERROR_TYPES;
    break;
  case CONGEST_CONTROL_CONGESTED_ON_M:
    status = HTTP_STATUS_SERVICE_UNAVAILABLE;
    reason = "Too many users";
    if (s->pCongestionEntry)
      body_type = s->pCongestionEntry->getErrorPage();
    else
      body_type = "congestion#retryAfter";
    s->hdr_info.response_error = TOTAL_RESPONSE_ERROR_TYPES;
    break;
  case STATE_UNDEFINED:
  case TRANSACTION_COMPLETE:
  default:                     /* unknown death */
    HTTP_ASSERT(!"[handle_server_died] Unreasonable state - not dead, shouldn't be here");
    status = HTTP_STATUS_BAD_GATEWAY;
    reason = NULL;
    body_type = "response#bad_response";
    break;
  }

  if (s->pCongestionEntry && s->pCongestionEntry->F_congested() && status != HTTP_STATUS_SERVICE_UNAVAILABLE) {
    s->pCongestionEntry->stat_inc_F();
    CONGEST_SUM_GLOBAL_DYN_STAT(congested_on_F_stat, 1);
    status = HTTP_STATUS_SERVICE_UNAVAILABLE;
    reason = "Service Unavailable";
    body_type = s->pCongestionEntry->getErrorPage();
    s->hdr_info.response_error = TOTAL_RESPONSE_ERROR_TYPES;
  }
  ////////////////////////////////////////////////////////
  // FIX: comment stuff above and below here, not clear //
  ////////////////////////////////////////////////////////

  switch (s->hdr_info.response_error) {
  case FTP_CONNECTION_OPEN_FAILED:
    status = HTTP_STATUS_BAD_GATEWAY;
    reason = "FTP Error";
    body_type = "ftp#error";
    break;
  case FTP_LOGIN_INCORRECT:
    status = HTTP_STATUS_UNAUTHORIZED;
    reason = "FTP Authentication Required";
    body_type = "ftp#auth_required";
    break;
  case NON_EXISTANT_RESPONSE_HEADER:
    status = HTTP_STATUS_BAD_GATEWAY;
    reason = "No Response Header From Server";
    body_type = "response#bad_response";
    break;
  case MISSING_REASON_PHRASE:
  case NO_RESPONSE_HEADER_ERROR:
  case NOT_A_RESPONSE_HEADER:
#ifdef REALLY_NEED_TO_CHECK_DATE_VALIDITY
  case BOGUS_OR_NO_DATE_IN_RESPONSE:
#endif
    status = HTTP_STATUS_BAD_GATEWAY;
    reason = "Malformed Server Response";
    body_type = "response#bad_response";
    break;
  case MISSING_STATUS_CODE:
    HTTP_ASSERT(s->next_hop_scheme != URL_WKSIDX_FTP);
    status = HTTP_STATUS_BAD_GATEWAY;
    reason = "Malformed Server Response Status";
    body_type = "response#bad_response";
    break;
  default:
    break;
  }

  if (reason == NULL) {
    status = HTTP_STATUS_BAD_GATEWAY;
    reason = "Server Connection Failed";
    body_type = "connect#failed_connect";
  }
  /////////////////////////////////////////////////
  // now add a default body, which is either the //
  // ftp error text or the reason phrase         //
  /////////////////////////////////////////////////
  char *body_text;
  if ((s->next_hop_scheme == URL_WKSIDX_FTP) && (s->ftp_info->error_msg))
    body_text = s->ftp_info->error_msg;
  else
    body_text = reason;         // Reason can never be NULL here (see above)
  build_error_response(s, status, reason, body_type, body_text);

  return;
}

// return true if the response to the given request is likely cacheable
// This function is called by build_request() to determine if the conditional
// headers should be removed from server request.
bool
HttpTransact::is_request_likely_cacheable(State * s, HTTPHdr * request)
{
  if ((s->method == HTTP_WKSIDX_GET) && !request->presence(MIME_PRESENCE_AUTHORIZATION)) {
    return true;
  }
  return false;
}

void
HttpTransact::build_request(State * s, HTTPHdr * base_request, HTTPHdr * outgoing_request, HTTPVersion outgoing_version)
{
  // this part is to restore the original URL in case, multiple cache
  // lookups have happened - client request has been changed as the result
  //
  // notice that currently, based_request IS client_request
  if (base_request == &s->hdr_info.client_request) {
    if (s->redirect_info.redirect_in_process) {
      // this is for auto redirect
      URL *r_url = &(s->redirect_info.redirect_url);
      ink_assert(r_url->valid());
      base_request->url_get()->copy(r_url);
    } else {
      // this is for multiple cache lookup
      URL *o_url = &(s->cache_info.original_url);
      if (o_url->valid())
        base_request->url_get()->copy(o_url);
    }
  }
  HttpTransactHeaders::copy_header_fields(base_request, outgoing_request,
                                          s->http_config_param->fwd_proxy_auth_to_parent);

  add_client_ip_to_outgoing_request(s, outgoing_request);

  HttpTransactHeaders::process_connection_headers(base_request, outgoing_request);

  HttpTransactHeaders::remove_privacy_headers_from_request(s->http_config_param, outgoing_request);

  HttpTransactHeaders::add_global_user_agent_header_to_request(s->http_config_param, outgoing_request);

  handle_request_keep_alive_headers(s, outgoing_version, outgoing_request);
  HttpTransactHeaders::handle_conditional_headers(&s->cache_info, outgoing_request);

  if (s->next_hop_scheme < 0)
    s->next_hop_scheme = URL_WKSIDX_HTTP;
  if (s->orig_scheme < 0)
    s->orig_scheme = URL_WKSIDX_HTTP;
  HttpTransactHeaders::insert_via_header_in_request(s->http_config_param,
                                                    s->orig_scheme,
                                                    &s->cache_info,
                                                    outgoing_request, s->via_string, this_machine()->ip);

  // We build 1.1 request header and then convert as necessary to
  //  the appropriate version in HttpTransact::build_request
  outgoing_request->version_set(HTTPVersion(1, 1));

  // Make sure our request version is defined
  HTTP_DEBUG_ASSERT(outgoing_version != HTTPVersion(0, 0));

  // HttpTransactHeaders::convert_request(outgoing_version, outgoing_request); // commented out this idea


  // Check whether a Host header field is missing from a 1.0 or 1.1 request.
  if (outgoing_version != HTTPVersion(0, 9) && !outgoing_request->presence(MIME_PRESENCE_HOST)) {
    URL *url = outgoing_request->url_get();


    int host_len;
    const char *host = url->host_get(&host_len);

    // Add a ':port' to the HOST header if the request is not going 
    // to the default port.
    int port = url->port_get();
    if (port != url_canonicalize_port(URL_TYPE_HTTP, 0)) {
      char *buf = (char *) xmalloc(host_len + 15);
      strncpy(buf, host, host_len);
      host_len += ink_snprintf(buf + host_len, host_len + 15, ":%d", port);
      outgoing_request->value_set(MIME_FIELD_HOST, MIME_LEN_HOST, buf, host_len);
      xfree(buf);
    } else {
      outgoing_request->value_set(MIME_FIELD_HOST, MIME_LEN_HOST, host, host_len);
    }
  }

  if (s->current.server == &s->server_info &&
      (s->next_hop_scheme == URL_WKSIDX_HTTP || s->next_hop_scheme == URL_WKSIDX_HTTPS)) {
    HttpTransactHeaders::remove_host_name_from_url(outgoing_request);
  }
  // If the response is most likely not cacheable, eg, request with Authorization,
  // do we really want to remove conditional headers to get large 200 response?
  // Answer: NO.  Since if the response is most likely not cacheable,
  // we don't remove conditional headers so that for a non-200 response
  // from the O.S., we will save bandwidth between proxy and O.S.
  if (s->current.mode == GENERIC_PROXY) {
    if (is_request_likely_cacheable(s, base_request)) {
      if (s->http_config_param->cache_when_to_revalidate != 4) {
        Debug("http_trans", "[build_request] " "request like cacheable and conditional headers removed");
        HttpTransactHeaders::remove_conditional_headers(base_request, outgoing_request);
      } else
        Debug("http_trans", "[build_request] " "request like cacheable but keep conditional headers");
    } else {
      // In this case, we send a conditional request
      // instead of the normal non-conditional request.
      Debug("http_trans", "[build_request] " "request not like cacheable and conditional headers not removed");
    }
  }

  s->request_sent_time = ink_cluster_time();
  s->current.now = s->request_sent_time;
  // The assert is backwards in this case because request is being (re)sent.
  HTTP_DEBUG_ASSERT(s->request_sent_time >= s->response_received_time);


  Debug("http_trans", "[build_request] request_sent_time: %ld", s->request_sent_time);
  if (!s->cop_test_page)
    DUMP_HEADER("http_hdrs", outgoing_request, s->state_machine_id, "Proxy's Request");

  HTTP_INCREMENT_TRANS_STAT(http_outgoing_requests_stat);

}

// build a (status_code) response based upon the given info


void
HttpTransact::build_response(State * s, HTTPHdr * base_response,
                             HTTPHdr * outgoing_response, HTTPVersion outgoing_version)
{
  build_response(s, base_response, outgoing_response, outgoing_version, HTTP_STATUS_NONE, NULL);
  return;
}


void
HttpTransact::build_response(State * s,
                             HTTPHdr * outgoing_response,
                             HTTPVersion outgoing_version, HTTPStatus status_code, const char *reason_phrase)
{
  build_response(s, NULL, outgoing_response, outgoing_version, status_code, reason_phrase);
  return;
}

void
HttpTransact::build_response(State * s,
                             HTTPHdr * base_response,
                             HTTPHdr * outgoing_response,
                             HTTPVersion outgoing_version, HTTPStatus status_code, const char *reason_phrase)
{

  if (reason_phrase == NULL) {
    reason_phrase = HttpMessageBody::StatusCodeName(status_code);
  }

  if (base_response == NULL) {

    HttpTransactHeaders::build_base_response(outgoing_response,
                                             status_code, reason_phrase, strlen(reason_phrase), s->current.now);
  } else {

    if ((status_code == HTTP_STATUS_NONE) || (status_code == base_response->status_get())) {
      HttpTransactHeaders::copy_header_fields(base_response,
                                              outgoing_response, s->http_config_param->fwd_proxy_auth_to_parent);
      HttpTransactHeaders::process_connection_headers(base_response, outgoing_response);

      if (s->http_config_param->insert_age_in_response)
        HttpTransactHeaders::insert_time_and_age_headers_in_response(s->
                                                                     request_sent_time,
                                                                     s->
                                                                     response_received_time,
                                                                     s->current.now, base_response, outgoing_response);

      // Note: We need to handle the "Content-Length" header first here
      //  since handle_content_length_header()
      //  determines whether we accept origin server's content-length.
      //  We need to have made a decision regard the content-length
      //  before processing the keep_alive headers
      //
      handle_content_length_header(s, outgoing_response, base_response);
    } else
      switch (status_code) {
      case HTTP_STATUS_NOT_MODIFIED:
        HttpTransactHeaders::build_base_response(outgoing_response,
                                                 status_code, reason_phrase, strlen(reason_phrase), s->current.now);

        // According to RFC 2616, Section 10.3.5,
        // a 304 response MUST contain Date header,
        // Etag and/or Content-location header,
        // and Expires, Cache-control, and Vary
        // (if they might be changed).
        // Since a proxy doesn't know if a header differs from
        // a user agent's cached document or not, all are sent.
        {
          const char *field_name[] = { MIME_FIELD_ETAG,
            MIME_FIELD_CONTENT_LOCATION,
            MIME_FIELD_EXPIRES,
            MIME_FIELD_CACHE_CONTROL,
            MIME_FIELD_VARY
          };
          int field_len[] = { MIME_LEN_ETAG,
            MIME_LEN_CONTENT_LOCATION,
            MIME_LEN_EXPIRES,
            MIME_LEN_CACHE_CONTROL,
            MIME_LEN_VARY
          };
          inku64 field_presence[] = { MIME_PRESENCE_ETAG,
            MIME_PRESENCE_CONTENT_LOCATION,
            MIME_PRESENCE_EXPIRES,
            MIME_PRESENCE_CACHE_CONTROL,
            MIME_PRESENCE_VARY
          };
          MIMEField *field;
          int len;
          const char *value;
          for (size_t i = 0; i < sizeof(field_len) / sizeof(field_len[0]); i++) {
            if (base_response->presence(field_presence[i])) {
              field = base_response->field_find(field_name[i], field_len[i]);
              value = field->value_get(&len);
              outgoing_response->value_append(field_name[i], field_len[i], value, len, 0);
            }
          }
        }
        break;

      case HTTP_STATUS_PRECONDITION_FAILED:
        // fall through
      case HTTP_STATUS_RANGE_NOT_SATISFIABLE:
        HttpTransactHeaders::build_base_response(outgoing_response,
                                                 status_code, reason_phrase, strlen(reason_phrase), s->current.now);
        break;
      default:
        // ink_assert(!"unexpected status code in build_response()");
        break;
      }
  }

  // the following is done whether base_response == NULL or not

  // If the response is prohibited from containing a body,
  //  we know the content length is trustable for keep-alive
  if (is_response_body_precluded(status_code, s->method)) {
    s->hdr_info.trust_response_cl = true;
  }

  handle_response_keep_alive_headers(s, outgoing_version, outgoing_response);

  if (s->next_hop_scheme < 0)
    s->next_hop_scheme = URL_WKSIDX_HTTP;
  HttpTransactHeaders::insert_via_header_in_response(s->http_config_param,
                                                     s->next_hop_scheme,
                                                     &s->cache_info, outgoing_response, s->via_string);

  HttpTransactHeaders::convert_response(outgoing_version, outgoing_response);

#ifndef INK_NO_REMAP
  // process reverse mappings on the location header
  HTTPStatus outgoing_status = outgoing_response->status_get();
  if ((outgoing_status != 200) && (((outgoing_status >= 300) && (outgoing_status < 400)) || (outgoing_status == 201))) {
    response_url_remap(outgoing_response);
  }
#endif //INK_NO_REMAP

  if (s->http_config_param->enable_http_stats) {
    if (s->hdr_info.server_response.valid() && s->http_config_param->wuts_enabled) {
      int reason_len;
      const char *reason = s->hdr_info.server_response.reason_get(&reason_len);
      if (reason != NULL && reason_len > 0)
        outgoing_response->reason_set(reason, reason_len);
    }

    HttpTransactHeaders::generate_and_set_wuts_codes(outgoing_response,
                                                     s->via_string,
                                                     &(s->squid_codes),
                                                     WUTS_PROXY_ID, ((s->http_config_param->wuts_enabled)
                                                                     ? (true) : (false)),
                                                     ((s->http_config_param->log_spider_codes)
                                                      ? (true) : (false)));
  }
  // NTLM proxy pass-through
  if ((status_code == HTTP_STATUS_UNAUTHORIZED ||
       // if build_response is called from
       // handle_no_cache_operation_on_forward_server_response,
       // the status_code passed is HTTP_STATUS_NONE
       (status_code == HTTP_STATUS_NONE &&
        s->hdr_info.server_response.valid() &&
        s->hdr_info.server_response.status_get() ==
        HTTP_STATUS_UNAUTHORIZED)) &&
      !s->http_config_param->reverse_proxy_enabled && !s->http_config_param->transparency_enabled) {
    MIMEField *auth_hdr = outgoing_response->field_find(MIME_FIELD_WWW_AUTHENTICATE,
                                                        MIME_LEN_WWW_AUTHENTICATE);
    if (auth_hdr) {
      int auth_len;
      const char *auth_value = auth_hdr->value_get(&auth_len);
      if (auth_len >= 9 && strncasecmp(auth_value, "Negotiate", 9) == 0) {
        auth_hdr = auth_hdr->m_next_dup;
        if (auth_hdr)
          auth_value = auth_hdr->value_get(&auth_len);
        else
          auth_len = 0;
      }
      if ((auth_len == 4 && memcmp(auth_value, "NTLM", 4) == 0) ||
          (auth_len > 5 && memcmp(auth_value, "NTLM ", 5) == 0)) {
        // Network Analyzer result based on MS ISA
        outgoing_response->value_set("Proxy-Support", 13, "Session-Based-Authentication", 28);
        s->state_machine->ua_session->session_based_auth = true;
      }
    }
  }

  HttpTransactHeaders::add_server_header_to_response(s->http_config_param, outgoing_response);

  // auth-response update
 // if (!s->state_machine->authAdapter.disabled()) {
  //  s->state_machine->authAdapter.UpdateResponseHeaders(outgoing_response);
 // }

  if (diags->on()) {
    if (base_response) {
      if (!s->cop_test_page)
        DUMP_HEADER("http_hdrs", base_response, s->state_machine_id, "Base Header for Building Response");
    }
    if (!s->cop_test_page)
      DUMP_HEADER("http_hdrs", outgoing_response, s->state_machine_id, "Proxy's Response");
  }

  return;
}

//////////////////////////////////////////////////////////////////////////////
//
//      void HttpTransact::build_error_response(
//          State *s,
//          HTTPStatus status_code,
//          char *reason_phrase_or_null,
//          char *error_body_type,
//          char *format, ...)
//
//      This method sets the requires state for an error reply, including
//      the error text, status code, reason phrase, and reply headers.  The
//      caller calls the method with the HttpTransact::State <s>, the
//      HTTP status code <status_code>, a user-specified reason phrase
//      string (or NULL) <reason_phrase_or_null>, and a printf-like
//      text format and arguments which are appended to the error text.
//
//      The <error_body_type> is the error message type, as specified by
//      the HttpBodyFactory customized error page system.
//
//      If the descriptive text <format> is not NULL or "", it is also
//      added to the error text body as descriptive text in the error body.
//      If <reason_phrase_or_null> is NULL, the default HTTP reason phrase
//      is used.  This routine DOES NOT check for buffer overflows.  The
//      caller should keep the messages small to be sure the error text
//      fits in the error buffer (ok, it's nasty, but at least I admit it!).
//
//////////////////////////////////////////////////////////////////////////////
void
HttpTransact::build_error_response(State * s,
                                   HTTPStatus status_code,
                                   char *reason_phrase_or_null, char *error_body_type, char *format, ...)
{
  va_list ap;
  char *reason_phrase;
  URL *url;
  char *url_string;
  char body_language[256], body_type[256];

  ////////////////////////////////////////////////////////////
  // get the url --- remember this is dynamically allocated //
  ////////////////////////////////////////////////////////////
  if (s->hdr_info.client_request.valid()) {
    url = s->hdr_info.client_request.url_get();
    url_string = url ? url->string_get(&s->arena) : NULL;
  } else {
    url_string = NULL;
  }

  //////////////////////////////////////////////////////
  //  If there is a request body, we must disable     //
  //  keep-alive to prevent the body being read as    //
  //  the next header (unless we've already drained   //
  //  which we do for NTLM auth)                      //
  //////////////////////////////////////////////////////
  if (s->hdr_info.request_content_length != 0 &&
      s->state_machine->client_request_body_bytes < s->hdr_info.request_content_length) {
    s->client_info.keep_alive = HTTP_NO_KEEPALIVE;
  } else {
    // We don't have a request body.  Since we are
    //  generating the error, we know we can trust
    //  the content-length
    s->hdr_info.trust_response_cl = true;
  }

  switch (status_code) {
  case HTTP_STATUS_BAD_REQUEST:
    SET_VIA_STRING(VIA_CLIENT_REQUEST, VIA_CLIENT_ERROR);
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_HEADER_SYNTAX);
    break;
  case HTTP_STATUS_BAD_GATEWAY:
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_CONNECTION);
    break;
  case HTTP_STATUS_GATEWAY_TIMEOUT:
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_TIMEOUT);
    break;
  case HTTP_STATUS_NOT_FOUND:
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_SERVER);
    break;
  case HTTP_STATUS_FORBIDDEN:
    SET_VIA_STRING(VIA_CLIENT_REQUEST, VIA_CLIENT_ERROR);
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_FORBIDDEN);
    break;
  case HTTP_STATUS_HTTPVER_NOT_SUPPORTED:
    SET_VIA_STRING(VIA_CLIENT_REQUEST, VIA_CLIENT_ERROR);
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_SERVER);
    break;
  case HTTP_STATUS_INTERNAL_SERVER_ERROR:
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_DNS_FAILURE);
    break;
  case HTTP_STATUS_MOVED_TEMPORARILY:
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_SERVER);
    break;
  case HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED:
    SET_VIA_STRING(VIA_CLIENT_REQUEST, VIA_CLIENT_ERROR);
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_AUTHORIZATION);
    break;
  case HTTP_STATUS_UNAUTHORIZED:
    SET_VIA_STRING(VIA_CLIENT_REQUEST, VIA_CLIENT_ERROR);
    SET_VIA_STRING(VIA_ERROR_TYPE, VIA_ERROR_AUTHORIZATION);
    break;
  default:
    break;
  }

  va_start(ap, format);
  reason_phrase =
    (reason_phrase_or_null ? reason_phrase_or_null : (char *) (HttpMessageBody::StatusCodeName(status_code)));
  if (unlikely(!reason_phrase))
    reason_phrase = "Unknown HTTP Status";

  // set the source to internal so that chunking is handled correctly
  s->source = SOURCE_INTERNAL;
  build_response(s, &s->hdr_info.client_response, s->client_info.http_version, status_code, reason_phrase);

  if (status_code == HTTP_STATUS_SERVICE_UNAVAILABLE) {
    if (s->pCongestionEntry != NULL) {
      int ret_tmp;
      int retry_after = s->pCongestionEntry->client_retry_after();
      s->congestion_control_crat = retry_after;
      if (s->hdr_info.client_response.value_get(MIME_FIELD_RETRY_AFTER, MIME_LEN_RETRY_AFTER, &ret_tmp) == NULL)
        s->hdr_info.client_response.value_set_int(MIME_FIELD_RETRY_AFTER, MIME_LEN_RETRY_AFTER, retry_after);
    }
  }
  if (status_code == HTTP_STATUS_PROXY_AUTHENTICATION_REQUIRED &&
      s->method == HTTP_WKSIDX_CONNECT && s->hdr_info.client_response.presence(MIME_PRESENCE_PROXY_CONNECTION)) {
    int has_ua_msie = 0;
    int user_agent_value_len, slen;
    const char *user_agent_value, *c, *e;
    user_agent_value =
      s->hdr_info.client_request.value_get(MIME_FIELD_USER_AGENT, MIME_LEN_USER_AGENT, &user_agent_value_len);
    if (user_agent_value && user_agent_value_len >= 4) {
      c = user_agent_value;
      e = c + user_agent_value_len - 4;
      while (1) {
        slen = (int) (e - c);
        c = (const char *) memchr(c, 'M', slen);
        if (c == NULL || (e - c) < 3)
          break;
        if ((c[1] == 'S') && (c[2] == 'I') && (c[3] == 'E')) {
          has_ua_msie = 1;
          break;
        }
        c++;
      }
    }

    if (has_ua_msie)
      s->hdr_info.client_response.value_set(MIME_FIELD_PROXY_CONNECTION, MIME_LEN_PROXY_CONNECTION, "close", 5);
  }
  // Add a bunch of headers to make sure that caches between
  // the Traffic Server and the client do not cache the error
  // page.
  s->hdr_info.client_response.value_set(MIME_FIELD_CACHE_CONTROL, MIME_LEN_CACHE_CONTROL, "no-store", 8);
  // Make sure there are no Expires and Last-Modified headers.
  s->hdr_info.client_response.field_delete(MIME_FIELD_EXPIRES, MIME_LEN_EXPIRES);
  s->hdr_info.client_response.field_delete(MIME_FIELD_LAST_MODIFIED, MIME_LEN_LAST_MODIFIED);


  /////////////////////////////////////////////////////////////
  // deallocate any existing response body --- it's possible //
  // that we have previous created an error msg but retried  //
  // the connection and are now making a new one.            //
  /////////////////////////////////////////////////////////////
  s->internal_msg_buffer_index = 0;
  if (s->internal_msg_buffer) {
    free_internal_msg_buffer(s->internal_msg_buffer, s->internal_msg_buffer_fast_allocator_size);
  }
  s->internal_msg_buffer_fast_allocator_size = -1;

  ////////////////////////////////////////////////////////////////////
  // create the error message using the "body factory", which will  //
  // build a customized error message if available, or generate the //
  // old style internal defaults otherwise --- the body factory     //
  // supports language targeting using the Accept-Language header   //
  ////////////////////////////////////////////////////////////////////

  s->internal_msg_buffer =
    body_factory->fabricate_with_old_api(error_body_type, s, 8192,
                                         &(s->internal_msg_buffer_size),
                                         body_language, body_type, status_code, reason_phrase, format, ap);

  s->hdr_info.client_response.value_set(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE, body_type, strlen(body_type));

  s->hdr_info.client_response.value_set(MIME_FIELD_CONTENT_LANGUAGE,
                                        MIME_LEN_CONTENT_LANGUAGE, body_language, strlen(body_language));

  ////////////////////////////////////////
  // log a description in the error log //
  ////////////////////////////////////////

  if (s->current.state == CONNECTION_ERROR) {
    char *reason_buffer;
    int buf_len = sizeof(char) * (strlen(get_error_string(s->cause_of_death_errno)) + 50);
    reason_buffer = (char *) alloca(buf_len);
    ink_snprintf(reason_buffer, buf_len, "Connect Error <%s/%d>", get_error_string(s->cause_of_death_errno),
                 s->cause_of_death_errno);
    reason_phrase = reason_buffer;
  }

  if (s->http_config_param->errors_log_error_pages) {
    if (!s->traffic_net_req) {
      char ip_string[128];
      unsigned char *p = (unsigned char *) &(s->client_info.ip);

      snprintf(ip_string, sizeof(ip_string), "%d.%d.%d.%d", p[0], p[1], p[2], p[3]);
      Log::error("RESPONSE: sent %s status %d (%s) for '%s'",
                 ip_string, status_code, reason_phrase, (url_string ? url_string : "<none>"));
    }
  }

  if (url_string) {
    s->arena.str_free(url_string);
  }

  s->next_action = PROXY_SEND_ERROR_CACHE_NOOP;
  return;
}

void
HttpTransact::build_redirect_response(State * s)
{
  Debug("http_redirect", "[HttpTransact::build_redirect_response]");
  URL *u;
  const char *old_host;
  int old_host_len;
  char *new_url = NULL;
  int new_url_len;
  char *to_free = NULL;

  char body_language[256], body_type[256];

  HTTPStatus status_code = HTTP_STATUS_MOVED_TEMPORARILY;
  char *reason_phrase = (char *) (HttpMessageBody::StatusCodeName(status_code));

  build_response(s, &s->hdr_info.client_response, s->client_info.http_version, status_code, reason_phrase);

  //////////////////////////////////////////////////////////
  // figure out what new url should be.  this little hack //
  // inserts expanded hostname into old url in order to   //
  // get scheme information, then puts the old url back.  //
  //////////////////////////////////////////////////////////
  u = s->hdr_info.client_request.url_get();
  old_host = u->host_get(&old_host_len);
  u->host_set(s->dns_info.lookup_name, strlen(s->dns_info.lookup_name));
  new_url = to_free = u->string_get(&s->arena, &new_url_len);
  if (new_url == NULL) {
    new_url = "";
  }
  u->host_set(old_host, old_host_len);

  //////////////////////////
  // set redirect headers //
  //////////////////////////
  HTTPHdr *h = &s->hdr_info.client_response;
  if (s->http_config_param->insert_response_via_string) {
    const char pa[] = "Proxy-agent";
    h->value_append(pa, sizeof(pa) - 1,
                    s->http_config_param->proxy_response_via_string,
                    s->http_config_param->proxy_response_via_string_len);
  }
  h->value_set(MIME_FIELD_LOCATION, MIME_LEN_LOCATION, new_url, new_url_len);

  //////////////////////////
  // set descriptive text //
  //////////////////////////
  s->internal_msg_buffer_index = 0;
  if (s->internal_msg_buffer) {
    free_internal_msg_buffer(s->internal_msg_buffer, s->internal_msg_buffer_fast_allocator_size);
  }
  s->internal_msg_buffer_fast_allocator_size = -1;

  s->internal_msg_buffer =
    body_factory->
    fabricate_with_old_api_build_va("redirect#moved_temporarily", s, 8192,
                                    &(s->internal_msg_buffer_size),
                                    body_language, body_type, status_code,
                                    reason_phrase,
                                    "%s <a href=\"%s\">%s</a>.  %s.",
                                    "The document you requested is now",
                                    new_url, new_url, "Please update your documents and bookmarks accordingly", NULL);


  h->set_content_length(s->internal_msg_buffer_size);
  h->value_set(MIME_FIELD_CONTENT_TYPE, MIME_LEN_CONTENT_TYPE, "text/html", 9);

  s->arena.str_free(to_free);
}

const char *
HttpTransact::get_error_string(int erno)
{
  if (erno >= 0) {
    return (strerror(erno));
  } else {
    switch (-erno) {
    case ENET_THROTTLING:
      return ("throttling");
    case ESOCK_DENIED:
      return ("socks error - denied");
    case ESOCK_TIMEOUT:
      return ("socks error - timeout");
    case ESOCK_NO_SOCK_SERVER_CONN:
      return ("socks error - no server connection");
//              this assumes that the following case occurs
//              when HttpSM.cc::state_origin_server_read_response
//                 receives an HTTP_EVENT_EOS. (line 1729 in HttpSM.cc,
//                 version 1.145.2.13.2.57)
    case UNKNOWN_INTERNAL_ERROR:
      return ("internal error - server connection terminated");
    default:
      return ("");
    }
  }
}

#ifndef INK_NO_FTP
/////////////////////////////////////////////////////////////////////////
// Somewhat nasty of a solution for translating reply codes into
// reason phrases for ftp servers that do not provide them. This 
// is rare, but I lifted these directly from the FTP rfc. If nothing
// else this will prevent us from serving error pages with no error
// string. 
// I went ahead a listed all the reply codes, not just the errors below.
// nasty, but I needed a quick solution.
/////////////////////////////////////////////////////////////////////////
const char *
HttpTransact::FtpReplyCodeToReasonPhrase(int code)
{
  char *msg = "Unspecified Reply";

  switch (code) {
  case 110:
    msg = "Restart marker reply";
    break;
  case 120:
    msg = "Service ready shortly, please try again later";
    break;
  case 125:
    msg = "Data connection already open; transfer starting";
    break;
  case 150:
    msg = "File status okay; about to open data connection";
    break;
  case 200:
    msg = "Command okay";
    break;
  case 202:
    msg = "Command not implemented";
    break;
  case 211:
    msg = "System status";
    break;
  case 212:
    msg = "Directory status";
    break;
  case 213:
    msg = "File status";
    break;
  case 214:
    msg = "Help message";
    break;
  case 215:
    msg = "NAME system type";
    break;
  case 220:
    msg = "Service ready for new user";
    break;
  case 221:
    msg = "Service closing control connection";
    break;
  case 225:
    msg = "Data connection open; no transfer in progress";
    break;
  case 226:
    msg = "Closing data connection. Requested file action successful";
    break;
  case 227:
    msg = "Entering Passive Mode";
    break;
  case 230:
    msg = "User logged in, proceed";
    break;
  case 250:
    msg = "Requested file action completed";
    break;
  case 257:
    msg = "PATHNAME created";
    break;
  case 331:
    msg = "User name okay, need password";
    break;
  case 332:
    msg = "Need account for login";
    break;
  case 350:
    msg = "Requested file action pending further information";
    break;
  case 421:
    msg = "Service not available";
    break;
  case 425:
    msg = "Can't open data connection";
    break;
  case 426:
    msg = "Connection closed; transfer aborted";
    break;
  case 450:
    msg = "Request failed, file unavailable";
    break;
  case 451:
    msg = "Request aborted; local error in processing";
    break;
  case 452:
    msg = "Request aborted; Insufficient storage space on system";
    break;
  case 500:
    msg = "Syntax error, command unrecognized";
    break;
  case 501:
    msg = "Syntax error in parameters or arguments";
    break;
  case 502:
    msg = "Command not implemented";
    break;
  case 503:
    msg = "Bad sequence of commands";
    break;
  case 504:
    msg = "Command not implemented for given parameter";
    break;
  case 530:
    msg = "Not logged in";
    break;
  case 532:
    msg = "Need account for storing files";
    break;
  case 550:
    msg = "File not found or access is denied";
    break;
  case 551:
    msg = "Request aborted; page type unknown";
    break;
  case 552:
    msg = "Request aborted; Exceeded storage allocation";
    break;
  case 553:
    msg = "Request aborted; file name not allowed";
    break;
  default:
    msg = "Unspecified Reply";
    break;
  }
  return (msg);
}

void
HttpTransact::SetFtpErrorMessage(State * s, IOBufferReader * error_message_reader)
{
  int message_len = error_message_reader->read_avail();

  if (s->ftp_info->error_msg) {
    xfree(s->ftp_info->error_msg);
  }
  if (message_len > 0) {
    s->ftp_info->error_msg = (char *) xmalloc(message_len + 1);
    error_message_reader->memcpy(s->ftp_info->error_msg, message_len, 0);
    s->ftp_info->error_msg[message_len] = '\0';
  } else {
    ////////////////////////////
    // no error message input //
    ////////////////////////////
    s->ftp_info->error_msg = 0;
  }

  if (!s->ftp_info->error_msg || strcmp(s->ftp_info->error_msg, "") == 0) {
    ////////////////////////////////////////////
    // There was no message text from the ftp //
    // server, or there was an ftp connection //
    // error. use our own message text.       //
    ////////////////////////////////////////////
    const char *msg = FtpReplyCodeToReasonPhrase(s->ftp_info->last_reply_code);
    if (msg) {
      if (s->ftp_info->error_msg) {
        xfree(s->ftp_info->error_msg);
      }
      s->ftp_info->error_msg = xstrdup(msg);
    }
  }

  return;
}
#endif //INK_NO_FTP

volatile ink_time_t global_time;
volatile ink32 cluster_time_delta;

ink_time_t
ink_cluster_time(void)
{
  ink_time_t old;
  int highest_delta = 0;

#ifdef DEBUG
  ink_mutex_acquire(&http_time_lock);
  ink_time_t local_time = time(NULL);
  last_http_local_time = local_time;
  ink_mutex_release(&http_time_lock);
#else
  ink_time_t local_time = time(NULL);
#endif

  highest_delta = (int) HttpConfig::m_master.cluster_time_delta;
//     highest_delta = 
//      lmgmt->record_data->readInteger("proxy.process.http.cluster_delta", 
//                                      &found);
//     if (! found) {
//      HTTP_DEBUG_ASSERT(!"Highest delta config value not found!");
//      highest_delta = 0L;
//     }

  Debug("http_trans",
        "[ink_cluster_time] local: %ld, highest_delta: %d, cluster: %ld",
        local_time, highest_delta, (local_time + (ink_time_t) highest_delta));

  HTTP_DEBUG_ASSERT(highest_delta >= 0);

  local_time += (ink_time_t) highest_delta;
  old = global_time;

  while (local_time > global_time) {
    if (ink_atomic_cas((ink32 *) & global_time, *((ink32 *) & old), *((ink32 *) & local_time))) {
      break;
    }
    old = global_time;
  }

  return global_time;
}

//
// The stat functions
//
void
HttpTransact::histogram_response_document_size(State * s, int doc_size)
{
  if (doc_size >= 0 && doc_size <= 100) {
    HTTP_INCREMENT_TRANS_STAT(http_response_document_size_100_stat);
  } else if (doc_size <= 1024) {
    HTTP_INCREMENT_TRANS_STAT(http_response_document_size_1K_stat);
  } else if (doc_size <= 3072) {
    HTTP_INCREMENT_TRANS_STAT(http_response_document_size_3K_stat);
  } else if (doc_size <= 5120) {
    HTTP_INCREMENT_TRANS_STAT(http_response_document_size_5K_stat);
  } else if (doc_size <= 10240) {
    HTTP_INCREMENT_TRANS_STAT(http_response_document_size_10K_stat);
  } else if (doc_size <= 1048576) {
    HTTP_INCREMENT_TRANS_STAT(http_response_document_size_1M_stat);
  } else {
    HTTP_INCREMENT_TRANS_STAT(http_response_document_size_inf_stat);
  }
  return;
}

void
HttpTransact::histogram_request_document_size(State * s, int doc_size)
{
  if (doc_size >= 0 && doc_size <= 100) {
    HTTP_INCREMENT_TRANS_STAT(http_request_document_size_100_stat);
  } else if (doc_size <= 1024) {
    HTTP_INCREMENT_TRANS_STAT(http_request_document_size_1K_stat);
  } else if (doc_size <= 3072) {
    HTTP_INCREMENT_TRANS_STAT(http_request_document_size_3K_stat);
  } else if (doc_size <= 5120) {
    HTTP_INCREMENT_TRANS_STAT(http_request_document_size_5K_stat);
  } else if (doc_size <= 10240) {
    HTTP_INCREMENT_TRANS_STAT(http_request_document_size_10K_stat);
  } else if (doc_size <= 1048576) {
    HTTP_INCREMENT_TRANS_STAT(http_request_document_size_1M_stat);
  } else {
    HTTP_INCREMENT_TRANS_STAT(http_request_document_size_inf_stat);
  }
  return;
}

void
HttpTransact::user_agent_connection_speed(State * s, ink_hrtime transfer_time, int nbytes)
{
  float bytes_per_hrtime = (transfer_time == 0) ? (nbytes) : ((float) nbytes / (float) (ink64) transfer_time);
  int bytes_per_sec = (int) (bytes_per_hrtime * HRTIME_SECOND);

  if (bytes_per_sec <= 100) {
    HTTP_INCREMENT_TRANS_STAT(http_user_agent_speed_bytes_per_sec_100_stat);
  } else if (bytes_per_sec <= 1024) {
    HTTP_INCREMENT_TRANS_STAT(http_user_agent_speed_bytes_per_sec_1K_stat);
  } else if (bytes_per_sec <= 10240) {
    HTTP_INCREMENT_TRANS_STAT(http_user_agent_speed_bytes_per_sec_10K_stat);
  } else if (bytes_per_sec <= 102400) {
    HTTP_INCREMENT_TRANS_STAT(http_user_agent_speed_bytes_per_sec_100K_stat);
  } else if (bytes_per_sec <= 1048576) {
    HTTP_INCREMENT_TRANS_STAT(http_user_agent_speed_bytes_per_sec_1M_stat);
  } else if (bytes_per_sec <= 10485760) {
    HTTP_INCREMENT_TRANS_STAT(http_user_agent_speed_bytes_per_sec_10M_stat);
  } else {
    HTTP_INCREMENT_TRANS_STAT(http_user_agent_speed_bytes_per_sec_100M_stat);
  }

  return;
}

/*
 * added request_process_time stat for loadshedding foo
 */
void
HttpTransact::client_result_stat(State * s, ink_hrtime total_time, ink_hrtime request_process_time)
{
  ClientTransactionResult_t client_transaction_result = CLIENT_TRANSACTION_RESULT_UNDEFINED;

  ///////////////////////////////////////////////////////
  // don't count errors we generated as hits or misses //
  ///////////////////////////////////////////////////////
  if ((s->source == SOURCE_INTERNAL) && (s->hdr_info.client_response.status_get() >= 400)) {
    client_transaction_result = CLIENT_TRANSACTION_RESULT_ERROR_OTHER;
  }

  switch (s->squid_codes.log_code) {
  case SQUID_LOG_ERR_CONNECT_FAIL:
  case SQUID_LOG_ERR_SPIDER_CONNECT_FAILED:
    HTTP_INCREMENT_TRANS_STAT((s->scheme == URL_WKSIDX_FTP) ? ftp_cache_misses_stat : http_cache_miss_cold_stat);
    client_transaction_result = CLIENT_TRANSACTION_RESULT_ERROR_CONNECT_FAIL;
    break;

  case SQUID_LOG_TCP_HIT:
  case SQUID_LOG_TCP_MEM_HIT:
    // It's possible to have two stat's instead of one, if needed.
    HTTP_INCREMENT_TRANS_STAT((s->scheme == URL_WKSIDX_FTP) ? ftp_cache_hits_stat : http_cache_hit_fresh_stat);
    client_transaction_result = CLIENT_TRANSACTION_RESULT_HIT_FRESH;
    break;

  case SQUID_LOG_TCP_REFRESH_HIT:
    HTTP_INCREMENT_TRANS_STAT((s->scheme == URL_WKSIDX_FTP) ? ftp_cache_hits_stat : http_cache_hit_reval_stat);
    client_transaction_result = CLIENT_TRANSACTION_RESULT_HIT_REVALIDATED;
    break;

  case SQUID_LOG_TCP_IMS_HIT:
    HTTP_INCREMENT_TRANS_STAT((s->scheme == URL_WKSIDX_FTP) ? ftp_cache_hits_stat : http_cache_hit_ims_stat);
    client_transaction_result = CLIENT_TRANSACTION_RESULT_HIT_FRESH;
    break;

  case SQUID_LOG_TCP_REF_FAIL_HIT:
    HTTP_INCREMENT_TRANS_STAT((s->scheme == URL_WKSIDX_FTP) ? ftp_cache_hits_stat : http_cache_hit_stale_served_stat);
    client_transaction_result = CLIENT_TRANSACTION_RESULT_HIT_FRESH;
    break;

  case SQUID_LOG_TCP_MISS:
    if ((GET_VIA_STRING(VIA_CACHE_RESULT) == VIA_IN_CACHE_NOT_ACCEPTABLE)
        || (GET_VIA_STRING(VIA_CACHE_RESULT) == VIA_CACHE_MISS)) {
      HTTP_INCREMENT_TRANS_STAT((s->scheme == URL_WKSIDX_FTP) ? ftp_cache_misses_stat : http_cache_miss_cold_stat);
      client_transaction_result = CLIENT_TRANSACTION_RESULT_MISS_COLD;
    } else {
      // FIX: what case is this for?  can it ever happen?
      HTTP_INCREMENT_TRANS_STAT((s->scheme ==
                                 URL_WKSIDX_FTP) ? ftp_cache_misses_stat : http_cache_miss_uncacheable_stat);
      client_transaction_result = CLIENT_TRANSACTION_RESULT_MISS_UNCACHABLE;
    }
    break;

  case SQUID_LOG_TCP_REFRESH_MISS:
    HTTP_INCREMENT_TRANS_STAT((s->scheme == URL_WKSIDX_FTP) ? ftp_cache_misses_stat : http_cache_miss_changed_stat);
    client_transaction_result = CLIENT_TRANSACTION_RESULT_MISS_CHANGED;
    break;

  case SQUID_LOG_TCP_CLIENT_REFRESH:
    HTTP_INCREMENT_TRANS_STAT((s->scheme ==
                               URL_WKSIDX_FTP) ? ftp_cache_misses_stat : http_cache_miss_client_no_cache_stat);
    client_transaction_result = CLIENT_TRANSACTION_RESULT_MISS_CLIENT_NO_CACHE;
    break;

  case SQUID_LOG_TCP_IMS_MISS:
    HTTP_INCREMENT_TRANS_STAT((s->scheme == URL_WKSIDX_FTP) ? ftp_cache_misses_stat : http_cache_miss_ims_stat);
    client_transaction_result = CLIENT_TRANSACTION_RESULT_MISS_COLD;
    break;

  case SQUID_LOG_TCP_SWAPFAIL:
    HTTP_INCREMENT_TRANS_STAT(http_cache_read_error_stat);
    client_transaction_result = CLIENT_TRANSACTION_RESULT_HIT_FRESH;
    break;

  case SQUID_LOG_ERR_READ_TIMEOUT:
  case SQUID_LOG_TCP_DENIED:
    // No cache result due to error
    client_transaction_result = CLIENT_TRANSACTION_RESULT_ERROR_OTHER;
    break;

  default:
    // FIX: What is the conditional below doing?
//          if (s->local_trans_stats[http_cache_lookups_stat].count == 1L)
//              HTTP_INCREMENT_TRANS_STAT(http_cache_miss_cold_stat);

    // FIX: I suspect the following line should not be set here,
    //      because it overrides the error classification above.
    //      Commenting out.
    // client_transaction_result = CLIENT_TRANSACTION_RESULT_MISS_COLD;

    break;
  }

  //////////////////////////////////////////
  // don't count aborts as hits or misses //
  //////////////////////////////////////////
  if (s->client_info.abort == ABORTED) {
    client_transaction_result = CLIENT_TRANSACTION_RESULT_ERROR_ABORT;
  } else if (s->client_info.abort == MAYBE_ABORTED) {
    client_transaction_result = CLIENT_TRANSACTION_RESULT_ERROR_POSSIBLE_ABORT;
  }
  // Increment the completed connection count
  HTTP_SUM_TRANS_STAT(http_completed_requests_stat, 1);

  // Set the stat now that we know what happend
  ink_hrtime total_msec = ink_hrtime_to_msec(total_time);
  ink_hrtime process_msec = ink_hrtime_to_msec(request_process_time);
  switch (client_transaction_result) {
  case CLIENT_TRANSACTION_RESULT_HIT_FRESH:
    HTTP_SUM_TRANS_STAT(http_ua_msecs_counts_hit_fresh_stat, total_msec);
    HTTP_SUM_TRANS_STAT(http_ua_msecs_counts_hit_fresh_process_stat, process_msec);
    break;
  case CLIENT_TRANSACTION_RESULT_HIT_REVALIDATED:
    HTTP_SUM_TRANS_STAT(http_ua_msecs_counts_hit_reval_stat, total_msec);
    break;
  case CLIENT_TRANSACTION_RESULT_MISS_COLD:
    HTTP_SUM_TRANS_STAT(http_ua_msecs_counts_miss_cold_stat, total_msec);
    break;
  case CLIENT_TRANSACTION_RESULT_MISS_CHANGED:
    HTTP_SUM_TRANS_STAT(http_ua_msecs_counts_miss_changed_stat, total_msec);
    break;
  case CLIENT_TRANSACTION_RESULT_MISS_CLIENT_NO_CACHE:
    HTTP_SUM_TRANS_STAT(http_ua_msecs_counts_miss_client_no_cache_stat, total_msec);
    break;
  case CLIENT_TRANSACTION_RESULT_MISS_UNCACHABLE:
    HTTP_SUM_TRANS_STAT(http_ua_msecs_counts_miss_uncacheable_stat, total_msec);
    break;
  case CLIENT_TRANSACTION_RESULT_ERROR_ABORT:
    HTTP_SUM_TRANS_STAT(http_ua_msecs_counts_errors_aborts_stat, total_msec);
    break;
  case CLIENT_TRANSACTION_RESULT_ERROR_POSSIBLE_ABORT:
    HTTP_SUM_TRANS_STAT(http_ua_msecs_counts_errors_possible_aborts_stat, total_msec);
    break;
  case CLIENT_TRANSACTION_RESULT_ERROR_CONNECT_FAIL:
    HTTP_SUM_TRANS_STAT(http_ua_msecs_counts_errors_connect_failed_stat, total_msec);
    break;
  case CLIENT_TRANSACTION_RESULT_ERROR_OTHER:
    HTTP_SUM_TRANS_STAT(http_ua_msecs_counts_errors_other_stat, total_msec);
    break;
  default:
    HTTP_SUM_TRANS_STAT(http_ua_msecs_counts_other_unclassified_stat, total_msec);
    if (is_debug_tag_set("http")) {
      ink_release_assert(!"unclassified statistic");
    }
    //debug_tag_assert("http",!"unclassified statistic");
    break;
  }
}

void
HttpTransact::origin_server_connection_speed(State * s, ink_hrtime transfer_time, int nbytes)
{
  float bytes_per_hrtime = (transfer_time == 0) ? (nbytes) : ((float) nbytes / (float) (ink64) transfer_time);
  int bytes_per_sec = (int) (bytes_per_hrtime * HRTIME_SECOND);

  if (bytes_per_sec <= 100) {
    HTTP_INCREMENT_TRANS_STAT(http_origin_server_speed_bytes_per_sec_100_stat);
  } else if (bytes_per_sec <= 1024) {
    HTTP_INCREMENT_TRANS_STAT(http_origin_server_speed_bytes_per_sec_1K_stat);
  } else if (bytes_per_sec <= 10240) {
    HTTP_INCREMENT_TRANS_STAT(http_origin_server_speed_bytes_per_sec_10K_stat);
  } else if (bytes_per_sec <= 102400) {
    HTTP_INCREMENT_TRANS_STAT(http_origin_server_speed_bytes_per_sec_100K_stat);
  } else if (bytes_per_sec <= 1048576) {
    HTTP_INCREMENT_TRANS_STAT(http_origin_server_speed_bytes_per_sec_1M_stat);
  } else if (bytes_per_sec <= 10485760) {
    HTTP_INCREMENT_TRANS_STAT(http_origin_server_speed_bytes_per_sec_10M_stat);
  } else {
    HTTP_INCREMENT_TRANS_STAT(http_origin_server_speed_bytes_per_sec_100M_stat);
  }

  return;
}

void
HttpTransact::update_aol_stats(State * s, ink_hrtime cache_lookup_time)
{
#ifndef TS_MICRO
  // Interested in stats about JG compressed images.  First
  //   check the port attribute for a fast path when we are not
  //   doing jg compression as the integer compare is much cheaper
  //   than a header fetch & string compare
  //
  if (s->client_info.port_attribute == SERVER_PORT_COMPRESSED && s->hdr_info.client_response.valid()) {

    int cont_type_len;
    const char *cont_type_str = s->hdr_info.client_response.value_get(MIME_FIELD_CONTENT_TYPE,
                                                                      MIME_LEN_CONTENT_TYPE,
                                                                      &cont_type_len);

    // This is how we will determine that this was a "JG" request.
    if (cont_type_str && (ptr_len_casecmp(cont_type_str, cont_type_len, "image/x-jg") == 0)) {

      if (s->squid_codes.wuts_proxy_status_code ==
          WUTS_PROXY_STATUS_CLIENT_ABORT ||
          s->squid_codes.log_code == SQUID_LOG_ERR_CLIENT_ABORT ||
          s->squid_codes.wuts_proxy_status_code ==
          WUTS_PROXY_STATUS_SPIDER_MEMBER_ABORTED || s->squid_codes.log_code == SQUID_LOG_ERR_SPIDER_MEMBER_ABORTED) {
        HTTP_INCREMENT_TRANS_STAT(http_jg_client_aborts_stat);
      }

      if (s->squid_codes.log_code == SQUID_LOG_TCP_HIT ||
          s->squid_codes.log_code == SQUID_LOG_TCP_MEM_HIT ||
          s->squid_codes.log_code == SQUID_LOG_TCP_REFRESH_HIT || s->squid_codes.log_code == SQUID_LOG_TCP_IMS_HIT) {
        HTTP_INCREMENT_TRANS_STAT(http_jg_cache_hits_stat);
        if (cache_lookup_time > 0) {
          HTTP_SUM_TRANS_STAT(http_jg_cache_hit_time_stat, cache_lookup_time);
        }
      } else {
        HTTP_INCREMENT_TRANS_STAT(http_jg_cache_misses_stat);
        if (cache_lookup_time > 0) {
          HTTP_SUM_TRANS_STAT(http_jg_cache_miss_time_stat, cache_lookup_time);
        }
      }
    }

  }
#endif
}





void
HttpTransact::update_size_and_time_stats(State * s,
                                         ink_hrtime total_time,
                                         ink_hrtime
                                         user_agent_write_time,
                                         ink_hrtime
                                         origin_server_read_time,
                                         ink_hrtime cache_lookup_time,
                                         int
                                         user_agent_request_header_size,
                                         int
                                         user_agent_request_body_size,
                                         int
                                         user_agent_response_header_size,
                                         int
                                         user_agent_response_body_size,
                                         int
                                         origin_server_request_header_size,
                                         int
                                         origin_server_request_body_size,
                                         int
                                         origin_server_response_header_size,
                                         int
                                         origin_server_response_body_size,
                                         int pushed_response_header_size,
                                         int pushed_response_body_size, CacheAction_t cache_action)
{

  int user_agent_request_size = user_agent_request_header_size + user_agent_request_body_size;
  int user_agent_response_size = user_agent_response_header_size + user_agent_response_body_size;
  int user_agent_bytes = user_agent_request_size + user_agent_response_size;

  int origin_server_request_size = origin_server_request_header_size + origin_server_request_body_size;
  int origin_server_response_size = origin_server_response_header_size + origin_server_response_body_size;
  int origin_server_bytes = origin_server_request_size + origin_server_response_size;

  // Background fill stats
  switch (s->state_machine->background_fill) {
  case BACKGROUND_FILL_COMPLETED:
    {
      int bg_size = origin_server_response_body_size - user_agent_response_body_size;
      bg_size = max(0, bg_size);
      HTTP_SUM_TRANS_STAT(http_background_fill_bytes_completed_stat, bg_size);
      break;
    }
  case BACKGROUND_FILL_ABORTED:
    {
      int bg_size = origin_server_response_body_size - user_agent_response_body_size;
      bg_size = max(0, bg_size);
      HTTP_SUM_TRANS_STAT(http_background_fill_bytes_aborted_stat, bg_size);
      break;
    }
  case BACKGROUND_FILL_NONE:
    break;
  case BACKGROUND_FILL_STARTED:
  default:
    ink_assert(0);
  }

  // Bandwidth Savings
  switch (s->squid_codes.log_code) {
  case SQUID_LOG_TCP_HIT:
  case SQUID_LOG_TCP_MEM_HIT:
    // It's possible to have two stat's instead of one, if needed.
    HTTP_INCREMENT_TRANS_STAT(http_tcp_hit_count_stat);
    HTTP_SUM_TRANS_STAT(http_tcp_hit_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_TRANS_STAT(http_tcp_hit_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_TCP_MISS:
    HTTP_INCREMENT_TRANS_STAT(http_tcp_miss_count_stat);
    HTTP_SUM_TRANS_STAT(http_tcp_miss_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_TRANS_STAT(http_tcp_miss_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_TCP_EXPIRED_MISS:
    HTTP_INCREMENT_TRANS_STAT(http_tcp_expired_miss_count_stat);
    HTTP_SUM_TRANS_STAT(http_tcp_expired_miss_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_TRANS_STAT(http_tcp_expired_miss_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_TCP_REFRESH_HIT:
    HTTP_INCREMENT_TRANS_STAT(http_tcp_refresh_hit_count_stat);
    HTTP_SUM_TRANS_STAT(http_tcp_refresh_hit_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_TRANS_STAT(http_tcp_refresh_hit_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_TCP_REFRESH_MISS:
    HTTP_INCREMENT_TRANS_STAT(http_tcp_refresh_miss_count_stat);
    HTTP_SUM_TRANS_STAT(http_tcp_refresh_miss_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_TRANS_STAT(http_tcp_refresh_miss_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_TCP_CLIENT_REFRESH:
    HTTP_INCREMENT_TRANS_STAT(http_tcp_client_refresh_count_stat);
    HTTP_SUM_TRANS_STAT(http_tcp_client_refresh_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_TRANS_STAT(http_tcp_client_refresh_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_TCP_IMS_HIT:
    HTTP_INCREMENT_TRANS_STAT(http_tcp_ims_hit_count_stat);
    HTTP_SUM_TRANS_STAT(http_tcp_ims_hit_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_TRANS_STAT(http_tcp_ims_hit_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_TCP_IMS_MISS:
    HTTP_INCREMENT_TRANS_STAT(http_tcp_ims_miss_count_stat);
    HTTP_SUM_TRANS_STAT(http_tcp_ims_miss_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_TRANS_STAT(http_tcp_ims_miss_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_ERR_CLIENT_ABORT:
  case SQUID_LOG_ERR_SPIDER_MEMBER_ABORTED:
    HTTP_INCREMENT_TRANS_STAT(http_err_client_abort_count_stat);
    HTTP_SUM_TRANS_STAT(http_err_client_abort_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_TRANS_STAT(http_err_client_abort_origin_server_bytes_stat, origin_server_bytes);
    break;
  case SQUID_LOG_ERR_CONNECT_FAIL:
  case SQUID_LOG_ERR_SPIDER_CONNECT_FAILED:
    HTTP_INCREMENT_TRANS_STAT(http_err_connect_fail_count_stat);
    HTTP_SUM_TRANS_STAT(http_err_connect_fail_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_TRANS_STAT(http_err_connect_fail_origin_server_bytes_stat, origin_server_bytes);
    break;
  default:
    HTTP_INCREMENT_TRANS_STAT(http_misc_count_stat);
    HTTP_SUM_TRANS_STAT(http_misc_user_agent_bytes_stat, user_agent_bytes);
    HTTP_SUM_TRANS_STAT(http_misc_origin_server_bytes_stat, origin_server_bytes);
    break;
  }

  // times
  HTTP_SUM_TRANS_STAT(http_total_transactions_time_stat, total_time);

  // sizes
  HTTP_SUM_TRANS_STAT(http_user_agent_request_header_total_size_stat, user_agent_request_header_size);
  HTTP_SUM_TRANS_STAT(http_user_agent_response_header_total_size_stat, user_agent_response_header_size);
  HTTP_SUM_TRANS_STAT(http_user_agent_request_document_total_size_stat, user_agent_request_body_size);
  HTTP_SUM_TRANS_STAT(http_user_agent_response_document_total_size_stat, user_agent_response_body_size);

  // proxy stats
  if (s->current.request_to == HttpTransact::PARENT_PROXY) {
    HTTP_SUM_TRANS_STAT(http_parent_proxy_request_total_bytes_stat,
                        origin_server_request_header_size + origin_server_request_body_size);
    HTTP_SUM_TRANS_STAT(http_parent_proxy_response_total_bytes_stat,
                        origin_server_response_header_size + origin_server_response_body_size);
    HTTP_SUM_TRANS_STAT(http_parent_proxy_transaction_time_stat, total_time);
  }
  // request header zero means the document was cached.
  // do not add to stats.
  if (origin_server_request_header_size > 0) {
    HTTP_SUM_TRANS_STAT(http_origin_server_request_header_total_size_stat, origin_server_request_header_size);
    HTTP_SUM_TRANS_STAT(http_origin_server_response_header_total_size_stat, origin_server_response_header_size);
    HTTP_SUM_TRANS_STAT(http_origin_server_request_document_total_size_stat, origin_server_request_body_size);
    HTTP_SUM_TRANS_STAT(http_origin_server_response_document_total_size_stat, origin_server_response_body_size);
  }

  if (s->method == HTTP_WKSIDX_PUSH) {
    HTTP_SUM_TRANS_STAT(http_pushed_response_header_total_size_stat, pushed_response_header_size);
    HTTP_SUM_TRANS_STAT(http_pushed_document_total_size_stat, pushed_response_body_size);
  }

  histogram_request_document_size(s, user_agent_request_body_size);
  histogram_response_document_size(s, user_agent_response_body_size);

  if (user_agent_write_time >= 0) {
    user_agent_connection_speed(s, user_agent_write_time, user_agent_response_size);
  }

  if (origin_server_request_header_size > 0 && origin_server_read_time > 0) {
    origin_server_connection_speed(s, origin_server_read_time, origin_server_response_size);
  }

  update_aol_stats(s, cache_lookup_time);

  return;
}

void
HttpTransact::initialize_bypass_variables(State * s)
{

  //////////////////////////////////////////
  // Handle potential transparency errors //
  //////////////////////////////////////////

  /*
   * NOTE: Removed ARM code from here
   */
}

// void HttpTransact::add_new_stat_block(State* s)
//
//   Adds a new stat block
//
void
HttpTransact::add_new_stat_block(State * s)
{
  // We keep the block around till the end of transaction
  //    We don't need explictly deallocate it later since
  //    when the transaction is over, the arena will be destroyed
  ink_assert(s->current_stats->next_insert == StatBlockEntries);
  StatBlock *new_block = (StatBlock *) s->arena.alloc(sizeof(StatBlock));
  new_block->init();
  s->current_stats->next = new_block;
  s->current_stats = new_block;
  Debug("http_trans", "Adding new large stat block");
}


void
HttpTransact::delete_warning_value(HTTPHdr * to_warn, HTTPWarningCode warning_code)
{


  int w_code = (int) warning_code;
  MIMEField *field = to_warn->field_find(MIME_FIELD_WARNING, MIME_LEN_WARNING);;

  // Loop over the values to see if we need to do anything
  if (field) {
    HdrCsvIter iter;

    int valid;
    int val_code;

    const char *value_str;
    int value_len;

    MIMEField *new_field = NULL;
    val_code = iter.get_first_int(field, &valid);

    while (valid) {
      if (val_code == w_code) {
        // Ok, found the value we're look to delete
        //  Look over and create a new field
        //  appending all elements that are not this
        //  value
        val_code = iter.get_first_int(field, &valid);

        while (valid) {
          if (val_code != warning_code) {
            value_str = iter.get_current(&value_len);
            if (new_field) {
              new_field->value_append(to_warn->m_heap, to_warn->m_mime, value_str, value_len, true);
            } else {
              new_field = to_warn->field_create();
              to_warn->field_value_set(new_field, value_str, value_len);
            }

          }
          val_code = iter.get_next_int(&valid);
        }

        to_warn->field_delete(MIME_FIELD_WARNING, MIME_LEN_WARNING);
        if (new_field) {
          new_field->name_set(to_warn->m_heap, to_warn->m_mime, MIME_FIELD_WARNING, MIME_LEN_WARNING);
          to_warn->field_attach(new_field);
        }

        return;
      }

      val_code = iter.get_next_int(&valid);
    }
  }
}

MimeTableEntry unknown_ftp_mime_type = { "ftp_unknown", "application/octet-stream", "binary", "unknown" };





////////////////////////////////////////////////////////////////////////////////////
//  
// Name    : is_connection_collapse_checks_success
//
// Details : This function is used to check for the success of various connection 
//           collapsing parameters.Connection collapsing is disabled in the foll cases
//              1) If Cache DISK not present
//              2) If read_while_writing is disabled
//              3) If request is Https
// 
// YTS Team, yamsat
////////////////////////////////////////////////////////////////////////////////////

bool
HttpTransact::is_connection_collapse_checks_success(State * s)
{
  bool match = true;
  URL *url = s->hdr_info.client_request.url_get();
  int rww_enabled = 0;
  TS_ReadConfigInteger(rww_enabled, "proxy.config.cache.enable_read_while_writer");
  match &= (url->scheme_get_wksidx() == URL_WKSIDX_HTTP);
  match &= HttpConfig::m_master.hashtable_enabled;
  match &= !(s->is_revalidation_necessary);
  match &= s->state_machine->is_cache_enabled;
  match &= rww_enabled;
  return match;
}

////////////////////////////////////////////////////////////////////////////////////
// Name         : ConnectionCollapsing
//
// Details      : This function is called from OSDNSLookup(). Checks if there is an
//                ongoing transaction already in progress for the same object.This 
//                is done by looking-up the hashtable.There would be two cases here.
//              1) If entry found, check if URL headers match perfectly. then return
//                 for scheduling this client to be piggy backed.
//              2) If entry not found in Hashtable, insert an entry and return with 
//                 status indicating normal procedure for connecting to OriginServer
//              3) If entry could not be inserted,this means that there was someother
//                 client who has inserted the request.Therefore, return with a status
//                 indicating CACHE_RELOOKUP.Then, this client could get the object 
//                 from the cache because of the read-while-write behavior
//
// YTS Team, yamsat
////////////////////////////////////////////////////////////////////////////////////
int
HttpTransact::ConnectionCollapsing(State * s)
{
  //Checking Hash table tries
  if (s->HashTable_Tries == 0) {
    s->HashTable_Tries++;
    if (is_connection_collapse_checks_success(s)) {
      char *url_str = s->request_data.get_string();
      int index = cacheProcessor.hashtable_tracker.KeyToIndex(url_str);
      HeaderAlternate *alternate = NULL;

      alternate = cacheProcessor.hashtable_tracker.lookup(index, url_str, s->request_data.hdr);
      xfree(url_str);

      if (alternate != NULL) {
        Debug("http_seq", "[HttpTransact::OSDNSLookup]URL entry found in HashTable");
        if (!alternate->response_noncacheable && HttpConfig::m_master.rww_wait_time != 0) {
          HttpVCTableEntry *ua_entry = s->state_machine->get_ua_entry();

          //Registering vc_handler for scheduling
          ua_entry->piggybacking_scheduled_handler = &HttpSM::connection_collapsing_piggyback_handler;
          s->state_machine->piggybacking_scheduled = true;

          //Scheduling the piggy backed clients to do Cache lookup
          s->state_machine->event_scheduled = s->state_machine->mutex->
            thread_holding->schedule_at(s->state_machine, HRTIME_MSECONDS(HttpConfig::m_master.rww_wait_time));

          return CONNECTION_COLLAPSING_SCHEDULED;
        }
      } else {
        //Creating a Hash entry in the Hash table
        if (NULL != (alternate = cacheProcessor.hashtable_tracker.insert(index, &(s->request_data), false))) {
          s->state_machine->request_inserted = true;
          s->state_machine->RequestHeader = alternate;
          s->state_machine->Hashtable_index = index;
          Debug("http_seq", "[HttpTransact::OSDNSLookup]Inserted URL entry in hashtable");
        } else {
          s->state_machine->request_inserted = false;
          s->state_machine->RequestHeader = NULL;
          xfree(alternate);
          alternate = NULL;
          return CACHE_RELOOKUP;
        }
      }
    }
  }
  return 0;
}

///////////////////////////////////////////////////////////////////////////////////////////////
//Name          : ConnectionCollapsing_for_revalidation
//
//Details       : This function is called from HandleCacheOpenReadHit(). If an object is determined
//                to be revalidated, check if there is some other client already in the process
//                of revalidation.This is done by looking-up the HashTable.The foll cases arise
//              1) If URL found in HashTable, check if the revalidate_window_period is expired.
//                 If yes, update the revalidation_start_time and proceed for OS connection.
//                 If no, return a status indicating that stale object be served from CACHE.
//              2) If URL not found,
//                      * insert the url in the hashTable and update the revalidation_start_time
//                         and return with the status indicating the connection to OS.
//                      * if url not inserted, this means that there was someother client who inserted
//                        it and there is a possibility of avoiding a OS connection. Therefore, return
//                        with status indicating that stale object be served from CACHE.
//
// YTS Team, yamsat
///////////////////////////////////////////////////////////////////////////////////////////////
int
HttpTransact::ConnectionCollapsing_for_revalidation(State * s)
{
  HeaderAlternate *alternate = NULL;

  // TODO eval whether we should get a reference here
  // except this getter also unescapes the string so maybe not...
  char *url_str = s->request_data.get_string();

  //Doing a Hashtable Lookup
  int index = cacheProcessor.hashtable_tracker.KeyToIndex(url_str);

  alternate = cacheProcessor.hashtable_tracker.lookup(index, url_str, s->request_data.hdr);
  xfree(url_str);
  if (alternate != NULL) {
    Debug("http_seq", "[HttpTransact::HandleCacheOpenReadHit]URL entry found in HashTable");
    if (alternate->revalidation_in_progress == true) {

      //Checking if request is in revalidate_window_period
      if (((ink_get_hrtime_internal() - (alternate->revalidation_start_time)) / REVALIDATE_CONV_FACTOR) <
          (HttpConfig::m_master.revalidate_window_period)) {
        //setting send_revalidate to false so as to serve stale object in cache
        return SERVE_STALE_OBJECT;
      } else {
        //Updating the revalidation_start_time
        cacheProcessor.hashtable_tracker.update_revalidation_start_time(index, alternate);
      }
    }
  }
  //if URL not found ie. if Hash Table Miss....
  else {
    if (NULL != (alternate = cacheProcessor.hashtable_tracker.insert(index, &(s->request_data), true))) {
      s->state_machine->request_inserted = true;
      s->state_machine->RequestHeader = alternate;
      s->state_machine->Hashtable_index = index;
      Debug("http_seq", "[HttpTransact::HandleCacheOpenReadHit]Inserted URL entry in hashtable");
    } else {
      xfree(alternate);
      s->state_machine->request_inserted = false;
      s->state_machine->RequestHeader = NULL;
      alternate = NULL;
      return SERVE_STALE_OBJECT;
    }
  }
  return 0;
}

