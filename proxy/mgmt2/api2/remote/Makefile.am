AM_CPPFLAGS = \
  -I$(top_srcdir)/libinktomi++ \
  -I$(top_srcdir)/librecords \
  -I$(top_srcdir)/proxy/mgmt2 \
  -I$(top_srcdir)/proxy/mgmt2/cli \
  -I$(top_srcdir)/proxy/mgmt2/cluster \
  -I$(top_srcdir)/proxy/mgmt2/preparse \
  -I$(top_srcdir)/proxy/mgmt2/utils \
  -I$(top_srcdir)/proxy/mgmt2/web \
  -I$(top_srcdir)/proxy \
  -I$(top_srcdir)/proxy/iocore \
  -I$(top_srcdir)/proxy/hdrs \
  -I$(top_srcdir)/proxy/mgmt2/api2 \
  -I$(top_srcdir)/proxy/mgmt2/api2/remote \
  -I$(top_srcdir)/proxy/mgmt2/api2/include

MGMT_DEFS = @MGMT_DEFS@
DEFS  += $(MGMT_DEFS)

##noinst_LIBRARIES = libmgmtapiremote.la 
##noinst_LTLIBRARIES = libmgmtapiremote.la 

bin_PROGRAMS = api_cli_remote

lib_LTLIBRARIES = libmgmtapiremote.la
#libmgmtapiremote_la_LDFLAGS = -static
libmgmtapiremote_la_SOURCES = \
  ../CfgContextImpl.cc \
  ../CfgContextManager.cc \
  ../CfgContextUtils.cc \
  ../CoreAPIShared.cc \
  ../EventCallback.cc \
  ../GenericParser.cc \
  ../INKMgmtAPI.cc \
  CoreAPIRemote.cc \
  EventRegistration.cc \
  EventRegistration.h \
  NetworkUtilsRemote.cc \
  NetworkUtilsRemote.h

api_cli_remote_SOURCES = APITestCliRemote.cc 
api_cli_remote_LDADD = \
  libmgmtapiremote.la \
  $(top_builddir)/libinktomi++/libinktomi++.a \
  @LIBTHREAD@ @LIBSOCKET@ @LIBNSL@ @LIBRESOLV@ \
  @LIBTCL@ @LIBDB@ @LIBRT@ @LIBDL@ @LIBSSL@ @LIBICONV@
