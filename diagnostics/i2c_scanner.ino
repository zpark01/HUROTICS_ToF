/*
 * Scans an I2C bus and prints whatever answers.
 *
 * Use this first if a ToF sensor is not being found. Fresh sensors show
 * up at 0x29; after the logger reassigns them they appear at 0x30/0x31.
 *
 * Serial monitor at 115200.
 */

#include <Wire.h>

void setup() {
  Serial.begin(115200);
  delay(1000);
  Wire2.begin();
  Serial.println("\n=== I2C scan ===");
  Serial.println("expecting ToF at 0x29, or 0x30/0x31 once assigned");
}

void loop() {
  int count = 0;
  Serial.println("scanning...");
  for (byte addr = 1; addr < 127; addr++) {
    Wire2.beginTransmission(addr);
    byte err = Wire2.endTransmission();
    if (err == 0) {
      Serial.print("  found 0x");
      if (addr < 16) Serial.print("0");
      Serial.print(addr, HEX);
      Serial.println();
      count++;
    }
  }
  if (count == 0) Serial.println("  nothing on the bus - check SDA/SCL and power");
  Serial.print("total ");
  Serial.print(count);
  Serial.println(" device(s)");
  Serial.println("-------------------------------");
  delay(1500);
}
