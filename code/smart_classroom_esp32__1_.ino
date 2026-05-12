/*
 * ============================================================
 *  SMART CLASSROOM MONITORING - ESP32 Firmware
 *  Kelompok 137 Nyquist - IoT Project
 * ============================================================
 *  Hardware:
 *    - ESP32 DevKit V1
 *    - PIR HC-SR501  -> GPIO 34
 *    - LDR 4-pin AO  -> GPIO 35
 *    - LDR 4-pin DO  -> GPIO 32
 *    - Relay 5V      -> GPIO 26 (aktif LOW)
 *    - LED Indikator -> GPIO 14
 *
 *  Dependensi Library (Install via Arduino Library Manager):
 *    - PubSubClient  (Nick O'Leary)
 *    - ArduinoJson   (Benoit Blanchon)
 * ============================================================
 *
 *  CARA INSTALL LIBRARY:
 *  Arduino IDE -> Sketch -> Include Library -> Manage Libraries
 *  Cari dan install: "PubSubClient" dan "ArduinoJson"
 *
 *  CARA UPLOAD:
 *  1. Board: "ESP32 Dev Module"
 *  2. Port: sesuai COM port
 *  3. Upload Speed: 115200
 * ============================================================
 */

#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>

// ============================================================
//  KONFIGURASI - SESUAIKAN DENGAN SETUP ANDA
// ============================================================

// WiFi
const char* WIFI_SSID     = "Shizika";
const char* WIFI_PASSWORD = "12sampe9";

// MQTT Broker (IP komputer yang menjalankan Mosquitto)
const char* MQTT_BROKER   = "10.105.116.135";  // Ganti dengan IP broker Anda
const int  MQTT_PORT     = 1883;
const char* MQTT_USER     = "admin";           // Username MQTT broker
const char* MQTT_PASS     = "password123";     // Password MQTT broker
const char* MQTT_CLIENT   = "esp32_classroom_1";

// MQTT Topics
const char* TOPIC_SENSOR  = "classroom/sensor";
const char* TOPIC_STATUS  = "classroom/status";
const char* TOPIC_CMD     = "classroom/command";  // untuk terima perintah dari dashboard

// ============================================================
//  PIN DEFINITIONS
// ============================================================
#define PIN_PIR         34   // PIR HC-SR501 output
#define PIN_LDR_ANALOG  35   // LDR output analog
#define PIN_LDR_DIGITAL 32   // LDR output digital
#define PIN_RELAY       26   // Relay IN (aktif LOW)
#define PIN_LED         14   // LED indikator status

// ============================================================
//  KONFIGURASI SISTEM
// ============================================================
#define INTERVAL_PUBLISH    60000   // Kirim data tiap 60 detik (1 menit)
#define PIR_DELAY           30000   // Tunggu 30 detik sebelum matikan lampu
#define LDR_THRESHOLD_DARK  2000    // Nilai ADC dibawah ini = gelap (sesuaikan!)
#define RELAY_ACTIVE_LOW    true    // true jika relay aktif LOW (umumnya begitu)

// ============================================================
//  VARIABEL GLOBAL
// ============================================================
WiFiClient   espClient;
PubSubClient mqttClient(espClient);

bool  pirState        = false;   // Ada orang = true
bool  relayState      = false;   // Lampu menyala = true
int   ldrValue        = 0;       // Nilai analog LDR (0-4095)
bool  ldrDigital      = false;   // Kondisi gelap digital
unsigned long lastPublish    = 0;
unsigned long lastPirDetect  = 0;
unsigned long lastReconnect  = 0;

// ============================================================
//  FUNGSI RELAY
// ============================================================
void setRelay(bool on) {
  relayState = on;
  if (RELAY_ACTIVE_LOW) {
    digitalWrite(PIN_RELAY, on ? LOW : HIGH);
  } else {
    digitalWrite(PIN_RELAY, on ? HIGH : LOW);
  }
  // LED ikut status relay
  digitalWrite(PIN_LED, on ? HIGH : LOW);
  Serial.printf("[RELAY] Lampu: %s\n", on ? "MENYALA" : "MATI");
}

// ============================================================
//  KONEKSI WIFI
// ============================================================
void connectWiFi() {
  Serial.printf("\n[WiFi] Menghubungkan ke: %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 30) {
    delay(500);
    Serial.print(".");
    retry++;
    // Kedipkan LED saat connecting
    digitalWrite(PIN_LED, !digitalRead(PIN_LED));
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n[WiFi] Terhubung! IP: %s\n", WiFi.localIP().toString().c_str());
    digitalWrite(PIN_LED, LOW);
  } else {
    Serial.println("\n[WiFi] GAGAL terhubung. Restart...");
    ESP.restart();
  }
}

// ============================================================
//  CALLBACK MQTT (terima perintah dari dashboard/Node-RED)
// ============================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  
  Serial.printf("[MQTT] Pesan dari %s: %s\n", topic, msg.c_str());
  
  // Parse JSON command
  StaticJsonDocument<200> doc;
  if (deserializeJson(doc, msg) == DeserializationError::Ok) {
    // Contoh: {"command":"relay","value":true}
    const char* cmd = doc["command"];
    if (cmd && strcmp(cmd, "relay") == 0) {
      bool val = doc["value"];
      setRelay(val);
    }
  }
}

// ============================================================
//  KONEKSI MQTT
// ============================================================
bool connectMQTT() {
  Serial.printf("[MQTT] Menghubungkan ke broker %s:%d ...\n", MQTT_BROKER, MQTT_PORT);
  
  if (mqttClient.connect(MQTT_CLIENT, MQTT_USER, MQTT_PASS)) {
    Serial.println("[MQTT] Terhubung!");
    mqttClient.subscribe(TOPIC_CMD);
    
    // Publish status online
    StaticJsonDocument<100> doc;
    doc["device"]  = MQTT_CLIENT;
    doc["status"]  = "online";
    char buf[100];
    serializeJson(doc, buf);
    mqttClient.publish(TOPIC_STATUS, buf, true); // retained
    return true;
  }
  
  Serial.printf("[MQTT] Gagal. State: %d\n", mqttClient.state());
  return false;
}

// ============================================================
//  BACA SENSOR
// ============================================================
void readSensors() {
  // Baca PIR
  pirState = digitalRead(PIN_PIR);
  
  // Baca LDR
  ldrValue   = analogRead(PIN_LDR_ANALOG);  // 0-4095 pada ESP32
  ldrDigital = digitalRead(PIN_LDR_DIGITAL); // HIGH = gelap (tergantung modul)
  
  // Konversi ke persentase kecerahan (0-100%)
  // ESP32 ADC 12-bit: 0=gelap total, 4095=terang penuh
  // Sesuaikan tergantung posisi potensiometer modul LDR
}

// ============================================================
//  LOGIKA KONTROL LAMPU
// ============================================================
void controlLogic() {
  unsigned long now = millis();
  
  if (pirState) {
    // Ada gerakan terdeteksi
    lastPirDetect = now;
    
    if (!relayState) {
      // Cek intensitas cahaya: nyalakan lampu jika gelap
      // ldrValue rendah = gelap (sesuaikan threshold!)
      bool isDark = (ldrValue < LDR_THRESHOLD_DARK);
      if (isDark) {
        setRelay(true);
        Serial.println("[LOGIC] Ruangan terisi + gelap -> Lampu ON");
      } else {
        Serial.printf("[LOGIC] Ruangan terisi, cahaya cukup (%d) -> Lampu tetap OFF\n", ldrValue);
      }
    }
    
  } else {
    // Tidak ada gerakan
    if (relayState) {
      // Jika sudah tidak ada gerakan > PIR_DELAY detik -> matikan
      if (now - lastPirDetect > PIR_DELAY) {
        setRelay(false);
        Serial.println("[LOGIC] Ruangan kosong -> Lampu OFF");
      }
    }
  }
}

// ============================================================
//  PUBLISH DATA KE MQTT
// ============================================================
void publishData() {
  if (!mqttClient.connected()) return;
  
  int brightnessPct = map(ldrValue, 0, 4095, 0, 100);
  
  // Buat JSON payload
  StaticJsonDocument<300> doc;
  doc["device"]      = MQTT_CLIENT;
  doc["timestamp"]   = millis();
  doc["pir"]         = pirState;
  doc["occupied"]    = pirState;              // Status terisi/kosong
  doc["ldr_raw"]     = ldrValue;
  doc["brightness"]  = brightnessPct;         // 0-100%
  doc["ldr_digital"] = ldrDigital;
  doc["relay"]       = relayState;
  doc["lamp"]        = relayState;
  doc["wifi_rssi"]   = WiFi.RSSI();
  
  char buf[300];
  serializeJson(doc, buf);
  
  bool ok = mqttClient.publish(TOPIC_SENSOR, buf);
  Serial.printf("[MQTT] Publish %s: %s\n", ok ? "OK" : "GAGAL", buf);
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);
  Serial.println("\n========================================");
  Serial.println("  Smart Classroom IoT - Kelompok 137");
  Serial.println("========================================");
  
  // Inisialisasi pin
  pinMode(PIN_PIR,         INPUT);
  pinMode(PIN_LDR_DIGITAL, INPUT);
  pinMode(PIN_RELAY,       OUTPUT);
  pinMode(PIN_LED,         OUTPUT);
  
  // Relay OFF saat boot (aktif LOW -> HIGH = OFF)
  setRelay(false);
  
  // Koneksi WiFi
  connectWiFi();
  
  // Setup MQTT
  mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
  mqttClient.setCallback(mqttCallback);
  mqttClient.setKeepAlive(60);
  
  connectMQTT();
  
  Serial.println("[SYSTEM] Siap! Mulai monitoring...");
  Serial.printf("[CONFIG] PIR timeout: %d detik\n", PIR_DELAY / 1000);
  Serial.printf("[CONFIG] Publish interval: %d detik\n", INTERVAL_PUBLISH / 1000);
  Serial.printf("[CONFIG] LDR threshold: %d\n", LDR_THRESHOLD_DARK);
}

// ============================================================
//  LOOP UTAMA
// ============================================================
void loop() {
  unsigned long now = millis();
  
  // Reconnect WiFi jika putus
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[WiFi] Koneksi putus, reconnect...");
    connectWiFi();
  }
  
  // Reconnect MQTT jika putus (coba tiap 5 detik)
  if (!mqttClient.connected()) {
    if (now - lastReconnect > 5000) {
      lastReconnect = now;
      if (!connectMQTT()) {
        Serial.println("[MQTT] Reconnect gagal, coba lagi...");
      }
    }
  }
  
  mqttClient.loop();
  
  // Baca sensor setiap loop
  readSensors();
  
  // Jalankan logika kontrol
  controlLogic();
  
  // Publish data tiap INTERVAL_PUBLISH
  if (now - lastPublish >= INTERVAL_PUBLISH) {
    lastPublish = now;
    publishData();
    
    // Debug serial
    Serial.printf("[SENSOR] PIR:%d | LDR:%d (%d%%) | Relay:%d\n",
      pirState, ldrValue, map(ldrValue,0,4095,0,100), relayState);
  }
  
  delay(200); // Sampling rate ~5Hz
}
