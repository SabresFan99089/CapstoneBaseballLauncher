#include "../include/Launcher.h"
#include <Servo.h>

#define sgn(x) ((x) < 0 ? -1 : ((x) > 0 ? 1 : 0));
Servo myServo;

// the maximum values of theta and phi
const int maxAngle = 30;

// Arduino pins for linear actuator
const int act_pin = A0;             // linear actuator potentiometer pin
const int act_RPWM = 10;            // linear actutator RPWM connection
const int act_LPWM = 11;            // linear actuator LWPM connection

Launcher::Launcher(int actuatorPin, int servoPin) {
    _actuatorPin = actuatorPin;
    _servoPin = servoPin;

    actReading = 0;
    strokeLength = 8.0;

    sensitivityMode = 0;
    sensitivity[0] = 7;  //fine
    sensitivity[1] = 15; //course
}

// moves the linear actuator in a given direction at a given speed
void Launcher::driveActuator(int Direction, int Speed) {
  switch(Direction){
    case 1:  // extension
      analogWrite(act_RPWM, Speed);
      analogWrite(act_LPWM, 0);
      break;
   
    case 0:  // no movement
      analogWrite(act_RPWM, 0);
      analogWrite(act_LPWM, 0);
      break;

    case -1:  // retraction
      analogWrite(act_RPWM, 0);
      analogWrite(act_LPWM, Speed);
      break;
  }
}

// moves the linear actuator accordingly if the vertical (up and down) IMU angle surpasses the minimum angle
void Launcher::moveAct(float phi) {

    // obtain the analog potentiometer reading of the linear actuator
    actReading = analogRead(act_pin);

    // constrain phi within the max angles
    if (abs(phi) > maxAngle)
    {
        phi = sgn(phi);
        phi = phi * maxAngle;
    }

    // the difference between phi and the activation angle for a given sensitivity
    float diff = abs(phi) - sensitivity[sensitivityMode];

    // determine the actuator speed based on phi
    float actSpeed = mapFloat(abs(phi), 0, maxAngle, 0, 255);

    // extends the linear actuator if phi surpasses the minimum angle
    if (diff > 0)
    {
        phi = sgn(phi);
        driveActuator(phi, actSpeed);
    }
    // does not move the linear actuator if the minimum angle isn't surpassed
    else
    {
        actPos = actReading;
        driveActuator(0, actSpeed);
    }
}

// moves the servo accordingly if the horizontal (left and right) IMU angle surpasses the minimum angle
void Launcher::moveServo(float theta) {

  // conversion from deg to microsec with 0 deg = 1472 microsec
  int maxMicro = round(1472.0 + (45.0 * 10.0 / 3.0));  
  int minMicro = round(1472.0 - (45.0 * 10.0 / 3.0));

  // obtain the analog potentiometer reading of the servo

  // constrain theta within the max angles
  if (abs(theta) > maxAngle) {
    theta = sgn(theta);
    theta = theta * maxAngle;
  }

  // the difference between theta and the activation angle for a given sensitivity
  float diff = abs(theta) - sensitivity[sensitivityMode];

  // determine the servo rotation based on theta
  float servoSpeed = mapFloat(abs(theta), 0, maxAngle, 0, 1); 
  
  // rotates the servo if the theta surpasses the minimum angle
  if (diff > 0) { 
    // the servo position (in degrees) is mapped to microseconds to gain more resolution
    microPos = map(theta, -1 * maxAngle, maxAngle, minMicro, maxMicro); 
    myServo.writeMicroseconds(microPos * servoSpeed);
  }
  // does not move the servo if the minimum angle isn't surpassed
  else { 
    myServo.writeMicroseconds(0);
  }
}