# Robot Configuration Parameters

This guide describes the coefficients and pins used by the robot firmware. Each entry explains its purpose, expected value range or magnitude, and the impact on behavior.

## Pin mapping
- **pml1 / pml2** — GPIOs for the left motor (PWM). Range: ESP32 pins (usually 0–39). Wrong values prevent movement in the intended direction.
- **pmr1 / pmr2** — GPIOs for the right motor. Range: 0–39. Incorrect pins cause crossed or missing control of the right wheel.
- **pel1 / pel2** — encoder phase pins for the left wheel. Range: input GPIO 0–39. Wrong selection distorts pulse counting, speed, and odometry.
- **per1 / per2** — encoder phase pins for the right wheel. Range: input GPIO 0–39. Errors lead to incorrect speed/distance estimation on the right wheel.
- **pled1 / pled2** — indicator LED pins. Range: 0–39. Used for visual status; wrong pins simply disable indication.
- **pclk / psda** — SCL/SDA pins for the I²C bus. Range: 0–39. Sensors/displays are connected here; misnumbering blocks I²C operation.
- **pbat** — analog input for measuring battery voltage. Range: ADC‑capable ESP32 pins. Using an unsuitable pin produces incorrect battery monitoring.
- **pch** — pin for charge/power-source detection. Range: 0–39. If wrong, power-status indication will be incorrect.

## Geometry and encoders
- **wrad** — wheel radius in centimeters. Typical values 2.5–4.5 cm. Underestimating raises the computed speed; overestimating lowers the distance estimate.
- **wdist** — distance between wheels in centimeters. Usually 15–25 cm. Affects arc calculation during turns; bad values cause understeer or oversteer.
- **er** — encoder pulses per full wheel revolution. Magnitude: 2000–3000 pulses/rev. Smaller numbers make the robot less sensitive to small motions; larger numbers make it more “nervous,” overestimating speed.

## Speed limits
- **maxs** — maximum allowed angular wheel speed, rad/s. Typical 10–20 rad/s. Used for controller saturation, limiting the commanded speed.
- **ks** — straightness correction gain. Typical magnitude 50–150. Higher values more aggressively reduce speed mismatch between wheels when driving straight.

## PID for angle (distance/heading moves)
- **kpa** — proportional gain for the angular controller. Magnitude 30–150. Higher values increase reaction to angle/distance error.
- **kia** — integral gain for the angular controller. Magnitude 50–120. Speeds up removal of steady-state error but causes overshoot if too high.
- **kda** — derivative gain for the angular controller. Magnitude 1–5. Dampens fast error changes.

## PID for wheel speed
- **kpsl / kpsr** — proportional gains for left/right wheel speed controllers. Typical magnitude 20–50. Define how strongly PWM reacts to mismatch between actual and target speed.
- **kis** — integral gain for the speed controller (shared for both wheels). Magnitude 0–10. Removes long-term speed error.
- **kdsl / kdsr** — derivative gains for the speed controllers. Magnitude 0–1. Help stabilize speed during quick load changes.

## Modes and debugging
- **ks** — straight-line hold coefficient (duplicated from speed limits for convenience). Magnitude 50–150. Higher values give more aggressive drift compensation.
- **debug** — flag for debug output (0 — off, 1 — on). Does not affect motion but is useful during tuning.

### Practical tuning steps
1. Start with geometry: set `wrad`, `wdist`, `er` according to the mechanics and encoder specs.
2. Verify motor and encoder pins: adjust `pml*`, `pmr*`, `pel*`, `per*` to match your board if needed.
3. Tune angle PID (`kpa`, `kia`, `kda`) for stable turns and accurate distance moves.
4. Tune speed PID (`kpsl`, `kpsr`, `kis`, `kdsl`, `kdsr`) so wheels ramp smoothly without jerks.
5. Adjust `ks` and `maxs` to balance straight-line stability and top speed.
6. Add indication and telemetry via `pled*`, `pclk/psda`, `pbat`, `pch` if desired.
