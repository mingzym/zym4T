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

/**************************************
 *
 * MgmtUtils.h
 *   Some utility and support functions for the management module.
 *
 * 
 */

#include "ink_unused.h"      /* MAGIC_EDITING_TAG */

#include "inktomi++.h"
#include "MgmtUtils.h"
#include "Diags.h"

#ifdef LOCAL_MANAGER
#include "mgmt2/Main.h"
#else
#include "mgmt2/../Main.h"
#endif

extern int diags_init;

#if defined (_WIN32)
#define syslog(_X, _Y, _Z)
#endif
static int use_syslog = 0;

/* mgmt_use_syslog()
 *
 *    Called to indicate that the syslog should be used and
 *       the log has been opened
 */
void
mgmt_use_syslog()
{
  use_syslog = 1;
}

/*
 * mgmt_readline(...)
 *   Simple, inefficient, read line function. Takes a socket to read
 * from, a char * to write into, and a max len to read. The newline
 * is stripped.
 *
 * Returns:  num bytes read
 *           -1  error
 */
int
mgmt_readline(int soc, char *buf, int maxlen)
{
  int n, rc;
  char c;

  for (n = 1; n < maxlen; n++) {

    if ((rc = ink_read_socket(soc, &c, 1)) == 1) {

      *buf++ = c;
      if (c == '\n') {
        --buf;
        *buf = '\0';
        if (*(buf - 1) == '\r') {
          --buf;
          *buf = '\0';
        }
        break;
      }
    } else if (rc == 0) {

      if (n == 1) {             /* EOF */
        return 0;
      } else {
        break;
      }
    } else {                    /* Error */
      return -1;
    }
  }
  return n;
}                               /* End mgmt_readline */


/* 
 * mgmt_writeline(...)
 *   Simple, inefficient, write line function. Takes a soc to write to,
 * a char * containing the data, and the number of bytes to write. 
 * It sends nbytes + 1 bytes worth of data, the + 1 being the newline 
 * character.
 *
 * Returns:    num bytes not written
 *             -1  error
 */
int
mgmt_writeline(int soc, char *data, int nbytes)
{
  int nleft, nwritten, n;
  char *tmp = data;

  nleft = nbytes;
  while (nleft > 0) {
    nwritten = ink_write_socket(soc, tmp, nleft);
    if (nwritten <= 0) {        /* Error or nothing written */
      return nwritten;
    }
    nleft -= nwritten;
    tmp += nwritten;
  }

  if ((n = ink_write_socket(soc, "\n", 1)) <= 0) {      /* Terminating newline */
    if (n < 0) {
      return n;
    } else {
      return (nbytes - nleft);
    }
  }

  return (nleft);               /* Paranoia */
}                               /* End mgmt_writeline */

#if !defined(_WIN32)

/* 
 * mgmt_read_pipe()
 * - Reads from a pipe
 *
 * Returns: bytes read
 *          0 on EOF
 *          -errno on error
 */

int
mgmt_read_pipe(int fd, char *buf, int bytes_to_read)
{
  int err = 0;
  char *p = buf;
  int bytes_read = 0;
  while (bytes_to_read > 0) {
    err = ink_read_socket(fd, p, bytes_to_read);
    if (err == 0) {
      return err;
    } else if (err < 0) {
      switch (errno) {
      case EINTR:
      case EAGAIN:
#if (HOST_OS == hpux)
      case EWOULDBLOCK:
#endif
        mgmt_sleep_msec(1);
        continue;
      default:
        return -errno;
      }
    }
    bytes_to_read -= err;
    bytes_read += err;
    p += err;
  }
  /*
     Ldone:
   */
  return bytes_read;
}

/* 
 * mgmt_write_pipe()
 * - Writes to a pipe
 *
 * Returns: bytes written
 *          0 on EOF
 *          -errno on error
 */

int
mgmt_write_pipe(int fd, char *buf, int bytes_to_write)
{
  int err = 0;
  char *p = buf;
  int bytes_written = 0;
  while (bytes_to_write > 0) {
    err = ink_write_socket(fd, p, bytes_to_write);
    if (err == 0) {
      return err;
    } else if (err < 0) {
      switch (errno) {
      case EINTR:
      case EAGAIN:
#if (HOST_OS == hpux)
      case EWOULDBLOCK:
#endif
        mgmt_sleep_msec(1);
        continue;
      default:
        return -errno;
      }
    }
    bytes_to_write -= err;
    bytes_written += err;
    p += err;
  }
  /*
     Ldone:
   */
  return bytes_written;
}

#else

/* 
 * mgmt_read_pipe()
 * - Reads from a message type named pipe.
 * - Blocking.
 *
 * Returns: num bytes read
 *          -1  error
 */
int
mgmt_read_pipe(HANDLE hpipe, char *buf, int maxlen)
{
  DWORD bytesRead = 0;
  OVERLAPPED ov;

  ov.Internal = ov.InternalHigh = ov.Offset = ov.OffsetHigh = 0;
  ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
  if (ReadFile(hpipe, (LPVOID) buf, maxlen, NULL, &ov) == 0) {
    if (GetLastError() != ERROR_IO_PENDING) {
      CloseHandle(ov.hEvent);
      return -1;
    }
  }
  if (WaitForSingleObject(ov.hEvent, INFINITE) == WAIT_FAILED) {
    CloseHandle(ov.hEvent);
    return -1;
  }
  if (GetOverlappedResult(hpipe, &ov, &bytesRead, FALSE) == 0) {
    CloseHandle(ov.hEvent);
    return -1;
  }

  CloseHandle(ov.hEvent);
  buf[bytesRead] = 0;
  return bytesRead;
}

/* 
 * mgmt_write_pipe()
 * - Writes to a message type named pipe.
 * - Blocking.
 *
 * Returns: num bytes not written
 *          -1  error
 */
int
mgmt_write_pipe(HANDLE hpipe, char *data, int nbytes)
{
  DWORD bytesWritten = 0;
  OVERLAPPED ov;

  ov.Internal = ov.InternalHigh = ov.Offset = ov.OffsetHigh = 0;
  ov.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
  if (WriteFile(hpipe, (LPCVOID) data, nbytes, NULL, &ov) == 0) {
    if (GetLastError() != ERROR_IO_PENDING) {
      CloseHandle(ov.hEvent);
      return -1;
    }
  }
  if (WaitForSingleObject(ov.hEvent, INFINITE) == WAIT_FAILED) {
    CloseHandle(ov.hEvent);
    return -1;
  }
  if (GetOverlappedResult(hpipe, &ov, &bytesWritten, FALSE) == 0) {
    CloseHandle(ov.hEvent);
    return -1;
  }

  CloseHandle(ov.hEvent);
  return (nbytes - bytesWritten);
}

#endif // !_WIN32

void
mgmt_blockAllSigs()
{
#if (HOST_OS != linux) && !defined(_WIN32)
  // Start by blocking all signals
  sigset_t allSigs;             // Set of all signals
  sigfillset(&allSigs);
  if (ink_thread_sigsetmask(SIG_SETMASK, &allSigs, NULL) < 0) {
    perror("ink_thread_sigsetmask");
  }
#endif
}

/*
 * mgmt_log(...)
 *   Really just a print wrapper, function takes a string and outputs the
 * result to log. Written so that we could turn off all output or at least
 * better control it.
 */
void
mgmt_log(FILE * log, const char *message_format, ...)
{
  va_list ap;
  char extended_format[4096], message[4096];

  va_start(ap, message_format);

#if defined(LOCAL_MANAGER) || defined(PROCESS_MANAGER)
  if (diags_init) {
    diags->print_va(NULL, DL_Note, "NOTE", NULL, message_format, (va_list) ap);
  } else {
#endif

    if (use_syslog) {
      snprintf(extended_format, sizeof(extended_format), "log ==> %s", message_format);
      vsprintf(message, extended_format, ap);
      syslog(LOG_WARNING, "%s", message);
    } else {
      snprintf(extended_format, sizeof(extended_format), "[E. Mgmt] log ==> %s", message_format);
      vsprintf(message, extended_format, ap);
      ink_assert(fwrite(message, strlen(message), 1, log) == 1);
    }
#if defined(LOCAL_MANAGER) || defined(PROCESS_MANAGER)
  }
#endif

  va_end(ap);
  return;
}                               /* End mgmt_log */

void
mgmt_log(const char *message_format, ...)
{
  va_list ap;
  char extended_format[4096], message[4096];

  va_start(ap, message_format);
#if defined(LOCAL_MANAGER) || defined(PROCESS_MANAGER)
  if (diags_init) {
    diags->print_va(NULL, DL_Note, "NOTE", NULL, message_format, ap);
  } else {
#endif

    if (use_syslog) {
      snprintf(extended_format, sizeof(extended_format), "log ==> %s", message_format);
      vsprintf(message, extended_format, ap);
      syslog(LOG_WARNING, "%s", message);
    } else {
      snprintf(extended_format, sizeof(extended_format), "[E. Mgmt] log ==> %s", message_format);
      vsprintf(message, extended_format, ap);
      ink_assert(fwrite(message, strlen(message), 1, stderr) == 1);
    }
#if defined(LOCAL_MANAGER) || defined(PROCESS_MANAGER)
  }
#endif

  va_end(ap);
  return;
}                               /* End mgmt_log */


/*
 * mgmt_log(...)
 *   Same as above, but intended for errors. 
 */
void
mgmt_elog(FILE * log, const char *message_format, ...)
{
  va_list ap;
  char extended_format[4096], message[4096];

  va_start(ap, message_format);

#if defined(LOCAL_MANAGER) || defined(PROCESS_MANAGER)
  if (diags_init) {
    int lerrno = errno;
    diags->print_va(NULL, DL_Error, "ERROR", NULL, message_format, ap);
    snprintf(message, sizeof(message), " (last system error %d: %s)\n", lerrno, strerror(lerrno));
    diags->print(NULL, DL_Error, "ERROR", NULL, message);
  } else {
#endif
    if (use_syslog) {
      snprintf(extended_format, sizeof(extended_format), "ERROR ==> %s", message_format);
      vsprintf(message, extended_format, ap);
#if !defined (_WIN32)
      syslog(LOG_ERR, "%s", message);
      syslog(LOG_ERR, " (last system error %d: %s)", errno, strerror(errno));
#endif
    } else {
      snprintf(extended_format, sizeof(extended_format), "[E. Mgmt] ERROR ==> %s", message_format);
      vsprintf(message, extended_format, ap);
      ink_assert(fwrite(message, strlen(message), 1, log) == 1);
      snprintf(message, sizeof(message), "(last system error %d: %s)", errno, strerror(errno));
      ink_assert(fwrite(message, strlen(message), 1, log) == 1);
    }
#if defined(LOCAL_MANAGER) || defined(PROCESS_MANAGER)
  }
#endif
  va_end(ap);

  return;
}                               /* End mgmt_elog */


void
mgmt_elog(const char *message_format, ...)
{
  va_list ap;
  char extended_format[4096], message[4096];

  va_start(ap, message_format);

#if defined(LOCAL_MANAGER) || defined(PROCESS_MANAGER)
  if (diags_init) {
    int lerrno = errno;
    diags->print_va(NULL, DL_Error, "ERROR", NULL, message_format, ap);
    snprintf(message, sizeof(message), " (last system error %d: %s)\n", lerrno, strerror(lerrno));
    diags->print(NULL, DL_Error, "ERROR", NULL, message);
  } else {
#endif

    if (use_syslog) {
      snprintf(extended_format, sizeof(extended_format), "ERROR ==> %s", message_format);
      vsprintf(message, extended_format, ap);
      syslog(LOG_ERR, "%s", message);
#if !defined (_WIN32)
      syslog(LOG_ERR, " (last system error %d: %s)", errno, strerror(errno));
#endif
    } else {
      snprintf(extended_format, sizeof(extended_format), "Manager ERROR: %s", message_format);
      vsprintf(message, extended_format, ap);
      ink_assert(fwrite(message, strlen(message), 1, stderr) == 1);
      snprintf(message, sizeof(message), "(last system error %d: %s)", errno, strerror(errno));
      ink_assert(fwrite(message, strlen(message), 1, stderr) == 1);
    }
#if defined(LOCAL_MANAGER) || defined(PROCESS_MANAGER)
  }
#endif
  va_end(ap);
  return;
}                               /* End mgmt_elog */


/*
 * mgmt_fatal(...)
 *   Same as above, but for fatal errors. Logs error, calls perror, and
 * asserts false.
 */
void
mgmt_fatal(FILE * log, const char *message_format, ...)
{
  va_list ap;
  char extended_format[4096], message[4096];

  va_start(ap, message_format);


#if defined(LOCAL_MANAGER) || defined(PROCESS_MANAGER)
  if (diags_init) {
    int lerrno = errno;
    diags->print_va(NULL, DL_Fatal, "FATAL", NULL, message_format, ap);
    snprintf(message, sizeof(message), " (last system error %d: %s)\n", lerrno, strerror(lerrno));
    diags->print(NULL, DL_Fatal, "FATAL", NULL, message);
  } else {
#endif

    snprintf(extended_format, sizeof(extended_format), "FATAL ==> %s", message_format);
    vsprintf(message, extended_format, ap);

    ink_assert(fwrite(message, strlen(message), 1, log) == 1);

    if (use_syslog) {
      syslog(LOG_ERR, "%s", message);
    }



    perror("[E. Mgmt] ");

    if (use_syslog) {
#if !defined (_WIN32)
      syslog(LOG_ERR, " (last system error %d: %s)", errno, strerror(errno));
#endif
    }
#if defined(LOCAL_MANAGER) || defined(PROCESS_MANAGER)
  }
#endif

  va_end(ap);

  mgmt_cleanup();
  _exit(1);
}                               /* End mgmt_fatal */



void
mgmt_fatal(const char *message_format, ...)
{
  va_list ap;
  char extended_format[4096], message[4096];

  va_start(ap, message_format);

#if defined(LOCAL_MANAGER) || defined(PROCESS_MANAGER)
  if (diags_init) {
    int lerrno = errno;
    diags->print_va(NULL, DL_Fatal, "FATAL", NULL, message_format, ap);
    snprintf(message, sizeof(message), " (last system error %d: %s)\n", lerrno, strerror(lerrno));
    diags->print(NULL, DL_Fatal, "FATAL", NULL, message);
  } else {
#endif

    snprintf(extended_format, sizeof(extended_format), "FATAL ==> %s", message_format);
    vsprintf(message, extended_format, ap);

    ink_assert(fwrite(message, strlen(message), 1, stderr) == 1);

    if (use_syslog) {
      syslog(LOG_ERR, "%s", message);
    }



    perror("[E. Mgmt] ");

    if (use_syslog) {
#if !defined (_WIN32)
      syslog(LOG_ERR, " (last system error %d: %s)", errno, strerror(errno));
#endif
    }
#if defined(LOCAL_MANAGER) || defined(PROCESS_MANAGER)
  }
#endif

  va_end(ap);

  mgmt_cleanup();
  _exit(1);
}                               /* End mgmt_fatal */

void
mgmt_cleanup()
{
#if defined(LOCAL_MANAGER)
  if (lmgmt != NULL) {
    lmgmt->mgmtShutdown(1, true);
  }
#endif
}

#ifndef _WIN32
static inline int
get_interface_mtu(int sock_fd, struct ifreq *ifr)
{
  if (ioctl(sock_fd, SIOCGIFMTU, ifr) < 0) {
    mgmt_log(stderr, "[getAddrForIntr] Unable to obtain MTU for " "interface '%s'", ifr->ifr_name);
    return 0;
  } else
#if (HOST_OS == sunos) || (HOST_OS == hpux)
    return ifr->ifr_metric;
#else
    return ifr->ifr_mtu;
#endif
}
#endif

bool
mgmt_getAddrForIntr(char *intrName, struct in_addr * addr, int *mtu)
{
  bool found = false;

  if (intrName == NULL) {
    return false;
  }
#ifndef _WIN32

  int fakeSocket;               // a temporary socket to pass to ioctl
  struct sockaddr_in *tmp;      // a tmp ptr for addresses
  struct ifconf ifc;            // ifconf information
  char *ifbuf;                  // ifconf buffer
  struct ifreq *ifr, *ifend;    // pointer to individual inferface info
  int lastlen;
  int len;

  // Prevent UMRs 
  memset(addr, 0, sizeof(struct in_addr));

  if ((fakeSocket = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
    mgmt_fatal(stderr, "[getAddrForIntr] Unable to create socket\n");
  }
  // INKqa06739
  // Fetch the list of network interfaces
  // . from Stevens, Unix Network Prog., pg 434-435
  ifbuf = 0;
  lastlen = 0;
  len = 128 * sizeof(struct ifreq);     // initial buffer size guess
  for (;;) {
    ifbuf = (char *) xmalloc(len);
    memset(ifbuf, 0, len);      // prevent UMRs
    ifc.ifc_len = len;
    ifc.ifc_buf = ifbuf;
    if (ioctl(fakeSocket, SIOCGIFCONF, &ifc) < 0) {
      if (errno != EINVAL || lastlen != 0) {
        mgmt_fatal(stderr, "[getAddrForIntr] Unable to read network interface configuration\n");
      }
    } else {
      if (ifc.ifc_len == lastlen) {
        break;
      }
      lastlen = ifc.ifc_len;
    }
    len *= 2;
    xfree(ifbuf);
  }

  ifr = ifc.ifc_req;
  found = false;
  // Loop through the list of interfaces
  ifend = (struct ifreq *) (ifc.ifc_buf + ifc.ifc_len);
  for (ifr = ifc.ifc_req; ifr < ifend;) {
    if (ifr->ifr_addr.sa_family == AF_INET && strcmp(ifr->ifr_name, intrName) == 0) {
      // Get the address of the interface
      if (ioctl(fakeSocket, SIOCGIFADDR, (char *) ifr) < 0) {
        mgmt_log(stderr, "[getAddrForIntr] Unable obtain address for network interface %s\n", intrName);
      } else {
        // Only look at the address if it an internet address
        if (ifr->ifr_ifru.ifru_addr.sa_family == AF_INET) {
          tmp = (struct sockaddr_in *) &ifr->ifr_ifru.ifru_addr;
          *addr = tmp->sin_addr;
          found = true;

          if (mtu)
            *mtu = get_interface_mtu(fakeSocket, ifr);

          break;
        } else {
          mgmt_log(stderr, "[getAddrForIntr] Interface %s is not configured for IP.\n", intrName);
        }
      }
    }
#if (HOST_OS == freebsd)
    ifr = (struct ifreq *) ((char *) &ifr->ifr_addr + ifr->ifr_addr.sa_len);
#else
    ifr = (struct ifreq *) (((char *) ifr) + sizeof(*ifr));
#endif
  }
  xfree(ifbuf);
  close(fakeSocket);

#else

  // There is no notion of network interface names on NT.
  // So we use a winnt_intr.config file to give each of
  // the interface a name.

  char intr_file[PATH_MAX + 1];
  FILE *fp = NULL;

#ifdef LOCAL_MANAGER
  snprintf(intr_file, sizeof(intr_file), "%s%sconfig%s%s", ts_base_dir, DIR_SEP, DIR_SEP, "winnt_intr.config");
#else
  snprintf(intr_file, sizeof(intr_file), "%s%sconfig%s%s", system_base_install, DIR_SEP, DIR_SEP, "winnt_intr.config");
#endif

  if ((fp = fopen(intr_file, "r")) == NULL) {
    mgmt_log(stderr, "[getAddrForIntr] Unable to open %s\n", intr_file);
  } else {
    char buffer[1024];
    char *p = NULL;

    while (!feof(fp)) {

      if (!fgets(buffer, 1024, fp)) {
        break;
      }
      // replace newline with null termination
      p = strchr(buffer, '\n');
      if (p) {
        *p = '\0';
      }
      // skip beginning line spaces
      p = buffer;
      while (*p != '\0' && isspace(*p)) {
        p++;
      }

      // skip blank or comment lines
      if (*p == '#' || *p == '\0') {
        continue;
      }

      if (p = strstr(p, intrName)) {
        p = p + strlen(intrName) + 1;
        in_addr_t cluster_ip = inet_addr(p);

        if (cluster_ip != INADDR_NONE) {
          addr->s_addr = cluster_ip;
          found = true;
          break;
        }
      }

    }
    fclose(fp);
  }

#endif // !_WIN32

  return found;
}                               /* End mgmt_getAddrForIntr */

/*
 * mgmt_sortipaddrs(...)
 *   Routine to sort and pick smallest ip addr.
 */
struct in_addr *
mgmt_sortipaddrs(int num, struct in_addr **list)
{
  int i = 0;
  unsigned long min;
  struct in_addr *entry, *tmp;

  min = (list[0])->s_addr;
  entry = list[0];
  while (i < num && (tmp = (struct in_addr *) list[i]) != NULL) {
    i++;
    if (min > tmp->s_addr) {
      min = tmp->s_addr;
      entry = tmp;
    }
  }
  return entry;
}                               /* End mgmt_sortipaddrs */

char *
mgmt_localhost_ip()
{
#if defined(LOCAL_MANAGER)
  bool found;
  char *hostname;
  unsigned int ip;
  int rec_err = RecGetRecordString_Xmalloc("proxy.node.hostname_FQ", &hostname);
  found = (rec_err == REC_ERR_OKAY);
  if (found && hostname) {
    ip = host_to_ip(hostname);
    if (ip != INADDR_ANY) {
      return inet_ntoa(*(struct in_addr *) &ip);
    }
  }
#endif
  return NULL;
}

#ifndef _WIN32
void
mgmt_sleep_sec(int seconds)
{
  sleep(seconds);
}

void
mgmt_sleep_msec(int msec)
{
#if !defined (alpha) && !defined(__alpha)
  usleep(msec * 1000);
#endif
}
#else
void
mgmt_sleep_sec(int seconds)
{
  Sleep(1000 * seconds);
}

void
mgmt_sleep_msec(int msec)
{
  Sleep(msec);
}
#endif
