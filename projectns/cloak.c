/*
 * Copyright (c) 2018 Janik Kleinhoff
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Services awareness of group registrations
 * Commands to manage cloak namespaces
 */

#include "atheme.h"
#include "projectns.h"

static void cmd_cloak(sourceinfo_t *si, int parc, char *parv[]);

command_t ps_cloak = { "CLOAK", N_("Manages project cloak namespaces."), PRIV_PROJECT_ADMIN, 4, cmd_cloak, { .path = "freenode/project_cloak" } };

static void cmd_cloak(sourceinfo_t *si, int parc, char *parv[])
{
	char *project   = parv[0];
	char *mode      = parv[1];
	char *namespace = parv[2];
	char *trailing  = parv[3];

	enum {
		CLOAKNS_BAD = 0,
		CLOAKNS_ADD,
		CLOAKNS_DEL,
	} add_or_del = CLOAKNS_BAD;

	if (mode)
	{
		if (strcasecmp(mode, "ADD") == 0)
			add_or_del = CLOAKNS_ADD;
		else if (strcasecmp(mode, "DEL") == 0)
			add_or_del = CLOAKNS_DEL;
	}

	if (!namespace || !add_or_del || trailing)
	{
		cmd_faultcode_t fault = (namespace ? fault_badparams : fault_needmoreparams);

		if (fault == fault_badparams)
			command_fail(si, fault, STR_INVALID_PARAMS, "CLOAK");
		else
			command_fail(si, fault, STR_INSUFFICIENT_PARAMS, "CLOAK");
		command_fail(si, fault, _("Syntax: CLOAK <project> ADD|DEL <namespace>"));
		return;
	}

	// Only check for new namespaces, in case we have bad entries from a previous configuration
	// as we wouldn't be able to delete them otherwise
	if (add_or_del == CLOAKNS_ADD)
	{
		if (strlen(namespace) >= HOSTLEN)
		{
			command_fail(si, fault_badparams, _("The provided cloak namespace is too long."));
			return;
		}

		const char *last_slash = strrchr(namespace, '/');
		if ((last_slash != NULL && !*(last_slash + 1)) || strchr(namespace, '*'))
		{
			command_fail(si, fault_badparams, _("Please specify only the base part of the cloak namespace."));
			return;
		}

		for (char *c = namespace; *c; c++)
		{
			if (!isprint(*c))
			{
				command_fail(si, fault_badparams, _("The provided cloak namespace contains invalid characters."));
				return;
			}
		}
	}

	struct projectns *p = projectsvs->project_find(project);

	if (!p)
	{
		command_fail(si, fault_nosuch_target, _("The project \2%s\2 does not exist."), project);
		return;
	}

	if (add_or_del == CLOAKNS_DEL)
	{
		mowgli_node_t *n, *tn;
		MOWGLI_ITER_FOREACH_SAFE(n, tn, p->cloak_ns.head)
		{
			const char *ns = n->data;
			if (strcasecmp(ns, namespace) == 0)
			{
				free(n->data);

				mowgli_node_delete(n, &p->cloak_ns);
				mowgli_node_free(n);

				break;
			}
		}

		logcommand(si, CMDLOG_ADMIN, "PROJECT:CLOAK:DEL: \2%s\2 from \2%s\2", namespace, p->name);
		command_success_nodata(si, _("The namespace \2%s\2 was unregistered from project \2%s\2."), namespace, p->name);
	}
	else // CLOAKNS_ADD
	{
		mowgli_node_t *n;
		MOWGLI_ITER_FOREACH(n, p->cloak_ns.head)
		{
			const char *ns = n->data;
			if (strcasecmp(ns, namespace) == 0)
			{
				command_fail(si, fault_nochange, _("The \2%s\2 namespace is already registered to project \2%s\2."), namespace, p->name);

				return;
			}
		}

		mowgli_node_add(sstrdup(namespace), mowgli_node_create(), &p->cloak_ns);

		logcommand(si, CMDLOG_ADMIN, "PROJECT:CLOAK:ADD: \2%s\2 to \2%s\2", namespace, p->name);
		command_success_nodata(si, _("The namespace \2%s\2 was registered to project \2%s\2."), namespace, p->name);
	}
}

static void mod_init(module_t *const restrict m)
{
	if (!use_projectns_main_symbols(m))
		return;
	service_named_bind_command("projectserv", &ps_cloak);
}

static void mod_deinit(const module_unload_intent_t unused)
{
	service_named_unbind_command("projectserv", &ps_cloak);
}

DECLARE_MODULE_V1
(
	"freenode/projectns/cloak", MODULE_UNLOAD_CAPABILITY_OK, mod_init, mod_deinit,
	"", "freenode <http://www.freenode.net>"
);
