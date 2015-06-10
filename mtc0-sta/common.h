/* common.h
 * Common includes
 * 
 * Copyright 2013 Akash Rawal
 * This file is part of MTC-Standalone.
 * 
 * MTC-Standalone is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * MTC-Standalone is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with MTC-Standalone.  If not, see <http://www.gnu.org/licenses/>.
 */

//Common header files
#include <mtc0/mtc.h>
#include <sys/socket.h>

#ifndef _MTC_PUBLIC
#include <string.h>
#include <error.h>
#include <assert.h>
#endif

#define _MTC_HEADER
//Target related header files

#include "fd_link.h"

#undef _MTC_HEADER
