# esp-libsupla

`esp-libsupla` is an embedded C component for creating SUPLA devices on ESP32 and ESP8266. It bundles the `libsupla` device and channel API with ESP platform support, NVS-backed configuration, Wi-Fi state helpers, and HTTP configuration handlers.

[SUPLA](https://supla.org) is an open smart-home platform. Its apps let users monitor and control compatible devices remotely, including lighting, gates, blinds, heating, and energy-related equipment. SUPLA supports both ready-made products and DIY integrations.

This component is for developers building custom ESP-based devices that communicate with SUPLA Cloud. For the platform, apps, and user information, visit [supla.org](https://supla.org); for integration resources, see [Integrate & Build](https://supla.org/en/get-started/integrate-and-build).

## Compatibility

- ESP-IDF component projects. The component manifest currently declares ESP-IDF `>=5.0`.
- ESP8266 RTOS-SDK projects using the legacy Make-based component system.

The repository includes `examples/default` and `examples/webserver`. The default example uses credentials entered in `menuconfig`; the webserver example provides an access-point configuration page when no SUPLA email is stored.

## Install

### ESP Component Registry

[![Component Registry](https://components.espressif.com/components/qb4-dev/esp-libsupla/badge.svg)](https://components.espressif.com/components/qb4-dev/esp-libsupla)

From an ESP-IDF project directory, add the dependency:

```sh
idf.py add-dependency "qb4-dev/esp-libsupla=*"
```

### Add to `components`

Clone the repository into your project's `components` directory:

```sh
mkdir -p components
git clone --recursive https://github.com/QB4-dev/esp-libsupla components/esp-libsupla
```

Include the component headers in application code with `#include <libsupla/device.h>` and, for ESP helpers, `#include <esp-supla.h>`.

## Build an Example

Use an ESP-IDF 5.x environment for the CMake/`idf.py` project flow. The default example is the CMake quick start:

```sh
cd examples/default
idf.py set-target esp32
idf.py menuconfig
idf.py build
idf.py flash monitor
```

Choose a supported ESP32 target in place of `esp32` when needed. The webserver example uses the built-in HTTP configuration handler. The current component CMake source list does not include `esp-supla-httpd.c`, so that example may fail to link with CMake until the source is added there. The legacy Make component list does include it.

To build with ESP8266 RTOS-SDK, activate that SDK's environment and use its legacy Make-based project flow:

```sh
cd examples/webserver
make menuconfig
make
make flash monitor
```

The default example is in `examples/default` and follows the same project build flow. In its `menuconfig`, set the Wi-Fi SSID/password, SUPLA email, and server. In the webserver example, first boot without a configured SUPLA email starts the configuration access point; connect to it, open the device's configuration page, and enter Wi-Fi and SUPLA settings. Once configured, add the device through the SUPLA app.

## Application Setup

Initialize NVS, load or generate the SUPLA credentials, and set the config on a device before starting it. `supla_esp_nvs_config_init()` loads settings from NVS and generates a GUID and auth key when they are unset. The account email and SUPLA server still need to be configured. The device should be started after network connectivity is available, and `supla_dev_iterate()` should be called regularly from the application's task or loop.

```c
#include <esp_err.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <nvs_flash.h>
#include <libsupla/device.h>
#include <esp-supla.h>

static struct supla_config config = {
	.email = "user@example.com",
	.server = "svrX.supla.org",
	.port = 2016,
	.ssl = 1,
};

void app_main(void)
{
	ESP_ERROR_CHECK(nvs_flash_init());
	ESP_ERROR_CHECK(supla_esp_nvs_config_init(&config));

	supla_dev_t *dev = supla_dev_create("My device", NULL);
	supla_dev_set_config(dev, &config);

	/* Initialize Wi-Fi and add channels before starting the device. */
	supla_dev_start(dev);
	for (;;) {
		supla_dev_iterate(dev);
		vTaskDelay(pdMS_TO_TICKS(100));
	}
}
```

Add channels with `supla_channel_create()` and `supla_dev_add_channel()`. See the example applications for complete Wi-Fi, channel, and event-loop setup. The HTTP helpers are declared in `esp-supla.h`; register `supla_dev_basic_httpd_handler` on the `/` GET and POST routes to use the built-in configuration page.

## TLS

The component exposes the `ESP_LIBSUPLA_USE_ESP_TLS` Kconfig option. The project Kconfig notes that ESP8266 SSL handshakes may be unstable at 80 MHz and recommends 160 MHz. Check the selected target and SDK configuration when enabling TLS.

## License

See [LICENSE](LICENSE) for the project license. The bundled `libsupla` library has its own [license](libsupla/LICENSE).

