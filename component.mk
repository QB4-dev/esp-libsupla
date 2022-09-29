#
# Component Makefile
#

COMPONENT_ADD_INCLUDEDIRS += libsupla/src
COMPONENT_ADD_INCLUDEDIRS += libsupla/include

COMPONENT_SRCDIRS += libsupla/src/supla-common
COMPONENT_OBJS += libsupla/src/supla-common/lck.o
COMPONENT_OBJS += libsupla/src/supla-common/log.o
COMPONENT_OBJS += libsupla/src/supla-common/proto.o
COMPONENT_OBJS += libsupla/src/supla-common/srpc.o

COMPONENT_SRCDIRS += libsupla/src/port
COMPONENT_OBJS += libsupla/src/port/util.o
COMPONENT_OBJS += libsupla/src/port/net.o

COMPONENT_SRCDIRS += libsupla/src
COMPONENT_OBJS += libsupla/src/device.o
COMPONENT_OBJS += libsupla/src/channel.o
COMPONENT_OBJS += libsupla/src/supla-value.o
COMPONENT_OBJS += libsupla/src/supla-extvalue.o
COMPONENT_OBJS += libsupla/src/supla-action-trigger.o

CFLAGS += -DSUPLA_DEVICE

COMPONENT_SRCDIRS += esp-supla-utils
COMPONENT_OBJS += esp-supla-utils/esp-supla-utils.o
