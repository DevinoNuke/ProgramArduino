#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Fuzzy.h>
#include <ArduinoJson.h>
#include "esp32-hal-ledc.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Inisialisasi display OLED
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Update kredensial Wi-Fi
const char* ssid = "SKBM Lt.2";       // Ganti dengan SSID Anda
const char* password = "senyumdulu";  // Ganti dengan password Anda

// Update detail broker MQTT
const char* mqtt_server = "broker.emqx.io"; 
const int mqtt_port = 1883; 
const char* mqtt_topic = "sensor/gsr"; 
const char* control_topic = "therapy/control"; // Ganti nama topic menjadi lebih jelas
const char* status_topic = "therapy/status";

// Pin untuk sensor GSR dan relay
const int gsrPin1 = 32; 
const int gsrPin2 = 33; 
const int relay = 5;
const int pwmPin = 25;

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long therapyStartTime = 0;
unsigned long therapyDuration = 60 * 1000; // Durasi default
bool therapyActive = false;

int voltageLevel = 0;
const int maxVoltage = 20;

Fuzzy *fuzzy = new Fuzzy();

// Deklarasi FuzzySets
FuzzySet *gsrLow, *gsrMedium, *gsrHigh;
FuzzySet *voltageLow, *voltageMedium, *voltageHigh;
FuzzySet *durationShort, *durationMedium, *durationLong;

// Ukuran buffer untuk JSON
const size_t CAPACITY = JSON_OBJECT_SIZE(10);

// Tambahkan deklarasi global
int gsrValue1 = 0;
int gsrValue2 = 0;

void setup_wifi() {
  delay(10);
  Serial.println();
  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message received from topic [");
  Serial.print(topic);
  Serial.print("]: ");
  
  // Buat buffer untuk JSON
  StaticJsonDocument<CAPACITY> doc;
  
  // Parse JSON message
  DeserializationError error = deserializeJson(doc, payload, length);
  
  if (error) {
    String errorMsg = "{\"status\": \"error\", \"message\": \"Format JSON tidak valid\"}";
    client.publish(status_topic, errorMsg.c_str());
    return;
  }

  if (strcmp(topic, control_topic) == 0) {
    // Cek command dari JSON
    const char* command = doc["command"];
    
    if (command != nullptr) {
      if (strcmp(command, "stop") == 0) {
        if (therapyActive) {
          stopTherapy();
        } else {
          String msg = "{\"status\": \"info\", \"message\": \"Tidak ada terapi yang sedang berjalan\"}";
          client.publish(status_topic, msg.c_str());
        }
      } else if (strcmp(command, "start") == 0) {
        int duration = doc["duration"] | 0; // Default 0 jika tidak ada
        if (duration > 0) {
          if (!therapyActive) {
            therapyDuration = duration * 60 * 1000; // Konversi ke milidetik
            startTherapy();
          } else {
            String msg = "{\"status\": \"warning\", \"message\": \"Terapi sedang berjalan\"}";
            client.publish(status_topic, msg.c_str());
          }
        } else {
          String msg = "{\"status\": \"error\", \"message\": \"Durasi harus lebih dari 0\"}";
          client.publish(status_topic, msg.c_str());
        }
      }
    }
  }
}

void sendStatusMessage() {
  StaticJsonDocument<CAPACITY> doc;
  
  if (therapyActive) {
    // Hitung waktu dalam detik
    unsigned long currentTime = millis();
    unsigned long elapsedTimeSeconds = (currentTime - therapyStartTime) / 1000;
    unsigned long remainingTimeSeconds = (therapyDuration / 1000) - elapsedTimeSeconds;
    
    doc["status"] = "running";
    doc["elapsed_time"] = elapsedTimeSeconds;
    doc["remaining_time"] = remainingTimeSeconds;
    doc["voltage"] = map(voltageLevel, 0, 100, 0, 24);
    doc["duration_total"] = therapyDuration / 1000; // Konversi ke detik
  } else {
    doc["status"] = "stopped";
  }

  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);
  client.publish(status_topic, jsonBuffer);
}

void startTherapy() {
  therapyStartTime = millis();
  therapyActive = true;
  
  // AKTIFKAN relay - pastikan ini HIGH jika relay aktif HIGH (atau LOW jika aktif LOW)
  digitalWrite(relay, LOW);  // Aktifkan relay
  
  // Set voltage level dan AKTIFKAN PWM dengan nilai yang cukup tinggi
  voltageLevel = 50;  // Set ke 50% (sekitar 12V)
  int dutyCycle = map(voltageLevel, 0, 100, 0, 255);
  ledcWrite(0, dutyCycle);  // Aktifkan PWM
  
  // Debug untuk relay dan PWM
  Serial.println("Therapy started.");
  Serial.print("Relay PIN: ");
  Serial.print(relay);
  Serial.print(" STATE: ");
  Serial.println(digitalRead(relay));
  Serial.print("PWM PIN: ");
  Serial.print(pwmPin);
  Serial.print(" Duty Cycle: ");
  Serial.println(dutyCycle);
  
  // Update display dan kirim status
  StaticJsonDocument<CAPACITY> doc;
  doc["status"] = "started";
  doc["duration"] = therapyDuration / 60000;
  doc["voltage"] = map(voltageLevel, 0, 100, 0, 24);
  
  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);
  client.publish(status_topic, jsonBuffer);
}

void stopTherapy() {
  therapyActive = false;
  
  // MATIKAN relay (HIGH untuk OFF)
  digitalWrite(relay, HIGH);
  
  // Update display ketika terapi berakhir
  updateDisplay(false, gsrValue1, gsrValue2);
  
  StaticJsonDocument<CAPACITY> doc;
  doc["status"] = "finished";
  doc["total_time"] = (millis() - therapyStartTime) / 1000;
  
  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);
  client.publish(status_topic, jsonBuffer);
  
  Serial.println("Therapy finished.");
  Serial.println("Relay deactivated - PIN STATE: HIGH");
}

// Fungsi untuk memperbarui display
void updateDisplay(bool isActive, int gsr1, int gsr2) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  display.setCursor(0, 0);
  display.print("GSR1: ");
  display.print(gsr1);
  
  display.setCursor(0, 10);
  display.print("GSR2: ");
  display.print(gsr2);
  
  display.setCursor(0, 20);
  display.print("Voltage: ");
  display.print(map(voltageLevel, 0, 100, 0, 24));
  display.print(" V");
  
  display.setCursor(0, 30);
  display.print("Duration: ");
  display.print(therapyDuration / 60000);
  display.print(" min");
  
  if (isActive) {
    // Tampilkan info terapi aktif
    unsigned long remaining = (therapyDuration - (millis() - therapyStartTime)) / 1000;
    
    display.setCursor(0, 40);
    display.print("STATUS: AKTIF");
    display.setCursor(0, 50);
    display.print("Sisa: ");
    display.print(remaining);
    display.print(" detik");
  } else {
    // Tampilkan info terapi tidak aktif
    display.setCursor(0, 40);
    display.print("STATUS: SIAP");
    display.setCursor(0, 50);
    display.print("Tunggu perintah...");
  }
  
  display.display();
}

void setVoltage(int level) {
  if (level >= 0 && level <= 100) {
    int voltageInVolts = map(level, 0, 100, 0, 24);
    if (voltageInVolts > maxVoltage) {
      Serial.println("Voltage exceeds limit! Turning off power supply.");
      digitalWrite(relay, HIGH); // Matikan relay jika melebihi batas
      therapyActive = false;
    } else {
      voltageLevel = level;
      int dutyCycle = map(voltageLevel, 0, 100, 0, 255);
      analogWrite(pwmPin, dutyCycle);  // Gunakan analogWrite sebagai alternatif

      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(0, 0);
      display.print("Voltage: ");
      display.print(voltageInVolts);
      display.println(" V");
      display.setCursor(0, 20);
      display.print("Level: ");
      display.print(level);
      display.println("%");
      display.display();

      Serial.print("Voltage set to: ");
      Serial.print(voltageInVolts);
      Serial.println(" V");
    }
  } else {
    Serial.println("Invalid voltage value (0-100).");
  }
}

void setupFuzzy() {
  FuzzyInput *gsr = new FuzzyInput(1);
  gsrLow = new FuzzySet(0, 0, 20, 50);
  gsrMedium = new FuzzySet(80, 100, 150, 200);
  gsrHigh = new FuzzySet(210, 250, 300, 350);
  gsr->addFuzzySet(gsrLow);
  gsr->addFuzzySet(gsrMedium);
  gsr->addFuzzySet(gsrHigh);
  fuzzy->addFuzzyInput(gsr);

  FuzzyOutput *voltage = new FuzzyOutput(1);
  voltageLow = new FuzzySet(0, 0, 5, 10);
  voltageMedium = new FuzzySet(8, 12, 12, 16);
  voltageHigh = new FuzzySet(14, 18, 24, 24);
  voltage->addFuzzySet(voltageLow);
  voltage->addFuzzySet(voltageMedium);
  voltage->addFuzzySet(voltageHigh);
  fuzzy->addFuzzyOutput(voltage);

  FuzzyOutput *duration = new FuzzyOutput(2);
  durationShort = new FuzzySet(0, 0, 10, 20);
  durationMedium = new FuzzySet(15, 25, 25, 35);
  durationLong = new FuzzySet(30, 40, 60, 60);
  duration->addFuzzySet(durationShort);
  duration->addFuzzySet(durationMedium);
  duration->addFuzzySet(durationLong);
  fuzzy->addFuzzyOutput(duration);

  FuzzyRuleAntecedent *ifGsrLow = new FuzzyRuleAntecedent();
  ifGsrLow->joinSingle(gsrLow);
  FuzzyRuleConsequent *thenVoltageHighDurationLong = new FuzzyRuleConsequent();
  thenVoltageHighDurationLong->addOutput(voltageHigh);
  thenVoltageHighDurationLong->addOutput(durationLong);
  fuzzy->addFuzzyRule(new FuzzyRule(1, ifGsrLow, thenVoltageHighDurationLong));

  FuzzyRuleAntecedent *ifGsrMedium = new FuzzyRuleAntecedent();
  ifGsrMedium->joinSingle(gsrMedium);
  FuzzyRuleConsequent *thenVoltageMediumDurationMedium = new FuzzyRuleConsequent();
  thenVoltageMediumDurationMedium->addOutput(voltageMedium);
  thenVoltageMediumDurationMedium->addOutput(durationMedium);
  fuzzy->addFuzzyRule(new FuzzyRule(2, ifGsrMedium, thenVoltageMediumDurationMedium));

  FuzzyRuleAntecedent *ifGsrHigh = new FuzzyRuleAntecedent();
  ifGsrHigh->joinSingle(gsrHigh);
  FuzzyRuleConsequent *thenVoltageLowDurationShort = new FuzzyRuleConsequent();
  thenVoltageLowDurationShort->addOutput(voltageLow);
  thenVoltageLowDurationShort->addOutput(durationShort);
  fuzzy->addFuzzyRule(new FuzzyRule(3, ifGsrHigh, thenVoltageLowDurationShort));
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Subscribe ke topic yang diperlukan
      client.subscribe(mqtt_topic);
      client.subscribe(control_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("OLED initialization failed"));
    for (;;);
  } else {
    Serial.println("OLED initialized successfully");
  }

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  Serial.println("MQTT setup complete");

  // Setup relay pin
  pinMode(relay, OUTPUT);
  digitalWrite(relay, HIGH);  // Relay OFF pada awal (active LOW)
  
  // Setup PWM dengan cara yang benar - gunakan ledcAttach, bukan ledcAttachPin
  ledcAttach(pwmPin, 5000, 8);  // Pin, Frequency 5000Hz, 8-bit resolution
  ledcWrite(0, 0);              // Set initial duty cycle to 0
  
  pinMode(gsrPin1, INPUT);
  pinMode(gsrPin2, INPUT);

  setupFuzzy();
  Serial.println("Fuzzy setup complete");
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }

  client.loop();

  // Baca nilai dari kedua sensor GSR
  gsrValue1 = analogRead(gsrPin1);
  gsrValue2 = analogRead(gsrPin2);

  // Hitung nilai voltase yang akan ditampilkan
  int displayVoltage = map(voltageLevel, 0, 100, 0, 24);

  // JANGAN proses fuzzy jika dalam terapi aktif
  if (!therapyActive) {
    // Proses fuzzy hanya saat standby
    fuzzy->setInput(1, gsrValue1);
    fuzzy->fuzzify();
    float optimalVoltage = fuzzy->defuzzify(1);
    float optimalDuration = fuzzy->defuzzify(2);
    
    // Setel voltage saat tidak aktif terapi
    voltageLevel = map(optimalVoltage, 0, 24, 0, 100);
    int dutyCycle = map(voltageLevel, 0, 100, 0, 255);
    ledcWrite(0, dutyCycle);
    
    therapyDuration = optimalDuration * 60 * 1000;
    displayVoltage = map(voltageLevel, 0, 100, 0, 24);
  } else {
    // SANGAT PENTING: Pastikan relay dan PWM tetap aktif selama terapi
    digitalWrite(relay, LOW);  // Aktifkan relay (LOW = ON pada umumnya)
    
    // Pastikan PWM tetap dioutputkan dengan nilai yang benar
    int dutyCycle = map(voltageLevel, 0, 100, 0, 255);
    ledcWrite(0, dutyCycle);
    
    // Debug
    Serial.print("PWM Duty Cycle: ");
    Serial.println(dutyCycle);
  }

  // Tampilkan informasi pada OLED dengan format yang jelas
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  
  // Tampilkan status di bagian atas
  display.setCursor(0, 0);
  display.print(therapyActive ? "STATUS: AKTIF" : "STATUS: SIAP");
  
  // Tampilkan voltase dengan ukuran besar di tengah
  display.setCursor(0, 16);
  display.setTextSize(2);
  display.print("VOLT: ");
  display.print(displayVoltage);
  display.print("V");
  
  // Kembali ke ukuran teks normal untuk informasi lainnya
  display.setTextSize(1);
  
  // Tampilkan durasi
  display.setCursor(0, 40);
  display.print("Durasi: ");
  display.print(therapyDuration / 60000);
  display.print(" menit");
  
  // Tampilkan waktu tersisa jika terapi aktif
  if (therapyActive) {
    unsigned long elapsedTime = millis() - therapyStartTime;
    unsigned long remainingTime = (therapyDuration > elapsedTime) ? 
                                 (therapyDuration - elapsedTime) / 1000 : 0;
    
    display.setCursor(0, 50);
    display.print("Sisa: ");
    display.print(remainingTime);
    display.print(" detik");
  }
  
  display.display();
  String gsrStr = String(gsrValue1) + "," + String(gsrValue2); 
  client.publish(mqtt_topic, gsrStr.c_str()); 

  // Debug log untuk monitoring
  if (therapyActive) {
    unsigned long elapsedTime = millis() - therapyStartTime;
    Serial.print("Therapy Active: ");
    Serial.print(elapsedTime / 1000);
    Serial.print("s / ");
    Serial.print(therapyDuration / 1000);
    Serial.println("s");
    Serial.print("Relay PIN STATE: ");
    Serial.println(digitalRead(relay));
    Serial.print("Voltage Level: ");
    Serial.print(displayVoltage);
    Serial.println("V");
    
    // Kirim status setiap 1 detik
    static unsigned long lastStatusTime = 0;
    if (millis() - lastStatusTime >= 1000) {
      sendStatusMessage();
      lastStatusTime = millis();
    }
    
    // Cek apakah terapi sudah selesai
    if (elapsedTime >= therapyDuration) {
      stopTherapy();
    }
  }

  // Format GSR data sebagai JSON
  StaticJsonDocument<CAPACITY> gsrDoc;
  gsrDoc["gsr1"] = gsrValue1;
  gsrDoc["gsr2"] = gsrValue2;
  
  char gsrBuffer[256];
  serializeJson(gsrDoc, gsrBuffer);
  client.publish(status_topic, gsrBuffer);

  delay(500);
}