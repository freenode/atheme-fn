/*
 * Copyright (c) 2018 Janik Kleinhoff
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Services awareness of group registrations
 * Common types
 */

#ifndef PROJECTNS_COMMON_H
#define PROJECTNS_COMMON_H

#include "atheme.h"

#define PRIV_PROJECT_ADMIN  "project:admin"
#define PRIV_PROJECT_AUSPEX "project:auspex"

// Arbitrary number that should avoid truncation even with various protocol overhead
#define PROJECTNAMELEN CHANNELLEN

#define PROJECTNS_ABIREV 3U

struct project_mark {
	time_t time;
	unsigned int number;
	char *mark;
	char *setter_id;
	char *setter_name;
};

struct projectns {
	char *name;
	bool any_may_register;
	char *reginfo;
	mowgli_list_t contacts;
	mowgli_list_t channel_ns;
	mowgli_list_t marks;
};

struct projectsvs_conf {
	char *namespace_separators;
#if 0
	bool register_require_namespace;
	char *register_require_namespace_exempt;
	char *register_project_advice;
#endif
};

struct projectsvs {
	service_t *me;
	mowgli_patricia_t *projects;
	mowgli_patricia_t *projects_by_channelns;
	struct projectsvs_conf config;
	struct projectns *(*project_new)(const char *name);
	void (*project_destroy)(struct projectns *p);
	void (*show_marks)(sourceinfo_t *si, struct projectns *p);
	char *(*parse_namespace)(const char *chan);
	bool (*is_valid_project_name)(const char *name);
	mowgli_list_t *(*entity_get_projects)(myentity_t *mt);
};

#endif
