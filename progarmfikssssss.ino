#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Fuzzy.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Inisialisasi display OLED
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Update kredensial Wi-Fi
const char* ssid = "Devino";       // Ganti dengan SSID Anda
const char* password = "nono1301";  // Ganti dengan password Anda

// Update detail broker MQTT
const char* mqtt_server = "broker.emqx.io"; 
const int mqtt_port = 1883; 
const char* mqtt_topic = "sensor/gsr"; 
const char* control_topic = "startherapy"; // Ganti dengan topik untuk kontrol terapi
const char* duration_topic = "therapy/duration"; // Topik untuk durasi

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
  for (int i = 0; i < length; i++) {
    Serial.print((char)payload[i]);
  }
  Serial.println();

  if (strcmp(topic, control_topic) == 0) {
    String message = String((char*)payload, length);
    if (message.equalsIgnoreCase("start")) {
      if (!therapyActive) {
        startTherapy();
      } else {
        Serial.println("Therapy is already running.");
      }
    } else if (message.equalsIgnoreCase("stop")) {
      if (therapyActive) {
        stopTherapy();
      } else {
        Serial.println("No therapy is running.");
      }
    }
  }

  if (strcmp(topic, duration_topic) == 0) {
    String message = String((char*)payload, length);
    therapyDuration = message.toInt() * 60 * 1000; // Konversi ke milidetik
    Serial.print("New therapy duration set to: ");
    Serial.print(therapyDuration / 60000);
    Serial.println(" minutes");
  }
}

void reconnect() {
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    if (client.connect("mqtt_esp")) {
      Serial.println("connected");
      client.subscribe(mqtt_topic);
      client.subscribe(control_topic);
      client.subscribe(duration_topic); // Subscribe to the new duration topic
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
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

void startTherapy() {
  therapyStartTime = millis();
  therapyActive = true;
  digitalWrite(relay, LOW); // Menyalakan relay
  Serial.println("Therapy started.");
  Serial.print("Therapy duration: ");
  Serial.print(therapyDuration / 60000);
  Serial.println(" minutes");
  Serial.print("Voltage: ");
  Serial.print(map(voltageLevel, 0, 100, 0, 24));
  Serial.println(" V");
}

void stopTherapy() {
  therapyActive = false;
  digitalWrite(relay, HIGH); // Mematikan relay
  Serial.println("Therapy finished.");
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

  pinMode(relay, OUTPUT);
  digitalWrite(relay, HIGH); // Matikan relay saat awal

  ledcAttach(pwmPin, 5000, 8);
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

  // Proses fuzzy untuk sensor GSR pertama
  fuzzy->setInput(1, gsrValue1);
  fuzzy->fuzzify();
  float optimalVoltage = fuzzy->defuzzify(1);
  float optimalDuration = fuzzy->defuzzify(2);

  // Gunakan nilai optimal untuk tegangan dan waktu
  setVoltage(map(optimalVoltage, 0, 24, 0, 100));
  therapyDuration = optimalDuration * 60 * 1000; 

  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(1);
  display.print("GSR1: ");
  display.print(gsrValue1);
  display.setCursor(0, 20);
  display.print("GSR2: ");
  display.print(gsrValue2);
  display.setCursor(0, 40);
  display.print("Voltage: ");
  display.print(map(voltageLevel, 0, 100, 0, 24));
  display.print(" V");
  display.setCursor(0, 50);
  display.print("Duration: ");
  display.print(therapyDuration / 60000);
  display.print(" min");
  display.display();

  String gsrStr = String(gsrValue1) + "," + String(gsrValue2); 
  client.publish(mqtt_topic, gsrStr.c_str()); 

  if (therapyActive) {
    Serial.print("Therapy Active: ");
    Serial.print("Elapsed Time: ");
    Serial.print(millis() - therapyStartTime);
    Serial.print(" Duration: ");
    Serial.println(therapyDuration);

    if (millis() - therapyStartTime >= therapyDuration) {
      stopTherapy();
    }
  }

  delay(500);
}