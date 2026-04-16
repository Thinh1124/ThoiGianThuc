#if defined(ARDUINO_ARCH_AVR)
#include <Arduino_FreeRTOS.h>
#include <SoftwareSerial.h>
#endif

// ===== PIN =====
#define RELAY_PIN 8
#define LED_ERROR 13

#define LINK_RX_PIN 10
#define LINK_TX_PIN 11
#define LINK_BAUD 9600

#if defined(ARDUINO_ARCH_AVR)
SoftwareSerial linkSerial(LINK_RX_PIN, LINK_TX_PIN);
#endif

#define LINK_SERIAL linkSerial

#define DEFAULT_DRY_THRESHOLD 1000
#define DEFAULT_WET_THRESHOLD 800
#define DEFAULT_SAFE_TEMP 32
#define DEFAULT_SAFETY_TIMEOUT 600000UL

// ===== STATE =====
typedef enum {
  IDLE = 0,
  PUMPING,
  OVERHEAT,
  ERROR
} SystemState;

volatile SystemState state = IDLE;

// ===== DATA NHẬN =====
volatile int16_t soilValue = 0;
volatile int8_t temperature = 0;
volatile int8_t humidity = 0;

// ===== CẤU HÌNH ĐỘNG TỪ ESP32 =====
volatile uint16_t dryThreshold = DEFAULT_DRY_THRESHOLD;
volatile uint16_t wetThreshold = DEFAULT_WET_THRESHOLD;
volatile int8_t safeTemp = DEFAULT_SAFE_TEMP;
volatile uint32_t safetyTimeoutMs = DEFAULT_SAFETY_TIMEOUT;
volatile int8_t manualOverride = -1; // -1: auto, 0: force off, 1: force on

volatile uint8_t pumpOn = 0;
volatile uint32_t pumpStart = 0;

// ===== BUFFER UART =====
char buffer[96];
uint8_t idx = 0;

bool parseConfigFrame(const char *line) {
  uint16_t dry;
  uint16_t wet;
  unsigned long timeoutMs;
  int safe;

  if (sscanf(line, "CFG:dry=%hu,wet=%hu,timeout=%lu,safe=%d", &dry, &wet, &timeoutMs, &safe) != 4) {
    return false;
  }

  if (dry > 1023 || wet > 1023 || dry <= wet) return false;
  if (timeoutMs < 1000UL || timeoutMs > 3600000UL) return false;
  if (safe < -20 || safe > 80) return false;

  dryThreshold = dry;
  wetThreshold = wet;
  safetyTimeoutMs = timeoutMs;
  safeTemp = (int8_t)safe;
  return true;
}

bool parseManualFrame(const char *line) {
  int v;
  if (sscanf(line, "M:%d", &v) != 1) {
    return false;
  }

  if (v != -1 && v != 0 && v != 1) {
    return false;
  }

  manualOverride = (int8_t)v;
  return true;
}

// ================= TASK UART =================
void Task_UART(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    while (LINK_SERIAL.available()) {
      char c = LINK_SERIAL.read();

      if (c == '\n') {
        buffer[idx] = '\0';

        int t, h, s;
        if (sscanf(buffer, "T:%d,H:%d,S:%d", &t, &h, &s) == 3) {
          if (t < -20) t = -20;
          if (t > 80) t = 80;
          if (h < 0) h = 0;
          if (h > 100) h = 100;
          if (s < 0) s = 0;
          if (s > 1023) s = 1023;

          temperature = (int8_t)t;
          humidity = (int8_t)h;
          soilValue = (int16_t)s;
        } else if (!parseConfigFrame(buffer)) {
          parseManualFrame(buffer);
        }

        idx = 0;
      } else {
        if (idx < sizeof(buffer) - 1) {
          buffer[idx++] = c;
        } else {
          idx = 0;
        }
      }
    }

    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// ================= TASK CONTROL =================
void Task_Control(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    int8_t manual = manualOverride;
    bool forceOn = (manual == 1);
    bool forceOff = (manual == 0);

    switch (state) {

      case IDLE:
        digitalWrite(RELAY_PIN, LOW);
        pumpOn = 0;

        if (temperature > safeTemp) state = OVERHEAT;
        else if (forceOn) {
          state = PUMPING;
          pumpStart = millis();
        }
        else if (!forceOff && soilValue > dryThreshold) {
          state = PUMPING;
          pumpStart = millis();
        }
        break;

      case PUMPING:
        digitalWrite(RELAY_PIN, HIGH);
        pumpOn = 1;

        if (temperature > safeTemp) state = OVERHEAT;
        else if ((millis() - pumpStart) > safetyTimeoutMs) state = ERROR;
        else if (forceOn) {
        }
        else if (forceOff) state = IDLE;
        else if (soilValue < wetThreshold) state = IDLE;
        break;

      case OVERHEAT:
        digitalWrite(RELAY_PIN, LOW);
        pumpOn = 0;

        if (temperature <= safeTemp) {
          if (forceOn) {
            state = PUMPING;
            pumpStart = millis();
          } else {
            state = IDLE;
          }
        }
        break;

      case ERROR:
        digitalWrite(RELAY_PIN, LOW);
        pumpOn = 0;

        digitalWrite(LED_ERROR, !digitalRead(LED_ERROR));
        break;
    }

    vTaskDelay(500 / portTICK_PERIOD_MS);
  }
}

// ================= TASK FEEDBACK =================
void Task_Feedback(void *pvParameters) {
  (void) pvParameters;

  uint8_t lastPumpState = 255;
  uint32_t lastBeat = 0;

  for (;;) {
    uint8_t currentPump = pumpOn;
    uint32_t now = millis();

    if (currentPump != lastPumpState || (now - lastBeat) >= 1000UL) {
      LINK_SERIAL.print(F("P:"));
      LINK_SERIAL.println(currentPump);

      Serial.print(F("P:"));
      Serial.println(currentPump);

      lastPumpState = currentPump;
      lastBeat = now;
    }

    vTaskDelay(100 / portTICK_PERIOD_MS);
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(9600);
  LINK_SERIAL.begin(LINK_BAUD);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_ERROR, OUTPUT);

  xTaskCreate(Task_UART, "UART", 100, NULL, 2, NULL);
  xTaskCreate(Task_Control, "CTRL", 100, NULL, 2, NULL);
  xTaskCreate(Task_Feedback, "FB", 100, NULL, 1, NULL);

#if defined(ARDUINO_ARCH_AVR)
  vTaskStartScheduler();
#endif
}

void loop() {}