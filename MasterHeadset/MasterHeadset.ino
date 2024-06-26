#include <DFRobotDFPlayerMini.h>
#include <avr/io.h>
#include <stdlib.h>
#include <Arduino.h>

#define SpeechSerial Serial1
#define DFSerial Serial2
#define BTSerial Serial3

DFRobotDFPlayerMini myDFPlayer;
bool DFPlayerActive;

// the default speed on startup
int currSpeed = 40;

// speech recognition
String speechMsg = "";
bool speechRecogOn = true;

// required for IMU calculations
#include <Adafruit_BNO08x.h>

// for I2C or UART
#define BNO08X_RESET -1

// the sample rate delay and IMU
#define SAMPLERATE_DELAY_MS (200)
Adafruit_BNO08x bno08x(BNO08X_RESET);
sh2_SensorValue_t sensorValue;

// top frequency is about 250Hz but this report is more accurate
sh2_SensorId_t reportType = SH2_ARVR_STABILIZED_RV;
int reportIntervalUs = 5000;

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
  int holdsReached;
};

// the curent angles at the time of calibration
float shift_theta = 0.0;
float shift_phi = 0.0;
boolean startUp = true;

// angles of interest
float theta = 0.0;
float phi = 0.0;

// the max values of theta and phi
int maxAngle = 30;

// the microlight switches
struct microlight_t redSwitch = { false, 8, 0, 0, 0 };
struct microlight_t blueSwitch = { false, 9, 0, 0, 0 };
struct microlight_t* switchPointers[] = { &redSwitch, &blueSwitch };

// hold time for microlight switches
const int holdTime = 4000;

// hold time for Sicko Mode
const int sickoModeTime = 10000;

// the max time to register a clicked switch
int clickTime = 500;

// the launcher is initially locked
bool locked = true;

// to keep track of the switch info to send to the launcher
int speedInfo;
int fireInfo;
int powerInfo;

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
  microlight->holdsReached = 0;
}

// returns if a switch was successfully clicked and released
bool validClick(microlight_t* microlight) {
  return (!microlight->prevClicked && (microlight->elapsedTime > 0 && microlight->elapsedTime < clickTime));
}

// returns if a switch was successfully held and released
bool validHoldAndRelease(microlight_t* microlight, long time) {
  return (!microlight->prevClicked && (microlight->elapsedTime > time) && (microlight->elapsedTime < sickoModeTime));
}

// returns if a switch was successfully held past a given time
bool validHold(microlight_t* microlight, long time) {
  return (microlight->elapsedTime > time);
}

// reads and updates the current state of each microlight switch
void readSwitches() {
  for (int i = 0; i < sizeof(switchPointers) / sizeof(microlight_t*); i++) {
    // the reading of the current switch we are evaluating
    int reading = digitalRead(switchPointers[i]->pin);

    // the switch was just clicked
    if (reading == LOW && !switchPointers[i]->prevClicked) {
      switchClicked(switchPointers[i]);
    }
    // the switch is currently held down
    else if (reading == LOW && switchPointers[i]->prevClicked) {
      switchHeld(switchPointers[i]);
    }
    // the switch was just released
    else if (reading == HIGH && switchPointers[i]->prevClicked) {
      switchReleased(switchPointers[i]);
    }
    // the switch is off
    else if (reading == HIGH && !switchPointers[i]->prevClicked) {
      resetSwitch(switchPointers[i]);
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

// generates a string of data describing the state of the switches
String generateSwitchData() {
  // separated by spaces to allow for both positive and negative numbers
  return String(speedInfo) + " " + String(fireInfo) + " " + String(powerInfo);
}

// sends information over BT
void sendInfo() {
  // start of text char
  char stx = 2;

  // end of text char
  char etx = 3;

  // for the microlight switches
  String switchData = generateSwitchData();

  // create the data string to send over BT
  String allData = stx + String(theta, 1) + " " + String(phi, 1) + " " + switchData + etx;

  // send the string over BT char by char
  for (int i = 0; i < allData.length(); i++) {
    BTSerial.write(allData.charAt(i));
    // Serial.print("Data sent: ");
    // Serial.println(allData.charAt(i));
  }
}

void performSpeechRec() {
  /* ----- Speech Commands
    10001	Ten
    10002	Twenty
    10003	Thirty
    10004	Forty
    10005	Fifty
    10006	Sixty
    10007	Seventy
    10008	Eighty
    10009	Ninety
    10010	Hundred
    10011	Calibrate
    10012	Fire
    10013	Lock
    10014	On
    10015	Off
    10016 Cancel
  */

  /* -------- AUDIO MP3s
   (val): Control name
  (1-10): Speed control - sets speed to val*10 speed
      11: Launcher is locked
      12: Launcher is unlocked
      13: Sensors calibrated
      14: Face forward and release to calibrate
      15: Firing
      16: Power on
      17: Power off
      18: Release to fire
      19: Speech recognition on
      20: Speech recognition off
      21: Speed up
      22: Release to decrease speed
      23: Release to turn speech recognition on
      24: Release to turn speech recognition off
      99: Easter egg
  */

  while (SpeechSerial.available() > 0) {
    // do not read if speech serial is turned off
    if (!speechRecogOn) {
      SpeechSerial.read();
    }
    // do read speech serial
    else {
      char c = SpeechSerial.read();
      speechMsg += c;
      if (c == '\n') {
        speechMsg = speechMsg.substring(0, speechMsg.length() - 1);
        int val = speechMsg.toInt();
        speechMsg = "";

        // speed control
        if (val <= 10) {
          // how much to increase or decrease the speed (1->5, 2->10, ..., 10->50)
          speedInfo = ((val * 10) - currSpeed) / 5;

          // update the current speed
          currSpeed = val * 10;
          myDFPlayer.playMp3Folder(val);
        } else {
          switch (val) {
            // calibrate the IMU
            case 11:
              updateShifts();
              myDFPlayer.playMp3Folder(13);
              break;

            // Lock/unlock the launcher
            case 13:
              locked = !locked;
              if (locked) {
                myDFPlayer.playMp3Folder(11);
              } else {
                myDFPlayer.playMp3Folder(12);
              }
              break;

            // turn on launcher
            case 14:
              Serial.println("Power on");
              powerInfo = 1;
              // reset the current speed
              currSpeed = 40;
              myDFPlayer.playMp3Folder(16);
              break;

            // turn off launcher
            case 15:
              Serial.println("Power off");
              powerInfo = 1;
              // reset the current speed
              currSpeed = 40;
              myDFPlayer.playMp3Folder(17);
              break;
          }
        }
      }
    }
  }
}

void setup() {
  // begin Serial communication
  Serial.begin(9600);
  SpeechSerial.begin(9600);
  BTSerial.begin(9600);

  // try to initialize the IMU
  if (!bno08x.begin_I2C()) {
    Serial.println("Failed to find BNO08x chip");
    delay(10);
  }

  Serial.println("BNO08x Found!");
  setReports(reportType, reportIntervalUs);
  Serial.println("Reading events");

  // initialize microlight switch pins
  for (int i = 0; i < sizeof(switchPointers) / sizeof(microlight_t*); i++) {
    pinMode(switchPointers[i]->pin, INPUT_PULLUP);
  }

  // audio module setup
  DFSerial.begin(9600);
  if (!myDFPlayer.begin(DFSerial, /*isACK = */ true, /*doReset = */ true)) {  // Use serial to communicate with mp3.
    Serial.println("No Connection to DFPlayer");
    DFPlayerActive = false;
  } else {
    DFPlayerActive = true;
    Serial.println(F("Connection Successful"));
    myDFPlayer.setTimeOut(500);  // Set serial communictaion time out 500ms
    myDFPlayer.volume(70);
    myDFPlayer.EQ(DFPLAYER_EQ_NORMAL);
    myDFPlayer.outputDevice(DFPLAYER_DEVICE_SD);
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
    if (startUp) {
      myDFPlayer.playMp3Folder(16);
      delay(1500);
      updateShifts();
      myDFPlayer.playMp3Folder(13);
      startUp = false;
    }

    // update theta and phi
    updateAngles();
  }

  // assume that we don't want to change speed, fire, or toggle power
  fireInfo = 0;
  powerInfo = 0;
  speedInfo = 0;

  performSpeechRec();
  readSwitches();

  // to play audio messages to indicate when to release the buttons
  if (validHold(&redSwitch, sickoModeTime) && redSwitch.holdsReached == 2) {
    redSwitch.holdsReached++;
    myDFPlayer.playMp3Folder(99);
    Serial.println("Release to play Sicko Mode.");
  } 
  else if (validHold(&redSwitch, holdTime) && redSwitch.holdsReached == 1) {
    redSwitch.holdsReached++;
    myDFPlayer.playMp3Folder(18);
    Serial.println("Release to fire launcher.");
  } 
  else if (validHold(&redSwitch, clickTime) && redSwitch.holdsReached == 0) {
    redSwitch.holdsReached++;
    myDFPlayer.playMp3Folder(22);
    Serial.println("Release to decrease the speed.");
  } 
  else if (validHold(&blueSwitch, holdTime) && blueSwitch.holdsReached == 1) {
    blueSwitch.holdsReached++;
    if (speechRecogOn) {
      myDFPlayer.playMp3Folder(24);
    } 
    else {
      myDFPlayer.playMp3Folder(23);
    }
    Serial.println("Release to toggle speech recognition.");
  } 
  else if (validHold(&blueSwitch, clickTime) && blueSwitch.holdsReached == 0) {
    blueSwitch.holdsReached++;
    myDFPlayer.playMp3Folder(14);
    Serial.println("Release to calibrate the IMU.");
  }

  // if only the red switch is held past the hold time, fire the launcher upon release
  if (validHoldAndRelease(&redSwitch, holdTime) && digitalRead(blueSwitch.pin) == HIGH) {
    Serial.println("Firing");
    // fire the launcher
    fireInfo = 1;
    myDFPlayer.playMp3Folder(15);
  }
  // if only the red switch is held past the click time, decrease the speed by 10
  else if (validHoldAndRelease(&redSwitch, clickTime) && digitalRead(blueSwitch.pin) == HIGH) {
    // decrease the speed by 5 if we can
    if (currSpeed > 5) {
      currSpeed -= 5;
      speedInfo = -1;
      // play the appropriate speed audio file
      if (currSpeed % 10 == 0) {
        // Serial.println("Decreasing speed by 5");
        myDFPlayer.playMp3Folder(currSpeed / 10);
      }
    } else {
      // Serial.println("Can't decrease speed.");
      // TODO play the appropriate speed audio file
    }
  }
  // if only the red switch is clicked, increase the speed by 10
  else if (validClick(&redSwitch) && digitalRead(blueSwitch.pin) == HIGH) {
    // increase the speed by 5 if we can
    if (currSpeed <= 95) {
      currSpeed += 5;
      speedInfo = 1;
      // play the appropriate speed audio file
      if (currSpeed % 10 == 0) {
        // Serial.println("Increasing speed by 5");
        myDFPlayer.playMp3Folder(currSpeed / 10);
      }
    } else {
      // Serial.println("Can't increase speed.");
      // TODO play appropriate audio
    }
  }
  // if only the blue switch is held past the hold time, toggle speech recognition
  else if (validHoldAndRelease(&blueSwitch, holdTime) && digitalRead(redSwitch.pin) == HIGH) {
    Serial.println("Toggling speech recognition");
    speechRecogOn = !speechRecogOn;
    if (speechRecogOn) {
      myDFPlayer.playMp3Folder(19);
    } else {
      myDFPlayer.playMp3Folder(20);
    }
  }
  // if only the blue switch is held past the click time, calibrate the IMU upon release
  else if (validHoldAndRelease(&blueSwitch, clickTime) && digitalRead(redSwitch.pin) == HIGH) {
    Serial.println("Calibrated");
    updateShifts();
    myDFPlayer.playMp3Folder(13);
  }
  // if only the blue switch is clicked, toggle lock
  else if (validClick(&blueSwitch) && digitalRead(redSwitch.pin) == HIGH) {
    Serial.println("Toggling lock");
    locked = !locked;

    // play the appropriate audio file
    if (locked) {
      myDFPlayer.playMp3Folder(11);
    } else {
      myDFPlayer.playMp3Folder(12);
    }
  }

  // set theta and phi to 0 if the launcher is locked or if we are calibrating the IMU
  if (locked || (digitalRead(blueSwitch.pin) == LOW && blueSwitch.elapsedTime > 250)) {
    theta = 0;
    phi = 0;
  }
  // otherwise, calculate the shifted theta and phi angles
  else {
    theta = getShiftedTheta(theta);
    phi = getShiftedPhi(phi);
  }

  // send the angles over BT
  sendInfo();

  // delay for the designated sample rate delay
  delay(SAMPLERATE_DELAY_MS);
}