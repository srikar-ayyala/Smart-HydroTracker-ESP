// including required libraries
// #include <Arduino.h>
#include <LiquidCrystal.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <WiFi.h>

// all data

// Water level storing data
const int readRate = 60;      // readings per minute
const int modeCount = 6;      // readings to take mode before storing the value (in seconds or minuts)
const int avgCount = 5;      // reading to take average value of (be in minutes)
const int totalReadings = (24 * 60 * readRate) / (modeCount * avgCount); // total readings in a day
int lastReading = -1000;
unsigned long nextBreakTime = 0;
int modeWaterReadings[modeCount];
int modeWaterReadingPtr = 0;
int avgWaterReadings[avgCount][2];
int avgWaterReadingPtr = -1; // points to data that is currently here, -1 shows that it doesn't have data // -1 means go to 0 next its not filled
int waterReadings[totalReadings][2]; // 2-D array where for each reading we store the time of reading and the value
int waterReadingPtr = 0;
int waterReadingPtr2 = -1;
int waterDisplayOffset = 0;

const int firebaseSendingDelay = 20000;
int sendDataprevMillis = 0;
FirebaseJson json;
FirebaseJson dimensionJson;
FirebaseJson waterReadingsJson;
bool sendDimensionData = false;

// Water Level Reading data
const int trigPin = 15;
const int echoPin = 13;
int duration, cm;
const int tempPin = 2;
float temperature;
float velocity;
int currReading = 0;
const int readingDelay = (60 * 1000) / readRate;
int prevReadingMillis = -readingDelay;

// Device data
const String DeviceID = "0001";

// firebase data
#define API_KEY "AIzaSyCfKCjyntm0ELFXZmPrzZuZuE7GXeb7eWA"
#define DATABASE_URL "https://test-82eb0-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define USER_EMAIL "hydrometer.0001@iot.com"
#define USER_PASSWORD "hydrometer.0001@iot"
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

bool signUpOk = false;

unsigned long timeStamp;

String waterLevelPath = "waterLevel";
// String waterLevelPath = "/waterLevel";
String timePath = "timeStamp";
// String timePath = "/timeStamp";
String volumePath = "/volume";
String heightPath = "/height";
String metaDataPath = "/meterData/" + DeviceID + "/metaData";
String readingsPath = "/meterData/" + DeviceID + "/readings";

// lcd data
const int lcdRows = 4, lcdCols = 20;
const int rsPin = 4, enPin = 16, d4Pin = 17, d5Pin = 5, d6Pin = 18, d7Pin = 19;
LiquidCrystal lcd(rsPin, enPin, d4Pin, d5Pin, d6Pin, d7Pin);

// time getting data
const char *ntpServer = "in.pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 19800;

// Input data
const int noOfInputPins = 4;
const int inputPin[noOfInputPins] = {33, 25, 27, 26}; // up down left right
bool inputFlags[noOfInputPins] = {LOW, LOW, LOW, LOW};
bool hasInputChanged = false;

// Screen data
int currScreen = 0;
int currRow = 0, currCol = 0;
const String menuScreen[lcdRows] = {"Wifi Settings", "Water Level", "Enter Dimensions", "Device ID"};

// Wifi data
const int wifiCheckDelay = 60000;
int prevScanMillis = -wifiCheckDelay;
int noOfWifiAvailable = 0;
String setWifiSSID = "", setWifiPassword = "vnax4735";

int wifiDisplayOffset = 0;

// password setting data
char passChoice[4] = {'A', 'a', '0', 0};
const String splCharStr = "!@#$%^&*()-_=+[]{};:\"',<.>/?";

// dimension information
const int noOfDimension = 2;
int dimension[noOfDimension] = {1000, 100};
char volumeEnter[5] = {'0', '1', '0', '0', '0'};
char heightEnter[5] = {'0', '0', '1', '0', '0'};

// defining functions at the top so that all the function below can use these two
void ShowCurrScreen();
void ChangeScreen(int newScreen);
void SetInputs();

// time functions

time_t now;
struct tm timeinfo;
bool isTimeConfig = false;

String ReadTime(int epochTime)
{
  // i saved time in UTC but i would like to show it in IST
  time_t t = epochTime + 19800;
  String newTime = asctime(gmtime(&t));
  return newTime.substring(11, 19);
}

unsigned long getTime()
{
  if (!getLocalTime(&timeinfo))
  {
    isTimeConfig = false;
    return (0);
  }
  time(&now);
  // returns seconds since January 1, 1970
  return now;
}

void SetNextBreakTime()
{
  // nextBreakTime = (timeStamp - timeStamp % (15*60)) + 15*60;
  int gap = (60 * modeCount * avgCount) / readRate;
  nextBreakTime = (timeStamp - timeStamp % gap) + gap;
}

void ConfigTime()
{
  if (WiFi.status() != WL_CONNECTED)
    return;
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  unsigned long tx = getTime();
  int count = 5;
  while (tx == 0 && count > 0)
  {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    tx = getTime();
    count--;
    delay(100);
  }
  if (count == 0)
  {
    isTimeConfig = false;
  }
  isTimeConfig = true;
  timeStamp = tx;
  SetNextBreakTime();
}

// Wifi functions

void ConnectToWifi()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(setWifiSSID);
  lcd.setCursor(1, 1);
  lcd.print(setWifiPassword);
  Serial.print(setWifiSSID);
  Serial.print(" ");
  Serial.println(setWifiPassword);
  while(1) {
    SetInputs();
    if(inputFlags[2]) {
      WiFi.begin(setWifiSSID.c_str(), setWifiPassword.c_str());
      ChangeScreen(6);
      return;
    }
    delay(500);
  }
  // ChangeScreen(6);
}

// Firebase functions

void pushWaterDataToFirebase()
{
  if(Firebase.ready() && signUpOk) {
    int count = 0;
    FirebaseJson temp;
    String a = readingsPath;
    // String a = readingsPath + "/abc";
    String b = "12";
    temp.set(b.c_str(), 52);
    b = "13";
    temp.set(b.c_str(), 55);
    // Firebase.RTDB.setJSON(&fbdo, a.c_str(), &temp);
    // Firebase.RTDB.updateNode(&fbdo, a.c_str(), &temp);
    bool isSent = false;
    while(count < 2) {
      // isSent = Firebase.RTDB.updateNode(&fbdo, a.c_str(), &temp);
      isSent = Firebase.RTDB.updateNode(&fbdo, a.c_str(), &waterReadingsJson);
      if(isSent) break;
      delay(500);
      count++;
    }
    if(isSent) waterReadingsJson.clear();
    // if(isSent) a.clear();
    count = 0;
    isSent = false;
    
    if(sendDimensionData) {
      delay(500);

      while(count < 2) {
        isSent = Firebase.RTDB.setJSON(&fbdo, metaDataPath.c_str(), &dimensionJson);
        if(isSent) break;
        delay(500);
        count++;
      }
      // while(!Firebase.RTDB.setJSON(&fbdo, metaDataPath.c_str(), &dimensionJson)) delay(1000);
      // Firebase.RTDB.setJSON(&fbdo, metaDataPath.c_str(), &dimensionJson);
      if(isSent) {
        sendDimensionData = false;
        dimensionJson.clear();
      }
    }
  }
}

void PushDataToFirebaseArray(int waterLevel)
{
  if(waterLevel <= dimension[1]) {
  // if(abs(lastReading - GetCurrVolume(waterLevel)) >= 8 && /waterLevel <= dimension[1]) {
    // lastReading = GetCurrVolume(waterLevel);
    json.set(waterLevelPath.c_str(), String(waterLevel));
    json.set(timePath.c_str(), String(timeStamp));
    waterReadingsJson.set(String(timeStamp), json);
  }
  if (millis() >= sendDataprevMillis + firebaseSendingDelay)
  {
    sendDataprevMillis = millis();
    pushWaterDataToFirebase();
  }
}

void ConnectToFirebase()
{
    config.api_key = API_KEY;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;
    config.database_url = DATABASE_URL;
    Firebase.reconnectWiFi(true);
    fbdo.setResponseSize(4096);
    config.token_status_callback = tokenStatusCallback;
    // Assign the maximum retry of token generation
    config.max_token_generation_retry = 5;
    Firebase.begin(&config, &auth);



    signUpOk = true;

    // if(waterReadingPtr > 0) {
    //   waterReadingsJson.clear();
    //   for(int i=0; i<waterReadingPtr; i++) {
    //       json.set(waterLevelPath.c_str(), String(waterReadings[i][1]));
    //       json.set(timePath.c_str(), String(waterReadings[i][0]));
    //       waterReadingsJson.set(String(waterReadings[i][0]), json);
    //   }
    //   pushWaterDataToFirebase();
    // }
}

// ultrasonic sensor

void SetwaterReadingPtr2()
{
  // waterReadingPtr2 = 0;
  // return;
  int t = waterReadingPtr - 1;
  if (t == -1)
    t = 0;
  while (t > 0 && (waterReadings[t][0] % 3600))
    t--;
  waterReadingPtr2 = t;
}

int GetCurrVolume(int h)
{
  int x = ((dimension[1] - h) * dimension[0]) / dimension[1];
  return x;
}

void ResolveDimensionInputs()
{
  dimensionJson.clear();
  lcd.clear();
  lcd.print("Calculating...");
  for (int k = 0; k < noOfDimension; k++)
  {
    int x = 0, pow = 1;
    for (int i = 4; i >= 0; i--)
    {
      x += ((k ? heightEnter[i] : volumeEnter[i]) - '0') * pow;
      pow *= 10;
    }
    dimension[k] = x;
    dimensionJson.set((k ? heightPath : volumePath).c_str(), String(dimension[k]));
  }
  sendDimensionData = true;
}

int GetWaterReading()
{
  temperature = analogRead(tempPin) / 2.048;
  velocity = 331 * sqrt((273+temperature)/273);
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  duration = pulseIn(echoPin, HIGH);
  return (duration * velocity) / 20000;
}

int find_mode()
{
  int c = 0, idx = -1;
  for (int i = 0; i < modeCount; i++)
  {
    int tc = 0;
    for (int k = i; k < modeCount; k++)
    {
      if (modeWaterReadings[i] == modeWaterReadings[k])
        tc++;
    }
    if (tc > c)
    {
      c = tc;
      idx = i;
    }
    if (tc == c)
    {
      idx = min(modeWaterReadings[i], modeWaterReadings[idx]) == modeWaterReadings[i] ? i : idx;
    }
  }
  return modeWaterReadings[idx];
}

void StoreWaterReading(int currReading)
{
  modeWaterReadings[modeWaterReadingPtr] = currReading;
  modeWaterReadingPtr++;

  Serial.print("mode: ");
  Serial.println(currReading);

  if (modeWaterReadingPtr == modeCount)
  {
    modeWaterReadingPtr = 0;
    int mode = find_mode();

    timeStamp = getTime();
    if (timeStamp == 0)
      return;

    if (timeStamp >= nextBreakTime || avgWaterReadingPtr == avgCount - 1)
    {
      avgWaterReadingPtr = -1;
      
      if (waterReadingPtr == totalReadings) waterReadingPtr = 0;
      
      waterReadings[waterReadingPtr][0] = nextBreakTime;
      int avg = 0;
      for (int i = 0; i < avgCount; i++)
        avg += avgWaterReadings[i][1];
      avg /= avgCount;
      waterReadings[waterReadingPtr][1] = avg;
      // Serial.print("waterR: ");
      // Serial.println(avg);

      SetNextBreakTime();
      waterReadingPtr++;

      if (currScreen == 7)
      {
        ChangeScreen(7);
      }
    }

    // Serial.print("avg: ");
    // Serial.println(mode);

    avgWaterReadingPtr++;
    avgWaterReadings[avgWaterReadingPtr][0] = timeStamp;
    avgWaterReadings[avgWaterReadingPtr][1] = mode;
    PushDataToFirebaseArray(mode);
    if (currScreen == 2) {
      ChangeScreen(2);
    }
  }
  // to update the screen
  Serial.println("");
}

void ReadWaterLevel()
{
  //   if(millis() - prevReadingMillis < readingDelay) return;
  if (!isTimeConfig || millis() - prevReadingMillis < readingDelay)
    return;

  prevReadingMillis = millis();
  currReading = GetWaterReading();
  StoreWaterReading(currReading);

  // if(abs(lastReading - currReading) >= 10) {
  //   lastReading = currReading;
  //   StoreWaterReading(currReading);
  // }
}

// Screen functions

// shows todays averaged water readings on lcd screen
void WaterHistoryScreen()
{

  if (inputFlags[0])
  {
    ChangeScreen(2);
    return;
  }

  if (waterReadingPtr2 >= waterReadingPtr)
  {
    SetwaterReadingPtr2();
    lcd.clear();
    lcd.print("No readings so far.");
    lcd.setCursor(19, 0);
    lcd.print("^");
    return;
  }

  if (inputFlags[2])
  {
    waterReadingPtr2 -= waterReadingPtr2 >= lcdRows ? lcdRows : 0;
  }
  else if (inputFlags[3])
  {
    waterReadingPtr2 += waterReadingPtr2 <= (waterReadingPtr - 1 - lcdRows) ? lcdRows : 0;
  }

  lcd.clear();
  int vol;
  for (int i = 0; (i < lcdRows && (waterReadingPtr2 + i <= waterReadingPtr - 1)); i++)
  {
    lcd.setCursor(2, i);
    lcd.print(ReadTime(waterReadings[waterReadingPtr2 + i][0]));
    lcd.print(": ");
    vol = GetCurrVolume(waterReadings[waterReadingPtr2 + i][1]);
    if(vol >= 0) {
      lcd.print(vol);
    } else {
      lcd.print("---");
    }
    lcd.print("L");
  }
  lcd.setCursor(19, 0);
  lcd.print("^");
  lcd.setCursor(0, 3);
  lcd.print("<");
  lcd.setCursor(19, 3);
  lcd.print(">");
}

// shows wifi status on lcd screen
// generally used after Wifi.begin()
void WifiStatusScreen()
{
  lcd.clear();
  lcd.print("Connecting...");
  delay(1000);
  lcd.print("...");
  lcd.setCursor(19, 3);
  lcd.print("<");

  while (WiFi.status() != WL_CONNECTED && WiFi.status() != WL_CONNECT_FAILED)
  {
    SetInputs();
    if (inputFlags[2])
      break;
    delay(100);
  }

  // lcd.clear();
  lcd.setCursor(0, 0);
  if (WiFi.status() != WL_CONNECTED)
  {
    lcd.print("Connection Failed...Please try again.");
    delay(2000);
  }
  else
  {
    ConfigTime();
    if (isTimeConfig)
    {
      ConnectToFirebase();
      lcd.clear();
      lcd.print("Successfully");
      lcd.setCursor(0, 1);
      lcd.print("connected to... ");
      lcd.setCursor(0, 2);
      lcd.print(setWifiSSID);
      delay(2000);
    }
    else
    {
      lcd.clear();
      lcd.print("Connection Failed...Please try again.");
      delay(2000);
    }
  }
  ChangeScreen(0);
  return;
}

// lcd screen to enter password for the chosen wifi
// used by going through show wifi screen
void EnterPasswordScreen()
{
  setWifiPassword = "vnax4735";
  if(setWifiPassword != "") {
    ConnectToWifi();
    return;
  }
  if (currCol == 0 && inputFlags[0])
  {
    ChangeScreen(0);
    return;
  }
  if (inputFlags[2])
  {
    currCol--;
    if(currCol == 2) currCol = 0;
  }
  else if (inputFlags[3])
  {
    currCol++;
    if (currCol == 16)
      currCol = 0;
  }

  if (currCol == 2 || currCol == 5 || currCol == 8 || currCol == 11)

  {
    // to change the character
    int idx = currCol / 3;
    if (inputFlags[0])
    {
      passChoice[idx]++;
    }
    else if (inputFlags[1])
    {
      passChoice[idx]--;
    }

    if (currCol == 11)
    {
      if (passChoice[idx] == splCharStr.length())
        passChoice[idx] = 0;
      else if (passChoice[idx] == -1)
        passChoice[idx] = splCharStr.length() - 1;
    }
    else if (currCol == 8)
    {
      if (passChoice[idx] > '9')
        passChoice[idx] = '0';
      else if (passChoice[idx] < '0')
        passChoice[idx] = '9';
    }
    else if (currCol == 5)
    {
      if (passChoice[idx] > 'z')
        passChoice[idx] = 'a';
      else if (passChoice[idx] < 'a')
        passChoice[idx] = 'z';
    }
    else
    {
      if (passChoice[idx] > 'Z')
        passChoice[idx] = 'A';
      else if (passChoice[idx] < 'A')
        passChoice[idx] = 'Z';
    }
  }
  if (currCol == 3 || currCol == 6 || currCol == 9 || currCol == 12)
  {
    if (inputFlags[0])
    {
      if (currCol == 12)
      {
        setWifiPassword += splCharStr[passChoice[3]];
      }
      else
      {
        setWifiPassword += char(passChoice[currCol / 3]);
      }
    }
  }
  if (currCol == 14)
  {
    if (inputFlags[0])
    {
      setWifiPassword = setWifiPassword.substring(0, setWifiPassword.length() - 1);
    }
  }
  if (currCol == 16)
  {
    if (inputFlags[0])
    {
      ConnectToWifi();
      return;
    }
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enter password:");
  lcd.setCursor(0, 1);
  if(setWifiPassword.length() <= 20) {
    lcd.print(setWifiPassword);
    for(int i=setWifiPassword.length(); i<20; i++) lcd.print("_");
  } else {
    lcd.print(setWifiPassword.substring(setWifiPassword.length()-20, 20));
  }

  lcd.setCursor(0, 2);
  lcd.print("<");
  lcd.print(" ");
  lcd.print(char(passChoice[0]));
  lcd.print("+ ");
  lcd.print(char(passChoice[1]));
  lcd.print("+ ");
  lcd.print(char(passChoice[2]));
  lcd.print("+ ");
  lcd.print(splCharStr[passChoice[3]]);
  lcd.print("+ X  ");
  lcd.write(5);
  lcd.setCursor(currCol, 3);
  lcd.write((currCol == 2 || currCol == 5 || currCol == 8 || currCol == 11)? 6: 4);
  lcd.setCursor(18, 3);
  lcd.print("<>");
}

// shows device ID on lcd screen
void DeviceIDScreen()
{
  if (inputFlags[2])
  {
    ChangeScreen(0);
    return;
  }
  lcd.clear();
  lcd.setCursor(2, 0);
  lcd.print("Device ID:");
  lcd.setCursor(2, 1);
  lcd.print(DeviceID);
  lcd.setCursor(0, 0);
  lcd.print("<");
}

// lcd screen to enter tank dimension
void EnterDimensionsScreen()
{
  if(currCol == 0 && inputFlags[0]) {
    ResolveDimensionInputs();
    ChangeScreen(0);
    return;
  }
  if (inputFlags[2])
  {
    currCol--;
    if(currCol == -1) currCol = 14;
    else if(currCol == 9) currCol = 6;
    else if(currCol == 1) currCol = 0;
  }
  else if (inputFlags[3])
  {
    currCol++;
    if (currCol == 15) currCol = 0;
    else if(currCol == 7) currCol = 10;
    else if(currCol == 1) currCol = 2;
  }

  if (currCol >= 2 && currCol <= 6)
  {
    if (inputFlags[0])
      volumeEnter[currCol - 2]++;
    else if (inputFlags[1])
      volumeEnter[currCol - 2]--;
    if (volumeEnter[currCol - 2] > '9')
      volumeEnter[currCol - 2] = '0';
    else if (volumeEnter[currCol - 2] < '0')
      volumeEnter[currCol - 2] = '9';
  }
  if (currCol >= 10 && currCol <= 14)
  {
    if (inputFlags[0])
      heightEnter[currCol - 10]++;
    else if (inputFlags[1])
      heightEnter[currCol - 10]--;
    if (heightEnter[currCol - 10] > '9')
      heightEnter[currCol - 10] = '0';
    else if (heightEnter[currCol - 10] < '0')
      heightEnter[currCol - 10] = '9';
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Enter tank dimension");
  lcd.setCursor(2, 1);
  lcd.print("Volume: Height:");
  lcd.setCursor(0, 2);
  lcd.print("<");
  lcd.setCursor(2, 2);
  for (int i = 0; i < 5; i++)
  {
    lcd.print(volumeEnter[i]);
  }
  lcd.print('L');
  lcd.setCursor(10, 2);
  for (int i = 0; i < 5; i++)
  {
    lcd.print(heightEnter[i]);
  }
  lcd.print("Cm");

  lcd.setCursor(currCol, 3);
  lcd.write(currCol==0?4:6);
  lcd.setCursor(18, 3);
  lcd.print("<>");
}

// shows current water level on lcd screen
void CurrWaterLevelScreen()
{

  if(dimension[0] == 0 || dimension[1] == 0) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Enter dimenstions");
    lcd.setCursor(0, 2);
    lcd.print("before checking data");
    delay(3000);
    ChangeScreen(0);
    return;
  } else if(isTimeConfig == false) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Connect to wifi");
    lcd.setCursor(0, 2);
    lcd.print("before checking data");
    delay(3000);
    ChangeScreen(0);
    return;
  }

  if (inputFlags[2])
  {
    ChangeScreen(0);
    return;
  }
  if (inputFlags[1])
  {
    SetwaterReadingPtr2();
    ChangeScreen(7);
    return;
  }

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Current water level");
  lcd.setCursor(2, 2);

  int p = avgWaterReadingPtr;
  if (p == -1)
    p = 0;
  if (p == avgCount)
    p = avgCount-1;

  lcd.print(ReadTime(avgWaterReadings[p][0]));
  lcd.print("- ");
  lcd.print(GetCurrVolume(avgWaterReadings[p][1]));
  lcd.print("L");
  lcd.setCursor(0, 3);
  lcd.print("<");
  lcd.setCursor(19, 3);
  lcd.write(1);
}

void DisplayWifiScreen()
{

  if(dimension[0] == 0 || dimension[1] == 0) {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Enter dimenstions");
    lcd.setCursor(0, 2);
    lcd.print("before connecting to wifi");
    delay(1000);
    ChangeScreen(0);
    return;
  }

  if (inputFlags[2])
  {
    wifiDisplayOffset = 0;
    ChangeScreen(0);
    return;
  }

  lcd.clear();
  lcd.print("Scanning...");

  if (millis() - prevScanMillis >= wifiCheckDelay)
  {
    wifiDisplayOffset = 0;
    prevScanMillis = millis();

    noOfWifiAvailable = WiFi.scanNetworks();

    currRow = 0;
  }

  if (noOfWifiAvailable > 0)
  {
    if (inputFlags[3])
    {
      setWifiSSID = WiFi.SSID(currRow + wifiDisplayOffset * lcdRows);

      setWifiPassword = "";
      if (WiFi.encryptionType(currRow + wifiDisplayOffset * lcdRows) == WIFI_AUTH_OPEN)
      {
        ConnectToWifi();
      }
      else
      {
        ChangeScreen(5);
      }
      wifiDisplayOffset = 0;
      return;
    }
    if (inputFlags[1])
    {
      currRow++;
      if (currRow == lcdRows)
      {
        if (wifiDisplayOffset <= noOfWifiAvailable - lcdRows)
        {
          wifiDisplayOffset += lcdRows;
          currRow = 0;
        }
        else
          currRow = lcdRows - 1;
      }
    }
    else if (inputFlags[0])
    {
      currRow--;
      if (currRow == -1)
      {
        if (wifiDisplayOffset > 0)
        {
          wifiDisplayOffset -= lcdRows;
          currRow = lcdRows - 1;
        }
        else
          currRow = 0;
      }
    }
  }

  lcd.clear();
  if (noOfWifiAvailable == 0)
  {
    lcd.setCursor(0, 0);
    lcd.print("No networks found...");
    return;
  }
  String wifiNetwork;
  for (int i = wifiDisplayOffset; i < min(noOfWifiAvailable + 1, wifiDisplayOffset + lcdRows); i++)
  {
    lcd.setCursor(2, i - wifiDisplayOffset);
    if(WiFi.SSID(i).length() > 12) {
      wifiNetwork = WiFi.SSID(i).substring(0, 12);
    } else wifiNetwork = WiFi.SSID(i);
    wifiNetwork = wifiNetwork + "(" + WiFi.RSSI(i) + ")";
    wifiNetwork += WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "" : "*";
    lcd.print(wifiNetwork);
  }
  lcd.setCursor(1, currRow);
  lcd.write(7);
  lcd.setCursor(0, 0);
  lcd.print("<");
  lcd.setCursor(0, 3);
  lcd.write(6);
}

void MainMenuScreen()
{
  if (inputFlags[3])
  {
    ChangeScreen(currRow + 1);
    return;
  }
  if (inputFlags[1])
  {
    currRow++;
    if (currRow == lcdRows)
      currRow = 0;
  }
  else if (inputFlags[0])
  {
    currRow--;
    if (currRow == -1)
      currRow = lcdRows - 1;
  }

  lcd.clear();
  for (int i = 0; i < lcdRows; i++)
  {
    lcd.setCursor(1, i);
    lcd.print(menuScreen[i]);
  }
  lcd.setCursor(0, currRow);
  lcd.write(7);
  lcd.setCursor(19, 3);
  lcd.write(6);
}

void ChangeScreen(int newScreen)
{
  currCol = 0;
  if(newScreen == 5) currCol = 2;
  currRow = 0;
  currScreen = newScreen;
  for (int i = 0; i < noOfInputPins; i++)
  {
    inputFlags[i] = LOW;
  }
  ShowCurrScreen();
}

void ShowCurrScreen()
{
  switch (currScreen)
  {
  case 0:
    MainMenuScreen();
    break;

  case 1:
    DisplayWifiScreen();
    break;

  case 2:
    CurrWaterLevelScreen();
    break;

  case 3:
    EnterDimensionsScreen();
    break;

  case 4:
    DeviceIDScreen();
    break;

  case 5:
    EnterPasswordScreen();
    break;

  case 6:
    WifiStatusScreen();
    break;

  case 7:
    WaterHistoryScreen();
    break;

  default:
    ChangeScreen(0);
    break;
  }
}

// button inputs

void ResolveInputs()
{
  if (hasInputChanged)
  {
    ShowCurrScreen();
  }
}

void SetInputs()
{
  hasInputChanged = false;
  bool newIp;
  for (int i = 0; i < noOfInputPins; i++)
  {
    newIp = digitalRead(inputPin[i]);

    if (newIp != inputFlags[i])
    {
      delay(10);
      newIp = digitalRead(inputPin[i]);
    }
    if (newIp)
      hasInputChanged = true;
    inputFlags[i] = newIp;
  }
}

// setup

byte down_arrow[8] = {
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b00000,
  0b10001,
  0b01010,
  0b00100
};

byte up_select_arrow[8] = {
  0b00100,
  0b01110,
  0b10101,
  0b00100,
  0b00100,
  0b00100,
  0b00100,
  0b00100
};

byte tick_mark[8] = {
  0b00000,
  0b00000,
  0b00001,
  0b00010,
  0b10100,
  0b01000,
  0b00000,
  0b00000
};

byte up_down_arrow[8] = {
  0b00100,
  0b01110,
  0b10101,
  0b00100,
  0b00100,
  0b10101,
  0b01110,
  0b00100
};

byte right_select_arrow[8] = {
  0b00000,
  0b00100,
  0b00010,
  0b11111,
  0b00010,
  0b00100,
  0b00000,
  0b00000
};

void setup()
{
  // input setup
  for (int i = 0; i < noOfInputPins; i++)
  {
    pinMode(inputPin[i], INPUT);
  }

  for (int i = 0; i < totalReadings; i++)
  {
    waterReadings[i][0] = 0;
    waterReadings[i][1] = 0;
  }

  for (int i = 0; i < avgCount; i++)
  {
    avgWaterReadings[i][0] = 0;
    avgWaterReadings[i][1] = 0;
  }

  for (int i = 0; i < modeCount; i++)
  {
    modeWaterReadings[i] = 0;
  }

  // lcd setup
  lcd.begin(20, 4);
  lcd.createChar(1, down_arrow);
  lcd.createChar(4, up_select_arrow);
  lcd.createChar(5, tick_mark);
  lcd.createChar(6, up_down_arrow);
  lcd.createChar(7, right_select_arrow);

  // wifi setup
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  //   // start display
  Serial.begin(115200);
  ShowCurrScreen();
}

// loop

void loop()
{
  ReadWaterLevel();
  SetInputs();
  ResolveInputs();
  delay(200);
}

// fix don't send data if it is negative or has large drop
// increase the rate to 120 decrease delay to 10 increase firebase stuff
// check reason for sudden hang.
