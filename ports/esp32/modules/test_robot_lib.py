from lineRobot import Robot
import time

def test():
    robot = Robot()
    robot.begin()
    print("Robot will move in 5 seconds(forward for 3 seconds)")
    time.sleep(5)
    robot.move_forward_seconds(3)
    print("Robot will turns left 90 degrees")
    time.sleep(2)
    robot.turn_left_angle(90)
    print("Robot will turns right 90 degrees")
    time.sleep(2)
    robot.turn_right_angle(90)
    print("Robot will move forward 50 cm")
    time.sleep(2)
    robot.move_forward_speed_distance(80, 50)
