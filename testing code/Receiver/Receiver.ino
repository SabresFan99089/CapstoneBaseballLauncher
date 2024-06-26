// for BT communication
#define BTSerial Serial3

// required for servo operation
#include <Servo.h>

// for sign operations
#define sgn(x) ((x) < 0 ? -1 : ((x) > 0 ? 1 : 0))

// start of text char
char stx = 2;

// end of text char
char etx = 3;

// to store the received data
String data = "";

// char to store read data
char c = ' ';

// IMU angles
float theta_angle = 0.0;
float phi_angle = 0.0;

// for the servo motor
Servo myServo;
int servoPin = 5;
int servoZero = 90;
int prevServoPos = servoZero;
float servoSpeed;  // the scaled rotation speed of the servo (value from 0-1)
float servoPos;    // the set position that the servo is being moved to

// the maximum values of theta and phi
int maxAngle = 30;

// Arduino pins for linear actuator (don't use pin 10)
int act_pin = A0;   // linear actuator potentiometer pin
int act_RPWM = 11;  // linear actutator RPWM connection
int act_LPWM = 12;  // linear actuator LWPM connection

// state variables for linear actuator
int actReading = 0;          // the value read by the linear actuator potentiometer
float actSpeed;              // speed of the linear actuator (value from 0-255)
int maxAnalogReading = 926;  // max value that linear actuator is allowed to move to
int minAnalogReading = 39;   // min value that linear actuator is allowed to move to

// to keep track of the current firing mode
// 0 = fine
// 1 = coarse
int sensitivityMode = 1;

// the minimum angle needed to activate
int sensitivity[] = { 7, 15 };

// to keep track of the switch info to send to the launcher
int speedChange;
int fire;

// the minimum time between each fire
long fireCooldown = 5000;

// the time when the launcher was fired last
long lastFireTime = 0;

// returns whether or not a given linear actuator movement is valid
bool validMovement(int dir, int pot) {
  return (dir == 0) || ((dir == 1) && (pot < maxAnalogReading)) || ((dir == -1) && (pot > minAnalogReading));
}

// maps input to output positions
float mapFloat(float x, float in_min, float in_max, float out_min, float out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// moves the linear actuator in a given direction at a given speed
void driveActuator(int dir, float Speed) {
  // update the current potentiometer reading
  actReading = analogRead(act_pin);

  // confirm that the actuator can move in the specified direction
  if (!validMovement(dir, actReading)) {
    return;
  }

  switch (dir) {
    case 1:  // extension
      analogWrite(act_RPWM, Speed);
      analogWrite(act_LPWM, 0);
      break;

    case 0:  // no movement
      analogWrite(act_RPWM, 0);
      analogWrite(act_LPWM, 0);
      break;

    case -1: // retraction
      analogWrite(act_RPWM, 0);
      analogWrite(act_LPWM, Speed);
      break;
  }
}

// moves the linear actuator accordingly if the vertical (up and down) IMU angle surpasses the minimum angle
void moveAct(float phi) {
  // the absolute value of phi
  float a = abs(phi);

  // constrain phi within the max angles
  if (a > maxAngle) {
    phi = sgn(phi) * maxAngle;
  }

  // the difference between phi and the activation angle for a given sensitivity
  float diff = abs(phi) - sensitivity[sensitivityMode];

  // determine the actuator speed based on phi
  float actSpeed = mapFloat(abs(phi), 0.0, maxAngle, 0.0, 255.0);

  // extends the linear actuator if phi surpasses the minimum angle
  if (diff > 0) {
    driveActuator(sgn(phi), actSpeed);
  }
  // does not move the linear actuator if the minimum angle isn't surpassed
  else {
    driveActuator(0, actSpeed);
  }
}

// moves the servo accordingly if the horizontal (left and right) IMU angle surpasses the minimum angle
void moveServo(float theta) {
  // absolute value of theta
  float a = abs(theta);

  // constrain theta within the max angles
  if (a > maxAngle) {
    theta = sgn(theta) * maxAngle;
  }

  // the difference between theta and the activation angle for a given sensitivity
  float diff = abs(theta) - sensitivity[sensitivityMode];

  // before moving, obtain the last written position of the servo
  prevServoPos = myServo.read();

  // rotates the servo if theta surpasses the minimum angle
  if (diff > 0) {
    if (theta > 0) {
      myServo.write(prevServoPos - 12);
    } else if (theta < 0) {
      myServo.write(prevServoPos + 12);
    }
  }
}

// obtain the first number in the string (theta)
float dataToTheta(String str) {
  return str.substring(0, str.indexOf(" ")).toFloat();
}

// obtain the second number in the string (phi)
float dataToPhi(String str) {
  int space1 = str.indexOf(" ");
  int space2 = (str.substring(space1 + 1, str.indexOf(etx))).indexOf(" ");
  return str.substring(space1 + 1, space2).toFloat();
}

// update the state variables for speed up, speed down, and fire
void updateStateVars(String str) {
  int space1 = str.indexOf(" ");
  int space2 = (str.substring(space1 + 1, str.indexOf(etx))).indexOf(" ");
  String stateVars = str.substring(space2 + 1, str.indexOf(etx));

  speedChange = int(stateVars.charAt(0));
  fire = int(stateVars.charAt(1));
}

// safely read and process a character from BT
char readSafe() {
  // the character to read
  char ch = -1;

  // read until we have a valid read
  while (!isValidChar(ch)) {
    // read while there is information to read
    while (BTSerial.available() > 0) {
      ch = char(BTSerial.read());
    }
  }

  // return the valid char
  return ch;
}

// returns whether or not the given char is valid
bool isValidChar(char ch) {
  // a valid char is one of [stx (2), etx (3), space (32), minus (45), period (46), number (48-57)]
  char validChars[15] = { 2, 3, 32, 45, 46, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57 };

  // assume the character is invalid
  bool isValid = false;

  // check the char against the list of valid chars
  for (int i = 0; i < 15; i++) {
    isValid = isValid || (ch == validChars[i]);
  }

  return isValid;
}

void setup() {
  // start serial monitor communication at 9600 baud rate
  Serial.begin(9600);
  Serial.print("Sketch:   ");
  Serial.println(__FILE__);
  Serial.print("Uploaded: ");
  Serial.println(__DATE__);
  Serial.println(" ");

  // start receiver BT at 9600 baud rate
  BTSerial.begin(9600);
  Serial.println("Receiver started at 9600");

  // zero the servo and attach it to the Arduino
  myServo.write(servoZero);
  myServo.attach(servoPin);

  // 1 second delay
  delay(1000);
}

void loop() {
  // read until we reach the beginning of the data string
  while (c != stx) {
    c = readSafe();
  }

  // read the next char (the first char of interest in the data string)
  c = readSafe();

  // continue reading and constructing the data string until etx or no more chars to read
  while (c != etx) {
    // append the most recent char to the data string
    data += String(c);

    // read the next char
    c = readSafe();
  }

  // check to see if the entire string was received (last char is etx)
  if (c == etx) {
    // update theta and phi and reset the data string
    theta_angle = dataToTheta(data);
    phi_angle = dataToPhi(data);

    // update the state variables for speed up, speed down, and fire
    updateStateVars(data);

    // move the servo and linear actuator
    moveServo(theta_angle);
    moveAct(phi_angle);

    if (fire && ((millis() - lastFireTime) > fireCooldown)) {
      // TODO: fire

      lastFireTime = millis();
    }

    // print what is received from BT
    for (int i = 0; i < data.length(); i++) {
      Serial.print(data.charAt(i));
    }

    // reset the data string
    data = "";

    Serial.println("");
    // Serial.println("Theta: " + String(theta_angle));
    // Serial.println("Phi: " + String(phi_angle));
  }
}