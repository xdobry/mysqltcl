#!/usr/bin/tcl
# Simple Test file to test all mysqltcl commands and parameters
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


# global connect variables
set dbuser root
set dbpassword ""
set dbank mysqltcltest

package require tcltest
variable SETUP {#common setup code}
variable CLEANUP {#common cleanup code}
tcltest::configure -verbose bet

proc getConnection {{addOptions {}} {withDB 1}} {
    global dbuser dbpassword dbank
    if {$withDB} {
        append addOptions " -db $dbank"
    }
    if {$dbpassword ne ""} {
	    append addOptions " -password $dbpassword"
    }
    return [eval mysql::connect -user $dbuser $addOptions]
}
proc prepareTestDB {} {
    global dbank
    set handle [getConnection {} 0]
    if {[lsearch [mysql::info $handle databases] $dbank]<0} {
        puts "Testdatabase $dbank does not exist. Create it"
        mysql::exec $handle "CREATE DATABASE $dbank"
    }
    mysql::use $handle $dbank
    
    catch {mysql::exec $handle {drop table Student}}

    mysql::exec $handle {
       	CREATE TABLE Student (
 		MatrNr int NOT NULL auto_increment,
		Name varchar(20),
		Semester int,
		PRIMARY KEY (MatrNr)
	)
    }
    mysql::exec $handle "INSERT INTO Student VALUES (1,'Sojka',4)"
    mysql::exec $handle "INSERT INTO Student VALUES (2,'Preisner',2)"
    mysql::exec $handle "INSERT INTO Student VALUES (3,'Killar',2)"
    mysql::exec $handle "INSERT INTO Student VALUES (4,'Penderecki',10)"
    mysql::exec $handle "INSERT INTO Student VALUES (5,'Turnau',2)"
    mysql::exec $handle "INSERT INTO Student VALUES (6,'Grechuta',3)"
    mysql::exec $handle "INSERT INTO Student VALUES (7,'Gorniak',1)"
    mysql::exec $handle "INSERT INTO Student VALUES (8,'Niemen',3)"
    mysql::exec $handle "INSERT INTO Student VALUES (9,'Bem',5)"
    mysql::close $handle
}

prepareTestDB

# no prepared statements in this version

tcltest::test {open-parameter-reconect} {reconnect} -body {
	set handle [getConnection {-reconnect 1}]
	mysql::close $handle
}

tcltest::test {open-parameter-new} {new options} -body {
	set handle [getConnection {-read-timeout 1000 -write-timeout 1000 -connect-timeout 1000 -protocol 0 -init-command "select 1" -ssl-verify-cert 1 -secure-auth 1}]
	mysql::close $handle
}

set conn [getConnection]

tcltest::test {mysql-sel-as-execute} {asexec} -body {
	mysql::sel $conn "INSERT INTO Student VALUES (10,'Kora',3)"
} -result -1

tcltest::test {mysql-sel-as-execute} {asexec} -body {
	mysql::sel $conn "INSERT INTO Student VALUES (11,'Kora',3)"
	mysql::result $conn affected_rows
} -result 1

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
