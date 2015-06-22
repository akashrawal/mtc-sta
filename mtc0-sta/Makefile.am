lib_LTLIBRARIES = libmtc0-sta.la

#Generated source files
BUILT_SOURCES = simple_router_declares.h simple_router_defines.h
CLEANFILES = simple_router_declares.h simple_router_defines.h 

simple_router_declares.h simple_router_defines.h: simple_router.mdl
	$(MDLC) $(srcdir)/simple_router.mdl

EXTRA_DIST = simple_router.mdl

mtc_sta_c = \
	io.c \
	event.c \
	fd_link.c \
	simple_router.c \
	simple_server.c

mtc_sta_h = \
	common.h \
	mtc-sta.h \
	io.h \
	event.h \
	fd_link.h \
	simple_router.h \
	simple_server.h

libmtc0_sta_la_SOURCES = $(mtc_sta_c) $(mtc_sta_h)
nodist_libmtc0_sta_la_SOURCES = \
	simple_router_declares.h simple_router_defines.h
                          
libmtc0_sta_la_CFLAGS = -Wall -I$(top_builddir)
libmtc0_sta_la_LIBADD = $(MTC_LIBS) -levent_core

mtcincludedir = $(includedir)/mtc0-sta
mtcinclude_HEADERS = $(mtc_sta_h)
