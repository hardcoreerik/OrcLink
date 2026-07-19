#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_intr_alloc.h>
#include <mbedtls/md.h>
#include <driver/usb_serial_jtag.h>
#include <usb/usb_host.h>

#include <atomic>
#include <cstdarg>
#include <cstring>

class OrcConsole {
 public:
  void begin(uint32_t) {
    if (usb_serial_jtag_is_driver_installed()) return;
    usb_serial_jtag_driver_config_t config = {
        .tx_buffer_size = 4096,
        .rx_buffer_size = 1024,
    };
    usb_serial_jtag_driver_install(&config);
  }

  int available() {
    if (has_pending_) return 1;
    has_pending_ = usb_serial_jtag_read_bytes(&pending_, 1, 0) == 1;
    return has_pending_ ? 1 : 0;
  }

  int read() {
    if (!has_pending_) return -1;
    has_pending_ = false;
    return pending_;
  }

  void print(char value) { write(&value, 1); }
  void print(const char* value) { write(value, strlen(value)); }
  void println() { print('\n'); }
  void println(const char* value) {
    print(value);
    println();
  }

  void printf(const char* format, ...) {
    char output[2048];
    va_list args;
    va_start(args, format);
    const int length = vsnprintf(output, sizeof(output), format, args);
    va_end(args);
    if (length > 0) write(output, min(static_cast<size_t>(length), sizeof(output) - 1));
  }

 private:
  void write(const void* data, size_t size) {
    usb_serial_jtag_write_bytes(data, size, pdMS_TO_TICKS(100));
  }

  uint8_t pending_ = 0;
  bool has_pending_ = false;
};

OrcConsole orc_console;
#ifdef Serial
#undef Serial
#endif
#define Serial orc_console

namespace {
constexpr int kButtonX = 390;
constexpr int kButtonY = 300;
constexpr int kButtonWidth = 500;
constexpr int kButtonHeight = 120;
constexpr uint32_t kJournalMagic = 0x4f4a4e32;
constexpr uint32_t kWorkflowMagic = 0x4f574631;
constexpr size_t kJournalCapacity = 8;
constexpr uint32_t kSessionTimeoutMs = 5000;
constexpr int kWifiClockPin = 12;
constexpr int kWifiCommandPin = 13;
constexpr int kWifiData0Pin = 11;
constexpr int kWifiData1Pin = 10;
constexpr int kWifiData2Pin = 9;
constexpr int kWifiData3Pin = 8;
constexpr int kWifiResetPin = 15;

struct JournalEntry {
  uint32_t sequence;
  char kind[20];
  int16_t x;
  int16_t y;
};

struct JournalState {
  uint32_t magic;
  uint32_t next_sequence;
  uint32_t acknowledged_sequence;
  uint8_t head;
  uint8_t count;
  uint32_t dropped_events;
  JournalEntry entries[kJournalCapacity];
};

struct WorkflowState {
  uint32_t magic;
  uint32_t config_revision;
  uint16_t max_runs;
  uint16_t runs;
};

char node_id[32];
uint64_t heartbeat_sequence = 0;
uint32_t last_heartbeat_ms = 0;
uint32_t last_ping_ms = 0;
uint32_t last_power_draw_ms = 0;
bool was_pressed = false;
bool paired = false;
bool authenticated = false;
bool offline_transition_handled = false;
Preferences preferences;
JournalState journal{};
WorkflowState workflow{};
uint8_t pairing_key[32];
char serial_input[320];
size_t serial_input_length = 0;
bool wifi_station_ready = false;
bool wifi_scan_running = false;
bool wifi_configured = false;
bool wifi_connected = false;
int wifi_network_count = -1;
char wifi_ssid[33]{};
char wifi_password[64]{};
std::atomic<uint32_t> rtl_sdr_status_revision{0};
uint32_t drawn_rtl_sdr_status_revision = 0;
char rtl_sdr_status[96] = "RTL-SDR: waiting for USB-A host";
char rtl_sdr_serial[48]{};
char rtl_sdr_speed[8] = "none";
uint16_t rtl_sdr_vid = 0;
uint16_t rtl_sdr_pid = 0;
uint8_t pending_usb_address = 0;
usb_host_client_handle_t usb_client = nullptr;
usb_device_handle_t rtl_sdr_device = nullptr;
bool rtl_sdr_gone = false;

void emit_identity();

void draw_touch_state(const char* message, uint32_t color) {
  M5.Display.fillRect(300, 480, 680, 70, TFT_BLACK);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(message, 640, 515);
}

void draw_session_state(const char* message, uint32_t color) {
  M5.Display.fillRect(250, 210, 780, 55, TFT_BLACK);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(message, 640, 237);
}

void draw_wifi_state() {
  char message[80];
  uint32_t color = TFT_ORANGE;
  if (!wifi_station_ready) {
    snprintf(message, sizeof(message), "Wi-Fi unavailable");
  } else if (wifi_connected) {
    snprintf(message, sizeof(message), "Wi-Fi connected: %s",
             WiFi.localIP().toString().c_str());
    color = TFT_GREEN;
  } else if (wifi_configured) {
    snprintf(message, sizeof(message), "Wi-Fi connecting");
    color = TFT_YELLOW;
  } else if (wifi_scan_running) {
    snprintf(message, sizeof(message), "Wi-Fi inventory scanning");
  } else if (wifi_network_count < 0) {
    snprintf(message, sizeof(message), "Wi-Fi inventory failed (%d)", wifi_network_count);
  } else {
    snprintf(message, sizeof(message), "Wi-Fi inventory: %d network%s", wifi_network_count,
             wifi_network_count == 1 ? "" : "s");
    color = TFT_CYAN;
  }
  M5.Display.fillRect(250, 590, 780, 55, TFT_BLACK);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(message, 640, 617);
}

const char* charging_state() {
  if (M5.Power.getType() == m5::Power_Class::pmic_unknown) return "unknown";
  switch (M5.Power.isCharging()) {
    case m5::Power_Class::is_charging:
      return "charging";
    case m5::Power_Class::is_discharging:
      return "discharging";
    default:
      return "unknown";
  }
}

void draw_power_state() {
  char message[96];
  const int32_t level = M5.Power.getBatteryLevel();
  const int16_t battery_mv = M5.Power.getBatteryVoltage();
  const int16_t vbus_mv = M5.Power.getVBUSVoltage();
  snprintf(message, sizeof(message), "Power: %ld%%  battery %dmV  USB %dmV",
           static_cast<long>(level), battery_mv, vbus_mv);
  M5.Display.fillRect(200, 655, 880, 45, TFT_BLACK);
  M5.Display.setTextColor(level >= 0 ? TFT_LIGHTGREY : TFT_ORANGE, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(message, 640, 677);
}

void set_rtl_sdr_status(const char* status) {
  strlcpy(rtl_sdr_status, status, sizeof(rtl_sdr_status));
  rtl_sdr_status_revision.fetch_add(1, std::memory_order_release);
}

void draw_rtl_sdr_state() {
  M5.Display.fillRect(150, 545, 980, 40, TFT_BLACK);
  M5.Display.setTextColor(strstr(rtl_sdr_status, "ready") ? TFT_GREEN : TFT_ORANGE,
                          TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(rtl_sdr_status, 640, 565);
}

void usb_string_to_ascii(const usb_str_desc_t* descriptor, char* output,
                         size_t output_size) {
  if (descriptor == nullptr || output_size == 0) {
    if (output_size > 0) output[0] = '\0';
    return;
  }
  const size_t characters = (descriptor->bLength - 2) / 2;
  const size_t count = min(characters, output_size - 1);
  for (size_t index = 0; index < count; ++index) {
    const uint16_t value = descriptor->wData[index];
    output[index] = value >= 32 && value <= 126 ? static_cast<char>(value) : '?';
  }
  output[count] = '\0';
}

void usb_client_event(const usb_host_client_event_msg_t* event, void*) {
  if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
    pending_usb_address = event->new_dev.address;
  } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE &&
             event->dev_gone.dev_hdl == rtl_sdr_device) {
    rtl_sdr_gone = true;
  }
}

void inspect_usb_device(uint8_t address) {
  usb_device_handle_t device = nullptr;
  if (usb_host_device_open(usb_client, address, &device) != ESP_OK) return;

  const usb_device_desc_t* descriptor = nullptr;
  usb_device_info_t info{};
  if (usb_host_get_device_descriptor(device, &descriptor) != ESP_OK ||
      usb_host_device_info(device, &info) != ESP_OK) {
    usb_host_device_close(usb_client, device);
    return;
  }

  char product[48]{};
  char serial[48]{};
  usb_string_to_ascii(info.str_desc_product, product, sizeof(product));
  usb_string_to_ascii(info.str_desc_serial_num, serial, sizeof(serial));
  const char* speed = info.speed == USB_SPEED_HIGH ? "high" :
                      info.speed == USB_SPEED_FULL ? "full" : "low";
  Serial.printf("RTL_SDR_USB vid=%04x pid=%04x speed=%s product=\"%s\" serial=\"%s\"\n",
                descriptor->idVendor, descriptor->idProduct, speed, product, serial);

  if (descriptor->idVendor == 0x0bda && descriptor->idProduct == 0x2838) {
    rtl_sdr_device = device;
    rtl_sdr_vid = descriptor->idVendor;
    rtl_sdr_pid = descriptor->idProduct;
    strlcpy(rtl_sdr_speed, speed, sizeof(rtl_sdr_speed));
    strlcpy(rtl_sdr_serial, serial, sizeof(rtl_sdr_serial));
    char status[96];
    snprintf(status, sizeof(status), "RTL-SDR V4 ready: %s USB, serial %s", speed, serial);
    set_rtl_sdr_status(status);
    Serial.println("RTL_SDR_PROBE_OK rtl2832u=true tuner=pending");
  } else {
    usb_host_device_close(usb_client, device);
  }
}

void usb_host_task(void*) {
  usb_host_config_t host_config{};
  host_config.intr_flags = ESP_INTR_FLAG_LEVEL1;
  host_config.peripheral_map = 0;  // ESP32-P4 default is the High-Speed controller.
  const esp_err_t install_result = usb_host_install(&host_config);
  if (install_result != ESP_OK) {
    Serial.printf("RTL_SDR_HOST_ERROR install=%s\n", esp_err_to_name(install_result));
    set_rtl_sdr_status("RTL-SDR: USB host install failed");
    vTaskDelete(nullptr);
  }

  usb_host_client_config_t client_config{};
  client_config.is_synchronous = false;
  client_config.max_num_event_msg = 4;
  client_config.async.client_event_callback = usb_client_event;
  const esp_err_t client_result = usb_host_client_register(&client_config, &usb_client);
  if (client_result != ESP_OK) {
    Serial.printf("RTL_SDR_HOST_ERROR client=%s\n", esp_err_to_name(client_result));
    set_rtl_sdr_status("RTL-SDR: USB client failed");
    vTaskDelete(nullptr);
  }
  set_rtl_sdr_status("RTL-SDR: USB-A host active, waiting");
  Serial.println("RTL_SDR_HOST_READY controller=high_speed");

  while (true) {
    uint32_t event_flags = 0;
    usb_host_lib_handle_events(pdMS_TO_TICKS(10), &event_flags);
    usb_host_client_handle_events(usb_client, pdMS_TO_TICKS(10));
    if (pending_usb_address != 0) {
      const uint8_t address = pending_usb_address;
      pending_usb_address = 0;
      inspect_usb_device(address);
    }
    if (rtl_sdr_gone) {
      rtl_sdr_gone = false;
      usb_host_device_close(usb_client, rtl_sdr_device);
      rtl_sdr_device = nullptr;
      rtl_sdr_vid = 0;
      rtl_sdr_pid = 0;
      strlcpy(rtl_sdr_speed, "none", sizeof(rtl_sdr_speed));
      rtl_sdr_serial[0] = '\0';
      set_rtl_sdr_status("RTL-SDR: disconnected");
      Serial.println("RTL_SDR_DISCONNECTED");
    }
  }
}

void initialize_rtl_sdr_host() {
  M5.Power.setExtOutput(true, m5::ext_USB);
  set_rtl_sdr_status("RTL-SDR: USB-A power enabled");
  if (xTaskCreate(usb_host_task, "rtl_usb_host", 6144, nullptr, 4, nullptr) != pdPASS) {
    set_rtl_sdr_status("RTL-SDR: host task failed");
  }
}

void initialize_wifi() {
  delay(1500);
  WiFi.setPins(kWifiClockPin, kWifiCommandPin, kWifiData0Pin, kWifiData1Pin,
               kWifiData2Pin, kWifiData3Pin, kWifiResetPin);
  wifi_station_ready = WiFi.mode(WIFI_STA);
  draw_wifi_state();
}

void start_wifi_inventory() {
  if (!wifi_station_ready || wifi_configured) return;
  wifi_network_count = WiFi.scanNetworks(true, true);
  wifi_scan_running = wifi_network_count == WIFI_SCAN_RUNNING;
  if (!wifi_scan_running) WiFi.scanDelete();
  draw_wifi_state();
}

void start_wifi_connection() {
  if (!wifi_station_ready || !wifi_configured) return;
  if (wifi_scan_running) WiFi.scanDelete();
  wifi_scan_running = false;
  wifi_network_count = -1;
  wifi_connected = false;
  WiFi.begin(wifi_ssid, wifi_password);
  draw_wifi_state();
}

void poll_wifi() {
  bool state_changed = false;
  if (wifi_scan_running) {
    const int result = WiFi.scanComplete();
    if (result != WIFI_SCAN_RUNNING) {
      wifi_scan_running = false;
      wifi_network_count = result;
      if (result >= 0) WiFi.scanDelete();
      state_changed = true;
    }
  }
  const bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wifi_connected) {
    wifi_connected = connected;
    state_changed = true;
  }
  if (state_changed) {
    draw_wifi_state();
    if (authenticated) emit_identity();
  }
}

void draw_ui() {
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  M5.Display.setTextSize(3);
  M5.Display.drawString("OrcLink", 640, 90);

  M5.Display.setTextColor(TFT_GREEN, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.drawString("M5Tab5 agent online", 640, 175);

  M5.Display.fillRoundRect(kButtonX, kButtonY, kButtonWidth, kButtonHeight, 18, TFT_DARKCYAN);
  M5.Display.drawRoundRect(kButtonX, kButtonY, kButtonWidth, kButtonHeight, 18, TFT_CYAN);
  M5.Display.setTextColor(TFT_WHITE, TFT_DARKCYAN);
  M5.Display.drawString("Tap to verify touch", 640, 360);

  M5.Display.setTextSize(2);
  draw_touch_state("Waiting for touch", TFT_LIGHTGREY);
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

void print_hex(const uint8_t* value, size_t size) {
  static constexpr char kHex[] = "0123456789abcdef";
  for (size_t index = 0; index < size; ++index) {
    Serial.print(kHex[value[index] >> 4]);
    Serial.print(kHex[value[index] & 0x0f]);
  }
}

void persist_journal() {
  preferences.putBytes("journal", &journal, sizeof(journal));
}

void persist_workflow() {
  preferences.putBytes("workflow", &workflow, sizeof(workflow));
}

void load_state() {
  preferences.begin("orclink", false);
  paired = preferences.getBytesLength("pair_key") == sizeof(pairing_key);
  if (paired) preferences.getBytes("pair_key", pairing_key, sizeof(pairing_key));
  if (preferences.getBytesLength("journal") == sizeof(journal)) {
    preferences.getBytes("journal", &journal, sizeof(journal));
  }
  if (journal.magic != kJournalMagic || journal.head >= kJournalCapacity ||
      journal.count > kJournalCapacity) {
    memset(&journal, 0, sizeof(journal));
    journal.magic = kJournalMagic;
    persist_journal();
  }
  if (preferences.getBytesLength("workflow") == sizeof(workflow)) {
    preferences.getBytes("workflow", &workflow, sizeof(workflow));
  }
  if (workflow.magic != kWorkflowMagic) {
    memset(&workflow, 0, sizeof(workflow));
    workflow.magic = kWorkflowMagic;
    persist_workflow();
  }
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
}

uint32_t append_journal(const char* kind, int16_t x = -1, int16_t y = -1) {
  size_t index;
  if (journal.count < kJournalCapacity) {
    index = (journal.head + journal.count) % kJournalCapacity;
    ++journal.count;
  } else {
    index = journal.head;
    journal.head = (journal.head + 1) % kJournalCapacity;
    ++journal.dropped_events;
  }
  JournalEntry& entry = journal.entries[index];
  entry.sequence = ++journal.next_sequence;
  strncpy(entry.kind, kind, sizeof(entry.kind) - 1);
  entry.kind[sizeof(entry.kind) - 1] = '\0';
  entry.x = x;
  entry.y = y;
  persist_journal();
  return entry.sequence;
}

bool hmac_matches(const char* value, const uint8_t* candidate) {
  uint8_t expected[32];
  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (info == nullptr ||
      mbedtls_md_hmac(info, pairing_key, sizeof(pairing_key),
                      reinterpret_cast<const uint8_t*>(value), strlen(value), expected) != 0) {
    return false;
  }
  uint8_t difference = 0;
  for (size_t index = 0; index < sizeof(expected); ++index) {
    difference |= expected[index] ^ candidate[index];
  }
  return difference == 0;
}

void run_offline_workflow() {
  if (workflow.config_revision == 0 || workflow.runs >= workflow.max_runs) return;
  ++workflow.runs;
  persist_workflow();
  draw_session_state("Host offline - local status workflow active", TFT_ORANGE);
  append_journal("workflow_run");
}

void acknowledge_journal(uint32_t sequence) {
  while (journal.count > 0 && journal.entries[journal.head].sequence <= sequence) {
    journal.head = (journal.head + 1) % kJournalCapacity;
    --journal.count;
  }
  if (sequence > journal.acknowledged_sequence) {
    journal.acknowledged_sequence = sequence;
    persist_journal();
  }
}

void emit_pending_journal() {
  if (!authenticated) return;
  for (size_t offset = 0; offset < journal.count; ++offset) {
    const JournalEntry& entry = journal.entries[(journal.head + offset) % kJournalCapacity];
    Serial.printf(
        "{\"type\":\"event\",\"message_id\":\"m5tab5_journal_%lu\","
        "\"protocol_version\":{\"major\":1,\"minor\":0},"
        "\"payload\":{\"node_id\":\"%s\",\"kind\":\"local_journal\","
        "\"journal_sequence\":%lu,\"event_kind\":\"%s\",\"x\":%d,\"y\":%d}}\n",
        static_cast<unsigned long>(entry.sequence), node_id,
        static_cast<unsigned long>(entry.sequence), entry.kind, entry.x, entry.y);
  }
}

bool point_in_button(int32_t x, int32_t y) {
  return x >= kButtonX && x < kButtonX + kButtonWidth &&
         y >= kButtonY && y < kButtonY + kButtonHeight;
}

void emit_touch(int32_t x, int32_t y) {
  char status[64];
  const uint32_t sequence = append_journal("operator_touch", x, y);
  snprintf(status, sizeof(status), "Touch verified (%lu) at %ld,%ld",
           static_cast<unsigned long>(sequence), static_cast<long>(x),
           static_cast<long>(y));
  draw_touch_state(status, TFT_GREEN);
  emit_pending_journal();
}

void emit_identity() {
  const String wifi_ip = wifi_connected ? WiFi.localIP().toString() : "0.0.0.0";
  const int32_t battery_level = M5.Power.getBatteryLevel();
  const int16_t battery_mv = M5.Power.getBatteryVoltage();
  const int32_t battery_ma = M5.Power.getBatteryCurrent();
  const int16_t vbus_mv = M5.Power.getVBUSVoltage();
  Serial.printf(
      "{\"type\":\"hello\",\"message_id\":\"m5tab5_hello_01\","
      "\"protocol_version\":{\"major\":1,\"minor\":0},"
      "\"payload\":{\"adapter\":{\"id\":\"m5tab5.serial\",\"version\":\"0.7.2\"},"
      "\"features\":[\"telemetry\",\"display\",\"touch\",\"authenticated_session\",\"journal\",\"network_inventory\",\"wifi_provisioning\",\"power_telemetry\",\"usb_host\",\"rtl_sdr_probe\"],"
      "\"capability_namespaces\":[\"m5tab5\"],\"config_schema_version\":1}}\n");
  Serial.printf(
      "{\"type\":\"node_snapshot\",\"message_id\":\"m5tab5_snapshot_%lu\","
      "\"protocol_version\":{\"major\":1,\"minor\":0},"
      "\"payload\":{\"node_id\":\"%s\",\"connection_state\":\"online\","
      "\"hardware\":{\"chip\":\"%s\",\"revision\":%u,\"cores\":%u,"
      "\"flash_bytes\":%u,\"psram_bytes\":%u,\"free_heap_bytes\":%u,"
      "\"display\":{\"width\":%d,\"height\":%d,\"touch_ready\":%s}},"
      "\"network\":{\"transport\":\"esp_hosted_sdio\",\"station_ready\":%s,"
      "\"configured\":%s,\"scan_result\":%d,\"connected\":%s,\"ip\":\"%s\"},"
      "\"power\":{\"source\":\"m5unified\",\"pmic_detected\":%s,\"pmic_type\":%d,"
      "\"battery_level\":%ld,\"battery_mv\":%d,"
      "\"battery_ma\":%ld,\"vbus_mv\":%d,\"charging\":\"%s\"},"
      "\"journal\":{\"latest_sequence\":%lu,\"acknowledged_sequence\":%lu,\"pending\":%u,\"dropped\":%lu},"
      "\"offline_workflow\":{\"id\":\"status_on_disconnect\",\"config_revision\":%lu,"
      "\"runs\":%u,\"max_runs\":%u},"
      "\"capabilities\":[\"m5tab5.device.info\",\"m5tab5.health.read\","
      "\"m5tab5.display.status\",\"m5tab5.input.touch\",\"m5tab5.event.journal.sync\","
      "\"m5tab5.network.inventory\",\"m5tab5.network.configure\","
      "\"m5tab5.power.read\"]}}\n",
      static_cast<unsigned long>(millis()), node_id, ESP.getChipModel(),
      ESP.getChipRevision(), ESP.getChipCores(), ESP.getFlashChipSize(),
      ESP.getPsramSize(), ESP.getFreeHeap(), M5.Display.width(), M5.Display.height(),
      M5.Touch.isEnabled() ? "true" : "false",
      wifi_station_ready ? "true" : "false", wifi_configured ? "true" : "false",
      wifi_network_count, wifi_connected ? "true" : "false", wifi_ip.c_str(),
      M5.Power.getType() == m5::Power_Class::pmic_unknown ? "false" : "true",
      static_cast<int>(M5.Power.getType()), static_cast<long>(battery_level), battery_mv,
      static_cast<long>(battery_ma), vbus_mv, charging_state(),
      static_cast<unsigned long>(journal.next_sequence),
      static_cast<unsigned long>(journal.acknowledged_sequence), journal.count,
      static_cast<unsigned long>(journal.dropped_events),
      static_cast<unsigned long>(workflow.config_revision), workflow.runs, workflow.max_runs);
}

void set_online() {
  if (!authenticated) return;
  draw_session_state("Authenticated host online", TFT_GREEN);
  append_journal("session_online");
  emit_identity();
  emit_pending_journal();
}

void process_command(char* command) {
  if (strcmp(command, "RTL_STATUS") == 0) {
    Serial.printf("RTL_SDR_STATUS connected=%s vid=%04x pid=%04x speed=%s serial=\"%s\"\n",
                  rtl_sdr_device != nullptr ? "true" : "false", rtl_sdr_vid,
                  rtl_sdr_pid, rtl_sdr_speed, rtl_sdr_serial);
    return;
  }
  if (strncmp(command, "PAIR ", 5) == 0) {
    uint8_t candidate[sizeof(pairing_key)];
    if (!decode_hex(command + 5, candidate, sizeof(candidate))) {
      Serial.println("PAIR_INVALID");
      return;
    }
    if (!paired) {
      memcpy(pairing_key, candidate, sizeof(pairing_key));
      preferences.putBytes("pair_key", pairing_key, sizeof(pairing_key));
      paired = true;
      append_journal("paired");
    }
    uint8_t difference = 0;
    for (size_t index = 0; index < sizeof(pairing_key); ++index) {
      difference |= pairing_key[index] ^ candidate[index];
    }
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
    for (size_t index = 0; index < sizeof(host_proof); ++index) {
      difference |= host_proof[index] ^ expected_host_proof[index];
    }
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
    offline_transition_handled = false;
    last_ping_ms = millis();
    set_online();
    return;
  }
  if (strcmp(command, "PING") == 0 && authenticated) {
    last_ping_ms = millis();
    return;
  }
  if (strncmp(command, "PREPARE_FLASH ", 14) == 0 && authenticated) {
    uint8_t digest[32];
    if (!decode_hex(command + 14, digest, sizeof(digest))) {
      Serial.println("FLASH_INVALID");
      return;
    }
    draw_session_state("Approved firmware update starting", TFT_CYAN);
    append_journal("firmware_approved");
    Serial.print("FLASH_READY ");
    print_hex(digest, sizeof(digest));
    Serial.println();
    emit_identity();
    emit_pending_journal();
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
    snprintf(signed_value, sizeof(signed_value), "wifi|%s|%s", ssid_hex, password_hex);
    if (!decode_hex_text(ssid_hex, candidate_ssid, sizeof(candidate_ssid)) ||
        candidate_ssid[0] == '\0' ||
        !decode_hex_text(password_hex, candidate_password, sizeof(candidate_password)) ||
        !decode_hex(signature_text, signature, sizeof(signature)) ||
        !hmac_matches(signed_value, signature)) {
      Serial.println("WIFI_INVALID");
      return;
    }
    strlcpy(wifi_ssid, candidate_ssid, sizeof(wifi_ssid));
    strlcpy(wifi_password, candidate_password, sizeof(wifi_password));
    preferences.putString("wifi_ssid", wifi_ssid);
    preferences.putString("wifi_pass", wifi_password);
    wifi_configured = true;
    append_journal("wifi_configured");
    Serial.println("WIFI_CONFIGURED");
    start_wifi_connection();
    emit_identity();
    emit_pending_journal();
    return;
  }
  if (strncmp(command, "INSTALL_STATUS ", 15) == 0 && authenticated) {
    char* revision_text = command + 15;
    char* max_runs_text = strchr(revision_text, ' ');
    if (max_runs_text == nullptr) {
      Serial.println("WORKFLOW_INVALID");
      return;
    }
    *max_runs_text++ = '\0';
    char* signature_text = strchr(max_runs_text, ' ');
    if (signature_text == nullptr) {
      Serial.println("WORKFLOW_INVALID");
      return;
    }
    *signature_text++ = '\0';
    const uint32_t revision = strtoul(revision_text, nullptr, 10);
    const uint32_t max_runs = strtoul(max_runs_text, nullptr, 10);
    uint8_t signature[32];
    char signed_value[48];
    snprintf(signed_value, sizeof(signed_value), "workflow|%lu|%lu",
             static_cast<unsigned long>(revision), static_cast<unsigned long>(max_runs));
    if (revision == 0 || max_runs == 0 || max_runs > UINT16_MAX ||
        !decode_hex(signature_text, signature, sizeof(signature)) ||
        !hmac_matches(signed_value, signature)) {
      Serial.println("WORKFLOW_INVALID");
      return;
    }
    if (revision <= workflow.config_revision) {
      Serial.println("WORKFLOW_STALE");
      return;
    }
    workflow.config_revision = revision;
    workflow.max_runs = static_cast<uint16_t>(max_runs);
    workflow.runs = 0;
    persist_workflow();
    append_journal("workflow_installed");
    Serial.printf("WORKFLOW_OK %lu\n", static_cast<unsigned long>(revision));
    emit_identity();
    emit_pending_journal();
    return;
  }
  if (strncmp(command, "ROTATE_KEY ", 11) == 0 && authenticated) {
    char* key_text = command + 11;
    char* signature_text = strchr(key_text, ' ');
    if (signature_text == nullptr) {
      Serial.println("ROTATE_INVALID");
      return;
    }
    *signature_text++ = '\0';
    uint8_t replacement[sizeof(pairing_key)];
    uint8_t signature[32];
    char signed_value[80];
    snprintf(signed_value, sizeof(signed_value), "rotate|%s", key_text);
    if (!decode_hex(key_text, replacement, sizeof(replacement)) ||
        !decode_hex(signature_text, signature, sizeof(signature)) ||
        !hmac_matches(signed_value, signature)) {
      Serial.println("ROTATE_INVALID");
      return;
    }
    memcpy(pairing_key, replacement, sizeof(pairing_key));
    preferences.putBytes("pair_key", pairing_key, sizeof(pairing_key));
    append_journal("credential_rotated");
    Serial.println("KEY_ROTATED");
    authenticated = false;
    offline_transition_handled = false;
    last_ping_ms = millis();
    draw_session_state("Credential rotated - host reconnect required", TFT_YELLOW);
    return;
  }
  if (strcmp(command, "TEST_PRESSURE") == 0 && authenticated) {
    for (int index = 0; index < 10; ++index) append_journal("pressure_test");
    Serial.printf("PRESSURE_OK %lu\n", static_cast<unsigned long>(journal.dropped_events));
    emit_identity();
    emit_pending_journal();
    return;
  }
  if (strcmp(command, "PRESSURE_ACK") == 0 && authenticated) {
    journal.dropped_events = 0;
    persist_journal();
    Serial.println("PRESSURE_CLEARED");
    emit_identity();
    return;
  }
  if (strncmp(command, "ACK ", 4) == 0 && authenticated) {
    const uint32_t sequence = strtoul(command + 4, nullptr, 10);
    if (sequence <= journal.next_sequence) acknowledge_journal(sequence);
  }
}

void poll_serial() {
  while (Serial.available() > 0) {
    const char value = static_cast<char>(Serial.read());
    if (value == '\n') {
      serial_input[serial_input_length] = '\0';
      if (serial_input_length > 0) process_command(serial_input);
      serial_input_length = 0;
    } else if (value != '\r' && serial_input_length < sizeof(serial_input) - 1) {
      serial_input[serial_input_length++] = value;
    }
  }
}
}  // namespace

void setup() {
  Serial.begin(115200);

  auto config = M5.config();
  M5.begin(config);
  M5.Display.setRotation(1);
  M5.Display.setBrightness(180);
  draw_ui();
  draw_rtl_sdr_state();
  initialize_wifi();
  initialize_rtl_sdr_host();

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BASE);
  snprintf(node_id, sizeof(node_id), "m5tab5_%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  load_state();
  if (wifi_configured) {
    start_wifi_connection();
  } else {
    start_wifi_inventory();
  }
  append_journal("boot");
  last_ping_ms = millis();
  offline_transition_handled = !paired;
  draw_session_state(paired ? "Waiting for authenticated host" : "Unpaired - USB provisioning",
                     paired ? TFT_YELLOW : TFT_ORANGE);
  draw_power_state();
}

void loop() {
  M5.update();
  poll_serial();
  poll_wifi();

  const uint32_t current_rtl_sdr_status_revision =
      rtl_sdr_status_revision.load(std::memory_order_acquire);
  if (drawn_rtl_sdr_status_revision != current_rtl_sdr_status_revision) {
    drawn_rtl_sdr_status_revision = current_rtl_sdr_status_revision;
    draw_rtl_sdr_state();
  }

  const auto touch = M5.Touch.getDetail(0);
  const bool pressed = touch.isPressed() || touch.wasPressed();
  if (pressed && !was_pressed && point_in_button(touch.x, touch.y)) {
    emit_touch(touch.x, touch.y);
  }
  was_pressed = pressed;

  const uint32_t now = millis();
  if (now - last_power_draw_ms >= 10000) {
    last_power_draw_ms = now;
    draw_power_state();
  }
  if (!offline_transition_handled && now - last_ping_ms > kSessionTimeoutMs) {
    authenticated = false;
    offline_transition_handled = true;
    draw_session_state("Host offline - local journal active", TFT_ORANGE);
    append_journal("session_degraded");
    run_offline_workflow();
  }
  if (authenticated && now - last_heartbeat_ms >= 2000) {
    last_heartbeat_ms = now;
    ++heartbeat_sequence;
    Serial.printf(
        "{\"type\":\"heartbeat\",\"message_id\":\"m5tab5_heartbeat_%llu\","
        "\"protocol_version\":{\"major\":1,\"minor\":0},"
        "\"payload\":{\"node_id\":\"%s\",\"sequence\":%llu,"
        "\"uptime_ms\":%u,\"free_heap_bytes\":%u,\"journal_pending\":%u}}\n",
        static_cast<unsigned long long>(heartbeat_sequence), node_id,
        static_cast<unsigned long long>(heartbeat_sequence), now, ESP.getFreeHeap(),
        journal.count);
  }

  delay(10);
}
