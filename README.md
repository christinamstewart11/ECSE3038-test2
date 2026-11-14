# ECSE3038 Test 2: The bathroom fan

An ESP32 (Wokwi) with a DHT22 humidity sensor and a fan LED. It POSTs humidity readings to a FastAPI + MongoDB Atlas API. The API pushes fan settings to the board over MQTT (class broker).

## Layout
- `api/`: FastAPI app (`app.py`), `requirements.txt`
- `board/`: PlatformIO project for the ESP32 (`src/main.cpp`, `diagram.json`, `wokwi.toml`)

## Run the API
1. `cd api`
2. `python -m venv venv`, activate it, then `python -m pip install -r requirements.txt`
3. Create `api/.env` (never committed) with `MONGODB_URI=<your Atlas connection string>` and `MQTT_NAME=<your name>`
4. `uvicorn app:app --host 127.0.0.1 --port 8000` (open `/docs` for the routes)

## Run the board
1. Open the `board` folder in VS Code with PlatformIO. Create `board/include/secrets.h` (never committed) containing `#define DEVICE_KEY ""`.
2. Build, then F1, Wokwi: Start Simulator.
3. Copy the MAC address from the Serial Monitor. The first POST returns 401, which is expected.
4. `POST /fans` with that MAC. Copy the returned key between the quotes in `secrets.h`.
5. Build and start Wokwi again. `PUT /fans/{mac}/settings` sets the fan's mode and humidity limit.