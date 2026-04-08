#include <Arduino_FreeRTOS.h>

// ===== PIN =====
#define RELAY_PIN 8
#define LED_ERROR 13

#define DRY_THRESHOLD 900
#define WET_THRESHOLD 600
#define SAFE_TEMP 32
#define SAFETY_TIMEOUT 600000UL

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

volatile uint8_t pumpOn = 0;
volatile uint32_t pumpStart = 0;

// ===== BUFFER UART =====
char buffer[32];
uint8_t idx = 0;

// ================= TASK UART =================
void Task_UART(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    while (Serial.available()) {
      char c = Serial.read();

      if (c == '\n') {
        buffer[idx] = '\0';

        int t, h, s;
        if (sscanf(buffer, "T:%d,H:%d,S:%d", &t, &h, &s) == 3) {
          temperature = t;
          humidity = h;
          soilValue = s;
        }

        idx = 0;
      } else {
        if (idx < sizeof(buffer) - 1) {
          buffer[idx++] = c;
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

    switch (state) {

      case IDLE:
        digitalWrite(RELAY_PIN, LOW);
        pumpOn = 0;

        if (temperature > SAFE_TEMP) state = OVERHEAT;
        else if (soilValue > DRY_THRESHOLD) {
          state = PUMPING;
          pumpStart = millis();
        }
        break;

      case PUMPING:
        digitalWrite(RELAY_PIN, HIGH);
        pumpOn = 1;

        if (temperature > SAFE_TEMP) state = OVERHEAT;
        else if (soilValue < WET_THRESHOLD) state = IDLE;
        else if ((millis() - pumpStart) > SAFETY_TIMEOUT) state = ERROR;
        break;

      case OVERHEAT:
        digitalWrite(RELAY_PIN, LOW);
        pumpOn = 0;

        if (temperature <= SAFE_TEMP) state = IDLE;
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

// ================= TASK MONITOR =================
void Task_Monitor(void *pvParameters) {
  (void) pvParameters;

  for (;;) {
    Serial.print(F("T:"));
    Serial.print(temperature);
    Serial.print(F(" H:"));
    Serial.print(humidity);
    Serial.print(F(" S:"));
    Serial.print(soilValue);
    Serial.print(F(" P:"));
    Serial.println(pumpOn);

    vTaskDelay(3000 / portTICK_PERIOD_MS);
  }
}

// ================= SETUP =================
void setup() {
  Serial.begin(9600);

  pinMode(RELAY_PIN, OUTPUT);
  pinMode(LED_ERROR, OUTPUT);

  xTaskCreate(Task_UART, "UART", 100, NULL, 2, NULL);
  xTaskCreate(Task_Control, "CTRL", 100, NULL, 2, NULL);
  xTaskCreate(Task_Monitor, "MON", 100, NULL, 1, NULL);

  vTaskStartScheduler();
}

void loop() {}