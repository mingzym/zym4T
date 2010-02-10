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
 * NetworkUtilsLocal.cc
 * 
 * contains implementation of local networking utility functions, such as
 * unmarshalling requests from a remote client and marshalling replies
 *
 * 
 ***************************************************************************/

#include "ink_config.h"
#include "ink_platform.h"
#include "ink_sock.h"
#include "Diags.h"
#include "MgmtUtils.h"
#include "CoreAPIShared.h"
#include "NetworkUtilsLocal.h"

#ifndef MAX_BUF_SIZE
#define MAX_BUF_SIZE 4096
#endif

/**************************************************************************
 * socket_flush
 *
 * flushes the socket by reading the entire message out of the socket 
 * and then gets rid of the msg
 **************************************************************************/
INKError
socket_flush(struct SocketInfo sock_info)
{
  int ret, byte_read = 0;
  char buf[MAX_BUF_SIZE];

  // check to see if anything to read; wait only for specified time 
  if (socket_read_timeout(sock_info.fd, MAX_TIME_WAIT, 0) <= 0) {
    return INK_ERR_NET_TIMEOUT;
  }
  // read entire message
  while (byte_read < MAX_BUF_SIZE) {

    ret = socket_read(sock_info, buf + byte_read, MAX_BUF_SIZE - byte_read);

    if (ret < 0) {
      if (errno == EAGAIN)
        continue;

      Debug("ts_main", "[socket_read_n] socket read for version byte failed.\n");
      mgmt_elog("[socket_flush] (INK_ERR_NET_READ) %s\n", strerror(errno));
      return INK_ERR_NET_READ;
    }

    if (ret == 0) {
      Debug("ts_main", "[socket_read_n] returned 0 on reading: %s.\n", strerror(errno));
      mgmt_log("[socket_flush] (INK_ERR_NET_EOF) %s\n", strerror(errno));
      return INK_ERR_NET_EOF;
    }
    // we are all good here
    byte_read += ret;
  }

  mgmt_elog("[socket_flush] uh oh! didn't finish flushing socket!\n");
  return INK_ERR_FAIL;
}

/**************************************************************************
 * socket_read_n							  
 * 
 * purpose: guarantees reading of n bytes or return error. 
 * input:   socket info struct, buffer to read into and number of bytes to read
 * output:  number of bytes read 
 * note:    socket_read is implemented in WebUtils.cc
 *************************************************************************/
INKError
socket_read_n(struct SocketInfo sock_info, char *buf, int bytes)
{
  int ret, byte_read = 0;

  // check to see if anything to read; wait for specified time 
  if (socket_read_timeout(sock_info.fd, MAX_TIME_WAIT, 0) <= 0) {
    return INK_ERR_NET_TIMEOUT;
  }
  // read until we fulfill the number
  while (byte_read < bytes) {
    ret = socket_read(sock_info, buf + byte_read, bytes - byte_read);

    // error!
    if (ret < 0) {
      if (errno == EAGAIN)
        continue;

      Debug("ts_main", "[socket_read_n] socket read for version byte failed.\n");
      mgmt_elog("[socket_read_n] (INK_ERR_NET_READ) %s\n", strerror(errno));
      return INK_ERR_NET_READ;
    }

    if (ret == 0) {
      Debug("ts_main", "[socket_read_n] returned 0 on reading: %s.\n", strerror(errno));
      mgmt_log("[socket_read_n] (INK_ERR_NET_EOF) %s\n", strerror(errno));
      return INK_ERR_NET_EOF;
    }
    // we are all good here
    byte_read += ret;
  }

  return INK_ERR_OKAY;
}

/**************************************************************************
 * socket_write_n
 * 
 * purpose: guarantees writing of n bytes or return error
 * input:   socket info struct, buffer to write from & number of bytes to write
 * output:  INK_ERR_xx (depends on num bytes written)
 * note:    socket_read is implemented in WebUtils.cc
 *************************************************************************/
INKError
socket_write_n(struct SocketInfo sock_info, const char *buf, int bytes)
{
  int ret, byte_wrote = 0;

  // makes sure the socket descriptor is writable
  if (socket_write_timeout(sock_info.fd, MAX_TIME_WAIT, 0) <= 0) {
    return INK_ERR_NET_TIMEOUT;
  }
  // read until we fulfill the number
  while (byte_wrote < bytes) {
    ret = socket_write(sock_info, buf + byte_wrote, bytes - byte_wrote);

    if (ret < 0) {
      Debug("ts_main", "[socket_write_n] return error %s \n", strerror(errno));
      mgmt_elog("[socket_write_n] %s\n", strerror(errno));
      if (errno == EAGAIN)
        continue;

      return INK_ERR_NET_WRITE;
    }

    if (ret == 0) {
      mgmt_elog("[socket_write_n] %s\n", strerror(errno));
      return INK_ERR_NET_EOF;
    }
    // we are all good here
    byte_wrote += ret;
  }

  return INK_ERR_OKAY;
}


/**********************************************************************
 * preprocess_msg
 *
 * purpose: reads in all the message; parses the message into header info
 *          (OpType + msg_len) and the request portion (used by the handle_xx fns)
 * input: sock_info - socket msg is read from
 *        op_t      - the operation type specified in the msg
 *        msg       - the data from the network message (no OpType or msg_len) 
 * output: INK_ERR_xx ( if INK_ERR_OKAY, then parameters set successfully)
 * notes: Since preprocess_msg already removes the OpType and msg_len, this part o
 *        the message is not dealt with by the other parsing functions
 **********************************************************************/
INKError
preprocess_msg(struct SocketInfo sock_info, OpType * op_t, char **req)
{
  INKError ret;
  int req_len;
  ink16 op;

  // read operation type
  ret = socket_read_n(sock_info, (char *) &op, SIZE_OP_T);
  if (ret != INK_ERR_OKAY) {
    Debug("ts_main", "[preprocess_msg] ERROR %d reading op type\n", ret);
    goto Lerror;
  }

  Debug("ts_main", "[preprocess_msg] operation = %d", op);
  *op_t = (OpType) op;          // convert to proper format

  // check if invalid op type
  if ((int) op >= TOTAL_NUM_OP_TYPES) {
    mgmt_elog("[preprocess_msg] ERROR: %d is invalid op type\n", op);

    // need to flush the invalid message from the socket
    if ((ret = socket_flush(sock_info)) != INK_ERR_NET_EOF)
      mgmt_log("[preprocess_msg] unsuccessful socket flushing\n");
    else
      mgmt_log("[preprocess_msg] successfully flushed the socket\n");

    goto Lerror;
  }
  // now read the request msg size
  ret = socket_read_n(sock_info, (char *) &req_len, SIZE_LEN);
  if (ret != INK_ERR_OKAY) {
    mgmt_elog("[preprocess_msg] ERROR %d reading msg size\n", ret);
    Debug("ts_main", "[preprocess_msg] ERROR %d reading msg size\n", ret);
    goto Lerror;
  }

  Debug("ts_main", "[preprocess_msg] length = %d\n", req_len);

  // use req msg length to fetch the rest of the message
  // first check that there is a "rest of the msg", some msgs just 
  // have the op specified
  if (req_len == 0) {
    *req = NULL;
    Debug("ts_main", "[preprocess_msg] request message = NULL\n");
  } else {
    *req = (char *) xmalloc(sizeof(char) * (req_len + 1));
    if (!(*req)) {
      ret = INK_ERR_SYS_CALL;
      goto Lerror;
    }

    ret = socket_read_n(sock_info, *req, req_len);
    if (ret != INK_ERR_OKAY) {
      xfree(*req);
      goto Lerror;
    }
    // add end of string to end of msg
    (*req)[req_len] = '\0';
    Debug("ts_main", "[preprocess_msg] request message = %s\n", *req);
  }

  return INK_ERR_OKAY;

Lerror:
  return ret;
}


/**********************************************************************
 * Unmarshal Requests
 **********************************************************************/

/**********************************************************************
 * parse_file_read_request
 *
 * purpose: parses a file read request from a remote API client
 * input: req - data that needs to be parsed 
 *        file - the file type sent in the request
 * output: INK_ERR_xx
 * notes: request format = <INKFileNameT>
 **********************************************************************/
INKError
parse_file_read_request(char *req, INKFileNameT * file)
{
  ink16 file_t;

  if (!req || !file)
    return INK_ERR_PARAMS;

  // get file type - copy first 2 bytes of request
  memcpy(&file_t, req, SIZE_FILE_T);
  *file = (INKFileNameT) file_t;

  return INK_ERR_OKAY;
}

/**********************************************************************
 * parse_file_write_request
 *
 * purpose: parses a file write request from a remote API client
 * input: socket info
 *        file - the file type to write that was sent in the request
 *        text - the text that needs to be written
 *        size - length of the text
 *        ver  - version of the file that is to be written
 * output: INK_ERR_xx
 * notes: request format = <INKFileNameT> <version> <size> <text>
 **********************************************************************/
INKError
parse_file_write_request(char *req, INKFileNameT * file, int *ver, int *size, char **text)
{
  ink16 file_t, f_ver;
  ink32 f_size;

  // check input is non-NULL
  if (!req || !file || !ver || !size || !text)
    return INK_ERR_PARAMS;

  // get file type - copy first 2 bytes of request
  memcpy(&file_t, req, SIZE_FILE_T);
  *file = (INKFileNameT) file_t;

  // get file version - copy next 2 bytes
  memcpy(&f_ver, req + SIZE_FILE_T, SIZE_VER);
  *ver = (int) f_ver;

  // get file size - copy next 4 bytes
  memcpy(&f_size, req + SIZE_FILE_T + SIZE_VER, SIZE_LEN);
  *size = (int) f_size;

  // get file text
  *text = (char *) xmalloc(sizeof(char) * (f_size + 1));
  if (!(*text))
    return INK_ERR_SYS_CALL;
  memcpy(*text, req + SIZE_FILE_T + SIZE_VER + SIZE_LEN, f_size);
  (*text)[f_size] = '\0';       // end buffer

  return INK_ERR_OKAY;
}

/**********************************************************************
 * parse_request_name_value
 *
 * purpose: parses a request w/ 2 args from a remote API client
 * input: req - request info from requestor
 *        name - first arg
 *        val  - second arg
 * output: INK_ERR_xx
 * notes: format= <name_len> <val_len> <name> <val>
 **********************************************************************/
INKError
parse_request_name_value(char *req, char **name_1, char **val_1)
{
  ink32 name_len, val_len;
  char *name, *val;

  if (!req || !name_1 || !val_1)
    return INK_ERR_PARAMS;

  // get record name length
  memcpy(&name_len, req, SIZE_LEN);

  // get record value length
  memcpy(&val_len, req + SIZE_LEN, SIZE_LEN);

  // get record name
  name = (char *) xmalloc(sizeof(char) * (name_len + 1));
  if (!name)
    return INK_ERR_SYS_CALL;
  memcpy(name, req + SIZE_LEN + SIZE_LEN, name_len);
  name[name_len] = '\0';        // end string
  *name_1 = name;

  // get record value - can be a MgmtInt, MgmtCounter ... 
  val = (char *) xmalloc(sizeof(char) * (val_len + 1));
  if (!val)
    return INK_ERR_SYS_CALL;
  memcpy(val, req + SIZE_LEN + SIZE_LEN + name_len, val_len);
  val[val_len] = '\0';          // end string
  *val_1 = val;

  return INK_ERR_OKAY;
}


/**********************************************************************
 * parse_diags_request
 *
 * purpose: parses a diags request
 * input: diag_msg - the diag msg to be outputted
 *        mode     - indicates what type of diag message
 * output: INK_ERR_xx
 * notes: request format = <INKDiagsT> <diag_msg_len> <diag_msg>
 **********************************************************************/
INKError
parse_diags_request(char *req, INKDiagsT * mode, char **diag_msg)
{
  ink16 diag_t;
  ink32 msg_len;

  // check input is non-NULL
  if (!req || !mode || !diag_msg)
    return INK_ERR_PARAMS;

  // get diags type - copy first 2 bytes of request
  memcpy(&diag_t, req, SIZE_DIAGS_T);
  *mode = (INKDiagsT) diag_t;

  // get msg size - copy next 4 bytes
  memcpy(&msg_len, req + SIZE_DIAGS_T, SIZE_LEN);

  // get msg
  *diag_msg = (char *) xmalloc(sizeof(char) * (msg_len + 1));
  if (!(*diag_msg))
    return INK_ERR_SYS_CALL;
  memcpy(*diag_msg, req + SIZE_DIAGS_T + SIZE_LEN, msg_len);
  (*diag_msg)[msg_len] = '\0';  // end buffer

  return INK_ERR_OKAY;
}

/**********************************************************************
 * parse_proxy_state_request
 *
 * purpose: parses a request to set the proxy state
 * input: diag_msg - the diag msg to be outputted
 *        mode     - indicates what type of diag message
 * output: INK_ERR_xx
 * notes: request format = <INKProxyStateT> <INKCacheClearT>
 **********************************************************************/
INKError
parse_proxy_state_request(char *req, INKProxyStateT * state, INKCacheClearT * clear)
{
  ink16 state_t, cache_t;

  // check input is non-NULL
  if (!req || !state || !clear)
    return INK_ERR_PARAMS;

  // get proxy on/off
  memcpy(&state_t, req, SIZE_PROXY_T);
  *state = (INKProxyStateT) state_t;

  // get cahce-clearing type
  memcpy(&cache_t, req + SIZE_PROXY_T, SIZE_TS_ARG_T);
  *clear = (INKCacheClearT) cache_t;

  return INK_ERR_OKAY;
}

/**********************************************************************
 * Marshal Replies 
 **********************************************************************/
/* NOTE: if the send function "return"s before writing to the socket 
  then that means that an error occurred, and so the calling function 
  must send_reply with the error that occurred. */

/**********************************************************************
 * send_reply
 *
 * purpose: sends a simple INK_ERR_* reply to the request made 
 * input: return value - could be extended to support more complex 
 *        error codes but for now use only INK_ERR_FAIL, INK_ERR_OKAY
 *        int fd - socket fd to use.
 * output: INK_ERR_*
 * notes: this function does not need to go through the internal structure
 *        so no cleaning up is done. 
 **********************************************************************/
INKError
send_reply(struct SocketInfo sock_info, INKError retval)
{
  INKError ret;
  char msg[SIZE_ERR_T];
  ink16 ret_val;

  // write the return value
  ret_val = (ink16) retval;
  memcpy(msg, (void *) &ret_val, SIZE_ERR_T);

  // now push it to the socket
  ret = socket_write_n(sock_info, msg, SIZE_ERR_T);

  return ret;
}

/**********************************************************************
 * send_reply_list 
 *
 * purpose: sends the reply in response to a request to get list of string 
 *          tokens (delimited by REMOTE_DELIM_STR)
 * input: sock_info -
 *        retval - INKError return type for the CoreAPI call
 *        list - string delimited list of string tokens
 * output: INK_ERR_*
 * notes: 
 * format: <INKError> <string_list_len> <delimited_string_list>
 **********************************************************************/
INKError
send_reply_list(struct SocketInfo sock_info, INKError retval, char *list)
{
  INKError ret;
  int msg_pos = 0, total_len;
  char *msg;
  ink16 ret_val;
  ink32 list_size;              // to be safe, typecast

  if (!list) {
    return INK_ERR_PARAMS;
  }

  total_len = SIZE_ERR_T + SIZE_LEN + strlen(list);
  msg = (char *) xmalloc(sizeof(char) * total_len);
  if (!msg)
    return INK_ERR_SYS_CALL;    // ERROR - malloc failed

  // write the return value
  ret_val = (ink16) retval;
  memcpy(msg, (void *) &ret_val, SIZE_ERR_T);
  msg_pos += SIZE_ERR_T;

  // write the length of the string list
  list_size = (ink32) strlen(list);
  memcpy(msg + msg_pos, (void *) &list_size, SIZE_LEN);
  msg_pos += SIZE_LEN;

  // write the event string list
  memcpy(msg + msg_pos, list, list_size);

  // now push it to the socket
  ret = socket_write_n(sock_info, msg, total_len);
  xfree(msg);

  return ret;
}


/**********************************************************************
 * send_record_get_reply
 *
 * purpose: sends reply to the record_get request made 
 * input: retval   - result of the record get request
 *        int fd   - socket fd to use.
 *        val      - the value of the record requested 
 *        val_size - num bytes the value occupies
 *        rec_type - the type of the record value requested
 * output: INK_ERR_*
 * notes: this function does not need to go through the internal structure
 *        so no cleaning up is done. 
 *        format = <INKError> <rec_val_len> <rec_type> <rec_val> 
 **********************************************************************/
INKError
send_record_get_reply(struct SocketInfo sock_info, INKError retval, void *val, int val_size, INKRecordT rec_type)
{
  INKError ret;
  int msg_pos = 0, total_len;
  char *msg;
  ink16 record_t, ret_val;
  ink32 v_size;                 // to be safe, typecast

  if (!val) {
    return INK_ERR_PARAMS;
  }

  total_len = SIZE_ERR_T + SIZE_LEN + SIZE_REC_T + val_size;
  msg = (char *) xmalloc(sizeof(char) * total_len);
  if (!msg)
    return INK_ERR_SYS_CALL;    // ERROR - malloc failed

  // write the return value
  ret_val = (ink16) retval;
  memcpy(msg, (void *) &ret_val, SIZE_ERR_T);
  msg_pos += SIZE_ERR_T;

  // write the size of the record value
  v_size = (ink32) val_size;
  memcpy(msg + msg_pos, (void *) &v_size, SIZE_LEN);
  msg_pos += SIZE_LEN;

  // write the record type
  record_t = (ink16) rec_type;
  memcpy(msg + msg_pos, (void *) &record_t, SIZE_REC_T);
  msg_pos += SIZE_REC_T;

  // write the record value
  memcpy(msg + msg_pos, val, val_size);

  // now push it to the socket
  ret = socket_write_n(sock_info, msg, total_len);
  xfree(msg);

  return ret;
}

/**********************************************************************
 * send_record_set_reply
 *
 * purpose: sends reply to the record_set request made 
 * input: 
 * output: INK_ERR_*
 * notes: this function does not need to go through the internal structure
 *        so no cleaning up is done. 
 *        format = 
 **********************************************************************/
INKError
send_record_set_reply(struct SocketInfo sock_info, INKError retval, INKActionNeedT action_need)
{
  INKError ret;
  int total_len;
  char *msg;
  ink16 action_t, ret_val;

  total_len = SIZE_ERR_T + SIZE_ACTION_T;
  msg = (char *) xmalloc(sizeof(char) * total_len);
  if (!msg)
    return INK_ERR_SYS_CALL;    // ERROR - malloc failed!

  // write the return value
  ret_val = (ink16) retval;
  memcpy(msg, (void *) &ret_val, SIZE_ERR_T);

  // write the action needed
  action_t = (ink16) action_need;
  memcpy(msg + SIZE_ERR_T, (void *) &action_t, SIZE_ACTION_T);

  // now push it to the socket
  ret = socket_write_n(sock_info, msg, total_len);

  xfree(msg);

  return ret;
}


/**********************************************************************
 * send_file_read_reply
 *
 * purpose: sends the reply in response to a file read request  
 * input: return value - could be extended to support more complex 
 *        error codes but for now use only INK_ERR_FAIL, INK_ERR_OKAY
 *        int fd - socket fd to use.
 * output: INK_ERR_*
 * notes: this function does not need to go through the internal structure
 *        so no cleaning up is done. 
 *        reply format = <INKError> <file_ver> <file_size> <file_text>
 **********************************************************************/
INKError
send_file_read_reply(struct SocketInfo sock_info, INKError retval, int ver, int size, char *text)
{
  INKError ret;
  int msg_pos = 0, msg_len;
  char *msg;
  ink16 ret_val, f_ver;
  ink32 f_size;                 // to be safe

  if (!text)
    return INK_ERR_PARAMS;

  // allocate space for buffer
  msg_len = SIZE_ERR_T + SIZE_VER + SIZE_LEN + size;
  msg = (char *) xmalloc(sizeof(char) * msg_len);
  if (!msg)
    return INK_ERR_SYS_CALL;

  // write the return value
  ret_val = (ink16) retval;
  memcpy(msg, (void *) &ret_val, SIZE_ERR_T);
  msg_pos += SIZE_ERR_T;

  // write file version
  f_ver = (ink16) ver;
  memcpy(msg + msg_pos, (void *) &f_ver, SIZE_VER);
  msg_pos += SIZE_VER;

  // write file size
  f_size = (ink32) size;
  memcpy(msg + msg_pos, (void *) &f_size, SIZE_LEN);
  msg_pos += SIZE_LEN;

  // write the file text
  memcpy(msg + msg_pos, text, size);

  // now push it to the socket
  ret = socket_write_n(sock_info, msg, msg_len);

  xfree(msg);

  return ret;
}


/**********************************************************************
 * send_proxy_state_get_reply
 *
 * purpose: sends the reply in response to a request to get state of proxy
 * input: 
 *        int fd - socket fd to use.
 * output: INK_ERR_*
 * notes: this function DOES NOT HAVE IT"S OWN INKError TO SEND!!!!
 *        reply format = <INKProxyStateT>
 **********************************************************************/
INKError
send_proxy_state_get_reply(struct SocketInfo sock_info, INKProxyStateT state)
{
  INKError ret;
  char msg[SIZE_PROXY_T];
  ink16 state_t;

  // write the state
  state_t = (ink16) state;
  memcpy(msg, (void *) &state_t, SIZE_PROXY_T);

  // now push it to the socket
  ret = socket_write_n(sock_info, msg, SIZE_PROXY_T);

  return ret;
}


/**********************************************************************
 * send_event_active_reply
 *
 * purpose: sends the reply in response to a request check if event is active
 * input: sock_info -
 *        retval - INKError return type for the EventIsActive core call
 *        active - is the requested event active or not?
 * output: INK_ERR_*
 * notes: 
 * format: <INKError> <bool>
 **********************************************************************/
INKError
send_event_active_reply(struct SocketInfo sock_info, INKError retval, bool active)
{
  INKError ret;
  int total_len;
  char *msg;
  ink16 is_active, ret_val;

  total_len = SIZE_ERR_T + SIZE_BOOL;
  msg = (char *) xmalloc(sizeof(char) * total_len);
  if (!msg)
    return INK_ERR_SYS_CALL;    // ERROR - malloc failed!

  // write the return value
  ret_val = (ink16) retval;
  memcpy(msg, (void *) &ret_val, SIZE_ERR_T);

  // write the boolean active state
  is_active = (ink16) active;
  memcpy(msg + SIZE_ERR_T, (void *) &is_active, SIZE_BOOL);

  // now push it to the socket
  ret = socket_write_n(sock_info, msg, total_len);

  xfree(msg);

  return ret;
}

/**********************************************************************
 * send_event_notification
 *
 * purpose: sends to the client a msg indicating that a certain event
 *          has occurred (this msg will be received by the event_poll_thread)
 * input: fd - file descriptor to use for writing
 *        event - the event that was signalled on TM side
 * output: INK_ERR_xx
 * note: format: <OpType> <event_name_len> <event_name> <desc_len> <desc>
 **********************************************************************/
INKError
send_event_notification(struct SocketInfo sock_info, INKEvent * event)
{
  INKError ret;
  int total_len, name_len, desc_len;
  char *msg;
  ink16 op_t;
  ink32 len;

  if (!event || !event->name || !event->description)
    return INK_ERR_PARAMS;

  name_len = strlen(event->name);
  desc_len = strlen(event->description);
  total_len = SIZE_OP_T + (SIZE_LEN * 2) + name_len + desc_len;
  msg = (char *) xmalloc(sizeof(char) * total_len);
  if (!msg)
    return INK_ERR_SYS_CALL;    // ERROR - malloc failed!

  // write the operation
  op_t = (ink16) EVENT_NOTIFY;
  memcpy(msg, (void *) &op_t, SIZE_OP_T);

  // write the size of the event name
  len = (ink32) name_len;
  memcpy(msg + SIZE_OP_T, (void *) &len, SIZE_LEN);

  // write the event name
  memcpy(msg + SIZE_OP_T + SIZE_LEN, event->name, name_len);

  // write size of description
  len = (ink32) desc_len;
  memcpy(msg + SIZE_OP_T + SIZE_LEN + name_len, (void *) &len, SIZE_LEN);

  // write the description
  memcpy(msg + SIZE_OP_T + SIZE_LEN + name_len + SIZE_LEN, event->description, desc_len);

  // now push it to the socket
  ret = socket_write_n(sock_info, msg, total_len);

  xfree(msg);

  return ret;
}
