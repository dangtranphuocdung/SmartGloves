import serial
import time
import json
import threading
import matplotlib.pyplot as plt
import matplotlib.animation as animation
from collections import deque

# ── Serial config ──────────────────────────────────────────────────────────────
SERIAL_PORT   = 'COM10'
BAUD_RATE     = 115200
WINDOW_SIZE   = 100          # samples visible on screen at once

# ── Shared state (written by reader thread, read by plot thread) ───────────────
lock         = threading.Lock()
buf_ax       = deque(maxlen=WINDOW_SIZE)
buf_ay       = deque(maxlen=WINDOW_SIZE)
buf_az       = deque(maxlen=WINDOW_SIZE)
buf_gx       = deque(maxlen=WINDOW_SIZE)
buf_gy       = deque(maxlen=WINDOW_SIZE)
buf_gz       = deque(maxlen=WINDOW_SIZE)

recording    = []            # samples for the current capture
is_recording = False
stop_reader  = False

SKIP_TOKENS  = ('ets', 'rst:', 'boot:', '╔', '║', '═', 'Waiting', 'MPU6050')


# ══════════════════════════════════════════════════════════════════════════════
# Background serial reader
# ══════════════════════════════════════════════════════════════════════════════
def serial_reader(ser):
    global is_recording, stop_reader
    while not stop_reader:
        if ser.in_waiting > 0:
            try:
                line = ser.readline().decode('utf-8', errors='replace').strip()
                if not line or any(t in line for t in SKIP_TOKENS):
                    continue
                values = [float(x) for x in line.split(',')]
                if len(values) != 6:
                    continue
                ax_, ay_, az_, gx_, gy_, gz_ = values
                with lock:
                    buf_ax.append(ax_); buf_ay.append(ay_); buf_az.append(az_)
                    buf_gx.append(gx_); buf_gy.append(gy_); buf_gz.append(gz_)
                    if is_recording:
                        recording.append(values)
            except ValueError:
                pass
            except Exception as e:
                print(f"❌ Reader error: {e}")
        else:
            time.sleep(0.001)


# ══════════════════════════════════════════════════════════════════════════════
# Real-time matplotlib figure
# ══════════════════════════════════════════════════════════════════════════════
def make_figure():
    plt.style.use('dark_background')
    fig, axes = plt.subplots(2, 1, figsize=(11, 6), facecolor='#0d0d0d')
    fig.suptitle('IMU Live Feed', color='#e0e0e0', fontsize=14,
                 fontfamily='monospace', y=0.97)

    for ax in axes:
        ax.set_facecolor('#111111')
        ax.tick_params(colors='#666')
        for spine in ax.spines.values():
            spine.set_edgecolor('#333')

    ax_acc, ax_gyr = axes

    # Accelerometer subplot
    ax_acc.set_ylabel('Accel (m/s²)', color='#aaa', fontsize=9)
    ax_acc.set_ylim(-20, 20)
    ax_acc.set_xlim(0, WINDOW_SIZE)
    ax_acc.axhline(0, color='#333', lw=0.6)
    line_ax, = ax_acc.plot([], [], color='#ff4f4f', lw=1.2, label='X')
    line_ay, = ax_acc.plot([], [], color='#4fcb6e', lw=1.2, label='Y')
    line_az, = ax_acc.plot([], [], color='#4fa8ff', lw=1.2, label='Z')
    ax_acc.legend(loc='upper right', fontsize=8,
                  facecolor='#1a1a1a', edgecolor='#444', labelcolor='#ccc')

    # Gyroscope subplot
    ax_gyr.set_ylabel('Gyro (°/s)', color='#aaa', fontsize=9)
    ax_gyr.set_xlabel('Samples', color='#aaa', fontsize=9)
    ax_gyr.set_ylim(-300, 300)
    ax_gyr.set_xlim(0, WINDOW_SIZE)
    ax_gyr.axhline(0, color='#333', lw=0.6)
    line_gx, = ax_gyr.plot([], [], color='#ff9f4f', lw=1.2, label='X')
    line_gy, = ax_gyr.plot([], [], color='#c74fff', lw=1.2, label='Y')
    line_gz, = ax_gyr.plot([], [], color='#4ffff0', lw=1.2, label='Z')
    ax_gyr.legend(loc='upper right', fontsize=8,
                  facecolor='#1a1a1a', edgecolor='#444', labelcolor='#ccc')

    # Recording indicator text
    rec_text = ax_acc.text(
        0.02, 0.88, '', transform=ax_acc.transAxes,
        color='#ff3030', fontsize=10, fontfamily='monospace',
        fontweight='bold'
    )

    status_text = ax_gyr.text(
        0.02, 0.05, 'Samples: 0', transform=ax_gyr.transAxes,
        color='#555', fontsize=8, fontfamily='monospace'
    )

    plt.tight_layout(rect=[0, 0, 1, 0.96])

    def update(_frame):
        with lock:
            ax  = list(buf_ax); ay = list(buf_ay); az = list(buf_az)
            gx  = list(buf_gx); gy = list(buf_gy); gz = list(buf_gz)
            n   = len(recording)
            rec = is_recording

        xs = range(len(ax))
        line_ax.set_data(xs, ax)
        line_ay.set_data(xs, ay)
        line_az.set_data(xs, az)
        line_gx.set_data(range(len(gx)), gx)
        line_gy.set_data(range(len(gy)), gy)
        line_gz.set_data(range(len(gz)), gz)

        rec_text.set_text('⏺ RECORDING' if rec else '')
        status_text.set_text(f'Samples in buffer: {n}')

        return line_ax, line_ay, line_az, line_gx, line_gy, line_gz, rec_text, status_text

    ani = animation.FuncAnimation(
        fig, update, interval=33,   # ~30 fps
        blit=True, cache_frame_data=False
    )
    return fig, ani


# ══════════════════════════════════════════════════════════════════════════════
# Main collection loop
# ══════════════════════════════════════════════════════════════════════════════
def main():
    global is_recording, stop_reader, recording

    ser = serial.Serial(SERIAL_PORT, BAUD_RATE, timeout=1)
    time.sleep(2)
    ser.reset_input_buffer()

    # Start background reader
    reader_thread = threading.Thread(target=serial_reader, args=(ser,), daemon=True)
    reader_thread.start()

    # Build and show the live plot (non-blocking)
    fig, ani = make_figure()
    plt.show(block=False)
    plt.pause(0.1)

    # ── Collection UI ──────────────────────────────────────────────────────
    letters       = ['C', 'P']
    samples_per   = 5
    training_data = []

    print("\n╔═══════════════════════════════════════════╗")
    print("║       TRAINING DATA COLLECTION            ║")
    print("╚═══════════════════════════════════════════╝")
    print("\nMake sure you've uploaded data-collection.cpp to ESP32!")
    print("Live graph is running in a separate window.\n")

    for letter in letters:
        print(f"\n{'='*50}")
        print(f"  Collecting data for letter: {letter}")
        print(f"{'='*50}")

        i = 0
        while i < samples_per:
            input(f"\n[{i + 1}/{samples_per}] Press Enter, then HOLD the button and draw '{letter}'...")

            # Start recording
            with lock:
                recording = []
                is_recording = True

            print("⏺  Recording for 2 seconds… GO!")
            fig.suptitle(f"⏺  RECORDING  '{letter}'  [{i+1}/{samples_per}]",
                         color='#ff4040', fontsize=13, fontfamily='monospace')
            plt.pause(2.0)

            # Stop recording
            with lock:
                is_recording = False
                captured = list(recording)

            fig.suptitle('IMU Live Feed', color='#e0e0e0',
                         fontsize=14, fontfamily='monospace')
            plt.pause(0.05)

            if len(captured) > 10:
                training_data.append({'letter': letter, 'sensor_data': captured})
                print(f"✅ Captured {len(captured)} samples for '{letter}'")
                i += 1
            else:
                print(f"❌ Only got {len(captured)} samples — please try again!")

    # ── Wrap up ────────────────────────────────────────────────────────────
    stop_reader = True
    ser.close()

    print(f"\n{'='*50}")
    print(f"  Collection Complete!")
    print(f"{'='*50}")
    print(f"Total samples collected: {len(training_data)}")

    with open('gesture_training_data.json', 'w') as f:
        json.dump(training_data, f, indent=2)
    print("✅ Saved to gesture_training_data.json")

    if training_data:
        print("\n📊 Preview of first sample:")
        print(f"  Letter:      {training_data[0]['letter']}")
        print(f"  Data points: {len(training_data[0]['sensor_data'])}")
        print(f"  First read:  {training_data[0]['sensor_data'][0]}")

    print("\nClose the plot window to exit.")
    plt.show()   # block until the user closes the window


if __name__ == '__main__':
    main()