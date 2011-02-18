set file libmysqltcl3.01

if {[file exists ./${file}[info sharedlibextension]]} {
    load ./${file}[info sharedlibextension]
} else {
    load ../${file}[info sharedlibextension]
}

