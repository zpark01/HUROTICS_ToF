/*
 * Both IMUs on their own, with a packet counter and rate readout.
 *
 * Useful for telling apart "no data arriving" from "data arriving but
 * the sensor is not moving". Move the sensor around while watching.
 *
 * Serial monitor at 115200.
 */

#include "Adafruit_BNO08x_RVC.h"

Adafruit_BNO08x_RVC imu1, imu2;
BNO08x_RVC_Data d1, d2;

bool imu1OK = false, imu2OK = false;

unsigned long pkt1 = 0, pkt2 = 0;         // packets received
unsigned long lastReport = 0;
unsigned long pkt1AtLastReport = 0, pkt2AtLastReport = 0;

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial.println("\n=== IMU test ===");

  Serial1.begin(115200);
  imu1OK = imu1.begin(&Serial1);
  Serial.println(imu1OK ? "[OK] IMU1" : "[WARN] IMU1 failed");

  Serial2.begin(115200);
  imu2OK = imu2.begin(&Serial2);
  Serial.println(imu2OK ? "[OK] IMU2" : "[WARN] IMU2 failed");

  Serial.println("move the sensors while watching");
  Serial.println("----------------------------------------------------------------------");
  Serial.println( time(s) | IMU1(pkts) yaw/pitch/roll   | IMU2(pkts) yaw/pitch/roll);
  Serial.println("----------------------------------------------------------------------");
}

void loop() {
  bool got1 = imu1OK && imu1.read(&d1);
  bool got2 = imu2OK && imu2.read(&d2);
  if (got1) pkt1++;
  if (got2) pkt2++;

  static unsigned long lastPrint = 0;
  unsigned long now = millis();

  // one line per 100 ms
  if (now - lastPrint >= 100) {
    lastPrint = now;
    Serial.print(now / 1000.0, 1);
    Serial.print("s | IMU1(#");
    Serial.print(pkt1);
    Serial.print(") ");
    Serial.print(d1.yaw, 2); Serial.print("/");
    Serial.print(d1.pitch, 2); Serial.print("/");
    Serial.print(d1.roll, 2);
    Serial.print("   | IMU2(#");
    Serial.print(pkt2);
    Serial.print(") ");
    Serial.print(d2.yaw, 2); Serial.print("/");
    Serial.print(d2.pitch, 2); Serial.print("/");
    Serial.println(d2.roll, 2);
  }

  // report the actual arrival rate every 2 s
  if (now - lastReport >= 2000) {
    unsigned long d_pkt1 = pkt1 - pkt1AtLastReport;
    unsigned long d_pkt2 = pkt2 - pkt2AtLastReport;
    float hz1 = d_pkt1 / 2.0;
    float hz2 = d_pkt2 / 2.0;
    Serial.print(">>> rate  IMU1: ");
    Serial.print(hz1, 1);
    Serial.print("Hz   IMU2: ");
    Serial.print(hz2, 1);
    Serial.println("Hz  (0 means nothing arriving)");
    pkt1AtLastReport = pkt1;
    pkt2AtLastReport = pkt2;
    lastReport = now;
  }
}
