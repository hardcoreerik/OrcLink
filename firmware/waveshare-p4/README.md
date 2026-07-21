# Waveshare ESP32-P4 OrcLink display proof

This target is for the measured Waveshare ESP32-P4-Module-DEV-KIT on COM3
(ESP32-P4 revision 1.3, 16 MB flash, MAC `80:f1:b2:d1:e0:d5`) with an MPI2418-
style 2.4-inch Raspberry Pi display attached to physical header pins 1-26.

The display uses an ILI9341 SPI controller and an XPT2046 resistive-touch
controller. The board preserves Raspberry Pi power and ground positions over
the first 26 pins. The resulting signal map is:

| Display signal | Physical pin | ESP32-P4 GPIO |
|---|---:|---:|
| TP_IRQ | 11 | 21 |
| LCD_RESET | 13 | 20 |
| LCD_DC / LCD_RS | 15 | 6 |
| LCD_MOSI / TP_MOSI | 19 | 3 |
| TP_MISO | 21 | 2 |
| LCD_SCLK / TP_SCLK | 23 | 0 |
| LCD_CS | 24 | 36 |
| TP_CS | 26 | 32 |

The current image initializes the LCD and XPT2046 touch controller and exposes
an OrcLink protocol-v1 serial node. It uses the same persistent one-time
pairing and mutual HMAC-SHA256 authentication contract as the M5Tab5 adapter.
Control-session liveness is maintained by authenticated `PING` messages with a
five-second lease. The screen shows `ORCLINK ONLINE` only during that lease.

The LCD is a three-page local status dashboard. Its default network page shows
the Ethernet DHCP address prominently along with link speed and duplex state.
The Wi-Fi page shows ESP-Hosted readiness, scan count, provisioning state, and
the Wi-Fi address when connected. The device page shows uptime, free heap,
display state, and touch state. A firm stylus or fingernail press cycles pages;
each press also emits the raw X/Y ADC values over serial for diagnostics.

`DISPLAY_STATUS` and `TOUCH_STATUS` remain local read-only diagnostics. Touch
calibration is deliberately deferred because page cycling only needs a reliable
press edge, not mapped screen coordinates.

The RTL-SDR driver is not enabled in this target yet.

The onboard IP101-compatible 100 Mbps PHY is initialized with the Waveshare
ESP32-P4 platform defaults: address 1, MDC GPIO31, MDIO GPIO52, reset GPIO51,
and external RMII clock input. DHCP state is available over `NETWORK_STATUS`
and authenticated heartbeat telemetry. Ethernet currently proves the network
interface; an OrcLink socket transport is a separate protocol step.

The board's Wi-Fi 6 radio is an ESP32-C6 companion connected to the P4 by
SDIO. Arduino's P4 `WiFi` facade initializes ESP-Hosted/`esp_wifi_remote` over
GPIO18/19/14/15/16/17 with C6 reset on GPIO54. With no stored credential the
firmware performs a read-only network inventory. Credentials are accepted only
through the authenticated, HMAC-signed `SET_WIFI` command and stored in NVS;
they are never compiled into the image or emitted in telemetry.

```powershell
pio run -e waveshare_p4_display
pio run -e waveshare_p4_display -t upload --upload-port COM3
pio device monitor --port COM3 --baud 115200
```

Preserve and hash the complete factory flash before uploading.
