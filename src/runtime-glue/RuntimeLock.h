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
#ifndef _RuntimeLock_h_
#define _RuntimeLock_h_ 1

#include "libfibre/OsLocks.h"

#if TESTING_LOCK_SPIN
typedef OsLock<4,TESTING_LOCK_SPIN,1> WorkerLock;
#else
typedef OsLock<0,0,0> WorkerLock;
#endif
typedef OsSemaphore   WorkerSemaphore;

#endif /* _RuntimeLock_h_ */
