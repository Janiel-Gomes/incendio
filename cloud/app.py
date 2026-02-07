import os
import datetime
import joblib
import pandas as pd
import numpy as np
import asyncio
import subprocess
import random
from fastapi import FastAPI, Request, HTTPException
from fastapi.middleware.cors import CORSMiddleware
from fastapi.templating import Jinja2Templates
from pydantic import BaseModel

app = FastAPI()

# Add CORS so the dashboard can fetch data
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

# Base path for the script
BASE_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(BASE_DIR)

# Load the model
MODEL_PATH = os.path.join(ROOT_DIR, "ai", "fire_model.pkl")
model = None
if os.path.exists(MODEL_PATH):
    model = joblib.load(MODEL_PATH)

# In-memory database for historical data
history = []

class SensorData(BaseModel):
    temperature: float
    humidity: float = 0.0
    flame: int       # 0 = Fogo, 1 = Normal
    device_id: str

templates = Jinja2Templates(directory=os.path.join(BASE_DIR, "templates"))

@app.get("/")
async def read_dashboard(request: Request):
    return templates.TemplateResponse(
        "dashboard.html", {"request": request, "history": history}
    )

@app.get("/last_event")
async def get_last_event():
    if not history:
        return {
            "timestamp": datetime.datetime.now().strftime("%H:%M:%S"),
            "temperature": 0.0,
            "humidity": 0.0,
            "flame": 0,
            "probability": 0.0,
            "status": "Aguardando Dados",
            "device_id": "none",
            "version": "4.0-production"
        }
    
    return history[-1]

@app.get("/update")
async def update_data(temp: float, flame: int, device: str = "pico_w", humidity: float = 0.0):
    # Reutiliza o modelo de dados para validação e lógica
    data = SensorData(temperature=temp, flame=flame, device_id=device, humidity=humidity)
    return await predict(data)

@app.post("/predict")
async def predict(data: SensorData):
    global model
    if model is None:
        if os.path.exists(MODEL_PATH):
            model = joblib.load(MODEL_PATH)
        else:
            return {"error": "Model not trained"}
    
    features = pd.DataFrame([[data.temperature, data.humidity]], columns=['Temperature[C]', 'Humidity[%]'])
    
    # Try to get probability if the model supports it
    try:
        prob = float(model.predict_proba(features)[0][1])
    except:
        # Fallback if predict_proba is not available
        prediction = int(model.predict(features)[0])
        prob = 1.0 if prediction == 1 else 0.0

    # Prioridade absoluta para o sensor de chama
    # Invertemos a lógica: 0 no sensor = detecção (lógica física)
    is_flame_detected = (data.flame == 0)
    
    if is_flame_detected:
        prob = 1.0
        status = "INCÊNDIO CONFIRMADO"
        flame_status_text = "Fogo detectado"
    else:
        # Se não há chama direta, usamos a probabilidade da IA baseada na temperatura
        status = "Incêndio" if prob > 0.7 else "Sistema Normal"
        flame_status_text = "Normal"
    
    log_entry = {
        "timestamp": datetime.datetime.now().strftime("%H:%M:%S"),
        "temperature": data.temperature,
        "humidity": data.humidity,
        "flame": data.flame,
        "flame_text": flame_status_text,
        "probability": prob,
        "status": status,
        "device_id": data.device_id
    }
    history.append(log_entry)
    
    if len(history) > 100:
        history.pop(0)
        
    return log_entry

if __name__ == "__main__":
    import uvicorn
    # Changed to port 5000 as expected by the dashboard JS
    uvicorn.run(app, host="0.0.0.0", port=5000)
