#include <Arduino.h>
#include <M5Unified.h>
#include <esp_mac.h>

namespace {
constexpr int kButtonX = 390;
constexpr int kButtonY = 300;
constexpr int kButtonWidth = 500;
constexpr int kButtonHeight = 120;

char node_id[32];
uint64_t heartbeat_sequence = 0;
uint64_t touch_sequence = 0;
uint32_t last_heartbeat_ms = 0;
bool was_pressed = false;

void draw_touch_state(const char* message, uint32_t color) {
  M5.Display.fillRect(300, 480, 680, 70, TFT_BLACK);
  M5.Display.setTextColor(color, TFT_BLACK);
  M5.Display.setTextDatum(middle_center);
  M5.Display.drawString(message, 640, 515);
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

bool point_in_button(int32_t x, int32_t y) {
  return x >= kButtonX && x < kButtonX + kButtonWidth &&
         y >= kButtonY && y < kButtonY + kButtonHeight;
}

void emit_touch(int32_t x, int32_t y) {
  ++touch_sequence;
  char status[64];
  snprintf(status, sizeof(status), "Touch verified (%llu) at %ld,%ld",
           static_cast<unsigned long long>(touch_sequence),
           static_cast<long>(x), static_cast<long>(y));
  draw_touch_state(status, TFT_GREEN);
  Serial.printf(
      "{\"type\":\"event\",\"message_id\":\"m5tab5_touch_%llu\","
      "\"protocol_version\":{\"major\":1,\"minor\":0},"
      "\"payload\":{\"node_id\":\"%s\",\"kind\":\"operator_touch\","
      "\"sequence\":%llu,\"x\":%ld,\"y\":%ld}}\n",
      static_cast<unsigned long long>(touch_sequence), node_id,
      static_cast<unsigned long long>(touch_sequence),
      static_cast<long>(x), static_cast<long>(y));
}
}  // namespace

void setup() {
  Serial.begin(115200);

  auto config = M5.config();
  M5.begin(config);
  M5.Display.setRotation(1);
  draw_ui();

  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_BASE);
  snprintf(node_id, sizeof(node_id), "m5tab5_%02x%02x%02x%02x%02x%02x",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  Serial.printf(
      "{\"type\":\"hello\",\"message_id\":\"m5tab5_hello_01\","
      "\"protocol_version\":{\"major\":1,\"minor\":0},"
      "\"payload\":{\"adapter\":{\"id\":\"m5tab5.serial\",\"version\":\"0.1.0\"},"
      "\"features\":[\"telemetry\",\"display\",\"touch\"],"
      "\"capability_namespaces\":[\"m5tab5\"],\"config_schema_version\":1}}\n");
  Serial.printf(
      "{\"type\":\"node_snapshot\",\"message_id\":\"m5tab5_snapshot_01\","
      "\"protocol_version\":{\"major\":1,\"minor\":0},"
      "\"payload\":{\"node_id\":\"%s\",\"connection_state\":\"online\","
      "\"hardware\":{\"chip\":\"%s\",\"revision\":%u,\"cores\":%u,"
      "\"flash_bytes\":%u,\"psram_bytes\":%u,\"free_heap_bytes\":%u,"
      "\"display\":{\"width\":%d,\"height\":%d,\"touch_ready\":%s}},"
      "\"capabilities\":[\"m5tab5.device.info\",\"m5tab5.health.read\","
      "\"m5tab5.display.status\",\"m5tab5.input.touch\"]}}\n",
      node_id, ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
      ESP.getFlashChipSize(), ESP.getPsramSize(), ESP.getFreeHeap(),
      M5.Display.width(), M5.Display.height(), M5.Touch.isEnabled() ? "true" : "false");
}

void loop() {
  M5.update();

  const auto touch = M5.Touch.getDetail(0);
  const bool pressed = touch.isPressed() || touch.wasPressed();
  if (pressed && !was_pressed && point_in_button(touch.x, touch.y)) {
    emit_touch(touch.x, touch.y);
  }
  was_pressed = pressed;

  const uint32_t now = millis();
  if (now - last_heartbeat_ms >= 2000) {
    last_heartbeat_ms = now;
    ++heartbeat_sequence;
    Serial.printf(
        "{\"type\":\"heartbeat\",\"message_id\":\"m5tab5_heartbeat_%llu\","
        "\"protocol_version\":{\"major\":1,\"minor\":0},"
        "\"payload\":{\"node_id\":\"%s\",\"sequence\":%llu,"
        "\"uptime_ms\":%u,\"free_heap_bytes\":%u}}\n",
        static_cast<unsigned long long>(heartbeat_sequence), node_id,
        static_cast<unsigned long long>(heartbeat_sequence), now, ESP.getFreeHeap());
  }

  delay(10);
}
