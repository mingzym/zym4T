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

  ClusterProcessor.cc
****************************************************************************/

#include "P_Cluster.h"
/*************************************************************************/
// ClusterProcessor member functions (Public class)
/*************************************************************************/
int cluster_port_number = DEFAULT_CLUSTER_PORT_NUMBER;
int cache_clustering_enabled = 0;

ClusterProcessor clusterProcessor;
RecRawStatBlock *cluster_rsb = NULL;
int ET_CLUSTER;

ClusterProcessor::ClusterProcessor():accept_handler(NULL), this_cluster(NULL)
{
}

ClusterProcessor::~ClusterProcessor()
{
  if (accept_handler) {
    accept_handler->ShutdownDelete();
    accept_handler = 0;
  }
}

int
ClusterProcessor::internal_invoke_remote(ClusterMachine * m, int cluster_fn,
                                         void *data, int len, int options, void *cmsg)
{
  EThread *thread = this_ethread();
  ProxyMutex *mutex = thread->mutex;
  //
  // RPC facility for intercluster communication available to other
  //  subsystems.
  //
  bool steal = (options & CLUSTER_OPT_STEAL ? true : false);
  bool delay = (options & CLUSTER_OPT_DELAY ? true : false);
  bool data_in_ocntl = (options & CLUSTER_OPT_DATA_IS_OCONTROL ? true : false);
  bool malloced = (cluster_fn == CLUSTER_FUNCTION_MALLOCED);
  OutgoingControl *c;

  ClusterHandler *ch = m->clusterHandler;
  if (!ch || (!malloced && !((unsigned int) cluster_fn < (inku32) SIZE_clusterFunction))) {
    // Invalid message or node is down, free message data
    if (malloced) {
      xfree(data);
    }
    if (cmsg) {
      invoke_remote_data_args *args = (invoke_remote_data_args *)
        (((OutgoingControl *) cmsg)->data + sizeof(ink32));
      ink_assert(args->magicno == invoke_remote_data_args::MagicNo);

      args->data_oc->freeall();
      ((OutgoingControl *) cmsg)->freeall();
    }
    if (data_in_ocntl) {
      c = *((OutgoingControl **) ((char *) data - sizeof(OutgoingControl *)));
      c->freeall();
    }
    return -1;
  }

  if (data_in_ocntl) {
    c = *((OutgoingControl **) ((char *) data - sizeof(OutgoingControl *)));
  } else {
    c = OutgoingControl::alloc();
  }
  CLUSTER_INCREMENT_DYN_STAT(CLUSTER_CTRL_MSGS_SENT_STAT);
  c->m = m;
  c->submit_time = ink_get_hrtime();

  if (malloced) {
    c->set_data((char *) data, len);
  } else {
    if (!data_in_ocntl) {
      c->len = len + sizeof(ink32);
      c->alloc_data();
    }
    if (!c->fast_data()) {
      CLUSTER_INCREMENT_DYN_STAT(CLUSTER_SLOW_CTRL_MSGS_SENT_STAT);
    }
    *(ink32 *) c->data = cluster_fn;
    if (!data_in_ocntl) {
      memcpy(c->data + sizeof(ink32), data, len);
    }
  }

  SET_CONTINUATION_HANDLER(c, (OutgoingCtrlHandler) & OutgoingControl::startEvent);

  /////////////////////////////////////
  // Compound message adjustments
  /////////////////////////////////////
  if (cmsg) {
    invoke_remote_data_args *args = (invoke_remote_data_args *)
      (((OutgoingControl *) cmsg)->data + sizeof(ink32));
    ink_assert(args->magicno == invoke_remote_data_args::MagicNo);
    args->msg_oc = c;
    c = (OutgoingControl *) cmsg;
  }
#ifndef CLUSTER_THREAD_STEALING
  delay = true;
#endif
  if (!delay) {
    EThread *tt = this_ethread();
    {
      int q = ClusterFuncToQpri(cluster_fn);
      ink_atomiclist_push(&ch->outgoing_control_al[q], (void *) c);

      MUTEX_TRY_LOCK(lock, ch->mutex, tt);
      if (!lock) {
        return 1;
      }
      if (steal)
        ch->steal_thread(tt);
      return 1;
    }
  } else {
    c->mutex = ch->mutex;
    eventProcessor.schedule_imm(c);
    return 0;
  }
}

int
ClusterProcessor::invoke_remote(ClusterMachine * m, int cluster_fn, void *data, int len, int options)
{
  return internal_invoke_remote(m, cluster_fn, data, len, options, (void *) NULL);
}

int
ClusterProcessor::invoke_remote_data(ClusterMachine * m, int cluster_fn,
                                     void *data, int data_len,
                                     IOBufferBlock * buf,
                                     int dest_channel, ClusterVCToken * token,
                                     void (*bufdata_free_proc) (void *), void *bufdata_free_proc_arg, int options)
{
  if (!buf) {
    // No buffer data, translate this into a invoke_remote() request
    return internal_invoke_remote(m, cluster_fn, data, data_len, options, (void *) NULL);
  }
  ink_assert(data);
  ink_assert(data_len);
  ink_assert(dest_channel);
  ink_assert(token);
  ink_assert(bufdata_free_proc);
  ink_assert(bufdata_free_proc_arg);

  /////////////////////////////////////////////////////////////////////////
  // Build the compound message as described by invoke_remote_data_args.
  /////////////////////////////////////////////////////////////////////////

  // Build OutgoingControl for buffer data
  OutgoingControl *bufdata_oc = OutgoingControl::alloc();
  bufdata_oc->set_data(buf, bufdata_free_proc, bufdata_free_proc_arg);

  // Build OutgoingControl for compound message header
  invoke_remote_data_args mh;
  mh.msg_oc = 0;
  mh.data_oc = bufdata_oc;
  mh.dest_channel = dest_channel;
  mh.token = *token;

  OutgoingControl *chdr = OutgoingControl::alloc();
  chdr->submit_time = ink_get_hrtime();
  chdr->len = sizeof(ink32) + sizeof(mh);
  chdr->alloc_data();
  *(ink32 *) chdr->data = -1;   // always -1 for compound message
  memcpy(chdr->data + sizeof(ink32), (char *) &mh, sizeof(mh));

  return internal_invoke_remote(m, cluster_fn, data, data_len, options, (void *) chdr);
}

void
ClusterProcessor::free_remote_data(char *p, int l)
{
  char *d = p - sizeof(ink32);  // reset to ptr to function code
  int data_hdr = ClusterControl::DATA_HDR;

  ink_release_assert(*((inku8 *) (d - data_hdr + 1)) == (inku8) ALLOC_DATA_MAGIC);
  char size_index = *(d - data_hdr);
  if (size_index >= 0) {
    ink_release_assert((0 <= size_index) && (size_index <= (DEFAULT_BUFFER_SIZES - 1)));
  } else {
    ink_release_assert(size_index == -1);
  }

  // Extract 'this' pointer

  ClusterControl *ccl;
  memcpy((char *) &ccl, (d - data_hdr + 2), sizeof(void *));
  ink_assert(ccl->valid_alloc_data());

  // Deallocate control structure and data

  ccl->freeall();
}

ClusterVConnection *
ClusterProcessor::open_local(Continuation * cont, ClusterMachine * m, ClusterVCToken & token, int options)
{
  //
  //  New connect protocol.
  //  As a VC initiator, establish the VC connection to the remote node
  //  by allocating the VC locally and requiring the caller to pass the
  //  token and channel id in the remote request.  The remote handler calls
  //  connect_local to establish the remote side of the connection.
  //
  bool immediate = ((options & CLUSTER_OPT_IMMEDIATE) ? true : false);
  bool allow_immediate = ((options & CLUSTER_OPT_ALLOW_IMMEDIATE) ? true : false);
  int result;

  ClusterHandler *ch = m->clusterHandler;
  if (!ch)
    return NULL;
  EThread *t = ch->thread;
  if (!t)
    return NULL;

  EThread *thread = this_ethread();
  ProxyMutex *mutex = thread->mutex;
  ClusterVConnection *vc = clusterVCAllocator.alloc();
  vc->new_connect_read = (options & CLUSTER_OPT_CONN_READ ? 1 : 0);
  vc->start_time = ink_get_hrtime();
  vc->last_activity_time = vc->start_time;
  vc->machine = m;
  vc->token.alloc();
  token = vc->token;
#ifdef CLUSTER_THREAD_STEALING
  CLUSTER_INCREMENT_DYN_STAT(CLUSTER_CONNECTIONS_OPENNED_STAT);
  CLUSTER_INCREMENT_DYN_STAT(CLUSTER_CONNECTIONS_OPEN_STAT);
  MUTEX_TRY_LOCK(lock, ch->mutex, thread);
  if (!lock) {
#endif
    if (immediate) {
      clusterVCAllocator_free(vc);
      return NULL;
    }
    vc->action_ = cont;
    ink_atomiclist_push(&ch->external_incoming_open_local, (void *) vc);
    return CLUSTER_DELAYED_OPEN;

#ifdef CLUSTER_THREAD_STEALING
  } else {
    if (!(immediate || allow_immediate))
      vc->action_ = cont;
    if ((result = vc->start(thread)) < 0) {
      return NULL;
    }
    if (immediate || allow_immediate) {
      return vc;
    } else {
      return CLUSTER_DELAYED_OPEN;
    }
  }
#endif
}

ClusterVConnection *
ClusterProcessor::connect_local(Continuation * cont, ClusterVCToken * token, int channel, int options)
{
  //
  // Establish VC connection initiated by remote node on the local node
  // using the given token and channel id.
  //
  bool immediate = ((options & CLUSTER_OPT_IMMEDIATE) ? true : false);
  bool allow_immediate = ((options & CLUSTER_OPT_ALLOW_IMMEDIATE) ? true : false);
  int result;

#ifdef LOCAL_CLUSTER_TEST_MODE
  int ip = inet_addr("127.0.0.1");
  ClusterMachine *m;
  m = this_cluster->current_configuration()->find(ip, token->ip_created);
#else
  ClusterMachine *m = this_cluster->current_configuration()->find(token->ip_created);
#endif
  if (!m)
    return NULL;
  ClusterHandler *ch = m->clusterHandler;
  if (!ch)
    return NULL;
  EThread *t = ch->thread;
  if (!t)
    return NULL;

  EThread *thread = this_ethread();
  ProxyMutex *mutex = thread->mutex;
  ClusterVConnection *vc = clusterVCAllocator.alloc();
  vc->new_connect_read = (options & CLUSTER_OPT_CONN_READ ? 1 : 0);
  vc->start_time = ink_get_hrtime();
  vc->last_activity_time = vc->start_time;
  vc->machine = m;
  vc->token = *token;
  vc->channel = channel;
#ifdef CLUSTER_THREAD_STEALING
  CLUSTER_INCREMENT_DYN_STAT(CLUSTER_CONNECTIONS_OPENNED_STAT);
  CLUSTER_INCREMENT_DYN_STAT(CLUSTER_CONNECTIONS_OPEN_STAT);
  MUTEX_TRY_LOCK(lock, ch->mutex, thread);
  if (!lock) {
#endif
    if (immediate) {
      clusterVCAllocator_free(vc);
      return NULL;
    }
    vc->mutex = ch->mutex;
    vc->action_ = cont;
    ch->thread->schedule_imm(vc);
    return CLUSTER_DELAYED_OPEN;
#ifdef CLUSTER_THREAD_STEALING
  } else {
    if (!(immediate || allow_immediate))
      vc->action_ = cont;
    if ((result = vc->start(thread)) < 0) {
      return NULL;
    }
    if (immediate || allow_immediate) {
      return vc;
    } else {
      return CLUSTER_DELAYED_OPEN;
    }
  }
#endif
}

bool ClusterProcessor::disable_remote_cluster_ops(ClusterMachine * m)
{
  if (!m)
    return false;

  ClusterHandler *
    ch = m->clusterHandler;
  if (ch) {
    return ch->disable_remote_cluster_ops;
  } else {
    return true;
  }
}

////////////////////////////////////////////////////////////////////////////
// Simplify debug access to stats
////////////////////////////////////////////////////////////////////////////
#if 0
#define GS RecRawStat// FIXME: GS conflicts with define in /usr/include/sys/regset.h
GS *p_CLUSTER_CONNECTIONS_OPEN_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_CONNECTIONS_OPEN_STAT);
GS *p_CLUSTER_CONNECTIONS_OPENNED_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_CONNECTIONS_OPENNED_STAT);
GS *p_CLUSTER_CON_TOTAL_TIME_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_CON_TOTAL_TIME_STAT);
GS *p_CLUSTER_CTRL_MSGS_SENT_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_CTRL_MSGS_SENT_STAT);
GS *p_CLUSTER_SLOW_CTRL_MSGS_SENT_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_SLOW_CTRL_MSGS_SENT_STAT);
GS *p_CLUSTER_CTRL_MSGS_RECVD_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_CTRL_MSGS_RECVD_STAT);
GS *p_CLUSTER_SLOW_CTRL_MSGS_RECVD_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_SLOW_CTRL_MSGS_RECVD_STAT);
GS *p_CLUSTER_CTRL_MSGS_SEND_TIME_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_CTRL_MSGS_SEND_TIME_STAT);
GS *p_CLUSTER_CTRL_MSGS_RECV_TIME_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_CTRL_MSGS_RECV_TIME_STAT);
GS *p_CLUSTER_READ_BYTES_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_READ_BYTES_STAT);
GS *p_CLUSTER_WRITE_BYTES_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_WRITE_BYTES_STAT);
GS *p_CLUSTER_OP_DELAYED_FOR_LOCK_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_OP_DELAYED_FOR_LOCK_STAT);
GS *p_CLUSTER_CONNECTIONS_LOCKED_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_CONNECTIONS_LOCKED_STAT);
GS *p_CLUSTER_CONNECTIONS_BUMPED_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_CONNECTIONS_BUMPED_STAT);
GS *p_CLUSTER_NODES_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_NODES_STAT);
GS *p_CLUSTER_NET_BACKUP_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_NET_BACKUP_STAT);
GS *p_machines_allocated_stat = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_MACHINES_ALLOCATED_STAT);
GS *p_machines_freed_stat = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_MACHINES_FREED_STAT);
GS *p_configuration_changes_stat = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_CONFIGURATION_CHANGES_STAT);
GS *p_delayed_reads_stat = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_DELAYED_READS_STAT);
GS *p_byte_bank_used_stat = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_BYTE_BANK_USED_STAT);
GS *p_alloc_data_news_stat = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_ALLOC_DATA_NEWS_STAT);
GS *p_write_bb_mallocs_stat = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_WRITE_BB_MALLOCS_STAT);
GS *p_partial_reads_stat = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_PARTIAL_READS_STAT);
GS *p_partial_writes_stat = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_PARTIAL_WRITES_STAT);
GS *p_cache_outstanding = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_CACHE_OUTSTANDING_STAT);
GS *p_remote_op_timeouts = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_REMOTE_OP_TIMEOUTS_STAT);
GS *p_remote_op_reply_timeouts = RecGetGlobalRawStatPtr(cluster_rsb,
                                                      CLUSTER_REMOTE_OP_REPLY_TIMEOUTS_STAT);
GS *p_chan_inuse = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_CHAN_INUSE_STAT);
GS *p_CLUSTER_OPEN_DELAYS_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_OPEN_DELAYS_STAT);
GS *p_CLUSTER_OPEN_DELAY_TIME_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_OPEN_DELAY_TIME_STAT);
GS *p_CLUSTER_CACHE_CALLBACKS_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_CACHE_CALLBACKS_STAT);
GS *p_CLUSTER_CACHE_CALLBACK_TIME_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_CACHE_CALLBACK_TIME_STAT);
GS *p_cluster_thread_steal_expires = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_THREAD_STEAL_EXPIRES_STAT);
GS *p_CLUSTER_RDMSG_ASSEMBLE_TIME_STAT = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_RDMSG_ASSEMBLE_TIME_STAT);
GS *p_cluster_ping_time = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_PING_TIME_STAT);
GS *p_cluster_setdata_no_clustervc = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_SETDATA_NO_CLUSTERVC_STAT);
GS *p_cluster_setdata_no_tunnel = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_SETDATA_NO_TUNNEL_STAT);
GS *p_cluster_setdata_no_cachevc = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_SETDATA_NO_CACHEVC_STAT);
GS *p_cluster_setdata_no_cluster = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_SETDATA_NO_CLUSTER_STAT);
GS *p_cluster_vc_write_stall = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_VC_WRITE_STALL_STAT);
GS *p_cluster_no_remote_space = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_NO_REMOTE_SPACE_STAT);
GS *p_cluster_level1_bank = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_LEVEL1_BANK_STAT);
GS *p_cluster_multilevel_bank = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_MULTILEVEL_BANK_STAT);
GS *p_cluster_vc_cache_insert_lock_misses = RecGetGlobalRawStatPtr(cluster_rsb,
                                                                 CLUSTER_VC_CACHE_INSERT_LOCK_MISSES_STAT);
GS *p_cluster_vc_cache_inserts = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_VC_CACHE_INSERTS_STAT);
GS *p_cluster_vc_cache_lookup_lock_misses = RecGetGlobalRawStatPtr(cluster_rsb,
                                                                 CLUSTER_VC_CACHE_LOOKUP_LOCK_MISSES_STAT);
GS *p_cluster_vc_cache_lookup_hits = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_VC_CACHE_LOOKUP_HITS_STAT);
GS *p_cluster_vc_cache_lookup_misses = RecGetGlobalRawStatPtr(cluster_rsb,
                                                            CLUSTER_VC_CACHE_LOOKUP_MISSES_STAT);
GS *p_cluster_vc_cache_scans = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_VC_CACHE_SCANS_STAT);
GS *p_cluster_vc_cache_scan_lock_misses = RecGetGlobalRawStatPtr(cluster_rsb,
                                                               CLUSTER_VC_CACHE_SCAN_LOCK_MISSES_STAT);
GS *p_cluster_vc_cache_purges = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_VC_CACHE_PURGES_STAT);
GS *p_cluster_write_lock_misses = RecGetGlobalRawStatPtr(cluster_rsb, CLUSTER_WRITE_LOCK_MISSES_STAT);
#endif
////////////////////////////////////////////////////////////////////////////

GlobalClusterPeriodicEvent *
  PeriodicClusterEvent;

#ifdef CLUSTER_TOMCAT
extern int cache_clustering_enabled;

int CacheClusterMonitorEnabled = 0;
int CacheClusterMonitorIntervalSecs = 1;

int cluster_send_buffer_size = 0;
int cluster_receive_buffer_size = 0;
unsigned long cluster_sockopt_flags = 0;

int RPC_only_CacheCluster = 0;
#endif

int
ClusterProcessor::init()
{
  cluster_rsb = RecAllocateRawStatBlock((int) cluster_stat_count);
  //
  // Statistics callbacks
  //
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.connections_open",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_CONNECTIONS_OPEN_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CONNECTIONS_OPEN_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.connections_opened",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_CONNECTIONS_OPENNED_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CONNECTIONS_OPENNED_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.connections_closed",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_CON_TOTAL_TIME_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CON_TOTAL_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.slow_ctrl_msgs_sent",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_SLOW_CTRL_MSGS_SENT_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_SLOW_CTRL_MSGS_SENT_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.connections_locked",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_CONNECTIONS_LOCKED_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CONNECTIONS_LOCKED_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.reads",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_READ_BYTES_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_READ_BYTES_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.read_bytes",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_READ_BYTES_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_READ_BYTES_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.writes",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_WRITE_BYTES_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_WRITE_BYTES_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.write_bytes",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_WRITE_BYTES_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_WRITE_BYTES_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.control_messages_sent",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_CTRL_MSGS_SEND_TIME_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CTRL_MSGS_SEND_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.control_messages_received",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_CTRL_MSGS_RECV_TIME_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CTRL_MSGS_RECV_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.op_delayed_for_lock",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_OP_DELAYED_FOR_LOCK_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_OP_DELAYED_FOR_LOCK_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.connections_bumped",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_CONNECTIONS_BUMPED_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CONNECTIONS_BUMPED_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.net_backup",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_NET_BACKUP_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_NET_BACKUP_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.nodes",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_NODES_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_NODES_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.machines_allocated",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_MACHINES_ALLOCATED_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_MACHINES_ALLOCATED_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.machines_freed",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_MACHINES_FREED_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_MACHINES_FREED_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.configuration_changes",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_CONFIGURATION_CHANGES_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CONFIGURATION_CHANGES_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.delayed_reads",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_DELAYED_READS_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_DELAYED_READS_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.byte_bank_used",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_BYTE_BANK_USED_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_BYTE_BANK_USED_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.alloc_data_news",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_ALLOC_DATA_NEWS_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_ALLOC_DATA_NEWS_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.write_bb_mallocs",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_WRITE_BB_MALLOCS_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_WRITE_BB_MALLOCS_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.partial_reads",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_PARTIAL_READS_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_PARTIAL_READS_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.partial_writes",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_PARTIAL_WRITES_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_PARTIAL_WRITES_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.cache_outstanding",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_CACHE_OUTSTANDING_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CACHE_OUTSTANDING_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.remote_op_timeouts",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_REMOTE_OP_TIMEOUTS_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_REMOTE_OP_TIMEOUTS_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.remote_op_reply_timeouts",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_REMOTE_OP_REPLY_TIMEOUTS_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_REMOTE_OP_REPLY_TIMEOUTS_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.chan_inuse",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_CHAN_INUSE_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CHAN_INUSE_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.open_delays",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_OPEN_DELAY_TIME_STAT, RecRawStatSyncSum);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_OPEN_DELAY_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.connections_avg_time",
                     RECD_FLOAT, RECP_NON_PERSISTENT, (int) CLUSTER_CON_TOTAL_TIME_STAT, RecRawStatSyncHrTimeAvg);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CON_TOTAL_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.control_messages_avg_send_time",
                     RECD_FLOAT, RECP_NON_PERSISTENT, (int) CLUSTER_CTRL_MSGS_SEND_TIME_STAT, RecRawStatSyncHrTimeAvg);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CTRL_MSGS_SEND_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.control_messages_avg_receive_time",
                     RECD_FLOAT, RECP_NON_PERSISTENT, (int) CLUSTER_CTRL_MSGS_RECV_TIME_STAT, RecRawStatSyncHrTimeAvg);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CTRL_MSGS_RECV_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.open_delay_time",
                     RECD_FLOAT, RECP_NON_PERSISTENT, (int) CLUSTER_OPEN_DELAY_TIME_STAT, RecRawStatSyncHrTimeAvg);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_OPEN_DELAY_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.cache_callback_time",
                     RECD_FLOAT, RECP_NON_PERSISTENT, (int) CLUSTER_CACHE_CALLBACK_TIME_STAT, RecRawStatSyncHrTimeAvg);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CACHE_CALLBACK_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.rmt_cache_callback_time",
                     RECD_FLOAT, RECP_NON_PERSISTENT,
                     (int) CLUSTER_CACHE_RMT_CALLBACK_TIME_STAT, RecRawStatSyncHrTimeAvg);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CACHE_RMT_CALLBACK_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.lkrmt_cache_callback_time",
                     RECD_FLOAT, RECP_NON_PERSISTENT,
                     (int) CLUSTER_CACHE_LKRMT_CALLBACK_TIME_STAT, RecRawStatSyncHrTimeAvg);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CACHE_LKRMT_CALLBACK_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.local_connection_time",
                     RECD_FLOAT, RECP_NON_PERSISTENT,
                     (int) CLUSTER_LOCAL_CONNECTION_TIME_STAT, RecRawStatSyncHrTimeAvg);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_LOCAL_CONNECTION_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.remote_connection_time",
                     RECD_FLOAT, RECP_NON_PERSISTENT,
                     (int) CLUSTER_REMOTE_CONNECTION_TIME_STAT, RecRawStatSyncHrTimeAvg);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_REMOTE_CONNECTION_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.rdmsg_assemble_time",
                     RECD_FLOAT, RECP_NON_PERSISTENT, (int) CLUSTER_RDMSG_ASSEMBLE_TIME_STAT, RecRawStatSyncHrTimeAvg);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_RDMSG_ASSEMBLE_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.cluster_ping_time",
                     RECD_FLOAT, RECP_NON_PERSISTENT, (int) CLUSTER_PING_TIME_STAT, RecRawStatSyncHrTimeAvg);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_PING_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.cache_callbacks",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_CACHE_CALLBACK_TIME_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CACHE_CALLBACK_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.rmt_cache_callbacks",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_CACHE_RMT_CALLBACK_TIME_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CACHE_RMT_CALLBACK_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.lkrmt_cache_callbacks",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_CACHE_LKRMT_CALLBACK_TIME_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_CACHE_LKRMT_CALLBACK_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.local_connections_closed",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_LOCAL_CONNECTION_TIME_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_LOCAL_CONNECTION_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.remote_connections_closed",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_REMOTE_CONNECTION_TIME_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_REMOTE_CONNECTION_TIME_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.setdata_no_clustervc",
                     RECD_INT, RECP_NON_PERSISTENT, (int) cluster_setdata_no_CLUSTERVC_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(cluster_setdata_no_CLUSTERVC_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.setdata_no_tunnel",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_SETDATA_NO_TUNNEL_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_SETDATA_NO_TUNNEL_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.setdata_no_cachevc",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_SETDATA_NO_CACHEVC_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_SETDATA_NO_CACHEVC_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.setdata_no_cluster",
                     RECD_INT, RECP_NON_PERSISTENT, (int) cluster_setdata_no_CLUSTER_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(cluster_setdata_no_CLUSTER_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.vc_write_stall",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_VC_WRITE_STALL_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_VC_WRITE_STALL_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.no_remote_space",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_NO_REMOTE_SPACE_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_NO_REMOTE_SPACE_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.level1_bank",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_LEVEL1_BANK_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_LEVEL1_BANK_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.multilevel_bank",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_MULTILEVEL_BANK_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_MULTILEVEL_BANK_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.vc_cache_insert_lock_misses",
                     RECD_INT, RECP_NON_PERSISTENT,
                     (int) CLUSTER_VC_CACHE_INSERT_LOCK_MISSES_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_VC_CACHE_INSERT_LOCK_MISSES_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.vc_cache_inserts",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_VC_CACHE_INSERTS_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_VC_CACHE_INSERTS_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.vc_cache_lookup_lock_misses",
                     RECD_INT, RECP_NON_PERSISTENT,
                     (int) CLUSTER_VC_CACHE_LOOKUP_LOCK_MISSES_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_VC_CACHE_LOOKUP_LOCK_MISSES_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.vc_cache_lookup_hits",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_VC_CACHE_LOOKUP_HITS_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_VC_CACHE_LOOKUP_HITS_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.vc_cache_lookup_misses",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_VC_CACHE_LOOKUP_MISSES_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_VC_CACHE_LOOKUP_MISSES_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.vc_cache_scans",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_VC_CACHE_SCANS_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_VC_CACHE_SCANS_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.vc_cache_scan_lock_misses",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_VC_CACHE_SCAN_LOCK_MISSES_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_VC_CACHE_SCAN_LOCK_MISSES_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.vc_cache_purges",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_VC_CACHE_PURGES_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_VC_CACHE_PURGES_STAT);
  RecRegisterRawStat(cluster_rsb, RECT_PROCESS,
                     "proxy.process.cluster.write_lock_misses",
                     RECD_INT, RECP_NON_PERSISTENT, (int) CLUSTER_WRITE_LOCK_MISSES_STAT, RecRawStatSyncCount);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_WRITE_LOCK_MISSES_STAT);
  CLUSTER_CLEAR_DYN_STAT(CLUSTER_NODES_STAT);   // clear sum and count
  // INKqa08033: win2k: ui: cluster warning light on
  // Used to call CLUSTER_INCREMENT_DYN_STAT here; switch to SUM_GLOBAL_DYN_STAT
  CLUSTER_SUM_GLOBAL_DYN_STAT(CLUSTER_NODES_STAT, 1);   // one node in cluster, ME

  // Setup interconnect load monitoring configuration options.
  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.cluster.load_monitor_enabled", 1, RECU_NULL, RECC_NULL, NULL);
  IOCORE_ReadConfigInteger(ClusterLoadMonitor::cf_monitor_enabled, "proxy.config.cluster.load_monitor_enabled");

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cluster.ping_send_interval_msecs", 100, RECU_NULL, RECC_NULL, NULL);
  IOCORE_ReadConfigInteger(ClusterLoadMonitor::cf_ping_message_send_msec_interval,
                           "proxy.config.cluster.ping_send_interval_msecs");

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cluster.ping_response_buckets", 100, RECU_NULL, RECC_NULL, NULL);
  IOCORE_ReadConfigInteger(ClusterLoadMonitor::cf_num_ping_response_buckets,
                           "proxy.config.cluster.ping_response_buckets");

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cluster.msecs_per_ping_response_buckets", 50, RECU_NULL, RECC_NULL, NULL);
  IOCORE_ReadConfigInteger(ClusterLoadMonitor::cf_msecs_per_ping_response_bucket,
                           "proxy.config.cluster.msecs_per_ping_response_bucket");

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cluster.ping_latency_threshold_msecs", 500, RECU_NULL, RECC_NULL, NULL);
  IOCORE_ReadConfigInteger(ClusterLoadMonitor::cf_ping_latency_threshold_msecs,
                           "proxy.config.cluster.ping_latency_threshold_msecs");

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cluster.load_compute_interval_msecs", 5000, RECU_NULL, RECC_NULL, NULL);
  IOCORE_ReadConfigInteger(ClusterLoadMonitor::cf_cluster_load_compute_msec_interval,
                           "proxy.config.cluster.load_compute_interval_msecs");

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cluster.periodic_timer_interval_msecs", 100, RECU_NULL, RECC_NULL, NULL);
  IOCORE_ReadConfigInteger(ClusterLoadMonitor::cf_cluster_periodic_msec_interval,
                           "proxy.config.cluster.periodic_timer_interval_msecs");

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cluster.ping_history_buf_length", 120, RECU_NULL, RECC_NULL, NULL);
  IOCORE_ReadConfigInteger(ClusterLoadMonitor::cf_ping_history_buf_length,
                           "proxy.config.cluster.ping_history_buf_length");

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cluster.cluster_load_clear_duration", 24, RECU_NULL, RECC_NULL, NULL);
  IOCORE_ReadConfigInteger(ClusterLoadMonitor::cf_cluster_load_clear_duration,
                           "proxy.config.cluster.cluster_load_clear_duration");

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cluster.cluster_load_exceed_duration", 4, RECU_NULL, RECC_NULL, NULL);
  IOCORE_ReadConfigInteger(ClusterLoadMonitor::cf_cluster_load_exceed_duration,
                           "proxy.config.cluster.cluster_load_exceed_duration");
  //
  // Configuration callbacks
  //
  if (cluster_port_number != DEFAULT_CLUSTER_PORT_NUMBER)
    cluster_port = cluster_port_number;
  else {
    IOCORE_RegisterConfigInteger(RECT_CONFIG,
                                 "proxy.config.cluster.cluster_port", -1, RECU_RESTART_TS, RECC_NULL, NULL);
    IOCORE_ReadConfigInteger(cluster_port, "proxy.config.cluster.cluster_port");
  }

  // Cluster monitor configuration
  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.cluster.enable_monitor", 0, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigInt32(CacheClusterMonitorEnabled, "proxy.config.cluster.enable_monitor");

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cluster.monitor_interval_secs", 1, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigInt32(CacheClusterMonitorIntervalSecs, "proxy.config.cluster.monitor_interval_secs");

  // Cluster internal parameters
  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cluster.receive_buffer_size", 10485760, RECU_NULL, RECC_NULL, NULL);
  IOCORE_ReadConfigInteger(cluster_receive_buffer_size, "proxy.config.cluster.receive_buffer_size");

  IOCORE_RegisterConfigInteger(RECT_CONFIG,
                               "proxy.config.cluster.send_buffer_size", 10485760, RECU_NULL, RECC_NULL, NULL);
  IOCORE_ReadConfigInteger(cluster_send_buffer_size, "proxy.config.cluster.send_buffer_size");

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.cluster.sock_option_flag", 0, RECU_NULL, RECC_NULL, NULL);
  IOCORE_ReadConfigInteger(cluster_sockopt_flags, "proxy.config.cluster.sock_option_flag");


  // RPC Cache Cluster option

  IOCORE_RegisterConfigInteger(RECT_CONFIG, "proxy.config.cluster.rpc_cache_cluster", 0, RECU_DYNAMIC, RECC_NULL, NULL);
  IOCORE_EstablishStaticConfigInt32(RPC_only_CacheCluster, "proxy.config.cluster.rpc_cache_cluster");

  int cluster_type = 0;
  // note : local variables work now using the same function. 
  // They are demarcated by their names
  IOCORE_RegisterConfigInteger(RECT_LOCAL, "proxy.local.cluster.type", 3, RECU_NULL, RECC_NULL, NULL);
  IOCORE_ReadConfigInteger(cluster_type, "proxy.local.cluster.type");

  create_this_cluster_machine();
#ifdef NON_MODULAR
  // Cluster API Initializations
  clusterAPI_init();
#endif
  // Start global Cluster periodic event
  PeriodicClusterEvent = NEW(new GlobalClusterPeriodicEvent);
  PeriodicClusterEvent->init();

  this_cluster = NEW(new Cluster);
  ClusterConfiguration *cc = NEW(new ClusterConfiguration);
  this_cluster->configurations.push(cc);
  cc->n_machines = 1;
  cc->machines[0] = this_cluster_machine();
  memset(cc->hash_table, 0, CLUSTER_HASH_TABLE_SIZE);
  // 0 dummy output data

  memset(channel_dummy_output, 0, sizeof(channel_dummy_output));

  if (cluster_type == 1) {
    cache_clustering_enabled = 1;
    Note("cache clustering enabled");
    compute_cluster_mode();
  } else {
    cache_clustering_enabled = 0;
    Note("cache clustering disabled");
  }
  return 0;
}

// function added to adhere to the name calling convention of init functions
int
init_clusterprocessor(void)
{
  return clusterProcessor.init();
}

int
ClusterProcessor::start()
{
#ifdef LOCAL_CLUSTER_TEST_MODE
  this_cluster_machine()->cluster_port = cluster_port;
#endif
  if (cache_clustering_enabled && (cacheProcessor.IsCacheEnabled() == CACHE_INITIALIZED)) {

    ET_CLUSTER = eventProcessor.spawn_event_threads(1);
    for (int i = 0; i < eventProcessor.n_threads_for_type[ET_CLUSTER]; i++) {
      initialize_thread_for_net(eventProcessor.eventthread[ET_CLUSTER][i], i);
    }

    IOCORE_RegisterConfigString(RECT_CONFIG,
                                "proxy.config.cluster.cluster_configuration",
                                "cluster.config", RECU_NULL, RECC_NULL, NULL);
    IOCORE_RegisterConfigUpdateFunc("proxy.config.cluster.cluster_configuration",
                                    machine_config_change, (void *) CLUSTER_CONFIG);
    do_machine_config_change((void *) CLUSTER_CONFIG, "proxy.config.cluster.cluster_configuration");
#ifdef USE_SEPARATE_MACHINE_CONFIG
    IOCORE_RegisterConfigUpdateFunc("proxy.config.cluster.machine_configuration",
                                    machine_config_change, (void *) MACHINE_CONFIG);
    do_machine_config_change((void *) MACHINE_CONFIG, "proxy.config.cluster.machine_configuration");
#endif

    accept_handler = NEW(new ClusterAccept(&cluster_port, cluster_receive_buffer_size, cluster_send_buffer_size));
    accept_handler->Init();
  }
  return 0;
}

void
ClusterProcessor::connect(char *hostname)
{
  //
  // Construct a cluster link to the given machine
  //
  ClusterHandler *ch = NEW(new ClusterHandler);
  SET_CONTINUATION_HANDLER(ch, (ClusterContHandler) & ClusterHandler::connectClusterEvent);
  ch->hostname = xstrdup(hostname);
  ch->connector = true;
  eventProcessor.schedule_imm(ch, ET_CLUSTER);
}

void
ClusterProcessor::connect(unsigned int ip, int port, bool delay)
{
  //
  // Construct a cluster link to the given machine
  //
  ClusterHandler *ch = NEW(new ClusterHandler);
  SET_CONTINUATION_HANDLER(ch, (ClusterContHandler) & ClusterHandler::connectClusterEvent);
  ch->ip = ip;
  ch->port = port;
  ch->connector = true;
  if (delay)
    eventProcessor.schedule_in(ch, CLUSTER_MEMBER_DELAY, ET_CLUSTER);
  else
    eventProcessor.schedule_imm(ch, ET_CLUSTER);
}

void
ClusterProcessor::send_machine_list(ClusterMachine * m)
{
  //
  // In testing mode, cluster nodes automagically connect to all
  // known hosts.  This function is called on connect to exchange those
  // lists.
  //
  MachineListMessage mlistmsg;
  int vers = MachineListMessage::protoToVersion(m->msg_proto_major);
  ClusterConfiguration *cc = this_cluster->current_configuration();
  void *data;
  int len;

  if (vers == MachineListMessage::MACHINE_LIST_MESSAGE_VERSION) {
    int n = 0;
    MachineListMessage *msg = &mlistmsg;

    while (n < cc->n_machines) {
      msg->ip[n] = cc->machines[n]->ip;
      n++;
    }
    msg->n_ip = n;
    data = (void *) msg;
    len = msg->sizeof_fixedlen_msg() + (n * sizeof(inku32));

  } else {
    //////////////////////////////////////////////////////////////
    // Create the specified down rev version of this message
    //////////////////////////////////////////////////////////////
    ink_release_assert(!"send_machine_list() bad msg version");
  }
  invoke_remote(m, MACHINE_LIST_CLUSTER_FUNCTION, data, len);
}

void
ClusterProcessor::compute_cluster_mode()
{
  if (RPC_only_CacheCluster) {
    if (cache_clustering_enabled > 0) {
      cache_clustering_enabled = -1;
      Note("RPC only cache clustering");
    }
  } else {
    if (cache_clustering_enabled < 0) {
      cache_clustering_enabled = 1;
      Note("RPC only cache clustering disabled");
    }
  }
}

// End of ClusterProcessor.cc
