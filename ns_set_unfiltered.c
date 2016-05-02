/*
 * Copyright (c) 2005 William Pitcock <nenolod -at- nenolod.net>
 * Copyright (c) 2007 Jilles Tjoelker
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Allows users to receive private messages from unregistered users.
 *
 * $Id$
 */

#include "atheme.h"
#include "uplink.h"

DECLARE_MODULE_V1
(
	"freenode/ns_set_unfiltered", false, _modinit, _moddeinit,
	"$Id$",
	"freenode <http://www.freenode.net>"
);

list_t *ns_set_cmdtree, *ns_helptree;

static void set_unfiltered_on_identify(void *vptr);
static void ns_cmd_set_unfiltered(sourceinfo_t *si, int parc, char *parv[]);

command_t ns_set_unfiltered = { "UNFILTERED", N_("Allows messages from unregistered users."), AC_NONE, 1, ns_cmd_set_unfiltered };

void _modinit(module_t *m)
{
	MODULE_USE_SYMBOL(ns_set_cmdtree, "nickserv/set", "ns_set_cmdtree");
	MODULE_USE_SYMBOL(ns_helptree, "nickserv/main", "ns_helptree");

	hook_add_event("user_identify");
	hook_add_hook("user_identify", set_unfiltered_on_identify);
	command_add(&ns_set_unfiltered, ns_set_cmdtree);
	help_addentry(ns_helptree, "SET UNFILTERED", "help/nickserv/set_unfiltered", NULL);
}

void _moddeinit(void)
{
	hook_del_hook("user_identify", set_unfiltered_on_identify);
	command_delete(&ns_set_unfiltered, ns_set_cmdtree);
	help_delentry(ns_helptree, "SET UNFILTERED");
}

static void do_set_unfiltered(user_t *u, bool enable)
{
	sts(":%s MODE %s +*", nicksvs.nick, nicksvs.nick);
	sts(":%s MODE %s %c6", nicksvs.nick, u->nick, enable ? '+' : '-');
	sts(":%s MODE %s -*", nicksvs.nick, nicksvs.nick);
}

static void do_set_unfiltered_all(myuser_t *mu, bool enable)
{
	node_t *n;
	user_t *u;

	LIST_FOREACH(n, mu->logins.head)
	{
		u = n->data;
		do_set_unfiltered(u, enable);
	}
}

/* SET UNFILTERED [ON|OFF] */
static void ns_cmd_set_unfiltered(sourceinfo_t *si, int parc, char *parv[])
{
	metadata_t *md;
	char *setting = parv[0];

	if (!setting)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "UNFILTERED");
		command_fail(si, fault_needmoreparams, _("Syntax: SET UNFILTERED ON|OFF"));
		return;
	}

	if (!si->smu)
	{
		command_fail(si, fault_noprivs, _("You are not logged in."));
		return;
	}

	if (strcasecmp(setting, "ON") == 0)
	{
		if ((md = metadata_find(si->smu, "private:unfiltered")) != NULL)
		{
			command_fail(si, fault_nochange, _("UNFILTERED is already enabled."));
		}
		else
		{
			metadata_add(si->smu, "private:unfiltered", "1");
			command_success_nodata(si, _("UNFILTERED is now enabled."));
		}
		do_set_unfiltered_all(si->smu, true);
	}
	else if (strcasecmp(setting, "OFF") == 0)
	{
		if ((md = metadata_find(si->smu, "private:unfiltered")) != NULL)
		{
			metadata_delete(si->smu, "private:unfiltered");
			command_success_nodata(si, _("UNFILTERED is now disabled."));
		}
		else
		{
			command_fail(si, fault_nochange, _("UNFILTERED is already disabled."));
		}
		do_set_unfiltered_all(si->smu, false);
	}
	else
	{
		command_fail(si, fault_badparams, _("Unknown value for UNFILTERED. Expected values are ON or OFF."));
	}
	return;
}

static void set_unfiltered_on_identify(void *vptr)
{
	user_t *u = vptr;
	myuser_t *mu = u->myuser;
	metadata_t *md;

	if (!(md = metadata_find(mu, "private:unfiltered")))
		return;

	do_set_unfiltered(u, true);
}

/* vim:cinoptions=>s,e0,n0,f0,{0,}0,^0,=s,ps,t0,c3,+s,(2s,us,)20,*30,gs,hs
 * vim:ts=8
 * vim:sw=8
 * vim:noexpandtab
 */
