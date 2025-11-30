# SaveUp — Smart Energy Insight Node

![SaveUp banner](docs/banner.png)  
*Reduce wasted electricity — monitor appliances, detect faults locally using TinyML, and get real-time alerts with ThingsBoard.*

---

## TL;DR (elevator pitch)
Nigeria’s power crisis has become a daily math problem nobody wants to solve.
Tariffs climbed, prepaid meters drain faster than people can say “unit don finish,” and every home, shop, hostel, and small business is now playing blindfolded hide-and-seek with their own appliances. Something is consuming power — but nobody knows what. Even engineers spend weeks probing circuits, guessing loads, swapping batteries, and upsizing solar components, only to walk away without real answers.

But imagine if the guessing stopped.

SaveUp steps into the chaos as a tiny, affordable plug- or circuit-level IoT node built for the real messiness of Nigerian electricity. It measures voltage, current, power, and environmental conditions with precision, then uses on-device TinyML — trained on Edge Impulse — to detect abnormal behavior instantly. No stable internet required. No cloud lag. The intelligence lives on the device.

When SaveUp spots standby waste, a failing freezer, an overloaded socket, or an appliance quietly gulping units, it flags it and reports to a ThingsBoard dashboard that’s simple enough for households and powerful enough for technicians. People finally know what to unplug. Engineers finally see what’s actually wrong. Solar installers stop guessing and start optimizing.

And while the problem is local, the innovation speaks globally:
It’s a scalable, edge-AI energy intelligence node built for the world’s hardest electrical environments — places where power is unstable, internet is unreliable, and every watt counts.

SaveUp turns energy awareness into energy savings, one plug at a time, proving that meaningful climate-tech doesn’t always need to be expensive… sometimes it just needs to be smart.

![SaveUp flyer](docs/saveUp.png) 

---

## Contents (this README)
1. Project story & high-level flow  
2. Hardware & BOM  
3. Circuit / wiring & safety notes (Stage 1) 
4. Firmware & calibration (Stage 2–3)  
5. ThingsBoard integration (Stage 4)  
6. Dataset & Edge Impulse pipeline (Stage 5)  
7. Model training & deployment details  
8. How it behaves in the real world — demo & screenshots  
9. What to add next + contributions  
10. Quick start & flash instructions  
11. License & credits

---

# 1. Project story & flow (start → finish)

**Problem:** electricity costs are high and power is unreliable in Africa. Appliances waste energy due to standby losses or failing components (fridges, motors). Most people don't have appliance-level visibility.

**Solution:** SaveUp — a small IoT node that:
- measures voltage, current, temperature, humidity,
- computes Vrms, Irms, Pavg, PF, and accumulates Wh,
- runs a TinyML anomaly detector on the raw waveform locally (Edge Impulse),
- publishes telemetry and anomaly flags to ThingsBoard for charts, alerts, and remote relay control.

**High-level flow (visual story):**
1. Sensors → raw ADC → sampling at **2 kHz**.  
2. Short-window processing (400 samples = 0.2 s) → Vrms/Irms/Pavg.  
3. Parallel: window samples added to EI buffer → inference run via `run_classifier()` → anomaly score.  
4. Telemetry JSON published to ThingsBoard: `{ Vrms, Irms, Pavg, PF, Wh, Temp, Hum, anomaly, anomaly_score }`  
5. ThingsBoard dashboard shows live charts, cost widgets, and alarms; can send RPC `setRelay` to the node.

(Insert animated GIF showing flow: sensor → device → ThingsBoard dashboard.)  
`![flow gif](docs/flow.gif)`

---

# 2. Hardware design & prototype

## Chosen hardware & why
- **Arduino Nano RP2040 Connect** — RP2040 performance + WiFi (good memory & community support. One can use better hardwares fromArduino, ESP, Raspberry Pi, etc.)  
- **ACS712-05A** — affordable current sensor (±5 A) suitable for single-appliance loads (use appropriate variant, such as ACS712-20A (better choice but wasn't available at the time of this project) if expecting higher current).  
- **ZMPT101B** voltage sensor** — small board for AC voltage sensing (isolation via transformer module). Also the best choice for this project.  
- **DHT11** — temperature & humidity (simple context. Choose DHT22 for a better result).  
- **5V Relay Module** — control/remote power cutoff.  
- **I2C LCD (0x27)** — quick local feedback.  
- **Misc: perfboard, fuse, mains-rated wires, connectors.**

> Why these: low-cost, easy to source locally, fits contest budget and is adequate for prototyping.

## Bill of Materials (BOM) — example
| Qty | Component | Purpose | Approx price (₦) | Notes / Supplier/ online |
|---:|---|---:|---:|---|
| 1 | Arduino Nano RP2040 Connect | MCU + Wi-Fi | 15,000–20,000 | Ossy Solar Ltd ([Amazon](https://www.amazon.com/Arduino-Nano-RP2040-Connect-Headers/dp/B095J4KFVT)) |
| 1 | ACS712-05A | Current sensing ±5A | 2,500 | Ossy Solar Ltd ([Amazon](https://www.amazon.com/acs712-current-sensor/s?k=acs712+current+sensor))|
| 1 | ZMPT101B | Voltage sensor | 5,000 | Ossy Solar Ltd ([Amazon](https://www.amazon.com/s?k=ZMPT101B&crid=1DMJSYMZLP4S&sprefix=zmpt101b%2Caps%2C675&ref=nb_sb_noss_1))|
| 1 | DHT11 | Temp & humidity | 1,500 | Ossy Solar Ltd ([Amazon](https://www.amazon.com/s?k=DHT11&crid=1ZWCZBVA2YP4B&sprefix=dht11%2Caps%2C392&ref=nb_sb_noss_1)) |
| 1 | Relay module (5V) | Remote on/off | 1,500 | Ossy Solar Ltd ([Amazon](https://www.amazon.com/s?k=Relay+module+%285V%29&crid=3BWZQ8MPK3IVY&sprefix=relay+module+5v+%2Caps%2C690&ref=nb_sb_noss_2)) |
| 1 | 5V PSU adapter | Board power | 1,000 | Ossy Solar Ltd ([Amazon](https://www.amazon.com/s?k=5V+PSU+adapter&crid=2HO46EON9DOCR&sprefix=5v+psu+adapter%2Caps%2C363&ref=nb_sb_noss)) |
| misc | Wires, connectors, fuse, enclosure | — | 6,000 | Ossy Solar Ltd |
| **Total** |  |  | **~₦24,000–29,000** | |

## Circuit & wiring (Stage 1)

**Important**: use fuses, insulated connectors, and keep mains wiring to a board with proper isolation. If unsure, ask a qualified electrician.

Assumptions: Mains = 230 VAC, 50 Hz (Nigeria). Use the ACS712 in series with live/hot conductor only.

## Hardware wiring
- **High-level**: Live (mains) → Fuse → **Relay** (COM) → Load (NO on relay) → Neutral.

- Place **ACS712** in series on the live conductor between fuse and load (or between COM and NO depending on layout).

- **ZMPT101B** module: connect module input across Live and Neutral (module provides isolated low-voltage AC output). Connect module output to RP2040 ADC through bias circuit if needed (ZMPT usually outputs AC centered around 0V — check module docs).

- **ACS712**: VCC = 5 V (or 5 V supply if sensor needs 5 V), GND = common ground with MCU, OUT → MCU ADC (through biasing circuit). ACS712 output at 0 A = VCC/2.

- **DHT11**: VCC = 5 V (or 3.3 V if module supports), DATA → RP2040 digital pin (with 4.7k–10k pull-up if required).

- **I²C LCD**: SDA/SCL → RP2040 SDA/SCL (I²C pins), VCC 5 V or 3.3 V depending on module; level-shift if needed.

- **RP2040 ADC**: ADC inputs expect 0–3.3 V. Do not feed >3.3 V. Use level shifting or scaling if required.

- **Common ground**: keep MCU ground and sensor grounds common; but be mindful ZMPT101B provides isolation at module level — treat carefully.

Pin (Arduino sketch-level):

ACS712 OUT → A0 (ADC)

ZMPT101B OUT → A1 (ADC)

DHT11 DATA → D2

Relay IN → D3 (use a transistor/optocoupler if module is not opto)

LCD I²C → SDA/SCL (pins on Nano RP2040 Connect)

**Schematic (visual)**: include `docs/schematic1.png` and `docs/schematic2.png`.  
`![schematic](docs/schematic3.png)`

**Soldering & prototyping**
- Steps: prepare perfboard, place modules, secure ACS712 with isolation, solder I2C lines for LCD, secure wires inside heat-shrink tubing, mount relay.
- Add animated GIF: `docs/soldering.gif` showing soldering steps and hot-air.

---

# 3. Firmware (Stage 2–3 summary)

## What the firmware does (v1.0-alpha)
- 2 kHz sampling (blocking window of 400 samples = 0.2 s).  
- ADC → convert to volts with `ADC_TO_V`. Subtract bias `Vbias` / `Ibias`.  
- Compute: `Vrms`, `Irms`, `Pavg`, `PF`, `Wh` accumulation.  
- DHT11 read on `D2` and I2C LCD at `0x27`.  
- MQTT to ThingsBoard: sends JSON telemetry periodically.
- Edge Impulse inference runs on buffered window and sets `anomaly` flags.  
- RPC `setRelay` supported for remote switching.

## Calibrate: bias & scale
**Calibration script** (serial mode) measured these:
Vbias = 1.449729f
Ibias = 2.305866f
VOLTAGE_SCALE = 227.540139771
CURRENT_SCALE = 0.264412100

**How to calibrate (summary):**
1. No-load bias (device offline from load) → measure ADC average for V and I → set `Vbias` and `Ibias`.  
2. Put calibrated multimeter on mains → read Vrms → compute `VOLTAGE_SCALE = Vrms_meter / Vrms_device`.  
3. Place known load or use clamp meter → compute `CURRENT_SCALE = Irms_meter / Irms_device`.

**Calibration script**: (path) `SaveUp_Calibration_v1/SaveUp_Calibration_V1.ino` — runs interactive serial prompts and prints CSV of calibration constants.

## Libraries to install
- WiFiNINA  
- PubSubClient  
- ArduinoJson (v6.x)  
- DHT sensor library  
- LiquidCrystal_I2C  
- Edge Impulse Arduino ZIP (your `ei-anomaly-detection-arduino-1.0.1.zip`)

## Where to find firmware
- `Final_firmware_Version_1.0-alpha/Final_firmware_Version_1.0-alpha.ino` (main sketch)  
- `SaveUp_Calibration_v1/SaveUp_Calibration_V1.ino` (bias & scale calibration helper)  
- `firmware/measurement_only.ino` (minimal stable sampling for data collection)

---

# 4. ThingsBoard & IoT integration (Stage 4)

## Steps (simple)
1. Create ThingsBoard Cloud or local account: **ThingsBoard Cloud** (or your host).  [Link](https://thingsboard.cloud/dashboards/all/ed478400-cc37-11f0-8d27-9f87c351edd8)
2. Create Device: `SaveUp Device` → copy Access Token.  
3. Update `ACCESS_TOKEN` in firmware and upload.  
4. Device publishes to MQTT topic: `v1/devices/me/telemetry`.  
5. Create Dashboard → add widgets (Vrms, Irms, Pavg, PF, Temp/Hum, Wh aggregates, Relay control).  

## Manual Widget Creation (For the Dashboard)
1) Vrms Line Chart
- Dashboard → Edit → Add widget → Charts → Line Chart
- Datasource: Device → select your SaveUp device → Keys → add Vrms
- Aggregation: None (raw)
- Time window: last 5 min (for live)

Save, place top-left.

2) Irms Line Chart
- Same as above but key Irms.

3) Pavg Gauge
- Widget: Gauges → Donut / Linear gauge
- Datasource key: Pavg
- Units: W
- Min: 0, Max: pick e.g. 2000 or adjust to expected loads

4) Power Factor Gauge
- Key: PF, min 0, max 1.

5) Temp & Humidity Dual Line
- Widget: Charts → Line Chart (multi-series)
- Keys: Temp and Hum.

6) Daily/Weekly Wh
- Widget: Aggregations → Time Series bar
- Datasource: Wh with aggregation SUM.

Configure time window to 1 day and 7 days respectively.

7) Cost Widget (Daily & Weekly)
- Add a Card widget. Use the widget’s settings to:
- Data source: timeseries Wh with SUM and time window 1 day (for daily).
- In the content/template field put a formula to compute cost, or use a script in the widget to compute:
    cost = (Wh_sum / 1000) * TARIFF_NAIRA_PER_KWH

Replace TARIFF_NAIRA_PER_KWH with your local tariff (e.g., 250 ₦/kWh).

Repeat for weekly.

**Tip**: ThingsBoard lets you use Liquid templates or JS inside some widgets to compute derived values. Use that if you want the dashboard to compute cost live.

8) Relay Control (Switch)
- Add widget → Controls → Switch
- Configure as an RPC control:
- Method: setRelay
- Params: raw true/false (or JSON { "state": true } depending on the firmware parsing)
- When toggled, ThingsBoard will publish RPC to v1/devices/me/rpc/request/<id>; your device must be subscribed (the firmware already subscribes).

9) Alarm Widget
- Add built-in Alarm widget to view generated alarms.


6. Create Rule Chain: monitor telemetry → create Alarm nodes (Overcurrent, Low PF, High Temp, anomaly flag) → Email/SMS/Webhook actions (optional).

##Rule Chain: Create Alert Rules & Actions
- Add simple nodes to the Root Rule Chain (or create a new Rule Chain and link it). The basic flow for each rule:
- Telemetry → Script Filter Node → Generate Alarm Node → Send Email/Webhook Node

### Create a Rule Chain (recommended)
Rule Chains → + Add new Rule Chain → name SaveUp Alerts

- Open SaveUp Alerts and Drag nodes as below:
    Node A — Input
    Msg Type Switch (or use default Originator Type = Device Telemetry)

    Node B — Script (filter for overcurrent)
    Script type node (JavaScript)
    Script:
        // payload example: { "Irms": 1.23, "PF": 0.98, "Temp": 30 }
        var data = msg;
        if (data && data.Irms && (data.Irms > 6.0)) {
        // forward the message and attach alarm metadata
        metadata.alarmType = "OVER_CURRENT";
        metadata.severity = "CRITICAL";
        metadata.details = "Irms="+data.Irms;
        return { msg: msg, metadata: metadata, msgType: msgType };
        }
        return null;

    Set output to next node on true.

    Node C — Create Alarm
    Create Alarm node: configure:  
    Alarm type: OVER_CURRENT    
    Severity: CRITICAL
    Propagate originator = true

    Node D — Email / Webhook
    Email node: configure SMTP settings in ThingsBoard (Admin → System Settings) or Webhook node to call an external service (SMS/WhatsApp API).
    
    Connect Create Alarm → Email/Webhook.

    Node E — RPC Action (optional)
    To automatically open relay when overcurrent is detected (careful): add RPC Call node that sends a one-way RPC to the device:
    Topic: v1/devices/me/rpc/request/1  
    Payload: {"method":"setRelay","params":false}

    Repeat the above with Script nodes for:

    PF low: if (data.PF && data.PF < 0.5) { ... }
    Temp high: if (data.Temp && data.Temp > 60) { ... }
    anomaly flag: if (data.anomaly === true) { ... }

    Important safety note: Auto-cutoff RPCs can be powerful — for testing use reduced thresholds and confirm you can remotely re-enable only after manual inspection.

## ThingsBoard telemetry format (example)
```json
{
  "Vrms": 230.45,
  "Irms": 0.432,
  "Pavg": 99.34,
  "PF": 0.98,
  "Wh": 12.345,
  "Temp": 29.1,
  "Hum": 55.2,
  "anomaly": true,
  "anomaly_score": 0.82
}
```

- Control (RPC)
To remotely toggle relay: from ThingsBoard widget or REST API use RPC method setRelay with params true/false.

## Find the JSON files for dashboard and rule chain:
- Dashboard - `./Dashboard.json`
- Rule Chain - `./RULE CHAIN.json`

## Images from ThingsBoard Set Up:
`docs/TB/1.png`
`docs/TB/2.png`
`docs/TB/4.png`
`docs/TB/10.png`
`docs/TB/9.png`
`docs/TB/13.png`
`docs/TB/12.png`
`docs/TB/6.png`
`docs/TB/5.png`

# 5. Dataset & Edge Impulse pipeline (Stage 5)

## Dataset collection
### Strategy and produced dataset
- What I collected: generated synthetic-ish time series derived from logged device samples (8 "normal" datasets, each 1 minute long) because time was limited.
- Channels captured: voltage, current (both sampled at 2000 Hz).
- Windowing: 400-sample windows (0.2 s), overlap 200 (50%).
- Labeling: normal for training anomaly detector; optional test anomaly files for evaluation if you can simulate faults.

### Curation & tips
- Include: different loads, seasons (temp), times of day, generator vs mains, powering cycles of compressor, motor startup ramps, standby modes.
- More data = better model generalization. For anomaly detection: lots of normal data (hours if possible). For supervised classifier: balanced labeled sets for each class.

**Note**: synthetic datasets are OK for prototyping but may not generalize to real-world faults.

## Images  from Data Collection:
`docs/EI/1.png`
`docs/EI/3.png`
`docs/EI/4.png`
`docs/EI/7.png`

# 6. Pipeline creation (Edge Impulse)

## Impulse settings (recommended)
- Click on create impulse
- Sampling rate: 2000 Hz
- Window size: 400 samples (0.2 s)
- Window increase: 200 (50% overlap)
- DSP blocks: Raw + Spectral (FFT size 256) + optional statistical features
- Learning block: Anomaly detection (autoencoder / EI anomaly block)

## Why these choices
- 2 kHz captures mains waveform plus harmonics and transients important for motor faults.
- 0.2 s window aligns with multiple mains cycles (10 cycles @50 Hz) giving consistent RMS measurements and transient detection.
- FFT helps detect new harmonics from bearing faults or compressor issues.
- Anomaly detector is ideal when you lack many labeled fault examples.

## Images  from Impulse creation
`docs/EI/8.png`
`docs/EI/9.png`
`docs/EI/10.png`
`docs/EI/12.png`

# 7. Model training

Train anomaly detector (start with default network); monitor AUC and ROC.
- Click Train model (Anomaly).
- Choose training options (leave defaults for first run).
- Click Start training — this may take a few minutes depending on model size chosen.

**What to watch:**
- Edge Impulse will report training loss and validation metrics. For anomaly detectors you'll see ROC/AUC or detection thresholds.
- If training fails due to insufficient data, upload more normal samples.

Tune model size, quantization (int8) and try to keep RAM < 64 KB and Flash < 200–300 KB.

## Why chosen settings
- Anomaly detection chosen because of limited labeled fault data and because it flags previously unseen abnormal patterns.
- Quantization to int8 was chosen to reduce memory footprint for on-device inference.

## Images  from Model Training
`docs/EI/13.png`
`docs/EI/14.png`
`docs/EI/15.png`
`docs/EI/16.png`
`docs/EI/17.png`

# 8. Testing & deployment

## Export & integration
- Export: Arduino library from Edge Impulse (zip).
- Install via Arduino IDE → Include .ZIP Library.
- Integrate run_classifier() flow into firmware. Use signal_t with get_data function that copies samples from a live circular buffer.

## Why Arduino library/TFLite?
- Arduino library from EI wraps TFLite Micro and simplifies sample buffer handling — fastest to integrate.
- RP2040 has enough resources; the Arduino export will be quantized and small enough.

## Validation checklist
- Confirm inference on known test windows in Edge Impulse Studio matches on-device inference.
- Verify telemetry anomaly and anomaly_score land in ThingsBoard and trigger Rule Chain alarms.
- Test RPC control from ThingsBoard and safety trip behavior.

9. Real-world demos & visuals
What SaveUp does in the field

Live dashboards show Vrms/Irms/time-series + Wh totals.

When an anomaly is detected (bearing fault / compressor failure / standby losses), device sets anomaly=true and sends anomaly_score.

Rule Chain can raise an alarm and optionally cut power via relay (auto-shutdown on severe faults).

Include animated screenshots & short video:

Wiring + soldering GIF (docs/soldering.gif)

Dashboard live chart GIF (docs/dashboard_live.gif)

Demo of anomaly detection on device serial logs (docs/ei_detect.gif)

Short explainer video (30–60 s) demonstrating end-to-end.