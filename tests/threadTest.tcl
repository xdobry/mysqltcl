package require Thread

for {set i 1} {$i <= 20} {incr i} {
     set threadId [thread::create]
     after 30
     thread::send -async $threadId {
          package require mysqltcl
          set mysqlId [mysqlconnect -user root -db uni]
          set rows [mysqlsel $mysqlId {select * from Student}]
	  for {set x 0} {$x<$rows} {incr x} {
	      after 20
	      set res  [mysqlnext $mysqlId]
	      set nr [lindex $res 0]
	      set name [lindex $res 1]
	      set sem [lindex $res 2]
	  }
          puts "ready mysqltcl $mysqlId"
          after 1000
          catch {mysqlclose $mysqlId} error
          puts "[thread::id] $mysqlId $error"
          thread::release
     } 
}     
vwait endProg
