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

#include "ink_unused.h"    /* MAGIC_EDITING_TAG */
#include "ink_assert.h"

#include "SimpleQueue.h"

/****************************************************************************
 *
 *  SimpleQueue.cc - a thread safe queue
 *  
 * 
 ****************************************************************************/

//
// class SimpleQueue 
//
//   The queue is a doublely linked list.  The two operations
//     permitted are add a new item to the end of the queue
//     and remove the first item.
//
//   An attempt to remove the first item  when the queue is empty
//     will block until an item is available
//
//   accessLock protects head, tail and entries in the linked list
//   waitSema is used to block a thread until an item is available

SimpleQueue::SimpleQueue()
{
  ink_mutex_init(&accessLock, "SimpleQueue Lock");
  ink_sem_init(&waitSema, 0);
  head = NULL;
  tail = NULL;
}

// 
// destory the queue
//
//    Does not attempt to deallocate the entries
//      bucket data points to.
//
SimpleQueue::~SimpleQueue()
{
  SimpleQueueEntry *current;
  SimpleQueueEntry *next;
  ink_mutex_acquire(&accessLock);

  // Delete all the containers in the queue
  current = head;
  while (current != NULL) {
    next = current->next;
    delete current;
    current = next;
  }

  ink_mutex_destroy(&accessLock);
  ink_sem_destroy(&waitSema);
}

//
// SimpleQueue::dequeue()
//
//   Waits until there is something on the queue
//     and then returns the first item
//
void *
SimpleQueue::dequeue()
{
  SimpleQueueEntry *headEntry;
  void *data;

  // Wait for something to be on the queue
  ink_sem_wait(&waitSema);

  ink_mutex_acquire(&accessLock);

  ink_assert(head != NULL && tail != NULL);

  headEntry = head;
  head = head->next;

  if (head == NULL) {
    // The queue is now empty
    tail = NULL;
  } else {
    // New head element
    head->prev = NULL;
  }

  ink_mutex_release(&accessLock);

  data = headEntry->data;
  delete headEntry;

  return data;
}

//
// SimpleQueue::pop()
//
//   Waits until there is something on the queue
//     and then returns the last item
//
void *
SimpleQueue::pop()
{
  SimpleQueueEntry *tailEntry;
  void *data;

  // Wait for something to be on the queue
  ink_sem_wait(&waitSema);

  ink_mutex_acquire(&accessLock);

  ink_assert(head != NULL && tail != NULL);

  tailEntry = tail;
  tail = tail->prev;

  if (tail == NULL) {
    // The queue is now empty
    head = NULL;
  } else {
    // New trail element
    tail->next = NULL;
  }

  ink_mutex_release(&accessLock);

  data = tailEntry->data;
  delete tailEntry;

  return data;
}

//
//  SimpleQueue::enqueue(void* data)
//
//     adds new item to the tail of the list
//
void
SimpleQueue::enqueue(void *data)
{
  SimpleQueueEntry *newEntry = new SimpleQueueEntry;
  newEntry->data = data;

  ink_mutex_acquire(&accessLock);

  if (tail == NULL) {
    tail = newEntry;
    ink_assert(head == NULL);
    head = newEntry;
    newEntry->next = NULL;
    newEntry->prev = NULL;
  } else {
    newEntry->prev = tail;
    newEntry->next = NULL;
    tail->next = newEntry;
    tail = newEntry;

  }

  ink_mutex_release(&accessLock);
  ink_sem_post(&waitSema);
}

//
//  SimpleQueue::push(void* data)
//
//     adds new item to the tail of the list
//
void
SimpleQueue::push(void *data)
{
  enqueue(data);
}

// SimpleQueue::Print()
//
//   Prints out the queue.  For DEBUG only, not thread safe
//
bool
SimpleQueue::isEmpty()
{
  return (head == NULL);
}

#if 0
// SimpleQueue::Print()
//
//   Prints out the queue.  For DEBUG only, not thread safe
//
void
SimpleQueue::Print()
{
  SimpleQueueEntry *current = head;
  cout << "Simple Queue: " << this << endl;
  while (current != NULL) {
    cout << "\tdata: " << current->data << " next: " << current->next << " prev: " << current->prev << endl;
    current = current->next;
  }
  cout << "-----------------------------------------" << endl;
}
#endif
