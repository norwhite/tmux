/* $OpenBSD$ */

/*
 * Copyright (c) 2021 Will <author@will.party>
 * Copyright (c) 2022 Jeff Chiang <pobomp@gmail.com>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF MIND, USE, DATA OR PROFITS, WHETHER
 * IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING
 * OUT OF OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <compat/queue.h>
#include <sys/types.h>

#include <stdlib.h>
#include <string.h>

#include "tmux.h"

/*
 * OSC 8 hyperlinks, described at:
 *
 *     https://gist.github.com/egmontkob/eb114294efbcd5adb1944c9f3cb5feda
 *
 * Each hyperlink and ID combination is assigned a number ("inner" in this
 * file) which is stored in an extended grid cell and maps into a tree here.
 *
 * Each URI has one inner number and one external ID (which tmux uses to send
 * the hyperlink to the terminal) and one internal ID (which is received from
 * the sending application inside tmux).
 *
 * Anonymous hyperlinks are each unique and are not reused even if they have
 * the same URI (terminals will not want to tie them together).
 */

static u_int MAX_HYPERLINKS = 5000;
static uint64_t hyperlink_next_external_id = 1;
static u_int global_hyperlink_count = 0;

struct hyperlink_uri {
	u_int			 inner;
	const char		*internal_id;
	const char		*external_id;
	const char		*uri;
	struct hyperlinks		*tree;

	TAILQ_ENTRY(hyperlink_uri) list_entry;
	RB_ENTRY(hyperlink_uri)	 by_inner_entry;
	RB_ENTRY(hyperlink_uri)	 by_uri_entry; /* by internal ID and URI */
};
RB_HEAD(hyperlink_by_uri_tree, hyperlink_uri);
RB_HEAD(hyperlink_by_inner_tree, hyperlink_uri);
TAILQ_HEAD(hyperlinks_dq, hyperlink_uri);
static struct hyperlinks_dq global_hyperlinks = \
TAILQ_HEAD_INITIALIZER(global_hyperlinks);

struct hyperlinks {
	u_int				next_inner;
	struct hyperlink_by_inner_tree	by_inner;
	struct hyperlink_by_uri_tree	by_uri;
};


static int
hyperlink_by_uri_cmp(struct hyperlink_uri *left, struct hyperlink_uri *right)
{
	int	r;

	if (*left->internal_id == '\0' || *right->internal_id == '\0') {
		/*
		 * If both URIs are anonymous, use the inner for comparison so
		 * that they do not match even if the URI is the same - each
		 * anonymous URI should be unique.
		 */
		if (*left->internal_id != '\0')
			return (-1);
		if (*right->internal_id != '\0')
			return (1);
		return (left->inner - right->inner);
	}

	r = strcmp(left->internal_id, right->internal_id);
	if (r != 0)
		return (r);
	return (strcmp(left->uri, right->uri));
}
RB_PROTOTYPE_STATIC(hyperlink_by_uri_tree, hyperlink_uri, by_uri_entry,
    hyperlink_by_uri_cmp);
RB_GENERATE_STATIC(hyperlink_by_uri_tree, hyperlink_uri, by_uri_entry,
    hyperlink_by_uri_cmp);

static int
hyperlink_by_inner_cmp(struct hyperlink_uri *left, struct hyperlink_uri *right)
{
	return (left->inner - right->inner);
}
RB_PROTOTYPE_STATIC(hyperlink_by_inner_tree, hyperlink_uri, by_inner_entry,
    hyperlink_by_inner_cmp);
RB_GENERATE_STATIC(hyperlink_by_inner_tree, hyperlink_uri, by_inner_entry,
    hyperlink_by_inner_cmp);

/* Store a new hyperlink or return if it already exists. */
u_int
hyperlink_put(struct hyperlinks *hl, const char *uri_in,
    const char *internal_id_in)
{
	struct hyperlink_uri	 find, *hlu;
	char			*uri, *internal_id, *external_id;

	/*
	 * Anonymous URI are stored with an empty internal ID and the tree
	 * comparator will make sure they never match each other (so each
	 * anonymous URI is unique).
	 */
	if (internal_id_in == NULL)
		internal_id_in = "";

	utf8_stravis(&uri, uri_in, VIS_OCTAL|VIS_CSTYLE);
	utf8_stravis(&internal_id, internal_id_in, VIS_OCTAL|VIS_CSTYLE);

	if (*internal_id_in != '\0') {
		find.uri = uri;
		find.internal_id = internal_id;

		hlu = RB_FIND(hyperlink_by_uri_tree, &hl->by_uri, &find);
		if (hlu != NULL) {
			free (uri);
			free (internal_id);
			return (hlu->inner);
		}
	}
	xasprintf(&external_id, "tmux%llX", hyperlink_next_external_id++);

	hlu = xcalloc(1, sizeof *hlu);
	hlu->inner = hl->next_inner++;
	hlu->internal_id = internal_id;
	hlu->external_id = external_id;
	hlu->uri = uri;
	hlu->tree = hl;
	RB_INSERT(hyperlink_by_uri_tree, &hl->by_uri, hlu);
	RB_INSERT(hyperlink_by_inner_tree, &hl->by_inner, hlu);
	TAILQ_INSERT_TAIL(&global_hyperlinks, hlu, list_entry);
	global_hyperlink_count++;
	log_debug("%s number %u", __func__, global_hyperlink_count);
	if ( global_hyperlink_count + 1 == MAX_HYPERLINKS) {
		hyperlink_remove(TAILQ_FIRST(&global_hyperlinks));
		global_hyperlink_count--;
	}
	return (hlu->inner);
}

/* Get hyperlink by inner number. */
int
hyperlink_get(struct hyperlinks *hl, u_int inner, const char **uri_out,
    const char **external_id_out)
{
	struct hyperlink_uri	find, *hlu;

	find.inner = inner;

	hlu = RB_FIND(hyperlink_by_inner_tree, &hl->by_inner, &find);
	if (hlu == NULL)
		return (0);
	*external_id_out = hlu->external_id;
	*uri_out = hlu->uri;
	return (1);
}

/* Initialize hyperlink set. */
struct hyperlinks *
hyperlink_init(void)
{
	struct hyperlinks	*hl;

	hl = xcalloc(1, sizeof *hl);
	hl->next_inner = 1;
	RB_INIT(&hl->by_uri);
	RB_INIT(&hl->by_inner);
	return (hl);
}

/* Free all hyperlinks but not the set itself. */
void
hyperlink_reset(struct hyperlinks *hl)
{
	struct hyperlink_uri	*hlu, *hlu1;

	RB_FOREACH_SAFE(hlu, hyperlink_by_inner_tree, &hl->by_inner, hlu1)
		hyperlink_remove(hlu);
}

/* Free hyperlink set. */
void
hyperlink_free(struct hyperlinks *hl)
{
	hyperlink_reset(hl);
	free(hl);
}

void
hyperlink_remove(struct hyperlink_uri *hlu)
{
	struct hyperlinks *owner = hlu->tree;

	TAILQ_REMOVE(&global_hyperlinks, hlu, list_entry);
	global_hyperlink_count--;

	free((void *)hlu->internal_id);
	free((void *)hlu->external_id);
	free((void *)hlu->uri);
	RB_REMOVE(hyperlink_by_inner_tree, &owner->by_inner, hlu);
	RB_REMOVE(hyperlink_by_uri_tree, &owner->by_uri, hlu);
	free(hlu);
}

/* Initialize global hyperlink queue. */
void
hyperlink_queue_init(void) {
}
