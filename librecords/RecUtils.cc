/** @file

  Record utils definitions

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

#include "P_RecUtils.h"
#include "P_RecCore.h"
#include "P_RecTree.h"

#include "ParseRules.h"
#include "ink_atomic.h"
#include "ink_snprintf.h"

// diags defined in RecCore.cc
extern Diags *g_diags;

//-------------------------------------------------------------------------
// RecAlloc
//-------------------------------------------------------------------------

RecRecord *
RecAlloc(RecT rec_type, char *name, RecDataT data_type)
{

  if (g_num_records >= REC_MAX_RECORDS) {
    return NULL;
  }
  int i = ink_atomic_increment(&g_num_records, 1);
  RecRecord *r = &(g_records[i]);
  // Note: record should already be memset to 0 from RecCoreInit()
  r->rec_type = rec_type;
  r->name = xstrdup(name);
  r->data_type = data_type;
  rec_mutex_init(&(r->lock), NULL);

  g_records_tree->rec_tree_insert(r->name);

  g_type_records[rec_type][g_type_num_records[rec_type]] = i;
  r->relative_order = g_type_num_records[rec_type];
  g_type_num_records[rec_type]++;

  return r;

}

//-------------------------------------------------------------------------
// RecDataClear
//-------------------------------------------------------------------------

void
RecDataClear(RecDataT data_type, RecData * data)
{

  if ((data_type == RECD_STRING) && (data->rec_string)) {
    xfree(data->rec_string);
  }
  memset(data, 0, sizeof(RecData));

}

//-------------------------------------------------------------------------
// RecDataSet
//-------------------------------------------------------------------------

bool
RecDataSet(RecDataT data_type, RecData * data_dst, RecData * data_src)
{

  bool rec_set = false;
  switch (data_type) {
  case RECD_STRING:
    if (data_src->rec_string == NULL) {
      if (data_dst->rec_string != NULL) {
        xfree(data_dst->rec_string);
        data_dst->rec_string = NULL;
        rec_set = true;
      }
    } else if (((data_dst->rec_string) && (strcmp(data_dst->rec_string, data_src->rec_string) != 0)) ||
               ((data_dst->rec_string == NULL) && (data_src->rec_string != NULL))) {
      if (data_dst->rec_string) {
        xfree(data_dst->rec_string);
      }
      data_dst->rec_string = xstrdup(data_src->rec_string);
      rec_set = true;
    }
    break;
  case RECD_INT:
    if (data_dst->rec_int != data_src->rec_int) {
      data_dst->rec_int = data_src->rec_int;
      rec_set = true;
    }
    break;
  case RECD_LLONG:
    if (data_dst->rec_llong != data_src->rec_llong) {
      data_dst->rec_llong = data_src->rec_llong;
      rec_set = true;
    }
    break;
  case RECD_FLOAT:
    if (data_dst->rec_float != data_src->rec_float) {
      data_dst->rec_float = data_src->rec_float;
      rec_set = true;
    }
    break;
  case RECD_COUNTER:
    if (data_dst->rec_counter != data_src->rec_counter) {
      data_dst->rec_counter = data_src->rec_counter;
      rec_set = true;
    }
    break;
  default:
    ink_assert(!"Wrong RECD type!");
  }
  return rec_set;

}

//-------------------------------------------------------------------------
// RecDataSetFromInk64
//-------------------------------------------------------------------------

bool
RecDataSetFromInk64(RecDataT data_type, RecData * data_dst, ink64 data_ink64)
{

  switch (data_type) {
  case RECD_INT:
    data_dst->rec_int = data_ink64;
    break;
  case RECD_LLONG:
    data_dst->rec_llong = data_ink64;
    break;
  case RECD_FLOAT:
    data_dst->rec_float = (float) (data_ink64);
    break;
  case RECD_STRING:
    {
      char buf[32 + 1];
      if (data_dst->rec_string) {
        xfree(data_dst->rec_string);
      }
      ink_snprintf(buf, 32, "%lld", data_ink64);
      data_dst->rec_string = xstrdup(buf);
      break;
    }
  case RECD_COUNTER:
    data_dst->rec_counter = data_ink64;
    break;
  default:
    ink_debug_assert(!"Unexpected RecD type");
    return false;
  }

  return true;

}

//-------------------------------------------------------------------------
// RecDataSetFromFloat
//-------------------------------------------------------------------------

bool
RecDataSetFromFloat(RecDataT data_type, RecData * data_dst, float data_float)
{

  switch (data_type) {
  case RECD_INT:
    data_dst->rec_int = (RecInt) data_float;
    break;
  case RECD_LLONG:
    data_dst->rec_llong = (RecLLong) data_float;
    break;
  case RECD_FLOAT:
    data_dst->rec_float = (float) (data_float);
    break;
  case RECD_STRING:
    {
      char buf[32 + 1];
      if (data_dst->rec_string) {
        xfree(data_dst->rec_string);
      }
      ink_snprintf(buf, 32, "%f", data_float);
      data_dst->rec_string = xstrdup(buf);
      break;
    }
  case RECD_COUNTER:
    data_dst->rec_counter = (RecCounter) data_float;
    break;
  default:
    ink_debug_assert(!"Unexpected RecD type");
    return false;
  }

  return true;

}

//-------------------------------------------------------------------------
// RecDataSetFromString
//-------------------------------------------------------------------------

bool
RecDataSetFromString(RecDataT data_type, RecData * data_dst, char *data_string)
{

  bool rec_set;
  RecData data_src;

  switch (data_type) {
  case RECD_INT:
    data_src.rec_int = ink_atoll(data_string);
    break;
  case RECD_LLONG:
    data_src.rec_llong = ink_atoll(data_string);
    break;
  case RECD_FLOAT:
    data_src.rec_float = atof(data_string);
    break;
  case RECD_STRING:
    if (strcmp((data_string), "NULL") == 0)
      data_src.rec_string = NULL;
    else
      data_src.rec_string = data_string;
    break;
  case RECD_COUNTER:
    data_src.rec_counter = ink_atoll(data_string);
    break;
  default:
    ink_debug_assert(!"Unexpected RecD type");
    return false;
  }
  rec_set = RecDataSet(data_type, data_dst, &data_src);

  return rec_set;

}

//-------------------------------------------------------------------------
// RecLog
//-------------------------------------------------------------------------

void
RecLog(DiagsLevel dl, const char *format_string, ...)
{
  va_list ap;
  va_start(ap, format_string);
  if (g_diags) {
    g_diags->log_va(NULL, dl, NULL, NULL, format_string, ap);
  }
  va_end(ap);
}

//-------------------------------------------------------------------------
// RecDebug
//-------------------------------------------------------------------------

void
RecDebug(DiagsLevel dl, const char *format_string, ...)
{
  va_list ap;
  va_start(ap, format_string);
  if (g_diags) {
    g_diags->log_va("rec", dl, NULL, NULL, format_string, ap);
  }
  va_end(ap);
}

//-------------------------------------------------------------------------
// RecDebugOff
//-------------------------------------------------------------------------

void
RecDebugOff()
{
  g_diags = NULL;
}
