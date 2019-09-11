/* -*-  c-file-style: "gnu" -*- */

/*
 * Copyright (c) 2010 The Boeing Company
 *
 * This file is part of GNU Zebra.
 *
 * GNU Zebra is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * GNU Zebra is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with GNU Zebra; see the file COPYING.  If not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include "zebra.h"
#include "linklist.h"
#include "log.h"

#include "ospf6_callbacks.h"

int
ospf6_add_hook (struct list *hooklist, void *hook)
{
  struct listnode *node;
  void *tmphook;

  assert (hook != NULL);

  for (ALL_LIST_ELEMENTS_RO (hooklist, node, tmphook))
    if (tmphook == hook)
      {
	zlog_err ("%s: hook %p already exists in hook list %p",
		  __func__, hook, hooklist);
	return -1;
      }

  listnode_add (hooklist, hook);

  return 0;
}

int
ospf6_remove_hook (struct list *hooklist, void *hook)
{
  struct listnode *node;
  void *tmphook;

  assert (hook != NULL);

  for (ALL_LIST_ELEMENTS_RO (hooklist, node, tmphook))
    if (tmphook == hook)
      break;

  if (!node)
    return -1;

  list_delete_node (hooklist, node);

  return 0;
}
