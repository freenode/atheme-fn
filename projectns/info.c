/*
 * Copyright (c) 2018 Janik Kleinhoff
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Services awareness of group registrations
 * Command to display information about registered projects
 */

#include "fn-compat.h"
#include "atheme.h"
#include "projectns.h"

static void cmd_info(sourceinfo_t *si, int parc, char *parv[]);

command_t ps_info = { "INFO", N_("Displays information about a project registration."), PRIV_PROJECT_AUSPEX, 1, cmd_info, { .path = "freenode/project_info" } };

static void cmd_info(sourceinfo_t *si, int parc, char *parv[])
{
	char *name = parv[0];

	if (!name)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "INFO");
		command_fail(si, fault_needmoreparams, _("Syntax: INFO <project>"));
		return;
	}

	struct projectns *p = projectsvs->project_find(name);
	if(!p)
	{
		command_fail(si, fault_nosuch_target, _("The project \2%s\2 does not exist."), name);
		return;
	}

	logcommand(si, CMDLOG_GET, "PROJECT:INFO: \2%s\2", p->name);
	command_success_nodata(si, _("Information on \2%s\2:"), p->name);

	char buf[BUFSIZE] = "";

	if (p->creation_time)
	{
		struct tm *tm = localtime(&p->creation_time);
		strftime(buf, sizeof buf, TIME_FORMAT, tm);
	}

	if (p->creation_time && p->creator)
		command_success_nodata(si, _("Registered on %s by %s"), buf, p->creator);
	else if (p->creation_time)
		command_success_nodata(si, _("Registered on %s"), buf);
	else if (p->creator)
		command_success_nodata(si, _("Registered by %s"), p->creator);

	buf[0] = '\0';

	mowgli_node_t *n;
	MOWGLI_ITER_FOREACH(n, p->channel_ns.head)
	{
		if (strlen(buf) > 80)
		{
			command_success_nodata(si, _("Channel namespaces: %s"), buf);
			buf[0] = '\0';
		}
		if (buf[0])
			mowgli_strlcat(buf, ", ", sizeof buf);
		mowgli_strlcat(buf, (const char*)n->data, sizeof buf);
	}

	command_success_nodata(si, _("Channel namespaces: %s"), (buf[0] ? buf : "(none)"));

	buf[0] = '\0';

	MOWGLI_ITER_FOREACH(n, p->cloak_ns.head)
	{
		if (strlen(buf) > 80)
		{
			command_success_nodata(si, _("Cloak namespaces: %s"), buf);
			buf[0] = '\0';
		}
		if (buf[0])
			mowgli_strlcat(buf, ", ", sizeof buf);
		mowgli_strlcat(buf, (const char*)n->data, sizeof buf);
	}

	command_success_nodata(si, _("Cloak namespaces: %s"), (buf[0] ? buf : "(none)"));

	buf[0] = '\0';

	MOWGLI_ITER_FOREACH(n, p->contacts.head)
	{
		if (strlen(buf) > 80)
		{
			command_success_nodata(si, _("Group contacts: %s"), buf);
			buf[0] = '\0';
		}
		if (buf[0])
			mowgli_strlcat(buf, ", ", sizeof buf);
		mowgli_strlcat(buf, ((myentity_t*)n->data)->name, sizeof buf);
	}

	command_success_nodata(si, _("Group contacts: %s"), (buf[0] ? buf : "(none)"));

	if (p->reginfo)
		command_success_nodata(si, _("\"See also\" displayed when registering channels: %s"), p->reginfo);

	if (p->any_may_register)
		command_success_nodata(si, _("Anyone may register channels in the project namespace"));
	else
		command_success_nodata(si, _("Only group contacts may register channels in the project namespace"));

	projectsvs->show_marks(si, p);

	command_success_nodata(si, _("*** \2End of Info\2 ***"));
}

static void mod_init(module_t *const restrict m)
{
	if (!use_projectns_main_symbols(m))
		return;
	service_named_bind_command("projectserv", &ps_info);
}

static void mod_deinit(const module_unload_intent_t unused)
{
	service_named_unbind_command("projectserv", &ps_info);
}

DECLARE_MODULE_V1
(
	"freenode/projectns/info", MODULE_UNLOAD_CAPABILITY_OK, mod_init, mod_deinit,
	"", "freenode <http://www.freenode.net>"
);
