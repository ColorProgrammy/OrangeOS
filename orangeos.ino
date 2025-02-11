// * @file           orangeos.ino
// * @autor          ColorProgrammy
// * @ver            v1.0

/*
Before using this product, please review the license. 
All Orange products are owned by ColorProgrammy.

Copyright (c) 2025 ColorProgrammy
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// Pins
const int DHTPin = 3;
const int buttonPin1 = 6;
const int buttonPin2 = 7;
/////

// Setup
LiquidCrystal_I2C lcd(0x27, 16, 2);
DHT dht(DHTPin, DHT11);

enum MenuState {
  MAIN_SCREEN,
  TEMP_HUMIDITY,
  SYSTEM_INFO,
  ABOUT
};

MenuState currentMenu = MAIN_SCREEN;
MenuState previousMenu = MAIN_SCREEN;

bool btn1Pressed = false;
bool btn2Pressed = false;
unsigned long lastDebounceTime = 0;
const int debounceDelay = 50; // Debounce delay

unsigned long lastScreenUpdate = 0;
unsigned long lastSensorRead = 0;

const int sensorReadInterval = 2000; // Reading the sensor
const int screenUpdateInterval = 5000; // Screen refresh

float temperature = 0;
float humidity = 0;
/////

// Custom characters
byte orange[] = {
  B00001,
  B00010,
  B00100,
  B01110,
  B11111,
  B01110,
  B00100,
  B00000
};
/////

void setup() {
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();

  lcd.createChar(0, orange);

  dht.begin();

  pinMode(buttonPin1, INPUT_PULLUP);
  pinMode(buttonPin2, INPUT_PULLUP);

  showBootScreen();
  delay(2000);
}

void loop() {
  checkButtons();
  readSensors();
  updateScreen();

  delay(50);
}

// Boot
void showBootScreen() {
  lcd.clear();
  lcd.print("Orange OS v1.0");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");
  delay(1000);
}
/////

// Checking buttons
void checkButtons() {
  int btn1 = digitalRead(buttonPin1);
  int btn2 = digitalRead(buttonPin2);

  if (millis() - lastDebounceTime > debounceDelay) {
    if (btn1 == LOW && !btn1Pressed) {
      btn1Pressed = true;
      lastDebounceTime = millis();
    }
    if (btn1 == HIGH && btn1Pressed) {
      btn1Pressed = false;
      if (millis() - lastDebounceTime > 500) {
        currentMenu = getNextMenu(currentMenu);
      } else {
        handleButton1();
      }
    }

    if (btn2 == LOW && !btn2Pressed) {
      btn2Pressed = true;
      lastDebounceTime = millis();
    }
    if (btn2 == HIGH && btn2Pressed) {
      btn2Pressed = false;
      if (millis() - lastDebounceTime > 500) {
        currentMenu = getPreviousMenu(currentMenu);
      } else {
        handleButton2();
      }
    }
  }
}
/////

// Handle buttons
void handleButton1() {
  currentMenu = getNextMenu(currentMenu);
}

void handleButton2() {
  currentMenu = getPreviousMenu(currentMenu);
}
/////

// Scrolling through the menu
MenuState getNextMenu(MenuState currentState) {
  if (currentState < ABOUT) {
    return (MenuState)(currentState + 1);
  }
  return MAIN_SCREEN;
}

MenuState getPreviousMenu(MenuState currentState) {
  if (currentState > MAIN_SCREEN) {
    return (MenuState)(currentState - 1);
  }
  return ABOUT;
}
/////

// Reading sensors
void readSensors() {
  if (millis() - lastSensorRead >= sensorReadInterval) {
    temperature = dht.readTemperature();
    humidity = dht.readHumidity();

    if (isnan(temperature)) {
      temperature = -99;
    }
    if (isnan(humidity)) {
      humidity = -1;
    }

    lastSensorRead = millis();
  }
}
/////

// LCD screen update
void updateScreen() {
  if (currentMenu != previousMenu || millis() - lastScreenUpdate >= screenUpdateInterval) {
    lcd.clear();
    switch (currentMenu) {
      case MAIN_SCREEN:
        showMainMenu();
        break;
      case TEMP_HUMIDITY:
        showClimate();
        break;
      case SYSTEM_INFO:
        showSystemInfo();
        break;
      case ABOUT:
        showAbout();
        break;
    }
    previousMenu = currentMenu;
    lastScreenUpdate = millis();
  }
}
/////

// Show functions
void showMainMenu() {
  lcd.print(">Home          ");
  lcd.write(byte(0));
  lcd.setCursor(0, 1);
  lcd.print("<Climate  About>");
}

void showClimate() {
  lcd.print(">Climate");
  lcd.setCursor(0, 1);
  lcd.print("T: ");
  lcd.print(temperature);
  lcd.print("C H: ");
  lcd.print(humidity);
  lcd.print("%");
}

void showSystemInfo() {
  lcd.print(">System Info");
  lcd.setCursor(0, 1);
  lcd.print("Uptime: ");
  lcd.print(millis() / 1000);
  lcd.print("s");
}

void showAbout() {
  lcd.print(">About");
  lcd.setCursor(0, 1);
  lcd.print("Orange OS v1.0");
}
/////
