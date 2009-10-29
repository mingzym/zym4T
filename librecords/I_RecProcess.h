/** @file

  Public RecProcess declarations

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

#ifndef _I_REC_PROCESS_H_
#define _I_REC_PROCESS_H_

#include "I_RecCore.h"
#include "I_EventSystem.h"

//-------------------------------------------------------------------------
// Initialization/Starting
//-------------------------------------------------------------------------

int RecProcessInit(RecModeT mode_type, Diags * diags = NULL);
int RecProcessInitMessage(RecModeT mode_type);
int RecProcessStart();

//-------------------------------------------------------------------------
// RawStat Registration
//-------------------------------------------------------------------------

RecRawStatBlock *RecAllocateRawStatBlock(int num_stats);

int RecRegisterRawStat(RecRawStatBlock * rsb,
                       RecT rec_type,
                       char *name, RecDataT data_type, RecPersistT persist_type, int id, RecRawStatSyncCb sync_cb);

// RecRawStatRange* RecAllocateRawStatRange (int num_buckets);

// int RecRegisterRawStatRange (RecRawStatRange *rsr,
//                           RecT rec_type,
//                           char *name,
//                           RecPersistT persist_type,
//                           int id,
//                           RecInt min,
//                           RecInt max);

//-------------------------------------------------------------------------
// Predefined RawStat Callbacks
//-------------------------------------------------------------------------

int RecRawStatSyncSum(const char *name, RecDataT data_type, RecData * data, RecRawStatBlock * rsb, int id);
int RecRawStatSyncCount(const char *name, RecDataT data_type, RecData * data, RecRawStatBlock * rsb, int id);
int RecRawStatSyncAvg(const char *name, RecDataT data_type, RecData * data, RecRawStatBlock * rsb, int id);
int RecRawStatSyncHrTimeAvg(const char *name, RecDataT data_type, RecData * data, RecRawStatBlock * rsb, int id);
int RecRawStatSyncIntMsecsToFloatSeconds(const char *name, RecDataT data_type,
                                         RecData * data, RecRawStatBlock * rsb, int id);
int RecRawStatSyncMHrTimeAvg(const char *name, RecDataT data_type, RecData * data, RecRawStatBlock * rsb, int id);

//-------------------------------------------------------------------------
// RawStat Setting/Getting
//-------------------------------------------------------------------------

// Note: The following RecIncrRawStatXXX calls are fast and don't
// require any ink_atomic_xxx64()'s to be executed.  Use these RawStat
// functions over other RawStat functions whenever possible.
inline int RecIncrRawStat(RecRawStatBlock * rsb, EThread * ethread, int id, ink64 incr = 1);
inline int RecIncrRawStatSum(RecRawStatBlock * rsb, EThread * ethread, int id, ink64 incr = 1);
inline int RecIncrRawStatCount(RecRawStatBlock * rsb, EThread * ethread, int id, ink64 incr = 1);
int RecIncrRawStatBlock(RecRawStatBlock * rsb, EThread * ethread, RecRawStat * stat_array);

int RecSetRawStatSum(RecRawStatBlock * rsb, int id, ink64 data);
int RecSetRawStatCount(RecRawStatBlock * rsb, int id, ink64 data);
int RecSetRawStatBlock(RecRawStatBlock * rsb, RecRawStat * stat_array);

int RecGetRawStatSum(RecRawStatBlock * rsb, int id, ink64 * data);
int RecGetRawStatCount(RecRawStatBlock * rsb, int id, ink64 * data);

//-------------------------------------------------------------------------
// Global RawStat Items (e.g. same as above, but no thread-local behavior)
//-------------------------------------------------------------------------

int RecIncrGlobalRawStat(RecRawStatBlock * rsb, int id, ink64 incr = 1);
int RecIncrGlobalRawStatSum(RecRawStatBlock * rsb, int id, ink64 incr = 1);
int RecIncrGlobalRawStatCount(RecRawStatBlock * rsb, int id, ink64 incr = 1);

int RecSetGlobalRawStatSum(RecRawStatBlock * rsb, int id, ink64 data);
int RecSetGlobalRawStatCount(RecRawStatBlock * rsb, int id, ink64 data);

int RecGetGlobalRawStatSum(RecRawStatBlock * rsb, int id, ink64 * data);
int RecGetGlobalRawStatCount(RecRawStatBlock * rsb, int id, ink64 * data);

RecRawStat *RecGetGlobalRawStatPtr(RecRawStatBlock * rsb, int id);
ink64 *RecGetGlobalRawStatSumPtr(RecRawStatBlock * rsb, int id);
ink64 *RecGetGlobalRawStatCountPtr(RecRawStatBlock * rsb, int id);

//-------------------------------------------------------------------------
// RecIncrRawStatXXX
//-------------------------------------------------------------------------
// inlined functions that are used very frequently.
// FIXME: move it to Inline.cc

inline RecRawStat *
raw_stat_get_tlp(RecRawStatBlock * rsb, int id, EThread * ethread)
{

  ink_debug_assert((id >= 0) && (id < rsb->max_stats));
  if (ethread == NULL) {
    ethread = this_ethread();
  }
  return (((RecRawStat *) ((char *) (ethread) + rsb->ethr_stat_offset)) + id);

}


inline int
RecIncrRawStat(RecRawStatBlock * rsb, EThread * ethread, int id, ink64 incr)
{
  RecRawStat *tlp = raw_stat_get_tlp(rsb, id, ethread);
  tlp->sum += incr;
  tlp->count += 1;
  return REC_ERR_OKAY;
}

/* This does not seem to work as intended ... */
inline int
RecDecrRawStat(RecRawStatBlock * rsb, EThread * ethread, int id, ink64 decr)
{
  RecRawStat *tlp = raw_stat_get_tlp(rsb, id, ethread);
  if (decr <= tlp->sum) {       // Assure that we stay positive
    tlp->sum -= decr;
    tlp->count += 1;
  }
  return REC_ERR_OKAY;
}

inline int
RecIncrRawStatSum(RecRawStatBlock * rsb, EThread * ethread, int id, ink64 incr)
{
  RecRawStat *tlp = raw_stat_get_tlp(rsb, id, ethread);
  tlp->sum += incr;
  return REC_ERR_OKAY;
}

inline int
RecIncrRawStatCount(RecRawStatBlock * rsb, EThread * ethread, int id, ink64 incr)
{
  RecRawStat *tlp = raw_stat_get_tlp(rsb, id, ethread);
  tlp->count += incr;
  return REC_ERR_OKAY;
}

#endif
