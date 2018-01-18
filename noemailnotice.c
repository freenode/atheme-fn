/*
 * Copyright (c) 2016 Mike Quin
 * Rights to this code are as documented in doc/LICENSE.
 *
 * freenode on-identify notice to users with no valid email address set
 *
 */

#include "atheme.h"

DECLARE_MODULE_V1
(
	"freenode/noemailnotice", FALSE, _modinit, _moddeinit,
	PACKAGE_STRING,
	"freenode <http://freenode.net>"
);

static void user_identify_notice(user_t *u);

void _modinit(module_t *m)
{
	hook_add_event("user_identify");
	hook_add_user_identify(user_identify_notice);
}

void _moddeinit(module_unload_intent_t intentvoid)
{
	hook_del_user_identify(user_identify_notice);
}

static void user_identify_notice(user_t *u)
{
	myuser_t *mu = u->myuser;
	if (mu->flags & MU_WAITAUTH)
	{
		return;
	}
	if (!validemail(mu->email))
	{
		myuser_notice(nicksvs.nick, mu, "WARNING: Your NickServ account does not have a valid email address set.");
		myuser_notice(nicksvs.nick, mu, "Should you forget your password it may not be possible to recover your acccount.");
		myuser_notice(nicksvs.nick, mu, "For help setting an email address, see \2/msg NickServ HELP SET EMAIL\2.");
		myuser_notice(nicksvs.nick, mu, "Should you need more assistance you can /join #freenode to find network staff.");
	}
}
