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

#include "zebra.h"
#include "prefix.h"
#include "log.h"

#include "ospf6_top.h"
#include "ospf6_area.h"
#include "ospf6_af.h"

enum {
  OSPF6_AF_FAILED = 0,
  OSPF6_AF_IPV6_UNICAST,
  OSPF6_AF_IPV6_MULTICAST,
  OSPF6_AF_IPV4_UNICAST,
  OSPF6_AF_IPV4_MULTICAST,
  OSPF6_AF_UNASSIGNED,
};

static int
ospf6_af_range (struct ospf6 *o)
{
  u_int8_t instance_id;

  assert (o != NULL);

  instance_id = o->instance_id;

  if (instance_id < 32)
    return OSPF6_AF_IPV6_UNICAST;
  else if (instance_id < 64)
    return OSPF6_AF_IPV6_MULTICAST;
  else if (instance_id < 96)
    return OSPF6_AF_IPV4_UNICAST;
  else if (instance_id < 128)
    return OSPF6_AF_IPV4_MULTICAST;
  else
    zlog_warn ("%s: Error: OSPF Instance-ID %u is reserved",
	       __func__, instance_id);

  return OSPF6_AF_UNASSIGNED;
}

bool
ospf6_af_is_ipv6_unicast (struct ospf6 *o)
{
  return (ospf6_af_range (o) == OSPF6_AF_IPV6_UNICAST);
}

bool
ospf6_af_is_ipv6_multicast (struct ospf6 *o)
{
  return (ospf6_af_range (o) == OSPF6_AF_IPV6_MULTICAST);
}

bool
ospf6_af_is_ipv4_unicast (struct ospf6 *o)
{
  return (ospf6_af_range (o) == OSPF6_AF_IPV4_UNICAST);
}

bool
ospf6_af_is_ipv4_multicast (struct ospf6 *o)
{
  return (ospf6_af_range (o) == OSPF6_AF_IPV4_MULTICAST);
}

bool
ospf6_af_is_ipv6 (struct ospf6 *o)
{
  int range;

  range = ospf6_af_range (o);

  return (range == OSPF6_AF_IPV6_UNICAST || range == OSPF6_AF_IPV6_MULTICAST);
}

bool
ospf6_af_is_ipv4 (struct ospf6 *o)
{
  int range;

  range = ospf6_af_range (o);

  return (range == OSPF6_AF_IPV4_UNICAST || range == OSPF6_AF_IPV4_MULTICAST);
}

/* convert an IPv6 address to IPv4 */
int
ospf6_af_address_convert6to4 (struct in_addr *addr4, struct in6_addr *addr6)
{
  uint32_t *a32 = (uint32_t *)addr6;

  addr4->s_addr = 0;

  if ((ospf6->af_interop ? a32[3] : a32[0]) != 0)
    return -1;

  if (a32[1] != 0 || a32[2] != 0)
    return -1;

  addr4->s_addr = (ospf6->af_interop ? a32[0] : a32[3]);

  return 0;
}

/* convert an IPv4 address to IPv6 */
void
ospf6_af_address_convert4to6 (struct in6_addr *addr6, struct in_addr *addr4)
{
  uint32_t *a32 = (uint32_t *)addr6;

  *(ospf6->af_interop ? &a32[0] : &a32[3]) = addr4->s_addr;
  a32[1] = 0;
  a32[2] = 0;
  *(ospf6->af_interop ? &a32[3] : &a32[0]) = 0;
}

static int
ospf6_af_validate_prefixlen (int af_range, unsigned int prefixlen)
{
  int err = -1;

  switch (af_range)
    {
    case OSPF6_AF_IPV6_UNICAST:
    case OSPF6_AF_IPV6_MULTICAST:
      if (prefixlen <= IPV6_MAX_PREFIXLEN)
	err = 0;
      break;

    case OSPF6_AF_IPV4_UNICAST:
    case OSPF6_AF_IPV4_MULTICAST:
      if (ospf6->af_interop)
      {
	if (prefixlen <= IPV4_MAX_PREFIXLEN)
	  err = 0;
      }
      else
      {
	if (prefixlen <= IPV6_MAX_PREFIXLEN && prefixlen >= 96)
	  err = 0;
      }
      break;

    default:
      zlog_warn ("%s: unknown address family range %i", __func__, af_range);
      break;
    }

  return err;
}

static unsigned int
ospf6_af_prefixlen6to4 (unsigned int prefixlen)
{
  if (ospf6->af_interop)
  {
    assert (prefixlen <= IPV4_MAX_PREFIXLEN);

    return prefixlen;
  }

  assert (prefixlen <= IPV6_MAX_PREFIXLEN);
  assert (prefixlen >= IPV6_MAX_PREFIXLEN - IPV4_MAX_PREFIXLEN);

  return IPV4_MAX_PREFIXLEN - (IPV6_MAX_PREFIXLEN - prefixlen);
}

static unsigned int
ospf6_af_prefixlen4to6 (unsigned int prefixlen)
{
  assert (prefixlen <= IPV4_MAX_PREFIXLEN);

  if (ospf6->af_interop)
    return prefixlen;

  return IPV6_MAX_PREFIXLEN - (IPV4_MAX_PREFIXLEN - prefixlen);
}

unsigned int
ospf6_af_prefixlen6 (struct ospf6 *o, unsigned int prefixlen6)
{
  if (ospf6_af_is_ipv4 (o) &&
      ospf6_af_validate_prefixlen (OSPF6_AF_IPV4_UNICAST, prefixlen6) == 0)
    return ospf6_af_prefixlen6to4 (prefixlen6);

  return prefixlen6;
}

/* convert an IPv6 prefix to IPv4; prefixes cannot overlap */
int
ospf6_af_prefix_convert6to4 (struct prefix_ipv4 *p4, struct prefix_ipv6 *p6)
{
  int err;
  struct in_addr addr4;

  memset (p4, 0, sizeof (*p4));

  if (p6->family != AF_INET6)
    return -1;

  if (p6->prefixlen >
      (ospf6->af_interop ? IPV4_MAX_PREFIXLEN : IPV6_MAX_PREFIXLEN))
    return -1;

  /* the prefix length must be at least 96 bits */
  if (!ospf6->af_interop && p6->prefixlen < 96)
    {
      char buf[64];

      prefix2str ((struct prefix *) p6, buf, sizeof (buf));
      zlog_warn ("%s: invalid ipv4 af address: %s", __func__, buf);
      return -1;
    }

  err = ospf6_af_address_convert6to4 (&addr4, &p6->prefix);
  if (err)
    return -1;

  p4->family = AF_INET;
  p4->prefixlen = ospf6_af_prefixlen6to4 (p6->prefixlen);
  p4->prefix = addr4;

  return 0;
}

/* convert an IPv4 prefix to IPv6; prefixes cannot overlap */
int
ospf6_af_prefix_convert4to6 (struct prefix_ipv6 *p6, struct prefix_ipv4 *p4)
{
  memset (p6, 0, sizeof (*p6));

  if (p4->family != AF_INET)
    return -1;

  if (p4->prefixlen > IPV4_MAX_PREFIXLEN)
    return -1;

  p6->family = AF_INET6;
  p6->prefixlen = ospf6_af_prefixlen4to6 (p4->prefixlen);
  ospf6_af_address_convert4to6 (&p6->prefix, &p4->prefix);

  return 0;
}

int
ospf6_af_validate_ipv6_unicast (struct in6_addr *addr)
{
  if (IN6_IS_ADDR_LINKLOCAL (addr))
    return -1;

  if (IN6_IS_ADDR_UNSPECIFIED (addr))
    return -1;

  if (IN6_IS_ADDR_LOOPBACK (addr))
    return -1;

  if (IN6_IS_ADDR_V4COMPAT (addr))
    return -1;

  if (IN6_IS_ADDR_V4MAPPED (addr))
    return -1;

  if (IN6_IS_ADDR_MULTICAST (addr))
    return -1;

  /* TODO: any other checks? */

  return 0;
}

int
ospf6_af_validate_ipv6_multicast (struct in6_addr *addr)
{
  if (IN6_IS_ADDR_MC_GLOBAL (addr))
    return 0;

  if (IN6_IS_ADDR_MC_ORGLOCAL (addr))
    return 0;

  if (IN6_IS_ADDR_MC_SITELOCAL (addr))
    return 0;

  return -1;
}

int
ospf6_af_validate_ipv4_unicast (struct in6_addr *addr)
{
  struct in_addr inaddr4;
  uint32_t addr4;

  if (ospf6_af_address_convert6to4 (&inaddr4, addr))
    return -1;

  addr4 = ntohl (inaddr4.s_addr);

  if (addr4 == INADDR_ANY)
    return -1;

  if (addr4 == INADDR_BROADCAST)
    return -1;

  /* loopback */
  if ((addr4 & 0xff000000) == 0x7f000000)
    return -1;

  if (IN_MULTICAST (addr4))
    return -1;

  /* TODO: any other checks? */

  return 0;
}

int
ospf6_af_validate_ipv4_multicast (struct in6_addr *addr)
{
  struct in_addr inaddr4;
  uint32_t addr4;

  if (ospf6_af_address_convert6to4 (&inaddr4, addr))
    return -1;

  addr4 = ntohl (inaddr4.s_addr);

  if (IN_MULTICAST (addr4) && addr4 > INADDR_MAX_LOCAL_GROUP)
    return 0;

  return -1;
}

int
ospf6_af_validate_prefix (struct ospf6 *o, struct in6_addr *prefix,
			  unsigned int prefixlen)
{
  int af_range, err;

  af_range = ospf6_af_range (o);

  err = ospf6_af_validate_prefixlen (af_range, prefixlen);
  if (err)
    return err;

  switch (af_range)
    {
    case OSPF6_AF_IPV6_UNICAST:
      err = ospf6_af_validate_ipv6_unicast (prefix);
      break;

    case OSPF6_AF_IPV6_MULTICAST:
      err = ospf6_af_validate_ipv6_multicast (prefix);
      break;

    case OSPF6_AF_IPV4_UNICAST:
      err = ospf6_af_validate_ipv4_unicast (prefix);
      break;

    case OSPF6_AF_IPV4_MULTICAST:
      err = ospf6_af_validate_ipv4_multicast (prefix);
      break;

    default:
      zlog_warn ("%s: unknown address family range %i", __func__, af_range);
      err = -1;
      break;
    }

  return err;
}

const char *
ospf6_prefix2str (struct ospf6 *o, struct prefix *prefix,
		  char *buf, size_t bufsize)
{
  struct prefix tmp, *p = prefix;
  int err;

  if (ospf6_af_is_ipv4 (o))
    {
      int err;

      err = ospf6_af_prefix_convert6to4 ((struct prefix_ipv4 *)&tmp,
					 (struct prefix_ipv6 *)prefix);
      if (err)
	{
	  char buf[PREFIXSTRLEN];

	  prefix2str (prefix, buf, sizeof (buf));
	  zlog_err ("%s: error converting prefix: %s", __func__, buf);
	}
      else
	{
	  p = &tmp;
	}
    }

  err = prefix2str (p, buf, bufsize);
  assert (err == 0);

  return buf;
}

const char *
ospf6_addr2str (struct ospf6 *o, struct in6_addr *addr,
		char *buf, size_t bufsize)
{
  void *src = addr;
  int af = AF_INET6;
  struct in_addr addr4;
  const char *s;

  if (ospf6_af_is_ipv4 (o))
    {
      int err;

      err = ospf6_af_address_convert6to4 (&addr4, addr);
      if (err)
	{
	  char buf[INET6_ADDRSTRLEN];

	  ospf6_addr2str6 (addr, buf, sizeof (buf));
	  zlog_err ("%s: error converting address: %s", __func__, buf);
	}
      else
	{
	  src = &addr4;
	  af = AF_INET;
	}
    }

  s = inet_ntop (af, src, buf, bufsize);
  assert (s == buf);

  return s;
}

const char *
ospf6_addr2str6 (struct in6_addr *addr, char *buf, size_t bufsize)
{
  const char *s;

  s = inet_ntop (AF_INET6, addr, buf, bufsize);
  assert (s == buf);

  return s;
}

const char *
ospf6_id2str (u_int32_t id, char *buf, size_t bufsize)
{
  const char *s;

  s = inet_ntop (AF_INET, &id, buf, bufsize);
  assert (s == buf);

  return s;
}

/**
 * Convert a string to a 32-bit id (in network byte order).
 *
 * @param id A pointer to where the id will be stored.
 *
 * @param s An id string in numbers-and-dots notation (see
 *          inet_aton(3) for a description of supported formats).
 *
 * @return Zero on success.  Nonzero if an error occurred.
 */
int
ospf6_str2id (const char *s, u_int32_t *id)
{
  struct in_addr addr;
  int r;

  r = inet_aton (s, &addr);
  if (r == 0)
    return -1;

  *id = addr.s_addr;

  return 0;
}

int
ospf6_str2prefix (struct ospf6 *o, const char *str, struct prefix *prefix)
{
  int r;

  r = str2prefix (str, prefix);
  if (r == 0)
    return 0;

  if (ospf6_af_is_ipv4 (o))
    {
      struct prefix_ipv4 p4;

      if (prefix->family != AF_INET)
       return 0;

      p4.family = prefix->family;
      p4.prefixlen = prefix->prefixlen;
      p4.prefix = prefix->u.prefix4;
      r = ospf6_af_prefix_convert4to6 ((struct prefix_ipv6 *)prefix, &p4);
      if (r)
	return 0;
    }
  else
    {
      if (prefix->family != AF_INET6)
       return 0;
    }

  return 1;
}
