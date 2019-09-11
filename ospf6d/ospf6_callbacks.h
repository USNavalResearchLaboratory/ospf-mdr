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

#ifndef _OSPF6_CALLBACKS_H_
#define _OSPF6_CALLBACKS_H_

/**
 * A macro to call all hooks in a given list
 *
 * @param hooklist The list of hooks to run.
 *
 * @param func_t The hook function type.
 *
 * @param args Arguments passed to each hook function.
 */
#define RUN_HOOKS(hooklist, func_t, args...)		\
  do {							\
    struct listnode *node;				\
    func_t hook;					\
    for (ALL_LIST_ELEMENTS_RO (hooklist, node, hook))	\
      hook (args);					\
  } while (0)

/**
 * Add a hook to the given list
 *
 * @param hooklist The list to add the hook to.
 *
 * @param hook A pointer to the hook function.
 *
 * @return Zero on success.  Nonzero if an error occurred.
 */
int ospf6_add_hook (struct list *hooklist, void *hook);

/**
 * Remove a hook from the given list
 *
 * @param hooklist The list to remove the hook from.
 *
 * @param hook A pointer to the hook function.
 *
 * @return Zero on success.  Nonzero if an error occurred.
 */
int ospf6_remove_hook (struct list *hooklist, void *hook);

#endif	/* _OSPF6_CALLBACKS_H_ */
