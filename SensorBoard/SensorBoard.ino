#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <math.h>
#include <time.h>

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
#define UART2_TX_PIN 17
#define UART2_RX_PIN 16

// ===== WIFI =====
const char* WIFI_SSID = "baitaplon";
const char* WIFI_PASS = "12345678";

// ===== SUPABASE =====
const char* SUPABASE_URL = "https://jelbdikdlcsufgcneuri.supabase.co";
const char* SUPABASE_API_KEY = "sb_publishable_ZPz5RRekgXhLQ2hARpOU1w_9U2nEt8v";

const char* SUPABASE_TELEMETRY_PATH = "/rest/v1/telemetry";
const char* SUPABASE_CONFIG_PATH = "/rest/v1/config?select=dry_threshold,wet_threshold,safe_temp,timeout_ms,pump_mode&id=eq.1&limit=1";
const char* SUPABASE_SCHEDULES_PATH = "/rest/v1/schedules?select=start_time,duration_minutes,days_of_week,enabled&enabled=eq.true&order=start_time.asc";
const char* SUPABASE_CONFIG_PATCH_PATH = "/rest/v1/config?id=eq.1";

// ===== TIME =====
static const long TZ_OFFSET_SEC = 7 * 3600L;
static const int DST_OFFSET_SEC = 0;
const char* NTP_SERVER_1 = "pool.ntp.org";
const char* NTP_SERVER_2 = "time.nist.gov";

// ===== TASK PERIOD =====
static const uint32_t SENSOR_PERIOD_MS = 2000UL;
static const uint32_t API_PERIOD_MS = 2000UL;
static const uint32_t OVERRIDE_HEARTBEAT_MS = 4000UL;

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
  char pumpMode[12];
} ThresholdConfig;

typedef struct {
  uint16_t startMinute;
  uint16_t durationMinutes;
  uint8_t daysMask;
  bool enabled;
} ScheduleEntry;

DHT dht(DHTPIN, DHTTYPE);
bool oledAvailable = false;

QueueHandle_t sensorQueue = NULL;
portMUX_TYPE dataMux = portMUX_INITIALIZER_UNLOCKED;

volatile SensorFrame latestFrame = {0, 0, 0, 0};
volatile ThresholdConfig config = {900, 600, 600000UL, 32, "threshold"};
volatile uint8_t pumpState = 0;
static const uint8_t MAX_SCHEDULES = 20;
volatile ScheduleEntry schedules[MAX_SCHEDULES];
volatile uint8_t scheduleCount = 0;
volatile bool clockSynced = false;

#define MAIN_SERIAL Serial2

static void snapshotConfig(ThresholdConfig &dst) {
  portENTER_CRITICAL(&dataMux);
  memcpy(&dst, (const void *)&config, sizeof(ThresholdConfig));
  portEXIT_CRITICAL(&dataMux);
}

static void snapshotLatestFrame(SensorFrame &dst) {
  portENTER_CRITICAL(&dataMux);
  memcpy(&dst, (const void *)&latestFrame, sizeof(SensorFrame));
  portEXIT_CRITICAL(&dataMux);
}

static void updateLatestFrame(const SensorFrame &src) {
  portENTER_CRITICAL(&dataMux);
  memcpy((void *)&latestFrame, &src, sizeof(SensorFrame));
  portEXIT_CRITICAL(&dataMux);
}

static uint8_t snapshotSchedules(ScheduleEntry *dst, uint8_t maxItems) {
  if (dst == NULL || maxItems == 0) return 0;

  uint8_t count;
  portENTER_CRITICAL(&dataMux);
  count = scheduleCount;
  if (count > maxItems) count = maxItems;
  memcpy(dst, (const void *)schedules, sizeof(ScheduleEntry) * count);
  portEXIT_CRITICAL(&dataMux);
  return count;
}

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

static bool extractJsonText(const String &body, const char *key, char *out, size_t outSize) {
  if (outSize == 0) return false;

  String token = "\"";
  token += key;
  token += "\"";

  int p = body.indexOf(token);
  if (p < 0) return false;

  p = body.indexOf(':', p);
  if (p < 0) return false;
  p++;

  while (p < body.length() && (body[p] == ' ' || body[p] == '\t')) p++;
  if (p >= body.length() || body[p] != '"') return false;
  p++;

  int start = p;
  while (p < body.length() && body[p] != '"') p++;
  if (p <= start) return false;

  String text = body.substring(start, p);
  size_t n = text.length();
  if (n >= outSize) n = outSize - 1;

  memcpy(out, text.c_str(), n);
  out[n] = '\0';
  return true;
}

static bool extractJsonBool(const String &body, const char *key, bool &out) {
  String token = "\"";
  token += key;
  token += "\"";

  int p = body.indexOf(token);
  if (p < 0) return false;

  p = body.indexOf(':', p);
  if (p < 0) return false;
  p++;

  while (p < body.length() && (body[p] == ' ' || body[p] == '\t')) p++;

  if (body.startsWith("true", p)) {
    out = true;
    return true;
  }
  if (body.startsWith("false", p)) {
    out = false;
    return true;
  }
  return false;
}

static bool extractJsonIntArray(const String &body, const char *key, uint8_t *out, size_t maxItems, size_t &outCount) {
  outCount = 0;
  if (out == NULL || maxItems == 0) return false;

  String token = "\"";
  token += key;
  token += "\"";

  int p = body.indexOf(token);
  if (p < 0) return false;

  p = body.indexOf(':', p);
  if (p < 0) return false;
  p++;

  while (p < body.length() && (body[p] == ' ' || body[p] == '\t')) p++;
  if (p >= body.length() || body[p] != '[') return false;
  p++;

  while (p < body.length()) {
    while (p < body.length() && (body[p] == ' ' || body[p] == '\t' || body[p] == ',')) p++;

    if (p >= body.length()) break;
    if (body[p] == ']') {
      return true;
    }

    int sign = 1;
    if (body[p] == '-') {
      sign = -1;
      p++;
    }

    if (p >= body.length() || body[p] < '0' || body[p] > '9') return false;

    long v = 0;
    while (p < body.length() && body[p] >= '0' && body[p] <= '9') {
      v = v * 10 + (body[p] - '0');
      p++;
    }
    v *= sign;

    if (outCount < maxItems) {
      out[outCount++] = (uint8_t)v;
    }

    while (p < body.length() && (body[p] == ' ' || body[p] == '\t')) p++;
    if (p < body.length() && body[p] == ']') {
      return true;
    }
  }

  return false;
}

static bool parseTimeToMinute(const String &value, uint16_t &minuteOfDay) {
  if (value.length() < 5) return false;

  int hh = value.substring(0, 2).toInt();
  int mm = value.substring(3, 5).toInt();
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59) return false;

  minuteOfDay = (uint16_t)(hh * 60 + mm);
  return true;
}

static bool parseScheduleObject(const String &obj, ScheduleEntry &entry) {
  char startBuf[12];
  startBuf[0] = '\0';

  long duration;
  bool enabledVal = true;
  uint8_t days[7];
  size_t dayCount = 0;

  if (!extractJsonText(obj, "start_time", startBuf, sizeof(startBuf))) return false;
  if (!extractJsonInt(obj, "duration_minutes", duration)) return false;
  if (duration < 1 || duration > 1440) return false;

  extractJsonBool(obj, "enabled", enabledVal);
  if (!extractJsonIntArray(obj, "days_of_week", days, 7, dayCount)) return false;

  uint16_t startMinute;
  if (!parseTimeToMinute(String(startBuf), startMinute)) return false;

  uint8_t mask = 0;
  for (size_t i = 0; i < dayCount; i++) {
    uint8_t day = days[i];
    if (day >= 1 && day <= 7) {
      mask |= (uint8_t)(1U << (day - 1));
    }
  }

  if (mask == 0) return false;

  entry.startMinute = startMinute;
  entry.durationMinutes = (uint16_t)duration;
  entry.daysMask = mask;
  entry.enabled = enabledVal;
  return true;
}

static bool syncClockIfNeeded(bool forceConfig) {
  if (WiFi.status() != WL_CONNECTED) return false;

  if (!clockSynced || forceConfig) {
    configTime(TZ_OFFSET_SEC, DST_OFFSET_SEC, NTP_SERVER_1, NTP_SERVER_2);
  }

  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 1000)) {
    clockSynced = true;
    return true;
  }

  return false;
}

static bool isScheduleActiveNow() {
  ScheduleEntry local[MAX_SCHEDULES];
  uint8_t count = snapshotSchedules(local, MAX_SCHEDULES);
  if (count == 0) return false;

  struct tm nowTime;
  if (!getLocalTime(&nowTime, 100)) return false;

  uint8_t day = (nowTime.tm_wday == 0) ? 1 : (uint8_t)(nowTime.tm_wday + 1);
  uint8_t prevDay = (day == 1) ? 7 : (uint8_t)(day - 1);
  uint16_t minuteOfDay = (uint16_t)(nowTime.tm_hour * 60 + nowTime.tm_min);

  for (uint8_t i = 0; i < count; i++) {
    const ScheduleEntry &sch = local[i];
    if (!sch.enabled) continue;
    if (sch.durationMinutes == 0) continue;

    uint16_t start = sch.startMinute;
    uint16_t end = (uint16_t)((start + sch.durationMinutes) % 1440);
    bool wraps = (start + sch.durationMinutes) >= 1440;

    if (!wraps) {
      bool dayMatch = (sch.daysMask & (uint8_t)(1U << (day - 1))) != 0;
      if (dayMatch && minuteOfDay >= start && minuteOfDay < end) return true;
    } else {
      if (minuteOfDay >= start) {
        bool dayMatch = (sch.daysMask & (uint8_t)(1U << (day - 1))) != 0;
        if (dayMatch) return true;
      } else if (minuteOfDay < end) {
        bool prevDayMatch = (sch.daysMask & (uint8_t)(1U << (prevDay - 1))) != 0;
        if (prevDayMatch) return true;
      }
    }
  }

  return false;
}

static void parseMainFeedbackLine(char *line) {
  char *p = strstr(line, "P:");
  if (p == NULL) return;

  p += 2;
  if (*p == '0' || *p == '1') {
    portENTER_CRITICAL(&dataMux);
    pumpState = (uint8_t)(*p - '0');
    portEXIT_CRITICAL(&dataMux);
  }
}

void sendConfigToMainBoard() {
  ThresholdConfig snap;
  snapshotConfig(snap);

  char line[96];
  snprintf(line, sizeof(line), "CFG:dry=%u,wet=%u,timeout=%lu,safe=%d\n",
           snap.dry,
           snap.wet,
           (unsigned long)snap.timeoutMs,
           snap.safeTemp);

  MAIN_SERIAL.print(line);
}

void sendSensorToMainBoard(const SensorFrame &frame) {
  char line[40];
  snprintf(line, sizeof(line), "T:%d,H:%u,S:%u\n",
           frame.temperature,
           frame.humidity,
           frame.soil);
  MAIN_SERIAL.print(line);
}

void sendPumpOverrideToMainBoard(int8_t overrideMode) {
  char line[12];
  snprintf(line, sizeof(line), "M:%d\n", overrideMode);
  MAIN_SERIAL.print(line);
}

int8_t calcPumpOverride(const ThresholdConfig &cfg) {
  if (strcmp(cfg.pumpMode, "schedule") == 0) {
    return isScheduleActiveNow() ? 1 : 0;
  }
  return -1;
}

void connectWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && (millis() - start) < 15000UL) {
    delay(200);
  }

  if (WiFi.status() == WL_CONNECTED) {
    syncClockIfNeeded(true);
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

  if (WiFi.status() == WL_CONNECTED) {
    syncClockIfNeeded(false);
  }
}

void pushTelemetryToSupabase() {
  if (WiFi.status() != WL_CONNECTED) return;

  SensorFrame snap;
  uint8_t pump;
  snapshotLatestFrame(snap);

  portENTER_CRITICAL(&dataMux);
  pump = pumpState;
  portEXIT_CRITICAL(&dataMux);

  char url[192];
  snprintf(url, sizeof(url), "%s%s", SUPABASE_URL, SUPABASE_TELEMETRY_PATH);

  char payload[176];
  snprintf(payload, sizeof(payload),
           "{\"temp\":%d,\"hum\":%u,\"soil\":%u,\"pump_state\":%u}",
           snap.temperature,
           snap.humidity,
           snap.soil,
           pump);

  HTTPClient http;
  http.begin(url);
  http.setTimeout(3000);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");
  int code = http.POST((uint8_t*)payload, strlen(payload));
  String errBody = (code < 200 || code >= 300) ? http.getString() : String();
  http.end();

  if (code >= 200 && code < 300) return;

  // Fallback for schema that uses pump_status instead of pump_state
  HTTPClient fallback;
  fallback.begin(url);
  fallback.setTimeout(3000);
  fallback.addHeader("apikey", SUPABASE_API_KEY);
  fallback.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  fallback.addHeader("Content-Type", "application/json");
  fallback.addHeader("Prefer", "return=minimal");

  char fallbackPayload[176];
  snprintf(fallbackPayload, sizeof(fallbackPayload),
           "{\"temp\":%d,\"hum\":%u,\"soil\":%u,\"pump_status\":%u}",
           snap.temperature,
           snap.humidity,
           snap.soil,
           pump);

  int fallbackCode = fallback.POST((uint8_t*)fallbackPayload, strlen(fallbackPayload));
  String fallbackErr = (fallbackCode < 200 || fallbackCode >= 300) ? fallback.getString() : String();
  fallback.end();

  if (fallbackCode < 200 || fallbackCode >= 300) {
    Serial.print("[telemetry] POST failed. code=");
    Serial.print(code);
    Serial.print(" body=");
    Serial.print(errBody);
    Serial.print(" | fallbackCode=");
    Serial.print(fallbackCode);
    Serial.print(" fallbackBody=");
    Serial.println(fallbackErr);
  }
}

void pullSchedulesFromSupabase() {
  if (WiFi.status() != WL_CONNECTED) return;

  char url[256];
  snprintf(url, sizeof(url), "%s%s", SUPABASE_URL, SUPABASE_SCHEDULES_PATH);

  HTTPClient http;
  http.begin(url);
  http.setTimeout(3000);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Accept", "application/json");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return;
  }

  String body = http.getString();
  http.end();

  ScheduleEntry nextList[MAX_SCHEDULES];
  uint8_t nextCount = 0;

  int seek = 0;
  while (seek < body.length() && nextCount < MAX_SCHEDULES) {
    int startObj = body.indexOf('{', seek);
    if (startObj < 0) break;
    int endObj = body.indexOf('}', startObj);
    if (endObj < 0) break;

    String obj = body.substring(startObj, endObj + 1);
    ScheduleEntry entry;
    if (parseScheduleObject(obj, entry)) {
      nextList[nextCount++] = entry;
    }

    seek = endObj + 1;
  }

  bool changed = false;
  ScheduleEntry curList[MAX_SCHEDULES];
  uint8_t curCount = snapshotSchedules(curList, MAX_SCHEDULES);

  if (curCount != nextCount) {
    changed = true;
  } else if (nextCount > 0) {
    changed = (memcmp(curList, nextList, sizeof(ScheduleEntry) * nextCount) != 0);
  }

  if (changed) {
    portENTER_CRITICAL(&dataMux);
    scheduleCount = nextCount;
    if (nextCount > 0) {
      memcpy((void *)schedules, nextList, sizeof(ScheduleEntry) * nextCount);
    }
    portEXIT_CRITICAL(&dataMux);
  }
}

void syncRuntimeConfigToSupabase(const SensorFrame &snap, uint8_t pump) {
  if (WiFi.status() != WL_CONNECTED) return;

  char url[192];
  snprintf(url, sizeof(url), "%s%s", SUPABASE_URL, SUPABASE_CONFIG_PATCH_PATH);

  HTTPClient http;
  http.begin(url);
  http.setTimeout(3000);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Prefer", "return=minimal");

  char payload[256];
  snprintf(payload, sizeof(payload),
           "{\"pump_state\":%u,\"temp\":%d,\"hum\":%u,\"soil\":%u}",
           pump,
           snap.temperature,
           snap.humidity,
           snap.soil);

  int code = http.PATCH((uint8_t*)payload, strlen(payload));
  http.end();

  if (code >= 200 && code < 300) return;

  HTTPClient fallback;
  fallback.begin(url);
  fallback.setTimeout(3000);
  fallback.addHeader("apikey", SUPABASE_API_KEY);
  fallback.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  fallback.addHeader("Content-Type", "application/json");
  fallback.addHeader("Prefer", "return=minimal");

  char fallbackPayload[256];
  snprintf(fallbackPayload, sizeof(fallbackPayload),
           "{\"pump_status\":%u,\"temp\":%d,\"hum\":%u,\"soil\":%u}",
           pump,
           snap.temperature,
           snap.humidity,
           snap.soil);

  int fallbackCode = fallback.PATCH((uint8_t*)fallbackPayload, strlen(fallbackPayload));
  fallback.end();

  if (fallbackCode >= 200 && fallbackCode < 300) return;
}

void pullConfigFromSupabase() {
  if (WiFi.status() != WL_CONNECTED) return;

  char url[256];
  snprintf(url, sizeof(url), "%s%s", SUPABASE_URL, SUPABASE_CONFIG_PATH);

  HTTPClient http;
  http.begin(url);
  http.setTimeout(3000);
  http.addHeader("apikey", SUPABASE_API_KEY);
  http.addHeader("Authorization", String("Bearer ") + SUPABASE_API_KEY);
  http.addHeader("Accept", "application/json");
  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    http.end();
    return;
  }

  String body = http.getString();
  http.end();

  bool changed = false;
  long v;
  char modeBuf[12];
  modeBuf[0] = '\0';
  ThresholdConfig nextCfg;
  snapshotConfig(nextCfg);

  if (extractJsonInt(body, "dry_threshold", v) && v >= 0 && v <= 1023) {
    if (nextCfg.dry != (uint16_t)v) {
      nextCfg.dry = (uint16_t)v;
      changed = true;
    }
  }

  if (extractJsonInt(body, "wet_threshold", v) && v >= 0 && v <= 1023) {
    if (nextCfg.wet != (uint16_t)v) {
      nextCfg.wet = (uint16_t)v;
      changed = true;
    }
  }

  if (extractJsonInt(body, "timeout_ms", v) && v >= 1000 && v <= 3600000) {
    if (nextCfg.timeoutMs != (uint32_t)v) {
      nextCfg.timeoutMs = (uint32_t)v;
      changed = true;
    }
  }

  if (extractJsonInt(body, "safe_temp", v) && v >= -20 && v <= 80) {
    if (nextCfg.safeTemp != (int8_t)v) {
      nextCfg.safeTemp = (int8_t)v;
      changed = true;
    }
  }

  if (extractJsonText(body, "pump_mode", modeBuf, sizeof(modeBuf))) {
    if (strcmp(modeBuf, "threshold") == 0 || strcmp(modeBuf, "schedule") == 0) {
      if (strcmp(nextCfg.pumpMode, modeBuf) != 0) {
        strncpy(nextCfg.pumpMode, modeBuf, sizeof(nextCfg.pumpMode) - 1);
        nextCfg.pumpMode[sizeof(nextCfg.pumpMode) - 1] = '\0';
        changed = true;
      }
    }
  }

  if (changed) {
    portENTER_CRITICAL(&dataMux);
    memcpy((void *)&config, &nextCfg, sizeof(ThresholdConfig));
    portEXIT_CRITICAL(&dataMux);
  }

  if (changed) {
    sendConfigToMainBoard();
  }
}

void TaskSensor(void *pvParameters) {
  (void) pvParameters;

  SensorFrame frame;
  for (;;) {
    int rawSoil = analogRead(SOIL_PIN);
    float tempRead = dht.readTemperature();
    float humRead = dht.readHumidity();
    int rawTemp = isnan(tempRead) ? 0 : (int)tempRead;
    int rawHum = isnan(humRead) ? 0 : (int)humRead;

    if (rawTemp < -20 || rawTemp > 80) rawTemp = 0;
    if (rawHum < 0 || rawHum > 100) rawHum = 0;
    if (rawSoil < 0) rawSoil = 0;
    if (rawSoil > 1023) rawSoil = 1023;

    frame.temperature = (int8_t)rawTemp;
    frame.humidity = (uint8_t)rawHum;
    frame.soil = (uint16_t)rawSoil;
    frame.tsMs = millis();

    updateLatestFrame(frame);

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
  char rxLine[64];
  uint8_t rxIdx = 0;

  for (;;) {
    if (xQueueReceive(sensorQueue, &frame, pdMS_TO_TICKS(100)) == pdTRUE) {
      sendSensorToMainBoard(frame);
    }

    while (MAIN_SERIAL.available() > 0) {
      char c = (char)MAIN_SERIAL.read();

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

  int8_t lastOverrideSent = 2;
  uint32_t lastOverrideSentAt = 0;

  for (;;) {
    reconnectWifiIfNeeded();
    syncClockIfNeeded(false);
    pullConfigFromSupabase();
    pullSchedulesFromSupabase();

    ThresholdConfig snap;
    SensorFrame frame;
    uint8_t pump;
    snapshotConfig(snap);
    snapshotLatestFrame(frame);
    portENTER_CRITICAL(&dataMux);
    pump = pumpState;
    portEXIT_CRITICAL(&dataMux);

    int8_t overrideMode = calcPumpOverride(snap);
    uint32_t now = millis();
    bool shouldResend = (now - lastOverrideSentAt) >= OVERRIDE_HEARTBEAT_MS;
    if (overrideMode != lastOverrideSent || shouldResend) {
      sendPumpOverrideToMainBoard(overrideMode);
      lastOverrideSent = overrideMode;
      lastOverrideSentAt = now;
    }

    pushTelemetryToSupabase();
    syncRuntimeConfigToSupabase(frame, pump);

    vTaskDelay(pdMS_TO_TICKS(API_PERIOD_MS));
  }
}

void setup() {
  Serial.begin(UART_BAUD);
  MAIN_SERIAL.begin(UART_BAUD, SERIAL_8N1, UART2_RX_PIN, UART2_TX_PIN);

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
  sendConfigToMainBoard();

  ThresholdConfig cfgSnap;
  snapshotConfig(cfgSnap);
  sendPumpOverrideToMainBoard(calcPumpOverride(cfgSnap));

  sensorQueue = xQueueCreate(1, sizeof(SensorFrame));

  xTaskCreate(TaskSensor, "Sensor", 3072, NULL, 2, NULL);
  xTaskCreate(TaskUART, "UART", 3072, NULL, 3, NULL);
  xTaskCreate(TaskApiSync, "Api", 8192, NULL, 1, NULL);
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}