"""
Modal Inference — Smart Glove Gesture Recognition
--------------------------------------------------
Matches the optimized training pipeline:
  • Loads .keras model (not legacy .h5)
  • Applies z-score normalisation from feature_norm.npy
  • Uses LabelEncoder for correct letter mapping
  • Model is loaded ONCE per container (not on every call)
  • Returns top-3 predictions with confidence scores
  • Input length matches MAX_LEN=150 used during training
"""

import modal
import numpy as np

app = modal.App("smart-glove-gestures")

MAX_LEN = 150   # must match training script

# ── Bundle all inference artefacts into the image ─────────────────────────────
image = (
    modal.Image.debian_slim()
    .pip_install("numpy", "tensorflow", "scikit-learn")
    .add_local_file(
        local_path="letter_recognition_model.keras",
        remote_path="/root/letter_recognition_model.keras"
    )
    .add_local_file(
        local_path="feature_norm.npy",
        remote_path="/root/feature_norm.npy"
    )
    .add_local_file(
        local_path="label_encoder.pkl",
        remote_path="/root/label_encoder.pkl"
    )
)


# ══════════════════════════════════════════════════════════════════════════════
# Remote function
# keep_warm=1  → one container stays alive so the model isn't reloaded every call
# ══════════════════════════════════════════════════════════════════════════════
@app.function(
    image=image,
    gpu="T4",
    keep_warm=1,                 # avoids cold-start model reload on repeated calls
    timeout=60,
)
def predict_letter(sensor_sequence: list) -> dict:
    import pickle
    import numpy as np
    import tensorflow as tf
    from tensorflow import keras

    # ── Load artefacts (cached in container memory across calls) ──────────
    # Using a module-level cache so repeated calls skip disk I/O
    global _model, _mean, _std, _le
    try:
        _model
    except NameError:
        print("🔄 Cold start — loading model and artefacts…")
        _model = keras.models.load_model('/root/letter_recognition_model.keras')
        norm   = np.load('/root/feature_norm.npy')   # shape (2, 6): [mean, std]
        _mean, _std = norm[0], norm[1]
        with open('/root/label_encoder.pkl', 'rb') as f:
            _le = pickle.load(f)
        print("✅ Model loaded.")

    # ── Preprocess ────────────────────────────────────────────────────────
    seq = np.array(sensor_sequence, dtype=np.float32)   # (T, 6)

    # Validate shape
    if seq.ndim != 2 or seq.shape[1] != 6:
        raise ValueError(f"Expected shape (T, 6), got {seq.shape}")

    # Pad or truncate to MAX_LEN
    if len(seq) < MAX_LEN:
        pad = np.zeros((MAX_LEN - len(seq), 6), dtype=np.float32)
        seq = np.vstack([seq, pad])
    else:
        seq = seq[:MAX_LEN]

    # Apply the SAME z-score normalisation used during training
    seq = (seq - _mean) / _std

    seq = seq.reshape(1, MAX_LEN, 6)   # batch dim

    # ── Predict ───────────────────────────────────────────────────────────
    probs = _model.predict(seq, verbose=0)[0]           # (NUM_CLASSES,)

    # Top-3 predictions
    top3_idx  = np.argsort(probs)[::-1][:3]
    top3      = [
        {
            'letter':     _le.inverse_transform([i])[0],
            'confidence': round(float(probs[i]), 4)
        }
        for i in top3_idx
    ]

    return {
        'prediction': top3[0]['letter'],
        'confidence': top3[0]['confidence'],
        'top3':       top3
    }


# ══════════════════════════════════════════════════════════════════════════════
# Local entry-point — test with a dummy sequence
# ══════════════════════════════════════════════════════════════════════════════
@app.local_entrypoint()
def main():
    import json

    print("Sending test sequence to cloud…")

    # Realistic dummy: small random values similar to normalised IMU data
    dummy_data = (np.random.randn(MAX_LEN, 6) * 0.5).tolist()

    result = predict_letter.remote(dummy_data)

    print("\n── Inference result ──────────────────────────")
    print(f"  Best guess : {result['prediction']}  ({result['confidence']*100:.1f}%)")
    print("  Top 3:")
    for r in result['top3']:
        bar = '█' * int(r['confidence'] * 20)
        print(f"    {r['letter']}  {bar:<20}  {r['confidence']*100:.1f}%")
    print("──────────────────────────────────────────────")