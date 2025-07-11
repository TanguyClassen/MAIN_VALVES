#include <WiFi.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

#define STEP_PIN        2
#define DIR_PIN         3
#define ENABLE_PIN      4
#define SWITCH_TOP      6
#define SWITCH_BOTTOM   7

const char* ssid = "Testing Facility";
const char* password = "testingfacility";
const char* server_ip = "192.168.1.225";  // Mac address
const uint16_t server_port = 4840;

WiFiClient client;

int b_Homing_E = 0, w_Main_EV = 0, b_SingleStep_E = 0;
int previous_homing_e = 0;
long pos_top = 0, pos_bottom = 0, current_position = 0, target_position = 0;
bool is_calibrated = false, is_calibrating = false;

const int CALIBRATION_DELAY_US = 3000; // ~20 RPM
const int MIN_DELAY_US = 500;          // fast speed
const int MAX_DELAY_US = 1200;         // slow start/stop
const float RAMP_FRACTION = 0.2;       // ramp in/out

void setup() {
  Serial.begin(115200);
  delay(1500);

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\n✅ WiFi connected");

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(SWITCH_TOP, INPUT_PULLUP);
  pinMode(SWITCH_BOTTOM, INPUT_PULLUP);
  digitalWrite(ENABLE_PIN, LOW);

  if (client.connect(server_ip, server_port)) {
    Serial.println("✅ Connected to TCP server!");
  } else {
    Serial.println("❌ TCP connection failed.");
  }
}

void loop() {
  if (!client.connected()) {
    Serial.println("🔄 Reconnecting...");
    client.connect(server_ip, server_port);
    delay(1000);
    return;
  }

  if (client.available()) {
    String line = client.readStringUntil('\n');
    StaticJsonDocument<400> doc;
    if (!deserializeJson(doc, line)) {
      b_Homing_E = doc["b_Homing_E"].as<int>();
      w_Main_EV = doc["w_Main_EV"].as<int>();
      b_SingleStep_E = doc["b_SingleStep_E"].as<int>();

      if (b_Homing_E == 1 && previous_homing_e == 0 && !is_calibrating) {
        runCalibration();
      }
      previous_homing_e = b_Homing_E;

      if (is_calibrated) {
        // 🛠 Fine closure step
        if (b_SingleStep_E == 1) {
          Serial.println("🔧 Performing fine single step...");
          digitalWrite(DIR_PIN, LOW); // closing direction
          singleStep(); singleStep();
          current_position -= 2;
          sendStatus(current_positionAsPercent());
        }

        // Main position control
        target_position = map(w_Main_EV, 0, 100, pos_bottom, pos_top);
        if (target_position != current_position) {
          moveTo(target_position);
          current_position = target_position;
          sendStatus(current_positionAsPercent());
        }
      }
    }
  }
}

void runCalibration() {
  is_calibrating = true;
  Serial.println("⚙️ Calibrating...");
  current_position = 0;

  digitalWrite(DIR_PIN, HIGH);
  while (digitalRead(SWITCH_TOP)) {
    singleStep(); delayMicroseconds(CALIBRATION_DELAY_US); current_position++;
  }
  pos_top = current_position; delay(300);

  digitalWrite(DIR_PIN, LOW);
  while (digitalRead(SWITCH_BOTTOM)) {
    singleStep(); delayMicroseconds(CALIBRATION_DELAY_US); current_position--;
  }
  pos_bottom = current_position; delay(300);

  Serial.println("✅ Calibration done");
  is_calibrated = true;
  is_calibrating = false;
  delay(100);
  sendStatus(1);
}

void moveTo(long to) {
  long total_steps = abs(to - current_position);
  if (total_steps == 0) return;

  bool dir = (to > current_position);
  digitalWrite(DIR_PIN, dir ? HIGH : LOW);
  digitalWrite(ENABLE_PIN, LOW);

  long ramp_steps = total_steps * RAMP_FRACTION;
  if (ramp_steps * 2 > total_steps) ramp_steps = total_steps / 2;
  long cruise_steps = total_steps - (2 * ramp_steps);

  for (long i = 0; i < ramp_steps; i++) {
    int delay_us = map(i, 0, ramp_steps, MAX_DELAY_US, MIN_DELAY_US);
    singleStep(); delayMicroseconds(delay_us);
  }

  for (long i = 0; i < cruise_steps; i++) {
    singleStep(); delayMicroseconds(MIN_DELAY_US);
  }

  for (long i = 0; i < ramp_steps; i++) {
    int delay_us = map(i, 0, ramp_steps, MIN_DELAY_US, MAX_DELAY_US);
    singleStep(); delayMicroseconds(delay_us);
  }
}

void singleStep() {
  digitalWrite(STEP_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(STEP_PIN, LOW);  delayMicroseconds(10);
}

void sendStatus(int value) {
  if (!client.connected()) return;
  StaticJsonDocument<64> doc;
  doc["status_epos_e"] = value;
  serializeJson(doc, client);
  client.print('\n');
}

int current_positionAsPercent() {
  if (pos_top == pos_bottom) return 0;
  return map(current_position, pos_bottom, pos_top, 0, 100);
}
