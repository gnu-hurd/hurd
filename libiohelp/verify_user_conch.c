/* 
   Copyright (C) 1993, 1994 Free Software Foundation

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

#include "ioserver.h"
#include <errno.h>

/* Check to see if the user identified by USER has conch C; if not,
   return an error, else, return 0.  */
error_t
verify_user_conch (struct conch *c, void *user)
{
  struct shared_io *user_sh;
  
  if (user != c->holder)
    return EPERM;
  user_sh = c->holder_shared_page;
  spin_lock (&user_sh->lock);
  if (user_sh->conch_status != USER_HAS_CONCH
      && user_sh->conch_status != USER_RELEASE_CONCH)
    {
      spin_unlock (&user_sh->lock);
      return EPERM;
    }
  spin_unlock (&user_sh->lock);
  return 0;
}
