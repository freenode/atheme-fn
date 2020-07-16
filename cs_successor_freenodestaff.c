/*
 * Copyright (c) 2012 Marien Zwart <marien.zwart@gmail.com>.
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Forces the successor for single-# channels to be freenode-staff,
 * if an account by that name exists.
 */

#include "fn-compat.h"
#include "atheme.h"

static void channel_pick_successor_hook(hook_channel_succession_req_t *req)
{
	return_if_fail(req != NULL);
	return_if_fail(req->mc != NULL);

	/* Leave double-# channels alone. */
	if (req->mc->name[0] == '#' && req->mc->name[1] == '#')
		return;

	/* Use freenode-staff if it exists.
	 * If myuser_find_ext returns NULL the normal successor logic is used.
	 * If some other user of this hook picked a successor
	 * we intentionally overrule it.
	 */
	req->mu = myuser_find_ext("freenode-staff");
}

static void mod_init(module_t *m)
{
	hook_add_first_channel_pick_successor(channel_pick_successor_hook);
}

static void mod_deinit(module_unload_intent_t intent)
{
	hook_del_channel_pick_successor(channel_pick_successor_hook);
}

DECLARE_MODULE_V1
(
	"freenode/cs_successor_freenodestaff", MODULE_UNLOAD_CAPABILITY_OK, mod_init, mod_deinit,
	"$Id: cs_successor_freenodestaff.c 65 2012-06-09 12:25:31Z stephen $",
	"freenode <http://freenode.net>"
);
