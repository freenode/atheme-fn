/*
 * Copyright (c) 2018-2019 Janik Kleinhoff
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Services awareness of group registrations
 * Core functionality - Data structure management
 */

#include "fn-compat.h"
#include "main.h"

bool contact_new(struct projectns * const p, myentity_t * const mt)
{
	mowgli_node_t *n, *tn;
	MOWGLI_ITER_FOREACH(n, p->contacts.head)
	{
		myentity_t *contact_mt = n->data;
		if (contact_mt == mt)
			return false;
	}

	mowgli_node_add(p,  mowgli_node_create(), projectsvs.entity_get_projects(mt));
	mowgli_node_add(mt, mowgli_node_create(), &p->contacts);
	return true;
}

bool contact_destroy(struct projectns * const p, myentity_t * const mt)
{
	mowgli_list_t *mt_projects = projectsvs.entity_get_projects(mt);
	mowgli_node_t *n, *tn;

	MOWGLI_ITER_FOREACH_SAFE(n, tn, mt_projects->head)
	{
		if (p == n->data)
		{
			mowgli_node_delete(n, mt_projects);
			mowgli_node_free(n);
			break;
		}
	}

	MOWGLI_ITER_FOREACH_SAFE(n, tn, p->contacts.head)
	{
		if (mt == n->data)
		{
			mowgli_node_delete(n, &p->contacts);
			mowgli_node_free(n);
			return true;
		}
	}

	return false;
}

struct projectns *project_new(const char * const name)
{
	struct projectns *project = smalloc(sizeof *project);

	project->name = sstrdup(name);
	project->any_may_register = projectsvs.config.default_open_registration;

	mowgli_patricia_add(projectsvs.projects, name, project);

	return project;
}

struct projectns *project_find(const char * const name)
{
	return mowgli_patricia_retrieve(projectsvs.projects, name);
}

void project_destroy(struct projectns * const p)
{
	mowgli_patricia_delete(projectsvs.projects, p->name);

	mowgli_node_t *n, *tn;

	MOWGLI_ITER_FOREACH_SAFE(n, tn, p->contacts.head)
	{
		contact_destroy(p, n->data);
	}

	MOWGLI_ITER_FOREACH_SAFE(n, tn, p->channel_ns.head)
	{
		char *ns = n->data;
		mowgli_patricia_delete(projectsvs.projects_by_channelns, ns);

		free(ns);

		mowgli_node_delete(n, &p->channel_ns);
		mowgli_node_free(n);
	}
	MOWGLI_ITER_FOREACH_SAFE(n, tn, p->cloak_ns.head)
	{
		char *ns = n->data;
		mowgli_patricia_delete(projectsvs.projects_by_cloakns, ns);

		free(ns);

		mowgli_node_delete(n, &p->cloak_ns);
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
	strshare_unref(p->creator);
	free(p);
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

	mowgli_list_free(l);
}

void init_structures(void)
{
	projectsvs.projects = mowgli_patricia_create(strcasecanon);
	projectsvs.projects_by_channelns = mowgli_patricia_create(irccasecanon);
	projectsvs.projects_by_cloakns = mowgli_patricia_create(strcasecanon);

	hook_add_myuser_delete(userdelete_hook);
}

void deinit_aux_structures(void)
{
	mowgli_patricia_destroy(projectsvs.projects_by_channelns, NULL, NULL);
	mowgli_patricia_destroy(projectsvs.projects_by_cloakns, NULL, NULL);

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

	hook_del_myuser_delete(userdelete_hook);
}
