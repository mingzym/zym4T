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
 LogFieldAliasMap.h

 
 ***************************************************************************/

#ifndef LOG_FIELD_ALIAS_MAP_H
#define LOG_FIELD_ALIAS_MAP_H

#include <stdarg.h>
#include <string.h>

#include "inktomi++.h"
#include "Ptr.h"
#include "LogUtils.h"
#include "ink_string.h"

/*****************************************************************************

The LogFieldAliasMap class is an abstract class used to provide an
interface to map between numbers of type IntType and strings. The
purpose is to obtain one representation from the other so that easy to
remember names can be used to refer to log fields of integer type.

The methods that subclasses should implement are:

1) asInt(char *key, IntType *val)

This method takes a string and sets the IntType argument to the
corresponding value, (unless the string is invalid). It returns an
error status.

2) asString(IntType key, char *buf, size_t bufLen, size_t *numChars=0)

This method takes an IntType key and writes its equivalent string to a
bufer buf of length bufLen. It sets the number of written characters
numChars (if numChars is not NULL), and returns an error status.

The IntType to string conversion is used when unmarshaling data prior to 
writing to a log file, and the string to IntType conversion is used when
building filters (so that the filter value can be specified as a string,
but the actual field comparison is done between IntTypes).

Note that LogFieldAliasMap is derived from RefCountObj, so once a map
is constructed a pointer to it can be passed to other objects (e.g.,
to a LogField object) without the object having to worry about freeing
any memory the map may have allocated.
 
 *****************************************************************************/


class LogFieldAliasMap:public RefCountObj
{
public:
  // the logging system assumes log entries of type sINT are 
  // unsigned integers (LOG_INT type) so we define IntType to be unsigned
  typedef unsigned int IntType;
  enum
  { ALL_OK = 0, INVALID_INT, INVALID_STRING, BUFFER_TOO_SMALL };

  virtual int asInt(char *key, IntType * val, bool case_sensitive = 0) const = 0;
  virtual int asString(IntType key, char *buf, size_t bufLen, size_t * numChars = 0) const = 0;
};

/*****************************************************************************

A LogFieldAliasTable implements a LogFieldAliasMap through a
straightforward table. The entries in the table are input with the
init(numPairs, ...) method.  Arguments to this method are the number
numPairs of table entries, followed by the entries themselves in the
form integer, string. For example:

table->init(3, 1, "one", 2, "two", 7, "seven")

 *****************************************************************************/

struct LogFieldAliasTableEntry
{
  bool valid;                   // entry in table is valid
  char *name;                   // the string equivalent
  size_t length;                // the length of the string
    LogFieldAliasTableEntry():valid(false), name(NULL), length(0)
  {
  };
};

class LogFieldAliasTable:public LogFieldAliasMap
{
private:

  size_t m_min;                 // minimum numeric value
  size_t m_max;                 // maximum numeric value
  size_t m_entries;             // number of entries in table
  LogFieldAliasTableEntry *m_table;     // array of table entries

public:

    LogFieldAliasTable():m_min(0), m_max(0), m_entries(0), m_table(0)
  {
  };
  ~LogFieldAliasTable() {
    delete[]m_table;
  };

  void init(size_t numPairs, ...);

  int asInt(char *key, IntType * val, bool case_sensitive_search = 0) const
  {
    int retVal = INVALID_STRING;

    for (size_t i = 0; i < m_entries; i++)
    {
      bool found;
      if (m_table[i].valid)
      {
        if (case_sensitive_search) {
          found = (strcmp(key, m_table[i].name) == 0);
        } else
        {
          found = (strcasecmp(key, m_table[i].name) == 0);
        }
      } else {
        found = false;
      }
      if (found) {
        *val = (unsigned int) (i + m_min);
        retVal = ALL_OK;
        break;
      }
    }

    return retVal;
  };

  int asString(IntType key, char *buf, size_t bufLen, size_t * numCharsPtr = 0) const
  {
    int retVal;
    size_t numChars;

    size_t i = key - m_min;
    if (m_entries && key >= m_min && key <= m_max && m_table[i].valid)
    {
      register size_t l = m_table[i].length;
      if (l < bufLen)
      {
        ink_strncpy(buf, m_table[key - m_min].name, bufLen);
        numChars = l;
        retVal = ALL_OK;
      } else
      {
        numChars = 0;
        retVal = BUFFER_TOO_SMALL;
      }
    } else {
      numChars = 0;
      retVal = INVALID_INT;
    }
    if (numCharsPtr) {
      *numCharsPtr = numChars;
    }
    return retVal;
  };
};


/*****************************************************************************

The LogFieldAliasIP class implements a LogFieldAliasMap that converts IP
addresses from their integer value to the "dot" notation and back.

 *****************************************************************************/

class LogFieldAliasIP:public LogFieldAliasMap
{
public:
  int asInt(char *str, IntType * ip, bool case_sensitive = 0) const
  {
    NOWARN_UNUSED(case_sensitive);
    unsigned a, b, c, d;
    // coverity[secure_coding]
    if (sscanf(str, "%u.%u.%u.%u", &a, &b, &c, &d) == 4) {
      *ip = d | (c << 8) | (b << 16) | (a << 24);
      return ALL_OK;
    } else
    {
      return INVALID_STRING;
    }
  };

  int asString(IntType ip, char *buf, size_t bufLen, size_t * numCharsPtr = 0) const
  {
    return (LogUtils::ip_to_str(ip, buf, bufLen, numCharsPtr) ? BUFFER_TOO_SMALL : ALL_OK);
/*
	int retVal;
	size_t numChars;
	size_t n = ink_snprintf (buf, bufLen, "%u.%u.%u.%u",
				 (ip >> 24) & 0xff, 
				 (ip >> 16) & 0xff, 
				 (ip >> 8)  & 0xff, 
				 ip         & 0xff);
	if (n < bufLen) {
	    numChars = n;
	    retVal = ALL_OK;
	} else {
	    numChars = bufLen - 1;
	    retVal = BUFFER_TOO_SMALL;
	}
	if (numCharsPtr) {
	    *numCharsPtr = numChars;
	}
	return retVal;
*/
  };
};

/*****************************************************************************

The LogFieldAliasIPhex class implements a LogFieldAliasMap that converts IP
addresses from their integer value to the "hex" notation and back.

 *****************************************************************************/

class LogFieldAliasIPhex:public LogFieldAliasMap
{
public:
  int asInt(char *str, IntType * ip, bool case_sensitive = 0) const
  {
    NOWARN_UNUSED(case_sensitive);
    unsigned a, b, c, d;
    // coverity[secure_coding]
    if (sscanf(str, "%2x%2x%2x%2x", &a, &b, &c, &d) == 4) {
      *ip = d | (c << 8) | (b << 16) | (a << 24);
      return ALL_OK;
    } else
    {
      return INVALID_STRING;
    }
  };

  int asString(IntType ip, char *buf, size_t bufLen, size_t * numCharsPtr = 0) const
  {

    return (LogUtils::timestamp_to_hex_str(ip, buf, bufLen, numCharsPtr) ? BUFFER_TOO_SMALL : ALL_OK);
  };
};

/*****************************************************************************

The LogFieldAliasTimehex class implements a LogFieldAliasMap that converts time
from their integer value to the "hex" notation and back.

 *****************************************************************************/

class LogFieldAliasTimeHex:public LogFieldAliasMap
{
public:
  int asInt(char *str, IntType * time, bool case_sensitive = 0) const
  {
    NOWARN_UNUSED(case_sensitive);
    unsigned long a;
    // coverity[secure_coding]
    if (sscanf(str, "%lx", (unsigned long *) &a) == 1) {
      *time = (IntType) a;
      return ALL_OK;
    } else
    {
      return INVALID_STRING;
    }
  };

  int asString(IntType time, char *buf, size_t bufLen, size_t * numCharsPtr = 0) const
  {
    return (LogUtils::timestamp_to_hex_str(time, buf, bufLen, numCharsPtr) ? BUFFER_TOO_SMALL : ALL_OK);
  };
};



//LOG_FIELD_ALIAS_MAP_H
#endif
