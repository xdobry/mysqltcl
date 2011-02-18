#!/usr/bin/tcl

if {[file exists ./libmysqltcl2.11.so]} {
    load ./libmysqltcl2.11.so
} else {
    load ../libmysqltcl2.11.so
}

puts "please observe memory consumption per top (Program break after reach 2000)"

set c [mysqlconnect -u root -db uni]
set i 0
set p 0
while 1 {
    set a [mysqlsel $c "select * from Student" -list]
    mysqlsel $c {select * from Student}
    while {[set row [mysqlnext $c]]!=""} {}
    set q [mysqlquery $c {select * from Student}]
    while {[set row [mysqlnext $q]]!=""} {}
    mysqlendquery $q
    mysqlsel $c {select MatrNr,Name,Semester from Student}
    mysqlmap $c {MatrNr Name Semester} {
	set all [list $MatrNr $Name $Semester]
    }
    if {$i>100} {puts "loop [incr p]"; set i 0}
    incr i
    if {$p>1000} break
}
