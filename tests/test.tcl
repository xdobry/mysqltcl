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

set handle [mysqlconnect -user root]

# use implicit database notation
puts "1 rows [mysqlsel $handle {select * from uni.Student}]"
puts "1 Table-col [mysqlcol $handle -current {name type length table non_null prim_key decimals numeric}]"
puts "1 [mysqlnext $handle]"

# Test sel and next functions
mysqluse $handle uni
puts "rows [mysqlsel $handle {select * from Student}]"
puts "Table-col [mysqlcol $handle -current {name type length table non_null prim_key decimals numeric}]"
# result status
puts "cols [mysqlresult $handle cols]"
puts "rows [mysqlresult $handle rows]"
puts "rows [mysqlresult $handle current]"


puts [mysqlnext $handle]
puts "current [mysqlresult $handle current]"
puts [mysqlnext $handle]
puts [mysqlnext $handle]
mysqlseek $handle 0
puts "seek to first tupel"
puts [mysqlnext $handle]
puts [mysqlnext $handle]

# Test map function
mysqlsel $handle {select MatrNr,Name
    from Student
    order by Name}

mysqlmap $handle {nr name} {
    if {$nr == {}} continue
    set tempr [list $nr $name]
    puts  [format  "nr %16s  name:%s"  $nr $name]
}

# Test query function
puts "Testing mysqltclquery"
set query1 [mysqlquery $handle {select MatrNr,Name From Student Order By Name}]
puts [mysqlnext $query1]
set query2 [mysqlquery $handle {select MatrNr,Name From Student Order By Name}]
puts [mysqlnext $query2]
mysqlendquery $query1
puts [mysqlnext $query2]
puts "cols [mysqlresult $query2 cols]"
puts "rows [mysqlresult $query2 rows]"
puts "current [mysqlresult $query2 current]"
mysqlseek $query2 0
puts [mysqlnext $query2]
puts "current after seek [mysqlresult $query2 current]"
puts "1 Table-col [mysqlcol $query2 -current {name type length table non_null prim_key decimals numeric}]"
# Free Handles (memory)
unset query1
mysqlendquery $query2
catch {
    mysqlnext "$query2"
}
puts "reading after endquery: $query2: $errorInfo"

unset query2

mysqlendquery $handle

puts "code=$mysqlstatus(code) command=$mysqlstatus(command) message=$mysqlstatus(message) nullvalue=$mysqlstatus(nullvalue)"

# Mysqlexec Test
mysqlexec $handle {INSERT INTO Student (Name,Semester) VALUES ('Artur Trzewik',11)}
puts "newid [set newid [mysqlinsertid $handle]]"
mysqlexec $handle "UPDATE Student SET Semester=12 WHERE MatrNr=$newid"
puts "Info [mysqlinfo $handle info]"
set affected [mysqlexec $handle "DELETE FROM Student WHERE MatrNr=$newid"]
puts "affected rows by DELETE $affected"

# Test NULL Value setting
mysqlexec $handle {INSERT INTO Student (Name) VALUES ('Null Value')}
set id [mysqlinsertid $handle]
set mysqlstatus(nullvalue) NULL
set res [lindex [mysqlsel $handle "select Name,Semester from Student where MatrNr=$id" -list] 0]
if {[lindex $res 1]!="NULL"} {error "no expected NULL value $res"}

# Metadata querries
puts "Table-col [mysqlcol $handle Student name]"
puts "Table-col [mysqlcol $handle Student {name type length table non_null prim_key decimals numeric}]"

# Info  
puts "databases: [mysqlinfo $handle databases]"
puts "dbname: [mysqlinfo $handle dbname]"
puts "host?: [mysqlinfo $handle  host?]"
puts "tables: [mysqlinfo $handle tables]"
 
# State
puts "state: [mysqlstate $handle]"
puts "state numeric: [mysqlstate $handle -numeric]"


# Error Handling
puts "Error Handling"

# bad handle
catch { mysqlsel bad0 {select * from Student} }
puts $errorInfo
puts "code=$mysqlstatus(code) command=$mysqlstatus(command) message=$mysqlstatus(message) nullvalue=$mysqlstatus(nullvalue)"
# bad querry 
catch { mysqlsel $handle {select * from Unknown} }
puts $errorInfo
puts "code=$mysqlstatus(code) command=$mysqlstatus(command) message=$mysqlstatus(message) nullvalue=$mysqlstatus(nullvalue)"
# bad command
catch { mysqlexec $handle {unknown command} }
puts $errorInfo
puts "code=$mysqlstatus(code) command=$mysqlstatus(command) message=$mysqlstatus(message) nullvalue=$mysqlstatus(nullvalue)"
# read after end by sel
set rows [mysqlsel $handle {select * from Student}]
for {set x 0} {$x<$rows} {incr x} {
    set res  [mysqlnext $handle]
    set nr [lindex $res 0]
    set name [lindex $res 1]
    set sem [lindex $res 2]
}
puts "afterend [mysqlnext $handle]"
puts "read after end"

#read after end by map
mysqlsel $handle {select * from Student}
mysqlmap $handle {nr name} {
    puts  [format  "nr %16s  name:%s"  $nr $name]
}
mysqlseek $handle 0
catch {
    mysqlmap $handle {nr name ere ere} {
	puts  [format  "nr %16s  name:%s"  $nr $name]
    }
}
puts $errorInfo

puts [mysqlsel $handle {select * from Student} -list]
puts [mysqlsel $handle {select * from Student} -flatlist]

set shandle [string trim " $handle "]
mysqlinfo $shandle databases

mysqlclose $handle

# Test Tcl_Obj pointer after closing handles

set a " $handle "
if {![catch {mysqlinfo $handle tables}]} {
    puts "handle should be closed (dangling pointer)"
}
unset handle
set a [string trim $a]
if {![catch {mysqlinfo $a tables}]} {
    puts "handle should be closed"
}


# Test multi-connection 20 handles

puts "multiconnect"
for {set x 0} {$x<20} {incr x} {
    lappend handles [mysqlconnect -user root -db uni]
}
foreach h $handles {
    puts "sel $h"
    mysqlsel $h {select * from Student}
}
puts "Close all"
mysqlclose

# Test close of handle with multi queries

set handle [mysqlconnect -user root -db uni]
for {set x 0} {$x<10} {incr x} {
    puts "new query"
    lappend queries [mysqlquery $handle {select * from Student}]
}
for {set x 0} {$x<10} {incr x} {
    puts "new query"
    mysqlquery $handle {select * from Student}
}
puts "closing"
mysqlclose $handle

catch {
    mysqlnext [lindex $queries 0]
}
puts $errorInfo

# closing handles with opened queries
set handle [mysqlconnect -user root -db uni]
mysqlquery $handle {select * from Student}
mysqlclose

# Testing false connecting
if {![catch {mysqlconnect -user nouser -db nodb}]} {
   error "this connection should not exist"
}

# Test of encondig option
set handle [mysqlconnect -user root -db uni -encoding iso8859-1]

set name {Artur Trzewik}
mysqlexec $handle "INSERT INTO Student (Name,Semester) VALUES ('$name',11)"
set newid [mysqlinsertid $handle]
set rname [lindex [lindex [mysqlsel $handle "select Name from Student where MatrNr = $newid" -list] 0] 0]
if {$rname!=$name} {
    error "read write with encoding fails $rname == $name"
}
mysqlexec $handle "DELETE FROM Student WHERE MatrNr = $newid"

# escaping

mysqlescape "art\"ur"
mysqlescape $handle "art\"ur"

# ping

if {![mysqlping $handle]} {
    error "connection shoul be alive"
}

# change user

mysqlchangeuser $handle root {}
mysqlchangeuser $handle root {} uni 
if {![catch {mysqlchangeuser $handle nonuser {} uni}]} {
    error "this mysqlchangeuser should cause error"
} else {
   puts $errorInfo
}

mysqlclose $handle

catch {
    mysqlconnect -user root -db uni -encoding nonexisting
}
puts $errorInfo

# endcoding
set handle [mysqlconnect -user root -db uni -encoding binary]
mysqlclose $handle

# testing connection flags

puts "testing connection options"
set handle [mysqlconnect -user root -db uni -ssl 1]
mysqlclose $handle

set handle [mysqlconnect -user root -db uni -noschema 1]
mysqlclose $handle

set handle [mysqlconnect -user root -db uni -noschema 0]
mysqlclose $handle

set handle [mysqlconnect -user root -db uni -compress 1]
mysqlclose $handle

set handle [mysqlconnect -user root -db uni -odbc 1]
mysqlclose $handle

# testing base info

puts [mysqlbaseinfo connectparameters]
puts [mysqlbaseinfo clientversion]

# testing different interpreters

set handle [mysqlconnect -user root -db uni]
set i1 [interp create]
$i1 eval {
  package require mysqltcl
  set hdl [mysqlconnect -user root -db uni]
  puts $hdl
}
interp delete $i1
mysqlinfo $handle databases
mysqlclose $handle

puts "End of test"
