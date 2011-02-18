#
# Makefile for mysqltcl 1.53
#

CC = gcc
CFLAGS = -O2

XHOME    = /usr/X11
TCLHOME  = /usr
TKHOME   = /usr
MYSQLHOME = /usr/local/mysql

SHARED = yes
#PLATFORM=SunOS-5.5.1-sparc
#PLATFORM=SunOS-5.6-sparc
PLATFORM=Linux-2.0-i586

ifeq ($(PLATFORM), SunOS-5.5.1-sparc)
  LIBS = -lsocket -lnsl
  LD = ld
  SHLIB_CFLAGS = 
  SHLIB_LDFLAGS = -G
  SHLIB_PRE = lib
  SHLIB_EXT = .so
  # There might be better way...
  SHLIBS = /usr/local/lib/gcc-lib/sparc-sun-solaris2.5/2.7.2.1/libgcc.a
endif

ifeq ($(PLATFORM), SunOS-5.6-sparc)
  LD = ld
  SHLIB_CFLAGS = 
  SHLIB_LDFLAGS = -G
  SHLIB_PRE = lib
  SHLIB_EXT = .so
  LIBS = -lsocket -lxnet 
  # There might be better way...
  SHLIBS = /usr/local/lib/gcc-lib/sparc-sun-solaris2.6/2.7.2.1/libgcc.a
endif

ifeq ($(PLATFORM), Linux-2.0-i586)
  LD = ld
  SHLIB_CFLAGS = -fpic
  SHLIB_LDFLAGS = -shared
  SHLIB_PRE = lib
  SHLIB_EXT = .so
endif

#===== END OF CONFIGURATION DEFINITIONS =====

ifeq ($(SHARED), yes)
  SHLIB    = $(SHLIB_PRE)mysqltcl$(SHLIB_EXT)
  CFLAGS  += $(SHLIB_CFLAGS)
  LDFLAGS += $(SHLIB_LDFLAGS)
  OUTPUT   = $(SHLIB)
else
  OUTPUT   = mysqltclsh mysqlwish
endif

CPPFLAGS  = -I$(XHOME)/include -I$(TCLHOME)/include -I$(TKHOME)/include \
	-I$(MYSQLHOME)/include
LOADLIBES = -L$(XHOME)/lib -L$(TCLHOME)/lib -L$(TKHOME)/lib \
	-L$(MYSQLHOME)/lib -lmysqlclient -ltk8.0 -ltcl8.0 -lX11 -lm -ldl $(LIBS) 

all: $(OUTPUT)

libmysqltcl.so: mysqltcl.o
	$(LD) $(LDFLAGS) -o $@ mysqltcl.o $(LOADLIBES) $(SHLIBS)

mysqltclsh: mysqltcl.o

mysqlwish: mysqltcl.o 

clean: FORCE
	rm -f *.o *~ core mysqltclsh mysqlwish libmysqltcl.so 

VER=1.53
dist: FORCE
	$(MAKE) clean
	cd $(HOME); tar cvf mysqltcl-$(VER).tar mysqltcl-$(VER); \
	gzip -f mysqltcl-$(VER).tar; rm -f mysqltcl-$(VER).tar

FORCE:

