/*
 * Copyright (c) 2009 - 2015, Micro Systems Marc Balmer, CH-5073 Gipf-Oberfrick
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Micro Systems Marc Balmer nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/* PostgreSQL extension module (using Lua) */

#ifdef __APPLE__
#include <libkern/OSByteOrder.h>
#define htobe64(x) OSSwapHostToBigInt64(x)
#elif __linux__
#include <endian.h>
#endif
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include <libpq-fe.h>
#include <libpq/libpq-fs.h>
#include <pg_config.h>

#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>

#include "luapgsql.h"

#if LUA_VERSION_NUM < 502
#define lua_setuservalue lua_setfenv
#define lua_getuservalue lua_getfenv
#endif

static PGconn **
pgsql_conn_new(lua_State *L) {
	PGconn **data;

	data = lua_newuserdata(L, sizeof(PGconn *));
	*data = NULL;
	lua_newtable(L);
	lua_setuservalue(L, -2);
	luaL_getmetatable(L, CONN_METATABLE);
	lua_setmetatable(L, -2);
	return data;
}

/*
 * Database Connection Control Functions
 */
static int
pgsql_connectdb(lua_State *L)
{
	PGconn **data;

	data = pgsql_conn_new(L);
	*data = PQconnectdb(luaL_checkstring(L, 1));
	if (*data == NULL)
		lua_pushnil(L);
	return 1;
}

static int
pgsql_connectStart(lua_State *L)
{
	PGconn **data;

	data = pgsql_conn_new(L);
	*data = PQconnectStart(luaL_checkstring(L, 1));
	if (*data == NULL)
		lua_pushnil(L);
	return 1;
}

static PGconn *
pgsql_conn(lua_State *L, int n)
{
	PGconn **data;

	data = luaL_checkudata(L, n, CONN_METATABLE);
	luaL_argcheck(L, *data != NULL, n, "database connection is finished");
	return *data;
}

static int
pgsql_connectPoll(lua_State *L)
{
	lua_pushinteger(L, PQconnectPoll(pgsql_conn(L, 1)));
	return 1;
}

static int
pgsql_libVersion(lua_State *L)
{
	lua_pushinteger(L, PQlibVersion());
	return 1;
}

#if PG_VERSION_NUM >= 90100
static int
pgsql_ping(lua_State *L)
{
	lua_pushinteger(L, PQping(luaL_checkstring(L, 1)));
	return 1;
}
#endif

static int
pgsql_encryptPassword(lua_State *L)
{
	char *encrypted;

	encrypted = PQencryptPassword(luaL_checkstring(L, 1),
	    luaL_checkstring(L, 2));
	if (encrypted != NULL) {
		lua_pushstring(L, encrypted);
		PQfreemem(encrypted);
	} else
		lua_pushnil(L);
	return 1;
}

static int
conn_finish(lua_State *L)
{
	PGconn **conn;

	conn = luaL_checkudata(L, 1, CONN_METATABLE);
	if (*conn) {
		/*
		 * Check in the registry if a value has been stored at
		 * index '*conn'; if a value is found, don't close the
		 * connection.
		 * This mechanism can be used when the PostgreSQL connection
		 * object is provided to Lua from a C program that wants to
		 * ensure the connections stays open, even when the Lua
		 * program has terminated.
		 * To prevent the closing of the connection, use the following
		 * code to set a value in the registry at index '*conn' just
		 * before handing the connection object to Lua:
		 *
		 * PGconn *conn, **data;
		 *
		 * conn = PQconnectdb(...);
		 * data = lua_newuserdata(L, sizeof(PGconn *));
		 * *data = conn;
		 * lua_pushlightuserdata(L, *data);
		 * lua_pushboolean(L, 1);
		 * lua_settable(L, LUA_REGISTRYINDEX);
		 */
		lua_pushlightuserdata(L, *conn);
		lua_gettable(L, LUA_REGISTRYINDEX);
		if (lua_isnil(L, -1)) {
			PQfinish(*conn);
			*conn = NULL;
			/* clean out now invalidated keys from uservalue */
			lua_getuservalue(L, 1);
			lua_pushnil(L);
			lua_setfield(L, -2, "trace_file");
		} else
			lua_pop(L, 1);
	}
	return 0;
}

static int
conn_reset(lua_State *L)
{
	PQreset(pgsql_conn(L, 1));
	return 0;
}

static int
conn_resetStart(lua_State *L)
{
	lua_pushinteger(L, PQresetStart(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_resetPoll(lua_State *L)
{
	lua_pushinteger(L, PQresetPoll(pgsql_conn(L, 1)));
	return 1;
}

/*
 * Connection status functions
 */
static int
conn_db(lua_State *L)
{
	lua_pushstring(L, PQdb(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_user(lua_State *L)
{
	lua_pushstring(L, PQuser(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_pass(lua_State *L)
{
	lua_pushstring(L, PQpass(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_host(lua_State *L)
{
	lua_pushstring(L, PQhost(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_port(lua_State *L)
{
	lua_pushstring(L, PQport(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_tty(lua_State *L)
{
	lua_pushstring(L, PQtty(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_options(lua_State *L)
{
	lua_pushstring(L, PQoptions(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_status(lua_State *L)
{
	lua_pushinteger(L, PQstatus(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_transactionStatus(lua_State *L)
{
	lua_pushinteger(L, PQtransactionStatus(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_parameterStatus(lua_State *L)
{
	const char *status;

	status = PQparameterStatus(pgsql_conn(L, 1), luaL_checkstring(L, 2));
	if (status == NULL)
		lua_pushnil(L);
	else
		lua_pushstring(L, status);
	return 1;
}

static int
conn_protocolVersion(lua_State *L)
{
	lua_pushinteger(L, PQprotocolVersion(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_serverVersion(lua_State *L)
{
	lua_pushinteger(L, PQserverVersion(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_errorMessage(lua_State *L)
{
	lua_pushstring(L, PQerrorMessage(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_socket(lua_State *L)
{
	lua_pushinteger(L, PQsocket(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_backendPID(lua_State *L)
{
	lua_pushinteger(L, PQbackendPID(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_connectionNeedsPassword(lua_State *L)
{
	lua_pushboolean(L, PQconnectionNeedsPassword(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_connectionUsedPassword(lua_State *L)
{
	lua_pushboolean(L, PQconnectionUsedPassword(pgsql_conn(L, 1)));
	return 1;
}

/*
 * Command Execution Functions
 */
static int
conn_exec(lua_State *L)
{
	PGresult **res;

	res = lua_newuserdata(L, sizeof(PGresult *));
	*res = PQexec(pgsql_conn(L, 1), luaL_checkstring(L, 2));
	luaL_getmetatable(L, RES_METATABLE);
	lua_setmetatable(L, -2);
	return 1;
}

static int
get_sql_params(lua_State *L, int t, int n, Oid *paramTypes, char **paramValues,
    int *paramLengths, int *paramFormats, int *count)
{
	int k, c;

	switch (lua_type(L, t)) {
	case LUA_TBOOLEAN:
		if (paramTypes != NULL)
			paramTypes[n] = BOOLOID;
		if (paramValues != NULL) {
			paramValues[n] = malloc(1);
			if (paramValues[n] == NULL)
				return -1;
			*(char *)paramValues[n] = lua_toboolean(L, t);
			paramLengths[n] = 1;
			paramFormats[n] = 1;
		}
		n = 1;
		break;
	case LUA_TNUMBER:
		if (paramTypes != NULL)
#if LUA_VERSION_NUM >= 503
			if (lua_isinteger(L, t))
				paramTypes[n] = INT8OID;
			else
#endif
				paramTypes[n] = FLOAT8OID;
		if (paramValues != NULL) {
			union {
				double v;
				uint64_t i;
			} swap;

#if LUA_VERSION_NUM >= 503
			if (lua_isinteger(L, t))
				swap.i = lua_tointeger(L, t);
			else
#endif
				swap.v = lua_tonumber(L, t);
			paramValues[n] = malloc(sizeof(uint64_t));
			if (paramValues[n] == NULL)
				return -1;
			*(uint64_t *)paramValues[n] = htobe64(swap.i);
			paramLengths[n] = sizeof(uint64_t);
			paramFormats[n] = 1;
		}
		n = 1;
		break;
	case LUA_TSTRING:
		if (paramTypes != NULL)
			paramTypes[n] = TEXTOID;
		if (paramValues != NULL) {
			const char *s;
			size_t len;

			s = lua_tolstring(L, t, &len);
			paramValues[n] = malloc(len + 1);
			if (paramValues[n] == NULL)
				return -1;
			/*
			 * lua_tolstring returns a string with '\0' after
			 * the last character.
			 */
			memcpy(paramValues[n], s, len + 1);
		}
		n = 1;
		break;
	case LUA_TNIL:
		if (paramValues != NULL)
			paramValues[n] = NULL;
		n = 1;
		break;
	case LUA_TTABLE:
		for (k = 1;; k++) {
			lua_pushinteger(L, k);
			lua_gettable(L, t);
			if (lua_isnil(L, -1))
				break;
			c = 0;
			if (get_sql_params(L, -1, n, paramTypes, paramValues,
			    paramLengths, paramFormats, &c))
			    	return -1;
			n += c;
			lua_pop(L, 1);
		}
		lua_pop(L, 1);
		break;
	default:
		return luaL_argerror(L, t, "unsupported type");
	}
	*count = n;
	return 0;
}

static int
conn_execParams(lua_State *L)
{
	PGresult **res;
	Oid *paramTypes;
	char **paramValues;
	int n, nParams, sqlParams, *paramLengths, *paramFormats, count;

	nParams = lua_gettop(L) - 2;	/* subtract connection and command */
	if (nParams < 0)
		nParams = 0;

	for (n = 0, sqlParams = 0; n < nParams; n++) {
		get_sql_params(L, 3 + n, sqlParams, NULL, NULL, NULL, NULL,
		    &count);
		sqlParams += count;
	}
	if (sqlParams) {
		paramTypes = calloc(sqlParams, sizeof(Oid));
		paramValues = calloc(sqlParams, sizeof(char *));
		paramLengths = calloc(sqlParams, sizeof(int));
		paramFormats = calloc(sqlParams, sizeof(int));

		if (paramTypes == NULL || paramValues == NULL
		    || paramLengths == NULL || paramFormats == NULL)
		    	goto errout;

		for (n = 0, sqlParams = 0; n < nParams; n++) {
			if (get_sql_params(L, 3 + n, sqlParams, paramTypes,
			    paramValues, paramLengths, paramFormats, &count))
			    	goto errout;
			sqlParams += count;
		}
	} else {
		paramTypes = NULL;
		paramValues = NULL;
		paramLengths = NULL;
		paramFormats = NULL;
	}
	res = lua_newuserdata(L, sizeof(PGresult *));
	*res = PQexecParams(pgsql_conn(L, 1),
	    luaL_checkstring(L, 2), sqlParams, paramTypes,
	    (const char * const*)paramValues, paramLengths, paramFormats, 0);
	luaL_getmetatable(L, RES_METATABLE);
	lua_setmetatable(L, -2);
	if (sqlParams) {
		for (n = 0; n < sqlParams; n++)
			free((void *)paramValues[n]);
		free(paramTypes);
		free(paramValues);
		free(paramLengths);
		free(paramFormats);
	}
	return 1;

errout:
	if (paramValues) {
		for (n = 0; n < sqlParams; n++)
			free((void *)paramValues[n]);
		free(paramValues);
	}
	free(paramTypes);
	free(paramLengths);
	free(paramFormats);
	return luaL_error(L, "out of memory");
}

static int
conn_prepare(lua_State *L)
{
	PGresult **res;
	Oid *paramTypes;
	int n, nParams, sqlParams, count;

	nParams = lua_gettop(L) - 3;	/* subtract connection, name, command */
	if (nParams < 0)
		nParams = 0;

	for (n = 0, sqlParams = 0; n < nParams; n++) {
		get_sql_params(L, 4 + n, sqlParams, NULL, NULL, NULL, NULL,
		    &count);
		sqlParams += count;
	}
	if (sqlParams) {
		paramTypes = calloc(sqlParams, sizeof(Oid));
		if (paramTypes == NULL)
			return luaL_error(L, "out of memory");

		for (n = 0, sqlParams = 0; n < nParams; n++) {
			if (get_sql_params(L, 4 + n, sqlParams,
			    paramTypes, NULL, NULL, NULL, &count)) {
			    	free(paramTypes);
			    	return luaL_error(L, "out of memory");
			}
			sqlParams += count;
		}
	} else
		paramTypes = NULL;
	res = lua_newuserdata(L, sizeof(PGresult *));
	*res = PQprepare(pgsql_conn(L, 1), luaL_checkstring(L, 2),
	    luaL_checkstring(L, 3), sqlParams, paramTypes);
	luaL_getmetatable(L, RES_METATABLE);
	lua_setmetatable(L, -2);
	if (sqlParams)
		free(paramTypes);
	return 1;
}

static int
conn_execPrepared(lua_State *L)
{
	PGresult **res;
	char **paramValues;
	int n, nParams, sqlParams, *paramLengths, *paramFormats, count;

	nParams = lua_gettop(L) - 2;	/* subtract connection and name */
	if (nParams < 0)
		nParams = 0;

	for (n = 0, sqlParams = 0; n < nParams; n++) {
		get_sql_params(L, 3 + n, sqlParams, NULL, NULL,  NULL, NULL,
		    &count);
		sqlParams += count;
	}
	if (sqlParams) {
		paramValues = calloc(sqlParams, sizeof(char *));
		paramLengths = calloc(sqlParams, sizeof(int));
		paramFormats = calloc(sqlParams, sizeof(int));

		if (paramValues == NULL || paramLengths == NULL
		    || paramFormats == NULL)
		    	goto errout;

		for (n = 0, sqlParams = 0; n < nParams; n++) {
			if (get_sql_params(L, 3 + n, sqlParams, NULL,
			    paramValues, paramLengths, paramFormats, &count))
			    	goto errout;
			sqlParams += count;
		}
	} else {
		paramValues = NULL;
		paramLengths = NULL;
		paramFormats = NULL;
	}
	res = lua_newuserdata(L, sizeof(PGresult *));
	*res = PQexecPrepared(pgsql_conn(L, 1), luaL_checkstring(L, 2),
	    sqlParams, (const char * const*)paramValues, paramLengths,
	    paramFormats, 0);
	luaL_getmetatable(L, RES_METATABLE);
	lua_setmetatable(L, -2);
	if (sqlParams) {
		for (n = 0; n < sqlParams; n++)
			free((void *)paramValues[n]);
		free(paramValues);
		free(paramLengths);
		free(paramFormats);
	}
	return 1;
errout:
	if (paramValues) {
		for (n = 0; n < sqlParams; n++)
			free(paramValues[n]);
		free(paramValues);
	}
	free(paramLengths);
	free(paramFormats);
	return luaL_error(L, "out of memory");
}

static int
conn_describePrepared(lua_State *L)
{
	PGresult **res;
	res = lua_newuserdata(L, sizeof(PGresult *));
	*res = PQdescribePrepared(pgsql_conn(L, 1), luaL_checkstring(L, 2));
	luaL_getmetatable(L, RES_METATABLE);
	lua_setmetatable(L, -2);
	return 1;
}

static int
conn_describePortal(lua_State *L)
{
	PGresult **res;
	res = lua_newuserdata(L, sizeof(PGresult *));
	*res = PQdescribePortal(pgsql_conn(L, 1), luaL_checkstring(L, 2));
	luaL_getmetatable(L, RES_METATABLE);
	lua_setmetatable(L, -2);
	return 1;
}

static int
conn_escapeString(lua_State *L)
{
	PGconn *d;
	size_t len;
	char *buf;
	const char *str;
	int error;

	d = pgsql_conn(L, 1);

	str = lua_tolstring(L, 2, &len);
	if (str == NULL) {
		lua_pushnil(L);
		return 1;
	}
	buf = calloc(len + 1, 2);
	if (buf == NULL)
		return luaL_error(L, "out of memory");

	PQescapeStringConn(d, buf, str, len, &error);
	if (!error)
		lua_pushstring(L, buf);
	else
		lua_pushnil(L);
	free(buf);
	return 1;
}

static int
conn_escapeLiteral(lua_State *L)
{
	const char *s;
	char *p;
	PGconn *d;
	size_t len;

	d = pgsql_conn(L, 1);
	s = luaL_checklstring(L, 2, &len);
	p = PQescapeLiteral(d, s, len);
	lua_pushstring(L, p);
	PQfreemem(p);
	return 1;
}

static int
conn_escapeIdentifier(lua_State *L)
{
	const char *s;
	char *p;
	PGconn *d;
	size_t len;

	d = pgsql_conn(L, 1);
	s = luaL_checklstring(L, 2, &len);
	p = PQescapeIdentifier(d, s, len);
	lua_pushstring(L, p);
	PQfreemem(p);
	return 1;
}

static int
conn_escapeBytea(lua_State *L)
{
	unsigned char *p;
	const unsigned char *s;
	PGconn *d;
	size_t from_length, to_length;

	d = pgsql_conn(L, 1);
	s = (const unsigned char *)luaL_checklstring(L, 2, &from_length);
	p = PQescapeByteaConn(d, s, from_length, &to_length);
	lua_pushstring(L, (const char *)p);
	lua_pushinteger(L, to_length);
	PQfreemem(p);
	return 2;
}

static int
conn_unescapeBytea(lua_State *L)
{
	unsigned char *p;
	size_t len;

	p = PQunescapeBytea((const unsigned char *)luaL_checkstring(L, 2),
	    &len);
	if (p == NULL)
		lua_pushnil(L);
	else {
		lua_pushlstring(L, (const char *)p, len);
		PQfreemem(p);
	}
	return 1;
}

/*
 * Asynchronous Command Execution Functions
 */
static int
conn_sendQuery(lua_State *L)
{
	lua_pushinteger(L, PQsendQuery(pgsql_conn(L, 1),
	    luaL_checkstring(L, 2)));
	return 1;
}

static int
conn_sendQueryParams(lua_State *L)
{
	Oid *paramTypes;
	char **paramValues;
	int n, nParams, sqlParams, *paramLengths, *paramFormats, count;

	nParams = lua_gettop(L) - 2;	/* subtract connection and command */
	if (nParams < 0)
		nParams = 0;

	for (n = 0, sqlParams = 0; n < nParams; n++) {
		get_sql_params(L, 3 + n, 0, NULL, NULL, NULL, NULL, &count);
		sqlParams += count;
	}
	if (sqlParams) {
		paramTypes = calloc(sqlParams, sizeof(Oid));
		paramValues = calloc(sqlParams, sizeof(char *));
		paramLengths = calloc(sqlParams, sizeof(int));
		paramFormats = calloc(sqlParams, sizeof(int));

		if (paramTypes == NULL || paramValues == NULL
		    || paramLengths == NULL || paramFormats == NULL)
		    	goto errout;

		for (n = 0, sqlParams = 0; n < nParams; n++) {
			if (get_sql_params(L, 3 + n, sqlParams, paramTypes,
			    paramValues, paramLengths, paramFormats, &count))
			    	goto errout;
			sqlParams += count;
		}
	} else {
		paramTypes = NULL;
		paramValues = NULL;
		paramLengths = NULL;
		paramFormats = NULL;
	}
	lua_pushinteger(L,
	    PQsendQueryParams(pgsql_conn(L, 1),
	    luaL_checkstring(L, 2), sqlParams, paramTypes,
	    (const char * const*)paramValues, paramLengths, paramFormats, 0));
	if (sqlParams) {
		for (n = 0; n < sqlParams; n++)
			free((void *)paramValues[n]);
		free(paramTypes);
		free(paramValues);
		free(paramLengths);
		free(paramFormats);
	}
	return 1;
errout:
	if (paramValues) {
		for (n = 0; n < sqlParams; n++)
			free((void *)paramValues[n]);
		free(paramValues);
	}
	free(paramTypes);
	free(paramLengths);
	free(paramFormats);
	return luaL_error(L, "out of memory");
}

static int
conn_sendPrepare(lua_State *L)
{
	Oid *paramTypes;
	int n, nParams, sqlParams, count;

	nParams = lua_gettop(L) - 3;	/* subtract connection, name, command */
	if (nParams < 0)
		nParams = 0;

	for (n = 0, sqlParams = 0; n < nParams; n++) {
		get_sql_params(L, 4 + n, 0, NULL, NULL, NULL, NULL, &count);
		sqlParams += count;
	}
	if (sqlParams) {
		paramTypes = calloc(sqlParams, sizeof(Oid));
		if (paramTypes == NULL)
			return luaL_error(L, "out of memory");

		for (n = 0, sqlParams = 0; n < nParams; n++) {
			if (get_sql_params(L, 4 + n, sqlParams,
			    paramTypes, NULL, NULL, NULL, &count)) {
			    	free(paramTypes);
			    	return luaL_error(L, "out of memory");
			}
			sqlParams += count;
		}
	} else
		paramTypes = NULL;
	lua_pushinteger(L, PQsendPrepare(pgsql_conn(L, 1),
	    luaL_checkstring(L, 2), luaL_checkstring(L, 3), sqlParams,
	    paramTypes));
	if (sqlParams)
		free(paramTypes);
	return 1;
}

static int
conn_sendQueryPrepared(lua_State *L)
{
	char **paramValues;
	int n, nParams, sqlParams, *paramLengths, *paramFormats, count;

	nParams = lua_gettop(L) - 2;	/* subtract connection and name */
	if (nParams < 0)
		nParams = 0;

	for (n = 0, sqlParams = 0; n < nParams; n++) {
		get_sql_params(L, 3 + n, 0, NULL, NULL, NULL, NULL, &count);
		sqlParams += count;
	}
	if (sqlParams) {
		paramValues = calloc(sqlParams, sizeof(char *));
		paramLengths = calloc(sqlParams, sizeof(int));
		paramFormats = calloc(sqlParams, sizeof(int));

		if (paramValues == NULL || paramLengths == NULL
		    || paramFormats == NULL)
		    	goto errout;

		for (n = 0, sqlParams = 0; n < nParams; n++) {
			if (get_sql_params(L, 3 + n, sqlParams, NULL,
			    paramValues, paramLengths, paramFormats, &count))
			    	goto errout;
			sqlParams += count;
		}
	} else {
		paramValues = NULL;
		paramLengths = NULL;
		paramFormats = NULL;
	}
	lua_pushinteger(L,
	    PQsendQueryPrepared(pgsql_conn(L, 1), luaL_checkstring(L, 2),
	    nParams, (const char * const*)paramValues, paramLengths,
	    paramFormats, 0));
	if (nParams) {
		for (n = 0; n < nParams; n++)
			free((void *)paramValues[n]);
		free(paramValues);
		free(paramLengths);
		free(paramFormats);
	}
	return 1;
errout:
	if (paramValues) {
		for (n = 0; n < nParams; n++)
			free(paramValues[n]);
		free(paramValues);
	}
	free(paramLengths);
	free(paramFormats);
	return luaL_error(L, "out of memory");
}

static int
conn_sendDescribePrepared(lua_State *L)
{
	lua_pushinteger(L,
	    PQsendDescribePrepared(pgsql_conn(L, 1), luaL_checkstring(L, 2)));
	return 1;
}

static int
conn_sendDescribePortal(lua_State *L)
{
	lua_pushinteger(L,
	    PQsendDescribePortal(pgsql_conn(L, 1), luaL_checkstring(L, 2)));
	return 1;
}

static int
conn_getResult(lua_State *L)
{
	PGresult *r, **res;

	r = PQgetResult(pgsql_conn(L, 1));
	if (r == NULL)
		lua_pushnil(L);
	else {
		res = lua_newuserdata(L, sizeof(PGresult *));
		*res = r;
		luaL_getmetatable(L, RES_METATABLE);
		lua_setmetatable(L, -2);
	}
	return 1;
}

static int
conn_cancel(lua_State *L)
{
	PGconn *d;
	PGcancel *cancel;
	char errbuf[256];
	int res = 1;

	d = pgsql_conn(L, 1);
	cancel = PQgetCancel(d);
	if (cancel != NULL) {
		res = PQcancel(cancel, errbuf, sizeof errbuf);
		if (!res) {
			lua_pushboolean(L, 0);
			lua_pushstring(L, errbuf);
		} else
			lua_pushboolean(L, 1);
		PQfreeCancel(cancel);
	} else
		lua_pushboolean(L, 0);
	return res == 1 ? 1 : 2;
}

#if PG_VERSION_NUM >= 90200
static int
conn_setSingleRowMode(lua_State *L)
{
	lua_pushinteger(L, PQsetSingleRowMode(pgsql_conn(L, 1)));
	return 1;
}
#endif

/*
 * Asynchronous Notification Functions
 */
static int
conn_notifies(lua_State *L)
{
	PGnotify **notify, *n;

	n = PQnotifies(pgsql_conn(L, 1));
	if (n == NULL)
		lua_pushnil(L);
	else {
		notify = lua_newuserdata(L, sizeof(PGnotify *));
		*notify = n;
		luaL_getmetatable(L, NOTIFY_METATABLE);
		lua_setmetatable(L, -2);
	}
	return 1;
}

/*
 * Commands associated with the COPY command
 */
static int
conn_putCopyData(lua_State *L)
{
	const char *data;
	size_t len;

	data = luaL_checklstring(L, 2, &len);
	lua_pushinteger(L, PQputCopyData(pgsql_conn(L, 1), data, len));
	return 1;
}

static int
conn_putCopyEnd(lua_State *L)
{
	lua_pushinteger(L, PQputCopyEnd(pgsql_conn(L, 1), NULL));
	return 1;
}

static int
conn_getCopyData(lua_State *L)
{
	int res;
	char *data;

	res = PQgetCopyData(pgsql_conn(L, 1), &data, 0);
	if (res > 0)
		lua_pushstring(L, data);
	else
		lua_pushnil(L);
	if (data)
		PQfreemem(data);
	return 1;
}

/*
 * Control functions
 */
static int
conn_clientEncoding(lua_State *L)
{
	lua_pushstring(L,
	    pg_encoding_to_char(PQclientEncoding(pgsql_conn(L, 1))));
	return 1;
}

static int
conn_setClientEncoding(lua_State *L)
{
	if (PQsetClientEncoding(pgsql_conn(L, 1), luaL_checkstring(L, 2)))
		lua_pushboolean(L, 0);
	else
		lua_pushboolean(L, 1);
	return 1;
}

static int
conn_setErrorVerbosity(lua_State *L)
{
	lua_pushinteger(L,
	    PQsetErrorVerbosity(pgsql_conn(L, 1), luaL_checkinteger(L, 2)));
	return 1;
}

static int
closef_untrace(lua_State *L)
{
	PGconn *conn;
	lua_CFunction cf;

	luaL_checkudata(L, 1, LUA_FILEHANDLE);

	/* untrace so libpq doesn't segfault */
	lua_getuservalue(L, 1);
	lua_getfield(L, -1, "PGconn");
	conn = pgsql_conn(L, -1);
	lua_getfield(L, -2, "old_uservalue");
#if LUA_VERSION_NUM >= 502
	lua_getfield(L, -3, "old_closef");
#else
	lua_getfield(L, -1, "__close");
#endif
	cf = lua_tocfunction(L, -1);
	lua_pop(L, 1);
	lua_setuservalue(L, 1);

	PQuntrace(conn);

	/* let go of PGconn's reference to file handle */
	lua_getuservalue(L, -1);
	lua_pushnil(L);
	lua_setfield(L, -2, "trace_file");

	/* pop stream uservalue, PGconn, PGconn uservalue */
	lua_pop(L, 3);

	/* call original close function */
	return (*cf)(L);
}

static int
conn_trace(lua_State *L)
{
	PGconn *conn;
#if LUA_VERSION_NUM >= 502
	luaL_Stream *stream;

	conn = pgsql_conn(L, 1);
	stream = luaL_checkudata(L, 2, LUA_FILEHANDLE);
	luaL_argcheck(L, stream->f != NULL, 2, "invalid file handle");

	/*
	 * Keep a reference to the file object in uservalue of connection
	 * so it doesn't get garbage collected.
	 */
	lua_getuservalue(L, 1);
	lua_pushvalue(L, 2);
	lua_setfield(L, -2, "trace_file");

	/*
	 * Swap out closef luaL_Stream member for our wrapper that will
	 * untrace.
	 */
	lua_createtable(L, 0, 3);
	lua_getuservalue(L, 2);
	lua_setfield(L, -2, "old_uservalue");
	lua_pushcfunction(L, stream->closef);
	lua_setfield(L, -2, "old_closef");
	lua_pushvalue(L, 1);
	lua_setfield(L, -2, "PGconn");
	lua_setuservalue(L, 2);
	stream->closef = closef_untrace;

	PQtrace(conn, stream->f);
#else
	FILE **fp;

	conn = pgsql_conn(L, 1);
	fp = luaL_checkudata(L, 2, LUA_FILEHANDLE);
	luaL_argcheck(L, *fp != NULL, 2, "invalid file handle");

	/*
	 * Keep a reference to the file object in uservalue of connection
	 * so it doesn't get garbage collected.
	 */
	lua_getuservalue(L, 1);
	lua_pushvalue(L, 2);
	lua_setfield(L, -2, "trace_file");

	/*
	 * Swap __close field in file environment for our wrapper that will
	 * untrace keep the old closef under the key of the PGconn.
	 */
	lua_createtable(L, 0, 3);
	lua_pushcfunction(L, closef_untrace);
	lua_setfield(L, -2, "__close");
	lua_getuservalue(L, 2);
	lua_setfield(L, -2, "old_uservalue");
	lua_pushvalue(L, 1);
	lua_setfield(L, -2, "PGconn");
	lua_setuservalue(L, 2);

	PQtrace(conn, *fp);
#endif
	return 0;
}

static int
conn_untrace(lua_State *L)
{
	PQuntrace(pgsql_conn(L, 1));

	/* Let go of PGconn's reference to file handle. */
	lua_getuservalue(L, 1);
	lua_pushnil(L);
	lua_setfield(L, -2, "trace_file");

	return 0;
}

/*
 * Miscellaneous Functions
 */
static int
conn_consumeInput(lua_State *L)
{
	lua_pushboolean(L, PQconsumeInput(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_isBusy(lua_State *L)
{
	lua_pushboolean(L, PQisBusy(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_setnonblocking(lua_State *L)
{
	lua_pushinteger(L,
	    PQsetnonblocking(pgsql_conn(L, 1), lua_toboolean(L, 2)));
	return 1;
}

static int
conn_isnonblocking(lua_State *L)
{
	lua_pushboolean(L, PQisnonblocking(pgsql_conn(L, 1)));
	return 1;
}

static int
conn_flush(lua_State *L)
{
	lua_pushinteger(L, PQflush(pgsql_conn(L, 1)));
	return 1;
}

/* Notice processing */
static void
noticeReceiver(void *arg, const PGresult *r)
{
	lua_State *L = (lua_State *)arg;
	PGresult **res;

	lua_pushstring(L, "__pgsqlNoticeReceiver");
	lua_rawget(L, LUA_REGISTRYINDEX);
	res = lua_newuserdata(L, sizeof(PGresult *));
	*res = (PGresult *)r;
	luaL_getmetatable(L, RES_METATABLE);
	lua_setmetatable(L, -2);

	if (lua_pcall(L, 1, 0, 0))
		luaL_error(L, "%s", lua_tostring(L, -1));
	*res = NULL;	/* avoid double free */
}

static void
noticeProcessor(void *arg, const char *message)
{
	lua_State *L = (lua_State *)arg;

	lua_pushstring(L, "__pgsqlNoticeProcessor");
	lua_rawget(L, LUA_REGISTRYINDEX);
	lua_pushstring(L, message);
	if (lua_pcall(L, 1, 0, 0))
		luaL_error(L, "%s", lua_tostring(L, -1));
}

static int
conn_setNoticeReceiver(lua_State *L)
{
	lua_pushstring(L, "__pgsqlNoticeReceiver");
	lua_pushvalue(L, -2);
	lua_rawset(L, LUA_REGISTRYINDEX);
	PQsetNoticeReceiver(pgsql_conn(L, 1), noticeReceiver, L);
	return 0;
}

static int
conn_setNoticeProcessor(lua_State *L)
{
	lua_pushstring(L, "__pgsqlNoticeProcessor");
	lua_pushvalue(L, -2);
	lua_rawset(L, LUA_REGISTRYINDEX);
	PQsetNoticeProcessor(pgsql_conn(L, 1), noticeProcessor, L);
	return 0;
}

/* Large objects */
static int
conn_lo_create(lua_State *L)
{
	Oid oid;

	if (lua_gettop(L) == 2)
		oid = luaL_checkinteger(L, 2);
	else
		oid = 0;
	lua_pushinteger(L, lo_create(pgsql_conn(L, 1), oid));
	return 1;
}

static int
conn_lo_import(lua_State *L)
{
	lua_pushinteger(L, lo_import(pgsql_conn(L, 1), luaL_checkstring(L, 2)));
	return 1;
}

static int
conn_lo_import_with_oid(lua_State *L)
{
	lua_pushinteger(L,
	    lo_import_with_oid(pgsql_conn(L, 1), luaL_checkstring(L, 2),
	    luaL_checkinteger(L, 3)));
	return 1;
}

static int
conn_lo_export(lua_State *L)
{
	lua_pushinteger(L,
	    lo_export(pgsql_conn(L, 1), luaL_checkinteger(L, 2),
	    luaL_checkstring(L, 3)));
	return 1;
}

static int
conn_lo_open(lua_State *L)
{
	largeObject **o;

	o = lua_newuserdata(L, sizeof(largeObject *));
	(*o)->conn = pgsql_conn(L, 1);
	(*o)->fd = lo_open((*o)->conn, luaL_checkinteger(L, 2),
	    luaL_checkinteger(L, 3));
	luaL_getmetatable(L, LO_METATABLE);
	lua_setmetatable(L, -2);
	return 1;
}

/*
 * Result set functions
 */
static int
res_status(lua_State *L)
{
	lua_pushinteger(L,
	    PQresultStatus(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE)));
	return 1;
}

static int
res_resStatus(lua_State *L)
{
	lua_pushstring(L, PQresStatus(luaL_checkinteger(L, 2)));
	return 1;
}

static int
res_errorMessage(lua_State *L)
{
	lua_pushstring(L,
	    PQresultErrorMessage(*(PGresult **)luaL_checkudata(L, 1,
	    RES_METATABLE)));
	return 1;
}

static int
res_errorField(lua_State *L)
{
	char *field;

	field = PQresultErrorField(
	    *(PGresult **)luaL_checkudata(L, 1, RES_METATABLE),
	    lua_tointeger(L, 2));
	if (field == NULL)
		lua_pushnil(L);
	else
		lua_pushstring(L, field);
	return 1;
}

static int
res_nfields(lua_State *L)
{
	lua_pushinteger(L,
	    PQnfields(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE)));
	return 1;
}

static int
res_ntuples(lua_State *L)
{
	lua_pushinteger(L,
	    PQntuples(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE)));
	return 1;
}

static int
res_fname(lua_State *L)
{
	lua_pushstring(L,
	    PQfname(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE),
	    luaL_checkinteger(L, 2) - 1));
	return 1;
}

static int
res_fnumber(lua_State *L)
{
	lua_pushinteger(L,
	    PQfnumber(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE),
	    luaL_checkstring(L, 2)) + 1);
	return 1;
}

static int
res_ftable(lua_State *L)
{
	lua_pushinteger(L,
	    PQftable(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE),
	    luaL_checkinteger(L, 2) - 1));
	return 1;
}

static int
res_ftablecol(lua_State *L)
{
	lua_pushinteger(L,
	    PQftablecol(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE),
	    luaL_checkinteger(L, 2) - 1));
	return 1;
}

static int
res_fformat(lua_State *L)
{
	lua_pushinteger(L,
	    PQfformat(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE),
	    luaL_checkinteger(L, 2) - 1));
	return 1;
}

static int
res_ftype(lua_State *L)
{
	lua_pushinteger(L,
	    PQftype(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE),
	    luaL_checkinteger(L, 2) - 1));
	return 1;
}

static int
res_fmod(lua_State *L)
{
	lua_pushinteger(L,
	    PQfmod(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE),
	    luaL_checkinteger(L, 2) - 1));
	return 1;
}

static int
res_fsize(lua_State *L)
{
	lua_pushinteger(L,
	    PQfsize(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE),
	    luaL_checkinteger(L, 2) - 1));
	return 1;
}

static int
res_binaryTuples(lua_State *L)
{
	lua_pushinteger(L,
	    PQbinaryTuples(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE)));
	return 1;
}

static int
res_getvalue(lua_State *L)
{
	lua_pushstring(L,
	    PQgetvalue(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE),
	    luaL_checkinteger(L, 2) - 1, luaL_checkinteger(L, 3) - 1));
	return 1;
}

static int
res_getisnull(lua_State *L)
{
	lua_pushboolean(L,
	    PQgetisnull(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE),
	    luaL_checkinteger(L, 2) - 1, luaL_checkinteger(L, 3) - 1));
	return 1;
}

static int
res_getlength(lua_State *L)
{
	lua_pushinteger(L,
	    PQgetlength(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE),
	    luaL_checkinteger(L, 2) - 1, luaL_checkinteger(L, 3) - 1));
	return 1;
}

static int
res_nparams(lua_State *L)
{
	lua_pushinteger(L,
	    PQnparams(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE)));
	return 1;
}

static int
res_paramtype(lua_State *L)
{
	lua_pushinteger(L,
	    PQparamtype(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE),
	    luaL_checkinteger(L, 2)) - 1);
	return 1;
}

static int
res_cmdStatus(lua_State *L)
{
	lua_pushstring(L,
	    PQcmdStatus(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE)));
	return 1;
}

static int
res_cmdTuples(lua_State *L)
{
	lua_pushstring(L,
	    PQcmdTuples(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE)));
	return 1;
}

static int
res_oidValue(lua_State *L)
{
	lua_pushinteger(L,
	    PQoidValue(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE)));
	return 1;
}

static int
res_oidStatus(lua_State *L)
{
	lua_pushstring(L,
	    PQoidStatus(*(PGresult **)luaL_checkudata(L, 1, RES_METATABLE)));
	return 1;
}

static int
res_clear(lua_State *L)
{
	PGresult **r;

	r = luaL_checkudata(L, 1, RES_METATABLE);
	if (r && *r)  {
		PQclear(*r);
		*r = NULL;
	}
	return 0;
}

/*
 * Notifies methods (objects returned by conn:notifies())
 */
static int
notify_relname(lua_State *L)
{
	PGnotify **n;

	n = luaL_checkudata(L, 1, NOTIFY_METATABLE);
	lua_pushstring(L, (*n)->relname);
	return 1;
}

static int
notify_pid(lua_State *L)
{
	PGnotify **n;

	n = luaL_checkudata(L, 1, NOTIFY_METATABLE);
	lua_pushinteger(L, (*n)->be_pid);
	return 1;
}

static int
notify_extra(lua_State *L)
{
	PGnotify **n;

	n = luaL_checkudata(L, 1, NOTIFY_METATABLE);
	lua_pushstring(L, (*n)->extra);
	return 1;
}

static int
notify_clear(lua_State *L)
{
	PGnotify **n;

	n = luaL_checkudata(L, 1, NOTIFY_METATABLE);
	if (*n)  {
		PQfreemem(*n);
		*n = NULL;
	}
	return 0;
}

/*
 * Large object functions
 */
static int
pgsql_lo_write(lua_State *L)
{
	largeObject **o;
	const char *s;
	size_t len;

	o = luaL_checkudata(L, 1, LO_METATABLE);
	s = lua_tolstring(L, 2, &len);
	lua_pushinteger(L, lo_write((*o)->conn, (*o)->fd, s, len));
	return 1;
}

static int
pgsql_lo_read(lua_State *L)
{
	largeObject **o;
	int res;
	char buf[256];	/* arbitrary size */

	o = luaL_checkudata(L, 1, LO_METATABLE);
	/* XXX don't hard code the buffer size */
	res = lo_read((*o)->conn, (*o)->fd, buf, sizeof buf);
	lua_pushstring(L, buf);
	lua_pushinteger(L, res);
	return 2;
}

static int
pgsql_lo_lseek(lua_State *L)
{
	largeObject **o;

	o = luaL_checkudata(L, 1, LO_METATABLE);
	lua_pushinteger(L, lo_lseek((*o)->conn, (*o)->fd,
	    luaL_checkinteger(L, 2), luaL_checkinteger(L, 3)));
	return 1;
}

static int
pgsql_lo_tell(lua_State *L)
{
	largeObject **o;

	o = luaL_checkudata(L, 1, LO_METATABLE);
	lua_pushinteger(L, lo_tell((*o)->conn, (*o)->fd));
	return 1;
}

static int
pgsql_lo_truncate(lua_State *L)
{
	largeObject **o;

	o = luaL_checkudata(L, 1, LO_METATABLE);
	lua_pushinteger(L, lo_truncate((*o)->conn, (*o)->fd,
	    luaL_checkinteger(L, 2)));
	return 1;
}

static int
pgsql_lo_close(lua_State *L)
{
	largeObject **o;

	o = luaL_checkudata(L, 1, LO_METATABLE);
	lua_pushinteger(L, lo_close((*o)->conn, (*o)->fd));
	*o = NULL;	/* prevent close during garbage collection time */
	return 1;
}

static int
pgsql_lo_clear(lua_State *L)
{
	largeObject **o;

	o = luaL_checkudata(L, 1, LO_METATABLE);
	if (*o)  {
		lo_close((*o)->conn, (*o)->fd);
		*o = NULL;
	}
	return 0;
}

/*
 * Module definitions, constants etc.
 */
struct constant {
	char *name;
	int value;
};

static struct constant pgsql_constant[] = {
	/* Connection status */
	{ "CONNECTION_STARTED",		CONNECTION_STARTED },
	{ "CONNECTION_MADE",		CONNECTION_MADE },
	{ "CONNECTION_AWAITING_RESPONSE", CONNECTION_AWAITING_RESPONSE },
	{ "CONNECTION_AUTH_OK",		CONNECTION_AUTH_OK },
	{ "CONNECTION_OK",		CONNECTION_OK },
	{ "CONNECTION_SSL_STARTUP",	CONNECTION_SSL_STARTUP },
	{ "CONNECTION_SETENV",		CONNECTION_SETENV },
	{ "CONNECTION_BAD",		CONNECTION_BAD },

	/* Resultset status codes */
	{ "PGRES_EMPTY_QUERY",		PGRES_EMPTY_QUERY },
	{ "PGRES_COMMAND_OK",		PGRES_COMMAND_OK },
	{ "PGRES_TUPLES_OK",		PGRES_TUPLES_OK },
#if PG_VERSION_NUM >= 90200
	{ "PGRES_SINGLE_TUPLE",		PGRES_SINGLE_TUPLE },
#endif
	{ "PGRES_COPY_OUT",		PGRES_COPY_OUT },
	{ "PGRES_COPY_IN",		PGRES_COPY_IN },
#if PG_VERSION_NUM >= 90100
	{ "PGRES_COPY_BOTH",		PGRES_COPY_BOTH },
#endif
	{ "PGRES_BAD_RESPONSE",		PGRES_BAD_RESPONSE },
	{ "PGRES_NONFATAL_ERROR",	PGRES_NONFATAL_ERROR },
	{ "PGRES_FATAL_ERROR",		PGRES_FATAL_ERROR },

	/* Polling status  */
	{ "PGRES_POLLING_FAILED",	PGRES_POLLING_FAILED },
	{ "PGRES_POLLING_READING",	PGRES_POLLING_READING },
	{ "PGRES_POLLING_WRITING",	PGRES_POLLING_WRITING },
	{ "PGRES_POLLING_OK",		PGRES_POLLING_OK },

	/* Transaction status */
	{ "PQTRANS_IDLE",		PQTRANS_IDLE },
	{ "PQTRANS_ACTIVE",		PQTRANS_ACTIVE },
	{ "PQTRANS_INTRANS",		PQTRANS_INTRANS },
	{ "PQTRANS_INERROR",		PQTRANS_INERROR },
	{ "PQTRANS_UNKNOWN",		PQTRANS_UNKNOWN },

	/* Diagnostic codes */
	{ "PG_DIAG_SEVERITY",		PG_DIAG_SEVERITY },
	{ "PG_DIAG_SQLSTATE",		PG_DIAG_SQLSTATE },
	{ "PG_DIAG_MESSAGE_PRIMARY",	PG_DIAG_MESSAGE_PRIMARY },
	{ "PG_DIAG_MESSAGE_DETAIL",	PG_DIAG_MESSAGE_DETAIL },
	{ "PG_DIAG_MESSAGE_HINT",	PG_DIAG_MESSAGE_HINT },
	{ "PG_DIAG_STATEMENT_POSITION",	PG_DIAG_STATEMENT_POSITION },
	{ "PG_DIAG_INTERNAL_POSITION",	PG_DIAG_INTERNAL_POSITION },
	{ "PG_DIAG_INTERNAL_QUERY",	PG_DIAG_INTERNAL_QUERY },
	{ "PG_DIAG_CONTEXT",		PG_DIAG_CONTEXT },
	{ "PG_DIAG_SOURCE_FILE",	PG_DIAG_SOURCE_FILE },
	{ "PG_DIAG_SOURCE_LINE",	PG_DIAG_SOURCE_LINE },
	{ "PG_DIAG_SOURCE_FUNCTION",	PG_DIAG_SOURCE_FUNCTION },

	/* Error verbosity */
	{ "PQERRORS_TERSE",		PQERRORS_TERSE },
	{ "PQERRORS_DEFAULT",		PQERRORS_DEFAULT },
	{ "PQERRORS_VERBOSE",		PQERRORS_VERBOSE },

#if PG_VERSION_NUM >= 90100
	/* PQping codes */
	{ "PQPING_OK",			PQPING_OK },
	{ "PQPING_REJECT",		PQPING_REJECT },
	{ "PQPING_NO_RESPONSE",		PQPING_NO_RESPONSE },
	{ "PQPING_NO_ATTEMPT",		PQPING_NO_ATTEMPT },
#endif

	/* Large objects */
	{ "INV_READ",			INV_READ },
	{ "INV_WRITE",			INV_WRITE },
	{ "SEEK_CUR",			SEEK_CUR },
	{ "SEEK_END",			SEEK_END },
	{ "SEEK_SET",			SEEK_SET },

	{ NULL,				0 }
};

static void
pgsql_set_info(lua_State *L)
{
	lua_pushliteral(L, "_COPYRIGHT");
	lua_pushliteral(L, "Copyright (C) 2009 - 2015 by "
	    "micro systems marc balmer");
	lua_settable(L, -3);
	lua_pushliteral(L, "_DESCRIPTION");
	lua_pushliteral(L, "PostgreSQL binding for Lua");
	lua_settable(L, -3);
	lua_pushliteral(L, "_VERSION");
	lua_pushliteral(L, "pgsql 1.4.4");
	lua_settable(L, -3);
}

int
luaopen_pgsql(lua_State *L)
{
	int n;
	struct luaL_Reg luapgsql[] = {
		/* Database Connection Control Functions */
		{ "connectdb", pgsql_connectdb },
		{ "connectStart", pgsql_connectStart },
		{ "libVersion", pgsql_libVersion },
#if PG_VERSION_NUM >= 90100
		{ "ping", pgsql_ping },
#endif
		{ "encryptPassword", pgsql_encryptPassword },
		{ NULL, NULL }
	};

	struct luaL_Reg conn_methods[] = {
		/* Database Connection Control Functions */
		{ "connectPoll", pgsql_connectPoll },
		{ "finish", conn_finish },
		{ "reset", conn_reset },
		{ "resetStart", conn_resetStart },
		{ "resetPoll", conn_resetPoll },

		/* Connection Status Functions */
		{ "db", conn_db },
		{ "user", conn_user },
		{ "pass", conn_pass },
		{ "host", conn_host },
		{ "port", conn_port },
		{ "tty", conn_tty },
		{ "options", conn_options },
		{ "status", conn_status },
		{ "transactionStatus", conn_transactionStatus },
		{ "parameterStatus", conn_parameterStatus },
		{ "protocolVersion", conn_protocolVersion },
		{ "serverVersion", conn_serverVersion },
		{ "errorMessage", conn_errorMessage },
		{ "socket", conn_socket },
		{ "backendPID", conn_backendPID },
		{ "connectionNeedsPassword", conn_connectionNeedsPassword },
		{ "connectionUsedPassword", conn_connectionUsedPassword },

		/* Command Execution Functions */
		{ "escapeString", conn_escapeString },
		{ "escapeLiteral", conn_escapeLiteral },
		{ "escapeIdentifier", conn_escapeIdentifier },
		{ "escapeBytea", conn_escapeBytea },
		{ "unescapeBytea", conn_unescapeBytea },
		{ "exec", conn_exec },
		{ "execParams", conn_execParams },
		{ "prepare", conn_prepare },
		{ "execPrepared", conn_execPrepared },
		{ "describePrepared", conn_describePrepared },
		{ "describePortal", conn_describePortal },

		/* Asynchronous command processing */
		{ "sendQuery", conn_sendQuery },
		{ "sendQueryParams", conn_sendQueryParams },
		{ "sendPrepare", conn_sendPrepare },
		{ "sendQueryPrepared", conn_sendQueryPrepared },
		{ "sendDescribePrepared", conn_sendDescribePrepared },
		{ "sendDescribePortal", conn_sendDescribePortal },
		{ "getResult", conn_getResult },
		{ "cancel", conn_cancel },

#if PG_VERSION_NUM >= 90200
		/* Retrieving query results row-by-row */
		{ "setSingleRowMode", conn_setSingleRowMode },
#endif

		/* Asynchronous Notifications Functions */
		{ "notifies", conn_notifies },

		/* Function associated with the COPY command */
		{ "putCopyData", conn_putCopyData },
		{ "putCopyEnd", conn_putCopyEnd },
		{ "getCopyData", conn_getCopyData },

		/* Control Functions */
		{ "clientEncoding", conn_clientEncoding },
		{ "setClientEncoding", conn_setClientEncoding },
		{ "setErrorVerbosity", conn_setErrorVerbosity },
		{ "trace", conn_trace },
		{ "untrace", conn_untrace },

		/* Miscellaneous Functions */
		{ "consumeInput", conn_consumeInput },
		{ "isBusy", conn_isBusy },
		{ "setnonblocking", conn_setnonblocking },
		{ "isnonblocking", conn_isnonblocking },
		{ "flush", conn_flush },

		/* Notice processing */
		{ "setNoticeReceiver", conn_setNoticeReceiver },
		{ "setNoticeProcessor", conn_setNoticeProcessor },

		/* Large Objects */
		{ "lo_create", conn_lo_create },
		{ "lo_import", conn_lo_import },
		{ "lo_import_with_oid", conn_lo_import_with_oid },
		{ "lo_export", conn_lo_export },
		{ "lo_open", conn_lo_open },
		{ NULL, NULL }
	};
	struct luaL_Reg res_methods[] = {
		/* Main functions */
		{ "status", res_status },
		{ "resStatus", res_resStatus },
		{ "errorMessage", res_errorMessage },
		{ "errorField", res_errorField },

		/* Retrieving query result information */
		{ "ntuples", res_ntuples },
		{ "nfields", res_nfields },
		{ "fname", res_fname },
		{ "fnumber", res_fnumber },
		{ "ftable", res_ftable },
		{ "ftablecol", res_ftablecol },
		{ "fformat", res_fformat },
		{ "ftype", res_ftype },
		{ "fmod", res_fmod },
		{ "fsize", res_fsize },
		{ "binaryTuples", res_binaryTuples },
		{ "getvalue", res_getvalue },
		{ "getisnull", res_getisnull },
		{ "getlength", res_getlength },
		{ "nparams", res_nparams },
		{ "paramtype", res_paramtype },

		/* Other result information */
		{ "cmdStatus", res_cmdStatus },
		{ "cmdTuples", res_cmdTuples },
		{ "oidValue", res_oidValue },
		{ "oidStatus", res_oidStatus },
		{ NULL, NULL }
	};
	struct luaL_Reg notify_methods[] = {
		{ "relname", notify_relname },
		{ "pid", notify_pid },
		{ "extra", notify_extra },
		{ NULL, NULL }
	};
	struct luaL_Reg lo_methods[] = {
		{ "write", pgsql_lo_write },
		{ "read", pgsql_lo_read },
		{ "lseek", pgsql_lo_lseek },
		{ "tell", pgsql_lo_tell },
		{ "truncate", pgsql_lo_truncate },
		{ "close", pgsql_lo_close },
		{ NULL, NULL }
	};

	if (luaL_newmetatable(L, CONN_METATABLE)) {
#if LUA_VERSION_NUM >= 502
		luaL_setfuncs(L, conn_methods, 0);
#else
		luaL_register(L, NULL, conn_methods);
#endif
		lua_pushliteral(L, "__gc");
		lua_pushcfunction(L, conn_finish);
		lua_settable(L, -3);

		lua_pushliteral(L, "__index");
		lua_pushvalue(L, -2);
		lua_settable(L, -3);

		lua_pushliteral(L, "__metatable");
		lua_pushliteral(L, "must not access this metatable");
		lua_settable(L, -3);
	}
	lua_pop(L, 1);

	if (luaL_newmetatable(L, RES_METATABLE)) {
#if LUA_VERSION_NUM >= 502
		luaL_setfuncs(L, res_methods, 0);
#else
		luaL_register(L, NULL, res_methods);
#endif
		lua_pushliteral(L, "__gc");
		lua_pushcfunction(L, res_clear);
		lua_settable(L, -3);

		lua_pushliteral(L, "__index");
		lua_pushvalue(L, -2);
		lua_settable(L, -3);

		lua_pushliteral(L, "__metatable");
		lua_pushliteral(L, "must not access this metatable");
		lua_settable(L, -3);
	}
	lua_pop(L, 1);

	if (luaL_newmetatable(L, NOTIFY_METATABLE)) {
#if LUA_VERSION_NUM >= 502
		luaL_setfuncs(L, notify_methods, 0);
#else
		luaL_register(L, NULL, notify_methods);
#endif
		lua_pushliteral(L, "__gc");
		lua_pushcfunction(L, notify_clear);
		lua_settable(L, -3);

		lua_pushliteral(L, "__index");
		lua_pushvalue(L, -2);
		lua_settable(L, -3);

		lua_pushliteral(L, "__metatable");
		lua_pushliteral(L, "must not access this metatable");
		lua_settable(L, -3);
	}
	lua_pop(L, 1);

	if (luaL_newmetatable(L, LO_METATABLE)) {
#if LUA_VERSION_NUM >= 502
		luaL_setfuncs(L, lo_methods, 0);
#else
		luaL_register(L, NULL, lo_methods);
#endif
		lua_pushliteral(L, "__gc");
		lua_pushcfunction(L, pgsql_lo_clear);
		lua_settable(L, -3);

		lua_pushliteral(L, "__index");
		lua_pushvalue(L, -2);
		lua_settable(L, -3);

		lua_pushliteral(L, "__metatable");
		lua_pushliteral(L, "must not access this metatable");
		lua_settable(L, -3);
	}
	lua_pop(L, 1);
#if LUA_VERSION_NUM >= 502
	luaL_newlib(L, luapgsql);
#else
	luaL_register(L, "pgsql", luapgsql);
#endif
	pgsql_set_info(L);
	for (n = 0; pgsql_constant[n].name != NULL; n++) {
		lua_pushinteger(L, pgsql_constant[n].value);
		lua_setfield(L, -2, pgsql_constant[n].name);
	};

	return 1;
}
