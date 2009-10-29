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

#include "ink_unused.h"        /* MAGIC_EDITING_TAG */
/***************************************/

#include "Compatability.h"
#include "ink_bool.h"
#include "Main.h"
#include "WebMgmtUtils.h"
#include "BaseRecords.h"
#include "Tokenizer.h"
#include "RecordsConfig.h"
#include "ink_regex-3.6.h"

/****************************************************************************
 *
 *  WebMgmtUtils.cc - Functions for interfacing to management records
 *
 *  
 * 
 ****************************************************************************/


// bool varSetFromStr(const char*, const char* )
//
// Sets the named local manager variable from the value string
// passed in.  Does the appropriate type conversion on
// value string to get it to the type of the local manager
// variable
//
//  returns true if the variable was successfully set 
//   and false otherwise 
//
bool
varSetFromStr(const char *varName, const char *value)
{
  RecDataT varDataType = RECD_NULL;
  bool found = true;
  int err = REC_ERR_FAIL;
  RecData data;

  memset(&data, 0, sizeof(RecData));

  err = RecGetRecordDataType((char *) varName, &varDataType);
  if (err != REC_ERR_OKAY) {
    return found;
  }
  // Use any empty string if we get a NULL so
  //  sprintf does puke.  However, we need to
  //  switch this back to NULL for STRING types
  if (value == NULL) {
    value = "";
  }

  switch (varDataType) {
  case RECD_INT:
    if (ink_sscan_longlong(value, &data.rec_int) == 1) {
      RecSetRecordInt((char *) varName, data.rec_int);
    } else {
      found = false;
    }
    break;
  case RECD_LLONG:
    if (ink_sscan_longlong(value, &data.rec_llong) == 1) {
      RecSetRecordLLong((char *) varName, data.rec_llong);
    } else {
      found = false;
    }
    break;
  case RECD_COUNTER:
    if (ink_sscan_longlong(value, &data.rec_counter) == 1) {
      RecSetRecordCounter((char *) varName, data.rec_counter);
    } else {
      found = false;
    }
    break;
  case RECD_FLOAT:
    // coverity[secure_coding]
    if (sscanf(value, "%f", &data.rec_float) == 1) {
      RecSetRecordFloat((char *) varName, data.rec_float);
    } else {
      found = false;
    }
    break;
  case RECD_STRING:
    if (*value == '\0') {
      RecSetRecordString((char *) varName, NULL);
    } else {
      RecSetRecordString((char *) varName, (char *) value);
    }
    break;
  case RECD_NULL:
  default:
    found = false;
    break;
  }

  return found;
}

// bool varSetFloat(const char* varName, RecFloat value) 
//
//  Sets the variable specifed by varName to value.  varName
//   must be a RecFloat variable.  No conversion is done for
//   other types unless convert is set to ture. In the case
//   of convert is ture, type conversion is perform if applicable.
//   By default, convert is set to be false and can be overrided
//   when the function is called.
//
bool
varSetFloat(const char *varName, RecFloat value, bool convert)
{
  RecDataT varDataType = RECD_NULL;
  bool found = true;
  int err = REC_ERR_FAIL;

  err = RecGetRecordDataType((char *) varName, &varDataType);
  if (err != REC_ERR_OKAY) {
    return found;
  }

  switch (varDataType) {
  case RECD_FLOAT:
    RecSetRecordFloat((char *) varName, (RecFloat) value);
    break;
  case RECD_INT:
    if (convert) {
      value += 0.5;             // rounding up
      RecSetRecordInt((char *) varName, (RecInt) value);
      break;
    }
  case RECD_LLONG:
    if (convert) {
      value += 0.5;             // rounding up
      RecSetRecordLLong((char *) varName, (RecLLong) value);
      break;
    }
  case RECD_COUNTER:
    if (convert) {
      RecSetRecordCounter((char *) varName, (RecCounter) value);
      break;
    }
  case RECD_STRING:
  case RECD_NULL:
  default:
    found = false;
    break;
  }

  return found;
}

// bool varSetCounter(const char* varName, RecCounter value) 
//
//  Sets the variable specifed by varName to value.  varName
//   must be an RecCounter variable.  No conversion is done for
//   other types unless convert is set to ture. In the case
//   of convert is ture, type conversion is perform if applicable.
//   By default, convert is set to be false and can be overrided
//   when the function is called.
//
bool
varSetCounter(const char *varName, RecCounter value, bool convert)
{
  RecDataT varDataType = RECD_NULL;
  bool found = true;
  int err = REC_ERR_FAIL;

  err = RecGetRecordDataType((char *) varName, &varDataType);
  if (err != REC_ERR_OKAY) {
    return found;
  }

  switch (varDataType) {
  case RECD_COUNTER:
    RecSetRecordCounter((char *) varName, (RecCounter) value);
    break;
  case RECD_INT:
    if (convert) {
      RecSetRecordInt((char *) varName, (RecInt) value);
      break;
    }
  case RECD_LLONG:
    if (convert) {
      RecSetRecordLLong((char *) varName, (RecLLong) value);
      break;
    }
  case RECD_FLOAT:
    if (convert) {
      RecSetRecordFloat((char *) varName, (RecFloat) value);
      break;
    }
  case RECD_STRING:
  case RECD_NULL:
  default:
    found = false;
    break;
  }

  return found;
}

// bool varSetInt(const char* varName, RecInt value) 
//
//  Sets the variable specifed by varName to value.  varName
//   must be an RecInt variable.  No conversion is done for
//   other types unless convert is set to ture. In the case
//   of convert is ture, type conversion is perform if applicable.
//   By default, convert is set to be false and can be overrided
//   when the function is called.
//
bool
varSetInt(const char *varName, RecInt value, bool convert)
{
  RecDataT varDataType = RECD_NULL;
  bool found = true;
  int err = REC_ERR_FAIL;

  err = RecGetRecordDataType((char *) varName, &varDataType);
  if (err != REC_ERR_OKAY) {
    return found;
  }

  switch (varDataType) {
  case RECD_INT:
    RecSetRecordInt((char *) varName, (RecInt) value);
    break;
  case RECD_LLONG:
    if (convert) {
      RecSetRecordLLong((char *) varName, (RecLLong) value);
      break;
    }
  case RECD_COUNTER:
    if (convert) {
      RecSetRecordCounter((char *) varName, (RecCounter) value);
      break;
    }
  case RECD_FLOAT:
    if (convert) {
      RecSetRecordFloat((char *) varName, (RecFloat) value);
      break;
    }
  case RECD_STRING:
  case RECD_NULL:
  default:
    found = false;
    break;
  }

  return found;
}

// bool varSetLLong(const char* varName, RecLLong value) 
//
//  Sets the variable specifed by varName to value.  varName
//   must be an RecLLong variable.  No conversion is done for
//   other types unless convert is set to ture. In the case
//   of convert is ture, type conversion is perform if applicable.
//   By default, convert is set to be false and can be overrided
//   when the function is called.
//
bool
varSetLLong(const char *varName, RecLLong value, bool convert)
{
  RecDataT varDataType = RECD_NULL;
  bool found = true;
  int err = REC_ERR_FAIL;

  err = RecGetRecordDataType((char *) varName, &varDataType);
  if (err != REC_ERR_OKAY) {
    return found;
  }

  switch (varDataType) {
  case RECD_LLONG:
    {
      RecSetRecordLLong((char *) varName, (RecLLong) value);
      break;
    }
  case RECD_INT:
    if (convert) {
      RecSetRecordInt((char *) varName, (RecInt) value);
    }
    break;
  case RECD_COUNTER:
    if (convert) {
      RecSetRecordCounter((char *) varName, (RecCounter) value);
      break;
    }
  case RECD_FLOAT:
    if (convert) {
      RecSetRecordFloat((char *) varName, (RecFloat) value);
      break;
    }
  case RECD_STRING:
  case RECD_NULL:
  default:
    found = false;
    break;
  }

  return found;
}

// bool varCounterFromName (const char*, RecFloat* )
//
//   Sets the *value to value of the varName.
// 
//  return true if bufVal was succefully set
//    and false otherwise 
//
bool
varCounterFromName(const char *varName, RecCounter * value)
{
  RecDataT varDataType = RECD_NULL;
  bool found = true;
  int err = REC_ERR_FAIL;

  err = RecGetRecordDataType((char *) varName, &varDataType);

  if (err == REC_ERR_FAIL) {
    return false;
  }

  switch (varDataType) {
  case RECD_INT:{
      RecInt tempInt = 0;
      RecGetRecordInt((char *) varName, &tempInt);
      *value = (RecCounter) tempInt;
      break;
    }
  case RECD_LLONG:{
      RecLLong tempLLong = 0;
      RecGetRecordLLong((char *) varName, &tempLLong);
      *value = (RecCounter) tempLLong;
      break;
    }
  case RECD_COUNTER:{
      *value = 0;
      RecGetRecordCounter((char *) varName, value);
      break;
    }
  case RECD_FLOAT:{
      RecFloat tempFloat = 0.0;
      RecGetRecordFloat((char *) varName, &tempFloat);
      *value = (RecCounter) tempFloat;
      break;
    }
  case RECD_STRING:
  case RECD_NULL:
  default:
    *value = -1;
    found = false;
    break;
  }

  return found;
}

// bool varFloatFromName (const char*, RecFloat* )
//
//   Sets the *value to value of the varName.
// 
//  return true if bufVal was succefully set
//    and false otherwise 
//
bool
varFloatFromName(const char *varName, RecFloat * value)
{
  RecDataT varDataType = RECD_NULL;
  bool found = true;
  int err = REC_ERR_FAIL;

  err = RecGetRecordDataType((char *) varName, &varDataType);

  switch (varDataType) {
  case RECD_INT:{
      RecInt tempInt = 0;
      RecGetRecordInt((char *) varName, &tempInt);
      *value = (RecFloat) tempInt;
      break;
    }
  case RECD_LLONG:{
      RecLLong tempLLong = 0;
      RecGetRecordLLong((char *) varName, &tempLLong);
      *value = (RecFloat) tempLLong;
      break;
    }
  case RECD_COUNTER:{
      RecCounter tempCounter = 0;
      RecGetRecordCounter((char *) varName, &tempCounter);
      *value = (RecCounter) tempCounter;
      break;
    }
  case RECD_FLOAT:{
      *value = 0.0;
      RecGetRecordFloat((char *) varName, value);
      break;
    }
  case RECD_STRING:
  case RECD_NULL:
  default:
    *value = -1.0;
    found = false;
    break;
  }

  return found;
}

// bool varIntFromName (const char*, RecInt* )
//
//   Sets the *value to value of the varName.
// 
//  return true if bufVal was succefully set
//    and false otherwise 
//
bool
varIntFromName(const char *varName, RecInt * value)
{
  RecDataT varDataType = RECD_NULL;
  bool found = true;
  int err = REC_ERR_FAIL;

  err = RecGetRecordDataType((char *) varName, &varDataType);

  if (err != REC_ERR_OKAY) {
    return false;
  }

  switch (varDataType) {
  case RECD_INT:{
      *value = 0;
      RecGetRecordInt((char *) varName, value);
      break;
    }
  case RECD_LLONG:{
      RecLLong tempLLong = 0;
      RecGetRecordLLong((char *) varName, &tempLLong);
      *value = (RecInt) tempLLong;
      break;
    }
  case RECD_COUNTER:{
      RecCounter tempCounter = 0;
      RecGetRecordCounter((char *) varName, &tempCounter);
      *value = (RecInt) tempCounter;
      break;
    }
  case RECD_FLOAT:{
      RecFloat tempFloat = 0.0;
      RecGetRecordFloat((char *) varName, &tempFloat);
      *value = (RecInt) tempFloat;
      break;
    }
  case RECD_STRING:
  case RECD_NULL:
  default:
    *value = -1;
    found = false;
    break;
  }

  return found;
}

// bool varLLongFromName (const char*, RecLLong* )
//
//   Sets the *value to value of the varName.
// 
//  return true if bufVal was succefully set
//    and false otherwise 
//
bool
varLLongFromName(const char *varName, RecLLong * value)
{
  RecDataT varDataType = RECD_NULL;
  bool found = true;
  int err = REC_ERR_FAIL;

  err = RecGetRecordDataType((char *) varName, &varDataType);

  if (err != REC_ERR_OKAY) {
    return false;
  }

  switch (varDataType) {
  case RECD_LLONG:{
      *value = 0;
      RecGetRecordLLong((char *) varName, value);
      break;
    }
  case RECD_INT:{
      RecInt tempInt = 0;
      RecGetRecordInt((char *) varName, &tempInt);
      *value = (RecLLong) tempInt;
      break;
    }
  case RECD_COUNTER:{
      RecCounter tempCounter = 0;
      RecGetRecordCounter((char *) varName, &tempCounter);
      *value = (RecLLong) tempCounter;
      break;
    }
  case RECD_FLOAT:{
      RecFloat tempFloat = 0.0;
      RecGetRecordFloat((char *) varName, &tempFloat);
      *value = (RecLLong) tempFloat;
      break;
    }
  case RECD_STRING:
  case RECD_NULL:
  default:
    *value = -1;
    found = false;
    break;
  }

  return found;
}

// void percentStrFromFloat(MgmtFloat, char* bufVal)
//
//  Converts a float to a percent string
//
//     bufVal must point to adequate space a la sprintf
//
void
percentStrFromFloat(RecFloat val, char *bufVal)
{
  int percent;

  percent = (int) ((val * 100.0) + 0.5);
  snprintf(bufVal, 4, "%d%%", percent);
}

// void commaStrFromInt(RecInt bytes, char* bufVal)
//   Converts an Int to string with commas in it
//
//     bufVal must point to adequate space a la sprintf
//
void
commaStrFromInt(RecInt bytes, char *bufVal)
{
  int len;
  int numCommas;
  char *curPtr;

  ink_sprintf(bufVal, "%lld", bytes);
  len = strlen(bufVal);

  // The string is too short to need commas
  if (len < 4) {
    return;
  }

  numCommas = (len - 1) / 3;
  curPtr = bufVal + (len + numCommas);
  *curPtr = '\0';
  curPtr--;

  for (int i = 0; i < len; i++) {
    *curPtr = bufVal[len - 1 - i];

    if ((i + 1) % 3 == 0 && curPtr != bufVal) {
      curPtr--;
      *curPtr = ',';
    }
    curPtr--;
  }

  ink_assert(curPtr + 1 == bufVal);
}

// void commaStrFromLLong(RecLLong bytes, char* bufVal)
//   Converts an LLong to string with commas in it
//
//     bufVal must point to adequate space a la sprintf
//
void
commaStrFromLLong(RecLLong bytes, char *bufVal)
{
  int len;
  int numCommas;
  char *curPtr;

  ink_sprintf(bufVal, "%lld", bytes);
  len = strlen(bufVal);

  // The string is too short to need commas
  if (len < 4) {
    return;
  }

  numCommas = (len - 1) / 3;
  curPtr = bufVal + (len + numCommas);
  *curPtr = '\0';
  curPtr--;

  for (int i = 0; i < len; i++) {
    *curPtr = bufVal[len - 1 - i];

    if ((i + 1) % 3 == 0 && curPtr != bufVal) {
      curPtr--;
      *curPtr = ',';
    }
    curPtr--;
  }

  ink_assert(curPtr + 1 == bufVal);
}

// void MbytesFromInt(RecInt bytes, char* bufVal) 
//     Converts into a string in units of megabytes
//      No unit specification is added
//
//     bufVal must point to adequate space a la sprintf
//
void
MbytesFromInt(RecInt bytes, char *bufVal)
{
  RecInt mBytes = bytes / 1048576;

  ink_sprintf(bufVal, "%lld", mBytes);
}

// void MbytesFromLLong(RecLLong bytes, char* bufVal) 
//     Converts into a string in units of megabytes
//      No unit specification is added
//
//     bufVal must point to adequate space a la sprintf
//
void
MbytesFromLLong(RecLLong bytes, char *bufVal)
{
  RecLLong mBytes = bytes / 1048576;

  ink_sprintf(bufVal, "%lld", mBytes);
}

// void bytesFromInt(RecInt bytes, char* bufVal) 
//
//    Converts mgmt into a string with one of
//       GB, MB, KB, B units
//
//     bufVal must point to adequate space a la sprintf
void
bytesFromInt(RecInt bytes, char *bufVal)
{
  const ink64 gb = 1073741824;
  const long int mb = 1048576;
  const long int kb = 1024;
  int bytesP;
  double unitBytes;

  if (bytes >= gb) {
    unitBytes = bytes / (double) gb;
    snprintf(bufVal, 15, "%.1f GB", unitBytes);
  } else {
    // Reduce the precision of the bytes parameter
    //   because we know that it less than 1GB which
    //   has plenty of precision for a regular int
    //   and saves from 64 bit arithmetic which may
    //   be expensive on some processors
    bytesP = (int) bytes;
    if (bytesP >= mb) {
      unitBytes = bytes / (double) mb;
      snprintf(bufVal, 15, "%.1f MB", unitBytes);
    } else if (bytesP >= kb) {
      unitBytes = bytes / (double) kb;
      snprintf(bufVal, 15, "%.1f KB", unitBytes);
    } else {
      snprintf(bufVal, 15, "%d", bytesP);
    }
  }
}

// void bytesFromLLong(RecLLong bytes, char* bufVal) 
//
//    Converts mgmt into a string with one of
//       GB, MB, KB, B units
//
//     bufVal must point to adequate space a la sprintf
void
bytesFromLLong(RecLLong bytes, char *bufVal)
{
  const ink64 gb = 1073741824;
  const long int mb = 1048576;
  const long int kb = 1024;
  double unitBytes;

  if (bytes >= gb) {
    unitBytes = bytes / (double) gb;
    snprintf(bufVal, 15, "%.1f GB", unitBytes);
  } else {
    if (bytes >= mb) {
      unitBytes = bytes / (double) mb;
      snprintf(bufVal, 15, "%.1f MB", unitBytes);
    } else if (bytes >= kb) {
      unitBytes = bytes / (double) kb;
      snprintf(bufVal, 15, "%.1f KB", unitBytes);
    } else {
      snprintf(bufVal, 15, "%lld", bytes);
    }
  }
}

// bool varStrFromName (const char*, char*, int)
//
//   Sets the bufVal string to the value of the local manager
//     named by varName.  bufLen is size of bufVal
// 
//  return true if bufVal was succefully set
//    and false otherwise 
//
//  EVIL ALERT: overviewRecord::varStrFromName is extremely
//    similar to this function except in how it gets it's
//    data.  Changes to this fuction must be propogated
//    to its twin.  Cut and Paste sucks but there is not
//    an easy way to merge the functions
//
bool
varStrFromName(const char *varNameConst, char *bufVal, int bufLen)
{
  char *varName = NULL;
  RecDataT varDataType = RECD_NULL;
  bool found = true;
  int varNameLen = 0;
  char formatOption = '\0';
  RecData data;
  int err = REC_ERR_FAIL;

  memset(&data, 0, sizeof(RecData));

  // Check to see if there is a \ option on the end of variable
  //   \ options indicate that we need special formatting
  //   of the results.  Supported \ options are
  //
  ///  b - bytes.  Ints and Counts only.  Amounts are
  //       transformed into one of GB, MB, KB, or B
  //
  varName = xstrdup(varNameConst);
  varNameLen = strlen(varName);
  if (varNameLen > 3 && varName[varNameLen - 2] == '\\') {
    formatOption = varName[varNameLen - 1];

    // Now that we know the format option, terminate the string
    //   to make the option disappear
    varName[varNameLen - 2] = '\0';

    // Return not found for unknown format options
    if (formatOption != 'b' && formatOption != 'm' && formatOption != 'c' && formatOption != 'p') {
      xfree(varName);
      return false;
    }
  }

  err = RecGetRecordDataType(varName, &varDataType);
  if (err == REC_ERR_FAIL) {
    xfree(varName);
    return false;
  }

  switch (varDataType) {
  case RECD_INT:
    RecGetRecordInt(varName, &data.rec_int);
    if (formatOption == 'b') {
      bytesFromInt(data.rec_int, bufVal);
    } else if (formatOption == 'm') {
      MbytesFromInt(data.rec_int, bufVal);
    } else if (formatOption == 'c') {
      commaStrFromInt(data.rec_int, bufVal);
    } else {
      ink_sprintf(bufVal, "%lld", data.rec_int);
    }
    break;

  case RECD_LLONG:
    RecGetRecordLLong(varName, &data.rec_llong);
    if (formatOption == 'b') {
      bytesFromLLong(data.rec_llong, bufVal);
    } else if (formatOption == 'm') {
      MbytesFromLLong(data.rec_llong, bufVal);
    } else if (formatOption == 'c') {
      commaStrFromLLong(data.rec_llong, bufVal);
    } else {
      ink_sprintf(bufVal, "%lld", data.rec_llong);
    }
    break;

  case RECD_COUNTER:
    RecGetRecordCounter(varName, &data.rec_counter);
    if (formatOption == 'b') {
      bytesFromInt((MgmtInt) data.rec_counter, bufVal);
    } else if (formatOption == 'm') {
      MbytesFromInt((MgmtInt) data.rec_counter, bufVal);
    } else if (formatOption == 'c') {
      commaStrFromInt(data.rec_counter, bufVal);
    } else {
      ink_sprintf(bufVal, "%lld", data.rec_counter);
    }
    break;
  case RECD_FLOAT:
    RecGetRecordFloat(varName, &data.rec_float);
    if (formatOption == 'p') {
      percentStrFromFloat(data.rec_float, bufVal);
    } else {
      snprintf(bufVal, bufLen, "%.2f", data.rec_float);
    }
    break;
  case RECD_STRING:
    RecGetRecordString_Xmalloc(varName, &data.rec_string);
    if (data.rec_string == NULL) {
      bufVal[0] = '\0';
    } else if (strlen(data.rec_string) < (size_t) (bufLen - 1)) {
      ink_strncpy(bufVal, data.rec_string, bufLen);
    } else {
      ink_strncpy(bufVal, data.rec_string, bufLen);
    }
    xfree(data.rec_string);
    break;
  default:
    found = false;
    break;
  }

  xfree(varName);
  return found;
}

// bool MgmtData::setFromName(const char*)
//
//    Fills in class variables from the given
//      variable name
//
//    Returns true if the information could be set
//     and false otherwise
//
bool
MgmtData::setFromName(const char *varName)
{
  bool found = true;
  int err;

  err = RecGetRecordDataType((char *) varName, &this->type);

  if (err == REC_ERR_FAIL) {
    return found;
  }

  switch (this->type) {
  case RECD_INT:
    RecGetRecordInt((char *) varName, &this->data.rec_int);
    break;
  case RECD_LLONG:
    RecGetRecordLLong((char *) varName, &this->data.rec_llong);
    break;
  case RECD_COUNTER:
    RecGetRecordCounter((char *) varName, &this->data.rec_counter);
    break;
  case RECD_FLOAT:
    RecGetRecordFloat((char *) varName, &this->data.rec_float);
    break;
  case RECD_STRING:
    RecGetRecordString_Xmalloc((char *) varName, &this->data.rec_string);
    break;
  case RECD_NULL:
  default:
    found = false;
    break;
  }

  return found;
}

MgmtData::MgmtData()
{
  type = RECD_NULL;
  memset(&data, 0, sizeof(RecData));
}

MgmtData::~MgmtData()
{
  if (type == RECD_STRING) {
    xfree(data.rec_string);
  }
}

// MgmtData::compareFromString(const char* str, strLen)
//
//  Compares the value of string converted to
//    data type of this_>type with value
//    held in this->data
//
bool
MgmtData::compareFromString(const char *str)
{
  RecData compData;
  bool compare = false;
  float floatDiff;

  switch (this->type) {
  case RECD_INT:
    if (str && recordRegexCheck("^[0-9]+$", str)) {
      compData.rec_int = ink_atoll(str);
      if (data.rec_int == compData.rec_int) {
        compare = true;
      }
    }
    break;
  case RECD_LLONG:
    if (str && recordRegexCheck("^[0-9]+$", str)) {
      compData.rec_llong = ink_atoll(str);
      if (data.rec_llong == compData.rec_llong) {
        compare = true;
      }
    }
    break;
  case RECD_COUNTER:
    if (str && recordRegexCheck("^[0-9]+$", str)) {
      compData.rec_counter = ink_atoll(str);
      if (data.rec_counter == compData.rec_counter) {
        compare = true;
      }
    }
    break;
  case RECD_FLOAT:
    compData.rec_float = atof(str);
    // HACK - There are some rounding problems with 
    //   floating point numbers so say we have a match if there difference
    //   is small 
    floatDiff = data.rec_float - compData.rec_float;
    if (floatDiff > -0.001 && floatDiff < 0.001) {
      compare = true;
    }
    break;
  case RECD_STRING:
    if (str == NULL || *str == '\0') {
      if (data.rec_string == NULL) {
        compare = true;
      }
    } else {
      if ((data.rec_string != NULL) && (strcmp(str, data.rec_string) == 0)) {
        compare = true;
      }
    }
    break;
  case RECD_NULL:
  default:
    compare = false;
    break;
  }

  return compare;
}

// void RecDataT varType(const char* varName)
//
//   Simply return the variable type
//
RecDataT
varType(const char *varName)
{
  RecDataT data_type;
  int err;

  err = RecGetRecordDataType((char *) varName, &data_type);

  if (err == REC_ERR_FAIL) {
    return RECD_NULL;
  }

  Debug("RecOp", "[varType] %s is of type %d\n", varName, data_type);
  return data_type;
}

// void computeXactMax()
//
//  Compute the maximum numder of transactions
//   for the machine based on the number of processors
//   and the load_factor
//
//   Store the result into a node var so that
//    it gets passed around the cluster
//
void
computeXactMax()
{
  int numCPU = 0;
  RecFloat loadFactor = 0;
  RecInt maxXact = 0;
  int err = REC_ERR_FAIL;

#if (HOST_OS == hpux)
  numCPU = pthread_num_processors_np();
#elif (HOST_OS == freebsd)
  numCPU = 1;
#else
  numCPU = sysconf(_SC_NPROCESSORS_ONLN);
#endif
  err = RecGetRecordFloat("proxy.config.admin.load_factor", &loadFactor);

  if (err == REC_ERR_OKAY || loadFactor < 20.0) {
    mgmt_log(stderr, "[computeXactMax] Invalid Or Missing proxy.config.admin.load_factor.  Using default\n");
    loadFactor = 43.0;
  }
  // The formula is: the first cpu counts as 1 and each subsequent CPU counts
  //    as .5
  if (numCPU > 1) {
    numCPU--;
    maxXact = (MgmtInt) (loadFactor * (1.0 + (numCPU * .5)));
  } else {
    maxXact = (MgmtInt) (loadFactor * 1);
  }

  ink_assert(varSetInt("proxy.node.xact_scale", maxXact));
}


// InkHashTable* processFormSubmission(char* submission) 
//
//  A generic way to handle a HTML form submission.
//  Creates a hash table with name value pairs
//
//  CALLEE must deallocate the returned hash table with
//   ink_hash_table_destroy_and_xfree_values(InkHashTable *ht_ptr)
//
#ifdef OEM

// OEM version supports select multiple 
// once this is stablized, will merge this code to GA
InkHashTable *
processFormSubmission(char *submission)
{

  InkHashTable *nameVal = ink_hash_table_create(InkHashTableKeyType_String);
  Tokenizer updates("&\n\r");
  Tokenizer pair("=");
  int numUpdates;
  char *name;
  char *value;
  char *submission_copy;
  int pairNum;
  char *old_value;
  char buffer[2048];

  if (submission == NULL) {
    ink_hash_table_destroy(nameVal);
    return NULL;
  }

  submission_copy = xstrdup(submission);
  numUpdates = updates.Initialize(submission_copy, SHARE_TOKS);

  for (int i = 0; i < numUpdates; i++) {
    pairNum = pair.Initialize(updates[i]);

    // We should have gotten either either 1 or 2 tokens
    //    One token indicates an variable being set to
    //    blank.  Two indicates the variable being set to
    //    a value.  If the submission is invalid, just forget
    //    about it.
    if (pairNum == 1 || pairNum == 2) {
      name = xstrdup(pair[0]);
      substituteUnsafeChars(name);

      // If the value is blank, store it as a null
      //   since BaseRecords represents empty as NULL
      //   as opposed to the empty string
      if (pairNum == 1) {
        value = NULL;
      } else {
        value = xstrdup(pair[1]);
        if (ink_hash_table_lookup(nameVal, (char *) name, (void **) &old_value)) {
          if (old_value) {
            strcpy(buffer, old_value);
            strcat(buffer, "&");
            value = xstrdup(strcat(buffer, value));
            ink_hash_table_delete(nameVal, name);
            xfree(old_value);
          }
        }
        substituteUnsafeChars(value);
      }

      ink_hash_table_insert(nameVal, name, value);
      xfree(name);
    }
  }
  xfree(submission_copy);

  return nameVal;
}

#else

InkHashTable *
processFormSubmission(char *submission)
{

  InkHashTable *nameVal = ink_hash_table_create(InkHashTableKeyType_String);
  Tokenizer updates("&\n\r");
  Tokenizer pair("=");
  int numUpdates;
  char *name;
  char *value;
  char *submission_copy;
  int pairNum;

  if (submission == NULL) {
    ink_hash_table_destroy(nameVal);
    return NULL;
  }

  submission_copy = xstrdup(submission);
  numUpdates = updates.Initialize(submission_copy, SHARE_TOKS);

  for (int i = 0; i < numUpdates; i++) {
    pairNum = pair.Initialize(updates[i]);

    // We should have gotten either either 1 or 2 tokens
    //    One token indicates an variable being set to
    //    blank.  Two indicates the variable being set to
    //    a value.  If the submission is invalid, just forget
    //    about it.
    if (pairNum == 1 || pairNum == 2) {
      name = xstrdup(pair[0]);
      substituteUnsafeChars(name);

      // If the value is blank, store it as a null
      //   since BaseRecords represents empty as NULL
      //   as opposed to the empty string
      if (pairNum == 1) {
        value = NULL;
      } else {
        value = xstrdup(pair[1]);
        substituteUnsafeChars(value);
      }

      ink_hash_table_insert(nameVal, name, value);
      xfree(name);
    }
  }
  xfree(submission_copy);

  return nameVal;
}
#endif

// InkHashTable* processFormSubmission_noSubstitute(char* submission) 
//
//  A generic way to handle a HTML form submission.
//  Creates a hash table with name value pairs
//
//  CALLEE must deallocate the returned hash table with
//   ink_hash_table_destroy_and_xfree_values(InkHashTable *ht_ptr)
//
//  Note: This function will _not_ substituteUnsafeChars()
InkHashTable *
processFormSubmission_noSubstitute(char *submission)
{

  InkHashTable *nameVal = ink_hash_table_create(InkHashTableKeyType_String);
  Tokenizer updates("&\n\r");
  Tokenizer pair("=");
  int numUpdates;
  char *name;
  char *value;
  char *submission_copy;
  int pairNum;

  if (submission == NULL) {
    ink_hash_table_destroy(nameVal);
    return NULL;
  }

  submission_copy = xstrdup(submission);
  numUpdates = updates.Initialize(submission_copy, SHARE_TOKS);

  for (int i = 0; i < numUpdates; i++) {
    pairNum = pair.Initialize(updates[i]);

    // We should have gotten either either 1 or 2 tokens
    //    One token indicates an variable being set to
    //    blank.  Two indicates the variable being set to
    //    a value.  If the submission is invalid, just forget
    //    about it.
    if (pairNum == 1 || pairNum == 2) {
      name = xstrdup(pair[0]);

      // If the value is blank, store it as a null
      //   since BaseRecords represents empty as NULL
      //   as opposed to the empty string
      if (pairNum == 1) {
        value = NULL;
      } else {
        value = xstrdup(pair[1]);
      }

      ink_hash_table_insert(nameVal, name, value);
      xfree(name);
    }
  }
  xfree(submission_copy);

  return nameVal;
}

//  
// Removes any cr/lf line breaks from the text data
//
int
convertHtmlToUnix(char *buffer)
{
  char *read = buffer;
  char *write = buffer;
  int numSub = 0;

  while (*read != '\0') {
    if (*read == '\015') {
      *write = ' ';
      read++;
      write++;
      numSub++;
    } else {
      *write = *read;
      write++;
      read++;
    }
  }
  *write = '\0';
  return numSub;
}

//  Substitutes HTTP unsafe character representations
//   with their actual values.  Modifies the passed
//   in string
//
int
substituteUnsafeChars(char *buffer)
{
  char *read = buffer;
  char *write = buffer;
  char subStr[3];
  long charVal;
  int numSub = 0;

  subStr[2] = '\0';
  while (*read != '\0') {
    if (*read == '%') {
      subStr[0] = *(++read);
      subStr[1] = *(++read);
      charVal = strtol(subStr, (char **) NULL, 16);
      *write = (char) charVal;
      read++;
      write++;
      numSub++;
    } else if (*read == '+') {
      *write = ' ';
      write++;
      read++;
    } else {
      *write = *read;
      write++;
      read++;
    }
  }
  *write = '\0';
  return numSub;
}

// Substitutes for characters that can be misconstrued
//   as part of an HTML tag
// Allocates a new string which the
//   the CALLEE MUST DELETE
//
char *
substituteForHTMLChars(const char *buffer)
{

  char *safeBuf;                // the return "safe" character buffer
  char *safeCurrent;            // where we are in the return buffer
  const char *inCurrent = buffer;       // where we are in the original buffer
  int inLength = strlen(buffer);        // how long the orig buffer in

  // Maximum character expansion is one to three
  unsigned int bufferToAllocate = (inLength * 5);
  safeBuf = new char[bufferToAllocate + 1];
  safeCurrent = safeBuf;

  while (*inCurrent != '\0') {
    switch (*inCurrent) {
    case '"':
      ink_strncpy(safeCurrent, "&quot;", bufferToAllocate);
      safeCurrent += 6;
      break;
    case '<':
      ink_strncpy(safeCurrent, "&lt;", bufferToAllocate);
      safeCurrent += 4;
      break;
    case '>':
      ink_strncpy(safeCurrent, "&gt;", bufferToAllocate);
      safeCurrent += 4;
      break;
    case '&':
      ink_strncpy(safeCurrent, "&amp;", bufferToAllocate);
      safeCurrent += 5;
      break;
    default:
      *safeCurrent = *inCurrent;
      safeCurrent += 1;
      break;
    }

    inCurrent++;
  }
  *safeCurrent = '\0';
  return safeBuf;
}


// bool ProxyShutdown() 
//
//  Attempts to turn the proxy off.  Returns
//    true if the proxy is off when the call returns
//    and false if it is still on
//
bool
ProxyShutdown()
{
  int i = 0;

  // Check to make sure that we are not already down
  if (!lmgmt->processRunning()) {
    return true;
  }
  // Send the shutdown event
  lmgmt->signalEvent(MGMT_EVENT_SHUTDOWN, "shutdown");

  // Wait for awhile for shtudown to happen
  do {
    mgmt_sleep_sec(1);
    i++;
  } while (i < 10 && lmgmt->processRunning());

  // See if we succeeded
  if (lmgmt->processRunning()) {
    return false;
  } else {
    return true;
  }
}

// void appendDefautDomain(char* hostname, int bufLength)
//
//   Appends the pasted in hostname with the default
//     domain if the hostname is an unqualified name
//
//   The default domain is obtained from the resolver libraries
//    data structure
//
//   Truncates the domain name if bufLength is too small
//
//
void
appendDefaultDomain(char *hostname, int bufLength)
{

  int len = strlen(hostname);
  const char msg[] = "Nodes will be know by their unqualified host name";
  static int error_before = 0;  // Race ok since effect is multple error msg

  ink_assert(len < bufLength);
  ink_assert(bufLength >= 64);

  // Ensure null termination of the result string
  hostname[bufLength - 1] = '\0';

  if (strchr(hostname, '.') == NULL) {
    if (_res.defdname[0] != '\0') {
      if (bufLength - 2 >= (int) (strlen(hostname) + strlen(_res.defdname))) {
        strncat(hostname, ".", bufLength - strlen(hostname));
        strncat(hostname, _res.defdname, MAXDNAME - 2 - strlen(hostname));
      } else {
        if (error_before == 0) {
          mgmt_log(stderr, "%s %s\n", "[appendDefaultDomain] Domain name is too long.", msg);
          error_before++;
        }
      }
    } else {
      if (error_before == 0) {
        mgmt_log(stderr, "%s %s\n", "[appendDefaultDomain] Unable to determine default domain name.", msg);
        error_before++;
      }
    }
  }
}

bool
recordValidityCheck(const char *varName, const char *value)
{
  RecCheckT check_t;
  char *pattern;

  if (RecGetRecordCheckType((char *) varName, &check_t) != REC_ERR_OKAY) {
    return false;
  }
  if (RecGetRecordCheckExpr((char *) varName, &pattern) != REC_ERR_OKAY) {
    return false;
  }

  switch (check_t) {
  case RECC_STR:
    if (recordRegexCheck(pattern, value)) {
      return true;
    }
    break;
  case RECC_INT:
    if (recordRangeCheck(pattern, value)) {
      return true;
    }
    break;
  case RECC_IP:
    if (recordIPCheck(pattern, value)) {
      return true;
    }
    break;
  case RECC_NULL:
    // skip checking
    return true;
  default:
    // unknown RecordCheckType...
    mgmt_log(stderr, "[WebMgmtUtil] error, unknown RecordCheckType for record %s\n", varName);
  }

  return false;


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

bool
recordRangeCheck(const char *pattern, const char *value)
{
  int l_limit;
  int u_limit;
  int val;
  char *p = (char *) pattern;
  Tokenizer dashTok("-");

  if (recordRegexCheck("^[0-9]+$", value)) {
    while (*p != '[') {
      p++;
    }                           // skip to '['
    if (dashTok.Initialize(++p, COPY_TOKS) == 2) {
      l_limit = atoi(dashTok[0]);
      u_limit = atoi(dashTok[1]);
      val = atoi(value);
      if (val >= l_limit && val <= u_limit) {
        return true;
      }
    }
  }
  return false;
}

bool
recordIPCheck(const char *pattern, const char *value)
{
  //  regex_t regex;
  //  int result;
  bool check;
  const char *range_pattern =
    "\\[[0-9]+\\-[0-9]+\\]\\.\\[[0-9]+\\-[0-9]+\\]\\.\\[[0-9]+\\-[0-9]+\\]\\.\\[[0-9]+\\-[0-9]+\\]";
  const char *ip_pattern = "[0-9]*[0-9]*[0-9].[0-9]*[0-9]*[0-9].[0-9]*[0-9]*[0-9].[0-9]*[0-9]*[0-9]";

  Tokenizer dotTok1(".");
  Tokenizer dotTok2(".");
  int i;

  check = true;
  if (recordRegexCheck(range_pattern, pattern) && recordRegexCheck(ip_pattern, value)) {
    if (dotTok1.Initialize((char *) pattern, COPY_TOKS) == 4 && dotTok2.Initialize((char *) value, COPY_TOKS) == 4) {
      for (i = 0; i < 4 && check; i++) {
        if (!recordRangeCheck(dotTok1[i], dotTok2[i])) {
          check = false;
        }
      }
      if (check) {
        return true;
      }
    }
  } else if (strcmp(value, "") == 0) {
    return true;
  }
  return false;
}

bool
recordRestartCheck(const char *varName)
{

  RecUpdateT update_t;

  if (RecGetRecordUpdateType((char *) varName, &update_t) != REC_ERR_OKAY) {
    return false;
  }

  if (update_t == RECU_RESTART_TS || update_t == RECU_RESTART_TM || update_t == RECU_RESTART_TC) {
    return true;
  }

  return false;

}

void
fileCheckSum(char *buffer, int size, char *checksum, const size_t checksumSize)
{
  INK_DIGEST_CTX md5_context;
  char checksum_md5[16];

  ink_code_incr_md5_init(&md5_context);
  ink_code_incr_md5_update(&md5_context, buffer, size);
  ink_code_incr_md5_final(checksum_md5, &md5_context);
  ink_code_md5_stringify(checksum, checksumSize, checksum_md5);
}

// int processSpawn()
//
//  Attempts to spawn out a process by given a list of arguments
//
int
processSpawn(char *args[],
             EnvBlock * env,
             textBuffer * input_buf, textBuffer * output_buf, bool nowait, bool run_as_root, bool * truncated)
{
  int status = 0;

#ifndef _WIN32
  char buffer[1024];
  int nbytes;
  int stdinPipe[2];
  int stdoutPipe[2];
  pid_t pid;
  long total;
  bool cutoff;
  char *too_large_msg = "\nfile too large, truncated here...";

  pipe(stdinPipe);
  if (!nowait) {
    pipe(stdoutPipe);
  }

  pid = fork();
  // failed to create child process
  if (pid == (pid_t) - 1) {
    status = 1;
    mgmt_elog(stderr, "[processSpawn] unable to fork [%d '%s']\n", errno, strerror(errno));

  } else if (pid == 0) {        // child process
    // close all the listening port in child process
    for (int i = 0; i < MAX_PROXY_SERVER_PORTS && lmgmt->proxy_server_fd[i] >= 0; i++) {
      ink_close_socket(lmgmt->proxy_server_fd[i]);
    }
    // set uid to be the effective uid if it's run as root
    if (run_as_root) {
      restoreRootPriv();
      if (setuid(geteuid()) == -1) {
        mgmt_elog(stderr, "[processSpawn] unable to set uid to euid");
      }
    }
    if (nowait) {
      // nowait - detach from parent process
      if (setsid() == (pid_t) - 1) {
        mgmt_elog(stderr, "[processSpawn] unable to detach process from parent by setsid");
      }
    }
    // setup stdin/stdout
    dup2(stdinPipe[0], STDIN_FILENO);
    close(stdinPipe[0]);
    close(stdinPipe[1]);
    if (!nowait) {
      dup2(stdoutPipe[1], STDOUT_FILENO);
      close(stdoutPipe[1]);
      close(stdoutPipe[0]);
    }
    // exec process
    if (env) {
      pid = execve(args[0], &args[0], env->toStringArray());
    } else {
      pid = execv(args[0], &args[0]);
    }
    if (pid == -1) {
      mgmt_elog(stderr, "[processSpawn] unable to execve [%s...]\n", args[0]);
    }
    _exit(1);

  } else {
    // parent process
    close(stdinPipe[0]);
    if (!nowait) {
      close(stdoutPipe[1]);
    }

    if (input_buf) {
      // write input_buf to stdin of child process
      write(stdinPipe[1], input_buf->bufPtr(), input_buf->spaceUsed());
    }
    close(stdinPipe[1]);

    if (!nowait) {
      // wait until child process is done
      if (output_buf) {
        total = 0;
        cutoff = false;
        if (truncated) {
          *truncated = false;
        }
        // write stdout to output buffer
        // anything > 1MB would be truncated
        // coverity[string_null_argument]
        while ((nbytes = read(stdoutPipe[0], buffer, 1024)) && !cutoff) {
          if (total > 1048576) {
            cutoff = true;

          } else {
            char *safebuf;
            safebuf = substituteForHTMLChars(buffer);
            if (safebuf) {
              if (strlen(safebuf) < sizeof(buffer)) {
                ink_strncpy(buffer, safebuf, sizeof(buffer));
              }
              delete[]safebuf;
            }
            output_buf->copyFrom(buffer, nbytes);
            total += nbytes;
          }
        }
        if (cutoff) {
          output_buf->copyFrom(too_large_msg, strlen(too_large_msg));
          if (truncated) {
            *truncated = true;
          }
          // drain the rest of the buffer
          while (read(stdoutPipe[0], buffer, 1024));
        }
      }
      waitpid(pid, &status, 0);
      if (status) {
        mgmt_elog(stderr, "[processSpawn] spawned process(%s) returns non-zero status '%d'", args[0], status);
      }
      close(stdoutPipe[0]);
    }
  }

  if (run_as_root) {
    removeRootPriv();
  }
#endif // _WIN32
  return status;
}

//-------------------------------------------------------------------------
// getFilesInDirectory
// 
// copied from MultiFiles::WalkFiles - but slightly modified
// returns -1 if directory does not exit
// returns 1 if everything is ok
//-------------------------------------------------------------------------
int
getFilesInDirectory(char *managedDir, ExpandingArray * fileList)
{
#ifndef _WIN32
  struct dirent *dirEntry;
  DIR *dir;
#else
  char *searchPattern;
  WIN32_FIND_DATA W32FD;
#endif
  char *fileName;
  char *filePath;
  struct stat fileInfo;
  //  struct stat records_config_fileInfo;
  fileEntry *fileListEntry;

#ifndef _WIN32
  if ((dir = opendir(managedDir)) == NULL) {
    mgmt_log(stderr, "[getFilesInDirectory] Unable to open %s directory: %s\n", managedDir, strerror(errno));
    return -1;
  }
  // The fun of Solaris - readdir_r requires a buffer passed into it
  //   The man page says this obscene expression gives us the proper
  //     size
  dirEntry = (struct dirent *) xmalloc(sizeof(struct dirent) + pathconf(".", _PC_NAME_MAX) + 1);

  struct dirent *result;
  while (ink_readdir_r(dir, dirEntry, &result) == 0) {
    if (!result)
      break;
    fileName = dirEntry->d_name;
    if (!fileName || !*fileName) {
      continue;
    }
    filePath = newPathString(managedDir, fileName);
    if (stat(filePath, &fileInfo) < 0) {
      mgmt_log(stderr, "[getFilesInDirectory] Stat of a %s failed : %s\n", fileName, strerror(errno));
    } else {
      // Ignore ., .., and any dot files
      if (fileName && *fileName != '.') {
        fileListEntry = (fileEntry *) xmalloc(sizeof(fileEntry));
        fileListEntry->c_time = fileInfo.st_ctime;
        ink_strncpy(fileListEntry->name, fileName, FILE_NAME_MAX);
        fileList->addEntry(fileListEntry);
      }
    }
    delete[]filePath;
  }

  xfree(dirEntry);
  closedir(dir);
#else
  // Append '\*' as a wildcard for FindFirstFile()
  searchPattern = newPathString(managedDir, "*");
  HANDLE hDInfo = FindFirstFile(searchPattern, &W32FD);

  if (INVALID_HANDLE_VALUE == hDInfo) {
    mgmt_log(stderr, "[getFilesInDirectory] FindFirstFile failed for %s: %s\n", searchPattern, ink_last_err());
    delete[]searchPattern;
    return -1;
  }
  delete[]searchPattern;

  while (FindNextFile(hDInfo, &W32FD)) {
    fileName = W32FD.cFileName;
    filePath = newPathString(managedDir, fileName);
    if (stat(filePath, &fileInfo) < 0) {
      mgmt_log(stderr, "[getFilesInDirectory] Stat of a %s failed: %s\n", fileName, strerror(errno));
    } else {
      // Ignore ., .., and any dot files
      if (fileName && *fileName != '.') {
        fileListEntry = (fileEntry *) xmalloc(sizeof(fileEntry));
        fileListEntry->c_time = fileInfo.st_ctime;
        strcpy(fileListEntry->name, fileName);
        fileList->addEntry(fileListEntry);
      }
    }
    delete[]filePath;
  }

  FindClose(hDInfo);
#endif

  fileList->sortWithFunction(fileEntryCmpFunc);
  return 1;
}

//-------------------------------------------------------------------------
// newPathString
//
// copied from MultiFile::newPathString
// Note: uses C++ new/delete for memory allocation/deallocation
//-------------------------------------------------------------------------
char *
newPathString(const char *s1, const char *s2)
{
  int newLen;
  char *newStr;

  newLen = strlen(s1) + strlen(s2) + 2;
  newStr = new char[newLen];
  ink_assert(newStr != NULL);
  snprintf(newStr, newLen, "%s%s%s", s1, DIR_SEP, s2);
  return newStr;
}