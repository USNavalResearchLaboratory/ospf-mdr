/*
 * Copyright (C) 2003 Yasuhiro Ohara
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

/* Shortest Path First calculation for OSPFv3 */

#include <zebra.h>

#include "log.h"
#include "memory.h"
#include "command.h"
#include "vty.h"
#include "prefix.h"
#include "pqueue.h"
#include "linklist.h"
#include "thread.h"

#include "ospf6_lsa.h"
#include "ospf6_lsdb.h"
#include "ospf6_route.h"
#include "ospf6_area.h"
#include "ospf6_spf.h"
#include "ospf6_intra.h"
#include "ospf6_interface.h"
#include "ospf6d.h"
#include "ospf6_neighbor.h"
#include "ospf6_af.h"
#include "ospf6_proto.h"
#include "ospf6_mdr.h"

unsigned char conf_debug_ospf6_spf = 0;

static int
uint32_cmp (u_int32_t a, u_int32_t b)
{
  if (a < b)
    return -1;

  if (a > b)
    return 1;

  return 0;
}

static int
ospf6_vertex_cmp (void *a, void *b)
{
  struct ospf6_vertex *va = (struct ospf6_vertex *) a;
  struct ospf6_vertex *vb = (struct ospf6_vertex *) b;
  int cmp;

  /* ascending order */
  cmp = uint32_cmp (va->cost, vb->cost);
  if (cmp == 0)
    cmp = uint32_cmp (va->hops, vb->hops);

  return cmp;
}

static int
ospf6_vertex_id_cmp (void *a, void *b)
{
  struct ospf6_vertex *va = (struct ospf6_vertex *) a;
  struct ospf6_vertex *vb = (struct ospf6_vertex *) b;
  int cmp;

  cmp = uint32_cmp (ntohl (ospf6_linkstate_prefix_adv_router (&va->vertex_id)),
                    ntohl (ospf6_linkstate_prefix_adv_router (&vb->vertex_id)));
  if (cmp == 0)
    {
      cmp = uint32_cmp (ntohl (ospf6_linkstate_prefix_id (&va->vertex_id)),
                        ntohl (ospf6_linkstate_prefix_id (&vb->vertex_id)));
    }

  return cmp;
}

static void
ospf6_spf_vertex_add_child (struct ospf6_vertex *parent,
			    struct ospf6_vertex *child)
{
  if (IS_OSPF6_DEBUG_SPF (PROCESS))
    {
      zlog_debug ("%s: adding vertex %s (%p) as child of %s (%p)",
                  __func__, child->name, child, parent->name, parent);
    }

  assert (child->parent == NULL);
  assert (listnode_lookup (parent->child_list, child) == NULL);

  child->parent = parent;
  listnode_add_sort (parent->child_list, child);
}

static void
ospf6_spf_vertex_del_child (struct ospf6_vertex *parent,
			    struct ospf6_vertex *child)
{
  struct listnode *node;

  if (IS_OSPF6_DEBUG_SPF (PROCESS))
    {
      zlog_debug ("%s: deleting vertex %s (%p) as child of %s (%p)",
                  __func__, child->name, child, parent->name, parent);
    }

  assert (child->parent == parent);

  node = listnode_lookup (parent->child_list, child);
  assert (node != NULL);

  child->parent = NULL;
  list_delete_node (parent->child_list, node);
}

static struct ospf6_vertex *
ospf6_vertex_create (struct ospf6_lsa *lsa, struct ospf6_vertex *parent)
{
  struct ospf6_vertex *v;
  int i;

  v = (struct ospf6_vertex *)
    XCALLOC (MTYPE_OSPF6_VERTEX, sizeof (struct ospf6_vertex));

  /* type */
  if (ntohs (lsa->header->type) == OSPF6_LSTYPE_ROUTER)
    v->type = OSPF6_VERTEX_TYPE_ROUTER;
  else if (ntohs (lsa->header->type) == OSPF6_LSTYPE_NETWORK)
    v->type = OSPF6_VERTEX_TYPE_NETWORK;
  else
    assert (0);

  /* vertex_id */
  ospf6_linkstate_prefix (lsa->header->adv_router, lsa->header->id,
                          &v->vertex_id);

  /* name */
  ospf6_linkstate_prefix2str (&v->vertex_id, v->name, sizeof (v->name));

  /* Associated LSA */
  v->lsa = lsa;

  /* capability bits + options */
  v->capability = *(u_char *)(OSPF6_LSA_HEADER_END (lsa->header));
  v->options[0] = *(u_char *)(OSPF6_LSA_HEADER_END (lsa->header) + 1);
  v->options[1] = *(u_char *)(OSPF6_LSA_HEADER_END (lsa->header) + 2);
  v->options[2] = *(u_char *)(OSPF6_LSA_HEADER_END (lsa->header) + 3);

  for (i = 0; i < OSPF6_MULTI_PATH_LIMIT; i++)
    ospf6_nexthop_clear (&v->nexthop[i]);

  v->child_list = list_new ();
  v->child_list->cmp = ospf6_vertex_id_cmp;

  if (IS_OSPF6_DEBUG_SPF (PROCESS))
    {
      zlog_debug ("%s: created vertex %s (%p)", __func__, v->name, v);
    }

  if (parent)
    ospf6_spf_vertex_add_child (parent, v);

  return v;
}

static void
ospf6_vertex_delete (struct ospf6_vertex *v)
{
  struct listnode *head;

  if (IS_OSPF6_DEBUG_SPF (PROCESS))
    {
      zlog_debug ("%s: deleting vertex %s (%p)", __func__, v->name, v);
    }

  if (v->parent)
    ospf6_spf_vertex_del_child (v->parent, v);

  for (head = listhead (v->child_list); head; head = listhead (v->child_list))
    {
      struct ospf6_vertex *child;

      child = listgetdata (head);
      ospf6_spf_vertex_del_child (v, child);
    }
  list_delete (v->child_list);

  XFREE (MTYPE_OSPF6_VERTEX, v);
}

static struct ospf6_lsa *
ospf6_spf_lsdb_lookup (u_int16_t type, u_int32_t id, u_int32_t adv_router,
		       struct ospf6_lsdb *lsdb)
{
  struct ospf6_lsa *lsa;

  lsa = ospf6_lsdb_lookup (type, id, adv_router, lsdb);

  if (lsa && OSPF6_LSA_IS_MAXAGE (lsa))
    {
      if (IS_OSPF6_DEBUG_SPF (PROCESS))
	zlog_debug ("%s: ignoring maxage lsa: %s", __func__, lsa->name);
      lsa = NULL;
    }

  return lsa;
}

static struct ospf6_lsa *
ospf6_lsdesc_lsa (caddr_t lsdesc, struct ospf6_vertex *v)
{
  struct ospf6_lsa *lsa;
  u_int16_t type = 0;
  u_int32_t id = 0, adv_router = 0;

  if (VERTEX_IS_TYPE (NETWORK, v))
    {
      type = htons (OSPF6_LSTYPE_ROUTER);
      id = htonl (0);
      adv_router = NETWORK_LSDESC_GET_NBR_ROUTERID (lsdesc);
    }
  else
    {
      if (ROUTER_LSDESC_IS_TYPE (POINTTOPOINT, lsdesc))
        {
          type = htons (OSPF6_LSTYPE_ROUTER);
          id = htonl (0);
          adv_router = ROUTER_LSDESC_GET_NBR_ROUTERID (lsdesc);
        }
      else if (ROUTER_LSDESC_IS_TYPE (TRANSIT_NETWORK, lsdesc))
        {
          type = htons (OSPF6_LSTYPE_NETWORK);
          id = htonl (ROUTER_LSDESC_GET_NBR_IFID (lsdesc));
          adv_router = ROUTER_LSDESC_GET_NBR_ROUTERID (lsdesc);
        }
    }

  lsa = ospf6_spf_lsdb_lookup (type, id, adv_router, v->area->lsdb);

  if (IS_OSPF6_DEBUG_SPF (PROCESS))
    {
      char ibuf[16], abuf[16];
      ospf6_id2str (id, ibuf, sizeof (ibuf));
      ospf6_id2str (adv_router, abuf, sizeof (abuf));
      if (lsa)
        zlog_debug ("  Link to: %s", lsa->name);
      else
        zlog_debug ("  Link to: [%s Id:%s Adv:%s] No LSA",
		    ospf6_lstype_name (type), ibuf, abuf);
    }

  return lsa;
}

static char *
ospf6_lsdesc_backlink (struct ospf6_lsa *lsa,
                       caddr_t lsdesc, struct ospf6_vertex *v)
{
  caddr_t backlink, found = NULL;
  int size;

  size = (OSPF6_LSA_IS_TYPE (ROUTER, lsa) ?
          sizeof (struct ospf6_router_lsdesc) :
          sizeof (struct ospf6_network_lsdesc));
  for (backlink = OSPF6_LSA_HEADER_END (lsa->header) + 4;
       backlink + size <= OSPF6_LSA_END (lsa->header); backlink += size)
    {
      assert (! (OSPF6_LSA_IS_TYPE (NETWORK, lsa) &&
                 VERTEX_IS_TYPE (NETWORK, v)));

      if (OSPF6_LSA_IS_TYPE (NETWORK, lsa) &&
          NETWORK_LSDESC_GET_NBR_ROUTERID (backlink)
            == v->lsa->header->adv_router)
        found = backlink;
      else if (VERTEX_IS_TYPE (NETWORK, v) &&
          ROUTER_LSDESC_IS_TYPE (TRANSIT_NETWORK, backlink) &&
          ROUTER_LSDESC_GET_NBR_ROUTERID (backlink)
            == v->lsa->header->adv_router &&
          ROUTER_LSDESC_GET_NBR_IFID (backlink)
            == ntohl (v->lsa->header->id))
        found = backlink;
      else
        {
          if (! ROUTER_LSDESC_IS_TYPE (POINTTOPOINT, backlink) ||
              ! ROUTER_LSDESC_IS_TYPE (POINTTOPOINT, lsdesc))
            continue;
          if (ROUTER_LSDESC_GET_NBR_IFID (backlink) !=
              ROUTER_LSDESC_GET_IFID (lsdesc) ||
              ROUTER_LSDESC_GET_NBR_IFID (lsdesc) !=
              ROUTER_LSDESC_GET_IFID (backlink))
            continue;
          if (ROUTER_LSDESC_GET_NBR_ROUTERID (backlink) !=
              v->lsa->header->adv_router ||
              ROUTER_LSDESC_GET_NBR_ROUTERID (lsdesc) !=
              lsa->header->adv_router)
            continue;
          found = backlink;
        }

      if (found != NULL)
        break;
    }

  if (IS_OSPF6_DEBUG_SPF (PROCESS))
    zlog_debug ("  Backlink %s", (found ? "OK" : "FAIL"));

  return found;
}

static void
ospf6_set_nexthop(struct ospf6_nexthop *nexthop, unsigned int ifindex,
		  struct in6_addr *linklocal_addr, const char *from_name)
{
  if (IS_OSPF6_DEBUG_SPF (PROCESS))
    {
      if (linklocal_addr)
        {
          char buf[64];

          ospf6_addr2str (ospf6, linklocal_addr, buf, sizeof (buf));
          zlog_debug ("  nexthop %s%%%s(%u) from %s", buf,
                      ifindex2ifname (ifindex), ifindex, from_name);
        }
      else
        {
          zlog_debug ("  nexthop %s(%u) from %s",
                      ifindex2ifname (ifindex), ifindex, from_name);
        }
    }

  nexthop->ifindex = ifindex;
  if (linklocal_addr)
    nexthop->address = *linklocal_addr;
}

static int
ospf6_nexthop_calc (struct ospf6_vertex *w, struct ospf6_vertex *v,
                    caddr_t lsdesc)
{
  int i, ifindex;
  struct ospf6_interface *oi;
  u_int16_t type;
  u_int32_t adv_router;
  struct ospf6_lsa *lsa;

  assert (VERTEX_IS_TYPE (ROUTER, w));
  ifindex = (VERTEX_IS_TYPE (NETWORK, v) ? v->nexthop[0].ifindex :
             ROUTER_LSDESC_GET_IFID (lsdesc));
  oi = ospf6_interface_lookup_by_ifindex (ifindex);
  if (oi == NULL)
    {
      if (IS_OSPF6_DEBUG_SPF (PROCESS))
        zlog_debug ("Can't find interface in SPF: ifindex %d", ifindex);
      return -1;
    }

  type = htons (OSPF6_LSTYPE_LINK);
  adv_router = (VERTEX_IS_TYPE (NETWORK, v) ?
                NETWORK_LSDESC_GET_NBR_ROUTERID (lsdesc) :
                ROUTER_LSDESC_GET_NBR_ROUTERID (lsdesc));

  i = 0;
  lsa = ospf6_lsdb_type_router_head (type, adv_router, oi->lsdb);

  if (lsa == NULL && ospf6_af_is_ipv6 (oi->area->ospf6))
    {
      struct ospf6_neighbor *on = ospf6_neighbor_lookup (adv_router, oi);
      if (on != NULL && IN6_IS_ADDR_LINKLOCAL (&on->linklocal_addr))
	{
	  ospf6_set_nexthop(&w->nexthop[i], ifindex,
			    &on->linklocal_addr, on->name);
          i++;
	}
    }

  for (; lsa && i < OSPF6_MULTI_PATH_LIMIT;
       lsa = ospf6_lsdb_type_router_next (type, adv_router, lsa))
    {
      struct ospf6_link_lsa *link_lsa;

      if (VERTEX_IS_TYPE (ROUTER, v) &&
          htonl (ROUTER_LSDESC_GET_NBR_IFID (lsdesc)) != lsa->header->id)
        continue;

      link_lsa = (struct ospf6_link_lsa *) OSPF6_LSA_HEADER_END (lsa->header);
      ospf6_set_nexthop(&w->nexthop[i], ifindex,
			&link_lsa->linklocal_addr, lsa->name);
      i++;
    }

  if (i == 0 && oi->type == OSPF6_IFTYPE_POINTOPOINT)
    {
      ospf6_set_nexthop(&w->nexthop[i], ifindex,
                        NULL, "point-to-point interface");
      i++;
    }

  if (i == 0)
    {
      if (IS_OSPF6_DEBUG_SPF (PROCESS))
	zlog_debug ("No nexthop for %s found", w->name);
      return -1;
    }

  return 0;
}

static int
ospf6_nexthop_cmp (const void *a, const void *b)
{
  const struct ospf6_nexthop *x = a;
  const struct ospf6_nexthop *y = b;
  int x_is_set, y_is_set;

  x_is_set = ospf6_nexthop_is_set (x);
  y_is_set = ospf6_nexthop_is_set (y);

  if (x_is_set && !y_is_set)
    return -1;
  else if (y_is_set && !x_is_set)
    return 1;
  else if (x->ifindex < y->ifindex)
    return -1;
  else if (x->ifindex > y->ifindex)
    return 1;
  else
    return memcmp (&x->address, &y->address, sizeof (x->address));
}

static int
ospf6_spf_add_nexthop (struct ospf6_nexthop existing[OSPF6_MULTI_PATH_LIMIT],
		       struct ospf6_nexthop *new)
{
  struct ospf6_nexthop *dst;
  int i;

  if (!ospf6_nexthop_is_set (new))
    return 0;

  dst = NULL;
  for (i = 0; i < OSPF6_MULTI_PATH_LIMIT; i++)
    {
      if (ospf6_nexthop_is_same (&existing[i], new))
	return 0;		/* new nexthop already exists */
      else if (!ospf6_nexthop_is_set (&existing[i]) && dst == NULL)
	dst = &existing[i];
    }

  if (dst != NULL)
    {
      /* install new nexthop */
      ospf6_nexthop_copy (dst, new);
      /* sort nexthops */
      qsort (existing, OSPF6_MULTI_PATH_LIMIT, sizeof (existing[0]),
	     ospf6_nexthop_cmp);
      return 0;
    }

  /* array is full */
  return -1;
}

static int
ospf6_spf_install (struct ospf6_vertex *v,
                   struct ospf6_route_table *result_table,
                   bool router_is_root)
{
  struct ospf6_route *route;
  int i;

  if (IS_OSPF6_DEBUG_SPF (PROCESS))
    zlog_debug ("SPF install %s hops %d cost %d",
		v->name, v->hops, v->cost);

  route = ospf6_route_lookup (&v->vertex_id, result_table);
  if (route && route->path.cost < v->cost)
    {
      if (IS_OSPF6_DEBUG_SPF (PROCESS))
        zlog_debug ("  already installed with lower cost (%d), ignore",
		    route->path.cost);
      ospf6_vertex_delete (v);
      return -1;
    }
  else if (route && route->path.cost == v->cost)
    {
      struct ospf6_vertex *prev;

      if (IS_OSPF6_DEBUG_SPF (PROCESS))
        zlog_debug ("  another path found, merge");

      prev = (struct ospf6_vertex *) route->route_option;
      assert (prev->hops <= v->hops);

      if (router_is_root)
        {
          for (i = 0; i < OSPF6_MULTI_PATH_LIMIT &&
                 ospf6_nexthop_is_set (&v->nexthop[i]); i++)
            {
              int err;

              err = ospf6_spf_add_nexthop (route->nexthop, &v->nexthop[i]);
              if (err)
                break;
            }

          /* copy merged results (all nexthops) back to vertex so future
           * children have access to complete nexthop information
           */
          for (i = 0; i < OSPF6_MULTI_PATH_LIMIT; i++)
            {
              struct ospf6_nexthop *nexthop = &route->nexthop[i];
              struct listnode *node;
              struct ospf6_vertex *w;

              ospf6_nexthop_copy (&prev->nexthop[i], nexthop);

              /* add nexthop to any existing children */
              if (ospf6_nexthop_is_set (nexthop))
                for (ALL_LIST_ELEMENTS_RO (prev->child_list, node, w))
                  ospf6_spf_add_nexthop (w->nexthop, nexthop);
            }
        }

      ospf6_vertex_delete (v);

      return -1;
    }

  /* There should be no case where candidate being installed (variable
     "v") is closer than the one in the SPF tree (variable "route").
     In the case something has gone wrong with the behavior of
     Priority-Queue. */

  /* the case where the route exists already is handled and returned
     up to here. */
  assert (route == NULL);

  route = ospf6_route_create ();
  memcpy (&route->prefix, &v->vertex_id, sizeof (struct prefix));
  route->type = OSPF6_DEST_TYPE_LINKSTATE;
  route->path.type = OSPF6_PATH_TYPE_INTRA;
  route->path.origin.type = v->lsa->header->type;
  route->path.origin.id = v->lsa->header->id;
  route->path.origin.adv_router = v->lsa->header->adv_router;
  route->path.metric_type = 1;
  route->path.cost = v->cost;
  route->path.cost_e2 = v->hops;
  route->path.router_bits = v->capability;
  route->path.options[0] = v->options[0];
  route->path.options[1] = v->options[1];
  route->path.options[2] = v->options[2];

  if (router_is_root)
    {
      for (i = 0; i < OSPF6_MULTI_PATH_LIMIT &&
             ospf6_nexthop_is_set (&v->nexthop[i]); i++)
        {
          ospf6_nexthop_copy (&route->nexthop[i], &v->nexthop[i]);
        }

      /* no nexthop should only happen when v is the root router */
      assert (i != 0 || v->lsa->header->adv_router == ospf6->router_id);
    }

  route->route_option = v;

  ospf6_route_add (route, result_table);
  return 0;
}

void
ospf6_spf_table_finish (struct ospf6_route_table *result_table)
{
  struct ospf6_route *route;
  struct ospf6_vertex *v;
  for (route = ospf6_route_head (result_table); route;
       route = ospf6_route_next (route))
    {
      v = (struct ospf6_vertex *) route->route_option;
      ospf6_vertex_delete (v);
      ospf6_route_remove (route, result_table);
    }
}

/* RFC2328 16.1.  Calculating the shortest-path tree for an area */
/* RFC2740 3.8.1.  Calculating the shortest path tree for an area */
void
ospf6_spf_calculation (u_int32_t router_id,
                       struct ospf6_route_table *result_table,
                       struct ospf6_area *oa)
{
  struct pqueue *candidate_list;
  struct ospf6_vertex *root, *v, *w;
  int i;
  int size;
  caddr_t lsdesc;
  struct ospf6_lsa *lsa;
  bool router_is_root;
  u_char all_root_neighbors_added = 0;

  ospf6_spf_table_finish (result_table);

  /* Install the calculating router itself as the root of the SPF tree */
  /* construct root vertex */
  lsa = ospf6_spf_lsdb_lookup (htons (OSPF6_LSTYPE_ROUTER), htonl (0),
			       router_id, oa->lsdb);
  if (lsa == NULL)
    return;

  /* initialize */
  candidate_list = pqueue_create ();
  candidate_list->cmp = ospf6_vertex_cmp;

  root = ospf6_vertex_create (lsa, NULL);
  root->area = oa;
  root->cost = 0;
  root->hops = 0;

  pqueue_enqueue (root, candidate_list);        // add root to candidate list

  router_is_root = (router_id == oa->ospf6->router_id);

  // If this router is the root,
  // For each manet interface, add all routable and Full neighbors for which
  // LSA exists to candidate list.
  if (router_is_root)
    {
      struct listnode *i2;
      struct ospf6_interface *oi;

      all_root_neighbors_added = 1;

      for (ALL_LIST_ELEMENTS_RO (oa->if_list, i2, oi))
	{
	  struct listnode *j;
	  struct ospf6_neighbor *on;

	  if (oi->state == OSPF6_INTERFACE_DOWN)
	    continue;
	  if (oi->type != OSPF6_IFTYPE_MDR)
	    {
	      if (oi->type != OSPF6_IFTYPE_LOOPBACK)
		all_root_neighbors_added = 0;
	      continue;
	    }
	  if (oi->mdr.AdjConnectivity == OSPF6_ADJ_FULLYCONNECTED &&
	      oi->mdr.LSAFullness == OSPF6_LSA_FULLNESS_FULL)
	    {
	      all_root_neighbors_added = 0;
	      continue;
	    }

	  for (ALL_LIST_ELEMENTS_RO (oi->neighbor_list, j, on))
	    {
	      // Add appropriate neighbors to the candidate list.
	      // This is done here instead of processing the root's LSA
	      // below, since next hop routers need not be in LSA.
	      // Consider all routable and Full neighbors.
	      if (on->mdr.routable || on->state == OSPF6_NEIGHBOR_FULL)
		{
		  struct ospf6_lsa *tmplsa;
		  struct in6_addr *linklocal_addr;
                  char *from;

		  lsa = ospf6_spf_lsdb_lookup (htons (OSPF6_LSTYPE_ROUTER),
					       htonl (0), on->router_id,
					       oa->lsdb);
		  if (lsa == NULL)
		    continue;

		  tmplsa = ospf6_lsdb_lookup (htons (OSPF6_LSTYPE_LINK),
					      htonl (on->ifindex),
					      on->router_id, oi->lsdb);
		  if (tmplsa)
                    {
                      struct ospf6_link_lsa *link_lsa;

                      link_lsa = (struct ospf6_link_lsa *)
                        OSPF6_LSA_HEADER_END (tmplsa->header);
                      linklocal_addr = &link_lsa->linklocal_addr;
                      from = tmplsa->name;
                    }
		  else if (ospf6_af_is_ipv6 (oa->ospf6) &&
			   IN6_IS_ADDR_LINKLOCAL (&on->linklocal_addr))
                    {
                      linklocal_addr = &on->linklocal_addr;
                      from = on->name;
                    }
		  else
                    {
                      linklocal_addr = NULL;
                    }

		  if (linklocal_addr != NULL)
		    {
		      v = ospf6_vertex_create (lsa, root);
		      v->area = oa;
		      v->cost = on->cost;
		      v->hops = 1;
                      ospf6_set_nexthop(&v->nexthop[0], oi->interface->ifindex,
                                        linklocal_addr, from);

                      if (IS_OSPF6_DEBUG_SPF (PROCESS))
                        zlog_debug ("  New candidate: %s hops %d cost %d",
                                    v->name, v->hops, v->cost);

		      pqueue_enqueue (v, candidate_list);
		    }
		  else if (IS_OSPF6_DEBUG_SPF (PROCESS))
		    {
		      char buf[INET_ADDRSTRLEN];

		      ospf6_id2str (on->router_id, buf, sizeof (buf));
		      zlog_debug ("%s: no nexthop found for %s",
				  __func__, buf);
		    }
		}
	    }
	}
    }

  /* Iterate until candidate-list becomes empty */
  while (candidate_list->size)
    {
      /* get closest candidate from priority queue */
      v = pqueue_dequeue (candidate_list);

      /* installing may result in merging or rejecting of the vertex */
      if (ospf6_spf_install (v, result_table, router_is_root) < 0)
        continue;

      // Except for the case of fully connected adjacencies and full LSAs,
      // the appropriate neighbors of the root have already been added
      // to candidate list.
      if (v == root && all_root_neighbors_added)
        continue;

      /* For each LS description in the just-added vertex V's LSA */
      size = (VERTEX_IS_TYPE (ROUTER, v) ?
              sizeof (struct ospf6_router_lsdesc) :
              sizeof (struct ospf6_network_lsdesc));
      for (lsdesc = OSPF6_LSA_HEADER_END (v->lsa->header) + 4;
           lsdesc + size <= OSPF6_LSA_END (v->lsa->header); lsdesc += size)
        {
	  int enqueue;

          lsa = ospf6_lsdesc_lsa (lsdesc, v);
          if (lsa == NULL)
            continue;

          if (! ospf6_lsdesc_backlink (lsa, lsdesc, v))
            continue;

          w = ospf6_vertex_create (lsa, v);
          w->area = oa;
          if (VERTEX_IS_TYPE (ROUTER, v))
            {
              w->cost = v->cost + ROUTER_LSDESC_GET_METRIC (lsdesc);
              w->hops = v->hops + (VERTEX_IS_TYPE (NETWORK, w) ? 0 : 1);
            }
          else /* NETWORK */
            {
              w->cost = v->cost;
              w->hops = v->hops + 1;
            }

          /* nexthop calculation */
	  enqueue = 1;
          if (router_is_root)
            {
              if (w->hops == 0)
                {
                  w->nexthop[0].ifindex = ROUTER_LSDESC_GET_IFID (lsdesc);
                }
              else if (w->hops == 1 && v->hops == 0)
                {
                  int err;
                  err = ospf6_nexthop_calc (w, v, lsdesc);
                  if (err)
                    enqueue = 0;
                }
              else
                {
                  for (i = 0; i < OSPF6_MULTI_PATH_LIMIT &&
                         ospf6_nexthop_is_set (&v->nexthop[i]); i++)
                    ospf6_nexthop_copy (&w->nexthop[i], &v->nexthop[i]);
                }
            }

	  if (enqueue)
	    {
	      /* add new candidate to the candidate_list */
	      if (IS_OSPF6_DEBUG_SPF (PROCESS))
		zlog_debug ("  New candidate: %s hops %d cost %d",
			    w->name, w->hops, w->cost);
	      pqueue_enqueue (w, candidate_list);
	    }
	  else
	    {
	      if (IS_OSPF6_DEBUG_SPF (PROCESS))
		zlog_debug ("  Ignoring vertex: %s hops %d cost %d",
			    w->name, w->hops, w->cost);
	      ospf6_vertex_delete (w);
	    }
        }
    }

  pqueue_delete (candidate_list);
}

static void
ospf6_spf_log_database (struct ospf6_area *oa)
{
  char *p, *end, buffer[256];
  struct listnode *node;
  struct ospf6_interface *oi;

  p = buffer;
  end = buffer + sizeof (buffer);

  snprintf (p, end - p, "SPF on DB (#LSAs):");
  p = (buffer + strlen (buffer) < end ? buffer + strlen (buffer) : end);
  snprintf (p, end - p, " Area %s: %d", oa->name, oa->lsdb->count);
  p = (buffer + strlen (buffer) < end ? buffer + strlen (buffer) : end);

  for (ALL_LIST_ELEMENTS_RO (oa->if_list, node, oi))
    {
      snprintf (p, end - p, " I/F %s: %d",
                oi->interface->name, oi->lsdb->count);
      p = (buffer + strlen (buffer) < end ? buffer + strlen (buffer) : end);
    }

  zlog_debug ("%s", buffer);
}

static int
ospf6_spf_calculation_thread (struct thread *t)
{
  struct ospf6_area *oa;
  struct timeval start, end, runtime;
  struct listnode *node;
  struct ospf6_interface *oi;
  int change;

  oa = (struct ospf6_area *) THREAD_ARG (t);
  oa->thread_spf_calculation = NULL;

  if (IS_OSPF6_DEBUG_SPF (PROCESS))
    zlog_debug ("SPF calculation for Area %s", oa->name);
  if (IS_OSPF6_DEBUG_SPF (DATABASE))
    ospf6_spf_log_database (oa);

  /* execute SPF calculation */
  quagga_gettime (QUAGGA_CLK_MONOTONIC, &start);
  ospf6_spf_calculation (oa->ospf6->router_id, oa->spf_table, oa);
  quagga_gettime (QUAGGA_CLK_MONOTONIC, &end);
  timersub (&end, &start, &runtime);

  if (IS_OSPF6_DEBUG_SPF (PROCESS) || IS_OSPF6_DEBUG_SPF (TIME))
    zlog_debug ("SPF runtime: %ld sec %ld usec",
		runtime.tv_sec, runtime.tv_usec);

  ospf6_intra_route_calculation (oa);
  ospf6_intra_brouter_calculation (oa);

  quagga_gettime (QUAGGA_CLK_MONOTONIC, &oa->last_spftime);

  change = 0;
  for (ALL_LIST_ELEMENTS_RO (oa->if_list, node, oi))
    {
      if (oi->type == OSPF6_IFTYPE_MDR &&
          oi->mdr.update_routable_neighbors_immediately)
        {
          change = ospf6_mdr_update_routable_neighbors (oi);
          if (change)
            break;
        }
    }

  // rerun spf if the set of routable neighbors has changed
  if (change)
    {
      ospf6_spf_calculation (oa->ospf6->router_id, oa->spf_table, oa);
      ospf6_intra_route_calculation (oa);
      ospf6_intra_brouter_calculation (oa);
    }

  return 0;
}

void
ospf6_spf_schedule (struct ospf6_area *oa)
{
  struct timeval now, *since;
  long delay_msec;

  if (oa->thread_spf_calculation)
    return;

  if (timerisset (&oa->last_spftime))
    since = &oa->last_spftime;
  else
    since = &oa->ospf6->starttime;

  quagga_gettime (QUAGGA_CLK_MONOTONIC, &now);

  delay_msec = oa->spf_holdtime_msec - timersub_msec (&now, since);
  if (delay_msec < (long)oa->spf_delay_msec)
    delay_msec = oa->spf_delay_msec;

  if (delay_msec && IS_OSPF6_DEBUG_SPF (PROCESS))
    zlog_debug ("%s: delaying spf calculation %li msec",
		__func__, delay_msec);

  oa->thread_spf_calculation =
    thread_add_timer_msec (master, ospf6_spf_calculation_thread,
			   oa, delay_msec);
}

void
ospf6_spf_display_subtree (struct vty *vty, const char *prefix, int rest,
                           struct ospf6_vertex *v)
{
  struct listnode *node, *nnode;
  struct ospf6_vertex *c;
  char *next_prefix;
  int len;
  int restnum;

  /* "prefix" is the space prefix of the display line */
  vty_out (vty, "%s+-%s [%d]%s", prefix, v->name, v->cost, VNL);

  len = strlen (prefix) + 4;
  next_prefix = (char *) malloc (len);
  if (next_prefix == NULL)
    {
      vty_out (vty, "malloc failed%s", VNL);
      return;
    }
  snprintf (next_prefix, len, "%s%s", prefix, (rest ? "|  " : "   "));

  restnum = listcount (v->child_list);
  for (ALL_LIST_ELEMENTS (v->child_list, node, nnode, c))
    {
      restnum--;
      ospf6_spf_display_subtree (vty, next_prefix, restnum, c);
    }

  free (next_prefix);
}

DEFUN (debug_ospf6_spf_process,
       debug_ospf6_spf_process_cmd,
       "debug ospf6 spf process",
       DEBUG_STR
       OSPF6_STR
       "Debug SPF Calculation\n"
       "Debug Detailed SPF Process\n"
      )
{
  unsigned char level = 0;
  level = OSPF6_DEBUG_SPF_PROCESS;
  OSPF6_DEBUG_SPF_ON (level);
  return CMD_SUCCESS;
}

DEFUN (debug_ospf6_spf_time,
       debug_ospf6_spf_time_cmd,
       "debug ospf6 spf time",
       DEBUG_STR
       OSPF6_STR
       "Debug SPF Calculation\n"
       "Measure time taken by SPF Calculation\n"
      )
{
  unsigned char level = 0;
  level = OSPF6_DEBUG_SPF_TIME;
  OSPF6_DEBUG_SPF_ON (level);
  return CMD_SUCCESS;
}

DEFUN (debug_ospf6_spf_database,
       debug_ospf6_spf_database_cmd,
       "debug ospf6 spf database",
       DEBUG_STR
       OSPF6_STR
       "Debug SPF Calculation\n"
       "Log number of LSAs at SPF Calculation time\n"
      )
{
  unsigned char level = 0;
  level = OSPF6_DEBUG_SPF_DATABASE;
  OSPF6_DEBUG_SPF_ON (level);
  return CMD_SUCCESS;
}

DEFUN (no_debug_ospf6_spf_process,
       no_debug_ospf6_spf_process_cmd,
       "no debug ospf6 spf process",
       NO_STR
       DEBUG_STR
       OSPF6_STR
       "Quit Debugging SPF Calculation\n"
       "Quit Debugging Detailed SPF Process\n"
      )
{
  unsigned char level = 0;
  level = OSPF6_DEBUG_SPF_PROCESS;
  OSPF6_DEBUG_SPF_OFF (level);
  return CMD_SUCCESS;
}

DEFUN (no_debug_ospf6_spf_time,
       no_debug_ospf6_spf_time_cmd,
       "no debug ospf6 spf time",
       NO_STR
       DEBUG_STR
       OSPF6_STR
       "Quit Debugging SPF Calculation\n"
       "Quit Measuring time taken by SPF Calculation\n"
      )
{
  unsigned char level = 0;
  level = OSPF6_DEBUG_SPF_TIME;
  OSPF6_DEBUG_SPF_OFF (level);
  return CMD_SUCCESS;
}

DEFUN (no_debug_ospf6_spf_database,
       no_debug_ospf6_spf_database_cmd,
       "no debug ospf6 spf database",
       NO_STR
       DEBUG_STR
       OSPF6_STR
       "Debug SPF Calculation\n"
       "Quit Logging number of LSAs at SPF Calculation time\n"
      )
{
  unsigned char level = 0;
  level = OSPF6_DEBUG_SPF_DATABASE;
  OSPF6_DEBUG_SPF_OFF (level);
  return CMD_SUCCESS;
}

int
config_write_ospf6_debug_spf (struct vty *vty)
{
  if (IS_OSPF6_DEBUG_SPF (PROCESS))
    vty_out (vty, "debug ospf6 spf process%s", VNL);
  if (IS_OSPF6_DEBUG_SPF (TIME))
    vty_out (vty, "debug ospf6 spf time%s", VNL);
  if (IS_OSPF6_DEBUG_SPF (DATABASE))
    vty_out (vty, "debug ospf6 spf database%s", VNL);
  return 0;
}

void
install_element_ospf6_debug_spf (void)
{
  install_element (ENABLE_NODE, &debug_ospf6_spf_process_cmd);
  install_element (ENABLE_NODE, &debug_ospf6_spf_time_cmd);
  install_element (ENABLE_NODE, &debug_ospf6_spf_database_cmd);
  install_element (ENABLE_NODE, &no_debug_ospf6_spf_process_cmd);
  install_element (ENABLE_NODE, &no_debug_ospf6_spf_time_cmd);
  install_element (ENABLE_NODE, &no_debug_ospf6_spf_database_cmd);
  install_element (CONFIG_NODE, &debug_ospf6_spf_process_cmd);
  install_element (CONFIG_NODE, &debug_ospf6_spf_time_cmd);
  install_element (CONFIG_NODE, &debug_ospf6_spf_database_cmd);
  install_element (CONFIG_NODE, &no_debug_ospf6_spf_process_cmd);
  install_element (CONFIG_NODE, &no_debug_ospf6_spf_time_cmd);
  install_element (CONFIG_NODE, &no_debug_ospf6_spf_database_cmd);
}

void
ospf6_spf_init (void)
{
}


