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
#ifndef _TRIE_H
#define _TRIE_H

#include <list>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

template<typename T>
class Trie
{
public:
  Trie() { m_root.Clear(); }

  // will return false for duplicates; key should be NULL-terminated
  // if key_len is defaulted to -1
  bool Insert(const char *key, const T &value, int rank, int key_len = -1);

  // will return false if not found; else value_ptr will point to found value
  bool Search(const char *key, T *&value_ptr, int key_len = -1);

  void Clear();

  typedef std::list<T*> ValuePointerList;
  const ValuePointerList &GetValues() const { return m_value_list; }

  virtual ~Trie() { Clear(); };
  

private:
  static const int N_NODE_CHILDREN = 256;

  struct Node 
  {
    T value;
    bool occupied;
    int rank;
    Node *children[N_NODE_CHILDREN];
    void Clear() {
      occupied = false;
      rank = 0;
      bzero(children, sizeof(Node *) * N_NODE_CHILDREN);
    };
    void Print(const char *debug_tag) const;
  };

  Node m_root;
  ValuePointerList m_value_list;
  
  void _CheckArgs(const char *key, int &key_len) const;
  void _Clear(Node *node);

  // make copy-constructor and assignment operator private
  // till we properly implement them
  Trie(const Trie<T> &rhs) { };
  Trie &operator =(const Trie<T> &rhs) { return *this; }
};

template<typename T>
void
Trie<T>::_CheckArgs(const char *key, int &key_len) const
{
  if (!key) {
    key_len = 0;
  } 
  else if (key_len == -1) {
    key_len = strlen(key);
  }
}

template<typename T> 
bool 
Trie<T>::Insert(const char *key, const T &value, int rank, int key_len /* = -1 */) 
{
  _CheckArgs(key, key_len);

  bool retval = false;
  Node *next_node;
  Node *curr_node = &m_root;
  int i = 0;

  while (true) {
    if (is_debug_tag_set("Trie::Insert")) {
      Debug("Trie::Insert", "Visiting Node...");
      curr_node->Print("Trie::Insert");
    }
    if (i == key_len) {
      break;
    }
    next_node = curr_node->children[key[i]];
    if (!next_node) {
      while (i < key_len) {
        Debug("Trie::Insert", "Creating child node for char %c (%d)", key[i], key[i]);
        curr_node->children[key[i]] = static_cast<Node*>(ink_malloc(sizeof(Node)));
        curr_node = curr_node->children[key[i]];
        curr_node->Clear();
        ++i;
      }
      break;
    }
    curr_node = next_node;
    ++i;
  }

  if (curr_node->occupied) {
    Error("Cannot insert duplicate!");
  }
  else {
    curr_node->occupied = true;
    curr_node->value = value;
    curr_node->rank = rank;
    m_value_list.push_back(&(curr_node->value));
    retval = true;
    Debug("Trie::Insert", "inserted new element!");
  }
  return retval;
}
  
template<typename T>
bool
Trie<T>::Search(const char *key, T *&value_ptr, int key_len /* = -1 */)
{
  _CheckArgs(key, key_len);
  
  Node *found_node = 0;
  Node *curr_node = &m_root;
  int i = 0;

  while (curr_node) {
    if (is_debug_tag_set("Trie::Search")) {
      Debug("Trie::Search", "Visiting node...");
      curr_node->Print("Trie::Search");
    }
    if (curr_node->occupied) {
      if (!found_node) {
        found_node = curr_node;
      }
      else {
        if (curr_node->rank < found_node->rank) {
          found_node = curr_node;
        }
      }
    }
    if (i == key_len) {
      break;
    }
    curr_node = curr_node->children[key[i]];
    ++i;
  }

  if (found_node) {
    Debug("Trie::Search", "Returning element with rank %d", found_node->rank);
    value_ptr = &(found_node->value);
    return true;
  }
  return false;
}

template<typename T>
void
Trie<T>::_Clear(Node *node) 
{
  for (int i = 0; i < N_NODE_CHILDREN; ++i) {
    if (node->children[i]) {
      _Clear(node->children[i]);
      ink_free(node->children[i]);
    }
  }
}

template<typename T>
void
Trie<T>::Clear()
{
  _Clear(&m_root);
  m_root.Clear();
  m_value_list.clear();
}

template<typename T>
void
Trie<T>::Node::Print(const char *debug_tag) const
{
  if (occupied) {
    Debug(debug_tag, "Node is occupied");
    Debug(debug_tag, "Node has rank %d", rank);
  } else {
    Debug(debug_tag, "Node is not occupied");
  }
  for (int i = 0; i < N_NODE_CHILDREN; ++i) {
    if (children[i]) {
      Debug(debug_tag, "Node has child for char %c", static_cast<char>(i));
    }
  }
}

#endif // _TRIE_H
