#include <LiquidCrystal.h>
#include <LedControl.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <EEPROM.h>
#include <math.h>

// lcd pins
LiquidCrystal lcd(9, 8, 7, 6, 5, 4); 

// matrix driver
LedControl lc(12, 11, 10, 1);          

// accelerometer object
Adafruit_MPU6050 mpu;

// pins
const int JOY_X = A0;   // joystick x axis
const int JOY_Y = A1;   // joystick y axis
const int JOY_SW = 2;   // joystick button

const int BUZZER = 13;        // buzzer pin
const int LCD_BACKLIGHT = 3;  // pwm pin for lcd brightness

// matrix sizes
const byte VIEW_SIZE = 8;    // physical matrix size
const byte MAP_SIZE = 16;   // virtual map size

// timing constants
const unsigned long JOY_REPEAT_MS = 150;   // input rate limit
const unsigned long BUTTON_DEBOUNCE_MS = 200;   // button debounce
const unsigned long BUTTON_LONG_MS = 2000;  // time for long press
const unsigned long IMU_READ_MS = 100;   // imu polling rate
const unsigned long INTRO_MS = 2000;  // intro duration
const unsigned long IMU_INIT_MS = 800;   // imu init message duration
const unsigned long HS_SCROLL_MS = 3000;  // highscore page flip speed
const unsigned long ABOUT_SCROLL_MS = 300;   // scrolling text speed
const unsigned long GAMEOVER_MS = 2000;  // game over screen duration
const unsigned long RESET_MSG_MS = 1000;  // reset confirmation duration
const unsigned long SOUND_TOGGLE_MS = 500;   // sound toggle feedback time
const unsigned long CALIB_MS = 500;   // calibration time

// blink timing
const unsigned long PLAYER_BLINK_MS = 150;  // player blink speed
const unsigned long EXIT_BLINK_MS = 700;  // exit blink speed

// input thresholds
const int   JOY_THRESHOLD = 300;    // joystick deadzone
const float IMU_TILT_THRESHOLD = 15.0f;  // unused for movement
const float IMU_UNDO_THRESHOLD = 30.0f;  // tilt angle for undo

// eeprom layout
const int  EEPROM_SETTINGS = 0;   // settings address
const int  EEPROM_HS = 50;  // highscore address
const byte SETTINGS_MAGIC = 42;  // check if eeprom is initialized
const byte HS_COUNT = 3;   // number of top scores
const byte NAME_LEN = 5;   // player name length
const int  HS_EMPTY = 9999;// default empty score

// menu sizes
const byte MENU_ITEMS = 5;
const byte SETTINGS_ITEMS = 3;

// game constants
const byte LAST_LEVEL = 3;

// brightness defaults
const byte DEFAULT_LCD_BRIGHT = 255;
const byte DEFAULT_MATRIX_BRIGHT = 2;

bool helpShown = false;
bool aboutShown = false;
bool hsShown = false;
bool reseShown = false;

// game states
enum GameState {
  STATE_IMU_INIT,
  STATE_INTRO,
  STATE_MENU,
  STATE_GAME,
  STATE_SETTINGS,
  STATE_EDIT_NAME,
  STATE_EDIT_SOUND,
  STATE_RESET_HS,
  STATE_SHOW_HS,
  STATE_ABOUT,
  STATE_HELP,
  STATE_GAMEOVER,
  STATE_WIN,
  STATE_CALIBRATING
};

GameState currentState = STATE_IMU_INIT; // start state

// hardware flags
bool imuAvailable = true;

// ui timers & flags
unsigned long imuInitStart = 0;
bool imuInitShown = false;

unsigned long introStart = 0;
bool introShown = false;

unsigned long calibStart = 0;
bool calibStarted = false;

unsigned long gameOverStart = 0;
byte gameOverScreen = 0;

unsigned long resetStart = 0;
bool resetShown= false;

unsigned long soundStart = 0;
bool soundShown = false;

unsigned long scrollTime = 0;
int scrollPos = 0;

int menuSelection = 0;
int settingsSelection = 0;
int nameEditPos = 0;

// highscore structure
struct HighScore {
  char name[NAME_LEN + 1];
  int  score;          // moves count
};

// settings structure
struct Settings {
  char playerName[NAME_LEN + 1];
  bool soundOn;
  byte lcdBrightness;
  byte matrixBrightness;
  byte initialized;    
};

Settings settings;

// win sound state machine
struct WinSound {
  bool          active;
  unsigned long startTime;
  byte          phase;
} winSound = { false, 0, 0 };

// helper to map logical coords to hardware matrix (rotation fix)
void setPixel(byte x, byte y, bool state) {
  if (x > 7 || y > 7) return;
  lc.setLed(0, 7 - y, 7 - x, state);
}

// imu controller (undo & calibration)
class IMUController {

  unsigned long lastReadTime = 0;
  float         pitchOffset  = 0.0f;
  float         rollOffset   = 0.0f;
  bool          initialized  = false;

public:

  // initialize mpu6050
  bool init() {
    if (!mpu.begin()) return false;
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
    initialized = true;
    return true;
  }

  // set current tilt as zero point
  void calibrate() {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);
    pitchOffset = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI;
    rollOffset  = atan2(a.acceleration.x, a.acceleration.z) * 180.0 / PI;
  }

  // check if tilted beyond threshold for undo
  bool detectUndoGesture() {
    if (!initialized) return false;

    static unsigned long lastUndoTime = 0;
    unsigned long now = millis();
    if (now - lastUndoTime < IMU_READ_MS) return false; // rate limit

    lastUndoTime = now;

    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    // calculate angles relative to calibration
    float pitch = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI - pitchOffset;
    float roll  = atan2(a.acceleration.x, a.acceleration.z) * 180.0 / PI - rollOffset;

    if (fabs(pitch) > IMU_UNDO_THRESHOLD || fabs(roll) > IMU_UNDO_THRESHOLD) {
      lastUndoTime = now + 500; // prevent double trigger
      return true;
    }
    return false;
  }

  bool isReady() { return initialized; }

} imu;

// input controller (joystick & button)
class InputController {

  unsigned long lastInputTime = 0;
  unsigned long lastButtonTime = 0;
  unsigned long buttonPressStart = 0;
  bool lastButtonState  = HIGH;
  bool longPressTriggered = false;

public:

  // center reading around 0
  int readX() { return analogRead(JOY_X) - 512; }
  int readY() { return -(analogRead(JOY_Y) - 512); }

  bool moved(int v) { return abs(v) > JOY_THRESHOLD; }

  // limit movement speed
  bool canProcessInput() {
    if (millis() - lastInputTime >= JOY_REPEAT_MS) {
      lastInputTime = millis();
      return true;
    }
    return false;
  }

  // read both axes
  bool readInput(int &dx, int &dy) {
    dx = readX();
    dy = readY();
    return (moved(dx) || moved(dy));
  }

  // detect single click
  bool buttonPressed() {
    unsigned long now = millis();
    if (now - lastButtonTime < BUTTON_DEBOUNCE_MS) return false;

    bool current = (digitalRead(JOY_SW) == LOW);

    if (current && lastButtonState == HIGH) {
      lastButtonState = current;
      lastButtonTime = now;
      buttonPressStart = now;
      longPressTriggered = false;
      return true;
    }

    lastButtonState = current;
    return false;
  }

  // detect hold
  bool buttonLongPress() {
    bool current = (digitalRead(JOY_SW) == LOW);
    unsigned long now = millis();

    if (current && !longPressTriggered) {
      if (now - buttonPressStart > BUTTON_LONG_MS) {
        longPressTriggered = true;
        return true;
      }
    }
    if (!current) longPressTriggered = false;
    return false;
  }

} input;

// audio system
struct ToneData {
  unsigned long startTime;
  int duration;
  bool active;
} currentTone = { 0, 0, false };

// non-blocking tone play
void playTone(int freq, int duration) {
  if (settings.soundOn) {
    tone(BUZZER, freq);
    currentTone.startTime = millis();
    currentTone.duration  = duration;
    currentTone.active = true;
  }
}

// stop tone when time is up
void updateTone() {
  if (currentTone.active && millis() - currentTone.startTime >= (unsigned long)currentTone.duration) {
    noTone(BUZZER);
    currentTone.active = false;
  }
}

// sound shortcuts
void soundMove()   { playTone(600, 25); }
void soundSelect() { playTone(900, 60); }
void soundUndo()   { playTone(500, 60); }
void soundWin()    { playTone(1200, 80); }

// init win sound sequence
void playWinSound() {
  if (!settings.soundOn) return;
  winSound.active = true;
  winSound.startTime = millis();
  winSound.phase = 0;
}

// process win melody
bool updateWinSound() {
  if (!winSound.active) return true;

  unsigned long elapsed = millis() - winSound.startTime;

  // play notes based on time elapsed
  if (elapsed < 100 && winSound.phase == 0) {
    tone(BUZZER, 800);
    winSound.phase = 1;
  } else if (elapsed >= 100 && elapsed < 220 && winSound.phase == 1) {
    noTone(BUZZER);
    winSound.phase = 2;
  } else if (elapsed >= 220 && elapsed < 320 && winSound.phase == 2) {
    tone(BUZZER, 1000);
    winSound.phase = 3;
  } else if (elapsed >= 320 && elapsed < 440 && winSound.phase == 3) {
    noTone(BUZZER);
    winSound.phase = 4;
  } else if (elapsed >= 440 && elapsed < 590 && winSound.phase == 4) {
    tone(BUZZER, 1200);
    winSound.phase = 5;
  } else if (elapsed >= 590 && winSound.phase == 5) {
    noTone(BUZZER);
    winSound.active = false;
    return true; // sequence finished
  }

  return false; // still playing
}

// settings & eeprom
void applySettings() {
  analogWrite(LCD_BACKLIGHT, DEFAULT_LCD_BRIGHT);
  lc.setIntensity(0, DEFAULT_MATRIX_BRIGHT);
}

void saveSettings() {
  EEPROM.put(EEPROM_SETTINGS, settings);
}

// load settings or set defaults
void loadSettings() {
  EEPROM.get(EEPROM_SETTINGS, settings);

  if (settings.initialized != SETTINGS_MAGIC) {
    strcpy(settings.playerName, "PLYR1");
    settings.soundOn = true;
    settings.lcdBrightness = DEFAULT_LCD_BRIGHT;
    settings.matrixBrightness = DEFAULT_MATRIX_BRIGHT;
    settings.initialized = SETTINGS_MAGIC;
    saveSettings();
  }

  settings.playerName[NAME_LEN] = '\0';
  applySettings();
}

void loadHighScores(HighScore scores[HS_COUNT]) {
  for (byte i = 0; i < HS_COUNT; i++) {
    EEPROM.get(EEPROM_HS + i * sizeof(HighScore), scores[i]);
    // check for invalid data
    if (scores[i].score < 0 || scores[i].score > HS_EMPTY) {
      strcpy(scores[i].name, "-----");
      scores[i].score = HS_EMPTY;
    }
    scores[i].name[NAME_LEN] = '\0';
  }
}

void saveHighScores(HighScore scores[HS_COUNT]) {
  for (byte i = 0; i < HS_COUNT; i++) {
    EEPROM.put(EEPROM_HS + i * sizeof(HighScore), scores[i]);
  }
}

void updateResetHS() {
  if (!reseShown) {
    reseShown = true;
    resetStart = millis();
  }

  if (input.buttonPressed()) {
    soundSelect();
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Reset done!");
    resetStart = millis();
  }

  if (millis() - resetStart > RESET_MSG_MS) {
    reseShown = false;
    currentState = STATE_SETTINGS;
  }
}


bool isNewHighScore(int score) {
  HighScore scores[HS_COUNT];
  loadHighScores(scores);
  return (score < scores[HS_COUNT - 1].score);
}

// insert score into sorted list
void addHighScore(int score) {
  HighScore scores[HS_COUNT];
  loadHighScores(scores);

  for (byte i = 0; i < HS_COUNT; i++) {
    if (score < scores[i].score) {
      // shift lower scores down
      for (byte j = HS_COUNT - 1; j > i; j--) {
        scores[j] = scores[j - 1];
      }
      strncpy(scores[i].name, settings.playerName, NAME_LEN);
      scores[i].name[NAME_LEN] = '\0';
      scores[i].score = score;
      break;
    }
  }

  saveHighScores(scores);
}

// game model (maze logic)
struct GameModel {

  bool wall[MAP_SIZE][MAP_SIZE];  // true = wall
  byte playerX, playerY;          // player position
  byte exitX, exitY;              // exit cell
  byte camX, camY;                // viewport top-left
  bool needsRedraw;
  byte currentLevel;
  int  moves;
  bool newHighScore;
  bool levelComplete;

  void clearMap() {
    for (byte y = 0; y < MAP_SIZE; y++)
      for (byte x = 0; x < MAP_SIZE; x++)
        wall[x][y] = false;
  }

  void init() {
    clearMap();
    moves         = 0;
    currentLevel  = 1;
    newHighScore  = false;
    levelComplete = false;
    loadLevel(currentLevel);
  }

  // simple hardcoded maze generation
  void loadLevel(byte level) {
    clearMap();
    levelComplete = false;
    moves         = 0;

    if (level == 1) {
      // level 1: border + corridor
      for (byte x = 0; x < 12; x++) {
        wall[x][0] = true;
        wall[x][7] = true;
      }
      for (byte y = 0; y < 8; y++) {
        wall[0][y] = true;
        wall[11][y] = true;
      }
      for (byte x = 2; x < 10; x++) {
        if (x != 6) wall[x][3] = true;
      }

      playerX = 2;
      playerY = 5;
      exitX   = 10;
      exitY   = 1;

    } else if (level == 2) {
      // level 2: larger area
      for (byte x = 1; x <= 13; x++) {
        wall[x][1]  = true;
        wall[x][10] = true;
      }
      for (byte y = 1; y <= 10; y++) {
        wall[1][y]  = true;
        wall[13][y] = true;
      }
      for (byte y = 3; y <= 9; y++) {
        if (y != 5) wall[5][y] = true;
      }
      for (byte x = 6; x <= 11; x++) {
        if (x != 8) wall[x][6] = true;
      }

      playerX = 3;
      playerY = 3;
      exitX = 11;
      exitY = 9;

    } else if (level == 3) {
      // level 3: complex
      for (byte i = 0; i <= 13; i++) {
        wall[i][0] = true;
        wall[i][13] = true;
        wall[0][i] = true;
        wall[13][i] = true;
      }

      wall[4][2] = true; wall[5][2] = true; wall[6][2] = true;
      wall[4][3] = true;
      wall[8][4] = true; wall[9][4] = true; wall[10][4] = true;
      wall[10][5] = true; wall[10][6] = true;
      wall[3][8] = true; wall[4][8] = true; wall[5][8] = true; wall[6][8] = true;
      wall[7][8] = true; wall[8][8] = true;

      playerX = 2;
      playerY = 2;
      exitX = 11;
      exitY = 11;

    } else {
      currentState = STATE_WIN;
      return;
    }

    // center camera
    camX = constrain(playerX - VIEW_SIZE / 2, 0, MAP_SIZE - VIEW_SIZE);
    camY = constrain(playerY - VIEW_SIZE / 2, 0, MAP_SIZE - VIEW_SIZE);
    needsRedraw = true;
    levelComplete = false;
  }

  bool isWall(byte x, byte y) {
    if (x >= MAP_SIZE || y >= MAP_SIZE) return true;
    return wall[x][y];
  }

  bool isExit(byte x, byte y) {
    return (x == exitX && y == exitY);
  }

  bool checkLevelComplete() {
    return (playerX == exitX && playerY == exitY);
  }

} game;

// undo history for single move
struct MoveHistory {
  byte prevPlayerX;
  byte prevPlayerY;
  bool hasUndo;
};

MoveHistory lastMove = {0, 0, false};

// matrix drawing helpers
void drawMatrixIcon(byte iconIndex) {
  const byte icons[5][8] = {
    {0x00, 0x18, 0x3C, 0x7E, 0x7E, 0x3C, 0x18, 0x00}, // play
    {0x18, 0x3C, 0x7E, 0x7E, 0x3C, 0x18, 0x7E, 0x00}, // trophy
    {0x18, 0x24, 0x7E, 0xFF, 0xFF, 0x7E, 0x24, 0x18}, // settings
    {0x3C, 0x42, 0x99, 0xA1, 0xA1, 0x99, 0x42, 0x3C}, // info
    {0x3C, 0x42, 0x81, 0x99, 0x18, 0x00, 0x18, 0x00}  // help
  };

  lc.clearDisplay(0);
  if (iconIndex >= 5) iconIndex = 0;

  for (byte row = 0; row < 8; row++) {
    for (byte col = 0; col < 8; col++) {
      bool pixelOn = (icons[iconIndex][row] >> (7 - col)) & 0x01;
      setPixel(col, row, pixelOn);
    }
  }
}

void drawViewport() {
  if (!game.needsRedraw) return;
  game.needsRedraw = false;

  lc.clearDisplay(0);

  unsigned long t = millis();
  bool playerBlink = ((t / PLAYER_BLINK_MS) % 2) == 0;
  bool exitBlink = ((t / EXIT_BLINK_MS) % 2) == 0;

  // draw visible walls
  for (byte y = 0; y < VIEW_SIZE; y++) {
    for (byte x = 0; x < VIEW_SIZE; x++) {
      byte vx = x + game.camX;
      byte vy = y + game.camY;
      if (game.isWall(vx, vy)) {
        setPixel(x, y, true);
      }
    }
  }

  // draw exit
  if (exitBlink) {
    int ex = game.exitX - game.camX;
    int ey = game.exitY - game.camY;
    if (ex >= 0 && ex < 8 && ey >= 0 && ey < 8) {
      setPixel(ex, ey, true);
    }
  }

  // draw player
  if (playerBlink) {
    int px = game.playerX - game.camX;
    int py = game.playerY - game.camY;
    if (px >= 0 && px < 8 && py >= 0 && py < 8) {
      setPixel(px, py, true);
    }
  }
}

// lcd renderer
class LCDRenderer {

public:

  void init() {
    lcd.begin(16, 2);
  }

  void renderMenu(int sel) {
    const char* menuText[MENU_ITEMS] = {
      "Start Game", "Highscore", "Settings", "About", "How to Play"
    };

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(">");
    lcd.print(menuText[sel]);
    lcd.setCursor(0, 1);
    byte next = (sel + 1) % MENU_ITEMS;
    lcd.print(" ");
    lcd.print(menuText[next]);
    drawMatrixIcon(sel);
  }

  void renderSettings(int sel) {
    const char* settingsText[SETTINGS_ITEMS] = {
      "Enter Name", "Sound", "Reset HS"
    };

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(">");
    lcd.print(settingsText[sel]);
    lcd.setCursor(0, 1);
    byte next = (sel + 1) % SETTINGS_ITEMS;
    lcd.print(" ");
    lcd.print(settingsText[next]);
  }

  void renderNameEdit() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Enter Name:");
    lcd.setCursor(0, 1);
    for (byte i = 0; i < NAME_LEN; i++) {
      if (i == nameEditPos) lcd.print("[");
      lcd.print(settings.playerName[i]);
      if (i == nameEditPos) lcd.print("]");
      else if (i < NAME_LEN - 1) lcd.print(" ");
    }
  }

  void renderSoundSettings() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Sound: ");
    lcd.print(settings.soundOn ? "ON" : "OFF");
    lcd.setCursor(0, 1);
    lcd.print("Press to toggle");
  }

  void renderResetHS() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Reset Highscore");
    lcd.setCursor(0, 1);
    lcd.print("Press to confirm");
  }

  void renderHighScore() {
    static byte page = 0;
    HighScore scores[HS_COUNT];
    loadHighScores(scores);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(scores[page].name);
    lcd.setCursor(8, 0);
    if (scores[page].score < HS_EMPTY) {
      lcd.print(scores[page].score);
      lcd.print("mv");
    } else {
      lcd.print("---");
    }

    lcd.setCursor(0, 1);
    byte next = (page + 1) % HS_COUNT;
    lcd.print(scores[next].name);
    lcd.setCursor(8, 1);
    if (scores[next].score < HS_EMPTY) {
      lcd.print(scores[next].score);
      lcd.print("mv");
    } else {
      lcd.print("---");
    }

    // auto scroll pages
    unsigned long now = millis();
    if (now - scrollTime > HS_SCROLL_MS) {
      page     = (page + 1) % HS_COUNT;
      scrollTime = now;
    }
  }

  void renderAbout() {
    const char aboutText[] = "Maze JOY+IMU - github.com/andreeam1234 - by Andreea   ";
    int len = strlen(aboutText);

    // scroll text
    unsigned long now = millis();
    if (now - scrollTime > ABOUT_SCROLL_MS) {
      scrollPos++;
      if (scrollPos > len) scrollPos = 0;
      scrollTime = now;
    }

    lcd.clear();
    lcd.setCursor(0, 0);
    for (byte i = 0; i < 16; i++) {
      int idx = (scrollPos + i) % len;
      lcd.print(aboutText[idx]);
    }
    lcd.setCursor(0, 1);
    if (imuAvailable) 
      lcd.print("JOY+UNDO ");
    else              
      lcd.print("JOY only ");
    lcd.print(settings.playerName);
  }

  void renderHowToPlay() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Move to blinking");
    lcd.setCursor(0, 1);
    lcd.print("Tilt=UNDO");
  }

  void renderHUD() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("L");
    lcd.print(game.currentLevel);
    lcd.print(" M:");
    lcd.print(game.moves);
    lcd.setCursor(0, 1);
    lcd.print(settings.playerName);
    lcd.setCursor(7, 1);
    if (imuAvailable) lcd.print(" UNDO");
    else              lcd.print(" JOY ");
  }

  void renderWin() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("YOU WIN!");
    lcd.setCursor(0, 1);
    lcd.print("Total:");
    lcd.print(game.moves);
    lcd.print("mv");

    lc.clearDisplay(0);
    // draw heart on matrix
    const byte heart[8] = {0x00, 0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18};
    for (byte row = 0; row < 8; row++) {
      for (byte col = 0; col < 8; col++) {
        bool pixelOn = (heart[row] >> (7 - col)) & 0x01;
        setPixel(col, row, pixelOn);
      }
    }
  }

};

LCDRenderer renderer;

// game controller (main logic)
class GameController {

public:

  void initHardware() {
    pinMode(JOY_SW, INPUT_PULLUP);
    pinMode(BUZZER, OUTPUT);
    pinMode(LCD_BACKLIGHT, OUTPUT);

    lc.shutdown(0, false);
    lc.setIntensity(0, DEFAULT_MATRIX_BRIGHT);
    lc.clearDisplay(0);
  }

  void updateCamera() {
    game.camX = constrain(game.playerX - VIEW_SIZE / 2, 0, MAP_SIZE - VIEW_SIZE);
    game.camY = constrain(game.playerY - VIEW_SIZE / 2, 0, MAP_SIZE - VIEW_SIZE);
  }

  bool tryMove(int newX, int newY) {
    if (game.isWall(newX, newY)) return false;

    // save state for UNDO
    lastMove.prevPlayerX = game.playerX;
    lastMove.prevPlayerY = game.playerY;
    lastMove.hasUndo = true;

    game.playerX = (byte)newX;
    game.playerY = (byte)newY;
    return true;
  }

  void undoLastMove() {
    if (!lastMove.hasUndo) return;

    game.playerX = lastMove.prevPlayerX;
    game.playerY = lastMove.prevPlayerY;

    if (game.moves > 0) game.moves--;

    lastMove.hasUndo = false;

    updateCamera();
    renderer.renderHUD();
    game.needsRedraw = true;
    soundUndo();
  }

  // menu logic
  void updateMenu() {
    if (!input.canProcessInput()) return;

    int y = input.readY();

    if (input.moved(y)) {
      if (y > 0) {
        menuSelection = (menuSelection - 1 + MENU_ITEMS) % MENU_ITEMS;
      } else {
        menuSelection = (menuSelection + 1) % MENU_ITEMS;
      }
      renderer.renderMenu(menuSelection);
      soundMove();
    }

    if (input.buttonPressed()) {
      soundSelect();
      switch (menuSelection) {
        case 0:
          currentState = STATE_GAME;
          game.init();
          lastMove.hasUndo = false;
          renderer.renderHUD();
          break;
        case 1:
          currentState = STATE_SHOW_HS;
          scrollTime   = millis();
          renderer.renderHighScore();
          break;
        case 2:
          currentState      = STATE_SETTINGS;
          settingsSelection = 0;
          renderer.renderSettings(0);
          break;
        case 3:
          currentState = STATE_ABOUT;
          scrollPos    = 0;
          scrollTime   = millis();
          renderer.renderAbout();
          break;
        case 4:
          currentState = STATE_HELP;
          renderer.renderHowToPlay();
          break;
      }
    }
  }

  // settings logic
  void updateSettings() {
    if (!input.canProcessInput()) return;

    int y = input.readY();

    if (input.moved(y)) {
      if (y > 0) {
        settingsSelection = (settingsSelection - 1 + SETTINGS_ITEMS) % SETTINGS_ITEMS;
      } else {
        settingsSelection = (settingsSelection + 1) % SETTINGS_ITEMS;
      }
      renderer.renderSettings(settingsSelection);
      soundMove();
    }

    if (input.buttonPressed()) {
      soundSelect();
      switch (settingsSelection) {
        case 0:
          currentState = STATE_EDIT_NAME;
          nameEditPos = 0;
          renderer.renderNameEdit();
          break;
        case 1:
          currentState = STATE_EDIT_SOUND;
          soundShown = false;
          renderer.renderSoundSettings();
          break;
        case 2:
          currentState = STATE_RESET_HS;
          resetShown = false;
          renderer.renderResetHS();
          break;
      }
    }

    // long press to exit settings
    if (input.buttonLongPress()) {
      soundUndo();
      currentState = STATE_MENU;
      renderer.renderMenu(0);
      saveSettings();
    }
  }

  void updateNameEdit() {
    if (!input.canProcessInput()) return;

    int dy = input.readY();
    int dx = input.readX();

    // change letter
    if (input.moved(dy)) {
      if (dy > 0) {
        settings.playerName[nameEditPos]++;
        if (settings.playerName[nameEditPos] > 'Z') {
          settings.playerName[nameEditPos] = 'A';
        }
      } else {
        settings.playerName[nameEditPos]--;
        if (settings.playerName[nameEditPos] < 'A') {
          settings.playerName[nameEditPos] = 'Z';
        }
      }
      renderer.renderNameEdit();
      soundMove();
    }

    // change position
    if (input.moved(dx)) {
      if (dx > 0) {
        nameEditPos = (nameEditPos + 1) % NAME_LEN;
      } else {
        nameEditPos = (nameEditPos - 1 + NAME_LEN) % NAME_LEN;
      }
      renderer.renderNameEdit();
      soundMove();
    }

    if (input.buttonPressed()) {
      soundSelect();
      saveSettings();
      currentState = STATE_SETTINGS;
      renderer.renderSettings(settingsSelection);
    }
  }

  void updateSoundSettings() {
    unsigned long now = millis();

    if (!soundShown) {
      renderer.renderSoundSettings();
      soundShown = true;
      soundStart = now;
    }

    if (input.buttonPressed()) {
      settings.soundOn = !settings.soundOn;
      renderer.renderSoundSettings();
      soundSelect();
      soundStart = now;
    }

    if (now - soundStart > SOUND_TOGGLE_MS) {
      saveSettings();
      currentState = STATE_SETTINGS;
      renderer.renderSettings(settingsSelection);
      soundShown = false;
    }
  }

  void updateResetHS() {
    unsigned long now = millis();

    if (!resetShown) {
      renderer.renderResetHS();
      resetShown = true;
      resetStart = now;
    }

    if (input.buttonPressed()) {
      soundSelect();
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Reset done!");
      resetStart = now;
    }

    if (now - resetStart > RESET_MSG_MS) {
      currentState = STATE_SETTINGS;
      renderer.renderSettings(settingsSelection);
      resetShown = false;
    }
  }

  // game loop
  void updateGame() {
    // check undo gesture
    if (imuAvailable && imu.detectUndoGesture()) {
      undoLastMove();
      drawViewport();
      return;
    }

    if (game.levelComplete) {
      if (updateWinSound()) {
        if (isNewHighScore(game.moves)) {
          addHighScore(game.moves);
          game.newHighScore = true;
        } else {
          game.newHighScore = false;
        }

        game.currentLevel++;
        if (game.currentLevel > LAST_LEVEL) {
          currentState = STATE_WIN;
          renderer.renderWin();
        } else {
          game.loadLevel(game.currentLevel);
          currentState    = STATE_GAMEOVER;
          gameOverScreen  = 0;
          gameOverStart   = millis();
        }
        game.levelComplete = false;
      }
      drawViewport();
      return;
    }

    // short press: exit
    if (input.buttonPressed()) {
      soundSelect();
      currentState   = STATE_MENU;
      menuSelection  = 0;
      renderer.renderMenu(0);
      lc.clearDisplay(0);
      return;
    }

    // long press: recalibrate
    if (input.buttonLongPress() && imuAvailable) {
      currentState   = STATE_CALIBRATING;
      calibStarted   = false;
      return;
    }

    if (!input.canProcessInput()) {
      drawViewport();
      return;
    }

    int dx = 0;
    int dy = 0;
    bool moved = false;

    if (input.readInput(dx, dy)) {
      int newX = game.playerX;
      int newY = game.playerY;

      // prioritize axis with larger movement
      if (abs(dx) > abs(dy)) {
        if (dx < 0) newX--;
        else if (dx > 0) newX++;
        moved = true;
      } else if (abs(dy) > 0) {
        if (dy < 0) newY--;
        else if (dy > 0) newY++;
        moved = true;
      }

      if (moved && tryMove(newX, newY)) {
        game.moves++;
        updateCamera();
        renderer.renderHUD();
        game.needsRedraw = true;
        soundMove();

        if (game.checkLevelComplete()) {
          game.levelComplete = true;
          playWinSound();
        }
      }
    }

    drawViewport();
  }

  void updateCalibrating() {
    unsigned long now = millis();

    if (!calibStarted) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Calibrating...");
      calibStart = now;
      calibStarted = true;
    }

    if (now - calibStart > CALIB_MS) {
      imu.calibrate();
      renderer.renderHUD();
      game.needsRedraw = true;
      currentState = STATE_GAME;
      calibStarted = false;
    }
  }

  void updateGameOver() {
    unsigned long now = millis();

    if (gameOverScreen == 0) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Level ");
      lcd.print(game.currentLevel - 1);
      lcd.print(" done!");
      lcd.setCursor(0, 1);
      lcd.print(game.moves);
      lcd.print(" moves");

      if (now - gameOverStart > GAMEOVER_MS || input.buttonPressed()) {
        gameOverScreen = 1;
        gameOverStart = now;
      }
    } 
    else {
      lcd.clear();
      lcd.setCursor(0, 0);
      if (game.newHighScore) {
        lcd.print("NEW HIGHSCORE!");
      } else {
        lcd.print("Score: ");
        lcd.print(game.moves);
      }
      lcd.setCursor(0, 1);
      lcd.print("Press button");

      if (input.buttonPressed()) {
        soundSelect();
        gameOverScreen = 0;
        currentState   = STATE_GAME;
        renderer.renderHUD();
      }
    }
  }

  // other screens
  void updateHighScore() {
    if (!hsShown) {
      renderer.renderHighScore();
      hsShown = true;
    }


    if (input.buttonPressed()) {
      soundUndo();
      hsShown = false;
      currentState = STATE_MENU;
      renderer.renderMenu(menuSelection);
    }
}
  void updateAbout() {
    if (!aboutShown) {
      renderer.renderAbout();
      aboutShown = true;
    }

    if (input.buttonPressed()) {
      soundUndo();
      aboutShown = false;
      currentState = STATE_MENU;
      renderer.renderMenu(menuSelection);
    }
}

  void updateHelp() {
    if (!helpShown) {
      renderer.renderHowToPlay();
      helpShown = true;
    }
    
    if (input.buttonPressed()) {
      soundUndo();
      currentState = STATE_MENU;
      helpShown = false;
      renderer.renderMenu(menuSelection);
    }
  }

  void updateWin() {
    renderer.renderWin();
    if (input.buttonPressed()) {
      soundSelect();
      currentState  = STATE_MENU;
      menuSelection = 0;
      renderer.renderMenu(0);
    }
  }

} gameController;

// state helpers
void updateIMUInit() {
  if (!imuInitShown) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Init IMU...");

    if (!imu.init()) {
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("IMU ERROR!");
      lcd.setCursor(0, 1);
      lcd.print("Using Joystick");
      imuAvailable = false;
    } else {
      imu.calibrate();
      lcd.setCursor(0, 1);
      lcd.print("IMU OK");
      imuAvailable = true;
    }

    imuInitStart = millis();
    imuInitShown = true;
  }

  unsigned long now = millis();
  if (now - imuInitStart >= IMU_INIT_MS) {
    currentState = STATE_INTRO;
    introShown = false;
  }
}

void updateIntro() {
  if (!introShown) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Maze on 8x8");
    lcd.setCursor(0, 1);
    lcd.print("JOYSTICK + IMU");
    introStart = millis();
    introShown = true;
  }

  if (millis() - introStart >= INTRO_MS) {
    currentState = STATE_MENU;
    menuSelection  = 0;
    renderer.renderMenu(0);
  }
}

void setup() {
  Serial.begin(115200);

  gameController.initHardware();
  renderer.init();
  loadSettings();

  currentState = STATE_IMU_INIT;
}

void loop() {
  updateTone();

  switch (currentState) {
    case STATE_IMU_INIT:
      updateIMUInit();
      break;
    case STATE_INTRO:
      updateIntro();
      break;
    case STATE_MENU:
      gameController.updateMenu();
      break;
    case STATE_GAME:
      gameController.updateGame();
      break;
    case STATE_SETTINGS:
      gameController.updateSettings();
      break;
    case STATE_EDIT_NAME:
      gameController.updateNameEdit();
      break;
    case STATE_EDIT_SOUND:
      gameController.updateSoundSettings();
      break;
    case STATE_RESET_HS:
      gameController.updateResetHS();
      break;
    case STATE_SHOW_HS:
      gameController.updateHighScore();
      break;
    case STATE_ABOUT:
      gameController.updateAbout();
      break;
    case STATE_HELP:
      gameController.updateHelp();
      break;
    case STATE_GAMEOVER:
      gameController.updateGameOver();
      break;
    case STATE_WIN:
      gameController.updateWin();
      break;
    case STATE_CALIBRATING:
      gameController.updateCalibrating();
      break;
  }
}