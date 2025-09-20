STOP

{
  "command": "py",
  "value": "from lineRobot import Robot\nrobot = Robot()\nrobot.begin()\nrobot.stop_motor_left()\nrobot.stop_motor_right()"
}

{
  "command": "py",
  "value": "print('hello world text w a s d')"
}
{
  "command": "ota-update",
  "url": "http://165.232.119.10:5000/download_bin?sketch=micropython"
}
{
  "command": "ota-update",
  "url": "https://lf1m.ondroid.org/download_bin?sketch=micropython"
}

## ДЛЯ ОБНОВЛЕНИЯ В HAMK
Передать файл
Запустить там сервер и отправить команду
{
  "command": "ota-update",
  "url": "http://172.31.150.7:15000/download_bin?sketch=micropython"
}

{
  "command": "set-coeff",
  "name": "maxs",
  "value": 15,
  "type": "float"
}

{
  "command": "get-coeff",
  "name": "broker_uri",
  "type": "string"
}

{
  "command": "set-coeff",
  "name": "ks",
  "type": "float",
  "value": 80.0
}

{
  "command": "set-coeff",
  "name": "wifi_pass",
  "type": "string",
  "value": "password"
}

{
  "command": "py",
  "value": "from lineRobot import Robot\ndef move():\n\trobot = Robot()\n\trobot.begin()\n\trobot.move_forward_speed_distance(80, 20)\nmove()"
}

{
  "command": "py",
  "value": "from lineRobot import Robot\ndef move():\n\trobot = Robot()\n\trobot.begin()\n\trobot.turn_right_angle(37)\nmove()"
}
{
  "command": "py",
  "value": "import ujson\nimport os\nprint(os.listdir())"
}

import os\nfrom espidf import esp_vfs_spiffs_register, esp_vfs_spiffs_conf_t\nconf = esp_vfs_spiffs_conf_t(base_path='/spiffs',partition_label='spiffs',max_files=8,format_if_mount_failed=True)\nesp_vfs_spiffs_register(conf)\nprint(os.listdir('/spiffs'))

{
  "command": "py",
  "value": "from lineRobot import Robot\ndef move():\n\trobot = Robot()\n\trobot.begin()\n\trobot.move_backward_speed_distance(50, 20)\n\trobot.stop_motor_left()\n\trobot.stop_motor_right()\nmove()"
}


{
  "command": "py",
  "value": "import os\nimport vfs\nfrom flashbdev import bdev\nos.mount(bdev, '/spiffs')"
}

{
  "command": "py",
  "value": "import esp32\npartitions = esp32.Partition.find(type=esp32.Partition.TYPE_DATA)\nfor p in partitions:\n\tinfo = p.info()\n\tprint(info)"
}
{
  "command": "battery-status"
}


{
  "command": "py",
  "value": "import ujson\nwith open(self.CONFIG_FILE, 'r') as f:\n\tprint(ujson.load(f))"
}

{
  "command": "py",
  "value": "import ujson\nwith open(self.CONFIG_FILE, 'r') as f:\n\tcontent = f.read()\n\tprint(content)"
}

{
  "command": "py",
  "value": "from lineRobot import Robot\nrobot = Robot()\nrobot.begin()\nrobot.stop_motor_left()\nrobot.stop_motor_right()"
}


import time
from lineRobot import Robot


robot = Robot()
print("Turning left")
robot.turn_left_angle(45)
print("Moving forward")
robot.move_forward_distance(20)
print("Turning right")
robot.turn_right()
print("Moving forward")
robot.move_forward_distance(20)
print("Turning right")
robot.turn_right()
print("Moving forward")
robot.move_forward_distance(20)
print("Turning right")
robot.turn_right()
print("Moving forward")
robot.move_forward_distance(20)