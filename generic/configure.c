/*
 * $Eid: mysqltcl.c,v 1.2 2002/02/15 18:52:08 artur Exp $
 *
 * MYSQL interface to Tcl
 *
 * Hakan Soderstrom, hs@soderstrom.se
 *
 */

/*
 * Copyright (c) 1994, 1995 Hakan Soderstrom and Tom Poindexter
 *
 * Permission to use, copy, modify, distribute, and sell this software
 * and its documentation for any purpose is hereby granted without fee,
 * provided that the above copyright notice and this permission notice
 * appear in all copies of the software and related documentation.
 *
 * THE SOFTWARE IS PROVIDED "AS-IS" AND WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS, IMPLIED OR OTHERWISE, INCLUDING WITHOUT LIMITATION, ANY
 * WARRANTY OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE.
 *
 * IN NO EVENT SHALL HAKAN SODERSTROM OR SODERSTROM PROGRAMVARUVERKSTAD
 * AB BE LIABLE FOR ANY SPECIAL, INCIDENTAL, INDIRECT OR CONSEQUENTIAL
 * DAMAGES OF ANY KIND, OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS
 * OF USE, DATA OR PROFITS, WHETHER OR NOT ADVISED OF THE POSSIBILITY
 * OF DAMAGE, AND ON ANY THEORY OF LIABILITY, ARISING OUT OF OR IN
 * CONNECTON WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

/*
 Modified after version 2.0 by Artur Trzewik
 see http://www.xdobry.de/mysqltcl
 Patch for encoding option by Alexander Schoepe (version2.20)
 */

#include "mysqltcl.h"

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Connect
 * Implements the mysqlconnect command:
 * usage: mysqlconnect ?option value ...?
 *
 * Results:
 *      handle - a character string of newly open handle
 *      TCL_OK - connect successful
 *      TCL_ERROR - connect not successful - error message returned
 */

static CONST char* MysqlConnectOpt[] = {
		"-host", "-user", "-password", "-db", "-port", "-socket", "-encoding", "-ssl",
		"-compress", "-noschema", "-odbc","-multistatement", "-multiresult",
		"-localfiles", "-ignorespace", "-foundrows", "-interactive",
		"-sslkey", "-sslcert", "-sslca", "-sslcapath",
		"-sslciphers", "-reconnect", "-read-timeout", "-write-timeout",
		"-connect-timeout", "-protocol", "-init-command",
		"-ssl-verify-cert", "-secure-auth", NULL };

int Mysqltcl_Connect(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	MysqltclState *statePtr = (MysqltclState *) clientData;
	int i, idx;
	char *hostname = NULL;
	char *user = NULL;
	char *password = NULL;
	char *db = NULL;
	int port = 0, flags = 0, booleanflag;
	char *socket = NULL;
	char *encodingname = NULL;
	int isSSL = 0;
	char *sslkey = NULL;
	char *sslcert = NULL;
	char *sslca = NULL;
	char *sslcapath = NULL;
	char *sslcipher = NULL;
	int intvalue;

	MysqlTclHandle *handle;
	const char *groupname = "mysqltcl";

	enum connectoption {
		MYSQL_CONNHOST_OPT,
		MYSQL_CONNUSER_OPT,
		MYSQL_CONNPASSWORD_OPT,
		MYSQL_CONNDB_OPT,
		MYSQL_CONNPORT_OPT,
		MYSQL_CONNSOCKET_OPT,
		MYSQL_CONNENCODING_OPT,
		MYSQL_CONNSSL_OPT,
		MYSQL_CONNCOMPRESS_OPT,
		MYSQL_CONNNOSCHEMA_OPT,
		MYSQL_CONNODBC_OPT,
		MYSQL_MULTISTATEMENT_OPT,
		MYSQL_MULTIRESULT_OPT,
		MYSQL_LOCALFILES_OPT,
		MYSQL_IGNORESPACE_OPT,
		MYSQL_FOUNDROWS_OPT,
		MYSQL_INTERACTIVE_OPT,
		MYSQL_SSLKEY_OPT,
		MYSQL_SSLCERT_OPT,
		MYSQL_SSLCA_OPT,
		MYSQL_SSLCAPATH_OPT,
		MYSQL_SSLCIPHERS_OPT,
		MYSQL_RECONNECT_OPT,
		MYSQL_READ_TIMEOUT_OPT,
		MYSQL_WRITE_TIMEOUT_OPT,
		MYSQL_CONNECT_TIMEOUT_OPT,
		MYSQL_PROTOCOL_OPT,
		MYSQL_INIT_COMMAND_OPT,
		MYSQL_SSL_VERIFY_CERT_OPT,
		MYSQL_SECURE_AUTH
	};

	if (!(objc & 1) || objc > (sizeof(MysqlConnectOpt) / sizeof(MysqlConnectOpt[0] - 1) * 2 + 1)) {
		Tcl_WrongNumArgs(
				interp,
				1,
				objv,
				"[-user xxx] [-db mysql] [-port 3306] [-host localhost] [-socket sock] [-password pass] [-encoding encoding] [-ssl boolean] [-compress boolean] [-odbc boolean] [-noschema boolean] [-reconnect boolean]");
		return TCL_ERROR;
	}

	handle = createMysqlHandle(statePtr);
	if (handle == 0) {
		panic("no memory for handle");
		return TCL_ERROR;
	}
	handle->connection = mysql_init(NULL);

	for (i = 1; i < objc; i++) {
		if (Tcl_GetIndexFromObj(interp, objv[i], MysqlConnectOpt, "option", 0, &idx) != TCL_OK)
			return TCL_ERROR;

		switch (idx) {
		case MYSQL_CONNHOST_OPT:
			hostname = Tcl_GetStringFromObj(objv[++i], NULL);
			break;
		case MYSQL_CONNUSER_OPT:
			user = Tcl_GetStringFromObj(objv[++i], NULL);
			break;
		case MYSQL_CONNPASSWORD_OPT:
			password = Tcl_GetStringFromObj(objv[++i], NULL);
			break;
		case MYSQL_CONNDB_OPT:
			db = Tcl_GetStringFromObj(objv[++i], NULL);
			break;
		case MYSQL_CONNPORT_OPT:
			if (Tcl_GetIntFromObj(interp, objv[++i], &port) != TCL_OK)
				return TCL_ERROR;
			break;
		case MYSQL_CONNSOCKET_OPT:
			socket = Tcl_GetStringFromObj(objv[++i], NULL);
			break;
		case MYSQL_CONNENCODING_OPT:
			encodingname = Tcl_GetStringFromObj(objv[++i], NULL);
			break;
		case MYSQL_CONNSSL_OPT:
			if (Tcl_GetBooleanFromObj(interp, objv[++i], &isSSL) != TCL_OK)
				return TCL_ERROR;
			break;
		case MYSQL_CONNCOMPRESS_OPT:
			if (Tcl_GetBooleanFromObj(interp, objv[++i], &booleanflag) != TCL_OK)
				return TCL_ERROR;
			if (booleanflag)
				flags |= CLIENT_COMPRESS;
			break;
		case MYSQL_CONNNOSCHEMA_OPT:
			if (Tcl_GetBooleanFromObj(interp, objv[++i], &booleanflag) != TCL_OK)
				return TCL_ERROR;
			if (booleanflag)
				flags |= CLIENT_NO_SCHEMA;
			break;
		case MYSQL_CONNODBC_OPT:
			if (Tcl_GetBooleanFromObj(interp, objv[++i], &booleanflag) != TCL_OK)
				return TCL_ERROR;
			if (booleanflag)
				flags |= CLIENT_ODBC;
			break;
		case MYSQL_MULTISTATEMENT_OPT:
			if (Tcl_GetBooleanFromObj(interp, objv[++i], &booleanflag) != TCL_OK)
				return TCL_ERROR;
			if (booleanflag)
				flags |= CLIENT_MULTI_STATEMENTS;
			break;
		case MYSQL_MULTIRESULT_OPT:
			if (Tcl_GetBooleanFromObj(interp, objv[++i], &booleanflag) != TCL_OK)
				return TCL_ERROR;
			if (booleanflag)
				flags |= CLIENT_MULTI_RESULTS;
			break;
		case MYSQL_LOCALFILES_OPT:
			if (Tcl_GetBooleanFromObj(interp, objv[++i], &booleanflag) != TCL_OK)
				return TCL_ERROR;
			if (booleanflag)
				flags |= CLIENT_LOCAL_FILES;
			break;
		case MYSQL_IGNORESPACE_OPT:
			if (Tcl_GetBooleanFromObj(interp, objv[++i], &booleanflag) != TCL_OK)
				return TCL_ERROR;
			if (booleanflag)
				flags |= CLIENT_IGNORE_SPACE;
			break;
		case MYSQL_FOUNDROWS_OPT:
			if (Tcl_GetBooleanFromObj(interp, objv[++i], &booleanflag) != TCL_OK)
				return TCL_ERROR;
			if (booleanflag)
				flags |= CLIENT_FOUND_ROWS;
			break;
		case MYSQL_INTERACTIVE_OPT:
			if (Tcl_GetBooleanFromObj(interp, objv[++i], &booleanflag) != TCL_OK)
				return TCL_ERROR;
			if (booleanflag)
				flags |= CLIENT_INTERACTIVE;
			break;
		case MYSQL_SSLKEY_OPT:
			sslkey = Tcl_GetStringFromObj(objv[++i], NULL);
			break;
		case MYSQL_SSLCERT_OPT:
			sslcert = Tcl_GetStringFromObj(objv[++i], NULL);
			break;
		case MYSQL_SSLCA_OPT:
			sslca = Tcl_GetStringFromObj(objv[++i], NULL);
			break;
		case MYSQL_SSLCAPATH_OPT:
			sslcapath = Tcl_GetStringFromObj(objv[++i], NULL);
			break;
		case MYSQL_SSLCIPHERS_OPT:
			sslcipher = Tcl_GetStringFromObj(objv[++i], NULL);
			break;
		case MYSQL_RECONNECT_OPT:
			if (Tcl_GetBooleanFromObj(interp, objv[++i], &booleanflag) != TCL_OK)
				return TCL_ERROR;
			mysql_options(handle->connection, MYSQL_OPT_RECONNECT,  (char *)&booleanflag);
			break;
		case MYSQL_READ_TIMEOUT_OPT:
			if (Tcl_GetIntFromObj(interp, objv[++i], &intvalue) != TCL_OK)
				return TCL_ERROR;
			mysql_options(handle->connection, MYSQL_OPT_READ_TIMEOUT, (char *)&intvalue);
			break;
		case MYSQL_WRITE_TIMEOUT_OPT:
			if (Tcl_GetIntFromObj(interp, objv[++i], &intvalue) != TCL_OK)
				return TCL_ERROR;
			mysql_options(handle->connection, MYSQL_OPT_WRITE_TIMEOUT, (char *)&intvalue);
			break;
		case MYSQL_CONNECT_TIMEOUT_OPT:
			if (Tcl_GetIntFromObj(interp, objv[++i], &intvalue) != TCL_OK)
				return TCL_ERROR;
			mysql_options(handle->connection, MYSQL_OPT_CONNECT_TIMEOUT, (char *)&intvalue);
			break;
		case MYSQL_PROTOCOL_OPT:
			if (Tcl_GetIntFromObj(interp, objv[++i], &intvalue) != TCL_OK)
				return TCL_ERROR;
			mysql_options(handle->connection, MYSQL_OPT_PROTOCOL, (char *)&intvalue);
			break;
		case MYSQL_INIT_COMMAND_OPT:
			mysql_options(handle->connection, MYSQL_INIT_COMMAND, Tcl_GetStringFromObj(objv[++i], NULL));
			break;
		case MYSQL_SSL_VERIFY_CERT_OPT:
			if (Tcl_GetBooleanFromObj(interp, objv[++i], &booleanflag) != TCL_OK)
				return TCL_ERROR;
			mysql_options(handle->connection, MYSQL_OPT_SSL_VERIFY_SERVER_CERT, (char *)&booleanflag);
			break;
		case MYSQL_SECURE_AUTH:
			if (Tcl_GetBooleanFromObj(interp, objv[++i], &booleanflag) != TCL_OK)
				return TCL_ERROR;
			mysql_options(handle->connection, MYSQL_SECURE_AUTH, (char *)&booleanflag);
			break;
		default:
			return mysql_prim_confl(interp, objc, objv, "Weirdness in options");
		}
	}



	/* the function below caused in version pre 3.23.50 segmentation fault */
	mysql_options(handle->connection, MYSQL_READ_DEFAULT_GROUP, groupname);

	if (isSSL) {
		mysql_ssl_set(handle->connection, sslkey, sslcert, sslca, sslcapath, sslcipher);
	}

	if (!mysql_real_connect(handle->connection, hostname, user, password, db, port, socket, flags)) {
		mysql_server_confl(interp, objc, objv, handle->connection);
		closeHandle(handle);
		return TCL_ERROR;
	}

	if (db) {
		strncpy(handle->database, db, MYSQL_NAME_LEN);
		handle->database[MYSQL_NAME_LEN - 1] = '\0';
	}

	if (encodingname == NULL || (encodingname != NULL && strcmp(encodingname, "binary") != 0)) {
		if (encodingname == NULL)
			encodingname = (char *) Tcl_GetEncodingName(NULL);
		handle->encoding = Tcl_GetEncoding(interp, encodingname);
		if (handle->encoding == NULL)
			return TCL_ERROR;
	}

	Tcl_SetObjResult(interp, Tcl_NewHandleObj(statePtr, handle));

	return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_BaseInfo
 * Implements the mysqlinfo command:
 * usage: mysqlbaseinfo option
 *
 */

int Mysqltcl_BaseInfo(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	int idx;
	Tcl_Obj *resList;
	char **option;
	static CONST char* MysqlInfoOpt[] = { "connectparameters", "clientversion",
#if (MYSQL_VERSION_ID >= 40107)
			"clientversionid",
#endif
			NULL };
	enum baseoption {
		MYSQL_BINFO_CONNECT, MYSQL_BINFO_CLIENTVERSION, MYSQL_BINFO_CLIENTVERSIONID
	};

	if (objc < 2) {
		Tcl_WrongNumArgs(interp, 1, objv, "connectparameters | clientversion");

		return TCL_ERROR;
	}
	if (Tcl_GetIndexFromObj(interp, objv[1], MysqlInfoOpt, "option", TCL_EXACT, &idx) != TCL_OK)
		return TCL_ERROR;

	/* First check the handle. Checking depends on the option. */
	switch (idx) {
	case MYSQL_BINFO_CONNECT:
		option = (char **) MysqlConnectOpt;
		resList = Tcl_NewListObj(0, NULL);

		while (*option != NULL) {
			Tcl_ListObjAppendElement(interp, resList, Tcl_NewStringObj(*option, -1));
			option++;
		}
		Tcl_SetObjResult(interp, resList);
		break;
	case MYSQL_BINFO_CLIENTVERSION:
		Tcl_SetObjResult(interp, Tcl_NewStringObj(mysql_get_client_info(), -1));
		break;
#if (MYSQL_VERSION_ID >= 40107)
	case MYSQL_BINFO_CLIENTVERSIONID:
		Tcl_SetObjResult(interp, Tcl_NewIntObj(mysql_get_client_version()));
		break;
#endif
	}
	return TCL_OK;
}

/*
 * Mysqltcl_CloseAll
 * Close all connections.
 */

static void Mysqltcl_CloseAll(ClientData clientData) {
	MysqltclState *statePtr = (MysqltclState *) clientData;
	Tcl_HashSearch search;
	MysqlTclHandle *handle;
	Tcl_HashEntry *entryPtr;
	int wasdeleted = 0;

	for (entryPtr = Tcl_FirstHashEntry(&statePtr->hash, &search); entryPtr != NULL; entryPtr = Tcl_NextHashEntry(
			&search)) {
		wasdeleted = 1;
		handle = (MysqlTclHandle *) Tcl_GetHashValue(entryPtr);

		if (handle->connection == 0)
			continue;
		closeHandle(handle);
	}
	if (wasdeleted) {
		Tcl_DeleteHashTable(&statePtr->hash);
		Tcl_InitHashTable(&statePtr->hash, TCL_STRING_KEYS);
	}
}

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Close --
 *    Implements the mysqlclose command:
 *    usage: mysqlclose ?handle?
 *
 *    results:
 *	null string
 */

int Mysqltcl_Close(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	MysqltclState *statePtr = (MysqltclState *) clientData;
	MysqlTclHandle *handle, *thandle;
	Tcl_HashEntry *entryPtr;
	Tcl_HashEntry *qentries[16];
	Tcl_HashSearch search;

	int i, qfound = 0;

	/* If handle omitted, close all connections. */
	if (objc == 1) {
		Mysqltcl_CloseAll(clientData);
		return TCL_OK;
	}

	if ((handle = mysql_prologue(interp, objc, objv, 2, 2, CL_CONN, "?handle?")) == 0)
		return TCL_ERROR;

	/* Search all queries and statements on this handle and close those */
	if (handle->type == HT_CONNECTION) {
		while (1) {
			for (entryPtr = Tcl_FirstHashEntry(&statePtr->hash, &search); entryPtr != NULL; entryPtr
					= Tcl_NextHashEntry(&search)) {

				thandle = (MysqlTclHandle *) Tcl_GetHashValue(entryPtr);
				if (thandle->connection == handle->connection && thandle->type != HT_CONNECTION) {
					qentries[qfound++] = entryPtr;
				}
				if (qfound == 16)
					break;
			}
			if (qfound > 0) {
				for (i = 0; i < qfound; i++) {
					entryPtr = qentries[i];
					thandle = (MysqlTclHandle *) Tcl_GetHashValue(entryPtr);
					Tcl_DeleteHashEntry(entryPtr);
					closeHandle(thandle);
				}
			}
			if (qfound != 16)
				break;
			qfound = 0;
		}
	}
	entryPtr = Tcl_FindHashEntry(&statePtr->hash,Tcl_GetStringFromObj(objv[1],NULL));
	if (entryPtr)
		Tcl_DeleteHashEntry(entryPtr);
	closeHandle(handle);
	return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Encoding
 *    usage: mysql::encoding handle ?encoding|binary?
 *
 */
int Mysqltcl_Encoding(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	MysqltclState *statePtr = (MysqltclState *) clientData;
	Tcl_HashSearch search;
	Tcl_HashEntry *entryPtr;
	MysqlTclHandle *handle, *qhandle;
	char *encodingname;
	Tcl_Encoding encoding;

	if ((handle = mysql_prologue(interp, objc, objv, 2, 3, CL_CONN, "handle")) == 0)
		return TCL_ERROR;
	if (objc == 2) {
		if (handle->encoding == NULL)
			Tcl_SetObjResult(interp, Tcl_NewStringObj("binary", -1));
		else
			Tcl_SetObjResult(interp, Tcl_NewStringObj(Tcl_GetEncodingName(handle->encoding), -1));
	} else {
		if (handle->type != HT_CONNECTION) {
			Tcl_SetObjResult(interp, Tcl_NewStringObj("encoding set can be used only on connection handle", -1));
			return TCL_ERROR;
		}
		encodingname = Tcl_GetStringFromObj(objv[2], NULL);
		if (strcmp(encodingname, "binary") == 0) {
			encoding = NULL;
		} else {
			encoding = Tcl_GetEncoding(interp, encodingname);
			if (encoding == NULL)
				return TCL_ERROR;
		}
		if (handle->encoding != NULL)
			Tcl_FreeEncoding(handle->encoding);
		handle->encoding = encoding;

		/* change encoding of all subqueries */
		for (entryPtr = Tcl_FirstHashEntry(&statePtr->hash, &search); entryPtr != NULL; entryPtr = Tcl_NextHashEntry(
				&search)) {
			qhandle = (MysqlTclHandle *) Tcl_GetHashValue(entryPtr);
			if (qhandle->type == HT_QUERY && handle->connection == qhandle->connection) {
				qhandle->encoding = encoding;
			}
		}

	}
	return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_ChangeUser
 *    usage: mysqlchangeuser handle user password database
 *    return TCL_ERROR if operation failed
 */

int Mysqltcl_ChangeUser(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	MysqlTclHandle *handle;
	int len;
	char *user, *password, *database = NULL;

	if ((handle = mysql_prologue(interp, objc, objv, 4, 5, CL_CONN, "handle user password ?database?")) == 0)
		return TCL_ERROR;

	user = Tcl_GetStringFromObj(objv[2], NULL);
	password = Tcl_GetStringFromObj(objv[3], NULL);
	if (objc == 5) {
		database = Tcl_GetStringFromObj(objv[4], &len);
		if (len >= MYSQL_NAME_LEN) {
			mysql_prim_confl(interp, objc, objv, "database name too long");
			return TCL_ERROR;
		}
	}
	if (mysql_change_user(handle->connection, user, password, database) != 0) {
		mysql_server_confl(interp, objc, objv, handle->connection);
		return TCL_ERROR;
	}
	if (database != NULL)
		strcpy(handle->database, database);
	return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_AutoCommit
 *    usage: mysql::autocommit bool
 *    set autocommit mode
 */
int Mysqltcl_AutoCommit(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	MysqlTclHandle *handle;
	int isAutocommit = 0;

	if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_CONN, "handle bool")) == 0)
		return TCL_ERROR;
	if (Tcl_GetBooleanFromObj(interp, objv[2], &isAutocommit) != TCL_OK)
		return TCL_ERROR;
	if (mysql_autocommit(handle->connection, isAutocommit) != 0) {
		mysql_server_confl(interp, objc, objv, handle->connection);
	}
	return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Use
 *    Implements the mysqluse command:

 *    usage: mysqluse handle dbname
 *
 *    results:
 *	Sets current database to dbname.
 */

int Mysqltcl_Use(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	int len;
	char *db;
	MysqlTclHandle *handle;

	if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_CONN, "handle dbname")) == 0)
		return TCL_ERROR;

	db = Tcl_GetStringFromObj(objv[2], &len);
	if (len >= MYSQL_NAME_LEN) {
		mysql_prim_confl(interp, objc, objv, "database name too long");
		return TCL_ERROR;
	}

	if (mysql_select_db(handle->connection, db) != 0) {
		return mysql_server_confl(interp, objc, objv, handle->connection);
	}
	strcpy(handle->database, db);
	return TCL_OK;
}

