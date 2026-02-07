import pandas as pd
import numpy as np
from sklearn.model_selection import train_test_split
from sklearn.tree import DecisionTreeClassifier
from sklearn.metrics import accuracy_score, classification_report
import joblib
import os

def train_model(csv_path='smoke_detection_iot.csv'):
    if not os.path.exists(csv_path):
        print(f"Dataset {csv_path} not found. Generating dummy data for demonstration...")
        # Create dummy data based on typical fire alarm patterns
        # Temperature, Humidity, TVOC, eCO2, Raw H2, Raw Ethanol, Pressure, PM1.0, PM2.5, NC0.5, NC1.0, NC2.5, Fire Alarm
        data_size = 1000
        np.random.seed(42)
        
        temp = np.random.uniform(20, 60, data_size)
        humidity = np.random.uniform(20, 90, data_size)
        tvoc = np.random.uniform(0, 20000, data_size)
        eco2 = np.random.uniform(400, 5000, data_size)
        fire_alarm = ((temp > 40) | (tvoc > 1000) | (humidity < 30)).astype(int)
        
        df = pd.DataFrame({
            'Temperature[C]': temp,
            'Humidity[%]': humidity,
            'TVOC[ppb]': tvoc,
            'eCO2[ppm]': eco2,
            'Fire Alarm': fire_alarm
        })
        df.to_csv(csv_path, index=False)
    else:
        df = pd.read_csv(csv_path)

    # Features selection
    # Adjust based on what the Pico actually sends. 
    # For now, we'll use Temperature and Humidity as they are the primary sensor outputs mentioned.
    features = ['Temperature[C]', 'Humidity[%]'] 
    X = df[features]
    y = df['Fire Alarm']

    X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=0.2, random_state=42)

    model = DecisionTreeClassifier(max_depth=5)
    model.fit(X_train, y_train)

    y_pred = model.predict(X_test)
    print(f"Accuracy: {accuracy_score(y_test, y_pred)}")
    print(classification_report(y_test, y_pred))

    model_dir = 'ai'
    if not os.path.exists(model_dir):
        os.makedirs(model_dir)
        
    joblib.dump(model, os.path.join(model_dir, 'fire_model.pkl'))
    print("Model saved as ai/fire_model.pkl")

if __name__ == "__main__":
    train_model()
