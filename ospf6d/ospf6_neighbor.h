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

#ifndef OSPF6_NEIGHBOR_H
#define OSPF6_NEIGHBOR_H

#include "ospf6_message.h"
#include "ospf6_mdr_neighbor.h"

/* Debug option */
extern unsigned char conf_debug_ospf6_neighbor;
#define OSPF6_DEBUG_NEIGHBOR_STATE   0x01
#define OSPF6_DEBUG_NEIGHBOR_EVENT   0x02
#define OSPF6_DEBUG_NEIGHBOR_ON(level) \
  (conf_debug_ospf6_neighbor |= (level))
#define OSPF6_DEBUG_NEIGHBOR_OFF(level) \
  (conf_debug_ospf6_neighbor &= ~(level))
#define IS_OSPF6_DEBUG_NEIGHBOR(level) \
  (conf_debug_ospf6_neighbor & OSPF6_DEBUG_NEIGHBOR_ ## level)

/* Neighbor structure */
struct ospf6_neighbor
{
  /* Neighbor Router ID String */
  char name[32];

  /* OSPFv3 Interface this neighbor belongs to */
  struct ospf6_interface *ospf6_if;

  /* Neighbor state */
  u_char state;

  /* timestamp of last changing state */
  struct timeval last_changed;

  /* Neighbor Router ID */
  u_int32_t router_id;

  /* Neighbor Interface ID */
  u_int32_t ifindex;

  /* Router Priority of this neighbor */
  u_char priority;

  u_int32_t drouter;
  u_int32_t bdrouter;
  u_int32_t prev_drouter;
  u_int32_t prev_bdrouter;

  /* Options field (Capability) */
  char options[3];

  /* IPaddr of I/F on our side link */
  struct in6_addr linklocal_addr;

  /* For Database Exchange */
  u_char               dbdesc_bits;
  u_int32_t            dbdesc_seqnum;
  /* Last received Database Description packet */
  struct ospf6_dbdesc  dbdesc_last;

  /* LS-list */
  struct ospf6_lsdb *summary_list;
  struct ospf6_lsdb *request_list;
  struct ospf6_lsdb *retrans_list;

  /* LSA list for message transmission */
  struct ospf6_lsdb *dbdesc_list;
  struct ospf6_lsdb *lsreq_list;
  struct ospf6_lsdb *lsupdate_list;
  struct ospf6_lsdb *lsack_list;

  /* Waiting to resend a link state request */
  bool request_retrans_wait;

  /* Inactivity timer */
  struct thread *inactivity_timer;

  /* Thread for sending message */
  struct thread *thread_send_dbdesc;
  struct thread *thread_send_lsreq;
  struct thread *thread_send_lsupdate;
  struct thread *thread_send_lsack;

  struct thread *thread_adjok;

  u_int32_t cost;		/* computed cost */

  struct ospf6_mdr_neighbor mdr;

  struct list *private_data_list;
};

struct ospf6_lls_header;

/**
 * Add private data to an ospf neighbor
 *
 * This function is analogous to ospf6_add_interface_data() and
 * associates private data with an ospf neighbor.  A unique data
 * identifier, meant to be stable across all neighbors, is used to
 * refer to the data.  A new identifier is assigned to the location
 * pointed to by @c id when its current value is zero.  Otherwise the
 * current value stored in @c id is used, which must not already be in
 * use for the given neighbor.
 *
 * @param on The ospf neighbor.
 *
 * @param id A pointer to the data id.
 *
 * @param data A pointer to the data.
 *
 * @return Zero on success.  Nonzero if an error occurred.
 */
int ospf6_add_neighbor_data (struct ospf6_neighbor *on,
			     unsigned int *id, void *data);

/**
 * Get private data associated with an ospf neighbor
 *
 * The given data identifier should be from an earlier
 * ospf6_add_neighbor_data() call and must be nonzero.
 *
 * @param on The ospf neighbor.
 *
 * @param id The data identifier.
 *
 * @return A pointer to the data reference by @c id or NULL if no data
 *         is found for the given identifier.
 */
void *ospf6_get_neighbor_data (struct ospf6_neighbor *on, unsigned int id);

/**
 * Get and remove private data associated with an ospf neighbor
 *
 * The given data identifier should be from an earlier
 * ospf6_add_neighbor_data() call and must be nonzero.
 *
 * @param on The ospf neighbor.
 *
 * @param id The data identifier.
 *
 * @return A pointer to the data reference by @c id or NULL if no data
 *         is found for the given identifier.
 */
void *ospf6_del_neighbor_data (struct ospf6_neighbor *on, unsigned int id);

/**
 * The structure used to register neighbor callbacks.
 *
 * Neighbor callbacks are registered independently for each ospf
 * interface.  Callback functions can be NULL if they are not needed.
 */
struct ospf6_neighbor_operations {
  /**
   * A neighbor create callback function
   *
   * This function is called when a new ospf neighbor is created and,
   * for any existing neighbors on a particular interface, when the
   * neighbor operations are registered.  A new neighbor will not be
   * created if this function fails.
   *
   * @param on The new ospf neighbor.
   *
   * @return Zero on success.  Nonzero if an error occurred.
   */
  int (*create) (struct ospf6_neighbor *on);

  /**
   * A neighbor delete callback function
   *
   * This function is called when an OSPF neighbor is deleted.  It
   * should perform cleanup and free resources as needed.
   *
   * @param on The new ospf neighbor.
   */
  void (*delete) (struct ospf6_neighbor *on);

  /**
   * A neighbor hello callback function
   *
   * This function is called when an ospf hello is received from a
   * neighbor.  Basic validation of the hello is done before calling
   * this callback.  Processing of the hello message does not continue
   * if this function returns nonzero.
   *
   * @param on The ospf neighbor.
   * @param oh A pointer to the ospf message header.
   * @param len The ospf message length, in bytes, including any
   *            link-local signaling data.
   *
   * @return Zero if hello processing should continue normally.
   *         Nonzero to suppress further processing.
   */
  int (*hello_recv) (struct ospf6_neighbor *on, struct ospf6_header *oh,
		     struct ospf6_lls_header *lls);

  /**
   * A neighbor state change callback function
   *
   * This function is called when the state of an existing OSPF
   * neighbor changes.
   *
   * @param on The new ospf neighbor.
   *
   * @param prev_state The neighbor's previous state.
   */
  void (*state_change) (struct ospf6_neighbor *on, u_char prev_state);

  /**
   * A neighbor operations remove callback function
   *
   * This function is called after the neighbor operations that were
   * previously registered for interface are removed.
   *
   * @param oi The ospf interface.
   *
   * @param ops A pointer to the neighbor operations structure that
   *            was removed.
   */
  void (*remove) (struct ospf6_interface *oi,
		  struct ospf6_neighbor_operations *ops);
};

/**
 * Register neighbor operations
 *
 * Neighbor operations are a set of callbacks that apply to all ospf
 * neighbors on the given interface.
 *
 * @param oi The ospf interface to register neighbor operations for.
 *
 * @param ops A pointer to the neighbor operations structure to
 *            register.  The pointer must remain valid as long as it
 *            remains registered.
 *
 * @return Zero on success.  Nonzero if an error occurred.
 */
int
ospf6_register_neighbor_operations (struct ospf6_interface *oi,
				    struct ospf6_neighbor_operations *ops);

/**
 * Remove neighbor operations
 *
 * This functions removes previously registered neighbor operations.
 *
 * @param oi The ospf interface to remove neighbor operations from.
 *
 * @param ops A pointer to the neighbor operations structure to
 *            remove.
 *
 * @return Zero on success.  Nonzero if an error occurred.
 */
int
ospf6_remove_neighbor_operations (struct ospf6_interface *oi,
				  struct ospf6_neighbor_operations *ops);

/* Neighbor state */
#define OSPF6_NEIGHBOR_DOWN     1
#define OSPF6_NEIGHBOR_ATTEMPT  2
#define OSPF6_NEIGHBOR_INIT     3
#define OSPF6_NEIGHBOR_TWOWAY   4
#define OSPF6_NEIGHBOR_EXSTART  5
#define OSPF6_NEIGHBOR_EXCHANGE 6
#define OSPF6_NEIGHBOR_LOADING  7
#define OSPF6_NEIGHBOR_FULL     8

extern const char *ospf6_neighbor_state_str[];


/* Function Prototypes */
int ospf6_neighbor_cmp (void *va, void *vb);

struct ospf6_neighbor *ospf6_neighbor_lookup (u_int32_t,
                                              struct ospf6_interface *);
struct ospf6_neighbor *ospf6_neighbor_create (u_int32_t,
                                              struct ospf6_interface *);
void ospf6_neighbor_delete (struct ospf6_neighbor *);

/* Neighbor event */
extern int hello_received (struct thread *);
extern int twoway_received (struct thread *);
extern int negotiation_done (struct thread *);
extern int exchange_done (struct thread *);
extern int loading_done (struct thread *);
extern int adj_ok (struct thread *);
extern int seqnumber_mismatch (struct thread *);
extern int bad_lsreq (struct thread *);
extern int oneway_received (struct thread *);
extern int inactivity_timer (struct thread *);
extern int need_adjacency (struct ospf6_neighbor *);
extern void ospf6_neighbor_exstart (struct ospf6_neighbor *);

extern void ospf6_neighbor_init (void);
extern int config_write_ospf6_debug_neighbor (struct vty *vty);
extern void install_element_ospf6_debug_neighbor (void);

extern void ospf6_neighbor_schedule_inactivity (struct ospf6_neighbor *on);
extern void ospf6_neighbor_schedule_adjok (struct ospf6_neighbor *on);

#endif /* OSPF6_NEIGHBOR_H */
