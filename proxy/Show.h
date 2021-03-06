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

  Show.h

  
 ****************************************************************************/

#ifndef _Show_h_
#define _Show_h_

#include "StatPages.h"

#define STREQ_PREFIX(_x,_s) (!strncasecmp(_x,_s,sizeof(_s)-1))

struct ShowCont;
typedef int (ShowCont::*ShowContEventHandler) (int event, Event * data);
struct ShowCont:Continuation
{
  Action action;
  char *buf, *start, *ebuf;
  int iarg;
  char *sarg;

  int show(char *s, ...)
  {
    va_list aap;
      va_start(aap, s);
    int l = ebuf - buf;
    int done = ink_vsnprintf(buf, l, s, aap);
    if (done > l - 256)
    {
      char *start2 = (char *) xrealloc(start, (ebuf - start) * 2);
        ebuf = start2 + (ebuf - start) * 2;
        buf = start2 + (buf - start);
        start = start2;
        l = ebuf - buf;
        done = ink_vsnprintf(buf, l, s, aap);
      if (done > l - 256)
      {
        va_end(aap);
        return EVENT_DONE;
      }
      buf += done;
    } else
      buf += done;

    va_end(aap);
    return EVENT_CONT;
  }

#define CHECK_SHOW(_x) if (_x == EVENT_DONE) return complete_error(event,e);

  int complete(int event, Event * e)
  {
    CHECK_SHOW(show("</BODY>\n</HTML>\n"));
    if (!action.cancelled) {
      StatPageData data(start, buf - start);
      action.continuation->handleEvent(STAT_PAGE_SUCCESS, &data);
    } else {
      xfree(start);
      start = NULL;
    }
    return done(VIO::CLOSE, event, e);
  }

  int complete_error(int event, Event * e)
  {
    xfree(start);
    start = NULL;
    if (!action.cancelled)
      action.continuation->handleEvent(STAT_PAGE_FAILURE, NULL);
    return done(VIO::ABORT, event, e);
  }

  int begin(char *name)
  {
    return show("<HTML>\n<HEAD><TITLE>%s</TITLE>\n"
                "<BODY BGCOLOR=\"#ffffff\" FGCOLOR=\"#00ff00\">\n" "<H1>%s</H1>\n", name, name);
  }

  int showError(int event, Event * e)
  {
    return complete_error(event, e);
  }

  virtual int done(int e, int event, void *data)
  {
    if (sarg) {
      xfree(sarg);
      sarg = NULL;
    }
    delete this;
    return EVENT_DONE;
  }

ShowCont(Continuation * c, HTTPHdr * h):
  Continuation(NULL), iarg(0), sarg(0) {
    mutex = c->mutex;
    action = c;
    buf = (char *) xmalloc(32000);
    start = buf;
    ebuf = buf + 32000;
  }
  ~ShowCont() {
    if (start)
      xfree(start);
  }
};


#endif
