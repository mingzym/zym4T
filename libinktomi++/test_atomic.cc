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



////////////////////////////////////////////////////////////////////////////
// NT Build Notes:
//   1) Enable the following #include. 
//      Visual C++ does not allow this under #ifdef
// #include "stdafx.h"
//
//   2) Create a new project within the existing TS project
//      which is a "WIN32 Console Application"    
//      with the "Simple Application" option.
//   3) Replace C/C++ Preprocessor definitions with traffic_server 
//      definitions.
//   4) Replace C/C++ Additional includes with traffic server
//      definitions.  Prepend "..\proxy" to directories in proxy.
//      Add "..\proxy".
//   5) Replace C/C++ project option "/MLd" with "/MTd"
//   6) Add libinktomi++ as dependency.
////////////////////////////////////////////////////////////////////////////

#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <poll.h>
#include <pthread.h>
#include "ink_atomic.h"
#include "ink_queue.h"
#include "ink_thread.h"
#include "ink_unused.h" /* MAGIC_EDITING_TAG */


#ifndef LONG_ATOMICLIST_TEST
#if 0
InkQueue Q;
void *
testfun(InkQueue * q)
{
  int num;

  if (q == NULL) {
    for (num = 2; num < 2000; num += 2) {
      assert(ink_queue_enqueue(&Q, (void *) num));
      if (num % 200 == 0) {
        ink_queue_print(stderr, &Q);
      }
    }
    sleep(10);
    exit(0);
  } else {
    for (;;) {
      num = (int) ink_queue_try_dequeue(q);
      if (num != 0)
        printf("%d\n", num);
    }
  }
  return NULL;
}
#endif

#define MAX_ALIST_TEST 10
#define MAX_ALIST_ARRAY 100000
InkAtomicList al[MAX_ALIST_TEST];
void *al_test[MAX_ALIST_TEST][MAX_ALIST_ARRAY];
volatile int al_done = 0;

void *
testalist(void *ame)
{
  int me = (int) (long) ame;
  int j, k;
  for (k = 0; k < MAX_ALIST_ARRAY; k++)
    ink_atomiclist_push(&al[k % MAX_ALIST_TEST], &al_test[me][k]);
  void *x;
  for (j = 0; j < 1000000; j++)
    if ((x = ink_atomiclist_pop(&al[me])))
      ink_atomiclist_push(&al[rand() % MAX_ALIST_TEST], x);
  ink_atomic_increment((int *) &al_done, 1);
  return NULL;
}
#endif // !LONG_ATOMICLIST_TEST

#ifdef LONG_ATOMICLIST_TEST
/************************************************************************/
#define MAX_ATOMIC_LISTS	(4 * 1024)
#define MAX_ITEMS_PER_LIST	(1 * 1024)
#define MAX_TEST_THREADS	64
static InkAtomicList alists[MAX_ATOMIC_LISTS];
struct listItem *items[MAX_ATOMIC_LISTS * MAX_ITEMS_PER_LIST];

struct listItem
{
  int data1;
  int data2;
  void *link;
  int data3;
  int data4;
  int check;
};

void
init_data()
{
  int j;
  int ali;
  struct listItem l;
  struct listItem *plistItem;

  for (ali = 0; ali < MAX_ATOMIC_LISTS; ali++)
    ink_atomiclist_init(&alists[ali], "alist", ((char *) &l.link - (char *) &l));

  for (ali = 0; ali < MAX_ATOMIC_LISTS; ali++) {
    for (j = 0; j < MAX_ITEMS_PER_LIST; j++) {
      plistItem = (struct listItem *) malloc(sizeof(struct listItem));
      items[ali + j] = plistItem;
      plistItem->data1 = ali + j;
      plistItem->data2 = ali + rand();
      plistItem->link = 0;
      plistItem->data3 = j + rand();
      plistItem->data4 = ali + j + rand();
      plistItem->check = (plistItem->data1 ^ plistItem->data2 ^ plistItem->data3 ^ plistItem->data4);
      ink_atomiclist_push(&alists[ali], plistItem);
    }
  }
}

void
cycle_data(void *d)
{
  InkAtomicList *l;
  struct listItem *pli;
  struct listItem *pli_next;
  int iterations;
  int me;

  me = (int) d;
  iterations = 0;

  while (1) {
    l = &alists[(me + rand()) % MAX_ATOMIC_LISTS];

    pli = (struct listItem *) ink_atomiclist_popall(l);
    if (!pli)
      continue;

    // Place listItems into random queues
    while (pli) {
      assert((pli->data1 ^ pli->data2 ^ pli->data3 ^ pli->data4) == pli->check);
      pli_next = (struct listItem *) pli->link;
      pli->link = 0;
      ink_atomiclist_push(&alists[(me + rand()) % MAX_ATOMIC_LISTS], (void *) pli);
      pli = pli_next;
    }
    iterations++;
    poll(0, 0, 10);             // 10 msec delay
    if ((iterations % 100) == 0)
      printf("%d ", me);
  }
}

/************************************************************************/
#endif // LONG_ATOMICLIST_TEST

int
main(int argc, const char *argv[])
{
#ifndef LONG_ATOMICLIST_TEST
#if 0
  InkSpinlock s;
  ink_spinlock_init(&s);
  InkQueue *q;
  ink_queue_init(&Q, "test");
  q = &Q;
#endif
  ink32 m = 1, n = 100;
  //ink64 lm = 1LL, ln = 100LL;
  char *m2 = "hello", *n2;

  printf("sizeof(ink32)==%d   sizeof(void *)==%d\n", (int)sizeof(ink32), (int)sizeof(void *));

#if 0
  INK_SPINLOCK_ACQUIRE(&s);
  printf("acquired: %s, %d\n", s.locked ? "locked" : "free", s.waiting);
  ink_spinlock_print(stdout, &s);

  ink_spinlock_release(&s);
  printf("released: %s, %d\n", s.locked ? "locked" : "free", s.waiting);
  ink_spinlock_print(stdout, &s);
#endif

  printf("CAS: %d == 1  then  2\n", m);
  n = ink_atomic_cas(&m, 1, 2);
  printf("changed to: %d,  result=%s\n", m, n ? "true" : "false");

  printf("CAS: %d == 1  then  3\n", m);
  n = ink_atomic_cas(&m, 1, 3);
  printf("changed to: %d,  result=%s\n", m, n ? "true" : "false");

  printf("CAS pointer: '%s' == 'hello'  then  'new'\n", m2);
  n = ink_atomic_cas_ptr((pvvoidp) & m2, (char *) "hello", (char *) "new");
  printf("changed to: %s, result=%s\n", m2, n ? (char *) "true" : (char *) "false");

  printf("CAS pointer: '%s' == 'hello'  then  'new2'\n", m2);
  n = ink_atomic_cas_ptr((pvvoidp) & m2, m2, (char *) "new2");
  printf("changed to: %s, result=%s\n", m2, n ? "true" : "false");

  n = 100;
  printf("Atomic Inc of %d\n", n);
  m = ink_atomic_increment((int *) &n, 1);
  printf("changed to: %d,  result=%d\n", n, m);

#if 0
  printf("CAS64: %lld == 1  then  2\n", lm);
  n = ink_atomic_cas64(&lm, 1, 2);
#ifdef __alpha
  printf("changed to: %ld,  result=%s\n", lm, ln ? "true" : "false");
#else
  printf("changed to: %lld,  result=%s\n", lm, ln ? "true" : "false");
#endif

#ifdef __alpha
  printf("CAS64: %ld == 1  then  3\n", lm);
#else
  printf("CAS64: %lld == 1  then  3\n", lm);
#endif
  n = ink_atomic_cas64(&lm, 1, 3);
#ifdef __alpha
  printf("changed to: %ld,  result=%s\n", lm, n ? "true" : "false");
#else
  printf("changed to: %lld,  result=%s\n", lm, n ? "true" : "false");
#endif

  ln = 10000001000LL;
#ifdef __alpha
  printf("Atomic 64 Inc of %ld by %ld\n", ln, 10000000000100LL);
#else
  printf("Atomic 64 Inc of %lld by %lld\n", ln, 10000000000100LL);
#endif
  lm = ink_atomic_increment64((long long *) &ln, 10000000000100LL);
#ifdef __alpha
  printf("changed to: %lld,  result=%lld\n", ln, lm);
#else
  printf("changed to: %lld,  result=%lld\n", ln, lm);
#endif

#endif /* ! NO_64BIT_ATOMIC */

  printf("Atomic Fetch-and-Add 2 to pointer to '%s'\n", m2);
  n2 = (char *) ink_atomic_increment_ptr((pvvoidp) & m2, 2);
  printf("changed to: %s,  result=%s\n", m2, n2);

  printf("Testing atomic lists\n");
  {
    int ali;
    srand(time(NULL));
    printf("sizeof(al_test) = %d\n", (int)sizeof(al_test));
    memset(&al_test[0][0], 0, sizeof(al_test));
    for (ali = 0; ali < MAX_ALIST_TEST; ali++)
      ink_atomiclist_init(&al[ali], "foo", 0);
    for (ali = 0; ali < MAX_ALIST_TEST; ali++) {
      ink_thread tid;
      pthread_attr_t attr;

      pthread_attr_init(&attr);
#if (HOST_OS != freebsd)
      pthread_attr_setstacksize(&attr, 1024 * 1024);
#endif
      ink_assert(pthread_create(&tid, &attr, testalist, (void *) ali) == 0);
    }
    while (al_done != MAX_ALIST_TEST)
      sleep(1);
  }
#endif // !LONG_ATOMICLIST_TEST

#ifdef LONG_ATOMICLIST_TEST
  printf("Testing atomic lists (long version)\n");
  {
    int id;

    init_data();
    for (id = 0; id < MAX_TEST_THREADS; id++) {
      assert(thr_create(NULL, 0, cycle_data, (void *) id, THR_NEW_LWP, NULL) == 0);
    }
  }
  while (1) {
    poll(0, 0, 10);             // 10 msec delay
  }
#endif // LONG_ATOMICLIST_TEST

#if 0
  assert(thr_create(NULL, 0, testfun, 0, THR_NEW_LWP, NULL) == 0);
  assert(thr_create(NULL, 0, testfun, 0, THR_NEW_LWP, NULL) == 0);
  assert(thr_create(NULL, 0, testfun, q, THR_NEW_LWP, NULL) == 0);
  assert(thr_create(NULL, 0, testfun, q, THR_NEW_LWP, NULL) == 0);
  assert(thr_create(NULL, 0, testfun, q, THR_NEW_LWP, NULL) == 0);
  assert(thr_create(NULL, 0, testfun, q, THR_NEW_LWP, NULL) == 0);
  assert(thr_create(NULL, 0, testfun, q, THR_NEW_LWP, NULL) == 0);
  assert(thr_create(NULL, 0, testfun, q, THR_NEW_LWP, NULL) == 0);
  assert(thr_create(NULL, 0, testfun, q, THR_NEW_LWP, NULL) == 0);
  assert(thr_create(NULL, 0, testfun, q, THR_NEW_LWP, NULL) == 0);
  while (1) {
    ;
  }
#endif
  return 0;
}
