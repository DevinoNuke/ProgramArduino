#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Fuzzy.h>
#include <ArduinoJson.h>

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
const char* control_topic = "therapy/control"; // Ganti nama topic menjadi lebih jelas
const char* status_topic = "therapy/status";

// Pin untuk sensor GSR dan relay
const int gsrPin1 = 32; 
const int gsrPin2 = 33; 
const int relay = 26;
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
  
  // Aktifkan relay dan PWM
  digitalWrite(relay, LOW);  // Nyalakan relay (active LOW)
  setVoltage(50);  // Set voltage ke level default (50%)
  
  StaticJsonDocument<CAPACITY> doc;
  doc["status"] = "started";
  doc["duration"] = therapyDuration / 60000;
  doc["voltage"] = map(voltageLevel, 0, 100, 0, 24);
  
  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);
  client.publish(status_topic, jsonBuffer);
  
  Serial.println("Therapy started.");
  Serial.println("Relay activated.");
  Serial.print("Initial voltage level: ");
  Serial.println(voltageLevel);
}

void stopTherapy() {
  therapyActive = false;
  digitalWrite(relay, HIGH); // Mematikan relay
  
  StaticJsonDocument<CAPACITY> doc;
  doc["status"] = "finished";
  doc["total_time"] = (millis() - therapyStartTime) / 1000;
  
  char jsonBuffer[256];
  serializeJson(doc, jsonBuffer);
  client.publish(status_topic, jsonBuffer);
  
  Serial.println("Therapy finished.");
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
      ledcWrite(pwmPin, dutyCycle);

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
  
  // Setup PWM
  ledcSetup(0, 5000, 8);     // Channel 0, 5000 Hz, 8-bit resolution
  ledcAttachPin(pwmPin, 0);  // Attach pwmPin ke channel 0

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
  int gsrValue1 = analogRead(gsrPin1);
  int gsrValue2 = analogRead(gsrPin2);

  if (therapyActive) {
    unsigned long currentTime = millis();
    unsigned long elapsedTime = currentTime - therapyStartTime;
    
    // Debug print untuk relay
    Serial.print("Therapy Active. Relay state: ");
    Serial.println(digitalRead(relay) == LOW ? "ON" : "OFF");
    Serial.print("Voltage Level: ");
    Serial.println(voltageLevel);
    
    // Kirim status setiap 1 detik
    static unsigned long lastStatusTime = 0;
    if (currentTime - lastStatusTime >= 1000) {
      sendStatusMessage();
      lastStatusTime = currentTime;
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