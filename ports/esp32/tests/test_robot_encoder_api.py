"""Host regression tests for the public Python API with a fake machine.Encoder backend.

These do not validate PCNT hardware; use the supervised on-device test for that.
Run: python3 -m unittest discover -s ports/esp32/tests -p test_robot_encoder_api.py
"""
import importlib.util
import math
from pathlib import Path
import sys
import types
import unittest
from unittest.mock import patch


class EncoderAPI(unittest.TestCase):
    def setUp(self):
        self.counts = [0, 0]
        counts = self.counts
        outer = self
        class Encoder:
            def __init__(self, index, pin_a, pin_b, *, phases, filter_ns):
                outer.assertEqual(phases, 4)
                outer.assertEqual(filter_ns, 1250)
                self.index = index
                counts[index] = 0
            def value(self, target=None):
                before = counts[self.index]
                if target is not None:
                    counts[self.index] = target
                return before
        class Pin:
            IN = 0
            def __init__(self, *args): pass
        self.pwm_initial_duties = []
        class PWM:
            def __init__(self, *args, **kwargs):
                self.value = kwargs.get('duty', 512)
                outer.pwm_initial_duties.append(self.value)
            def duty(self, value): self.value = value
        machine = types.SimpleNamespace(Pin=Pin, PWM=PWM, Timer=object, Encoder=Encoder)
        clock = types.SimpleNamespace(ticks_ms=lambda: 0, sleep_ms=lambda ms: None)
        config = {'pel1':35,'pel2':34,'per1':14,'per2':27,'pml1':25,'pml2':26,'pmr1':33,'pmr2':32,
                  'wrad':3.4,'wdist':19,'er':2300,'maxs':13,'kpa':110,'kia':80,'kda':4,
                  'kpsl':25.5,'kpsr':24.5,'kdsl':.7,'kdsr':0,'kis':0,'ks':130,'debug':0,
                  'ila':1.5,'ils':4,'msc':25,'smi':30,'ffl_fwd':20,'ffl_rev':30,'ffr_fwd':36,'ffr_rev':36}
        spec = importlib.util.spec_from_file_location('test_lineRobot', Path(__file__).parents[1] / 'modules/lineRobot.py')
        self.module = importlib.util.module_from_spec(spec)
        with patch.dict(sys.modules, {'machine':machine,'ujson':types.SimpleNamespace(),'time':clock}):
            spec.loader.exec_module(self.module)
        self.module.Robot._load_config = lambda _: dict(config)
        self.robot = self.module.Robot()

    def test_live_counts_and_units(self):
        self.counts[:] = [2300, 1150]
        self.assertEqual(self.robot.encoder_degrees_left(), 360)
        self.assertEqual(self.robot.encoder_degrees_right(), -180)
        self.assertAlmostEqual(self.robot.encoder_radian_left(), 2*math.pi)
        self.assertAlmostEqual(self.robot.encoder_radian_right(), -math.pi)

    def test_resets_and_assignments(self):
        self.robot.reset_left_encoder_value(123)
        self.robot.reset_right_encoder_value(-456)
        self.assertEqual(self.counts, [123,456])
        self.robot.reset_left_encoder()
        self.assertEqual(self.counts, [0,456])
        self.robot.reset_right_encoder()
        self.assertEqual(self.counts, [0,0])
        self.robot.encoder_position_left += 4
        self.robot.encoder_position_right -= 8
        self.assertEqual(self.counts, [4,8])

    def test_stop_preserves_only_when_requested(self):
        self.counts[:] = [123,456]
        self.robot.run_motor_left(50)
        self.robot.run_motor_right(-50)
        self.robot.stop(False)
        self.assertEqual(self.counts, [123,456])
        self.assertEqual([p.value for p in (self.robot.in1,self.robot.in2,self.robot.in3,self.robot.in4)], [0,0,0,0])
        self.robot.stop()
        self.assertEqual(self.counts, [0,0])

    def test_repeated_robot_uses_same_counter_backend(self):
        self.counts[:] = [100,200]
        second = self.module.Robot()
        self.assertEqual(self.counts, [0,0])
        self.counts[:] = [3,-7]
        self.assertEqual((self.robot.encoder_position_left,second.encoder_position_right), (3,7))

    def test_pwm_starts_disabled(self):
        self.assertEqual(self.pwm_initial_duties, [0,0,0,0])
        self.module.Robot()
        self.assertEqual(self.pwm_initial_duties, [0]*8)


if __name__ == '__main__':
    unittest.main()
