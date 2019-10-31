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
#ifndef _Garage_h_
#define _Garage_h_ 1

template<typename Lock, typename Condition>
class Garage {
  struct Link {
    Link*     next;
    Condition cond;
    void*     ptr;
  };
  Lock  lock;
  Link* stack;
  
public:
  Garage() : stack(nullptr) {}
  void* park() {
    Link link;
    lock.acquire();
    link.next = stack;
    stack = &link;
    link.cond.wait(lock);
    return link.ptr;
  }
  bool run(void* ptr) {
    lock.acquire();
    if (!stack) {
      lock.release();
      return false;
    }
    Link* link = stack;
    stack = link->next;
    lock.release();      // can unlock early...
    link->ptr = ptr;
    link->cond.signal(); // ...since cond is private
    return true;
  }
};

#endif /* _Garage_h_ */
