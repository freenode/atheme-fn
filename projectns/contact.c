/*
 * Copyright (c) 2018 Janik Kleinhoff
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Services awareness of group registrations
 * Commands to manage registered contacts
 */

#include "fn-compat.h"
#include "atheme.h"
#include "projectns.h"

static void cmd_contact(sourceinfo_t *si, int parc, char *parv[]);

command_t ps_contact = { "CONTACT", N_("Manages project contacts."), PRIV_PROJECT_ADMIN, 3, cmd_contact, { .path = "freenode/project_contact" } };

static void cmd_contact(sourceinfo_t *si, int parc, char *parv[])
{
	char *project   = parv[0];
	char *mode      = parv[1];
	char *target    = parv[2];

	enum {
		CONTACT_BAD = 0,
		CONTACT_ADD,
		CONTACT_DEL,
	} add_or_del = CONTACT_BAD;

	if (mode)
	{
		if (strcasecmp(mode, "ADD") == 0)
			add_or_del = CONTACT_ADD;
		else if (strcasecmp(mode, "DEL") == 0)
			add_or_del = CONTACT_DEL;
	}

	if (!target || !add_or_del)
	{
		cmd_faultcode_t fault = (target ? fault_badparams : fault_needmoreparams);

		if (fault == fault_badparams)
			command_fail(si, fault, STR_INVALID_PARAMS, "CONTACT");
		else
			command_fail(si, fault, STR_INSUFFICIENT_PARAMS, "CONTACT");
		command_fail(si, fault, _("Syntax: CONTACT <project> ADD|DEL <account>"));
		return;
	}

	myuser_t *mu = myuser_find_ext(target);

	if (!mu)
	{
		command_fail(si, fault_nosuch_target, _("\2%s\2 is not registered."), target);
		return;
	}

	struct projectns *p = projectsvs->project_find(project);

	if (!p)
	{
		command_fail(si, fault_nosuch_target, _("The project \2%s\2 does not exist."), project);
		return;
	}

	if (add_or_del == CONTACT_ADD)
	{
		if (projectsvs->contact_new(p, mu))
		{
			logcommand(si, CMDLOG_ADMIN, "PROJECT:CONTACT:ADD: \2%s\2 to \2%s\2", entity(mu)->name, p->name);
			command_success_nodata(si, _("\2%s\2 was set as a contact for project \2%s\2."), entity(mu)->name, p->name);
		}
		else
		{
			command_fail(si, fault_nochange, _("\2%s\2 is already listed as contact for project \2%s\2."), entity(mu)->name, p->name);
		}
	}
	else // CONTACT_DEL
	{
		if (projectsvs->contact_destroy(p, mu))
		{
			logcommand(si, CMDLOG_ADMIN, "PROJECT:CONTACT:DEL: \2%s\2 from \2%s\2", entity(mu)->name, p->name);
			command_success_nodata(si, _("\2%s\2 was removed as a contact for project \2%s\2."), entity(mu)->name, p->name);
			if (!p->contacts.count)
				command_success_nodata(si, _("The project \2%s\2 now has no contacts."), p->name);
		}
		else
		{
			command_fail(si, fault_nochange, _("\2%s\2 was not listed as a contact for project \2%s\2."), entity(mu)->name, p->name);
		}
	}
}

static void mod_init(module_t *const restrict m)
{
	if (!use_projectns_main_symbols(m))
		return;
	service_named_bind_command("projectserv", &ps_contact);
}

static void mod_deinit(const module_unload_intent_t unused)
{
	service_named_unbind_command("projectserv", &ps_contact);
}

DECLARE_MODULE_V1
(
	"freenode/projectns/contact", MODULE_UNLOAD_CAPABILITY_OK, mod_init, mod_deinit,
	"", "freenode <http://www.freenode.net>"
);
