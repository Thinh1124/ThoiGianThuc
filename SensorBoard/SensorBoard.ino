#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <WiFi.h>
#include <HTTPClient.h>

// ===== OLED =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ===== SENSOR =====
#define DHTPIN 2
#define DHTTYPE DHT11
#define SOIL_PIN 34

// ===== UART TO MAIN BOARD =====
#define UART_BAUD 9600

// ===== WIFI =====
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

// ===== REMOTE API SERVER =====
const char* API_BASE_URL = "http://192.168.1.100:3000";
const char* API_PUSH_PATH = "/data";     // ESP32 -> server (POST telemetry)
const char* API_PULL_PATH = "/config";   // server -> ESP32 (GET config)

// ===== TASK PERIOD =====
static const uint32_t SENSOR_PERIOD_MS = 2000UL;
static const uint32_t RUNTIME_PERIOD_MS = 1000UL;
static const uint32_t API_PERIOD_MS = 2000UL;

// ===== DATA STRUCT =====
typedef struct {
  int8_t temperature;
  uint8_t humidity;
  uint16_t soil;
  uint32_t tsMs;
} SensorFrame;

typedef struct {
  uint16_t dry;
  uint16_t wet;
  uint32_t timeoutMs;
  int8_t safeTemp;
} ThresholdConfig;

DHT dht(DHTPIN, DHTTYPE);
bool oledAvailable = false;

QueueHandle_t sensorQueue = NULL;
portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;

volatile SensorFrame latestFrame = {0, 0, 0, 0};
volatile ThresholdConfig config = {900, 600, 600000UL, 32};
volatile uint32_t runtimeSec = 0;
volatile uint8_t pumpState = 0;
volatile uint32_t mainLastSeenMs = 0;

static bool extractJsonInt(const String &body, const char *key, long &out) {
  String token = "\"";
  token += key;
  token += "\"";

  int p = body.indexOf(token);
  if (p < 0) return false;

  p = body.indexOf(':', p);
  if (p < 0) return false;
  p++;

  while (p < body.length() && (body[p] == ' ' || body[p] == '\t')) p++;

  int sign = 1;
  if (p < body.length() && body[p] == '-') {
    sign = -1;
    p++;
  }

  if (p >= body.length() || body[p] < '0' || body[p] > '9') return false;

  long v = 0;
  while (p < body.length() && body[p] >= '0' && body[p] <= '9') {
    v = v * 10 + (body[p] - '0');
    p++;
  }

  out = v * sign;
  return true;
}

static void parseMainFeedbackLine(char *line) {
  char *p = strstr(line, "P:");
  if (p == NULL) return;

  p += 2;
  if (*p == '0' || *p == '1') {
    portENTER_CRITICAL(&dataMux);
    pumpState = (uint8_t)(*p - '0');
    mainLastSeenMs = millis();
    portEXIT_CRITICAL(&dataMux);
  }
}

void sendConfigToMainBoard() {
  ThresholdConfig snap;

  portENTER_CRITICAL(&dataMux);
  snap = config;
  portEXIT_CRITICAL(&dataMux);

  char line[96];
  snprintf(line, sizeof(line), "CFG:dry=%u,wet=%u,timeout=%lu,safe=%d\n",
           snap.dry,
           snap.wet,
           (unsigned long)snap.timeoutMs,
           snap.safeTemp);

  Serial.print(line);
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000UL) {
    delay(200);
  }
}

void reconnectWifiIfNeeded() {
  if (WiFi.status() == WL_CONNECTED) return;

  WiFi.disconnect();
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 5000UL) {
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void pushTelemetryToServer() {
  if (WiFi.status() != WL_CONNECTED) return;

  SensorFrame snap;
  ThresholdConfig cfg;
  uint32_t rt;
  uint8_t pump;
  uint8_t mainOnline = 0;

  portENTER_CRITICAL(&dataMux);
  snap = latestFrame;
  cfg = config;
  rt = runtimeSec;
  pump = pumpState;
  if (mainLastSeenMs > 0 && (millis() - mainLastSeenMs) < 10000UL) {
    mainOnline = 1;
  }
  portEXIT_CRITICAL(&dataMux);

  char url[128];
  snprintf(url, sizeof(url), "%s%s", API_BASE_URL, API_PUSH_PATH);

  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"temp\":%d,\"hum\":%u,\"soil\":%u,\"pump\":%u,\"time\":%lu,\"mainOnline\":%u,\"config\":{\"dry\":%u,\"wet\":%u,\"timeout\":%lu,\"safeTemp\":%d}}",
           snap.temperature,
           snap.humidity,
           snap.soil,
           pump,
           (unsigned long)rt,
           mainOnline,
           cfg.dry,
           cfg.wet,
           (unsigned long)cfg.timeoutMs,
           cfg.safeTemp);

  HTTPClient http;
  http.begin(url);
  http.addHeader("Content-Type", "application/json");
  http.POST((uint8_t*)payload, strlen(payload));
  http.end();
}

void pullConfigFromServer() {
  if (WiFi.status() != WL_CONNECTED) return;

  char url[128];
  snprintf(url, sizeof(url), "%s%s", API_BASE_URL, API_PULL_PATH);

  HTTPClient http;
  http.begin(url);
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return;
  }

  String body = http.getString();
  http.end();

  bool changed = false;
  long v;

  portENTER_CRITICAL(&dataMux);
  if (extractJsonInt(body, "dry", v)) {
    if (v >= 0 && v <= 1023 && config.dry != (uint16_t)v) {
      config.dry = (uint16_t)v;
      changed = true;
    }
  }

  if (extractJsonInt(body, "wet", v)) {
    if (v >= 0 && v <= 1023 && config.wet != (uint16_t)v) {
      config.wet = (uint16_t)v;
      changed = true;
    }
  }

  if (extractJsonInt(body, "timeout", v)) {
    if (v >= 1000 && v <= 3600000 && config.timeoutMs != (uint32_t)v) {
      config.timeoutMs = (uint32_t)v;
      changed = true;
    }
  }

  if (extractJsonInt(body, "safeTemp", v)) {
    if (v >= -20 && v <= 80 && config.safeTemp != (int8_t)v) {
      config.safeTemp = (int8_t)v;
      changed = true;
    }
  }
  portEXIT_CRITICAL(&dataMux);

  if (changed) {
    sendConfigToMainBoard();
  }
}

void TaskSensor(void *pvParameters) {
  (void) pvParameters;

  SensorFrame frame;
  for (;;) {
    int rawSoil = analogRead(SOIL_PIN);
    int rawTemp = dht.readTemperature();
    int rawHum = dht.readHumidity();

    if (rawTemp < -20 || rawTemp > 80) rawTemp = 0;
    if (rawHum < 0 || rawHum > 100) rawHum = 0;
    if (rawSoil < 0) rawSoil = 0;
    if (rawSoil > 1023) rawSoil = 1023;

    frame.temperature = (int8_t)rawTemp;
    frame.humidity = (uint8_t)rawHum;
    frame.soil = (uint16_t)rawSoil;
    frame.tsMs = millis();

    portENTER_CRITICAL(&dataMux);
    latestFrame = frame;
    portEXIT_CRITICAL(&dataMux);

    if (sensorQueue != NULL) {
      xQueueOverwrite(sensorQueue, &frame);
    }

    if (oledAvailable) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setTextColor(WHITE);

      display.setCursor(0, 0);
      display.print("Temp: ");
      display.print(frame.temperature);
      display.println(" C");

      display.setCursor(0, 16);
      display.print("Hum: ");
      display.print(frame.humidity);
      display.println(" %");

      display.setCursor(0, 32);
      display.print("Soil: ");
      display.println(frame.soil);

      display.display();
    }

    vTaskDelay(pdMS_TO_TICKS(SENSOR_PERIOD_MS));
  }
}

void TaskUART(void *pvParameters) {
  (void) pvParameters;

  SensorFrame frame;
  char line[40];
  char rxLine[64];
  uint8_t rxIdx = 0;

  for (;;) {
    if (xQueueReceive(sensorQueue, &frame, pdMS_TO_TICKS(100)) == pdTRUE) {
      snprintf(line, sizeof(line), "T:%d,H:%u,S:%u\n",
               frame.temperature,
               frame.humidity,
               frame.soil);
      Serial.print(line);
    }

    while (Serial.available() > 0) {
      char c = (char)Serial.read();

      if (c == '\n') {
        rxLine[rxIdx] = '\0';
        parseMainFeedbackLine(rxLine);
        rxIdx = 0;
      } else if (c != '\r') {
        if (rxIdx < (sizeof(rxLine) - 1)) {
          rxLine[rxIdx++] = c;
        } else {
          rxIdx = 0;
        }
      }
    }

    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

void TaskApiSync(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    reconnectWifiIfNeeded();
    pullConfigFromServer();
    pushTelemetryToServer();
    vTaskDelay(pdMS_TO_TICKS(API_PERIOD_MS));
  }
}

void TaskRuntime(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    portENTER_CRITICAL(&dataMux);
    runtimeSec++;
    portEXIT_CRITICAL(&dataMux);

    vTaskDelay(pdMS_TO_TICKS(RUNTIME_PERIOD_MS));
  }
}

void setup() {
  Serial.begin(9600);

  dht.begin();

  analogReadResolution(10);

  // ===== OLED INIT =====
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledAvailable = true;
    display.clearDisplay();
  } else {
    oledAvailable = false;
  }

  connectWifi();

  sensorQueue = xQueueCreate(1, sizeof(SensorFrame));

  xTaskCreate(TaskSensor, "Sensor", 3072, NULL, 2, NULL);
  xTaskCreate(TaskUART, "UART", 3072, NULL, 3, NULL);
  xTaskCreate(TaskApiSync, "Api", 6144, NULL, 1, NULL);
  xTaskCreate(TaskRuntime, "Run", 2048, NULL, 1, NULL);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}