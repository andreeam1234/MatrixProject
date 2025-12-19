# Maze Runner Matrix Game

This repository contains the source code and documentation for my Arduino-based Matrix Project. It is a complex maze crawler game featuring an 8x8 LED Matrix for gameplay, a 16x2 LCD for the HUD/Menu, and an IMU (Accelerometer) for gesture-based mechanics.

## 1. Task Requirements
The project requirements were to create a standalone game using an Arduino, an LED Matrix, and an LCD display. Key constraints and features implemented include:
- **Menu System:** A fully navigable menu with Start, Highscore, Settings, About, and Help sections.
- **Game Logic:** A functional game with levels, score tracking, enemies, and win/loss states.
- **Hardware Interaction:** Usage of a Joystick for movement and Buttons for selection.
- **Advanced Sensor Integration:** Utilization of an MPU6050 Accelerometer for specific game mechanics ("Shake to Freeze").
- **Data Persistence:** Saving Highscores and Settings (Name, Brightness, Sound) to EEPROM to retain data

## 2. Picture of the Setup
<img width="2048" height="1622" alt="image" src="https://github.com/user-attachments/assets/1d850e42-8a7b-4f02-a847-206e9bb0296c" />

## 3. Functionality Video
https://youtu.be/6EoG8MCU07M?si=uCDSjojT18ylCPnK

*Video demonstrates:*
- Menu navigation
- Gameplay across all 3 levels
- Enemy AI behavior
- Shake-to-freeze mechanic
- Settings customization
- Highscore system

## 4. Documentation

### Introduction & Backstory
I have always been a fan of classic dungeon crawlers and puzzle games. For this project, I wanted to recreate the feeling of exploration found in these type of games, but adapted for the limitations of an 8x8 display.

The inspiration comes from games like *Pac-Man* and *The Legend of Zelda* (dungeons). However, I wanted to add a physical twist to the gameplay. Instead of just pressing buttons, I wanted the player to physically interact with the console. This led to the integration of the **MPU6050 Accelerometer**. If the player gets overwhelmed by enemies, they can physically **shake the entire console** to unleash a "shockwave" that freezes enemies for a few seconds.

### Game Description
**The Maze Runner** is a level-based game where the map (16x16) is larger than the visible screen (8x8). The player must navigate a "virtual camera" view to explore the dark maze.

* **Objective:** Find the **KEY** (blinking fast) to unlock the **EXIT** (blinking slow).
* **Enemies:** AI-controlled dots that chase the player. Speed increases with difficulty.
* **Levels:** There are 3 distinct levels with increasing difficulty and different wall layouts.
* **Lives:** The player starts with 3 lives. Touching an enemy results in losing a life.

### How to Play

1.  **Menu Navigation:**
    * Use **Joystick Up/Down** to scroll through options.
    * Press **Joystick Button** to select.
    * **Long Press** Joystick Button to return to the previous menu.
2.  **In-Game Movement:** Use the **Joystick** to move the player (the steady dot) through the maze.
3.  **Interaction:**
    * Walk over the **Key** to collect it.
    * Walk into the **Exit** after collecting the key to finish the level.
4.  **Special Ability (THE SHAKE):**
    * If you are cornered by an enemy, **SHAKE** the controller/breadboard vigorously.
    * The accelerometer detects the G-force and **freezes** the enemies for 3 seconds.
    * *Note:* This has a cooldown/duration logic implemented in the code.

### Used Components

| Component | Role | Pin Connection (Arduino Uno) |
| :--- | :--- | :--- |
| **Arduino Uno** | Microcontroller | - |
| **8x8 LED Matrix** | Main Game Display | DIN: 12, CLK: 11, CS: 10 |
| **MAX7219 Driver** | Controls Matrix | (Integrated with Matrix) |
| **16x2 LCD Display** | HUD (Score, Lives) & Menu | RS: 9, EN: 8, D4: 7, D5: 6, D6: 5, D7: 4 |
| **Joystick Module** | Input (X/Y Axis + Button) | X: A0, Y: A1, SW: 2 |
| **MPU6050** | Accelerometer (Shake Detection) | SDA: A4, SCL: A5 |
| **Passive Buzzer** | Sound Effects | Pin 13 |
| **10kΩ Potentiometer** | LCD Contrast Adjustment | Connected to LCD VO |
| **Resistors/Wires** | Connections | - |

## 5. Code Architecture

### State Machine Pattern
The entire game is built around a **finite state machine** that controls which logic runs each frame:

```
┌─────────────────┐
│  STATE_IMU_INIT │ (Initialize accelerometer)
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   STATE_INTRO   │ (Show game title + animation)
└────────┬────────┘
         │
         ▼
┌─────────────────┐
│   STATE_MENU    │◄──────────────────┐
└────────┬────────┘                   │
         │                             │
    ┌────┴────┬─────────┬─────────┐  │
    ▼         ▼         ▼         ▼   │
┌────────┐┌────────┐┌────────┐┌──────┴───┐
│  GAME  ││HIGH-   ││SETTINGS││  ABOUT/  │
│        ││SCORE   ││        ││  HELP    │
└────────┘└────────┘└────────┘└──────────┘
    │
    ├─► STATE_WAIT_LEVEL_NEXT (Transition between levels)
    │
    ├─► STATE_WAIT_RESPAWN (Player hit, respawning)
    │
    ├─► STATE_GAMEOVER (Out of lives)
    │
    └─► STATE_WIN (Beat all levels)
```

**Benefits of State Machine:**
- Clear separation of concerns (menu logic ≠ game logic)
- Easy to debug (know exactly which code is running)
- Memory efficient (only one state active at a time)
- Prevents input bleeding between screens

### Settings Sub-State Machine
The settings menu uses a **nested state machine** for cleaner code:

```
STATE_SETTINGS
    │
    └─► SET_MENU (Browsing settings)
         ├─► SET_NAME (Editing player name)
         ├─► SET_LCD (Adjusting LCD brightness)
         ├─► SET_MATRIX (Adjusting matrix brightness)
         ├─► SET_SOUND_CONFIRM (Showing ON/OFF confirmation)
         └─► SET_RESET_CONFIRM (Confirming highscore reset)
```

**Why a sub-state machine?**
- Prevents complex boolean flag combinations
- Each sub-state has clear entry/exit logic
- Easy to add new settings without breaking existing code
- Makes debouncing and input handling reliable

### Memory Layout

#### EEPROM Map (1KB total)
```
Address 0-49:   Settings struct (50 bytes)
    - Player name (6 bytes including \0)
    - Sound on/off (1 byte)
    - LCD brightness (1 byte)
    - Matrix brightness (1 byte)
    - Magic number (1 byte)
    - Padding (40 bytes reserved)

Address 50-79:  HighScore slot 1 (10 bytes)
    - Name (6 bytes)
    - Score (4 bytes as int)

Address 80-109: HighScore slot 2 (10 bytes)

Address 110-139: HighScore slot 3 (10 bytes)

Address 140+:   Unused
```


## 6. Algorithm Details

### Enemy Pathfinding
The enemy uses a **greedy chase algorithm** with obstacle avoidance:

```cpp
// Pseudocode for enemy movement
Calculate dx = playerX - enemyX
Calculate dy = playerY - enemyY

If abs(dx) > abs(dy):
    // Move horizontally first
    Try moving toward player on X axis
    If blocked:
        Try moving toward player on Y axis
Else:
    // Move vertically first
    Try moving toward player on Y axis
    If blocked:
        Try moving toward player on X axis
```

**Algorithm Properties:**
- Always makes progress toward player (unless fully blocked)
- Can navigate around simple obstacles
- Deterministic (not random)


### Camera System
The 8×8 viewport follows the player with smart boundary handling:

```
16×16 Map:                  8×8 Viewport:
┌────────────────┐         ┌────────┐
│                │         │ ▓▓▓▓   │  Camera follows player
│   ┌────────┐   │         │ ▓░░▓   │  but stays within bounds
│   │ ▓▓▓▓   │   │         │ ▓P▓▓   │
│   │ ▓░░▓   │◄──┼─────────│ ▓▓▓▓   │  P = Player
│   │ ▓P▓▓   │   │         └────────┘  ▓ = Wall
│   │ ▓▓▓▓   │   │                     ░ = Empty
│   └────────┘   │         Camera position calculated:
│                │         camX = constrain(playerX - 4, 0, 8)
│                │         camY = constrain(playerY - 4, 0, 8)
└────────────────┘
```

**Camera Behavior:**
- **Centered:** When player is in middle of map, camera centers on them
- **Edge-locked:** When player near edge, camera stops scrolling
- **Smooth:** Updates every frame with player movement

**Implementation:**
```cpp
void updateCamera() {
    // Try to center player in 8×8 view
    // constrain() prevents camera from showing off-map area
    camX = constrain(playerX - VIEW_SIZE/2, 0, MAP_SIZE - VIEW_SIZE);
    camY = constrain(playerY - VIEW_SIZE/2, 0, MAP_SIZE - VIEW_SIZE);
}
```

### Collision Detection
```
Player movement logic:
┌─────────────────────────┐
│ Read joystick direction │
└────────┬────────────────┘
         │
         ▼
┌─────────────────────────┐
│ Calculate new position  │
│ newX = playerX + dx     │
│ newY = playerY + dy     │
└────────┬────────────────┘
         │
         ▼
┌─────────────────────────┐
│ Check if wall at (x,y)? │
└────┬──────────┬─────────┘
     NO        YES
     │          │
     ▼          ▼
  MOVE      BLOCKED
```

**Collision Types Handled:**
1. **Wall collision:** Check `wall[x][y]` array
2. **Boundary collision:** Check if `x >= MAP_SIZE` or `y >= MAP_SIZE`
3. **Enemy collision:** Check if `playerX == enemyX && playerY == enemyY`
4. **Item collision:** Check if player position matches key/exit

### IMU Shake Detection Algorithm
The accelerometer measures G-force in 3 axes (X, Y, Z):

```
Shake Detection Logic:
────────────────────────

1. Read acceleration:
   accelX = raw X-axis value (g)
   accelY = raw Y-axis value (g)
   accelZ = raw Z-axis value (g)

2. Calculate magnitude (ignore vertical axis):
   magnitude = √(accelX² + accelY²)
   
3. Compare to threshold:
   if magnitude > 15.0g:
       SHAKE DETECTED!

Why ignore Z-axis?
- Z measures vertical (up/down) movement
- We want horizontal shaking motion
- Prevents false triggers from lifting console
```

**Shake Detection Parameters:**
- **Threshold:** 15.0g (aggressive shake needed)
- **Cooldown:** 100ms between readings (prevent spam)
- **Effect Duration:** 3000ms (3 seconds freeze)
- **Throttling:** Can't activate while already frozen

**Math Behind It:**
```
Normal gravity = 1g
Sitting still = ~1g on Z-axis, ~0g on X/Y

Vigorous shake = 10-20g on X/Y axes
magnitude = √(15² + 10²) = √(225 + 100) = √325 ≈ 18g  Detected!

Small movement = 2-5g
magnitude = √(3² + 2²) = √(9 + 4) = √13 ≈ 3.6g  Not detected
```

---

## 7. Building the Project

### Required Libraries
Install via Arduino IDE Library Manager:
```
- LiquidCrystal (built-in)
- LedControl by Eberhard Fahle (v1.0.6+)
- Adafruit MPU6050 (v2.2.4+)
- Adafruit Unified Sensor (v1.1.9+)
- Wire (built-in)
- EEPROM (built-in)
```

### Wiring Diagram
```
Arduino Uno Connections:
────────────────────────

8×8 LED Matrix (MAX7219):
  VCC  → 5V
  GND  → GND
  DIN  → Pin 12
  CLK  → Pin 11
  CS   → Pin 10

16×2 LCD Display:
  VSS  → GND
  VDD  → 5V
  VO   → Potentiometer (contrast)
  RS   → Pin 9
  RW   → GND
  EN   → Pin 8
  D4   → Pin 7
  D5   → Pin 6
  D6   → Pin 5
  D7   → Pin 4
  A    → 5V (backlight)
  K    → GND

Joystick Module:
  GND  → GND
  +5V  → 5V
  VRx  → A0
  VRy  → A1
  SW   → Pin 2

MPU6050 (Accelerometer):
  VCC  → 5V
  GND  → GND
  SCL  → A5
  SDA  → A4

Passive Buzzer:
  +    → Pin 13
  -    → GND

Backlight Control (PWM):
  LCD A → Pin 3 (via transistor)
  
Potentiometer (Contrast):
  Left  → GND
  Middle→ LCD VO
  Right → 5V
```

## 8. Code Structure

### Main Classes & Structs
```cpp
// Data structures
struct Settings           // User preferences (EEPROM)
struct HighScore         // Leaderboard entry (EEPROM)
struct Enemy             // Enemy position
struct GameModel         // All game state (map, player, etc)

// Controllers
class IMUController      // Accelerometer interface
class InputController    // Joystick + button handling
class LCDRenderer        // All LCD display logic
class GameController     // Main game logic & updates

// State machines
enum GameState          // Top-level states
enum SettingsState      // Settings sub-states
```

### Key Functions Flow
```
setup()
  ├─→ initHardware()
  ├─→ loadSettings()
  └─→ Set currentState = STATE_IMU_INIT

loop()
  ├─→ updateTone()         (Stop sounds if done)
  └─→ switch(currentState):
        ├─ STATE_MENU → updateMenu()
        ├─ STATE_GAME → 
        │    ├─→ detectShake()
```


