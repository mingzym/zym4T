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
Assertions

***************************************************************************/

#include <stdio.h>
#include <string.h>
#include "ink_assert.h"
#include "ink_error.h"
#include "ink_unused.h"
#include "ink_string.h"       /* MAGIC_EDITING_TAG */

int
_ink_assert(const char *a, const char *f, int l)
{
  char buf1[101];
  char buf2[256];

#ifndef NO_ASSERTS
  ink_strncpy(buf1, f, 100);
  snprintf(buf2, sizeof(buf2), "%s:%d: failed assert `", buf1, l);
  strncat(buf2, a, 100);
  strncat(buf2, "`", 1);
  ink_fatal(1, buf2);
#endif /* NO_ASSERTS */

  return (0);
}
