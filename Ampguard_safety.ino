#include <ESP32Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ── Pin Definitions ────────────────────────────────────────
const int SERVO_PIN  = 19;
const int ACS_PIN    = 34;
const int BUZZER_PIN = 18;
// ───────────────────────────────────────────────────────────

// ── LCD Setup ──────────────────────────────────────────────
LiquidCrystal_I2C lcd(0x27, 16, 2);
// ───────────────────────────────────────────────────────────

// ── ACS712 Calibration ─────────────────────────────────────
const float VCC         = 3.3;
const float ADC_MAX     = 4095.0;
const float SENSITIVITY = 0.066;  // 0.066 for 30A, 0.100 for 20A
// ───────────────────────────────────────────────────────────

// ── Threshold ──────────────────────────────────────────────
const float CURRENT_THRESHOLD = 1.4;  // Amps
bool servoDisabled = false;
// ───────────────────────────────────────────────────────────

float zeroOffset = VCC / 2.0;
Servo myServo;

// ── Calibrate Zero Offset ──────────────────────────────────
void calibrateZeroOffset() {
  long sum = 0;
  const int samples = 200;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(ACS_PIN);
    delayMicroseconds(200);
  }
  zeroOffset = (sum / (float)samples / ADC_MAX) * VCC;
  Serial.print("Calibrated zero offset: ");
  Serial.print(zeroOffset, 4);
  Serial.println(" V");
}

// ── Read Current ───────────────────────────────────────────
float readCurrent() {
  long sum = 0;
  const int samples = 50;
  for (int i = 0; i < samples; i++) {
    sum += analogRead(ACS_PIN);
    delayMicroseconds(200);
  }
  float avgRaw  = sum / (float)samples;
  float voltage = (avgRaw / ADC_MAX) * VCC;
  return fabsf((voltage - zeroOffset) / SENSITIVITY);
}

// ── Update LCD Normal Operation ────────────────────────────
void updateLCD(int angle, float current) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Angle: ");
  lcd.print(angle);
  lcd.print((char)223);  // degree symbol
  lcd.print("        ");

  lcd.setCursor(0, 1);
  lcd.print("Curr: ");
  lcd.print(current, 3);
  lcd.print(" A");
}

// ── Show Danger on LCD ─────────────────────────────────────
void showDangerLCD(float triggerCurrent) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("DANGER!MTR STOP!");
  lcd.setCursor(0, 1);
  lcd.print("OC:");
  lcd.print(triggerCurrent, 2);
  lcd.print("A RST ESP32");
}

// ── Buzzer Alert (active buzzer — 3 short beeps) ───────────
void buzzerAlert() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(200);
    digitalWrite(BUZZER_PIN, LOW);
    delay(150);
  }
}

// ── Continuous Buzzer for Halt State ──────────────────────
void buzzerContinuous() {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(600);
  digitalWrite(BUZZER_PIN, LOW);
  delay(400);
}

// ── Disable Servo and Latch ────────────────────────────────
void disableServo(float triggerCurrent) {
  myServo.write(90);
  delay(300);
  myServo.detach();
  servoDisabled = true;

  showDangerLCD(triggerCurrent);
  buzzerAlert();  // 3 beeps on overcurrent

  Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
  Serial.print  ("!! OVERCURRENT: ");
  Serial.print  (triggerCurrent, 3);
  Serial.println(" A — SERVO DISABLED !!");
  Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
  Serial.println("!! Reset the ESP32 to re-enable !!");
  Serial.println("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
}

// ── Move Servo and Monitor Current ────────────────────────
void moveServoAndLog(int angle) {
  if (servoDisabled) {
    Serial.println("Servo is disabled — skipping move.");
    return;
  }

  Serial.print("Moving to ");
  Serial.print(angle);
  Serial.print((char)176);
  Serial.print("  |  ");

  myServo.write(angle);

  float peakCurrent = 0;
  for (int i = 0; i < 20; i++) {
    float c = readCurrent();
    if (c > peakCurrent) peakCurrent = c;

    updateLCD(angle, c);

    if (c >= CURRENT_THRESHOLD) {
      disableServo(c);
      return;
    }

    delay(50);
  }

  Serial.print("Peak current: ");
  Serial.print(peakCurrent, 3);
  Serial.println(" A");
}

// ── Setup ──────────────────────────────────────────────────
void setup() {
  Serial.begin(115200);
  delay(1000);

  // Buzzer pin
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  // LCD init
  Wire.begin(21, 22);
  lcd.init();
  lcd.init();
  lcd.backlight();

  lcd.setCursor(0, 0);
  lcd.print("  Servo Monitor ");
  lcd.setCursor(0, 1);
  lcd.print(" Calibrating... ");

  // Servo init
  ESP32PWM::allocateTimer(0);
  myServo.setPeriodHertz(50);
  myServo.attach(SERVO_PIN, 500, 2400);

  Serial.println("=== ESP32 + Servo + ACS712 + LCD + Buzzer ===");
  Serial.println("Calibrating zero offset — keep servo still...");
  calibrateZeroOffset();

  // Single beep on successful startup
  digitalWrite(BUZZER_PIN, HIGH);
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("  Calib  Done!  ");
  lcd.setCursor(0, 1);
  lcd.print(" Starting Sweep ");
  delay(1500);

  Serial.println("Angle | Peak Current");
  Serial.println("------+-------------");

  myServo.write(90);
  delay(1000);
}

// ── Loop ───────────────────────────────────────────────────
void loop() {
  if (servoDisabled) {
    showDangerLCD(CURRENT_THRESHOLD);
    buzzerContinuous();          // slow beep while halted
    lcd.backlight();   delay(600);
    lcd.noBacklight(); delay(400);
    Serial.println("System halted — reset ESP32 to restart.");
    return;
  }

  moveServoAndLog(0);   delay(500);
  moveServoAndLog(45);  delay(500);
  moveServoAndLog(90);  delay(500);
  moveServoAndLog(135); delay(500);
  moveServoAndLog(180); delay(500);
  moveServoAndLog(135); delay(500);
  moveServoAndLog(90);  delay(500);
  moveServoAndLog(45);  delay(500);

  float idle = readCurrent();
  Serial.print("Idle current (holding position): ");
  Serial.print(idle, 3);
  Serial.println(" A");
  Serial.println("---");

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Idle / Holding  ");
  lcd.setCursor(0, 1);
  lcd.print("Curr: ");
  lcd.print(idle, 3);
  lcd.print(" A");

  delay(2000);
}