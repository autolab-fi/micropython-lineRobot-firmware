import os
import sys
import importlib.util
import json
import types
import unittest


MODULES = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "modules"))
if MODULES not in sys.path:
    sys.path.insert(0, MODULES)

from calibration_math import summarize_straight_passes


class StraightCalibrationMathTests(unittest.TestCase):
    def test_balanced_passes(self):
        result = summarize_straight_passes([(4.0, 4.0), (6.0, 6.0)])
        self.assertEqual(result["avg_left_rad"], 5.0)
        self.assertEqual(result["avg_right_rad"], 5.0)
        self.assertEqual(result["mismatch_ratio"], 0.0)

    def test_signed_mismatch_preserves_slower_side(self):
        result = summarize_straight_passes([(11.0, 9.0)])
        self.assertAlmostEqual(result["signed_mismatch_ratio"], 0.2)
        self.assertAlmostEqual(result["mismatch_ratio"], 0.2)

    def test_rejects_empty_or_stalled_measurements(self):
        with self.assertRaises(ValueError):
            summarize_straight_passes([])
        with self.assertRaises(ValueError):
            summarize_straight_passes([(1.0, 0.0)])


class StraightCalibrationHardwareBoundaryTests(unittest.TestCase):
    def test_measurement_reads_encoders_before_reset(self):
        fake_line_robot = types.ModuleType("lineRobot")
        fake_line_robot.Robot = object
        original_line_robot = sys.modules.get("lineRobot")
        original_ujson = sys.modules.get("ujson")
        sys.modules["lineRobot"] = fake_line_robot
        sys.modules["ujson"] = json
        try:
            spec = importlib.util.spec_from_file_location(
                "calibration_under_test", os.path.join(MODULES, "calibration.py")
            )
            calibration = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(calibration)
        finally:
            if original_line_robot is None:
                sys.modules.pop("lineRobot", None)
            else:
                sys.modules["lineRobot"] = original_line_robot
            if original_ujson is None:
                sys.modules.pop("ujson", None)
            else:
                sys.modules["ujson"] = original_ujson

        class FakeTime:
            current = 0

            @classmethod
            def ticks_ms(cls):
                cls.current += 2
                return cls.current

            @staticmethod
            def ticks_diff(current, start):
                return current - start

        class FakeRobot:
            reset = False
            stopped_without_reset = False

            def reset_encoders(self):
                self.reset = True

            def reset_regulators(self):
                pass

            def run_motors_speed(self, _left, _right):
                pass

            def stop(self, reset_encoders=True):
                self.stopped_without_reset = not reset_encoders

            def encoder_radian_left(self):
                self.assert_not_reset()
                return 4.0

            def encoder_radian_right(self):
                self.assert_not_reset()
                return 3.5

            def assert_not_reset(self):
                if self.reset:
                    raise AssertionError("encoder was reset before it was read")

        robot = FakeRobot()
        # The routine itself resets at the beginning; observe only resets after stop.
        def reset_with_phase():
            if robot.stopped_without_reset:
                robot.reset = True
            else:
                robot.reset = False

        robot.reset_encoders = reset_with_phase
        calibration.time = FakeTime
        left, right = calibration._measure_straight_pass(robot, duration_ms=1)
        self.assertEqual((left, right), (4.0, 3.5))
        self.assertTrue(robot.stopped_without_reset)
        self.assertTrue(robot.reset)


if __name__ == "__main__":
    unittest.main()
