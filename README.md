# Sokoban Matrix Game

## Project Description

Sokoban puzzle game implementation on 8x8 LED matrix with LCD, joystick and IMU sensor.

# Checkpoint 1 

## Task requirements
This project is a small interactive system built with an Arduino, a joystick, an LCD screen, and an 8×8 LED matrix driven by a MAX7219. The goal was to create a menu you can scroll through using the joystick, show a short intro message at startup, and build a simple game where the player moves around a larger 16×16 “virtual map” while only seeing an 8×8 window on the LED matrix. The joystick controls both the menu and the movement, and a buzzer gives sound feedback for actions. Everything updates smoothly, and the code is organized into clear parts (input handling, rendering, game logic) so it’s easy to understand and extend later. This serves as the foundation for the full matrix game that will be developed in the next checkpoint.

## Hardware Components

| Component | Model | Purpose |
|-----------|-------|---------|
| Microcontroller | Arduino Uno | Main processing unit |
| LED Matrix | 8x8 MAX7219 | Game display |
| LCD | 16x2 | Menu and HUD |
| Joystick | 2-axis analog + button | Input (Checkpoint 1) |
| IMU | MPU6050 | Tilt control (Checkpoint 2+) |
| Buzzer | Passive | Audio feedback |
| Button | Push button | Undo function (future) |

## Wiring Diagram

### LCD (16x2)
```
RS  → Pin 9
EN  → Pin 8
D4  → Pin 7
D5  → Pin 6
D6  → Pin 5
D7  → Pin 4
VSS → GND
VDD → 5V
V0  → Potentiometer (contrast)
A   → 5V (backlight)
K   → GND
```

### LED Matrix (MAX7219)
```
VCC → 5V
GND → GND
DIN → Pin 12
CLK → Pin 11
CS  → Pin 10
```

### Joystick
```
VCC → 5V
GND → GND
VRx → A0
VRy → A1
SW  → Pin 2
```

### Buzzer
```
+ → Pin 3
- → GND
```

## Checkpoint 1 - Interactive Drawing 

### Features Implemented

- **LCD Menu System**: Navigate between Start Demo and About
- **Interactive Drawing Mode**: Move cursor on 8x8 matrix
- **Real-time HUD**: LCD displays cursor position (X, Y) 
- **Audio Feedback**: Different tones for navigation, selection, and toggle
- **Exit Function**: Hold button 2 seconds to return to menu

### Usage Instructions

#### Menu Navigation
- **Joystick UP/DOWN**: Select menu option
- **Press Button**: Confirm selection
- **Options**: Start Demo | About

#### Drawing Mode
1. **Move**: Push joystick in any direction
2. **Monitor**: LCD shows `X:_ Y:_ [STATE]`
3. **Exit**: Hold button for 2 seconds

### Picture of the setup
<img width="2048" height="1536" alt="image" src="https://github.com/user-attachments/assets/8d936e22-4c1a-4353-a416-6177b4f35e86" />
<img width="2048" height="1536" alt="image" src="https://github.com/user-attachments/assets/1c8cae66-b353-4436-8fc4-2e2109590d8e" />

### Functionality video
https://youtu.be/j4vbeKR538k?si=LQfCYmYOxDWTJvJt

## Final Game Direction

### **Sokoban on LED Matrix (with IMU/Joystick control)**  
The final project will be a simplified Sokoban puzzle game where:

- The player moves on the LED matrix  
- Boxes can be pushed into target positions  
- IMU sensor will be used for extended control (e.g., UNDO gesture)  
- LCD will display game information and level stats  

---

## Checkpoint 2 Plan
- Movement system for Sokoban player  
- Box pushing logic  
- Collision with walls  
- First playable Sokoban level  
  - **UNDO**
  - **PAUSE**
---

# Checkpoint 2

## 1. Task Requirements

This project was created for the "Introduction to Robotics" course. The main objective was to create a game using an LED Matrix and an LCD display.

**Functionalities implemented:**
* **Menu System:** A navigable menu on the LCD (Start Game, Highscore, Settings, About, How to Play).
* **Virtual Map:** The game runs on a 16x16 virtual map, rendered on an 8x8 physical matrix (camera viewport logic).
* **Movement:** Controlled via Joystick.
* **Gestures:** IMU (MPU6050) integration allowing the player to "Undo" the last move by physically tilting the setup.
* **Persistence:** Highscores and settings (Player Name, Sound ON/OFF) are saved in the EEPROM.
* **Audio:** Sound effects for movement, selection, and winning.
* **Levels:** Multiple levels with increasing difficulty.

## 2. Pictures of the Setup
<img width="2048" height="1536" alt="image" src="https://github.com/user-attachments/assets/15ee7a7d-8e99-441b-b696-5b731f832b69" />

## 3. Functionality video
https://www.youtube.com/watch?si=3_toPJXLSHWIAQbD&v=ATic-cvk5uQ&feature=youtu.be

## 4. Hardware Components

| Component | Model | Purpose |
|-----------|-------|---------|
| Microcontroller | Arduino Uno | Main processing unit |
| LED Matrix | 8x8 MAX7219 | Game display |
| LCD | 16x2 | Menu and HUD |
| Joystick | 2-axis analog + button | Input |
| IMU | MPU6050 | Tilt control |
| Buzzer | Passive | Audio feedback |
| Button | Push button | Undo function (future) |

* **Other:**
    * Breadboard & Jumper wires
    * Resistors (for LCD contrast/backlight if needed)
    * 10uF Capacitor (for power smoothing, optional)

## 5. Circuit Diagram & Pinout

### LCD 1602 (Parallel Connection)
| LCD Pin | Arduino Pin |
| :--- | :--- |
| RS | 9 |
| EN | 8 |
| D4 | 7 |
| D5 | 6 |
| D6 | 5 |
| D7 | 4 |
| V0 | Potentiometer |

### MAX7219 Matrix
| Matrix Pin | Arduino Pin |
| :--- | :--- |
| DIN | 12 |
| CLK | 11 |
| CS | 10 |

### Peripherals
* **Joystick X:** A0
* **Joystick Y:** A1
* **Joystick Switch:** Digital 2
* **Buzzer:** Digital 13
* **MPU6050:** SDA (A4) / SCL (A5) 

## 6. How to Play

1.  **Navigation:** Use the Joystick to move the player (blinking dot) through the maze.
2.  **Objective:** Reach the blinking exit point.
3.  **Undo Move:** If you make a mistake, **tilt the entire breadboard** (detected by MPU6050) to step back to the previous position.
4.  **Menu:** Use the Joystick up/down to scroll and the button to select. Long press the button inside the Settings menu to save and exit.

## 7. Libraries Used

* `LiquidCrystal.h` - Standard Arduino library for LCDs.
* `LedControl.h` - For controlling the MAX7219 LED Matrix.
* `Adafruit_MPU6050.h` - For the accelerometer.
* `Adafruit_Sensor.h` - Dependency for the MPU6050.
* `EEPROM.h` - For saving data to non-volatile memory.
