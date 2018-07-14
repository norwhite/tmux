/* $OpenBSD$ */

/*
 * Copyright (c) 2007 Nicholas Marriott <nicholas.marriott@gmail.com>
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

#include "tmux.h"

/*
 * Bind a key to a command.
 */

static enum cmd_retval	cmd_set_next_table(struct cmd *, struct cmdq_item *);

const struct cmd_entry cmd_set_next_table_entry = {
	.name = "set-next-table",

	.args = { "T:", 0, 1 },
	.usage = "[-T key-table]",

	.flags = CMD_AFTERHOOK,
	.exec = cmd_set_next_table
};

static enum cmd_retval
cmd_set_next_table(struct cmd *self, __unused struct cmdq_item *item)
{
	struct args	*args = self->args;
	const char	*tablename;
	const char	*next_table;

	if (args_has(args, 'T'))
		tablename = args_get(args, 'T');
	else if (args_has(args, 'n'))
		tablename = "root";
	else
		tablename = "prefix";

	if (args->argc == 0) {
		next_table = "";
	} else {
		next_table = args->argv[0];
	}

	key_bindings_set_next_table(tablename, next_table);
	return (CMD_RETURN_NORMAL);
}
