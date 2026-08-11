/*
 * Live readout of all four sensors. No SD, no buttons - just prints
 * distance and angle so you can confirm the rig works before a run.
 *
 * Same wiring as the main logger. Serial monitor at 115200.
 */

#include <Wire.h>
#include <VL53L4CD.h>
#include "Adafruit_BNO08x_RVC.h"

#define PIN_XSHUT1 11
#define PIN_XSHUT2 12
#define ADDR_TOF1 0x30
#define ADDR_TOF2 0x31

VL53L4CD tof1, tof2;
Adafruit_BNO08x_RVC imu1, imu2;
BNO08x_RVC_Data imu1Data, imu2Data;

uint8_t serial1RxBuf[256];
uint8_t serial2RxBuf[256];

bool tofOK = false, imu1OK = false, imu2OK = false;

float imu1_yaw=0, imu1_pitch=0, imu1_roll=0;
float imu2_yaw=0, imu2_pitch=0, imu2_roll=0;
unsigned long imu1_pkt=0, imu2_pkt=0;   // packets received, to confirm data is still flowing

unsigned long lastPrint = 0;
const unsigned long PRINT_INTERVAL_MS = 150;

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
  if (!tof1.init()) { Serial.println("[ERR] ToF1 init failed"); return false; }
  tof1.setAddress(ADDR_TOF1);
  delay(10);

  pinMode(PIN_XSHUT2, INPUT);
  delay(50);
  tof2.setBus(&Wire);
  tof2.setTimeout(500);
  if (!tof2.init()) { Serial.println("[ERR] ToF2 init failed"); return false; }
  tof2.setAddress(ADDR_TOF2);
  delay(10);

  tof1.startContinuous();
  tof2.startContinuous();
  return true;
}

void drainIMU1() {
  if (!imu1OK) return;
  while (imu1.read(&imu1Data)) {
    imu1_yaw = imu1Data.yaw;
    imu1_pitch = imu1Data.pitch;
    imu1_roll = imu1Data.roll;
    imu1_pkt++;
  }
}
void drainIMU2() {
  if (!imu2OK) return;
  while (imu2.read(&imu2Data)) {
    imu2_yaw = imu2Data.yaw;
    imu2_pitch = imu2Data.pitch;
    imu2_roll = imu2Data.roll;
    imu2_pkt++;
  }
}

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=== sensor monitor ===");

  Serial1.addMemoryForRead(serial1RxBuf, sizeof(serial1RxBuf));
  Serial2.addMemoryForRead(serial2RxBuf, sizeof(serial2RxBuf));

  Wire.begin();
  tofOK = initToF();
  Serial.println(tofOK ? "[OK] both ToF ready" : "[WARN] ToF not ready");

  Serial1.begin(115200);
  imu1OK = imu1.begin(&Serial1);
  Serial.println(imu1OK ? "[OK] IMU1 ready" : "[WARN] IMU1 not ready");

  Serial2.begin(115200);
  imu2OK = imu2.begin(&Serial2);
  Serial.println(imu2OK ? "[OK] IMU2 ready" : "[WARN] IMU2 not ready");

  Serial.println("--------------------------------------------------------------");
  Serial.println("packet counters should climb steadily");
  Serial.println("--------------------------------------------------------------");
}

void loop() {
  uint16_t d1 = 0, d2 = 0;
  uint8_t s1 = 255, s2 = 255;
  if (tofOK) {
    tof1.read();
    if (!tof1.timeoutOccurred()) { d1 = tof1.ranging_data.range_mm; s1 = tof1.ranging_data.range_status; }
    tof2.read();
    if (!tof2.timeoutOccurred()) { d2 = tof2.ranging_data.range_mm; s2 = tof2.ranging_data.range_status; }
  }

  drainIMU1();
  drainIMU2();

  unsigned long now = millis();
  if (now - lastPrint >= PRINT_INTERVAL_MS) {
    lastPrint = now;

    Serial.print("ToF1: ");
    Serial.print(d1); Serial.print("mm(st");Serial.print(s1);Serial.print(")");
    Serial.print(" | ToF2: ");
    Serial.print(d2); Serial.print("mm(st");Serial.print(s2);Serial.print(")");

    Serial.print(" || IMU1[#");
    Serial.print(imu1_pkt);
    Serial.print("] y/p/r: ");
    Serial.print(imu1_yaw,1); Serial.print("/");
    Serial.print(imu1_pitch,1); Serial.print("/");
    Serial.print(imu1_roll,1);

    Serial.print(" | IMU2[#");
    Serial.print(imu2_pkt);
    Serial.print("] y/p/r: ");
    Serial.print(imu2_yaw,1); Serial.print("/");
    Serial.print(imu2_pitch,1); Serial.print("/");
    Serial.println(imu2_roll,1);
  }
}
