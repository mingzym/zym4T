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

  SocketManager.cc
 ****************************************************************************/
#include "inktomi++.h"

#if (HOST_OS != linux)
#include <sys/filio.h>
#endif

#if (HOST_OS == solaris)
#include <sys/types.h>
#include <sys/mman.h>
extern "C" int madvise(caddr_t, size_t, int); // FIXME: why is this not being found
#endif

#include "P_EventSystem.h"


SocketManager socketManager;
int monitor_read_activity = !!getenv("MONITOR_READS");
int monitor_write_activity = !!getenv("MONITOR_WRITES");

#define MONITOR_STRING "%lX %lX %d %s\n"


void
monitor_disk_read(int fd, void *buf, int size, off_t offset, char *tag)
{
  (void) fd;
  (void) buf;
  fprintf(stderr, "READ: " MONITOR_STRING, (unsigned long) offset,
          (unsigned long) offset + size, size, (tag ? tag : ""));
}


void
monitor_disk_write(int fd, void *buf, int size, off_t offset, char *tag)
{
  (void) fd;
  (void) buf;
  fprintf(stderr, "WRITE: " MONITOR_STRING, (unsigned long) offset,
          (unsigned long) offset + size, size, (tag ? tag : ""));
}

SocketManager::SocketManager()
{
  pagesize = getpagesize();
}

SocketManager::~SocketManager()
{
  // free the hash table and values
}

int
safe_msync(caddr_t addr, size_t len, caddr_t end, int flags)
{
  (void) end;
  caddr_t a = (caddr_t) (((unsigned long) addr) & ~(socketManager.pagesize - 1));
  size_t l = (len + (addr - a) + socketManager.pagesize - 1)
    & ~(socketManager.pagesize - 1);
  if ((a + l) > end)
    l = end - a;                // strict limit
#if (HOST_OS == linux)
/* Fix INKqa06500
   Under Linux, msync(..., MS_SYNC) calls are painfully slow, even on
   non-dirty buffers. This is true as of kernel 2.2.12. We sacrifice
   restartability under OS in order to avoid a nasty performance hit
   from a kernel global lock. */
  if (flags & MS_SYNC)
    flags = (flags & ~MS_SYNC) | MS_ASYNC;
#endif
  int res = msync(a, l, flags);
  return res;
}

int
safe_madvise(caddr_t addr, size_t len, caddr_t end, int flags)
{
  (void) end;
#if (HOST_OS == linux)
  (void) addr;
  (void) len;
  (void) end;
  (void) flags;
  return 0;
#else
  caddr_t a = (caddr_t) (((unsigned long) addr) & ~(socketManager.pagesize - 1));
  size_t l = (len + (addr - a) + socketManager.pagesize - 1)
    & ~(socketManager.pagesize - 1);
  int res = 0;
  res = madvise(a, l, flags);
  return res;
#endif
}

int
safe_mlock(caddr_t addr, size_t len, caddr_t end)
{

  caddr_t a = (caddr_t) (((unsigned long) addr) & ~(socketManager.pagesize - 1));
  size_t l = (len + (addr - a) + socketManager.pagesize - 1)
    & ~(socketManager.pagesize - 1);
  if ((a + l) > end)
    l = end - a;                // strict limit
  int res = mlock(a, l);
  return res;
}

typedef struct _DIP_ele
{
  struct _DIP_ele *next;
  u_long val;
} DIP_ele;

int
SocketManager::ink_bind(SOCKET s, struct sockaddr *name, int namelen, short Proto)
{
  (void) Proto;
  int retval;

  retval = safe_bind(s, name, namelen);

  if (retval < 0) {
    return retval;
  }

  return retval;
}


int
SocketManager::close(int s, teFDType eT)
{
  int res;
  if (!s) {
    printf("broken UDPConnection trying to close stdin\n");
    return 0;
  }
  do {
    if (keSocket == eT) {
    }
    res =::close(s);
    if (res < 0)
      res = -errno;
  } while (res == -EINTR);
  return res;
}
