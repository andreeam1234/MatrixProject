#include <LiquidCrystal.h>
#include <LedControl.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <EEPROM.h>
#include <math.h>

const byte PIN_LCD_RS = 9;
const byte PIN_LCD_EN = 8;
const byte PIN_LCD_D4 = 7;
const byte PIN_LCD_D5 = 6;
const byte PIN_LCD_D6 = 5;
const byte PIN_LCD_D7 = 4;

const byte PIN_MATRIX_DIN = 12;
const byte PIN_MATRIX_CLK = 11;
const byte PIN_MATRIX_CS = 10;
const byte MATRIX_COUNT = 1;

LiquidCrystal lcd(PIN_LCD_RS, PIN_LCD_EN, PIN_LCD_D4, PIN_LCD_D5, PIN_LCD_D6, PIN_LCD_D7);
LedControl lc(PIN_MATRIX_DIN, PIN_MATRIX_CLK, PIN_MATRIX_CS, MATRIX_COUNT);

Adafruit_MPU6050 mpu;                     // MPU6050 sensor object

const int JOY_X = A0;                     // joystick X axis (left-right)
const int JOY_Y = A1;                     // joystick Y axis (up-down)
const int JOY_SW = 2;                     // joystick button (LOW = pressed)

const int BUZZER = 13;                    // buzzer output pin
const int LCD_BACKLIGHT = 3;              // LCD backlight PWM pin (0–255)

const byte VIEW_SIZE = 8;                 // visible matrix size (8x8)
const byte MAP_SIZE = 16;                // full maze size (16x16)

// ====================== TIMING CONSTANTS ===================
const unsigned long JOY_REPEAT_MS = 150;   // joystick repeat delay (ms)
const unsigned long BUTTON_DEBOUNCE_MS = 200;   // button debounce time (ms)
const unsigned long BUTTON_LONG_MS = 2000;  // long press duration (ms)
const unsigned long INTRO_MS = 2000;  // intro screen duration (ms)
const unsigned long IMU_INIT_MS = 800;   // IMU init screen duration (ms)
const unsigned long HS_SCROLL_MS = 2500;  // highscore scroll interval (ms)
const unsigned long ABOUT_SCROLL_MS = 300;   // about text scroll speed (ms)
const unsigned long TRANSITION_MS = 1500;  // level transition delay (ms)
const unsigned long RESPAWN_MS = 1000;  // respawn delay (ms)

const unsigned long ENEMY_SPEED_1 = 800;        // enemy speed (levels 1–2)
const unsigned long ENEMY_SPEED_3 = 400;        // enemy speed (level 3)
const unsigned long FREEZE_TIME = 3000;       // enemy freeze duration (ms)

const int   JOY_THRESHOLD  = 300;                // joystick movement threshold
const float SHAKE_THRESHOLD = 15.0f;             // IMU shake detection threshold

// ====================== EEPROM LAYOUT ======================
const int  EEPROM_SETTINGS = 0;                  // settings EEPROM address
const int  EEPROM_HS = 50;                 // highscores EEPROM address
const byte SETTINGS_MAGIC = 99;                 // EEPROM validation value

const byte HS_COUNT = 3;                         // number of highscores stored
const byte NAME_LEN = 5;                         // player name length
const int  HS_EMPTY = 9999;                      // empty highscore marker

const byte MENU_ITEMS = 5;                   // main menu items
const byte SETTINGS_ITEMS = 6;                   // settings menu items

const byte LAST_LEVEL = 3;                       // final game level
const byte MAX_LIVES = 3;                        // player starting lives

const byte DEFAULT_LCD_BRIGHT = 200;           // default LCD brightness
const byte DEFAULT_MATRIX_BRIGHT = 5;             // default matrix brightness


// ====================== STATE MACHINE ======================
// enum defines all possible game states 
enum GameState {
  STATE_IMU_INIT,         // 0: checking if motion sensor works
  STATE_INTRO,            // 1: showing game title
  STATE_MENU,             // 2: main menu (start, highscore, settings, etc)
  STATE_GAME,             // 3: actively playing the game
  STATE_SETTINGS,         // 4: in settings menu
  STATE_HIGHSCORE,        // 5: viewing highscores
  STATE_ABOUT,            // 6: viewing credits/info
  STATE_HELP,             // 7: viewing instructions
  STATE_GAMEOVER,         // 8: player died and ran out of lives
  STATE_WIN,              // 9: player beat all 3 levels
  STATE_WAIT_LEVEL_NEXT,  // 10: brief pause before loading next level
  STATE_WAIT_RESPAWN      // 11: brief pause before respawning after death
};

// NEW: enum for settings sub-states (makes settings menu cleaner)
enum SettingsState {
  SET_MENU,           // 0: browsing settings menu
  SET_NAME,           // 1: editing player name
  SET_LCD,            // 2: adjusting lcd brightness
  SET_MATRIX,         // 3: adjusting matrix brightness
  SET_SOUND_CONFIRM,  // 4: showing sound on/off confirmation
  SET_RESET_CONFIRM,  // 5: confirming highscore reset
  SET_RESET_DONE 
};

SettingsState settingsState = SET_MENU;     // current settings sub-state
GameState currentState = STATE_IMU_INIT;    // current game state (starts with IMU init)

bool imuAvailable = true;                   // true if IMU initialized successfully

unsigned long imuInitStart = 0;           // IMU initialization start time (ms)
unsigned long introStart = 0;           // intro screen start time (ms)
unsigned long scrollTime = 0;           // scroll animation timing (ms)
unsigned long gameOverStart = 0;           // game over screen start time (ms)
unsigned long stateTimer = 0;           // generic state transition timer (ms)
unsigned long lastEnemyMove = 0;           // last enemy movement time (ms)

bool imuInitShown = false;                 // IMU init screen already shown
bool introShown = false;                 // intro screen already shown

int  scrollPos = 0;                     // scrolling text index
int  menuSelection = 0;                     // selected main menu item (0–4)
int  settingsSelection = 0;                 // selected settings menu item

byte nameEditPos   = 0;                     // selected character index when editing name

bool lastGameNewHighScore = false;           // true if last game set new highscore
bool enemiesFrozen = false;           // true while enemies are frozen
unsigned long freezeStart = 0;               // enemy freeze start time (ms)
unsigned long resetDoneStart = 0;
unsigned long introAnimTimer = 0; // intro animation timing

byte introRow = 0; // current animated row on matrix (0–7)

// string stored in program memory 
const char aboutText[] PROGMEM = "Maze Game - by Andreea - github.com/andreeam1234  ";

// ====================== DATA STRUCTURES ====================
struct HighScore { 
  char name[NAME_LEN + 1]; 
  int  score;               // number of moves (lower is better)
};

struct Settings {
  char playerName[NAME_LEN + 1];  // player's 5-char name
  bool soundOn;                    // true = sound enabled, false = muted
  byte lcdBrightness;              // 0-255, higher = brighter backlight
  byte matrixBrightness;           // 0-15, higher = brighter leds
  byte initialized;                // must equal SETTINGS_MAGIC (99) if valid
};
// create a global instance of Settings struct
Settings settings;

// struct holding enemy position (x,y coordinates)
struct Enemy { 
  byte x, y;  // position on 16x16 map
};

// struct holding all game state data
struct GameModel {
  // wall[x][y] = true means there's a wall at (x,y)
  bool wall[MAP_SIZE][MAP_SIZE];
  
  byte playerX, playerY;  // player position 
  byte exitX, exitY;      // exit door position
  byte keyX, keyY;        // key position
  bool hasKey;            // true if player collected the key
  byte camX, camY;        // top-left corner of 8x8 camera viewport
  byte currentLevel;      // 1, 2, or 3
  int  moves;             // total moves this game (score)
  byte lives;             // remaining lives (0-3)
  Enemy enemy;            // enemy position

  // function to set all walls to false 
  void clearMap() {   
    for (byte y = 0; y < MAP_SIZE; y++)      
      for (byte x = 0; x < MAP_SIZE; x++)    
        wall[x][y] = false;                  // set to false (no wall)
  }

  // function to start a new game
  void init() {
    moves = 0;              // reset move counter
    lives = MAX_LIVES;      // give player 3 lives
    currentLevel = 1;       // start at level 1
    loadLevel(currentLevel); // load level 1 layout
  }

  // function to load a specific level's layout
  void loadLevel(byte level) {
    clearMap();       // remove all existing walls
    hasKey = false;   // player doesn't have key yet
    
    // create border walls around entire map
    for (byte i = 0; i < MAP_SIZE; i++) {
      wall[i][0] = true;              // top wall (y=0)
      wall[i][MAP_SIZE-1] = true;     // bottom wall (y=15)
      wall[0][i] = true;              // left wall (x=0)
      wall[MAP_SIZE-1][i] = true;     // right wall (x=15)
    }
    
    // level-specific layouts
    if (level == 1) {
      // create horizontal wall from x=4 to x=11 at y=8
      for (byte i = 4; i < 12; i++)  
        wall[i][8] = true;
      
      // set starting positions
      playerX = 2; playerY = 2;       // player starts top-left
      exitX = 14; exitY = 13;         // exit at bottom-right
      keyX = 8; keyY = 4;             // key at top-middle
      enemy.x = 12; enemy.y = 12;     // enemy at bottom-right
    } 
    else if (level == 2) {
      // create two vertical walls at x=5 and x=10
      for (byte y = 2; y < 14; y++) {     
        wall[5][y] = true;                // left vertical wall
        wall[10][y] = true;               // right vertical wall
      }
      // create gaps in the walls
      wall[5][10] = false;  // gap in left wall at y=10
      wall[10][5] = false;  // gap in right wall at y=5
      
      playerX = 2; playerY = 7;
      exitX = 14; exitY = 7;
      keyX = 13; keyY = 2;
      enemy.x = 7; enemy.y = 7;
    }
    else if (level == 3) {
      // create grid pattern: walls at every even coordinate
      for (byte x = 2; x < 14; x += 2)     
        for (byte y = 2; y < 14; y += 2)   
          wall[x][y] = true;               // checkerboard pattern
      
      playerX = 1; playerY = 1;
      exitX = 14; exitY = 14;
      keyX = 14; keyY = 1;
      enemy.x = 7; enemy.y = 7;
    }
    updateCamera();  // center camera on player
  }

  // function to check if a position has a wall
  bool isWall(byte x, byte y) { 
    // if coordinates out of bounds, treat as wall
    if (x >= MAP_SIZE || y >= MAP_SIZE) return true;
    // otherwise return the wall array value
    return wall[x][y]; 
  }
  
  // function to update camera position to follow player
  void updateCamera() {
    // try to center camera on player: playerX - 4 would center an 8x8 view
    // but constrain keeps camera within valid bounds
    // if player at x=2: camX = constrain(2-4, 0, 8) = constrain(-2, 0, 8) = 0
    // if player at x=8: camX = constrain(8-4, 0, 8) = constrain(4, 0, 8) = 4
    camX = constrain(playerX - VIEW_SIZE/2, 0, MAP_SIZE - VIEW_SIZE);
    camY = constrain(playerY - VIEW_SIZE/2, 0, MAP_SIZE - VIEW_SIZE);
  }
} game;  

// ====================== IMU CONTROLLER =====================
class IMUController {
  bool initialized = false;  //has init() succeeded?
  
public:
  // function to initialize the mpu6050 sensor
  bool init() {
    // try to start communication with sensor
    if (!mpu.begin()) return false;  // sensor not found, return failure
    
    // configure sensor settings
    mpu.setAccelerometerRange(MPU6050_RANGE_8_G);      // measure up to 8g
    mpu.setGyroRange(MPU6050_RANGE_500_DEG);           // 500 degrees/sec
    mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);        // smooth readings
    
    initialized = true;   // mark as  initialized
    return true;          // return success
  }
  
  // function to detect if device is being shaken
  bool detectShake() {
    if (!initialized) return false;  // can't detect if sensor not working
    
    // static variable keeps its value between function calls
    static unsigned long lastRead = 0;
    unsigned long now = millis();
    
    // only check sensor every 100ms (throttle to avoid excessive readings)
    if (now - lastRead < 100) return false;
    lastRead = now;
    
    // read sensor data into event structures
    sensors_event_t a, g, temp;  // a=accelerometer, g=gyro, temp=temperature
    mpu.getEvent(&a, &g, &temp);
    
    // calculate magnitude of x and y acceleration (ignoring z/vertical)
    // sqrt(x^2 + y^2) gives distance from origin in 2d plane
    // if device shaken sideways, x or y will spike, increasing total
    float total = sqrt(a.acceleration.x * a.acceleration.x + a.acceleration.y * a.acceleration.y);
    
    // compare total acceleration to threshold
    return (total > SHAKE_THRESHOLD);  // true if shake detected
  }
} imu;  

// ====================== INPUT CONTROLLER ===================
// class handling all joystick and button input
class InputController {
  // variables for timing and debouncing
  unsigned long lastInputTime = 0;     // when we last processed joystick
  unsigned long lastButtonTime = 0;    // when button last changed state
  unsigned long buttonPressTime = 0;   // when button was first pressed down
  bool lastButtonState = HIGH;         // previous button state (HIGH=released)
  bool longPressSent = false;          // have we sent long press event yet?
  
public:
  // read x-axis: analogRead returns 0-1023, center is ~512
  // negate to make right positive, left negative
  int readX() { return -(analogRead(JOY_X) - 512); }
  
  // read y-axis: analogRead returns 0-1023, center is ~512
  // subtract 512 to get -512 to +511 range
  // negate to make up positive, down negative
  int readY() { return -(analogRead(JOY_Y) - 512); }
  
  // check if a value exceeds threshold (is joystick pushed enough?)
  bool moved(int v) { return abs(v) > JOY_THRESHOLD; }
  
  // check if enough time has passed to process new input
  bool canProcess() {
    // if 150ms have passed since last input
    if (millis() - lastInputTime >= JOY_REPEAT_MS) {
      lastInputTime = millis();  // update timestamp
      return true;               // allow processing
    }
    return false;  // too soon, don't process yet
  }
  
  // read both joystick axes at once (by reference: changes dx and dy)
  bool readAxes(int &dx, int &dy) { 
    dx = readX();  // store x value in dx variable
    dy = readY();  // store y value in dy variable
    // return true if either axis moved significantly
    return (moved(dx) || moved(dy)); 
  }
  
  // check for button press (with debouncing to filter noise)
  bool buttonPressed() {
    unsigned long now = millis();
    
    // if less than 200ms since last button event, ignore (debouncing)
    if (now - lastButtonTime < BUTTON_DEBOUNCE_MS) return false;
    
    // read current button state (LOW=pressed, HIGH=released)
    bool current = (digitalRead(JOY_SW) == LOW);
    
    // detect falling edge: button just transitioned from released to pressed
    if (current && lastButtonState == HIGH) {
      lastButtonState = current;     // save new state
      lastButtonTime = now;          // save when it changed
      buttonPressTime = now;         // save when press started
      longPressSent = false;         // reset long press flag
      return true;                   // button was just pressed
    }
    
    lastButtonState = current;  // update saved state
    return false;               // no press detected
  }
  
  // check for long press (button held for 2+ seconds)
  bool buttonLongPress() {
    // read current button state
    bool current = (digitalRead(JOY_SW) == LOW);
    unsigned long now = millis();
    
    // if button currently pressed and we haven't sent long press yet
    if (current && !longPressSent) {
      // calculate how long button has been held
      // if held for more than 2000ms
      if (now - buttonPressTime > BUTTON_LONG_MS) {
        longPressSent = true;  // mark as sent (only send once)
        return true;           // long press detected
      }
    }
    
    // if button released, reset flag for next press
    if (!current) longPressSent = false;
    
    return false;  // no long press
  }
} input;  

// ====================== AUDIO SYSTEM ======================
struct ToneData { 
  unsigned long startTime;  // millis() when tone started
  int duration;             // how many ms to play
  bool active;              // is a tone currently playing?
} currentTone = {0, 0, false};  // initialize all to 0/false

// function to start playing a tone
void playTone(int freq, int duration) {
  if (settings.soundOn) {           // only if sound enabled in settings
    tone(BUZZER, freq);              // start buzzer at frequency
    currentTone.startTime = millis(); // record start time
    currentTone.duration = duration;  // record how long to play
    currentTone.active = true;        // mark as active
  }
}

// function to stop tone if duration expired
void updateTone() {
  // if tone is playing
  if (currentTone.active) {
    // calculate how long it's been playing
    unsigned long elapsed = millis() - currentTone.startTime;
    // if duration exceeded
    if (elapsed >= (unsigned long)currentTone.duration) {
      noTone(BUZZER);           // stop the buzzer
      currentTone.active = false; // mark as inactive
    }
  }
}

// convenience functions for different sound effects
void soundMove() { playTone(600, 25); }      // quick beep (600Hz, 25ms)
void soundSelect() { playTone(900, 60); }    // menu selection (900Hz, 60ms)
void soundUndo() { playTone(500, 60); }      // cancel/back (500Hz, 60ms)
void soundWin() { playTone(1200, 80); }      // happy beep (1200Hz, 80ms)
void soundFreeze() { playTone(1500, 200); }  // freeze sound (1500Hz, 200ms)
void soundHit() { playTone(200, 500); }      // low thud (200Hz, 500ms)
void soundKey() { playTone(1000, 200); }     // key pickup (1000Hz, 200ms)

// ====================== EEPROM & SETTINGS ==================
// function to apply settings to hardware
void applySettings() { 
  // set lcd backlight brightness (0-255 pwm value)
  analogWrite(LCD_BACKLIGHT, settings.lcdBrightness);
  
  // set matrix brightness (0-15 value)
  lc.setIntensity(0, settings.matrixBrightness); 
}

// function to save settings struct to eeprom
void saveSettings() { 
  // EEPROM.put() writes entire struct starting at address 0
  EEPROM.put(EEPROM_SETTINGS, settings); 
}

// function to load settings from eeprom (or create defaults)
void loadSettings() {
  // read settings struct from eeprom address 0
  EEPROM.get(EEPROM_SETTINGS, settings);
  
  // check if eeprom was ever initialized 
  if (settings.initialized != SETTINGS_MAGIC) {
    // eeprom never initialized, use default values
    strcpy(settings.playerName, "PLYR1");  // copy "PLYR1" into name array
    settings.soundOn = true;
    settings.lcdBrightness = DEFAULT_LCD_BRIGHT; 
    settings.matrixBrightness = DEFAULT_MATRIX_BRIGHT;
    settings.initialized = SETTINGS_MAGIC;  // write magic number
    saveSettings();  // save defaults to eeprom
  }
  
  // ensure name is null-terminated 
  settings.playerName[NAME_LEN] = '\0';
  
  // apply settings to hardware
  applySettings();
}

// function to load all highscores from eeprom
void loadHighScores(HighScore scores[HS_COUNT]) {
  // loop through all 3 highscore slots
  for (byte i = 0; i < HS_COUNT; i++) {
    // calculate eeprom address: base + (slot * size of one entry)
    // slot 0: 50 + (0*10) = 50
    // slot 1: 50 + (1*10) = 60
    // slot 2: 50 + (2*10) = 70
    int address = EEPROM_HS + i * sizeof(HighScore);
    
    // read highscore struct from calculated address
    EEPROM.get(address, scores[i]);
    
    // validate score (check for garbage data)
    if (scores[i].score < 0 || scores[i].score > HS_EMPTY) {
      // invalid data, reset to empty
      strcpy(scores[i].name, "-----");
      scores[i].score = HS_EMPTY;
    }
    
    // ensure name is null-terminated
    scores[i].name[NAME_LEN] = '\0';
  }
}

// function to save all highscores to eeprom
void saveHighScores(HighScore scores[HS_COUNT]) { 
  // loop through all 3 highscore slots
  for (byte i = 0; i < HS_COUNT; i++) {
    // calculate address same way as load
    int address = EEPROM_HS + i * sizeof(HighScore);
    // write struct to eeprom
    EEPROM.put(address, scores[i]); 
  }
}

// function to check if a score qualifies for highscore list
bool isNewHighScore(int score) { 
  HighScore scores[HS_COUNT];  // create temp array
  loadHighScores(scores);      // load current highscores
  
  // compare to worst highscore (last in sorted list)
  // lower moves = better score
  // if new score < worst score, it qualifies
  return (score < scores[HS_COUNT - 1].score); 
}

// function to insert a new highscore into the list
void addHighScore(int score) {
  HighScore scores[HS_COUNT];  // create temp array
  loadHighScores(scores);      // load current highscores
  
  // find where new score should be inserted
  for (byte i = 0; i < HS_COUNT; i++) {
    // if new score is better than this slot
    if (score < scores[i].score) {
      // shift worse scores down one position
      // start from bottom to avoid overwriting
      for (byte j = HS_COUNT - 1; j > i; j--) {
        scores[j] = scores[j - 1];  // copy previous score down
      }
      
      // insert new score in the gap created
      strncpy(scores[i].name, settings.playerName, NAME_LEN);
      scores[i].name[NAME_LEN] = '\0';
      scores[i].score = score;
      break;  //
    }
  }
  
  saveHighScores(scores);  // write updated list to eeprom
}

// ====================== MATRIX RENDERING ===================
// function to turn on/off a single led on the 8x8 matrix
void setPixel(byte x, byte y, bool state) { 
  // safety check: coordinates must be 0-7
  if (x > 7 || y > 7) return;
  
  // coordinates are inverted for correct orientation
  // 7-y flips vertically, 7-x flips horizontally
  // state: true=on, false=off
  lc.setLed(0, 7 - y, 7 - x, state); 
}

// function to draw a menu icon on the matrix
void drawMatrixIcon(byte iconIndex) {
  // 2d array of icon patterns: 5 icons, each 8 rows of 8 bits
  // each row is a byte where bits represent pixels (1=on, 0=off)
  const byte icons[5][8] = {
    {0x00, 0x18, 0x3C, 0x7E, 0x7E, 0x3C, 0x18, 0x00}, // play arrow shape
    {0x18, 0x3C, 0x7E, 0x7E, 0x3C, 0x18, 0x7E, 0x00}, // trophy shape
    {0x18, 0x24, 0x7E, 0xFF, 0xFF, 0x7E, 0x24, 0x18}, // gear shape
    {0x3C, 0x42, 0x99, 0xA1, 0xA1, 0x99, 0x42, 0x3C}, // info 'i' shape
    {0x3C, 0x42, 0x81, 0x99, 0x18, 0x00, 0x18, 0x00}  // question mark
  };
  
  lc.clearDisplay(0);  // clear matrix first
  
  // safety check: keep iconIndex in valid range 0-4
  if (iconIndex >= 5) iconIndex = 0;
  
  // loop through each row of icon
  for (byte row = 0; row < 8; row++) {
    // loop through each column
    for (byte col = 0; col < 8; col++) {
      // extract bit at position col from the row byte
      // example: if row byte is 0x3C (00111100 in binary)
      // and col=2: (0x3C >> (7-2)) = (00111100 >> 5) = 00000001
      // & 0x01 extracts just the rightmost bit
      bool pixelOn = (icons[iconIndex][row] >> (7 - col)) & 0x01;
      setPixel(col, row, pixelOn);  // light up this pixel if bit is 1
    }
  }
}

// function to draw the visible portion of game map
void drawViewport() {
  lc.clearDisplay(0);           // clear matrix first
  unsigned long t = millis();   // get current time for animations
  
  // draw all walls in visible 8x8 area
  for (byte y = 0; y < VIEW_SIZE; y++) {
    for (byte x = 0; x < VIEW_SIZE; x++) {
      // convert viewport coordinates to map coordinates
      // if camera at (4,4) and drawing viewport (2,3):
      // map position is (4+2, 4+3) = (6,7)
      if (game.isWall(x + game.camX, y + game.camY)) 
        setPixel(x, y, true);  // draw wall pixel
    }
  }
  
  // draw exit door (with blinking animation if key collected)
  // calculate exit position relative to camera viewport
  int ex = game.exitX - game.camX;  // exit x in viewport coordinates
  int ey = game.exitY - game.camY;  // exit y in viewport coordinates
  
  // only draw if exit is within visible 8x8 area
  if (ex >= 0 && ex < 8 && ey >= 0 && ey < 8) {
    if (game.hasKey) {
      // if player has key, make exit blink slowly
      // t / 700 divides time by 700ms intervals
      // % 2 gives remainder when divided by 2 (result is 0 or 1)
      // so exit blinks: on for 700ms, off for 700ms, repeat
      if ((t / 700) % 2 == 0)  // if even interval
        setPixel(ex, ey, true); // turn on
      // if odd interval, skip drawing (appears off)
    } 
    else {
      setPixel(ex, ey, true);  // if no key, exit is solid (always on)
    }
  }
  
  // draw key (only if not collected yet)
  if (!game.hasKey) {
    // calculate key position relative to camera
    int kx = game.keyX - game.camX;
    int ky = game.keyY - game.camY;
    
    // only draw if key is within visible 8x8 area
    if (kx >= 0 && kx < 8 && ky >= 0 && ky < 8) {
      // make key blink fast to attract attention
      // t / 300 divides time into 300ms intervals
      // % 2 gives 0 or 1, so key blinks every 300ms
      if ((t / 300) % 2 == 0)
        setPixel(kx, ky, true);
    }
  }
  
  // draw player (always visible, highest priority)
  // calculate player position relative to camera
  int px = game.playerX - game.camX;
  int py = game.playerY - game.camY;
  
  // only draw if player is within visible area
  if (px >= 0 && px < 8 && py >= 0 && py < 8) 
    setPixel(px, py, true);  // player is always solid (no blinking)
  
  // draw enemy (with blinking to distinguish from player)
  // calculate enemy position relative to camera
  int enX = game.enemy.x - game.camX;
  int enY = game.enemy.y - game.camY;
  
  // only draw if enemy is within visible area
  if (enX >= 0 && enX < 8 && enY >= 0 && enY < 8) {
    // make enemy blink fast (same rate as key)
    if ((t / 300) % 2 == 0)
      setPixel(enX, enY, true);
  }
}

// ====================== LCD RENDERER =======================
// class to handle all lcd display operations
class LCDRenderer {
  // print menu text by index
  void printMenuText(int index) {
    switch(index) {
      case 0: lcd.print(F("Start Game")); break;
      case 1: lcd.print(F("Highscore")); break;
      case 2: lcd.print(F("Settings")); break;
      case 3: lcd.print(F("About")); break;
      case 4: lcd.print(F("How to Play")); break;
    }
  }
  
  // private helper function to print settings text by index
  void printSettingsText(int index) {
    switch(index) {
      case 0: lcd.print(F("Name")); break;
      case 1: lcd.print(F("LCD Bright")); break;
      case 2: lcd.print(F("Matrix Bright")); break;
      case 3: lcd.print(F("Sound")); break;
      case 4: lcd.print(F("Reset HS")); break;
      case 5: lcd.print(F("Back")); break;      
    }
  }

public:
  // initialize lcd: 16 columns, 2 rows
  void init() { lcd.begin(16, 2); }

  // render main menu with current selection highlighted
  void renderMenu(int sel) {
    lcd.clear();  // clear previous content
    
    // row 0: show selected item with ">" cursor
    lcd.setCursor(0, 0);  // column 0, row 0
    lcd.print(F(">"));    // cursor symbol
    printMenuText(sel);   // selected item text
    
    // row 1: show next item
    lcd.setCursor(0, 1);  // column 0, row 1
    lcd.print(F(" "));   
    // (sel + 1) % MENU_ITEMS, if sel=4, next is (4+1)%5=0
    printMenuText((sel + 1) % MENU_ITEMS);
    
    // show corresponding icon on matrix
    drawMatrixIcon(sel);
  }

  // render game hud with level, lives, moves, key status, freeze status
  void renderHUD() {
    lcd.clear();
    
    // top row: "L1 <3:3     FRZ"
    lcd.setCursor(0, 0);  // start at column 0, row 0
    lcd.print(F("L"));    // "L" for level
    lcd.print(game.currentLevel);  // print level number (1-3)
    lcd.print(F(" <3:"));  // heart symbol and colon
    lcd.print(game.lives); // print remaining lives (0-3)
    
    // if enemies frozen, show "FRZ" 
    lcd.setCursor(11, 0);
    if (enemiesFrozen) lcd.print(F("FRZ"));
    
    // bottom row: "M:42    KEY PLYR1"
    lcd.setCursor(0, 1);  // start at column 0, row 1
    lcd.print(F("M:"));   // "M:" for moves
    lcd.print(game.moves); // print move count
    
    // if player has key, show "KEY" at column 8
    lcd.setCursor(7, 1);
    if (game.hasKey) lcd.print(F("KEY"));
    
    // show player name at column 12
    lcd.setCursor(11, 1);
    lcd.print(settings.playerName);
  }

  // render settings menu with current selection highlighted
  void renderSettings(int sel) {
    lcd.clear();
    
    // row 0: selected setting with ">"
    lcd.setCursor(0, 0);
    lcd.print(F(">"));
    printSettingsText(sel);
    
    // row 1: next setting with " "
    lcd.setCursor(0, 1);
    lcd.print(F(" "));
    // wrap around: if sel=4, next is (4+1)%5=0
    printSettingsText((sel + 1) % SETTINGS_ITEMS);
  }

  // render name editing screen with current character in brackets
  void renderNameEdit() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Name:"));  // title on row 0
    
    lcd.setCursor(0, 1);  // name on row 1
    // loop through each character of name
    for (byte i = 0; i < NAME_LEN; i++) {
      // if this is the character being edited
      if (i == nameEditPos) 
        lcd.print(F("["));  // opening bracket
      
      // print the character
      lcd.print(settings.playerName[i]);
      
      // if this is the character being edited
      if (i == nameEditPos) 
        lcd.print(F("]"));  // closing bracket
      // otherwise add spacing
      else if (i < NAME_LEN - 1)  // don't add space after last char
        lcd.print(F(" "));
    }
    // result looks like: "A [B] C D E" where B is being edited
  }

  // render lcd brightness adjustment screen
  void renderLCDBrightness() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("LCD Bright:"));  // title on row 0
    
    lcd.setCursor(0, 1);
    // cast byte to int for proper printing
    lcd.print((int)settings.lcdBrightness);  // value on row 1
  }

  // render matrix brightness adjustment screen
  void renderMatrixBrightness() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Matrix Bright:"));  // title on row 0
    
    lcd.setCursor(0, 1);
    lcd.print((int)settings.matrixBrightness);  // value on row 1
    
    // light up entire matrix to preview brightness
    // loop through all 64 pixels (8x8 = 64)
    for(int i = 0; i < 64; i++) {
      // i % 8 gives x coordinate: 
      // i / 8 gives y coordinate: 
      setPixel(i % 8, i / 8, true);
    }
  }

  // render highscore list with auto-scrolling
  void renderHighScore() {
    // static variable keeps value between function calls
    static byte page = 0;  // which pair of scores to show (0,1,2)
    
    HighScore scores[HS_COUNT];  // create temp array
    loadHighScores(scores);      // load from eeprom
    
    // clear previous name with spaces 
    lcd.setCursor(0, 0);
    lcd.print("   ");  // print 3 spaces
    
    // row 0: show score at current page
    lcd.setCursor(0, 0);
    lcd.print(scores[page].name);  // player name
    lcd.setCursor(9, 0);           // move to column 9
    
    // check if score is valid (not empty placeholder)
    if (scores[page].score < HS_EMPTY) {
      lcd.print(scores[page].score);  // print move count
      lcd.print(F("mv"));             // "mv" for moves
    } 
    else {
      lcd.print(F("---"));  // empty slot
    }
    
    // row 1: show next score
    // (page + 1) % HS_COUNT wraps
    byte next = (page + 1) % HS_COUNT;
    lcd.setCursor(0, 1);
    lcd.print(scores[next].name);
    lcd.setCursor(9, 1);
    
    if (scores[next].score < HS_EMPTY) {
      lcd.print(scores[next].score);
      lcd.print(F("mv"));
    } 
    else {
      lcd.print(F("---"));
    }
    
    // auto-scroll every 2.5 seconds
    unsigned long now = millis();
    // if 2500ms have passed since last scroll
    if (now - scrollTime > HS_SCROLL_MS) {
      page = (page + 1) % HS_COUNT;  // advance to next pair
      scrollTime = now; // reset timer
    }
  }

  // render about screen with scrolling text
  void renderAbout() {
    // strlen_P() gets length of PROGMEM string
    int len = strlen_P(aboutText);
    unsigned long now = millis();
    
    // advance scroll position every 300ms
    if (now - scrollTime > ABOUT_SCROLL_MS) {
      scrollPos++;           // move one character forward
      if (scrollPos > len)   // if reached end
        scrollPos = 0;       // wrap back to start
      scrollTime = now;      // reset timer
    }
    
    // display 16 characters from scrolling position
    lcd.setCursor(0, 0);
    for (byte i = 0; i < 16; i++) {
      // calculate which character to show
      // (scrollPos + i) % len wraps around at end of string
      // example: if scrollPos=50, len=52, i=5: (50+5)%52=55%52=3
      int idx = (scrollPos + i) % len;
      
      // pgm_read_byte_near() reads one byte from program memory
      char c = pgm_read_byte_near(aboutText + idx);
      lcd.print(c);
    }
    
    // row 1: static subtitle
    lcd.setCursor(0, 1);
    lcd.print(F("Maze 16x16 + IMU"));
  }

  // render how to play instructions
  void renderHowToPlay() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("Get KEY, then EXIT"));  // objective on row 0
    lcd.setCursor(0, 1);
    lcd.print(F("Shake=Freeze 3s"));     // special ability on row 1
  }

  // render victory screen
  void renderWin() {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(F("YOU WIN!"));  // victory message
    
    lcd.setCursor(0, 1);
    lcd.print(F("Total:"));
    lcd.print(game.moves);     // final score (total moves)
    lcd.print(F("mv"));
    
    // draw heart icon on matrix
    lc.clearDisplay(0);
    // heart pattern: each byte is one row of 8 bits
    const byte heart[8] = {0x00, 0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18};
    
    // draw each row of heart
    for (byte r = 0; r < 8; r++) {
      for (byte c = 0; c < 8; c++) {
        // extract bit at position c from row byte
        // (heart[r] >> (7-c)) shifts bit to rightmost position
        // & 0x01 masks all bits except rightmost
        if ((heart[r] >> (7 - c)) & 0x01) 
          setPixel(c, r, true);
      }
    }
  }
} renderer; 

// ====================== GAME CONTROLLER ====================
// class containing all game logic
class GameController {
public:
  // initialize all hardware pins and components
  void initHardware() {
    // set button pin as input with internal pullup resistor
    pinMode(JOY_SW, INPUT_PULLUP);
    
    pinMode(BUZZER, OUTPUT); // buzzer as output
    pinMode(LCD_BACKLIGHT, OUTPUT); // backlight as output
    
    lc.shutdown(0, false);  // wake up matrix (false = not shutdown)
    lc.setIntensity(0, DEFAULT_MATRIX_BRIGHT);  // set brightness
    lc.clearDisplay(0);     // clear any leftover pixels
  }

  // update enemy movement (chase player)
  void updateEnemy() {
    // check if enemies are frozen from shake
    if (enemiesFrozen) {
      // calculate how long frozen
      unsigned long elapsed = millis() - freezeStart;
      
      // if freeze duration exceeded
      if (elapsed > FREEZE_TIME) {
        enemiesFrozen = false;  // unfreeze
        renderer.renderHUD();   // update display (remove FRZ indicator)
      }
      return;  // skip movement while frozen
    }
    
    // determine movement speed based on level
    // level 3 is faster (400ms) than levels 1-2 (800ms)
    unsigned long speed = (game.currentLevel == 3) ? ENEMY_SPEED_3 : ENEMY_SPEED_1;
    
    // check if enough time passed since last move
    unsigned long elapsed = millis() - lastEnemyMove;
    if (elapsed < speed) return;  // not time to move yet
    
    lastEnemyMove = millis();  // record this move time
    
    // calculate distance to player (can be negative)
    int dx = game.playerX - game.enemy.x;  // horizontal distance
    int dy = game.playerY - game.enemy.y;  // vertical distance
    
    // example: player at (10,8), enemy at (7,12)
    // dx = 10-7 = 3 (player is 3 units to the right)
    // dy = 8-12 = -4 (player is 4 units above)
    
    // tentative next position (start with current position)
    byte nx = game.enemy.x;
    byte ny = game.enemy.y;
    
    // move primarily in axis with larger distance
    // abs() returns absolute value: abs(-4)=4, abs(3)=3
    if (abs(dx) > abs(dy)) {
      // horizontal distance is larger, so move horizontally
      // if dx > 0, player is to the right, so add 1 (move right)
      // if dx < 0, player is to the left, so add -1 (move left)
      nx += (dx > 0) ? 1 : -1;
      
      // check if new position is blocked by wall
      if (game.isWall(nx, ny)) {
        // can't move horizontally, try vertically instead
        nx = game.enemy.x;  // restore x (cancel horizontal move)
        ny += (dy > 0) ? 1 : -1;  // try vertical move
        
        // if vertical also blocked, give up this turn
        if (game.isWall(nx, ny)) return;
      }
    } 
    else {
      // vertical distance is larger (or equal), so move vertically
      ny += (dy > 0) ? 1 : -1;
      
      // check if blocked
      if (game.isWall(nx, ny)) {
        // can't move vertically, try horizontally instead
        ny = game.enemy.y;  // restore y
        nx += (dx > 0) ? 1 : -1;  // try horizontal move
        
        if (game.isWall(nx, ny)) return;  // both blocked, give up
      }
    }
    
    // move was valid, update enemy position
    game.enemy.x = nx;
    game.enemy.y = ny;
    
    // check if enemy caught player
    if (game.enemy.x == game.playerX && game.enemy.y == game.playerY) 
      playerHit();  // trigger collision
  }

  // handle player getting hit by enemy
  void playerHit() {
    soundHit();      // play hit sound
    game.lives--;    // subtract one life
    
    // check if out of lives
    if (game.lives == 0) {
      // game over
      currentState = STATE_GAMEOVER;
      gameOverStart = millis();  // record time for delay
      
      lcd.clear();
      lcd.print(F("GAME OVER"));
      lcd.setCursor(0, 1);
      lcd.print(F("Score: "));
      lcd.print(game.moves);
      
      // check if achieved highscore
      if (isNewHighScore(game.moves)) {
        addHighScore(game.moves);  // add to list
      }
    } 
    else {
      // still have lives, respawn
      currentState = STATE_WAIT_RESPAWN;
      stateTimer = millis();  // start respawn timer
      
      lcd.clear();
      lcd.print(F("OUCH! Lives:"));
      lcd.print(game.lives);
    }
  }

  // check joystick input and move player
  void checkSensors() {
    if (!input.canProcess()) return;  // throttle input to 150ms intervals
    
    int dx = 0, dy = 0;  // will hold joystick values
    
    // read joystick axes
    if (input.readAxes(dx, dy)) {
      // joystick moved significantly
      
      // calculate tentative new position
      int newX = game.playerX;
      int newY = game.playerY;
      
      // move in dominant axis (whichever moved more)
      if (abs(dx) > abs(dy)) {
        // horizontal movement is larger
        // if dx < 0, joystick left, so subtract 1
        // if dx > 0, joystick right, so add 1
        newX += (dx < 0) ? -1 : 1;
      } 
      else {
        // vertical movement is larger (or equal)
        newY += (dy < 0) ? -1 : 1;
      }
      
      // check if move is valid (not into wall)
      if (!game.isWall(newX, newY)) {
        // move is valid, update player position
        game.playerX = newX;
        game.playerY = newY;
        game.moves++;           // increment move counter
        game.updateCamera();    // adjust camera to follow player
        renderer.renderHUD();   // update display
        soundMove();            // play move sound
        
        // check if player picked up key
        if (!game.hasKey && newX == game.keyX && newY == game.keyY) {
          game.hasKey = true;
          soundKey();
          renderer.renderHUD();  // update to show KEY indicator
        }
        
        // check if player reached exit
        if (newX == game.exitX && newY == game.exitY) {
          if (game.hasKey) {
            // player has key, can exit
            soundWin();
            
            // check if more levels remain
            if (game.currentLevel < LAST_LEVEL) {
              // advance to next level
              game.currentLevel++;
              currentState = STATE_WAIT_LEVEL_NEXT;
              stateTimer = millis();
              lcd.clear();
              lcd.print(F("Level Done!"));
            } 
            else {
              // beat final level (level 3), game won!
              lastGameNewHighScore = isNewHighScore(game.moves);
              if (lastGameNewHighScore) addHighScore(game.moves);
              currentState = STATE_WIN;
              renderer.renderWin();
            }
          } 
          else {
            // tried to exit without key
            soundUndo();  // error sound
          }
        }
        
        // check if moved onto enemy
        if (game.playerX == game.enemy.x && game.playerY == game.enemy.y) 
          playerHit();
      }
    }
  }

  // update main menu (handle navigation and selection)
  void updateMenu() {
    if (!input.canProcess()) return;  // throttle input
    
    int y = input.readY();  // read vertical axis
    
    if (input.moved(y)) {
      // joystick moved vertically
      
      if (y > 0) {
        // joystick up: go to previous item
        // (menuSelection - 1) would go -1 when at 0
        // + MENU_ITEMS ensures value is positive before modulo
        // example: sel=0: (0-1+5) % 5 = 4 % 5 = 4 
        menuSelection = (menuSelection - 1 + MENU_ITEMS) % MENU_ITEMS;
      } 
      else {
        // joystick down: go to next item
        // example: sel=4: (4+1) % 5 = 5 % 5 = 0
        menuSelection = (menuSelection + 1) % MENU_ITEMS;
      }
      
      renderer.renderMenu(menuSelection);  // update display
      soundMove();                          // play sound
    }
    
    if (input.buttonPressed()) {
      // button pressed, select current menu item
      soundSelect();
      lc.clearDisplay(0);  // clear matrix
      
      switch (menuSelection) {
        case 0:  // start game
          game.init();              // initialize new game
          renderer.renderHUD();     // show game hud
          currentState = STATE_GAME; // switch to game state
          break;
          
        case 1:  // highscore
          scrollTime = millis();     // start scroll timer
          currentState = STATE_HIGHSCORE;
          break;
          
        case 2:  // settings
          settingsSelection = 0;     // start at first setting
          settingsState = SET_MENU;  // NEW: start in menu sub-state
          renderer.renderSettings(0);
          currentState = STATE_SETTINGS;
          break;
          
        case 3:  // about
          scrollPos = 0;             // start scroll at beginning
          scrollTime = millis();
          renderer.renderAbout();
          currentState = STATE_ABOUT;
          break;
          
        case 4:  // how to play
          renderer.renderHowToPlay();
          currentState = STATE_HELP;
          break;
      }
    }
  }

  void updateSettings() {

    // STATE: SETTINGS MENU
    // browsing the settings menu 
    if (settingsState == SET_MENU) {

      // check if enough time passed for input processing
      if (input.canProcess()) {
        int y = input.readY();
        
        if (input.moved(y)) {
          // navigate settings menu using ternary operator for compact code
          // if y > 0 (up): go to previous, else: go to next
          settingsSelection = (y > 0)
            ? (settingsSelection - 1 + SETTINGS_ITEMS) % SETTINGS_ITEMS
            : (settingsSelection + 1) % SETTINGS_ITEMS;

          renderer.renderSettings(settingsSelection);
          soundMove();
        }
      }

      // button pressed: enter the selected setting
      if (input.buttonPressed()) {
        soundSelect();

        switch (settingsSelection) {
          case 0:  // NAME
            nameEditPos = 0;  // start at first character
            settingsState = SET_NAME;  // switch to name editing state
            renderer.renderNameEdit();
            break;

          case 1:  // LCD BRIGHTNESS
            settingsState = SET_LCD;  // switch to lcd brightness state
            renderer.renderLCDBrightness();
            break;

          case 2:  // MATRIX BRIGHTNESS
            settingsState = SET_MATRIX;  // switch to matrix brightness state
            renderer.renderMatrixBrightness();
            break;

          case 3:  // SOUND ON/OFF
            // toggle sound immediately
            settings.soundOn = !settings.soundOn;  
            saveSettings();
            
            // switch to confirmation state to show result
            settingsState = SET_SOUND_CONFIRM;
            lcd.clear();
            lcd.print(F("Sound: "));
            // ternary operator: if soundOn is true, print "ON", else print "OFF"
            lcd.print(settings.soundOn ? F("ON") : F("OFF"));
            break;

          case 4:  // RESET HIGHSCORE
            // switch to confirmation state (requires button press to confirm)
            HighScore empty;
            strcpy(empty.name, "-----");
            empty.score = HS_EMPTY;

            for (byte i = 0; i < HS_COUNT; i++) {
              int address = EEPROM_HS + i * sizeof(HighScore);
              EEPROM.put(address, empty);
            }

            lcd.clear();
            lcd.print(F("HS Reset OK"));

            resetDoneStart = millis();  
            settingsState = SET_RESET_CONFIRM;
            soundSelect();
            break;
          case 5: // Back
            saveSettings();
            settingsState = SET_MENU;
            currentState = STATE_MENU;
            renderer.renderMenu(menuSelection);
            break;
        }
      }

      // long press to exit settings menu entirely
      if (input.buttonLongPress()) {
        soundUndo();
        saveSettings();  // save any changes
        currentState = STATE_MENU;  // return to main menu
        renderer.renderMenu(menuSelection);
      }
    }

    // STATE: NAME EDITING
    else if (settingsState == SET_NAME) {

      if (input.canProcess()) {
        int dx = input.readX();  // horizontal axis
        int dy = input.readY();  // vertical axis
        bool changed = false;    // flag to track if anything changed

        // vertical movement: change current character
        if (input.moved(dy)) {
          // add 1 if up, subtract 1 if down
          settings.playerName[nameEditPos] += (dy > 0 ? 1 : -1);
          
          // if past 'Z', go to 'A'
          if (settings.playerName[nameEditPos] > 'Z') 
            settings.playerName[nameEditPos] = 'A';
          // if before 'A', go to 'Z'
          if (settings.playerName[nameEditPos] < 'A') 
            settings.playerName[nameEditPos] = 'Z';
          
          changed = true;
        }

        // horizontal movement: move between character positions
        if (input.moved(dx)) {
          nameEditPos = (dx > 0)
            ? (nameEditPos + 1) % NAME_LEN              // move right
            : (nameEditPos + NAME_LEN - 1) % NAME_LEN;  // move left 
          
          changed = true;
        }

        // only update display and play sound if something changed
        if (changed) {
          renderer.renderNameEdit();
          soundMove();
        }
      }

      // button pressed: save and exit name editing
      if (input.buttonPressed()) {
        saveSettings();
        settingsState = SET_MENU;  // return to settings menu
        renderer.renderSettings(settingsSelection);
        soundSelect();
      }
    }
    // STATE: LCD BRIGHTNESS
    else if (settingsState == SET_LCD) {
      if (input.canProcess()) {
        int dy = input.readY();
        
        if (input.moved(dy)) {
          int newBrightness = settings.lcdBrightness;
          
          if (dy > 0)
            newBrightness += 20;
          else
            newBrightness -= 20;
          
          newBrightness = constrain(newBrightness, 40, 255);
          
          settings.lcdBrightness = (byte)newBrightness;
          
          analogWrite(LCD_BACKLIGHT, settings.lcdBrightness);          
          renderer.renderLCDBrightness();
          soundMove();
        }
      }

      if (input.buttonPressed()) {
        saveSettings();
        settingsState = SET_MENU;
        renderer.renderSettings(settingsSelection);
        soundSelect();
      }
    }
    // STATE: MATRIX BRIGHTNESS
    else if (settingsState == SET_MATRIX) {

      if (input.canProcess()) {
        int dy = input.readY();
        
        if (input.moved(dy)) {
          // change by 1
          settings.matrixBrightness = constrain(
            settings.matrixBrightness + (dy > 0 ? 1 : -1),
            0,   // minimum (off)
            15   // maximum (full brightness)
          );
          
          lc.setIntensity(0, settings.matrixBrightness);
          renderer.renderMatrixBrightness();
          soundMove();
        }
      }

      // button pressed: save and exit
      if (input.buttonPressed()) {
        saveSettings();
        lc.clearDisplay(0);  // clear the preview pattern
        settingsState = SET_MENU;  // return to settings menu
        renderer.renderSettings(settingsSelection);
        soundSelect();
      }
    }
    // STATE: SOUND CONFIRMATION
    // showing "Sound: ON" or "Sound: OFF"
    // waiting for button press to return to menu
    else if (settingsState == SET_SOUND_CONFIRM) {

      // button pressed: return to settings menu
      if (input.buttonPressed()) {
        settingsState = SET_MENU;
        renderer.renderSettings(settingsSelection);
        soundSelect();  // test sound if enabled
      }
    }

    // STATE: RESET CONFIRMATION
    else if (settingsState == SET_RESET_CONFIRM) {

      if (input.buttonPressed()) {

        HighScore empty;
        strcpy(empty.name, "-----");
        empty.score = HS_EMPTY;

        for (byte i = 0; i < HS_COUNT; i++) {
          int address = EEPROM_HS + i * sizeof(HighScore);
          EEPROM.put(address, empty);
        }

        lcd.clear();
        lcd.print(F("HS Reset OK"));

        resetDoneStart = millis();        
        settingsState = SET_RESET_DONE;   
        soundSelect();
      }
    }

    else if (settingsState == SET_RESET_DONE) {
      if (millis() - resetDoneStart >= 800) {

        settingsSelection = 0;
        settingsState = SET_MENU;
        renderer.renderSettings(settingsSelection);
      }
}


  }

  // update highscore display
  void updateHighScore() { 
    static bool entered = true;  // true = need to initialize
    
    if(entered) {
      // first time entering this screen
      lcd.clear();           // clear lcd
      lc.clearDisplay(0);    // clear matrix
      scrollTime = millis(); // reset scroll timer
      entered = false;       // mark as initialized
    }
    
    renderer.renderHighScore();  // update display with scrolling
    
    // exit on any button press
    if (input.buttonPressed() || input.buttonLongPress()) { 
      soundUndo();
      entered = true;  // reset flag for next time
      currentState = STATE_MENU;
      renderer.renderMenu(menuSelection);
    } 
  }
  
  // update about screen
  void updateAbout() { 
    // static variable for initialization tracking
    static bool entered = true;
    
    if(entered) {
      // first time entering this screen
      lcd.clear();
      lc.clearDisplay(0);
      scrollPos = 0;           // reset scroll position
      scrollTime = millis();   // reset scroll timer
      entered = false;
    } 
    
    renderer.renderAbout();  // update scrolling text
    
    // exit on any button press
    if (input.buttonPressed() || input.buttonLongPress()) { 
      soundUndo();
      entered = true;  // reset flag for next time
      currentState = STATE_MENU;
      renderer.renderMenu(menuSelection);
    } 
  }
  
  // update how to play screen
  void updateHelp() { 
    // static variable for initialization tracking
    static bool entered = false;
    
    if(!entered) {
      // first time entering this screen
      lc.clearDisplay(0);
      entered = true;
    } 
    
    // exit on any button press
    if (input.buttonPressed() || input.buttonLongPress()) { 
      soundUndo();
      entered = false;  // reset flag for next time
      currentState = STATE_MENU;
      renderer.renderMenu(menuSelection);
    } 
  }
  
  // update game over screen
  void updateGameOver() { 
    // wait 1 second before allowing input 
    unsigned long elapsed = millis() - gameOverStart;
    
    if (elapsed > 1000) {  // if more than 1000ms have passed
      if(input.buttonPressed()) { 
        soundSelect();
        currentState = STATE_MENU;
        renderer.renderMenu(menuSelection);
      } 
    }
  }

  // update victory screen
  void updateWin() { 
    // immediately allow button press
    static bool drawn = false;

    if (!drawn) {
      renderer.renderWin();   
      drawn = true;
    }

    if (input.buttonPressed()) {
      soundSelect();
      drawn = false;          
      currentState = STATE_MENU;
      renderer.renderMenu(menuSelection);
    }
  }
  
  // transition between levels
  void updateWaitLevelNext() {
    // wait for transition delay
    unsigned long elapsed = millis() - stateTimer;
    
    if (elapsed > TRANSITION_MS) {  // if 1500ms have passed
      game.loadLevel(game.currentLevel);  // load next level layout
      renderer.renderHUD();               // show updated hud
      drawViewport();                     // draw initial view
      currentState = STATE_GAME;          // resume gameplay
    }
  }
  
  // respawn player after death
  void updateWaitRespawn() {
    // wait for respawn delay
    unsigned long elapsed = millis() - stateTimer;
    
    if (elapsed > RESPAWN_MS) {  // if 1000ms have passed
      // reset player and enemy positions based on current level
      // each level has predetermined spawn points
      
      if(game.currentLevel == 1) {
        game.playerX = 2;
        game.playerY = 2;
        game.enemy.x = 12;
        game.enemy.y = 12;
      }
      else if(game.currentLevel == 2) {
        game.playerX = 2;
        game.playerY = 7;
        game.enemy.x = 7;
        game.enemy.y = 7;
      }
      else {  // level 3
        game.playerX = 1;
        game.playerY = 1;
        game.enemy.x = 7;
        game.enemy.y = 7;
      }
      
      lastEnemyMove = millis();  // reset enemy movement timer
      enemiesFrozen = false;     // clear any freeze effect
      renderer.renderHUD();      // update display
      drawViewport();            // draw initial view
      currentState = STATE_GAME; // resume gameplay
    }
  }
} gameController; 

// function to handle imu initialization screen
void updateIMUInit() {
  if (!imuInitShown) {
    // first time entering this state
    lcd.clear();
    lcd.print(F("Init Sensor..."));
    
    // attempt to initialize imu sensor
    if (!imu.init()) {
      // initialization failed
      lcd.setCursor(0, 1);
      lcd.print(F("IMU Error!"));
      imuAvailable = false;  // mark as unavailable (game still works)
    } 
    else {
      // initialization successful
      lcd.setCursor(0, 1);
      lcd.print(F("IMU OK!"));
      imuAvailable = true;  // mark as available (shake feature works)
    }
    
    imuInitStart = millis();  // record when screen appeared
    imuInitShown = true;      // mark as shown
  }
  
  // wait for display duration
  unsigned long elapsed = millis() - imuInitStart;
  if (elapsed > IMU_INIT_MS)  // if 800ms have passed
    currentState = STATE_INTRO;  // move to intro screen
}

// function to handle intro screen with animation
void updateIntro() {

  // first time entering intro state
  if (!introShown) {
    lcd.clear();

    // display title centered
    lcd.setCursor(2, 0);
    lcd.print(F("MAZE RUNNER"));
    lcd.setCursor(2, 1);
    lcd.print(F("Matrix Game"));

    introStart = millis();        // record when intro started
    introAnimTimer = millis();    // timer for matrix animation
    introRow = 0;                 // start from first row
    lc.clearDisplay(0);           // clear matrix

    introShown = true;
  }

  // animated matrix fill effect (one row every 50 ms)
  if (introRow < 8) {
    if (millis() - introAnimTimer >= 50) {
      lc.setRow(0, introRow, 0xFF);  // light entire row
      introRow++;                   // move to next row
      introAnimTimer = millis();    // reset animation timer
    }
  }

  // wait for intro duration
  if (millis() - introStart > INTRO_MS) {
    lc.clearDisplay(0);             // clear matrix
    currentState = STATE_MENU;      // go to main menu
    renderer.renderMenu(menuSelection);
  }
}

void setup() {

  Serial.begin(9600);
  
  gameController.initHardware();  // setup pins and components
  renderer.init();                 // initialize lcd
  loadSettings();                  // load settings from eeprom
  
  
  currentState = STATE_IMU_INIT;  // start at imu init screen
}

void loop() {
  // update tone system first (stop tone if duration expired)
  updateTone();
  
  switch (currentState) {
    case STATE_IMU_INIT:
      updateIMUInit();  // checking imu sensor
      break;
      
    case STATE_INTRO:
      updateIntro();  // showing intro animation
      break;
      
    case STATE_MENU:
      gameController.updateMenu();  // navigating main menu
      break;
      
    case STATE_GAME:
      
      // check for shake gesture (freeze enemies)
      // only check if: imu available AND not already frozen AND shake detected
      if (imuAvailable && !enemiesFrozen && imu.detectShake()) {
        enemiesFrozen = true;       // activate freeze
        freezeStart = millis();     // record start time
        soundFreeze();              // play freeze sound
        renderer.renderHUD();       // update display (show FRZ)
      }
      
      gameController.checkSensors();  // handle player input/movement
      gameController.updateEnemy();   // update enemy ai
      drawViewport();                 // render current view on matrix
      break;
      
    case STATE_SETTINGS:
      gameController.updateSettings();  // in settings menu (with sub-states)
      break;
      
    case STATE_HIGHSCORE:
      gameController.updateHighScore();  // viewing highscores
      break;
      
    case STATE_ABOUT:
      gameController.updateAbout();  // viewing about/credits
      break;
      
    case STATE_HELP:
      gameController.updateHelp();  // viewing instructions
      break;
      
    case STATE_GAMEOVER:
      gameController.updateGameOver();  // game over screen
      break;
      
    case STATE_WIN:
      gameController.updateWin();  // victory screen
      break;
      
    case STATE_WAIT_LEVEL_NEXT:
      gameController.updateWaitLevelNext();  // transitioning to next level
      break;
      
    case STATE_WAIT_RESPAWN:
      gameController.updateWaitRespawn();  // waiting to respawn after death
      break;
  }
  
}