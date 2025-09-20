import json
import paho.mqtt.client as mqtt
import os
import sys
from typing import Dict, Any, Union

def load_json_file(file_path: str) -> Dict[str, Any]:
    """Загружает JSON файл и возвращает словарь с данными."""
    try:
        with open(file_path, 'r') as file:
            return json.load(file)
    except FileNotFoundError:
        print(f"Ошибка: Файл {file_path} не найден.")
        sys.exit(1)
    except json.JSONDecodeError:
        print(f"Ошибка: Файл {file_path} содержит невалидный JSON.")
        sys.exit(1)

def save_json_file(file_path: str, data: Dict[str, Any]):
    """Сохраняет данные обратно в JSON файл."""
    with open(file_path, 'w') as file:
        json.dump(data, file, indent=2)

def get_value_type(value: Any) -> str:
    """Определяет тип значения для MQTT команды."""
    if isinstance(value, int):
        return "int"
    elif isinstance(value, float):
        return "float"
    else:
        return "string"

def create_mqtt_command(name: str, value: Any) -> Dict[str, str]:
    """Создает команду для отправки по MQTT."""
    return {
        "command": "set-coeff",
        "value": value,
        "type": get_value_type(value),
        "name": name
    }

def send_mqtt_message(client: mqtt.Client, topic: str, data: Dict[str, Any]):
    """Отправляет сообщение по MQTT."""
    payload = json.dumps(data)
    result = client.publish(topic, payload)
    if result.rc == mqtt.MQTT_ERR_SUCCESS:
        print(f"Успешно отправлено: {payload}")
    else:
        print(f"Ошибка отправки: {mqtt.error_string(result.rc)}")

def connect_mqtt(broker_uri: str) -> mqtt.Client:
    """Подключается к MQTT брокеру и возвращает клиент."""
    try:
        client = mqtt.Client()
        if "://" in broker_uri:
            protocol, rest = broker_uri.split("://")
            if protocol == "mqtts":
                client.tls_set()
            host, port = rest.split(":")
        else:
            host, port = broker_uri.split(":")
        client.username_pw_set("ondroid-iot", "pQT1#TCeeWulV2PL")
        client.connect(host, int(port))
        client.loop_start()
        return client
    except Exception as e:
        print(f"Ошибка подключения к MQTT брокеру: {e}")
        sys.exit(1)

def send_all_values(client: mqtt.Client, topic: str, data: Dict[str, Any]):
    """Отправляет все значения из файла."""
    for name, value in data.items():
        if name == "broker_uri":  # Пропускаем URI брокера
            continue
        command = create_mqtt_command(name, value)
        send_mqtt_message(client, topic, command)

def manual_mode(client: mqtt.Client, topic: str, data: Dict[str, Any]):
    """Ручной режим с подтверждением перед каждой отправкой."""
    for name, value in data.items():
        if name == "broker_uri":
            continue
            
        print(f"\nПоле: {name}")
        print(f"Значение: {value}")
        choice = input("Отправить это значение? (y/n): ").lower()
        
        if choice == 'y':
            command = create_mqtt_command(name, value)
            send_mqtt_message(client, topic, command)
        elif choice == 'n':
            continue
        else:
            print("Неверный ввод, пропускаем...")

def specific_value_mode(client: mqtt.Client, topic: str, data: Dict[str, Any], file_path: str):
    """Режим отправки конкретного значения с обновлением файла."""
    print("\nДоступные поля:")
    items = list(data.items())
    for i, (name, value) in enumerate(items, 1):
        if name != "broker_uri":
            print(f"{i}. {name}: {value} (тип: {type(value).__name__})")
    
    while True:
        choice = input("\nВведите номер поля или имя поля (или 'q' для выхода): ").strip()
        if choice.lower() == 'q':
            return
            
        # Попробуем найти поле по номеру
        try:
            if choice.isdigit():
                index = int(choice) - 1
                if 0 <= index < len(items):
                    name, old_value = items[index]
                else:
                    print("Неверный номер поля.")
                    continue
            else:
                if choice in data:
                    name, old_value = choice, data[choice]
                else:
                    print("Неверное имя поля.")
                    continue
        except Exception as e:
            print(f"Ошибка: {e}")
            continue
            
        if name == "broker_uri":
            print("Нельзя изменить URI брокера.")
            continue
            
        print(f"\nВыбрано поле: {name}")
        print(f"Текущее значение: {old_value} (тип: {type(old_value).__name__})")
        
        new_value = input("Введите новое значение (или нажмите Enter для текущего): ").strip()
        if not new_value:
            new_value = old_value
        else:
            # Преобразуем ввод к правильному типу
            if isinstance(old_value, int):
                try:
                    new_value = int(new_value)
                except ValueError:
                    print("Ошибка: введите целое число.")
                    continue
            elif isinstance(old_value, float):
                try:
                    new_value = float(new_value)
                except ValueError:
                    print("Ошибка: введите число с плавающей точкой.")
                    continue
        
        # Обновляем значение в данных
        data[name] = new_value
        
        # Отправляем по MQTT
        command = create_mqtt_command(name, new_value)
        send_mqtt_message(client, topic, command)
        
        # Сохраняем изменения в файл
        save_json_file(file_path, data)
        print("Файл обновлен.")

def main():
    if len(sys.argv) < 2:
        print("Использование: python script.py <путь_к_json_файлу>")
        sys.exit(1)
        
    file_path = sys.argv[1]
    data = load_json_file(file_path)
    
    # Проверяем наличие URI брокера в файле
    if "broker_uri" not in data:
        print("Ошибка: В JSON файле отсутствует broker_uri.")
        sys.exit(1)
    
    # Подключаемся к MQTT брокеру
    client = connect_mqtt(data["broker_uri"])
    
    # Запрашиваем топик
    default_topic = f'{data.get("topic_system", "lfmp1/system")}/input'
    topic = input(f"Введите MQTT топик для отправки (по умолчанию {default_topic}): ").strip()
    if not topic:
        topic = default_topic
    
    # Выбираем режим работы
    while True:
        print("\nВыберите режим работы:")
        print("1. Отправить все значения")
        print("2. Ручной режим (с подтверждением для каждого значения)")
        print("3. Режим конкретного значения (с изменением файла)")
        print("4. Выход")
        
        choice = input("Ваш выбор (1-4): ").strip()
        
        if choice == '1':
            send_all_values(client, topic, data)
        elif choice == '2':
            manual_mode(client, topic, data)
        elif choice == '3':
            specific_value_mode(client, topic, data, file_path)
        elif choice == '4':
            break
        else:
            print("Неверный ввод, попробуйте снова.")
    
    # Отключаемся от MQTT
    client.loop_stop()
    client.disconnect()
    print("Работа завершена.")

if __name__ == "__main__":
    main()
