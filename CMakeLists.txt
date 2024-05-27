set(include_dirs "include" "libsupla/src" "libsupla/include")
set(srcs "libsupla/src/supla-common/lck.c"
         "libsupla/src/supla-common/log.c"
         "libsupla/src/supla-common/proto.c"
         "libsupla/src/supla-common/srpc.c"
         "libsupla/src/device.c"
         "libsupla/src/channel.c"
         "libsupla/src/supla-value.c"
         "libsupla/src/supla-extvalue.c"
         "libsupla/src/supla-action-trigger.c"
         "platform/arch_esp.c"
         "esp-supla/esp-supla.c"
)
set(requires "esp_http_server" "nvs_flash" "json" "esp_netif" "esp_wifi")

idf_component_register(
    SRCS "${srcs}"
    INCLUDE_DIRS "${include_dirs}"
    REQUIRES "${requires}"
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC "-DSUPLA_DEVICE")

