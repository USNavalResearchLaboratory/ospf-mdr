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

#ifndef OSPF6_INTERFACE_H
#define OSPF6_INTERFACE_H

#include "if.h"
#include "ospf6_mdr_interface.h"

/* Debug option */
extern unsigned char conf_debug_ospf6_interface;
#define OSPF6_DEBUG_INTERFACE_ON() \
  (conf_debug_ospf6_interface = 1)
#define OSPF6_DEBUG_INTERFACE_OFF() \
  (conf_debug_ospf6_interface = 0)
#define IS_OSPF6_DEBUG_INTERFACE \
  (conf_debug_ospf6_interface)

/* Interface structure */
struct ospf6_interface
{
  /* IF info from zebra */
  struct interface *interface;

  /* back pointer */
  struct ospf6_area *area;

  /* list of ospf6 neighbor */
  struct list *neighbor_list;

  /* linklocal address of this I/F */
  struct in6_addr *linklocal_addr;

  /* IPv4 linklocal address of this I/F */
  struct in_addr *linklocal_addr_ipv4;

  /* Interface ID; use interface->ifindex */

  /* Instance ID; use area->ospf6->instance_id */

  /* I/F transmission delay */
  u_int32_t transdelay;

  /* Router Priority */
  u_char priority;

  /* Time Interval */
  u_int16_t hello_interval;
  u_int16_t dead_interval;
  u_int32_t rxmt_interval;

  unsigned int config_status;
#define HELLO_INTERVAL_CONFIGURED	(1U << 0)
#define DEAD_INTERVAL_CONFIGURED	(1U << 1)
#define RXMT_INTERVAL_CONFIGURED	(1U << 2)
#define LINK_LSA_SUPPRESSION_CONFIGURED	(1U << 3)
#define ALLOW_IMMEDIATE_HELLO_CONFIGURED (1U << 4)

  /* Cost */
  u_int32_t cost;
  bool cost_configured; /* true if an interface cost was configured */

  /* I/F MTU */
  u_int32_t ifmtu;

  /* Interface State */
  u_char state;

  /* OSPF6 Interface flag */
  char flag;

  /* MTU mismatch check */
  u_char mtu_ignore;

  /* Decision of DR Election */
  u_int32_t drouter;
  u_int32_t bdrouter;
  u_int32_t prev_drouter;
  u_int32_t prev_bdrouter;

  /* Linklocal LSA Database: includes Link-LSA */
  struct ospf6_lsdb *lsdb;
  struct ospf6_lsdb *lsdb_self;

  struct ospf6_lsdb *lsupdate_list;
  struct ospf6_lsdb *lsack_list;

  /* Ongoing Tasks */
  struct thread *thread_send_hello;
  struct thread *thread_send_lsupdate;
  struct thread *thread_send_lsack;

  struct thread *thread_network_lsa;
  struct thread *thread_link_lsa;
  struct thread *thread_intra_prefix_lsa;

  struct ospf6_route_table *route_connected;

  /* prefix-list name to filter connected prefix */
  char *plist_name;

  /* OSPF6 Interface Type */
  u_char type;

  int LinkLSASuppression;

  bool allow_immediate_hello;
  struct timeval last_hello_time;
  unsigned int initial_immediate_hello_delay; /* msec */
  unsigned int immediate_hello_delay;	      /* msec */

  int flood_delay;              //msec

  bool relax_neighbor_inactivity;
  unsigned int adjacency_formation_limit;

  struct ospf6_mdr_interface mdr;

  struct list *private_data_list;
};

struct vty;
struct ospf6;

/**
 * Add private data to an ospf interface
 *
 * Private data can be associated with an ospf interface by calling
 * this function.  A unique data identifier, meant to be stable across
 * all interfaces, is used to refer to the data.  A new identifier is
 * assigned to the location pointed to by @c id when its current value
 * is zero.  Otherwise the current value stored in @c id is used,
 * which must not already be in use for the given interface.
 *
 * @param oi The ospf interface.
 *
 * @param id A pointer to the data id.
 *
 * @param data A pointer to the data.
 *
 * @return Zero on success.  Nonzero if an error occurred.
 */
int ospf6_add_interface_data (struct ospf6_interface *oi,
			      unsigned int *id, void *data);

/**
 * Get private data associated with an ospf interface
 *
 * The given data identifier should be from an earlier
 * ospf6_add_interface_data() call and must be nonzero.
 *
 * @param oi The ospf interface.
 *
 * @param id The data identifier.
 *
 * @return A pointer to the data reference by @c id or NULL if no data
 *         is found for the given identifier.
 */
void *ospf6_get_interface_data (struct ospf6_interface *oi, unsigned int id);

/**
 * Get and remove private data associated with an ospf interface
 *
 * The given data identifier should be from an earlier
 * ospf6_add_interface_data() call and must be nonzero.
 *
 * @param oi The ospf interface.
 *
 * @param id The data identifier.
 *
 * @return A pointer to the data reference by @c id or NULL if no data
 *         is found for the given identifier.
 */
void *ospf6_del_interface_data (struct ospf6_interface *oi, unsigned int id);

/**
 * The structure used to register interface callbacks.
 *
 * Callback functions can be NULL if they are not needed.
 */
struct ospf6_interface_operations {
  /**
   * An initialization function
   *
   * If given, this function is called once either as the process is
   * starting or when the interface operations are registered.  This
   * can be used for general initialization such as installing vty
   * command elements.
   */
  void (*init) (void);

  /**
   * An interface create callback function
   *
   * This function is called when a new ospf interface is created and,
   * for any existing interfaces, when the interface operations are
   * registered.  A new interface will not be created if this function
   * fails.
   *
   * @param oi The new ospf interface.
   *
   * @return Zero on success.  Nonzero if an error occurred.
   */
  int (*create) (struct ospf6_interface *oi);

  /**
   * An interface delete callback function
   *
   * This function is called when an OSPF interface is deleted.  It
   * should perform cleanup and free resources as needed.
   *
   * @param oi The ospf interface to be deleted.
   */
  void (*delete) (struct ospf6_interface *oi);

  /**
   * A configuration callback function
   *
   * This function is called to describe the current configuration.
   * The vty commands needed to change default operation should be
   * generated.
   *
   * @param oi The ospf interface.
   *
   * @param vty The VTY structure to use.
   */
  void (*config_write) (struct ospf6_interface *oi, struct vty *vty);

  /**
   * An interface cost update callback function
   *
   * This function is called when the ospf interface cost changes.
   *
   * @param oi The affected ospf interface.
   */
  void (*cost_update) (struct ospf6_interface *oi);

};

/**
 * Register interface operations
 *
 * Interface operations are a set of callbacks that apply to all ospf
 * interfaces.
 *
 * @param ops A pointer to the interface operations structure to
 *            register.  The pointer must remain valid as long as it
 *            remains registered.
 *
 * @return Zero on success.  Nonzero if an error occurred.
 */
int
ospf6_register_interface_operations (struct ospf6_interface_operations *ops);

/**
 * A macro to automatically register interface operations
 *
 * This macro can be used to have interface operations registered as
 * the process starts.
 *
 * @param ops A pointer to the interface operations structure to be
 *            registered.
 */
#define OSPF6_INTERFACE_OPERATIONS(ops)					\
  static void __attribute__((constructor)) __ ## ops ## _init (void)	\
  {									\
    int err;								\
									\
    err = ospf6_register_interface_operations (&ops);			\
    assert (!err);							\
  }

#define OSPF6_IFTYPE_NONE              0
#define OSPF6_IFTYPE_POINTOPOINT       1
#define OSPF6_IFTYPE_BROADCAST         2
#define OSPF6_IFTYPE_NBMA              3
#define OSPF6_IFTYPE_POINTOMULTIPOINT  4
#define OSPF6_IFTYPE_VIRTUALLINK       5
#define OSPF6_IFTYPE_LOOPBACK          6
#define OSPF6_IFTYPE_MDR               7
#define OSPF6_IFTYPE_MAX               8

/* interface state */
#define OSPF6_INTERFACE_NONE             0
#define OSPF6_INTERFACE_DOWN             1
#define OSPF6_INTERFACE_LOOPBACK         2
#define OSPF6_INTERFACE_WAITING          3
#define OSPF6_INTERFACE_POINTTOPOINT     4
#define OSPF6_INTERFACE_DROTHER          5
#define OSPF6_INTERFACE_BDR              6
#define OSPF6_INTERFACE_DR               7
#define OSPF6_INTERFACE_MAX              8

extern const char *ospf6_interface_state_str[];

/* flags */
#define OSPF6_INTERFACE_DISABLE      0x01
#define OSPF6_INTERFACE_PASSIVE      0x02

/* default values */
#define OSPF6_INTERFACE_HELLO_INTERVAL 10
#define OSPF6_INTERFACE_DEAD_INTERVAL  40
#define OSPF6_INTERFACE_RXMT_INTERVAL  5
#define OSPF6_INTERFACE_COST           1
#define OSPF6_INTERFACE_PRIORITY       1
#define OSPF6_INTERFACE_TRANSDELAY     1
#define OSPF6_INTERFACE_FLOOD_DELAY    100

/* default values */
#define OSPF6_INITIAL_IMMEDIATE_HELLO_DELAY 2


/* Function Prototypes */

extern struct ospf6_interface *ospf6_interface_lookup_by_ifindex (int);
extern struct ospf6_interface *ospf6_interface_vtyget (struct vty *);
extern struct ospf6_interface *ospf6_interface_get (struct interface *);
extern void ospf6_interface_delete (struct ospf6_interface *);

extern void ospf6_interface_enable (struct ospf6_interface *);
extern void ospf6_interface_disable (struct ospf6_interface *);

extern void ospf6_interface_if_add (struct interface *);
extern void ospf6_interface_if_del (struct interface *);
extern void ospf6_interface_state_update (struct interface *);
extern bool ospf6_interface_has_linklocal_addr (struct ospf6_interface *);
extern void ospf6_interface_connected_route_update (struct interface *);
extern void ospf6_interface_update_bandwidth (struct ospf6_interface *);

extern bool ospf6_interface_prefix_is_connected (struct ospf6_interface *oi,
						 struct prefix *prefix);
extern bool ospf6_area_prefix_is_connected (struct ospf6_area *oa,
					    struct prefix *prefix);
extern bool ospf6_prefix_is_connected (struct ospf6 *o, struct prefix *prefix);

/* interface event */
extern int interface_up (struct thread *);
extern int interface_down (struct thread *);
extern int wait_timer (struct thread *);
extern int backup_seen (struct thread *);
extern int neighbor_change (struct thread *);

extern void ospf6_interface_init (void);
extern void ospf6_interface_terminate (void);

extern int config_write_ospf6_debug_interface (struct vty *vty);
extern void install_element_ospf6_debug_interface (void);

#endif /* OSPF6_INTERFACE_H */
