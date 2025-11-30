#include <LiquidCrystal.h>
#include <LedControl.h>

// lcd pins 
LiquidCrystal lcd(9, 8, 7, 6, 5, 4);

// max7219 
LedControl lc(12, 11, 10, 1);

const int JOY_X = A0;   // joystick x axis
const int JOY_Y = A1;   // joystick y axis
const int JOY_SW = 2;   // joystick button

const int BUZZER = 3;   // buzzer pin
const bool USE_BUZZER = true;  // enable/disable sound

// matrix sizes
const byte VIEW_SIZE = 8;   // physical matrix is 8x8
const byte MAP_SIZE = 16;   // virtual map is 16x16

// system times
const unsigned long JOY_REPEAT_MS = 150;   // joystick input rate limit
const unsigned long BUTTON_DEBOUNCE_MS = 200;  // button debounce
const unsigned long MATRIX_REFRESH_MS = 40;    // redraw speed

unsigned long introStart = 0;
bool introShown = false;

// game states
enum GameState { STATE_INTRO, STATE_MENU, STATE_GAME, STATE_ABOUT };
GameState currentState = STATE_MENU;  // starting state

// input controller
class InputController {

    unsigned long lastInputTime = 0;   // last joystick action time
    unsigned long lastButtonTime = 0;  // last button press time
    bool lastButtonState = HIGH;       // previous button state

public:

    int readX() { return analogRead(JOY_X) - 512; }   // x movement
    int readY() { return -(analogRead(JOY_Y) - 512); } // y movemen

    bool moved(int v) { return abs(v) > 300; }  // threshold to detect move

    bool canProcessInput() {
        if (millis() - lastInputTime >= JOY_REPEAT_MS) {
            lastInputTime = millis();
            return true;
        }
        return false;
    }

    bool buttonPressed() {
        unsigned long now = millis();
        if (now - lastButtonTime < BUTTON_DEBOUNCE_MS) return false;

        bool current = (digitalRead(JOY_SW) == LOW); // button pressed

        if (current && lastButtonState == HIGH) {    
            lastButtonState = current;
            lastButtonTime = now;
            return true;
        }

        lastButtonState = current;
        return false;
    }

} input;

// audio
struct ToneData {
    unsigned long startTime;  // when current tone started
    int duration;             // tone length
    bool active;              // tone active or not
} currentTone = {0, 0, false};

void playTone(int freq, int duration) {
    if (USE_BUZZER) {
        tone(BUZZER, freq);
        currentTone = { millis(), duration, true };
    }
}

void updateTone() {
    if (currentTone.active && millis() - currentTone.startTime >= currentTone.duration) {
        noTone(BUZZER);
        currentTone.active = false;
    }
}

void soundMove()   { playTone(600, 25); }  // move beep
void soundSelect() { playTone(900, 60); }  // select beep

// game model
struct GameModel {

    bool map[MAP_SIZE][MAP_SIZE];  // 16x16 map
    byte playerX, playerY;         // player position on 16x16
    byte camX, camY;               // camera position (top-left of 8x8)
    bool needsRedraw;              // flag for redraw

    void init() {

        // fill map with off pixels
        for (int y = 0; y < MAP_SIZE; y++)
            for (int x = 0; x < MAP_SIZE; x++)
                map[x][y] = false;

        playerX = 8;   // spawn at center
        playerY = 8;

        camX = constrain(playerX - 4, 0, MAP_SIZE - VIEW_SIZE); // center camera
        camY = constrain(playerY - 4, 0, MAP_SIZE - VIEW_SIZE);

        needsRedraw = true;
    }

} game;

// lcd renderer
class LCDRenderer {

public:

    void init() {
        lcd.begin(16, 2);
    }

    void renderMenu(int sel) {
        lcd.clear();
        lcd.setCursor(0, 0);
        lcd.print(sel == 0 ? ">Start game" : " Start game");
        lcd.setCursor(0, 1);
        lcd.print(sel == 1 ? ">About" : " About");
    }

    void renderAbout() {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("16x16 matrix");
        lcd.setCursor(0, 1); lcd.print("in progress");
    }

    void renderHUD(byte px, byte py) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("game");
        lcd.setCursor(0, 1);lcd.print("Sokoban");
    }
};

LCDRenderer renderer;


// game controller
class GameController {

    int menuSelection = 0;  // selected menu row
    unsigned long lastMatrixUpdate = 0;  // last draw time

public:

    void initHardware() {
        pinMode(JOY_SW, INPUT_PULLUP);   // joystick button
        if (USE_BUZZER) pinMode(BUZZER, OUTPUT);

        lc.shutdown(0, false);   // enable matrix
        lc.setIntensity(0, 2);   
        lc.clearDisplay(0);
    }

    // draw portion of 16x16 onto 8x8
    void drawViewport() {

        if (!game.needsRedraw) return;
        game.needsRedraw = false;

        lc.clearDisplay(0);

        for (int y = 0; y < VIEW_SIZE; y++) {
            for (int x = 0; x < VIEW_SIZE; x++) {
                int vx = x + game.camX;
                int vy = y + game.camY;
                lc.setLed(0, y, x, game.map[vx][vy]); // draw tile
            }
        }

        // draw player inside camera
        int px = game.playerX - game.camX;
        int py = game.playerY - game.camY;

        if (px >= 0 && px < 8 && py >= 0 && py < 8)
            lc.setLed(0, py, px, true); // player pixel
    }

    // menu
    void updateMenu() {

        if (!input.canProcessInput()) return;

        int y = input.readY();

        if (input.moved(y)) {
            menuSelection = (y > 0 ? 0 : 1);   // simple 2-option menu
            renderer.renderMenu(menuSelection);
            soundMove();
        }

        if (input.buttonPressed()) {
            soundSelect();
            if (menuSelection == 0) startGame();
            else showAbout();
        }
    }

    // game
    void startGame() {
        currentState = STATE_GAME;
        game.init();
        renderer.renderHUD(game.playerX, game.playerY);
        drawViewport();
    }

    void updateCamera() {
        game.camX = constrain(game.playerX - 4, 0, MAP_SIZE - VIEW_SIZE);
        game.camY = constrain(game.playerY - 4, 0, MAP_SIZE - VIEW_SIZE);
    }

    void updateGame() {

        if (input.buttonPressed()) {
            soundSelect();
            currentState = STATE_MENU;
            renderer.renderMenu(0);
            lc.clearDisplay(0);
            return;
        }

        if (!input.canProcessInput()) {
            drawViewport();
            return;
        }

        int dx = input.readX();
        int dy = input.readY();
        bool moved = false;

        // horizontal move
        if (input.moved(dx)) {
            if (dx < 0 && game.playerX > 0) game.playerX--;
            else if (dx > 0 && game.playerX < MAP_SIZE - 1) game.playerX++;
            moved = true;
        }

        // vertical move
        if (input.moved(dy)) {
            if (dy < 0 && game.playerY > 0) game.playerY--;
            else if (dy > 0 && game.playerY < MAP_SIZE - 1) game.playerY++;
            moved = true;
        }

        if (moved) {
            updateCamera();
            renderer.renderHUD(game.playerX, game.playerY);
            game.needsRedraw = true;
            soundMove();
        }

        drawViewport();
    }

    // about screen
    void showAbout() {
        currentState = STATE_ABOUT;
        renderer.renderAbout();
        lc.clearDisplay(0);
    }

    void updateAbout() {
        if (input.buttonPressed()) {
            soundSelect();
            currentState = STATE_MENU;
            renderer.renderMenu(0);
        }
    }

} gameController;

void updateIntro() {
    // if first time entering intro display intro
    if (!introShown) {
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("Matrix Project");
        lcd.setCursor(0, 1); lcd.print("Welcome!");
        introStart = millis();
        introShown = true;
    }

    // after 1500 ms switch to menu
    if (millis() - introStart >= 1500) {
        currentState = STATE_MENU;
        renderer.renderMenu(0);
    }
}

void setup() {
    gameController.initHardware();
    renderer.init();
    
    currentState = STATE_INTRO; 

    renderer.renderMenu(0);
}

void loop() {
    updateTone();

    switch (currentState) {
        case STATE_INTRO: 
            updateIntro(); 
            break;
        case STATE_MENU: 
            gameController.updateMenu(); 
            break;
        case STATE_GAME: 
            gameController.updateGame(); 
            break;
        case STATE_ABOUT: 
            gameController.updateAbout(); 
            break;
    }
}
