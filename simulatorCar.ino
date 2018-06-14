// Début import des librairies //
#include <Arduino.h>
#include <MPU6050.h>
#include <RH_RF95.h>
#include <stdint.h>
#include <Servo.h>
#include <SoftwareSerial.h>
#include <USBAPI.h>
#include <Wire.h>
#include <WString.h>
#include <stddef.h>
// Fin import des librairies //

#define SERIAL_CLI_ENABLED true 

#define ENGINE_CONTROLLER_PIN 5
#define ENGINE_DIRECTION_PIN 4
#define ENGINE_TURN_PIN 6
#define ENGINE_DEFAULT_SPEED 50

#define CAMERA_X_PIN 8
#define CAMERA_Y_PIN 9

#define NETWORKING_RADIO_MAX_RETRY 1
#define NETWORKING_RADIO_READ_PIN 2
#define NETWORKING_RADIO_WRITE_PIN 3
#define NETWORKING_RADIO_FREQUENCY 434.0

#define LED_PIN 13

int16_t ax, ay, az;
int16_t gx, gy, gz;
int16_t mx, my, mz;

String STRING1 = "********************";
String STRING2 = "********************";

class CoreSystem {
  private:
    bool blinkState;
  public:
    CoreSystem() {
      this->blinkState = false;
    }
    void initialize() {
      Serial.println("[CoreSystem] initializing...");

      Serial.println("[CoreSystem] initialized.");
    }
    void blinkLed() {
      this->blinkState = !this->blinkState;
      digitalWrite(LED_PIN, this->blinkState);
    }
};

class Engine {
  private:
    byte speed = 0;
    byte motorControllerPin = 0;
    byte motorDirectionPin = 0;
    Servo servoTurn;
  public:
    Engine() {
      /*// http://www.instructables.com/id/Arduino-Motor-Shield-Tutorial/
         Function				Channel A		Channel B
         Direction			Digital 12		Digital 13
         Speed (PWM)			Digital 3		Digital 11
         Brake				Digital 9		Digital 8
         Current Sensing		Analog 0		Analog 1
      */
    }
    void initialize(byte motorPin, byte directionPin, byte turnPin, byte defaultSpeed) {
      Serial.println("[Engine] initializing...");

      this->speed = defaultSpeed;
      this->motorControllerPin = motorPin;
      this->motorDirectionPin = directionPin;

      pinMode(motorPin, OUTPUT);
      pinMode(directionPin, OUTPUT);
      pinMode(turnPin, OUTPUT);
      
      digitalWrite(this->motorControllerPin, 255);
      digitalWrite(this->motorControllerPin, 0);
      this->servoTurn.attach(turnPin);

      Serial.println("[Engine] initialized.");
    }
    void move(byte newSpeed, byte newDirection) {
      analogWrite(this->motorControllerPin, newSpeed);
      digitalWrite(this->motorDirectionPin, newDirection);
    }
    void turn(word angle) {
      this->servoTurn.write(angle);
    }
};

class CameraServo {
  private:
    Servo servoX;
    Servo servoY;
  public:
    CameraServo() {
    }
    void initialize(byte xPin, byte yPin) {
      Serial.println("[CameraServo] initializing...");

      this->servoX.attach(xPin);
      this->servoY.attach(yPin);

      Serial.println("[CameraServo] initialized.");
    }
    void moveX(word x) {
      this->servoX.write(x);
    }
    void moveY(word y) {
      this->servoY.write(y);
    }
    void move(word x, word y) {
      this->servoX.write(x);
      this->servoY.write(y);
    }
    void moveFromGyro() {
      word xPosition = this->processRotationValue((word) mx, 20, 120);
      word yPosition = this->processRotationValue((word) my, 80, 120);
      this->move(xPosition, yPosition);
    }
    int processRotationValue(word value, word minAngle, word maxAngle) {
      value = 90 - ((word) (value / 20)) * 20;
      return constrain(value, minAngle, maxAngle);
    }
};

class Environment {
  private:
    MPU6050 accelGyro; //
    bool accelGyroWorking = false;
  public:
    Environment() {
    }
    void initialize() {
      Serial.println("[Environment] initializing...");

      Serial.println("[Environment] [I2C] Initializing devices...");
      Serial.print("[Environment] [MPU6050] Testing device connections... ");
      this->accelGyro.initialize();
      accelGyroWorking = this->accelGyro.testConnection();
      Serial.println(this->accelGyro.testConnection() ? "READY" : "FAILED");

      Serial.println("[Environment] initialized.");
    }
    void updateMotion() {
      if (!this->accelGyroWorking) {
        return;
      }
      this->accelGyro.getMotion9(&ax, &ay, &az, &gx, &gy, &gz, &mx, &my, &mz);
    }
    void debugGyro() {
      if (!this->accelGyroWorking) {
        Serial.println("[Environment] [MPU6050] [Debug] Not connected.");
        return;
      }
      Serial.print("[Environment] [MPU6050] [Debug] a/g/m:\t");
      Serial.print(ax);
      Serial.print("\t");
      Serial.print(ay);
      Serial.print("\t");
      Serial.print(az);
      Serial.print("\t");
      Serial.print(gx);
      Serial.print("\t");
      Serial.print(gy);
      Serial.print("\t");
      Serial.print(gz);
      Serial.print("\t");
      Serial.print(mx);
      Serial.print("\t");
      Serial.print(my);
      Serial.print("\t");
      Serial.println(mz);
    }
};

enum RadioProtocol {
  CAMERA, // [REQUEST]
  /*
     String:
        c:x____,y____******
           3     9
     Explain:
        x: [int 0-360] Camera X value,
        y: [int 0-360] Camera Y value,
  */
  ROTATION, // [REQUEST]
  /*
     String:
        r:a____************
           3
     Explain:
        a: [int 0-360] New motor angle
  */
  ACTION, // [REQUEST]
  /*
     String:
        a:d_,s____*********
           3  6
     Explain:
        f: [int 0-1] Forward or backward
        s: [int 0-?] Speed
  */
  STATUS, // [RESPONSE]
  /*
     String:
        a:s_***************
           3
     Explain:
        f: [int 0-1] Fail(0) or success(1)
  */
  OTHER
};

class RadioNetworking {
  private:
    SoftwareSerial radioSoftwareSerial;
    RH_RF95 rf95; //
    bool radioWorking = false;
  public:
    RadioNetworking(byte netReadPin, byte netWritePin) :
      radioSoftwareSerial(netReadPin, netWritePin), rf95(radioSoftwareSerial) {
    }
    void initialize() {
      Serial.println("[RadioNetworking] initializing...");
      Serial.println("[RadioNetworking] [Server] initializing LoRas RH_RF95 radio...");
      Serial.print("[RadioNetworking] [Server] initializing ");
      int retry = 0;
      bool success = false;
      while (!success) {
        success = this->rf95.init();
        retry++;
        if (retry > NETWORKING_RADIO_MAX_RETRY || success) {
          break;
        }
        Serial.print("FAILED (");
        Serial.print(retry);
        Serial.print("/");
        Serial.print(NETWORKING_RADIO_MAX_RETRY);
        Serial.print("), ");
        delay(100);
      }

      this->radioWorking = success;
      if (success) {
        Serial.println("READY");
      } else {
        Serial.println("GIVING UP");
      }
      Serial.println("[RadioNetworking] initialized.");
    }
    void startServer(float frequency) {
      this->rf95.setFrequency(frequency);
      Serial.println("[RadioNetworking] [Server] Listening...");
    }
    void handle(Engine* engine, CameraServo* cameraServo) {
      uint8_t buffer[RH_RF95_MAX_MESSAGE_LEN];
      uint8_t length = sizeof(buffer);

      //			Serial.print("[Arduino] [Server] Available: ");
      //			Serial.println(rf95.available());

      if (rf95.available()) {
        if (rf95.recv(buffer, &length)) {
          Serial.print("[RadioNetworking] [Server] Received: ");
          Serial.println((char*) buffer);

          bool success = true;

          char protocol = buffer[0];
          char* request = (char*) buffer;
          
          if (protocol == 'c') { // CAMERA
            STRING1 = 0x01; // x value
            STRING2 = 0x01; // y value

            for (int i = 0; i < 4; i++) {
              char xCharAt = request[3 + i];
              if (xCharAt != '_') {
                STRING1 += String(xCharAt);
              }
              char yCharAt = request[9 + i];
              if (yCharAt != '_') {
                STRING2 += String(yCharAt);
              }
            }

            Serial.print(F("[RadioNetworking] [Server] [Protocol: Camera] New value: x="));
            Serial.print(STRING1.substring(1));
            Serial.print(F(", y="));
            Serial.println(STRING2.substring(1));

            int xPosition = cameraServo->processRotationValue((word) STRING1.substring(1).toInt(), 20, 120);
            int yPosition = cameraServo->processRotationValue((word) STRING2.substring(1).toInt(), 80, 120);
            cameraServo->move(xPosition, yPosition);
          } else if (protocol == 'r') { // ROTATION
            STRING1 = 0x01; // angle value

            for (int i = 0; i < 4; i++) {
              char angleCharAt = request[3 + i];
              if (angleCharAt != '_') {
                STRING1 += String(angleCharAt);
              }
            }

            Serial.print(F("[RadioNetworking] [Server] [Protocol: Rotation] New value: angle="));
            Serial.println(STRING1.substring(1));

            engine->turn((word) STRING1.substring(1).toInt());
          } else if (protocol == 'a') { // ACTION
            STRING1 = 0x01; // direction value
            STRING2 = 0x01; // speed value

            STRING1 += String(request[3]);

            for (int i = 0; i < 4; i++) {
              char speedCharAt = request[6 + i];
              if (speedCharAt != '_') {
                STRING2 += String(speedCharAt);
              }
            }

            Serial.print(F("[RadioNetworking] [Server] [Protocol: Action] New value: direction="));
            Serial.print(STRING1.substring(1) == "0" ? "FORWARD" : "BACKWARD");
            Serial.print(F(", speed="));
            Serial.println(STRING2.substring(1));

            engine->move((byte) STRING2.substring(1).toInt(), STRING1.substring(1).toInt());
          } else { // UNKNOWN
            success = false;
            Serial.print(F("[RadioNetworking] [Server] [Protocol: Unknown] Unknown protocol identifier: "));
            Serial.println(protocol);
          }
          uint8_t reply[] = "s;_";
          reply[2] = success ? '1' : '0';
          this->rf95.send(reply, sizeof(reply));
          this->rf95.waitPacketSent();
          Serial.print(F("[RadioNetworking] [Server] [Protocol] Validating client with success: "));
          Serial.println(success ? "TRUE" : "FALSE");
        } else {
          Serial.println(F("[RadioNetworking] [Server] Failed receiving packet"));
        }
      }
    }
    void sendPacket(uint8_t* data, bool wait) {
      Serial.print("[RadioNetworking] [Server] Sending: ");
      Serial.println((char*) data);
      this->rf95.send(data, sizeof(data));
      if (wait) {
        this->rf95.waitPacketSent();
      }
    }
    bool isRadioWorking() {
      return this->radioWorking;
    }
    RH_RF95 getRf95() {
      return this->rf95;
    }
};

class Utils {
  public:
    void fillArray(uint8_t array[], String buffer, int offset) {
      for (int i = 0; i < int(buffer.length()); i++) {
        array[offset + i] = (char) buffer[i];
      }
    }
};

CoreSystem *coreSystem;
Engine *engine;
Environment *environment;
CameraServo *cameraServo;
RadioNetworking *radioNetworking;
Utils *utils;

void setup() {
  Wire.begin();
  Serial.begin(115200);

  Serial.println("[Arduino] woke up!");
  Serial.println("[Arduino] [Setup] starting...");

  coreSystem = new CoreSystem();
  engine = new Engine();
  environment = new Environment();
  cameraServo = new CameraServo();
  radioNetworking = new RadioNetworking(NETWORKING_RADIO_READ_PIN, NETWORKING_RADIO_WRITE_PIN);
  utils = new Utils();

  coreSystem->initialize();
  engine->initialize(ENGINE_CONTROLLER_PIN, ENGINE_DIRECTION_PIN, ENGINE_TURN_PIN, ENGINE_DEFAULT_SPEED);
  environment->initialize();
  cameraServo->initialize(CAMERA_X_PIN, CAMERA_Y_PIN);
  radioNetworking->initialize();
  radioNetworking->startServer(NETWORKING_RADIO_FREQUENCY);

  Serial.println("[Arduino] [Setup] finished.");
}

bool loopMessage = false;
void loop() {
  if (!loopMessage) {
    Serial.println("[Arduino] [Thread] First loop called!");
    loopMessage = true;
  }

  coreSystem->blinkLed();
  environment->updateMotion();
  //  environment->debugGyro();
  //cameraServo->moveFromGyro();

  radioNetworking->handle(engine, cameraServo);

  listenSerialInterface();
  delay(300);
}

void listenSerialInterface() {
  if (!SERIAL_CLI_ENABLED) {
    return;
  }
  if (Serial.available() != 0) {
    String line = "";
    while (Serial.available() != 0) {
      char charectere = Serial.read();
      if (charectere == ';') {
        break;
      }
      line = line + charectere;
    }

    if (line == "a") {
      //			engine->brake(true);
    } else if (line == "b") {
      //			engine->brake(false);
    } else if (line == "c") {
      //			engine->brakeDelay(ENGINE_BRAKE_DELAY);
    } else if (line == "d") {
      engine->move(1, 255);
      //engine->move();
    } else if (line == "e") {
      engine->move(0, 255);
    } else if (line == "f") {
      // engine->setSpeed(50);
    } else if (line == "g") {
      // engine->setSpeed(250);
    } else if (line == "h") {
      // engine->setSpeed(800);
    } else if (line == "i") {
      engine->turn(70);
    } else if (line == "j") {
      engine->turn(120);
    } else if (line == "k") {
      analogWrite(ENGINE_CONTROLLER_PIN, 255);
    } else if (line == "l") {
      analogWrite(ENGINE_CONTROLLER_PIN, 20);
    }
  }
}

