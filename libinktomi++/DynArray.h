/** @file

  Dynamic Array Implementation used by Regex.cc

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

#ifndef __DYN_ARRAY_H__
#define __DYN_ARRAY_H__


#include "Resource.h"


template<class T> class DynArray {
public:
  DynArray(const T * val, int initial_size = 0);
  ~DynArray();

#ifndef __GNUC__
  operator  const T *() const;
#endif
  operator  T *();
  T & operator[](int idx);
  T & operator()(int idx);
  T *detach();
  T defvalue() const;
  int length();
  void clear();
  void set_length(int i);

private:
  void resize(int new_size);

private:
  T * data;
  const T *default_val;
  int size;
  int pos;
};


template<class T> inline DynArray<T>::DynArray(const T * val, int initial_size)
  :
data(NULL),
default_val(val),
size(0),
pos(-1)
{
  if (initial_size > 0) {
    int i = 1;

    while (i < initial_size)
      i <<= 1;

    resize(i);
  }
}

template<class T> inline DynArray<T>::~DynArray()
{
  if (data) {
    delete[]data;
  }
}

#ifndef __GNUC__
template<class T> inline DynArray<T>::operator  const T *()
const
{
  return
    data;
}
#endif

template <
  class
  T >
  inline
  DynArray <
  T >::operator
T * ()
{
  return data;
}

template<class T> inline T & DynArray<T>::operator [](int idx) {
  return data[idx];
}

template<class T> inline T & DynArray<T>::operator ()(int idx) {
  if (idx >= size) {
    int new_size;

    if (size == 0) {
      new_size = 64;
    } else {
      new_size = size * 2;
    }

    if (idx >= new_size) {
      new_size = idx + 1;
    }

    resize(new_size);
  }

  if (idx > pos) {
    pos = idx;
  }

  return data[idx];
}

template<class T> inline T * DynArray<T>::detach()
{
  T *d;

  d = data;
  data = NULL;

  return d;
}

template<class T> inline T DynArray<T>::defvalue() const
{
  return *default_val;
}

template<class T> inline int DynArray<T>::length()
{
  return pos + 1;
}

template<class T> inline void DynArray<T>::clear()
{
  if (data) {
    delete[]data;
    data = NULL;
  }

  size = 0;
  pos = -1;
}

template<class T> inline void DynArray<T>::set_length(int i)
{
  pos = i - 1;
}

template<class T> inline void DynArray<T>::resize(int new_size)
{
  if (new_size > size) {
    T *new_data;
    int i;

    new_data = NEW(new T[new_size]);

    for (i = 0; i < size; i++) {
      new_data[i] = data[i];
    }

    for (; i < new_size; i++) {
      new_data[i] = (T) * default_val;
    }

    if (data) {
      delete[]data;
    }
    data = new_data;
    size = new_size;
  }
}


#endif /* __DYN_ARRAY_H__ */

