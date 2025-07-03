# Robot Control Library Documentation

A MicroPython library for controlling differential drive robots with encoder feedback and PID control.

## Quick API Reference

### Basic Movement
- `move_forward_distance(dist)` - Move forward by distance (cm)
- `move_backward_distance(dist)` - Move backward by distance (cm)
- `move_forward_seconds(seconds)` - Move forward for time duration
- `move_backward_seconds(seconds)` - Move backward for time duration

### Turning
- `turn_left()` - Turn left 90°
- `turn_right()` - Turn right 90°
- `turn_left_angle(angle)` - Turn left by specific angle
- `turn_right_angle(angle)` - Turn right by specific angle
- `rotate()` - Rotate 180°

### Motor Control
- `run_motors_speed(left_speed, right_speed)` - Run motors at percentage speeds (-100 to 100)
- `stop()` - Stop both motors

### Encoder Access
- `encoder_position_left/right` - Raw encoder counts
- `encoder_degrees_left/right()` - Position in degrees
- `encoder_radian_left/right()` - Position in radians
- `get_speed_l/r()` - Wheel speed in rad/s

---

## Contents

1. [Overview](#overview)
2. [Hardware Requirements](#hardware-requirements)
3. [Configuration](#configuration)
4. [Basic Usage](#basic-usage)
5. [Advanced Features](#advanced-features)
6. [Method Reference](#method-reference)
7. [Examples](#examples)

## Overview

This library provides high-level control for a differential drive robot with:
- **Dual motor control** with PWM speed regulation
- **Quadrature encoder feedback** for precise positioning
- **PID control** for smooth motion and accuracy
- **Configurable parameters** stored in JSON format
- **Blocking protection** to prevent multiple simultaneous commands

The robot uses separate PID controllers for:
- Speed control (maintaining target wheel speeds)
- Angle control (precise distance/rotation movements)
- Straight-line correction (preventing drift)

## Hardware Requirements

### Motors
- 2x DC motors with PWM control (4 pins per motor)
- Left motor: pins for forward/backward control
- Right motor: pins for forward/backward control

### Encoders
- 2x Quadrature encoders (2 pins per encoder)
- Left encoder: A and B phase pins
- Right encoder: A and B phase pins

### Default Pin Configuration
```python
# Motor pins
Left Motor:  pins 25, 26
Right Motor: pins 33, 32

# Encoder pins  
Left Encoder:  pins 35, 34
Right Encoder: pins 14, 27
```

## Configuration

The robot loads configuration from `settings.json` or uses defaults:

```json
{
  "version": 1,
  "pml1": 25, "pml2": 26,     // Left motor pins
  "pmr1": 33, "pmr2": 32,     // Right motor pins
  "pel1": 35, "pel2": 34,     // Left encoder pins
  "per1": 14, "per2": 27,     // Right encoder pins
  "wrad": 3.05,               // Wheel radius (cm)
  "wdist": 18.4,              // Distance between wheels (cm)
  "er": 2376,                 // Encoder pulses per revolution
  "kpa": 45.0,                // Angle PID - Proportional
  "kia": 100.0,               // Angle PID - Integral
  "kda": 2.5,                 // Angle PID - Derivative
  "kpsl": 41.5,               // Left speed PID - Proportional
  "kpsr": 28.0,               // Right speed PID - Proportional
  "kdsl": 0.5,                // Left speed PID - Derivative
  "kdsr": 0.1,                // Right speed PID - Derivative
  "kis": 0.0,                 // Speed PID - Integral
  "ks": 80.0,                 // Straight-line correction factor
  "maxs": 15.0                // Maximum speed (rad/s)
}
```

## Basic Usage

### Initialize Robot
```python
from lineRobot import Robot

# Use default configuration
robot = Robot()

# Or override specific parameters
robot = Robot(wrad=3.2, maxs=12.0)
```

### Simple Movement
```python
# Move forward 20 cm
robot.move_forward_distance(20)

# Move backward 15 cm  
robot.move_backward_distance(15)

# Turn left 90 degrees
robot.turn_left()

# Turn right 45 degrees
robot.turn_right_angle(45)
```

### Timed Movement
```python
# Move forward for 2 seconds
robot.move_forward_seconds(2)

# Move backward for 1.5 seconds
robot.move_backward_seconds(1.5)
```

## Advanced Features

### Manual Speed Control
```python
# Run left motor at 50%, right at 60%
robot.run_motors_speed(50, 60)

# Run in opposite directions
robot.run_motors_speed(-30, 30)  # Spin left

# Stop motors
robot.stop()
```

### Encoder Monitoring
```python
# Get current positions
left_pos = robot.encoder_position_left
right_pos = robot.encoder_position_right

# Get positions in degrees
left_deg = robot.encoder_degrees_left()
right_deg = robot.encoder_degrees_right()

# Get current wheel speeds
left_speed = robot.get_speed_l()
right_speed = robot.get_speed_r()
```

## Method Reference

### Movement Methods
- `move_forward_distance(dist)` - Move forward by distance in cm
- `move_backward_distance(dist)` - Move backward by distance in cm
- `move_forward_speed_distance(speed, dist)` - Move forward with custom speed percentage
- `move_backward_speed_distance(speed, dist)` - Move backward with custom speed percentage
- `move_forward_seconds(seconds)` - Move forward for specified time
- `move_backward_seconds(seconds)` - Move backward for specified time

### Rotation Methods
- `turn_left()` - Turn left 90 degrees
- `turn_right()` - Turn right 90 degrees
- `turn_left_angle(angle)` - Turn left by specified degrees
- `turn_right_angle(angle)` - Turn right by specified degrees
- `rotate()` - Rotate 180 degrees

### Motor Control Methods
- `run_motors_speed(left_speed, right_speed)` - Run motors at percentage speeds
- `run_motor_left(pwm_value)` - Run left motor with raw PWM (-1023 to 1023)
- `run_motor_right(pwm_value)` - Run right motor with raw PWM (-1023 to 1023)
- `stop()` - Stop both motors
- `stop_motor_left()` - Stop left motor only
- `stop_motor_right()` - Stop right motor only

### Encoder Methods
- `encoder_degrees_left()` - Get left encoder position in degrees
- `encoder_degrees_right()` - Get right encoder position in degrees
- `encoder_radian_left()` - Get left encoder position in radians
- `encoder_radian_right()` - Get right encoder position in radians
- `get_speed_l(interval)` - Get left wheel speed in rad/s
- `get_speed_r(interval)` - Get right wheel speed in rad/s
- `get_speed_motors(interval)` - Get both wheel speeds
- `reset_encoders()` - Reset both encoder positions to zero
- `reset_left_encoder()` - Reset left encoder to zero
- `reset_right_encoder()` - Reset right encoder to zero

### Utility Methods
- `reset_regulators()` - Reset PID controller states
- `constrain(value, min_val, max_val)` - Constrain value within range
- `set_block_true()` - Enable blocking mode

## Examples

### Example 1: Square Path
```python
from lineRobot import Robot

robot = Robot()

# Draw a square (20cm sides)
for i in range(4):
    robot.move_forward_distance(20)
    robot.turn_right()
```

### Example 2: Speed Control
```python
from lineRobot import Robot

robot = Robot()

# Move forward at 80% speed for 30cm
robot.move_forward_speed_distance(80, 30)

# Move backward at 60% speed for 15cm
robot.move_backward_speed_distance(60, 15)
```

### Example 3: Precise Rotation
```python
from lineRobot import Robot

robot = Robot()

# Turn to specific angles
robot.turn_left_angle(30)   # Turn left 30°
robot.turn_right_angle(60)  # Turn right 60°
robot.turn_left_angle(30)   # Turn left 30° (back to start)
```

### Example 4: Manual Motor Control
```python
from lineRobot import Robot
import time

robot = Robot()

# Spin in place for 2 seconds
robot.run_motors_speed(-50, 50)  # Left backward, right forward
time.sleep(2)
robot.stop()

# Drive in arc
robot.run_motors_speed(30, 60)   # Right motor faster
time.sleep(3)
robot.stop()
```

### Example 5: Encoder Monitoring
```python
from lineRobot import Robot
import time

robot = Robot()

# Monitor position while moving
robot.reset_encoders()
robot.run_motors_speed(40, 40)

for i in range(10):
    left_pos = robot.encoder_degrees_left()
    right_pos = robot.encoder_degrees_right()
    print(f"Left: {left_pos}°, Right: {right_pos}°")
    time.sleep(0.5)

robot.stop()
```

### Example 6: Configuration Override
```python
from lineRobot import Robot

# Create robot with custom wheel radius and max speed
robot = Robot(wrad=3.2, maxs=12.0, kpa=50.0)

# Robot will use these values instead of defaults
robot.move_forward_distance(25)
```

---

## Notes

- All distances are in centimeters
- All angles are in degrees
- Speed percentages range from -100 to 100
- The robot automatically handles PID control for smooth movement
- Encoder interrupts provide real-time position feedback
- The library includes safety features like motor constraining and smooth acceleration
