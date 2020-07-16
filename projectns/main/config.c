/*
 * Copyright (c) 2018-2019 Janik Kleinhoff
 * Rights to this code are as documented in doc/LICENSE.
 *
 * Services awareness of group registrations
 * Core functionality - Configuration handlers
 */

#include "main.h"

void init_config(void)
{
	add_dupstr_conf_item("NAMESPACE_SEPARATORS", &projectsvs.me->conf_table, 0, &projectsvs.config.namespace_separators, "-");
	add_bool_conf_item("DEFAULT_OPEN_REGISTRATION", &projectsvs.me->conf_table, 0, &projectsvs.config.default_open_registration, false);
}

void deinit_config(void)
{
	del_conf_item("NAMESPACE_SEPARATORS", &projectsvs.me->conf_table);
	del_conf_item("DEFAULT_OPEN_REGISTRATION", &projectsvs.me->conf_table);
}
