"""
Gesture Letter Recognition — Optimized for NVIDIA Ada (RTX 4000-series)
------------------------------------------------------------------------
Key improvements over baseline:
  • Data augmentation  (jitter, scale, time-warp, rotation)
  • Per-feature z-score normalisation
  • Bidirectional LSTM + Multi-Head Attention
  • Residual dense block
  • Label smoothing + class-weight balancing
  • Mixed-precision (FP16) for Ada tensor cores
  • Cosine-annealing LR schedule with warm-up
  • cuDNN-optimised LSTM kernels
  • Only saves letters actually present in the dataset
"""

import json
import numpy as np
import tensorflow as tf
from tensorflow import keras
from tensorflow.keras import layers, mixed_precision
from sklearn.model_selection import StratifiedKFold
from sklearn.preprocessing import LabelEncoder
from scipy.interpolate import interp1d
import os

# ══════════════════════════════════════════════════════════════════════════════
# 0.  GPU / mixed-precision setup
# ══════════════════════════════════════════════════════════════════════════════
mixed_precision.set_global_policy('mixed_float16')          # Ada tensor cores
gpus = tf.config.list_physical_devices('GPU')
for gpu in gpus:
    tf.config.experimental.set_memory_growth(gpu, True)
print(f"GPUs found: {[g.name for g in gpus]}")

# ══════════════════════════════════════════════════════════════════════════════
# 1.  Hyper-parameters
# ══════════════════════════════════════════════════════════════════════════════
MAX_LEN      = 150          # samples per sequence (raise if 2-s gives >100 pts)
EPOCHS       = 300
BATCH_SIZE   = 32           # tune: try 64 if VRAM allows
LSTM_UNITS   = 128
ATTN_HEADS   = 4
DENSE_UNITS  = 128
DROPOUT      = 0.35
LR_MAX       = 3e-3
LR_MIN       = 1e-5
WARMUP_EP    = 10
LABEL_SMOOTH = 0.1
N_FOLDS      = 5            # stratified k-fold cross-validation
AUG_FACTOR   = 4            # augmented copies per original sample

# ══════════════════════════════════════════════════════════════════════════════
# 2.  Data augmentation helpers
# ══════════════════════════════════════════════════════════════════════════════
def jitter(seq, sigma=0.05):
    """Add Gaussian noise scaled to per-feature std."""
    noise = np.random.randn(*seq.shape) * sigma * seq.std(axis=0)
    return seq + noise

def scale(seq, sigma=0.15):
    """Random per-feature scaling."""
    factors = 1.0 + np.random.randn(seq.shape[1]) * sigma
    return seq * factors

def time_warp(seq, sigma=0.2):
    """Smooth random time warping via cubic interpolation."""
    T = seq.shape[0]
    orig_steps = np.arange(T)
    warp_steps = (np.cumsum(np.abs(np.random.randn(T)) + 1e-3))
    warp_steps = warp_steps / warp_steps[-1] * (T - 1)
    warped = np.stack([
        interp1d(warp_steps, seq[:, c], kind='cubic', fill_value='extrapolate')(orig_steps)
        for c in range(seq.shape[1])
    ], axis=1)
    return warped

def rotate_imu(seq, max_deg=15):
    """Small random rotation in the XY plane of accel+gyro axes."""
    theta = np.deg2rad(np.random.uniform(-max_deg, max_deg))
    c, s = np.cos(theta), np.sin(theta)
    R = np.array([[c, -s], [s, c]])
    out = seq.copy()
    out[:, :2]  = seq[:, :2]  @ R.T   # accel XY
    out[:, 3:5] = seq[:, 3:5] @ R.T   # gyro  XY
    return out

def augment(seq):
    """Return AUG_FACTOR augmented variants of one sequence."""
    fns = [jitter, scale, time_warp, rotate_imu]
    variants = []
    for _ in range(AUG_FACTOR):
        s = seq.copy()
        for fn in np.random.choice(fns, size=np.random.randint(1, 4), replace=False):
            s = fn(s)
        variants.append(s)
    return variants

# ══════════════════════════════════════════════════════════════════════════════
# 3.  Load & preprocess
# ══════════════════════════════════════════════════════════════════════════════
with open('gesture_training_data.json', 'r') as f:
    data = json.load(f)

raw_X, raw_y = [], []
for sample in data:
    seq = np.array(sample['sensor_data'], dtype=np.float32)
    # Pad / truncate
    if len(seq) < MAX_LEN:
        pad = np.zeros((MAX_LEN - len(seq), 6), dtype=np.float32)
        seq = np.vstack([seq, pad])
    else:
        seq = seq[:MAX_LEN]
    raw_X.append(seq)
    raw_y.append(sample['letter'])

raw_X = np.array(raw_X, dtype=np.float32)   # (N, MAX_LEN, 6)
raw_y = np.array(raw_y)

# ── Normalise: z-score per feature across the whole dataset ──────────────────
mean = raw_X.reshape(-1, 6).mean(axis=0)
std  = raw_X.reshape(-1, 6).std(axis=0) + 1e-8
raw_X = (raw_X - mean) / std
np.save('feature_norm.npy', np.stack([mean, std]))   # needed at inference time

# ── Encode labels (only letters present in data) ─────────────────────────────
le = LabelEncoder()
y_int = le.fit_transform(raw_y)
NUM_CLASSES = len(le.classes_)
print(f"Letters in dataset: {le.classes_}  ({NUM_CLASSES} classes)")

import pickle
with open('label_encoder.pkl', 'wb') as f:
    pickle.dump(le, f)

# ── Augment training data ─────────────────────────────────────────────────────
aug_X, aug_y = list(raw_X), list(y_int)
for seq, label in zip(raw_X, y_int):
    for v in augment(seq):
        aug_X.append(v.astype(np.float32))
        aug_y.append(label)

aug_X = np.array(aug_X, dtype=np.float32)
aug_y = np.array(aug_y)

# Class weights to handle imbalance
from sklearn.utils.class_weight import compute_class_weight
class_weights = compute_class_weight('balanced', classes=np.unique(aug_y), y=aug_y)
class_weight_dict = dict(enumerate(class_weights))

# One-hot with label smoothing baked in at compile time (handled by loss below)
aug_y_cat = keras.utils.to_categorical(aug_y, num_classes=NUM_CLASSES)

print(f"Total samples after augmentation: {len(aug_X)}")

# ══════════════════════════════════════════════════════════════════════════════
# 4.  Model
# ══════════════════════════════════════════════════════════════════════════════
def build_model(num_classes):
    inp = keras.Input(shape=(MAX_LEN, 6))

    # ── Bi-LSTM stack ──────────────────────────────────────────────────────
    x = layers.Bidirectional(
        layers.LSTM(LSTM_UNITS, return_sequences=True,
                    dropout=0.1, recurrent_dropout=0.0)   # recurrent_dropout=0 → cuDNN kernel
    )(inp)
    x = layers.Bidirectional(
        layers.LSTM(LSTM_UNITS // 2, return_sequences=True,
                    dropout=0.1, recurrent_dropout=0.0)
    )(x)

    # ── Multi-Head Self-Attention ─────────────────────────────────────────
    attn = layers.MultiHeadAttention(
        num_heads=ATTN_HEADS, key_dim=LSTM_UNITS // ATTN_HEADS
    )(x, x)
    attn = layers.LayerNormalization()(attn + x)   # residual

    # ── Global pooling: concat avg + max for richer representation ────────
    avg = layers.GlobalAveragePooling1D()(attn)
    mx  = layers.GlobalMaxPooling1D()(attn)
    x   = layers.Concatenate()([avg, mx])

    # ── Residual dense block ──────────────────────────────────────────────
    skip = layers.Dense(DENSE_UNITS)(x)
    x    = layers.Dense(DENSE_UNITS, activation='swish')(x)
    x    = layers.BatchNormalization()(x)
    x    = layers.Dropout(DROPOUT)(x)
    x    = layers.Dense(DENSE_UNITS, activation='swish')(x)
    x    = layers.BatchNormalization()(x)
    x    = layers.Add()([x, skip])
    x    = layers.Dropout(DROPOUT)(x)

    # ── Output (float32 for numerical stability with mixed precision) ─────
    out = layers.Dense(num_classes, activation='softmax', dtype='float32')(x)

    return keras.Model(inp, out)

# ══════════════════════════════════════════════════════════════════════════════
# 5.  LR schedule: linear warm-up → cosine annealing
# ══════════════════════════════════════════════════════════════════════════════
def lr_schedule(epoch):
    if epoch < WARMUP_EP:
        return LR_MIN + (LR_MAX - LR_MIN) * (epoch / WARMUP_EP)
    progress = (epoch - WARMUP_EP) / max(EPOCHS - WARMUP_EP, 1)
    return LR_MIN + 0.5 * (LR_MAX - LR_MIN) * (1 + np.cos(np.pi * progress))

# ══════════════════════════════════════════════════════════════════════════════
# 6.  Cross-validated training
# ══════════════════════════════════════════════════════════════════════════════
skf      = StratifiedKFold(n_splits=N_FOLDS, shuffle=True, random_state=42)
fold_acc = []

os.makedirs('checkpoints', exist_ok=True)

for fold, (train_idx, val_idx) in enumerate(skf.split(aug_X, aug_y)):
    print(f"\n{'═'*55}")
    print(f"  FOLD {fold+1} / {N_FOLDS}")
    print(f"{'═'*55}")

    X_tr, X_val = aug_X[train_idx], aug_X[val_idx]
    y_tr, y_val = aug_y_cat[train_idx], aug_y_cat[val_idx]

    model = build_model(NUM_CLASSES)
    model.compile(
        optimizer=keras.optimizers.Adam(learning_rate=LR_MAX),
        loss=keras.losses.CategoricalCrossentropy(label_smoothing=LABEL_SMOOTH),
        metrics=['accuracy']
    )

    if fold == 0:
        model.summary()

    callbacks = [
        keras.callbacks.LearningRateScheduler(lr_schedule, verbose=0),
        keras.callbacks.EarlyStopping(
            monitor='val_accuracy', patience=30,
            restore_best_weights=True, verbose=1
        ),
        keras.callbacks.ModelCheckpoint(
            f'checkpoints/fold_{fold+1}_best.keras',
            monitor='val_accuracy', save_best_only=True, verbose=0
        ),
        keras.callbacks.TensorBoard(log_dir=f'logs/fold_{fold+1}')
    ]

    history = model.fit(
        X_tr, y_tr,
        validation_data=(X_val, y_val),
        epochs=EPOCHS,
        batch_size=BATCH_SIZE,
        class_weight=class_weight_dict,
        callbacks=callbacks,
        verbose=1
    )

    best_val = max(history.history['val_accuracy'])
    fold_acc.append(best_val)
    print(f"  ✅ Fold {fold+1} best val accuracy: {best_val:.4f}")

print(f"\n{'═'*55}")
print(f"  Cross-validation results")
print(f"{'═'*55}")
for i, acc in enumerate(fold_acc):
    print(f"  Fold {i+1}: {acc:.4f}")
print(f"  Mean : {np.mean(fold_acc):.4f}  ±  {np.std(fold_acc):.4f}")

# ══════════════════════════════════════════════════════════════════════════════
# 7.  Final model: retrain on ALL data with best fold's architecture
# ══════════════════════════════════════════════════════════════════════════════
print(f"\n{'═'*55}")
print("  Retraining final model on full dataset…")
print(f"{'═'*55}")

best_fold = int(np.argmax(fold_acc)) + 1
print(f"  Using fold {best_fold} as reference (val_acc={fold_acc[best_fold-1]:.4f})")

final_model = build_model(NUM_CLASSES)
final_model.compile(
    optimizer=keras.optimizers.Adam(learning_rate=LR_MAX),
    loss=keras.losses.CategoricalCrossentropy(label_smoothing=LABEL_SMOOTH),
    metrics=['accuracy']
)

final_callbacks = [
    keras.callbacks.LearningRateScheduler(lr_schedule, verbose=0),
    keras.callbacks.EarlyStopping(
        monitor='accuracy', patience=40,
        restore_best_weights=True, verbose=1
    ),
    keras.callbacks.ModelCheckpoint(
        'letter_recognition_model.keras',
        monitor='accuracy', save_best_only=True, verbose=1
    ),
]

final_model.fit(
    aug_X, aug_y_cat,
    epochs=EPOCHS,
    batch_size=BATCH_SIZE,
    class_weight=class_weight_dict,
    callbacks=final_callbacks,
    verbose=1
)

# Also export as TFLite for edge deployment (optional)
converter = tf.lite.TFLiteConverter.from_keras_model(final_model)
converter.optimizations = [tf.lite.Optimize.DEFAULT]
tflite_model = converter.convert()
with open('letter_recognition_model.tflite', 'wb') as f:
    f.write(tflite_model)

print("\n✅ Saved:")
print("   letter_recognition_model.keras  — full Keras model")
print("   letter_recognition_model.tflite — quantized TFLite")
print("   feature_norm.npy                — normalisation params")
print("   label_encoder.pkl               — label ↔ letter mapping")
print("   checkpoints/                    — per-fold best weights")
print("   logs/                           — TensorBoard logs")
print("\nTo view training curves:")
print("   tensorboard --logdir logs/")