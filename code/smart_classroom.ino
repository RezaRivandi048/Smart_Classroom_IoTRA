#define PIR_PIN 13
#define LDR_PIN 34
#define RELAY_PIN 26

int ldrValue = 0;
int pirState = 0;

unsigned long lastMotionTime = 0;
const unsigned long delayOff = 10000; // 10 detik
const int threshold = 2000;

bool roomOccupied = false;
bool lampStatus = false;

void setup() {
  Serial.begin(115200);

  pinMode(PIR_PIN, INPUT);
  pinMode(RELAY_PIN, OUTPUT);

  digitalWrite(RELAY_PIN, LOW);
}

void loop() {

  // ===== BACA SENSOR =====
  pirState = digitalRead(PIR_PIN);
  ldrValue = analogRead(LDR_PIN);

  // ===== DETEKSI KEHADIRAN =====
  if (pirState == HIGH) {
    roomOccupied = true;
    lastMotionTime = millis();
  }

  // ===== TIMER AUTO OFF =====
  if (millis() - lastMotionTime > delayOff) {
    roomOccupied = false;
  }

  // ===== LOGIKA UTAMA =====
  if (roomOccupied && ldrValue > threshold) {
    lampStatus = true;
  } else {
    lampStatus = false;
  }

  digitalWrite(RELAY_PIN, lampStatus ? HIGH : LOW);

  // ===== MONITORING SERIAL (INI IoT SIMULASI) =====
  Serial.println("===== SMART CLASSROOM SYSTEM =====");

  Serial.print("Waktu (ms): ");
  Serial.println(millis());

  Serial.print("Status Ruangan: ");
  Serial.println(roomOccupied ? "TERISI" : "KOSONG");

  Serial.print("Sensor PIR: ");
  Serial.println(pirState == HIGH ? "TERDETEKSI" : "TIDAK");

  Serial.print("Nilai LDR: ");
  Serial.println(ldrValue);

  Serial.print("Kondisi Cahaya: ");
  if (ldrValue > threshold) {
    Serial.println("GELAP");
  } else {
    Serial.println("TERANG");
  }

  Serial.print("Status Lampu: ");
  Serial.println(lampStatus ? "ON" : "OFF");

  Serial.println("=================================\n");

  delay(1000);
}
