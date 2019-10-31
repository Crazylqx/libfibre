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
#ifndef _RuntimeDebug_h_
#define _RuntimeDebug_h_ 1

#include "libfibre/lfbasics.h"

template<typename... Args> inline void RuntimeDebugB(const Args&... a) {
//  dprintl(a...);
}
template<typename... Args> inline void RuntimeDebugS(const Args&... a) {
//  dprintl(a...);
}
template<typename... Args> inline void RuntimeDebugT(const Args&... a) {
//  dprintl(a...);
}
template<typename... Args> inline void RuntimeDebugP(const Args&... a) {
//  dprintl(a...);
}

#endif /* _RuntimeDebug_h_ */
