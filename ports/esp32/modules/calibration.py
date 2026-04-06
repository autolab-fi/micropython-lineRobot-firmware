import os
import time
import ujson

from lineRobot import Robot

SETTINGS_FILE = "settings.json"


def _load_settings():
    if SETTINGS_FILE in os.listdir():
        with open(SETTINGS_FILE, "r") as f:
            return ujson.load(f)
    return {}


def _save_settings(settings):
    with open(SETTINGS_FILE, "w") as f:
        ujson.dump(settings, f)
    if hasattr(os, "settings"):
        os.settings.update(settings)


def _update_settings(updates):
    settings = _load_settings()
    settings.update(updates)
    _save_settings(settings)


def _measure_straight_pass(robot, speed=35, duration_ms=1200):
    robot.reset_encoders()
    robot.reset_regulators()
    start_time = time.ticks_ms()

    try:
        while time.ticks_diff(time.ticks_ms(), start_time) < duration_ms:
            robot.run_motors_speed(speed, speed)
    finally:
        robot.stop()

    left_progress = abs(robot.encoder_radian_left())
    right_progress = abs(robot.encoder_radian_right())
    return left_progress, right_progress


def auto_calibrate_straight(speed=35, duration_ms=1200, passes=3):
    robot = Robot()
    total_left = 0.0
    total_right = 0.0

    print("Auto-calibration: straight-line encoder balancing started")
    print("Place robot on a long straight surface with free space ahead")
    time.sleep_ms(1500)

    for idx in range(passes):
        left_progress, right_progress = _measure_straight_pass(robot, speed=speed, duration_ms=duration_ms)
        total_left += left_progress
        total_right += right_progress
        print(
            "Pass {}: left={:.3f} rad right={:.3f} rad".format(
                idx + 1, left_progress, right_progress
            )
        )
        time.sleep_ms(700)

    avg_left = total_left / passes if passes else 0.0
    avg_right = total_right / passes if passes else 0.0
    if avg_left <= 0.01 or avg_right <= 0.01:
        raise ValueError("Calibration failed: encoder progress too small")

    base_settings = _load_settings()
    current_ks = float(base_settings.get("ks", 80.0))
    current_msc = int(base_settings.get("msc", 25))
    mismatch_ratio = abs(avg_left - avg_right) / max(avg_left, avg_right)

    proposed_ks = current_ks
    if mismatch_ratio < 0.03:
        proposed_ks = max(30.0, current_ks * 0.95)
    elif mismatch_ratio > 0.12:
        proposed_ks = min(140.0, current_ks * 1.15)

    proposed_msc = current_msc
    if mismatch_ratio < 0.03:
        proposed_msc = max(10, current_msc - 2)
    elif mismatch_ratio > 0.12:
        proposed_msc = min(45, current_msc + 4)
    elif mismatch_ratio > 0.06:
        proposed_msc = min(40, current_msc + 2)

    _update_settings({
        "ks": round(proposed_ks, 2),
        "msc": int(proposed_msc),
    })

    print("Auto-calibration finished")
    print("Average left={:.3f} rad right={:.3f} rad mismatch={:.2%}".format(avg_left, avg_right, mismatch_ratio))
    print("Updated ks={} msc={}".format(round(proposed_ks, 2), int(proposed_msc)))

    return {
        "avg_left": avg_left,
        "avg_right": avg_right,
        "mismatch_ratio": mismatch_ratio,
        "ks": round(proposed_ks, 2),
        "msc": int(proposed_msc),
    }


def auto_calibrate_all():
    return auto_calibrate_straight()
