/* -*-  c-file-style: "gnu" -*- */

/*
 * Copyright (c) 2009-2010 The Boeing Company
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

#ifndef _OSPF6_ZEBRA_LINKMETRICS_H_
#define _OSPF6_ZEBRA_LINKMETRICS_H_

struct ospf6_interface;
struct ospf6_neighbor;
struct zebra_linkmetrics;
struct zebra_linkstatus;
struct zclient;

typedef void (*linkmetrics_hook_t) (struct ospf6_neighbor *,
				    struct zebra_linkmetrics *);
typedef void (*linkstatus_hook_t) (struct ospf6_interface *,
				   struct ospf6_neighbor *,
				   struct zebra_linkstatus *);

int ospf6_add_linkmetrics_hook (linkmetrics_hook_t hook);
int ospf6_remove_linkmetrics_hook (linkmetrics_hook_t hook);

int ospf6_add_linkstatus_hook (linkstatus_hook_t hook);
int ospf6_remove_linkstatus_hook (linkstatus_hook_t hook);

int ospf6_zebra_linkmetrics (int command, struct zclient *zclient,
			     zebra_size_t length);
int ospf6_zebra_linkstatus (int command, struct zclient *zclient,
			    zebra_size_t length);

void ospf6_zebra_update_linkmetrics (struct ospf6_neighbor *on,
				     struct zebra_linkmetrics *linkmetrics);

#endif	/* _OSPF6_ZEBRA_LINKMETRICS_H_ */
