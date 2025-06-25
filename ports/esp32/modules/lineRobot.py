import time
import math
import ujson
import os
from machine import Pin, PWM, Timer

class Robot:
    CONFIG_FILE = "settings.json"
    
    def __init__(self, **kwargs):

        # Загружаем конфиг из файла или используем значения по умолчанию
        config = self._load_config()
        print(f"config: {config['version']}")
        
        # Обновляем конфиг переданными аргументами
        config.update(kwargs)
        
        # Инициализация параметров
        self._init_params(config)
        
        # Инициализация оборудования
        self._init_hardware(config)
        
        # Инициализация состояния
        self._init_state()
        # Инициалищация interrupt
        self.begin()
    
    def _load_config(self):
        # Значения по умолчанию
        default_config = {
            "version": -1,
            "pml1": 25,
            "pml2": 26,
            "pmr1": 33,
            "pmr2": 32,
            "pel1": 35,
            "pel2": 34,
            "per1": 14,
            "per2": 27,
            "wrad": 3.05,
            "wdist": 18.4,
            "er": 2376,
            "kpa": 45.0,
            "kia": 100.0,
            "kda": 2.5,
            "kpsl": 41.5,
            "kpsr": 28.0,
            "kdsl": 0.5,
            "kdsr": 0.1,
            "kis": 0.0,
            "ks": 80.0,
            "maxs": 15.0
        }
        try:
            #print("loading params from the memory")
            if self.CONFIG_FILE in os.listdir():
                with open(self.CONFIG_FILE, 'r') as f:
                    return ujson.load(f)
            print("ERROR reading file")
            return default_config
        except Exception as e:
            print(f"Error loading config: {e}")
            return default_config
    
    def _init_params(self, config):
        # Physical parameters
        self.RADIUS_WHEEL = config["wrad"]
        self.distance_between_wheel_and_center = config["wdist"] / 2
        self.pulses_per_revolution = config["er"]
        self.max_speed_radians = config["maxs"]
        self.k_speed_radians = config["maxs"] / 100.0
        
        # PID parameters
        self.kp_ang = config["kpa"]
        self.ki_ang = config["kia"]
        self.kd_ang = config["kda"]
        self.kp_speed_left = config["kpsl"]
        self.kp_speed_right = config["kpsr"]
        self.kd_speed_left = config["kdsl"]
        self.kd_speed_right = config["kdsr"]
        self.ki_speed = config["kis"]
        self.k_straight = config["ks"]
    
    def _init_hardware(self, config):
        # Motor pins
        self.in1 = PWM(Pin(config["pml1"]), freq=1000)
        self.in2 = PWM(Pin(config["pml2"]), freq=1000)
        self.in3 = PWM(Pin(config["pmr1"]), freq=1000)
        self.in4 = PWM(Pin(config["pmr2"]), freq=1000)
        
        # Encoder pins
        self.encoder_pin_a_left = Pin(config["pel1"], Pin.IN)
        self.encoder_pin_b_left = Pin(config["pel2"], Pin.IN)
        self.encoder_pin_a_right = Pin(config["per1"], Pin.IN)
        self.encoder_pin_b_right = Pin(config["per2"], Pin.IN)
    
    def _init_state(self):
        # Encoder state
        self.encoder_position_left = 0
        self.encoder_position_right = 0
        self.last_encoded_l = 0
        self.last_encoded_r = 0
        
        # Control variables
        self.target_angle = 0
        self.left_motor_signal = 0
        self.right_motor_signal = 0
        self.STANDARD_SPEED_PERCENTAGE = 65
        self.STANDARD_SPEED_PERCENTAGE_SLOW = 50
        
        # PID state variables
        self.reset_regulators()
        
        # Block flag
        self.block = False
        
    def begin(self):
        """Initialize encoder interrupts"""
        self.encoder_pin_a_left.irq(trigger=Pin.IRQ_RISING | Pin.IRQ_FALLING, 
                                   handler=self._update_encoder_left)
        self.encoder_pin_b_left.irq(trigger=Pin.IRQ_RISING | Pin.IRQ_FALLING, 
                                   handler=self._update_encoder_left)
        self.encoder_pin_a_right.irq(trigger=Pin.IRQ_RISING | Pin.IRQ_FALLING, 
                                    handler=self._update_encoder_right)
        self.encoder_pin_b_right.irq(trigger=Pin.IRQ_RISING | Pin.IRQ_FALLING, 
                                    handler=self._update_encoder_right)
    
    def _update_encoder_left(self, pin):
        """Left encoder interrupt handler"""
        msb_l = self.encoder_pin_a_left.value()
        lsb_l = self.encoder_pin_b_left.value()
        encoded_l = (msb_l << 1) | lsb_l
        sum_l = (self.last_encoded_l << 2) | encoded_l
        
        if sum_l in [0b1101, 0b0100, 0b0010, 0b1011]:
            self.encoder_position_left += 1
        elif sum_l in [0b1110, 0b0111, 0b0001, 0b1000]:
            self.encoder_position_left -= 1
            
        self.last_encoded_l = encoded_l
    
    def _update_encoder_right(self, pin):
        """Right encoder interrupt handler"""
        msb_r = self.encoder_pin_a_right.value()
        lsb_r = self.encoder_pin_b_right.value()
        encoded_r = (msb_r << 1) | lsb_r
        sum_r = (self.last_encoded_r << 2) | encoded_r
        
        if sum_r in [0b1101, 0b0100, 0b0010, 0b1011]:
            self.encoder_position_right -= 1
        elif sum_r in [0b1110, 0b0111, 0b0001, 0b1000]:
            self.encoder_position_right += 1
            
        self.last_encoded_r = encoded_r
    
    def constrain(self, value, min_val, max_val):
        """Constrain value between min and max"""
        return max(min_val, min(max_val, value))
    
    def run_motor_left(self, u):
        """Run left motor with PWM value u (-1023 to 1023)"""
        u = self.constrain(u, -1023, 1023)
        if u < 0:
            self.in1.duty(0)
            self.in2.duty(-u)
        else:
            self.in1.duty(u)
            self.in2.duty(0)
    
    def run_motor_right(self, u):
        """Run right motor with PWM value u (-1023 to 1023)"""
        u = self.constrain(u, -1023, 1023)
        if u < 0:
            self.in3.duty(0)
            self.in4.duty(-u)
        else:
            self.in3.duty(u)
            self.in4.duty(0)
    
    def stop_motor_left(self):
        """Stop left motor"""
        self.in1.duty(0)
        self.in2.duty(0)
    
    def stop_motor_right(self):
        """Stop right motor"""
        self.in3.duty(0)
        self.in4.duty(0)
    
    def stop(self):
        """Stop both motors"""
        self.stop_motor_left()
        self.stop_motor_right()
    
    def encoder_degrees_left(self):
        """Get left encoder position in degrees"""
        return self.encoder_position_left * 360 // self.pulses_per_revolution
    
    def encoder_degrees_right(self):
        """Get right encoder position in degrees"""
        return self.encoder_position_right * 360 // self.pulses_per_revolution
    
    def encoder_radian_left(self):
        """Get left encoder position in radians"""
        return self.encoder_position_left * 2.0 * math.pi / self.pulses_per_revolution
    
    def encoder_radian_right(self):
        """Get right encoder position in radians"""
        return self.encoder_position_right * 2.0 * math.pi / self.pulses_per_revolution
    
    def get_speed_l(self, interval=20):
        """Get left wheel speed in rad/s"""
        last_pos = self.encoder_radian_left()
        time.sleep_ms(interval)
        cur_pos = self.encoder_radian_left()
        delta_pos = cur_pos - last_pos
        delta_time = interval / 1000.0
        return delta_pos / delta_time
    
    def get_speed_r(self, interval=20):
        """Get right wheel speed in rad/s"""
        last_pos = self.encoder_radian_right()
        time.sleep_ms(interval)
        cur_pos = self.encoder_radian_right()
        delta_pos = cur_pos - last_pos
        delta_time = interval / 1000.0
        return delta_pos / delta_time
    
    def get_speed_motors(self, interval=50):
        """Get both wheel speeds in rad/s"""
        last_pos_l = self.encoder_radian_left()
        last_pos_r = self.encoder_radian_right()
        time.sleep_ms(interval)
        cur_pos_l = self.encoder_radian_left()
        cur_pos_r = self.encoder_radian_right()
        delta_time = interval / 1000.0
        speed_l = (cur_pos_l - last_pos_l) / delta_time
        speed_r = (cur_pos_r - last_pos_r) / delta_time
        return speed_l, speed_r
    
    def compute_pid_speed_motor(self, err, kp, kd, ki, integral, previous_err, last_time):
        """Compute PID for speed control"""
        # Proportional
        P = kp * err
        
        # Time calculation
        current_time = time.ticks_ms()
        dt = time.ticks_diff(current_time, last_time) / 1000.0
        
        # Integral (only when P is small)
        if abs(P) < 25:
            integral += err * dt
        I = ki * integral
        
        # Derivative
        if dt > 0:
            D = kd * (err - previous_err) / dt
        else:
            D = 0
        
        output = P + I + D
        motor_speed = self.constrain(int(output * 4), -1023, 1023)  # Scale to PWM range
        
        return motor_speed, integral, err, current_time
    
    def compute_pid_angle_motor(self, err, kp, kd, ki, integral, previous_err, last_time):
        """Compute PID for angle control"""
        # Proportional
        P = kp * err
        
        # Time calculation
        current_time = time.ticks_ms()
        dt = time.ticks_diff(current_time, last_time) / 1000.0
        
        # Integral (only when P is small)
        if abs(P) < 35:
            integral += err * dt
        I = ki * integral
        
        # Derivative
        if dt > 0:
            D = kd * (err - previous_err) / dt
        else:
            D = 0
        
        output = P + I + D
        motor_speed = self.constrain(int(output), -100, 100)
        
        return motor_speed, integral, err, current_time
    
    def run_motors_speed(self, speed_left, speed_right):
        """Run motors at specified speeds (percentage)"""
        speed_left = self.constrain(speed_left, -100, 100)
        speed_right = self.constrain(speed_right, -100, 100)
        
        cur_speed_l, cur_speed_r = self.get_speed_motors(50)
        
        target_speed_l = speed_left * self.k_speed_radians
        target_speed_r = speed_right * self.k_speed_radians
        
        err_l = target_speed_l - cur_speed_l
        err_r = target_speed_r - cur_speed_r
        
        # Update PID for left motor
        self.left_motor_signal, self.integral_speed_left, self.previous_err_speed_left, self.last_time_left_speed = \
            self.compute_pid_speed_motor(err_l, self.kp_speed_left, self.kd_speed_left, 
                                       self.ki_speed, self.integral_speed_left, 
                                       self.previous_err_speed_left, self.last_time_left_speed)
        
        # Update PID for right motor
        self.right_motor_signal, self.integral_speed_right, self.previous_err_speed_right, self.last_time_right_speed = \
            self.compute_pid_speed_motor(err_r, self.kp_speed_right, self.kd_speed_right, 
                                       self.ki_speed, self.integral_speed_right, 
                                       self.previous_err_speed_right, self.last_time_right_speed)
        
        self.run_motor_left(self.left_motor_signal)
        self.run_motor_right(self.right_motor_signal)
    
    def reset_regulators(self):
        """Reset PID controllers"""
        current_time = time.ticks_ms()
        self.last_time_left_speed = current_time + 3
        self.last_time_right_speed = current_time + 3
        self.last_time_left = current_time + 2
        self.last_time_right = current_time + 2
        
        self.integral_speed_left = 0
        self.previous_err_speed_left = 0
        self.integral_speed_right = 0
        self.previous_err_speed_right = 0
        
        self.integral_ang_left = 0
        self.previous_err_ang_left = 0
        self.integral_ang_right = 0
        self.previous_err_ang_right = 0
    
    def reset_encoders(self):
        """Reset encoder positions"""
        self.encoder_position_left = 0
        self.encoder_position_right = 0
    
    def set_block_true(self):
        """Enable blocking mode"""
        self.block = True
    
    def move_forward_speed_distance(self, sp, dist):
        """Move forward with specified speed for specified distance"""
        if self.block:
            return
            
        # Calculate target angle
        self.target_angle = dist / self.RADIUS_WHEEL * 1.02
        self.reset_encoders()
        self.reset_regulators()
        
        angle_left = 0.0
        angle_right = 0.0
        k_sp = abs(sp) / 100.0
        start_time = time.ticks_ms()
        period = self.constrain(int(200 * dist / k_sp + 3000), 0, 30000)
        
        self.previous_err_ang_left = self.target_angle
        self.previous_err_ang_right = self.target_angle
        
        while ((abs(self.target_angle - angle_left) > 0.2 or 
                abs(self.target_angle - angle_right) > 0.2) and 
               time.ticks_diff(time.ticks_ms(), start_time) < period):
            
            angle_left = self.encoder_radian_left()
            angle_right = self.encoder_radian_right()
            
            # PID control for both motors
            left_motor_speed, self.integral_ang_left, self.previous_err_ang_left, self.last_time_left = \
                self.compute_pid_angle_motor(self.target_angle - angle_left, self.kp_ang, 
                                           self.kd_ang, self.ki_ang, self.integral_ang_left,
                                           self.previous_err_ang_left, self.last_time_left)
            
            right_motor_speed, self.integral_ang_right, self.previous_err_ang_right, self.last_time_right = \
                self.compute_pid_angle_motor(self.target_angle - angle_right, self.kp_ang, 
                                           self.kd_ang, self.ki_ang, self.integral_ang_right,
                                           self.previous_err_ang_right, self.last_time_right)
            
            # Apply speed scaling
            left_motor_speed = self.constrain(int(left_motor_speed * k_sp), -75, 75)
            right_motor_speed = self.constrain(int(right_motor_speed * k_sp), -75, 75)
            
            # Smooth start
            elapsed = time.ticks_diff(time.ticks_ms(), start_time)
            if elapsed < 500:
                power = self.constrain(0.5 + elapsed / 1000.0, 0.5, 1.0)
                left_motor_speed = int(left_motor_speed * power)
                right_motor_speed = int(right_motor_speed * power)
            
            # Straight line correction
            if abs(self.previous_err_ang_left) < abs(self.previous_err_ang_right):
                right_motor_speed += int((angle_left - angle_right) * self.k_straight)
            elif abs(self.previous_err_ang_left) > abs(self.previous_err_ang_right):
                left_motor_speed += int((angle_right - angle_left) * self.k_straight)
            
            self.run_motors_speed(left_motor_speed, right_motor_speed)
        
        self.stop()
    
    def move_backward_speed_distance(self, sp, dist):
        """Move backward with specified speed for specified distance"""
        if self.block:
            return
            
        self.target_angle = dist / self.RADIUS_WHEEL * 1.02
        self.reset_encoders()
        self.reset_regulators()
        
        angle_left = 0.0
        angle_right = 0.0
        k_sp = abs(sp) / 100.0
        start_time = time.ticks_ms()
        period = self.constrain(int(200 * dist / k_sp + 3000), 0, 30000)
        
        self.previous_err_ang_left = self.target_angle
        self.previous_err_ang_right = self.target_angle
        
        while ((abs(angle_left - self.target_angle) > 0.2 or 
                abs(angle_right - self.target_angle) > 0.2) and 
               time.ticks_diff(time.ticks_ms(), start_time) < period):
            
            angle_left = abs(self.encoder_radian_left())
            angle_right = abs(self.encoder_radian_right())
            
            left_motor_speed, self.integral_ang_left, self.previous_err_ang_left, self.last_time_left = \
                self.compute_pid_angle_motor(self.target_angle - angle_left, self.kp_ang, 
                                           self.kd_ang, self.ki_ang, self.integral_ang_left,
                                           self.previous_err_ang_left, self.last_time_left)
            
            right_motor_speed, self.integral_ang_right, self.previous_err_ang_right, self.last_time_right = \
                self.compute_pid_angle_motor(self.target_angle - angle_right, self.kp_ang, 
                                           self.kd_ang, self.ki_ang, self.integral_ang_right,
                                           self.previous_err_ang_right, self.last_time_right)
            
            left_motor_speed = self.constrain(left_motor_speed, -75, 75)
            right_motor_speed = self.constrain(right_motor_speed, -75, 75)
            
            # Smooth start
            elapsed = time.ticks_diff(time.ticks_ms(), start_time)
            if elapsed < 500:
                power = self.constrain(0.5 + elapsed / 1000.0, 0.5, 1.0)
                left_motor_speed = int(left_motor_speed * power)
                right_motor_speed = int(right_motor_speed * power)
            
            # Straight line correction
            if abs(self.previous_err_ang_left) < abs(self.previous_err_ang_right):
                right_motor_speed += int((angle_left - angle_right) * self.k_straight)
            elif abs(self.previous_err_ang_left) > abs(self.previous_err_ang_right):
                left_motor_speed += int((angle_right - angle_left) * self.k_straight)
            
            self.run_motors_speed(-left_motor_speed, -right_motor_speed)
        
        self.stop()
    
    def move_forward_distance(self, dist):
        """Move forward for specified distance at standard speed"""
        self.move_forward_speed_distance(self.STANDARD_SPEED_PERCENTAGE, dist)
    
    def move_backward_distance(self, dist):
        """Move backward for specified distance at standard speed"""
        self.move_backward_speed_distance(self.STANDARD_SPEED_PERCENTAGE, dist)
    
    def move_forward_seconds(self, seconds):
        """Move forward for specified time"""
        if self.block:
            return
            
        self.reset_encoders()
        self.reset_regulators()
        start_time = time.ticks_ms()
        period = seconds * 1000
        
        while time.ticks_diff(time.ticks_ms(), start_time) < period:
            angle_left = self.encoder_radian_left()
            angle_right = self.encoder_radian_right()
            
            left_motor_speed = self.STANDARD_SPEED_PERCENTAGE_SLOW
            right_motor_speed = self.STANDARD_SPEED_PERCENTAGE_SLOW
            
            # Smooth start
            elapsed = time.ticks_diff(time.ticks_ms(), start_time)
            if elapsed < 500:
                power = self.constrain(0.5 + elapsed / 1000.0, 0.5, 1.0)
                left_motor_speed = int(left_motor_speed * power)
                right_motor_speed = int(right_motor_speed * power)
            
            # Straight line correction
            if angle_left > angle_right:
                right_motor_speed += int((angle_left - angle_right) * self.k_straight)
            elif angle_left < angle_right:
                left_motor_speed += int((angle_right - angle_left) * self.k_straight)
            
            self.run_motors_speed(left_motor_speed, right_motor_speed)
        
        self.stop()
    
    def move_backward_seconds(self, seconds):
        """Move backward for specified time"""
        if self.block:
            return
            
        self.reset_encoders()
        self.reset_regulators()
        start_time = time.ticks_ms()
        period = seconds * 1000
        
        while time.ticks_diff(time.ticks_ms(), start_time) < period:
            angle_left = self.encoder_radian_left()
            angle_right = self.encoder_radian_right()
            
            left_motor_speed = self.STANDARD_SPEED_PERCENTAGE_SLOW
            right_motor_speed = self.STANDARD_SPEED_PERCENTAGE_SLOW
            
            # Smooth start
            elapsed = time.ticks_diff(time.ticks_ms(), start_time)
            if elapsed < 500:
                power = self.constrain(0.5 + elapsed / 1000.0, 0.5, 1.0)
                left_motor_speed = int(left_motor_speed * power)
                right_motor_speed = int(right_motor_speed * power)
            
            # Straight line correction
            if abs(angle_left) > abs(angle_right):
                right_motor_speed += int((angle_left - angle_right) * self.k_straight)
            elif abs(angle_left) < abs(angle_right):
                left_motor_speed += int((angle_right - angle_left) * self.k_straight)
            
            self.run_motors_speed(-left_motor_speed, -right_motor_speed)
        
        self.stop()
    
    def turn_left_angle(self, angle):
        """Turn left by specified angle in degrees"""
        if self.block:
            return
            
        self.reset_encoders()
        self.reset_regulators()
        error = 0.07
        self.target_angle = angle * self.distance_between_wheel_and_center * math.pi / (self.RADIUS_WHEEL * 180)
        start_time = time.ticks_ms()
        period = 80 * angle + 5000
        
        angle_left = 0.0
        angle_right = 0.0
        self.previous_err_ang_left = self.target_angle
        self.previous_err_ang_right = self.target_angle
        
        while ((abs(self.previous_err_ang_left) > error or 
                abs(self.previous_err_ang_right) > error) and 
               time.ticks_diff(time.ticks_ms(), start_time) < period):
            
            angle_left = abs(self.encoder_radian_left())
            angle_right = abs(self.encoder_radian_right())
            
            left_motor_speed, self.integral_ang_left, self.previous_err_ang_left, self.last_time_left = \
                self.compute_pid_angle_motor(self.target_angle - angle_left, self.kp_ang, 
                                           self.kd_ang, self.ki_ang, self.integral_ang_left,
                                           self.previous_err_ang_left, self.last_time_left)
            
            right_motor_speed, self.integral_ang_right, self.previous_err_ang_right, self.last_time_right = \
                self.compute_pid_angle_motor(self.target_angle - angle_right, self.kp_ang, 
                                           self.kd_ang, self.ki_ang, self.integral_ang_right,
                                           self.previous_err_ang_right, self.last_time_right)
            
            left_motor_speed = self.constrain(left_motor_speed, -75, 75)
            right_motor_speed = self.constrain(right_motor_speed, -75, 75)
            
            # Smooth start
            elapsed = time.ticks_diff(time.ticks_ms(), start_time)
            if elapsed < 500:
                power = self.constrain(0.5 + elapsed / 1000.0, 0.5, 1.0)
                left_motor_speed = int(left_motor_speed * power)
                right_motor_speed = int(right_motor_speed * power)
            
            # Synchronization correction
            if abs(self.previous_err_ang_left) < abs(self.previous_err_ang_right):
                right_motor_speed += int((angle_left - angle_right) * self.k_straight)
            elif abs(self.previous_err_ang_left) > abs(self.previous_err_ang_right):
                left_motor_speed += int((angle_right - angle_left) * self.k_straight)
            
            final_left = int(-left_motor_speed * self.STANDARD_SPEED_PERCENTAGE / 100.0)
            final_right = int(right_motor_speed * self.STANDARD_SPEED_PERCENTAGE / 100.0)
            self.run_motors_speed(final_left, final_right)
        
        self.stop()
        time.sleep_ms(500)
        
    def turn_right_angle(self, angle):
        """Turn right by specified angle in degrees"""
        if self.block:
            return
            
        self.reset_encoders()
        self.reset_regulators()
        error = 0.07
        self.target_angle = angle * self.distance_between_wheel_and_center * math.pi / (self.RADIUS_WHEEL * 180)
        start_time = time.ticks_ms()
        period = 80 * angle + 5000
        
        angle_left = 0.0
        angle_right = 0.0
        self.previous_err_ang_left = self.target_angle
        self.previous_err_ang_right = self.target_angle
        
        while ((abs(self.previous_err_ang_left) > error or 
                abs(self.previous_err_ang_right) > error) and 
               time.ticks_diff(time.ticks_ms(), start_time) < period):
            
            angle_left = abs(self.encoder_radian_left())
            angle_right = abs(self.encoder_radian_right())
            
            left_motor_speed, self.integral_ang_left, self.previous_err_ang_left, self.last_time_left = \
                self.compute_pid_angle_motor(self.target_angle - angle_left, self.kp_ang, 
                                           self.kd_ang, self.ki_ang, self.integral_ang_left,
                                           self.previous_err_ang_left, self.last_time_left)
            
            right_motor_speed, self.integral_ang_right, self.previous_err_ang_right, self.last_time_right = \
                self.compute_pid_angle_motor(self.target_angle - angle_right, self.kp_ang, 
                                           self.kd_ang, self.ki_ang, self.integral_ang_right,
                                           self.previous_err_ang_right, self.last_time_right)
            
            left_motor_speed = self.constrain(left_motor_speed, -75, 75)
            right_motor_speed = self.constrain(right_motor_speed, -75, 75)
            
            # Smooth start
            elapsed = time.ticks_diff(time.ticks_ms(), start_time)
            if elapsed < 500:
                power = self.constrain(0.5 + elapsed / 1000.0, 0.5, 1.0)
                left_motor_speed = int(left_motor_speed * power)
                right_motor_speed = int(right_motor_speed * power)
            
            # Synchronization correction
            if abs(self.previous_err_ang_left) < abs(self.previous_err_ang_right):
                right_motor_speed += int((angle_left - angle_right) * self.k_straight)
            elif abs(self.previous_err_ang_left) > abs(self.previous_err_ang_right):
                left_motor_speed += int((angle_right - angle_left) * self.k_straight)
            
            final_left = int(left_motor_speed * self.STANDARD_SPEED_PERCENTAGE / 100.0)
            final_right = int(-right_motor_speed * self.STANDARD_SPEED_PERCENTAGE / 100.0)
            self.run_motors_speed(final_left, final_right)
        
        self.stop()
        time.sleep_ms(500)
    
    def reset_left_encoder(self):
        """Reset left encoder position"""
        self.encoder_position_left = 0
    
    def reset_right_encoder(self):
        """Reset right encoder position"""
        self.encoder_position_right = 0
    
    def reset_left_encoder_value(self, value):
        """Reset left encoder to specific value"""
        self.encoder_position_left = value
    
    def reset_right_encoder_value(self, value):
        """Reset right encoder to specific value"""
        self.encoder_position_right = value
    
    def turn_left(self):
        """Turn left 90 degrees"""
        self.turn_left_angle(90)
    
    def turn_right(self):
        """Turn right 90 degrees"""
        self.turn_right_angle(90)
    
    def rotate(self):
        """Rotate 180 degrees"""
        self.turn_right_angle(180)
