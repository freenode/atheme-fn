/*
 * Copyright (C) 2005-2007 William Pitcock, et al.
 * Rights to this code are as documented in doc/LICENSE.
 *
 * This code contains the channel mode definitions for oftc-hybrid.
 *
 * $Id: oftc-hybrid.h 21 2007-09-12 22:21:29Z jilles $
 */

#ifndef RATBOX_H
#define RATBOX_H


/* Note: constants here are in sync with hyperion.h and charybdis.h. */
#define CMODE_NOCOLOR	0x00001000	/* oftc-hybrid +c */
#define CMODE_REGONLY	0x00002000	/* oftc-hybrid +R */
#define CMODE_OPMOD	0x00004000	/* oftc-hybrid +z */
/*efine CMODE_FINVITE	0x00008000	 * oftc-hybrid +g */
/*efine CMODE_EXLIMIT   0x00010000       * oftc-hybrid +L */
/*efine CMODE_PERM      0x00020000       * oftc-hybrid +P */
/*efine CMODE_FTARGET   0x00040000       * oftc-hybrid +F */
/*efine CMODE_DISFWD    0x00080000       * oftc-hybrid +Q */
#define CMODE_MODREG    0x00200000      /* oftc-hybrid +M */
#define CMODE_SSLONLY   0x00400000      /* oftc-hybrid +S */

/*#define CMODE_HALFOP	0x10000000*/

#endif
