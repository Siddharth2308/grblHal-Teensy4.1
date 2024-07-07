#include <Arduino.h>

#define AXIS_EN     10
#define AXIS_PULSE   2
#define AXIS_DIR     3

void setup() {
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println("Test Started");

  pinMode(AXIS_EN, OUTPUT);
  pinMode(AXIS_PULSE, OUTPUT);
  pinMode(AXIS_DIR, OUTPUT);

  digitalWrite(AXIS_EN, LOW);
}

void loop() {
  // put your main code here, to run repeatedly:
  digitalWrite(AXIS_DIR, HIGH);
  for(int x = 0; x < 800; x++){
    digitalWrite(AXIS_PULSE, HIGH);
    delayMicroseconds(500);
    digitalWrite(AXIS_PULSE, LOW);
    delayMicroseconds(500);
  }

  delay(1000);

  digitalWrite(AXIS_DIR, LOW);
  for (int x = 0; x < 800; x++)
  {
    digitalWrite(AXIS_PULSE, HIGH);
    delayMicroseconds(500);
    digitalWrite(AXIS_PULSE, LOW);
    delayMicroseconds(500);
  }

  delay(1000);
}

