import os
import datetime
from fastapi import FastAPI, Request
from fastapi.middleware.cors import CORSMiddleware
from fastapi.templating import Jinja2Templates
from pydantic import BaseModel

app = FastAPI()

# Configuração de CORS para permitir acesso do Dashboard
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)

BASE_DIR = os.path.dirname(os.path.abspath(__file__))
templates = Jinja2Templates(directory=os.path.join(BASE_DIR, "templates"))

print("\n" + "="*50)
print("SERVIDOR IOT INICIADO")
print(f"Endereço para o Dashboard: http://localhost:5000")
print("="*50 + "\n")

# Banco de dados em memória para os últimos 100 eventos
history = []

class SensorData(BaseModel):
    temperature: float
    flame: int       # 0 = Fogo, 1 = Normal
    device_id: str
    humidity: float = 0.0

@app.get("/")
async def read_dashboard(request: Request):
    return templates.TemplateResponse(
        "index.html", {"request": request, "history": history}
    )

@app.get("/last_event")
async def get_last_event():
    if not history:
        return {
            "timestamp": datetime.datetime.now().strftime("%H:%M:%S"),
            "temperature": 0.0,
            "flame": 1,
            "flame_text": "Normal",
            "status": "Aguardando Dados",
            "probability": 0.0
        }
    return history[-1]

@app.get("/update")
async def update_data(request: Request, temp: float, flame: int, device: str = "pico_w"):
    # Log visível no terminal para debug
    client_ip = request.client.host
    print(f"\n>>>> RECEBIDO: {temp}C, Flama: {flame} de {client_ip}")

    # Lógica de negócio e status
    flame_detected = (flame == 0)
    status = "ALERTA DE INCÊNDIO" if flame_detected else "Sistema Normal"
    flame_text = "Fogo detectado" if flame_detected else "Normal"
    
    # Simulação de probabilidade baseada na temperatura se não houver chama direta
    # Para fins de demonstração na interface
    prob = 1.0 if flame_detected else (max(0, min(1, (temp - 30) / 20)) if temp > 30 else 0.05)

    log_entry = {
        "timestamp": datetime.datetime.now().strftime("%H:%M:%S"),
        "temperature": temp,
        "flame": flame,
        "flame_text": flame_text,
        "status": status,
        "probability": prob,
        "device_id": device
    }
    
    history.append(log_entry)
    if len(history) > 100:
        history.pop(0)
        
    print(f"[{log_entry['timestamp']}] Dados recebidos: {temp}C, Flama: {flame_text}")
    return log_entry

if __name__ == "__main__":
    import uvicorn
    # Rodando na porta 5000 como configurado na Pico
    uvicorn.run(app, host="0.0.0.0", port=5000)
