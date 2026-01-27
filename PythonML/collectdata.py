# collect_training_data.py
import serial
import time
import json

ser = serial.Serial('COM4', 115200)
training_data = []

letters = ['A', 'B', 'C', 'D', 'E']  # Start small

for letter in letters:
    print(f"\n=== Collecting data for letter: {letter} ===")
    print("Draw the letter 5 times. Press Enter to start each recording...")

    for i in range(5):
        input(f"Recording {i + 1}/5 - Press Enter when ready...")

        print("Recording for 2 seconds... GO!")
        recording = []
        start_time = time.time()

        while time.time() - start_time < 2.0:
            if ser.in_waiting > 0:
                line = ser.readline().decode('utf-8').strip()
                # Expecting format: "ax,ay,az,gx,gy,gz"
                values = [float(x) for x in line.split(',')]
                recording.append(values)

        training_data.append({
            'letter': letter,
            'sensor_data': recording
        })

        print(f"Captured {len(recording)} samples")

# Save training data
with open('gesture_training_data.json', 'w') as f:
    json.dump(training_data, f)

print("\n✅ Training data collected!")