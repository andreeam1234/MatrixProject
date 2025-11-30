# Sokoban Matrix Game

## Project Description

Sokoban puzzle game implementation on 8x8 LED matrix with LCD, joystick and IMU sensor.

## Checkpoint 1 

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

