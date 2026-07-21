#include <Arduino.h>
#include <M5Unified.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <esp_intr_alloc.h>
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#include <driver/usb_serial_jtag.h>
#include <usb/usb_helpers.h>
#include <usb/usb_host.h>

#include <atomic>
#include <cmath>
#include <cstdarg>
#include <cstring>

#include "rtl_sdr_v4_transfers.h"

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
constexpr uint32_t kRtlSampleRateSps = 960000;
constexpr uint32_t kRtlCaptureBytes = 9600000;
constexpr size_t kRtlBulkBytes = 32768;
constexpr size_t kRtlControlMps = 64;
constexpr uint32_t kRtlControlTimeoutMs = 1000;
constexpr uint32_t kRtlCaptureTimeoutMs = 30000;
constexpr size_t kRtlAudioBufferSamples = 832;
constexpr size_t kRtlSpectrumBins = 128;
constexpr int kSpectrumX = 64;
constexpr int kSpectrumY = 112;
constexpr int kSpectrumWidth = 1152;
constexpr int kSpectrumHeight = 224;
constexpr int kWaterfallY = 368;
constexpr int kWaterfallHeight = 288;
constexpr int kSdrControlsY = 664;
constexpr int kSdrControlsHeight = 48;
constexpr int kSdrKzelX = 64;
constexpr int kSdrKzelWidth = 320;
constexpr int kSdrNoaaX = 400;
constexpr int kSdrNoaaWidth = 420;
constexpr int kSdrStopX = 836;
constexpr int kSdrStopWidth = 380;

enum class RtlPreset : uint8_t { kzel, noaa };

// Independently observed 100 MHz final-tune sequence. Fixed presets patch only
// the calculated divider and PLL bytes immediately before these records run.
constexpr RtlControlRecord kRtlFinalTuneTemplate[] = {
    {0x0074, 0x0610, 0x40, 2, {0x17, 0x20}},
    {0x0074, 0x0610, 0x40, 2, {0x1a, 0x2a}},
    {0x0074, 0x0610, 0x40, 2, {0x1b, 0x34}},
    {0x0074, 0x0610, 0x40, 2, {0x10, 0xa4}},
    {0x0074, 0x0610, 0x40, 2, {0x08, 0xc0}},
    {0x0074, 0x0610, 0x40, 2, {0x09, 0x40}},
    {0x0074, 0x0610, 0x40, 2, {0x0c, 0x68}},
    {0x0074, 0x0610, 0x40, 2, {0x10, 0xa4}},
    {0x0074, 0x0610, 0x40, 2, {0x1a, 0x22}},
    {0x0074, 0x0610, 0x40, 2, {0x12, 0x06}},
    {0x0074, 0x0610, 0x40, 1, {0x00}},
    {0x0074, 0x0600, 0xc0, 5, {}},
    {0x0074, 0x0610, 0x40, 2, {0x10, 0x84}},
    {0x0074, 0x0610, 0x40, 2, {0x14, 0xca}},
    {0x0074, 0x0610, 0x40, 2, {0x12, 0x06}},
    {0x0074, 0x0610, 0x40, 2, {0x16, 0x90}},
    {0x0074, 0x0610, 0x40, 2, {0x15, 0x5a}},
    {0x0074, 0x0610, 0x40, 1, {0x00}},
    {0x0074, 0x0600, 0xc0, 3, {}},
    {0x0074, 0x0610, 0x40, 2, {0x1a, 0x2a}},
    {0x0074, 0x0610, 0x40, 2, {0x17, 0x20}},
    {0x0074, 0x0610, 0x40, 2, {0x06, 0x30}},
};

struct WbfmState {
  int32_t i_sum = 0;
  int32_t q_sum = 0;
  uint8_t rf_phase = 0;
  float previous_i = 0;
  float previous_q = 0;
  bool have_previous = false;
  float channel_filter = 0;
  float audio_sum = 0;
  uint8_t audio_phase = 0;
  float deemphasis = 0;
  float dc = 0;
  uint8_t buffer = 0;
  uint64_t samples = 0;
  uint32_t queued_chunks = 0;
  uint32_t dropped_chunks = 0;
  uint8_t spectrum_phase = 0;
  int16_t peak = 0;
  double square_sum = 0;
};

WbfmState rtl_wbfm;
int16_t rtl_audio_buffers[3][kRtlAudioBufferSamples];
uint8_t rtl_iq_processing[kRtlBulkBytes];
float rtl_spectrum_real[kRtlSpectrumBins];
float rtl_spectrum_imaginary[kRtlSpectrumBins];
float rtl_spectrum_levels[kRtlSpectrumBins];
float rtl_spectrum_window[kRtlSpectrumBins];
bool rtl_spectrum_window_ready = false;

enum class RtlCaptureState : uint8_t {
  disconnected,
  ready,
  queued,
  running,
  complete,
  failed,
};

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
std::atomic<RtlCaptureState> rtl_capture_state{RtlCaptureState::disconnected};
std::atomic<bool> rtl_capture_requested{false};
std::atomic<RtlPreset> rtl_requested_preset{RtlPreset::kzel};
std::atomic<bool> rtl_continuous_requested{false};
std::atomic<bool> rtl_stop_requested{false};
std::atomic<bool> rtl_restart_requested{false};
std::atomic<bool> rtl_ui_active{false};
std::atomic<bool> usb_transfer_done{false};
uint64_t rtl_capture_bytes = 0;
uint8_t rtl_capture_min = 0;
uint8_t rtl_capture_max = 0;
double rtl_capture_mean = 0;
char rtl_capture_sha256[65]{};
char rtl_capture_error[64] = "not run";

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
  const bool ready = strstr(rtl_sdr_status, "ready") != nullptr;
  M5.Display.fillRect(150, 545, 980, 40, TFT_BLACK);
  M5.Display.setTextColor(ready ? TFT_GREEN : TFT_ORANGE, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(rtl_sdr_status, 640, 565);
  if (ready) {
    M5.Display.fillRoundRect(kButtonX, kButtonY, kButtonWidth, kButtonHeight, 18,
                             TFT_DARKGREEN);
    M5.Display.drawRoundRect(kButtonX, kButtonY, kButtonWidth, kButtonHeight, 18,
                             TFT_GREEN);
    M5.Display.setTextColor(TFT_WHITE, TFT_DARKGREEN);
    M5.Display.drawString("Open SDR - KZEL 96.1", 640, 360);
  }
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

void usb_transfer_complete(usb_transfer_t* transfer) {
  usb_transfer_done.store(true, std::memory_order_release);
}

bool wait_for_usb_transfer(usb_transfer_t* transfer, uint32_t timeout_ms) {
  const uint32_t started = millis();
  while (!usb_transfer_done.load(std::memory_order_acquire) && !rtl_sdr_gone &&
         millis() - started < timeout_ms) {
    uint32_t event_flags = 0;
    usb_host_lib_handle_events(0, &event_flags);
    usb_host_client_handle_events(usb_client, pdMS_TO_TICKS(10));
  }
  if (!usb_transfer_done.load(std::memory_order_acquire)) {
    if (transfer->bEndpointAddress != 0) {
      usb_host_endpoint_halt(transfer->device_handle, transfer->bEndpointAddress);
      usb_host_endpoint_flush(transfer->device_handle, transfer->bEndpointAddress);
    }
    const uint32_t flush_started = millis();
    while (!usb_transfer_done.load(std::memory_order_acquire) &&
           millis() - flush_started < 1000) {
      uint32_t event_flags = 0;
      usb_host_lib_handle_events(0, &event_flags);
      usb_host_client_handle_events(usb_client, pdMS_TO_TICKS(10));
    }
  }
  return usb_transfer_done.load(std::memory_order_acquire) &&
         transfer->status == USB_TRANSFER_STATUS_COMPLETED;
}

bool run_control_record(const RtlControlRecord& record, uint8_t request = 0,
                        bool expect_stall = false) {
  for (uint8_t attempt = 0; attempt < 2; ++attempt) {
    const bool is_in = (record.request_type & USB_BM_REQUEST_TYPE_DIR_IN) != 0;
    const size_t transfer_bytes = sizeof(usb_setup_packet_t) +
        (is_in ? usb_round_up_to_mps(record.length, kRtlControlMps) : record.length);
    usb_transfer_t* transfer = nullptr;
    if (usb_host_transfer_alloc(transfer_bytes, 0, &transfer) != ESP_OK) {
      strlcpy(rtl_capture_error, "control allocation failed", sizeof(rtl_capture_error));
      return false;
    }
    auto* setup = reinterpret_cast<usb_setup_packet_t*>(transfer->data_buffer);
    setup->bmRequestType = record.request_type;
    setup->bRequest = request;
    setup->wValue = record.value;
    setup->wIndex = record.index;
    setup->wLength = record.length;
    if (record.request_type == 0x40 && record.length > 0) {
      memcpy(transfer->data_buffer + sizeof(*setup), record.data, record.length);
    }
    transfer->num_bytes = transfer_bytes;
    transfer->device_handle = rtl_sdr_device;
    transfer->bEndpointAddress = 0;
    transfer->callback = usb_transfer_complete;
    transfer->context = nullptr;
    usb_transfer_done.store(false, std::memory_order_release);
    const esp_err_t submitted = usb_host_transfer_submit_control(usb_client, transfer);
    const bool completed = submitted == ESP_OK &&
                           wait_for_usb_transfer(transfer, kRtlControlTimeoutMs);
    const size_t expected_bytes = sizeof(*setup) + record.length;
    const bool exact = completed &&
                       transfer->actual_num_bytes == static_cast<int>(expected_bytes);
    const bool stalled = submitted == ESP_OK &&
                         usb_transfer_done.load(std::memory_order_acquire) &&
                         transfer->status == USB_TRANSFER_STATUS_STALL;
    if (!exact) {
      snprintf(rtl_capture_error, sizeof(rtl_capture_error),
               "control failed status=%d actual=%d expected=%u",
               submitted == ESP_OK ? static_cast<int>(transfer->status) : -1,
               transfer->actual_num_bytes, static_cast<unsigned>(expected_bytes));
    }
    if (usb_transfer_done.load(std::memory_order_acquire) || submitted != ESP_OK) {
      usb_host_transfer_free(transfer);
    }
    if (exact) {
      if (!expect_stall) return true;
      strlcpy(rtl_capture_error, "expected control STALL completed", sizeof(rtl_capture_error));
      return false;
    }
    if (stalled) {
      Serial.printf(
          "RTL_EP0_STALL bm=%02x request=%02x value=%04x index=%04x length=%u expected=%s\n",
          record.request_type, request, record.value, record.index, record.length,
          expect_stall ? "true" : "false");
      if (expect_stall) return true;
    }
    if (expect_stall || !stalled || attempt != 0 || rtl_sdr_gone) return false;
    Serial.println("RTL_EP0_STALL_RETRY attempt=1");
  }
  return false;
}

bool run_rtl_initialization() {
  for (size_t index = 0; index < std::size(kRtlInitTransfers); ++index) {
    const bool expect_stall = index >= kRtlInitExpectedStallFirst &&
                              index <= kRtlInitExpectedStallLast;
    if (!run_control_record(kRtlInitTransfers[index], 0, expect_stall)) return false;
  }
  return true;
}

bool set_rtl_sample_rate_960k() {
  for (size_t index = 462; index <= 477; ++index) {
    RtlControlRecord record = kRtlInitTransfers[index];
    if (index == 464) {
      record.data[0] = 0x07;
      record.data[1] = 0x80;
    }
    if (!run_control_record(record)) return false;
  }
  return true;
}

template <size_t Count>
bool run_control_records(const RtlControlRecord (&records)[Count], bool best_effort) {
  bool ok = true;
  for (const auto& record : records) {
    if (!run_control_record(record)) {
      ok = false;
      if (!best_effort) break;
    }
  }
  return ok;
}

const char* rtl_preset_name(RtlPreset preset) {
  return preset == RtlPreset::noaa ? "NOAA KEC42" : "KZEL";
}

uint32_t rtl_preset_frequency(RtlPreset preset) {
  return preset == RtlPreset::noaa ? 162400000 : 96100000;
}

bool run_rtl_preset(RtlPreset preset) {
  for (size_t index = 0; index < std::size(kRtlFinalTuneTemplate); ++index) {
    RtlControlRecord record = kRtlFinalTuneTemplate[index];
    if (preset == RtlPreset::noaa) {
      if (index == 3 || index == 7) record.data[1] = 0x84;  // VCO / 16 setup.
      if (index == 12) record.data[1] = 0x64;               // VCO / 16 active.
      if (index == 13) record.data[1] = 0x08;               // Integer divider.
      if (index == 15) record.data[1] = 0x9d;               // Fraction high byte.
      if (index == 16) record.data[1] = 0x82;               // Fraction low byte.
    } else {
      if (index == 13) record.data[1] = 0x4a;
      if (index == 15) record.data[1] = 0x65;
      if (index == 16) record.data[1] = 0xaf;
    }
    if (!run_control_record(record)) return false;
  }
  return true;
}

const char* rtl_capture_state_name(RtlCaptureState state) {
  switch (state) {
    case RtlCaptureState::ready: return "ready";
    case RtlCaptureState::queued: return "queued";
    case RtlCaptureState::running: return "running";
    case RtlCaptureState::complete: return "complete";
    case RtlCaptureState::failed: return "failed";
    default: return "disconnected";
  }
}

void draw_sdr_controls(RtlPreset preset, bool running) {
  M5.Display.fillRect(0, kSdrControlsY - 4, 1280, 720 - kSdrControlsY + 4,
                      TFT_BLACK);
  auto button = [](int x, int width, const char* text, uint32_t color) {
    M5.Display.fillRoundRect(x, kSdrControlsY, width, kSdrControlsHeight, 10, color);
    M5.Display.drawRoundRect(x, kSdrControlsY, width, kSdrControlsHeight, 10, TFT_WHITE);
    M5.Display.setTextColor(TFT_WHITE, color);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString(text, x + width / 2, kSdrControlsY + kSdrControlsHeight / 2);
  };
  button(kSdrKzelX, kSdrKzelWidth, "KZEL 96.1 WBFM",
         preset == RtlPreset::kzel ? TFT_DARKGREEN : TFT_DARKGREY);
  button(kSdrNoaaX, kSdrNoaaWidth, "NOAA 162.400 NFM",
         preset == RtlPreset::noaa ? TFT_DARKGREEN : TFT_DARKGREY);
  button(kSdrStopX, kSdrStopWidth, running ? "STOP" : "IDLE - TAP A PRESET",
         running ? TFT_MAROON : TFT_DARKGREY);
}

void draw_sdr_screen(RtlPreset preset) {
  const uint32_t frequency_hz = rtl_preset_frequency(preset);
  const double frequency_mhz = frequency_hz / 1000000.0;
  char label[64];
  if (!rtl_spectrum_window_ready) {
    constexpr float kPi = 3.14159265358979323846f;
    for (size_t index = 0; index < kRtlSpectrumBins; ++index) {
      rtl_spectrum_window[index] =
          0.5f - 0.5f * cosf(2.0f * kPi * index / (kRtlSpectrumBins - 1));
    }
    rtl_spectrum_window_ready = true;
  }
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(3);
  M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
  snprintf(label, sizeof(label), "%s  %.3f MHz", rtl_preset_name(preset), frequency_mhz);
  M5.Display.drawString(label, 640, 42);
  M5.Display.setTextSize(1);
  M5.Display.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  snprintf(label, sizeof(label), "%s   |   960 kS/s   |   RTL-SDR BLOG V4",
           preset == RtlPreset::noaa ? "NFM" : "WBFM");
  M5.Display.drawString(label, 640, 82);
  M5.Display.drawRect(kSpectrumX, kSpectrumY, kSpectrumWidth, kSpectrumHeight,
                      TFT_DARKGREY);
  for (int line = 1; line < 4; ++line) {
    const int y = kSpectrumY + line * kSpectrumHeight / 4;
    M5.Display.drawFastHLine(kSpectrumX, y, kSpectrumWidth, 0x2104);
  }
  for (int line = 0; line <= 4; ++line) {
    const int x = kSpectrumX + line * kSpectrumWidth / 4;
    M5.Display.drawFastVLine(x, kSpectrumY, kSpectrumHeight, 0x2104);
  }
  for (int marker = 0; marker <= 4; ++marker) {
    snprintf(label, sizeof(label), marker == 4 ? "%.3f MHz" : "%.3f",
             frequency_mhz - 0.480 + marker * 0.240);
    M5.Display.setTextColor(marker == 2 ? TFT_GREEN : TFT_LIGHTGREY, TFT_BLACK);
    M5.Display.drawString(label, kSpectrumX + marker * kSpectrumWidth / 4, 350);
  }
  M5.Display.fillRect(kSpectrumX, kWaterfallY, kSpectrumWidth, kWaterfallHeight,
                      TFT_BLACK);
  M5.Display.drawRect(kSpectrumX, kWaterfallY, kSpectrumWidth, kWaterfallHeight,
                      TFT_DARKGREY);
  M5.Display.setScrollRect(kSpectrumX + 1, kWaterfallY + 1, kSpectrumWidth - 2,
                           kWaterfallHeight - 2, TFT_BLACK);
  draw_sdr_controls(preset, true);
}

uint16_t waterfall_color(float level) {
  level = constrain(level, 0.0f, 1.0f);
  uint8_t red = 0;
  uint8_t green = 0;
  uint8_t blue = 0;
  if (level < 0.25f) {
    blue = static_cast<uint8_t>(40 + level * 4 * 180);
  } else if (level < 0.5f) {
    const float ramp = (level - 0.25f) * 4;
    green = static_cast<uint8_t>(ramp * 220);
    blue = 220;
  } else if (level < 0.75f) {
    const float ramp = (level - 0.5f) * 4;
    red = static_cast<uint8_t>(ramp * 255);
    green = 220;
    blue = static_cast<uint8_t>((1.0f - ramp) * 220);
  } else {
    const float ramp = (level - 0.75f) * 4;
    red = 255;
    green = static_cast<uint8_t>(220 + ramp * 35);
    blue = static_cast<uint8_t>(ramp * 255);
  }
  return M5.Display.color565(red, green, blue);
}

void draw_spectrum(const uint8_t* iq, size_t bytes) {
  if (bytes < kRtlSpectrumBins * 2 || ++rtl_wbfm.spectrum_phase < 32) return;
  rtl_wbfm.spectrum_phase = 0;
  constexpr float kPi = 3.14159265358979323846f;
  for (size_t index = 0; index < kRtlSpectrumBins; ++index) {
    rtl_spectrum_real[index] =
        (static_cast<int>(iq[index * 2]) - 128) * rtl_spectrum_window[index];
    rtl_spectrum_imaginary[index] =
        (static_cast<int>(iq[index * 2 + 1]) - 128) * rtl_spectrum_window[index];
  }
  for (size_t index = 1, reversed = 0; index < kRtlSpectrumBins; ++index) {
    size_t bit = kRtlSpectrumBins >> 1;
    for (; reversed & bit; bit >>= 1) reversed ^= bit;
    reversed ^= bit;
    if (index < reversed) {
      std::swap(rtl_spectrum_real[index], rtl_spectrum_real[reversed]);
      std::swap(rtl_spectrum_imaginary[index], rtl_spectrum_imaginary[reversed]);
    }
  }
  for (size_t length = 2; length <= kRtlSpectrumBins; length <<= 1) {
    const float angle = -2.0f * kPi / length;
    const float step_real = cosf(angle);
    const float step_imaginary = sinf(angle);
    for (size_t base = 0; base < kRtlSpectrumBins; base += length) {
      float twiddle_real = 1;
      float twiddle_imaginary = 0;
      for (size_t offset = 0; offset < length / 2; ++offset) {
        const size_t upper = base + offset;
        const size_t lower = upper + length / 2;
        const float product_real = rtl_spectrum_real[lower] * twiddle_real -
                                   rtl_spectrum_imaginary[lower] * twiddle_imaginary;
        const float product_imaginary = rtl_spectrum_real[lower] * twiddle_imaginary +
                                        rtl_spectrum_imaginary[lower] * twiddle_real;
        rtl_spectrum_real[lower] = rtl_spectrum_real[upper] - product_real;
        rtl_spectrum_imaginary[lower] = rtl_spectrum_imaginary[upper] - product_imaginary;
        rtl_spectrum_real[upper] += product_real;
        rtl_spectrum_imaginary[upper] += product_imaginary;
        const float next_real = twiddle_real * step_real -
                                twiddle_imaginary * step_imaginary;
        twiddle_imaginary = twiddle_real * step_imaginary +
                            twiddle_imaginary * step_real;
        twiddle_real = next_real;
      }
    }
  }

  float maximum = -120.0f;
  for (size_t bin = 0; bin < kRtlSpectrumBins; ++bin) {
    const size_t shifted = (bin + kRtlSpectrumBins / 2) % kRtlSpectrumBins;
    rtl_spectrum_levels[bin] =
        10.0f * log10f(rtl_spectrum_real[shifted] * rtl_spectrum_real[shifted] +
                       rtl_spectrum_imaginary[shifted] * rtl_spectrum_imaginary[shifted] +
                       1.0f);
    maximum = max(maximum, rtl_spectrum_levels[bin]);
  }
  const float floor = maximum - 48.0f;
  M5.Display.fillRect(kSpectrumX + 1, kSpectrumY + 1, kSpectrumWidth - 2,
                      kSpectrumHeight - 2, TFT_BLACK);
  for (int line = 1; line < 4; ++line) {
    const int y = kSpectrumY + line * kSpectrumHeight / 4;
    M5.Display.drawFastHLine(kSpectrumX + 1, y, kSpectrumWidth - 2, 0x2104);
  }
  int previous_x = kSpectrumX;
  int previous_y = kSpectrumY + kSpectrumHeight - 2;
  M5.Display.scroll(0, -3);
  for (size_t bin = 0; bin < kRtlSpectrumBins; ++bin) {
    const float normalized =
        constrain((rtl_spectrum_levels[bin] - floor) / 48.0f, 0.0f, 1.0f);
    const int x = kSpectrumX + static_cast<int>(bin * kSpectrumWidth /
                                                (kRtlSpectrumBins - 1));
    const int y = kSpectrumY + kSpectrumHeight - 2 -
                  static_cast<int>(normalized * (kSpectrumHeight - 4));
    if (bin != 0) M5.Display.drawLine(previous_x, previous_y, x, y, TFT_CYAN);
    previous_x = x;
    previous_y = y;
    const int cell_x = kSpectrumX + 1 + static_cast<int>(bin * (kSpectrumWidth - 2) /
                                                         kRtlSpectrumBins);
    const int next_x = kSpectrumX + 1 + static_cast<int>((bin + 1) *
                                                         (kSpectrumWidth - 2) /
                                                         kRtlSpectrumBins);
    M5.Display.fillRect(cell_x, kWaterfallY + kWaterfallHeight - 4,
                        max(1, next_x - cell_x), 3, waterfall_color(normalized));
  }
  M5.Display.drawFastVLine(kSpectrumX + kSpectrumWidth / 2, kSpectrumY,
                           kSpectrumHeight + kWaterfallHeight + 32, TFT_GREEN);
}

float fast_phase(float cross, float dot) {
  // ponytail: ~0.07 rad atan2 approximation; use a DSP vector atan2 only if
  // measured demodulation quality requires it and still sustains 2.4 MS/s.
  constexpr float kQuarterPi = 0.78539816339f;
  constexpr float kThreeQuarterPi = 2.35619449019f;
  const float magnitude = fabsf(cross) + 1e-10f;
  float angle;
  if (dot >= 0) {
    const float ratio = (dot - magnitude) / (dot + magnitude);
    angle = kQuarterPi - kQuarterPi * ratio;
  } else {
    const float ratio = (dot + magnitude) / (magnitude - dot);
    angle = kThreeQuarterPi - kQuarterPi * ratio;
  }
  return cross < 0 ? -angle : angle;
}

void demodulate_fm(const uint8_t* iq, size_t bytes, float audio_scale) {
  int16_t* audio = rtl_audio_buffers[rtl_wbfm.buffer];
  size_t audio_count = 0;
  for (size_t offset = 0; offset + 1 < bytes; offset += 2) {
    rtl_wbfm.i_sum += static_cast<int32_t>(iq[offset]) - 128;
    rtl_wbfm.q_sum += static_cast<int32_t>(iq[offset + 1]) - 128;
    if (++rtl_wbfm.rf_phase != 4) continue;

    const float i = rtl_wbfm.i_sum;
    const float q = rtl_wbfm.q_sum;
    rtl_wbfm.i_sum = 0;
    rtl_wbfm.q_sum = 0;
    rtl_wbfm.rf_phase = 0;
    if (rtl_wbfm.have_previous) {
      const float phase = fast_phase(rtl_wbfm.previous_i * q -
                                         rtl_wbfm.previous_q * i,
                                     rtl_wbfm.previous_i * i +
                                         rtl_wbfm.previous_q * q);
      rtl_wbfm.channel_filter += 0.28f * (phase - rtl_wbfm.channel_filter);
      rtl_wbfm.audio_sum += rtl_wbfm.channel_filter;
      if (++rtl_wbfm.audio_phase == 5) {
        const float demodulated = rtl_wbfm.audio_sum * 0.2f;
        rtl_wbfm.audio_sum = 0;
        rtl_wbfm.audio_phase = 0;
        rtl_wbfm.deemphasis += 0.217f * (demodulated - rtl_wbfm.deemphasis);
        rtl_wbfm.dc += 0.001f * (rtl_wbfm.deemphasis - rtl_wbfm.dc);
        const int32_t scaled =
            lroundf((rtl_wbfm.deemphasis - rtl_wbfm.dc) * audio_scale);
        const int16_t sample = static_cast<int16_t>(constrain(scaled, -16000, 16000));
        audio[audio_count++] = sample;
        const int16_t magnitude = sample < 0 ? -sample : sample;
        if (magnitude > rtl_wbfm.peak) rtl_wbfm.peak = magnitude;
        rtl_wbfm.square_sum += static_cast<double>(sample) * sample;
        ++rtl_wbfm.samples;
      }
    }
    rtl_wbfm.previous_i = i;
    rtl_wbfm.previous_q = q;
    rtl_wbfm.have_previous = true;
  }
  if (audio_count == 0) return;
  if (M5.Speaker.playRaw(audio, audio_count, 48000, false, 1, 0, false)) {
    ++rtl_wbfm.queued_chunks;
  } else {
    ++rtl_wbfm.dropped_chunks;
  }
  rtl_wbfm.buffer = (rtl_wbfm.buffer + 1) % std::size(rtl_audio_buffers);
}

void run_rtl_capture() {
  const RtlPreset preset = rtl_requested_preset.load(std::memory_order_acquire);
  const bool continuous = rtl_continuous_requested.load(std::memory_order_acquire);
  const uint32_t frequency_hz = rtl_preset_frequency(preset);
  const float audio_scale = preset == RtlPreset::noaa ? 30000.0f : 7000.0f;
  rtl_capture_state.store(RtlCaptureState::running, std::memory_order_release);
  rtl_ui_active.store(true, std::memory_order_release);
  set_rtl_sdr_status(continuous ? "RTL-SDR V4: continuous listening"
                                : "RTL-SDR V4: bounded capture running");
  draw_sdr_screen(preset);
  rtl_capture_bytes = 0;
  rtl_capture_min = 0;
  rtl_capture_max = 0;
  rtl_capture_mean = 0;
  rtl_capture_sha256[0] = '\0';
  strlcpy(rtl_capture_error, "none", sizeof(rtl_capture_error));
  rtl_wbfm = {};

  const usb_config_desc_t* config = nullptr;
  esp_err_t result = usb_host_get_active_config_descriptor(rtl_sdr_device, &config);
  if (result != ESP_OK || config == nullptr || config->bConfigurationValue != 1) {
    strlcpy(rtl_capture_error, "USB configuration 1 is not active",
            sizeof(rtl_capture_error));
    rtl_capture_state.store(RtlCaptureState::failed, std::memory_order_release);
    set_rtl_sdr_status("RTL-SDR V4: configuration error");
    return;
  }
  result = usb_host_interface_claim(usb_client, rtl_sdr_device, 0, 0);
  if (result != ESP_OK) {
    snprintf(rtl_capture_error, sizeof(rtl_capture_error), "interface claim: %s",
             esp_err_to_name(result));
    rtl_capture_state.store(RtlCaptureState::failed, std::memory_order_release);
    set_rtl_sdr_status("RTL-SDR V4: interface claim failed");
    return;
  }

  const RtlControlRecord standard_probe{0x0100, 0x0000, 0x80, 18, {}};
  const bool standard_in_ok = run_control_record(standard_probe, USB_B_REQUEST_GET_DESCRIPTOR);
  const bool initialized = standard_in_ok && run_rtl_initialization();
  const bool rate_set = initialized && set_rtl_sample_rate_960k();
  const bool tuned = rate_set && run_rtl_preset(preset);
  M5.Speaker.stop();
  M5.Speaker.setVolume(80);
  const bool speaker_ok = M5.Speaker.isEnabled() &&
                          (M5.Speaker.isRunning() || M5.Speaker.begin());
  Serial.printf("RTL_EP0_CONTROL_PROBE standard_in=%s captured_init=%s "
                "sample_rate=%s preset=%s tuned=%s\n",
                standard_in_ok ? "ok" : "failed", initialized ? "ok" : "failed",
                rate_set ? "ok" : "failed", rtl_preset_name(preset),
                tuned ? "ok" : "failed");
  bool stream_ok = tuned && speaker_ok;
  if (tuned && !speaker_ok) {
    strlcpy(rtl_capture_error, "speaker unavailable", sizeof(rtl_capture_error));
  }
  usb_transfer_t* bulk = nullptr;
  mbedtls_sha256_context sha;
  mbedtls_sha256_init(&sha);
  uint64_t sum = 0;
  uint64_t submitted_bytes = 0;
  uint8_t minimum = UINT8_MAX;
  uint8_t maximum = 0;
  size_t requested = 0;
  bool bulk_in_flight = false;
  const uint32_t stream_started = millis();

  if (stream_ok && (usb_host_transfer_alloc(kRtlBulkBytes, 0, &bulk) != ESP_OK ||
                    (!continuous && mbedtls_sha256_starts(&sha, 0) != 0))) {
    strlcpy(rtl_capture_error, "stream setup failed", sizeof(rtl_capture_error));
    stream_ok = false;
  }

  auto submit_bulk = [&](size_t bytes) {
    requested = bytes;
    bulk->num_bytes = bytes;
    bulk->device_handle = rtl_sdr_device;
    bulk->bEndpointAddress = 0x81;
    bulk->callback = usb_transfer_complete;
    bulk->context = nullptr;
    usb_transfer_done.store(false, std::memory_order_release);
    const esp_err_t submitted = usb_host_transfer_submit(bulk);
    if (submitted != ESP_OK) {
      snprintf(rtl_capture_error, sizeof(rtl_capture_error), "bulk submit: %s",
               esp_err_to_name(submitted));
      return false;
    }
    bulk_in_flight = true;
    submitted_bytes += bytes;
    return true;
  };

  if (stream_ok) {
    stream_ok = submit_bulk(
        continuous ? kRtlBulkBytes
                   : min(kRtlBulkBytes, static_cast<size_t>(kRtlCaptureBytes)));
  }
  while (stream_ok && !rtl_sdr_gone && bulk_in_flight) {
    const uint32_t elapsed = millis() - stream_started;
    if (!continuous && elapsed >= kRtlCaptureTimeoutMs) {
      strlcpy(rtl_capture_error, "capture timeout", sizeof(rtl_capture_error));
      stream_ok = false;
      break;
    }
    const bool completed = wait_for_usb_transfer(
        bulk, continuous ? kRtlControlTimeoutMs : kRtlCaptureTimeoutMs - elapsed);
    bulk_in_flight = false;
    if (!completed || bulk->actual_num_bytes != static_cast<int>(requested)) {
      snprintf(rtl_capture_error, sizeof(rtl_capture_error),
               "bulk failed status=%d actual=%d expected=%u",
               static_cast<int>(bulk->status), bulk->actual_num_bytes,
               static_cast<unsigned>(requested));
      stream_ok = false;
      break;
    }
    const size_t completed_bytes = requested;
    memcpy(rtl_iq_processing, bulk->data_buffer, completed_bytes);

    const bool should_queue = continuous
        ? !rtl_stop_requested.load(std::memory_order_acquire)
        : submitted_bytes < kRtlCaptureBytes;
    if (should_queue) {
      const size_t next_bytes = continuous
          ? kRtlBulkBytes
          : min(kRtlBulkBytes,
                static_cast<size_t>(kRtlCaptureBytes - submitted_bytes));
      if (!submit_bulk(next_bytes)) {
        stream_ok = false;
        break;
      }
    }

    for (size_t index = 0; index < completed_bytes; ++index) {
      const uint8_t value = rtl_iq_processing[index];
      minimum = min(minimum, value);
      maximum = max(maximum, value);
      sum += value;
    }
    demodulate_fm(rtl_iq_processing, completed_bytes, audio_scale);
    draw_spectrum(rtl_iq_processing, completed_bytes);
    if (!continuous &&
        mbedtls_sha256_update(&sha, rtl_iq_processing, completed_bytes) != 0) {
      strlcpy(rtl_capture_error, "SHA-256 update failed", sizeof(rtl_capture_error));
      stream_ok = false;
      break;
    }
    rtl_capture_bytes += completed_bytes;
    if (continuous && rtl_stop_requested.load(std::memory_order_acquire)) break;
  }
  if (rtl_sdr_gone && stream_ok) {
    strlcpy(rtl_capture_error, "RTL-SDR disconnected", sizeof(rtl_capture_error));
    stream_ok = false;
  }

  const uint32_t stream_elapsed_ms = millis() - stream_started;
  uint8_t digest[32];
  const bool digest_ok = continuous || mbedtls_sha256_finish(&sha, digest) == 0;
  if (stream_ok && rtl_capture_bytes > 0 && digest_ok) {
    static constexpr char kHex[] = "0123456789abcdef";
    if (continuous) {
      strlcpy(rtl_capture_sha256, "not_recorded", sizeof(rtl_capture_sha256));
    } else {
      for (size_t index = 0; index < sizeof(digest); ++index) {
        rtl_capture_sha256[index * 2] = kHex[digest[index] >> 4];
        rtl_capture_sha256[index * 2 + 1] = kHex[digest[index] & 0x0f];
      }
      rtl_capture_sha256[64] = '\0';
    }
    rtl_capture_min = minimum;
    rtl_capture_max = maximum;
    rtl_capture_mean = static_cast<double>(sum) / rtl_capture_bytes;
  } else {
    stream_ok = false;
    if (strcmp(rtl_capture_error, "none") == 0) {
      strlcpy(rtl_capture_error, rtl_capture_bytes == 0 ? "empty capture"
                                                        : "SHA-256 finish failed",
              sizeof(rtl_capture_error));
    }
  }
  mbedtls_sha256_free(&sha);
  if (bulk != nullptr && bulk_in_flight) {
    wait_for_usb_transfer(bulk, kRtlControlTimeoutMs);
    bulk_in_flight = false;
  }
  if (bulk != nullptr && usb_transfer_done.load(std::memory_order_acquire)) {
    usb_host_transfer_free(bulk);
  }

  if (initialized && !rtl_sdr_gone) {
    char stream_error[sizeof(rtl_capture_error)];
    if (!stream_ok) strlcpy(stream_error, rtl_capture_error, sizeof(stream_error));
    const bool cleanup_ok = run_control_records(kRtlCleanupTransfers, !stream_ok);
    if (!stream_ok) {
      strlcpy(rtl_capture_error, stream_error, sizeof(rtl_capture_error));
    } else if (!cleanup_ok) {
      stream_ok = false;
    }
  }
  if (usb_host_interface_release(usb_client, rtl_sdr_device, 0) != ESP_OK && stream_ok) {
    strlcpy(rtl_capture_error, "interface release failed", sizeof(rtl_capture_error));
    stream_ok = false;
  }

  const bool requested_capture_complete =
      continuous ? rtl_stop_requested.load(std::memory_order_acquire)
                 : rtl_capture_bytes == kRtlCaptureBytes;
  if (stream_ok && requested_capture_complete && minimum != maximum &&
      rtl_wbfm.queued_chunks > 0) {
    rtl_capture_state.store(RtlCaptureState::complete, std::memory_order_release);
    set_rtl_sdr_status("RTL-SDR V4: capture complete");
    draw_sdr_controls(preset, false);
    const double audio_rms = sqrt(rtl_wbfm.square_sum / rtl_wbfm.samples);
    const double effective_sps = stream_elapsed_ms == 0
        ? 0
        : static_cast<double>(rtl_capture_bytes) * 500.0 / stream_elapsed_ms;
    Serial.printf("RTL_CAPTURE_OK preset=\"%s\" frequency_hz=%u bytes=%llu "
                  "stream_ms=%u effective_sps=%.0f min=%u max=%u mean=%.3f sha256=%s "
                  "audio_samples=%llu audio_peak=%d audio_rms=%.1f audio_chunks=%u dropped=%u\n",
                  rtl_preset_name(preset), frequency_hz,
                  static_cast<unsigned long long>(rtl_capture_bytes),
                  stream_elapsed_ms, effective_sps, rtl_capture_min, rtl_capture_max,
                  rtl_capture_mean, rtl_capture_sha256,
                  static_cast<unsigned long long>(rtl_wbfm.samples), rtl_wbfm.peak,
                  audio_rms, rtl_wbfm.queued_chunks, rtl_wbfm.dropped_chunks);
  } else {
    if (stream_ok) {
      strlcpy(rtl_capture_error, minimum == maximum ? "constant stream" : "speaker queue failed",
              sizeof(rtl_capture_error));
    }
    rtl_capture_state.store(RtlCaptureState::failed, std::memory_order_release);
    set_rtl_sdr_status("RTL-SDR V4: capture failed");
    draw_sdr_controls(preset, false);
    Serial.printf("RTL_CAPTURE_ERROR bytes=%llu reason=\"%s\"\n",
                  static_cast<unsigned long long>(rtl_capture_bytes), rtl_capture_error);
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

  char manufacturer[48]{};
  char product[48]{};
  char serial[48]{};
  usb_string_to_ascii(info.str_desc_manufacturer, manufacturer, sizeof(manufacturer));
  usb_string_to_ascii(info.str_desc_product, product, sizeof(product));
  usb_string_to_ascii(info.str_desc_serial_num, serial, sizeof(serial));
  const char* speed = info.speed == USB_SPEED_HIGH ? "high" :
                      info.speed == USB_SPEED_FULL ? "full" : "low";
  Serial.printf("RTL_SDR_USB vid=%04x pid=%04x speed=%s manufacturer=\"%s\" product=\"%s\" serial=\"%s\"\n",
                descriptor->idVendor, descriptor->idProduct, speed, manufacturer,
                product, serial);

  if (descriptor->idVendor == 0x0bda && descriptor->idProduct == 0x2838 &&
      strcmp(manufacturer, "RTLSDRBlog") == 0 && strcmp(product, "Blog V4") == 0 &&
      strcmp(serial, "00000001") == 0) {
    rtl_sdr_device = device;
    rtl_sdr_vid = descriptor->idVendor;
    rtl_sdr_pid = descriptor->idProduct;
    strlcpy(rtl_sdr_speed, speed, sizeof(rtl_sdr_speed));
    strlcpy(rtl_sdr_serial, serial, sizeof(rtl_sdr_serial));
    rtl_capture_state.store(RtlCaptureState::ready, std::memory_order_release);
    char status[96];
    snprintf(status, sizeof(status), "RTL-SDR V4 ready: %s USB, serial %s", speed, serial);
    set_rtl_sdr_status(status);
    Serial.printf("RTL_SDR_PROBE_OK v4=true presets=96100000,162400000 "
                  "sample_rate_sps=%u validation_bytes=%u continuous_touch=true\n",
                  kRtlSampleRateSps, kRtlCaptureBytes);
  } else {
    if (descriptor->idVendor == 0x0bda && descriptor->idProduct == 0x2838) {
      Serial.println("RTL_SDR_REJECTED reason=official_v4_identity_mismatch");
    }
    usb_host_device_close(usb_client, device);
  }
}

void clear_rtl_sdr_device() {
  if (rtl_sdr_device != nullptr) {
    usb_host_device_close(usb_client, rtl_sdr_device);
    rtl_sdr_device = nullptr;
  }
  rtl_sdr_vid = 0;
  rtl_sdr_pid = 0;
  strlcpy(rtl_sdr_speed, "none", sizeof(rtl_sdr_speed));
  rtl_sdr_serial[0] = '\0';
  rtl_capture_state.store(RtlCaptureState::disconnected, std::memory_order_release);
  set_rtl_sdr_status("RTL-SDR: disconnected");
  Serial.println("RTL_SDR_DISCONNECTED");
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
      clear_rtl_sdr_device();
    }
    if (rtl_sdr_device != nullptr && !rtl_sdr_gone &&
        rtl_capture_requested.exchange(false, std::memory_order_acq_rel)) {
      run_rtl_capture();
      if (rtl_restart_requested.exchange(false, std::memory_order_acq_rel) &&
          rtl_sdr_device != nullptr && !rtl_sdr_gone) {
        rtl_stop_requested.store(false, std::memory_order_release);
        rtl_capture_requested.store(true, std::memory_order_release);
      }
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

void queue_local_rtl_preset(RtlPreset preset) {
  if (rtl_sdr_device == nullptr) return;
  rtl_requested_preset.store(preset, std::memory_order_release);
  rtl_continuous_requested.store(true, std::memory_order_release);
  const RtlCaptureState state = rtl_capture_state.load(std::memory_order_acquire);
  if (state == RtlCaptureState::running) {
    rtl_restart_requested.store(true, std::memory_order_release);
    rtl_stop_requested.store(true, std::memory_order_release);
  } else {
    rtl_restart_requested.store(false, std::memory_order_release);
    rtl_stop_requested.store(false, std::memory_order_release);
    rtl_capture_requested.store(true, std::memory_order_release);
  }
  append_journal(preset == RtlPreset::noaa ? "sdr_noaa" : "sdr_kzel");
}

void handle_sdr_touch(int32_t x, int32_t y) {
  if (y < kSdrControlsY || y >= kSdrControlsY + kSdrControlsHeight) return;
  if (x >= kSdrKzelX && x < kSdrKzelX + kSdrKzelWidth) {
    queue_local_rtl_preset(RtlPreset::kzel);
  } else if (x >= kSdrNoaaX && x < kSdrNoaaX + kSdrNoaaWidth) {
    queue_local_rtl_preset(RtlPreset::noaa);
  } else if (x >= kSdrStopX && x < kSdrStopX + kSdrStopWidth) {
    rtl_restart_requested.store(false, std::memory_order_release);
    rtl_stop_requested.store(true, std::memory_order_release);
    append_journal("sdr_stop");
  }
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
      "\"payload\":{\"adapter\":{\"id\":\"m5tab5.serial\",\"version\":\"0.8.23\"},"
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
  if (strcmp(command, "RTL_CAPTURE_STATUS") == 0) {
    const RtlCaptureState state = rtl_capture_state.load(std::memory_order_acquire);
    if (state != RtlCaptureState::complete && state != RtlCaptureState::failed) {
      Serial.printf("RTL_CAPTURE_STATUS state=%s\n", rtl_capture_state_name(state));
      return;
    }
    Serial.printf(
        "RTL_CAPTURE_STATUS state=%s bytes=%llu min=%u max=%u mean=%.3f sha256=%s error=\"%s\"\n",
        rtl_capture_state_name(state),
        static_cast<unsigned long long>(rtl_capture_bytes), rtl_capture_min,
        rtl_capture_max, rtl_capture_mean,
        rtl_capture_sha256[0] == '\0' ? "none" : rtl_capture_sha256,
        rtl_capture_error);
    return;
  }
  if (strcmp(command, "RTL_STOP") == 0 && authenticated) {
    rtl_restart_requested.store(false, std::memory_order_release);
    rtl_stop_requested.store(true, std::memory_order_release);
    Serial.println("RTL_STOPPING");
    return;
  }
  const bool continuous_command = strncmp(command, "RTL_LISTEN", 10) == 0;
  const bool kzel_capture = strcmp(command, "RTL_CAPTURE") == 0 ||
                            strcmp(command, "RTL_CAPTURE KZEL") == 0 ||
                            strcmp(command, "RTL_LISTEN KZEL") == 0;
  const bool noaa_capture = strcmp(command, "RTL_CAPTURE NOAA") == 0 ||
                            strcmp(command, "RTL_LISTEN NOAA") == 0;
  if ((kzel_capture || noaa_capture) && authenticated) {
    const RtlPreset preset = noaa_capture ? RtlPreset::noaa : RtlPreset::kzel;
    RtlCaptureState expected = RtlCaptureState::ready;
    const RtlCaptureState current = rtl_capture_state.load(std::memory_order_acquire);
    if (current == RtlCaptureState::complete || current == RtlCaptureState::failed) {
      expected = current;
    }
    if (rtl_sdr_device == nullptr ||
        !rtl_capture_state.compare_exchange_strong(expected, RtlCaptureState::queued,
                                                   std::memory_order_acq_rel)) {
      Serial.println("RTL_CAPTURE_BUSY_OR_UNAVAILABLE");
      return;
    }
    rtl_requested_preset.store(preset, std::memory_order_release);
    rtl_continuous_requested.store(continuous_command, std::memory_order_release);
    rtl_stop_requested.store(false, std::memory_order_release);
    rtl_restart_requested.store(false, std::memory_order_release);
    rtl_capture_requested.store(true, std::memory_order_release);
    Serial.printf("RTL_CAPTURE_QUEUED frequency_hz=%u sample_rate_sps=%u "
                  "bytes=%u continuous=%s\n",
                  rtl_preset_frequency(preset), kRtlSampleRateSps, kRtlCaptureBytes,
                  continuous_command ? "true" : "false");
    return;
  }
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
    if (!rtl_ui_active.load(std::memory_order_acquire)) draw_rtl_sdr_state();
  }

  const auto touch = M5.Touch.getDetail(0);
  const bool pressed = touch.isPressed() || touch.wasPressed();
  if (pressed && !was_pressed) {
    if (rtl_ui_active.load(std::memory_order_acquire)) {
      handle_sdr_touch(touch.x, touch.y);
    } else if (point_in_button(touch.x, touch.y)) {
      if (rtl_sdr_device != nullptr) {
        queue_local_rtl_preset(RtlPreset::kzel);
      } else {
        emit_touch(touch.x, touch.y);
      }
    }
  }
  was_pressed = pressed;

  const uint32_t now = millis();
  if (!rtl_ui_active.load(std::memory_order_acquire) &&
      now - last_power_draw_ms >= 10000) {
    last_power_draw_ms = now;
    draw_power_state();
  }
  if (!offline_transition_handled && now - last_ping_ms > kSessionTimeoutMs) {
    authenticated = false;
    offline_transition_handled = true;
    if (!rtl_ui_active.load(std::memory_order_acquire)) {
      draw_session_state("Host offline - local journal active", TFT_ORANGE);
    }
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
