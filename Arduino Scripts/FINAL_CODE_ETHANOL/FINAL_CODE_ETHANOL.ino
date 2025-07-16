#include <WiFi.h>
#include <WiFiClient.h>
#include <ArduinoJson.h>

#define STEP_PIN        2
#define DIR_PIN         3
#define ENABLE_PIN      4
#define SWITCH_TOP      6
#define SWITCH_BOTTOM   7
#define TRIG_PIN        12


const char* ssid = "Testing Facility";
const char* password = "testingfacility";
const char* server_ip = "192.168.1.228"; 
const uint16_t server_port = 4850;

WiFiClient client;

int b_Homing_E = 0, w_Main_EV = 0, b_SingleStep_E = 0;
int previous_homing_e = 0;
long pos_top = 0, pos_bottom = 0, current_position = 0, target_position = 0;
bool is_calibrated = false, is_calibrating = false;

const int CALIBRATION_DELAY_US = 3000; // ~20 RPM
const int MIN_DELAY_US = 1800;          // fast speed
const int MAX_DELAY_US = 1800;         // slow start/stop
const float RAMP_FRACTION = 1;       // ramp in/out

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
  pinMode(TRIG_PIN, OUTPUT);
  
  digitalWrite(ENABLE_PIN, LOW);

  if (client.connect(server_ip, server_port)) {
    Serial.println("✅ Connected to TCP server!");
  } else {
    Serial.println("❌ TCP connection failed.");
  }
}

void loop() {
  // 🔌 Reconnexion TCP
  if (!client.connected()) {
    Serial.println("🔄 Reconnecting...");
    client.connect(server_ip, server_port);
    delay(1000);
    return;
  }

  // 📩 Lecture TCP
  if (client.available()) {
    String line = client.readStringUntil('\n');
    StaticJsonDocument<400> doc;

    if (!deserializeJson(doc, line)) {
      b_Homing_E      = doc["b_Homing_E"].as<int>();
      w_Main_EV       = doc["w_Main_EV"].as<int>();        
      b_SingleStep_E  = doc["b_SingleStep_E"].as<int>();

      // Calibration prioritaire
      if (b_Homing_E == 1 && previous_homing_e == 0 && !is_calibrating) {
        is_calibrating = true;
        return; // va se déclencher au prochain tour
      }
      previous_homing_e = b_Homing_E;

      // Petite étape manuelle
      if (is_calibrated && !is_calibrating && b_SingleStep_E == 1) {
        digitalWrite(DIR_PIN, LOW);
        singleStep(); singleStep();
        current_position -= 2;
        sendStatus(current_positionAsPercent());
      }

      // 🎯 Mise à jour live de la cible
      if (is_calibrated && !is_calibrating) {
        target_position = map(w_Main_EV, 0, 100, pos_bottom, pos_top);
        Serial.print("📡 Nouvelle consigne : ");
        Serial.print(w_Main_EV); Serial.print("% → ");
        Serial.println(target_position);
      }
    }
  }

  // 🛠 Calibration déclenchée
  if (is_calibrating) {
    runCalibration();
    return;
  }

  // ⚙️ Mouvement live vers la consigne (pas à pas)
  if(is_calibrated && target_position != current_position) {
    stepTowardTarget();  // Fait 1 pas par loop()
  }
}

void stepTowardTarget() {
  if(target_position != current_position){ digitalWrite(TRIG_PIN, HIGH); }
  else { digitalWrite(TRIG_PIN, LOW); }

  bool direction = (target_position > current_position);
  digitalWrite(DIR_PIN, direction ? HIGH : LOW);
  digitalWrite(ENABLE_PIN, LOW);

  // Ramping (simple: vitesse lente ici)
  int delay_us = (abs(target_position - current_position) < 20) ? MAX_DELAY_US : MIN_DELAY_US;

  singleStep();
  current_position += direction ? 1 : -1;
  delayMicroseconds(delay_us);

  // 🖥 Statut
  sendStatus(current_positionAsPercent());
}


void runCalibration() {
  Serial.println("⚙️ Calibration...");
  is_calibrating = true;
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
  Serial.print("pos_top: ");
  Serial.println(pos_top);
  Serial.print("pos_bottom: ");
  Serial.println(pos_bottom);
  delay(100);
  sendStatus(1);
}

/*
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
    singleStep();
    current_position += dir ? 1 : -1;
    delayMicroseconds(delay_us);
  }

  for (long i = 0; i < cruise_steps; i++) {
    singleStep();
    current_position += dir ? 1 : -1;
    delayMicroseconds(MIN_DELAY_US);
  }

  for (long i = 0; i < ramp_steps; i++) {
    int delay_us = map(i, 0, ramp_steps, MIN_DELAY_US, MAX_DELAY_US);
    singleStep();
    current_position += dir ? 1 : -1;
    delayMicroseconds(delay_us);
  }
}*/


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
