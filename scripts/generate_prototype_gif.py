from PIL import Image, ImageDraw, ImageFont

W, H = 960, 540
BG = (16, 18, 24)
CARD = (28, 32, 42)
TEXT = (232, 236, 245)
MUTED = (160, 170, 190)
GREEN = (56, 194, 123)
YELLOW = (245, 192, 78)
RED = (242, 85, 96)
BLUE = (90, 168, 255)

font = ImageFont.load_default()

states = [
    ("NORMAL", GREEN, 0.08, "none", "Sensors stable, baseline learning", "mqtt: connected, publish ok"),
    ("WARNING", YELLOW, 0.42, "gas", "Gas rising above baseline", "mqtt: publish warning snapshot"),
    ("ALARM", RED, 0.86, "vibration", "Vibration spike detected", "mqtt: alarm + anomaly queued/published"),
    ("ALARM", RED, 0.79, "vibration", "Latch active during safe hold", "mqtt: offline -> queue depth 3"),
    ("WARNING", YELLOW, 0.51, "temperature", "Values dropping but still elevated", "mqtt: reconnect, flushing queue"),
    ("NORMAL", GREEN, 0.14, "none", "System recovered, anomaly cleared", "mqtt: steady publish cadence"),
]

frames = []
for i in range(len(states) * 7):
    phase = i / 7.0
    idx = min(int(phase), len(states) - 1)
    state, color, score, src, note, mqtt = states[idx]

    img = Image.new("RGB", (W, H), BG)
    d = ImageDraw.Draw(img)

    # title
    d.text((24, 20), "ESP32 Edge Monitor - Prototype Simulation Demo", fill=TEXT, font=font)
    d.text((24, 40), "Flow: Sensor -> Processing -> Anomaly -> Control -> MQTT", fill=MUTED, font=font)

    # architecture bar
    x0, y0 = 24, 76
    labels = ["sensor_task", "processing_task", "anomaly_detector", "control_task", "telemetry_task"]
    for j, label in enumerate(labels):
        bx = x0 + j * 182
        bw, bh = 170, 42
        active = (j == (idx % len(labels)))
        fill = (40, 48, 63) if not active else (58, 75, 105)
        d.rounded_rectangle((bx, y0, bx + bw, y0 + bh), radius=8, fill=fill)
        d.text((bx + 8, y0 + 14), label, fill=TEXT, font=font)
        if j < len(labels) - 1:
            d.text((bx + 174, y0 + 14), "->", fill=MUTED, font=font)

    # status cards
    d.rounded_rectangle((24, 140, 470, 510), radius=10, fill=CARD)
    d.rounded_rectangle((490, 140, 936, 510), radius=10, fill=CARD)

    d.text((40, 160), "Runtime State", fill=TEXT, font=font)
    d.text((40, 185), f"Node State: {state}", fill=color, font=font)
    d.text((40, 210), f"Anomaly Score: {score:.2f}", fill=BLUE, font=font)
    d.text((40, 235), f"Anomaly Source: {src}", fill=TEXT, font=font)

    # anomaly bar
    bar_x, bar_y, bar_w, bar_h = 40, 275, 400, 20
    d.rectangle((bar_x, bar_y, bar_x + bar_w, bar_y + bar_h), outline=MUTED)
    fill_w = int(bar_w * score)
    bar_color = GREEN if score < 0.35 else YELLOW if score < 0.7 else RED
    d.rectangle((bar_x + 1, bar_y + 1, bar_x + fill_w, bar_y + bar_h - 1), fill=bar_color)
    d.text((40, 305), "Thresholds: warn=0.35, alarm=0.70", fill=MUTED, font=font)

    d.text((40, 340), "Sensors", fill=TEXT, font=font)
    temp = 29 + idx * 1.5 if idx < 3 else 34 - (idx - 2) * 1.7
    hum = 55 + idx * 2.2 if idx < 3 else 62 - (idx - 2) * 1.8
    vib = [0.04, 0.11, 0.76, 0.68, 0.24, 0.08][idx]
    gas = [1350, 1820, 2450, 2200, 1720, 1410][idx]
    d.text((40, 365), f"temp_avg={temp:.1f}C   hum_avg={hum:.1f}%", fill=TEXT, font=font)
    d.text((40, 385), f"vib_avg={vib:.2f}g    gas={gas}", fill=TEXT, font=font)
    d.text((40, 420), note, fill=MUTED, font=font)

    d.text((506, 160), "Telemetry / Console", fill=TEXT, font=font)
    d.text((506, 185), mqtt, fill=TEXT, font=font)

    # pseudo-json log
    json_lines = [
        '{',
        f'  "state": "{state}",',
        f'  "anomaly_score": {score:.2f},',
        f'  "anomaly_src": "{src}",',
        f'  "temp_avg": {temp:.1f}, "vib_avg": {vib:.2f},',
        f'  "gas": {gas}',
        '}',
    ]
    yy = 220
    for line in json_lines:
        d.text((506, yy), line, fill=(200, 225, 210), font=font)
        yy += 22

    # footer
    d.text((24, 518), "Prototype GIF (illustrative) - not a direct screen recording", fill=MUTED, font=font)

    frames.append(img)

out_path = "assets/prototype-simulation-demo.gif"
frames[0].save(
    out_path,
    save_all=True,
    append_images=frames[1:],
    duration=260,
    loop=0,
    optimize=True,
)
print(out_path)
