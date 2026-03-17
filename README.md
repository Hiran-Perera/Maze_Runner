# Maze_Runner
A FreeRTOS-based 2D embedded game for ESP32 with joystick control, real-time task scheduling, and ST7735 TFT display rendering.

## Overview

This project is a real-time embedded game developed on the ESP32 platform using FreeRTOS. The game uses multiple tasks to manage rendering, input handling, game logic, and event communication in a structured way. A joystick is used for player control, and all graphics are displayed on a 160x128 ST7735 TFT screen.

The project was built to demonstrate how real-time operating system concepts can be applied in an interactive embedded system. Instead of placing all logic inside a single loop, the game is organized into separate tasks that run concurrently. This improves responsiveness, code structure, and timing control.

## Main Features

- FreeRTOS-based multitasking architecture
- 2D side-scrolling gameplay on ESP32
- Joystick-based movement and control
- ST7735 TFT display output
- Task separation for input, update, and rendering
- Real-time obstacle handling and collision detection
- Menu, gameplay, and game over states
- Embedded game design with lightweight graphics

## Hardware Used

- ESP32 development board
- ST7735 TFT display
- Joystick module
- Connecting wires
- Breadboard or soldered setup
- Optional buzzer for sound effects

## Pin Configuration

Update this section if your wiring changes.

### Joystick Pins
- `JOY_X = GPIO 34`
- `JOY_Y = GPIO 35`
- `JOY_SW = GPIO 25`

### Output Pins
- `BL_PIN = GPIO 27`  
  TFT backlight control

- `BUZZER_PIN = GPIO 26`  
  Buzzer output

### TFT Display Pins
The TFT uses SPI communication. These pins are usually defined inside the TFT display library configuration or `User_Setup.h`.

ESP32 to ST7735 connection is:

- `MOSI` -> GPIO 23
- `SCLK` -> GPIO 18
- `CS` -> GPIO 5
- `DC` -> GPIO 2
- `RST` -> GPIO 4
- `VCC` -> 3.3V
- `GND` -> GND

Important:
Check your actual TFT wiring and library setup before uploading. The SPI pins may vary depending on your hardware configuration.

## Display Configuration

This game is designed for a:

- `ST7735 TFT display`
- Resolution: `160 x 128`
- SPI interface
- Full-color graphics output

The display is used to draw:

- Main menu
- Player character
- Obstacles
- Background elements
- Score or status text
- Game over and level complete screens

A sprite buffer is used to improve screen drawing performance and reduce visible flicker during gameplay.

## How the Game Works

The player controls a character that moves through obstacles in a side-scrolling level. The game continuously updates the player position, obstacle movement, collision checks, and screen rendering.

The joystick provides analog input for movement. Depending on your game version, the player may switch modes such as cube and jet movement. Obstacles, portals, and special level objects are placed along the map, and the player must avoid collisions to survive and complete the level.

## RTOS Design

This project uses FreeRTOS to divide the game into multiple real-time tasks.

### Why FreeRTOS Was Used

FreeRTOS was used to make the game more responsive and better organized. In an embedded game, several activities need to happen almost at the same time:

- Reading joystick input
- Updating player physics
- Checking collisions
- Rendering graphics
- Handling sound or events

Using separate tasks makes the code easier to manage and shows how multitasking works in a real embedded system.

### Task-Based Architecture

The game is split into logical tasks such as:

#### 1. Input Task
Reads joystick values and button states from the hardware.  
This task captures player actions and sends them to the rest of the game system.

#### 2. Game Logic Task
Updates the player state, obstacle positions, collisions, game states, and progression.  
This is the core gameplay task.

#### 3. Render Task
Draws each frame to the TFT display.  
This task updates the visible game world, menus, and UI elements.

#### 4. Audio or Event Task
If used, this task handles sound effects or triggered events separately from the main loop.

### RTOS Benefits in This Project

- Better separation of responsibilities
- Smoother input response
- Easier expansion of the game
- More realistic embedded systems design
- Demonstrates concurrent execution principles

## Software Structure

The code includes core embedded programming concepts such as:

- Enums for game states and object types
- Structs for level data and particles
- Real-time scheduling with FreeRTOS
- SPI graphics rendering
- Analog joystick input handling
- Collision detection logic
- State transitions between menu, gameplay, and end screens

Common parts of the program include:

- Game state definitions
- Player and obstacle logic
- Display initialization
- Input reading
- Render functions
- FreeRTOS task creation

## Example Game States

The game uses different states to control flow, such as:

- `Menu`
- `WaitingToStart`
- `Running`
- `GameOver`
- `LevelComplete`

This makes the game easier to manage because each screen or phase behaves independently.

## Controls

### Joystick
- Horizontal or vertical movement depending on the game mode
- Button press can be used to start the game or trigger actions

### Typical Use
- Move through the menu
- Start the game
- Control the player during obstacles
- Restart after losing

## Development Goal

The purpose of this project was not only to make a playable game, but also to apply RTOS concepts in a practical embedded system. It demonstrates how real-time scheduling and task separation can be used in an ESP32 game environment.

This makes the project useful for:

- Embedded systems coursework
- RTOS demonstrations
- ESP32 graphics projects
- Real-time game experiments
- Educational presentations

## Libraries and Tools

This project uses:

- Arduino framework
- FreeRTOS
- SPI
- TFT display library such as `Adafruit_ST7735` or `TFT_eSPI`
- Standard C/C++ for embedded logic

Your exact libraries may vary depending on the version of the code.

## How to Build and Upload

### 1. Install Required Software
- Arduino IDE or PlatformIO
- ESP32 board package
- Required display libraries
- FreeRTOS support included with ESP32 Arduino core

### 2. Install Libraries
Install the necessary libraries used in your code, such as:
- `Adafruit_GFX`
- `Adafruit_ST7735`
or
- `TFT_eSPI`

### 3. Configure the TFT Display
Set the correct SPI pins and display driver in the library configuration.

### 4. Connect Hardware
Wire the ESP32, joystick, and TFT display according to the pin configuration.

### 5. Upload the Code
Select the correct ESP32 board and COM port, then upload the sketch.

### 6. Run the Game
Power the ESP32 and use the joystick to interact with the game.

## Project Learning Outcomes

This project helped develop skills in:

- FreeRTOS task design
- Embedded game programming
- ESP32 peripheral integration
- SPI display control
- Real-time input processing
- State machine design
- System-level debugging
