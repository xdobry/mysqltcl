#!/usr/bin/tcl
# Write and read file into database
# using 
# binarytest.tcl file
# The output file will be written with file.bin

if {[file exists ./libmysqltcl2.14.so]} {
    load ./libmysqltcl2.14.so
} else {
    load ../libmysqltcl2.14.so
}

set handle [mysqlconnect -user root -db uni]

set file [lindex $argv 0]
set fhandle [open $file r]
fconfigure $fhandle -translation binary
set binary [read $fhandle]
close $fhandle

mysqlexec $handle "INSERT INTO Binarytest (data) VALUES ('[mysqlescape $binary]')"
set id [mysqlinsertid $handle]

set nfile [file tail $file].bin
set fhandle [open $nfile w]
fconfigure $fhandle -translation binary
#set nbinary [lindex [lindex [mysqlsel $handle "SELECT data from Binarytest where id=$id" -list] 0] 0]
mysqlsel $handle "SELECT data from Binarytest where id=$id"
set nbinary [encoding convertfrom utf-8 [lindex [mysqlnext $handle] 0]]
puts "primary length [string bytelength $binary] new length [string bytelength $nbinary] - [string length $binary]  [string length $nbinary]"
puts -nonewline $fhandle $nbinary
close $fhandle
