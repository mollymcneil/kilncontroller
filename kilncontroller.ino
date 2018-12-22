// for LCD: include/replace library LiquidCrystal_V1.2.1.zip from https://bitbucket.org/fmalpartida/new-liquidcrystal/downloads/
//for thermocouple: get library: https://github.com/adafruit/MAX6675-library

#include <Wire.h>  
#include <LiquidCrystal_I2C.h> // Using version 1.2.1
#include <max6675.h>
#include <OnewireKeypad.h>
#include <EEPROM.h>

#define DEBUG 1

unsigned long MINUTE = (long)60 * 1000;
unsigned long HOUR = MINUTE * 60;

// The LCD constructor - address shown is 0x27 - check it
LiquidCrystal_I2C lcdI2C(0x27, 2, 1, 0, 4, 5, 6, 7, 3, POSITIVE);  
LCD *lcd = &lcdI2C;

//thermocouple
int ktcSO = 10;
int ktcCS = 11;
int ktcCLK = 12;

MAX6675 ktc(ktcCLK, ktcCS, ktcSO);

int relayPin = 9;

// keypad

#define Rows 4
#define Cols 4
#define Pin A1
#define Row_Res 4700
#define Col_Res 1000
char KEYS[]= {
  '1','2','3','A',
  '4','5','6','B',
  '7','8','9','C',
  '*','0','#','D'
};
OnewireKeypad <Print, 16> KP2(Serial, KEYS, Rows, Cols, A1, Row_Res, Col_Res);

unsigned int EEPROM_CHECK = 0xab1e;

// logic
unsigned int target1 = 1550;
unsigned int rate1 = 300;
unsigned int soak1_input = 0;
unsigned long soak1 = 0;

unsigned int target2 = 0;
unsigned int rate2 = 0;
unsigned int soak2_input = 0;
unsigned long soak2 = 0;

enum inputs {
  input_target1,
  input_rate1,
  input_soak1,
  input_target2,
  input_rate2,
  input_soak2
};

enum states {
  state_init,
  state_navigating,
  state_editing,
  state_error
};

enum phases {
  phase_off,
  phase_ramp1,
  phase_ramp2,
  phase_soak1,
  phase_soak2
};

int current_input = input_target1;
int current_state = state_init;
int current_phase = phase_off;

unsigned long started_at = 0;

unsigned long display_updated_at = 0;
unsigned long current_time = 0;

double current_temp = 0;
long long current_temp_updated_at = 0;

double start_temp = 0;
double ideal_temp = 0;
int relay_on = 0;

void setup() {

  lcd->begin(20, 4);
  lcd->setCursor(0,0); // first character - 1st line
  lcd->print("Trippnwyk Kiln!");

  KP2.SetDebounceTime(50);
  Serial.begin(9600);

  pinMode(relayPin, OUTPUT);
  digitalWrite(relayPin, 0);

  readEEProm();

  Serial.print("Hello! "); Serial.println(analogRead(A2));

  soak1 = timeInputToTime(soak1_input);
  soak2 = timeInputToTime(soak2_input);
  
  delay(1500); // Allow max to initialize? (and display to say hi!)
  current_temp = ktc.readFahrenheit();
  current_state = state_navigating;
  renderDisplay();
    Serial.print("Hello! "); Serial.println(analogRead(A2));

    for ( uint8_t i = 0, R = 4 - 1, C = 4 - 1; i < 16; i++ ) {
      float V = (5.0 * float( Col_Res )) / (float(Col_Res) + (float(Row_Res) * float(R)) + (float(Col_Res) * float(C)));
      float Vfinal = V * (1015 / 5.0);

      Serial.print(KEYS[4 * R + C]); Serial.print(": ");
      Serial.println(Vfinal);

      if ( C == 0 ) {
        R--;
        C = 4 - 1;
      } else { C--;}
    }


}

void readEEProm() {
  int check = 0;

  int eeAddress = 0;
  
  EEPROM.get(0, check);
  EEPROM.get(2, target1);
  EEPROM.get(4, target2);
  EEPROM.get(6, rate1);
  EEPROM.get(8, rate2);
  EEPROM.get(10, soak1_input);
  EEPROM.get(12, soak2_input);

  if (check != EEPROM_CHECK || target1 > 9999 || target2 > 9999 || rate1 > 999 || rate2 > 999 || soak1_input > 9999 || soak2_input > 9999) {
    target1 = target2 = rate1 = rate2 = soak1_input = soak2_input = 0;
  }
}

void writeEEProm() {
  EEPROM.put(0, EEPROM_CHECK);
  EEPROM.put(2, target1);
  EEPROM.put(4, target2);
  EEPROM.put(6, rate1);
  EEPROM.put(8, rate2);
  EEPROM.put(10, soak1_input);
  EEPROM.put(12, soak2_input);
  readEEProm();
}

void renderDisplay() {
  if (current_state == state_error) {
    return;
  }
  lcd->setCursor(0, 0);

  lcd->print("AT "); printTarget(current_temp);
  
  switch (current_phase) {
  case phase_off:
    lcd->print("        OFF ");
    break;
  case phase_ramp1:
    lcd->print(" RAMP1 "); printTime(current_time - started_at);
    break;
  case phase_ramp2:
    lcd->print(" RAMP2 "); printTime(current_time - started_at);
    break;
  case phase_soak1:
    lcd->print(" SOAK1 "); printTime(current_time - started_at);
    break;
  case phase_soak2:
    lcd->print(" SOAK2 "); printTime(current_time - started_at);
    break;
  
  }

  lcd->setCursor(0, 1);
  lcd->print("  TARGET  RATE  SOAK");
  lcd->setCursor(0, 2);
  lcd->print("1: "); printTarget(target1); lcd->print("  "); printRate(rate1); lcd->print(" "); printTimeInput(soak1_input);

  lcd->setCursor(0, 3);
  lcd->print("2: "); printTarget(target2); lcd->print("  "); printRate(rate2); lcd->print(" "); printTimeInput(soak2_input);
  
  renderCursor();
  
  display_updated_at = current_time;
}

void renderCursor() {

  if (current_state == state_editing || current_state == state_navigating) {
    
    lcd->cursor();
  
    switch (current_input) {
    case input_target1:
      lcd->setCursor(6, 2);
      break;
    case input_target2:
      lcd->setCursor(6, 3);
      break;
    case input_rate1:
      lcd->setCursor(12, 2);
      break;
    case input_rate2:
      lcd->setCursor(12, 3);
      break;
    case input_soak1:
      lcd->setCursor(19, 2);
      break;
    case input_soak2:
      lcd->setCursor(19, 3);
      break;
    default:
      lcd->setCursor(0, 0);
      lcd->print("Error: invalid state");
      lcd->setCursor(0, 1);
      lcd->print("current_input: ");
      lcd->print(current_input);
      current_state = state_error;
    }
  } else {
    lcd->noBlink();
    lcd->noCursor();
  }

}

// 5 chars "1550°" or "   0°"
void printTarget(double temp) {
  static char str[6];
  sprintf(str, "%4i\xdf", (int)temp);
  lcd->print(str);
}

// 4 chars "1550°" or "   0°"
void printRate(double temp) {
  static char str[5];
  sprintf(str, "%3i\xdf\x00", round(temp));
  lcd->print(str);
}

// 5 chars "12:30" or " 1:30"
void printTime(unsigned long ms) {
  unsigned long minutes = ms / MINUTE;
  static char str[6];
  sprintf(str, "%2lu:%02lu", minutes / 60, minutes % 60);
  lcd->print(str);
}

// 5 chars "12:30" or " 1:30"
void printTimeInput(int timeInput) {
  static char str[6];
  sprintf(str, "%2i:%02i", timeInput / 100, timeInput % 100);
  lcd->print(str);
}
 
void loop() {
  if (current_state == state_error) {
    return;
  }
  
  current_time = millis();
   
  if (KP2.Getkey() && KP2.Key_State() == PRESSED) {
     handleKey(KP2.Getkey());
  }

  if ((current_time - current_temp_updated_at) > 1000) {
    current_temp_updated_at = current_time;
    current_temp = ktc.readFahrenheit();

change_phase:
    switch (current_phase) {
    case phase_off:
      ideal_temp = 0;
      break;

    case phase_ramp1:
      if (current_temp >= target1) {
        started_at = current_time;
        current_phase = phase_soak1;
        goto change_phase;
      }
      if (rate1 <= 0) {
        ideal_temp = target1;
      } else {
        ideal_temp = start_temp + 25 +rate1 * (double)(current_time - started_at) / HOUR;
        if (ideal_temp > target1) {
          ideal_temp = target1;
        }
        Serial.print("TIME ");
        Serial.print(current_time - started_at);
        Serial.print(" ideal ");
        Serial.println(ideal_temp);
      }
      
      break;
    case phase_ramp2:
      if (current_temp >= target2) {
        started_at = current_time;
        current_phase = phase_soak2;
        goto change_phase;
      }
      if (rate2 <= 0) {
        ideal_temp = target2;
      } else {
        ideal_temp = target1 + 25 + rate1 * (double)(current_time - started_at) / HOUR;
        if (ideal_temp > target2) {
          ideal_temp = target2;
        }
      }

      break;
    case phase_soak1:
      if (current_time - started_at >= soak1) {
        started_at = current_time;
        current_phase = phase_ramp2;
        goto change_phase;
      }
      ideal_temp = target1;
      
      break;
  
    case phase_soak2:
     if (current_time - started_at >= soak2) {
        started_at = current_time;
        current_phase = phase_off;
        goto change_phase;
      }
      ideal_temp = target2;
      
      break;
    }
    if (false) {
      Serial.print("Phase: ");
      Serial.print(current_phase);
      Serial.print(" Current: ");
      Serial.print(current_temp);
      Serial.print(" Ideal: ");
      Serial.print(ideal_temp);
      Serial.print(" Relay: ");
      Serial.print(relay_on);
      Serial.print("\n");
    }

    if (current_temp < ideal_temp && !relay_on) {
      relay_on = true;
      digitalWrite(relayPin, 1);
      Serial.println("RELAY ON");
    } else if (current_temp > ideal_temp && relay_on) {
      relay_on = false;
      digitalWrite(relayPin, 0);
      Serial.println("RELAY OFF");
    }

    renderDisplay(); 
  }


}

void readFirstDigit(char key) {
  int value = key - '0';
  if (value < 0 || value > 9) {
      lcd->setCursor(0, 0);
      lcd->print("Error: invalid digit: ");
      lcd->print(key);
      lcd->print(value);
      current_state = state_error;
      return;
  }
  
  switch (current_input) {
    case input_target1:
      target1 = value;
      break;
    case input_target2:
      target2 = value;
      break;
    case input_rate1:
      rate1 = value;
      break;
    case input_rate2:
      rate2 = value;
      break;
    case input_soak1:
      soak1_input = value;
      soak1 = timeInputToTime(soak1_input);
      break;
    case input_soak2:
      soak2_input = value;
      soak2 = timeInputToTime(soak2_input);
      break;
  }
}

unsigned long timeInputToTime(int input) {
  unsigned long hours = input / 100;
  unsigned long minutes = input % 100;
  return hours * HOUR + minutes * MINUTE;
}

void readNextDigit(char key) {
  int value = key - '0';
  if (value < 0 || value > 9) {
      lcd->setCursor(0, 0);
      lcd->print("Error: invalid digit: ");
      lcd->print(key);
      current_state = state_error;
      return;
  }
  
  switch (current_input) {
    case input_target1:
      if (target1 < 1000) {
        target1 = 10 * target1 + value;
      }
      break;
    case input_target2:
      if (target2 < 1000) {
        target2 = 10 * target2 + value;
      }
      break;
    case input_rate1:
      if (rate1 < 100) {
        rate1 = 10 * rate1 + value;
      }
      break;
    case input_rate2:
      if (rate2 < 100) {
        rate2 = 10 * rate2 + value;
      }
      break;
    case input_soak1:
      if (soak1_input < 1000) {
        soak1_input = 10 * soak1_input + value;
        soak1 = timeInputToTime(soak1_input);
      }
      break;
    case input_soak2:
      if (soak2_input < 1000) {
        soak2_input = 10 * soak2_input + value;
        soak2 = timeInputToTime(soak2_input);
      }
      break;
  }
}
void startRamp1() {
  writeEEProm();
  current_state = state_init;
  started_at = current_time;
  start_temp = current_temp;
  current_phase = phase_ramp1;
}

void handleKey(char key) {

  if (DEBUG) {
    Serial.print(analogRead(A1));
    Serial.print(" Key: ");
    Serial.print(key);
    Serial.print(" State: ");
    Serial.print(current_state);
    Serial.print(" Input: ");
    Serial.print(current_input);
  }
  
  switch (current_state) {
  case state_init:

    switch (key) {
      case 'A':
        current_input = input_target1;
        current_state = state_navigating;
        break;
      case 'B':
        current_input = input_soak1;
        current_state = state_navigating;
        break;
      
      case 'C':
        break;
      case 'D':
        startRamp1();
        break;
        
      case '*':
      case '#':
      case '0' ... '9':

        // TODO
        break;
      default:
        goto invalid_key;
        break;
    }

    break;

  case state_navigating:
    switch (key) {
      case 'A':
        current_input = (current_input + 1) % 6;
        break;
      case 'B':
        current_input = (current_input + 5) % 6;
        break;

      case 'C':
        readFirstDigit('0');
        break;
        
      case 'D':
        startRamp1();
        break;
      case '*':
      case '#':
        // TODO
        break;
        
      case '0' ... '9':
        readFirstDigit(key);
        current_state = state_editing;
        break;

      default:
        goto invalid_key;
        break;
    }
    
    break;

  case state_editing:
    switch (key) {
      case 'A':
        current_input = (current_input + 1) % 6;
        current_state = state_navigating;
        break;
      case 'B':
        current_input = (current_input + 5) % 6;
        current_state = state_navigating;
        break;

      case 'C':
        readFirstDigit('0');
        current_state = state_editing;
        break;
        
      case 'D':
        startRamp1();
        break;
        
      case '*':
      case '#':
        // TODO
        break;
        
      case '0' ... '9':
        readNextDigit(key);
        break;

      default:
        goto invalid_key;
        break;
    }
    
    break;
 
  case state_error:
    // TODO: add a way to reset?
    break;
  default:
    lcd->setCursor(0, 0);
    lcd->print("Error: invalid state");
    lcd->setCursor(0, 1);
    lcd->print("current_state: ");
    lcd->print(current_state);
    current_state = state_error;
  }

  renderDisplay();

  if (DEBUG) {
    Serial.print(" ->");
    Serial.print(" State: ");
    Serial.print(current_state);
    Serial.print(" Input: ");
    Serial.print(current_input);
    Serial.print("\n");
  }
  return;

invalid_key:
    lcd->setCursor(0, 0);
    lcd->print("Error: invalid key: ");
    lcd->print(key);
    current_state = state_error;
    renderDisplay();
    return;
}

