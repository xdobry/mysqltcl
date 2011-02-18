/*
 * $Id: mysqltcl.c,v 1.2 1997/04/27 22:01:27 greg Exp greg $
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
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <tcl.h>
#include <mysql.h>

#include <errno.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>


#define MYSQL_HANDLES      15	/* Default number of handles available. */
#define MYSQL_BUFF_SIZE	1024	/* Conversion buffer size for various needs. */
#define MYSQL_SMALL_SIZE  TCL_RESULT_SIZE /* Smaller buffer size. */
#define MYSQL_NAME_LEN     80    /* Max. host, database name length. */

typedef struct MysqlTclHandle {
  MYSQL * connection ;         /* Connection handle, if connected; -1 otherwise. */
  char host[MYSQL_NAME_LEN] ;      /* Host name, if connected. */
  char database[MYSQL_NAME_LEN] ;  /* Db name, if selected; NULL otherwise. */
  MYSQL_RES* result ;              /* Stored result, if any; NULL otherwise. */
  int res_count ;                 /* Count of unfetched rows in result. */
  int col_count ;                 /* Column count in result, if any. */
  MYSQL *mysql ;		  /* Some other pointer */
} MysqlTclHandle;

static MysqlTclHandle   MysqlHandle[MYSQL_HANDLES];  

static char *MysqlHandlePrefix = "mysql";
/* Prefix string used to identify handles.
 * The following must be strlen(MysqlHandlePrefix).
 */
#define MYSQL_HPREFIX_LEN 5

/* Array for status info, and its elements. */
static char *MysqlStatusArr = "mysqlstatus";
#define MYSQL_STATUS_CODE "code"
#define MYSQL_STATUS_CMD  "command"
#define MYSQL_STATUS_MSG  "message"
#define MYSQL_STATUS_NULLV  "nullvalue"

/* C variable corresponding to mysqlstatus(nullvalue) */
static char* MysqlNullvalue = NULL ;
#define MYSQL_NULLV_INIT ""

/* Options to the 'info', 'result', 'col' combo commands. */
     
static char* MysqlDbOpt[] =
{
  "dbname", "dbname?", "tables", "host", "host?", "databases"
};
#define MYSQL_INFNAME_OPT 0
#define MYSQL_INFNAMEQ_OPT 1
#define MYSQL_INFTABLES_OPT 2
#define MYSQL_INFHOST_OPT 3
#define MYSQL_INFHOSTQ_OPT 4
#define MYSQL_INFLIST_OPT 5

#define MYSQL_INF_OPT_MAX 5

static char* MysqlResultOpt[] =
{
  "rows", "rows?", "cols", "cols?", "current", "current?"
};
#define MYSQL_RESROWS_OPT 0
#define MYSQL_RESROWSQ_OPT 1
#define MYSQL_RESCOLS_OPT 2
#define MYSQL_RESCOLSQ_OPT 3
#define MYSQL_RESCUR_OPT 4
#define MYSQL_RESCURQ_OPT 5

#define MYSQL_RES_OPT_MAX 5

/* Column info definitions. */

static char* MysqlColkey[] =
{
  "table", "name", "type", "length", "prim_key", "non_null"
};

#define MYSQL_COL_TABLE_K 0
#define MYSQL_COL_NAME_K 1
#define MYSQL_COL_TYPE_K 2
#define MYSQL_COL_LENGTH_K 3
#define MYSQL_COL_PRIMKEY_K 4
#define MYSQL_COL_NONNULL_K 5

#define MYSQL_COL_K_MAX 5

/* Macro for checking handle syntax; arguments:
 * 'h' must point to the handle.
 * 'H' must be an int variable.
 * RETURNS the handle index; or -1 on syntax conflict.
 * SIDE EFFECT: 'H' will contain the handle index on success.
 * 'h' will point to the first digit on success.
 * ASSUMES constant MYSQL_HANDLES >= 10.
 */
#define HSYNTAX(h,H) \
(((strncmp(h,MysqlHandlePrefix,MYSQL_HPREFIX_LEN) == 0) && \
(h+=MYSQL_HPREFIX_LEN) && isdigit(h[0]))? \
 (((H=h[0]-'0')+1 && h[1]=='\0')?H: \
  ((isdigit(h[1]) && h[2]=='\0' && (H=10*H+h[1]-'0')<MYSQL_HANDLES)?H:-1)) \
:-1)


/* Prototypes for all functions. */

extern Tcl_CmdProc  Mysqltcl_Connect;
extern Tcl_CmdProc  Mysqltcl_Use;
extern Tcl_CmdProc  Mysqltcl_Sel;
extern Tcl_CmdProc  Mysqltcl_Next;
extern Tcl_CmdProc  Mysqltcl_Seek;
extern Tcl_CmdProc  Mysqltcl_Map;
extern Tcl_CmdProc  Mysqltcl_Exec;
extern Tcl_CmdProc  Mysqltcl_Close;
extern Tcl_CmdProc  Mysqltcl_Info;
extern Tcl_CmdProc  Mysqltcl_Result;
extern Tcl_CmdProc  Mysqltcl_Col;
extern Tcl_CmdProc  Mysqltcl_State;
extern Tcl_CmdProc  Mysqltcl_InsertId;

  
/* CONFLICT HANDLING
 *
 * Every command begins by calling 'mysql_prologue'.
 * This function resets mysqlstatus(code) to zero; the other array elements
 * retain their previous values.
 * The function also saves argc/argv in global variables.
 * After this the command processing proper begins.
 *
 * If there is a conflict, the message is taken from one of the following
 * sources,
 * -- this code (mysql_prim_confl),
 * -- the database server (mysql_server_confl),
 * -- a POSIX (system call) diagnostic.
 * A complete message is put together from the above plus the name of the
 * command where the conflict was detected.
 * The complete message is returned as the Tcl result and is also stored in
 * mysqlstatus(message).
 * mysqlstatus(code) is set to "-1", except for POSIX conflicts where 'errno'
 * is used.
 * In addition, the whole command where the conflict was detected is put
 * together from the saved argc/argv and is copied into mysqlstatus(command).
 */

static Tcl_Interp* saved_interp;
static int saved_argc;
static char** saved_argv;


/*
 *----------------------------------------------------------------------
 * mysql_reassemble
 * Reassembles the current command from the saved argv; copies it into
 * mysqlstatus(command).
 */

static void
mysql_reassemble ()
{
  unsigned int flags = TCL_GLOBAL_ONLY | TCL_LIST_ELEMENT;
  int idx ;

  for (idx = 0; idx < saved_argc; ++idx)
    {
      Tcl_SetVar2 (saved_interp, MysqlStatusArr, MYSQL_STATUS_CMD,
		   saved_argv[idx], flags) ;
      flags |= TCL_APPEND_VALUE ;
    }
}


/*
 *----------------------------------------------------------------------
 * mysql_posix_confl
 * Conflict handling after a failed system call.
 */

static int
mysql_posix_confl (syscall)
     char* syscall;
{
  char buf[MYSQL_SMALL_SIZE];
  char* strerror () ;
  char* msg = strerror (errno) ;

  sprintf(buf, "%d", errno) ;
  Tcl_SetVar2 (saved_interp, MysqlStatusArr, MYSQL_STATUS_CODE, buf,
	       TCL_GLOBAL_ONLY);
  Tcl_SetResult (saved_interp, "", TCL_STATIC) ;
  Tcl_AppendResult (saved_interp, saved_argv[0], "/POSIX: (", syscall,
		    ") ", (msg == NULL) ? "" : msg, (char*)NULL);
  Tcl_SetVar2 (saved_interp, MysqlStatusArr, MYSQL_STATUS_MSG,
	       saved_interp->result, TCL_GLOBAL_ONLY);
  mysql_reassemble () ;
  return TCL_ERROR ;
}


/*
 *----------------------------------------------------------------------
 * mysql_prim_confl
 * Conflict handling after a primitive conflict.
 *
 */

static int
mysql_prim_confl (msg)
     char* msg ;
{
  Tcl_SetVar2 (saved_interp, MysqlStatusArr, MYSQL_STATUS_CODE, "-1",
	       TCL_GLOBAL_ONLY);
  Tcl_SetResult (saved_interp, "", TCL_STATIC) ;
  Tcl_AppendResult (saved_interp, saved_argv[0], ": ", msg, (char*)NULL) ;
  Tcl_SetVar2 (saved_interp, MysqlStatusArr, MYSQL_STATUS_MSG,
	       saved_interp->result, TCL_GLOBAL_ONLY);
  mysql_reassemble () ;
  return TCL_ERROR ;
}


/*
 *----------------------------------------------------------------------
 * mysql_server_confl
 * Conflict handling after an mySQL conflict.
 *
 */

static int
mysql_server_confl (int hand, MYSQL * connection)
{
  char* mysql_errorMsg;

  /* mysql_errorMsg = mysql_error(connection);	*/
  mysql_errorMsg = "blah blah blah";
  mysql_errorMsg = mysql_error(connection);

  Tcl_SetVar2 (saved_interp, MysqlStatusArr, MYSQL_STATUS_CODE, "-1",
	       TCL_GLOBAL_ONLY);
  Tcl_SetResult (saved_interp, "", TCL_STATIC) ;
  Tcl_AppendResult (saved_interp, saved_argv[0], "/db server: ",
		    (mysql_errorMsg == NULL) ? "" : mysql_errorMsg, (char*)NULL) ;
  Tcl_SetVar2 (saved_interp, MysqlStatusArr, MYSQL_STATUS_MSG,
	       saved_interp->result, TCL_GLOBAL_ONLY);
  mysql_reassemble () ;
  return TCL_ERROR ;
}


/*----------------------------------------------------------------------
 * get_handle_plain
 * Check handle syntax (and nothing else).
 * RETURN: MysqlHandle index number or -1 on error.
 */
static int
get_handle_plain (handle) 
     char *handle;
{
  int hi ;
  char* hp = handle ;
  
  if (HSYNTAX(hp,hi) < 0)
    {
      mysql_prim_confl ("weird handle") ; /*  */
      return -1 ;
    }
  else
    return hi ;
}


/*----------------------------------------------------------------------
 * get_handle_conn
 * Check handle syntax, verify that the handle is connected.
 * RETURN: MysqlHandle index number or -1 on error.
 */
static int
get_handle_conn (handle) 
     char *handle;
{
  int hi ;
  char* hp = handle ;

  if (HSYNTAX(hp,hi) < 0)
    {
      mysql_prim_confl ("weird handle") ;
      return -1 ;
    }

  if (MysqlHandle[hi].connection == 0)
    {
      mysql_prim_confl ("handle not connected") ;
      return -1 ;
    }
  else
    return hi ;
}


/*----------------------------------------------------------------------
 * get_handle_db
 * Check handle syntax, verify that the handle is connected and that
 * there is a current database.
 * RETURN: MysqlHandle index number or -1 on error.
 */
static int
get_handle_db (handle) 
     char *handle;
{
  int hi ;
  char* hp = handle ;

  if (HSYNTAX(hp,hi) < 0)
    {
      mysql_prim_confl ("weird handle") ;
      return -1 ;
    }

  if (MysqlHandle[hi].connection == 0)
    {
      mysql_prim_confl ("handle not connected") ;
      return -1 ;
    }

  if (MysqlHandle[hi].database[0] == '\0')
    {
      mysql_prim_confl ("no current database") ;
      return -1 ;
    }
  else
    return hi ;
}


/*----------------------------------------------------------------------
 * get_handle_res
 * Check handle syntax, verify that the handle is connected and that
 * there is a current database and that there is a pending result.
 * RETURN: MysqlHandle index number or -1 on error.
 */

static int
get_handle_res (handle) 
     char *handle;
{
  int hi ;
  char* hp = handle ;

  if (HSYNTAX(hp,hi) < 0)
    {
      mysql_prim_confl ("weird handle") ;
      return -1 ;
    }

  if (MysqlHandle[hi].connection == 0)
    {
      mysql_prim_confl ("handle not connected") ;
      return -1 ;
    }

  if (MysqlHandle[hi].database[0] == '\0')
    {
      mysql_prim_confl ("no current database") ;
      return -1 ;
    }

  if (MysqlHandle[hi].result == NULL)
    {
      mysql_prim_confl ("no result pending") ; /*  */
      return -1 ;
    }
  else
    return hi ;
}


/* 
 *----------------------------------------------------------------------
 * handle_init
 * Initialize the handle array.
 */
static void 
handle_init () 
{
  int i ;

  for (i = 0; i < MYSQL_HANDLES; i++) {
    MysqlHandle[i].connection = (MYSQL *)0 ;
    MysqlHandle[i].host[0] = '\0' ;
    MysqlHandle[i].database[0] = '\0' ;
    MysqlHandle[i].result = NULL ;
    MysqlHandle[i].res_count = 0 ;
    MysqlHandle[i].col_count = 0 ;
  }
}


/*
 *----------------------------------------------------------------------
 * clear_msg
 *
 * Clears all error and message elements in the global array variable.
 *
 */

static void
clear_msg(interp)
    Tcl_Interp *interp;
{
    Tcl_SetVar2(interp, MysqlStatusArr, MYSQL_STATUS_CODE, "0", TCL_GLOBAL_ONLY);
    Tcl_SetVar2(interp, MysqlStatusArr, MYSQL_STATUS_CMD, "", TCL_GLOBAL_ONLY);
    Tcl_SetVar2(interp, MysqlStatusArr, MYSQL_STATUS_MSG, "", TCL_GLOBAL_ONLY);
}


/*
 *----------------------------------------------------------------------
 * mysql_prologue
 *
 * Does most of standard command prologue; required for all commands
 * having conflict handling.
 * 'req_args' must be the required number of arguments for the command,
 * including the command word.
 * 'usage_msg' must be a usage message, leaving out the command name.
 * Checks the handle assumed to be present in argv[1] if 'check' is not NULL.
 * RETURNS: Handle index or -1 on failure.
 * Returns zero if 'check' is NULL.
 * SIDE EFFECT: Sets the Tcl result on failure.
 */

static int
mysql_prologue (interp, argc, argv, req_args, check, usage_msg)
     Tcl_Interp *interp;
     int         argc;
     char      **argv;
     int         req_args;
     char       *usage_msg;
     int (*check) () ; /* Pointer to function for checking the handle. */
{
  char buf[MYSQL_BUFF_SIZE];
  int hand = 0;
  int need;

  /* Reset mysqlstatus(code). */
  Tcl_SetVar2 (interp, MysqlStatusArr, MYSQL_STATUS_CODE, "0",
	       TCL_GLOBAL_ONLY);

  /* Save command environment. */
  saved_interp = interp;
  saved_argc = argc ;
  saved_argv = argv ;

  /* Check number of minimum args. */
  if ((need = req_args - argc) > 0) 
    {
      sprintf (buf, "%d more %s needed: %s %s", need, (need>1)?"args":"arg",
	       argv[0], usage_msg);
      (void)mysql_prim_confl (buf) ;
      return -1 ;
    }

  /* Check the handle.
   * The function is assumed to set the status array on conflict.
   */
  if (check != NULL && (hand = check (argv[1])) < 0)
    return -1 ;

  return (hand);
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

static int
mysql_colinfo (interp, fld, keyw)
     Tcl_Interp  *interp;
     MYSQL_FIELD* fld ;
     char* keyw ;
{
  char buf[MYSQL_SMALL_SIZE];
  char keybuf[MYSQL_SMALL_SIZE];
  int idx ;
  char* res ;
  int retcode ;
  
  for (idx = 0;
       idx <= MYSQL_COL_K_MAX && strcmp (MysqlColkey[idx], keyw) != 0;
       idx++) ;

  switch (idx)
    {
    case MYSQL_COL_TABLE_K:
      res = fld->table ;
      break ;
    case MYSQL_COL_NAME_K:
      res = fld->name ;
      break ;
    case MYSQL_COL_TYPE_K:
      switch (fld->type)
	{
	case FIELD_TYPE_DECIMAL:
	  res = "decimal";
	  break;
	case FIELD_TYPE_TINY:
	  res = "tiny";
	  break;
	case FIELD_TYPE_SHORT:
	  res = "short";
	  break;
	case FIELD_TYPE_LONG:
	  res = "long" ;
	  break ;
	case FIELD_TYPE_FLOAT:
	  res = "float";
	  break;
	case FIELD_TYPE_DOUBLE:
	  res = "double";
	  break;
	case FIELD_TYPE_NULL:
	  res = "null";
	  break;
	case FIELD_TYPE_TIMESTAMP:
	  res = "timestamp";
	  break;
	case FIELD_TYPE_LONGLONG:
	  res = "long long";
	  break;
	case FIELD_TYPE_INT24:
	  res = "int24";
	  break;
	case FIELD_TYPE_DATE:
	  res = "date";
	  break;
	case FIELD_TYPE_TIME:
	  res = "time";
	  break;
	case FIELD_TYPE_DATETIME:
	  res = "date time";
	  break;
	case FIELD_TYPE_YEAR:
	  res = "year";
	  break;
	case FIELD_TYPE_NEWDATE:
	  res = "new date";
	  break;
	case FIELD_TYPE_ENUM:
	  res = "enum"; /* fyll på??? */
	  break;
	case FIELD_TYPE_SET: /* samma */
	  res = "set";
	  break;
	case FIELD_TYPE_TINY_BLOB:
	  res = "tiny blob";
	  break;
	case FIELD_TYPE_MEDIUM_BLOB:
	  res = "medium blob";
	  break;
	case FIELD_TYPE_LONG_BLOB:
	  res = "long blob";
	  break;
	case FIELD_TYPE_BLOB:
	  res = "blob";
	  break;
	case FIELD_TYPE_VAR_STRING:
	  res = "var string";
	  break;
	case FIELD_TYPE_STRING:
	  res = "string" ;
	  break ;
	default:
	  sprintf (buf, "column '%s' has weird datatype", fld->name) ;
	  res = NULL ;
	}
      break ;
    case MYSQL_COL_LENGTH_K:
      sprintf (buf, "%d", fld->length) ;
      res = buf ;
      break ;
    case MYSQL_COL_PRIMKEY_K:
      sprintf (buf, "%c", (IS_PRI_KEY(fld->flags))?'1':'0') ;
      res = buf ;
      break ;
    case MYSQL_COL_NONNULL_K:
      sprintf (buf, "%c", (IS_NOT_NULL(fld->flags))?'1':'0') ;
      res = buf ;
      break ;
    default:
      if (strlen (keyw) >= MYSQL_NAME_LEN)
	{
	  strncpy (keybuf, keyw, MYSQL_NAME_LEN) ;
	  strcat (keybuf, "...") ;
	}
      else
	strcpy (keybuf, keyw) ;

      sprintf (buf, "unknown option: %s", keybuf) ;
      res = NULL ;
    }

  if (res == NULL)
    {
      (void)mysql_prim_confl (buf) ;
      retcode = 1 ;
    }
  else
    {
      Tcl_AppendElement (interp, res) ;
      retcode = 0 ;
    }

  return retcode ;
}


/*
 *----------------------------------------------------------------------
 * Mysqltcl_Kill
 * Close all connections.
 *
 */

void
Mysqltcl_Kill (clientData)
    ClientData clientData;
{
  int i ;

  for (i = 0; i < MYSQL_HANDLES; i++)
    {
      if (MysqlHandle[i].connection != 0)
	mysql_close (MysqlHandle[i].mysql) ;
    }
  
  handle_init () ;
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

int
Mysqltcl_Init (interp)
    Tcl_Interp *interp;
{
  int i;
  char nbuf[MYSQL_SMALL_SIZE];

  /*
   * Initialize mySQL proc structures 
   */
  handle_init () ;

  /*
   * Initialize the new Tcl commands.
   * Deleting any command will close all connections.
   */
  Tcl_CreateCommand (interp, "mysqlconnect", Mysqltcl_Connect, (ClientData)NULL,
		     Mysqltcl_Kill);
  Tcl_CreateCommand (interp, "mysqluse",     Mysqltcl_Use,     (ClientData)NULL,
		     Mysqltcl_Kill);
  Tcl_CreateCommand (interp, "mysqlsel",     Mysqltcl_Sel,     (ClientData)NULL,
		     Mysqltcl_Kill);
  Tcl_CreateCommand (interp, "mysqlnext",    Mysqltcl_Next,    (ClientData)NULL,
		     Mysqltcl_Kill);
  Tcl_CreateCommand (interp, "mysqlseek",    Mysqltcl_Seek,    (ClientData)NULL,
                     Mysqltcl_Kill);
  Tcl_CreateCommand (interp, "mysqlmap",     Mysqltcl_Map,     (ClientData)NULL,
		     Mysqltcl_Kill);
  Tcl_CreateCommand (interp, "mysqlexec",    Mysqltcl_Exec,    (ClientData)NULL,
		     Mysqltcl_Kill);
  Tcl_CreateCommand (interp, "mysqlclose",   Mysqltcl_Close,   (ClientData)NULL,
		     Mysqltcl_Kill);
  Tcl_CreateCommand (interp, "mysqlinfo",    Mysqltcl_Info,    (ClientData)NULL,
		     Mysqltcl_Kill);
  Tcl_CreateCommand (interp, "mysqlresult",  Mysqltcl_Result,  (ClientData)NULL,
		     Mysqltcl_Kill);
  Tcl_CreateCommand (interp, "mysqlcol",     Mysqltcl_Col,     (ClientData)NULL,
		     Mysqltcl_Kill);
  Tcl_CreateCommand (interp, "mysqlstate",   Mysqltcl_State,   (ClientData)NULL,
		     Mysqltcl_Kill);
  Tcl_CreateCommand (interp, "mysqlinsertid",Mysqltcl_InsertId,(ClientData)NULL,
		     Mysqltcl_Kill);

  /* Initialize mysqlstatus global array. */
  clear_msg(interp);

  /* Link the null value element to the corresponding C variable. */
  if ((MysqlNullvalue = (char*)malloc (12)) == NULL)
    {
      fprintf (stderr, "*** mysqltcl: out of memory\n") ;
      return TCL_ERROR ;
    }
  (void)strcpy (MysqlNullvalue, MYSQL_NULLV_INIT);
  (void)sprintf (nbuf, "%s(%s)", MysqlStatusArr, MYSQL_STATUS_NULLV) ;
  Tcl_LinkVar (interp, nbuf, (char*)&MysqlNullvalue, TCL_LINK_STRING) ;

  /* A little sanity check.
   * If this message appears you must change the source code and recompile.
   */
  if (strlen (MysqlHandlePrefix) == MYSQL_HPREFIX_LEN)
    return TCL_OK;
  else
    {
      fprintf (stderr, "*** mysqltcl (mysqltcl.c): handle prefix inconsistency!\n") ;
      return TCL_ERROR ;
    }
}


/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Connect
 * Implements the mysqlconnect command:
 * usage: mysqlconnect [-user user [-password password]] ?server-host?
 *	                
 * Results:
 *      handle - a character string of newly open handle
 *      TCL_OK - connect successful
 *      TCL_ERROR - connect not successful - error message returned
 */

int
Mysqltcl_Connect (clientData, interp, argc, argv)
     ClientData   clientData;
     Tcl_Interp  *interp;
     int          argc;
     char       **argv;
{
  int        hand = -1;
  int        i;
  char       buf[MYSQL_BUFF_SIZE];
  MYSQL * connect, * mysql ;
  char *hostname = NULL;
  char *user = NULL;
  char *password = NULL;
  
  /* Pro-forma check (should never fail). */
  if (mysql_prologue (interp, argc, argv, 1, NULL, "[-user user [-password password]] ?hostname?") < 0)
    return TCL_ERROR;

  for (i = 1; i < argc; i++) {
    if (i < argc - 1 && *argv[i] == '-') {
      if (user == NULL && strcmp(argv[i]+1, "user") == 0) {
	user = argv[i+1];
	i++;
      } else if (user != NULL && password == NULL && strcmp(argv[i]+1, "password") == 0) {
	password = argv[i+1];
	i++;
      } else {
	return mysql_prim_confl("Usage: mysqlconnect [-user user [-password password]] ?hostname?");
      }
    } else if (i == argc - 1) {
      hostname = argv[i];
    } else {
      return mysql_prim_confl("Usage: mysqlconnect [-user user [-password password]] ?hostname?");
    }
  }

  /* Find an unused handle. */
  for (i = 0; i < MYSQL_HANDLES; i++) {
    if (MysqlHandle[i].connection == 0) {
      hand = i;
      break;
    }
  }

  /* Malloc some memory for the mysql pointer */
  mysql = (MYSQL *)malloc(sizeof(MYSQL));

  if (hand == -1)
    return mysql_prim_confl ("no mySQL handles available");

  if (hostname != NULL)
    {
      connect = mysql_connect (mysql, hostname, user, password) ;
	MysqlHandle[hand].mysql = mysql;
      strncpy (MysqlHandle[hand].host, argv[1], MYSQL_NAME_LEN) ;
      MysqlHandle[hand].host[MYSQL_NAME_LEN - 1] = '\0' ;
    }
  else
    {
      if (gethostname (buf, MYSQL_NAME_LEN) == 0)
	{
	  connect = mysql_connect (mysql, (char*)NULL, user, password) ;
	MysqlHandle[hand].mysql = mysql;
	  strcpy (MysqlHandle[hand].host, buf) ;
	}
      else
	return mysql_posix_confl ("gethostname") ;
    }

  if (connect == 0)
    {
      MysqlHandle[hand].connection = (MYSQL *)0 ; /* Just to be sure. */
      return mysql_server_confl (hand, MysqlHandle[hand].mysql);
    }
  else
    MysqlHandle[hand].connection = connect ;

  /* Construct handle and return. */
  sprintf(buf, "%s%d", MysqlHandlePrefix, hand);
  Tcl_SetResult(interp, buf, TCL_VOLATILE);

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

Mysqltcl_Use (clientData, interp, argc, argv)
     ClientData   clientData;
     Tcl_Interp  *interp;
     int          argc;
     char       **argv;
{
  int hand;
  
  if ((hand = mysql_prologue(interp, argc, argv, 3, get_handle_conn,
			    "handle dbname")) < 0)
    return TCL_ERROR;

  if (strlen (argv[2]) >= MYSQL_NAME_LEN)
    return mysql_prim_confl ("database name too long") ;
  if (mysql_select_db (MysqlHandle[hand].mysql, argv[2]) < 0)
    return mysql_server_confl (hand, MysqlHandle[hand].mysql) ;

  strcpy (MysqlHandle[hand].database, argv[2]) ;
  return TCL_OK;
}



/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_Sel
 *    Implements the mysqlsel command:
 *    usage: mysqlsel handle sel-query
 *	                
 *    results:
 *
 *    SIDE EFFECT: Flushes any pending result, even in case of conflict.
 *    Stores new results.
 */

Mysqltcl_Sel (clientData, interp, argc, argv)
     ClientData   clientData;
     Tcl_Interp  *interp;
     int          argc;
     char       **argv;
{
  int     hand;
  
  if ((hand = mysql_prologue(interp, argc, argv, 3, get_handle_db,
			    "handle sel-query")) < 0)
    return TCL_ERROR;

  /* Flush any previous result. */
  if (MysqlHandle[hand].result != NULL)
    {
      mysql_free_result (MysqlHandle[hand].result) ;
      MysqlHandle[hand].result = NULL ;
    }

  if (mysql_query (MysqlHandle[hand].mysql, argv[2]) < 0)
    return mysql_server_confl (hand, MysqlHandle[hand].connection) ;

  if ((MysqlHandle[hand].result = mysql_store_result (MysqlHandle[hand].mysql)) == NULL)
    {
      (void)strcpy (interp->result, "-1") ;
    }
  else
    {
      MysqlHandle[hand].res_count = mysql_num_rows (MysqlHandle[hand].result) ;
      MysqlHandle[hand].col_count = mysql_num_fields (MysqlHandle[hand].result) ;
      (void)sprintf (interp->result, "%d", MysqlHandle[hand].res_count) ;
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
 *
 * SIDE EFFECT: Flushes any pending result, even in case of conflict.
 */

int
Mysqltcl_Exec (clientData, interp, argc, argv)
     ClientData   clientData;
     Tcl_Interp  *interp;
     int          argc;
     char       **argv;
{
  int     hand;
  
  if ((hand = mysql_prologue(interp, argc, argv, 3, get_handle_db,
			    "handle sql-statement")) < 0)
    return TCL_ERROR;

  /* Flush any previous result. */
  if (MysqlHandle[hand].result != NULL)
    {
      mysql_free_result (MysqlHandle[hand].result) ;
      MysqlHandle[hand].result = NULL ;
    }

  if (mysql_query (MysqlHandle[hand].mysql, argv[2]) < 0)
    return mysql_server_confl (hand, MysqlHandle[hand].mysql) ;

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

Mysqltcl_Next (clientData, interp, argc, argv)
     ClientData   clientData;
     Tcl_Interp  *interp;
     int          argc;
     char       **argv;
{
  int hand;
  int idx ;
  MYSQL_ROW row ;
  char* val ;
  
  if ((hand = mysql_prologue(interp, argc, argv, 2, get_handle_res,
			    "handle")) < 0)
    return TCL_ERROR;

  
  if (MysqlHandle[hand].res_count == 0)
    return TCL_OK ;
  else if ((row = mysql_fetch_row (MysqlHandle[hand].result)) == NULL)
    {
      MysqlHandle[hand].res_count = 0 ;
      return mysql_prim_confl ("result counter out of sync") ;
    }
  else
    MysqlHandle[hand].res_count-- ;
  
  for (idx = 0 ; idx < MysqlHandle[hand].col_count ; idx++)
    {
      if ((val = *row++) == NULL)
	val = MysqlNullvalue ;
      Tcl_AppendElement (interp, val) ;
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

Mysqltcl_Seek (clientData, interp, argc, argv)
    ClientData   clientData;
    Tcl_Interp  *interp;
    int          argc;
    char       **argv;
{
    int hand;
    int res;
    int row;
    int total;
   
    if ((hand = mysql_prologue(interp, argc, argv, 3, get_handle_res,
                              " handle row-index")) < 0)
      return TCL_ERROR;

    if ((res = Tcl_GetInt (interp, argv[2], &row)) != TCL_OK)
      return res;
    
    total = mysql_num_rows (MysqlHandle[hand].result);
    
    if (total + row < 0)
      {
	mysql_data_seek (MysqlHandle[hand].result, 0);
	MysqlHandle[hand].res_count = total;
      }
    else if (row < 0)
      {
	mysql_data_seek (MysqlHandle[hand].result, total + row);
	MysqlHandle[hand].res_count = -row;
      }
    else if (row >= total)
      {
	mysql_data_seek (MysqlHandle[hand].result, row);
	MysqlHandle[hand].res_count = 0;
      }
    else
      {
	mysql_data_seek (MysqlHandle[hand].result, row);
	MysqlHandle[hand].res_count = total - row;
      }

    (void)sprintf (interp->result, "%d", MysqlHandle[hand].res_count) ;
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

int
Mysqltcl_Map (clientData, interp, argc, argv)
     ClientData   clientData;
     Tcl_Interp  *interp;
     int          argc;
     char       **argv;
{
  int code ;
  int count ;
  int hand ;
  int idx ;
  int listArgc ;
  char** listArgv ;
  MYSQL_ROW row ;
  char* val ;
  
  if ((hand = mysql_prologue(interp, argc, argv, 4, get_handle_res,
			    "handle binding-list script")) < 0)
    return TCL_ERROR;

  if (Tcl_SplitList (interp, argv[2], &listArgc, &listArgv) != TCL_OK)
    return TCL_ERROR ;
  
  if (listArgc > MysqlHandle[hand].col_count)
    {
      ckfree ((char*)listArgv) ;
      return mysql_prim_confl ("too many variables in binding list") ;
    }
  else
    count = (listArgc < MysqlHandle[hand].col_count)?listArgc
      :MysqlHandle[hand].col_count ;
  
  while (MysqlHandle[hand].res_count > 0)
    {
      /* Get next row, decrement row counter. */
      if ((row = mysql_fetch_row (MysqlHandle[hand].result)) == NULL)
	{
	  MysqlHandle[hand].res_count = 0 ;
	  ckfree ((char*)listArgv) ;
	  return mysql_prim_confl ("result counter out of sync") ;
	}
      else
	MysqlHandle[hand].res_count-- ;
      
      /* Bind variables to column values. */
      for (idx = 0; idx < count; idx++)
	{
	  if (listArgv[idx][0] != '-')
	    {
	      if ((val = *row++) == NULL)
		val = MysqlNullvalue ;
	      if (Tcl_SetVar (interp, listArgv[idx], val, TCL_LEAVE_ERR_MSG)
		  == NULL)
		{
		  ckfree ((char*)listArgv) ;
		  return TCL_ERROR ;
		}
	    }
	  else
	    row++ ;
	}

      /* Evaluate the script. */
      if ((code = Tcl_Eval (interp, argv[3])) != TCL_OK)
	switch (code) 
	  {
	  case TCL_CONTINUE:
	    continue ;
	    break ;
	  case TCL_BREAK:
	    ckfree ((char*)listArgv) ;
	    return TCL_OK ;
	    break ;
	  default:
	    ckfree ((char*)listArgv) ;
	    return code ;
	  }
    }
  ckfree ((char*)listArgv) ;
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

int
Mysqltcl_Info (clientData, interp, argc, argv)
     ClientData   clientData;
     Tcl_Interp  *interp;
     int          argc;
     char       **argv;
{
  char buf[MYSQL_BUFF_SIZE];
  int count ;
  int hand ;
  int idx ;
  MYSQL_RES* list ;
  MYSQL_ROW row ;
  char* val ;
  
  /* We can't fully check the handle at this stage. */
  if ((hand = mysql_prologue(interp, argc, argv, 3, get_handle_plain,
			    "handle option")) < 0)
    return TCL_ERROR;

  for (idx = 0;
       idx <= MYSQL_INF_OPT_MAX && strcmp (argv[2], MysqlDbOpt[idx]) != 0;
       idx++) ;

  /* First check the handle. Checking depends on the option. */
  switch (idx)
    {
    case MYSQL_INFNAME_OPT:
    case MYSQL_INFTABLES_OPT:
      hand = get_handle_db (argv[1]) ;
      break ;
    case MYSQL_INFNAMEQ_OPT:
      if ((hand = get_handle_conn (argv[1])) >= 0)
	{
	  if (MysqlHandle[hand].database[0] == '\0')
	    return TCL_OK ; /* Return empty string if no current db. */
	}
      break ;
    case MYSQL_INFHOST_OPT:
    case MYSQL_INFLIST_OPT:
      hand = get_handle_conn (argv[1]) ;
      break ;
    case MYSQL_INFHOSTQ_OPT:
      if (MysqlHandle[hand].connection == 0)
	return TCL_OK ; /* Return empty string if not connected. */
      break ;
    default: /* unknown option */
      sprintf (buf, "'%s' unknown option", argv[2]);
      return mysql_prim_confl (buf) ;
    }

  if (hand < 0)
      return TCL_ERROR ;

  /* Handle OK, return the requested info. */
  switch (idx)
    {
    case MYSQL_INFNAME_OPT:
    case MYSQL_INFNAMEQ_OPT:
      strcpy (interp->result, MysqlHandle[hand].database) ;
      break ;
    case MYSQL_INFTABLES_OPT:
      if ((list = mysql_list_tables (MysqlHandle[hand].mysql,(char*)NULL)) == NULL)
	return mysql_prim_confl ("could not access table names; server may have gone away") ;

      for (count = mysql_num_rows (list); count > 0; count--)
	{
	  val = *(row = mysql_fetch_row (list)) ;
	  Tcl_AppendElement (interp, (val == NULL)?"":val) ;
	}
      mysql_free_result (list) ;
      break ;
    case MYSQL_INFHOST_OPT:
    case MYSQL_INFHOSTQ_OPT:
      strcpy (interp->result, MysqlHandle[hand].host) ;
      break ;
    case MYSQL_INFLIST_OPT:
      if ((list = mysql_list_dbs (MysqlHandle[hand].mysql,(char*)NULL)) == NULL)
	return mysql_prim_confl ("could not access database names; server may have gone away") ;

      for (count = mysql_num_rows (list); count > 0; count--)
	{
	  val = *(row = mysql_fetch_row (list)) ;
	  Tcl_AppendElement (interp, (val == NULL)?"":val) ;
	}
      mysql_free_result (list) ;
      break ;
    default: /* should never happen */
      return mysql_prim_confl ("weirdness in Mysqltcl_Info") ;
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

int
Mysqltcl_Result (clientData, interp, argc, argv)
     ClientData   clientData;
     Tcl_Interp  *interp;
     int          argc;
     char       **argv;
{
  char buf[MYSQL_BUFF_SIZE];
  int count ;
  int hand ;
  int idx ;
  MYSQL_RES* list ;
  MYSQL_ROW row ;


  /* We can't fully check the handle at this stage. */
  if ((hand = mysql_prologue(interp, argc, argv, 3, get_handle_plain,
			    " handle option")) < 0)
    return TCL_ERROR;

  for (idx = 0;
       idx <= MYSQL_RES_OPT_MAX && strcmp (argv[2], MysqlResultOpt[idx]) != 0;
       idx++) ;

  /* First check the handle. Checking depends on the option. */
  switch (idx)
    {
    case MYSQL_RESROWS_OPT:
    case MYSQL_RESCOLS_OPT:
    case MYSQL_RESCUR_OPT:
      hand = get_handle_res (argv[1]) ;
      break ;
    case MYSQL_RESROWSQ_OPT:
    case MYSQL_RESCOLSQ_OPT:
    case MYSQL_RESCURQ_OPT:
      if ((hand = get_handle_db (argv[1])) >= 0)
	{
	  if (MysqlHandle[hand].result == NULL)
	    return TCL_OK ; /* Return empty string if no pending result. */
	}
      break ;
    default: /* unknown option */
      sprintf (buf, "'%s' unknown option", argv[2]);
      return mysql_prim_confl (buf) ;
    }

  if (hand < 0)
    return TCL_ERROR ;

  /* Handle OK; return requested info. */
  switch (idx)
    {
    case MYSQL_RESROWS_OPT:
    case MYSQL_RESROWSQ_OPT:
      sprintf (interp->result, "%d", MysqlHandle[hand].res_count) ;
      break ;
    case MYSQL_RESCOLS_OPT:
    case MYSQL_RESCOLSQ_OPT:
      sprintf (interp->result, "%d", MysqlHandle[hand].col_count) ;
      break ;
    case MYSQL_RESCUR_OPT:
    case MYSQL_RESCURQ_OPT:
      sprintf (interp->result, "%d", mysql_num_rows (MysqlHandle[hand].result)
	       - MysqlHandle[hand].res_count) ;
    default:
      ;
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

Mysqltcl_Col (clientData, interp, argc, argv)
     ClientData   clientData;
     Tcl_Interp  *interp;
     int          argc;
     char       **argv;
{
  char buf[MYSQL_BUFF_SIZE];
  int coln ;
  int conflict ;
  int current_db ;
  int hand;
  int idx ;
  int listArgc ;
  char** listArgv ;
  MYSQL_FIELD* fld ;
  MYSQL_RES* result ;
  char* sep ;
  int simple ;
  
  /* This check is enough only without '-current'. */
  if ((hand = mysql_prologue(interp, argc, argv, 4, get_handle_db,
			    "handle table-name option ?option ...?")) < 0)
    return TCL_ERROR;

  /* Fetch column info.
   * Two ways: explicit database and table names, or current.
   */
  current_db = strcmp (argv[2], "-current") == 0 ;
  
  if (current_db)
    {
      if ((hand = get_handle_res (argv[1])) < 0)
	return TCL_ERROR ;
      else
	result = MysqlHandle[hand].result ;
    }
  else
    {
/*      if ((result = mysql_list_fields (MysqlHandle[hand].connection, argv[2], (char*)NULL)) == NULL)	*/
      if ((result = mysql_list_fields (MysqlHandle[hand].mysql, argv[2], (char*)NULL)) == NULL)
	{
	  sprintf (buf, "no column info for table '%s'; %s", argv[2],
		   "server may have gone away") ;
	  return mysql_prim_confl (buf) ;
	}
    }

  /* Must examine the first specifier at this point. */
  if (Tcl_SplitList (interp, argv[3], &listArgc, &listArgv) != TCL_OK)
    return TCL_ERROR ;

  conflict = 0 ;
  simple = (argc == 4) && (listArgc == 1) ;

  if (simple)
    {
      mysql_field_seek (result, 0) ;
      while ((fld = mysql_fetch_field (result)) != NULL)
	if (mysql_colinfo (interp, fld, argv[3]))
	  {
	    conflict = 1 ;
	    break ;
	  }
    }
  else if (listArgc > 1)
    {
      mysql_field_seek (result, 0) ;
      for (sep = "{"; (fld = mysql_fetch_field (result)) != NULL; sep = " {")
	{
	  Tcl_AppendResult (interp, sep, (char*)NULL) ;
	  for (coln = 0; coln < listArgc; coln++)
	    if (mysql_colinfo (interp, fld, listArgv[coln]))
	      {
		conflict = 1 ;
		break ;
	      }
	  if (conflict)
	    break ;
	  Tcl_AppendResult (interp, "}", (char*)NULL) ;
	}
      ckfree ((char*)listArgv) ;
    }
  else
    {
      ckfree ((char*)listArgv) ; /* listArgc == 1, no splitting */
      for (idx = 3, sep = "{"; idx < argc; idx++, sep = " {")
	{
	  Tcl_AppendResult (interp, sep, (char*)NULL) ;
	  mysql_field_seek (result, 0) ;
	  while ((fld = mysql_fetch_field (result)) != NULL)
	    if (mysql_colinfo (interp, fld, argv[idx]))
	      {
		conflict = 1 ;
		break ;
	      }
	  if (conflict)
	    break ;
	  Tcl_AppendResult (interp, "}", (char*)NULL) ;
	}
    }
  
  if (!current_db)
    mysql_free_result (result) ;
  return (conflict)?TCL_ERROR:TCL_OK ;
}


/*
 *----------------------------------------------------------------------
 *
 * Mysqltcl_State
 *    Implements the mysqlstate command:
 *    usage: mysqlstate ?-numeric? handle 
 *	                
 */

Mysqltcl_State (clientData, interp, argc, argv)
     ClientData   clientData;
     Tcl_Interp  *interp;
     int          argc;
     char       **argv;
{
  int hi;
  char* hp ;
  int numeric ;
  char* res ;
  
  if (mysql_prologue(interp, argc, argv, 2, NULL, "?-numeric? handle") < 0)
    return TCL_ERROR;

  if ((numeric = (strcmp (argv[1], "-numeric") == 0)) && argc < 3)
    return mysql_prim_confl ("handle required") ;
  
  hp = (numeric)?argv[2]:argv[1] ;

  if (HSYNTAX(hp,hi) < 0)
    res = (numeric)?"0":"NOT_A_HANDLE" ;
  else if (MysqlHandle[hi].connection == 0)
    res = (numeric)?"1":"UNCONNECTED" ;
  else if (MysqlHandle[hi].database[0] == '\0')
    res = (numeric)?"2":"CONNECTED" ;
  else if (MysqlHandle[hi].result == NULL)
    res = (numeric)?"3":"IN_USE" ;
  else
    res = (numeric)?"4":"RESULT_PENDING" ;

  (void)strcpy (interp->result, res) ;
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

Mysqltcl_InsertId (clientData, interp, argc, argv)
     ClientData   clientData;
     Tcl_Interp  *interp;
     int          argc;
     char       **argv;
{
  int hand;
  char* res ;
  MYSQL* mysql;
  
  if ((hand = mysql_prologue(interp, argc, argv, 2, get_handle_conn,
			    "handle")) < 0)
    return TCL_ERROR;

  mysql = MysqlHandle[hand].mysql;

  (void)sprintf (interp->result, "%d", mysql_insert_id(mysql)) ;

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

Mysqltcl_Close (clientData, interp, argc, argv)
     ClientData   clientData;
     Tcl_Interp  *interp;
     int          argc;
     char       **argv;
{
  int     hand;
  
  /* If handle omitted, close all connections. */
  if (argc == 1)
    {
      Mysqltcl_Kill ((ClientData)NULL) ;
      return TCL_OK ;
    }
  
  if ((hand = mysql_prologue(interp, argc, argv, 2, get_handle_conn,
			    "handle")) < 0)
    return TCL_ERROR;

  mysql_close (MysqlHandle[hand].connection) ;

  MysqlHandle[hand].connection = (MYSQL *)0 ;
  MysqlHandle[hand].host[0] = '\0' ;
  MysqlHandle[hand].database[0] = '\0' ;

  if (MysqlHandle[hand].result != NULL)
    mysql_free_result (MysqlHandle[hand].result) ;
    
  MysqlHandle[hand].result = NULL ;
  MysqlHandle[hand].res_count = 0 ;

  return TCL_OK;
}
