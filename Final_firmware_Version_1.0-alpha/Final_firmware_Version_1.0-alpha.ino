/* Final_firmware_Version_1.0-alpha.ino
   SaveUp v1.0-alpha - Nano RP2040 Connect
   - 2 kHz sampling, Vrms/Irms/Pavg/PF/Wh
   - DHT11, I2C LCD
   - MQTT -> ThingsBoard
   - Edge Impulse anomaly detection (from exported Arduino ZIP)
   - Relay control + safety
   - Author: Muhammad Idris
*/

/* === Includes === */
#include <Arduino.h>
#include <Wire.h>
#include <WiFiNINA.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>   // MUST install ArduinoJson v6+
#include <Anomaly_Detection_inferencing.h> // from EI exported ZIP
#include "edge-impulse-sdk/classifier/ei_run_classifier.h" // EI runtime

// ===== WiFi + ThingsBoard =====
const char WIFI_SSID[]      = "YOUR_SSID";
const char WIFI_PASS[]      = "YOUR_PASSWORD";
const char THINGSBOARD_SERVER[] = "mqtt.thingsboard.cloud";  // or local TB
const uint16_t THINGSBOARD_PORT = 1883;
const char ACCESS_TOKEN[]   = "YOUR_ACCESS_TOKEN";    // Replace with your device token

/* === Pins === */
const uint8_t ADC_PIN_VOLT = A1; // ZMPT101B
const uint8_t ADC_PIN_CURR = A0; // ACS712-05A
const uint8_t DHT_PIN = 2;       // DHT11
const uint8_t RELAY_PIN = 3;     // Relay control (active HIGH)

/* === Sensors/UI === */
#define DHTTYPE DHT11
DHT dht(DHT_PIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);

/* === Sampling config === */
const unsigned int SAMPLE_FREQ = 2000; // Hz
const unsigned long SAMPLE_PERIOD_US = 1000000UL / SAMPLE_FREQ; // 500 us
const unsigned int MAINS_HZ = 50;
const unsigned int SAMPLES_PER_CYCLE = SAMPLE_FREQ / MAINS_HZ; // 40
const unsigned int CYCLES_PER_WINDOW = 10;
const unsigned int SAMPLES_PER_WINDOW = SAMPLES_PER_CYCLE * CYCLES_PER_WINDOW; // 400

/* === ADC conversion (RP2040 12-bit) === */
const float ADC_REF_V = 3.3f;
const int ADC_MAX = 4095;
const float ADC_TO_V = ADC_REF_V / (float)ADC_MAX;

/* === ACS712 sensitivity nominal === */
const float ACS_SENS = 0.185f; // V/A (5A module nominal)

/* === Calibration constants (from Stage2) === */
const float Vbias = 1.449729f;       // ADC volts (voltage channel offset)
const float Ibias = 2.305866f;       // ADC volts (current channel offset)
const float VOLTAGE_SCALE = 227.540139771f; // Vrms multiplier
const float CURRENT_SCALE = 0.264412100f;   // Irms multiplier

/* === Telemetry / MQTT state === */
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

unsigned long lastTelemetryMs = 0;
const unsigned long TELEMETRY_INTERVAL_MS = 5000UL;
double energy_Wh = 0.0;

/* === Safety thresholds === */
const float OVERCURRENT_TRIP_A = 6.0f;
const float PF_ALERT_THRESH = 0.5f;

/* === Relay & flags === */
volatile bool relayState = false;
volatile bool safetyTripped = false;

/* === Edge Impulse buffer configuration === */
#define EI_NUM_CHANNELS 2
#define EI_WINDOW_SAMPLES SAMPLES_PER_WINDOW // 400

static float ei_buffer[EI_WINDOW_SAMPLES * EI_NUM_CHANNELS];
static size_t ei_buf_ix = 0;
static float *g_ei_buffer_ptr = ei_buffer;
static size_t g_ei_total_length = EI_WINDOW_SAMPLES * EI_NUM_CHANNELS;

/* EI result globals */
float g_last_anomaly_score = 0.0f;
bool g_last_anomaly_flag = false;
const float ANOMALY_THRESHOLD = 0.65f;
int anomaly_count = 0;
const int ANOMALY_REQUIRED = 2;

/* Forward declarations */
void connectWiFi();
void connectMQTT();
void publishTelemetry(float Vrms, float Irms, float Pavg, float PF, double Wh, float temp, float hum, bool anomaly, float anomaly_score);
void measureWindow_and_feed_ei(float &Vrms_out, float &Irms_out, float &Pavg_out);
void mqttCallback(char* topic, byte* payload, unsigned int length);
void setRelay(bool on);

/* === Edge Impulse C-style get_data callback ===
   run_classifier expects a signal_t where get_data is a C function:
     int get_data(size_t offset, size_t length, float *out_ptr)
   We provide ei_get_data that reads from global g_ei_buffer_ptr.

   IMPORTANT: If your EI model expects channel-major layout (all channel0 samples then channel1)
   you will need to change the re-ordering logic in ei_get_data accordingly.
*/
int ei_get_data(size_t offset, size_t length, float *out_ptr) {
  // Protect bounds
  if (offset >= g_ei_total_length) return -1;
  if (offset + length > g_ei_total_length) {
    length = g_ei_total_length - offset;
  }

  // Copy contiguous block from global buffer (assumes the classifier expects the
  // same sample ordering that we used: interleaved [v0,i0,v1,i1,...]).
  // If your model expects channel-major, convert here.
  for (size_t i = 0; i < length; i++) {
    out_ptr[i] = g_ei_buffer_ptr[offset + i];
  }
  return 0;
}

/* === setup === */
void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);
  relayState = false;

  dht.begin();
  Wire.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("SaveUp v1 Initializing");
  delay(600);
  lcd.clear();

  analogReadResolution(12); // RP2040 ADC 12-bit

  connectWiFi();
  mqttClient.setServer(THINGSBOARD_SERVER, THINGSBOARD_PORT);
  mqttClient.setCallback(mqttCallback);

  Serial.println("=== SaveUp v1 + EI Ready ===");
  Serial.print("Vbias="); Serial.print(Vbias,6);
  Serial.print(" Ibias="); Serial.print(Ibias,6);
  Serial.print(" Vscale="); Serial.print(VOLTAGE_SCALE,6);
  Serial.print(" Iscale="); Serial.println(CURRENT_SCALE,6);
}

/* === main loop === */
void loop() {
  if (WiFi.status() != WL_CONNECTED) connectWiFi();
  if (!mqttClient.connected()) connectMQTT();
  mqttClient.loop();

  float Vrms, Irms, Pavg;
  measureWindow_and_feed_ei(Vrms, Irms, Pavg);

  float Vrms_real = Vrms * VOLTAGE_SCALE;
  float Irms_real = Irms * CURRENT_SCALE;
  float Pavg_real = Pavg * (VOLTAGE_SCALE * CURRENT_SCALE);
  float Apparent = Vrms_real * Irms_real;
  float PF = (Apparent > 1e-6f) ? (Pavg_real / Apparent) : 0.0f;

  float windowSeconds = (float)SAMPLES_PER_WINDOW / (float)SAMPLE_FREQ;
  energy_Wh += (Pavg_real * windowSeconds) / 3600.0f;

  float temp = dht.readTemperature();
  float hum = dht.readHumidity();
  if (isnan(temp)) temp = -127.0f;
  if (isnan(hum)) hum = -127.0f;

  if (Irms_real > OVERCURRENT_TRIP_A) {
    safetyTripped = true;
    setRelay(false);
    Serial.println("SAFETY TRIP: Overcurrent, relay opened");
  }

  // LCD
  lcd.setCursor(0,0);
  lcd.print("V:");
  lcd.print(Vrms_real,1);
  lcd.print(" I:");
  lcd.print(Irms_real,2);
  lcd.setCursor(0,1);
  lcd.print("P:");
  lcd.print(Pavg_real,1);
  lcd.print("W ");
  if (safetyTripped) {
    lcd.setCursor(12,1); lcd.print("TRIP");
  } else {
    lcd.setCursor(12,1); lcd.print(relayState ? "ON " : "OFF");
  }

  unsigned long now = millis();
  if (now - lastTelemetryMs >= TELEMETRY_INTERVAL_MS) {
    publishTelemetry(Vrms_real, Irms_real, Pavg_real, PF, energy_Wh, temp, hum, g_last_anomaly_flag, g_last_anomaly_score);
    lastTelemetryMs = now;
  }

  delay(1);
}

/* === measurement + EI feeding === */
void measureWindow_and_feed_ei(float &Vrms_out, float &Irms_out, float &Pavg_out) {
  double sum_v2 = 0.0;
  double sum_i2 = 0.0;
  double sum_p  = 0.0;
  unsigned long nextMicros = micros();

  for (unsigned int n = 0; n < SAMPLES_PER_WINDOW; ++n) {
    while ((long)(micros() - nextMicros) < 0) { /* busy wait */ }

    uint16_t rawV = analogRead(ADC_PIN_VOLT);
    uint16_t rawI = analogRead(ADC_PIN_CURR);
    float vADC = (float)rawV * ADC_TO_V;
    float iADC = (float)rawI * ADC_TO_V;
    float v_inst = vADC - Vbias;
    float i_inst_volts = iADC - Ibias;
    float i_inst = i_inst_volts / ACS_SENS;

    sum_v2 += (double)v_inst * (double)v_inst;
    sum_i2 += (double)i_inst * (double)i_inst;
    sum_p  += (double)v_inst * (double)i_inst;

    // Push into interleaved EI buffer: [v0,i0,v1,i1,...]
    size_t pos = ei_buf_ix;
    if (pos + 1 < EI_WINDOW_SAMPLES * EI_NUM_CHANNELS) {
      ei_buffer[pos]     = v_inst;
      ei_buffer[pos + 1] = i_inst;
      ei_buf_ix += 2;
    } else {
      // fill remaining (shouldn't usually hit this branch due to bounds)
      if (pos < EI_WINDOW_SAMPLES * EI_NUM_CHANNELS) ei_buffer[pos] = v_inst;
      if (pos + 1 < EI_WINDOW_SAMPLES * EI_NUM_CHANNELS) ei_buffer[pos+1] = i_inst;
      ei_buf_ix = EI_WINDOW_SAMPLES * EI_NUM_CHANNELS;
    }

    nextMicros += SAMPLE_PERIOD_US;
  } // sample loop

  Vrms_out = (float)sqrt(sum_v2 / (double)SAMPLES_PER_WINDOW);
  Irms_out = (float)sqrt(sum_i2 / (double)SAMPLES_PER_WINDOW);
  Pavg_out = (float)(sum_p / (double)SAMPLES_PER_WINDOW);

  // If buffer full, create signal_t and run EI classifier
  if (ei_buf_ix >= (EI_WINDOW_SAMPLES * EI_NUM_CHANNELS)) {
    signal_t signal;
    // Total length: prefer raw-samples macro if present
    #if defined(EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME)
      signal.total_length = EI_CLASSIFIER_RAW_SAMPLES_PER_FRAME;
    #elif defined(EI_CLASSIFIER_RAW_SAMPLE_COUNT)
      signal.total_length = EI_CLASSIFIER_RAW_SAMPLE_COUNT;
    #else
      // fallback: use DSP input frame size (may represent features; if run_classifier fails, change this)
      signal.total_length = EI_CLASSIFIER_DSP_INPUT_FRAME_SIZE;
    #endif

    // point to our buffer & length
    g_ei_buffer_ptr = ei_buffer;
    g_ei_total_length = EI_WINDOW_SAMPLES * EI_NUM_CHANNELS;

    // assign C-style function pointer
    signal.get_data = &ei_get_data;

    // run classifier
    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR err = run_classifier(&signal, &result, false);
    if (err != EI_IMPULSE_OK) {
      Serial.print("EI run_classifier error: "); Serial.println(err);
    } else {
      float score = 0.0f;
      #if EI_CLASSIFIER_HAS_ANOMALY
        score = result.anomaly;
      #else
        score = 0.0f;
      #endif

      g_last_anomaly_score = score;
      bool is_anom = (score >= ANOMALY_THRESHOLD);
      if (is_anom) anomaly_count++;
      else anomaly_count = 0;

      if (anomaly_count >= ANOMALY_REQUIRED) g_last_anomaly_flag = true;
      else g_last_anomaly_flag = false;

      Serial.print("EI anomaly score: "); Serial.print(score, 4);
      Serial.print("  flag: "); Serial.println(g_last_anomaly_flag ? "YES":"no");
      // Optionally: take immediate action if severe anomaly (not done here by default)
    }

    // keep 50% overlap: move second half to start
    int half_samples = (EI_WINDOW_SAMPLES / 2) * EI_NUM_CHANNELS;
    memmove(ei_buffer, ei_buffer + half_samples, sizeof(float) * half_samples);
    ei_buf_ix = half_samples;
  }
}

/* === MQTT / connect / callback === */
void connectWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000UL) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WiFi OK. IP: "); Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi failed");
  }
}

void connectMQTT() {
  if (WiFi.status() != WL_CONNECTED) return;
  Serial.print("Connecting MQTT...");
  while (!mqttClient.connected()) {
    if (mqttClient.connect("SaveUpNode", ACCESS_TOKEN, NULL)) {
      Serial.println("connected");
      mqttClient.subscribe("v1/devices/me/rpc/request/+");
      break;
    } else {
      Serial.print(".");
      delay(1000);
    }
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // payload is not null-terminated; cast carefully
  StaticJsonDocument<256> doc;
  DeserializationError err = deserializeJson(doc, (const char*)payload, length);
  if (err) {
    Serial.print("RPC JSON parse err: "); Serial.println(err.c_str());
    return;
  }
  const char* method = doc["method"];
  if (!method) return;

  if (strcmp(method, "setRelay") == 0) {
    bool state = false;
    JsonVariant p = doc["params"];
    if (p.is<bool>()) state = p.as<bool>();
    else if (p.is<int>()) state = (p.as<int>() != 0);
    else if (p.is<const char*>()) state = (strcmp(p.as<const char*>(), "true") == 0);
    setRelay(state);
    Serial.print("RPC setRelay -> "); Serial.println(state ? "ON":"OFF");
  }
}

void setRelay(bool on) {
  if (safetyTripped && on) {
    Serial.println("Blocked: safety trip active");
    return;
  }
  relayState = on;
  digitalWrite(RELAY_PIN, on ? HIGH : LOW);
}

/* === Telemetry publish === */
void publishTelemetry(float Vrms, float Irms, float Pavg, float PF, double Wh, float temp, float hum, bool anomaly, float anomaly_score) {
  char payload[512];
  int len = snprintf(payload, sizeof(payload),
                     "{\"Vrms\":%.2f,\"Irms\":%.3f,\"Pavg\":%.2f,\"PF\":%.3f,\"Wh\":%.3f,\"Temp\":%.2f,\"Hum\":%.2f,\"anomaly\":%s,\"anomaly_score\":%.3f,\"relay\":%s}",
                     Vrms, Irms, Pavg, PF, Wh, temp, hum,
                     anomaly ? "true" : "false",
                     anomaly_score,
                     relayState ? "true" : "false");
  if (len <= 0) { Serial.println("Payload error"); return; }
  if (!mqttClient.connected()) { Serial.println("MQTT not connected - skip publish"); return; }
  bool ok = mqttClient.publish("v1/devices/me/telemetry", payload);
  if (!ok) Serial.println("Publish failed");
  else {
    Serial.print("Published: "); Serial.println(payload);
  }
}
