/*
 * mysqltcl.h
 *
 *  Created on: 08.02.2011
 *      Author: artur
 */

#ifndef MYSQLTCL_H_
#define MYSQLTCL_H_

#ifdef _WINDOWS
   #include <windows.h>
   #define PACKAGE "mysqltcl"
   #define PACKAGE_VERSION "3.051"
#endif

#include <tcl.h>
#include <mysql.h>

#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>

#define MYSQL_SMALL_SIZE  TCL_RESULT_SIZE /* Smaller buffer size. */
#define MYSQL_NAME_LEN     80    /* Max. database name length. */
#define PREPARED_STATEMENT

enum MysqlHandleType {HT_CONNECTION=1,HT_QUERY=2,HT_STATEMENT=3};

typedef struct MysqlTclHandle {
  MYSQL * connection;         /* Connection handle, if connected; NULL otherwise. */
  char database[MYSQL_NAME_LEN];  /* Db name, if selected; NULL otherwise. */
  MYSQL_RES* result;              /* Stored result, if any; NULL otherwise. */
  int res_count;                 /* Count of unfetched rows in result. */
  int col_count;                 /* Column count in result, if any. */
  int number;                    /* handle id */
  enum MysqlHandleType type;                      /* handle type */
  Tcl_Encoding encoding;         /* encoding for connection */
#ifdef PREPARED_STATEMENT
  MYSQL_STMT *statement;         /* used only by prepared statements*/
  MYSQL_BIND *bindParam;
  MYSQL_BIND *bindResult;
  MYSQL_RES *resultMetadata;
  MYSQL_RES *paramMetadata;
  unsigned long* resultLengths;
  int *needColumnFetch;
#endif
} MysqlTclHandle;

typedef struct MysqltclState {
  Tcl_HashTable hash;
  int handleNum;
  char *MysqlNullvalue;
  // Tcl_Obj *nullObjPtr;
} MysqltclState;

/* Array for status info, and its elements. */
#define MYSQL_STATUS_ARR "mysqlstatus"

#define MYSQL_STATUS_CODE "code"
#define MYSQL_STATUS_CMD  "command"
#define MYSQL_STATUS_MSG  "message"
#define MYSQL_STATUS_NULLV  "nullvalue"

#define FUNCTION_NOT_AVAILABLE "function not available"

/* C variable corresponding to mysqlstatus(nullvalue) */
#define MYSQL_NULLV_INIT ""

/* Check Level for mysql_prologue */
enum CONNLEVEL {CL_PLAIN,CL_CONN,CL_DB,CL_RES};

Tcl_ObjType mysqlNullType;

/* Prototypes for all functions. */

/* mysqltcl.c */
void closeHandle(MysqlTclHandle *handle);
int mysql_server_confl(Tcl_Interp *interp,int objc,Tcl_Obj *CONST objv[],MYSQL * connection);
int mysql_prim_confl(Tcl_Interp *interp,int objc,Tcl_Obj *CONST objv[],char *msg);
void freeResult(MysqlTclHandle *handle);
Tcl_Obj *Tcl_NewHandleObj(MysqltclState *statePtr,MysqlTclHandle *handle);
MysqlTclHandle *mysql_prologue(Tcl_Interp *interp,int objc,Tcl_Obj *CONST objv[],int req_min_args,int req_max_args,enum CONNLEVEL check_level,char *usage_msg);
MysqlTclHandle *createMysqlHandle(MysqltclState *statePtr);
Tcl_Obj *mysql_colinfo(Tcl_Interp *interp,int objc,Tcl_Obj *CONST objv[],MYSQL_FIELD* fld,Tcl_Obj * keyw);

/* query.c */
int Mysqltcl_Sel(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int Mysqltcl_Query(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int Mysqltcl_EndQuery(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int Mysqltcl_Fetch(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int Mysqltcl_Seek(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int Mysqltcl_Map(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int Mysqltcl_Receive(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int Mysqltcl_Exec(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int Mysqltcl_NewNull(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int Mysqltcl_MoreResult(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int Mysqltcl_NextResult(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
MysqlTclHandle *createHandleFrom(MysqltclState *statePtr,MysqlTclHandle *handle,enum MysqlHandleType handleType);

/* configure.c */
int Mysqltcl_Connect(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int Mysqltcl_BaseInfo(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int Mysqltcl_Close(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int Mysqltcl_Encoding(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int Mysqltcl_ChangeUser(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int Mysqltcl_AutoCommit(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int Mysqltcl_Use(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

/* pstatement.c */
int Mysqltcl_Prepare(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);
int Mysqltcl_PExecute(ClientData clientData, Tcl_Interp *interp, int objc, Tcl_Obj *const objv[]);

#endif /* MYSQLTCL_H_ */
