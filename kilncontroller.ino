// for LCD: include/replace library LiquidCrystal_V1.2.1.zip from https://bitbucket.org/fmalpartida/new-liquidcrystal/downloads/
//for thermocouple: get library: https://github.com/adafruit/MAX6675-library


#include <Wire.h>  
#include <LiquidCrystal_I2C.h> // Using version 1.2.1
#include "max6675.h"
 
// The LCD constructor - address shown is 0x27 - check it
LiquidCrystal_I2C lcd(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  

//thermocouple
int ktcSO = 8;
int ktcCS = 9;
int ktcCLK = 10;

MAX6675 ktc(ktcCLK, ktcCS, ktcSO);


void setup()
{
  Serial.begin(9600);
  // give the MAX a little time to settle
  delay(500);
  
  lcd.begin(20,4); // trying 20 characters over 4 lines
  lcd.backlight();
  
  lcd.setCursor(0,0); // first character - 1st line
  lcd.print("Hello World!");
  // 8th character - 2nd line 
  lcd.setCursor(8,2);
  lcd.print("----boo-----");
  lcd.print(ktc.readCelsius());
  lcd.print("\t Deg C ");

  Serial.print("Deg C = "); 
   Serial.print(ktc.readCelsius());
   Serial.print("\t Deg F = ");
   Serial.println(ktc.readFahrenheit());
 
   delay(500);
}
 
 
void loop()
{
}

