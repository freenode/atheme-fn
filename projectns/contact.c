/*
 * Copyright (c) 2018 Janik Kleinhoff
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Services awareness of group registrations
 * Commands to manage registered contacts
 */

#include "atheme.h"
#include "projectns.h"

static void cmd_contact(sourceinfo_t *si, int parc, char *parv[]);

command_t ps_contact = { "CONTACT", N_("Manages project contacts."), PRIV_PROJECT_ADMIN, 3, cmd_contact, { .path = "freenode/project_contact" } };

static void cmd_contact(sourceinfo_t *si, int parc, char *parv[])
{
	char *project   = parv[0];
	char *mode      = parv[1];
	char *accname   = parv[2];

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

	if (!accname || !add_or_del)
	{
		cmd_faultcode_t fault = (accname ? fault_badparams : fault_needmoreparams);

		if (fault == fault_badparams)
			command_fail(si, fault, STR_INVALID_PARAMS, "CONTACT");
		else
			command_fail(si, fault, STR_INSUFFICIENT_PARAMS, "CONTACT");
		command_fail(si, fault, _("Syntax: PROJECT CONTACT <project> ADD|DEL <account>"));
		return;
	}

	/* Only allow myusers for now.
	 * Rationale: while we could accept arbitrary entities,
	 *  - exttargets are not suitable group contacts
	 *  - GroupServ groups:
	 *     - don't exist on freenode as of writing this
	 *     - maybe shouldn't be GCs (otoh, they might replace some uses for role accounts?)
	 *     - aren't really handled the way we handle myusers (with nickserv hooks and all)
	 * If you're looking to change this, make sure to check for allow_foundership
	 * since "can't be a channel founder" should catch the more obviously stupid things.
	 */
	myuser_t *mu = myuser_find(accname);

	if (!mu)
	{
		command_fail(si, fault_nosuch_target, _("\2%s\2 is not registered."), accname);
		return;
	}

	struct projectns *p = mowgli_patricia_retrieve(projectsvs->projects, project);

	if (!p)
	{
		command_fail(si, fault_nosuch_target, _("The project \2%s\2 does not exist."), project);
		return;
	}

	if (add_or_del == CONTACT_ADD)
	{
		mowgli_node_t *n, *tn;
		MOWGLI_ITER_FOREACH(n, p->contacts.head)
		{
			myentity_t *contact_mt = n->data;
			if (contact_mt == entity(mu))
			{
				command_fail(si, fault_nochange, _("\2%s\2 is already listed as contact for project \2%s\2."), contact_mt->name, p->name);
				return;
			}
		}

		mowgli_node_add(p, mowgli_node_create(), projectsvs->entity_get_projects(entity(mu)));
		mowgli_node_add(entity(mu), mowgli_node_create(), &p->contacts);

		logcommand(si, CMDLOG_ADMIN, "PROJECT:CONTACT:ADD: \2%s\2 to \2%s\2", entity(mu)->name, p->name);
		command_success_nodata(si, _("\2%s\2 was set as a contact for project \2%s\2."), entity(mu)->name, p->name);
	}
	else // CONTACT_DEL
	{
		mowgli_list_t *l = projectsvs->entity_get_projects(entity(mu));
		mowgli_node_t *n, *tn;

		bool was_listed = false;

		MOWGLI_ITER_FOREACH_SAFE(n, tn, l->head)
		{
			if (p == (struct projectns*)n->data)
			{
				mowgli_node_delete(n, l);
				mowgli_node_free(n);
				break;
			}
		}

		MOWGLI_ITER_FOREACH_SAFE(n, tn, p->contacts.head)
		{
			if (entity(mu) == (myentity_t*)n->data)
			{
				mowgli_node_delete(n, &p->contacts);
				mowgli_node_free(n);
				was_listed = true;
				break;
			}
		}

		if (was_listed)
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
