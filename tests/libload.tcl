set file libmysqltcl2.51

if {[file exists ./${file}[info sharedlibextension]]} {
    load ./${file}[info sharedlibextension]
} else {
    load ../${file}[info sharedlibextension]
}

