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

static int Mysqltcl_Escape(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int Mysqltcl_Info(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int Mysqltcl_Result(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int Mysqltcl_Col(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int Mysqltcl_State(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int Mysqltcl_InsertId(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
static int MysqlHandleSet _ANSI_ARGS_((Tcl_Interp *interp,Tcl_Obj *objPtr));
static void MysqlHandleFree _ANSI_ARGS_((Tcl_Obj *objPtr));
static int MysqlNullSet _ANSI_ARGS_((Tcl_Interp *interp,Tcl_Obj *objPtr));
static void UpdateStringOfNull _ANSI_ARGS_((Tcl_Obj *objPtr));


static char *MysqlHandlePrefix = "mysql";
/* Prefix string used to identify handles.
 * The following must be strlen(MysqlHandlePrefix).
 */
#define MYSQL_HPREFIX_LEN 5

/* handle object type
 * This section defince funtions for Handling new Tcl_Obj type */

Tcl_ObjType mysqlHandleType = {
    "mysqlhandle",
    MysqlHandleFree,
    (Tcl_DupInternalRepProc *) NULL,
    NULL,
    MysqlHandleSet
};

Tcl_ObjType mysqlNullType = {
    "mysqlnull",
    (Tcl_FreeInternalRepProc *) NULL,
    (Tcl_DupInternalRepProc *) NULL,
    UpdateStringOfNull,
    MysqlNullSet
};

static int MysqlNullSet(Tcl_Interp *interp, Tcl_Obj *objPtr)
{
    Tcl_ObjType *oldTypePtr = objPtr->typePtr;

    if ((oldTypePtr != NULL) && (oldTypePtr->freeIntRepProc != NULL)) {
        oldTypePtr->freeIntRepProc(objPtr);
    }
    objPtr->typePtr = &mysqlNullType;
    return TCL_OK;
}

static void UpdateStringOfNull(Tcl_Obj *objPtr) {
	int valueLen;
	MysqltclState *state = (MysqltclState *)objPtr->internalRep.otherValuePtr;

	valueLen = strlen(state->MysqlNullvalue);
	objPtr->bytes = Tcl_Alloc(valueLen+1);
	strcpy(objPtr->bytes,state->MysqlNullvalue);
	objPtr->length = valueLen;
}


static MysqltclState *getMysqltclState(Tcl_Interp *interp) {
  Tcl_CmdInfo cmdInfo;
  if (Tcl_GetCommandInfo(interp,"mysqlconnect",&cmdInfo)==0) {
    return NULL;
  }
  return (MysqltclState *)cmdInfo.objClientData;
}

static int MysqlHandleSet(Tcl_Interp *interp, register Tcl_Obj *objPtr)
{
    Tcl_ObjType *oldTypePtr = objPtr->typePtr;
    char *string;
    MysqlTclHandle *handle;
    Tcl_HashEntry *entryPtr;
    MysqltclState *statePtr;

    string = Tcl_GetStringFromObj(objPtr, NULL);  
    statePtr = getMysqltclState(interp);
    if (statePtr==NULL) return TCL_ERROR;

    entryPtr = Tcl_FindHashEntry(&statePtr->hash,string);
    if (entryPtr == NULL) {

      handle=0;
    } else {
      handle=(MysqlTclHandle *)Tcl_GetHashValue(entryPtr);
    }
    if (!handle) {
        if (interp != NULL)
	  return TCL_ERROR;
    }
    if ((oldTypePtr != NULL) && (oldTypePtr->freeIntRepProc != NULL)) {
        oldTypePtr->freeIntRepProc(objPtr);
    }
    
    objPtr->internalRep.otherValuePtr = (MysqlTclHandle *) handle;
    objPtr->typePtr = &mysqlHandleType;
    Tcl_Preserve((char *)handle);
    return TCL_OK;
}


static void MysqlHandleFree(Tcl_Obj *obj)
{
  MysqlTclHandle *handle = (MysqlTclHandle *)obj->internalRep.otherValuePtr;
  Tcl_Release((char *)handle);
}

static int GetHandleFromObj(Tcl_Interp *interp,Tcl_Obj *objPtr,MysqlTclHandle **handlePtr)
{
    if (Tcl_ConvertToType(interp, objPtr, &mysqlHandleType) != TCL_OK)
        return TCL_ERROR;
    *handlePtr = (MysqlTclHandle *)objPtr->internalRep.otherValuePtr;
    return TCL_OK;
}

Tcl_Obj *Tcl_NewHandleObj(MysqltclState *statePtr,MysqlTclHandle *handle)
{
    register Tcl_Obj *objPtr;
    char buffer[MYSQL_HPREFIX_LEN+TCL_DOUBLE_SPACE+1];
    register int len;
    Tcl_HashEntry *entryPtr;
    int newflag;

    objPtr=Tcl_NewObj();
    /* the string for "query" can not be longer as MysqlHandlePrefix see buf variable */
    len=sprintf(buffer, "%s%d", (handle->type==HT_QUERY) ? "query" : MysqlHandlePrefix,handle->number);    
    objPtr->bytes = Tcl_Alloc((unsigned) len + 1);
    strcpy(objPtr->bytes, buffer);
    objPtr->length = len;
    
    entryPtr=Tcl_CreateHashEntry(&statePtr->hash,buffer,&newflag);
    Tcl_SetHashValue(entryPtr,handle);     
  
    objPtr->internalRep.otherValuePtr = handle;
    objPtr->typePtr = &mysqlHandleType;

    Tcl_Preserve((char *)handle);  

    return objPtr;
}




/* CONFLICT HANDLING
 *
 * Every command begins by calling 'mysql_prologue'.
 * This function resets mysqlstatus(code) to zero; the other array elements
 * retain their previous values.
 * The function also saves objc/objv in global variables.
 * After this the command processing proper begins.
 *
 * If there is a conflict, the message is taken from one of the following
 * sources,
 * -- this code (mysql_prim_confl),
 * -- the database server (mysql_server_confl),
 * A complete message is put together from the above plus the name of the
 * command where the conflict was detected.
 * The complete message is returned as the Tcl result and is also stored in
 * mysqlstatus(message).
 * mysqlstatus(code) is set to "-1" for a primitive conflict or to mysql_errno
 * for a server conflict
 * In addition, the whole command where the conflict was detected is put
 * together from the saved objc/objv and is copied into mysqlstatus(command).
 */

/*
 *-----------------------------------------------------------
 * set_statusArr
 * Help procedure to set Tcl global array with mysqltcl internal
 * informations
 */

static void set_statusArr(Tcl_Interp *interp,char *elem_name,Tcl_Obj *tobj)
{
  Tcl_SetVar2Ex (interp,MYSQL_STATUS_ARR,elem_name,tobj,TCL_GLOBAL_ONLY); 
}

/*
 *----------------------------------------------------------------------
 * clear_msg
 *
 * Clears all error and message elements in the global array variable.
 *
 */

static void
clear_msg(Tcl_Interp *interp)
{
  set_statusArr(interp,MYSQL_STATUS_CODE,Tcl_NewIntObj(0));
  set_statusArr(interp,MYSQL_STATUS_CMD,Tcl_NewObj());
  set_statusArr(interp,MYSQL_STATUS_MSG,Tcl_NewObj());
}

/*
 *----------------------------------------------------------------------
 * mysql_reassemble
 * Reassembles the current command from the saved objv; copies it into
 * mysqlstatus(command).
 */

static void mysql_reassemble(Tcl_Interp *interp,int objc,Tcl_Obj *CONST objv[])
{
   set_statusArr(interp,MYSQL_STATUS_CMD,Tcl_NewListObj(objc, objv));
}

/*
 * free result from handle and consume left result of multresult statement 
 */
void freeResult(MysqlTclHandle *handle)
{
	MYSQL_RES* result;
	if (handle->result != NULL) {
		mysql_free_result(handle->result);
		handle->result = NULL ;
	}
#if (MYSQL_VERSION_ID >= 50000)
	while (!mysql_next_result(handle->connection)) {
		result = mysql_store_result(handle->connection);
		if (result) {
			mysql_free_result(result);
		}
	}
#endif
}

/*
 *----------------------------------------------------------------------
 * mysql_prim_confl
 * Conflict handling after a primitive conflict.
 *
 */

int mysql_prim_confl(Tcl_Interp *interp,int objc,Tcl_Obj *CONST objv[],char *msg)
{
  set_statusArr(interp,MYSQL_STATUS_CODE,Tcl_NewIntObj(-1));

  Tcl_ResetResult(interp) ;
  Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
                          Tcl_GetString(objv[0]), ": ", msg, (char*)NULL);

  set_statusArr(interp,MYSQL_STATUS_MSG,Tcl_GetObjResult(interp));

  mysql_reassemble(interp,objc,objv) ;
  return TCL_ERROR ;
}


/*
 *----------------------------------------------------------------------
 * mysql_server_confl
 * Conflict handling after an mySQL conflict.
 * If error it set error message and return TCL_ERROR
 * If no error occurs it returns TCL_OK
 */

int mysql_server_confl(Tcl_Interp *interp,int objc,Tcl_Obj *CONST objv[],MYSQL * connection)
{
  const char* mysql_errorMsg;
  if (mysql_errno(connection)) {
    mysql_errorMsg = mysql_error(connection);

    set_statusArr(interp,MYSQL_STATUS_CODE,Tcl_NewIntObj(mysql_errno(connection)));


    Tcl_ResetResult(interp) ;
    Tcl_AppendStringsToObj(Tcl_GetObjResult(interp),
                          Tcl_GetString(objv[0]), "/db server: ",
		          (mysql_errorMsg == NULL) ? "" : mysql_errorMsg,
                          (char*)NULL) ;

    set_statusArr(interp,MYSQL_STATUS_MSG,Tcl_GetObjResult(interp));

    mysql_reassemble(interp,objc,objv);
    return TCL_ERROR;
  } else {
    return TCL_OK;
  }
}

static  MysqlTclHandle *get_handle(Tcl_Interp *interp,int objc,Tcl_Obj *CONST objv[],int check_level) 
{
  MysqlTclHandle *handle;
  if (GetHandleFromObj(interp, objv[1], &handle) != TCL_OK) {
    mysql_prim_confl(interp,objc,objv,"not mysqltcl handle") ;
    return NULL;
  }
  if (check_level==CL_PLAIN) return handle;
  if (handle->connection == 0) {
      mysql_prim_confl(interp,objc,objv,"handle already closed (dangling pointer)") ;
      return NULL;
  }
  if (check_level==CL_CONN) return handle;
  if (check_level!=CL_RES) {
    if (handle->database[0] == '\0') {
      mysql_prim_confl(interp,objc,objv,"no current database") ;
      return NULL;
    }
    if (check_level==CL_DB) return handle;
  }
  if (handle->result == NULL) {
      mysql_prim_confl(interp,objc,objv,"no result pending") ;
      return NULL;
  }
  return handle;
}




MysqlTclHandle *createMysqlHandle(MysqltclState *statePtr)
{
  MysqlTclHandle *handle;
  handle=(MysqlTclHandle *)Tcl_Alloc(sizeof(MysqlTclHandle));
  memset(handle,0,sizeof(MysqlTclHandle));
  if (handle == 0) {
    panic("no memory for handle");
    return handle;
  }
  handle->type = HT_CONNECTION;

  /* MT-safe, because every thread in tcl has own interpreter */
  handle->number=statePtr->handleNum++;
  return handle;
}


void closeHandle(MysqlTclHandle *handle)
{
  freeResult(handle);
  if (handle->type==HT_CONNECTION) {
    mysql_close(handle->connection);
  }
#ifdef PREPARED_STATEMENT
  if (handle->type==HT_STATEMENT) {
    if (handle->statement!=NULL)
	    mysql_stmt_close(handle->statement);
	if (handle->bindResult!=NULL)
		Tcl_Free((char *)handle->bindResult);
    if (handle->bindParam!=NULL)
    	Tcl_Free((char *)handle->bindParam);
    if (handle->resultMetadata!=NULL)
	    mysql_free_result(handle->resultMetadata);
    if (handle->paramMetadata!=NULL)
	    mysql_free_result(handle->paramMetadata);
  }
#endif
  handle->connection = (MYSQL *)NULL;
  if (handle->encoding!=NULL && handle->type==HT_CONNECTION)
  {
    Tcl_FreeEncoding(handle->encoding);
    handle->encoding = NULL;
  }
  Tcl_EventuallyFree((char *)handle,TCL_DYNAMIC);
}

/*
 *----------------------------------------------------------------------
 * mysql_prologue
 *
 * Does most of standard command prologue; required for all commands
 * having conflict handling.
 * 'req_min_args' must be the minimum number of arguments for the command,
 * including the command word.
 * 'req_max_args' must be the maximum number of arguments for the command,
 * including the command word.
 * 'usage_msg' must be a usage message, leaving out the command name.
 * Checks the handle assumed to be present in objv[1] if 'check' is not NULL.
 * RETURNS: Handle index or -1 on failure.
 * SIDE EFFECT: Sets the Tcl result on failure.
 */

MysqlTclHandle *mysql_prologue(Tcl_Interp *interp,int objc,Tcl_Obj *CONST objv[],int req_min_args,int req_max_args,int check_level,char *usage_msg)
{
  /* Check number of args. */
  if (objc < req_min_args || objc > req_max_args) {
      Tcl_WrongNumArgs(interp, 1, objv, usage_msg);
      return NULL;
  }

  /* Reset mysqlstatus(code). */
  set_statusArr(interp,MYSQL_STATUS_CODE,Tcl_NewIntObj(0));

  /* Check the handle.
   * The function is assumed to set the status array on conflict.
   */
  return (get_handle(interp,objc,objv,check_level));
}

/*
 *----------------------------------------------------------------------
 * mysql_colinfo
 *
 * Given an MYSQL_FIELD struct and a string keyword appends a piece of
 * column info (one item) to the Tcl result.
 * ASSUMES 'fld' is non-null.
 * RETURNS 0 on success, 1 otherwise.
 * SIDE EFFECT: Sets the result and status on failure.
 */

static Tcl_Obj *mysql_colinfo(Tcl_Interp *interp,int objc,Tcl_Obj *CONST objv[],MYSQL_FIELD* fld,Tcl_Obj * keyw)
{
  int idx ;

  static CONST char* MysqlColkey[] =
    {
      "table", "name", "type", "length", "prim_key", "non_null", "numeric", "decimals", NULL
    };
  enum coloptions {
    MYSQL_COL_TABLE_K, MYSQL_COL_NAME_K, MYSQL_COL_TYPE_K, MYSQL_COL_LENGTH_K, 
    MYSQL_COL_PRIMKEY_K, MYSQL_COL_NONNULL_K, MYSQL_COL_NUMERIC_K, MYSQL_COL_DECIMALS_K};

  if (Tcl_GetIndexFromObj(interp, keyw, MysqlColkey, "option",
                          TCL_EXACT, &idx) != TCL_OK)
    return NULL;

  switch (idx)
    {
    case MYSQL_COL_TABLE_K:
      return Tcl_NewStringObj(fld->table, -1) ;
    case MYSQL_COL_NAME_K:
      return Tcl_NewStringObj(fld->name, -1) ;
    case MYSQL_COL_TYPE_K:
      switch (fld->type)
	{


	case FIELD_TYPE_DECIMAL:
	  return Tcl_NewStringObj("decimal", -1);
	case FIELD_TYPE_TINY:
	  return Tcl_NewStringObj("tiny", -1);
	case FIELD_TYPE_SHORT:
	  return Tcl_NewStringObj("short", -1);
	case FIELD_TYPE_LONG:
	  return Tcl_NewStringObj("long", -1) ;
	case FIELD_TYPE_FLOAT:
	  return Tcl_NewStringObj("float", -1);
	case FIELD_TYPE_DOUBLE:
	  return Tcl_NewStringObj("double", -1);
	case FIELD_TYPE_NULL:
	  return Tcl_NewStringObj("null", -1);
	case FIELD_TYPE_TIMESTAMP:
	  return Tcl_NewStringObj("timestamp", -1);
	case FIELD_TYPE_LONGLONG:
	  return Tcl_NewStringObj("long long", -1);
	case FIELD_TYPE_INT24:
	  return Tcl_NewStringObj("int24", -1);
	case FIELD_TYPE_DATE:
	  return Tcl_NewStringObj("date", -1);
	case FIELD_TYPE_TIME:
	  return Tcl_NewStringObj("time", -1);
	case FIELD_TYPE_DATETIME:
	  return Tcl_NewStringObj("date time", -1);
	case FIELD_TYPE_YEAR:
	  return Tcl_NewStringObj("year", -1);
	case FIELD_TYPE_NEWDATE:
	  return Tcl_NewStringObj("new date", -1);
	case FIELD_TYPE_ENUM:
	  return Tcl_NewStringObj("enum", -1); 
	case FIELD_TYPE_SET:
	  return Tcl_NewStringObj("set", -1);
	case FIELD_TYPE_TINY_BLOB:
	  return Tcl_NewStringObj("tiny blob", -1);
	case FIELD_TYPE_MEDIUM_BLOB:
	  return Tcl_NewStringObj("medium blob", -1);
	case FIELD_TYPE_LONG_BLOB:
	  return Tcl_NewStringObj("long blob", -1);
	case FIELD_TYPE_BLOB:
	  return Tcl_NewStringObj("blob", -1);
	case FIELD_TYPE_VAR_STRING:
	  return Tcl_NewStringObj("var string", -1);
	case FIELD_TYPE_STRING:
	  return Tcl_NewStringObj("string", -1);
#if MYSQL_VERSION_ID >= 50000
	case MYSQL_TYPE_NEWDECIMAL:
	   return Tcl_NewStringObj("newdecimal", -1);
	case MYSQL_TYPE_GEOMETRY:
	   return Tcl_NewStringObj("geometry", -1);
	case MYSQL_TYPE_BIT:
	   return Tcl_NewStringObj("bit", -1);
#endif
	default:
	  return Tcl_NewStringObj("unknown", -1);
	}
      break ;
    case MYSQL_COL_LENGTH_K:
      return Tcl_NewIntObj(fld->length) ;
    case MYSQL_COL_PRIMKEY_K:
      return Tcl_NewBooleanObj(IS_PRI_KEY(fld->flags));
    case MYSQL_COL_NONNULL_K:
      return Tcl_NewBooleanObj(IS_NOT_NULL(fld->flags));
    case MYSQL_COL_NUMERIC_K:
      return Tcl_NewBooleanObj(IS_NUM(fld->type));
    case MYSQL_COL_DECIMALS_K:
      return IS_NUM(fld->type)? Tcl_NewIntObj(fld->decimals): Tcl_NewIntObj(-1);
    default: /* should never happen */
      mysql_prim_confl(interp,objc,objv,"weirdness in mysql_colinfo");
      return NULL ;
    }
}


/*
 * Invoked from Interpreter by removing mysqltcl command

 * Warnign: This procedure can be called only once
 */
static void Mysqltcl_Kill(ClientData clientData) 
{ 
   MysqltclState *statePtr = (MysqltclState *)clientData; 
   Tcl_HashEntry *entryPtr; 
   MysqlTclHandle *handle;
   Tcl_HashSearch search; 

   for (entryPtr=Tcl_FirstHashEntry(&statePtr->hash,&search); 
       entryPtr!=NULL;
       entryPtr=Tcl_NextHashEntry(&search)) {
     handle=(MysqlTclHandle *)Tcl_GetHashValue(entryPtr);
     if (handle->connection == 0) continue;
     closeHandle(handle);
   } 
   Tcl_Free(statePtr->MysqlNullvalue);
   Tcl_Free((char *)statePtr); 
}



/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Escape
 *    Implements the mysqlescape command:
 *    usage: mysqlescape string
 *	                
 *    results:
 *	Escaped string for use in queries.
 */

static int Mysqltcl_Escape(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  int len;
  char *inString, *outString;
  MysqlTclHandle *handle;
  
  if (objc <2 || objc>3) {
      Tcl_WrongNumArgs(interp, 1, objv, "?handle? string");
      return TCL_ERROR;
  }
  if (objc==2) {
    inString=Tcl_GetStringFromObj(objv[1], &len);
    outString=Tcl_Alloc((len<<1) + 1);
    len=mysql_escape_string(outString, inString, len);
    Tcl_SetStringObj(Tcl_GetObjResult(interp), outString, len);
    Tcl_Free(outString);
  } else { 
    if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_CONN,
			    "handle string")) == 0)
      return TCL_ERROR;
    inString=Tcl_GetStringFromObj(objv[2], &len);
    outString=Tcl_Alloc((len<<1) + 1);
    len=mysql_real_escape_string(handle->connection, outString, inString, len);
    Tcl_SetStringObj(Tcl_GetObjResult(interp), outString, len);
    Tcl_Free(outString);
  }
  return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Info
 * Implements the mysqlinfo command:
 * usage: mysqlinfo handle option
 *


 */

static int Mysqltcl_Info(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{

  int count ;
  MysqlTclHandle *handle;
  int idx ;
  MYSQL_RES* list ;
  MYSQL_ROW row ;
  const char* val ;
  Tcl_Obj *resList;
  static CONST char* MysqlDbOpt[] =
    {
      "dbname", "dbname?", "tables", "host", "host?", "databases",
      "info","serverversion",
#if (MYSQL_VERSION_ID >= 40107)
      "serverversionid","sqlstate",
#endif
      "state",NULL
    };
  enum dboption {
    MYSQL_INFNAME_OPT, MYSQL_INFNAMEQ_OPT, MYSQL_INFTABLES_OPT,
    MYSQL_INFHOST_OPT, MYSQL_INFHOSTQ_OPT, MYSQL_INFLIST_OPT, MYSQL_INFO,
    MYSQL_INF_SERVERVERSION,MYSQL_INFO_SERVERVERSION_ID,MYSQL_INFO_SQLSTATE,MYSQL_INFO_STATE
  };
  
  /* We can't fully check the handle at this stage. */
  if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_PLAIN,
			    "handle option")) == 0)
    return TCL_ERROR;

  if (Tcl_GetIndexFromObj(interp, objv[2], MysqlDbOpt, "option",
                          TCL_EXACT, &idx) != TCL_OK)
    return TCL_ERROR;

  /* First check the handle. Checking depends on the option. */
  switch (idx) {
  case MYSQL_INFNAMEQ_OPT:
    if ((handle = get_handle(interp,objc,objv,CL_CONN))!=NULL) {
      if (handle->database[0] == '\0')
	return TCL_OK ; /* Return empty string if no current db. */
    }
    break ;
  case MYSQL_INFNAME_OPT:
  case MYSQL_INFTABLES_OPT:
  case MYSQL_INFHOST_OPT:
  case MYSQL_INFLIST_OPT:
    /* !!! */
    handle = get_handle(interp,objc,objv,CL_CONN);
    break;
  case MYSQL_INFO:
  case MYSQL_INF_SERVERVERSION:
#if (MYSQL_VERSION_ID >= 40107)
  case MYSQL_INFO_SERVERVERSION_ID:
  case MYSQL_INFO_SQLSTATE:
#endif
  case MYSQL_INFO_STATE:
    break;

  case MYSQL_INFHOSTQ_OPT:
    if (handle->connection == 0)
      return TCL_OK ; /* Return empty string if not connected. */
    break;
  default: /* should never happen */
    return mysql_prim_confl(interp,objc,objv,"weirdness in Mysqltcl_Info") ;
  }
  
  if (handle == 0) return TCL_ERROR ;

  /* Handle OK, return the requested info. */
  switch (idx) {
  case MYSQL_INFNAME_OPT:
  case MYSQL_INFNAMEQ_OPT:
    Tcl_SetObjResult(interp, Tcl_NewStringObj(handle->database, -1));
    break ;
  case MYSQL_INFTABLES_OPT:
    if ((list = mysql_list_tables(handle->connection,(char*)NULL)) == NULL)
      return mysql_server_confl(interp,objc,objv,handle->connection);
    
    resList = Tcl_GetObjResult(interp);
    for (count = mysql_num_rows(list); count > 0; count--) {
      val = *(row = mysql_fetch_row(list)) ;
      Tcl_ListObjAppendElement(interp, resList, Tcl_NewStringObj((val == NULL)?"":val,-1));
    }
    mysql_free_result(list) ;
    break ;
  case MYSQL_INFHOST_OPT:

  case MYSQL_INFHOSTQ_OPT:
    Tcl_SetObjResult(interp, Tcl_NewStringObj(mysql_get_host_info(handle->connection), -1));
    break ;
  case MYSQL_INFLIST_OPT:
    if ((list = mysql_list_dbs(handle->connection,(char*)NULL)) == NULL)
      return mysql_server_confl(interp,objc,objv,handle->connection);
    
    resList = Tcl_GetObjResult(interp);
    for (count = mysql_num_rows(list); count > 0; count--) {
      val = *(row = mysql_fetch_row(list)) ;
      Tcl_ListObjAppendElement(interp, resList,
				Tcl_NewStringObj((val == NULL)?"":val,-1));
    }
    mysql_free_result(list) ;
    break ;
  case MYSQL_INFO:
    val = mysql_info(handle->connection);
    if (val!=NULL) {
      Tcl_SetObjResult(interp, Tcl_NewStringObj(val,-1));      
    }
    break;
  case MYSQL_INF_SERVERVERSION:
     Tcl_SetObjResult(interp, Tcl_NewStringObj(mysql_get_server_info(handle->connection),-1));
     break;
#if (MYSQL_VERSION_ID >= 40107)
  case MYSQL_INFO_SERVERVERSION_ID:
	 Tcl_SetObjResult(interp, Tcl_NewIntObj(mysql_get_server_version(handle->connection)));
	 break;
  case MYSQL_INFO_SQLSTATE:
     Tcl_SetObjResult(interp, Tcl_NewStringObj(mysql_sqlstate(handle->connection),-1));
     break;
#endif
  case MYSQL_INFO_STATE:
     Tcl_SetObjResult(interp, Tcl_NewStringObj(mysql_stat(handle->connection),-1));
     break;
  default: /* should never happen */
    return mysql_prim_confl(interp,objc,objv,"weirdness in Mysqltcl_Info") ;
  }

  return TCL_OK ;
}




/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Result

 * Implements the mysqlresult command:
 * usage: mysqlresult handle option
 *
 */

static int Mysqltcl_Result(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  int idx ;
  MysqlTclHandle *handle;
  static CONST char* MysqlResultOpt[] =
    {
     "rows", "rows?", "cols", "cols?", "current", "current?", NULL
    };
  enum resultoption {
    MYSQL_RESROWS_OPT, MYSQL_RESROWSQ_OPT, MYSQL_RESCOLS_OPT, 
    MYSQL_RESCOLSQ_OPT, MYSQL_RESCUR_OPT, MYSQL_RESCURQ_OPT
  };
  /* We can't fully check the handle at this stage. */
  if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_PLAIN,
			    " handle option")) == 0)

    return TCL_ERROR;

  if (Tcl_GetIndexFromObj(interp, objv[2], MysqlResultOpt, "option",
                          TCL_EXACT, &idx) != TCL_OK)
    return TCL_ERROR;

  /* First check the handle. Checking depends on the option. */
  switch (idx) {
  case MYSQL_RESROWS_OPT:
  case MYSQL_RESCOLS_OPT:
  case MYSQL_RESCUR_OPT:
    handle = get_handle(interp,objc,objv,CL_RES) ;
    break ;
  case MYSQL_RESROWSQ_OPT:
  case MYSQL_RESCOLSQ_OPT:
  case MYSQL_RESCURQ_OPT:
    if ((handle = get_handle(interp,objc,objv,CL_RES))== NULL)
      return TCL_OK ; /* Return empty string if no pending result. */
    break ;
  default: /* should never happen */
    return mysql_prim_confl(interp,objc,objv,"weirdness in Mysqltcl_Result") ;
  }
  
  
  if (handle == 0)
    return TCL_ERROR ;

  /* Handle OK; return requested info. */
  switch (idx) {
  case MYSQL_RESROWS_OPT:
  case MYSQL_RESROWSQ_OPT:
    Tcl_SetObjResult(interp, Tcl_NewIntObj(handle->res_count));
    break ;
  case MYSQL_RESCOLS_OPT:
  case MYSQL_RESCOLSQ_OPT:
    Tcl_SetObjResult(interp, Tcl_NewIntObj(handle->col_count));
    break ;
  case MYSQL_RESCUR_OPT:
  case MYSQL_RESCURQ_OPT:
    Tcl_SetObjResult(interp,
                       Tcl_NewIntObj(mysql_num_rows(handle->result)
	                             - handle->res_count)) ;
    break ;
  default:
    return mysql_prim_confl(interp,objc,objv,"weirdness in Mysqltcl_Result");
  }
  return TCL_OK ;
}


/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Col

 *    Implements the mysqlcol command:
 *    usage: mysqlcol handle table-name option ?option ...?
 *           mysqlcol handle -current option ?option ...?
 * '-current' can only be used if there is a pending result.
 *	                
 *    results:
 *	List of lists containing column attributes.
 *      If a single attribute is requested the result is a simple list.
 *
 * SIDE EFFECT: '-current' disturbs the field position of the result.
 */

static int Mysqltcl_Col(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  int coln ;
  int current_db ;
  MysqlTclHandle *handle;
  int idx ;
  int listObjc ;
  Tcl_Obj **listObjv, *colinfo, *resList, *resSubList;
  MYSQL_FIELD* fld ;
  MYSQL_RES* result ;
  char *argv ;
  
  /* This check is enough only without '-current'. */
  if ((handle = mysql_prologue(interp, objc, objv, 4, 99, CL_CONN,
			    "handle table-name option ?option ...?")) == 0)
    return TCL_ERROR;

  /* Fetch column info.
   * Two ways: explicit database and table names, or current.
   */
  argv=Tcl_GetStringFromObj(objv[2],NULL);
  current_db = strcmp(argv, "-current") == 0;
  
  if (current_db) {
    if ((handle = get_handle(interp,objc,objv,CL_RES)) == 0)
      return TCL_ERROR ;
    else
      result = handle->result ;
  } else {
    if ((result = mysql_list_fields(handle->connection, argv, (char*)NULL)) == NULL) {
      return mysql_server_confl(interp,objc,objv,handle->connection) ;
    }
  }
  /* Must examine the first specifier at this point. */
  if (Tcl_ListObjGetElements(interp, objv[3], &listObjc, &listObjv) != TCL_OK)
    return TCL_ERROR ;
  resList = Tcl_GetObjResult(interp);
  if (objc == 4 && listObjc == 1) {
      mysql_field_seek(result, 0) ;
      while ((fld = mysql_fetch_field(result)) != NULL)
        if ((colinfo = mysql_colinfo(interp,objc,objv,fld, objv[3])) != NULL) {
            Tcl_ListObjAppendElement(interp, resList, colinfo);
        } else {
            goto conflict;
	    }
  } else if (objc == 4 && listObjc > 1) {
      mysql_field_seek(result, 0) ;
      while ((fld = mysql_fetch_field(result)) != NULL) {
        resSubList = Tcl_NewListObj(0, NULL);
        for (coln = 0; coln < listObjc; coln++)
            if ((colinfo = mysql_colinfo(interp,objc,objv,fld, listObjv[coln])) != NULL) {
                Tcl_ListObjAppendElement(interp, resSubList, colinfo);
            } else {

               goto conflict; 
            }
        Tcl_ListObjAppendElement(interp, resList, resSubList);
	}
  } else {
      for (idx = 3; idx < objc; idx++) {
        resSubList = Tcl_NewListObj(0, NULL);
        mysql_field_seek(result, 0) ;
        while ((fld = mysql_fetch_field(result)) != NULL)
        if ((colinfo = mysql_colinfo(interp,objc,objv,fld, objv[idx])) != NULL) {

            Tcl_ListObjAppendElement(interp, resSubList, colinfo);
        } else {
            goto conflict; 
        }
        Tcl_ListObjAppendElement(interp, resList, resSubList);
      }
  }
  if (!current_db) mysql_free_result(result) ;
  return TCL_OK;
  
  conflict:
    if (!current_db) mysql_free_result(result) ;
    return TCL_ERROR;
}


/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_State
 *    Implements the mysqlstate command:
 *    usage: mysqlstate handle ?-numeric?

 *	                
 */

static int Mysqltcl_State(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  MysqlTclHandle *handle;
  int numeric=0 ;
  Tcl_Obj *res;

  if (objc!=2 && objc!=3) {
      Tcl_WrongNumArgs(interp, 1, objv, "handle ?-numeric");
      return TCL_ERROR;
  }

  if (objc==3) {
    if (strcmp(Tcl_GetStringFromObj(objv[2],NULL), "-numeric"))
      return mysql_prim_confl(interp,objc,objv,"last parameter should be -numeric");
    else

      numeric=1;
  }
  
  if (GetHandleFromObj(interp, objv[1], &handle) != TCL_OK)
    res = (numeric)?Tcl_NewIntObj(0):Tcl_NewStringObj("NOT_A_HANDLE",-1);
  else if (handle->connection == 0)
    res = (numeric)?Tcl_NewIntObj(1):Tcl_NewStringObj("UNCONNECTED",-1);
  else if (handle->database[0] == '\0')
    res = (numeric)?Tcl_NewIntObj(2):Tcl_NewStringObj("CONNECTED",-1);
  else if (handle->result == NULL)
    res = (numeric)?Tcl_NewIntObj(3):Tcl_NewStringObj("IN_USE",-1);
  else
    res = (numeric)?Tcl_NewIntObj(4):Tcl_NewStringObj("RESULT_PENDING",-1);

  Tcl_SetObjResult(interp, res);
  return TCL_OK ;
}


/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_InsertId
 *    Implements the mysqlstate command:
 *    usage: mysqlinsertid handle 
 *    Returns the auto increment id of the last INSERT statement
 *	                
 */

static int Mysqltcl_InsertId(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{

  MysqlTclHandle *handle;
  
  if ((handle = mysql_prologue(interp, objc, objv, 2, 2, CL_CONN,
			    "handle")) == 0)
    return TCL_ERROR;

  Tcl_SetObjResult(interp, Tcl_NewIntObj(mysql_insert_id(handle->connection)));

  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Ping
 *    usage: mysqlping handle
 *    It can be used to check and refresh (reconnect after time out) the connection
 *    Returns 0 if connection is OK
 */


static int Mysqltcl_Ping(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  MysqlTclHandle *handle;
  
  if ((handle = mysql_prologue(interp, objc, objv, 2, 2, CL_CONN,
			    "handle")) == 0)
    return TCL_ERROR;

  Tcl_SetObjResult(interp, Tcl_NewBooleanObj(mysql_ping(handle->connection)==0));

  return TCL_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Commit
 *    usage: mysql::commit
 *    
 */

static int Mysqltcl_Commit(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
#if (MYSQL_VERSION_ID < 40107)
  Tcl_AddErrorInfo(interp, FUNCTION_NOT_AVAILABLE);
  return TCL_ERROR;
#else
  MysqlTclHandle *handle;

  if ((handle = mysql_prologue(interp, objc, objv, 2, 2, CL_CONN,
			    "handle")) == 0)
    return TCL_ERROR;
  if (mysql_commit(handle->connection)!=0) {
  	mysql_server_confl(interp,objc,objv,handle->connection);
  }
  return TCL_OK;
#endif
}
/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Rollback
 *    usage: mysql::rollback
 *
 */

static int Mysqltcl_Rollback(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
#if (MYSQL_VERSION_ID < 40107)
  Tcl_AddErrorInfo(interp, FUNCTION_NOT_AVAILABLE);
  return TCL_ERROR;
#else
  MysqlTclHandle *handle;

  if ((handle = mysql_prologue(interp, objc, objv, 2, 2, CL_CONN,
			    "handle")) == 0)
    return TCL_ERROR;
  if (mysql_rollback(handle->connection)!=0) {
      mysql_server_confl(interp,objc,objv,handle->connection);
  }
  return TCL_OK;
#endif
}

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_WarningCount
 *    usage: mysql::warningcount
 *
 */

static int Mysqltcl_WarningCount(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
#if (MYSQL_VERSION_ID < 40107)
  Tcl_AddErrorInfo(interp, FUNCTION_NOT_AVAILABLE);
  return TCL_ERROR;
#else
  MysqlTclHandle *handle;
  int count = 0;

  if ((handle = mysql_prologue(interp, objc, objv, 2, 2, CL_CONN,
			    "handle")) == 0)
    return TCL_ERROR;
  count = mysql_warning_count(handle->connection);
  Tcl_SetObjResult(interp,Tcl_NewIntObj(count));
  return TCL_OK;
#endif
}
/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_IsNull
 *    usage: mysql::isnull value
 *
 */

static int Mysqltcl_IsNull(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  int boolResult = 0;
  if (objc != 2) {
      Tcl_WrongNumArgs(interp, 1, objv, "value");
      return TCL_ERROR;
  }
  boolResult = objv[1]->typePtr == &mysqlNullType;
  Tcl_SetObjResult(interp,Tcl_NewBooleanObj(boolResult));
  return TCL_OK;
}

/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_SetServerOption
 *    usage: mysql::setserveroption (-
 *
 */
#if (MYSQL_VERSION_ID >= 40107)
static CONST char* MysqlServerOpt[] =
    {
      "-multi_statment_on", "-multi_statment_off", "-auto_reconnect_on", "-auto_reconnect_off", NULL
    };
#endif
 
static int Mysqltcl_SetServerOption(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
#if (MYSQL_VERSION_ID < 40107)
  Tcl_AddErrorInfo(interp, FUNCTION_NOT_AVAILABLE);
  return TCL_ERROR;
#else
  MysqlTclHandle *handle;
  int idx;
  enum enum_mysql_set_option mysqlServerOption;
  
  enum serveroption {
    MYSQL_MSTATMENT_ON_SOPT, MYSQL_MSTATMENT_OFF_SOPT
  };

  if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_CONN,
			    "handle option")) == 0)
    return TCL_ERROR;

  if (Tcl_GetIndexFromObj(interp, objv[2], MysqlServerOpt, "option",
                          0, &idx) != TCL_OK)
      return TCL_ERROR;

  switch (idx) {
    case MYSQL_MSTATMENT_ON_SOPT:
      mysqlServerOption = MYSQL_OPTION_MULTI_STATEMENTS_ON;
      break;
    case MYSQL_MSTATMENT_OFF_SOPT:
      mysqlServerOption = MYSQL_OPTION_MULTI_STATEMENTS_OFF;
      break;
    default:
      return mysql_prim_confl(interp,objc,objv,"Weirdness in server options");
  }
  if (mysql_set_server_option(handle->connection,mysqlServerOption)!=0) {
  	mysql_server_confl(interp,objc,objv,handle->connection);
  }
  return TCL_OK;
#endif
}
/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_ShutDown
 *    usage: mysql::shutdown handle
 *
 */
static int Mysqltcl_ShutDown(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  MysqlTclHandle *handle;

  if ((handle = mysql_prologue(interp, objc, objv, 2, 2, CL_CONN,
			    "handle")) == 0)
    return TCL_ERROR;
#if (MYSQL_VERSION_ID >= 40107)
  if (mysql_shutdown(handle->connection,SHUTDOWN_DEFAULT)!=0) {
#else
  if (mysql_shutdown(handle->connection)!=0) {
#endif
  	mysql_server_confl(interp,objc,objv,handle->connection);
  }
  return TCL_OK;
}


#ifdef PREPARED_STATEMENT
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

static int Mysqltcl_Prepare(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  MysqltclState *statePtr = (MysqltclState *)clientData;

  MysqlTclHandle *handle;
  MysqlTclHandle *shandle;
  MYSQL_STMT *statement;
  char *query;
  int queryLen;
  int resultColumns;
  int paramCount;

  if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_CONN,
			    "handle sql-statement")) == 0)
    return TCL_ERROR;

  statement = mysql_stmt_init(handle->connection);
  if (statement==NULL) {
  	return TCL_ERROR;
  }
  query = (char *)Tcl_GetByteArrayFromObj(objv[2], &queryLen);
  if (mysql_stmt_prepare(statement,query,queryLen)) {

  	mysql_stmt_close(statement);
    return mysql_server_confl(interp,objc,objv,handle->connection);
  }
  if ((shandle = createHandleFrom(statePtr,handle,HT_STATEMENT)) == NULL) return TCL_ERROR;
  shandle->statement=statement;
  shandle->resultMetadata = mysql_stmt_result_metadata(statement);
  shandle->paramMetadata = mysql_stmt_param_metadata(statement);
  /* set result bind memory */
  resultColumns = mysql_stmt_field_count(statement);
  if (resultColumns>0) {
  	shandle->bindResult = (MYSQL_BIND *)Tcl_Alloc(sizeof(MYSQL_BIND)*resultColumns);
    memset(shandle->bindResult,0,sizeof(MYSQL_BIND)*resultColumns);
  }
  paramCount = mysql_stmt_param_count(statement);
  if (resultColumns>0) {
  	shandle->bindParam = (MYSQL_BIND *)Tcl_Alloc(sizeof(MYSQL_BIND)*paramCount);
    memset(shandle->bindParam,0,sizeof(MYSQL_BIND)*paramCount);
  }
  Tcl_SetObjResult(interp, Tcl_NewHandleObj(statePtr,shandle));
  return TCL_OK;
}
static int Mysqltcl_ParamMetaData(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  MysqltclState *statePtr = (MysqltclState *)clientData;
  MysqlTclHandle *handle;
  MYSQL_RES *res;
  MYSQL_ROW row;
  Tcl_Obj *colinfo,*resObj;
  unsigned long *lengths;
  int i;
  int colCount;
  MYSQL_FIELD* fld;

  if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_CONN,
			    "statement-handle")) == 0)
    return TCL_ERROR;
  if(handle->type!=HT_STATEMENT)
  	return TCL_ERROR;

  resObj = Tcl_GetObjResult(interp);
  printf("statement %p count %d\n",handle->statement,mysql_stmt_param_count(handle->statement));
  res = mysql_stmt_result_metadata(handle->statement);
  printf("res %p\n",res);
  if(res==NULL)
  	return TCL_ERROR;

  mysql_field_seek(res, 0) ;
  while ((fld = mysql_fetch_field(res)) != NULL) {
        if ((colinfo = mysql_colinfo(interp,objc,objv,fld, objv[2])) != NULL) {
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

static int Mysqltcl_PSelect(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  MysqltclState *statePtr = (MysqltclState *)clientData;
  MysqlTclHandle *handle;

  if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_CONN,
			    "handle sql-statement")) == 0)
    return TCL_ERROR;
  if (handle->type!=HT_STATEMENT) {
  	return TCL_ERROR;
  }
  mysql_stmt_reset(handle->statement);
  if (mysql_stmt_execute(handle->statement)) {
  	return mysql_server_confl(interp,objc,objv,handle->connection);
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

static int Mysqltcl_PFetch(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  MysqltclState *statePtr = (MysqltclState *)clientData;
  MysqlTclHandle *handle;

  if ((handle = mysql_prologue(interp, objc, objv, 2, 2, CL_CONN,
			    "prep-stat-handle")) == 0)
    return TCL_ERROR;
  if (handle->type!=HT_STATEMENT) {
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

static int Mysqltcl_PExecute(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[])
{
  MysqltclState *statePtr = (MysqltclState *)clientData;
  MysqlTclHandle *handle;

  if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_CONN,
			    "handle sql-statement")) == 0)
    return TCL_ERROR;
  if (handle->type!=HT_STATEMENT) {
  	return TCL_ERROR;
  }
  mysql_stmt_reset(handle->statement);

  if (mysql_stmt_param_count(handle->statement)!=0) {
	  Tcl_SetStringObj(Tcl_GetObjResult(interp),"works only for 0 params",-1);
	  return TCL_ERROR;
  }
  if (mysql_stmt_execute(handle->statement))
  {
	Tcl_SetStringObj(Tcl_GetObjResult(interp),mysql_stmt_error(handle->statement),-1);
  	return TCL_ERROR;
  }
  return TCL_OK;
}
#endif

/*
 *----------------------------------------------------------------------
 * Mysqltcl_Init
 * Perform all initialization for the MYSQL to Tcl interface.
 * Adds additional commands to interp, creates message array, initializes
 * all handles.
 *
 * A call to Mysqltcl_Init should exist in Tcl_CreateInterp or
 * Tcl_CreateExtendedInterp.

 */


#ifdef _WINDOWS
__declspec( dllexport )
#endif
int Mysqltcl_Init(interp)
    Tcl_Interp *interp;
{
  char nbuf[MYSQL_SMALL_SIZE];
  MysqltclState *statePtr;
 
  if (Tcl_InitStubs(interp, "8.1", 0) == NULL)
    return TCL_ERROR;
  if (Tcl_PkgRequire(interp, "Tcl", "8.1", 0) == NULL)
    return TCL_ERROR;
  if (Tcl_PkgProvide(interp, "mysqltcl" , PACKAGE_VERSION) != TCL_OK)
    return TCL_ERROR;
  /*

   * Initialize the new Tcl commands.
   * Deleting any command will close all connections.
   */
   statePtr = (MysqltclState*)Tcl_Alloc(sizeof(MysqltclState)); 
   Tcl_InitHashTable(&statePtr->hash, TCL_STRING_KEYS);
   statePtr->handleNum = 0;

   Tcl_CreateObjCommand(interp,"mysqlconnect",Mysqltcl_Connect,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqluse", Mysqltcl_Use,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqlescape", Mysqltcl_Escape,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqlsel", Mysqltcl_Sel,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqlnext", Mysqltcl_Fetch,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqlseek", Mysqltcl_Seek,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqlmap", Mysqltcl_Map,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqlexec", Mysqltcl_Exec,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqlclose", Mysqltcl_Close,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqlinfo", Mysqltcl_Info,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqlresult", Mysqltcl_Result,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqlcol", Mysqltcl_Col,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqlstate", Mysqltcl_State,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqlinsertid", Mysqltcl_InsertId,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqlquery", Mysqltcl_Query,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqlendquery", Mysqltcl_EndQuery,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqlbaseinfo", Mysqltcl_BaseInfo,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqlping", Mysqltcl_Ping,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqlchangeuser", Mysqltcl_ChangeUser,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"mysqlreceive", Mysqltcl_Receive,(ClientData)statePtr, NULL);
   
   Tcl_CreateObjCommand(interp,"::mysql::connect",Mysqltcl_Connect,(ClientData)statePtr, Mysqltcl_Kill);
   Tcl_CreateObjCommand(interp,"::mysql::use", Mysqltcl_Use,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::escape", Mysqltcl_Escape,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::sel", Mysqltcl_Sel,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::fetch", Mysqltcl_Fetch,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::seek", Mysqltcl_Seek,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::map", Mysqltcl_Map,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::exec", Mysqltcl_Exec,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::close", Mysqltcl_Close,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::info", Mysqltcl_Info,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::result", Mysqltcl_Result,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::col", Mysqltcl_Col,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::state", Mysqltcl_State,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::insertid", Mysqltcl_InsertId,(ClientData)statePtr, NULL);
   /* new in mysqltcl 2.0 */
   Tcl_CreateObjCommand(interp,"::mysql::query", Mysqltcl_Query,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::endquery", Mysqltcl_EndQuery,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::baseinfo", Mysqltcl_BaseInfo,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::ping", Mysqltcl_Ping,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::changeuser", Mysqltcl_ChangeUser,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::receive", Mysqltcl_Receive,(ClientData)statePtr, NULL);
   /* new in mysqltcl 3.0 */
   Tcl_CreateObjCommand(interp,"::mysql::autocommit", Mysqltcl_AutoCommit,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::commit", Mysqltcl_Commit,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::rollback", Mysqltcl_Rollback,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::nextresult", Mysqltcl_NextResult,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::moreresult", Mysqltcl_MoreResult,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::warningcount", Mysqltcl_WarningCount,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::isnull", Mysqltcl_IsNull,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::newnull", Mysqltcl_NewNull,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::setserveroption", Mysqltcl_SetServerOption,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::shutdown", Mysqltcl_ShutDown,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::encoding", Mysqltcl_Encoding,(ClientData)statePtr, NULL);
   /* new in mysqltcl 4.0 */

   /* prepared statements */

#ifdef PREPARED_STATEMENT
   Tcl_CreateObjCommand(interp,"::mysql::prepare", Mysqltcl_Prepare,(ClientData)statePtr, NULL);
   // Tcl_CreateObjCommand(interp,"::mysql::parammetadata", Mysqltcl_ParamMetaData,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::pselect", Mysqltcl_PSelect,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::pselect", Mysqltcl_PFetch,(ClientData)statePtr, NULL);
   Tcl_CreateObjCommand(interp,"::mysql::pexecute", Mysqltcl_PExecute,(ClientData)statePtr, NULL);
#endif
   

   
   /* Initialize mysqlstatus global array. */
   
   clear_msg(interp);
  
   /* Link the null value element to the corresponding C variable. */
   if ((statePtr->MysqlNullvalue = Tcl_Alloc (12)) == NULL) return TCL_ERROR;
   strcpy (statePtr->MysqlNullvalue, MYSQL_NULLV_INIT);
   sprintf (nbuf, "%s(%s)", MYSQL_STATUS_ARR, MYSQL_STATUS_NULLV);

   /* set null object in mysqltcl state */
   /* statePtr->nullObjPtr = Mysqltcl_NewNullObj(statePtr); */
   
   if (Tcl_LinkVar(interp,nbuf,(char *)&statePtr->MysqlNullvalue, TCL_LINK_STRING) != TCL_OK)
     return TCL_ERROR;
   
   /* Register the handle object type */
   Tcl_RegisterObjType(&mysqlHandleType);
   /* Register own null type object */
   Tcl_RegisterObjType(&mysqlNullType);
   
   /* A little sanity check.
    * If this message appears you must change the source code and recompile.
   */
   if (strlen(MysqlHandlePrefix) == MYSQL_HPREFIX_LEN)
     return TCL_OK;
   else {
     panic("*** mysqltcl (mysqltcl.c): handle prefix inconsistency!\n");
     return TCL_ERROR ;
   }
}

#ifdef _WINDOWS
__declspec( dllexport )
#endif
int Mysqltcl_SafeInit(interp)
    Tcl_Interp *interp;
{
  return Mysqltcl_Init(interp);
}

