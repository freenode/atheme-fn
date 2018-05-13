/*
 * Copyright (c) 2018 Janik Kleinhoff
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Services awareness of group registrations
 * Core functionality
 */

#include "projectns_common.h"

#define ENT_PRIVDATA_NAME "freenode:projects"
#define PERSIST_STORAGE_NAME "atheme.freenode.projectns.main.persist"

#define DB_TYPE_PROJECT           "FNGROUP"
#define DB_TYPE_REGINFO           "FNGRI"
#define DB_TYPE_CONTACT           "FNGC"
#define DB_TYPE_CHANNEL_NAMESPACE "FNCNS"
#define DB_TYPE_MARK              "FNGM"

unsigned int projectns_abirev = PROJECTNS_ABIREV;

char *parse_namespace(const char *chan);
bool is_valid_project_name(const char * const name);
mowgli_list_t *entity_get_projects(myentity_t *mt);
struct projectns *project_new(const char * const name);
void project_destroy(struct projectns * const p);
void show_marks(sourceinfo_t *si, struct projectns * const p);

struct projectsvs projectsvs = {
	.me = NULL,
	.project_new = project_new,
	.project_destroy = project_destroy,
	.show_marks = show_marks,
	.parse_namespace = parse_namespace,
	.is_valid_project_name = is_valid_project_name,
	.entity_get_projects = entity_get_projects,
};

struct projectns_main_persist {
	unsigned int version;

	service_t *service;
	mowgli_patricia_t *projects;
};

struct projectns *project_new(const char * const name)
{
	struct projectns *p = smalloc(sizeof(*p));
	p->name = sstrdup(name);

	return p;
}

void project_destroy(struct projectns * const p)
{
	mowgli_patricia_delete(projectsvs.projects, p->name);

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
		char *ns = n->data;
		mowgli_patricia_delete(projectsvs.projects_by_channelns, ns);

		free(ns);

		mowgli_node_delete(n, &p->channel_ns);
		mowgli_node_free(n);
	}
	MOWGLI_ITER_FOREACH_SAFE(n, tn, p->marks.head)
	{
		struct project_mark *mark = n->data;

		free(mark->setter_id);
		free(mark->setter_name);
		free(mark->mark);
		free(mark);

		mowgli_node_delete(n, &p->marks);
		mowgli_node_free(n);
	}
	free(p->name);
	free(p->reginfo);
	free(p);
}

void show_marks(sourceinfo_t *si, struct projectns *p)
{
	mowgli_node_t *n;
	MOWGLI_ITER_FOREACH(n, p->marks.head)
	{
		struct project_mark *m = n->data;

		struct tm tm;
		char time[BUFSIZE];
		tm = *localtime(&m->time);

		strftime(time, sizeof time, TIME_FORMAT, &tm);

		myuser_t *setter;
		const char *setter_name;

		if ((setter = myuser_find_uid(m->setter_id)) != NULL)
			setter_name = entity(setter)->name;
		else
			setter_name = m->setter_name;

		if (strcasecmp(setter_name, m->setter_name))
		{
			command_success_nodata(
					si,
					_("Mark \2%d\2 set by \2%s\2 (%s) on \2%s\2: %s"),
					m->number,
					m->setter_name,
					setter_name,
					time,
					m->mark
					);
		}
		else
		{
			command_success_nodata(
					si,
					_("Mark \2%d\2 set by \2%s\2 on \2%s\2: %s"),
					m->number,
					setter_name,
					time,
					m->mark
					);
		}
	}
}

/* You need to free() the return value of this when done */
char *parse_namespace(const char *chan)
{
	char *buf = sstrdup(chan);
	(void) strtok(buf, projectsvs.config.namespace_separators);

	return buf;
}

bool is_valid_project_name(const char * const name)
{
	/* Screen for anything that'd break parameter parsing or the protocol.
	 * Don't check for other kinds of stupidity as this module is meant to
	 * be used by network staff, who should know better. *grumble*
	 */
	return !(strchr(name, ' ') || strchr(name, '\n') || strchr(name, '\r') || strlen(name) >= PROJECTNAMELEN);
}

mowgli_list_t *entity_get_projects(myentity_t *mt)
{
	mowgli_list_t *l;

	l = privatedata_get(mt, ENT_PRIVDATA_NAME);
	if (l)
		return l;

	l = mowgli_list_create();
	privatedata_set(mt, ENT_PRIVDATA_NAME, l);

	return l;
}

static void db_h_project(database_handle_t *db, const char *type)
{
	const char *name     = db_sread_word(db);
	unsigned int any_reg = db_sread_uint(db);

	struct projectns *l = smalloc(sizeof(*l));
	l->name = sstrdup(name);
	l->any_may_register = any_reg;

	mowgli_patricia_add(projectsvs.projects, l->name, l);
}

static void db_h_reginfo(database_handle_t *db, const char *type)
{
	const char *name = db_sread_word(db);
	const char *info = db_sread_str(db);

	struct projectns *project = mowgli_patricia_retrieve(projectsvs.projects, name);
	project->reginfo = sstrdup(info);
}

static void db_h_mark(database_handle_t *db, const char *type)
{
	const char *name = db_sread_word(db);
	unsigned int num = db_sread_uint(db);
	time_t time      = db_sread_time(db);

	const char *setter_id   = db_sread_word(db);
	const char *setter_name = db_sread_word(db);

	const char *text = db_sread_str(db);

	struct projectns *project = mowgli_patricia_retrieve(projectsvs.projects, name);
	struct project_mark *mark = smalloc(sizeof *mark);
	mark->number = num;
	mark->time   = time;
	mark->mark   = sstrdup(text);
	mark->setter_id   = sstrdup(setter_id);
	mark->setter_name = sstrdup(setter_name);

	mowgli_node_add(mark, mowgli_node_create(), &project->marks);
}

static void db_h_contact(database_handle_t *db, const char *type)
{
	const char *project_name = db_sread_word(db);
	const char *contact_name = db_sread_word(db);

	struct projectns *project = mowgli_patricia_retrieve(projectsvs.projects, project_name);
	myentity_t *contact = myentity_find(contact_name);

	mowgli_node_add(project, mowgli_node_create(), entity_get_projects(contact));
	mowgli_node_add(contact, mowgli_node_create(), &project->contacts);
}

static void db_h_channelns(database_handle_t *db, const char *type)
{
	const char *project_name = db_sread_word(db);
	const char *namespace    = db_sread_word(db);

	struct projectns *project = mowgli_patricia_retrieve(projectsvs.projects, project_name);

	mowgli_patricia_add(projectsvs.projects_by_channelns, namespace, project);
	mowgli_node_add(sstrdup(namespace), mowgli_node_create(), &project->channel_ns);
}

static void write_projects_db(database_handle_t *db)
{
	mowgli_patricia_iteration_state_t state;
	struct projectns *project;

	MOWGLI_PATRICIA_FOREACH(project, &state, projectsvs.projects)
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
		MOWGLI_ITER_FOREACH(n, project->marks.head)
		{
			struct project_mark *mark = n->data;
			db_start_row(db, DB_TYPE_MARK);
			db_write_word(db, project->name);
			db_write_uint(db, mark->number);
			db_write_time(db, mark->time);
			db_write_word(db, mark->setter_id);
			db_write_word(db, mark->setter_name);
			db_write_str(db, mark->mark);
			db_commit_row(db);
		}

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

/* FIXME: This should hook into non-user entity deletions
 * as well (e.g. groupserv groups). Atheme does not currently
 * provide a generic entity deletion hook so this will have to do.
 */
static void userdelete_hook(myuser_t *mu)
{
	mowgli_list_t *l = entity_get_projects(entity(mu));
	mowgli_node_t *n, *tn;

	MOWGLI_ITER_FOREACH_SAFE(n, tn, l->head)
	{
		struct projectns *p = n->data;
		mowgli_node_delete(n, l);
		mowgli_node_free(n);

		slog(LG_REGISTER, _("PROJECT:CONTACT:LOST: \2%s\2 from \2%s\2"), entity(mu)->name, p->name);

		mowgli_node_t *n2, *tn2;
		MOWGLI_ITER_FOREACH_SAFE(n2, tn2, p->contacts.head)
		{
			if (entity(mu) == (myentity_t*)n2->data)
			{
				mowgli_node_delete(n2, &p->contacts);
				mowgli_node_free(n2);
				break;
			}
		}
	}
}

static void free_project_cb(const char *key, void *data, void *privdata)
{
	project_destroy((struct projectns *)data);
}

static void mod_init(module_t *const restrict m)
{
	projectsvs.projects = mowgli_patricia_create(strcasecanon);
	projectsvs.projects_by_channelns = mowgli_patricia_create(irccasecanon);

	struct projectns_main_persist *rec = mowgli_global_storage_get(PERSIST_STORAGE_NAME);
	if (rec)
	{
		// Fun to deal with upgrades
		if (rec->version > PROJECTNS_ABIREV)
		{
			slog(LG_ERROR, "freenode/projectns/main: attempted to load data from newer module (%u > %u)", rec->version, PROJECTNS_ABIREV);
			slog(LG_ERROR, "freenode/projectns/main: This module cannot be safely reloaded without restarting services");
			/* (among other things, it would cause us memory leaks as there may be pointers in the
			 * newer struct projectns that we won't know to free, plus data might be lost.
			 * Best to play it safe.)
			 */
			m->mflags = MODTYPE_FAIL;
			// At this point we can return as operserv/modreload will bail out anyway
			return;
		}
		slog(LG_DEBUG, "freenode/projectns/main: restoring pre-reload structures (old: %u; new: %u)", rec->version, PROJECTNS_ABIREV);
		projectsvs.me = rec->service;

		/* If rec->version == PROJECTNS_ABIREV, we could probably re-use rec->projects safely.
		 * However, this would mean the upgrade codepath would be separate and much less tested.
		 */

		mowgli_patricia_iteration_state_t state;
		struct projectns *old_p;

		MOWGLI_PATRICIA_FOREACH(old_p, &state, rec->projects)
		{
			mowgli_patricia_delete(rec->projects, old_p->name);
			struct projectns *new = smalloc(sizeof(*new));
			new->name = old_p->name;
			new->any_may_register = old_p->any_may_register;
			new->reginfo = old_p->reginfo;

			mowgli_patricia_add(projectsvs.projects, new->name, new);

			/* These are safe as the list metadata is copied by value;
			 * the actual lists comprise nodes of strings (for channel namespaces)
			 * or myentity_t* (for contacts), which are still valid.
			 *
			 * We do need to restore the reverse mappings as we destroyed them
			 * on unloading due to them having pointers that would now be stale.
			 */
			new->channel_ns = old_p->channel_ns;
			new->contacts   = old_p->contacts;

			mowgli_node_t *n;
			MOWGLI_ITER_FOREACH(n, new->channel_ns.head)
			{
				mowgli_patricia_add(projectsvs.projects_by_channelns, n->data, new);
			}

			MOWGLI_ITER_FOREACH(n, new->contacts.head)
			{
				mowgli_node_add(new, mowgli_node_create(), entity_get_projects(n->data));
			}

			/* If you wish to restore anything else, it will not have been there
			 * in past versions, so you *must* check rec->version to see whether
			 * the data is present or you *will* cause a crash or worse.
			 */
			free(old_p);
		}

		// don't pass a destructor callback; we took care of everything while iterating
		mowgli_patricia_destroy(rec->projects, NULL, NULL);

		/* The comment at the end of the above block applies outside the foreach as well. */
		mowgli_global_storage_free(PERSIST_STORAGE_NAME);
		free(rec);
		// Whew, we're done.
	}
	else
	{
		projectsvs.me = service_add("projectserv", NULL);
	}

	add_dupstr_conf_item("NAMESPACE_SEPARATORS", &projectsvs.me->conf_table, 0, &projectsvs.config.namespace_separators, "-");

	db_register_type_handler(DB_TYPE_PROJECT, db_h_project);
	db_register_type_handler(DB_TYPE_MARK, db_h_mark);
	db_register_type_handler(DB_TYPE_REGINFO, db_h_reginfo);
	db_register_type_handler(DB_TYPE_CONTACT, db_h_contact);
	db_register_type_handler(DB_TYPE_CHANNEL_NAMESPACE, db_h_channelns);

	hook_add_event("db_write");
	hook_add_db_write(write_projects_db);

	hook_add_event("myuser_delete");
	hook_add_myuser_delete(userdelete_hook);
}

static void mod_deinit(const module_unload_intent_t intent)
{
	if (intent == MODULE_UNLOAD_INTENT_RELOAD)
	{
		struct projectns_main_persist *rec = smalloc(sizeof rec);
		rec->version  = PROJECTNS_ABIREV;
		rec->service  = projectsvs.me;
		rec->projects = projectsvs.projects;

		mowgli_global_storage_put(PERSIST_STORAGE_NAME, rec);
	}
	else
	{
		service_delete(projectsvs.me);
		mowgli_patricia_destroy(projectsvs.projects, free_project_cb, NULL);
	}

	mowgli_patricia_destroy(projectsvs.projects_by_channelns, NULL, NULL);

	/* Clear entity->project mappings
	 * These store lists of pointers, which will be replaced
	 * even if we are being reloaded
	 */
	myentity_t *mt;
	myentity_iteration_state_t state;

	MYENTITY_FOREACH(mt, &state)
	{
		mowgli_list_t *l = privatedata_get(mt, ENT_PRIVDATA_NAME);
		if (l)
		{
			mowgli_node_t *n, *tn;
			MOWGLI_ITER_FOREACH_SAFE(n, tn, l->head)
			{
				mowgli_node_delete(n, l);
				mowgli_node_free(n);
			}
		}
	}

	del_conf_item("NAMESPACE_SEPARATORS", &projectsvs.me->conf_table);

	db_unregister_type_handler(DB_TYPE_PROJECT);
	db_unregister_type_handler(DB_TYPE_MARK);
	db_unregister_type_handler(DB_TYPE_REGINFO);
	db_unregister_type_handler(DB_TYPE_CONTACT);
	db_unregister_type_handler(DB_TYPE_CHANNEL_NAMESPACE);

	hook_del_myuser_delete(userdelete_hook);
	hook_del_db_write(write_projects_db);
}

DECLARE_MODULE_V1
(
	"freenode/projectns/main", MODULE_UNLOAD_CAPABILITY_RELOAD_ONLY, mod_init, mod_deinit,
	"", "freenode <http://www.freenode.net>"
);
