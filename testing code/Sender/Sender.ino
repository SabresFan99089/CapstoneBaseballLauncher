// required for IMU calculations
#include <Adafruit_BNO08x.h>

// for I2C or UART
#define BNO08X_RESET -1

// the sample rate delay and IMU
#define SAMPLERATE_DELAY_MS (250)
Adafruit_BNO08x bno08x(BNO08X_RESET);
sh2_SensorValue_t sensorValue;

// top frequency is about 250Hz but this report is more accurate
sh2_SensorId_t reportType = SH2_ARVR_STABILIZED_RV;
long reportIntervalUs = 5000;

// euler angles
struct euler_t {
  float yaw;
  float pitch;
  float roll;
} ypr;

// for microlight switches
struct microlight_t {
  bool prevClicked;
  int pin;
  long clickedTime;
  long elapsedTime;
};

//  Pins for HM-10 BT module
//  BT VCC to Arduino 5V out
//  BT GND to GND
//  Arduino 48 (Mega) RX -> BT TX no need voltage divider
//  Arduino 46 (Mega) TX -> BT RX through a voltage divider (5v to 3.3v)

// https://www.pjrc.com/teensy/td_libs_AltSoftSerial.html
#include <AltSoftSerial.h>
AltSoftSerial sender;

// the curent angles at the time of calibration
float shift_theta = 0.0;
float shift_phi = 0.0;
int updateCount = 0;

// angles of interest
float theta = 0.0;
float phi = 0.0;

// the max values of theta and phi
int maxAngle = 30;

// to keep track of the current firing mode
// 0 = fine
// 1 = coarse
int sensitivityMode = 0;

// the minimum angle needed to activate
int sensitivity[] = { 7, 15 };

// the microlight switches
struct microlight_t speedUp = { false, 2, 0, 0 };
struct microlight_t speedDown = { false, 3, 0, 0 };
struct microlight_t lock_and_calibrate = { false, 4, 0, 0 };
struct microlight_t fire_and_power = { false, 5, 0, 0 };
struct microlight_t* switchPointers[] = { &speedUp, &speedDown, &lock_and_calibrate, &fire_and_power };

// the min time required to register a held switch
long holdTime = 3000;

// the max time to register a clicked switch
long clickTime = 1000;

// updates the structs when a microlight switch is clicked
void switchClicked(microlight_t* microlight) {
  microlight->prevClicked = true;
  microlight->clickedTime = millis();
}

// updates the structs when a microlight switch is held
void switchHeld(microlight_t* microlight) {
  microlight->elapsedTime = millis() - microlight->clickedTime;
}

// updates the structs when a microlight switch is released
void switchReleased(microlight_t* microlight) {
  microlight->prevClicked = false;
  microlight->elapsedTime = millis() - microlight->clickedTime;
}

// resets a microlight switch to its default state
void resetSwitch(microlight_t* microlight) {
  microlight->prevClicked = false;
  microlight->clickedTime = 0;
  microlight->elapsedTime = 0;
}

// returns if a switch was successfully clicked (released in a short time)
bool validClick(microlight_t* microlight) {
  return (!microlight->prevClicked && (microlight->elapsedTime > 0 && microlight->elapsedTime < clickTime));
}

// returns if a switch was successfully held (released in a long time or not at all)
bool validHold(microlight_t* microlight) {
  return (microlight->elapsedTime > holdTime);
}

// reads and updates the current state of each microlight switch
void readSwitches() {
  for (int i = 0; i < 1; i++) {
    // the reading of the current switch we are evaluating
    int reading = digitalRead(switchPointers[i]->pin);

    // the switch was just clicked
    if (reading == LOW && !switchPointers[i]->prevClicked) {
      switchClicked(switchPointers[i]);
      // Serial.println("CLICKED");
    }
    // the switch is currently held down
    else if (reading == LOW && switchPointers[i]->prevClicked) {
      switchHeld(switchPointers[i]);
      // Serial.println("HELD");
    }
    // the switch was just released
    else if (reading == HIGH && switchPointers[i]->prevClicked) {
      switchReleased(switchPointers[i]);
      // Serial.println("RELEASED");
    }
    // the switch is off
    else if (reading == HIGH && !switchPointers[i]->prevClicked) {
      resetSwitch(switchPointers[i]);
      // Serial.println("OFF");
    }
  }
}

// for BNO085 reading output
void setReports(sh2_SensorId_t reportType, long report_interval) {
  Serial.println("Setting desired reports");
  if (!bno08x.enableReport(reportType, report_interval)) {
    Serial.println("Could not enable stabilized remote vector");
  }
}

// maps input to output positions
float mapFloat(long x, long in_min, long in_max, long out_min, long out_max) {
  return (float)(x - in_min) * (out_max - out_min) / (float)(in_max - in_min) + out_min;
}

// returns the shifted value of theta after calibration
float getShiftedTheta(float theta) {
  float shifted = theta + (-1.0 * shift_theta);

  // to bound the angle from -180 to 180 degrees
  if (shifted > 180.0) {
    shifted = shifted - 360.0;
  } else if (shifted < -180.0) {
    shifted = shifted + 360.0;
  }

  // to flip left/right
  return -shifted;
}

// returns the shifted value of phi after calibration
float getShiftedPhi(float phi) {
  return phi + (-1.0 * shift_phi);
}

// converts quaternions to euler angles
void quaternionToEuler(float qr, float qi, float qj, float qk, euler_t* ypr, bool degrees = false) {
  float sqr = sq(qr);
  float sqi = sq(qi);
  float sqj = sq(qj);
  float sqk = sq(qk);

  ypr->yaw = atan2(2.0 * (qi * qj + qk * qr), (sqi - sqj - sqk + sqr));
  ypr->pitch = asin(-2.0 * (qi * qk - qj * qr) / (sqi + sqj + sqk + sqr));
  ypr->roll = atan2(2.0 * (qj * qk + qi * qr), (-sqi - sqj + sqk + sqr));

  if (degrees) {
    ypr->yaw *= RAD_TO_DEG;
    ypr->pitch *= RAD_TO_DEG;
    ypr->roll *= RAD_TO_DEG;
  }
}

// converts quaternions to euler angles from rotational vector
void quaternionToEulerRV(sh2_RotationVectorWAcc_t* rotational_vector, euler_t* ypr, bool degrees = false) {
  quaternionToEuler(rotational_vector->real, rotational_vector->i, rotational_vector->j, rotational_vector->k, ypr, degrees);
}

// updates the angle shifts
void updateShifts() {
  shift_theta = ypr.yaw;
  shift_phi = ypr.pitch;
}

// updates the angles themselves
void updateAngles() {
  theta = ypr.yaw;
  phi = ypr.pitch;
}

// sends information over BT
void sendInfo() {
  // start of text char
  char stx = 2;

  // end of text char
  char etx = 3;

  // create the data string to send over BT
  String data = stx + String(theta, 1) + " " + String(phi, 1) + etx;

  // send the string over BT char by char
  for (int i = 0; i < data.length(); i++) {
    sender.write(data.charAt(i));
    // Serial.print("Data sent: ");
    // Serial.println(data.charAt(i));
  }
}

void setup() {
  // start serial monitor communication at 9600 baud rate
  Serial.begin(9600);
  Serial.println(" ");

  // start sender BT at 9600 baud rate
  sender.begin(9600);
  Serial.println("Sender started at 9600");

  // try to initialize the IMU
  if (!bno08x.begin_I2C()) {
    Serial.println("Failed to find BNO08x chip");
    while (1) { delay(10); }
  }
  Serial.println("BNO08x Found!");
  setReports(reportType, reportIntervalUs);
  Serial.println("Reading events");

  // initialize microlight switch pins
  for (int i = 0; i < 1; i++) {
    pinMode(switchPointers[i]->pin, INPUT_PULLUP);
  }

  // crucial
  delay(2000);
}

void loop() {
  // zero the IMU or get readings
  if (bno08x.getSensorEvent(&sensorValue)) {
    // read the IMU
    quaternionToEulerRV(&sensorValue.un.arvrStabilizedRV, &ypr, true);

    // zero the IMU at the very beginning of the program
    if (updateCount < 1) {
      updateShifts();
      updateCount++;
    }
    // update theta and phi
    else {
      updateAngles();
    }
  }

  // check for click/hold/release
  readSwitches();

  // the speed up switch was released quickly
  if (validClick(&speedUp)) {
    // TODO: speed up
    // Serial.println("Speed up clicked");
  }

  // the speed down switch was released quickly
  if (validClick(&speedDown)) {
    // TODO: speed down
    // Serial.println("Speed down clicked");
  }

  // the lock and calibrate switch was released quickly
  if (validClick(&lock_and_calibrate)) {
    // TODO: lock the launcher
    //Serial.println("Lock/calibrate clicked");
  }
  // the lock and calibrate switch was held
  else if (validHold(&lock_and_calibrate)) {
    // calibrate the IMU
    // updateShifts();
    // Serial.println("Lock/calibrate held");
  }

  // the fire and power switch was released quickly
  if (validClick(&fire_and_power)) {
    // TODO: fire the launcher
    // Serial.println("Fire/power clicked");
  }
  // the fire and power switch was held
  else if (validHold(&fire_and_power)) {
    // TODO: power the launcher on/off
    // Serial.println("Fire/power held");
  }

  // convert the euler angles to theta and phi
  theta = getShiftedTheta(theta);
  phi = getShiftedPhi(phi);

  Serial.println(theta);
  Serial.println(phi);
  Serial.println("");

  // send the angles over BT
  sendInfo();

  // delay for the designated sample rate delay
  delay(SAMPLERATE_DELAY_MS);
}