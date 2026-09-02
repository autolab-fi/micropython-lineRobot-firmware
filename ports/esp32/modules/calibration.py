import time
import ujson

from calibration_math import summarize_straight_passes
from lineRobot import Robot


def _validate_run_id(run_id):
    if not isinstance(run_id, str) or not run_id or len(run_id) > 48:
        raise ValueError("Calibration run_id must contain 1 to 48 characters")
    for char in run_id:
        if not (char.isalnum() or char in "-_"):
            raise ValueError("Calibration run_id contains an unsupported character")


def _emit(prefix, payload):
    print(prefix + ujson.dumps(payload))


def _coefficient_snapshot(robot):
    return {
        "wrad": robot.RADIUS_WHEEL,
        "wdist": robot.distance_between_wheel_and_center * 2,
        "er": robot.pulses_per_revolution,
        "kpsl": robot.kp_speed_left,
        "kpsr": robot.kp_speed_right,
        "ks": robot.k_straight,
        "maxs": robot.max_speed_radians,
    }


def _measure_straight_pass_with_telemetry(
    robot,
    run_id,
    pass_index,
    direction,
    speed,
    duration_ms,
    sample_interval_ms,
):
    robot.reset_encoders()
    robot.reset_regulators()
    start_time = time.ticks_ms()
    next_sample_ms = 0
    signed_speed = speed * direction

    _emit("CALIBRATION_EVENT ", {
        "run": run_id,
        "event": "pass_start",
        "pass": pass_index,
        "dir": direction,
    })
    try:
        while time.ticks_diff(time.ticks_ms(), start_time) < duration_ms:
            robot.run_motors_speed(signed_speed, signed_speed)
            elapsed_ms = time.ticks_diff(time.ticks_ms(), start_time)
            if elapsed_ms >= next_sample_ms:
                _emit("CALIBRATION_SAMPLE ", {
                    "run": run_id,
                    "pass": pass_index,
                    "t_ms": elapsed_ms,
                    "dir": direction,
                    "lr": robot.encoder_radian_left(),
                    "rr": robot.encoder_radian_right(),
                    "ls": robot.current_speed_left,
                    "rs": robot.current_speed_right,
                    "lp": robot.left_motor_signal,
                    "rp": robot.right_motor_signal,
                })
                next_sample_ms += sample_interval_ms
    finally:
        robot.stop(reset_encoders=False)

    left_progress = abs(robot.encoder_radian_left())
    right_progress = abs(robot.encoder_radian_right())
    robot.reset_encoders()
    result = {
        "pass": pass_index,
        "dir": direction,
        "left_rad": left_progress,
        "right_rad": right_progress,
    }
    event = {"run": run_id, "event": "pass_end"}
    event.update(result)
    _emit("CALIBRATION_EVENT ", event)
    return result

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


def measure_straight_bidirectional(
    run_id,
    speed=20,
    duration_ms=600,
    cycles=2,
    sample_interval_ms=50,
):
    """Measure forward/reverse passes without changing persistent settings."""
    _validate_run_id(run_id)
    if speed < 10 or speed > 30:
        raise ValueError("Calibration speed must be between 10 and 30 percent")
    if duration_ms < 300 or duration_ms > 1000:
        raise ValueError("Calibration duration must be between 300 and 1000 ms")
    if cycles < 1 or cycles > 3:
        raise ValueError("Calibration cycles must be between 1 and 3")
    if sample_interval_ms < 40 or sample_interval_ms > 200:
        raise ValueError("Calibration sample interval must be between 40 and 200 ms")

    robot = None
    passes = []
    try:
        robot = Robot()
        _emit("CALIBRATION_EVENT ", {
            "run": run_id,
            "event": "run_start",
            "speed": speed,
            "duration_ms": duration_ms,
            "cycles": cycles,
            "coefficients": _coefficient_snapshot(robot),
        })
        time.sleep_ms(700)
        pass_index = 1
        for _ in range(cycles):
            for direction in (1, -1):
                passes.append(_measure_straight_pass_with_telemetry(
                    robot,
                    run_id,
                    pass_index,
                    direction,
                    speed,
                    duration_ms,
                    sample_interval_ms,
                ))
                pass_index += 1
                time.sleep_ms(500)

        result = {
            "status": "measured",
            "mode": "straight_bidirectional",
            "measurement_only": True,
            "run": run_id,
            "speed_percent": speed,
            "duration_ms": duration_ms,
            "cycles": cycles,
            "passes": passes,
            "coefficients": _coefficient_snapshot(robot),
        }
        _emit("CALIBRATION_EVENT ", {"run": run_id, "event": "run_end"})
        _emit("CALIBRATION_RESULT ", result)
        return result
    except Exception as exc:
        if robot is not None:
            robot.stop(reset_encoders=False)
        result = {
            "status": "error",
            "mode": "straight_bidirectional",
            "measurement_only": True,
            "run": run_id,
            "message": str(exc),
        }
        _emit("CALIBRATION_RESULT ", result)
        raise


def auto_calibrate_all():
    # Kept as a compatibility alias until turn and square measurements are
    # orchestrated by the camera-aware worker.
    return auto_calibrate_straight()
