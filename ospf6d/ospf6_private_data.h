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

#ifndef _OSPF6_PRIVATE_DATA_H_
#define _OSPF6_PRIVATE_DATA_H_

struct list *ospf6_private_data_list (void);
int ospf6_add_private_data (struct list *private_data_list,
			    unsigned int *id, void *data);
void *ospf6_get_private_data (struct list *private_data_list,
			      unsigned int id);
void *ospf6_del_private_data (struct list *private_data_list,
			      unsigned int id);

#endif	/* _OSPF6_PRIVATE_DATA_H_ */
