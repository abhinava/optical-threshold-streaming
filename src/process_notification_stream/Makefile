usage:
	@echo "See README file for more instructions"
	@echo "make all              	 Build all example files"
	@echo "make clean            	 Remove all built and intermediary files"
	@echo "make start            	 Start ConfD daemon and example notifier app using the builtin replay store"
	@echo "make stop             	 Stop any ConfD daemon and example notifier app"
	@echo "make nc-query         	 Run NETCONF query against ConfD"
	@echo "make nc-subscribe         Subscribe for the interface stream using NETCONF"
	@echo "make nc-replay            Replay the interface stream using NETCONF"
	@echo "make nc-filter            Replay the interface stream with a filter using NETCONF"
	@echo "make nc-subscribe-netconf Subscribe for the NETCONF stream using the NETCONF protocol"
	@echo "make cli     	     	 Start the CONFD Command Line Interface"
	@echo "make cli-c   	     	 Start the CONFD Command Line Interface, C-style"
	@echo "make cli-j   	     	 Start the CONFD Command Line Interface, J-style"

######################################################################
# Where is ConfD installed? Make sure CONFD_DIR points it out
CONFD_DIR ?= ../../..

# Include standard ConfD build definitions and rules
include $(CONFD_DIR)/src/confd/build/include.mk

# In case CONFD_DIR is not set (correctly), this rule will trigger
$(CONFD_DIR)/src/confd/build/include.mk:
	@echo 'Where is ConfD installed? Set $$CONFD_DIR to point it out!'
	@echo ''

## If you get irritated by the fail on warnings, set this variable
ifeq ($(shell echo $$FXS_NO_FAIL_ON_WARNING), true)
FXS_WERR     =
else
FXS_WERR     ?= --fail-on-warnings
endif

######################################################################
# Example specific definitions and rules
CONFD_FLAGS = --addloadpath $(CONFD_DIR)/etc/confd
START_FLAGS ?=
CONFD_SO = $(CONFD_DIR)/lib
UNAME := $(shell uname -m)

ifeq ($(UNAME), x86_64)
	CFLAGS += -m64
endif
ifeq ($(UNAME), i686)
	CFLAGS += -m32
endif
ifeq ($(UNAME), i386)
	CFLAGS += -m32
endif

BUSYBOX_BS = no

ifeq ($(BUSYBOX_BS), yes)
	CFLAGS += -DBUSYBOX
endif

CFLAGS	+= $(EXPAT_INC) -g
LIBS	+= $(EXPAT_LIB)

PROJ_HOME = ../../
YANG_PATH = $(PROJ_HOME)/yang
CFLAGS += -I$(YANG_PATH)
CONFD_FLAGS += --addloadpath $(YANG_PATH)

PROC_MON_STREAM_SRC_HOME = $(PROJ_HOME)/src/process_notification_stream
PROG_NAME = process_notifier
PROC_MON_STREAM_PROG = $(PROC_MON_STREAM_SRC_HOME)/$(PROG_NAME)

CXX = g++

all: process_notifier $(CDB_DIR) ssh-keydir
	@echo "Build complete"

process_notifier: process_monitor_notifier.o
	 $(CXX) $(PROC_MON_STREAM_SRC_HOME)/process_monitor_notifier.o $(LIBS) $(CFLAGS) -ansi -pedantic -o $(PROC_MON_STREAM_PROG)

process_monitor_notifier.o: $(PROC_MON_STREAM_SRC_HOME)/process_monitor_notifier.cpp \
	$(YANG_PATH)/openconfig-system-terminal.h \
	$(YANG_PATH)/openconfig-system-management.h \
	$(YANG_PATH)/openconfig-system.h \
	$(YANG_PATH)/openconfig-system-logging.h \
	$(YANG_PATH)/openconfig-inet-types.h \
	$(YANG_PATH)/openconfig-yang-types.h \
	$(YANG_PATH)/openconfig-types.h \
	$(YANG_PATH)/openconfig-procmon.h \
	$(YANG_PATH)/openconfig-procmon-ext.h \
	$(YANG_PATH)/openconfig-messages.h \
	$(YANG_PATH)/openconfig-alarms.h \
	$(YANG_PATH)/openconfig-alarm-types.h \
	$(YANG_PATH)/openconfig-platform.h \
	$(YANG_PATH)/openconfig-platform-types.h \
	$(YANG_PATH)/openconfig-platform-ext.h \
	$(YANG_PATH)/openconfig-aaa.h \
	$(YANG_PATH)/openconfig-aaa-types.h

%.o: %.cpp
	$(CXX) -c -g -I $(CONFD_DIR)/include -I $(YANG_PATH) $(CFLAGS) $<

%.h: %.fxs
	$(CONFDC) --emit-h $*.h $<

.SECONDARY:

%.fxs: %.yang
	$(CONFDC) $(FXS_WERR) $(EXTRA_LINK_FLAGS) --yangpath $(YANG_PATH) -c -o $@  $<

######################################################################
clean: xclean
	rm -rf $(PROC_MON_STREAM_PROG) *.o $(YANG_PATH)/*.h $(YANG_PATH)/*.fxs confd_prim.conf 2> /dev/null || true
	
xclean:
	rm -rf \
		*.o *.a *.xso *.fxs *.xsd *.ccl \
		*_proto.h \
		$(CDB_DIR) *.db aaa_cdb.* \
		rollback*/rollback{0..999} rollback{0..999} \
		cli-history \
		host.key host.cert ssh-keydir \
		*.log confderr.log.* \
		etc *.access \
		running.invalid global.data _tmp* local.data

start:  stop
	cp confd.conf confd_prim.conf
	$(CONFD) -c ./confd_prim.conf $(CONFD_FLAGS)
	LD_LIBRARY_PATH=$(CONFD_SO) $(PROC_MON_STREAM_PROG)

######################################################################
stop:
	### Stopping any confd daemon
	$(CONFD) --stop || true
	$(KILLALL) -r $(PROG_NAME) || true

######################################################################
nc-query:
	$(CONFD_DIR)/bin/netconf-console --get -x netconf

nc-subscribe:
	$(CONFD_DIR)/bin/netconf-console -s all sub.xml

nc-replay:
	$(CONFD_DIR)/bin/netconf-console -s all replay.xml

nc-filter:
	$(CONFD_DIR)/bin/netconf-console -s all filter.xml

nc-subscribe-netconf:
	$(CONFD_DIR)/bin/netconf-console --create-subscription=NETCONF

edit-config1:
	$(CONFD_DIR)/bin/netconf-console --edit-config=edit1.xml

edit-config2:
	$(CONFD_DIR)/bin/netconf-console --edit-config=edit2.xml

######################################################################

cli:
	$(CONFD_DIR)/bin/confd_cli --user=admin --groups=admin \
		--interactive || echo Exit

cli-c:
	$(CONFD_DIR)/bin/confd_cli -C --user=admin --groups=admin \
		--interactive || echo Exit

cli-j:
	$(CONFD_DIR)/bin/confd_cli -J --user=admin --groups=admin \
		--interactive || echo Exit
