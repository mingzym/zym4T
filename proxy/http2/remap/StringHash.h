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

#ifndef _STRING_HASH_H_
#define _STRING_HASH_H_

#define STRINGHASH_MIN_TBL_SIZE 0x10
#define STRINGHASH_MAX_TBL_SIZE 0x1000000

/**
 * StringHashEntry holds the actual pointer to hash entry
**/
class StringHashEntry
{
public:
  StringHashEntry * next;
  unsigned long hashid;
  int hash_table_index;
  int strsize;
  char *str;
  void *ptr;

    StringHashEntry();
   ~StringHashEntry();
    StringHashEntry & clean();
  const char *setstr(const char *_str, int _strsize = (-1));
};

/**
 * String Hash for secondary remap lookup
**/
class StringHash
{
public:
  int hash_size;
  int hash_mask;
  int hash_mask_size;
  int max_hit_level;
  bool ignore_case;
  StringHashEntry **hash;

    StringHash(int _hash_size = 0x1000, bool _ignore_case = false);
   ~StringHash();
private:
  unsigned long csum_calc(void *_buf, int size);
public:
    StringHashEntry * find_or_add(void *_ptr, const char *str, int strsize = (-1));
};

#endif
