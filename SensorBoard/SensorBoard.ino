#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <DHT.h>

// ===== OLED =====
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ===== SENSOR =====
#define DHTPIN 2
#define DHTTYPE DHT11
#define SOIL_PIN A0

DHT dht(DHTPIN, DHTTYPE);
bool oledAvailable = false;

void setup() {
  Serial.begin(9600);

  dht.begin();

  // ===== OLED INIT =====
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    oledAvailable = true;
    display.clearDisplay();
  } else {
    oledAvailable = false;
  }
}

void loop() {
  int soil = analogRead(SOIL_PIN);
  int temp = dht.readTemperature();
  int hum  = dht.readHumidity();

  if (temp < 0 || temp > 50) temp = 0;
  if (hum < 20 || hum > 100) hum = 0;

  // ===== HIỂN THỊ OLED =====
  if (oledAvailable) {
    display.clearDisplay();

    display.setTextSize(1);
    display.setTextColor(WHITE);

    display.setCursor(0, 0);
    display.print("Temp: ");
    display.print(temp);
    display.println(" C");

    display.setCursor(0, 16);
    display.print("Hum: ");
    display.print(hum);
    display.println(" %");

    display.setCursor(0, 32);
    display.print("Soil: ");
    display.println(soil);

    display.display();
  }

  // ===== GỬI UART =====
  Serial.print("T:");
  Serial.print(temp);
  Serial.print(",H:");
  Serial.print(hum);
  Serial.print(",S:");
  Serial.println(soil);

  delay(2000);
}