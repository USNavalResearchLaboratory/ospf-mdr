/* -*-  c-file-style: "gnu" -*- */

/*
 * Copyright (c) 2011 The Boeing Company
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

#ifndef OSPF6_AF_H
#define OSPF6_AF_H

#include <stdbool.h>

struct ospf6;
struct in_addr;
struct in6_addr;
struct prefix;
struct prefix_ipv4;
struct prefix_ipv6;

extern bool ospf6_af_is_ipv6_unicast (struct ospf6 *o);
extern bool ospf6_af_is_ipv6_multicast (struct ospf6 *o);
extern bool ospf6_af_is_ipv4_unicast (struct ospf6 *o);
extern bool ospf6_af_is_ipv4_multicast (struct ospf6 *o);

extern bool ospf6_af_is_ipv6 (struct ospf6 *o);
extern bool ospf6_af_is_ipv4 (struct ospf6 *o);

extern int ospf6_af_address_convert6to4 (struct in_addr *addr4,
					 struct in6_addr *addr6);
extern void ospf6_af_address_convert4to6 (struct in6_addr *addr6,
					  struct in_addr *addr4);
extern int ospf6_af_prefix_convert6to4 (struct prefix_ipv4 *p4,
					struct prefix_ipv6 *p6);
extern int ospf6_af_prefix_convert4to6 (struct prefix_ipv6 *p6,
					struct prefix_ipv4 *p4);
extern unsigned int ospf6_af_prefixlen6 (struct ospf6 *o,
					 unsigned int prefixlen6);

extern int ospf6_af_validate_ipv6_unicast (struct in6_addr *addr);
extern int ospf6_af_validate_ipv6_multicast (struct in6_addr *addr);
extern int ospf6_af_validate_ipv4_unicast (struct in6_addr *addr);
extern int ospf6_af_validate_ipv4_multicast (struct in6_addr *addr);
extern int ospf6_af_validate_prefix (struct ospf6 *o,
				     struct in6_addr *prefix,
				     unsigned int prefixlen);

extern const char *ospf6_prefix2str (struct ospf6 *o, struct prefix *prefix,
				     char *buf, size_t bufsize);
extern const char *ospf6_addr2str (struct ospf6 *o, struct in6_addr *addr,
				   char *buf, size_t bufsize);
extern const char *ospf6_addr2str6 (struct in6_addr *addr,
				    char *buf, size_t bufsize);
extern const char *ospf6_id2str (u_int32_t id, char *buf, size_t bufsize);
extern int ospf6_str2id (const char *s, u_int32_t *id);
extern int ospf6_str2prefix (struct ospf6 *o, const char *str,
			     struct prefix *prefix);

#endif /* OSPF6_AF_H */
