import time
import ujson

from calibration_math import summarize_straight_passes
from lineRobot import Robot

def _measure_straight_pass(robot, speed=20, duration_ms=600):
    robot.reset_encoders()
    robot.reset_regulators()
    start_time = time.ticks_ms()

    try:
        while time.ticks_diff(time.ticks_ms(), start_time) < duration_ms:
            robot.run_motors_speed(speed, speed)
    finally:
        # Preserve encoder values until the measurement has been captured.
        robot.stop(reset_encoders=False)

    left_progress = abs(robot.encoder_radian_left())
    right_progress = abs(robot.encoder_radian_right())
    robot.reset_encoders()
    return left_progress, right_progress


def auto_calibrate_straight(speed=20, duration_ms=600, passes=3):
    if speed < 10 or speed > 60:
        raise ValueError("Calibration speed must be between 10 and 60 percent")
    if duration_ms < 300 or duration_ms > 5000:
        raise ValueError("Calibration duration must be between 300 and 5000 ms")
    if passes < 1 or passes > 10:
        raise ValueError("Calibration passes must be between 1 and 10")

    try:
        robot = Robot()
        measurements = []

        print("Calibration measurement: straight-line encoder pass started")
        print("Place robot on a long straight surface with free space ahead")
        time.sleep_ms(1500)

        for idx in range(passes):
            left_progress, right_progress = _measure_straight_pass(robot, speed=speed, duration_ms=duration_ms)
            measurements.append((left_progress, right_progress))
            print(
                "Pass {}: left={:.3f} rad right={:.3f} rad".format(
                    idx + 1, left_progress, right_progress
                )
            )
            time.sleep_ms(700)

        result = summarize_straight_passes(measurements)
        result.update({
            "status": "measured",
            "mode": "straight",
            "measurement_only": True,
            "speed_percent": speed,
            "duration_ms": duration_ms,
        })

        print("Calibration measurement finished; settings were not changed")
        print("CALIBRATION_RESULT " + ujson.dumps(result))
        return result
    except Exception as exc:
        result = {
            "status": "error",
            "mode": "straight",
            "measurement_only": True,
            "message": str(exc),
        }
        print("CALIBRATION_RESULT " + ujson.dumps(result))
        raise


def auto_calibrate_all():
    # Kept as a compatibility alias until turn and square measurements are
    # orchestrated by the camera-aware worker.
    return auto_calibrate_straight()
