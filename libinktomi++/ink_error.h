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

  ink_error.h

  Error reporting routines for libinktomi.a.

 ****************************************************************************/

#ifndef _ink_error_h_
#define	_ink_error_h_

#include <stdarg.h>
#include "ink_platform.h"

#include "ink_apidefs.h"
#include "ink_defs.h"

#ifdef __cplusplus
extern "C"
{
#endif                          /* __cplusplus */

/* used all over the place - currently used by NT only. */
  typedef enum
  {
    keNo_OP = 0,
    keENCAPSULATED_STRING = 1,
    keINSERT_STRING = 2
  } eInsertStringType;


  inkcoreapi void ink_fatal_va(int return_code, const char *message_format, va_list ap);
  void ink_fatal(int return_code, const char *message_format, ...) PRINTFLIKE(2, 3);
  void ink_pfatal(int return_code, const char *message_format, ...) PRINTFLIKE(2, 3);
  void ink_warning(const char *message_format, ...) PRINTFLIKE(1, 2);
  void ink_pwarning(const char *message_format, ...) PRINTFLIKE(1, 2);
  void ink_notice(const char *message_format, ...) PRINTFLIKE(1, 2);
  void ink_eprintf(const char *message_format, ...) PRINTFLIKE(1, 2);
  void ink_error(const char *message_format, ...) PRINTFLIKE(1, 2);
  void ink_dprintf(int debug_level, const char *message_format, ...) PRINTFLIKE(2, 3);
  void ink_fatal_die(const char *message_format, ...) PRINTFLIKE(1, 2);

  void ink_die_die_die(int retval);
  void ink_segv();
  int ink_set_dprintf_level(int debug_level);


#ifdef __cplusplus
}
#endif                          /* __cplusplus */

#endif
