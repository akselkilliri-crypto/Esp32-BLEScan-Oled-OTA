#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <Update.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>

// === НАСТРОЙКИ WI-FI (ОБЯЗАТЕЛЬНО ИЗМЕНИТЬ!) ===
const char* ssid = "MyWiFi";        // ← ЗАМЕНИТЕ НА ВАШУ СЕТЬ!
const char* password = "12345678";  // ← ЗАМЕНИТЕ НА ВАШ ПАРОЛЬ!
// ==============================================

// === ПИНЫ ===
#define BUTTON_1_PIN 4   // Первая кнопка (внутренняя подтяжка)
#define BUTTON_2_PIN 5   // Вторая кнопка (внутренняя подтяжка)
#define OTA_BUTTON_PIN 0 // Третья кнопка для OTA (GPIO0 - Boot button)
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C

// === ГЛОБАЛЬНЫЕ ПЕРЕМЕННЫЕ ===
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
AsyncWebServer server(80);
BLEScan* pBLEScan;
bool inOtaMode = false;
char devicesList[256];  // Используем массив char вместо String для экономии памяти
unsigned long lastScanTime = 0;
const unsigned long scanInterval = 5000; // Сканирование каждые 5 секунд
uint8_t deviceCount = 0;

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      if (deviceCount >= 10) return; // Ограничиваем количество устройств
      
      const char* name = advertisedDevice.getName().c_str();
      if (strlen(name) > 0 && strcmp(name, "(null)") != 0) {
        if (strlen(devicesList) > 0 && strlen(devicesList) + strlen(name) + 2 < 255) {
          strcat(devicesList, "\n");
          strcat(devicesList, name);
        } else if (strlen(devicesList) == 0) {
          strcpy(devicesList, name);
        }
        deviceCount++;
      }
    }
};

void setupOLED() {
  Wire.begin(21, 22); // SDA, SCL
  if (!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("OLED не найден!");
    return;
  }
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0,0);
  display.println("BLE Scanner");
  display.display();
}

void setupBLE() {
  Serial.println("Запуск BLE сканера...");
  
  BLEDevice::init("Scanner");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);
}

void startOTAServer() {
  Serial.println("Запуск OTA сервера...");
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("OTA MODE");
  display.println("IP: ");
  display.println(WiFi.localIP().toString());
  display.display();

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    String html = "<h2>OTA Update</h2>";
    html += "<form method='POST' enctype='multipart/form-data' action='/update'>";
    html += "<input type='file' name='firmware' accept='.bin' required>";
    html += "<input type='submit' value='Update'>";
    html += "</form>";
    request->send(200, "text/html", html);
  });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request){
    request->send(200, "text/plain", Update.hasError() ? "Update failed" : "Update success! Rebooting...");
    ESP.restart();
  }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final){
    if(!index){
      Update.begin(UPDATE_SIZE_UNKNOWN);
    }
    if(len){
      Update.write(data, len);
    }
    if(final){
      if(!Update.end(true)){
        Update.printError(Serial);
      }
    }
  });

  server.begin();
  inOtaMode = true;
  Serial.println("OTA сервер запущен");
}

void setupWiFi() {
  Serial.print("Подключение к Wi-Fi");
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nУспешно! IP: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nОшибка подключения к Wi-Fi");
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Wi-Fi ERROR");
    display.display();
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== ESP32 BLE Scanner ===");
  
  // Инициализация кнопок с внутренней подтяжкой
  pinMode(BUTTON_1_PIN, INPUT_PULLUP);
  pinMode(BUTTON_2_PIN, INPUT_PULLUP);
  pinMode(OTA_BUTTON_PIN, INPUT_PULLUP);
  
  // Проверка кнопки OTA при старте (удерживать при включении)
  Serial.println("Проверка кнопки OTA...");
  bool otaButtonPressed = false;
  
  for (int i = 0; i < 10; i++) {
    if (digitalRead(OTA_BUTTON_PIN) == LOW) {
      otaButtonPressed = true;
      break;
    }
    delay(100);
  }

  setupOLED();
  memset(devicesList, 0, sizeof(devicesList)); // Очистка буфера
  
  if (otaButtonPressed) {
    Serial.println("Кнопка OTA нажата! Запуск OTA сервера...");
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);
    display.println("OTA MODE");
    display.println("Hold BTN");
    display.display();
    
    setupWiFi();
    if (WiFi.status() == WL_CONNECTED) {
      startOTAServer();
    }
  } else {
    Serial.println("Кнопка OTA не нажата. Запуск BLE сканера...");
    setupBLE();
  }
}

void loop() {
  if (inOtaMode) {
    return;
  }
  
  // Обработка кнопок
  static bool lastButton1State = HIGH;
  static bool lastButton2State = HIGH;
  
  bool button1State = digitalRead(BUTTON_1_PIN);
  bool button2State = digitalRead(BUTTON_2_PIN);
  
  // Проверка нажатия кнопки 1
  if (lastButton1State != button1State) {
    lastButton1State = button1State;
    if (button1State == LOW) { // Нажатие
      Serial.println("Кнопка 1 нажата");
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0,0);
      display.println("Сканирование...");
      display.display();
      
      // Очистка списка перед сканированием
      memset(devicesList, 0, sizeof(devicesList));
      deviceCount = 0;
      
      pBLEScan->clearResults();
      pBLEScan->start(3); // 3 секунды сканирования
      delay(3100); // Даем время на сканирование
      pBLEScan->stop();
    }
  }
  
  // Проверка нажатия кнопки 2
  if (lastButton2State != button2State) {
    lastButton2State = button2State;
    if (button2State == LOW) { // Нажатие
      Serial.println("Кнопка 2 нажата");
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0,0);
      display.println("Устройства:");
      
      // Отображение списка устройств
      char* p = strtok(devicesList, "\n");
      int line = 10;
      while (p != NULL && line < 54) {
        display.setCursor(0, line);
        display.println(p);
        line += 10;
        p = strtok(NULL, "\n");
      }
      
      display.display();
    }
  }
  
  delay(10);
}
