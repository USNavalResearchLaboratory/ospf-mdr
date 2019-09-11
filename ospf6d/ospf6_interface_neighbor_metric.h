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

#ifndef _OSPF6_INTERFACE_NEIGHBOR_METRIC_H_
#define _OSPF6_INTERFACE_NEIGHBOR_METRIC_H_

#include "ospf6_neighbor.h"

/**
 * The structure used to register a neighbor metric manager.
 *
 * A name is required, all other members are optional and can be NULL
 * if not needed.
 */
struct ospf6_interface_neighbor_metric_params {
  /**
   * The name of this neighbor metric manager.
   */
  const char *name;

  /**
   * A delete callback function
   *
   * This function is called when the neighbor metric manager is
   * deleted.  It should perform cleanup and free resources as needed.
   *
   * @param oi The ospf interface that the metric manager is being
   *           removed from.
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

  /**
   * The set of neighbor operation callbacks used to notify the metric
   * manager of neighbor events.
   */
  struct ospf6_neighbor_operations nbrops;

  /**
   * A pointer to private data that can be set when
   * ospf6_interface_register_neighbor_metric() is called and
   * retrieved later using ospf6_interface_neighbor_metric_data().
   */
  void *data;
};

/**
 * Register a neighbor metric manager
 *
 * A neighbor metric manager can manipulate costs independently for
 * each neighbor on a given ospf interface.  Only one metric manager
 * can be active at a time.  A unique stable identifier, @c id, is
 * associated with each neighbor metric manager.  The value @c id
 * points to must be zero when this function is first called to
 * register a particular metric manager; a new id value will be
 * assigned which is then unchanged for later registrations, across
 * all interfaces.
 *
 * @param oi The ospf interface that the neighbor metric manager will
 *           be registered for.
 *
 * @param id A pointer to the metric manager identifier.
 *
 * @param params The neighbor metric manager parameter structure.
 *
 * @param vty An optional VTY structure used for any error messages.
 *
 * @return Zero on success.  Nonzero if an error occurred.
 */
int
ospf6_interface_register_neighbor_metric (struct ospf6_interface *oi,
					  unsigned int *id,
					  const struct ospf6_interface_neighbor_metric_params *params,
					  struct vty *vty);
/**
 * Enable a neighbor metric manager
 *
 * This function enables the neighbor event callbacks of a metric
 * manager and prevents another metric manager from registering.
 *
 * @param oi The ospf interface.
 *
 * @param id The neighbor metric manager identifier.
 *
 * @return Zero on success.  Nonzero if an error occurred.
 */
int
ospf6_interface_enable_neighbor_metric (struct ospf6_interface *oi,
					unsigned int id);

/**
 * Disable a neighbor metric manager
 *
 * This function disables the neighbor event callbacks of a metric
 * manager and allows another metric manager to register.
 *
 * @param oi The ospf interface.
 *
 * @param id The neighbor metric manager identifier.
 *
 * @return Zero on success.  Nonzero if an error occurred.
 */
int
ospf6_interface_disable_neighbor_metric (struct ospf6_interface *oi,
					 unsigned int id);

/**
 * Update a neighbor's cost
 *
 * Update the cost metric for the given neighbor.  Neighbor metric
 * managers should only change a neighbor's cost using this function.
 * This function fails if the calling metric manager is not currently
 * registered on the interface @c on is associated with.  The new
 * metric assigned to a neighbor is never less than the current
 * interface cost.
 *
 * @param on The ospf neighbor.
 *
 * @param newmetric The new cost metric.
 *
 * @param id The neighbor metric manager identifier.
 *
 * @return Zero on success.  Nonzero if an error occurred.
 */
int
ospf6_interface_update_neighbor_metric (struct ospf6_neighbor *on,
					u_int16_t newmetric, unsigned int id);

/**
 * Reset the cost metric of all neighbors
 *
 * This function sets the cost metric of all neighbors to the current
 * interface cost.
 *
 * @param oi The ospf interface.
 *
 * @param id The neighbor metric manager identifier.
 *
 * @return Zero on success.  Nonzero if an error occurred.
 */
int
ospf6_interface_reset_neighbor_metric (struct ospf6_interface *oi,
				       unsigned int id);

/**
 * Check if a neighbor metric manager is registered.
 *
 * @param oi The ospf interface.
 *
 * @param id The neighbor metric manager identifier.
 *
 * @return Nonzero if @c id is the currently registered metric
 *         manager; zero otherwise.
 */
int
ospf6_interface_neighbor_metric_registered (struct ospf6_interface *oi,
					    unsigned int id);

/**
 * Check if a neighbor metric manager is enabled.
 *
 * @param oi The ospf interface.
 *
 * @param id The neighbor metric manager identifier.
 *
 * @return Nonzero if @c id is the currently registered metric manager
 *         and is enabled; zero otherwise.
 */
int
ospf6_interface_neighbor_metric_enabled (struct ospf6_interface *oi,
					 unsigned int id);

/**
 * Get private data
 *
 * This function returns the private data associated with a neighbor
 * metric manager during registration.
 *
 * @param oi The ospf interface.
 *
 * @param id The neighbor metric manager identifier.
 *
 * @return The data pointer from
 *         ospf6_interface_register_neighbor_metric() on success.
 *         NULL if an error occurred.
 */
void *
ospf6_interface_neighbor_metric_data (struct ospf6_interface *oi,
				      unsigned int id);
#endif	/* _OSPF6_INTERFACE_NEIGHBOR_METRIC_H_ */
