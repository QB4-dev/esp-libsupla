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

COMPONENT_SRCDIRS += libsupla/src
COMPONENT_OBJS += libsupla/src/device.o
COMPONENT_OBJS += libsupla/src/channel.o
COMPONENT_OBJS += libsupla/src/supla-value.o
COMPONENT_OBJS += libsupla/src/supla-extvalue.o
COMPONENT_OBJS += libsupla/src/supla-action-trigger.o

COMPONENT_SRCDIRS += platform
COMPONENT_OBJS += platform/arch_esp.o

CFLAGS += -DSUPLA_DEVICE

COMPONENT_SRCDIRS += esp-supla
COMPONENT_OBJS += esp-supla/esp-supla.o
COMPONENT_OBJS += esp-supla/esp-supla-httpd.o
