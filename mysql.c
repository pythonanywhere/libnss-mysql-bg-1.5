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

static const char rcsid[] =
    "$Id$";

#include "nss_mysql.h"
#include <stdio.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#include <stdarg.h>
#include <time.h>
#include <netinet/in.h>

con_info_t ci = { nfalse, 0, 0, 0, NULL };

static freturn_t
_nss_mysql_save_socket_info (void)
{
  socklen_t local_size = sizeof (struct sockaddr);
  socklen_t remote_size = sizeof (struct sockaddr);
  int r;

  function_enter;
  memset(&(ci.sock_info.local), 0, sizeof (ci.sock_info.local));
  r = getsockname (ci.link.net.fd, &(ci.sock_info.local), &local_size);
  if (r != RETURN_SUCCESS)
    function_return (RETURN_FAILURE);

  memset(&(ci.sock_info.remote), 0, sizeof (ci.sock_info.remote));
  r = getpeername (ci.link.net.fd, &(ci.sock_info.remote), &remote_size);
  function_return (r);
}

static nboolean
_nss_mysql_is_same_sockaddr (struct sockaddr orig, struct sockaddr cur)
{
  struct sockaddr_in orig_sin, cur_sin;
  sa_family_t family;

  function_enter;
  family = *(sa_family_t *)&orig;
  switch (family)
    {
    case AF_INET:
        orig_sin = *(struct sockaddr_in *) &orig;
        cur_sin = *(struct sockaddr_in *) &cur;
        if (orig_sin.sin_port != cur_sin.sin_port)
          function_return (nfalse);
        if (orig_sin.sin_addr.s_addr != cur_sin.sin_addr.s_addr)
          function_return (nfalse);
        break;
    case AF_UNIX:
        if (memcmp (&orig, &cur, sizeof (struct sockaddr)) != 0)
          function_return (nfalse);
        break;
    default:
        _nss_mysql_log (LOG_ERR, "%s: Unhandled family: %d", FNAME, family);
        break;
    }
  function_return (ntrue);
}

static nboolean
_nss_mysql_validate_socket (void)
{
  socklen_t local_size = sizeof (struct sockaddr);
  socklen_t remote_size = sizeof (struct sockaddr);
  struct sockaddr check;

  function_enter;

  memset(&check, 0, sizeof (check));
  if (getpeername (ci.link.net.fd, &check, &remote_size) != RETURN_SUCCESS)
    {
      _nss_mysql_debug (FNAME, D_CONNECT, "getpeername() failed");
      function_return (nfalse);
    }
  if (_nss_mysql_is_same_sockaddr (ci.sock_info.remote, check) != ntrue)
    {
      _nss_mysql_debug (FNAME, D_CONNECT, "remote socket mismatch");
      function_return (nfalse);
    }

  memset(&check, 0, sizeof (check));
  if (getsockname (ci.link.net.fd, &check, &local_size) != RETURN_SUCCESS)
    {
      _nss_mysql_debug (FNAME, D_CONNECT, "getsockname() failed");
      function_return (nfalse);
    }
  if (_nss_mysql_is_same_sockaddr (ci.sock_info.local, check) != ntrue)
    {
      _nss_mysql_debug (FNAME, D_CONNECT, "local socket mismatch");
      function_return (nfalse);
    }

  function_return (ntrue);
}

static NSS_STATUS
_nss_mysql_try_server (sql_server_t server)
{
  function_enter;
  _nss_mysql_debug (FNAME, D_CONNECT, "Connecting to server %s", server.host);
#ifdef HAVE_MYSQL_REAL_CONNECT /* comes from mysql.h, NOT config.h! */
#if MYSQL_VERSION_ID >= 32200  /* ditto */
  if (mysql_real_connect (&(ci.link), server.host, server.username,
                          server.password, NULL, server.port,
                          server.socket, 0))
#else /* MYSQL_VERSION_ID < 32200 */
  if (mysql_real_connect (&(ci.link), server.host, server.username,
                          server.password, server.port, server.socket, 0))
#endif /* MYSQL_VERSION_ID >= 32200 */
#else /* HAVE_MYSQL_REAL_CONNECT */
  if (mysql_connect (&(ci.link), server.host, server.username,
                     server.password))
#endif /* HAVE_MYSQL_REAL_CONNECT */
  
    {
      if (mysql_select_db (&(ci.link), server.database) != RETURN_SUCCESS)
        {
          _nss_mysql_log (LOG_EMERG, "Unable to select database %s: %s",
                          server.database, mysql_error ((&ci.link)));
          _nss_mysql_close_sql (CLOSE_LINK);
          function_return (NSS_UNAVAIL);
        }
      if (_nss_mysql_save_socket_info () != RETURN_SUCCESS )
        {
          _nss_mysql_log (LOG_EMERG, "Unable to save socket info");
          _nss_mysql_close_sql (CLOSE_LINK);
          function_return (NSS_UNAVAIL);
        }
      function_return (NSS_SUCCESS);
    }
  _nss_mysql_log (LOG_ALERT, "Connection to server number %s failed: %s",
                  server.host, mysql_error (&(ci.link)));
  function_return (NSS_UNAVAIL);
}

static nboolean
_nss_mysql_check_existing_connection (void)
{
  function_enter;
  if (ci.valid == nfalse)
    function_return (nfalse);

  if (_nss_mysql_validate_socket () == nfalse)
    {
       _nss_mysql_debug (FNAME, D_CONNECT,
                         "Socket changed - forcing reconnect");
      _nss_mysql_close_sql (CLOSE_RESULT);
      ci.valid = nfalse;
      function_return (nfalse);
    }
#ifdef HAVE_MYSQL_PING
  if (mysql_ping (&(ci.link)) == RETURN_FAILURE)
    {
      _nss_mysql_debug (FNAME, D_CONNECT, "Existing link failed: %s",
                        mysql_error (&(ci.link)));
      _nss_mysql_close_sql (CLOSE_LINK);
      function_return (nfalse);
    }
#endif
  _nss_mysql_debug (FNAME, D_CONNECT, "Using existing link");
  function_return (ntrue);
}

NSS_STATUS
_nss_mysql_connect_sql (conf_t conf)
{
  int i;
  time_t curTime;

  function_enter;
  if (_nss_mysql_check_existing_connection () == ntrue)
    {
      if (ci.result == NULL && ci.server_num != 0
          && ci.last_attempt + conf.global.retry <= time (&curTime))
        {
          _nss_mysql_debug (FNAME, D_CONNECT,
                            "Attempting to revert to primary server");
          _nss_mysql_close_sql (CLOSE_LINK);
          ci.server_num = 0;
          time (&(ci.last_attempt));
        }
      else
        {
          function_return (NSS_SUCCESS);
        }
    }

#ifdef HAVE_MYSQL_INIT
  if (mysql_init (&(ci.link)) == NULL)
    {
      _nss_mysql_log (LOG_EMERG, "mysql_init() failed");
      function_return (NSS_UNAVAIL);
    }
#endif /* HAVE_MYSQL_INIT */

  for (i = ci.server_num; i < conf.num_servers; i++)
    {
      if (_nss_mysql_try_server (conf.sql.server[i]) == NSS_SUCCESS)
        {
          ci.server_num = i;
          ci.valid = ntrue;
          function_return (NSS_SUCCESS);
        }
    }
  _nss_mysql_log (LOG_EMERG, "Unable to connect to any MySQL servers");
  function_return (NSS_UNAVAIL);
}

NSS_STATUS
_nss_mysql_close_sql (int flags)
{

  function_enter;
  if (flags & (CLOSE_RESULT | CLOSE_LINK) && ci.result)
    {
      _nss_mysql_debug (FNAME, D_CONNECT | D_MEMORY, "Freeing result");
      mysql_free_result (ci.result);
      ci.result = NULL;
    }
  if (flags & CLOSE_LINK && ci.valid == ntrue)
    {
      _nss_mysql_debug (FNAME, D_CONNECT, "Closing link");
      mysql_close (&(ci.link));
      ci.valid = nfalse;
    }
  function_return (NSS_SUCCESS);
}

NSS_STATUS
_nss_mysql_run_query (conf_t conf, char *query)
{
  int attempts;

  function_enter;
  attempts = 0;

  while (attempts < conf.num_servers)
    {
      attempts++;
      _nss_mysql_debug (FNAME, D_QUERY, "Query attempt #%d", attempts);
      ci.server_num = (ci.server_num + attempts - 1)
                      % conf.num_servers;
      if (_nss_mysql_connect_sql (conf) != NSS_SUCCESS)
        continue;
      if (!query || !strlen (query))
        {
          _nss_mysql_log (LOG_ALERT,
                          "Server #%d is missing a query in config",
                          ci.server_num);
          continue;
        }
      _nss_mysql_debug (FNAME, D_QUERY,
                        "Executing query on server #%d: %s",
                        ci.server_num, query);
      if ((mysql_query (&(ci.link), query)) != RETURN_SUCCESS)
        {
          _nss_mysql_log (LOG_ALERT, "Query failed: %s",
                          mysql_error (&(ci.link)));
          _nss_mysql_close_sql (CLOSE_LINK);
          continue;
        }
      if ((ci.result = mysql_store_result (&(ci.link))) == NULL)
        {
          _nss_mysql_log (LOG_ALERT, "store_result failed: %s",
                          mysql_error (&(ci.link)));
          _nss_mysql_close_sql (CLOSE_LINK);
          continue;
        }
      _nss_mysql_debug (FNAME, D_QUERY, "Rows returned: %d",
                        mysql_num_rows (ci.result));
      function_return (NSS_SUCCESS);
    }
  _nss_mysql_log (LOG_EMERG, "Unable to perform query on any MySQL server");
  function_return (NSS_UNAVAIL);
}

NSS_STATUS
_nss_mysql_load_result (void *result, char *buffer, size_t buflen,
                        field_info_t *fields)
{
  MYSQL_ROW row;
  int to_return;
  field_info_t *f;
  int i;
  int num_rows;
  char *p;
  int bufused;

  function_enter;
  num_rows = mysql_num_rows (ci.result);
  _nss_mysql_debug (FNAME, D_QUERY, "num = %d, idx = %d", num_rows, ci.idx);

  if (num_rows == 0)
    function_return (NSS_NOTFOUND);

  if (ci.idx > num_rows - 1)
    function_return (NSS_NOTFOUND);

  mysql_data_seek (ci.result, ci.idx);
  ci.idx++;

  if ((row = mysql_fetch_row (ci.result)) == NULL)
    {
      _nss_mysql_log (LOG_ALERT, "mysql_fetch_row() failed: %s",
                      mysql_error (&(ci.link)));
      function_return (NSS_UNAVAIL);
    }

  memset (buffer, 0, buflen);
  bufused = 0;
  for (f = fields, i = 0; f->name; f++, i++)
    {
      p = buffer + bufused;
      _nss_mysql_debug (FNAME, D_PARSE, "Loading: %s = '%s' (%d %d %d)",
                        f->name, row[i], i, f->ofs, f->type);
      to_return = _nss_mysql_liswb (row[i], result, p, buflen, &bufused,
                                    f->ofs, f->type);
      if (to_return != NSS_SUCCESS)
        function_return (to_return);
    }
  function_return (NSS_SUCCESS);
}

void
_nss_mysql_reset_ent (void)
{
  function_enter;
  _nss_mysql_close_sql (CLOSE_RESULT);
  ci.idx = 0;
  function_leave;
}

nboolean
_nss_mysql_active_result (void)
{
  function_enter;
  if (ci.result == NULL)
    function_return (nfalse);
  function_return (ntrue);
}
