# script for produce html and ngroff from tcl man page
# see doctools from tcllib

package require doctools

::doctools::new dl -file mysqltcl.man -format html
set file [open mysqltcl.html w]
set filein [open mysqltcl.man]
puts $file [dl format [read $filein]]
close $filein
close $file

::doctools::new dl2 -file mysqltcl.man -format nroff
set file [open mysqltcl.n w]
set filein [open mysqltcl.man]
puts $file [dl2 format [read $filein]]
close $filein
close $file


