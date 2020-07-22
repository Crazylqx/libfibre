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
#ifndef _RuntimeStack_h_
#define _RuntimeStack_h_ 1

#include "libfibre/Fibre.h"

inline void RuntimeStartStack(funcvoid3_t func, ptr_t arg1, ptr_t arg2, ptr_t arg3) {
  try {
    func(arg1, arg2, arg3);
  } catch (abi::__forced_unwind*) {}
}

inline void RuntimePreStackSwitch(StackContext& currStack, StackContext& nextStack, _friend<StackContext> fs) {
  Fibre& currFibre = reinterpret_cast<Fibre&>(currStack);
  Fibre& nextFibre = reinterpret_cast<Fibre&>(nextStack);
  currFibre.deactivate(nextFibre, fs);
  Context::setCurrStack(nextFibre, fs);
}

inline void RuntimePostStackSwitch(StackContext& newStack, _friend<StackContext> fs) {
  Fibre& newFibre = reinterpret_cast<Fibre&>(newStack);
  newFibre.activate(fs);
}

inline void RuntimeStackDestroy(StackContext& prevStack, _friend<StackContext> fs) {
  Fibre& prevFibre = reinterpret_cast<Fibre&>(prevStack);
  prevFibre.destroy(fs);
}

#endif /* _RuntimeStack_h_ */
