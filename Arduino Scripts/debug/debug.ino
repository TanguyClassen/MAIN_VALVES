// :coche_blanche: CLEAN ARDUINO CODE WITH LINEAR RAMPING + TCP CRASH FIX
#include <WiFi.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>
#define STEP_PIN        2
#define DIR_PIN         3
#define ENABLE_PIN      4
#define SWITCH_TOP      6
#define SWITCH_BOTTOM   7
const char* ssid = "Tanguy";
const char* password = "Testing Facility";
const char* server_ip = "192.168.1.136";
const uint16_t server_port = 4841;
WiFiClient client;
int homing_e = 0, main_e = 0;
int previous_homing_e = 0;
long pos_top = 0, pos_bottom = 0, current_position = 0, target_position = 0;
bool is_calibrated = false, is_calibrating = false;
const int steps_per_rev = 200;
const int microstepping = 16;
const int CALIBRATION_DELAY_US = 3000; // ~20 RPM
const int MIN_DELAY_US = 250;     // fast speed
const int MAX_DELAY_US = 1000;    // slow start/stop
const float RAMP_FRACTION = 0.1;  // ramp in/out
void setup() {
  Serial.begin(115200);
  delay(1500);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }
  Serial.println("\n:coche_blanche: WiFi connected");
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(SWITCH_TOP, INPUT_PULLUP);
  pinMode(SWITCH_BOTTOM, INPUT_PULLUP);
  digitalWrite(ENABLE_PIN, LOW);
  if (client.connect(server_ip, server_port)) {
    Serial.println(":coche_blanche: Connected to TCP server!");
  } else {
    Serial.println(":x: TCP connection failed.");
  }
}
void loop() {
  if (!client.connected()) {
    Serial.println(":flèches_sens_inverse_des_aiguilles: Reconnecting...");
    client.connect(server_ip, server_port);
    delay(1000);
    return;
  }
  if (client.available()) {
    String line = client.readStringUntil('\n');
    StaticJsonDocument<400> doc;
    if (!deserializeJson(doc, line)) {
      homing_e = doc["homing_e"].as<int>();
      main_e = doc["main_e"].as<int>();
      if (homing_e == 1 && previous_homing_e == 0 && !is_calibrating) {
        runCalibration();
      }
      previous_homing_e = homing_e;
      if (is_calibrated) {
        target_position = map(main_e, 0, 100, pos_bottom, pos_top);
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
  Serial.println(":roue_dentée: Calibrating...");
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
  Serial.println(":coche_blanche: Calibration done");
  is_calibrated = true;
  is_calibrating = false;
  delay(100);  // prevent immediate TCP flush crash
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
  // Ramp-up
  for (long i = 0; i < ramp_steps; i++) {
    int delay_us = map(i, 0, ramp_steps, MAX_DELAY_US, MIN_DELAY_US);
    singleStep(); delayMicroseconds(delay_us);
  }
  // Cruise
  for (long i = 0; i < cruise_steps; i++) {
    singleStep(); delayMicroseconds(MIN_DELAY_US);
  }
  // Ramp-down
  //for (long i = 0; i < ramp_steps; i++) {
  //  int delay_us = map(i, 0, ramp_steps, MIN_DELAY_US, MAX_DELAY_US);
  //  singleStep(); delayMicroseconds(delay_us);
  //}
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