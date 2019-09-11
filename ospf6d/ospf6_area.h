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

#ifndef OSPF_AREA_H
#define OSPF_AREA_H

#include "ospf6_top.h"

struct ospf6_area
{
  /* Reference to Top data structure */
  struct ospf6 *ospf6;

  /* Area-ID */
  u_int32_t area_id;

  /* Area-ID string */
  char name[16];

  /* flag */
  u_char flag;

  /* OSPF Option */
  u_char options[3];

  /* Summary routes to be originated (includes Configured Address Ranges) */
  struct ospf6_route_table *range_table;
  struct ospf6_route_table *summary_prefix;
  struct ospf6_route_table *summary_router;
  u_int32_t inter_area_id;

  /* OSPF interface list */
  struct list *if_list;

  struct ospf6_lsdb *lsdb;
  struct ospf6_lsdb *lsdb_self;

  struct ospf6_route_table *spf_table;
  struct ospf6_route_table *route_table;

  struct thread  *thread_spf_calculation;
  struct timeval last_spftime;
  unsigned int spf_delay_msec;
  unsigned int spf_holdtime_msec;

  struct thread *thread_router_lsa;
  struct thread *thread_intra_prefix_lsa;
  u_int32_t router_lsa_size_limit;

  /* Area announce list */
  struct
  {
    char *name;
    struct access_list *list;
  } _export;
#define EXPORT_NAME(A)  (A)->_export.name
#define EXPORT_LIST(A)  (A)->_export.list

  /* Area acceptance list */
  struct
  {
    char *name;
    struct access_list *list;
  } import;
#define IMPORT_NAME(A)  (A)->import.name
#define IMPORT_LIST(A)  (A)->import.list

  /* Type 3 LSA Area prefix-list */
  struct
  {
    char *name;
    struct prefix_list *list;
  } plist_in;
#define PREFIX_NAME_IN(A)  (A)->plist_in.name
#define PREFIX_LIST_IN(A)  (A)->plist_in.list

  struct
  {
    char *name;
    struct prefix_list *list;
  } plist_out;
#define PREFIX_NAME_OUT(A)  (A)->plist_out.name
#define PREFIX_LIST_OUT(A)  (A)->plist_out.list

  struct list *private_data_list;
};

#define OSPF6_AREA_ENABLE     0x01
#define OSPF6_AREA_ACTIVE     0x02
#define OSPF6_AREA_TRANSIT    0x04 /* TransitCapability */
#define OSPF6_AREA_STUB       0x08

#define BACKBONE_AREA_ID (htonl (0))
#define IS_AREA_BACKBONE(oa) ((oa)->area_id == BACKBONE_AREA_ID)
#define IS_AREA_ENABLED(oa) (CHECK_FLAG ((oa)->flag, OSPF6_AREA_ENABLE))
#define IS_AREA_ACTIVE(oa) (CHECK_FLAG ((oa)->flag, OSPF6_AREA_ACTIVE))
#define IS_AREA_TRANSIT(oa) (CHECK_FLAG ((oa)->flag, OSPF6_AREA_TRANSIT))
#define IS_AREA_STUB(oa) (CHECK_FLAG ((oa)->flag, OSPF6_AREA_STUB))

#define OSPF6_DEFAULT_SPF_DELAY_MSEC	100
#define OSPF6_DEFAULT_SPF_HOLDTIME_MSEC	500

struct vty;

/* prototypes */
extern int ospf6_area_cmp (void *va, void *vb);

extern struct ospf6_area *ospf6_area_create (u_int32_t, struct ospf6 *);
extern void ospf6_area_delete (struct ospf6_area *);
extern struct ospf6_area *ospf6_area_lookup (u_int32_t, struct ospf6 *);
extern struct ospf6_area *ospf6_area_get (u_int32_t, struct ospf6 *);

extern void ospf6_area_enable (struct ospf6_area *);
extern void ospf6_area_disable (struct ospf6_area *);

extern void ospf6_area_show (struct vty *, struct ospf6_area *);

extern void ospf6_area_config_write (struct vty *vty);
extern void ospf6_area_init (void);
extern void ospf6_area_terminate (void);

extern int ospf6_area_add_data (struct ospf6_area *oa,
				unsigned int *id, void *data);
extern void *ospf6_area_get_data (struct ospf6_area *oa, unsigned int id);
extern void *ospf6_area_del_data (struct ospf6_area *oa, unsigned int id);

/**
 * The structure used to register area callbacks.
 *
 * Callback functions can be NULL if they are not needed.
 */
struct ospf6_area_operations {
  /**
   * An initialization function
   *
   * If given, this function is called once either as the process is
   * starting or when the area operations are registered.  This
   * can be used for general initialization such as installing vty
   * command elements.
   */
  void (*init) (void);

  /**
   * An area create callback function
   *
   * This function is called when a new ospf area is created and,
   * for any existing areas, when the area operations are
   * registered.  A new area will not be created if this function
   * fails.
   *
   * @param oa The new ospf area.
   *
   * @return Zero on success.  Nonzero if an error occurred.
   */
  int (*create) (struct ospf6_area *oa);

  /**
   * An area delete callback function
   *
   * This function is called when an OSPF area is deleted.  It
   * should perform cleanup and free resources as needed.
   *
   * @param oa The ospf area to be deleted.
   */
  void (*delete) (struct ospf6_area *oa);

  /**
   * A configuration callback function
   *
   * This function is called to describe the current configuration.
   * The vty commands needed to change default operation should be
   * generated.
   *
   * @param oa The ospf area.
   *
   * @param vty The VTY structure to use.
   */
  void (*config_write) (struct ospf6_area *oa, struct vty *vty);
};

extern int
ospf6_area_register_operations (struct ospf6_area_operations *ops);

/**
 * A macro to automatically register area operations
 *
 * This macro can be used to have area operations registered as
 * the process starts.
 *
 * @param ops A pointer to the area operations structure to be
 *            registered.
 */
#define OSPF6_AREA_OPERATIONS(ops)					\
  static void __attribute__((constructor)) __ ## ops ## _init (void)	\
  {									\
    int err;								\
									\
    err = ospf6_area_register_operations (&ops);			\
    assert (!err);							\
  }

#endif /* OSPF_AREA_H */
