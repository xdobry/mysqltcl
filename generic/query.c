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

/*----------------------------------------------------------------------

 * mysql_QueryTclObj
 * This to method control how tcl data is transfered to mysql and
 * how data is imported into tcl from mysql
 * Return value : Zero on success, Non-zero if an error occurred.
 */
static int mysql_QueryTclObj(MysqlTclHandle *handle, Tcl_Obj *obj) {
	char *query;
	int result, queryLen;

	Tcl_DString queryDS;

	query = Tcl_GetStringFromObj(obj, &queryLen);

	if (handle->encoding == NULL) {
		query = (char *) Tcl_GetByteArrayFromObj(obj, &queryLen);
		result = mysql_real_query(handle->connection, query, queryLen);
	} else {
		Tcl_UtfToExternalDString(handle->encoding, query, -1, &queryDS);
		queryLen = Tcl_DStringLength(&queryDS);
		result = mysql_real_query(handle->connection, Tcl_DStringValue(&queryDS), queryLen);
		Tcl_DStringFree(&queryDS);
	}
	return result;
}

/*
 * Create new Mysql NullObject
 * (similar to Tcl API for example Tcl_NewIntObj)
 */
static Tcl_Obj *Mysqltcl_NewNullObj(MysqltclState *mysqltclState) {
	Tcl_Obj *objPtr;
	objPtr = Tcl_NewObj();
	objPtr->bytes = NULL;
	objPtr->typePtr = &mysqlNullType;
	objPtr->internalRep.otherValuePtr = mysqltclState;
	return objPtr;
}

static Tcl_Obj *getRowCellAsObject(MysqltclState *mysqltclState, MysqlTclHandle *handle, MYSQL_ROW row, int length) {
	Tcl_Obj *obj;
	Tcl_DString ds;

	if (*row) {
		if (handle->encoding != NULL) {
			Tcl_ExternalToUtfDString(handle->encoding, *row, length, &ds);
			obj = Tcl_NewStringObj(Tcl_DStringValue(&ds), Tcl_DStringLength(&ds));
			Tcl_DStringFree(&ds);
		} else {
			obj = Tcl_NewByteArrayObj((unsigned char *) *row, length);
		}
	} else {
		obj = Mysqltcl_NewNullObj(mysqltclState);
	}
	return obj;
}

MysqlTclHandle *createHandleFrom(MysqltclState *statePtr, MysqlTclHandle *handle, enum MysqlHandleType handleType) {
	int number;
	MysqlTclHandle *qhandle;
	qhandle = createMysqlHandle(statePtr);
	/* do not overwrite the number */
	number = qhandle->number;
	if (!qhandle)
		return qhandle;
	memcpy(qhandle, handle, sizeof(MysqlTclHandle));
	qhandle->type = handleType;
	qhandle->number = number;
	return qhandle;
}

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Sel
 *    Implements the mysqlsel command:
 *    usage: mysqlsel handle sel-query ?-list|-flatlist?
 *
 *    results:
 *
 *    SIDE EFFECT: Flushes any pending result, even in case of conflict.
 *    Stores new results.
 */

int Mysqltcl_Sel(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	MysqltclState *statePtr = (MysqltclState *) clientData;
	Tcl_Obj *res, *resList;
	MYSQL_ROW row;
	MysqlTclHandle *handle;
	unsigned long *lengths;

	static CONST char* selOptions[] = { "-list", "-flatlist", NULL };
	/* Warning !! no option number */
	int i, selOption = 2, colCount;

	if ((handle = mysql_prologue(interp, objc, objv, 3, 4, CL_CONN, "handle sel-query ?-list|-flatlist?")) == 0)
		return TCL_ERROR;

	if (objc == 4) {
		if (Tcl_GetIndexFromObj(interp, objv[3], selOptions, "option", TCL_EXACT, &selOption) != TCL_OK)
			return TCL_ERROR;
	}

	/* Flush any previous result. */
	freeResult(handle);

	if (mysql_QueryTclObj(handle, objv[2])) {
		return mysql_server_confl(interp, objc, objv, handle->connection);
	}
	if (selOption < 2) {
		/* If imadiatly result than do not store result in mysql client library cache */
		handle->result = mysql_use_result(handle->connection);
	} else {
		handle->result = mysql_store_result(handle->connection);
	}

	if (handle->result == NULL) {
		if (selOption == 2)
			Tcl_SetObjResult(interp, Tcl_NewIntObj(-1));
	} else {
		colCount = handle->col_count = mysql_num_fields(handle->result);
		res = Tcl_GetObjResult(interp);
		handle->res_count = 0;
		switch (selOption) {
		case 0: /* -list */
			while ((row = mysql_fetch_row(handle->result)) != NULL) {
				resList = Tcl_NewListObj(0, NULL);
				lengths = mysql_fetch_lengths(handle->result);
				for (i = 0; i < colCount; i++, row++) {
					Tcl_ListObjAppendElement(interp, resList, getRowCellAsObject(statePtr, handle, row, lengths[i]));
				}
				Tcl_ListObjAppendElement(interp, res, resList);
			}
			break;
		case 1: /* -flatlist */
			while ((row = mysql_fetch_row(handle->result)) != NULL) {
				lengths = mysql_fetch_lengths(handle->result);
				for (i = 0; i < colCount; i++, row++) {
					Tcl_ListObjAppendElement(interp, res, getRowCellAsObject(statePtr, handle, row, lengths[i]));
				}
			}
			break;
		case 2: /* No option */
			handle->res_count = mysql_num_rows(handle->result);
			Tcl_SetIntObj(res, handle->res_count);
			break;
		}
	}
	return TCL_OK;
}

/*
 * Mysqltcl_Query
 * Works as mysqltclsel but return an $query handle that allow to build
 * nested queries on simple handle
 */

int Mysqltcl_Query(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	MysqltclState *statePtr = (MysqltclState *) clientData;
	MYSQL_RES *result;
	MysqlTclHandle *handle, *qhandle;

	if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_CONN,

	"handle sqlstatement")) == 0)
		return TCL_ERROR;

	if (mysql_QueryTclObj(handle, objv[2])) {
		return mysql_server_confl(interp, objc, objv, handle->connection);
	}

	if ((result = mysql_store_result(handle->connection)) == NULL) {
		Tcl_SetObjResult(interp, Tcl_NewIntObj(-1));
		return TCL_OK;
	}
	if ((qhandle = createHandleFrom(statePtr, handle, HT_QUERY)) == NULL)
		return TCL_ERROR;
	qhandle->result = result;
	qhandle->col_count = mysql_num_fields(qhandle->result);

	qhandle->res_count = mysql_num_rows(qhandle->result);
	Tcl_SetObjResult(interp, Tcl_NewHandleObj(statePtr, qhandle));
	return TCL_OK;
}

/*
 * Mysqltcl_Enquery
 * close and free a query handle
 * if handle is not query than the result will be discarted
 */

int Mysqltcl_EndQuery(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	MysqltclState *statePtr = (MysqltclState *) clientData;
	Tcl_HashEntry *entryPtr;
	MysqlTclHandle *handle;

	if ((handle = mysql_prologue(interp, objc, objv, 2, 2, CL_CONN, "queryhandle")) == 0)
		return TCL_ERROR;

	if (handle->type == HT_QUERY) {
		entryPtr = Tcl_FindHashEntry(&statePtr->hash,Tcl_GetStringFromObj(objv[1],NULL));
		if (entryPtr) {
			Tcl_DeleteHashEntry(entryPtr);
		}
		closeHandle(handle);
	} else {
		freeResult(handle);
	}
	return TCL_OK;
}

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

void MysqlBindFreeBuffer(
    MYSQL_BIND* b,		/* Pointer to a binding array */
    int i			/* Index into the array */
) {
 	if (b[i].buffer) {
	    Tcl_Free(b[i].buffer);
	    b[i].buffer = NULL;
	}
	b[i].buffer_length = 0;
}

static int fetch_pstatement(MysqltclState *statePtr, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[],
		MysqlTclHandle *handle) {
	Tcl_Obj *resList=NULL,*colObj=NULL;
	int resultColumns;
	void* bufPtr;
	Tcl_WideInt bitVal;
	unsigned char byte;
	int j,idx,fetchResult;
	MYSQL_FIELD* fields = NULL;
	unsigned long * resultLengths = handle->resultLengths;

	printf("fetching pstatement\n");

	// mysql_stmt_bind_result(handle->statement, handle->bindResult);

	fetchResult = mysql_stmt_fetch(handle->statement);
	if (fetchResult==0 || fetchResult==MYSQL_DATA_TRUNCATED) {
		resList = Tcl_NewObj();
		resultColumns = mysql_stmt_field_count(handle->statement);
		fields = mysql_fetch_fields(handle->resultMetadata);
		for (idx = 0; idx < resultColumns; idx++) {
			if (!handle->bindResult[idx].is_null_value) {
			    if (resultLengths[idx]>handle->bindResult[idx].buffer_length)  {
			    	printf("size to small is %ld required %ld error %d\n",handle->bindResult[idx].buffer_length,resultLengths[idx],handle->bindResult[idx].error_value);
			    	MysqlBindFreeBuffer(handle->bindResult, idx);
			    	MysqlBindAllocBuffer(handle->bindResult, idx, resultLengths[idx] + 1);
			    	handle->needColumnFetch[idx]=1;
			    	if (mysql_stmt_fetch_column(handle->statement,&handle->bindResult[idx],idx, 0)) {
			    		// TODO clean up
			    		printf("can not fetch column\n");
			    		return TCL_ERROR;
			    	}
			    } else {
			    	// strange behaviur if once the column has used additional fetch_column
			    	// it need it always or one need rebind result before next fetch
			    	if (handle->needColumnFetch[idx]) {
			    		printf("need columns fetch for %d\n",idx);
			    		if (mysql_stmt_fetch_column(handle->statement,&handle->bindResult[idx],idx, 0)) {
				    		// TODO clean up
				    		printf("can not fetch column\n");
				    		return TCL_ERROR;
				    	}
			    	}
			    }
			    bufPtr = handle->bindResult[idx].buffer;
			    switch (handle->bindResult[idx].buffer_type) {
				case MYSQL_TYPE_BIT:
					bitVal = 0;
					for (j = 0; j < resultLengths[idx]; ++j) {
						byte = ((unsigned char*) bufPtr)[resultLengths[idx] - 1 - j];
						bitVal |= (byte << (8 * j));
					}
					colObj = Tcl_NewWideIntObj(bitVal);
					break;
				case MYSQL_TYPE_DOUBLE:
					colObj = Tcl_NewDoubleObj(*(double*) bufPtr);
					break;
				case MYSQL_TYPE_LONG:
					colObj = Tcl_NewIntObj(*(int*) bufPtr);
					break;
				case MYSQL_TYPE_LONGLONG:
					colObj = Tcl_NewWideIntObj(*(Tcl_WideInt*) bufPtr);
					break;
				default:
					// From mysql manual 22.2.1. C API Data types
					// To distinguish between binary and non-binary data for string data types, check whether the charsetnr  value is 63.
					if (fields[idx].charsetnr == 63) {
						colObj = Tcl_NewByteArrayObj((unsigned char*) bufPtr, resultLengths[idx]);
					} else {
						colObj = Tcl_NewStringObj((char*) bufPtr, resultLengths[idx]);
					}
					break;
				}
			} else {
				colObj = Mysqltcl_NewNullObj(statePtr);
			}
			Tcl_ListObjAppendElement(interp, resList, colObj);
		}
		if (objc == 3) {
			if (Tcl_ObjSetVar2(interp, objv[2], NULL, resList, TCL_LEAVE_ERR_MSG) == NULL) {
				return TCL_ERROR;
			}
			Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
		} else {
			Tcl_SetObjResult(interp, resList);
		}
	} else {
		// Empty Result
		if (objc == 3) {
			Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
		}
		return TCL_OK;
	}
	return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Fetch
 *    Implements the mysqlnext command:

 *    usage: mysql::fetch handle ?row?
 *
 *    results:
 *	next row from pending results as tcl list, or null list.
 */

int Mysqltcl_Fetch(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	MysqltclState *statePtr = (MysqltclState *) clientData;
	MysqlTclHandle *handle;
	int idx;
	MYSQL_ROW row;
	Tcl_Obj *resList;
	unsigned long *lengths;

	if ((handle = mysql_prologue(interp, objc, objv, 2, 3, CL_RES, "handle")) == 0)
		return TCL_ERROR;

	if (handle->type == HT_STATEMENT) {
		return fetch_pstatement(clientData, interp, objc, objv, handle);
	}

	if (handle->res_count == 0) {
		if (objc == 3) {
			Tcl_SetObjResult(interp, Tcl_NewBooleanObj(0));
		}
		return TCL_OK;
	} else if ((row = mysql_fetch_row(handle->result)) == NULL) {
		handle->res_count = 0;
		return mysql_prim_confl(interp, objc, objv, "result counter out of sync");
	} else
		handle->res_count--;

	lengths = mysql_fetch_lengths(handle->result);

	resList = Tcl_NewObj();
	for (idx = 0; idx < handle->col_count; idx++, row++) {
		Tcl_ListObjAppendElement(interp, resList, getRowCellAsObject(statePtr, handle, row, lengths[idx]));
	}

	if (objc == 3) {
		if (Tcl_ObjSetVar2(interp, objv[2], NULL, resList, TCL_LEAVE_ERR_MSG) == NULL) {
			return TCL_ERROR;
		}
		Tcl_SetObjResult(interp, Tcl_NewBooleanObj(1));
	} else {
		Tcl_SetObjResult(interp, resList);
	}
	return TCL_OK;

}

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Seek
 *    Implements the mysqlseek command:
 *    usage: mysqlseek handle rownumber
 *
 *    results:
 *	number of remaining rows
 */

int Mysqltcl_Seek(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	MysqlTclHandle *handle;
	int row;
	int total;

	if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_RES, " handle row-index")) == 0)
		return TCL_ERROR;

	if (Tcl_GetIntFromObj(interp, objv[2], &row) != TCL_OK)
		return TCL_ERROR;

	total = mysql_num_rows(handle->result);

	if (total + row < 0) {
		mysql_data_seek(handle->result, 0);

		handle->res_count = total;
	} else if (row < 0) {
		mysql_data_seek(handle->result, total + row);
		handle->res_count = -row;
	} else if (row >= total) {
		mysql_data_seek(handle->result, row);
		handle->res_count = 0;
	} else {
		mysql_data_seek(handle->result, row);
		handle->res_count = total - row;
	}

	Tcl_SetObjResult(interp, Tcl_NewIntObj(handle->res_count));
	return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Map
 * Implements the mysqlmap command:
 * usage: mysqlmap handle binding-list script
 *
 * Results:
 * SIDE EFFECT: For each row the column values are bound to the variables
 * in the binding list and the script is evaluated.
 * The variables are created in the current context.
 * NOTE: mysqlmap works very much like a 'foreach' construct.
 * The 'continue' and 'break' commands may be used with their usual effect.
 */

int Mysqltcl_Map(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	MysqltclState *statePtr = (MysqltclState *) clientData;
	int code;
	int count;

	MysqlTclHandle *handle;
	int idx;
	int listObjc;
	Tcl_Obj *tempObj, *varNameObj;
	MYSQL_ROW row;
	int *val;
	unsigned long *lengths;

	if ((handle = mysql_prologue(interp, objc, objv, 4, 4, CL_RES, "handle binding-list script")) == 0)
		return TCL_ERROR;

	if (Tcl_ListObjLength(interp, objv[2], &listObjc) != TCL_OK)
		return TCL_ERROR;

	if (listObjc > handle->col_count) {
		return mysql_prim_confl(interp, objc, objv, "too many variables in binding list");
	} else
		count = (listObjc < handle->col_count) ? listObjc : handle->col_count;

	val = (int*) Tcl_Alloc((count * sizeof(int)));

	for (idx = 0; idx < count; idx++) {
		val[idx] = 1;
		if (Tcl_ListObjIndex(interp, objv[2], idx, &varNameObj) != TCL_OK)
			return TCL_ERROR;
		if (Tcl_GetStringFromObj(varNameObj, 0)[0] != '-')
			val[idx] = 1;
		else
			val[idx] = 0;
	}

	while (handle->res_count > 0) {
		/* Get next row, decrement row counter. */
		if ((row = mysql_fetch_row(handle->result)) == NULL) {
			handle->res_count = 0;
			Tcl_Free((char *) val);
			return mysql_prim_confl(interp, objc, objv, "result counter out of sync");
		} else
			handle->res_count--;

		/* Bind variables to column values. */
		for (idx = 0; idx < count; idx++, row++) {
			lengths = mysql_fetch_lengths(handle->result);
			if (val[idx]) {
				tempObj = getRowCellAsObject(statePtr, handle, row, lengths[idx]);
				if (Tcl_ListObjIndex(interp, objv[2], idx, &varNameObj) != TCL_OK)
					goto error;
				if (Tcl_ObjSetVar2(interp, varNameObj, NULL, tempObj, 0) == NULL)
					goto error;
			}
		}

		/* Evaluate the script. */
		switch (code = Tcl_EvalObjEx(interp, objv[3], 0)) {
		case TCL_CONTINUE:
		case TCL_OK:
			break;
		case TCL_BREAK:
			Tcl_Free((char *) val);
			return TCL_OK;
		default:
			Tcl_Free((char *) val);
			return code;
		}
	}
	Tcl_Free((char *) val);
	return TCL_OK;
	error: Tcl_Free((char *) val);
	return TCL_ERROR;
}

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Receive
 * Implements the mysqlmap command:
 * usage: mysqlmap handle sqlquery binding-list script
 *
 * The method use internal mysql_use_result that no cache statment on client but
 * receive it direct from server
 *
 * Results:
 * SIDE EFFECT: For each row the column values are bound to the variables
 * in the binding list and the script is evaluated.
 * The variables are created in the current context.
 * NOTE: mysqlmap works very much like a 'foreach' construct.
 * The 'continue' and 'break' commands may be used with their usual effect.

 */

int Mysqltcl_Receive(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	MysqltclState *statePtr = (MysqltclState *) clientData;
	int code = 0;
	int count = 0;

	MysqlTclHandle *handle;
	int idx;
	int listObjc;
	Tcl_Obj *tempObj, *varNameObj;
	MYSQL_ROW row;
	int *val = NULL;
	int breakLoop = 0;
	unsigned long *lengths;

	if ((handle = mysql_prologue(interp, objc, objv, 5, 5, CL_CONN, "handle sqlquery binding-list script")) == 0)
		return TCL_ERROR;

	if (Tcl_ListObjLength(interp, objv[3], &listObjc) != TCL_OK)
		return TCL_ERROR;

	freeResult(handle);

	if (mysql_QueryTclObj(handle, objv[2])) {
		return mysql_server_confl(interp, objc, objv, handle->connection);
	}

	if ((handle->result = mysql_use_result(handle->connection)) == NULL) {
		return mysql_server_confl(interp, objc, objv, handle->connection);
	} else {
		while ((row = mysql_fetch_row(handle->result)) != NULL) {
			if (val == NULL) {
				/* first row compute all data */
				handle->col_count = mysql_num_fields(handle->result);
				if (listObjc > handle->col_count) {
					return mysql_prim_confl(interp, objc, objv, "too many variables in binding list");
				} else {
					count = (listObjc < handle->col_count) ? listObjc : handle->col_count;
				}
				val = (int*) Tcl_Alloc((count * sizeof(int)));
				for (idx = 0; idx < count; idx++) {
					if (Tcl_ListObjIndex(interp, objv[3], idx, &varNameObj) != TCL_OK)
						return TCL_ERROR;
					if (Tcl_GetStringFromObj(varNameObj, 0)[0] != '-')
						val[idx] = 1;
					else
						val[idx] = 0;
				}
			}
			for (idx = 0; idx < count; idx++, row++) {
				lengths = mysql_fetch_lengths(handle->result);

				if (val[idx]) {
					if (Tcl_ListObjIndex(interp, objv[3], idx, &varNameObj) != TCL_OK) {
						Tcl_Free((char *) val);
						return TCL_ERROR;
					}
					tempObj = getRowCellAsObject(statePtr, handle, row, lengths[idx]);
					if (Tcl_ObjSetVar2(interp, varNameObj, NULL, tempObj, TCL_LEAVE_ERR_MSG) == NULL) {
						Tcl_Free((char *) val);
						return TCL_ERROR;
					}
				}
			}

			/* Evaluate the script. */
			switch (code = Tcl_EvalObjEx(interp, objv[4], 0)) {
			case TCL_CONTINUE:
			case TCL_OK:
				break;
			case TCL_BREAK:
				breakLoop = 1;
				break;
			default:
				breakLoop = 1;
				break;
			}
			if (breakLoop == 1)
				break;
		}
	}
	if (val != NULL) {
		Tcl_Free((char *) val);
	}
	/*  Read all rest rows that leave in error or break case */
	while ((row = mysql_fetch_row(handle->result)) != NULL)
		;
	if (code != TCL_CONTINUE && code != TCL_OK && code != TCL_BREAK) {
		return code;
	} else {
		return mysql_server_confl(interp, objc, objv, handle->connection);
	}
}

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Exec
 * Implements the mysqlexec command:
 * usage: mysqlexec handle sql-statement
 *
 * Results:
 * Number of affected rows on INSERT, UPDATE or DELETE, 0 otherwise.
 *
 * SIDE EFFECT: Flushes any pending result, even in case of conflict.
 */
int Mysqltcl_Exec(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	MysqlTclHandle *handle;
	int affected;
	Tcl_Obj *resList;
	if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_CONN, "handle sql-statement")) == 0)
		return TCL_ERROR;

	/* Flush any previous result. */
	freeResult(handle);

	if (mysql_QueryTclObj(handle, objv[2]))
		return mysql_server_confl(interp, objc, objv, handle->connection);

	if ((affected = mysql_affected_rows(handle->connection)) < 0)
		affected = 0;

#if (MYSQL_VERSION_ID >= 50000)
	if (!mysql_next_result(handle->connection)) {
		resList = Tcl_GetObjResult(interp);
		Tcl_ListObjAppendElement(interp, resList, Tcl_NewIntObj(affected));
		do {
			if ((affected = mysql_affected_rows(handle->connection)) < 0)
				affected = 0;
			Tcl_ListObjAppendElement(interp, resList, Tcl_NewIntObj(affected));
		} while (!mysql_next_result(handle->connection));
		return TCL_OK;
	}
#endif
	Tcl_SetIntObj(Tcl_GetObjResult(interp), affected);
	return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_NewNull
 *    usage: mysql::newnull
 *
 */

int Mysqltcl_NewNull(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
	if (objc != 1) {
		Tcl_WrongNumArgs(interp, 1, objv, "");
		return TCL_ERROR;
	}
	Tcl_SetObjResult(interp, Mysqltcl_NewNullObj((MysqltclState *) clientData));
	return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_MoreResult
 *    usage: mysql::moreresult handle
 *    return true if more results exists
 */

int Mysqltcl_MoreResult(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
#if (MYSQL_VERSION_ID < 40107)
	Tcl_AddErrorInfo(interp, FUNCTION_NOT_AVAILABLE);
	return TCL_ERROR;
#else
	MysqlTclHandle *handle;
	int boolResult = 0;

	if ((handle = mysql_prologue(interp, objc, objv, 2, 2, CL_RES, "handle")) == 0)
		return TCL_ERROR;
	boolResult = mysql_more_results(handle->connection);
	Tcl_SetObjResult(interp, Tcl_NewBooleanObj(boolResult));
	return TCL_OK;
#endif
}
/*

 *----------------------------------------------------------------------
 *
 * Mysqltcl_NextResult
 *    usage: mysql::nextresult
 *
 *  return nummber of rows in result set. 0 if no next result
 */

int Mysqltcl_NextResult(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj * const objv[]) {
#if (MYSQL_VERSION_ID < 40107)
	Tcl_AddErrorInfo(interp, FUNCTION_NOT_AVAILABLE);
	return TCL_ERROR;
#else
	MysqlTclHandle *handle;
	int result = 0;

	if ((handle = mysql_prologue(interp, objc, objv, 2, 2, CL_RES, "handle")) == 0)
		return TCL_ERROR;
	if (handle->result != NULL) {
		mysql_free_result(handle->result);
		handle->result = NULL;
	}
	result = mysql_next_result(handle->connection);
	if (result == -1) {
		Tcl_SetObjResult(interp, Tcl_NewIntObj(0));
		return TCL_OK;
	}
	if (result < 0) {
		return mysql_server_confl(interp, objc, objv, handle->connection);
	}
	handle->result = mysql_store_result(handle->connection);
	if (handle->result == NULL) {
		Tcl_SetObjResult(interp, Tcl_NewIntObj(-1));
	} else {
		handle->res_count = mysql_num_rows(handle->result);
		Tcl_SetObjResult(interp, Tcl_NewIntObj(handle->res_count));
	}
	return TCL_OK;
#endif
}
