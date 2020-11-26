/******************************************************************************
    Copyright (C) Martin Karsten 2015-2020

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
#ifndef _LockFreeQueues_h_
#define _LockFreeQueues_h_ 1

// https://doi.org/10.1145/103727.103729
// the MCS queue can be used to construct an MCS lock or the Nemesis queue
// next() might stall, if the queue contains one element and the producer of a second elements waits before setting 'prev->vnext'
template<typename Node, Node* volatile&(*Next)(Node&)>
class QueueMCS {
  Node* volatile tail;
public:
  QueueMCS() : tail(nullptr) {}
  bool empty() const { return tail == nullptr; }

  static void clear(Node& elem) {
#if TESTING_ENABLE_ASSERTIONS
    Next(elem) = nullptr;
#endif
  }

  bool tryPushEmpty(Node& first, Node& last) {
    if (!empty()) return false;
#if !TESTING_ENABLE_ASSERTIONS
    Next(last) = nullptr;
#endif
    Node* expected = nullptr;
    return  __atomic_compare_exchange_n(&tail, &expected, &last, false, __ATOMIC_SEQ_CST, __ATOMIC_RELAXED);
  }

  bool tryPushEmpty(Node& elem) { return tryPushEmpty(elem, elem); }

  Node* push(Node& first, Node& last) {
#if !TESTING_ENABLE_ASSERTIONS
    Next(last) = nullptr;
#endif
    // make sure writes to 'next' are not reordered with this update
    Node* prev = __atomic_exchange_n(&tail, &last, __ATOMIC_SEQ_CST); // swing tail to last of new element(s)
    if (prev) Next(*prev) = &first;
    return prev;
  }

  Node* push(Node& elem) { return push(elem, elem); }

  Node* next(Node& elem) {
    Node* expected = &elem;
    if (__atomic_compare_exchange_n(&tail, &expected, nullptr, false, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST)) return nullptr;
    while (!Next(elem)) Pause();         // producer in push()
    return Next(elem);
  }
};

// https://doi.org/10.1109/CCGRID.2006.31, similar to MCS lock
// note that modifications to 'head' need to be integrated with basic MCS operations in this particular way
// pop() inherits the potential stall from MCS queue's next() (see base class above)
template<typename Node, Node* volatile&(*Next)(Node&)>
class QueueNemesis : public QueueMCS<Node,Next> {
  Node* volatile head;
public:
  QueueNemesis() : head(nullptr) {}

  Node* push(Node& first, Node& last) {
    Node* prev = QueueMCS<Node,Next>::push(first, last);
    if (!prev) head = &first;
    return prev;
  }

  Node* push(Node& elem) { return push(elem, elem); }

  Node* pop(Node*& next) {
    if (!head) return nullptr;
    Node* elem = head;                                           // return head
    if (Next(*elem)) {
      head = next = Next(*elem);
      MemoryFence();                                          // force memory sync
    } else {
      head = nullptr; // store nullptr in head before potential modification of tail in next()
      next = QueueMCS<Node,Next>::next(*elem); // memory sync in next()
      if (next) head = next;
    }
    QueueMCS<Node,Next>::clear(*elem);
    return elem;
  }

  Node* pop() {
    Node* dummy = nullptr;
    return pop(dummy);
  }
};

#endif /* _LockFreeQueues_h_ */
