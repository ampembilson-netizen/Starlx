// Starlx greenhouse controller for Arduino Uno + HM-10 BLE module.
//
// Wiring (HM-10 is 3.3V logic on its RX pin):
//   HM-10 VCC -> 5V (or 3.3V), GND -> GND
//   HM-10 TXD -> Uno pin 2
//   HM-10 RXD <- Uno pin 3 through a voltage divider (1k from pin 3 to RXD, 2k from RXD to GND)
// Sensors:
//   Soil moisture sensor -> A0   (higher reading = drier)
//   Light sensor (LDR divider) -> A1   (higher reading = brighter)
//   TMP36 temperature sensor -> A2
// Relays (active-low modules): pump -> pin 7, fan -> pin 8, grow light -> pin 9
//
// Commands Starlx sends (one per line): PUMP ON/OFF, FAN ON/OFF, LIGHT ON/OFF,
// AUTO ON/OFF, STATUS. The same commands work from the Serial Monitor at 9600 baud.
// Every 3 seconds it reports: T=24.3 S=512 L=700 PUMP=0 FAN=0 LIGHT=0 AUTO=0

#include <SoftwareSerial.h>

SoftwareSerial ble(2, 3);  // RX, TX

const int SOIL_PIN = A0;
const int LDR_PIN = A1;
const int TEMP_PIN = A2;
const int PUMP_PIN = 7;
const int FAN_PIN = 8;
const int LIGHT_PIN = 9;

const bool RELAY_ACTIVE_LOW = true;

// Calibrate these for your own sensors.
const int SOIL_DRY = 600;              // above this = soil is dry
const int LIGHT_DARK = 300;            // below this = too dark
const float FAN_ON_C = 30.0;           // fan on above this temperature
const float FAN_OFF_C = 27.0;          // fan off below this temperature
const unsigned long PUMP_MAX_MS = 15000UL;     // safety: pump never runs longer than this
const unsigned long PUMP_COOLDOWN_MS = 60000UL; // auto mode waits this long between waterings

bool pumpOn = false, fanOn = false, lightOn = false, autoMode = false;
unsigned long pumpStart = 0, lastPumpEnd = 0, lastReport = 0;
String bleLine = "";
String serialLine = "";

void writeRelay(int pin, bool on) {
  bool level = RELAY_ACTIVE_LOW ? !on : on;
  digitalWrite(pin, level ? HIGH : LOW);
}

void setPump(bool on) {
  if (on && !pumpOn) pumpStart = millis();
  if (!on && pumpOn) lastPumpEnd = millis();
  pumpOn = on;
  writeRelay(PUMP_PIN, on);
}
void setFan(bool on) { fanOn = on; writeRelay(FAN_PIN, on); }
void setLight(bool on) { lightOn = on; writeRelay(LIGHT_PIN, on); }

float readTempC() {
  float volts = analogRead(TEMP_PIN) * 5.0 / 1024.0;
  return (volts - 0.5) * 100.0;  // TMP36
}

void report() {
  String msg = "T=" + String(readTempC(), 1) +
               " S=" + String(analogRead(SOIL_PIN)) +
               " L=" + String(analogRead(LDR_PIN)) +
               " PUMP=" + String(pumpOn ? 1 : 0) +
               " FAN=" + String(fanOn ? 1 : 0) +
               " LIGHT=" + String(lightOn ? 1 : 0) +
               " AUTO=" + String(autoMode ? 1 : 0);
  ble.println(msg);
  Serial.println(msg);
}

void handleCommand(String cmd) {
  cmd.trim();
  cmd.toUpperCase();
  if (cmd == "PUMP ON") setPump(true);
  else if (cmd == "PUMP OFF") setPump(false);
  else if (cmd == "FAN ON") setFan(true);
  else if (cmd == "FAN OFF") setFan(false);
  else if (cmd == "LIGHT ON") setLight(true);
  else if (cmd == "LIGHT OFF") setLight(false);
  else if (cmd == "AUTO ON") autoMode = true;
  else if (cmd == "AUTO OFF") autoMode = false;
  else if (cmd != "STATUS") return;
  report();
}

void readLines(Stream &port, String &buf) {
  while (port.available()) {
    char c = port.read();
    if (c == '\n') {
      handleCommand(buf);
      buf = "";
    } else if (c != '\r' && buf.length() < 40) {
      buf += c;
    }
  }
}

void runAuto() {
  if (!autoMode) return;
  unsigned long now = millis();
  int soil = analogRead(SOIL_PIN);
  int light = analogRead(LDR_PIN);
  float t = readTempC();

  if (!pumpOn && soil > SOIL_DRY && (lastPumpEnd == 0 || now - lastPumpEnd > PUMP_COOLDOWN_MS)) setPump(true);
  if (pumpOn && soil <= SOIL_DRY) setPump(false);

  if (!fanOn && t > FAN_ON_C) setFan(true);
  if (fanOn && t < FAN_OFF_C) setFan(false);

  setLight(light < LIGHT_DARK);
}

void setup() {
  // Set relay pins to OFF before making them outputs, so nothing clicks on at boot.
  writeRelay(PUMP_PIN, false);
  writeRelay(FAN_PIN, false);
  writeRelay(LIGHT_PIN, false);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  Serial.begin(9600);
  ble.begin(9600);
}

void loop() {
  readLines(ble, bleLine);
  readLines(Serial, serialLine);

  // Safety: the pump never runs longer than PUMP_MAX_MS.
  if (pumpOn && millis() - pumpStart > PUMP_MAX_MS) {
    setPump(false);
    report();
  }

  runAuto();

  if (millis() - lastReport > 3000UL) {
    lastReport = millis();
    report();
  }
}
