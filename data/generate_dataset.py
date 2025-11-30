import numpy as np
import pandas as pd
from scipy import signal

# ----------------------------------------------------------
# SETTINGS (adjust as needed)
# ----------------------------------------------------------
fs = 2000                    # Sampling rate (2 kHz)
duration_sec = 600            # 60 seconds of synthetic data
N = fs * duration_sec        # Total samples

fundamental_freq = 50        # 50 Hz mains frequency
noise_level_v = 0.01         # Voltage noise amplitude
noise_level_i = 0.015        # Current noise amplitude

# ----------------------------------------------------------
# LEARN BASIC STATISTICS FROM YOUR SAMPLE
# ----------------------------------------------------------
samples = np.array([
[0.151502,2.274139],
[0.060440,2.179853],
[0.003223,2.076703],
[0.003223,2.000147],
[0.000000,1.927619],
[0.024176,1.856703],
[0.278022,1.803517],
[0.473846,1.763223],
[0.639048,1.710842],
[0.739780,1.714066],
[0.826813,1.718095],
[0.846679,1.723022],
[0.832979,1.732872],
[1.442491,2.260440],
[1.575458,2.359560],
[1.668938,2.421611],
[1.706813,2.449817],
[1.776117,2.519121],
[1.535971,2.587619],
[1.330476,2.658535],
[1.164469,2.721392],
[1.000879,2.767326],
[0.930769,2.800366],
[1.036337,2.798755],
[1.098388,2.787472],
[1.149158,2.779414],
[1.135458,2.747985],
[1.096777,2.724615],
[1.061319,2.791502],
[1.122564,2.793113],
[1.153993,2.776191],
[1.135458,2.737509],
[1.095971,2.706081],
[1.061319,2.664982],
[1.010549,2.626300],
[0.887253,2.556996],
[0.462564,1.762418],
[0.636630,1.724542],
[0.775238,1.710842],
[1.142711,2.786667],
[1.161245,2.770550],
[1.142711,2.740733],
[1.086300,2.690769],
[1.052454,2.672235],
[0.990403,2.617436],
[0.857436,2.527179],
[0.746227,2.449011],
[0.618901,2.374872],
[0.489157,2.284615],
[0.357802,2.190330],
[0.236117,2.093626],
[0.142637,2.021905],
[0.081392,1.967106],
[0.038968,1.928424],
[0.019102,1.899608],
[0.009322,1.876237],
[0.004651,1.860703],
[0.002325,1.847003],
[0.001162,1.838051],
[0.000581,1.830989],
[0.000291,1.826318],
[0.000000,1.822647]
])
[]
V_samples = samples[:,0]
I_samples = samples[:,1]

mean_v, std_v = np.mean(V_samples), np.std(V_samples)
mean_i, std_i = np.mean(I_samples), np.std(I_samples)

# ----------------------------------------------------------
# SYNTHETIC SIGNAL GENERATION
# ----------------------------------------------------------
t = np.arange(N) / fs
# Make slowly varying amplitude (simulates load changes)
amp_mod = 0.1 * np.sin(2*np.pi*0.2*t) + 1.0

# Base sinusoidal + small harmonics
voltage_signal = (mean_v +
                  amp_mod * (0.2*np.sin(2*np.pi*fundamental_freq*t) +
                             0.05*np.sin(2*np.pi*3*fundamental_freq*t)) +
                  np.random.normal(0, noise_level_v, N))

current_signal = (mean_i +
                  amp_mod * (0.15*np.sin(2*np.pi*fundamental_freq*t + 0.3) +
                             0.03*np.sin(2*np.pi*5*fundamental_freq*t)) +
                  np.random.normal(0, noise_level_i, N))

# ----------------------------------------------------------
# SAVE TO CSV FOR EDGE IMPULSE
# ----------------------------------------------------------
df = pd.DataFrame({
    "Vadc": voltage_signal,
    "Iadc": current_signal
})

df.to_csv("synthetic_normal_v_xii_2khz.csv", index=False)
print("Generated synthetic_normal_v_xii_2khz.csv")
