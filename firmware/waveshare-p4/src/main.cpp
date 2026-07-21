#include <Arduino.h>
#include <ETH.h>
#include <Network.h>
#include <Preferences.h>
#include <WiFi.h>

#include <driver/spi_master.h>
#include <driver/gpio.h>
#include <esp_chip_info.h>
#include <esp_heap_caps.h>
#include <esp_lcd_panel_io.h>
#include <esp_mac.h>
#include <mbedtls/md.h>

#include <cstdint>
#include <cstring>

namespace {

constexpr spi_host_device_t kLcdHost = SPI2_HOST;
constexpr int kLcdSclk = 0;
constexpr int kLcdMosi = 3;
constexpr int kLcdMiso = 2;
constexpr int kLcdDc = 6;
constexpr int kLcdReset = 20;
constexpr int kLcdCs = 36;
constexpr int kTouchCs = 32;
constexpr int kTouchIrq = 21;
constexpr int kEthernetPhyAddress = 1;
constexpr int kEthernetMdc = 31;
constexpr int kEthernetMdio = 52;
constexpr int kEthernetReset = 51;
constexpr int kWidth = 320;
constexpr int kHeight = 240;

esp_lcd_panel_io_handle_t panel_io = nullptr;
spi_device_handle_t touch_device = nullptr;
bool display_ready = false;
bool touch_ready = false;
uint16_t* framebuffer = nullptr;
char node_id[40]{};
uint64_t heartbeat_sequence = 0;
uint32_t last_heartbeat_ms = 0;
uint32_t last_ping_ms = 0;
Preferences preferences;
uint8_t pairing_key[32]{};
bool paired = false;
bool authenticated = false;
bool ethernet_started = false;
bool ethernet_link_up = false;
bool ethernet_has_ip = false;
volatile bool ethernet_link_event = false;
volatile bool ethernet_ip_event = false;
volatile bool ethernet_lost_event = false;
bool wifi_station_ready = false;
bool wifi_scan_running = false;
bool wifi_configured = false;
bool wifi_connected = false;
int wifi_network_count = -1;
char wifi_ssid[33]{};
char wifi_password[64]{};
char serial_command[320]{};
size_t serial_command_length = 0;
uint8_t dashboard_page = 0;
bool dashboard_dirty = true;
uint32_t last_dashboard_refresh_ms = 0;

constexpr uint32_t kAuthenticationLeaseMs = 5000;
constexpr uint8_t kDashboardPageCount = 3;

struct Glyph {
  char character;
  uint8_t rows[7];
};

constexpr Glyph kGlyphs[] = {
    {'0', {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}},
    {'1', {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'2', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}},
    {'3', {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e}},
    {'4', {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}},
    {'5', {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e}},
    {'6', {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e}},
    {'7', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'8', {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}},
    {'9', {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e}},
    {'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
    {'B', {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}},
    {'C', {0x0f, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0f}},
    {'D', {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}},
    {'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}},
    {'F', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}},
    {'G', {0x0f, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0f}},
    {'H', {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
    {'I', {0x0e, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0e}},
    {'J', {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}},
    {'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x19, 0x15, 0x13, 0x13, 0x11}},
    {'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
    {'P', {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}},
    {'Q', {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d}},
    {'R', {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}},
    {'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}},
    {'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x0a, 0x0a, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a}},
    {'X', {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}},
    {'Y', {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}},
    {'-', {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0c, 0x0c}},
    {':', {0x00, 0x0c, 0x0c, 0x00, 0x0c, 0x0c, 0x00}},
    {'/', {0x01, 0x02, 0x02, 0x04, 0x08, 0x08, 0x10}},
};

uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
  return static_cast<uint16_t>(((red & 0xf8) << 8) | ((green & 0xfc) << 3) |
                               (blue >> 3));
}

void fill_rect(int x, int y, int width, int height, uint16_t color) {
  if (framebuffer == nullptr) return;
  const int x0 = constrain(x, 0, kWidth);
  const int y0 = constrain(y, 0, kHeight);
  const int x1 = constrain(x + width, 0, kWidth);
  const int y1 = constrain(y + height, 0, kHeight);
  for (int row = y0; row < y1; ++row) {
    for (int column = x0; column < x1; ++column) {
      framebuffer[row * kWidth + column] = color;
    }
  }
}

const uint8_t* glyph_rows(char character) {
  for (const Glyph& glyph : kGlyphs) {
    if (glyph.character == character) return glyph.rows;
  }
  return nullptr;
}

void draw_text(int x, int y, const char* text, int scale, uint16_t color) {
  while (*text != '\0') {
    if (*text == ' ') {
      x += 6 * scale;
      ++text;
      continue;
    }
    const uint8_t* rows = glyph_rows(*text++);
    if (rows == nullptr) {
      x += 6 * scale;
      continue;
    }
    for (int row = 0; row < 7; ++row) {
      for (int column = 0; column < 5; ++column) {
        if ((rows[row] & (1 << (4 - column))) != 0) {
          fill_rect(x + column * scale, y + row * scale, scale, scale, color);
        }
      }
    }
    x += 6 * scale;
  }
}

void draw_text_centered(int y, const char* text, int scale, uint16_t color) {
  const int width = static_cast<int>(strlen(text)) * 6 * scale - scale;
  draw_text(max(0, (kWidth - width) / 2), y, text, scale, color);
}

esp_err_t initialize_display() {
  spi_bus_config_t bus_config{};
  bus_config.mosi_io_num = kLcdMosi;
  bus_config.miso_io_num = kLcdMiso;
  bus_config.sclk_io_num = kLcdSclk;
  bus_config.quadwp_io_num = -1;
  bus_config.quadhd_io_num = -1;
  bus_config.max_transfer_sz = kWidth * kHeight * sizeof(uint16_t);
  esp_err_t result = spi_bus_initialize(kLcdHost, &bus_config, SPI_DMA_CH_AUTO);
  if (result != ESP_OK) return result;

  esp_lcd_panel_io_spi_config_t io_config{};
  io_config.cs_gpio_num = kLcdCs;
  io_config.dc_gpio_num = kLcdDc;
  io_config.spi_mode = 0;
  io_config.pclk_hz = 20 * 1000 * 1000;
  io_config.trans_queue_depth = 1;
  io_config.lcd_cmd_bits = 8;
  io_config.lcd_param_bits = 8;
  result = esp_lcd_new_panel_io_spi(
      static_cast<esp_lcd_spi_bus_handle_t>(kLcdHost), &io_config, &panel_io);
  if (result != ESP_OK) return result;

  gpio_set_direction(static_cast<gpio_num_t>(kLcdReset), GPIO_MODE_OUTPUT);
  gpio_set_level(static_cast<gpio_num_t>(kLcdReset), 0);
  delay(20);
  gpio_set_level(static_cast<gpio_num_t>(kLcdReset), 1);
  delay(120);

  auto command = [](int value, const uint8_t* parameters = nullptr,
                    size_t parameter_count = 0) {
    return esp_lcd_panel_io_tx_param(panel_io, value, parameters,
                                     parameter_count);
  };
  const uint8_t power_control_1[] = {0x23};
  const uint8_t power_control_2[] = {0x10};
  const uint8_t vcom_control_1[] = {0x3e, 0x28};
  const uint8_t vcom_control_2[] = {0x86};
  const uint8_t memory_access[] = {0x28};  // landscape, BGR
  const uint8_t pixel_format[] = {0x55};  // RGB565
  const uint8_t frame_rate[] = {0x00, 0x18};
  const uint8_t display_function[] = {0x08, 0x82, 0x27};
  const uint8_t gamma_select[] = {0x01};
  const uint8_t positive_gamma[] = {0x0f, 0x31, 0x2b, 0x0c, 0x0e,
                                    0x08, 0x4e, 0xf1, 0x37, 0x07,
                                    0x10, 0x03, 0x0e, 0x09, 0x00};
  const uint8_t negative_gamma[] = {0x00, 0x0e, 0x14, 0x03, 0x11,
                                    0x07, 0x31, 0xc1, 0x48, 0x08,
                                    0x0f, 0x0c, 0x31, 0x36, 0x0f};

  if ((result = command(0x01)) != ESP_OK) return result;
  delay(150);
  if ((result = command(0x28)) != ESP_OK) return result;
  if ((result = command(0xc0, power_control_1, sizeof(power_control_1))) != ESP_OK)
    return result;
  if ((result = command(0xc1, power_control_2, sizeof(power_control_2))) != ESP_OK)
    return result;
  if ((result = command(0xc5, vcom_control_1, sizeof(vcom_control_1))) != ESP_OK)
    return result;
  if ((result = command(0xc7, vcom_control_2, sizeof(vcom_control_2))) != ESP_OK)
    return result;
  if ((result = command(0x36, memory_access, sizeof(memory_access))) != ESP_OK)
    return result;
  if ((result = command(0x3a, pixel_format, sizeof(pixel_format))) != ESP_OK)
    return result;
  if ((result = command(0xb1, frame_rate, sizeof(frame_rate))) != ESP_OK)
    return result;
  if ((result = command(0xb6, display_function, sizeof(display_function))) != ESP_OK)
    return result;
  if ((result = command(0x26, gamma_select, sizeof(gamma_select))) != ESP_OK)
    return result;
  if ((result = command(0xe0, positive_gamma, sizeof(positive_gamma))) != ESP_OK)
    return result;
  if ((result = command(0xe1, negative_gamma, sizeof(negative_gamma))) != ESP_OK)
    return result;
  if ((result = command(0x11)) != ESP_OK) return result;
  delay(120);
  if ((result = command(0x29)) != ESP_OK) return result;
  delay(20);
  display_ready = true;
  return ESP_OK;
}

esp_err_t initialize_touch() {
  spi_device_interface_config_t touch_config{};
  touch_config.clock_speed_hz = 2 * 1000 * 1000;
  touch_config.mode = 0;
  touch_config.spics_io_num = kTouchCs;
  touch_config.queue_size = 1;
  esp_err_t result = spi_bus_add_device(kLcdHost, &touch_config, &touch_device);
  if (result != ESP_OK) return result;
  gpio_set_direction(static_cast<gpio_num_t>(kTouchIrq), GPIO_MODE_INPUT);
  gpio_set_pull_mode(static_cast<gpio_num_t>(kTouchIrq), GPIO_PULLUP_ONLY);
  touch_ready = true;
  return ESP_OK;
}

bool read_touch(uint16_t* raw_x, uint16_t* raw_y) {
  if (!touch_ready || gpio_get_level(static_cast<gpio_num_t>(kTouchIrq)) != 0)
    return false;
  auto read_axis = [](uint8_t command) {
    uint8_t transmit[3] = {command, 0, 0};
    uint8_t receive[3]{};
    spi_transaction_t transaction{};
    transaction.length = 24;
    transaction.tx_buffer = transmit;
    transaction.rx_buffer = receive;
    if (spi_device_transmit(touch_device, &transaction) != ESP_OK) return uint16_t{0};
    return static_cast<uint16_t>((receive[1] << 8 | receive[2]) >> 3);
  };
  *raw_x = read_axis(0xd0);
  *raw_y = read_axis(0x90);
  return *raw_x != 0 && *raw_y != 0;
}

void flush_framebuffer() {
  const uint8_t columns[] = {0x00, 0x00, 0x01, 0x3f};
  const uint8_t rows[] = {0x00, 0x00, 0x00, 0xef};
  esp_lcd_panel_io_tx_param(panel_io, 0x2a, columns, sizeof(columns));
  esp_lcd_panel_io_tx_param(panel_io, 0x2b, rows, sizeof(rows));
  esp_lcd_panel_io_tx_color(panel_io, 0x2c, framebuffer,
                            kWidth * kHeight * sizeof(uint16_t));
}

void draw_dashboard_footer() {
  const uint16_t muted = rgb565(115, 145, 95);
  fill_rect(0, 205, kWidth, 35, rgb565(16, 28, 12));
  fill_rect(0, 205, kWidth, 2, rgb565(118, 185, 0));
  draw_text(10, 214, authenticated ? "ORCLINK ONLINE" : "ORCLINK OFFLINE", 2,
            authenticated ? rgb565(157, 255, 0) : rgb565(245, 120, 100));
  char page[8];
  snprintf(page, sizeof(page), "%u/%u", dashboard_page + 1,
           kDashboardPageCount);
  draw_text(278, 214, page, 2, muted);
}

void draw_network_page() {
  const uint16_t green = rgb565(157, 255, 0);
  const uint16_t white = rgb565(232, 238, 228);
  const uint16_t muted = rgb565(122, 138, 106);
  draw_text(14, 43, "NETWORK", 2, green);
  fill_rect(14, 62, 292, 1, rgb565(46, 58, 46));

  if (ethernet_link_up) {
    draw_text(14, 74, ethernet_has_ip ? "ETHERNET ONLINE" : "ETHERNET DHCP", 2,
              ethernet_has_ip ? green : rgb565(204, 167, 0));
    draw_text(14, 101, "IP ADDRESS", 2, muted);
    const String address =
        ethernet_has_ip ? ETH.localIP().toString() : String("0.0.0.0");
    draw_text_centered(122, address.c_str(), 3, white);
    char link[32];
    snprintf(link, sizeof(link), "%u MBPS %s", ETH.linkSpeed(),
             ETH.fullDuplex() ? "FULL" : "HALF");
    draw_text_centered(160, link, 2, muted);
  } else {
    draw_text(14, 74, "ETHERNET OFFLINE", 2, rgb565(245, 120, 100));
    draw_text_centered(118,
                       ethernet_started ? "WAITING FOR LINK" : "ETH INIT FAILED",
                       2, white);
  }
  draw_text(218, 190, "TOUCH FOR MORE", 1, muted);
}

void draw_wifi_page() {
  const uint16_t green = rgb565(157, 255, 0);
  const uint16_t white = rgb565(232, 238, 228);
  const uint16_t muted = rgb565(122, 138, 106);
  draw_text(14, 43, "WI-FI SIDECAR", 2, green);
  fill_rect(14, 62, 292, 1, rgb565(46, 58, 46));

  const char* status = !wifi_station_ready
                           ? "RADIO OFFLINE"
                           : wifi_connected
                                 ? "WI-FI CONNECTED"
                                 : wifi_configured
                                       ? "WI-FI CONNECTING"
                                       : wifi_scan_running ? "SCANNING"
                                                           : "STATION READY";
  draw_text(14, 75, status, 2,
            wifi_connected
                ? green
                : wifi_station_ready ? white : rgb565(245, 120, 100));

  char networks[32];
  if (wifi_network_count >= 0)
    snprintf(networks, sizeof(networks), "NETWORKS %d", wifi_network_count);
  else
    snprintf(networks, sizeof(networks), "NETWORKS --");
  draw_text(14, 105, networks, 2, muted);
  draw_text(14, 135, wifi_configured ? "CONFIGURED YES" : "CONFIGURED NO", 2,
            muted);
  if (wifi_connected) {
    const String address = WiFi.localIP().toString();
    draw_text_centered(166, address.c_str(), 2, white);
  } else {
    draw_text(14, 166, "IP --", 2, muted);
  }
  draw_text(218, 190, "TOUCH FOR MORE", 1, muted);
}

void draw_device_page() {
  const uint16_t green = rgb565(157, 255, 0);
  const uint16_t white = rgb565(232, 238, 228);
  const uint16_t muted = rgb565(122, 138, 106);
  draw_text(14, 43, "DEVICE HEALTH", 2, green);
  fill_rect(14, 62, 292, 1, rgb565(46, 58, 46));
  draw_text(14, 74, "ESP32-P4", 2, white);

  char uptime[32];
  snprintf(uptime, sizeof(uptime), "UPTIME %lu S",
           static_cast<unsigned long>(millis() / 1000));
  draw_text(14, 102, uptime, 2, muted);
  char heap[32];
  snprintf(heap, sizeof(heap), "FREE HEAP %u KB", ESP.getFreeHeap() / 1024);
  draw_text(14, 128, heap, 2, muted);
  draw_text(14, 154, display_ready ? "DISPLAY READY" : "DISPLAY FAILED", 2,
            display_ready ? green : rgb565(245, 120, 100));
  draw_text(14, 179, touch_ready ? "TOUCH READY" : "TOUCH FAILED", 2,
            touch_ready ? green : rgb565(245, 120, 100));
}

void draw_dashboard() {
  if (!display_ready || framebuffer == nullptr) return;
  fill_rect(0, 0, kWidth, kHeight, rgb565(14, 14, 14));
  fill_rect(0, 0, kWidth, 34, rgb565(16, 24, 14));
  fill_rect(0, 32, kWidth, 2, rgb565(118, 185, 0));
  draw_text(12, 6, "ORCLINK", 3, rgb565(157, 255, 0));
  draw_text(229, 10, "P4 EDGE", 2, rgb565(122, 138, 106));

  switch (dashboard_page) {
    case 0:
      draw_network_page();
      break;
    case 1:
      draw_wifi_page();
      break;
    default:
      draw_device_page();
      break;
  }
  draw_dashboard_footer();
  flush_framebuffer();
  dashboard_dirty = false;
  last_dashboard_refresh_ms = millis();
}

void draw_orclink_screen() {
  framebuffer = static_cast<uint16_t*>(heap_caps_malloc(
      kWidth * kHeight * sizeof(uint16_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
  if (framebuffer == nullptr) return;
  draw_dashboard();
}

void draw_connection_status(const char* status, uint16_t background,
                            uint16_t foreground) {
  (void)status;
  (void)background;
  (void)foreground;
  dashboard_dirty = true;
  draw_dashboard();
}

void on_network_event(arduino_event_id_t event) {
  switch (event) {
    case ARDUINO_EVENT_ETH_CONNECTED:
      ethernet_link_event = true;
      break;
    case ARDUINO_EVENT_ETH_GOT_IP:
      ethernet_ip_event = true;
      break;
    case ARDUINO_EVENT_ETH_LOST_IP:
    case ARDUINO_EVENT_ETH_DISCONNECTED:
    case ARDUINO_EVENT_ETH_STOP:
      ethernet_lost_event = true;
      break;
    default:
      break;
  }
}

void initialize_ethernet() {
  Network.onEvent(on_network_event);
  ethernet_started = ETH.begin(ETH_PHY_IP101, kEthernetPhyAddress, kEthernetMdc,
                               kEthernetMdio, kEthernetReset, EMAC_CLK_EXT_IN);
  Serial.printf(
      "ETH_INIT started=%s phy=IP101 address=%d mdc=%d mdio=%d reset=%d\n",
      ethernet_started ? "true" : "false", kEthernetPhyAddress, kEthernetMdc,
      kEthernetMdio, kEthernetReset);
}

void service_network_events() {
  if (ethernet_lost_event) {
    ethernet_lost_event = false;
    ethernet_link_up = false;
    ethernet_has_ip = false;
    dashboard_dirty = true;
    Serial.println("ETH_STATUS link=false ip=none");
    if (!authenticated)
      draw_connection_status("ETH OFFLINE", rgb565(75, 20, 20),
                             rgb565(255, 175, 130));
  }
  if (ethernet_link_event) {
    ethernet_link_event = false;
    ethernet_link_up = true;
    dashboard_dirty = true;
    Serial.println("ETH_STATUS link=true dhcp=pending");
  }
  if (ethernet_ip_event) {
    ethernet_ip_event = false;
    ethernet_link_up = true;
    ethernet_has_ip = true;
    dashboard_dirty = true;
    Serial.printf("ETH_STATUS link=true ip=%s speed_mbps=%u duplex=%s\n",
                  ETH.localIP().toString().c_str(), ETH.linkSpeed(),
                  ETH.fullDuplex() ? "full" : "half");
    if (!authenticated)
      draw_connection_status("ETH ONLINE", rgb565(5, 55, 80),
                             rgb565(100, 225, 255));
  }
}

void start_wifi_connection() {
  if (!wifi_station_ready || !wifi_configured) return;
  if (wifi_scan_running) WiFi.scanDelete();
  wifi_scan_running = false;
  wifi_network_count = -1;
  wifi_connected = false;
  WiFi.begin(wifi_ssid, wifi_password);
  Serial.println("WIFI_STATUS configured=true connecting=true");
}

void initialize_wifi() {
  WiFi.setPins(18, 19, 14, 15, 16, 17, 54);
  wifi_station_ready = WiFi.mode(WIFI_STA);
  Serial.printf(
      "WIFI_INIT station_ready=%s transport=esp_hosted_sdio "
      "clk=18 cmd=19 d0=14 d1=15 d2=16 d3=17 reset=54\n",
      wifi_station_ready ? "true" : "false");
  if (wifi_configured) {
    start_wifi_connection();
  } else if (wifi_station_ready) {
    wifi_network_count = WiFi.scanNetworks(true, true);
    wifi_scan_running = wifi_network_count == WIFI_SCAN_RUNNING;
    if (!wifi_scan_running) WiFi.scanDelete();
  }
}

void poll_wifi() {
  if (wifi_scan_running) {
    const int result = WiFi.scanComplete();
    if (result != WIFI_SCAN_RUNNING) {
      wifi_scan_running = false;
      wifi_network_count = result;
      dashboard_dirty = true;
      if (result >= 0) WiFi.scanDelete();
      Serial.printf("WIFI_SCAN networks=%d\n", result);
    }
  }
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wifi_connected) {
    wifi_connected = connected;
    dashboard_dirty = true;
    Serial.printf("WIFI_STATUS connected=%s ip=%s rssi_dbm=%ld\n",
                  connected ? "true" : "false",
                  connected ? WiFi.localIP().toString().c_str() : "none",
                  connected ? static_cast<long>(WiFi.RSSI()) : 0L);
    if (connected && !authenticated)
      draw_connection_status("WIFI ONLINE", rgb565(35, 45, 90),
                             rgb565(140, 205, 255));
  }
}

bool decode_hex(const char* value, uint8_t* output, size_t output_size) {
  auto digit = [](char value) -> int {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
  };
  if (strlen(value) != output_size * 2) return false;
  for (size_t index = 0; index < output_size; ++index) {
    const int high = digit(value[index * 2]);
    const int low = digit(value[index * 2 + 1]);
    if (high < 0 || low < 0) return false;
    output[index] = static_cast<uint8_t>((high << 4) | low);
  }
  return true;
}

bool decode_hex_text(const char* value, char* output, size_t output_size) {
  const size_t length = strlen(value);
  if (length % 2 != 0 || length / 2 >= output_size) return false;
  auto digit = [](char value) -> int {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
  };
  for (size_t index = 0; index < length / 2; ++index) {
    const int high = digit(value[index * 2]);
    const int low = digit(value[index * 2 + 1]);
    if (high < 0 || low < 0) return false;
    output[index] = static_cast<char>((high << 4) | low);
  }
  output[length / 2] = '\0';
  return true;
}

bool hmac_matches(const char* value, const uint8_t* candidate) {
  uint8_t expected[32];
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == nullptr ||
      mbedtls_md_hmac(info, pairing_key, sizeof(pairing_key),
                      reinterpret_cast<const uint8_t*>(value), strlen(value),
                      expected) != 0)
    return false;
  uint8_t difference = 0;
  for (size_t index = 0; index < sizeof(expected); ++index)
    difference |= expected[index] ^ candidate[index];
  return difference == 0;
}

void print_hex(const uint8_t* value, size_t size) {
  static constexpr char kHex[] = "0123456789abcdef";
  for (size_t index = 0; index < size; ++index) {
    Serial.print(kHex[value[index] >> 4]);
    Serial.print(kHex[value[index] & 0x0f]);
  }
}

void emit_identity() {
  esp_chip_info_t chip{};
  esp_chip_info(&chip);
  Serial.printf(
      "{\"type\":\"hello\",\"message_id\":\"waveshare_p4_hello_01\","
      "\"protocol_version\":{\"major\":1,\"minor\":0},\"payload\":{"
      "\"adapter\":{\"id\":\"waveshare-p4.serial\",\"version\":\"0.1.0\"},"
      "\"features\":[\"telemetry\",\"display\",\"touch\",\"ethernet\","
      "\"wifi_inventory\",\"wifi_provisioning\"],"
      "\"capability_namespaces\":[\"waveshare_p4\",\"display\",\"network\"],"
      "\"config_schema_version\":1}}\n");
  Serial.printf(
      "{\"type\":\"node_snapshot\",\"message_id\":\"waveshare_p4_snapshot_01\","
      "\"protocol_version\":{\"major\":1,\"minor\":0},\"payload\":{"
      "\"node_id\":\"%s\",\"connection_state\":\"online\","
      "\"hardware\":{\"chip\":\"ESP32-P4\",\"revision\":%u,\"cores\":%u,"
      "\"flash_bytes\":%u,\"psram_bytes\":%u,\"display\":{"
      "\"controller\":\"ILI9341\",\"width\":320,\"height\":240},"
      "\"network\":{\"ethernet\":{\"link\":%s,\"ip\":\"%s\"},"
      "\"wifi\":{\"transport\":\"esp_hosted_sdio\",\"station_ready\":%s,"
      "\"configured\":%s,\"scan_result\":%d,\"connected\":%s,\"ip\":\"%s\"}}},"
      "\"capabilities\":[\"waveshare_p4.device.info\","
      "\"waveshare_p4.health.read\",\"display.status.show\","
      "\"display.touch.read\"],\"journal_pending\":0}}\n",
      node_id, chip.revision, chip.cores, ESP.getFlashChipSize(),
      ESP.getPsramSize(), ethernet_link_up ? "true" : "false",
      ethernet_has_ip ? ETH.localIP().toString().c_str() : "",
      wifi_station_ready ? "true" : "false", wifi_configured ? "true" : "false",
      wifi_network_count, wifi_connected ? "true" : "false",
      wifi_connected ? WiFi.localIP().toString().c_str() : "");
}

void process_command(char* command) {
  if (strncmp(command, "PAIR ", 5) == 0) {
    uint8_t candidate[sizeof(pairing_key)];
    if (!decode_hex(command + 5, candidate, sizeof(candidate))) {
      Serial.println("PAIR_INVALID");
      return;
    }
    if (!paired) {
      memcpy(pairing_key, candidate, sizeof(pairing_key));
      if (preferences.putBytes("pair_key", pairing_key, sizeof(pairing_key)) !=
          sizeof(pairing_key)) {
        memset(pairing_key, 0, sizeof(pairing_key));
        Serial.println("PAIR_ERROR");
        return;
      }
      paired = true;
    }
    uint8_t difference = 0;
    for (size_t index = 0; index < sizeof(pairing_key); ++index)
      difference |= pairing_key[index] ^ candidate[index];
    Serial.println(difference == 0 ? "PAIR_OK" : "PAIR_LOCKED");
    return;
  }

  if (strncmp(command, "AUTH ", 5) == 0 && paired) {
    uint8_t nonce[16];
    uint8_t host_proof[32];
    uint8_t expected_host_proof[32];
    uint8_t signature[32];
    char* proof_hex = strchr(command + 5, ' ');
    if (proof_hex == nullptr) {
      Serial.println("AUTH_INVALID");
      return;
    }
    *proof_hex++ = '\0';
    if (!decode_hex(command + 5, nonce, sizeof(nonce)) ||
        !decode_hex(proof_hex, host_proof, sizeof(host_proof))) {
      Serial.println("AUTH_INVALID");
      return;
    }
    const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    uint8_t host_input[sizeof("host") - 1 + sizeof(nonce)];
    memcpy(host_input, "host", sizeof("host") - 1);
    memcpy(host_input + sizeof("host") - 1, nonce, sizeof(nonce));
    if (info == nullptr ||
        mbedtls_md_hmac(info, pairing_key, sizeof(pairing_key), host_input,
                        sizeof(host_input), expected_host_proof) != 0) {
      Serial.println("AUTH_ERROR");
      return;
    }
    uint8_t difference = 0;
    for (size_t index = 0; index < sizeof(host_proof); ++index)
      difference |= host_proof[index] ^ expected_host_proof[index];
    if (difference != 0) {
      Serial.println("AUTH_DENIED");
      return;
    }
    uint8_t device_input[sizeof("device") - 1 + sizeof(nonce)];
    memcpy(device_input, "device", sizeof("device") - 1);
    memcpy(device_input + sizeof("device") - 1, nonce, sizeof(nonce));
    if (mbedtls_md_hmac(info, pairing_key, sizeof(pairing_key), device_input,
                        sizeof(device_input), signature) != 0) {
      Serial.println("AUTH_ERROR");
      return;
    }
    Serial.print("AUTH_OK ");
    print_hex(signature, sizeof(signature));
    Serial.println();
    authenticated = true;
    last_ping_ms = millis();
    draw_connection_status("ORCLINK ONLINE", rgb565(5, 70, 45),
                           rgb565(120, 255, 155));
    emit_identity();
    return;
  }

  if (strcmp(command, "PING") == 0 && authenticated) {
    last_ping_ms = millis();
    return;
  }
  if (strncmp(command, "SET_WIFI ", 9) == 0 && authenticated) {
    char* ssid_hex = command + 9;
    char* password_hex = strchr(ssid_hex, ' ');
    if (password_hex == nullptr) {
      Serial.println("WIFI_INVALID");
      return;
    }
    *password_hex++ = '\0';
    char* signature_text = strchr(password_hex, ' ');
    if (signature_text == nullptr) {
      Serial.println("WIFI_INVALID");
      return;
    }
    *signature_text++ = '\0';
    char candidate_ssid[sizeof(wifi_ssid)];
    char candidate_password[sizeof(wifi_password)];
    uint8_t signature[32];
    char signed_value[208];
    snprintf(signed_value, sizeof(signed_value), "wifi|%s|%s", ssid_hex,
             password_hex);
    if (!decode_hex_text(ssid_hex, candidate_ssid, sizeof(candidate_ssid)) ||
        candidate_ssid[0] == '\0' ||
        !decode_hex_text(password_hex, candidate_password,
                         sizeof(candidate_password)) ||
        !decode_hex(signature_text, signature, sizeof(signature)) ||
        !hmac_matches(signed_value, signature)) {
      Serial.println("WIFI_INVALID");
      return;
    }
    strlcpy(wifi_ssid, candidate_ssid, sizeof(wifi_ssid));
    strlcpy(wifi_password, candidate_password, sizeof(wifi_password));
    if (preferences.putString("wifi_ssid", wifi_ssid) == 0 ||
        preferences.putString("wifi_pass", wifi_password) == 0) {
      Serial.println("WIFI_STORE_ERROR");
      return;
    }
    wifi_configured = true;
    dashboard_dirty = true;
    Serial.println("WIFI_CONFIGURED");
    start_wifi_connection();
    return;
  }
  if (strcmp(command, "DISPLAY_STATUS") == 0) {
    Serial.printf(
        "DISPLAY_STATUS initialized=%s controller=ILI9341 width=320 height=240\n",
        display_ready ? "true" : "false");
    return;
  }
  if (strcmp(command, "TOUCH_STATUS") == 0) {
    Serial.printf("TOUCH_STATUS initialized=%s irq_gpio=%d irq_level=%d cs_gpio=%d\n",
                  touch_ready ? "true" : "false", kTouchIrq,
                  gpio_get_level(static_cast<gpio_num_t>(kTouchIrq)), kTouchCs);
    return;
  }
  if (strcmp(command, "NETWORK_STATUS") == 0) {
    Serial.printf(
        "NETWORK_STATUS ethernet_initialized=%s ethernet_link=%s ethernet_ip=%s "
        "ethernet_speed_mbps=%u ethernet_duplex=%s wifi_station_ready=%s "
        "wifi_configured=%s wifi_scan_result=%d wifi_connected=%s wifi_ip=%s\n",
        ethernet_started ? "true" : "false",
        ethernet_link_up ? "true" : "false",
        ethernet_has_ip ? ETH.localIP().toString().c_str() : "none",
        ethernet_link_up ? ETH.linkSpeed() : 0,
        ethernet_link_up && ETH.fullDuplex() ? "full" : "unknown",
        wifi_station_ready ? "true" : "false", wifi_configured ? "true" : "false",
        wifi_network_count, wifi_connected ? "true" : "false",
        wifi_connected ? WiFi.localIP().toString().c_str() : "none");
  }
}

void read_serial_commands() {
  while (Serial.available() > 0) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\r') continue;
    if (value == '\n') {
      serial_command[serial_command_length] = '\0';
      if (serial_command_length > 0) process_command(serial_command);
      serial_command_length = 0;
      continue;
    }
    if (serial_command_length + 1 < sizeof(serial_command)) {
      serial_command[serial_command_length++] = value;
    } else {
      serial_command_length = 0;
      Serial.println("COMMAND_TOO_LONG");
    }
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(500);

  uint8_t mac[6]{};
  esp_read_mac(mac, ESP_MAC_BASE);
  snprintf(node_id, sizeof(node_id), "waveshare_p4_%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  preferences.begin("orclink", false);
  paired = preferences.getBytesLength("pair_key") == sizeof(pairing_key);
  if (paired) preferences.getBytes("pair_key", pairing_key, sizeof(pairing_key));
  const String stored_ssid = preferences.isKey("wifi_ssid")
                                 ? preferences.getString("wifi_ssid", "")
                                 : String();
  const String stored_password = preferences.isKey("wifi_pass")
                                     ? preferences.getString("wifi_pass", "")
                                     : String();
  wifi_configured = !stored_ssid.isEmpty() && stored_ssid.length() <= 32 &&
                    stored_password.length() <= 63;
  if (wifi_configured) {
    stored_ssid.toCharArray(wifi_ssid, sizeof(wifi_ssid));
    stored_password.toCharArray(wifi_password, sizeof(wifi_password));
  }

  const esp_err_t display_result = initialize_display();
  Serial.printf("DISPLAY_INIT result=%s\n", esp_err_to_name(display_result));
  if (display_result == ESP_OK) {
    draw_orclink_screen();
    delay(100);
    const esp_err_t touch_result = initialize_touch();
    Serial.printf("TOUCH_INIT result=%s irq_gpio=%d cs_gpio=%d\n",
                  esp_err_to_name(touch_result), kTouchIrq, kTouchCs);
  }
  initialize_ethernet();
  delay(1500);
  initialize_wifi();
  Serial.printf("ORCLINK_READY node_id=%s paired=%s\n", node_id,
                paired ? "true" : "false");
}

void loop() {
  static bool touch_was_down = false;
  uint16_t raw_x = 0;
  uint16_t raw_y = 0;
  const bool touch_irq_active =
      touch_ready && gpio_get_level(static_cast<gpio_num_t>(kTouchIrq)) == 0;
  const bool touch_down = read_touch(&raw_x, &raw_y);
  if (touch_down && !touch_was_down) {
    Serial.printf("TOUCH raw_x=%u raw_y=%u\n", raw_x, raw_y);
    dashboard_page = (dashboard_page + 1) % kDashboardPageCount;
    dashboard_dirty = true;
    draw_dashboard();
  }
  touch_was_down = touch_down;

  read_serial_commands();
  service_network_events();
  poll_wifi();

  if (authenticated && millis() - last_ping_ms > kAuthenticationLeaseMs) {
    authenticated = false;
    draw_connection_status("ORCLINK OFFLINE", rgb565(75, 20, 20),
                           rgb565(255, 175, 130));
  }

  if (display_ready &&
      (dashboard_dirty ||
       (dashboard_page == 2 && millis() - last_dashboard_refresh_ms >= 1000)))
    draw_dashboard();

  if (authenticated && millis() - last_heartbeat_ms >= 2000) {
    last_heartbeat_ms = millis();
    ++heartbeat_sequence;
    Serial.printf(
        "{\"type\":\"heartbeat\",\"message_id\":\"waveshare_p4_heartbeat_%llu\","
        "\"protocol_version\":{\"major\":1,\"minor\":0},\"payload\":{"
        "\"node_id\":\"%s\",\"sequence\":%llu,\"uptime_ms\":%lu,"
        "\"free_heap_bytes\":%u,\"display_ready\":%s,\"touch_ready\":%s,"
        "\"touch_irq_level\":%d,\"touch_irq_active\":%s,"
        "\"network_transport\":\"ethernet\",\"network_link\":%s,"
        "\"network_ip\":\"%s\",\"wifi_station_ready\":%s,"
        "\"wifi_configured\":%s,\"wifi_connected\":%s,\"wifi_ip\":\"%s\"}}\n",
        static_cast<unsigned long long>(heartbeat_sequence), node_id,
        static_cast<unsigned long long>(heartbeat_sequence),
        static_cast<unsigned long>(millis()), ESP.getFreeHeap(),
        display_ready ? "true" : "false", touch_ready ? "true" : "false",
        touch_ready ? gpio_get_level(static_cast<gpio_num_t>(kTouchIrq)) : -1,
        touch_irq_active ? "true" : "false",
        ethernet_link_up ? "true" : "false",
        ethernet_has_ip ? ETH.localIP().toString().c_str() : "",
        wifi_station_ready ? "true" : "false", wifi_configured ? "true" : "false",
        wifi_connected ? "true" : "false",
        wifi_connected ? WiFi.localIP().toString().c_str() : "");
  }
  delay(10);
}
