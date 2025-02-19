/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright 2010-2022, Tarantool AUTHORS, please see AUTHORS file.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "small/rlist.h"

#if defined(__cplusplus)
extern "C" {
#endif /* defined(__cplusplus) */

struct cord;
struct index;
struct index_read_view;
struct space;
struct space_upgrade_read_view;

/** Read view of a space. */
struct space_read_view {
	/** Link in read_view::spaces. */
	struct rlist link;
	/** Read view that owns this space. */
	struct read_view *rv;
	/** Space id. */
	uint32_t id;
	/** Space name. */
	char *name;
	/**
	 * Runtime tuple format needed to access tuple field names by name.
	 * Referenced (ref counter incremented).
	 *
	 * A new format is created only if read_view_opts::needs_field_names
	 * is set, otherwise runtime_tuple_format is used.
	 *
	 * We can't just use the space tuple format as is because it allocates
	 * tuples from the space engine arena, which is single-threaded, while
	 * a read view may be used from threads other than tx. Good news is
	 * runtime tuple formats are reusable so if we create more than one
	 * read view of the same space, we will use just one tuple format for
	 * them all.
	 */
	struct tuple_format *format;
	/**
	 * Upgrade function for this space read view or NULL if there wasn't
	 * a space upgrade in progress at the time when this read view was
	 * created or read_view_opts::needs_space_upgrade wasn't set.
	 */
	struct space_upgrade_read_view *upgrade;
	/** Replication group id. See space_opts::group_id. */
	uint32_t group_id;
	/**
	 * Max index id.
	 *
	 * (The number of entries in index_map is index_id_max + 1.)
	 */
	uint32_t index_id_max;
	/**
	 * Sparse (may contain nulls) array of index read views,
	 * indexed by index id.
	 */
	struct index_read_view **index_map;
};

static inline struct index_read_view *
space_read_view_index(struct space_read_view *space_rv, uint32_t id)
{
	return id <= space_rv->index_id_max ? space_rv->index_map[id] : NULL;
}

/** Read view of the entire database. */
struct read_view {
	/** List of engine read views, linked by engine_read_view::link. */
	struct rlist engines;
	/** List of space read views, linked by space_read_view::link. */
	struct rlist spaces;
	/** Thread that activated the read view, see read_view_activate(). */
	struct cord *owner;
};

#define read_view_foreach_space(space_rv, rv) \
	rlist_foreach_entry(space_rv, &(rv)->spaces, link)

/** Read view creation options. */
struct read_view_opts {
	/**
	 * Space filter: should return true if the space should be included
	 * into the read view.
	 *
	 * Default: include all spaces (return true, ignore arguments).
	 */
	bool
	(*filter_space)(struct space *space, void *arg);
	/**
	 * Index filter: should return true if the index should be included
	 * into the read view.
	 *
	 * Default: include all indexes (return true, ignore arguments).
	 */
	bool
	(*filter_index)(struct space *space, struct index *index, void *arg);
	/**
	 * Argument passed to filter functions.
	 */
	void *filter_arg;
	/**
	 * If this flag is set, a new runtime tuple format will be created for
	 * each read view space to support accessing tuple fields by name,
	 * otherwise the preallocated name-less runtime tuple format will be
	 * used instead.
	 */
	bool needs_field_names;
	/**
	 * If this flag is set and there's a space upgrade in progress at the
	 * time when this read view is created, create an upgrade function that
	 * can be applied to tuples retrieved from this read view. See also
	 * space_read_view::upgrade.
	 */
	bool needs_space_upgrade;
	/**
	 * Temporary spaces aren't included into this read view unless this
	 * flag is set.
	 */
	bool needs_temporary_spaces;
};

/** Sets read view options to default values. */
void
read_view_opts_create(struct read_view_opts *opts);

/**
 * Opens a database read view: all changes done to the database after a read
 * view was open will not be visible from the read view.
 *
 * Engines that don't support read view creation are silently skipped.
 *
 * A read view must be activated before use, see read_view_activate(). After a
 * read view is activated, it may only be use in the thread that activated it.
 *
 * Returns 0 on success. On error, returns -1 and sets diag.
 */
int
read_view_open(struct read_view *rv, const struct read_view_opts *opts);

/**
 * Closes a database read view.
 *
 * The read view must be deactivated, see read_view_deactivate().
 */
void
read_view_close(struct read_view *rv);

/**
 * Activates a read view for use in the current thread.
 *
 * Returns 0 on success. On error, returns -1 and sets diag.
 */
int
read_view_activate(struct read_view *rv);

/**
 * Deactivates a read view.
 *
 * A read view may only be deactivated by the thread that activated it.
 */
void
read_view_deactivate(struct read_view *rv);

/**
 * Prepares a tuple retrieved from a read view to be returned to the user.
 *
 * This function applies the space upgrade function if the read view was open
 * while the space upgrade was in progress. It may only be called in the thread
 * that activated the read view, see read_view_activate().
 *
 * If the tuple doesn't need any processing, it's returned as is, otherwise
 * a new tuple is allocated and pinned with tuple_bless. On error, NULL is
 * returned and diag is set.
 */
struct tuple *
read_view_process_result(struct space_read_view *space_rv, struct tuple *tuple);

#if defined(__cplusplus)
} /* extern "C" */
#endif /* defined(__cplusplus) */
