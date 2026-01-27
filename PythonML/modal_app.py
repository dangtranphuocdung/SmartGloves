# modal_app.py
import modal

# Create a Modal app
app = modal.App("smart-glove-gestures")

# Define container image with dependencies
image = modal.Image.debian_slim().pip_install(
    "numpy",
    "scikit-learn",
    "tensorflow",  # or "torch" if using PyTorch
    "openai"
)