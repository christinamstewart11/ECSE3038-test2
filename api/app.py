import json
import os
import secrets
from datetime import datetime, timezone
from typing import Literal

import paho.mqtt.publish as publish
from dotenv import load_dotenv
from fastapi import FastAPI, Header, HTTPException, Query, Response
from pydantic import BaseModel, Field
from pymongo import ASCENDING, MongoClient
from pymongo.errors import DuplicateKeyError

load_dotenv()

MQTT_BROKER = "135.232.203.144"
MQTT_NAME = os.getenv("MQTT_NAME")

client = MongoClient(os.getenv("MONGODB_URI"), tz_aware=True)
db = client["test2"]
fans = db["fans"]
readings = db["readings"]
fan_settings = db["settings"]

fans.create_index([("mac", ASCENDING)], unique=True)

app = FastAPI()


class FanIn(BaseModel):
    mac: str
    room: str


class ReadingIn(BaseModel):
    mac: str
    humidity: float = Field(ge=0, le=100)


class SettingsIn(BaseModel):
    mode: Literal["auto", "on", "off"]
    max_humidity: float = Field(ge=0, le=100)


# ---- A1: fans ----
@app.post("/fans", status_code=201)
def create_fan(fan: FanIn):
    doc = {"mac": fan.mac, "room": fan.room, "key": secrets.token_urlsafe(16)}
    try:
        fans.insert_one(doc)
    except DuplicateKeyError:
        raise HTTPException(status_code=409, detail="A fan with MAC " + fan.mac + " already exists")
    doc.pop("_id")
    return doc


@app.get("/fans")
def list_fans():
    return list(fans.find({}, {"_id": 0, "key": 0}))


@app.get("/fans/{mac}")
def get_fan(mac: str):
    fan = fans.find_one({"mac": mac}, {"_id": 0, "key": 0})
    if fan is None:
        raise HTTPException(status_code=404, detail="No fan with MAC " + mac)
    return fan


# ---- A2: readings in ----
@app.post("/readings", status_code=201)
def create_reading(reading: ReadingIn, x_api_key: str | None = Header(default=None)):
    if x_api_key is None:
        raise HTTPException(status_code=401, detail="Missing X-API-Key")
    owner = fans.find_one({"key": x_api_key})
    if owner is None:
        raise HTTPException(status_code=401, detail="Unknown API key")
    if owner["mac"] != reading.mac:
        raise HTTPException(status_code=403, detail="That key belongs to another fan")
    doc = reading.model_dump()
    doc["time"] = datetime.now(timezone.utc)
    readings.insert_one(doc)
    doc.pop("_id")
    return doc


# ---- A3: readings out ----
@app.get("/readings")
def get_readings(mac: str | None = None, limit: int = Query(default=10, ge=1, le=100)):
    query = {}
    if mac is not None:
        query["mac"] = mac
    return list(readings.find(query, {"_id": 0}).sort("time", -1).limit(limit))


# ---- A4: settings ----
@app.put("/fans/{mac}/settings")
def put_settings(mac: str, new: SettingsIn, response: Response):
    if fans.find_one({"mac": mac}) is None:
        raise HTTPException(status_code=404, detail="No fan with MAC " + mac)
    doc = new.model_dump()
    doc["mac"] = mac
    result = fan_settings.replace_one({"mac": mac}, doc, upsert=True)
    if result.upserted_id is not None:
        response.status_code = 201
    try:
        message = json.dumps({"mode": new.mode, "max_humidity": new.max_humidity})
        publish.single("ecse3038/" + MQTT_NAME + "/fan/settings", message,
                       retain=True, hostname=MQTT_BROKER)
    except Exception as error:
        print("Could not publish the settings:", error)
    doc.pop("_id", None)
    return doc


# ---- A5: delete ----
@app.delete("/fans/{mac}", status_code=204)
def delete_fan(mac: str, x_api_key: str | None = Header(default=None)):
    if fans.find_one({"mac": mac}) is None:
        raise HTTPException(status_code=404, detail="No fan with MAC " + mac)
    if x_api_key is None:
        raise HTTPException(status_code=401, detail="Missing X-API-Key")
    owner = fans.find_one({"key": x_api_key})
    if owner is None:
        raise HTTPException(status_code=401, detail="Unknown API key")
    if owner["mac"] != mac:
        raise HTTPException(status_code=403, detail="That key belongs to another fan")
    fans.delete_one({"mac": mac})
    fan_settings.delete_one({"mac": mac})
    return Response(status_code=204)