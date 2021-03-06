/*
 * Copyright (c) 1983, 1987, 1989
 *    The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *      This product includes software developed by the University of
 *      California, Berkeley and its contributors.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

/*
 * Copyright (c) 2004 by Internet Systems Consortium, Inc. ("ISC")
 * Portions Copyright (c) 1996-1999 by Internet Software Consortium.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND ISC DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS.  IN NO EVENT SHALL ISC BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT
 * OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
  Imported from Bind-9.5.2-P2

  Changes:

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

#ifndef _ink_resolver_h_
#define	_ink_resolver_h_

#include "ink_platform.h"
#include <resolv.h>
#include <arpa/nameser.h>
#ifdef HAVE_NET_PPP_DEFS_H
#include <net/ppp_defs.h>
#endif

#define INK_RES_F_VC        0x00000001      /*%< socket is TCP */
#define INK_RES_F_CONN      0x00000002      /*%< socket is connected */
#define INK_RES_F_EDNS0ERR  0x00000004      /*%< EDNS0 caused errors */
#define INK_RES_F__UNUSED   0x00000008      /*%< (unused) */
#define INK_RES_F_LASTMASK  0x000000F0      /*%< ordinal server of last res_nsend */
#define INK_RES_F_LASTSHIFT 4               /*%< bit position of LASTMASK "flag" */
#define INK_RES_GETLAST(res) (((res)._flags & INK_RES_F_LASTMASK) >> INK_RES_F_LASTSHIFT)

/* res_findzonecut2() options */
#define INK_RES_EXHAUSTIVE  0x00000001      /*%< always do all queries */
#define INK_RES_IPV4ONLY    0x00000002      /*%< IPv4 only */
#define INK_RES_IPV6ONLY    0x00000004      /*%< IPv6 only */

/*%
 *  * Resolver options (keep these in synch with res_debug.c, please)
 *   */
#define INK_RES_INIT        0x00000001      /*%< address initialized */
#define INK_RES_DEBUG       0x00000002      /*%< print debug messages */
#define INK_RES_AAONLY      0x00000004      /*%< authoritative answers only (!IMPL)*/
#define INK_RES_USEVC       0x00000008      /*%< use virtual circuit */
#define INK_RES_PRIMARY     0x00000010      /*%< query primary server only (!IMPL) */
#define INK_RES_IGNTC       0x00000020      /*%< ignore trucation errors */
#define INK_RES_RECURSE     0x00000040      /*%< recursion desired */
#define INK_RES_DEFNAMES    0x00000080      /*%< use default domain name */
#define INK_RES_STAYOPEN    0x00000100      /*%< Keep TCP socket open */
#define INK_RES_DNSRCH      0x00000200      /*%< search up local domain tree */
#define INK_RES_INSECURE1   0x00000400      /*%< type 1 security disabled */
#define INK_RES_INSECURE2   0x00000800      /*%< type 2 security disabled */
#define INK_RES_NOALIASES   0x00001000      /*%< shuts off HOSTALIASES feature */
#define INK_RES_USE_INET6   0x00002000      /*%< use/map IPv6 in gethostbyname() */
#define INK_RES_ROTATE      0x00004000      /*%< rotate ns list after each query */
#define INK_RES_NOCHECKNAME 0x00008000      /*%< do not check names for sanity. */
#define INK_RES_KEEPTSIG    0x00010000      /*%< do not strip TSIG records */
#define INK_RES_BLAST       0x00020000      /*%< blast all recursive servers */
#define INK_RES_NSID        0x00040000      /*%< request name server ID */
#define INK_RES_NOTLDQUERY  0x00100000      /*%< don't unqualified name as a tld */
#define INK_RES_USE_DNSSEC  0x00200000      /*%< use DNSSEC using OK bit in OPT */
/* #define INK_RES_DEBUG2   0x00400000 */   /* nslookup internal */
/* KAME extensions: use higher bit to avoid conflict with ISC use */
#define INK_RES_USE_DNAME   0x10000000      /*%< use DNAME */
#define INK_RES_USE_EDNS0   0x40000000      /*%< use EDNS0 if configured */
#define INK_RES_NO_NIBBLE2  0x80000000      /*%< disable alternate nibble lookup */

#define INK_RES_DEFAULT     (INK_RES_RECURSE | INK_RES_DEFNAMES | \
                         INK_RES_DNSRCH | INK_RES_NO_NIBBLE2)

#define INK_MAXNS                   32      /*%< max # name servers we'll track */
#define INK_MAXDFLSRCH              3       /*%< # default domain levels to try */
#define INK_MAXDNSRCH               6       /*%< max # domains in search path */
#define INK_LOCALDOMAINPARTS        2       /*%< min levels in name that is "local" */
#define INK_RES_TIMEOUT             5       /*%< min. seconds between retries */
#define INK_MAXRESOLVSORT           10      /*%< number of net to sort on */
#define INK_RES_TIMEOUT             5       /*%< min. seconds between retries */
#define INK_RES_MAXNDOTS            15      /*%< should reflect bit field size */
#define INK_RES_MAXRETRANS          30      /*%< only for resolv.conf/RES_OPTIONS */
#define INK_RES_MAXRETRY            5       /*%< only for resolv.conf/RES_OPTIONS */
#define INK_RES_DFLRETRY            2       /*%< Default #/tries. */
#define INK_RES_MAXTIME             65535   /*%< Infinity, in milliseconds. */

#define INK_NS_TYPE_ELT  0x40 /*%< EDNS0 extended label type */
#define INK_DNS_LABELTYPE_BITSTRING 0x41

#ifndef NS_GET16
#define NS_GET16(s, cp) do { \
        register const u_char *t_cp = (const u_char *)(cp); \
        (s) = ((u_int16_t)t_cp[0] << 8) \
            | ((u_int16_t)t_cp[1]) \
            ; \
        (cp) += NS_INT16SZ; \
} while (0)
#endif

#ifndef NS_GET32
#define NS_GET32(l, cp) do { \
        register const u_char *t_cp = (const u_char *)(cp); \
        (l) = ((u_int32_t)t_cp[0] << 24) \
            | ((u_int32_t)t_cp[1] << 16) \
            | ((u_int32_t)t_cp[2] << 8) \
            | ((u_int32_t)t_cp[3]) \
            ; \
        (cp) += NS_INT32SZ; \
} while (0)
#endif

#ifndef NS_PUT16
#define NS_PUT16(s, cp) do { \
        register u_int16_t t_s = (u_int16_t)(s); \
        register u_char *t_cp = (u_char *)(cp); \
        *t_cp++ = t_s >> 8; \
        *t_cp   = t_s; \
        (cp) += NS_INT16SZ; \
} while (0)
#endif

#ifndef NS_PUT32
#define NS_PUT32(l, cp) do { \
        register u_int32_t t_l = (u_int32_t)(l); \
        register u_char *t_cp = (u_char *)(cp); \
        *t_cp++ = t_l >> 24; \
        *t_cp++ = t_l >> 16; \
        *t_cp++ = t_l >> 8; \
        *t_cp   = t_l; \
        (cp) += NS_INT32SZ; \
} while (0)
#endif

struct __ink_res_state {
  int     retrans;                /*%< retransmission time interval */
  int     retry;                  /*%< number of times to retransmit */
#ifdef sun
  u_int   options;                /*%< option flags - see below. */
#else
  u_long  options;                /*%< option flags - see below. */
#endif
  int     nscount;                /*%< number of name servers */
  struct sockaddr_in
  nsaddr_list[INK_MAXNS];     /*%< address of name server */
#define nsaddr  nsaddr_list[0]          /*%< for backward compatibility */
  u_short id;                     /*%< current message id */
  char    *dnsrch[MAXDNSRCH+1];   /*%< components of domain to search */
  char    defdname[256];          /*%< default domain (deprecated) */
#ifdef sun
  u_int   pfcode;                 /*%< RES_PRF_ flags - see below. */
#else
  u_long  pfcode;                 /*%< RES_PRF_ flags - see below. */
#endif
  unsigned ndots:4;               /*%< threshold for initial abs. query */
  unsigned nsort:4;               /*%< number of elements in sort_list[] */
  char    unused[3];
  struct {
    struct in_addr  addr;
    u_int32_t       mask;
  } sort_list[MAXRESOLVSORT];
  res_send_qhook qhook;           /*%< query hook */
  res_send_rhook rhook;           /*%< response hook */
  int     res_h_errno;            /*%< last one set for this context */
  int     _vcsock;                /*%< PRIVATE: for res_send VC i/o */
  u_int   _flags;                 /*%< PRIVATE: see below */
  u_int   _pad;                   /*%< make _u 64 bit aligned */
  union {
    /* On an 32-bit arch this means 512b total. */
    char    pad[72 - 4*sizeof (int) - 2*sizeof (void *)];
    struct {
      u_int16_t               nscount;
      u_int16_t               nstimes[INK_MAXNS]; /*%< ms. */
      int                     nssocks[INK_MAXNS];
      struct __ink_res_state_ext *ext;    /*%< extention for IPv6 */
    } _ext;
  } _u;
};
typedef __ink_res_state *ink_res_state;

union ink_res_sockaddr_union {
        struct sockaddr_in      sin;
#ifdef IN6ADDR_ANY_INIT
        struct sockaddr_in6     sin6;
#endif
#ifdef ISC_ALIGN64
        int64_t                 __align64;      /*%< 64bit alignment */
#else
        int32_t                 __align32;      /*%< 32bit alignment */
#endif
        char                    __space[128];   /*%< max size */
};

struct __ink_res_state_ext {
        union ink_res_sockaddr_union nsaddrs[INK_MAXNS];
        struct sort_list {
                int     af;
                union {
                        struct in_addr  ina;
                        struct in6_addr in6a;
                } addr, mask;
        } sort_list[MAXRESOLVSORT];
        char nsuffix[64];
        char nsuffix2[64];
};


int ink_res_init(ink_res_state, unsigned long *pHostList,
                 int *pPort = 0, char *pDefDomain = 0, char *pSearchList = 0);
int ink_res_mkquery(ink_res_state, int, const char *, int, int,
                    const unsigned char *, int, const unsigned char *, unsigned char *, int);

#if (HOST_OS != linux)
int inet_aton(register const char *cp, struct in_addr *addr);
#endif

int ink_ns_name_ntop(const u_char *src, char *dst, size_t dstsiz);


#endif                          /* _ink_resolver_h_ */
