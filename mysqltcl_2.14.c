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
#ifdef _WINDOWS
   #include <windows.h>
   #define PACKAGE "mysqltcl"
   #define VERSION "2.14"
#endif

#include <tcl.h>
#include <mysql.h>

#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>


/* A few macros for making the code more readable */

#define DECLARE_CMD(func) \
static int func _ANSI_ARGS_((ClientData clientData, \
		   Tcl_Interp *interp, int objc, Tcl_Obj *CONST objv[]))

#define DEFINE_CMD(func) \
static int func(clientData, interp, objc, objv) \
    ClientData clientData; \
    Tcl_Interp *interp;	\
    int objc; \
    Tcl_Obj *CONST objv[];

#define ADD_CMD(cmdName, cmdProc) \
Tcl_CreateObjCommand(interp, #cmdName, cmdProc, NULL, Mysqltcl_Kill)

/* Compile-time constants */

#define MYSQL_SMALL_SIZE  TCL_RESULT_SIZE /* Smaller buffer size. */
#define MYSQL_NAME_LEN     80    /* Max. host, database name length. */

typedef struct MysqlTclHandle {
  MYSQL * connection ;         /* Connection handle, if connected; -1 otherwise. */
  char host[MYSQL_NAME_LEN] ;      /* Host name, if connected. */
  char database[MYSQL_NAME_LEN] ;  /* Db name, if selected; NULL otherwise. */
  MYSQL_RES* result ;              /* Stored result, if any; NULL otherwise. */
  int res_count ;                 /* Count of unfetched rows in result. */
  int col_count ;                 /* Column count in result, if any. */
  int number;
  int isquery;
} MysqlTclHandle;

/* one global Hash for mysql handles */
static Tcl_HashTable handleTable;

static char *MysqlHandlePrefix = "mysql";
/* Prefix string used to identify handles.
 * The following must be strlen(MysqlHandlePrefix).
 */
#define MYSQL_HPREFIX_LEN 5

/* Array for status info, and its elements. */
#define MYSQL_STATUS_ARR "mysqlstatus"

#define MYSQL_STATUS_CODE "code"
#define MYSQL_STATUS_CMD  "command"
#define MYSQL_STATUS_MSG  "message"
#define MYSQL_STATUS_NULLV  "nullvalue"

/* C variable corresponding to mysqlstatus(nullvalue) */
static char *MysqlNullvalue = NULL ;
#define MYSQL_NULLV_INIT ""

/* Check Level for mysql_prologue */
#define CL_PLAIN 0
#define CL_CONN  1
#define CL_DB    2
#define CL_RES 3

/* Prototypes for all functions. */

DECLARE_CMD(Mysqltcl_Connect);
DECLARE_CMD(Mysqltcl_Use);
DECLARE_CMD(Mysqltcl_Escape);
DECLARE_CMD(Mysqltcl_Sel);
DECLARE_CMD(Mysqltcl_Next);
DECLARE_CMD(Mysqltcl_Seek);
DECLARE_CMD(Mysqltcl_Map);
DECLARE_CMD(Mysqltcl_Exec);
DECLARE_CMD(Mysqltcl_Close);
DECLARE_CMD(Mysqltcl_Info);
DECLARE_CMD(Mysqltcl_Result);
DECLARE_CMD(Mysqltcl_Col);
DECLARE_CMD(Mysqltcl_State);
DECLARE_CMD(Mysqltcl_InsertId);
DECLARE_CMD(Mysqltcl_Query);

static int MysqlHandleSet _ANSI_ARGS_((Tcl_Interp *interp,
           Tcl_Obj *objPtr));
static void MysqlHandleFree _ANSI_ARGS_((Tcl_Obj *objPtr));


/* handle object type 
 * This section defince funtions for Handling new Tcl_Obj type */
  
Tcl_ObjType mysqlHandleType = {
    "mysqlhandle", 
    MysqlHandleFree,
    (Tcl_DupInternalRepProc *) NULL,
    NULL,
    MysqlHandleSet
};

static int
MysqlHandleSet(interp, objPtr)
    Tcl_Interp *interp;		/* Used for error reporting if not NULL. */
    register Tcl_Obj *objPtr;	/* The object to convert. */
{
    Tcl_ObjType *oldTypePtr = objPtr->typePtr;
    char *string;
    MysqlTclHandle *handle;
    Tcl_HashEntry *entryPtr;

    string=Tcl_GetStringFromObj(objPtr, NULL);  
    entryPtr = Tcl_FindHashEntry(&handleTable,string);
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
    // printf("p set obj handle %i %x\n",handle->isquery,handle);

    return TCL_OK;
}
static void
MysqlHandleFree(Tcl_Obj *obj)
{
  MysqlTclHandle *handle = (MysqlTclHandle *)obj->internalRep.otherValuePtr;
  Tcl_Release((char *)handle);
  // printf("r free obj handle %i %x\n",handle->isquery,handle);
}

static int
GetHandleFromObj(interp, objPtr, handlePtr)
    Tcl_Interp *interp;
    register Tcl_Obj *objPtr;
    register MysqlTclHandle **handlePtr;
{
    if (Tcl_ConvertToType (interp, objPtr, &mysqlHandleType) != TCL_OK)
        return TCL_ERROR;
    *handlePtr = (MysqlTclHandle *)objPtr->internalRep.otherValuePtr;
    return TCL_OK;
}

static Tcl_Obj *
Tcl_NewHandleObj(handle)
    register MysqlTclHandle* handle;
{
    register Tcl_Obj *objPtr;
    char buffer[MYSQL_HPREFIX_LEN+TCL_DOUBLE_SPACE+1];
    register int len;
    Tcl_HashEntry *entryPtr;
    int newflag;

    objPtr=Tcl_NewObj();
    /* the string for "query" can not be longer as MysqlHandlePrefix see buf variable */
    len=sprintf(buffer, "%s%d", (handle->isquery) ? "query" : MysqlHandlePrefix,handle->number);    
    objPtr->bytes = Tcl_Alloc((unsigned) len + 1);
    strcpy(objPtr->bytes, buffer);
    objPtr->length = len;
    
    entryPtr=Tcl_CreateHashEntry(&handleTable,buffer,&newflag);
    Tcl_SetHashValue(entryPtr,handle);     
  
    objPtr->internalRep.otherValuePtr = handle;
    objPtr->typePtr = &mysqlHandleType;

    Tcl_Preserve((char *)handle);  
    // printf("p new obj handle %i %x\n",handle->isquery,handle);

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

static void 
set_statusArr(Tcl_Interp *interp,char *elem_name,Tcl_Obj *tobj)
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

static void
mysql_reassemble (Tcl_Interp *interp,int objc,Tcl_Obj *CONST objv[])
{
   set_statusArr(interp,MYSQL_STATUS_CMD,Tcl_NewListObj(objc, objv));
}


/*
 *----------------------------------------------------------------------
 * mysql_prim_confl
 * Conflict handling after a primitive conflict.
 *
 */

static int
mysql_prim_confl (Tcl_Interp *interp,int objc,Tcl_Obj *CONST objv[],char *msg)
{
  set_statusArr(interp,MYSQL_STATUS_CODE,Tcl_NewIntObj(-1));

  Tcl_ResetResult (interp) ;
  Tcl_AppendStringsToObj (Tcl_GetObjResult(interp),
                          Tcl_GetString(objv[0]), ": ", msg, (char*)NULL);

  set_statusArr(interp,MYSQL_STATUS_MSG,Tcl_GetObjResult(interp));

  mysql_reassemble (interp,objc,objv) ;
  return TCL_ERROR ;
}


/*
 *----------------------------------------------------------------------
 * mysql_server_confl
 * Conflict handling after an mySQL conflict.
 *
 */

static int
mysql_server_confl (Tcl_Interp *interp,int objc,Tcl_Obj *CONST objv[],MYSQL * connection)
{
  char* mysql_errorMsg;

  mysql_errorMsg = mysql_error(connection);

  set_statusArr(interp,MYSQL_STATUS_CODE,Tcl_NewIntObj(mysql_errno(connection)));

  Tcl_ResetResult (interp) ;
  Tcl_AppendStringsToObj (Tcl_GetObjResult(interp),
                          Tcl_GetString(objv[0]), "/db server: ",
		          (mysql_errorMsg == NULL) ? "" : mysql_errorMsg,
                          (char*)NULL) ;

  set_statusArr(interp,MYSQL_STATUS_MSG,Tcl_GetObjResult(interp));

  mysql_reassemble (interp,objc,objv) ;
  return TCL_ERROR ;
}

static  MysqlTclHandle *
get_handle (Tcl_Interp *interp,int objc,Tcl_Obj *CONST objv[],int check_level) 
{
  MysqlTclHandle *handle;
  if (GetHandleFromObj(interp, objv[1], &handle) != TCL_OK) {
    mysql_prim_confl (interp,objc,objv,"not mysqltcl handle") ;
    return NULL;
  }
  if (check_level==CL_PLAIN) return handle;
  if (handle->connection == 0) {
      mysql_prim_confl (interp,objc,objv,"handle already closed (dangling pointer)") ;
      return NULL;
  }
  if (check_level==CL_CONN) return handle;
  if (check_level!=CL_RES) {
    if (handle->database[0] == '\0') {
      mysql_prim_confl (interp,objc,objv,"no current database") ;
      return NULL;
    }
    if (check_level==CL_DB) return handle;
  }
  if (handle->result == NULL) {
      mysql_prim_confl (interp,objc,objv,"no result pending") ;
      return NULL;
  }
  return handle;
}

/*----------------------------------------------------------------------
 * mysql_QueryTclObj
 * getRowCellAsObject
 * This to method control how tcl data is transfered to mysql and
 * how data is imported into tcl from mysql
 */
static int mysql_QueryTclObj(MysqlTclHandle *handle,Tcl_Obj *obj)
{
  char *query;
  int result,queryLen;
  Tcl_DString queryDS;

  query=Tcl_GetStringFromObj(obj, &queryLen);
  Tcl_UtfToExternalDString(NULL, query, -1, &queryDS);
  queryLen = Tcl_DStringLength(&queryDS); 
  result =  mysql_real_query(handle->connection,Tcl_DStringValue(&queryDS),queryLen);
  Tcl_DStringFree(&queryDS);
  return result;
} 
static Tcl_Obj *getRowCellAsObject(MYSQL_ROW row,int length) 
{
  Tcl_Obj *obj;
  if (*row) {
    obj = Tcl_NewByteArrayObj(*row,length);
  } else {
    obj = Tcl_NewStringObj(MysqlNullvalue,-1);
  } 
  return obj;
}
 
static MysqlTclHandle *createMysqlHandle() 
{
  static int HandleNum=0;
  MysqlTclHandle *handle;
  handle=(MysqlTclHandle *)Tcl_Alloc(sizeof(MysqlTclHandle));
  if (handle == 0) {
    panic("no memory for handle");
    return handle;
  }
  handle->connection = (MYSQL *)0 ;
  handle->host[0] = '\0' ;
  handle->database[0] = '\0' ;
  handle->result = NULL ;
  handle->res_count = 0 ;
  handle->col_count = 0 ;
  handle->isquery = 0;

  //printf("p create handle %i %x\n",handle->isquery,handle);

  /* not MT-safe, static  */
  handle->number=HandleNum++;
  return handle;
}

static MysqlTclHandle *createQueryHandleFrom(MysqlTclHandle *handle)
{
  int number;
  MysqlTclHandle *qhandle;
  qhandle = createMysqlHandle();
  number = qhandle->number;
  if (!qhandle) return qhandle;
  memcpy(qhandle,handle,sizeof(MysqlTclHandle));
  qhandle->isquery=1;
  qhandle->number=number;
  return qhandle;
}
static void closeHandle(MysqlTclHandle *handle)
{
  handle->connection = (MYSQL *)0;
  // printf("r close handle %i %x\n",handle->isquery,handle);
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

static MysqlTclHandle *
mysql_prologue (interp, objc, objv, req_min_args, req_max_args, check_level, usage_msg)
     Tcl_Interp *interp;
     int         objc;
     Tcl_Obj *CONST objv[];
     int         req_min_args;
     int         req_max_args;
     int check_level;
     char *usage_msg;
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

static Tcl_Obj *
mysql_colinfo (interp,objc,objv,fld,keyw)
     Tcl_Interp *interp;
     int         objc;
     Tcl_Obj *CONST objv[];
     MYSQL_FIELD* fld ;
     Tcl_Obj * keyw ;
{
  char buf[MYSQL_SMALL_SIZE];
  int idx ;

  static CONST char* MysqlColkey[] =
    {
      "table", "name", "type", "length", "prim_key", "non_null", "numeric", "decimals", NULL
    };
  enum coloptions {
    MYSQL_COL_TABLE_K, MYSQL_COL_NAME_K, MYSQL_COL_TYPE_K, MYSQL_COL_LENGTH_K, MYSQL_COL_PRIMKEY_K,
    MYSQL_COL_NONNULL_K, MYSQL_COL_NUMERIC_K, MYSQL_COL_DECIMALS_K};

  if (Tcl_GetIndexFromObj(interp, keyw, (char **)MysqlColkey, "option",
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
	  return Tcl_NewStringObj("enum", -1); /* fyll p忿? */
	case FIELD_TYPE_SET: /* samma */
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
	  return Tcl_NewStringObj("string", -1) ;
	default:
	  sprintf (buf, "column '%s' has weird datatype", fld->name) ;
          mysql_prim_confl (interp,objc,objv,buf) ;
	  return NULL ;
	}
      break ;
    case MYSQL_COL_LENGTH_K:
      return Tcl_NewIntObj(fld->length) ;
    case MYSQL_COL_PRIMKEY_K:
      return Tcl_NewBooleanObj(IS_PRI_KEY(fld->flags)) ;
    case MYSQL_COL_NONNULL_K:
      return Tcl_NewBooleanObj(IS_NOT_NULL(fld->flags)) ;
    case MYSQL_COL_NUMERIC_K:
      return Tcl_NewBooleanObj(IS_NUM(fld->type));
    case MYSQL_COL_DECIMALS_K:
      return IS_NUM(fld->type)? Tcl_NewIntObj(fld->decimals): Tcl_NewIntObj(-1);
    default: /* should never happen */
      mysql_prim_confl (interp,objc,objv,"weirdness in mysql_colinfo") ;
      return NULL ;
    }
}


/*
 *----------------------------------------------------------------------
 * Mysqltcl_Kill
 * Close all connections.
 *
 */

static void
Mysqltcl_Kill (clientData)
    ClientData clientData;
{
  Tcl_HashEntry *entryPtr;
  Tcl_HashSearch search;
  MysqlTclHandle *handle;
  int wasdeleted=0;

  for (entryPtr=Tcl_FirstHashEntry(&handleTable,&search); 
       entryPtr!=NULL;
       entryPtr=Tcl_NextHashEntry(&search)) {
    wasdeleted=1;
    handle=(MysqlTclHandle *)Tcl_GetHashValue(entryPtr);

    if (handle->connection == 0) continue;

    if (handle->result != NULL)
      mysql_free_result (handle->result) ;

    mysql_close (handle->connection);
    closeHandle(handle);
  }
  if (wasdeleted) {
    Tcl_DeleteHashTable(&handleTable);
    Tcl_InitHashTable (&handleTable, TCL_STRING_KEYS);
  }
}




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

DEFINE_CMD(Mysqltcl_Connect)
{
  int        i, idx;
  char *hostname = NULL;
  char *user = NULL;
  char *password = NULL;
  char *db = NULL;
  int port = 0;
  char *socket = NULL;
  MysqlTclHandle *handle;
  const char *groupname = "mysqltcl";

  static CONST char* MysqlConnectOpt[] =
    {
      "-host", "-user", "-password", "-db", "-port", "-socket", NULL
    };
  enum connectoption {
    MYSQL_CONNHOST_OPT, MYSQL_CONNUSER_OPT, MYSQL_CONNPASSWORD_OPT, 
    MYSQL_CONNDB_OPT, MYSQL_CONNPORT_OPT, MYSQL_CONNSOCKET_OPT
  };
  if (!(objc & 1) || 
    objc>(sizeof(MysqlConnectOpt)/sizeof(MysqlConnectOpt[0]-1)*2+1)) {
    Tcl_WrongNumArgs(interp, 1, objv, "[-user xxx] [-db mysql] [-port 3306] [-host localhost] [-socket sock] [-password pass]");
	return TCL_ERROR;
  }
              
  for (i = 1; i < objc; i++) {
    if (Tcl_GetIndexFromObj(interp, objv[i], (char **)MysqlConnectOpt, "option",
                          0, &idx) != TCL_OK)
    return TCL_ERROR;
    
    switch (idx)
        {
        case MYSQL_CONNHOST_OPT:
            hostname = Tcl_GetStringFromObj(objv[++i],NULL);
            break;
        case MYSQL_CONNUSER_OPT:
            user = Tcl_GetStringFromObj(objv[++i],NULL);
            break;
        case MYSQL_CONNPASSWORD_OPT:
            password = Tcl_GetStringFromObj(objv[++i],NULL);
            break;
        case MYSQL_CONNDB_OPT:
            db = Tcl_GetStringFromObj(objv[++i],NULL);
            break;
        case MYSQL_CONNPORT_OPT:
            if(Tcl_GetIntFromObj(interp, objv[++i], &port) != TCL_OK)
                return TCL_ERROR;
            break;
        case MYSQL_CONNSOCKET_OPT:
            socket = Tcl_GetStringFromObj(objv[++i],NULL);
            break;
        default:
	        return mysql_prim_confl(interp,objc,objv,"Weirdness in options");            
        }
  }

  handle = createMysqlHandle();

  if (handle == 0) {
    panic("no memory for handle");
    return TCL_ERROR;
  }

  handle->connection = mysql_init(NULL);

  mysql_options(handle->connection,MYSQL_READ_DEFAULT_GROUP,groupname);

  if (!mysql_real_connect (handle->connection, hostname, user,
                                password, db, port, socket, 0)) {
      mysql_server_confl (interp,objc,objv,handle->connection);
      mysql_close (handle->connection);
      closeHandle(handle);
      return TCL_ERROR;
  }

  if (hostname) {
    strncpy (handle->host, hostname, MYSQL_NAME_LEN) ;
    handle->host[MYSQL_NAME_LEN - 1] = '\0' ;
  } else {
    strcpy (handle->host, "localhost");
  }

  if (db) {
    strncpy (handle->database, db, MYSQL_NAME_LEN) ;
    handle->database[MYSQL_NAME_LEN - 1] = '\0' ;
  }

  Tcl_SetObjResult(interp, Tcl_NewHandleObj(handle));

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

DEFINE_CMD(Mysqltcl_Use)
{
  int len;
  char *db;
  MysqlTclHandle *handle;  

  if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_CONN,
			    "handle dbname")) == 0)
    return TCL_ERROR;

  db=Tcl_GetStringFromObj(objv[2], &len);
  if (len >= MYSQL_NAME_LEN)
    return mysql_prim_confl (interp,objc,objv,"database name too long") ;
  if (mysql_select_db (handle->connection, db) < 0)
    return mysql_server_confl (interp,objc,objv,handle->connection) ;

  strcpy (handle->database, db) ;
  return TCL_OK;
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

DEFINE_CMD(Mysqltcl_Escape)
{
  int len;
  char *inString, *outString;
  
  if (objc != 2) {
      Tcl_WrongNumArgs(interp, 2, objv, "string");
      return TCL_ERROR;
  }
  /* !!! here the real_escape command should be used 
     this need a additional parameter connection */

  inString=Tcl_GetStringFromObj(objv[1], &len);
  outString=Tcl_Alloc((len<<1) + 1);
  len=mysql_escape_string(outString, inString, len);
  Tcl_SetStringObj(Tcl_GetObjResult(interp), outString, len);
  Tcl_Free(outString);
  return TCL_OK;
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

DEFINE_CMD(Mysqltcl_Sel)
{
  Tcl_Obj *res, *resList;
  MYSQL_ROW row;
  MysqlTclHandle *handle;
  unsigned long *lengths;
  static char* selOptions[] = {"-list", "-flatlist", NULL};
  /* Warning !! no option number */
  int i,selOption=2,colCount;
  
  if ((handle = mysql_prologue(interp, objc, objv, 3, 4, CL_CONN,
			    "handle sel-query ?-list|-flatlist?")) == 0)
    return TCL_ERROR;


  if (objc==4) {
    if (Tcl_GetIndexFromObj(interp, objv[3], (char **)selOptions, "option",
			    TCL_EXACT, &selOption) != TCL_OK)
      return TCL_ERROR;
  }
       
  /* Flush any previous result. */
  if (handle->result != NULL) {
    mysql_free_result (handle->result) ;
    handle->result = NULL ;
  }

  if (mysql_QueryTclObj(handle,objv[2])) {
    return mysql_server_confl (interp,objc,objv,handle->connection);
  }

  if ((handle->result = mysql_store_result (handle->connection)) == NULL) {
    if (selOption==2) Tcl_SetObjResult(interp, Tcl_NewIntObj(-1));
  } else {
    colCount = handle->col_count = mysql_num_fields (handle->result) ;
    res = Tcl_GetObjResult(interp);
    handle->res_count = 0;
    switch (selOption) {
    case 0: /* -list */
      while ((row = mysql_fetch_row (handle->result)) != NULL) {
	resList = Tcl_NewListObj(0, NULL);
	lengths = mysql_fetch_lengths(handle->result);
	for (i=0; i< colCount; i++, row++) {
	  Tcl_ListObjAppendElement (interp, resList,getRowCellAsObject(row,lengths[i]));
	}
	Tcl_ListObjAppendElement (interp, res, resList);
      }  
      break;
    case 1: /* -flatlist */
      while ((row = mysql_fetch_row (handle->result)) != NULL) {
	lengths = mysql_fetch_lengths(handle->result);
	for (i=0; i< colCount; i++, row++) {
	  Tcl_ListObjAppendElement (interp, res,getRowCellAsObject(row,lengths[i]));
	}
      }  
      break;
    case 2: /* No option */
      handle->res_count = mysql_num_rows (handle->result);
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
DEFINE_CMD(Mysqltcl_Query)
{
  MYSQL_RES *result;
  MysqlTclHandle *handle, *qhandle;
  
  if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_CONN,
			    "handle sqlstatement")) == 0)
    return TCL_ERROR;
       
  if (mysql_QueryTclObj(handle,objv[2])) {
    return mysql_server_confl (interp,objc,objv,handle->connection);
  }

  if ((result = mysql_store_result (handle->connection)) == NULL) {
    Tcl_SetObjResult(interp, Tcl_NewIntObj(-1));
    return TCL_OK;
  } 
  if ((qhandle = createQueryHandleFrom(handle)) == NULL) return TCL_ERROR;
  qhandle->result = result;
  qhandle->col_count = mysql_num_fields (qhandle->result) ;
  qhandle->res_count = mysql_num_rows (qhandle->result);
  Tcl_SetObjResult(interp, Tcl_NewHandleObj(qhandle));
  return TCL_OK;
}
DEFINE_CMD(Mysqltcl_EndQuery)
{
  Tcl_HashEntry *entryPtr;
  MysqlTclHandle *handle;
  
  if ((handle = mysql_prologue(interp, objc, objv, 2, 2, CL_CONN,
			    "queryhanle")) == 0)
    return TCL_ERROR;

  if (handle->result != NULL) {
    mysql_free_result (handle->result) ;
    handle->result = NULL ;
  }
  if (handle->isquery) {
    entryPtr = Tcl_FindHashEntry(&handleTable,Tcl_GetStringFromObj(objv[1],NULL));
    if (entryPtr) {
      Tcl_DeleteHashEntry(entryPtr);
    }
    closeHandle(handle);
  }
  return TCL_OK;
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

DEFINE_CMD(Mysqltcl_Exec)
{
  MysqlTclHandle *handle;
  int affected;

  if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_CONN,
			    "handle sql-statement")) == 0)
    return TCL_ERROR;

  /* Flush any previous result. */
  if (handle->result != NULL) {
      mysql_free_result (handle->result) ;
      handle->result = NULL ;
  }

  if (mysql_QueryTclObj(handle,objv[2]))
    return mysql_server_confl (interp,objc,objv,handle->connection);

  if ((affected=mysql_affected_rows(handle->connection)) < 0) affected=0;
  Tcl_SetIntObj(Tcl_GetObjResult(interp),affected);  
  return TCL_OK ;
}


/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Next
 *    Implements the mysqlnext command:
 *    usage: mysqlnext handle
 *	                
 *    results:
 *	next row from pending results as tcl list, or null list.
 */

DEFINE_CMD(Mysqltcl_Next)
{
  MysqlTclHandle *handle;
  int idx ;
  MYSQL_ROW row ;
  Tcl_Obj *resList;
  unsigned long *lengths;

  if ((handle = mysql_prologue(interp, objc, objv, 2, 2, CL_RES,
			    "handle")) == 0)
    return TCL_ERROR;

  
  if (handle->res_count == 0)
    return TCL_OK ;
  else if ((row = mysql_fetch_row (handle->result)) == NULL) {
    handle->res_count = 0 ;
    return mysql_prim_confl (interp,objc,objv,"result counter out of sync") ;
  } else
    handle->res_count-- ;
  
  lengths = mysql_fetch_lengths(handle->result);

  resList = Tcl_GetObjResult(interp);
  for (idx = 0 ; idx < handle->col_count ; idx++, row++) {
    Tcl_ListObjAppendElement (interp, resList,getRowCellAsObject(row,lengths[idx]));
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

DEFINE_CMD(Mysqltcl_Seek)
{
    MysqlTclHandle *handle;
    int row;
    int total;
   
    if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_RES,
                              " handle row-index")) == 0)
      return TCL_ERROR;

    if (Tcl_GetIntFromObj (interp, objv[2], &row) != TCL_OK)
      return TCL_ERROR;
    
    total = mysql_num_rows (handle->result);
    
    if (total + row < 0) {
      mysql_data_seek (handle->result, 0);
      handle->res_count = total;
    } else if (row < 0) {
      mysql_data_seek (handle->result, total + row);
      handle->res_count = -row;
    } else if (row >= total) {
      mysql_data_seek (handle->result, row);
      handle->res_count = 0;
    } else {
      mysql_data_seek (handle->result, row);
      handle->res_count = total - row;
    }

    Tcl_SetObjResult(interp, Tcl_NewIntObj(handle->res_count)) ;
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

DEFINE_CMD(Mysqltcl_Map)
{
  int code ;
  int count ;
  MysqlTclHandle *handle;
  int idx ;
  int listObjc ;
  Tcl_Obj** listObjv ;
  MYSQL_ROW row ;
  int *val;
  unsigned long *lengths;
  
  
  if ((handle = mysql_prologue(interp, objc, objv, 4, 4, CL_RES,
			    "handle binding-list script")) == 0)
    return TCL_ERROR;

  if (Tcl_ListObjGetElements (interp, objv[2], &listObjc, &listObjv) != TCL_OK)
    return TCL_ERROR ;
  
  if (listObjc > handle->col_count)
    {
      return mysql_prim_confl (interp,objc,objv,"too many variables in binding list") ;
    }
  else
    count = (listObjc < handle->col_count)?listObjc
      :handle->col_count ;
  
  val=(int*)Tcl_Alloc((count * sizeof (int)));
  for (idx=0; idx<count; idx++) {
    if (Tcl_GetStringFromObj(listObjv[idx],0)[0] != '-')
        val[idx]=1;
    else
        val[idx]=0;
  }
  
  while (handle->res_count > 0) {
    /* Get next row, decrement row counter. */
    if ((row = mysql_fetch_row (handle->result)) == NULL) {
      handle->res_count = 0 ;
      Tcl_Free((char *)val);
      return mysql_prim_confl (interp,objc,objv,"result counter out of sync") ;
    } else
      handle->res_count-- ;
      
    /* Bind variables to column values. */
    for (idx = 0; idx < count; idx++, row++) {
      lengths = mysql_fetch_lengths(handle->result);
      if (val[idx]) {
	if (Tcl_ObjSetVar2 (interp, 
			    listObjv[idx], NULL,getRowCellAsObject(row,lengths[idx]),
			    TCL_LEAVE_ERR_MSG) == NULL) {
	  Tcl_Free((char *)val);
	  return TCL_ERROR ;
	}
      }
    }

    /* Evaluate the script. */
    switch(code=Tcl_EvalObjEx(interp, objv[3],0)) {
    case TCL_CONTINUE:
    case TCL_OK:
      break ;
    case TCL_BREAK:
      Tcl_Free((char *)val);
      return TCL_OK ;
    default:
      Tcl_Free((char *)val);
      return code ;
    }
  }
  Tcl_Free((char *)val);
  return TCL_OK ;
}


/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Info
 * Implements the mysqlinfo command:
 * usage: mysqlinfo handle option
 *
 */

DEFINE_CMD(Mysqltcl_Info)
{
  int count ;
  MysqlTclHandle *handle;
  int idx ;
  MYSQL_RES* list ;
  MYSQL_ROW row ;
  char* val ;
  Tcl_Obj *resList;
  static CONST char* MysqlDbOpt[] =
    {
      "dbname", "dbname?", "tables", "host", "host?", "databases","info", NULL
    };
  enum dboption {
    MYSQL_INFNAME_OPT, MYSQL_INFNAMEQ_OPT, MYSQL_INFTABLES_OPT,
    MYSQL_INFHOST_OPT, MYSQL_INFHOSTQ_OPT, MYSQL_INFLIST_OPT, MYSQL_INFO
  };
  
  /* We can't fully check the handle at this stage. */
  if ((handle = mysql_prologue(interp, objc, objv, 3, 3, CL_PLAIN,
			    "handle option")) == 0)
    return TCL_ERROR;

  if (Tcl_GetIndexFromObj(interp, objv[2], (char **)MysqlDbOpt, "option",
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
    break ;
  case MYSQL_INFHOSTQ_OPT:
    if (handle->connection == 0)
      return TCL_OK ; /* Return empty string if not connected. */
    break ;
  case MYSQL_INFO:
    if (handle->connection == 0)
      return TCL_OK ; /* Return empty string if not connected. */
    break;
  default: /* should never happen */
    return mysql_prim_confl (interp,objc,objv,"weirdness in Mysqltcl_Info") ;
  }
  
  if (handle == 0) return TCL_ERROR ;

  /* Handle OK, return the requested info. */
  switch (idx) {
  case MYSQL_INFNAME_OPT:
  case MYSQL_INFNAMEQ_OPT:
    Tcl_SetObjResult(interp, Tcl_NewStringObj(handle->database, -1));
    break ;
  case MYSQL_INFTABLES_OPT:
    if ((list = mysql_list_tables (handle->connection,(char*)NULL)) == NULL)
      return mysql_server_confl (interp,objc,objv,handle->connection);
    
    resList = Tcl_GetObjResult(interp);
    for (count = mysql_num_rows (list); count > 0; count--) {
      val = *(row = mysql_fetch_row (list)) ;
      Tcl_ListObjAppendElement (interp, resList, Tcl_NewStringObj((val == NULL)?"":val,-1));
    }
    mysql_free_result (list) ;
    break ;
  case MYSQL_INFHOST_OPT:
  case MYSQL_INFHOSTQ_OPT:
    Tcl_SetObjResult(interp, Tcl_NewStringObj(handle->host, -1));
    break ;
  case MYSQL_INFLIST_OPT:
    if ((list = mysql_list_dbs (handle->connection,(char*)NULL)) == NULL)
      return mysql_server_confl (interp,objc,objv,handle->connection);
    
    resList = Tcl_GetObjResult(interp);
    for (count = mysql_num_rows (list); count > 0; count--) {
      val = *(row = mysql_fetch_row (list)) ;
      Tcl_ListObjAppendElement (interp, resList,
				Tcl_NewStringObj((val == NULL)?"":val,-1));
    }
    mysql_free_result (list) ;
    break ;
  case MYSQL_INFO:
    val = mysql_info(handle->connection);
    if (val!=NULL) {
      Tcl_SetObjResult(interp, Tcl_NewStringObj(val,-1));      
    }
    break;
  default: /* should never happen */
    return mysql_prim_confl (interp,objc,objv,"weirdness in Mysqltcl_Info") ;
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

DEFINE_CMD(Mysqltcl_Result)
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

  if (Tcl_GetIndexFromObj(interp, objv[2], (char **)MysqlResultOpt, "option",
                          TCL_EXACT, &idx) != TCL_OK)
    return TCL_ERROR;

  /* First check the handle. Checking depends on the option. */
  switch (idx) {
  case MYSQL_RESROWS_OPT:
  case MYSQL_RESCOLS_OPT:
  case MYSQL_RESCUR_OPT:
    handle = get_handle (interp,objc,objv,CL_RES) ;
    break ;
  case MYSQL_RESROWSQ_OPT:
  case MYSQL_RESCOLSQ_OPT:
  case MYSQL_RESCURQ_OPT:
    if ((handle = get_handle (interp,objc,objv,CL_RES))== NULL)
      return TCL_OK ; /* Return empty string if no pending result. */
    break ;
  default: /* should never happen */
    return mysql_prim_confl (interp,objc,objv,"weirdness in Mysqltcl_Result") ;
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
                       Tcl_NewIntObj(mysql_num_rows (handle->result)
	                             - handle->res_count)) ;
    break ;
  default:
    return mysql_prim_confl (interp,objc,objv,"weirdness in Mysqltcl_Result");
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

DEFINE_CMD(Mysqltcl_Col)
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
  current_db = strcmp (argv, "-current") == 0;
  
  if (current_db) {
    if ((handle = get_handle (interp,objc,objv,CL_RES)) == 0)
      return TCL_ERROR ;
    else
      result = handle->result ;
  } else {
    if ((result = mysql_list_fields (handle->connection, argv, (char*)NULL)) == NULL) {
      return mysql_server_confl (interp,objc,objv,handle->connection) ;
    }
  }
  /* Must examine the first specifier at this point. */
  if (Tcl_ListObjGetElements (interp, objv[3], &listObjc, &listObjv) != TCL_OK)
    return TCL_ERROR ;
  resList = Tcl_GetObjResult(interp);
  if (objc == 4 && listObjc == 1) {
      mysql_field_seek (result, 0) ;
      while ((fld = mysql_fetch_field (result)) != NULL)
        if ((colinfo = mysql_colinfo (interp,objc,objv,fld, objv[3])) != NULL) {
            Tcl_ListObjAppendElement (interp, resList, colinfo);
        } else {
            goto conflict;
	    }
  } else if (objc == 4 && listObjc > 1) {
      mysql_field_seek (result, 0) ;
      while ((fld = mysql_fetch_field (result)) != NULL) {
        resSubList = Tcl_NewListObj(0, NULL);
        for (coln = 0; coln < listObjc; coln++)
            if ((colinfo = mysql_colinfo (interp,objc,objv,fld, listObjv[coln])) != NULL) {
                Tcl_ListObjAppendElement (interp, resSubList, colinfo);
            } else {
               goto conflict; 
            }
        Tcl_ListObjAppendElement (interp, resList, resSubList);
	}
  } else {
      for (idx = 3; idx < objc; idx++) {
        resSubList = Tcl_NewListObj(0, NULL);
        mysql_field_seek (result, 0) ;
        while ((fld = mysql_fetch_field (result)) != NULL)
        if ((colinfo = mysql_colinfo (interp,objc,objv,fld, objv[idx])) != NULL) {
            Tcl_ListObjAppendElement (interp, resSubList, colinfo);
        } else {
            goto conflict; 
        }
        Tcl_ListObjAppendElement (interp, resList, resSubList);
      }
  }
  if (!current_db) mysql_free_result (result) ;
  return TCL_OK;
  
  conflict:
    if (!current_db) mysql_free_result (result) ;
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

DEFINE_CMD(Mysqltcl_State)
{
  MysqlTclHandle *handle;
  int numeric=0 ;
  Tcl_Obj *res;
  
  if (mysql_prologue(interp, objc, objv, 2, 3, NULL, "handle ?-numeric?") == 0)
    return TCL_ERROR;

  if (objc==3) {
    if (strcmp (Tcl_GetStringFromObj(objv[2],NULL), "-numeric"))
      return mysql_prim_confl (interp,objc,objv,"last parameter should be -numeric") ;
    else
      numeric=1;
  }
  
  if (GetHandleFromObj(NULL, objv[1], &handle) != TCL_OK)
    res = (numeric)?Tcl_NewIntObj(0):Tcl_NewStringObj("NOT_A_HANDLE",-1) ;
  else if (handle->connection == 0)
    res = (numeric)?Tcl_NewIntObj(1):Tcl_NewStringObj("UNCONNECTED",-1) ;
  else if (handle->database[0] == '\0')
    res = (numeric)?Tcl_NewIntObj(2):Tcl_NewStringObj("CONNECTED",-1) ;
  else if (handle->result == NULL)
    res = (numeric)?Tcl_NewIntObj(3):Tcl_NewStringObj("IN_USE",-1) ;
  else
    res = (numeric)?Tcl_NewIntObj(4):Tcl_NewStringObj("RESULT_PENDING",-1) ;

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

DEFINE_CMD(Mysqltcl_InsertId)
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
 * Mysqltcl_Close --
 *    Implements the mysqlclose command:
 *    usage: mysqlclose ?handle?
 *	                
 *    results:
 *	null string
 */

DEFINE_CMD(Mysqltcl_Close)
{
  MysqlTclHandle *handle,*thandle;
  Tcl_HashEntry *entryPtr;
  Tcl_HashEntry *qentries[16];
  Tcl_HashSearch search;

  int i,qfound = 0;

  /* If handle omitted, close all connections. */
  if (objc == 1) {
      Mysqltcl_Kill ((ClientData)NULL) ;
      return TCL_OK ;
  }
  
  if ((handle = mysql_prologue(interp, objc, objv, 2, 2, CL_CONN,
			    "?handle?")) == 0)
    return TCL_ERROR;

  if (handle->result != NULL)
    mysql_free_result (handle->result) ;

  /* Search all queries on this handle and close those */
  if (!handle->isquery)  {
    mysql_close(handle->connection);
    while (1) {
      for (entryPtr=Tcl_FirstHashEntry(&handleTable,&search); 
	   entryPtr!=NULL;
	   entryPtr=Tcl_NextHashEntry(&search)) {

	thandle=(MysqlTclHandle *)Tcl_GetHashValue(entryPtr);
	if (thandle->connection == handle->connection &&
	    thandle->isquery) {
	  qentries[qfound++] = entryPtr;
	}
	if (qfound==16) break;
      }
      if (qfound>0) {
	for(i=0;i<qfound;i++) {
	  entryPtr=qentries[i];
	  thandle=(MysqlTclHandle *)Tcl_GetHashValue(entryPtr);
	  Tcl_DeleteHashEntry(entryPtr);
	  if (thandle->result != NULL)
	    mysql_free_result (thandle->result);
	  closeHandle(thandle);
	}
      }
      if (qfound!=16) break;
      qfound = 0;
    }
  }
  entryPtr = Tcl_FindHashEntry(&handleTable,Tcl_GetStringFromObj(objv[1],NULL));
  if (entryPtr) Tcl_DeleteHashEntry(entryPtr);
  closeHandle(handle);
  return TCL_OK;
}

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
int Mysqltcl_Init (interp)
    Tcl_Interp *interp;
{
  char nbuf[MYSQL_SMALL_SIZE];
 
  if (Tcl_InitStubs(interp, "8.1", 0) == NULL)
    return TCL_ERROR;
  if (Tcl_PkgRequire(interp, "Tcl", "8.1", 0) == NULL)
    return TCL_ERROR;
  if (Tcl_PkgProvide(interp, "mysqltcl" , VERSION) != TCL_OK)
    return TCL_ERROR;
  /*
   * Initialize the new Tcl commands.
   * Deleting any command will close all connections.
   */
  ADD_CMD(mysqlconnect, Mysqltcl_Connect);
  ADD_CMD(mysqluse, Mysqltcl_Use);
  ADD_CMD(mysqlescape, Mysqltcl_Escape);
  ADD_CMD(mysqlsel, Mysqltcl_Sel);
  ADD_CMD(mysqlnext, Mysqltcl_Next);
  ADD_CMD(mysqlseek, Mysqltcl_Seek);
  ADD_CMD(mysqlmap, Mysqltcl_Map);
  ADD_CMD(mysqlexec, Mysqltcl_Exec);
  ADD_CMD(mysqlclose, Mysqltcl_Close);
  ADD_CMD(mysqlinfo, Mysqltcl_Info);
  ADD_CMD(mysqlresult, Mysqltcl_Result);
  ADD_CMD(mysqlcol, Mysqltcl_Col);
  ADD_CMD(mysqlstate, Mysqltcl_State);
  ADD_CMD(mysqlinsertid, Mysqltcl_InsertId);
  ADD_CMD(mysqlquery, Mysqltcl_Query);
  ADD_CMD(mysqlendquery, Mysqltcl_EndQuery);

  /* Initialize mysqlstatus global array. */

  clear_msg(interp);
  
  /* Initialize HashTable for mysql handles */
  Tcl_InitHashTable (&handleTable, TCL_STRING_KEYS);

  /* Link the null value element to the corresponding C variable. */
  if ((MysqlNullvalue = (char*)Tcl_Alloc (12)) == NULL) return TCL_ERROR ;
  strcpy (MysqlNullvalue, MYSQL_NULLV_INIT);
  sprintf (nbuf, "%s(%s)", MYSQL_STATUS_ARR, MYSQL_STATUS_NULLV) ;

  if (Tcl_LinkVar (interp,nbuf,(char *)&MysqlNullvalue, TCL_LINK_STRING) != TCL_OK)
    return TCL_ERROR;

  /* Register the handle object type */
  Tcl_RegisterObjType(&mysqlHandleType);

  /* A little sanity check.
   * If this message appears you must change the source code and recompile.
   */
  if (strlen (MysqlHandlePrefix) == MYSQL_HPREFIX_LEN)
    return TCL_OK;
  else {
    panic("*** mysqltcl (mysqltcl.c): handle prefix inconsistency!\n");
    return TCL_ERROR ;
  }
}
