/* Copyright (C) 2002 Ben Goodwin
   This file is part of the nss-mysql library.

   The nss-mysql library is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   The nss-mysql library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with the nss-mysql library; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Global variables and miscellaneous support routines
 */

static const char rcsid[] =
    "$Id$";

#include "nss_mysql.h"
#include <stdarg.h>

/* There's probably a better way than this hack; if so, please tell me */
#ifndef __USE_GNU
#define __USE_GNU
#include <dlfcn.h>
#undef __USE_GNU
#endif

pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
pthread_once_t _nss_mysql_once_control = {PTHREAD_ONCE_INIT};

static void
_nss_mysql_atfork_prepare (void)
{
  LOCK;
}

static void
_nss_mysql_atfork_parent (void)
{
  UNLOCK;
}

static void
_nss_mysql_atfork_child (void)
{
  _nss_mysql_close_sql (NULL, ntrue);
  UNLOCK;
}

static void
_nss_mysql_pthread_once_init (void)
{
  int (*pthread_atfork)();

  pthread_atfork = dlsym (RTLD_DEFAULT, "pthread_atfork");
  if (pthread_atfork)
    (*pthread_atfork) (_nss_mysql_atfork_prepare, _nss_mysql_atfork_parent,
                       _nss_mysql_atfork_child);
}

NSS_STATUS
_nss_mysql_init (void)
{
  int (*pthread_once)();

  pthread_once = dlsym (RTLD_DEFAULT, "pthread_once");
  if (pthread_once)
    (*pthread_once) (&_nss_mysql_once_control, _nss_mysql_pthread_once_init);
  return (_nss_mysql_load_config ());
}

/*
 * Syslog a message at PRIORITY.
 * Do *NOT* change this to maintain persistent connection - it will fail
 * under certain circumstances where programs using this library open and
 * close their own file descriptors.  I save the MySQL socket information
 * for later comparison for the same reason.
 */
void
_nss_mysql_log (int priority, char *fmt, ...)
{
  va_list ap;

  openlog (PACKAGE, OPENLOG_OPTIONS, SYSLOG_FACILITY);
  va_start (ap, fmt);
  vsyslog (priority, fmt, ap);
  va_end (ap);
  closelog ();
}

#ifdef HAVE_NSS_COMMON_H
NSS_STATUS
_nss_mysql_default_destr (nss_backend_t *be, void *args)
{
  _nss_mysql_free (be);
  /* Closing link & freeing memory unnecessary due to link w/ '-z nodelete' */
  return (NSS_SUCCESS);

}
#endif

/*
 * SET/END ent's call this.  While the definition of endent is to close
 * the file, I see no reason to actually do that - just clear the
 * current result set.
 */
void
_nss_mysql_reset_ent (MYSQL_RES **mresult)
{
  _nss_mysql_close_result (mresult);
}

