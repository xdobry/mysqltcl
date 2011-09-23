/*
 *
 * MYSQL interface to Tcl
 *
 * created by: Artur Trzewik mail@xdobry.de
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

#ifdef PREPARED_STATEMENT

static void* MysqlBindAllocBuffer(MYSQL_BIND* b, /* Pointer to a binding array */
int i, /* Index into the array */
unsigned long len /* Length of the buffer to allocate or 0 */
) {
	void* block = NULL;
	if (len != 0) {
		block = Tcl_Alloc(len);
	}
	b[i].buffer = block;
	b[i].buffer_length = len;
	return block;
}

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Prepare --
 *    Implements the mysql::prepare command:
 *    usage: mysql::prepare handle statements
 *
 *    results:
 *	    prepared statment handle
 */

int Mysqltcl_Prepare(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	MysqltclState *statePtr = (MysqltclState *) clientData;

	MysqlTclHandle *handle;
	MysqlTclHandle *shandle;
	MYSQL_STMT *statement;
	MYSQL_FIELD* fields = NULL;
	char *query;
	int queryLen;
	int resultColumns;
	int paramCount;
	int i;

	if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_CONN, "handle sql-statement")) == 0)
		return TCL_ERROR;

	statement = mysql_stmt_init(handle->connection);
	if (statement == NULL) {
		printf("prepare stmt3\n");
		mysql_prim_confl(interp, objc, objv, "can not stmt init");
		return TCL_ERROR;
	}
	// TODO encoding

	query = (char *) Tcl_GetByteArrayFromObj(objv[2], &queryLen);
	if (mysql_stmt_prepare(statement, query, queryLen)) {
		Tcl_SetStringObj(Tcl_GetObjResult(interp), mysql_stmt_error(statement), -1);
		mysql_stmt_close(statement);
		return TCL_ERROR;
	}

	if ((shandle = createHandleFrom(statePtr, handle, HT_STATEMENT)) == NULL) {
		mysql_prim_confl(interp, objc, objv, "can not create handle");
		return TCL_ERROR;
	}
	printf("prepare stmt6\n");

	shandle->statement = statement;
	shandle->resultMetadata = mysql_stmt_result_metadata(statement);
	shandle->paramMetadata = mysql_stmt_param_metadata(statement);
	/* set result bind memory */
	resultColumns = mysql_stmt_field_count(statement);
	printf("stmt fields count %d\n", resultColumns);
	if (resultColumns > 0) {
		fields = mysql_fetch_fields(shandle->resultMetadata);
		shandle->bindResult = (MYSQL_BIND *) Tcl_Alloc(sizeof(MYSQL_BIND) * resultColumns);
		memset(shandle->bindResult, 0, sizeof(MYSQL_BIND) * resultColumns);
		shandle->needColumnFetch = (int * )Tcl_Alloc(sizeof(int) * resultColumns);
		shandle->resultLengths = (unsigned long*) Tcl_Alloc(sizeof(unsigned long) * resultColumns);
		for (i = 0; i < resultColumns; i++) {
			shandle->needColumnFetch[i]=0;
			switch (fields[i].type) {
			case MYSQL_TYPE_FLOAT:
			case MYSQL_TYPE_DOUBLE:
				shandle->bindResult[i].buffer_type = MYSQL_TYPE_DOUBLE;
				MysqlBindAllocBuffer(shandle->bindResult, i, sizeof(double));
				shandle->resultLengths[i] = sizeof(double);
				break;
			case MYSQL_TYPE_BIT:
				shandle->bindResult[i].buffer_type = MYSQL_TYPE_BIT;
				MysqlBindAllocBuffer(shandle->bindResult, i, fields[i].length);
				shandle->resultLengths[i] = fields[i].length;
				break;
			case MYSQL_TYPE_LONGLONG:
				shandle->bindResult[i].buffer_type = MYSQL_TYPE_LONGLONG;
				MysqlBindAllocBuffer(shandle->bindResult, i, sizeof(Tcl_WideInt));
				shandle->resultLengths[i] = sizeof(Tcl_WideInt);
				break;
			case MYSQL_TYPE_TINY:
			case MYSQL_TYPE_SHORT:
			case MYSQL_TYPE_INT24:
			case MYSQL_TYPE_LONG:
				shandle->bindResult[i].buffer_type = MYSQL_TYPE_LONG;
				MysqlBindAllocBuffer(shandle->bindResult, i, sizeof(int));
				shandle->resultLengths[i] = sizeof(int);
				break;
			default:
				shandle->bindResult[i].buffer_type = MYSQL_TYPE_STRING;
				if (fields[i].length<2048) {
					MysqlBindAllocBuffer(shandle->bindResult, i, fields[i].length+1);
					shandle->resultLengths[i] = fields[i].length+1;
 				} else {
 					MysqlBindAllocBuffer(shandle->bindResult, i, 0);
 					shandle->resultLengths[i] = 0;
 				}
				break;
			}
			printf("result param %d len %ld fieldlen %ld maxlen %ld type %d\n", i,shandle->resultLengths[i],fields[i].length,fields[i].max_length,fields[i].type);
			shandle->bindResult[i].length = shandle->resultLengths + i;
		}
	}
	paramCount = mysql_stmt_param_count(statement);
	printf("stmt param count %d\n", paramCount);
	if (paramCount > 0) {
		shandle->bindParam = (MYSQL_BIND *) Tcl_Alloc(sizeof(MYSQL_BIND) * paramCount);
		memset(shandle->bindParam, 0, sizeof(MYSQL_BIND) * paramCount);
		for (i = 0; i < paramCount; i++) {
			shandle->bindParam[i].buffer_type = MYSQL_TYPE_VAR_STRING;
			shandle->bindParam[i].buffer = NULL;
			shandle->bindParam[i].buffer_length = 0;
		}
	}
	printf("prepare stmt finished\n");
	Tcl_SetObjResult(interp, Tcl_NewHandleObj(statePtr, shandle));
	return TCL_OK;
}

static int Mysqltcl_ParamMetaData(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	MysqltclState *statePtr = (MysqltclState *) clientData;
	MysqlTclHandle *handle;
	MYSQL_RES *res;
	MYSQL_ROW row;
	Tcl_Obj *colinfo, *resObj;
	unsigned long *lengths;
	int i;
	int colCount;
	MYSQL_FIELD* fld;

	if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_CONN, "statement-handle")) == 0)
		return TCL_ERROR;
	if (handle->type != HT_STATEMENT)
		return TCL_ERROR;

	resObj = Tcl_GetObjResult(interp);
	printf("statement %p count %ld\n", handle->statement, mysql_stmt_param_count(handle->statement));
	res = mysql_stmt_result_metadata(handle->statement);
	printf("res %p\n", res);
	if (res == NULL)
		return TCL_ERROR;

	mysql_field_seek(res, 0);
	while ((fld = mysql_fetch_field(res)) != NULL) {
		if ((colinfo = mysql_colinfo(interp, objc, objv, fld, objv[2])) != NULL) {
			Tcl_ListObjAppendElement(interp, resObj, colinfo);
		} else {
			goto conflict;
		}
	}
	conflict:

	mysql_free_result(res);
	return TCL_OK;
}
/*----------------------------------------------------------------------
 *
 * Mysqltcl_PSelect --
 *    Implements the mysql::pselect command:
 *    usage: mysql::pselect $statement_handle ?arguments...?
 *
 *    results:
 *	    number of returned rows
 */

static int Mysqltcl_PSelect(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	MysqltclState *statePtr = (MysqltclState *) clientData;
	MysqlTclHandle *handle;

	if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_CONN, "handle sql-statement")) == 0)
		return TCL_ERROR;
	if (handle->type != HT_STATEMENT) {
		return TCL_ERROR;
	}
	mysql_stmt_reset(handle->statement);
	if (mysql_stmt_execute(handle->statement)) {
		return mysql_server_confl(interp, objc, objv, handle->connection);
	}
	mysql_stmt_bind_result(handle->statement, handle->bindResult);
	mysql_stmt_store_result(handle->statement);
	return TCL_OK;
}
/*----------------------------------------------------------------------
 *
 * Mysqltcl_PFetch --
 *    Implements the mysql::pfetch command:
 *    usage: mysql::pfetch $statement_handle
 *
 *    results:
 *	    number of returned rows
 */

static int Mysqltcl_PFetch(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	MysqltclState *statePtr = (MysqltclState *) clientData;
	MysqlTclHandle *handle;

	if ((handle = mysql_prologue(interp, objc, objv, 2, 2, CL_CONN, "prep-stat-handle")) == 0)
		return TCL_ERROR;
	if (handle->type != HT_STATEMENT) {
		return TCL_ERROR;
	}

	return TCL_OK;
}

/*----------------------------------------------------------------------
 *
 * Mysqltcl_PExecute --
 *    Implements the mysql::pexecute command:
 *    usage: mysql::pexecute statement-handle ?arguments...?
 *
 *    results:
 *	    number of effected rows
 */

int Mysqltcl_PExecute(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	MysqlTclHandle *handle;
	int paramCount;
	int i;
	int len;
	int affected_rows, resultColumns;
	char *paramValStr;
	Tcl_Obj * paramValObj;

	if ((handle = mysql_prologue(interp, objc, objv, 2, -1, CL_CONN, "handle param1 param2")) == 0)
		return TCL_ERROR;
	if (handle->type != HT_STATEMENT) {
		return TCL_ERROR;
	}

	if (mysql_stmt_reset(handle->statement)) {
		Tcl_SetStringObj(Tcl_GetObjResult(interp), "can not reset stmt", -1);
		return TCL_ERROR;
	}

	paramCount = mysql_stmt_param_count(handle->statement);

	if (paramCount != objc - 2) {
		// TODO exactly message
		Tcl_SetStringObj(Tcl_GetObjResult(interp), "wrong parameter count", -1);
		return TCL_ERROR;
	}

	for (i = 0; i < paramCount; i++) {
		paramValObj = objv[i + 2];
		/*
		 * At this point, paramValObj contains the parameter to bind.
		 * Convert the parameters to the appropriate data types for
		 * MySQL's prepared statement interface, and bind them.
		 */

		switch (handle->bindParam[i].buffer_type) {
		default:
			// TODO types and encoding
			paramValStr = Tcl_GetStringFromObj(paramValObj, &len);
			handle->bindParam[i].buffer_length = len;
			handle->bindParam[i].buffer = paramValStr;
			break;
		}
	}
	if (paramCount > 0) {
		if (mysql_stmt_bind_param(handle->statement, handle->bindParam)) {
			Tcl_SetStringObj(Tcl_GetObjResult(interp), mysql_stmt_error(handle->statement), -1);
			return TCL_ERROR;
		}
	}

	if (mysql_stmt_execute(handle->statement)) {
		Tcl_SetStringObj(Tcl_GetObjResult(interp), mysql_stmt_error(handle->statement), -1);
		return TCL_ERROR;
	}

	resultColumns = mysql_stmt_field_count(handle->statement);
	if (resultColumns == 0) {
		affected_rows = mysql_stmt_affected_rows(handle->statement);
		Tcl_SetIntObj(Tcl_GetObjResult(interp), affected_rows);
		return TCL_OK;
	}
	if (mysql_stmt_bind_result(handle->statement, handle->bindResult)) {
		Tcl_SetStringObj(Tcl_GetObjResult(interp), mysql_stmt_error(handle->statement), -1);
		return TCL_ERROR;
	}
	if (mysql_stmt_store_result(handle->statement)) {
		Tcl_SetStringObj(Tcl_GetObjResult(interp), mysql_stmt_error(handle->statement), -1);
		return TCL_ERROR;
	}

	return TCL_OK;
}

#endif
