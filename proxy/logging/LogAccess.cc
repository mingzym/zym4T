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

/***************************************************************************
 LogAccess.cc

 This file implements the LogAccess class.  However, LogAccess is an
 abstract base class, providing an interface that logging uses to get
 information from a module, such as HTTP or ICP.  Each module derives a
 specific implementation from this base class (such as LogAccessHttp), and
 implements the virtual accessor functions there.

 The LogAccess class also defines a set of static functions that are used
 to provide support for marshalling and unmarshalling support for the other
 LogAccess derived classes.

 
 ***************************************************************************/
#include "ink_unused.h"

#include <assert.h>
#include "ink_platform.h"

#include "Error.h"
#include "HTTP.h"

#include "P_Net.h"
#include "P_Cache.h"
#include "I_Machine.h"
#include "LogAccess.h"
#include "LogLimits.h"
#include "LogField.h"
#include "LogFilter.h"
#include "LogUtils.h"
#include "LogFormat.h"
#include "LogObject.h"
#include "LogConfig.h"
#include "LogBuffer.h"
#include "Log.h"


/*------------------------------------------------------------------------- 
  LogAccess::init
  -------------------------------------------------------------------------*/

void
LogAccess::init()
{
  if (initialized) {
    return;
  }
  //
  // Here is where we would perform any initialization code.
  //

  initialized = true;
}

/*------------------------------------------------------------------------- 
  The following functions provide a default implementation for the base
  class marshalling routines so that each subsequent LogAccess* class only
  has to implement those functions that are to override this default
  implementation.
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_client_host_ip(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

/*int
LogAccess::marshal_client_auth_user_name(char *buf)
{
  DEFAULT_STR_FIELD;
}a*/

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_client_req_text(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_client_req_http_method(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_client_req_url(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_client_req_url_canon(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_client_req_unmapped_url_canon(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_client_req_unmapped_url_path(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_client_req_url_path(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_client_req_url_scheme(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  This case is special because it really stores 2 ints.
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_client_req_http_version(char *buf)
{
  if (buf) {
    LOG_INT major = 0;
    LOG_INT minor = 0;
    marshal_int(buf, major);
    marshal_int((buf + MIN_ALIGN), minor);
  }
  return (2 * MIN_ALIGN);
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_client_req_header_len(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_client_req_body_len(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_client_finish_status_code(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_client_gid(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_client_accelerator_id(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_proxy_resp_content_type(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_proxy_resp_squid_len(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_proxy_resp_content_len(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_proxy_resp_status_code(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_proxy_resp_header_len(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_proxy_resp_origin_bytes(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_proxy_resp_cache_bytes(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_proxy_finish_status_code(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_cache_result_code(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_proxy_req_header_len(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_proxy_req_body_len(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_proxy_req_server_name(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_proxy_req_server_ip(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_proxy_hierarchy_route(char *buf)
{
  DEFAULT_INT_FIELD;
}

#ifndef INK_NO_CONGESTION_CONTROL
/*-------------------------------------------------------------------------
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_client_retry_after_time(char *buf)
{
  DEFAULT_INT_FIELD;
}
#endif

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_proxy_host_name(char *buf)
{
  char *str = NULL;
  Machine *machine = this_machine();

  if (machine) {
    str = machine->hostname;
  }
  int len = LogAccess::strlen(str);
  if (buf) {
    marshal_str(buf, str, len);
  }
  return len;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_proxy_host_ip(char *buf)
{
  char *str = NULL;
  Machine *machine = this_machine();
  int len = 0;

  if (machine) {
    str = machine->ip_string;
    len = LogAccess::strlen(str);
  }
  if (buf) {
    marshal_str(buf, str, len);
  }
  return len;
}


/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_server_host_ip(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_server_host_name(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_server_resp_status_code(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_server_resp_content_len(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_server_resp_header_len(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  This case is special because it really stores 2 ints.
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_server_resp_http_version(char *buf)
{
  if (buf) {
    LOG_INT major = 0;
    LOG_INT minor = 0;
    marshal_int(buf, major);
    marshal_int((buf + MIN_ALIGN), minor);
  }
  return (2 * MIN_ALIGN);
}

int
LogAccess::marshal_cache_write_code(char *buf)
{
  DEFAULT_INT_FIELD;
}

int
LogAccess::marshal_cache_write_transform_code(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_transfer_time_ms(char *buf)
{
  DEFAULT_INT_FIELD;
}

int
LogAccess::marshal_transfer_time_s(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_bandwidth(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_file_size(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_time_to_first_client_byte_ms(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_stream_type(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_external_plugin_transaction_id(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
//MIXT SDK_VER_2
int
LogAccess::marshal_external_plugin_string(char *buf)
{
  DEFAULT_STR_FIELD;
}

//MIXT SDK_VER_2

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_stream_duration_ms(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_client_dns_name(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_client_os(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_client_os_version(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_client_cpu(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_client_player_version(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_client_player_language(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_client_user_agent(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_referer_url(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_audio_codec(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_video_codec(char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_client_bytes_received(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_client_pkts_received(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_client_lost_pkts(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_client_lost_net_pkts(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_client_lost_continuous_pkts(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_client_pkts_ecc_recover(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_client_pkts_resent_recover(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_client_resend_request(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_client_buffer_count(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_client_buffer_ts(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_client_quality_per(char *buf)
{
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/
int
LogAccess::marshal_http_header_field(LogField::Container container, char *field, char *buf)
{
  DEFAULT_STR_FIELD;
}


/*------------------------------------------------------------------------- 

  -------------------------------------------------------------------------*/
int
LogAccess::marshal_http_header_field_escapify(LogField::Container container, char *field, char *buf)
{
  DEFAULT_STR_FIELD;
}

/*------------------------------------------------------------------------- 

  The following functions have a non-virtual base-class implementation.
  -------------------------------------------------------------------------*/

/*------------------------------------------------------------------------- 
  LogAccess::marshal_client_req_timestamp_sec

  This does nothing because the timestamp is already in the LogEntryHeader.
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_client_req_timestamp_sec(char *buf)
{
  // in the case of aggregate fields, we need the space, so we'll always
  // reserve it.  For a non-aggregate timestamp, this space is not used.
  DEFAULT_INT_FIELD;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_entry_type(char *buf)
{
  if (buf) {
    LOG_INT val = (LOG_INT) entry_type();
    marshal_int(buf, val);
  }
  return MIN_ALIGN;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_config_int_var(char *config_var, char *buf)
{
  if (buf) {
    LOG_INT val = (LOG_INT) LOG_ConfigReadInteger(config_var);
    marshal_int(buf, val);
  }
  return MIN_ALIGN;
}

/*------------------------------------------------------------------------- 
  -------------------------------------------------------------------------*/

int
LogAccess::marshal_config_str_var(char *config_var, char *buf)
{
  char *str = NULL;
  str = LOG_ConfigReadString(config_var);
  int len = LogAccess::strlen(str);
  if (buf) {
    marshal_str(buf, str, len);
  }
  xfree(str);
  return len;
}

// To allow for a generic marshal_record function, rather than
// multiple functions (one per data type) we always marshal a record
// as a string of a fixed length.  We use a fixed length because the
// marshal_record function can be called with a null *buf to request
// the length of the record, and later with a non-null *buf to
// actually request the record to be inserted in the buffer, and both
// calls should return the same number of characters. If we did not
// enforce a fixed size, this would not necesarilly be the case
// because records --statistics in particular-- can potentially change
// between one call and the other.
//
int
LogAccess::marshal_record(char *record, char *buf)
{
  const unsigned int max_chars = MARSHAL_RECORD_LENGTH;

  if (NULL == buf) {
    return max_chars;
  }

  const char *record_not_found_msg = "RECORD_NOT_FOUND";
  const unsigned int record_not_found_chars = 17;
  ink_debug_assert(::strlen(record_not_found_msg) + 1 == record_not_found_chars);

  char ascii_buf[max_chars];
  register char *out_buf;
  register unsigned int num_chars;

#define LOG_INTEGER RECD_INT
#define LOG_COUNTER RECD_COUNTER
#define LOG_FLOAT   RECD_FLOAT
#define LOG_STRING  RECD_STRING
  typedef RecInt LogInt;
  typedef RecCounter LogCounter;
  typedef RecFloat LogFloat;
  typedef RecString LogString;

  RecDataT stype = RECD_NULL;
  bool found = false;

  if (RecGetRecordDataType(record, &stype) != REC_ERR_OKAY) {

    out_buf = "INVALID_RECORD";
    num_chars = 15;
    ink_debug_assert(::strlen(out_buf) + 1 == num_chars);

  } else {

    if (LOG_INTEGER == stype || LOG_COUNTER == stype) {

      // we assume MgmtInt and MgmtIntCounter are ink64 for the
      // conversion below, if this ever changes we should modify
      // accordingly
      //
      ink_debug_assert(sizeof(ink64) >= sizeof(LogInt) && sizeof(ink64) >= sizeof(LogCounter));

      // so that a 64 bit integer will fit (including sign and eos)
      //
      ink_debug_assert(max_chars > 21);

      ink64 val = (ink64) (LOG_INTEGER == stype ? REC_readInteger(record, &found) : REC_readCounter(record, &found));

      if (found) {

        out_buf = LogUtils::ink64_to_str(ascii_buf, max_chars, val, &num_chars);
        ink_debug_assert(out_buf);

      } else {

        out_buf = (char *) record_not_found_msg;
        num_chars = record_not_found_chars;
      }

    } else if (LOG_FLOAT == stype) {

      // we assume MgmtFloat is at least a float for the conversion below
      // (the conversion itself assumes a double because of the %e)
      // if this ever changes we should modify accordingly
      //
      ink_debug_assert(sizeof(double) >= sizeof(LogFloat));

      LogFloat val = REC_readFloat(record, &found);

      if (found) {
        // ink_snprintf does not support "%e" in the format
        // and we want to use "%e" because it is the most concise
        // notation

        num_chars = snprintf(ascii_buf, sizeof(ascii_buf), "%e", val) + 1;      // include eos

        // the "%e" field above should take 13 characters at most
        //
        ink_debug_assert(num_chars <= max_chars);

        // the following should never be true
        //
        if (num_chars > max_chars) {
          // data does not fit, output asterisks
          out_buf = "***";
          num_chars = 4;
          ink_debug_assert(::strlen(out_buf) + 1 == num_chars);
        } else {
          out_buf = ascii_buf;
        }
      } else {
        out_buf = (char *) record_not_found_msg;
        num_chars = record_not_found_chars;
      }

    } else if (LOG_STRING == stype) {

      out_buf = REC_readString(record, &found);

      if (found) {
        if (out_buf != 0 && out_buf[0] != 0) {
          num_chars =::strlen(out_buf) + 1;
          if (num_chars > max_chars) {
            // truncate string and write ellipsis at the end
            ink_memcpy(ascii_buf, out_buf, max_chars - 4);
            ascii_buf[max_chars - 1] = 0;
            ascii_buf[max_chars - 2] = '.';
            ascii_buf[max_chars - 3] = '.';
            ascii_buf[max_chars - 4] = '.';
            out_buf = ascii_buf;
            num_chars = max_chars;
          }
        } else {
          out_buf = "NULL";
          num_chars = 5;
          ink_debug_assert(::strlen(out_buf) + 1 == num_chars);
        }
      } else {
        out_buf = (char *) record_not_found_msg;
        num_chars = record_not_found_chars;
      }

    } else {

      out_buf = "INVALID_MgmtType";
      num_chars = 17;
      ink_debug_assert(!"invalid MgmtType for requested record");
      ink_debug_assert(::strlen(out_buf) + 1 == num_chars);
    }
  }

  ink_debug_assert(num_chars <= max_chars);
  ink_memcpy(buf, out_buf, num_chars);

#ifdef PURIFY
  for (int i = num_chars + 1; i < max_chars; ++i) {
    buf[i] = '$';
  }
#endif

  return max_chars;
}

/*------------------------------------------------------------------------- 
  The following functions are helper functions for the LogAccess* classes
  -------------------------------------------------------------------------*/

/*------------------------------------------------------------------------- 
  LogAccess::marshal_int

  Place the given value into the buffer.  Note that the buffer needs to be
  aligned with the size of MIN_ALIGN for this to succeed.  We also convert
  to network byte order, just in case we read the data on a different
  machine; unmarshal_int() will convert back to host byte order.

  ASSUMES dest IS NOT NULL.
  -------------------------------------------------------------------------*/

#if DO_NOT_INLINE
void
LogAccess::marshal_int(char *dest, LOG_INT source)
{
  ink_assert(dest != NULL);
  *((LOG_INT *) dest) = htonl(source);
}

void
LogAccess::marshal_int_no_byte_order_conversion(char *dest, LOG_INT source)
{
  ink_assert(dest != NULL);
  *((LOG_INT *) dest) = source;
}
#endif

/*------------------------------------------------------------------------- 
  LogAccess::marshal_str

  Copy the given string to the destination buffer, including the trailing
  NULL.  For binary formatting, we need the NULL to distinguish the end of
  the string, and we'll remove it for ascii formatting.
  ASSUMES dest IS NOT NULL.
  The array pointed to by dest must be at least padded_len in length.
  -------------------------------------------------------------------------*/

void
LogAccess::marshal_str(char *dest, char *source, int padded_len)
{
  if (source == NULL || source[0] == 0 || padded_len == 0) {
    source = DEFAULT_STR;
  }
  ink_strncpy(dest, source, padded_len);

#ifdef DEBUG
  //
  // what padded_len should be, if there is no padding, is strlen()+1.
  // if not, then we needed to pad and should touch the intermediate
  // bytes to avoid UMR errors when the buffer is written.
  //
  size_t real_len = (::strlen(source) + 1);
  while ((int) real_len < padded_len) {
    dest[real_len] = '$';
    real_len++;
  }
#endif
}

/*------------------------------------------------------------------------- 
  LogAccess::marshal_mem

  This is a version of marshal_str that works with unterminated strings.
  In this case, we'll copy the buffer and then add a trailing null that
  the rest of the system assumes.
  -------------------------------------------------------------------------*/

void
LogAccess::marshal_mem(char *dest, char *source, int actual_len, int padded_len)
{
  if (source == NULL || source[0] == 0 || actual_len == 0) {
    source = DEFAULT_STR;
    actual_len = DEFAULT_STR_LEN;
    ink_debug_assert(actual_len < padded_len);
  }
  memcpy(dest, source, actual_len);
  dest[actual_len] = 0;         // add terminating null

#ifdef DEBUG
  //
  // what len should be, if there is no padding, is strlen()+1.
  // if not, then we needed to pad and should touch the intermediate
  // bytes to avoid UMR errors when the buffer is written.
  //
  int real_len = actual_len + 1;
  while (real_len < padded_len) {
    dest[real_len] = '$';
    real_len++;
  }
#endif
}

inline int
LogAccess::unmarshal_with_map(LOG_INT code, char *dest, int len, Ptr<LogFieldAliasMap> map, char *msg)
{
  int codeStrLen;

  switch (map->asString(code, dest, len, (size_t *) & codeStrLen)) {

  case LogFieldAliasMap::INVALID_INT:
    if (msg) {
      const int bufSize = 64;
      char invalidCodeMsg[bufSize];
      codeStrLen = ink_snprintf(invalidCodeMsg, 64, "%s(%d)", msg, code);
      if (codeStrLen < bufSize && codeStrLen < len) {
        ink_strncpy(dest, invalidCodeMsg, len);
      } else {
        codeStrLen = -1;
      }
    } else {
      codeStrLen = -1;
    }
    break;
  case LogFieldAliasMap::BUFFER_TOO_SMALL:
    codeStrLen = -1;
    break;
  }

  return codeStrLen;
}

/*------------------------------------------------------------------------- 
  LogAccess::unmarshal_int

  Return the integer pointed at by the buffer and advance the buffer
  pointer past the int.  The int will be converted back to host byte order.
  -------------------------------------------------------------------------*/

LOG_INT LogAccess::unmarshal_int(char **buf)
{
  ink_assert(buf != NULL);
  ink_assert(*buf != NULL);
  LOG_INT
    val;

  val = ntohl(*((LOG_INT *) (*buf)));
  *buf += MIN_ALIGN;
  return val;
}

/*------------------------------------------------------------------------- 
  unmarshal_itoa

  This routine provides a fast conversion from a binary int to a string.
  It returns the number of characters formatted.  "dest" must point to the
  LAST character of an array large enough to store the complete formatted
  number.
  -------------------------------------------------------------------------*/

LOG_INT LogAccess::unmarshal_itoa(LOG_INT val, char *dest, int field_width, char leading_char)
{
  ink_assert(dest != NULL);

  char *
    p = dest;
  if (val <= 0) {
    *p-- = '0';
    while (dest - p < field_width) {
      *p-- = leading_char;
    }
    return (int) (dest - p);
  }

  while (val) {
    *p-- = '0' + (val % 10);
    val /= 10;
  }
  while (dest - p < field_width) {
    *p-- = leading_char;
  }
  return (int) (dest - p);
}

/*------------------------------------------------------------------------- 
  unmarshal_itox

  This routine provides a fast conversion from a binary int to a hex string.
  It returns the number of characters formatted.  "dest" must point to the
  LAST character of an array large enough to store the complete formatted
  number.
  -------------------------------------------------------------------------*/

LOG_INT LogAccess::unmarshal_itox(LOG_INT val, char *dest, int field_width, char leading_char)
{
  ink_assert(dest != NULL);

  char *
    p = dest;
  static char
    table[] = "0123456789abcdef?";

  for (int i = 0; i < (int) (sizeof(LOG_INT) * 2); i++) {
    *p-- = table[val & 0xf];
    val >>= 4;
  }
  while (dest - p < field_width) {
    *p-- = leading_char;
  }
  return (int) (dest - p);
}

/*------------------------------------------------------------------------- 
  LogAccess::unmarshal_int_to_str

  Return the string representation of the integer pointed at by buf.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_int_to_str(char **buf, char *dest, int len)
{
  ink_assert(buf != NULL);
  ink_assert(*buf != NULL);
  ink_assert(dest != NULL);

  char val_buf[128];
  LOG_INT val = unmarshal_int(buf);
  int val_len = unmarshal_itoa(val, val_buf + 127);
  if (val_len < len) {
    memcpy(dest, val_buf + 128 - val_len, val_len);
    return val_len;
  }
  return -1;
}

/*------------------------------------------------------------------------- 
  LogAccess::unmarshal_int_to_str_hex

  Return the string representation (hexadecimal) of the integer pointed at by buf.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_int_to_str_hex(char **buf, char *dest, int len)
{
  ink_assert(buf != NULL);
  ink_assert(*buf != NULL);
  ink_assert(dest != NULL);

  char val_buf[128];
  LOG_INT val = unmarshal_int(buf);
  int val_len = unmarshal_itox(val, val_buf + 127);
  if (val_len < len) {
    memcpy(dest, val_buf + 128 - val_len, val_len);
    return val_len;
  }
  return -1;
}



/*------------------------------------------------------------------------- 
  LogAccess::unmarshal_str

  Retrieve the string from the location pointed at by the buffer and
  advance the pointer past the string.  The local strlen function is used
  to advance the pointer, thus matching the corresponding strlen that was
  used to lay the string into the buffer.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_str(char **buf, char *dest, int len)
{
  ink_assert(buf != NULL);
  ink_assert(*buf != NULL);
  ink_assert(dest != NULL);

  char *val_buf = *buf;
  int val_len = (int)::strlen(val_buf);
  *buf += LogAccess::strlen(val_buf);   // this is how it was stored
  if (val_len < len) {
    memcpy(dest, val_buf, val_len);
    return val_len;
  }
  return -1;
}

int
LogAccess::unmarshal_ttmsf(char **buf, char *dest, int len)
{
  ink_assert(buf != NULL);
  ink_assert(*buf != NULL);
  ink_assert(dest != NULL);

  LOG_INT val = unmarshal_int(buf);
  float secs = (float) val / 1000;
  int val_len = ink_snprintf(dest, len, "%.3f", secs);
  return val_len;
}

/*------------------------------------------------------------------------- 
  LogAccess::unmarshal_http_method

  Retrieve the int pointed at by the buffer and treat as an HttpMethod
  enumerated type.  Then lookup the string representation for that enum and
  return the string.  Advance the buffer pointer past the enum.
  -------------------------------------------------------------------------*/
/*
int
LogAccess::unmarshal_http_method (char **buf, char *dest, int len)
{
    return unmarshal_str (buf, dest, len);
}
*/
/*------------------------------------------------------------------------- 
  LogAccess::unmarshal_http_version

  The http version is marshalled as two consecutive integers, the first for
  the major number and the second for the minor number.  Retrieve both
  numbers and return the result as "HTTP/major.minor".
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_http_version(char **buf, char *dest, int len)
{
  ink_assert(buf != NULL);
  ink_assert(*buf != NULL);
  ink_assert(dest != NULL);

  static char *http = "HTTP/";
  static int http_len = (int)::strlen(http);

  char val_buf[128];
  char *p = val_buf;

  memcpy(p, http, http_len);
  p += http_len;

  int res1 = unmarshal_int_to_str(buf, p, 128 - http_len);
  if (res1 < 0) {
    return -1;
  }
  p += res1;
  *p++ = '.';
  int res2 = unmarshal_int_to_str(buf, p, 128 - http_len - res1 - 1);
  if (res2 < 0) {
    return -1;
  }

  int val_len = http_len + res1 + res2 + 1;
  if (val_len < len) {
    memcpy(dest, val_buf, val_len);
    return val_len;
  }
  return -1;
}

/*------------------------------------------------------------------------- 
  LogAccess::unmarshal_http_text

  The http text is simply the fields http_method (cqhm) + url (cqu) +
  http_version (cqhv), all right next to each other, in that order.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_http_text(char **buf, char *dest, int len)
{
  ink_assert(buf != NULL);
  ink_assert(*buf != NULL);
  ink_assert(dest != NULL);

  char *p = dest;

//    int res1 = unmarshal_http_method (buf, p, len);
  int res1 = unmarshal_str(buf, p, len);
  if (res1 < 0) {
    return -1;
  }
  p += res1;
  *p++ = ' ';
  int res2 = unmarshal_str(buf, p, len - res1 - 1);
  if (res2 < 0) {
    return -1;
  }
  p += res2;
  *p++ = ' ';
  int res3 = unmarshal_http_version(buf, p, len - res1 - res2 - 2);
  if (res3 < 0) {
    return -1;
  }
  return res1 + res2 + res3 + 2;
}

/*------------------------------------------------------------------------- 
  LogAccess::unmarshal_http_status

  An http response status code (pssc,sssc) is just an INT, but it's always
  formatted with three digits and leading zeros.  So, we need a special
  version of unmarshal_int_to_str that does this leading zero formatting.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_http_status(char **buf, char *dest, int len)
{
  ink_assert(buf != NULL);
  ink_assert(*buf != NULL);
  ink_assert(dest != NULL);

  char val_buf[128];
  LOG_INT val = unmarshal_int(buf);
  int val_len = unmarshal_itoa(val, val_buf + 127, 3, '0');
  if (val_len < len) {
    memcpy(dest, val_buf + 128 - val_len, val_len);
    return val_len;
  }
  return -1;
}

/*------------------------------------------------------------------------- 
  LogAccess::unmarshal_ip

  Retrieve the int pointed at by the buffer and treat as an IP address.
  Convert to a string and return the string.  Advance the buffer pointer.
  String has the form "ddd.ddd.ddd.ddd".
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_ip(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map)
{
  ink_assert(buf != NULL);
  ink_assert(*buf != NULL);
  ink_assert(dest != NULL);

  return (LogAccess::unmarshal_with_map(unmarshal_int(buf), dest, len, map));
}

/*------------------------------------------------------------------------- 
  LogAccess::unmarshal_hierarchy

  Retrieve the int pointed at by the buffer and treat as a
  SquidHierarchyCode.  Use this as an index into the local string
  conversion tables and return the string equivalent to the enum.
  Advance the buffer pointer.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_hierarchy(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map)
{
  ink_assert(buf != NULL);
  ink_assert(*buf != NULL);
  ink_assert(dest != NULL);

  return (LogAccess::unmarshal_with_map(unmarshal_int(buf), dest, len, map, "INVALID_CODE"));
}

/*------------------------------------------------------------------------- 
  LogAccess::unmarshal_finish_status

  Retrieve the int pointed at by the buffer and treat as a finish code.
  Use the enum as an index into a string table and return the string equiv
  of the enum.  Advance the pointer.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_finish_status(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map)
{
  ink_assert(buf != NULL);
  ink_assert(*buf != NULL);
  ink_assert(dest != NULL);

  return (LogAccess::unmarshal_with_map(unmarshal_int(buf), dest, len, map, "UNKNOWN_FINISH_CODE"));
}


/*------------------------------------------------------------------------- 
  LogAccess::unmarshal_cache_code

  Retrieve the int pointed at by the buffer and treat as a SquidLogCode.
  Use this to index into the local string tables and return the string
  equiv of the enum.  Advance the pointer.
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_cache_code(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map)
{
  ink_assert(buf != NULL);
  ink_assert(*buf != NULL);
  ink_assert(dest != NULL);

  return (LogAccess::unmarshal_with_map(unmarshal_int(buf), dest, len, map, "ERROR_UNKNOWN"));
}

/*------------------------------------------------------------------------- 
  LogAccess::unmarshal_entry_type
  -------------------------------------------------------------------------*/

int
LogAccess::unmarshal_entry_type(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map)
{
  ink_assert(buf != NULL);
  ink_assert(*buf != NULL);
  ink_assert(dest != NULL);

  return (LogAccess::unmarshal_with_map(unmarshal_int(buf), dest, len, map, "UNKNOWN_ENTRY_TYPE"));
}

int
LogAccess::unmarshal_cache_write_code(char **buf, char *dest, int len, Ptr<LogFieldAliasMap> map)
{
  ink_assert(buf != NULL);
  ink_assert(*buf != NULL);
  ink_assert(dest != NULL);

  return (LogAccess::unmarshal_with_map(unmarshal_int(buf), dest, len, map, "UNKNOWN_CACHE_WRITE_CODE"));
}

int
LogAccess::unmarshal_record(char **buf, char *dest, int len)
{
  ink_assert(buf != NULL);
  ink_assert(*buf != NULL);
  ink_assert(dest != NULL);

  char *val_buf = *buf;
  int val_len = (int)::strlen(val_buf);
  *buf += MARSHAL_RECORD_LENGTH;        // this is how it was stored
  if (val_len < len) {
    memcpy(dest, val_buf, val_len);
    return val_len;
  }
  return -1;
}

/*------------------------------------------------------------------------- 
  resolve_logfield_string

  This function resolves the given custom log format string using the given
  LogAccess context and returns the resulting string, which is xmalloc'd.
  The caller is responsible for xfree'ing the return result.  If there are
  any problems, NULL is returned.
  -------------------------------------------------------------------------*/

char *
resolve_logfield_string(LogAccess * context, const char *format_str)
{
  if (!context) {
    Debug("log2-resolve", "No context to resolve?");
    return NULL;
  }

  if (!format_str) {
    Debug("log2-resolve", "No format to resolve?");
    return NULL;
  }

  Debug("log2-resolve", "Resolving: %s", format_str);

  //
  // Divide the format string into two parts: one for the printf-style
  // string and one for the symbols.
  //
  char *printf_str = NULL;
  char *fields_str = NULL;
  int n_fields = LogFormat::parse_format_string(format_str,
                                                &printf_str, &fields_str);

  Debug("log2-resolve", "%d fields: %s", n_fields, fields_str);
  Debug("log2-resolve", "printf string: %s", printf_str);

  //
  // Make sure that we delete these strings no matter how we exit
  //
  LogMemoryDeleter d1(printf_str, USE_XFREE);
  LogMemoryDeleter d2(fields_str, USE_XFREE);

  //
  // Perhaps there were no fields to resolve?  Then just return the
  // format_str.
  //
  if (!n_fields) {
    Debug("log2-resolve", "No fields found; returning copy of format_str");
    return xstrdup(format_str);
  }

  LogFieldList fields;
  bool contains_aggregates;
  int field_count = LogFormat::parse_symbol_string(fields_str, &fields,
                                                   &contains_aggregates);

  if (field_count != n_fields) {
    Debug("log2-resolve", "format_str contains %d invalid field symbols", n_fields - field_count);
    return NULL;
  }
  //
  // Ok, now marshal the data out of the LogAccess object and into a
  // temporary storage buffer.  Make sure the LogAccess context is
  // initialized first.
  //
  Debug("log2-resolve", "Marshaling data from LogAccess into buffer ...");
  context->init();
  unsigned bytes_needed = fields.marshal_len(context);
  char *buf = (char *) xmalloc(bytes_needed);
  ink_assert(buf != NULL);
  unsigned bytes_used = fields.marshal(context, buf);
  ink_assert(bytes_needed == bytes_used);
  Debug("log2-resolve", "    %u bytes marshalled", bytes_used);

  //
  // Now we can "unmarshal" the data from the buffer into a string,
  // combining it with the data from the printf string.  The problem is,
  // we're not sure how much space it will take when it's unmarshalled.
  // So, we'll just guess.
  //
  char *result = (char *) xmalloc(8192);
  memset(result, 0, 8192);      // makes sure the buffer is null terminated

  unsigned bytes_resolved = LogBuffer::resolve_custom_entry(&fields, printf_str, buf, result,
                                                            8192, LogUtils::timestamp(), 0,
                                                            LOG_SEGMENT_VERSION);
  ink_assert(bytes_resolved <= 8192);

  xfree(buf);

  return result;
}
