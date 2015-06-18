#include <EEPROM.h>

#include <LCD.h>
#include <I2CIO.h>
#include <LiquidCrystal_SR3W.h>
#include <LiquidCrystal.h>
#include <LiquidCrystal_I2C.h>
#include <FastIO.h>
#include <LiquidCrystal_SR2W.h>
#include <LiquidCrystal_SR.h>
#include <Wire.h>

//#define DEBUG 1

#include <SPI.h>
#include <dht.h>

// Sensor:
int adjustment = 0; // In C
int tempBuffer = 3; // measure +- 3c
int tempTarget = 7;

// Timekeeping:
uint32_t timeBoot = 0;
uint32_t timeOn = 0;
uint32_t timeOff = 0;

// in milliseconds; time to 'thaw' if we can't reach target temperature.
#define THAWTIME (5 * 60 * 1000)

// in milliseconds; "on" duty cycle minimum.
#define MINRUN (5 * 60 * 1000)

// in milliseconds; "on" duty cycle maximum.
#define MAXRUN (15 * 60 * 1000)

// Sensor:
dht DHT;
char dhtstatus[16];
#define DHT11_PIN 9

// Relay Stage
enum e_stages {
  OFF,     // Manually disabled
  INIT,    // Haven't started yet.
  FAN,     // Just fan
  COOLING, // Compressor
  DEFROST, // Hit low-temp max, thawing condenser, just fan.
  PUMPONLY // Test only, compress only.
};
char stage = INIT;
#define FAN_PIN 2
#define FANHIGH_PIN 4
#define FANMEDIUM_PIN 5
#define COMPRESSOR_PIN 3

// LCD
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);    // I2C address (0x27 is default)

int freeRam() {
  extern int __heap_start,*__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int) __brkval);  
}

void setupLCD()
{
  // set up the LCD's number of columns and rows: 
  lcd.begin(16, 2);
  lcd.backlight();
}

const __FlashStringHelper *stageName(char s)
{
  switch( s )
  {
    case OFF:
      return F("Off");
    case INIT:
      return F("Init");
    case FAN:
      return F("Circulating");
    case COOLING:
      return F("Cooling");
    case DEFROST:
      return F("Defrosting");
  }
  return F("Unknown");
}

void lcdSlide();

void setup() {
#ifdef DEBUG
  Serial.begin(57600);
  while (!Serial) {
    // wait for serial port to connect. Needed for Leonardo only
    delay(10);
  }

  Serial.print(F("Free RAM: "));
  Serial.println(freeRam());
#endif

  setupLCD();
  pinMode(FAN_PIN, OUTPUT);
  pinMode(FANMEDIUM_PIN, OUTPUT);
  pinMode(FANHIGH_PIN, OUTPUT);
  pinMode(COMPRESSOR_PIN, OUTPUT);

  timeBoot = millis();
  timeOn = 0;
  timeOff = 0;
  
#ifdef DEBUG
    Serial.print(F("Free RAM: "));
    Serial.println(freeRam());
#endif
}

void lcdSlide()
{
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(stageName(stage));
  lcd.setCursor(0, 1);
  lcd.print(DHT.temperature+adjustment, 1);
  lcd.print(F("C "));
  lcd.print(dhtstatus);
#ifdef DEBUG
  Serial.print(F("Stage: "));
  Serial.print(stageName(stage));
  Serial.print(F(", Temp: "));
  Serial.print(DHT.temperature+adjustment, 1);
  Serial.print(F("C, Status: "));
  Serial.println(dhtstatus);
#endif
}

void readStatus()
{
  int chk = DHT.read21(DHT11_PIN);
  switch (chk)
  {
    case DHTLIB_OK:
      strcpy( dhtstatus, "ok" );
      break;
    case DHTLIB_ERROR_CHECKSUM: 
      strcpy( dhtstatus, "csum" );
      break;
    case DHTLIB_ERROR_TIMEOUT:
      strcpy( dhtstatus, "tout" );
      break;
    case DHTLIB_ERROR_CONNECT:
      strcpy( dhtstatus, "noconn" );
      break;
    case DHTLIB_ERROR_ACK_L:
      strcpy( dhtstatus, "losig" );
      break;
    case DHTLIB_ERROR_ACK_H:
      strcpy( dhtstatus, "hisig" );
      break;
    default:
      strcpy( dhtstatus, "err" );
  }
}

void setStage()
{
  int fanPin = 1;
  int fanMediumPin = 0;
  int fanHighPin = 0;
  int compressorPin = 0;
  
  if( stage == OFF )
  {
    fanPin = 0;
    fanHighPin = 0;
    compressorPin = 0;
  }
  else
  {
    if( stage == COOLING )
    {
      fanPin = 1;
      fanMediumPin = 1;
      fanHighPin = 1;
      compressorPin = 1;
    }
    else if( stage == DEFROST )
    {
      fanPin = 1;
      fanMediumPin = 1;
      fanHighPin = 1;
    }
  }
    
  digitalWrite( FAN_PIN, fanPin );
  digitalWrite( FANMEDIUM_PIN, fanMediumPin );
  digitalWrite( FANHIGH_PIN, fanHighPin );
  digitalWrite( COMPRESSOR_PIN, compressorPin );
}

void updateStage()
{
  if( stage == OFF )
  {
    setStage();
    return;
  }
    
  uint32_t now = millis();
#ifdef DEBUG
  Serial.print(F("Time now: "));
  Serial.println(now);
#endif
  if( DHT.temperature + adjustment > ( tempTarget + tempBuffer ) )
  {
    if( stage == COOLING )
    {
      // Have we been running for 15 mins yet?
      if( now >= timeOff )
      {
        // Ran this long, still haven't cooled enough. Defrost for ~5 minutes.
        stage = DEFROST;
        timeOn = now;
        timeOff = now + THAWTIME;
        setStage();
#ifdef DEBUG
        Serial.print(F("Thawing until: "));
        Serial.println(timeOff);
#endif
      }
      else
        return; // Keep fighting!
    }
    else
    {
      if( stage == DEFROST && now < timeOff )
      {
        // Not yet...
        return;
      }
      
      stage = COOLING;
      timeOn = now;
      timeOff = now + MAXRUN;
      setStage();
#ifdef DEBUG
      Serial.print(F("Cooling until: "));
      Serial.println(timeOff);
#endif
    }
  }
  else
  {
    if( stage == COOLING && now < timeOn + MINRUN )
    {
      // Minimum duty cycle not met yet, keep going on...
      return;
    }
    
#ifdef DEBUG
    if( stage == COOLING )
    {
      Serial.print(F("Cooling done, minimum cycle complete: "));
      Serial.println(timeOn + MINRUN);
    }
#endif

    stage = FAN;
    timeOn = now;
    timeOff = now;
    setStage();
  }
}

unsigned int loops = 0;
void loop() {
  delay(1000);

  loops++;
  //if( loops % 5 == 0 || loops == 1 ) // Every 5 seconds:
  {
    readStatus();

    updateStage();

    lcdSlide();
/*
#ifdef DEBUG
    Serial.print(F("Free RAM: "));
    Serial.println(freeRam());
#endif
*/
  }
}

