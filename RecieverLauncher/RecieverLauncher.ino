#include <AltSoftSerial.h>
#include "include/Launcher.h"

const int launcherProcessingSpeed = 500;

const int SPEEDUP_PIN = 123;
const int SPEEDDOWN_PIN = 123;

AltSoftSerial bluetoothSerial;
Launcher _launcher;

char speechRecognition() {
}

void setup() {
    // Open serial communications and wait for port to open:
    Serial.begin(19200);
    // set the data rate for the SoftwareSerial port
    bluetoothSerial.begin(9600);
}

void clearBluetoothBuffer() {
  while(bluetoothSerial.available()) {
    char clear = bluetoothSerial.read();
  }
}

void addSpeed(int speed)
{
    delay(launcherProcessingSpeed);

    if (speed < 0)
    {
        addSpeed(speed + 1);
        digitalWrite(SPEEDDOWN_PIN, HIGH);
        digitalWrite(SPEEDDOWN_PIN, LOW);
    }
    else if (speed > 0)
    {
        addSpeed(speed - 1);
        digitalWrite(SPEEDUP_PIN, HIGH);
        digitalWrite(SPEEDUP_PIN, LOW);
    }

    clearBluetoothBuffer();
}

float readBytes() {
  if (bluetoothSerial.available() >= sizeof(float))
  { // Check if there are enough bytes available
    // Read the incoming bytes into a byte array
    byte byteArray[sizeof(float)];
    bluetoothSerial.readBytes(byteArray, sizeof(byteArray));

    // Convert the byte array back to an integer
    float receivedData;
    memcpy(&receivedData, byteArray, sizeof(receivedData));

    return receivedData;
  }
}

void loop() {
  // read PHI THETA values from Master IMU to write to Reciever actuator and servo
  if(bluetoothSerial.read() =='P') 
  {
    //float phi = readBytes();
    //float theta = readBytes();
    String str = receiver.readString();
    float phi = str.substring(0, str.indexOf(" ")).toFloat();
    float theta = str.substring(str.indexOf(" "), str.length()).toFloat();

    Serial.println("phi: " + String(phi) + " / theta: " + String(theta));
  
    _launcher.moveAct(phi);
    _launcher.moveServo(theta);
  }

  else if(bluetoothSerial.read() =='S')
  {
    float num = readBytes();
    Serial.println("Recieved: " + String(num));
    addSpeed(num);
  }

  else if(bluetoothSerial.read() =='H')
  {
    _launcher.setSensitivity('H');
  }

  else if(bluetoothSerial.read() =='L')
  {
    _launcher.setSensitivity('L');
  }
}
