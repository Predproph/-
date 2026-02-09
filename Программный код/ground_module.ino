#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Keypad.h>
#include <LiquidCrystal.h>

// ID станции
const uint16_t GROUND_STATION_ID = 0xABCD;

// Пины nRF24L01
#define CE_PIN   A0
#define CSN_PIN  A2

// Пины LCD
#define LCD_PIN_RS  A4
#define LCD_PIN_E   A5
#define LCD_PIN_DB4 9
#define LCD_PIN_DB5 10
#define LCD_PIN_DB6 A3
#define LCD_PIN_DB7 A1

// Клавиатура 4x3
const byte ROWS = 4;
const byte COLS = 3;
char keys[ROWS][COLS] = {
  {'1', '2', '3'},
  {'4', '5', '6'},
  {'7', '8', '9'},
  {'*', '0', '#'}
};
byte rowPins[ROWS] = {5, 6, 7, 8};
byte colPins[COLS] = {2, 3, 4};

// Объекты
RF24 radio(CE_PIN, CSN_PIN);
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);
LiquidCrystal lcd(LCD_PIN_RS, LCD_PIN_E, LCD_PIN_DB4, LCD_PIN_DB5, LCD_PIN_DB6, LCD_PIN_DB7);

// Адреса радио
const byte writingAddress[6] = "00001";
const byte readingAddress[6] = "00002";

// Структура пакета
struct RadioPacket {
  uint16_t deviceId;
  int8_t angleX;
  int8_t angleY;
  uint8_t command;
  uint8_t status;
};

// Команды
const uint8_t CMD_AUTO_SCAN = 0;
const uint8_t CMD_MANUAL = 1;
const uint8_t CMD_STOP = 2;
const uint8_t CMD_RESET = 3;
const uint8_t CMD_INIT = 4;

// Переменные
int inputAngleX = 0;
int inputAngleY = 0;
bool isNegative = false;
bool isEnteringX = true;
bool isManualMode = false;
bool systemInitialized = false;

void setup() {
  Serial.begin(9600);
  
  lcd.begin(16, 2);
  lcd.clear();
  lcd.print("Ground Station");
  lcd.setCursor(0, 1);
  lcd.print("Init...");
  delay(1000);

  if (!radio.begin()) {
    lcd.clear();
    lcd.print("Radio error!");
    Serial.println("Radio error!");
    while (1);
  }

  radio.setPALevel(RF24_PA_MIN);
  radio.openWritingPipe(writingAddress);
  radio.openReadingPipe(1, readingAddress);
  radio.startListening(); 

  lcd.clear();
  lcd.print("Ready");
  lcd.setCursor(0, 1);
  lcd.print("5:Init");
  delay(2000);
}

void loop() {
  // Проверяем входящие сообщения — БЫСТРО
  if (radio.available()) {
    RadioPacket packet;
    radio.read(&packet, sizeof(packet));
    displayStatus(packet);
  }

  // Обработка клавиатуры
  char key = keypad.getKey();
  if (key) {
    handleKeyPress(key);
  }

  delay(20);
}

void checkIncomingMessages() {
  radio.startListening();
  if (radio.available()) {
    RadioPacket packet;
    radio.read(&packet, sizeof(packet));
    displayStatus(packet);
  }
  radio.stopListening();
}

void handleKeyPress(char key) {
  lcd.clear();

  switch (key) {
    // === УПРАВЛЕНИЕ ===
    case '5': // вместо 'A'
      if (!systemInitialized) {
        sendCommand(CMD_INIT, 0, 0);
        lcd.print("Init systems");
        systemInitialized = true;
      } else {
        sendCommand(CMD_AUTO_SCAN, 0, 0);
        lcd.print("Auto scan mode");
        isManualMode = false;
      }
      break;

    case '6': // вместо 'B'
      if (systemInitialized) {
        isManualMode = true;
        isEnteringX = true;
        inputAngleX = 0;
        inputAngleY = 0;
        isNegative = false;
        lcd.print("Manual mode");
        lcd.setCursor(0, 1);
        lcd.print("Enter X angle");
        delay(1500);
        lcd.clear();
        lcd.print("X: 0");
      } else {
        lcd.print("Init first!");
        lcd.setCursor(0, 1);
        lcd.print("Press 5 to init");
        delay(1500);
      }
      break;

    case '7': // вместо 'C'
      if (systemInitialized && isManualMode) {
        sendCommand(CMD_MANUAL, inputAngleX, inputAngleY);
        lcd.print("Sending angles");
        lcd.setCursor(0, 1);
        lcd.print("X:");
        lcd.print(inputAngleX);
        lcd.print(" Y:");
        lcd.print(inputAngleY);
        delay(1500);
      }
      break;

    case '8': // вместо 'D'
      if (systemInitialized) {
        sendCommand(CMD_RESET, 0, 0);
        lcd.print("Reset position");
        isManualMode = false;
        delay(1500);
      } else {
        lcd.print("Init first!");
        delay(1000);
      }
      break;

    // === ВВОД УГЛОВ ===
    case '*':
      isNegative = true;
      updateAngleDisplay();
      break;

    case '#':
      if (systemInitialized && isManualMode) {
        isEnteringX = !isEnteringX;
        isNegative = false;
        lcd.clear();
        if (isEnteringX) {
          lcd.print("Enter X angle");
          lcd.setCursor(0, 1);
          lcd.print("X: ");
          lcd.print(inputAngleX);
        } else {
          lcd.print("Enter Y angle");
          lcd.setCursor(0, 1);
          lcd.print("Y: ");
          lcd.print(inputAngleY);
        }
        delay(1500);
      }
      break;

    // Цифры 0-4 — для ввода углов
    case '0': case '1': case '2': case '3': case '4':
      if (systemInitialized && isManualMode) {
        int angleValue;
        if (key == '0') {
          angleValue = 0; // 0 градусов
        } else {
          int num = key - '0';
          angleValue = num * 10;
          if (isNegative) angleValue = -angleValue;
        }
        angleValue = constrain(angleValue, -40, 40);

        if (isEnteringX) {
          inputAngleX = angleValue;
        } else {
          inputAngleY = angleValue;
        }
        isNegative = false;
        updateAngleDisplay();
      }
      break;

    // Остальные кнопки (9 и т.д.) — игнорируем
    default:
      break;
  }
}


void updateAngleDisplay() {
  lcd.clear();
  if (isEnteringX) {
    lcd.print("X: ");
    lcd.print(inputAngleX);
  } else {
    lcd.print("Y: ");
    lcd.print(inputAngleY);
  }
  lcd.setCursor(0, 1);
  lcd.print("*- #switch 7-send");
}

void sendCommand(uint8_t command, int8_t angleX, int8_t angleY) {
  RadioPacket packet;
  packet.deviceId = GROUND_STATION_ID;
  packet.angleX = constrain(angleX, -40, 40);
  packet.angleY = constrain(angleY, -40, 40);
  packet.command = command;
  packet.status = 0;

  radio.stopListening();
  
  bool result = false;
  for (int i = 0; i < 3; i++) { // до 3 попыток
    result = radio.write(&packet, sizeof(packet));
    if (result) break;
    delay(10);
  }
  
  radio.startListening();

  if (!result) {
    lcd.clear();
    lcd.print("Send failed");
    delay(300); // короткая задержка
  }
}

void displayStatus(RadioPacket packet) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ID: ");
  lcd.print(packet.deviceId, HEX);
  lcd.print(" S:");
  lcd.print(packet.status);

  lcd.setCursor(0, 1);
  lcd.print("X:");
  lcd.print(packet.angleX);
  lcd.print(" Y:");
  lcd.print(packet.angleY);
  lcd.print(" ");

  switch (packet.command) {
    case CMD_INIT:     lcd.print("INIT"); break;
    case CMD_AUTO_SCAN: lcd.print("AUTO"); break;
    case CMD_MANUAL:   lcd.print("MAN"); break;
    case CMD_RESET:    lcd.print("RST"); break;
    case CMD_STOP:     lcd.print("STOP"); break;
    default:           lcd.print("UNK");
  }
  delay(500);
}