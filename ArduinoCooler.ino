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

#define DEBUG 1

#include <SPI.h>
#include <WebSocket.h>
#include <Ethernet.h>
#include <Time.h>

#include <dht.h>

#include "webtime.h"

#define EEPROM_VERSION 2

bool needTime = true;

// Just our loop counter, we loop once each second-ish:
unsigned int loops = 0;

// Sensor:
char adjustment = 0; // In C
char tempBuffer = 3; // measure +- 3c
char tempTarget = 7;

// Timekeeping:
uint32_t timeBoot = 0;
uint32_t timeOn = 0;
uint32_t timeOff = 0;

// Schedule (on-off, offset from 12am in seconds):
uint32_t startTime = ( 8 * 60 * 60 ); // 8 AM
uint32_t stopTime = ( ( 23 * 60 * 60 ) + 50 * 60 ); // 11:50 PM

// from GMT. for example, PST (-08:00) would be (0 - (8 * 60 * 60)) or -28800
int32_t timeOffset = ( 0 - (6 * 60 * 60));
char timeUrl[16];

// in seconds; time to 'thaw' if we can't reach target temperature.
uint32_t thawtime = (5 * 60);

// in seconds; "on" duty cycle minimum.
uint32_t minruntime = (5 * 60);

// in seconds; "on" duty cycle maximum.
uint32_t maxruntime = (15 * 60);

// coldest it's been since starting COOL phase
double coldesttemp = 0;

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
#define COMPRESSOR_PIN 2
#define FAN_PIN 5
#define FANMEDIUM_PIN 6
#define FANHIGH_PIN 7

// LCD
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);    // I2C address (0x27 is default)

// Network
byte ip[] = { 192, 168, 3, 55 };
byte nameserver[] = { 8, 8, 8, 8 };
byte gateway[] = { 192, 168, 3, 1 };
byte netmask[] = { 255, 255, 255, 0 };
byte mac[] = { 0x10, 0x4F, 0x33, 0x4B, 0x16, 0x55 };

EthernetServer server = EthernetServer(80);

int freeRam() {
  extern int __heap_start,*__brkval;
  int v;
  return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int) __brkval);  
}

void readConfig() {
  int i, eepPos = 0;
  char *cminruntime, *cmaxruntime, *cthawtime, *ctimeStart, *ctimeStop, *ctimeOffset;
  cminruntime = (char*)&minruntime;
  cmaxruntime = (char*)&maxruntime;
  cthawtime = (char*)&thawtime;
  ctimeStart = (char*)&startTime;
  ctimeStop = (char*)&stopTime;
  ctimeOffset = (char*)&timeOffset;
  char ver = EEPROM.read(eepPos++);
  if( ver != EEPROM_VERSION )
  {
    // Factory Defaults:
    strcpy_P( timeUrl, PSTR("google.ca") );
    writeConfig();
    return;
  }

  for( i=0; i < 6; i++ )
    mac[i] = EEPROM.read(eepPos++);
  for( i=0; i < 4; i++ )
    ip[i] = EEPROM.read(eepPos++);
  for( i=0; i < 4; i++ )
    nameserver[i] = EEPROM.read(eepPos++);
  for( i=0; i < 4; i++ )
    gateway[i] = EEPROM.read(eepPos++);
  for( i=0; i < 4; i++ )
    netmask[i] = EEPROM.read(eepPos++);
  adjustment = (char)EEPROM.read(eepPos++);
  tempTarget = (char)EEPROM.read(eepPos++);
  tempBuffer = (char)EEPROM.read(eepPos++);
  for( i=0; i < 4; i++ )
    cminruntime[i] = EEPROM.read(eepPos++);
  for( i=0; i < 4; i++ )
    cmaxruntime[i] = EEPROM.read(eepPos++);
  for( i=0; i < 4; i++ )
    cthawtime[i] = EEPROM.read(eepPos++);
  for( i=0; i < 4; i++ )
    ctimeStart[i] = EEPROM.read(eepPos++);
  for( i=0; i < 4; i++ )
    ctimeStop[i] = EEPROM.read(eepPos++);
  for( i=0; i < 4; i++ )
    ctimeOffset[i] = EEPROM.read(eepPos++);
  for( i=0; i < sizeof(timeUrl); i++ )
    timeUrl[i] = EEPROM.read(eepPos++);
}

void writeConfig() {
  int i, eepPos = 0;
  char *cminruntime, *cmaxruntime, *cthawtime, *ctimeStart, *ctimeStop, *ctimeOffset;
  cminruntime = (char*)&minruntime;
  cmaxruntime = (char*)&maxruntime;
  cthawtime = (char*)&thawtime;
  ctimeStart = (char*)&startTime;
  ctimeStop = (char*)&stopTime;
  ctimeOffset = (char*)&timeOffset;
  EEPROM.write(eepPos++, EEPROM_VERSION);
  for( i=0; i < 6; i++ )
    EEPROM.write(eepPos++, mac[i]);
  for( i=0; i < 4; i++ )
    EEPROM.write(eepPos++, ip[i]);
  for( i=0; i < 4; i++ )
    EEPROM.write(eepPos++, nameserver[i]);
  for( i=0; i < 4; i++ )
    EEPROM.write(eepPos++, gateway[i]);
  for( i=0; i < 4; i++ )
    EEPROM.write(eepPos++, netmask[i]);
  EEPROM.write(eepPos++, adjustment);
  EEPROM.write(eepPos++, tempTarget);
  EEPROM.write(eepPos++, tempBuffer);
  for( i=0; i < 4; i++ )
    EEPROM.write(eepPos++, cminruntime[i]);
  for( i=0; i < 4; i++ )
    EEPROM.write(eepPos++, cmaxruntime[i]);
  for( i=0; i < 4; i++ )
    EEPROM.write(eepPos++, cthawtime[i]);
  for( i=0; i < 4; i++ )
    EEPROM.write(eepPos++, ctimeStart[i]);
  for( i=0; i < 4; i++ )
    EEPROM.write(eepPos++, ctimeStop[i]);
  for( i=0; i < 4; i++ )
    EEPROM.write(eepPos++, ctimeOffset[i]);
  for( i=0; i < sizeof(timeUrl); i++ )
    EEPROM.write(eepPos++, timeUrl[i]);
}

void setupEthernet()
{
  Ethernet.begin(mac, ip, nameserver, gateway, netmask);
#ifdef DEBUG
  Serial.println(F("Ethernet cfg"));
#endif
  delay(100);
}

void setupTime()
{
  uint32_t oldTime = now();
  
  uint32_t t = 0;
  int tries = 0;
  EthernetClient c;
  while( t == 0 )
  { 
    tries++;
    if( tries == 5 )
      return;

#ifdef DEBUG
    Serial.println(F("Requesting time..."));
#endif
    t = webUnixTime(c, timeUrl);
    if( t == 0 )
    {
      delay(5000);
      continue;
    }
#ifdef DEBUG
    Serial.print(F("UTC Time: "));
    Serial.println(t);
#endif
    setTime(t);
    adjustTime(timeOffset);
#ifdef DEBUG
    Serial.print(F("Local Time: "));
    Serial.println(now());
#endif

    needTime = false;

    // Compensate our phase time...
    timeOn += ( t - oldTime );
    timeOff += ( t - oldTime );
  }
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
      return F("Sleep");
    case INIT:
      return F("Init");
    case FAN:
      return F("Fan");
    case COOLING:
      return F("Cool");
    case DEFROST:
      return F("Defrost");
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

  timeBoot = millis() * 0.001;
  timeOn = 0;
  timeOff = 0;
  startTime = 0;
  stopTime = 86400;
  
  readConfig();
  setupLCD();
  lcdSlide();

  setupEthernet();
  setupTime();

  pinMode(FAN_PIN, OUTPUT);
  pinMode(FANMEDIUM_PIN, OUTPUT);
  pinMode(FANHIGH_PIN, OUTPUT);
  pinMode(COMPRESSOR_PIN, OUTPUT);

#ifdef DEBUG
    Serial.print(F("Free RAM: "));
    Serial.println(freeRam());
#endif
}

char lcdPage = 0;
void lcdSlide()
{
  uint32_t n = now();
  lcd.clear();
  lcd.setCursor(0, 0);
  if( lcdPage == 0 )
  {
    lcd.print(stageName(stage));
    if( stage != INIT )
    {
      uint32_t dur = (timeOff - n);
      if( stage == FAN )
        dur = 0 - dur; // Show positive number, tells user how long we've been in FAN phase.
      lcd.print(F(" for "));
      lcd.print(dur);
      lcd.print(F("s"));
    }
    lcd.setCursor(0, 1);
    lcd.print(DHT.temperature+adjustment, 1);
    lcd.print(F("C "));
    lcd.print(dhtstatus);
  }
  else if( lcdPage == 1 )
  {
    uint8_t h = hour(n);
    uint8_t m = minute(n);
    uint8_t s = second(n);
    lcd.print(ip[0]);
    lcd.print(".");
    lcd.print(ip[1]);
    lcd.print(".");
    lcd.print(ip[2]);
    lcd.print(".");
    lcd.print(ip[3]);
    lcd.setCursor(0, 1);
    lcd.print(F("Time: "));
    lcd.print(h);
    lcd.print(":");
    if( m < 10 )
      lcd.print("0");
    lcd.print(m);
    lcd.print(":");
    if( s < 10 )
      lcd.print("0");
    lcd.print(s);
  }
  else if( lcdPage == 2 )
  {
    bool isdays = false;
    uint32_t uptime = millis() * 0.001;
    uint16_t d = 0;
    if( uptime >= 86400 )
    {
      isdays = true;
      d = floor( uptime / 86400 );
    }
    uint8_t h = hour(uptime);
    uint8_t m = minute(uptime);
    uint8_t s = second(uptime);
    lcd.print(F("KulaShaker v"));
    lcd.print(EEPROM_VERSION);
    lcd.setCursor(0, 1);
    lcd.print(F("Up: "));
    if( d > 0 )
    {
      lcd.print(d);
      lcd.print("d, ");
    }
    lcd.print(h);
    lcd.print(":");
    if( m < 10 )
      lcd.print("0");
    lcd.print(m);
    lcd.print(":");
    if( s < 10 )
      lcd.print("0");
    lcd.print(s);
  }
  else if( lcdPage == 3 )
  {
    uint8_t h = hour(startTime);
    uint8_t m = minute(startTime);
    uint8_t s = second(startTime);
    lcd.print(F("On:  "));
    lcd.print(h);
    lcd.print(":");
    if( m < 10 )
      lcd.print("0");
    lcd.print(m);
    lcd.print(":");
    if( s < 10 )
      lcd.print("0");
    lcd.print(s);
    
    lcd.setCursor(0, 1);
    h = hour(stopTime);
    m = minute(stopTime);
    s = second(stopTime);
    lcd.print(F("Off: "));
    lcd.print(h);
    lcd.print(":");
    if( m < 10 )
      lcd.print("0");
    lcd.print(m);
    lcd.print(":");
    if( s < 10 )
      lcd.print("0");
    lcd.print(s);
  }

  // Change pages every 5 seconds.
  if( loops % 500 == 0 )
    lcdPage++;

  if( lcdPage == 4 )
    lcdPage = 0;
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
    fanMediumPin = 0;
    fanHighPin = 0;
    compressorPin = 0;
  }
  else if( stage == COOLING )
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
    compressorPin = 0;
  }

#ifdef DEBUG
  Serial.print(F("Pins: "));
  Serial.print(compressorPin);
  Serial.print(F("/"));
  Serial.print(fanPin);
  Serial.print(F("/"));
  Serial.print(fanMediumPin);
  Serial.print(F("/"));
  Serial.println(fanHighPin);
#endif
    
  digitalWrite( FAN_PIN, fanPin == 0 );
  digitalWrite( FANMEDIUM_PIN, fanMediumPin == 0 );
  digitalWrite( FANHIGH_PIN, fanHighPin == 0 );
  digitalWrite( COMPRESSOR_PIN, compressorPin == 0 );
}

void updateStage()
{
  // Follow the schedule..
  time_t n = now();
  uint32_t h = ( ((uint32_t)hour(n)) * 60 * 60 );
  uint32_t m = ( minute(n) * 60 );
  uint32_t s = second(n);
  uint32_t nowSecs = h + m + s;

  // If outside our "running" schedule window:
  if( nowSecs <= startTime || nowSecs >= stopTime )
  {
    if( stage == OFF )
      return;

    stage = OFF;
    timeOn = n;
    timeOff = n - stopTime + startTime;
    setStage();
    return;
  }

  if( ( DHT.temperature + adjustment ) > ( tempTarget + tempBuffer ) )
  {
    if( stage == COOLING )
    {
      // Have we been running for 15 mins yet?
      if( n >= timeOff )
      {
        // Ran this long, still haven't cooled enough. Defrost for ~5 minutes.
        stage = DEFROST;
        timeOn = n;
        timeOff = n + thawtime;
        setStage();
      }
      else
      {
        if( DHT.temperature < coldesttemp )
          coldesttemp = DHT.temperature;
          
        return; // Keep fighting!
      }
    }
    else
    {
      if( stage == DEFROST && n < timeOff )
      {
        // Not yet...
        return;
      }

      coldesttemp = DHT.temperature;
      stage = COOLING;
      timeOn = n;
      timeOff = n + maxruntime;
      setStage();
    }
  }
  else
  {
    if( stage == COOLING && n < timeOn + minruntime )
    {
      // Minimum duty cycle not met yet, keep going on...
      return;
    }

    if( stage == FAN )
      return;

    stage = FAN;
    timeOn = n;
    timeOff = n;
    setStage();
  }
}

void sendQuad(EthernetClient &client, const __FlashStringHelper *pfx, byte *quad)
{
  client.print(pfx);
  for( byte x=0; x < 4; x++ )
  {
    client.print(quad[x]);
    if( x != 3 )
      client.print(".");
  }
}

void sendUInt(EthernetClient &client, const __FlashStringHelper *pfx, uint32_t val)
{
  client.print(pfx);
  client.print(val);
}

void sendInt(EthernetClient &client, const __FlashStringHelper *pfx, int32_t val)
{
  client.print(pfx);
  client.print(val);
}

void sendStatus(EthernetClient &client)
{
  uint32_t n = now();
  int x;
  client.println(F("HTTP/1.1 200 OK"));
  client.println(F("Connection: close"));
  client.println(F("Access-Control-Allow-Origin: *"));
  client.println(F("Content-Type: text/plain"));
  client.println();
  sendQuad(client, F("a=s&ip="), ip);
  sendQuad(client, F("&dns="), nameserver);
  sendQuad(client, F("&nm="), netmask);
  sendQuad(client, F("&gw="), gateway);
  sendInt(client, F("&adj="), adjustment);
  sendUInt(client, F("&on="), startTime);
  sendUInt(client, F("&off="), stopTime);
  sendInt(client, F("&targ="), tempTarget);
  sendInt(client, F("&buf="), tempBuffer);
  sendUInt(client, F("&thaw="), thawtime);
  sendUInt(client, F("&min="), minruntime);
  sendUInt(client, F("&max="), maxruntime);
  sendInt(client, F("&toff="), timeOffset);
  client.print(F("&timeUrl="));
  client.print(timeUrl);
  client.print(F("&st="));
  client.print(stageName(stage));
  client.print(F("&temp="));
  client.print(DHT.temperature + adjustment);
  client.print(F("&stoff="));
  client.print((int)(timeOff - n));
  client.print(F("&v="));
  client.println(EEPROM_VERSION);
}

void parseQuad(char *val, byte *into)
{
  char ipos=0, spos=0, vpos=0;
  char seg[5] = { '\0', '\0', '\0', '\0', '\0' };

  char v;
  for( v = val[vpos++]; spos < sizeof(seg)-1 && ipos < 4; v = val[vpos++] )
  {
    if( v == '\0' )
    {
      into[ipos++] = atoi(seg);
      return;
    }
    else if( v == '.' )
    {
      into[ipos++] = atoi(seg);
      spos = 0;
      seg[spos] = '\0';
    }
    else
    {
      seg[spos++] = v;
      seg[spos] = '\0';
    }
  }
}

void handlePair(char *key, char *val)
{
  // a=c&ip=123.123.123.123&dns=123.123.123.123&nm=255.255.255.255&gw=123.123.123.123
  // &adj=-11&on=86390&off=86395&targ=100&buf=100&thaw=86400&min=86400&max=86400
  // &timeUrl=http://fortfire.ca/whatever.txt HTTP/1.1
  // valid keys:
  // a ip dns nm gw adj off on targ buf thaw min max timeUrl
#ifdef DEBUG
  Serial.print(F("Key: '"));
  Serial.print(key);
  Serial.print(F("' / Value: '"));
  Serial.print(val);
  Serial.println("'");
#endif
  if( strcmp(key, "ip") == 0 )
    parseQuad(val, ip);
  else if( strcmp(key, "dns") == 0 )
    parseQuad(val, nameserver);
  else if( strcmp(key, "nm") == 0 )
    parseQuad(val, netmask);
  else if( strcmp(key, "gw") == 0 )
    parseQuad(val, gateway);
  else if( strcmp(key, "adj") == 0 )
    adjustment = atoi(val);
  else if( strcmp(key, "on") == 0 )
    startTime = strtol(val, NULL, 10);
  else if( strcmp(key, "off") == 0 )
    stopTime = strtol(val, NULL, 10);
  else if( strcmp(key, "targ") == 0 )
    tempTarget = atoi(val);
  else if( strcmp(key, "buf") == 0 )
    tempBuffer = atoi(val);
  else if( strcmp(key, "thaw") == 0 )
    thawtime = strtol(val, NULL, 10);
  else if( strcmp(key, "min") == 0 )
    minruntime = strtol(val, NULL, 10);
  else if( strcmp(key, "max") == 0 )
    maxruntime = strtol(val, NULL, 10);
  else if( strcmp(key, "toff") == 0 )
    timeOffset = strtol(val, NULL, 10);
  else if( strcmp_P(key, PSTR("timeUrl")) == 0 )
    strcpy( timeUrl, val );
}

void parseRequest(EthernetClient &client, char *request)
{
  bool onNext = false;
  bool onVal = false;
  char *key=NULL, *val=NULL, *curs;
  int x;
  for( curs=request;; curs++ )
  {
    if( curs[0] == '\0' )
    {
      if( key && val )
        handlePair( key, val );
      return;
    }
    else if( onNext )
    {
      onNext = false;
      key = curs;
    }
    else if( onVal )
    {
      onVal = false;
      val = curs;
    }
    else if( curs[0] == '?' )
    {
      key = val = NULL;
      onNext = true;
    }
    else if( curs[0] == '&' )
    {
      onNext = true;
      curs[0] = '\0';
      if( key && val )
        handlePair( key, val );
      key = val = NULL;
    }
    else if( curs[0] == '=' )
    {
      curs[0] = '\0';
      onVal = true;
    }
  }
}

void handlePacket()
{
  bool onReq = false;
  int reqPos = 0;
  char req[350];
  // GET /?a=c&ip=123.123.123.123&dns=123.123.123.123&nm=255.255.255.255&gw=123.123.123.123&adj=-11&on=86390&off=86395&targ=100&buf=100&thaw=86400&min=86400&max=86400&timeUrl=http://fortfire.ca/whatever.txt HTTP/1.1
  // Example long config request...
  
  // listen for incoming clients
  EthernetClient client = server.available();
  if (client) {
    // an http request ends with a blank line
    while (client.connected()) {
      if (client.available()) {
        char c = client.read();
        // if you've gotten to the end of the line (received a newline
        // character) and the line is blank, the http request has ended,
        // so you can send a reply
        if( !onReq && c == ' ' )
          onReq = true;
        else if( onReq && c == ' ' )
        {
          if( reqPos > 5 )
          {
            parseRequest(client, req);
            writeConfig();
          }
          break;
        }
        else if( onReq && reqPos < (sizeof(req)-1) )
        {
          req[ reqPos++ ] = c;
          req[ reqPos ] = '\0';
        }
      }
    }

    sendStatus(client);

    // give the web browser time to receive the data
    delay(10);
    
    // close the connection:
    client.stop();
  }
}

void avoidFrostingOver()
{
  if( stage != COOLING )
    return;

  if( DHT.temperature < coldesttemp )
  {
    coldesttemp = DHT.temperature;
    return;
  }

  if( coldesttemp < (DHT.temperature - tempBuffer) )
  {
    // We've come up <tempBuffer> degrees, gotta defrost.
    time_t n = now();
    stage = DEFROST;
    timeOn = n;
    timeOff = n + thawtime;
    setStage();
  }
}

void loop() {
  delay(10);

  loops++;

  if( loops % 100 == 0 )
  {
    readStatus();

    avoidFrostingOver();

    updateStage();

    lcdSlide();
  }

  // Try to set the time again if it failed last time.
  if( needTime && ( loops % 3000 == 0 ) )
      setupTime();

  handlePacket();
}

