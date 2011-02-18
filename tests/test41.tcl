#!/usr/bin/tcl
# Simple Test file to test all mysqltcl commands and parameters
# up from version mysqltcl 3.0 and mysql 4.1
# please create test database first
# from test.sql file
# >mysql -u root
# >create database uni;
#
# >mysql -u root <test.sql
# please adapt the parameters for mysqlconnect some lines above

if {[file exists libload.tcl]} {
    source libload.tcl
} else {
    source [file join [file dirname [info script]] libload.tcl]
}

package require tcltest
variable SETUP {#common setup code}
variable CLEANUP {#common cleanup code}
tcltest::configure -verbose bet

proc setConnect {} {
   global conn
   set conn [mysqlconnect -user root -db uni -multistatement 1]
}
# Create Table suitable for transaction tests
proc initTestTable {} {
   global conn
   # drop table if exists
   catch {mysql::exec $conn {drop table transtest}}
   mysql::exec $conn {
      create table transtest (
         id int,
         name varchar(20)
      ) ENGINE=INNODB
   }
}

tcltest::test {null-1.0} {creating of null} {
  set null [mysql::newnull]
  mysql::isnull $null
} {1}

tcltest::test {null-1.1} {null checking} {
  mysql::isnull blabla
} {0}

tcltest::test {null-1.2} {null checking} {
  mysql::isnull [mysql::newnull]
} {1}

tcltest::test {null-1.3} {null checking} {
  mysql::isnull {}
} {0}

tcltest::test {null-1.4} {null checking} {
  mysql::isnull [lindex [list [mysql::newnull]] 0]
} {1}

# We need connection for folowing tests
setConnect
initTestTable

tcltest::test {autocommit} {setting autocommit} -body {
   mysql::autocommit $conn 0
}
tcltest::test {autocommit} {setting autocommit} -body {
   mysql::autocommit $conn 1
}
tcltest::test {autocommit} {setting false autocommit} -body {
   mysql::autocommit $conn nobool
} -returnCodes error -match glob -result "expected boolean value*"

mysql::autocommit $conn 0

tcltest::test {commit} {commit} -body {
   mysqlexec $conn {delete from transtest where name='committest'}
   mysqlexec $conn {insert into transtest (name,id) values ('committest',2)}
   mysql::commit $conn
   set res [mysqlexec $conn {delete from transtest where name='committest'}]
   mysql::commit $conn
   return $res
} -result 1

tcltest::test {rollback-1.0} {roolback} -body {
   mysqlexec $conn {delete from transtest where name='committest'}
   mysqlexec $conn {insert into transtest (name,id) values ('committest',2)}
   mysql::rollback $conn
   set res [mysqlexec $conn {delete from transtest where name='committest'}]
   mysql::commit $conn
   return $res
} -result 0

tcltest::test {rollback-1.1} {roolback by auto-commit 1} -body {
   mysql::autocommit $conn 1
   mysqlexec $conn {delete from transtest where name='committest'}
   mysqlexec $conn {insert into transtest (name,id) values ('committest',2)}
   # rollback should not affect
   mysql::rollback $conn
   set res [mysqlexec $conn {delete from transtest where name='committest'}]
   return $res
} -result 1


tcltest::test {warning-count-1.0} {check mysql::warningcount} -body {
   set list [mysql::sel $conn {select * from Student} -list]
   mysql::warningcount $conn
} -result 0


tcltest::test {multistatement-1.0} {inserting multi rows} -body {
   mysql::exec $conn {
      insert into transtest (name,id) values ('row1',31);
      insert into transtest (name,id) values ('row2',32);
      insert into transtest (name,id) values ('row3',33);
   }
} -result 1

tcltest::test {moreresult-1.3} {arg counts} -body {
   mysql::moreresult
} -returnCodes error -match glob -result "wrong # args:*"

tcltest::test {moreresult-1.0} {only one result} -body {
   mysql::ping $conn
   mysql::sel $conn {select * from transtest}
   mysql::moreresult $conn
} -result 0

tcltest::test {moreresult-1.1} {only one result} -body {
   mysql::ping $conn
   mysql::sel $conn {
      select * from transtest;
      select * from Student;
   }
   while {[llength [mysql::fetch $conn]]>0} {}
   mysql::moreresult $conn
} -result 1

tcltest::test {nextresult-1.0} {only one result} -body {
   mysql::ping $conn
   mysql::sel $conn {
      select * from transtest;
      select * from Student;
   }
   while {[llength [set row [mysql::fetch $conn]]]>0} {
   }
   mysql::nextresult $conn
   set hadRow 0
   while {[llength [set row [mysql::fetch $conn]]]>0} {
      set hadRow 1
   }
   return $hadRow
} -result 1 -returnCodes 2

tcltest::test {setserveroption-1.0} {set multistatment off} -body {
   mysql::setserveroption $conn -multi_statment_off
   mysql::exec $conn {
      insert into transtest (name,id) values ('row1',31);
      insert into transtest (name,id) values ('row2',32);
      insert into transtest (name,id) values ('row3',33);
   }
} -returnCodes error -match glob -result "mysql::exec/db server*"

tcltest::test {setserveroption-1.1} {set multistatment on} -body {
   mysql::setserveroption $conn -multi_statment_on
   mysql::exec $conn {
      insert into transtest (name,id) values ('row1',31);
      insert into transtest (name,id) values ('row2',32);
      insert into transtest (name,id) values ('row3',33);
   }
   return
}

tcltest::test {info-1.0} {asking about host} -body {
   set res [mysql::info $conn host]
   expr {[string length $res]>0}
} -result 1

tcltest::test {info-1.1} {serverversion} -body {
  mysql::info $conn serverversion
  expr {[mysql::info $conn serverversionid]>0}
} -result 1

tcltest::test {info-1.2} {sqlstate} -body {
  mysql::info $conn sqlstate
  return
}

tcltest::test {state-1.0} {reported bug in 3.51} -body {
  mysql::state nothandle -numeric
} -result 0

tcltest::test {state-1.1} {reported bug in 3.51} -body {
  mysql::state nothandle
} -result NOT_A_HANDLE

tcltest::test {null-2.0} {reading and checking null from database} -body {
  mysql::ping $conn
  mysql::autocommit $conn 1
  mysql::exec $conn {
       delete from transtest where name="nulltest"
  }
  mysql::exec $conn {
       insert into transtest (name,id) values ('nulltest',NULL);
  }
  mysql::sel $conn {select id from transtest where name='nulltest'}
  set res [lindex [mysql::fetch $conn] 0]
  mysql::isnull $res
} -result 1

tcltest::test {baseinfo-1.0} {clientversionid} -body {
  expr {[mysql::baseinfo clientversionid]>0}
} -result 1


# no prepared statements in this version
if 0 {

tcltest::test {preparedstatment-1.0} {create test} -body {
  set phandle [mysql::prepare $conn {insert into transtest (id,name) values (?,?)}]
  mysql::close $phandle
  return
}

tcltest::test {preparedstatment-1.1} {create errortest} -body {
  set phandle [mysql::prepare $conn {nosql command ?,?}]
  mysql::close $phandle
  return
} -returnCodes error -match glob -result "*SQL*"

tcltest::test {preparedstatment-1.3} {select} -body {
  set phandle [mysql::prepare $conn {select id,name from transtest}]
  mysql::pselect $phandle
  set rowcount 0
  while {[llength [set row [mysql::fetch $phandle]]]>0} {
  	 incr rowcount
  }
  mysql::close $phandle
  return
}


tcltest::test {preparedstatment-1.2} {insert} -body {
  set phandle [mysql::prepare $conn {insert into transtest (id,name) values (?,?)}]
  set count [mysql::param $phandle count]
  mysql::param $phandle type 0
  mysql::param $phandle type 1
  mysql::param $phandle type
  mysql::pexecute $phandle 2 Artur
  mysql::close $phandle
  return $count
} -result 2


tcltest::test {preparedstatment-1.4} {select mit bind} -body {
  set phandle [mysql::prepare $conn {select id,name from transtest where id=?}]
  set countin [mysql::paramin $phandle count]
  set countout [mysql::paramin $phandle count]
  mysql::paramin $phandle type 0
  mysql::paramin $phandle type
  mysql::paramout $phandle type 0
  mysql::paramout $phandle type 1
  mysql::paramout $phandle type
  mysql::execute $phandle
  mysql::close $phandle
  list $countin $countout
} -result {1 2}

}

tcltest::cleanupTests

puts "End of test"

