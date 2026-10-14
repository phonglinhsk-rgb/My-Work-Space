#include<Wire.h>
#include<LiquidCrystal_I2C.h>
#include<Servo.h>
#include<math.h>
#include<stdlib.h>
// keo xuong dong 365 test xem Ưire co ho tro gia tri getWireTimeoutFlag khong

#define LCDADDR 0x27
#define HIP_PIN 2
#define SHOULDER_PIN 3
#define ELBOW_PIN 4
#define ARM_2_PIN 5
#define ARM_1_PIN 6
#define LEFT_PIN 7
#define OK_PIN 8
#define RIGHT_PIN 9

LiquidCrystal_I2C LCD(LCDADDR, 20, 4);
Servo HIP;
Servo SHOULDER;
Servo ELBOW;
Servo ARM_1;
Servo ARM_2;

const bool RUN_SERVOS = false;
// test, chua cho chay that

const float RADIUS = 7.0;
const float FOREARM = 6.5;
const float WRIST = 2.0;
const float RAD_TO_DEGREE = 180.0 / PI;

const int SHOULDER_HOME_CMD = 55;
const int ELBOW_HOME_CMD = 55;       
const float SHOULDER_DIRECTION = 1; 
const float ELBOW_DIRECTION = 1;
const int HIP_STOP_CMD = 90;
const int HIP_FORWARD_CMD = 100;
const int HIP_BACKWARD_CMD = 80;
const float HIP_FORWARD_TIME = 2000.0;
const float HIP_BACKWARD_TIME = 2000.0;
const unsigned long ARM_TIME = 2000;
const unsigned long GRIP_TIME = 1000;

enum class HMI_STATE {
  PICK_MENU,
  ENTER_XYZ,
  MESSAGE,
  DONE
};
enum class SYSTEM_STATE {
  SETUP,
  HOME_WAIT,
  WAIT,
  HIP_RUNNING,
  HIP_WAIT,
  ARM_RUNNING,
  ARM_WAIT,
  GRIP_CLOSING,
  GRIP_OPENING,
  HOLD,
  STOPPED
};
HMI_STATE hmistate = HMI_STATE::DONE;
HMI_STATE afterMessage = HMI_STATE::PICK_MENU;
SYSTEM_STATE systemstate = SYSTEM_STATE::SETUP;

const char* MENU[] = {"SET NEW MOVE", "MOVE", "OPEN GRIP"};
const char KEYS[] = "123456789-0,V<";
const char AXIS[] = "XYZ";
int MENU_CHOICE = 0;
int KEY_CHOICE = 0;
int AXIS_CHOICE = 0;
float target[3] = {0, 0, 0};
float draft[3] = {0, 0, 0};
bool targetReady = false;
char input[8] = "";
int inputLength = 0;
bool updateLCD = true;
const char* messageLine1 = "";
const char* messageLine2 = "";
const char* mathError = "";
const char* stopReason = "";

struct ButtonState {
  bool lastRaw = HIGH;
  bool stable = HIGH;
  unsigned long lastChange = 0;
};
ButtonState leftButton;
ButtonState okButton;
ButtonState rightButton;

struct ROBOT_ANGLE {
  float hip;
  float shoulder;
  float elbow;
};
ROBOT_ANGLE targetAngle;
float hipPosition = 0;
float shoulderCurrent = SHOULDER_HOME_CMD;
float elbowCurrent = ELBOW_HOME_CMD;
float shoulderStart = SHOULDER_HOME_CMD;
float elbowStart = ELBOW_HOME_CMD;
float arm1Current = 90;
float arm2Current = 90;
float arm1Start = 90;
float arm2Start = 90;
bool gripperClosed = false;
unsigned long timescaling = 0;
unsigned long lastServoUpdate = 0;
unsigned long hipRunTime = 0;

void setline(int line) {
  LCD.setCursor(0, line);
  LCD.print("                    ");
  LCD.setCursor(0, line);
}
void showline(int line, const char* x) {
  setline(line);
  LCD.print(x);
}
void show(const char* x, const char* y) {
  showline(0, x);
  showline(1, y);
  setline(2);
  setline(3);
}
void initButtonState(ButtonState &button, uint8_t pin) {
  bool current = digitalRead(pin);
  button.lastRaw = current;
  button.stable = current;
  button.lastChange = millis();
}
bool buttonPressed(uint8_t pin, ButtonState &button) {
  bool current = digitalRead(pin);
  unsigned long now = millis();
  if (current != button.lastRaw) {
    button.lastRaw = current;
    button.lastChange = now;
  }
  if (now - button.lastChange >= 40) {
    if (current != button.stable) {
      button.stable = current;
      if (button.stable == LOW) {
        return true;
      }
    }
  }
  return false;
}
void setstate(SYSTEM_STATE nextState) {
  systemstate = nextState;
  timescaling = millis();
  lastServoUpdate = millis();
  updateLCD = true;
}
bool isRunning() {
  return systemstate == SYSTEM_STATE::HOME_WAIT ||
         systemstate == SYSTEM_STATE::HIP_RUNNING ||
         systemstate == SYSTEM_STATE::HIP_WAIT ||
         systemstate == SYSTEM_STATE::ARM_RUNNING ||
         systemstate == SYSTEM_STATE::ARM_WAIT ||
         systemstate == SYSTEM_STATE::GRIP_CLOSING ||
         systemstate == SYSTEM_STATE::GRIP_OPENING;
}
void stopRobot(const char* reason) {
  if (RUN_SERVOS) {
    HIP.write(HIP_STOP_CMD);
  }
  stopReason = reason;
  hmistate = HMI_STATE::DONE;
  setstate(SYSTEM_STATE::STOPPED);
}
void message(const char* line1, const char* line2, HMI_STATE nextHMI) {
  messageLine1 = line1;
  messageLine2 = line2;
  afterMessage = nextHMI;
  hmistate = HMI_STATE::MESSAGE;
  updateLCD = true;
}

void drawScreen() {
  updateLCD = false; 
  // xhi ve lai khi co thay doi, tranh LCD nhap nhay.

  if (hmistate == HMI_STATE::PICK_MENU) {
    showline(0, RUN_SERVOS ? "ROBOT ARM" : "TEST: NO MOTOR");
    for (int i = 0; i < 3; i++) {
      setline(i + 1);
      LCD.print(i == MENU_CHOICE ? ">" : " ");
      LCD.print(MENU[i]);
    }
    return;
  }
  if (hmistate == HMI_STATE::ENTER_XYZ) {
    for (int row = 0; row < 4; row++) {
      setline(row);
      for (int column = 0; column < 3; column++) {
        int key = row * 3 + column;
        LCD.print(key == KEY_CHOICE ? '[' : ' ');
        LCD.print(KEYS[key]);
        LCD.print(key == KEY_CHOICE ? ']' : ' ');
      }
      LCD.setCursor(10, row);
      if (row < 3) {
        LCD.print(AXIS[row]);
        LCD.print('=');
        if (row == AXIS_CHOICE) {
          for (int i = 0; i < inputLength; i++) {
            LCD.print(input[i] == '.' ? ',' : input[i]);
          }
          LCD.print('_');
        } else {
          LCD.print(draft[row], 2);
        }
      } else {
        LCD.print(KEY_CHOICE == 12 ? "[V] " : " V  ");
        LCD.print(KEY_CHOICE == 13 ? "[<]" : " < ");
      }
    }
    return;
  }
  if (hmistate == HMI_STATE::MESSAGE) {
    show(messageLine1, messageLine2);
    showline(3, "OK: BACK");
    return;
  }
  if (systemstate == SYSTEM_STATE::SETUP) {
    show("ALIGN HIP TO +Y", "OK: SET HOME");
    showline(2, "FOREARM HORIZONTAL");
    showline(3, RUN_SERVOS ? "MOTOR ENABLED" : "TEST: NO MOTOR");
    return;
  }
  if (systemstate == SYSTEM_STATE::STOPPED) {
    show("STOPPED", stopReason);
    showline(2, "ALIGN HOME + RESET");
    return;
  }
  show(RUN_SERVOS ? "ROBOT MOVING" : "TEST: NO MOTOR", "");
  switch (systemstate) {
    case SYSTEM_STATE::HOME_WAIT: showline(1, "SETTING HOME"); break;
    case SYSTEM_STATE::HIP_RUNNING: showline(1, "TURN HIP"); break;
    case SYSTEM_STATE::HIP_WAIT: showline(1, "HIP SETTLE"); break;
    case SYSTEM_STATE::ARM_RUNNING: showline(1, "MOVE SHOULDER/ELBOW"); break;
    case SYSTEM_STATE::ARM_WAIT: showline(1, "ARM SETTLE"); break;
    case SYSTEM_STATE::GRIP_CLOSING: showline(1, "CLOSE GRIP"); break;
    case SYSTEM_STATE::GRIP_OPENING: showline(1, "OPEN GRIP"); break;
    default: break;
  }
  showline(3, "OK: STOP");
}

bool calculateAngles(float X, float Y, float Z, ROBOT_ANGLE &angle) {
  if (!isfinite(X) || !isfinite(Y) || !isfinite(Z)) {
    mathError = "INVALID NUMBER";
    return false;
  }
  float A = sqrt(X * X + Y * Y);
  // mat phang toa do OAZ

  float D = sqrt(A * A + Z * Z); 
  // canh shoulder -> diem gap

  float L2 = sqrt(FOREARM * FOREARM + WRIST * WRIST);
  if (Z < 0 || D <= 2.4 || D > 13.33) {
    mathError = "CHECK D OR Z";
    return false;
  }
  float cosDelta = (RADIUS * RADIUS + D * D - L2 * L2) / (2 * RADIUS * D);
  float cosPhi = (RADIUS * RADIUS + L2 * L2 - D * D) / (2 * RADIUS * L2);
  
  // acos() chi nhan [-1,1]
  if (cosDelta < -1.0 || cosDelta > 1.0 ||
      cosPhi < -1.0 || cosPhi > 1.0) {
    mathError = "NO TRIANGLE";
    return false;
  }
  cosDelta = constrain(cosDelta, -1.0, 1.0);
  cosPhi = constrain(cosPhi, -1.0, 1.0);
  float delta = (acos(cosDelta) + atan2(Z, A)) * RAD_TO_DEGREE;
  float phi = acos(cosPhi) * RAD_TO_DEGREE;
  float alpha = atan2(WRIST, FOREARM) * RAD_TO_DEGREE;
  
  // phi la goc trong tam giac; elbowAngle la goc forearm so voi radius

  float elbowAngle = phi - 180.0 + alpha;
  if (phi > 150.0) {
    mathError = "PHI OVER 150";
    return false;
  }
  float shoulderCommand = SHOULDER_HOME_CMD + SHOULDER_DIRECTION * (delta - 55.0);
  float elbowCommand = ELBOW_HOME_CMD + ELBOW_DIRECTION * (elbowAngle - (-55.0));
  if (shoulderCommand < 0 || shoulderCommand > 150 ||
      elbowCommand < 0 || elbowCommand > 150) {
    mathError = "SERVO OUT OF RANGE";
    return false;
  }

  float epsilon = hipPosition;
  if (A > 0.0001) {
    epsilon = atan2(X, Y) * RAD_TO_DEGREE;
  }
  // chi ghi ket qua sau khi tat ca dieu kien hop le (chay tét)
  angle.hip = epsilon;
  angle.shoulder = shoulderCommand;
  angle.elbow = elbowCommand;
  Serial.print("A="); Serial.print(A);
  Serial.print(" D="); Serial.print(D);
  Serial.print(" delta="); Serial.print(delta);
  Serial.print(" phi="); Serial.print(phi);
  Serial.print(" elbowAngle="); Serial.println(elbowAngle);
  return true;
}


void startHome() {
  if (RUN_SERVOS) {
    HIP.attach(HIP_PIN);
    HIP.write(HIP_STOP_CMD);
    SHOULDER.attach(SHOULDER_PIN);
    ELBOW.attach(ELBOW_PIN);
    ARM_1.attach(ARM_1_PIN);
    ARM_2.attach(ARM_2_PIN);
    SHOULDER.write(SHOULDER_HOME_CMD);
    ELBOW.write(ELBOW_HOME_CMD);
    ARM_1.write(90);
    ARM_2.write(90);
  }
  hipPosition = 0;
  hmistate = HMI_STATE::DONE;
  setstate(SYSTEM_STATE::HOME_WAIT);
}
void startMove() {
  if (!targetReady) {
    message("SET XYZ FIRST", "CHOOSE SET NEW MOVE", HMI_STATE::PICK_MENU);
    return;
  }
  if (gripperClosed) {
    message("GRIP IS CLOSED", "OPEN GRIP FIRST", HMI_STATE::PICK_MENU);
    return;
  }
  if (!calculateAngles(target[0], target[1], target[2], targetAngle)) {
    message("INVALID TARGET", mathError, HMI_STATE::PICK_MENU);
    return;
  }
  shoulderStart = shoulderCurrent;
  elbowStart = elbowCurrent;
  hmistate = HMI_STATE::DONE;
  float hipChange = targetAngle.hip - hipPosition;
  if (fabs(hipChange) <= 0.2) {
    setstate(SYSTEM_STATE::HIP_WAIT);
    return;
  }
  int command;
  if (hipChange > 0) {
    command = HIP_FORWARD_CMD;
    hipRunTime = fabs(hipChange) * HIP_FORWARD_TIME / 360.0 + 0.5;
  } else {
    command = HIP_BACKWARD_CMD;
    hipRunTime = fabs(hipChange) * HIP_BACKWARD_TIME / 360.0 + 0.5;
  }
  if (hipRunTime == 0) {
    hipRunTime = 1;
  }
  setstate(SYSTEM_STATE::HIP_RUNNING);
  drawScreen();
#if defined(WIRE_HAS_TIMEOUT)
  if (Wire.getWireTimeoutFlag()) {
    Wire.clearWireTimeoutFlag();
    stopRobot("LCD I2C TIMEOUT");
    return;
  }
#endif
  timescaling = millis();
  if (RUN_SERVOS) {
    HIP.write(command);
  }
}
// test ham co san trong thu vien, tuy ban ide co the co hoac khong
void runHip() {
  if (millis() - timescaling >= hipRunTime) {
    if (RUN_SERVOS) {
      HIP.write(HIP_STOP_CMD);
    }
    hipPosition = targetAngle.hip;
    setstate(SYSTEM_STATE::HIP_WAIT);
  }
}
float movementProgress(unsigned long duration) {
  float u = (millis() - timescaling) / (float)duration;
  if (u > 1.0) {
    u = 1.0;
  }
  return u * u * (3.0 - 2.0 * u);
}
void runArm() {
  if (millis() - lastServoUpdate < 20) {
    return;
  }
  lastServoUpdate = millis();
  float progress = movementProgress(ARM_TIME);
  shoulderCurrent = shoulderStart + (targetAngle.shoulder - shoulderStart) * progress;
  elbowCurrent = elbowStart + (targetAngle.elbow - elbowStart) * progress;
  if (RUN_SERVOS) {
    SHOULDER.write((int)(shoulderCurrent + 0.5));
    ELBOW.write((int)(elbowCurrent + 0.5));
  }
  if (progress >= 1.0) {
    setstate(SYSTEM_STATE::ARM_WAIT);
  }
}
void startGrip(bool closing) {
  arm1Start = arm1Current;
  arm2Start = arm2Current;
  hmistate = HMI_STATE::DONE;
  if (closing) {
    setstate(SYSTEM_STATE::GRIP_CLOSING);
  } else {
    setstate(SYSTEM_STATE::GRIP_OPENING);
  }
}
void runGrip(bool closing) {
  if (millis() - lastServoUpdate < 20) {
    return;
  }
  lastServoUpdate = millis();
  int arm1Target = 90;
  int arm2Target = 90;
  if (closing) {
    arm1Target = 180;
    arm2Target = 0;
  }
  float progress = movementProgress(GRIP_TIME);
  arm1Current = arm1Start + (arm1Target - arm1Start) * progress;
  arm2Current = arm2Start + (arm2Target - arm2Start) * progress;
  if (RUN_SERVOS) {
    ARM_1.write((int)(arm1Current + 0.5));
    ARM_2.write((int)(arm2Current + 0.5));
  }
  if (progress >= 1.0) {
    gripperClosed = closing;
    hmistate = HMI_STATE::PICK_MENU;
    if (closing) {
      setstate(SYSTEM_STATE::HOLD);
    } else {
      setstate(SYSTEM_STATE::WAIT);
    }
  }
}

void clearInput() {
  inputLength = 0;
  input[0] = '\0';
}
void selectKey() {
  char key = KEYS[KEY_CHOICE];
  if (key == '<') {
    if (inputLength > 0) {
      inputLength--;
      input[inputLength] = '\0';
    } else if (AXIS_CHOICE > 0) {
      AXIS_CHOICE--;
    } else {
      hmistate = HMI_STATE::PICK_MENU;
    }
    return;
  }
  int digits = 0;
  int decimals = 0;
  bool hasPoint = false;
  for (int i = 0; i < inputLength; i++) {
    if (input[i] == '.') {
      hasPoint = true;
    } else if (input[i] >= '0' && input[i] <= '9') {
      digits++;
      if (hasPoint) {
        decimals++;
      }
    }
  }
  if (key == 'V') {
    if (digits == 0) {
      message("ENTER A NUMBER", "ZERO: SELECT 0", HMI_STATE::ENTER_XYZ);
      return;
    }
    draft[AXIS_CHOICE] = atof(input);
    // ham co san cua stdlib

    if (AXIS_CHOICE < 2) {
      AXIS_CHOICE++;
      clearInput();
      KEY_CHOICE = 0;
    } else if (calculateAngles(draft[0], draft[1], draft[2], targetAngle)) {
      for (int i = 0; i < 3; i++) {
        target[i] = draft[i];
      }
      targetReady = true;
      hmistate = HMI_STATE::PICK_MENU;
      MENU_CHOICE = 1;
    } else {
      message("INVALID TARGET", mathError, HMI_STATE::ENTER_XYZ);
    }
    return;
  }
  if (key == '-') {
    if (inputLength != 0) {
      return;
    }
  } else if (key == ',') {
    if (hasPoint) {
      return;
    }
    key = '.'; //
  } else if (digits >= 4 || decimals >= 2) {
    return;
  }
  if (inputLength < 7) {
    input[inputLength] = key;
    inputLength++;
    input[inputLength] = '\0';
  }
}
void left() {
  switch (hmistate) {
    case HMI_STATE::PICK_MENU:
      if (MENU_CHOICE <= 0) {
        MENU_CHOICE = 2;
      } else {
        MENU_CHOICE--;
      }
      break;
    case HMI_STATE::ENTER_XYZ:
      if (KEY_CHOICE <= 0) {
        KEY_CHOICE = 13;
      } else {
        KEY_CHOICE--;
      }
      break;
    default: break;
  }
  updateLCD = true;
}
void right() {
  switch (hmistate) {
    case HMI_STATE::PICK_MENU:
      if (MENU_CHOICE >= 2) {
        MENU_CHOICE = 0;
      } else {
        MENU_CHOICE++;
      }
      break;
    case HMI_STATE::ENTER_XYZ:
      if (KEY_CHOICE >= 13) {
        KEY_CHOICE = 0;
      } else {
        KEY_CHOICE++;
      }
      break;
    default: break;
  }
  updateLCD = true;
}
void ok() {
  if (isRunning()) {
    stopRobot("OK PRESSED");
    return;
  }
  if (systemstate == SYSTEM_STATE::STOPPED) {
    return;
  }
  if (systemstate == SYSTEM_STATE::SETUP) {
    startHome();
    return;
  }
  switch (hmistate) {
    case HMI_STATE::PICK_MENU:
      if (MENU_CHOICE == 0) {
        for (int i = 0; i < 3; i++) {
          draft[i] = 0;
        }
        AXIS_CHOICE = 0;
        KEY_CHOICE = 0;
        clearInput();
        hmistate = HMI_STATE::ENTER_XYZ;
      } else if (MENU_CHOICE == 1) {
        startMove();
      } else {
        startGrip(false);
      }
      break;
    case HMI_STATE::ENTER_XYZ:
      selectKey();
      break;
    case HMI_STATE::MESSAGE:
      hmistate = afterMessage;
      break;
    default: break;
  }
  if (systemstate != SYSTEM_STATE::HIP_RUNNING) {
    updateLCD = true;
  }
}
void button() {
  bool leftPressed = buttonPressed(LEFT_PIN, leftButton);
  bool okPressed = buttonPressed(OK_PIN, okButton);
  bool rightPressed = buttonPressed(RIGHT_PIN, rightButton);
  if (okPressed) {
    ok();
    return;
  }
  if (isRunning() || systemstate == SYSTEM_STATE::STOPPED) {
    return;
  }
  if (leftPressed) {
    left();
    return;
  }
  if (rightPressed) {
    right();
  }
}

void setup() {
  Serial.begin(9600);
  pinMode(LEFT_PIN, INPUT_PULLUP);
  pinMode(OK_PIN, INPUT_PULLUP);
  pinMode(RIGHT_PIN, INPUT_PULLUP);
  initButtonState(leftButton, LEFT_PIN);
  initButtonState(okButton, OK_PIN);
  initButtonState(rightButton, RIGHT_PIN);
  Wire.begin();
#if defined(WIRE_HAS_TIMEOUT)
  Wire.setWireTimeout(3000, true);
#endif
  LCD.init();
  LCD.backlight();
  drawScreen();
}
void loop() {
#if defined(WIRE_HAS_TIMEOUT)
  if (Wire.getWireTimeoutFlag()) {
    Wire.clearWireTimeoutFlag();
    if (systemstate != SYSTEM_STATE::STOPPED) {
      stopRobot("LCD I2C TIMEOUT");
    }
  }
#endif
  button();
  switch (systemstate) {
    case SYSTEM_STATE::HOME_WAIT:
      if (millis() - timescaling >= 1200) {
        hmistate = HMI_STATE::PICK_MENU;
        setstate(SYSTEM_STATE::WAIT);
      }
      break;
    case SYSTEM_STATE::HIP_RUNNING:
      runHip();
      break;
    case SYSTEM_STATE::HIP_WAIT:
      if (millis() - timescaling >= 200) {
        setstate(SYSTEM_STATE::ARM_RUNNING);
      }
      break;
    case SYSTEM_STATE::ARM_RUNNING:
      runArm();
      break;
    case SYSTEM_STATE::ARM_WAIT:
      if (millis() - timescaling >= 300) {
        startGrip(true);
      }
      break;
    case SYSTEM_STATE::GRIP_CLOSING:
      runGrip(true);
      break;
    case SYSTEM_STATE::GRIP_OPENING:
      runGrip(false);
      break;
    case SYSTEM_STATE::SETUP:
    case SYSTEM_STATE::WAIT:
    case SYSTEM_STATE::HOLD:
    case SYSTEM_STATE::STOPPED:
      break;
  }
  if (updateLCD && systemstate != SYSTEM_STATE::HIP_RUNNING) {
    drawScreen();
  }
}
