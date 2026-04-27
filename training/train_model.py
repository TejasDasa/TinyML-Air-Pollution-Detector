#!/usr/bin/env python3
"""
Air Pollution Detector – TFLite Model Training Script
======================================================

This script trains a small neural network on labelled air quality data and
exports it as a TFLite flatbuffer that can be deployed directly to the ESP32.

DATA FORMAT (CSV)
-----------------
Expected file: training/data/collected_data.csv
Columns (in order):
    pm2_5, pm10, voc_index, nox_index, co2_ppm, iaq,
    temperature, humidity, pressure,
    aqs_label, rdr_label

    aqs_label  – Air Quality Score  [0, 1]  (1 = excellent, 0 = very polluted)
    rdr_label  – Respiratory Distress Risk [0, 1]  (1 = high risk)

If no CSV exists, a synthetic dataset is generated for development/testing.

COLLECTING REAL DATA
--------------------
1. Flash the firmware with serial logging enabled.
2. Walk your campus routes while the device logs to Serial at 115200 baud.
3. Use `tools/log_serial.py` (or any serial terminal) to capture readings.
4. Label the data (or derive labels from standard AQI / WHO thresholds).
5. Re-run this script with your CSV to produce a calibrated model.

OUTPUT
------
  training/output/model_data.h  – C header file with the TFLite flatbuffer
                                   Copy this into src/ml/model_data.h and
                                   set MODEL_LOADED = 1.

NORMALISATION CONSTANTS
-----------------------
After training, update the kNorm* arrays in src/ml/inference.h to match
the printed "Feature statistics" output of this script.
"""

import os
import sys
import struct
import numpy as np
import pandas as pd
from pathlib import Path

# ── Constants ──────────────────────────────────────────────────────────────────
FEATURES = ['pm2_5', 'pm10', 'voc_index', 'nox_index', 'co2_ppm',
            'iaq', 'temperature', 'humidity', 'pressure']
TARGETS  = ['aqs_label', 'rdr_label']

DATA_DIR   = Path(__file__).parent / 'data'
OUTPUT_DIR = Path(__file__).parent / 'output'
OUTPUT_DIR.mkdir(exist_ok=True)

N_INPUTS  = len(FEATURES)
N_OUTPUTS = len(TARGETS)

# ── Imports ────────────────────────────────────────────────────────────────────
try:
    import tensorflow as tf
    from sklearn.model_selection import train_test_split
    from sklearn.preprocessing import StandardScaler
    import matplotlib.pyplot as plt
    print(f"TensorFlow {tf.__version__}")
except ImportError as e:
    print(f"Missing dependency: {e}")
    print("Run: pip install -r requirements.txt")
    sys.exit(1)


# ═══════════════════════════════════════════════════════════════════════════════
# 1. Load or synthesise dataset
# ═══════════════════════════════════════════════════════════════════════════════

def generate_synthetic_data(n_samples: int = 5000) -> pd.DataFrame:
    """
    Generates a synthetic dataset using physics-inspired heuristics.
    Suitable for initial testing only – replace with real campus walk data.
    """
    rng = np.random.default_rng(42)

    # Simulate three scenario types: clean, moderate, polluted
    scenarios = rng.choice(['clean', 'moderate', 'polluted'],
                            size=n_samples, p=[0.5, 0.3, 0.2])

    def randn_clamp(loc, scale, lo, hi, size):
        return np.clip(rng.normal(loc, scale, size), lo, hi)

    rows = []
    for s in scenarios:
        if s == 'clean':
            pm25 = randn_clamp(8,   6,   0,  35,  1)[0]
            pm10 = randn_clamp(18,  12,  0,  60,  1)[0]
            voc  = randn_clamp(100, 30,  1, 200,  1)[0]
            nox  = randn_clamp(10,  8,   1, 100,  1)[0]
            co2  = randn_clamp(450, 50, 380, 800,  1)[0]
            iaq  = randn_clamp(50,  25,  0, 150,  1)[0]
        elif s == 'moderate':
            pm25 = randn_clamp(30,  15,  12,  80,  1)[0]
            pm10 = randn_clamp(65,  30,  25, 160,  1)[0]
            voc  = randn_clamp(200, 60, 100, 350,  1)[0]
            nox  = randn_clamp(60,  30,  10, 200,  1)[0]
            co2  = randn_clamp(900, 200, 500,1800,  1)[0]
            iaq  = randn_clamp(150, 60,  50, 300,  1)[0]
        else:  # polluted
            pm25 = randn_clamp(100, 40,  55, 300,  1)[0]
            pm10 = randn_clamp(200, 80, 100, 500,  1)[0]
            voc  = randn_clamp(350, 80, 200, 500,  1)[0]
            nox  = randn_clamp(180, 60,  80, 500,  1)[0]
            co2  = randn_clamp(2000,600,1000,5000,  1)[0]
            iaq  = randn_clamp(300, 80, 150, 500,  1)[0]

        temp = randn_clamp(22, 8, -5, 45, 1)[0]
        hum  = randn_clamp(50, 20, 10, 95, 1)[0]
        pres = randn_clamp(1013, 15, 970, 1050, 1)[0]

        # Derive labels from heuristics (same logic as firmware fallback)
        pm25_bad  = min(pm25 / 150.0, 1.0)
        pm10_bad  = min(pm10 / 354.0, 1.0)
        co2_bad   = min(max(co2 - 400, 0) / 4600.0, 1.0)
        voc_bad   = min((voc - 1) / 499.0, 1.0)
        iaq_bad   = min(iaq / 500.0, 1.0)
        pollution = 0.35*pm25_bad + 0.20*pm10_bad + 0.20*iaq_bad + 0.15*co2_bad + 0.10*voc_bad
        aqs = 1.0 - np.clip(pollution, 0, 1) + rng.normal(0, 0.02)
        aqs = float(np.clip(aqs, 0, 1))

        rdr_pm25 = min(pm25 / 55.0, 1.0)
        rdr_pm10 = min(pm10 / 154.0, 1.0)
        rdr_co2  = min(max(co2 - 1000, 0) / 3000.0, 1.0)
        rdr_voc  = min(max(voc - 150, 0) / 350.0, 1.0)
        rdr_iaq  = min(max(iaq - 100, 0) / 400.0, 1.0)
        rdr_nox  = min((nox - 1) / 199.0, 1.0)
        rdr = (0.30*rdr_pm25 + 0.20*rdr_pm10 + 0.20*rdr_iaq +
               0.15*rdr_co2  + 0.10*rdr_nox  + 0.05*rdr_voc)
        rdr = float(np.clip(rdr + rng.normal(0, 0.02), 0, 1))

        rows.append([pm25, pm10, voc, nox, co2, iaq, temp, hum, pres, aqs, rdr])

    df = pd.DataFrame(rows, columns=FEATURES + TARGETS)
    print(f"[DATA] Generated {n_samples} synthetic samples")
    return df


def load_data() -> pd.DataFrame:
    csv_path = DATA_DIR / 'collected_data.csv'
    if csv_path.exists():
        df = pd.read_csv(csv_path)
        print(f"[DATA] Loaded {len(df)} samples from {csv_path}")
        missing = [c for c in FEATURES + TARGETS if c not in df.columns]
        if missing:
            print(f"[DATA] ERROR: Missing columns: {missing}")
            sys.exit(1)
        return df[FEATURES + TARGETS].dropna()
    else:
        print(f"[DATA] No CSV found at {csv_path} – using synthetic data")
        print(f"[DATA] Tip: collect real data with the firmware and place it at {csv_path}")
        DATA_DIR.mkdir(exist_ok=True)
        df = generate_synthetic_data()
        df.to_csv(DATA_DIR / 'synthetic_data.csv', index=False)
        return df


# ═══════════════════════════════════════════════════════════════════════════════
# 2. Build and train model
# ═══════════════════════════════════════════════════════════════════════════════

def build_model() -> tf.keras.Model:
    inp = tf.keras.Input(shape=(N_INPUTS,), name='sensor_input')
    x   = tf.keras.layers.Dense(32, activation='relu', name='fc1')(inp)
    x   = tf.keras.layers.Dense(16, activation='relu', name='fc2')(x)
    out = tf.keras.layers.Dense(N_OUTPUTS, activation='sigmoid', name='output')(x)
    model = tf.keras.Model(inputs=inp, outputs=out)
    model.compile(
        optimizer=tf.keras.optimizers.Adam(1e-3),
        loss='mse',
        metrics=['mae'])
    return model


def train(df: pd.DataFrame):
    X = df[FEATURES].values.astype(np.float32)
    y = df[TARGETS].values.astype(np.float32)

    # Normalisation
    scaler = StandardScaler()
    X_scaled = scaler.fit_transform(X)

    print("\n[NORM] Feature statistics (copy into src/ml/inference.h kNorm* arrays):")
    for i, feat in enumerate(FEATURES):
        print(f"  {feat:<20} mean={scaler.mean_[i]:8.3f}  std={scaler.scale_[i]:8.3f}")

    X_train, X_val, y_train, y_val = train_test_split(
        X_scaled, y, test_size=0.2, random_state=42)

    model = build_model()
    model.summary()

    callbacks = [
        tf.keras.callbacks.EarlyStopping(patience=20, restore_best_weights=True),
        tf.keras.callbacks.ReduceLROnPlateau(factor=0.5, patience=10),
    ]

    history = model.fit(
        X_train, y_train,
        validation_data=(X_val, y_val),
        epochs=200,
        batch_size=32,
        callbacks=callbacks,
        verbose=1)

    # Evaluate
    val_loss, val_mae = model.evaluate(X_val, y_val, verbose=0)
    print(f"\n[EVAL] Validation MSE={val_loss:.4f}  MAE={val_mae:.4f}")

    # Plot training curves
    fig, axes = plt.subplots(1, 2, figsize=(12, 4))
    axes[0].plot(history.history['loss'],     label='train')
    axes[0].plot(history.history['val_loss'], label='val')
    axes[0].set_title('Loss (MSE)'); axes[0].legend()
    axes[1].plot(history.history['mae'],      label='train')
    axes[1].plot(history.history['val_mae'],  label='val')
    axes[1].set_title('MAE'); axes[1].legend()
    plt.tight_layout()
    plt.savefig(OUTPUT_DIR / 'training_curves.png', dpi=150)
    print(f"[PLOT] Saved training curves to {OUTPUT_DIR / 'training_curves.png'}")

    return model, scaler


# ═══════════════════════════════════════════════════════════════════════════════
# 3. Convert to TFLite and export C header
# ═══════════════════════════════════════════════════════════════════════════════

def convert_to_tflite(model: tf.keras.Model) -> bytes:
    converter = tf.lite.TFLiteConverter.from_keras_model(model)

    # Full-integer quantisation reduces model size and speeds up inference
    # on ESP32. Requires representative dataset.
    # For now, use float32 which is more straightforward.
    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    tflite_model = converter.convert()
    print(f"[TFLITE] Model size: {len(tflite_model)} bytes")
    return tflite_model


def write_c_header(model_bytes: bytes, output_path: Path):
    header = [
        "// Auto-generated by training/train_model.py – DO NOT EDIT",
        "// To regenerate: python training/train_model.py",
        "#pragma once",
        "",
        "#ifndef MODEL_LOADED",
        "#define MODEL_LOADED 1",
        "#endif",
        "",
        f"// TFLite model size: {len(model_bytes)} bytes",
        f"alignas(8) const unsigned char model_data[] = {{",
    ]

    # Format as hex bytes, 12 per line
    hex_bytes = [f"0x{b:02x}" for b in model_bytes]
    for i in range(0, len(hex_bytes), 12):
        chunk = ', '.join(hex_bytes[i:i+12])
        header.append(f"    {chunk},")

    header += [
        "};",
        f"const unsigned int model_data_len = {len(model_bytes)};",
        "",
    ]

    output_path.write_text('\n'.join(header))
    print(f"[OUT] C header written to {output_path}")
    print(f"[OUT] Copy {output_path} to src/ml/model_data.h")
    print(f"[OUT] Ensure MODEL_LOADED is set to 1 in the header")


# ═══════════════════════════════════════════════════════════════════════════════
# Main
# ═══════════════════════════════════════════════════════════════════════════════

if __name__ == '__main__':
    print("=== Air Pollution Detector – TFLite Model Training ===\n")
    df      = load_data()
    model, scaler = train(df)
    tflite  = convert_to_tflite(model)
    write_c_header(tflite, OUTPUT_DIR / 'model_data.h')

    # Also save the raw .tflite file for inspection
    (OUTPUT_DIR / 'air_quality_model.tflite').write_bytes(tflite)
    print(f"\n[DONE] Model saved to {OUTPUT_DIR}/air_quality_model.tflite")
    print("[DONE] Next steps:")
    print("  1. Copy output/model_data.h  →  src/ml/model_data.h")
    print("  2. Re-flash firmware")
    print("  3. Verify Serial output shows 'TFLite ML' inference mode")
