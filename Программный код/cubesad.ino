#include <SPI.h>
#include <nRF24L01.h>
#include <RF24.h>
#include <Servo.h>

// Пины
#define CE_PIN      9
#define CSN_PIN    10
#define SERVO_X_PIN 3
#define SERVO_Y_PIN 5
#define LASER_PIN   7
#define BUTTON_PIN  2

// IDs устройств
const uint16_t ONBOARD_SYSTEM_ID  = 0x5678;
const uint16_t GROUND_STATION_ID  = 0xABCD;

// Радио
RF24 radio(CE_PIN, CSN_PIN);
const byte readingAddress[6] = "00001";
const byte writingAddress[6] = "00002";

// Сервы
Servo servoX;
Servo servoY;

// Калибровка: индекс 0..8 = углы -40°..+40° с шагом 10°
const int ANGLE_TO_US_X[9] = {1920, 1820, 1690, 1550, 1410, 1260, 1140, 1030, 900};
const int ANGLE_TO_US_Y[9] = {2030, 1880, 1760, 1610, 1500, 1350, 1240, 1120, 1000};

// Команды
const uint8_t CMD_INIT = 4;
const uint8_t CMD_AUTO_SCAN = 0;
const uint8_t CMD_MANUAL = 1;
const uint8_t CMD_STOP = 2;
const uint8_t CMD_RESET = 3;

// Режимы
const uint8_t MODE_WAITING = 0;
const uint8_t MODE_INITIALIZING = 1;
const uint8_t MODE_AUTO_SCAN = 2;
const uint8_t MODE_MANUAL = 3;

uint8_t currentMode = MODE_WAITING;
int currentAngleX = 0;
int currentAngleY = 0;

// Лазер и кнопка
bool laserEnabled = true;
bool buttonPressed = false;
unsigned long pressTime = 0;
const unsigned long HOLD_MS = 500;

// Автосканирование
const int SCAN_STEPS = 9;
const int SCAN_TYPES = 4;
const int scanPattern[SCAN_TYPES][SCAN_STEPS][2] = {
  {{0,-40},{0,-30},{0,-20},{0,-10},{0,0},{0,10},{0,20},{0,30},{0,40}},
  {{-40,0},{-30,0},{-20,0},{-10,0},{0,0},{10,0},{20,0},{30,0},{40,0}},
  {{-40,-40},{-30,-30},{-20,-20},{-10,-10},{0,0},{10,10},{20,20},{30,30},{40,40}},
  {{-40,40},{-30,30},{-20,20},{-10,10},{0,0},{10,-10},{20,-20},{30,-30},{40,-40}}
};
int scanType = 0;
int scanStep = 0;
unsigned long lastMove = 0;
const unsigned long MOVE_DELAY = 3000;

struct RadioPacket {
  uint16_t deviceId;
  int8_t angleX;
  int8_t angleY;
  uint8_t command;
  uint8_t status;
};

void setup() {
  Serial.begin(9600);
  
  pinMode(LASER_PIN, OUTPUT);
  digitalWrite(LASER_PIN, HIGH);
  
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  
  servoX.attach(SERVO_X_PIN);
  servoY.attach(SERVO_Y_PIN);
  setServoAngle(0, 0);
  
  if (!radio.begin()) {
    while (1) {
      digitalWrite(LASER_PIN, !digitalRead(LASER_PIN));
      delay(200);
    }
  }
  radio.setPALevel(RF24_PA_MIN);
  radio.openWritingPipe(writingAddress);
  radio.openReadingPipe(1, readingAddress);
  radio.startListening();
  
  // Индикация готовности
  for (int i = 0; i < 3; i++) {
    digitalWrite(LASER_PIN, LOW);
    delay(100);
    digitalWrite(LASER_PIN, HIGH);
    delay(100);
  }
}

void loop() {
  // Управление лазером кнопкой
  if (digitalRead(BUTTON_PIN) == LOW) {
    if (!buttonPressed) {
      pressTime = millis();
      buttonPressed = true;
    } else if (millis() - pressTime >= HOLD_MS) {
      laserEnabled = !laserEnabled;
      digitalWrite(LASER_PIN, laserEnabled ? HIGH : LOW);
      buttonPressed = false;
    }
  } else {
    buttonPressed = false;
  }

  // Приём команд
  while (radio.available()) {
    RadioPacket pkt;
    radio.read(&pkt, sizeof(pkt));
    if (pkt.deviceId == GROUND_STATION_ID) processCommand(pkt);
  }

  // Автосканирование
  if (currentMode == MODE_AUTO_SCAN && millis() - lastMove >= MOVE_DELAY) {
    autoScanStep();
  }
}

void processCommand(RadioPacket pkt) {
  switch (pkt.command) {
    case CMD_INIT:
      currentMode = MODE_INITIALIZING;
      sendStatus(CMD_INIT, MODE_INITIALIZING);
      currentMode = MODE_WAITING;
      break;
      
    case CMD_AUTO_SCAN:
      currentMode = MODE_AUTO_SCAN;
      scanType = 0;
      scanStep = 0;
      sendStatus(CMD_AUTO_SCAN, MODE_AUTO_SCAN);
      break;
      
    case CMD_MANUAL:
      currentMode = MODE_MANUAL;
      moveTo(pkt.angleX, pkt.angleY);
      sendStatus(CMD_MANUAL, MODE_MANUAL);
      break;
      
    case CMD_STOP:
      currentMode = MODE_WAITING;
      sendStatus(CMD_STOP, MODE_WAITING);
      break;
      
    case CMD_RESET:
      currentMode = MODE_WAITING;
      moveTo(0, 0);
      sendStatus(CMD_RESET, MODE_WAITING);
      break;
  }
}


void autoScanStep() {
  if (scanStep >= SCAN_STEPS) {
    scanType++;
    scanStep = 0;
  }
  
  if (scanType >= SCAN_TYPES) {
    currentMode = MODE_WAITING;
    moveTo(0, 0);
    sendStatus(CMD_RESET, MODE_WAITING);
    // Сброс радио для очистки буфера после завершения сканирования
    radio.powerDown();
    delay(10);
    radio.powerUp();
    radio.startListening();
    return;
  }
  
  int x = scanPattern[scanType][scanStep][0];
  int y = scanPattern[scanType][scanStep][1];
  moveTo(x, y);
  sendStatus(CMD_AUTO_SCAN, MODE_AUTO_SCAN);  // Отправка статуса после КАЖДОГО шага
  
  scanStep++;
  lastMove = millis();
}

void moveTo(int x, int y) {
  x = constrain(round(x / 10.0) * 10, -40, 40);
  y = constrain(round(y / 10.0) * 10, -40, 40);
  setServoAngle(x, y);
  currentAngleX = x;
  currentAngleY = y;
}

void setServoAngle(int x, int y) {
  int idxX = map(x, -40, 40, 0, 8);
  int idxY = map(y, -40, 40, 0, 8);
  servoX.writeMicroseconds(ANGLE_TO_US_X[idxX]);
  servoY.writeMicroseconds(ANGLE_TO_US_Y[idxY]);
}

void sendStatus(uint8_t cmd, uint8_t mode) {
  RadioPacket pkt = {
    ONBOARD_SYSTEM_ID,
    (int8_t)currentAngleX,
    (int8_t)currentAngleY,
    cmd,
    mode
  };
  
  radio.stopListening();
  radio.write(&pkt, sizeof(pkt));
  radio.startListening();
}
