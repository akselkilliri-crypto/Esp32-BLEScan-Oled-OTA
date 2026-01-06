#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
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
WebServer server(80);
BLEScan* pBLEScan;
bool inOtaMode = false;
String devicesList = "";
unsigned long lastScanTime = 0;
const unsigned long scanInterval = 5000; // Сканирование каждые 5 секунд

class MyAdvertisedDeviceCallbacks: public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice advertisedDevice) {
      devicesList += advertisedDevice.getName() + " ";
      if (devicesList.length() > 200) {
        devicesList = devicesList.substring(0, 200) + "...";
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
  display.println("BLE Scanner + OTA");
  display.display();
}

void setupBLE() {
  Serial.println("Запуск BLE сканера...");
  
  BLEDevice::init("ESP32 Scanner");
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
  display.println("--------");
  display.println("IP: " + WiFi.localIP().toString());
  display.println("http://<IP>/");
  display.display();

  server.on("/", HTTP_GET, []() {
    String html = "<h2>ESP32 OTA Updater</h2>";
    html += "<p>Текущая прошивка: BLE Scanner</p>";
    html += "<form method='POST' enctype='multipart/form-data' action='/update'>";
    html += "<input type='file' name='firmware' accept='.bin' required>";
    html += "<input type='submit' value='Прошить'>";
    html += "</form>";
    server.send(200, "text/html", html);
  });

  server.on("/update", HTTP_POST, []() {
    server.send(200, "text/plain", Update.hasError() ? "Ошибка" : "OK! Перезагрузка...");
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Update.begin(UPDATE_SIZE_UNKNOWN);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      Update.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        ESP.restart();
      }
    }
  });

  server.begin();
  inOtaMode = true;
  Serial.println("OTA сервер запущен");
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n=== ESP32 BLE Scanner + OTA ===");
  
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
  
  if (otaButtonPressed) {
    Serial.println("Кнопка OTA нажата! Запуск OTA сервера...");
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0,0);
    display.println("OTA MODE");
    display.println("Нажата кнопка");
    display.display();
    
    // Подключение к Wi-Fi для OTA
    WiFi.begin(ssid, password);
    Serial.print("Подключение к Wi-Fi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
      delay(500);
      Serial.print(".");
      attempts++;
    }
    
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("\nУспешно! IP: " + WiFi.localIP().toString());
      startOTAServer();
    } else {
      Serial.println("\nОшибка подключения к Wi-Fi");
      display.clearDisplay();
      display.setCursor(0,0);
      display.println("Wi-Fi ERROR");
      display.display();
    }
  } else {
    Serial.println("Кнопка OTA не нажата. Запуск BLE сканера...");
    setupBLE();
  }
}

void loop() {
  if (inOtaMode) {
    server.handleClient();
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
      display.println("BTN1: Сканирование");
      display.display();
      devicesList = ""; // Очистка списка для нового сканирования
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
      display.println("BTN2: Устройства:");
      display.println(devicesList);
      display.display();
    }
  }
  
  // Периодическое сканирование BLE
  if (millis() - lastScanTime > scanInterval) {
    lastScanTime = millis();
    Serial.println("Сканирование BLE устройств...");
    devicesList = "";
    BLEScanResults foundDevices = pBLEScan->start(3, false); // 3 секунды сканирования
    pBLEScan->clearResults();
    
    if (display.display()) {
      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(0,0);
      display.println("BLE Устройства:");
      display.println(devicesList.substring(0, 128)); // Ограничение для дисплея
      display.display();
    }
  }
  
  delay(10); // Короткая задержка для стабильности
}
