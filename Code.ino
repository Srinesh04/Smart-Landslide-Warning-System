#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>

//WIFI CONFIG
const char* ssid = "Spidey";
const char* password = "vic******";
const char* sheetURL = "https://script.google.com/macros/s/AKfycbw6tnlKWAMRDcMZbWEprkILdAMctBdAhmrpHWy6S6aLppKYCrhiZylC0UENz3VcWGUh/exec";

//TELEGRAM
String botToken = "8275160557:AAHIPzNGsu-MZlzORqpBTjhTmUTjzvunlRY";
String chatId   = "1539376959";

void sendTelegram(String message) {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = "https://api.telegram.org/bot" + botToken + "/sendMessage?chat_id=" + chatId + "&text=" + message;
    http.begin(url);
    http.GET();
    http.end();
  }
}

//VIBRATION
#define VIB_PIN 25
volatile unsigned long vibrationCount = 0;
volatile unsigned long lastInterruptTime = 0;

void IRAM_ATTR handleVibration() {
  unsigned long t = millis();
  if (t - lastInterruptTime > 300) {
    vibrationCount++;
    lastInterruptTime = t;
  }
}

//MPU6050
const uint8_t MPU_ADDR = 0x68;
const float ACCEL_SCALE = 16384.0;
const float GYRO_SCALE  = 131.0;

float accelBiasX=0, accelBiasY=0, accelBiasZ=0;
float gyroBiasX=0, gyroBiasY=0, gyroBiasZ=0;

void readRawMPU(int16_t &ax, int16_t &ay, int16_t &az,
                int16_t &gx, int16_t &gy, int16_t &gz) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x3B);
  Wire.endTransmission(false);
  Wire.requestFrom(MPU_ADDR, 14, true);
  ax = Wire.read()<<8 | Wire.read();
  ay = Wire.read()<<8 | Wire.read();
  az = Wire.read()<<8 | Wire.read();
  Wire.read(); Wire.read();
  gx = Wire.read()<<8 | Wire.read();
  gy = Wire.read()<<8 | Wire.read();
  gz = Wire.read()<<8 | Wire.read();
}

void calibrateMPU() {
  const int samples = 500;
  long sumAx=0,sumAy=0,sumAz=0,sumGx=0,sumGy=0,sumGz=0;
  for(int i=0;i<samples;i++){
    int16_t ax,ay,az,gx,gy,gz;
    readRawMPU(ax,ay,az,gx,gy,gz);
    sumAx+=ax; sumAy+=ay; sumAz+=az;
    sumGx+=gx; sumGy+=gy; sumGz+=gz;
    delay(5);
  }
  accelBiasX=sumAx/(float)samples;
  accelBiasY=sumAy/(float)samples;
  accelBiasZ=sumAz/(float)samples;
  gyroBiasX=sumGx/(float)samples;
  gyroBiasY=sumGy/(float)samples;
  gyroBiasZ=sumGz/(float)samples;
}

//SOIL
const int SOIL_A_PIN = 34;
const int DRY_CAL = 3500;
const int WET_CAL = 1200;

float rawToMoisturePercent(int raw){
  float pct = map(raw, DRY_CAL, WET_CAL, 0, 100);
  if(pct<0) pct=0; 
  if(pct>100) pct=100;
  return pct;
}

String moistureLabel(float pct){
  if(pct > 60.0) return "WET";
  else if(pct > 30.0) return "MOIST";
  else return "DRY";
}

//SETUP
void setup(){
  Serial.begin(115200);

  WiFi.begin(ssid,password);
  while(WiFi.status()!=WL_CONNECTED){ delay(500); }

  pinMode(VIB_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(VIB_PIN), handleVibration, FALLING);

  Wire.begin();
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(0x6B); Wire.write(0);
  Wire.endTransmission(true);
  delay(100);
  calibrateMPU();

  Serial.println("System Ready");
}

//LOOP
void loop(){

  int16_t rawAx,rawAy,rawAz,rawGx,rawGy,rawGz;
  readRawMPU(rawAx,rawAy,rawAz,rawGx,rawGy,rawGz);
  float ax=(rawAx-accelBiasX)/ACCEL_SCALE;
  float ay=(rawAy-accelBiasY)/ACCEL_SCALE;
  float az=(rawAz-accelBiasZ)/ACCEL_SCALE;
  float gx=(rawGx-gyroBiasX)/GYRO_SCALE;
  float gy=(rawGy-gyroBiasY)/GYRO_SCALE;
  float gz=(rawGz-gyroBiasZ)/GYRO_SCALE;

  int rawSoil = analogRead(SOIL_A_PIN);
  float soilPct = rawToMoisturePercent(rawSoil);
  String soilLab = moistureLabel(soilPct);

  bool soilAbnormal = (soilPct >30);
  bool tiltAbnormal = (fabs(ax) > 0.5 || fabs(ay) > 0.5);
  bool vibAbnormal = (vibrationCount >= 2);
  int abnormalCount = (soilAbnormal?1:0) + (tiltAbnormal?1:0) + (vibAbnormal?1:0);

  //SERIAL PRINT
  Serial.println("-------------------------------------------------");
  Serial.print("Accel(g): ");
  Serial.print(ax,2); Serial.print(", ");
  Serial.print(ay,2); Serial.print(", ");
  Serial.println(az,2);

  Serial.print("Gyro(deg/s): ");
  Serial.print(gx,2); Serial.print(", ");
  Serial.print(gy,2); Serial.print(", ");
  Serial.println(gz,2);

  Serial.print("Soil: ");
  Serial.print(soilPct,1); Serial.print("% ");
  Serial.print(soilLab);
  Serial.print(" | VibCount=");
  Serial.println(vibrationCount);

  if (abnormalCount >= 2) {
    Serial.println("Status: ⚠ LANDSLIDE RISK");
  } else {
    Serial.println("Status: Normal");
  }
  Serial.println("-------------------------------------------------\n");
  // ==================================================


  if (abnormalCount >= 2) {
    sendTelegram("⚠ LANDSLIDE ALERT!");
  }

  //Google Sheet Upload
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    String url = String(sheetURL) +
                 "?ax=" + String(ax,2) +
                 "&ay=" + String(ay,2) +
                 "&az=" + String(az,2) +
                 "&gx=" + String(gx,2) +
                 "&gy=" + String(gy,2) +
                 "&gz=" + String(gz,2) +
                 "&soilPct=" + String(soilPct,1) +
                 "&soil=" + soilLab +
                 "&vibration=" + String(vibrationCount) +
                 "&risk=" + (abnormalCount >= 2 ? "1" : "0");

    http.begin(url);
    http.GET();
    http.end();
  }

  vibrationCount = 0;
  delay(1000);
}
