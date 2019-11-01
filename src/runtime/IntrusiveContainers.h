/******************************************************************************
    Copyright (C) Martin Karsten 2015-2019

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/
#ifndef _IntrusiveContainer_h_
#define _IntrusiveContainer_h_ 1

#include "runtime/Basics.h"

template<typename T,size_t CNT=1> class SingleLink;
template<typename T,size_t CNT=1> class DoubleLink;

template<typename T,size_t NUM=0,size_t CNT=1,typename LT=SingleLink<T,CNT>> class IntrusiveStack;
template<typename T,size_t NUM=0,size_t CNT=1,typename LT=SingleLink<T,CNT>> class IntrusiveQueue;
template<typename T,size_t NUM=0,size_t CNT=1,typename LT=DoubleLink<T,CNT>> class IntrusiveRing;
template<typename T,size_t NUM=0,size_t CNT=1,typename LT=DoubleLink<T,CNT>> class IntrusiveList;
template<typename T,size_t NUM=0,size_t CNT=1,typename LT=SingleLink<T,CNT>> class IntrusiveQueueNemesis;
template<typename T,size_t NUM=0,size_t CNT=1,typename LT=SingleLink<T,CNT>,bool Blocking=false> class IntrusiveQueueStub;

template<typename T,size_t CNT> class SingleLink {
  template<typename,size_t,size_t,typename> friend class IntrusiveStack;
  template<typename,size_t,size_t,typename> friend class IntrusiveQueue;
  template<typename,size_t,size_t,typename> friend class IntrusiveQueueNemesis;
  template<typename,size_t,size_t,typename,bool> friend class IntrusiveQueueStub;
  friend class BlockingLock;
  struct {
    union {
      T* next;
      T* volatile vnext;
    };
  } link[CNT];
protected:
  SingleLink() {
#if TESTING_ENABLE_ASSERTIONS
    for (size_t i = 0; i < CNT; i++) { link[i].next = nullptr; }
#endif
  }
};

template<typename T,size_t CNT> class DoubleLink {
  template<typename,size_t,size_t,typename> friend class IntrusiveStack;
  template<typename,size_t,size_t,typename> friend class IntrusiveQueue;
  template<typename,size_t,size_t,typename> friend class IntrusiveRing;
  template<typename,size_t,size_t,typename> friend class IntrusiveList;
  template<typename,size_t,size_t,typename> friend class IntrusiveQueueNemesis;
  template<typename,size_t,size_t,typename,bool> friend class IntrusiveQueueStub;
  friend class BlockingLock;
  struct {
    union {
      T* next;
      T* volatile vnext;
    };
    T* prev;
  } link[CNT];
protected:
  DoubleLink() {
#if TESTING_ENABLE_ASSERTIONS
    for (size_t i = 0; i < CNT; i++) { link[i].next = link[i].prev = nullptr; }
#endif
  }
};

template<typename T, size_t NUM, size_t CNT, typename LT> class IntrusiveStack {
  static_assert(NUM < CNT, "NUM >= CNT");
public:
  typedef LT Link;

private:
  T* head;

public:
  IntrusiveStack() : head(nullptr) {}
  bool empty() const { return head == nullptr; }

  T*              front()       { return head; }
  const T*        front() const { return head; }

  static T*       next(      T& elem) { return elem.link[NUM].next; }
  static const T* next(const T& elem) { return elem.link[NUM].next; }
  static bool     test(const T& elem) { return elem.link[NUM].next; }

  static void     clear(T& elem) {
#if TESTING_ENABLE_ASSERTIONS
    elem.link[NUM].next = nullptr;
#endif
  }

  void push(T& first, T& last) {
    RASSERT(!test(last), FmtHex(&first));
    last.link[NUM].next = head;
    head = &first;
  }

  void push(T& elem) {
    push(elem, elem);
  }

  T* pop() {
    RASSERT(!empty(), FmtHex(this));
    T* last = head;
    head = last->link[NUM].next;
    clear(*last);
    return last;
  }

  T* pop(size_t& count) { // returns pointer to last element popped
    RASSERT(!empty(), FmtHex(this));
    T* last = head;
    for (size_t i = 1; i < count; i += 1) {
      if (last->link[NUM].next == nullptr) count = i; // breaks loop and sets count
      else last = last->link[NUM].next;
    }
    head = last->link[NUM].next;
    clear(*last);
    return last;
  }

  void transferFrom(IntrusiveStack& es, size_t& count) {
    if (es.empty()) return;
    T* first = es.front();
    T* last = es.pop(count);
    push(*first, *last);
  }
};


template<typename T, size_t NUM, size_t CNT, typename LT> class IntrusiveQueue {
  static_assert(NUM < CNT, "NUM >= CNT");
public:
  typedef LT Link;

private:
  T* head;
  T* tail;

public:
  IntrusiveQueue() : head(nullptr), tail(nullptr) {}
  bool empty() const {
    RASSERT((head == nullptr) == (tail == nullptr), FmtHex(this));
    return head == nullptr;
  }

  T*              front()       { return head; }
  const T*        front() const { return head; }
  T*              back()        { return tail; }
  const T*        back()  const { return tail; }

  static T*       next(      T& elem) { return elem.link[NUM].next; }
  static const T* next(const T& elem) { return elem.link[NUM].next; }
  static bool     test(const T& elem) { return elem.link[NUM].next; }

  static void     clear(T& elem) {
#if TESTING_ENABLE_ASSERTIONS
    elem.link[NUM].next = nullptr;
#endif
  }

  void push(T& first, T& last) {
    RASSERT(!test(last), FmtHex(&first));
    if (!head) head = &first;
    else {
      RASSERT(tail != nullptr, FmtHex(this));
      tail->link[NUM].next = &first;
    }
#if !TESTING_ENABLE_ASSERTIONS
    last.link[NUM].next = nullptr;
#endif
    tail = &last;
  }

  void push(T& elem) {
    push(elem, elem);
  }

  T* pop() {
    RASSERT(!empty(), FmtHex(this));
    T* last = head;
    head = last->link[NUM].next;
    if (tail == last) tail = nullptr;
    clear(*last);
    return last;
  }

  T* pop(size_t& count) {
    RASSERT(!empty(), FmtHex(this));
    T* last = head;
    for (size_t i = 1; i < count; i += 1) {
      if (last->link[NUM].next == nullptr) count = i; // breaks loop and sets count
      else last = last->link[NUM].next;
    }
    head = last->link[NUM].next;
    if (tail == last) tail = nullptr;
    clear(*last);
    return last;
  }

  T* popAll() {
    RASSERT(!empty(), FmtHex(this));
    T* last = tail;
    head = tail = nullptr;
    clear(*last);
    return last;
  }

  void transferFrom(IntrusiveQueue& eq, size_t& count) {
    if (eq.empty()) return;
    T* first = eq.front();
    T* last = eq.pop(count);
    push(*first, *last);
  }

  void transferAllFrom(IntrusiveQueue& eq) {
    if (eq.empty()) return;
    T* first = eq.front();
    T* last = eq.popAll();
    push(*first, *last);
  }
};

template<typename T, size_t NUM, size_t CNT, typename LT> class IntrusiveRing {
  static_assert(NUM < CNT, "NUM >= CNT");

  static void separate(T& first, T& last) {
    RASSERT(test(first), FmtHex(&first));
    RASSERT(test(last), FmtHex(&last));
    first.link[NUM].prev->link[NUM].next =  last.link[NUM].next;
     last.link[NUM].next->link[NUM].prev = first.link[NUM].prev;
  }

  static void combine_before(T& next, T& first, T&last) {
    RASSERT(test(next), FmtHex(&next));
    last.link[NUM].next = &next;
    next.link[NUM].prev->link[NUM].next = &first;
    first.link[NUM].prev = next.link[NUM].prev;
    next.link[NUM].prev = &last;
  }

  static void combine_after(T& prev, T& first, T& last) {
    RASSERT(test(prev), FmtHex(&prev));
    first.link[NUM].prev = &prev;
    prev.link[NUM].next->link[NUM].prev = &last;
    last.link[NUM].next = prev.link[NUM].next;
    prev.link[NUM].next = &first;
  }

public:
  typedef LT Link;

public:
  static T*       next(      T& elem) { return elem.link[NUM].next; }
  static const T* next(const T& elem) { return elem.link[NUM].next; }
  static T*       prev(      T& elem) { return elem.link[NUM].prev; }
  static const T* prev(const T& elem) { return elem.link[NUM].prev; }
  static bool     test(const T& elem) { return elem.link[NUM].next && elem.link[NUM].prev; }

  static void clear(T& first, T& last) {
#if TESTING_ENABLE_ASSERTIONS
    first.link[NUM].prev = last.link[NUM].next = nullptr;
#endif
  }

  static void clear(T& elem) {
    clear(elem, elem);
  }

  static void close(T& first, T& last) {
    first.link[NUM].prev = &last;
    last.link[NUM].next = &first;
  }

  static void close(T& elem) {
    close(elem, elem);
  }

  static void insert_before(T& next, T& first, T&last) {
    RASSERT(first.link[NUM].prev == nullptr, FmtHex(&first));
    RASSERT(last.link[NUM].next == nullptr, FmtHex(&last));
    combine_before(next, first, last);
  }

  static void insert_before(T& next, T& elem) {
    insert_before(next, elem, elem);
  }

  static void insert_after(T& prev, T& first, T& last) {
    RASSERT(first.link[NUM].prev == nullptr, FmtHex(&first));
    RASSERT(last.link[NUM].next == nullptr, FmtHex(&last));
    combine_after(prev, first, last);
  }

  static void insert_after(T& prev, T& elem) {
    insert_after(prev, elem, elem);
  }

  static T* remove(T& first, T& last) {
    separate(first, last);
    clear(first, last);
    return &last;
  }

  static T* remove(T& elem) {
    return remove(elem, elem);
  }

  static void join_before(T& next, T& first, T&last) {
    RASSERT(first.link[NUM].prev == &last, FmtHex(&first));
    RASSERT(last.link[NUM].next == &first, FmtHex(&last));
    combine_before(next, first, last);
  }

  static void join_before(T& next, T& elem) {
    combine_before(next, elem, elem);
  }

  static void join_after(T& prev, T& first, T&last) {
    RASSERT(first.link[NUM].prev == &last, FmtHex(&first));
    RASSERT(last.link[NUM].next == &first, FmtHex(&last));
    combine_after(prev, first, last);
  }

  static void join_after(T& prev, T& elem) {
    combine_after(prev, elem, elem);
  }

  static T* split(T& first, T& last) {
    separate(first, last);
    close(first, last);
    return &last;
  }

  static T* split(T& elem) {
    return split(elem, elem);
  }
};

// NOTE WELL: This design using '_anchorlink' and and downcasting to 'anchor'
// only works, if Link is the first class that T inherits from.
template<typename T, size_t NUM, size_t CNT, typename LT> class IntrusiveList : public IntrusiveRing<T,NUM,CNT,LT> {
public:
  typedef LT Link;

private:
  Link _anchorlink;
  T* anchor;

public:
  IntrusiveList() : anchor(static_cast<T*>(&_anchorlink)) {
    anchor->link[NUM].next = anchor->link[NUM].prev = anchor;
  }

  using IntrusiveRing<T,NUM,CNT,LT>::next;
  using IntrusiveRing<T,NUM,CNT,LT>::prev;
  using IntrusiveRing<T,NUM,CNT,LT>::clear;
  using IntrusiveRing<T,NUM,CNT,LT>::insert_before;
  using IntrusiveRing<T,NUM,CNT,LT>::insert_after;
  using IntrusiveRing<T,NUM,CNT,LT>::remove;

  T*       front()       { return anchor->link[NUM].next; }
  const T* front() const { return anchor->link[NUM].next; }
  T*       back()        { return anchor->link[NUM].prev; }
  const T* back()  const { return anchor->link[NUM].prev; }

  T*       edge()        { return anchor; }
  const T* edge()  const { return anchor; }

  bool     empty() const { return front() == edge(); }

  T* remove(T& first, size_t& count) {
    RASSERT(test(first), FmtHex(&first));
    T* last = &first;
    for (size_t i = 1; i < count; i += 1) {
      if (last->link[NUM].next == edge()) count = i; // breaks loop and sets count
      else last = last->link[NUM].next;
    }
    return remove(first, *last);
  }

  T* removeAll() { return remove(*front(), *back()); }

  void push_front(T& elem)            { insert_before(*front(), elem); }
  void push_back(T& elem)             { insert_after (*back(),  elem); }
  void splice_back(T& first, T& last) { insert_after (*back(),  first, last); }

  T* pop_front() { RASSERT(!empty(), FmtHex(this)); return remove(*front()); }
  T* pop_back()  { RASSERT(!empty(), FmtHex(this)); return remove(*back()); }

  void transferFrom(IntrusiveList& el, size_t& count) {
    if (el.empty()) return;
    T* first = el.front();
    T* last = el.remove(*first, count);
    splice_back(*first, *last);
  }

  void transferAllFrom(IntrusiveList& el) {
    if (el.empty()) return;
    T* first = el.front();
    T* last = el.removeAll();
    splice_back(*first, *last);
  }
};

// https://doi.org/10.1109/CCGRID.2006.31, similar to MCS lock
// the Nemesis queue might stall the consumer of the last element, if a producer waits before setting 'prev->vnext'
template<typename T, size_t NUM, size_t CNT, typename LT> class IntrusiveQueueNemesis {
  static_assert(NUM < CNT, "NUM >= CNT");
public:
  typedef LT Link;

private:
  T* volatile head;
  T* tail;

public:
  IntrusiveQueueNemesis(): head(nullptr), tail(nullptr) {}

  static void clear(T& elem) {
#if TESTING_ENABLE_ASSERTIONS
    elem.link[NUM].vnext = nullptr;
#endif
  }

  bool push(T& first, T& last) {
#if !TESTING_ENABLE_ASSERTIONS
    last.link[NUM].vnext = nullptr;
#endif
    // make sure previous write to 'vnext' and following write to 'head' are not reordered with this update
    T* prev = __atomic_exchange_n(&tail, &last, __ATOMIC_SEQ_CST); // swing tail to last of new element(s)
    if (prev) {
      prev->link[NUM].vnext = &first;
      return false;
    } else {
      head = &first;
      return true;
    }
  }

  bool push(T& elem) { return push(elem, elem); }

  T* peek() const { return head; }

  template<bool Peeked = false>
  T* pop() {
    if (!head) return nullptr;
    T* element = head;                                   // head will be returned
    if (element->link[NUM].vnext) {
      head = element->link[NUM].vnext;
    } else {
      head = nullptr;
      T* expected = element;
      // make sure previous write to 'head' and following write to 'vnext' are not reordered with this update
      if (!__atomic_compare_exchange_n(&tail, &expected, nullptr, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED)) {
        while (!element->link[NUM].vnext) Pause();       // producer in push()
        head = element->link[NUM].vnext;                 // set new head
      }
    }
    clear(*element);
    return element;
  }

  void transferAllFrom(IntrusiveQueue<T,NUM,CNT,LT>& eq) {
    if (eq.empty()) return;
    T* first = eq.front();
    T* last = eq.popAll();
    push(*first, *last);
  }
};

// http://doc.cat-v.org/inferno/concurrent_gc/concurrent_gc.pdf
// https://www.cs.rice.edu/~johnmc/papers/cqueues-mellor-crummey-TR229-1987.pdf
// http://www.1024cores.net/home/lock-free-algorithms/queues/intrusive-mpsc-node-based-queue
// https://github.com/samanbarghi/MPSCQ/blob/master/src/MPSCQueue.h
//
// NOTE WELL: This design using '_anchorlink' and and downcasting to 'anchor'
// only works, if Link is the first class that T inherits from.
template<typename T, size_t NUM, size_t CNT, typename LT, bool Blocking> class IntrusiveQueueStub {
  static_assert(NUM < CNT, "NUM >= CNT");
public:
  typedef LT Link;

private:
  Link _anchorlink;
  T*   stub;
  T*   head;
  T*   tail;

  // peek/pop operate in chunks of elements and re-append stub after each chunk
  // after re-appending stub, tail points to stub, if no further insertions -> empty!
  bool checkStub() {
    if (head == stub) {                                  // if current front chunk is empty
      if (Blocking) {                                    // BLOCKING:
        Link* expected = stub;                           //   check if tail also points at stub -> empty?
        Link* xchg = (Link*)(uintptr_t(expected) | 1);   //   if yes, mark queue empty
        bool empty = __atomic_compare_exchange_n((Link**)&tail, &expected, xchg, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
        if (empty) return false;                         //   queue is empty and is marked now
        if (uintptr_t(expected) & 1) return false;       //   queue is empty and was marked before
      } else {                                           // NONBLOCKING:
        if (tail == stub) return false;                  //   check if tail also points at stub -> empty?
      }
      while (!stub->link[NUM].vnext) Pause();            // producer in push()
      head = stub->link[NUM].vnext;                      // remove stub
      push(*stub);                                       // re-append stub at end
    }
    return true;
  }

public:
  IntrusiveQueueStub() : stub(static_cast<T*>(&_anchorlink)) {
    head = tail = stub->link[NUM].vnext = stub->link[NUM].prev = stub;
    if (Blocking) tail = (T*)(uintptr_t(tail) | 1);      // mark queue empty
  }

  static void clear(T& elem) {
#if TESTING_ENABLE_ASSERTIONS
    elem.link[NUM].vnext = nullptr;
#endif
  }

  bool push(T& first, T& last) {
#if !TESTING_ENABLE_ASSERTIONS
    last.link[NUM].vnext = nullptr;
#endif
    T* prev = __atomic_exchange_n((T**)&tail, &last, __ATOMIC_SEQ_CST); // swing tail to last of new element(s)
    bool empty = false;
    if (Blocking) {                                 // BLOCKING:
      empty = uintptr_t(prev) & 1;                  //   check empty marking
      prev = (T*)(uintptr_t(prev) & ~uintptr_t(1)); //   clear marking
    }
    prev->link[NUM].vnext = &first;                 // append segments to previous tail
    return empty;
  }

  bool push(T& elem) { return push(elem, elem); }

  T* peek() {
    if (!checkStub()) return nullptr;
    return head;
  }

  template<bool Peeked = false>
  T* pop() {
    if (!Peeked && !checkStub()) return nullptr;
    T* retval = head;                               // head will be returned
    while (!head->link[NUM].vnext) Pause();         // producer in push()
    head = head->link[NUM].vnext;                   // remove head
    clear(*retval);
    return retval;
  }

  bool transferAllFrom(IntrusiveQueue<T,NUM,CNT,LT>& eq) {
    if (eq.empty()) return false;
    T* first = eq.front();
    T* last = eq.popAll();
    return push(*first, *last);
  }
};

#endif /* _IntrusiveContainer_h_ */
