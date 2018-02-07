/*
 * Copyright (c) 2018 Janik Kleinhoff
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Services awareness of group registrations, initial prototype.
 */

#include "atheme.h"

#define DB_TYPE_PROJECT           "FNGROUP"
#define DB_TYPE_REGINFO           "FNGRI"
#define DB_TYPE_CONTACT           "FNGC"
#define DB_TYPE_CHANNEL_NAMESPACE "FNCNS"

#define PRIV_PROJECT_ADMIN  "project:admin"
#define PRIV_PROJECT_AUSPEX "project:auspex"

static mowgli_patricia_t *project_cmdtree;

static void os_cmd_project(sourceinfo_t *si, int parc, char *parv[]);
static void cmd_register(sourceinfo_t *si, int parc, char *parv[]);
static void cmd_drop(sourceinfo_t *si, int parc, char *parv[]);
static void cmd_info(sourceinfo_t *si, int parc, char *parv[]);
static void cmd_list(sourceinfo_t *si, int parc, char *parv[]);
static void cmd_channel(sourceinfo_t *si, int parc, char *parv[]);
static void cmd_contact(sourceinfo_t *si, int parc, char *parv[]);
static void cmd_set(sourceinfo_t *si, int parc, char *parv[]);
static void set_openreg(sourceinfo_t *si, int parc, char *parv[]);
static void set_reginfo(sourceinfo_t *si, int parc, char *parv[]);

command_t os_project = { "PROJECT", N_("Manages registered projects."), AC_NONE, 4, os_cmd_project, { .path = "freenode/os_project" } };

command_t os_project_register = { "REGISTER", N_("Adds a project registration."), PRIV_PROJECT_ADMIN, 1, cmd_register, { .path = "freenode/os_project_register" } };
command_t os_project_drop = { "DROP", N_("Deletes a project registration."), PRIV_PROJECT_ADMIN, 1, cmd_drop, { .path = "freenode/os_project_drop" } };
command_t os_project_info = { "INFO", N_("Displays information about a project registration."), PRIV_PROJECT_AUSPEX, 1, cmd_info, { .path = "freenode/os_project_info" } };
command_t os_project_list = { "LIST", N_("Lists project registrations."), PRIV_PROJECT_AUSPEX, 1, cmd_list, { .path = "freenode/os_project_list" } };
command_t os_project_channel = { "CHANNEL", N_("Manages project channel namespaces."), PRIV_PROJECT_ADMIN, 3, cmd_channel, { .path = "freenode/os_project_channel" } };
command_t os_project_contact = { "CONTACT", N_("Manages project contacts."), PRIV_PROJECT_ADMIN, 3, cmd_contact, { .path = "freenode/os_project_contact" } };

struct projectns {
	char *name;
	bool any_may_register;
	char *reginfo;
	mowgli_list_t contacts;
	mowgli_list_t channel_ns;
};

static mowgli_patricia_t *projects;
static mowgli_patricia_t *projects_by_channelns;

char *namespace_separators;
bool register_require_namespace;
char *register_require_namespace_exempt;
char *register_project_advice;

/* You need to free() the return value of this when done */
static char *parse_namespace(const char *chan)
{
	char *buf = sstrdup(chan);
	(void) strtok(buf, namespace_separators);

	return buf;
}

static bool is_valid_project_name(const char * const name)
{
	/* Screen for anything that'd break parameter parsing or the protocol.
	 * Don't check for other kinds of stupidity as this module is meant to
	 * be used by network staff, who should know better.
	 */
	return !(strchr(name, ' ') || strchr(name, '\n') || strchr(name, '\r'));
}

static mowgli_list_t *entity_get_projects(myentity_t *mt)
{
	mowgli_list_t *l;

	l = privatedata_get(mt, "freenode:projects");
	if (l)
		return l;

	l = mowgli_list_create();
	privatedata_set(mt, "freenode:projects", l);

	return l;
}

static struct projectns *entity_is_contact(const myentity_t * const mt, const char *channel)
{
	char *namespace = parse_namespace(channel);
	struct projectns *p = mowgli_patricia_retrieve(projects_by_channelns, namespace);

	free(namespace);

	if (!p)
		return NULL;

	mowgli_node_t *n;
	MOWGLI_ITER_FOREACH(n, p->contacts.head)
	{
		myentity_t *contact = n->data;

		if (contact == mt)
			return p;
	}

	return NULL;
}

static void contact_delete(struct projectns *p, myentity_t *mt)
{
	mowgli_list_t *l = entity_get_projects(mt);
	mowgli_node_t *n, *tn;

	MOWGLI_ITER_FOREACH_SAFE(n, tn, l->head)
	{
		if (p == (struct projectns*)n->data)
		{
			mowgli_node_delete(n, l);
			mowgli_node_free(n);
		}
	}

	MOWGLI_ITER_FOREACH_SAFE(n, tn, p->contacts.head)
	{
		if (mt == (myentity_t*)n->data)
		{
			mowgli_node_delete(n, &p->contacts);
			mowgli_node_free(n);
		}
	}
}

static void channelns_delete(struct projectns *p, const char *ns)
{
	mowgli_list_t *l = &p->channel_ns;
	mowgli_node_t *n, *tn;

	MOWGLI_ITER_FOREACH_SAFE(n, tn, l->head)
	{
		if (strcasecmp(ns, (char*)n->data) == 0)
		{
			free(n->data);
			mowgli_node_delete(n, l);
			mowgli_node_free(n);
		}
	}

	mowgli_patricia_delete(projects_by_channelns, ns);
}

static void os_cmd_project(sourceinfo_t *si, int parc, char *parv[])
{
	char *subcmd = parv[0];

	if (!subcmd)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "PROJECT");
		command_fail(si, fault_needmoreparams, _("Syntax: PROJECT <subcommand>"));
		return;
	}

	command_t *c;
	if ((c = command_find(project_cmdtree, subcmd)))
	{
		command_exec(si->service, si, c, parc - 1, parv + 1);
	}
	else
	{
		command_fail(si, fault_badparams, _("Invalid subcommand. Use \2/%s%s HELP PROJECT\2 for a command listing."), (ircd->uses_rcommand == false) ? "msg " : "", si->service->nick);
	}
}

static void cmd_register(sourceinfo_t *si, int parc, char *parv[])
{
	char *name = parv[0];

	if (!name)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "REGISTER");
		command_fail(si, fault_needmoreparams, _("Syntax: PROJECT REGISTER <project>"));
		return;
	}

	if (mowgli_patricia_retrieve(projects, name))
	{
		command_fail(si, fault_alreadyexists, _("\2%s\2 is already registered."), name);
		return;
	}

	if (!is_valid_project_name(name))
	{
		command_fail(si, fault_badparams, _("\2%s\2 is not a valid project name."), name);
		return;
	}

	struct projectns *p = smalloc(sizeof(*p));
	p->name = sstrdup(name);
	p->any_may_register = false;

	mowgli_patricia_add(projects, p->name, p);

	logcommand(si, CMDLOG_ADMIN, "PROJECT:REGISTER: \2%s\2", name);
	command_success_nodata(si, _("The project \2%s\2 has been registered."), name);
}

static void cmd_drop(sourceinfo_t *si, int parc, char *parv[])
{
	char *name = parv[0];

	if (!name)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "DROP");
		command_fail(si, fault_needmoreparams, _("Syntax: PROJECT DROP <project>"));
		return;
	}

	struct projectns *p = mowgli_patricia_retrieve(projects, name);
	if(!p)
	{
		command_fail(si, fault_nosuch_target, _("The project \2%s\2 does not exist."), name);
		return;
	}

	mowgli_patricia_delete(projects, name);
	mowgli_node_t *n, *tn;
	MOWGLI_ITER_FOREACH_SAFE(n, tn, p->contacts.head)
	{
		mowgli_list_t *l = entity_get_projects((myentity_t*)n->data);

		mowgli_node_t *n2, *tn2;
		MOWGLI_ITER_FOREACH_SAFE(n2, tn2, l->head)
		{
			mowgli_node_delete(n2, l);
			mowgli_node_free(n2);
		}

		mowgli_node_delete(n, &p->contacts);
		mowgli_node_free(n);
	}
	MOWGLI_ITER_FOREACH_SAFE(n, tn, p->channel_ns.head)
	{
		const char *ns = n->data;
		mowgli_patricia_delete(projects_by_channelns, ns);

		free(n->data);

		mowgli_node_delete(n, &p->channel_ns);
		mowgli_node_free(n);
	}
	free(p->name);
	free(p);

	logcommand(si, CMDLOG_ADMIN, "PROJECT:DROP: \2%s\2", name);
	command_success_nodata(si, _("The registration for the project \2%s\2 has been dropped."), name);
}

static void cmd_info(sourceinfo_t *si, int parc, char *parv[])
{
	char *name = parv[0];

	if (!name)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "INFO");
		command_fail(si, fault_needmoreparams, _("Syntax: PROJECT INFO <project>"));
		return;
	}

	struct projectns *p = mowgli_patricia_retrieve(projects, name);
	if(!p)
	{
		command_fail(si, fault_nosuch_target, _("The project \2%s\2 does not exist."), name);
		return;
	}

	logcommand(si, CMDLOG_GET, "PROJECT:INFO: \2%s\2", p->name);
	command_success_nodata(si, _("Information on \2%s\2:"), p->name);

	char buf[BUFSIZE] = "";
	mowgli_node_t *n;
	MOWGLI_ITER_FOREACH(n, p->channel_ns.head)
	{
		if (buf[0])
			mowgli_strlcat(buf, ", ", sizeof buf);
		mowgli_strlcat(buf, (const char*)n->data, sizeof buf);
	}

	command_success_nodata(si, _("Channel namespaces: %s"), (buf[0] ? buf : "(none)"));

	buf[0] = '\0';

	MOWGLI_ITER_FOREACH(n, p->contacts.head)
	{
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

	command_success_nodata(si, _("*** \2End of Info\2 ***"));
}

static void cmd_list(sourceinfo_t *si, int parc, char *parv[])
{
	char *pattern = parv[0];

	if (!pattern)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "LIST");
		command_fail(si, fault_needmoreparams, _("Syntax: PROJECT LIST <pattern>"));
		return;
	}

	mowgli_patricia_iteration_state_t state;
	struct projectns *project;
	unsigned int matches = 0;

	command_success_nodata(si, _("Registered projects matching pattern \2%s\2:"), pattern);

	MOWGLI_PATRICIA_FOREACH(project, &state, projects)
	{
		if (!match(pattern, project->name))
		{
			matches++;
			char channels[BUFSIZE] = "";
			mowgli_node_t *n;
			MOWGLI_ITER_FOREACH(n, project->channel_ns.head)
			{
				if (channels[0])
					mowgli_strlcat(channels, ", ", sizeof channels);
				mowgli_strlcat(channels, (const char*)n->data, sizeof channels);
			}

			char contacts[BUFSIZE] = "";
			MOWGLI_ITER_FOREACH(n, project->contacts.head)
			{
				if (contacts[0])
					mowgli_strlcat(contacts, ", ", sizeof contacts);
				mowgli_strlcat(contacts, ((myentity_t*)n->data)->name, sizeof contacts);
			}
			command_success_nodata(si, _("- %s (%s; %s)"), project->name,
			                           (channels[0] ? channels : _("\2no channels\2")),
			                           (contacts[0] ? contacts : _("\2no contacts\2")));
		}
	}

	if (matches == 0)
		command_success_nodata(si, _("No projects matched pattern \2%s\2"), pattern);
	else
		command_success_nodata(si, ngettext(N_("\2%d\2 match for pattern \2%s\2"), N_("\2%d\2 matches for pattern \2%s\2"), matches), matches, pattern);
	logcommand(si, CMDLOG_ADMIN, "PROJECT:LIST: \2%s\2 (\2%d\2 matches)", pattern, matches);
}

static void cmd_channel(sourceinfo_t *si, int parc, char *parv[])
{
	char *project   = parv[0];
	char *mode      = parv[1];
	char *namespace = parv[2];

	enum {
		CHANNS_BAD = 0,
		CHANNS_ADD,
		CHANNS_DEL,
	} add_or_del = CHANNS_BAD;

	if (mode)
	{
		if (strcasecmp(mode, "ADD") == 0)
			add_or_del = CHANNS_ADD;
		else if (strcasecmp(mode, "DEL") == 0)
			add_or_del = CHANNS_DEL;
	}

	if (!namespace || !add_or_del)
	{
		cmd_faultcode_t fault = (namespace ? fault_badparams : fault_needmoreparams);

		if (fault == fault_badparams)
			command_fail(si, fault, STR_INVALID_PARAMS, "CHANNEL");
		else
			command_fail(si, fault, STR_INSUFFICIENT_PARAMS, "CHANNEL");
		command_fail(si, fault, _("Syntax: PROJECT CHANNEL <project> ADD|DEL <#namespace>"));
		return;
	}

	if (namespace[0] != '#')
	{
		command_fail(si, fault_badparams, _("\2%s\2 is not a valid channel name."), namespace);
		return;
	}

	char *canon_ns = parse_namespace(namespace);

	if (irccasecmp(canon_ns, namespace) != 0)
	{
		command_fail(si, fault_badparams, _("\2%s\2 is not a namespace root; use \2%s\2 instead."), namespace, canon_ns);
		free(canon_ns);
		return;
	}
	else
	{
		free(canon_ns);
	}

	struct projectns *p = mowgli_patricia_retrieve(projects, project);

	if (!p)
	{
		command_fail(si, fault_nosuch_target, _("The project \2%s\2 does not exist."), project);
		return;
	}

	struct projectns *chan_p = mowgli_patricia_retrieve(projects_by_channelns, namespace);
	if (chan_p && add_or_del == CHANNS_ADD)
	{
		command_fail(si, fault_alreadyexists, _("The \2%s\2 namespace already belongs to project \2%s\2."), namespace, chan_p->name);
		return;
	}
	if (!chan_p && add_or_del == CHANNS_DEL)
	{
		command_fail(si, fault_nochange, _("The \2%s\2 namespace is not registered to any project."), namespace);
		return;
	}

	if (add_or_del == CHANNS_DEL)
	{
		if (chan_p != p)
		{
			command_fail(si, fault_nosuch_key, _("The \2%s\2 namespace is registered to project \2%s\2, but you tried to remove it from project \2%s\2."), namespace, p->name, chan_p->name);
			return;
		}

		mowgli_patricia_delete(projects_by_channelns, namespace);

		mowgli_node_t *n, *tn;
		MOWGLI_ITER_FOREACH_SAFE(n, tn, chan_p->channel_ns.head)
		{
			const char *ns = n->data;
			if (strcasecmp(ns, namespace) == 0)
			{
				free(n->data);

				mowgli_node_delete(n, &chan_p->channel_ns);
				mowgli_node_free(n);

				break;
			}
		}

		logcommand(si, CMDLOG_ADMIN, "PROJECT:CHANNEL:DEL: \2%s\2 from \2%s\2", namespace, chan_p->name);
		command_success_nodata(si, _("The namespace \2%s\2 was unregistered from project \2%s\2."), namespace, chan_p->name);
	}
	else // CHANNS_ADD
	{
		/* We've checked above that this namespace isn't already registered */
		mowgli_patricia_add(projects_by_channelns, namespace, p);
		mowgli_node_add(sstrdup(namespace), mowgli_node_create(), &p->channel_ns);

		logcommand(si, CMDLOG_ADMIN, "PROJECT:CHANNEL:ADD: \2%s\2 to \2%s\2", namespace, p->name);
		command_success_nodata(si, _("The namespace \2%s\2 was registered to project \2%s\2."), namespace, p->name);
	}
}

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

	struct projectns *p = mowgli_patricia_retrieve(projects, project);

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

		mowgli_node_add(p, mowgli_node_create(), entity_get_projects(entity(mu)));
		mowgli_node_add(entity(mu), mowgli_node_create(), &p->contacts);

		logcommand(si, CMDLOG_ADMIN, "PROJECT:CONTACT:ADD: \2%s\2 to \2%s\2", entity(mu)->name, p->name);
		command_success_nodata(si, _("\2%s\2 was set as a contact for project \2%s\2."), entity(mu)->name, p->name);
	}
	else // CONTACT_DEL
	{
		mowgli_list_t *l = entity_get_projects(entity(mu));
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

static void cmd_set(sourceinfo_t *si, int parc, char *parv[])
{
	char *project_name = parv[0];
	char *setting      = parv[1];

	if (!setting)
	{
		command_fail(si, fault_needmoreparams, STR_INSUFFICIENT_PARAMS, "SET");
		command_fail(si, fault_needmoreparams, _("Syntax: PROJECT SET <project> <setting> [parameters]"));
		return;
	}

	command_t *c;
	if ((c = command_find(project_cmdtree, setting)))
	{
		parv[1] = project_name;
		command_exec(si->service, si, c, parc - 1, parv + 1);
	}
	else
	{
		command_fail(si, fault_badparams, _("Invalid setting. Use \2/%s%s HELP PROJECT SET\2 for a list of available settings."), (ircd->uses_rcommand == false) ? "msg " : "", si->service->nick);
	}
}

static void db_h_project(database_handle_t *db, const char *type)
{
	const char *name     = db_sread_word(db);
	unsigned int any_reg = db_sread_uint(db);

	struct projectns *l = smalloc(sizeof(*l));
	l->name = sstrdup(name);
	l->any_may_register = any_reg;

	mowgli_patricia_add(projects, l->name, l);
}

static void db_h_reginfo(database_handle_t *db, const char *type)
{
	const char *name = db_sread_word(db);
	const char *info = db_sread_str(db);

	struct projectns *project = mowgli_patricia_retrieve(projects, name);
	project->reginfo = sstrdup(info);
}

static void db_h_contact(database_handle_t *db, const char *type)
{
	const char *project_name = db_sread_word(db);
	const char *contact_name = db_sread_word(db);

	struct projectns *project = mowgli_patricia_retrieve(projects, project_name);
	myentity_t *contact = myentity_find(contact_name);

	mowgli_node_add(project, mowgli_node_create(), entity_get_projects(contact));
	mowgli_node_add(contact, mowgli_node_create(), &project->contacts);
}

static void db_h_channelns(database_handle_t *db, const char *type)
{
	const char *project_name = db_sread_word(db);
	const char *namespace    = db_sread_word(db);

	struct projectns *project = mowgli_patricia_retrieve(projects, project_name);

	mowgli_patricia_add(projects_by_channelns, namespace, project);
	mowgli_node_add(sstrdup(namespace), mowgli_node_create(), &project->channel_ns);
}

static void write_projects_db(database_handle_t *db)
{
	mowgli_patricia_iteration_state_t state;
	struct projectns *project;

	MOWGLI_PATRICIA_FOREACH(project, &state, projects)
	{
		db_start_row(db, DB_TYPE_PROJECT);
		db_write_word(db, project->name);
		db_write_uint(db, project->any_may_register);
		db_commit_row(db);

		if (project->reginfo)
		{
			db_start_row(db, DB_TYPE_REGINFO);
			db_write_word(db, project->name);
			db_write_str(db, project->reginfo);
			db_commit_row(db);
		}

		mowgli_node_t *n;
		MOWGLI_ITER_FOREACH(n, project->contacts.head)
		{
			db_start_row(db, DB_TYPE_CONTACT);
			db_write_word(db, project->name);
			db_write_word(db, ((myentity_t*)n->data)->name);
			db_commit_row(db);
		}

		MOWGLI_ITER_FOREACH(n, project->channel_ns.head)
		{
			db_start_row(db, DB_TYPE_CHANNEL_NAMESPACE);
			db_write_word(db, project->name);
			db_write_word(db, (char*)n->data);
			db_commit_row(db);
		}
	}
}

static void userinfo_hook(hook_user_req_t *hdata)
{
	if (hdata->si->smu == hdata->mu ||
			has_priv(hdata->si, PRIV_USER_AUSPEX))
	{
		mowgli_node_t *n;
		mowgli_list_t *plist = entity_get_projects(entity(hdata->mu));
		MOWGLI_ITER_FOREACH(n, plist->head)
		{
			struct projectns *project = n->data;

			mowgli_node_t *n2;
			char buf[BUFSIZE] = "";
			MOWGLI_ITER_FOREACH(n2, project->channel_ns.head)
			{
				if (buf[0])
					mowgli_strlcat(buf, ", ", sizeof buf);
				mowgli_strlcat(buf, (const char*)n2->data, sizeof buf);
			}

			command_success_nodata(hdata->si, "Group contact for %s (%s)", project->name, buf);
		}
	}
}

static void chaninfo_hook(hook_channel_req_t *hdata)
{
	char *namespace = parse_namespace(hdata->mc->name);
	struct projectns *p = mowgli_patricia_retrieve(projects_by_channelns, namespace);

	if (p)
		command_success_nodata(hdata->si, "The \2%s\2 namespace is registered to the \2%s\2 project", namespace, p->name);
	else
		command_success_nodata(hdata->si, "The \2%s\2 namespace is not registered to any project", namespace);

	free(namespace);
}

static void try_register_hook(hook_channel_register_check_t *hdata)
{
	char *namespace = parse_namespace(hdata->name);
	struct projectns *project = mowgli_patricia_retrieve(projects_by_channelns, namespace);

	if (register_require_namespace && !project && match(register_require_namespace_exempt, hdata->name))
	{
		hdata->approved = 1;
		command_fail(hdata->si, fault_noprivs, _("The \2%s\2 namespace is not registered to any project, so you cannot use it."), namespace);
		if (register_project_advice)
			command_fail(hdata->si, fault_noprivs, "%s", register_project_advice);
	}
	else if (project && !project->any_may_register)
	{
		mowgli_node_t *n;
		bool is_gc = false;
		MOWGLI_ITER_FOREACH(n, project->contacts.head)
		{
			if (entity(hdata->si->smu) == n->data)
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
	char *namespace = parse_namespace(hdata->mc->name);
	struct projectns *project = mowgli_patricia_retrieve(projects_by_channelns, namespace);

	if (project)
	{
		command_success_nodata(hdata->si, _("The \2%s\2 namespace is managed by the \2%s\2 project."), namespace, project->name);
		if (project->reginfo)
			command_success_nodata(hdata->si, _("See %s for more information."), project->reginfo);
	}

	free(namespace);
}

static void _modinit(module_t *m)
{
	MODULE_TRY_REQUEST_DEPENDENCY(m, "operserv/main");
	MODULE_TRY_REQUEST_DEPENDENCY(m, "chanserv/main");

	project_cmdtree = mowgli_patricia_create(strcasecanon);
	command_add(&os_project_register, project_cmdtree);
	command_add(&os_project_drop, project_cmdtree);
	command_add(&os_project_info, project_cmdtree);
	command_add(&os_project_list, project_cmdtree);
	command_add(&os_project_contact, project_cmdtree);
	command_add(&os_project_channel, project_cmdtree);

	service_named_bind_command("operserv", &os_project);

	projects = mowgli_patricia_create(strcasecanon);
	projects_by_channelns = mowgli_patricia_create(irccasecanon);

	db_register_type_handler(DB_TYPE_PROJECT, db_h_project);
	db_register_type_handler(DB_TYPE_REGINFO, db_h_reginfo);
	db_register_type_handler(DB_TYPE_CONTACT, db_h_contact);
	db_register_type_handler(DB_TYPE_CHANNEL_NAMESPACE, db_h_channelns);

	add_dupstr_conf_item("NAMESPACE_SEPARATORS", &chansvs.me->conf_table, 0, &namespace_separators, "-");
	add_bool_conf_item("REGISTER_REQUIRE_NAMESPACE", &chansvs.me->conf_table, 0, &register_require_namespace, false);
	add_dupstr_conf_item("REGISTER_REQUIRE_NAMESPACE_EXEMPT", &chansvs.me->conf_table, 0, &register_require_namespace_exempt, NULL);
	add_dupstr_conf_item("REGISTER_PROJECT_ADVICE", &chansvs.me->conf_table, 0, &register_project_advice, NULL);

	hook_add_event("user_info");
	hook_add_user_info(userinfo_hook);

	hook_add_event("channel_info");
	hook_add_channel_info(chaninfo_hook);

	hook_add_event("channel_can_register");
	hook_add_channel_can_register(try_register_hook);

	hook_add_event("channel_register");
	hook_add_channel_register(did_register_hook);

	hook_add_event("db_write");
	hook_add_db_write(write_projects_db);
}

static void free_project_cb(const char *key, void *data, void *privdata)
{
	free(data);
}

static void _moddeinit(module_unload_intent_t unused)
{
	service_named_unbind_command("operserv", &os_project);

	command_delete(&os_project_register, project_cmdtree);
	command_delete(&os_project_drop, project_cmdtree);
	command_delete(&os_project_info, project_cmdtree);
	command_delete(&os_project_list, project_cmdtree);
	command_delete(&os_project_contact, project_cmdtree);
	command_delete(&os_project_channel, project_cmdtree);

	mowgli_patricia_destroy(project_cmdtree, NULL, NULL);

	db_unregister_type_handler(DB_TYPE_PROJECT);
	db_unregister_type_handler(DB_TYPE_REGINFO);
	db_unregister_type_handler(DB_TYPE_CONTACT);
	db_unregister_type_handler(DB_TYPE_CHANNEL_NAMESPACE);

	del_conf_item("NAMESPACE_SEPARATORS", &chansvs.me->conf_table);
	del_conf_item("REGISTER_REQUIRE_NAMESPACE", &chansvs.me->conf_table);
	del_conf_item("REGISTER_REQUIRE_NAMESPACE_EXEMPT", &chansvs.me->conf_table);
	del_conf_item("REGISTER_PROJECT_ADVICE", &chansvs.me->conf_table);

	mowgli_patricia_destroy(projects_by_channelns, NULL, NULL);
	mowgli_patricia_destroy(projects, free_project_cb, NULL);

	hook_del_user_info(userinfo_hook);
	hook_del_channel_info(chaninfo_hook);
	hook_del_db_write(write_projects_db);
	hook_del_channel_can_register(try_register_hook);
	hook_del_channel_register(did_register_hook);
}

DECLARE_MODULE_V1
(
	"freenode/projectns", MODULE_UNLOAD_CAPABILITY_NEVER, _modinit, _moddeinit,
	"", "freenode <http://www.freenode.net>"
);
