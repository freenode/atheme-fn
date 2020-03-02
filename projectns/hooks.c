/*
 * Copyright (c) 2018 Janik Kleinhoff
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Services awareness of group registrations
 * Hooks into existing functionality
 */

#include "fn-compat.h"
#include "atheme.h"
#include "projectns.h"

static bool register_require_namespace;
static char *register_require_namespace_exempt;
static char *register_project_advice;

static void userinfo_hook(hook_user_req_t *hdata)
{
	if (hdata->si->smu == hdata->mu ||
			has_priv(hdata->si, PRIV_PROJECT_AUSPEX))
	{
		mowgli_node_t *n;
		mowgli_list_t *plist = projectsvs->myuser_get_projects(hdata->mu);
		MOWGLI_ITER_FOREACH(n, plist->head)
		{
			struct projectns *project = n->data;

			mowgli_node_t *n2;
			char buf[BUFSIZE] = "";
			bool need_separator = false;
			MOWGLI_ITER_FOREACH(n2, project->channel_ns.head)
			{
				if (strlen(buf) > 80)
				{
					command_success_nodata(hdata->si, _("Group contact for %s (%s)"), project->name, buf);
					buf[0] = '\0';
				}
				need_separator = true;
				if (buf[0])
					mowgli_strlcat(buf, ", ", sizeof buf);
				mowgli_strlcat(buf, (const char*)n2->data, sizeof buf);
			}
			MOWGLI_ITER_FOREACH(n2, project->cloak_ns.head)
			{
				if (strlen(buf) > 80)
				{
					command_success_nodata(hdata->si, _("Group contact for %s (%s)"), project->name, buf);
					buf[0] = '\0';
					need_separator = false;
				}
				if (buf[0])
				{
					if (need_separator)
					{
						mowgli_strlcat(buf, "; ", sizeof buf);
						need_separator = false;
					}
					else
					{
						mowgli_strlcat(buf, ", ", sizeof buf);
					}
				}

				char ns[BUFSIZE];
				snprintf(ns, sizeof ns, "%s/*", (const char*)n2->data);
				mowgli_strlcat(buf, ns, sizeof buf);
			}

			if (buf[0])
				command_success_nodata(hdata->si, _("Group contact for %s (%s)"), project->name, buf);
			else
				command_success_nodata(hdata->si, _("Group contact for %s"), project->name);
		}
	}
}

static void chaninfo_hook(hook_channel_req_t *hdata)
{
	char *namespace = NULL;
	struct projectns *p = projectsvs->channame_get_project(hdata->mc->name, &namespace);

	if (p)
		command_success_nodata(hdata->si, "The \2%s\2 namespace is registered to the \2%s\2 project", namespace, p->name);

	free(namespace);
}

static void try_register_hook(hook_channel_register_check_t *hdata)
{
	char *namespace = NULL;
	struct projectns *project = projectsvs->channame_get_project(hdata->name, &namespace);

	if (register_require_namespace && !project && match(register_require_namespace_exempt, hdata->name))
	{
		hdata->approved = 1;
		command_fail(hdata->si, fault_noprivs, _("The given channel name is not registered to any project, so you cannot use it."));
		if (register_project_advice)
			command_fail(hdata->si, fault_noprivs, "%s", register_project_advice);
	}
	else if (project && !project->any_may_register)
	{
		mowgli_node_t *n;
		bool is_gc = false;
		MOWGLI_ITER_FOREACH(n, project->contacts.head)
		{
			if (hdata->si->smu == n->data)
			{
				is_gc = true;
				break;
			}
		}

		if (!is_gc)
		{
			hdata->approved = 1;
			command_fail(hdata->si, fault_noprivs, _("The \2%s\2 namespace is registered to the \2%s\2 project, so only authorized contacts may register new channels."), namespace, project->name);
			if (project->reginfo)
				command_fail(hdata->si, fault_noprivs, _("See %s for more information."), project->reginfo);
		}
	}

	free(namespace);
}

static void did_register_hook(hook_channel_req_t *hdata)
{
	char *namespace = NULL;
	struct projectns *project = projectsvs->channame_get_project(hdata->mc->name, &namespace);

	if (project)
	{
		command_success_nodata(hdata->si, _("The \2%s\2 namespace is managed by the \2%s\2 project."), namespace, project->name);
		if (project->reginfo)
			command_success_nodata(hdata->si, _("See %s for more information."), project->reginfo);
	}

	free(namespace);
}

static void mod_init(module_t *const restrict m)
{
	if (!use_projectns_main_symbols(m))
		return;

	hook_add_user_info(userinfo_hook);
	hook_add_channel_info(chaninfo_hook);
	hook_add_channel_can_register(try_register_hook);
	hook_add_channel_register(did_register_hook);

	add_bool_conf_item("REGISTER_REQUIRE_NAMESPACE", &projectsvs->me->conf_table, 0, &register_require_namespace, false);
	add_dupstr_conf_item("REGISTER_REQUIRE_NAMESPACE_EXEMPT", &projectsvs->me->conf_table, 0, &register_require_namespace_exempt, NULL);
	add_dupstr_conf_item("REGISTER_PROJECT_ADVICE", &projectsvs->me->conf_table, 0, &register_project_advice, NULL);
}

static void mod_deinit(const module_unload_intent_t unused)
{
	hook_del_user_info(userinfo_hook);
	hook_del_channel_info(chaninfo_hook);
	hook_del_channel_can_register(try_register_hook);
	hook_del_channel_register(did_register_hook);

	del_conf_item("REGISTER_REQUIRE_NAMESPACE", &projectsvs->me->conf_table);
	del_conf_item("REGISTER_REQUIRE_NAMESPACE_EXEMPT", &projectsvs->me->conf_table);
	del_conf_item("REGISTER_PROJECT_ADVICE", &projectsvs->me->conf_table);
}

DECLARE_MODULE_V1
(
	"freenode/projectns/hooks", MODULE_UNLOAD_CAPABILITY_OK, mod_init, mod_deinit,
	"", "freenode <http://www.freenode.net>"
);
