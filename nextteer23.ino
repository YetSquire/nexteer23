// libraries
#include <LiquidCrystal.h>
#include <SevSeg.h>
#include <IRremote.h>
#include <Wire.h>
#include <Stepper.h>

//IR receiver constants
const int receiverPin = 12;      // Signal Pin of IR receiver to Arduino Digital Pin 12
IRrecv irrecv(receiverPin);     // create instance of 'irrecv'
decode_results results;      // create instance of 'decode_results'

// LCD setup
LiquidCrystal lcd(10);

// Segment setup
SevSeg sevseg;
byte segmentPins[] = {0, 1, 16, 15, 14, 2, 5, 17};

//Button constant
const int buttonPin = 3;

//Stepper setup
const int revolution = 2038;
Stepper myStepper = Stepper(revolution, 9, 7, 8, 6);

//global variables
int lock = 0;
byte s;
byte m;
byte h;  
unsigned long mi = 0;

void setup() 
{
  Wire.begin();
  //Serial.begin(9600); Disabled so that digital pins 0 and 1 work
  // set up the LCD's number of columns and rows:
  lcd.begin(16, 2);
  // printing setup
  lcd.print("Andy Yao, 23'");
  lcd.setCursor(0, 1);
  lcd.print("8:00:00");

  //enable IR reciever
  irrecv.enableIRIn();

  //Used sevseg library to setup 7-segment display
  byte numDigits = 1;
  byte digitPins[] = {};
  bool resistorsOnSegments = true;
  byte hardwareConfig = COMMON_CATHODE; 
  sevseg.begin(hardwareConfig, numDigits, digitPins, segmentPins, resistorsOnSegments);
  sevseg.setBrightness(90);
  sevseg.setNumber(lock);

  //Stepper speed
  myStepper.setSpeed(5);
  
  //Button setup
  pinMode(buttonPin, INPUT);
}

void loop() 
{
  //displayTime() updates the time
  displayTime();
  //transmission sends either an array of bytes or specifically 3 bytes
  transmission(0x01, 0x01, 0x01);
  //recieving information, device must always be working on a lock
  Wire.requestFrom(0x10, 1, true);
  while (Wire.available() > 0) 
  {
    Serial.print(lock); 
    //no idea if this will work, gotta test
    byte x = Wire.read();
    lock = (int)x;
  }
//;-)
  //display lock number
  sevseg.setNumber(lock);
  sevseg.refreshDisplay(); 

  if (lock == 1)
  {
    //time, read hours, minutes, seconds
    transmission(0x10, 0x01, 0x01);
    Wire.requestFrom(0x10, 1);
    while (Wire.available()) 
    {
      int x = Wire.read();
      Serial.println(x); 
      h = (int)x;
      Serial.println(h); 
    }
    transmission(0x10, 0x01, 0x02);
    Wire.requestFrom(0x10, 1);
    while (Wire.available()) 
    {
      int x = Wire.read();
      Serial.println(x); 
      m = (int)x;
      Serial.println(m); 
    }
    transmission(0x10, 0x01, 0x03);
    Wire.requestFrom(0x10, 1);
    while (Wire.available()) 
    {
      int x = Wire.read();
      Serial.println(x); 
      s = (int)x;
      Serial.println(s); 
    }

    //send in 8 hours, 0 minutes, 0 seconds
    byte payload[] = {0x10, 0x02, 0x01, 0x08, 0x00, 0x00};
    transmission(payload, sizeof(payload));
    mi = millis();
    //The lock confirm transmission
    lockVerification();
  }
  else if (lock == 2)
  {
    //IR Reciever
    //need to include serial value reading just in case
    int hold = 0;
    byte payload[6]; //meant to hold the 6 digits of the code

    while (hold < 6)
    {
      displayTime();  //we need to display time while in the loop
      if (irrecv.decode(&results))   //if recieved an IR signal
      {
        displayTime();
        if (IR() != 255)
        {
          byte x = IR();
          payload[hold] = x;
        }
        else hold = hold -1;
        delay(500);                 // to make sure signal isn't read twice
        irrecv.resume();            // get ready to receive the next value
        hold++;
      }  
    }
    //Serial.println("sent: ");
    //for (int x = 0; x < 6; x++) Serial.print(payload[x]);
    //Serial.print(payload);
    byte ho[] = {0x20, 0x02, 0x01, payload[0], payload[1], payload[2], payload[3], payload[4], payload[5]};
    transmission(ho, sizeof(ho));
    lockVerification();
  }
  else if (lock == 3)
  {
    //Encoded message
    byte x;
    byte y;

    transmission(0x30, 0x01, 0x01);
    Wire.requestFrom(0x10, 2);
    while (Wire.available()) 
    {
      y = Wire.read();
      x = Wire.read();
      //seemingly backwards bc initially I assumed y would be more significant, and now I'm too lazy to change everything
      Serial.print(s); 
    }
    delay(6000);
    int ya = (int)y % 16; //get the "tens digit"
    int yb = (int)y/16; //get the "ones digit"
    Serial.print("ya");
    Serial.println(ya);
    Serial.print("yb");
    Serial.println(yb);
    long hold = x + yb*16*16*16 + ya*16*16; //combine the x and the y taking into account significance
    Serial.print("hold");
    Serial.println(hold);
    long b = ((((hold*hold)^hold)<<4)^hold) % 65536; //it might have been easier to do it in base 10 but this works too
    //b became decimal, so convert decimal to hex, send it back in
    byte big = b / 256;
    byte small = b % 256;
    byte ret[] = {0x30, 0x02, 0x01, big, small};
    transmission(ret, sizeof(ret));

    lockVerification();
  }
  else if (lock == 4)
  {
    //stepper
    int hold = 0;
    bool clockwise = true; //assumed DIAL travels clockwise, later checking to confirm it
    byte payload[7]; //so this is supposed to be the order in which the dial is supposed to turn
    //byte payload[] = {17, 14, 2, 5, 8, 2, 17}; //practice run
    //starting with where it is currently, with the next 6 being the digits of the code
    byte d;
    transmission(0x40, 0x01, 0x01);
    Wire.requestFrom(0x10, 6);
    while (Wire.available()) 
    {
      displayTime(); //dunno if reading is fast enough, gotta check it
      payload[hold+1] = Wire.read(); //the first array index is blank
      hold++;
      Serial.print(payload[hold]); 
    }

    for (hold = 0; hold < 6; hold++) //we only travel 6 times
    {
      int st = location();
      payload[hold] = st;
      //however, because the stepper is "backwards", we need to go negative to turn the dial cw
      if (!clockwise)
      {
        Serial.print("travelling: ");
        Serial.print(payload[hold]);
        Serial.print(" to ");
        Serial.println(payload[hold+1]);
        while (st != (int)payload[hold+1])
        {
          myStepper.step(revolution/20);
          st = location();
          if (st == 20) st = 0;
          if (st == -1) st = 19;
          Serial.print("location: ");
          Serial.println(st);
          displayTime();
          delay(500);
        }
        clockwise = true;
      }
      else
      {
        Serial.print("travelling: ");
        Serial.print(payload[hold]);
        Serial.print(" to ");
        Serial.println(payload[hold+1]);
        while (st != (int)payload[hold+1])
        {
          myStepper.step(-revolution/20);
          st = location();
          if (st == -1) st = 19;
          if (st == 20) st = 0;
          displayTime();
          delay(500);
          Serial.print("location: ");
          Serial.println(st);
        }
        clockwise = false;
      }
      delay(10000); //when finished, give 10 seconds to have the user confirm it
      if (digitalRead(buttonPin) == LOW) 
      {
        hold = hold -1;  //otherwise, try it again
        clockwise = !clockwise; //This will reset it to going the original direction (because otherwise it would be swapped)
      }
      else
      {
        transmission(0x40, 0x02, 0x01);
      }
    }
    lockVerification(); 
  }
  else if (lock == 5)
  {
    //set time 
    //getting correct h, m, s
    long h1 = h;
    long m1 = m;
    long s1 = s;
    unsigned long time = (millis()-mi)/1000 + h1*3600 + m1*60 + s1;
    Serial.println(time);
    int a = (time+4)%60; //Add 4 to account for the fact that Arduino can be a bit slow sometimes
    int b = (time/60)%60;
    int c = time/3600;
    Serial.println(a);
    Serial.println(b);
    Serial.println(c);
    byte payload[] = {0x10, 0x02, 0x01, byte(c), byte(b), byte(a)};
    transmission(payload, sizeof(payload));

    lockVerification();
    }
  else Serial.print("uh oh");
}

int location()
{
  transmission(0x40, 0x01, 0x02); //figure out where we are
  Wire.requestFrom(0x10, 3);
  int ret;
  while (Wire.available()) 
  {
    byte t = Wire.read();
    byte o = Wire.read();
    byte d = Wire.read();
    ret = byteToInt(t)*10 + byteToInt(o); //byteToInt translates ascii into an integer
    Serial.println(ret);
  }
  return ret;
}

void transmission(byte a, byte b, byte c)
{
      Wire.beginTransmission(0x10);
      byte payload[] = {a, b, c};
      Wire.write(payload, sizeof(payload));
      Wire.endTransmission();
      Serial.println();
      Serial.print("Transmitted: ");
      Serial.print(a);
      Serial.print(" ");
      Serial.print(b);
      Serial.print(" ");
      Serial.print(c);
      Serial.print(" ");
}

void transmission(byte payload[], int length)
{
      Wire.beginTransmission(0x10);
      Wire.write(payload, length);
      Wire.endTransmission();
      Serial.print("Payload transmitted: ");
      for (int x = 0; x < length; x++)
      {
        Serial.print(payload[x]);
        Serial.print(" ");
      }
}

int byteToInt(byte b)
{
  switch(b)
  {
    case 0x30: return 0; break;
    case 0x31: return 1; break;
    case 0x32: return 2; break;
    case 0x33: return 3; break;
    case 0x34: return 4; break;
    case 0x35: return 5; break;
    case 0x36: return 6; break;
    case 0x37: return 7; break;
    case 0x38: return 8; break;
    case 0x39: return 9; break;
    default: return -1; break;
  }
}

void displayTime()
{
  int t = (millis()-mi)/1000;
  lcd.setCursor(0, 1);
  int s1 = t%60;
  int m1 = (t/60)%60;
  int h1 = t/3600 + 8;
  if (m1 < 10 && s1 < 10) lcd.print(String(h1) + ":0" + String(m1) + ":0" + String(s1));
  else if (s1 < 10) lcd.print(String(h1) + ":" + String(m1) + ":0" + String(s1));
  else if (m1 < 10) lcd.print(String(h1) + ":0" + String(m1) + ":" + String(s1));
  else lcd.print(String(h1) + ":" + String(m1) + ":" + String(s1));
}

void lockVerification()
{
  transmission(0x01, 0x02, 0x01);
  lock = 0;
  delay(3000);
}

byte IR() {          // takes action based on IR code received
  switch(results.value)
  {
    //credit to the following values: https://www.makerguides.com/ir-receiver-remote-arduino-tutorial/

    case 0xFF6897: Serial.println("0"); return 0x00;   break;
    case 0xFF30CF: Serial.println("1"); return 0x01;   break;
    case 0xFF18E7: Serial.println("2"); return 0x02;   break;
    case 0xFF7A85: Serial.println("3"); return 0x03;   break;
    case 0xFF10EF: Serial.println("4"); return 0x04;   break;
    case 0xFF38C7: Serial.println("5"); return 0x05;   break;
    case 0xFF5AA5: Serial.println("6"); return 0x06;   break;
    case 0xFF42BD: Serial.println("7"); return 0x07;   break;//248 0 243
    case 0xFF4AB5: Serial.println("8"); return 0x08;   break;
    case 0xFF52AD: Serial.println("9"); return 0x09;   break;
    case 0xFFFFFFFF: Serial.println(" ERROR"); return 255; break;  

  default: 
    Serial.print(" other button   ");
    Serial.println(results.value);

  return 255;
  }
}