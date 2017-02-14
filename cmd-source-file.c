/* $OpenBSD$ */

/*
 * Copyright (c) 2008 Tiago Cunha <me@tiagocunha.org>
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

#include <sys/types.h>

#include <errno.h>
#include <glob.h>
#include <stdlib.h>
#include <string.h>

#include "tmux.h"

/*
 * Sources a configuration file.
 */

static enum cmd_retval	cmd_source_file_exec(struct cmd *, struct cmdq_item *);

static enum cmd_retval	cmd_source_file_done(struct cmdq_item *, void *);

const struct cmd_entry cmd_source_file_entry = {
	.name = "source-file",
	.alias = "source",

	.args = { "q", 1, 1 },
	.usage = "[-q] path",

	.flags = 0,
	.exec = cmd_source_file_exec
};

static enum cmd_retval
cmd_source_file_exec(struct cmd *self, struct cmdq_item *item)
{
	struct args		*args = self->args;
	struct client		*c = item->client;
	struct session  	*s;
	int			 quiet;
	struct cmdq_item	*new_item;
	enum cmd_retval		 retval;
	char			*glob_pattern, *tmp;
	const char		*cwd;
	glob_t			 g;
	u_int			 i;

	if (*args->argv[0] == '/')
		glob_pattern = xstrdup(args->argv[0]);
	else {
		if (c != NULL && c->session == NULL && c->cwd != NULL)
			cwd = c->cwd;
		else if (c != NULL && (s = c->session) != NULL && s->cwd != NULL)
			cwd = s->cwd;
		else
			cwd = ".";

		utf8_stravis(&tmp, cwd, VIS_GLOB);
		xasprintf(&glob_pattern, "%s/%s", tmp, args->argv[0]);
		free(tmp);
	}

	log_debug("source-file glob %s", glob_pattern);

	quiet = args_has(args, 'q');
	retval = CMD_RETURN_NORMAL;
	if (glob(glob_pattern, 0, NULL, &g) != 0) {
		if (!quiet || errno != ENOENT) {
			cmdq_error(item, "%s: %s", args->argv[0], strerror(errno));
			retval = CMD_RETURN_ERROR;
		}
		free(glob_pattern);
		return (retval);
	}
	free(glob_pattern);

	for (i = 0; i < (u_int)g.gl_pathc; i++) {
		if (load_cfg(g.gl_pathv[i], c, item, quiet) != 0)
			retval = CMD_RETURN_ERROR;
	}
	if (cfg_finished) {
		new_item = cmdq_get_callback(cmd_source_file_done, NULL);
		cmdq_insert_after(item, new_item);
	}

	globfree(&g);
	return (retval);
}

static enum cmd_retval
cmd_source_file_done(struct cmdq_item *item, __unused void *data)
{
	cfg_print_causes(item);
	return (CMD_RETURN_NORMAL);
}
