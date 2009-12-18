/** @file

  Implementation of a simple linked list queue

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

#include <stdio.h>
#include <stdlib.h>
#if (HOST_OS != freebsd) && (HOST_OS != darwin)
#include <malloc.h>
#endif
#include <assert.h>
#include <limits.h>
#include "ink_unused.h" /* MAGIC_EDITING_TAG */

#include "llqueue.h"
#include "errno.h"

#define RECORD_CHUNK 1024

// These are obviously not used anywhere, I don't know if or how they
// were supposed to work, but #ifdef'ing them out of here for now.
#ifdef NOT_USED_HERE
static LLQrec *
newrec(LLQ * Q)
{
  LLQrec * new_val;
  int i;

  if (Q->free != NULL) {
    new_val = Q->free;
    Q->free = Q->free->next;
    return new_val;
  }


  Q->free = (LLQrec *) xmalloc(RECORD_CHUNK * sizeof(LLQrec));

  if (!Q->free)
    return NULL;

  for (i = 0; i < RECORD_CHUNK; i++)
    Q->free[i].next = &Q->free[i + 1];

  Q->free[RECORD_CHUNK - 1].next = NULL;


  new_val = Q->free;
  Q->free = Q->free->next;

  return new_val;
}

// Not used either ...
static void
freerec(LLQ * Q, LLQrec * rec)
{
  rec->next = Q->free;
  Q->free = rec;
}
#endif

LLQ *
create_queue()
{
  const char *totally_bogus_name = "create_queue";
  LLQ * new_val;

  new_val = (LLQ *) xmalloc(sizeof(LLQ));
  if (!new_val)
    return NULL;

#if (HOST_OS == darwin)
  static int qnum = 0;
  char sname[NAME_MAX];
  qnum++;
  snprintf(sname,NAME_MAX,"%s%d",totally_bogus_name,qnum);
  ink_sem_unlink(sname); // FIXME: remove, semaphore should be properly deleted after usage
  new_val->sema = ink_sem_open(sname, O_CREAT | O_EXCL, 0777, 0);
#else /* !darwin */
  ink_sem_init(&(new_val->sema), 0);
#endif /* !darwin */
  ink_mutex_init(&(new_val->mux), totally_bogus_name);

  new_val->head = new_val->tail = new_val->free = NULL;
  new_val->len = new_val->highwater = 0;

  return new_val;
}

// matching delete function, only for empty queue!
void
delete_queue(LLQ * Q)
{
  // There seems to have been some ideas of making sure that this queue is
  // actually empty ...
  //
  //    LLQrec * qrec;
  if (Q) {
    xfree(Q);
  }
  return;
}

int
enqueue(LLQ * Q, void *data)
{
  LLQrec * new_val;

  ink_mutex_acquire(&(Q->mux));

//new_val= newrec(Q);
  new_val = (LLQrec *) xmalloc(sizeof(LLQrec));

  if (!new_val) {
    ink_mutex_release(&(Q->mux));
    return 0;
  }

  new_val->data = data;
  new_val->next = NULL;

  if (Q->tail)
    Q->tail->next = new_val;
  Q->tail = new_val;

  if (Q->head == NULL)
    Q->head = Q->tail;

  Q->len++;
  if (Q->len > Q->highwater)
    Q->highwater = Q->len;
  ink_mutex_release(&(Q->mux));
#if (HOST_OS == darwin)
  ink_sem_post(Q->sema);
#else
  ink_sem_post(&(Q->sema));
#endif
  return 1;
}

unsigned long
queue_len(LLQ * Q)
{
  unsigned long len;

  /* Do I really need to grab the lock here? */
  /* ink_mutex_acquire(&(Q->mux)); */
  len = Q->len;
  /* ink_mutex_release(&(Q->mux)); */
  return len;
}

unsigned long
queue_highwater(LLQ * Q)
{
  unsigned long highwater;

  /* Do I really need to grab the lock here? */
  /* ink_mutex_acquire(&(Q->mux)); */
  highwater = Q->highwater;
  /* ink_mutex_release(&(Q->mux)); */
  return highwater;
}



/* 
 *---------------------------------------------------------------------------
 *
 * queue_is_empty
 *  
 *  Is the queue empty?
 *  
 * Results:
 *  nonzero if empty, zero else.
 *  
 * Side Effects:
 *  none.
 *  
 * Reentrancy:     n/a.
 * Thread Safety:  safe.
 * Mem Management: n/a.
 *  
 *---------------------------------------------------------------------------
 */
int
queue_is_empty(LLQ * Q)
{
  unsigned long len;

  len = queue_len(Q);

  if (len)
    return 0;
  else
    return 1;
}

void *
dequeue(LLQ * Q)
{
  LLQrec * rec;
  void *d;
#if (HOST_OS == darwin)
  ink_sem_wait(Q->sema);
#else
  ink_sem_wait(&(Q->sema));
#endif
  ink_mutex_acquire(&(Q->mux));


  if (Q->head == NULL) {

    ink_mutex_release(&(Q->mux));

    return NULL;
  }

  rec = Q->head;

  Q->head = Q->head->next;
  if (Q->head == NULL)
    Q->tail = NULL;

  d = rec->data;
//freerec(Q, rec);
  xfree(rec);

  Q->len--;
  ink_mutex_release(&(Q->mux));

  return d;
}



#ifdef LLQUEUE_MAIN

void *
testfun(void *unused)
{
  int num;
  LLQ *Q;

  Q = create_queue();
  assert(Q);

  do {
    scanf("%d", &num);
    if (num == 0) {
      printf("DEQUEUE: %d\n", (int) dequeue(Q));
    } else if (num == -1) {
      printf("queue_is_empty: %d\n", queue_is_empty(Q));
    } else {
      printf("enqueue: %d\n", num);
      enqueue(Q, (void *) num);
    }
  } while (num >= -1);

  return NULL;
}

/*
 * test harness-- hit Ctrl-C if it blocks or you get tired.
 */
void
main()
{
  assert(thr_create(NULL, 0, testfun, (void *) NULL, THR_NEW_LWP, NULL) == 0);
  while (1) {
    ;
  }
}

#endif
