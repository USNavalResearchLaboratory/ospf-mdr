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
#include "memory.h"
#include "linklist.h"
#include "log.h"

#include "ospf6_private_data.h"

struct ospf6_private_data {
  unsigned int id;
  void *data;
};

static struct ospf6_private_data *
ospf6_private_data_alloc (unsigned int id, void *data)
{
  struct ospf6_private_data *pdata;

  pdata = XMALLOC (MTYPE_OSPF6_OTHER, sizeof (*pdata));
  pdata->id = id;
  pdata->data = data;

  return pdata;
}

static void
ospf6_private_data_free (void *data)
{
  XFREE (MTYPE_OSPF6_OTHER, data);
}

struct list *
ospf6_private_data_list (void)
{
  struct list *l;
  l = list_new ();
  l->del = ospf6_private_data_free;

  return l;
}

int
ospf6_add_private_data (struct list *private_data_list,
			unsigned int *id, void *data)
{
  struct listnode *node;
  struct ospf6_private_data *pdata;
  unsigned int tmpid, maxid;

  tmpid = *id;
  maxid = *id;

  for (ALL_LIST_ELEMENTS_RO (private_data_list, node, pdata))
    {
      if (tmpid != 0 && pdata->id == tmpid)
	{
	  zlog_err ("%s: private data id %u already exists in list %p",
		    __func__, tmpid, private_data_list);
	  return -1;
	}
      else if (pdata->id > maxid)
	maxid = pdata->id;
    }

  if (tmpid == 0)
    {
      tmpid = maxid + 1;
      assert (tmpid > maxid);
    }

  pdata = ospf6_private_data_alloc (tmpid, data);
  listnode_add (private_data_list, pdata);

  *id = tmpid;

  return 0;
}

static struct listnode *
__ospf6_get_private_data (struct list *private_data_list, unsigned int id)
{
  struct listnode *node;
  struct ospf6_private_data *pdata;

  assert (id != 0);

  for (ALL_LIST_ELEMENTS_RO (private_data_list, node, pdata))
    if (pdata->id == id)
      return node;

  zlog_err ("%s: private data id %u not found in list %p",
	    __func__, id, private_data_list);

  return NULL;
}

void *
ospf6_get_private_data (struct list *private_data_list, unsigned int id)
{
  struct listnode *node;

  node = __ospf6_get_private_data (private_data_list, id);
  if (node)
    {
      struct ospf6_private_data *pdata;

      pdata = listgetdata (node);
      assert (pdata->id == id);

      return pdata->data;
    }

  return NULL;
}

void *
ospf6_del_private_data (struct list *private_data_list, unsigned int id)
{
  struct listnode *node;

  node = __ospf6_get_private_data (private_data_list, id);
  if (node)
    {
      struct ospf6_private_data *pdata;
      void *data;

      pdata = listgetdata (node);
      assert (pdata->id == id);
      data = pdata->data;

      list_delete_node (private_data_list, node);
      ospf6_private_data_free (pdata);

      return data;
    }

  return NULL;
}
