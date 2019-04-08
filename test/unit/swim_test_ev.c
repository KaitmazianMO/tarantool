/*
 * Copyright 2010-2019, Tarantool AUTHORS, please see AUTHORS file.
 *
 * Redistribution and use in source and binary forms, with or
 * without modification, are permitted provided that the following
 * conditions are met:
 *
 * 1. Redistributions of source code must retain the above
 *    copyright notice, this list of conditions and the
 *    following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above
 *    copyright notice, this list of conditions and the following
 *    disclaimer in the documentation and/or other materials
 *    provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY <COPYRIGHT HOLDER> ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * <COPYRIGHT HOLDER> OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
 * LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF
 * THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */
#include "swim_test_ev.h"
#include "swim_test_transport.h"
#include "trivia/util.h"
#include "swim/swim_ev.h"
#include "tarantool_ev.h"
#define HEAP_FORWARD_DECLARATION
#include "salad/heap.h"
#include "assoc.h"
#include "say.h"
#include <stdbool.h>

/** Global watch, propagated by new events. */
static double watch = 0;

/**
 * Increasing event identifiers are used to preserve order of
 * events with the same deadline.
 */
static int event_id = 0;

/**
 * SWIM testing event loop has two event types - natural libev
 * events like timer, and artificial like fake socket blocking.
 */
enum swim_event_type {
	SWIM_EVENT_TIMER,
	SWIM_EVENT_BRK,
};

struct swim_event;

typedef void (*swim_event_process_f)(struct swim_event *, struct ev_loop *);
typedef void (*swim_event_delete_f)(struct swim_event *);

/**
 * Base event. It is stored in the event heap and virtualizes
 * other events.
 */
struct swim_event {
	/** Type, for assertions only. */
	enum swim_event_type type;
	/**
	 * When that event should be invoked according to the fake
	 * watch.
	 */
	double deadline;
	/** A link in the event heap. */
	struct heap_node in_event_heap;
	/** ID to sort events with the same deadline. */
	int id;
	/**
	 * Process the event. Usually the event is deleted right
	 * after that.
	 */
	swim_event_process_f process;
	/** Just delete the event. Called on event heap reset. */
	swim_event_delete_f delete;
};

/**
 * Heap comparator. Heap's top stores an event with the nearest
 * deadline and the smallest ID in that deadline.
 */
static inline bool
swim_event_less(const struct swim_event *e1, const struct swim_event *e2)
{
	if (e1->deadline == e2->deadline)
		return e1->id < e2->id;
	return e1->deadline < e2->deadline;
}

#define HEAP_NAME event_heap
#define HEAP_LESS(h, e1, e2) swim_event_less(e1, e2)
#define heap_value_t struct swim_event
#define heap_value_attr in_event_heap
#include "salad/heap.h"

/** Event heap. Event loop pops them from here. */
static heap_t event_heap;

/** Libev watcher is matched to exactly one event here. */
static struct mh_i64ptr_t *events_hash;

/**
 * Create a new event which should call @a process after @a delay
 * fake seconds. @A delete is called explicitly when the event
 * is deleted by SWIM explicitly, and when the event heap is
 * reset.
 */
static void
swim_event_create(struct swim_event *e, enum swim_event_type type, double delay,
		  swim_event_process_f process, swim_event_delete_f delete)
{
	e->deadline = swim_time() + delay;
	e->id = event_id++;
	e->process = process;
	e->delete = delete;
	e->type = type;
	event_heap_insert(&event_heap, e);
}

/** Destroy a basic event. */
static inline void
swim_event_destroy(struct swim_event *e)
{
	event_heap_delete(&event_heap, e);
}

/** Destroy a event and free its resources. */
static inline void
swim_event_delete(struct swim_event *e)
{
	e->delete(e);
}

/** Find an event by @a watcher. */
static struct swim_event *
swim_event_by_ev(struct ev_watcher *watcher)
{
	mh_int_t rc = mh_i64ptr_find(events_hash, (uint64_t) watcher, NULL);
	if (rc == mh_end(events_hash))
		return NULL;
	return (struct swim_event *) mh_i64ptr_node(events_hash, rc)->val;
}

/** Timer event generated by libev. */
struct swim_timer_event {
	struct swim_event base;
	/**
	 * Libev watcher. Used to store callback and to find the
	 * event by watcher pointer. It is necessary because SWIM
	 * operates by libev watchers.
	 */
	struct ev_watcher *watcher;
};

/** Destroy a timer event and free its resources. */
static void
swim_timer_event_delete(struct swim_event *e)
{
	assert(e->type == SWIM_EVENT_TIMER);
	struct swim_timer_event *te = (struct swim_timer_event *) e;
	mh_int_t rc = mh_i64ptr_find(events_hash, (uint64_t) te->watcher, NULL);
	assert(rc != mh_end(events_hash));
	mh_i64ptr_del(events_hash, rc, NULL);
	swim_event_destroy(e);
	free(te);
}

/** Process a timer event and delete it. */
static void
swim_timer_event_process(struct swim_event *e, struct ev_loop *loop)
{
	assert(e->type == SWIM_EVENT_TIMER);
	struct ev_watcher *w = ((struct swim_timer_event *) e)->watcher;
	swim_timer_event_delete(e);
	ev_invoke(loop, w, EV_TIMER);
}

/** Create a new timer event. */
static void
swim_timer_event_new(struct ev_watcher *watcher, double delay)
{
	struct swim_timer_event *e =
		(struct swim_timer_event *) malloc(sizeof(*e));
	assert(e != NULL);
	swim_event_create(&e->base, SWIM_EVENT_TIMER, delay,
			  swim_timer_event_process, swim_timer_event_delete);
	e->watcher = watcher;
	assert(swim_event_by_ev(watcher) == NULL);
	struct mh_i64ptr_node_t node = {(uint64_t) watcher, e};
	mh_int_t rc = mh_i64ptr_put(events_hash, &node, NULL, NULL);
	(void) rc;
	assert(rc != mh_end(events_hash));
}

/**
 * Breakpoint event for debug. It does nothing but stops the event
 * loop after a timeout to allow highlevel API to check some
 * cases. The main feature is that a test can choose that timeout,
 * while natural SWIM events usually are out of control. That
 * events allows to check conditions between natural events.
 */
struct swim_brk_event {
	struct swim_event base;
};

/** Delete a breakpoint event. */
static void
swim_brk_event_delete(struct swim_event *e)
{
	assert(e->type == SWIM_EVENT_BRK);
	swim_event_destroy(e);
	free(e);
}

/**
 * Breakpoint event processing is nothing but the event deletion.
 */
static void
swim_brk_event_process(struct swim_event *e, struct ev_loop *loop)
{
	(void) loop;
	assert(e->type == SWIM_EVENT_BRK);
	swim_brk_event_delete(e);
}

void
swim_ev_set_brk(double delay)
{
	struct swim_brk_event *e = (struct swim_brk_event *) malloc(sizeof(*e));
	assert(e != NULL);
	swim_event_create(&e->base, SWIM_EVENT_BRK, delay,
			  swim_brk_event_process, swim_brk_event_delete);
}

/** Implementation of global time visible in SWIM. */
double
swim_time(void)
{
	return watch;
}

/**
 * Start of a timer generates a delayed event. If a timer is
 * already started - nothing happens.
 */
void
swim_ev_timer_start(struct ev_loop *loop, struct ev_timer *base)
{
	(void) loop;
	if (swim_event_by_ev((struct ev_watcher *) base) != NULL)
		return;
	/* Create the periodic watcher and one event. */
	swim_timer_event_new((struct ev_watcher *) base, base->at);
}

/** Time stop cancels the event if the timer is active. */
void
swim_ev_timer_stop(struct ev_loop *loop, struct ev_timer *base)
{
	(void) loop;
	/*
	 * Delete the watcher and its events. Should be only one.
	 */
	struct swim_event *e = swim_event_by_ev((struct ev_watcher *) base);
	if (e == NULL)
		return;
	swim_event_delete(e);
}

/** Process all the events with the next nearest deadline. */
void
swim_test_ev_do_loop_step(struct ev_loop *loop)
{
	struct swim_event *next_e, *e = event_heap_top(&event_heap);
	if (e != NULL) {
		assert(e->deadline >= watch);
		/* Multiple events can have the same deadline. */
		watch = e->deadline;
		say_verbose("Loop watch %f", watch);
		do {
			e->process(e, loop);
			next_e = event_heap_top(&event_heap);
			assert(e != next_e);
			e = next_e;
		} while (e != NULL && e->deadline == watch);
	}
}

void
swim_test_ev_reset(void)
{
	struct swim_event *e;
	while ((e = event_heap_top(&event_heap)) != NULL)
		swim_event_delete(e);
	assert(mh_size(events_hash) == 0);
	event_id = 0;
	watch = 0;
}

void
swim_test_ev_init(void)
{
	events_hash = mh_i64ptr_new();
	assert(events_hash != NULL);
	event_heap_create(&event_heap);
}

void
swim_test_ev_free(void)
{
	swim_test_ev_reset();
	event_heap_destroy(&event_heap);
	mh_i64ptr_delete(events_hash);
}
