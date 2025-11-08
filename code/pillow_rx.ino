const int RX_GO_PIN   = 2;    // Receiver D0
const int MOTOR_PIN   = 6;    // PWM to vibration module S (or MOSFET gate)

const unsigned long VIB_DURATION_MS = 30000;
uint8_t VIB_LEVEL = 255;                      // 0..255 intensity

unsigned long vibrateUntil = 0;

void setup() {
  pinMode(RX_GO_PIN, INPUT);
  pinMode(MOTOR_PIN, OUTPUT);
  analogWrite(MOTOR_PIN, 0);      // motor off
}

void loop() {
  bool go = digitalRead(RX_GO_PIN); 

  if (go) {
    while ((vibrateUntil && millis() <= vibrateUntil)) {
      analogWrite(MOTOR_PIN, VIB_LEVEL);
      delay(1000);
      analogWrite(MOTOR_PIN, 0);
      delay(500);
    }
    vibrateUntil = millis() + VIB_DURATION_MS; // extend window while GO present
  }

  if (vibrateUntil && millis() >= vibrateUntil) {
    analogWrite(MOTOR_PIN, 0);
    vibrateUntil = 0;
  }

  delay(5);
}
