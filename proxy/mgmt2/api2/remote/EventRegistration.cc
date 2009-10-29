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
 * Filename: EventRegistration.cc
 * Purpose: This file contains functions and structures used in event 
 *          notification and callbacks for remote clients; also has the 
 *          thread that services event notification.
 * Created: 2/15/01
 * 
 * 
 ***************************************************************************/

#include <stdio.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <string.h>

#include "ink_thread.h"
#include "inktomi++.h"
#include "MgmtSocket.h"

#include "INKMgmtAPI.h"
#include "EventRegistration.h"
#include "CoreAPIShared.h"
#include "NetworkUtilsDefs.h"
#include "NetworkUtilsRemote.h"
#include "EventCallback.h"

#ifndef _WIN32

CallbackTable *remote_event_callbacks;

/**********************************************************************
 * event_poll_thread_main
 * 
 * purpose: thread listens on the client's event socket connection; 
 *          only reads from the event_socket connection and  
 *          processes EVENT_NOTIFY messages; each time client 
 *          makes new event-socket connection to TM, must launch
 *          a new event_poll_thread_main thread 
 * input:   arg - contains the socket_fd to listen on
 * output:  NULL - if error
 * notes:   each time the client's socket connection to TM is reset
 *          a new thread will be launched as old one dies; there are 
 *          only two places where a new thread is created:
 *          1) when client first connects (INKInit call)
 *          2) client reconnects() due to a TM restart
 * Uses blocking socket; so blocks until receives an event notification.
 * Shouldn't need to use select since only waiting for a notification 
 * message from event_callback_main thread!
 **********************************************************************/
void *
event_poll_thread_main(void *arg)
{
  INKError err;
  int sock_fd;
  INKEvent *event_notice;

  sock_fd = *((int *) arg);     // should be same as event_socket_fd

  // Not use here.
  //fd_set selectFDs;
  //struct timeval timeout;

  // the sock_fd is going to be the one we listen for events on
  while (1) {

    // possible sock_fd is invalid if TM restarts and client reconnects
    if (sock_fd < 0) {
      //fprintf(stderr, "[event_poll_thread_main] EXIT: invalid socket %d\n", sock_fd);
      return NULL;              // exit thread
    }
    // read the entire message, so create INKEvent for the callback
    event_notice = INKEventCreate();
    err = parse_event_notification(sock_fd, event_notice);
    if (err == INK_ERR_NET_READ || err == INK_ERR_NET_EOF) {
      goto END;                 // socket connection error; kill the thread!
    } else if (err != INK_ERR_OKAY) {
      INKEventDestroy(event_notice);
      continue;                 // skip the message
    }
    // got event notice; spawn new thread to handle the event's callback functions
    ink_thread_create(event_callback_thread, (void *) event_notice);

  }                             // end while(1)

END:
  INKEventDestroy(event_notice);
  ink_thread_exit(NULL);
  return NULL;
}

/**********************************************************************
 * event_callback_thread
 *
 * purpose: Given an event, determines and calls the registered cb functions
 *          in the CallbackTable for remote events
 * input: arg - should be an INKEvent with the event info sent from TM msg
 * output: returns when done calling all the callbacks
 * notes: None
 **********************************************************************/
void *
event_callback_thread(void *arg)
{
  INKEvent *event_notice;
  EventCallbackT *event_cb;
  int index;

  event_notice = (INKEvent *) arg;
  index = (int) event_notice->id;
  LLQ *func_q;                  // list of callback functions need to call

  func_q = create_queue();
  if (!func_q) {
    return NULL;
  }
  // obtain lock
  ink_mutex_acquire(&remote_event_callbacks->event_callback_lock);

  INKEventSignalFunc cb;

  // check if we have functions to call
  if (remote_event_callbacks->event_callback_l[index] &&
      (!queue_is_empty(remote_event_callbacks->event_callback_l[index]))) {

    int queue_depth;

    queue_depth = queue_len(remote_event_callbacks->event_callback_l[index]);

    for (int i = 0; i < queue_depth; i++) {
      event_cb = (EventCallbackT *) dequeue(remote_event_callbacks->event_callback_l[index]);
      cb = event_cb->func;
      enqueue(remote_event_callbacks->event_callback_l[index], event_cb);
      enqueue(func_q, (void *) cb);     // add callback function only to list
    }
  }
  // release lock
  ink_mutex_release(&remote_event_callbacks->event_callback_lock);

  // execute the callback function
  while (!queue_is_empty(func_q)) {
    cb = (INKEventSignalFunc) dequeue(func_q);
    (*cb) (event_notice->name, event_notice->description, event_notice->priority, NULL);
  }

  // clean up event notice 
  INKEventDestroy(event_notice);
  delete_queue(func_q);

  // all done!
  return NULL;
}

#endif
