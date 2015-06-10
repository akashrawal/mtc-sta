lib_LTLIBRARIES = libmtc0-sta.la

mtc_sta_c = fd_link.c

mtc_sta_h = common.h mtc-sta.h

libmtc0_sta_la_SOURCES = $(mtc_sta_c) $(mtc_sta_h)
                          
libmtc0_sta_la_CFLAGS = -Wall
libmtc0_sta_la_LIBADD  = $(MTC_LIBS)

mtcincludedir = $(includedir)/mtc0-sta
mtcinclude_HEADERS = $(mtc_sta_h)
