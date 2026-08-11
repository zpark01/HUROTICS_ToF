/*
 * Gait data logger - Teensy 4.1
 *
 * Two VL53L4CD ToF sensors on I2C, two BNO085 IMUs on hardware serial,
 * six labelling buttons, status LED, logging to the onboard SD slot.
 *
 * Wiring
 *   ToF #1   VIN 3.3V, GND, SDA 18, SCL 19, XSHUT 11
 *   ToF #2   VIN 3.3V, GND, SDA 18, SCL 19, XSHUT 12
 *   IMU #1   VIN 3.3V, GND, SDA -> pin 0 (RX1), P0 -> 3.3V
 *   IMU #2   VIN 3.3V, GND, SDA -> pin 7 (RX2), P0 -> 3.3V
 *   LED      DI 6
 *   Buttons  pins 2, 3, 4, 5, 9, 10, other side to GND
 *   SD       onboard slot, FAT32
 *
 * The IMUs run in UART-RVC mode. Pin is labelled SDA on the breakout but
 * it is the sensor's transmit line, so it goes to an RX pin. P0 must be
 * high or the sensor stays in I2C mode and never sends anything. Leave
 * P1 and SCL unconnected.
 *
 * Teensy pins are not 5V tolerant. Everything except the LED runs at 3.3V.
 *
 * Buttons
 *   1  (pin 2)   toggles START / STOP
 *   2  (pin 3)   standing
 *   3  (pin 4)   level_walk        3+4 -> stair_down,  3+5 -> ramp_down
 *   4  (pin 5)   stair_up
 *   5  (pin 9)   ramp_up
 *   6  (pin 10)  toggles sit_down / stand_up
 *
 * CSV columns
 *   elapsed_ms, label,
 *   dist1_mm, status1, signal1_kcps, ambient1_kcps, sigma1_mm,
 *   dist2_mm, status2, signal2_kcps, ambient2_kcps, sigma2_mm,
 *   yaw1, pitch1, roll1, yaw2, pitch2, roll2
 *
 * Libraries: VL53L4CD (Pololu), Adafruit BNO08x RVC, Adafruit NeoPixel
 * Board: Teensy 4.1
 */

#include <Wire.h>
#include <VL53L4CD.h>
#include "Adafruit_BNO08x_RVC.h"
#include <SD.h>
#include <Adafruit_NeoPixel.h>

// ---- pins ----
#define PIN_XSHUT1 11
#define PIN_XSHUT2 12
#define PIN_LED    6
const uint8_t BTN_PINS[6] = {2, 3, 4, 5, 9, 10};

#define ADDR_TOF1 0x30
#define ADDR_TOF2 0x31

const uint16_t SAMPLE_INTERVAL_MS = 20;   // aiming for 50 Hz
const unsigned long DEBOUNCE_MS   = 40;
const unsigned long COMBO_WINDOW_MS = 150;
const int TIMEOUT_LIMIT = 5;
const unsigned long REINIT_INTERVAL = 500;
const int FLUSH_EVERY = 50;

VL53L4CD tof1, tof2;
Adafruit_BNO08x_RVC imu1, imu2;
BNO08x_RVC_Data imu1Data, imu2Data;
Adafruit_NeoPixel led(1, PIN_LED, NEO_GRB + NEO_KHZ800);

// Bigger UART buffers. The ToF reads block for a few ms and the IMUs
// keep streaming through it, so the default buffer overruns.
uint8_t serial1RxBuf[256];
uint8_t serial2RxBuf[256];

bool logging = false;
unsigned long sessionStart = 0;
unsigned long lastSampleMs = 0;
String currentLabel = "none";

bool sensorOK = false;          // ToF healthy?
bool imu1OK = false, imu2OK = false;
int  consecutiveTimeouts = 0;
unsigned long lastReinitTry = 0;

float imu1_yaw=0, imu1_pitch=0, imu1_roll=0;
float imu2_yaw=0, imu2_pitch=0, imu2_roll=0;

int btn1Count = 0;
int btn6Count = 0;

struct BtnState {
  bool lastReading, stableState;
  unsigned long lastChange;
  bool justPressed;
};
BtnState btns[6];

bool comboPending = false;
unsigned long comboStart = 0;
bool p3=false, p4=false, p5=false;

File logFile;
String logFileName = "";
int sampleCountSinceFlush = 0;

// LED state: 0 boot, 1 idle, 2 logging, 3 sensor dropout, 4 fatal
int ledState = 0;
unsigned long ledBlinkTimer = 0;
bool ledBlinkOn = true;
unsigned long labelFlashUntil = 0;

void setLEDColor(uint8_t r, uint8_t g, uint8_t b) {
  led.setPixelColor(0, led.Color(r, g, b));
  led.show();
}
void updateLED() {
  unsigned long now = millis();
  if (labelFlashUntil > now) { setLEDColor(120, 0, 160); return; }
  bool anyMissing = (!sensorOK || !imu1OK || !imu2OK);
  switch (ledState) {
    case 0: setLEDColor(80, 80, 80); break;
    case 4: setLEDColor(150, 0, 0);  break;
    case 1:
      // Cyan instead of blue when a sensor is missing, so you notice before
// starting a run rather than after.
      if (anyMissing) setLEDColor(0, 100, 100);
      else            setLEDColor(0, 0, 100);
      break;
    case 2: setLEDColor(0, 100, 0);  break;
    case 3:
      if (now - ledBlinkTimer > 250) { ledBlinkTimer = now; ledBlinkOn = !ledBlinkOn; }
      setLEDColor(ledBlinkOn ? 150 : 0, ledBlinkOn ? 80 : 0, 0);
      break;
  }
}

// ---- ToF init: bring them up one at a time and reassign addresses ----
bool initToF() {
  pinMode(PIN_XSHUT1, OUTPUT);
  pinMode(PIN_XSHUT2, OUTPUT);
  digitalWrite(PIN_XSHUT1, LOW);
  digitalWrite(PIN_XSHUT2, LOW);
  delay(50);

  pinMode(PIN_XSHUT1, INPUT);
  delay(50);
  tof1.setBus(&Wire);
  tof1.setTimeout(500);
  if (!tof1.init()) { Serial.println("ERR,ToF1 init failed"); return false; }
  tof1.setAddress(ADDR_TOF1);
  delay(10);

  pinMode(PIN_XSHUT2, INPUT);
  delay(50);
  tof2.setBus(&Wire);
  tof2.setTimeout(500);
  if (!tof2.init()) { Serial.println("ERR,ToF2 init failed"); return false; }
  tof2.setAddress(ADDR_TOF2);
  delay(10);

  tof1.startContinuous();
  tof2.startContinuous();
  return true;
}

// ---- IMU: drain everything queued so we keep the freshest reading ----
void drainIMU1() {
  if (!imu1OK) return;
  while (imu1.read(&imu1Data)) {
    imu1_yaw = imu1Data.yaw;
    imu1_pitch = imu1Data.pitch;
    imu1_roll = imu1Data.roll;
  }
}
void drainIMU2() {
  if (!imu2OK) return;
  while (imu2.read(&imu2Data)) {
    imu2_yaw = imu2Data.yaw;
    imu2_pitch = imu2Data.pitch;
    imu2_roll = imu2Data.roll;
  }
}

// ---- next free filename ----
String makeFileName() {
  int idx = 0;
  char name[24];
  do { idx++; sprintf(name, "/LOG_%03d.csv", idx); } while (SD.exists(name) && idx < 999);
  return String(name);
}

// ---- labels and session control ----
void setLabel(const char* lbl) {
  currentLabel = lbl;
  Serial.print("LABEL,");
  Serial.println(currentLabel);
  labelFlashUntil = millis() + 150;
}
void startSession() {
  logFileName = makeFileName();
  logFile = SD.open(logFileName.c_str(), FILE_WRITE);
  if (!logFile) { Serial.println("ERR,could not create file"); return; }
  logFile.println("elapsed_ms,label,dist1_mm,status1,signal1_kcps,ambient1_kcps,sigma1_mm,dist2_mm,status2,signal2_kcps,ambient2_kcps,sigma2_mm,yaw1,pitch1,roll1,yaw2,pitch2,roll2");
  logFile.flush();
  sessionStart = millis();
  lastSampleMs = 0;
  currentLabel = "none";
  sampleCountSinceFlush = 0;
  logging = true;
  ledState = 2;
  Serial.print("START,");
  Serial.println(logFileName);
}
void stopSession() {
  if (!logging) return;
  logging = false;
  logFile.flush();
  logFile.close();
  ledState = 1;
  Serial.print("STOP,saved:");
  Serial.println(logFileName);
}
void toggleSession() {
  btn1Count++;
  if (btn1Count % 2 == 1) { if (!logging) startSession(); }
  else                    { if (logging) stopSession(); }
}
void toggleSitStand() {
  btn6Count++;
  if (btn6Count % 2 == 1) setLabel("sit_down");
  else                    setLabel("stand_up");
}

// ---- buttons ----
void scanButtons() {
  unsigned long now = millis();
  for (int i = 0; i < 6; i++) {
    btns[i].justPressed = false;
    bool reading = (digitalRead(BTN_PINS[i]) == LOW);
    if (reading != btns[i].lastReading) { btns[i].lastChange = now; btns[i].lastReading = reading; }
    if ((now - btns[i].lastChange) > DEBOUNCE_MS) {
      if (reading != btns[i].stableState) {
        btns[i].stableState = reading;
        if (reading) btns[i].justPressed = true;
      }
    }
  }
}
void handleButtons() {
  unsigned long now = millis();
  if (btns[0].justPressed) toggleSession();
  if (btns[1].justPressed) setLabel("standing");
  if (btns[5].justPressed) toggleSitStand();

  if (!comboPending) {
    if (btns[2].justPressed || btns[3].justPressed || btns[4].justPressed) {
      comboPending = true; comboStart = now;
      p3 = btns[2].justPressed; p4 = btns[3].justPressed; p5 = btns[4].justPressed;
    }
  } else {
    if (btns[2].justPressed) p3 = true;
    if (btns[3].justPressed) p4 = true;
    if (btns[4].justPressed) p5 = true;
  }
  if (comboPending && (now - comboStart) >= COMBO_WINDOW_MS) {
    if (p3 && p4)       setLabel("stair_down");
    else if (p3 && p5)  setLabel("ramp_down");
    else if (p3)        setLabel("level_walk");
    else if (p4)        setLabel("stair_up");
    else if (p5)        setLabel("ramp_up");
    comboPending = false; p3 = p4 = p5 = false;
  }
}

// ---- write one row ----
bool logSample() {
  tof1.read(); bool to1 = tof1.timeoutOccurred();
  tof2.read(); bool to2 = tof2.timeoutOccurred();
  if (to1 && to2) return false;

  uint16_t d1=tof1.ranging_data.range_mm; uint8_t s1=tof1.ranging_data.range_status;
  uint16_t g1=tof1.ranging_data.signal_rate_kcps; uint16_t a1=tof1.ranging_data.ambient_rate_kcps;
  uint16_t sg1=tof1.ranging_data.sigma_mm;
  uint16_t d2=tof2.ranging_data.range_mm; uint8_t s2=tof2.ranging_data.range_status;
  uint16_t g2=tof2.ranging_data.signal_rate_kcps; uint16_t a2=tof2.ranging_data.ambient_rate_kcps;
  uint16_t sg2=tof2.ranging_data.sigma_mm;
  if (to1) s1=255;
  if (to2) s2=255;

  unsigned long elapsed = millis() - sessionStart;
  char line[280];
  snprintf(line, sizeof(line),
    "%lu,%s,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%.1f,%.1f,%.1f,%.1f,%.1f,%.1f",
    elapsed, currentLabel.c_str(),
    d1,s1,g1,a1,sg1, d2,s2,g2,a2,sg2,
    imu1_yaw, imu1_pitch, imu1_roll,
    imu2_yaw, imu2_pitch, imu2_roll);

  logFile.println(line);
  sampleCountSinceFlush++;
  if (sampleCountSinceFlush >= FLUSH_EVERY) { logFile.flush(); sampleCountSinceFlush = 0; }
  return true;
}

// ---- setup ----
void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\nBOOT,gait logger starting");

  led.begin();
  led.setBrightness(60);
  ledState = 0;
  updateLED();

  for (int i = 0; i < 6; i++) {
    pinMode(BTN_PINS[i], INPUT_PULLUP);
    btns[i] = {false, false, 0, false};
  }

  // Must come before begin().
  Serial1.addMemoryForRead(serial1RxBuf, sizeof(serial1RxBuf));
  Serial2.addMemoryForRead(serial2RxBuf, sizeof(serial2RxBuf));

  Wire.begin();   

  sensorOK = initToF();
  Serial.println(sensorOK ? "SENSOR_OK,both ToF ready" : "SENSOR_WAIT,ToF not ready, retrying");

  Serial1.begin(115200);
  imu1OK = imu1.begin(&Serial1);
  Serial.println(imu1OK ? "IMU1_OK,IMU1 ready on Serial1" : "IMU1_WARN,IMU1 not responding");

  Serial2.begin(115200);
  imu2OK = imu2.begin(&Serial2);
  Serial.println(imu2OK ? "IMU2_OK,IMU2 ready on Serial2" : "IMU2_WARN,IMU2 not responding");

  if (!SD.begin(BUILTIN_SDCARD)) {
    Serial.println("FATAL,SD mount failed - check card and FAT32 format");
    ledState = 4; updateLED();
    while (1) delay(2000);
  }
  Serial.println("SD_OK,card mounted");

  ledState = 1;
  Serial.println("READY,button 1 starts and stops a run");
}

// ---- loop ----
void loop() {
  scanButtons();
  handleButtons();
  updateLED();

  if (!sensorOK) {
    if (millis() - lastReinitTry >= REINIT_INTERVAL) {
      lastReinitTry = millis();
      if (initToF()) {
        sensorOK = true; consecutiveTimeouts = 0;
        ledState = logging ? 2 : 1;
        Serial.println("RECOVER,ToF back online");
      }
    }
    drainIMU1();   // Keep draining while ToF is down, otherwise the buffer backs up.
    drainIMU2();
    return;
  }

  if (logging) {
    unsigned long nowElapsed = millis() - sessionStart;
    if (nowElapsed - lastSampleMs >= SAMPLE_INTERVAL_MS) {
      lastSampleMs = nowElapsed;
      if (logSample()) {
        consecutiveTimeouts = 0;
        if (ledState == 3) ledState = 2;
      } else {
        consecutiveTimeouts++;
        if (consecutiveTimeouts >= TIMEOUT_LIMIT) {
          sensorOK = false;
          ledState = 3;
          Serial.println("LOST,ToF dropped out, retrying");
        }
      }
    }
  }

  // Right after the blocking ToF read, catch up on whatever queued.
  drainIMU1();
  drainIMU2();
}
