/*
   Copyright (C) 1994,99,2002 Free Software Foundation, Inc.

   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2, or (at
   your option) any later version.

   This program is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. */

#include "priv.h"
#include "trivfs_io_S.h"
#include <assert-backtrace.h>
#include <fcntl.h>

kern_return_t
trivfs_S_io_write (struct trivfs_protid *cred,
		   mach_port_t reply,
		   mach_msg_type_name_t replytype,
		   const_data_t data,
		   mach_msg_type_number_t datalen,
		   off_t off,
		   vm_size_t *amt)
{
  if (!(trivfs_allow_open & O_WRITE))
    return EBADF;

  assert_backtrace (!trivfs_support_write);
  return EOPNOTSUPP;
}
