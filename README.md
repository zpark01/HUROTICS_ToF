# ToF + IMU Gait Logger

Handover package. August 2026.

---

## What this is

A wearable rig that measures the distance from each thigh to the ground
while walking, alongside thigh orientation, and logs everything to an SD
card with a button-pressed activity label.

Two reasons it exists:

1. An IMU only reacts once the foot has already landed. A ranging sensor
   aimed slightly forward sees the ground before you step on it.
2. Existing IMU recordings mostly have no activity labels. If activities
   can be separated from the sensor signal alone, that becomes a way to
   label data that was never annotated.

### Where it stands

| | |
| --- | --- |
| Stair activity classification (supervised) | 99.0% |
| Stair activity separation (no labels given) | 97.7%, ARI 0.93 |
| Ramp separation (no labels given) | 68% - weak, see notes |
| Data collected so far | one subject, 7 sessions, ~1200 s |

Proof of concept. Nothing here has been validated across subjects.

---

## Parts

| Item | Model | Qty | Notes |
| --- | --- | --- | --- |
| MCU | Teensy 4.1 | 1 | 4.0 will not work, no SD slot |
| Ranging sensor | VL53L4CD (Pololu) | 2 | 120 cm range |
| IMU | Adafruit BNO085 | 2 | |
| Status LED | WS2812 breakout | 1 | any single LED works |
| Buttons | tactile switch | 6 | |
| SD card | 32 GB or less | 1 | FAT32 |
| Power | 5 V power bank | 1 | use a data-capable USB cable |

### Arduino libraries

- `VL53L4CD` by Pololu
- `Adafruit BNO08x RVC` — **not** the plain `Adafruit BNO08x` library
- `Adafruit NeoPixel`
- Teensyduino, installed separately from PJRC

Board: `Tools > Board > Teensy 4.1`

### Python

```
pip install pandas numpy matplotlib scikit-learn
```

---

## Wiring

| Part | Pin | Teensy 4.1 |
| --- | --- | --- |
| **ToF #1** (left) | VIN | 3.3V |
| | GND | GND |
| | SDA | 18 |
| | SCL | 19 |
| | XSHUT | 11 |
| **ToF #2** (right) | VIN | 3.3V |
| | GND | GND |
| | SDA | 18 (shared) |
| | SCL | 19 (shared) |
| | XSHUT | 12 |
| **IMU #1** (left) | VIN | 3.3V |
| | GND | GND |
| | **SDA** | **0 (RX1)** |
| | **P0** | **3.3V** |
| **IMU #2** (right) | VIN | 3.3V |
| | GND | GND |
| | **SDA** | **7 (RX2)** |
| | **P0** | **3.3V** |
| **LED** | DI | 6 |
| | GND / 5V | GND / 5V |
| **Buttons 1-6** | one side | 2, 3, 4, 5, 9, 10 |
| | other side | GND (common) |
| **SD** | — | onboard slot |

### Three things that will cost you a day if missed

**Teensy is not 5V tolerant.** Everything except the WS2812 runs at 3.3V.

**The IMU pin labelled SDA is a transmit line.** In UART-RVC mode it
carries data out of the sensor, so it connects to an RX pin on the
Teensy, not to a data pin. Leave SCL unconnected.

**P0 must be pulled to 3.3V.** Without it the sensor sits in I2C mode and
sends nothing at all — the serial port stays completely silent. Leave P1
alone.

| PS1 (P1) | PS0 (P0) | Mode |
| --- | --- | --- |
| Low | Low | I2C |
| Low | **High** | **UART-RVC — use this** |
| High | Low | UART |
| High | High | SPI |

### Mounting

Strap the sensors to the lower thigh, roughly 60 cm above the ground.
Aim the ToF beam at the floor but tilted **forward about 10 degrees**,
not straight down.

The tilt matters. People stand upright on a slope rather than leaning
with it, so a straight-down beam reads the same distance whether the
ground is flat or inclined. Tilting it forward is what makes the reading
change with the terrain.

Mount the IMU with the component side facing the leg.

---

## Bringing it up

Work through these in order the first time. Going straight to the main
logger makes it hard to tell which part is broken.

**1. Are the ToF sensors on the bus?**

Upload `diagnostics/i2c_scanner.ino`, open the serial monitor at 115200.
Two devices should appear.

**2. Are the IMUs sending anything?**

Upload `diagnostics/uart_check.ino`. Byte counts print every 2 seconds.
A count of zero means P0 is not connected.

**3. Do all four read sensibly?**

Upload `firmware/02_sensor_monitor.ino`. Move the sensors around and
watch the numbers follow.

**4. Main logger.**

Upload `firmware/01_MAIN_gait_logger.ino`. On boot you should see:

```
SENSOR_OK,both ToF ready
IMU1_OK,IMU1 ready on Serial1
IMU2_OK,IMU2 ready on Serial2
SD_OK,card mounted
READY,button 1 starts and stops a run
```

---

## Recording

### Buttons

| Button | Pin | Label |
| --- | --- | --- |
| 1 | 2 | toggles START / STOP |
| 2 | 3 | standing |
| 3 | 4 | level_walk |
| 3 + 4 together | | stair_down |
| 3 + 5 together | | ramp_down |
| 4 | 5 | stair_up |
| 5 | 9 | ramp_up |
| 6 | 10 | toggles sit_down / stand_up |

Combinations need both buttons within about 150 ms.

### LED

| Colour | Meaning |
| --- | --- |
| White | booting |
| **Blue** | idle, all sensors present |
| **Cyan** | idle, but a sensor is missing |
| Green | recording |
| Orange, blinking | ToF dropped out, retrying |
| Red | SD mount failed |
| Purple flash | a label button registered |

Check for blue before starting. Cyan means something did not come up and
you would be recording an incomplete session.

### Procedure

1. Power on, confirm blue.
2. Press button 1 to start.
3. Walk, pressing the matching button whenever the activity changes.
4. Press button 1 again to stop.
5. Power down, pull the card. Files land as `LOG_001.csv` and so on.

Hold each activity for at least 10 seconds — shorter segments get
dropped during preprocessing. Repeat stairs and ramps several times; a
handful of events is not enough to conclude anything.

Bright sunlight degrades the ranging measurements considerably. Overcast
days or shade give much cleaner data.

---

## Analysis

Put the CSVs somewhere under `analysis/` and run in order.

```bash
# quality check - run this the same day you collect
python3 01_check_quality.py data/LOG_001.csv

# look at the signals
python3 02_visualize.py data/LOG_001.csv

# supervised benchmark
python3 03_classify.py data/LOG_001.csv data/LOG_002.csv

# how well activities separate with no labels given
python3 04_unsupervised.py data/*.csv
```

### CSV columns

| Column | Meaning |
| --- | --- |
| elapsed_ms | milliseconds since the run started |
| label | whichever button was last pressed |
| dist1_mm / dist2_mm | left / right distance |
| status1 / status2 | 0 is a good reading; anything else is suspect |
| signal_kcps | returned signal strength |
| ambient_kcps | background light, climbs sharply outdoors |
| sigma_mm | measurement uncertainty; smaller is better |
| yaw / pitch / roll | orientation — **only pitch is usable**, see below |

### If you change the analysis code

Feature extraction lives in `gait_lib.py`. Editing it changes what
scripts 03 and 04 report.

Do not switch the cross-validation to a shuffled split. Windows overlap
by design, so a random split puts nearly identical rows on both sides.
On the current data a shuffled split reports 99% where a time-blocked
split reports 83%. `03_classify.py` prints both so the gap stays visible.

---

## Known problems

### IMU yaw and roll are unusable

Pitch is stable. Yaw and roll jump by 90 degrees or more between
consecutive samples — 140 and 36 times respectively in one session.

This is gimbal lock. Euler angles lose a degree of freedom when pitch
approaches ±90°, and the current mounting keeps pitch around −80°, so
the rig sits in that condition the whole time.

Pitch happens to be the axis that matters most for walking — hip flexion
and extension covers 40 to 60 degrees of range while the other axes move
maybe 5 to 10 — so the analysis works, but two thirds of the orientation
data is being thrown away.

Two ways out: rotate the sensor 90 degrees so pitch sits near zero, or
log quaternions instead of Euler angles. Quaternions do not have this
failure mode at all.

### Sample rate runs at about 40% of target

Firmware asks for 50 Hz, measured rate is around 20 Hz. SD writes and
sensor reads are eating the loop. `FLUSH_EVERY` is the first thing to
try raising — at the cost of losing more data if power is cut mid-run.
Timing individual sections with `micros()` will show where it actually
goes.

### Outdoor readings get much noisier

`sigma` goes from about 4 mm indoors to 30–50 mm in sunlight, with
`ambient_kcps` roughly 20x higher. Longer timing budgets, physical
shading, or a different sensor are the options.

### Ramps separate less cleanly than stairs

Ramp signal only appears while the leg is swinging forward. With the
thigh near vertical the beam only sees the ground directly underfoot,
where a slope looks much like flat ground. This is geometry, not a
sensor defect.

---

## What to do next

**Fix the data quality issues first.** The gimbal lock and the sample
rate both cap how far anything else can go. Neither is hard to fix.

**Then collect more.** One subject is not a result. Multiple people,
more ramp data specifically, indoor and outdoor, and eventually
impaired gait — which is likely to look quite different from healthy
walking.

**Then try running it alongside H-Medi**, recording IMU and ToF at the
same time, and check whether labels derived from the ToF data agree
with the button-pressed ones.

**Longer term**, move the classifier onto the MCU so labelling happens
live instead of in post.

---

## Files

```
firmware/
  01_MAIN_gait_logger.ino   the logger
  02_sensor_monitor.ino     live readout, no recording

diagnostics/
  i2c_scanner.ino           what is on the I2C bus
  uart_check.ino            are the IMUs sending bytes
  imu_only_test.ino         IMUs alone, with packet rate

analysis/
  gait_lib.py               preprocessing and features, shared
  _font.py                  font fallback for plots
  01_check_quality.py       session sanity check
  02_visualize.py           signal plots
  03_classify.py            supervised benchmark
  04_unsupervised.py        clustering without labels
```
