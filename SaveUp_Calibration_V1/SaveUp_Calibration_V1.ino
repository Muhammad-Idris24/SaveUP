/* 
  Edge AI SaveUp - Calibration script (Serial logger + CSV)
  For: Arduino Nano RP2040 Connect
  Sensors: ACS712-05A -> ADC_ACS, ZMPT101B -> ADC_ZMPT, DHT11 optional
  NOTE: Ensure sensor outputs are within 0-3.3V ADC range before running.
  Author: Muhammad Idris (calibration helper)
*/

#include <Arduino.h>

// === PIN CONFIG ===
const int ADC_ACS_PIN = A0;    // ACS712 output (current) -> ADC
const int ADC_ZMPT_PIN = A1;   // ZMPT101B output (voltage) -> ADC
const int DHT_PIN = 2;         // optional
const unsigned long BAUD = 115200;

// === ADC / sampling config ===
const float Vref = 3.3f;       // RP2040 ADC reference
const int ADC_MAX = 4095;      // 12-bit ADC resolution
const float ADC_TO_V = Vref / ADC_MAX;

// Recommended: sampleFreq = 2000 Hz (2kHz)
const unsigned int SAMPLE_FREQ = 2000;
const unsigned int SAMPLE_PERIOD_US = 1000000UL / SAMPLE_FREQ;

// Sample over N mains cycles. For 50 Hz and 40 samples/cycle -> samplesPerCycle = SAMPLE_FREQ / 50
const unsigned int MAINS_HZ = 50;
const unsigned int SAMPLES_PER_CYCLE = SAMPLE_FREQ / MAINS_HZ;
const unsigned int CYCLES_TO_MEASURE = 10;
const unsigned int SAMPLES_PER_WINDOW = SAMPLES_PER_CYCLE * CYCLES_TO_MEASURE;

// === ACS712 sensitivity for 5A module ===
// NOTE: this is nominal. Use calibration to get real CURRENT_SCALE.
// ACS712-05A typical sensitivity ≈ 185 mV/A when powered at 5V (0.185 V/A).
const float ACS_SENS = 0.185f;  // V per amp (nominal)

// calibration values (to be computed interactively)
float Vbias = 0.0f;
float Ibias = 0.0f;
float VOLTAGE_SCALE = 1.0f;
float CURRENT_SCALE = 1.0f;

// Simple millis timestamp helper
unsigned long t0;

void waitForEnter() {
  Serial.println(F("\n>>> Press Enter when ready <<<"));
  while (true) {
    if (Serial.available()) {
      String s = Serial.readStringUntil('\n');
      break;
    }
    delay(10);
  }
}

// synchronous sample window: returns Vrms, Irms, Pavg (real power)
void measureWindow(unsigned int samples, float &Vrms_out, float &Irms_out, float &Pavg_out) {
  double sum_v2 = 0.0;
  double sum_i2 = 0.0;
  double sum_p = 0.0;

  // stable timing using micros
  unsigned long nextMicros = micros();

  for (unsigned int n = 0; n < samples; ++n) {
    // wait until next sample time
    while ((long)(micros() - nextMicros) < 0) { /* busy wait */ }
    unsigned int rawV = analogRead(ADC_ZMPT_PIN);
    unsigned int rawI = analogRead(ADC_ACS_PIN);

    float vVoltage = rawV * ADC_TO_V;
    float vCurrent = rawI * ADC_TO_V;

    // convert to instantaneous mains units using current offsets and scales (scales default 1)
    float v_inst = (vVoltage - Vbias) * VOLTAGE_SCALE;                 // volts (instant)
    float i_inst = ((vCurrent - Ibias) / ACS_SENS) * CURRENT_SCALE;    // amps (instant)

    sum_v2 += (double)v_inst * (double)v_inst;
    sum_i2 += (double)i_inst * (double)i_inst;
    sum_p  += (double)v_inst * (double)i_inst;

    nextMicros += SAMPLE_PERIOD_US;
  }

  Vrms_out = sqrt(sum_v2 / samples);
  Irms_out = sqrt(sum_i2 / samples);
  Pavg_out = (sum_p / samples);
}

// helper to print CSV header
void printCSVHeader() {
  Serial.println(F("timestamp_ms,Vrms_meas,Irms_meas,Pavg_meas,Vbias_V,Ibias_V,VOLTAGE_SCALE,CURRENT_SCALE"));
}

void setup() {
  Serial.begin(BAUD);
  while (!Serial) delay(10);
  pinMode(ADC_ACS_PIN, INPUT);
  pinMode(ADC_ZMPT_PIN, INPUT);

  Serial.println(F("\n=== SaveUp Calibration Utility ==="));
  Serial.println(F("Interactive calibration for ZMPT101B + ACS712-05A"));
  Serial.println(F("Ensure multimeter is ready and probes are safe."));
  Serial.println(F("Make sure ADC inputs are not driven above 3.3V."));
  Serial.println();

  t0 = millis();

  // Step 1: Zero / bias calibration (no-load)
  Serial.println(F("STEP A: NO-LOAD BIAS MEASUREMENT"));
  Serial.println(F("Ensure the monitored outlet is DISCONNECTED from load (no appliance)."));
  Serial.println(F("Leave device powered and mains connected (so ZMPT sees voltage) but no load."));
  waitForEnter();

  // Measure average ADC voltages for a short time to compute bias offsets
  // We'll take a few windows and average them for stability
  const int windowsForBias = 3;
  double Vbias_acc = 0.0;
  double Ibias_acc = 0.0;
  for (int w = 0; w < windowsForBias; ++w) {
    float Vrms_m, Irms_m, Pavg_m;
    measureWindow(SAMPLES_PER_WINDOW, Vrms_m, Irms_m, Pavg_m);

    // But we need bias in volts at ADC (not Vrms). We'll sample mid-ADC using raw ADC reads average:
    // Simpler: take quick average of raw ADC for each channel over one window
    double avgRawV = 0.0, avgRawI = 0.0;
    unsigned long nextMicros = micros();
    for (unsigned int n = 0; n < SAMPLES_PER_WINDOW; ++n) {
      while ((long)(micros() - nextMicros) < 0) {}
      unsigned int rv = analogRead(ADC_ZMPT_PIN);
      unsigned int ri = analogRead(ADC_ACS_PIN);
      avgRawV += rv;
      avgRawI += ri;
      nextMicros += SAMPLE_PERIOD_US;
    }
    avgRawV /= (double)SAMPLES_PER_WINDOW;
    avgRawI /= (double)SAMPLES_PER_WINDOW;
    double avgVvolts = avgRawV * ADC_TO_V;
    double avgIvolts = avgRawI * ADC_TO_V;
    Vbias_acc += avgVvolts;
    Ibias_acc += avgIvolts;
    Serial.print(F("  Bias window ")); Serial.print(w+1);
    Serial.print(F(": ADC Vbias(V) = ")); Serial.print(avgVvolts, 4);
    Serial.print(F("  ADC Ibias(V) = ")); Serial.println(avgIvolts, 4);
  }
  Vbias = (float)(Vbias_acc / windowsForBias);
  Ibias = (float)(Ibias_acc / windowsForBias);

  Serial.println();
  Serial.print(F("Computed Vbias (ADC volts) = ")); Serial.println(Vbias, 6);
  Serial.print(F("Computed Ibias (ADC volts) = ")); Serial.println(Ibias, 6);
  Serial.println(F("Bias values saved to memory (volatile)."));

  // Step 2: Voltage scaling with multimeter
  Serial.println();
  Serial.println(F("STEP B: VOLTAGE SCALING"));
  Serial.println(F("Connect your trusted multimeter across the mains (LIVE and NEUTRAL)"));
  Serial.println(F("Set meter to AC RMS (e.g., 230V)."));
  Serial.println(F("When ready, press Enter to let the device sample its Vrms."));
  waitForEnter();

  float Vrms_measured = 0.0, Irms_meas_tmp = 0.0, Pavg_tmp = 0.0;
  measureWindow(SAMPLES_PER_WINDOW, Vrms_measured, Irms_meas_tmp, Pavg_tmp);
  Serial.print(F("Device measured Vrms = ")); Serial.print(Vrms_measured, 4);
  Serial.println(F(" V (device units)"));

  Serial.println(F("Now enter the MULTIMETER Vrms value (numeric, e.g., 230.0) then press Enter:"));
  while (!Serial.available()) { delay(10); }
  String vline = Serial.readStringUntil('\n');
  vline.trim();
  float Vrms_true = vline.toFloat();
  if (Vrms_true <= 0.0) {
    Serial.println(F("Invalid input. Using Vrms_true = Vrms_measured (scale = 1)."));
    VOLTAGE_SCALE = 1.0f;
  } else {
    VOLTAGE_SCALE = Vrms_true / Vrms_measured;
  }
  Serial.print(F("Computed VOLTAGE_SCALE = ")); Serial.println(VOLTAGE_SCALE, 6);

  // Step 3: Current scaling with known load and clamp meter
  Serial.println();
  Serial.println(F("STEP C: CURRENT SCALING"));
  Serial.println(F("Attach a reliable known load (or the appliance) and measure current with a clamp meter or multimeter."));
  Serial.println(F("Make sure the ACS712 is in series with LIVE conductor for the measured circuit."));
  Serial.println(F("When ready, press Enter to let the device sample its Irms."));
  waitForEnter();

  float Vrms_tmp2 = 0.0, Irms_measured = 0.0, Pavg_measured = 0.0;
  measureWindow(SAMPLES_PER_WINDOW, Vrms_tmp2, Irms_measured, Pavg_measured);

  Serial.print(F("Device measured Irms = ")); Serial.print(Irms_measured, 5);
  Serial.println(F(" A (device units)"));

  Serial.println(F("Now enter the TRUE current measured by clamp-meter (numeric, e.g., 0.435) then press Enter:"));
  while (!Serial.available()) { delay(10); }
  String iline = Serial.readStringUntil('\n');
  iline.trim();
  float Irms_true = iline.toFloat();
  if (Irms_true <= 0.0) {
    Serial.println(F("Invalid input. Using Irms_true = Irms_measured (scale = 1)."));
    CURRENT_SCALE = 1.0f;
  } else {
    CURRENT_SCALE = Irms_true / Irms_measured;
  }
  Serial.print(F("Computed CURRENT_SCALE = ")); Serial.println(CURRENT_SCALE, 6);

  // Final verification measurement and CSV output
  Serial.println();
  Serial.println(F("=== VERIFICATION MEASUREMENT ==="));
  float Vrms_verify=0, Irms_verify=0, Pavg_verify=0;
  measureWindow(SAMPLES_PER_WINDOW, Vrms_verify, Irms_verify, Pavg_verify);

  float Vrms_real = Vrms_verify * VOLTAGE_SCALE;
  float Irms_real = Irms_verify * CURRENT_SCALE;
  float Pavg_real = Pavg_verify * VOLTAGE_SCALE * CURRENT_SCALE; // approximate

  unsigned long timestamp_ms = millis() - t0;

  // Print computed constants and CSV header + sample line
  Serial.println();
  Serial.println(F("=== CALIBRATION RESULTS ==="));
  Serial.print(F("Vbias (ADC volts) = ")); Serial.println(Vbias, 6);
  Serial.print(F("Ibias (ADC volts) = ")); Serial.println(Ibias, 6);
  Serial.print(F("VOLTAGE_SCALE = ")); Serial.println(VOLTAGE_SCALE, 9);
  Serial.print(F("CURRENT_SCALE = ")); Serial.println(CURRENT_SCALE, 9);

  Serial.println();
  Serial.println(F("CSV header and sample line (copy to spreadsheet):"));
  printCSVHeader();
  // Output CSV line
  Serial.print(timestamp_ms); Serial.print(',');
  Serial.print(Vrms_real, 5); Serial.print(',');
  Serial.print(Irms_real, 6); Serial.print(',');
  Serial.print(Pavg_real, 6); Serial.print(',');
  Serial.print(Vbias, 6); Serial.print(',');
  Serial.print(Ibias, 6); Serial.print(',');
  Serial.print(VOLTAGE_SCALE, 9); Serial.print(',');
  Serial.println(CURRENT_SCALE, 9);

  Serial.println();
  Serial.println(F("Calibration finished. Please copy the VOLTAGE_SCALE and CURRENT_SCALE into your main firmware."));
  Serial.println(F("If you want to run calibration again, press the board's Reset button."));
}

void loop() {
  // calibration runs in setup(); nothing in loop
  delay(1000);
}
