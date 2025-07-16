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

int b_Homing_O = 0, w_Main_OV = 0, b_SingleStep_O = 0;
int previous_homing_e = 0;
long pos_top = 0, pos_bottom = 0;
long current_position = 0;         // position actuelle en steps
volatile long target_position = 0; // position cible (en steps)
bool is_calibrated = false, is_calibrating = false;

// Pour la calibration
const int CALIBRATION_DELAY_US = 3000; // ~20 RPM

// Délais de pas (en microsecondes)
// On utilisera une approche non bloquante pour piloter le temps entre deux steps.
const int MIN_DELAY_US = 800;          // délai minimal pour vitesse élevée (plus bas = plus rapide)
const int MAX_DELAY_US = 1800;         // délai maximal quand on approche de la cible

// Variables pour la gestion non bloquante du mouvement
unsigned long lastStepTime = 0;

void setup() {
  // Serial.begin(115200);
  // delay(1500);

  // WiFi.begin(ssid, password);
  // while (WiFi.status() != WL_CONNECTED) {
  //   delay(500); 
  //   Serial.print(".");
  // }
  // Serial.println("\n✅ WiFi connected");

  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  pinMode(SWITCH_TOP, INPUT_PULLUP);
  pinMode(SWITCH_BOTTOM, INPUT_PULLUP);
  pinMode(TRIG_PIN, OUTPUT);
  
  // digitalWrite(ENABLE_PIN, LOW);

  // if (client.connect(server_ip, server_port)) {
  //   Serial.println("✅ Connected to TCP server!");
  // } else {
  //   Serial.println("❌ TCP connection failed.");
  // }
}

void loop() {


  digitalWrite(TRIG_PIN, LOW);
  delay(1000);
  digitalWrite(TRIG_PIN, HIGH);
  delay(1000);




  /*
  // 🔌 Reconnexion TCP si nécessaire
  if (!client.connected()) {
    Serial.println("🔄 Reconnecting...");
    client.connect(server_ip, server_port);
    delay(1000);
    return;
  }

  // 📩 Lecture TCP : réception des nouvelles consignes en JSON
  if (client.available()) {
    String line = client.readStringUntil('\n');
    StaticJsonDocument<400> doc;
    if (!deserializeJson(doc, line)) {
      b_Homing_O      = doc["b_Homing_O"].as<int>();
      w_Main_OV       = doc["w_Main_OV"].as<int>();        
      b_SingleStep_O  = doc["b_SingleStep_O"].as<int>();

      // Priorité à la calibration
      if (b_Homing_O == 1 && previous_homing_e == 0 && !is_calibrating) {
        is_calibrating = true;
      }
      previous_homing_e = b_Homing_O;

      // Si commande de fine étape manuelle
      if (is_calibrated && !is_calibrating && b_SingleStep_O == 1) {
        digitalWrite(DIR_PIN, LOW);
        singleStep(); singleStep();
        current_position -= 2;
        sendStatus(current_positionAsPercent());
      }

      // Mise à jour continue de la consigne
      if (is_calibrated && !is_calibrating) {
        // La consigne en pourcentage est convertie en position (steps) entre pos_bottom et pos_top.
        target_position = map(w_Main_OV, 0, 100, pos_bottom, pos_top);
        Serial.print("📡 Consigne reçue : ");
        Serial.print(w_Main_OV);
        Serial.print("% → cible (steps) : ");
        Serial.println(target_position);
      }
    }
  }

  // Lancement de la calibration si demandée
  if (is_calibrating) {
    runCalibration();
    return;
  }

  // ⚙️ MOUVEMENT NON BLOQUANT : 
  // À chaque appel de loop(), on vérifie si suffisamment de temps s'est écoulé pour effectuer un step.
  updateStepper();
}

/// Fonction non bloquante pour piloter le stepper
void updateStepper() {
  // Si on est déjà à la cible, on désactive TRIG_PIN (indicateur de mouvement) et on ne fait rien.
  if (target_position == current_position) {
    digitalWrite(TRIG_PIN, LOW);
    return;
  }
  
  // Allume TRIG_PIN pour indiquer qu'on est en mouvement.
  digitalWrite(TRIG_PIN, HIGH);
  
  // Détermine la direction à adopter pour aller vers target_position
  bool direction = (target_position > current_position);
  digitalWrite(DIR_PIN, direction ? HIGH : LOW);
  digitalWrite(ENABLE_PIN, LOW);
  
  // Détermine dynamiquement le délai entre deux steps :
  // Quand la différence (erreur) est grande, on utilise un délai plus court (mouvement rapide).
  // Quand l'erreur est faible, on ralentit (avec MAX_DELAY_US).
  int error = abs(target_position - current_position);
  int stepDelay = (error < 20) ? MAX_DELAY_US : MIN_DELAY_US;

  // Vérifie si le délai requis s'est écoulé depuis le dernier step
  if (micros() - lastStepTime >= (unsigned long)stepDelay) {
    singleStep();
    current_position += direction ? 1 : -1;
    lastStepTime = micros();

    // Mise à jour du statut en envoyant la position actuelle en pourcentage
    sendStatus(current_positionAsPercent());
    
    // Affichage de debug
    Serial.print("🚀 current_position: ");
    Serial.print(current_position);
    Serial.print(" | cible: ");
    Serial.println(target_position);
  }
}

/// Génère un pulse sur STEP_PIN pour avancer d'un pas
void singleStep() {
  digitalWrite(STEP_PIN, HIGH); 
  delayMicroseconds(10);
  digitalWrite(STEP_PIN, LOW);  
  delayMicroseconds(10);
}

/// Exécute la procédure de calibration
void runCalibration() {
  Serial.println("⚙️ Calibration...");
  // Lors de la calibration, on bloque le mouvement
  is_calibrating = true;
  current_position = 0;

  // Recherche de la butée haute
  digitalWrite(DIR_PIN, HIGH);
  while (digitalRead(SWITCH_TOP)) {
    singleStep();
    delayMicroseconds(CALIBRATION_DELAY_US);
    current_position++;
  }
  pos_top = current_position;
  delay(300);

  // Recherche de la butée basse
  digitalWrite(DIR_PIN, LOW);
  while (digitalRead(SWITCH_BOTTOM)) {
    singleStep();
    delayMicroseconds(CALIBRATION_DELAY_US);
    current_position--;
  }
  pos_bottom = current_position;
  delay(300);

  Serial.println("✅ Calibration terminée");
  Serial.print("pos_top: "); Serial.println(pos_top);
  Serial.print("pos_bottom: "); Serial.println(pos_bottom);
  is_calibrated = true;
  is_calibrating = false;
  delay(100);
  sendStatus(1);
}

/// Envoie le statut (position actuelle en %) au serveur
void sendStatus(int value) {
  if (!client.connected()) return;
  StaticJsonDocument<64> doc;
  doc["status_epos_e"] = value;
  serializeJson(doc, client);
  client.print('\n');
}

/// Retourne la position actuelle sous forme de pourcentage (par rapport aux butées calibrées)
int current_positionAsPercent() {
  if (pos_top == pos_bottom) return 0;
  return map(current_position, pos_bottom, pos_top, 0, 100);*/
}
