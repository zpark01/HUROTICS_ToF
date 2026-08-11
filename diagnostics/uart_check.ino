/*
 * Counts raw bytes arriving on Serial1 and Serial2.
 *
 * If an IMU is silent this tells you whether anything is coming through
 * at all, before the library gets involved. A count stuck at zero almost
 * always means P0 is not pulled to 3.3V, so the sensor is still sitting
 * in I2C mode.
 *
 * Serial monitor at 115200.
 */

void setup() {
  Serial.begin(115200);
  delay(800);
  Serial1.begin(115200);   // IMU#1
  Serial2.begin(115200);   // IMU#2
  Serial.println("\n=== raw UART byte count ===");
  Serial.println("zero bytes usually means P0 is not tied to 3.3V");
  Serial.println("bytes arriving but garbled - check SDA goes to an RX pin");
  Serial.println("-------------------------------------------------");
}

unsigned long count1 = 0, count2 = 0;
unsigned long lastReport = 0;

void loop() {
  while (Serial1.available()) {
    byte b = Serial1.read();
    count1++;
    if (count1 <= 20) {   // first 20 bytes only, as hex
      Serial.print("S1:0x");
      if (b < 16) Serial.print("0");
      Serial.print(b, HEX);
      Serial.print(" ");
    }
  }
  while (Serial2.available()) {
    byte b = Serial2.read();
    count2++;
    if (count2 <= 20) {
      Serial.print("S2:0x");
      if (b < 16) Serial.print("0");
      Serial.print(b, HEX);
      Serial.print(" ");
    }
  }

  if (millis() - lastReport > 2000) {
    lastReport = millis();
    Serial.println();
    Serial.print("[2s] Serial1 bytes: ");
    Serial.print(count1);
    Serial.print("   Serial2 bytes: ");
    Serial.println(count2);
    Serial.println("-------------------------------------------------");
  }
}
