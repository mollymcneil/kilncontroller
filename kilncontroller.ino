// for LCD: include/replace library LiquidCrystal_V1.2.1.zip from https://bitbucket.org/fmalpartida/new-liquidcrystal/downloads/
//for thermocouple: get library: https://github.com/adafruit/MAX6675-library

#include <Wire.h>  
#include <LiquidCrystal_I2C.h> // Using version 1.2.1
#include <max6675.h>
#include "OnewireKeypad.h"
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

int RELAY_PIN = 9;

int RAMP_PIN = 3;
int SOAK_PIN = 4;
int COOL_PIN = 5;

// keypad

#define Rows 4
#define Cols 4
#define Pin A1
#define Row_Res 4700
#define Col_Res 1000
#define Calc_Res 1000
#define Keyboard_Fudge 3

char KEYS[]= {
  '1','2','3','A',
  '4','5','6','B',
  '7','8','9','C',
  '*','0','#','D'
};
OnewireKeypad <Print, 16> KP2(Serial, KEYS, Rows, Cols, A1, Row_Res, Col_Res, Calc_Res);

unsigned int EEPROM_CORRUPT = 0x0;
unsigned int EEPROM_CHECK = 0x51be;



typedef struct {
  int target;
  int rate;
  int soak_input;
} phase;

typedef enum {
  field_rate,
  field_target,
  field_soak_input
} field;

typedef enum {
  mode_navigate,
  mode_edit,
  mode_none,
  mode_ramp,
  mode_soak,
} mode;

int editing_phase = 0;
int running_phase = 0;
int running_mode = mode_none;
int relay_on = 0;
int consecutive_c = 0;

#define MAX_PHASE 9
phase phases[MAX_PHASE];

int MINIMUM_TEMP = 150;

int current_temp = 0;
int ideal_temp = 0;
int ramp_start_temp = 0;

field editing_field = field_target;
mode editing_mode = mode_navigate;

unsigned long run_phase_start = 0;
unsigned long current_time = 0;
unsigned long current_temp_updated_at = 0;
unsigned long relay_last_toggled_at = 0;

unsigned long phase_get_soak_time(phase *p) {
  unsigned long hours = p->soak_input / 100;
  unsigned long minutes = p->soak_input % 100;
  return hours * HOUR + minutes * MINUTE;
}

bool phase_is_cool(int i, phase *p) {
  return i > 0 && phases[i - 1].target > p->target;
}

void phase_update_target(phase *p, char key, bool first) {
  int digit = key - '0';
  if (digit < 0 || digit > 9) {
    return;
  }

  if (first) {
    p->target = digit;
    return;
  }

  if (p->target < 1000) {
    p->target = p->target * 10 + digit;
  }
}

void phase_update_rate(int i, phase *p, char key, bool first) {
  int digit = key - '0';
  if (digit < 0 || digit > 9) {
     return;
  }

  if (i > 0 && p->target < phases[i - 1].target) {
    digit = 0 - digit;
  }

  if (first) {
    p->rate = digit;
    return;
  }

  if (p->rate < 100 && p->rate > -100) {
    p->rate = p->rate * 10 + digit;
  }
}

void phase_update_soak_input(phase *p, char key, bool first) {
  int digit = key - '0';
  if (digit < 0 || digit > 9) {
    return;
  }

  if (first) {
    p->soak_input = digit;
    return;
  }

  if (p->soak_input < 1000) {
    p->soak_input = p->soak_input * 10 + digit;
  }
}

void phase_header_to_screen(char *buff, size_t size) {
  snprintf(buff, size, "%s", "  TARGET  RATE  HOLD");
}

void phase_to_screen(int i, phase *p, char *buff, size_t size) {
  if (phase_is_cool(i, p) && p->rate == 0) {
    snprintf(buff, size, "%i:%5i\xdf   -%1i\xdf %2i:%02i",
        i + 1, p->target, p->rate, p->soak_input / 100, p->soak_input % 100
    );
    return;
  }

  snprintf(buff, size, "%i:%5i\xdf%5i\xdf %2i:%02i",
      i + 1, p->target, p->rate, p->soak_input / 100, p->soak_input % 100
  );
}

char buff[4][30];

void set_cursor_position(int r, int c) {
}

void current_state_to_screen(char *buff, size_t size) {

  int i = 0;

    i += snprintf(buff, size, "NOW %4i\xdf ", current_temp);

  if (running_mode == mode_none) {
    i += snprintf(buff + i, size - i, "      OFF ");
    return;
  }

  if (running_mode == mode_ramp) {
    if (phase_is_cool(running_phase, &phases[running_phase])) {
      i += snprintf(buff + i, size - i, "COOL%i", running_phase + 1);
    } else {
      i += snprintf(buff + i, size - i, "RAMP%i", running_phase + 1);
    }
  } else if (running_mode == mode_soak) {
    i += snprintf(buff + i, size - i, "HOLD%i", running_phase + 1);
  }

  int time_in_minutes = (current_time - run_phase_start) / MINUTE;
  
  i += snprintf(buff + i, size - i, "%2i:%02i", time_in_minutes / 60, time_in_minutes % 60);
}

void update_display() {

  int phase_offset = editing_phase == 0 ? 0 : editing_phase - 1;
  if (editing_mode == mode_none && running_mode != mode_none) {
    phase_offset = running_phase == 0 ? 0 : running_phase - 1;
  }

  current_state_to_screen(buff[0], sizeof(buff[0]));
  phase_header_to_screen(buff[1], sizeof(buff[1]));
  phase_to_screen(phase_offset, &phases[phase_offset], buff[2], sizeof(buff[2]));
  phase_to_screen(phase_offset + 1, &phases[phase_offset + 1], buff[3], sizeof(buff[3]));

  for (int i = 0; i < 4; i++) {
    lcd->setCursor(0, i);
    lcd->print(buff[i]);
  }

  if (editing_mode == mode_navigate || editing_mode == mode_edit) {
    int cursor_col =
      editing_field == field_target ? 6 :
      editing_field == field_rate ? 12 :
      editing_field == field_soak_input ? 19 :
      0;

    int cursor_row = editing_phase == 0 ? 2 : 3;

    lcd->cursor();
    lcd->noBlink();
    lcd->setCursor(cursor_col, cursor_row);
  } else if (running_mode == mode_ramp || running_mode == mode_soak) {
    lcd->blink();
    lcd->noCursor();
    lcd->setCursor(0, running_phase == 0 ? 2 : 3);
  } else {
    lcd->noCursor();
    lcd->noBlink();
  }
}

void handle_key(char key) {
  Serial.print("Handle key: ");
  Serial.print(key);
  Serial.print(" ");
  Serial.print(analogRead(A1));
  Serial.print("\n");

  if (key != 'C' && key != '0') {
    consecutive_c = 0;
  }

  switch (key) {
  case '0'...'9':

    if (editing_mode == mode_edit || editing_mode == mode_navigate) {

      switch (editing_field) {
      case field_target:
        phase_update_target(&phases[editing_phase], key, editing_mode == mode_navigate);
        break;

      case field_rate:
        phase_update_rate(editing_phase, &phases[editing_phase], key, editing_mode == mode_navigate);
        break;

      case field_soak_input:
        phase_update_soak_input(&phases[editing_phase], key, editing_mode == mode_navigate);
        break;

      }
      editing_mode = mode_edit;
    }
    break;

  case 'A':
  case 'a':

    if (editing_mode == mode_edit || editing_mode == mode_navigate) {
      if (editing_field == field_soak_input) {
        editing_phase = (editing_phase + 1) % MAX_PHASE;
      }

      editing_field = 
        editing_field == field_target ? field_rate :
        editing_field == field_rate ? field_soak_input :
        field_target;

    }

    editing_mode = mode_navigate;

    break;

  case 'B':
  case 'b':

    if (editing_mode == mode_edit || editing_mode == mode_navigate) {
      if (editing_field == field_target) {
        editing_phase = (editing_phase + MAX_PHASE - 1) % MAX_PHASE;
      }

      editing_field =
        editing_field == field_soak_input ? field_rate :
        editing_field == field_rate ? field_target :
        field_soak_input;

    }

    editing_mode = mode_navigate;

    break;

  case 'C':
  case 'c':
    consecutive_c += 1;

    if (consecutive_c == 2) {
      consecutive_c = 0;
      lcd->clear();
      lcd->setCursor(0, 0);
      lcd->print("Resetting...");
      for (int i = 0; i < MAX_PHASE; i++) {
        phases[i].rate = phases[i].target = phases[i].soak_input = 0;
      }
      write_ee_prom();
      delay(1000);
    } else {

      editing_mode = mode_navigate;
      handle_key('0');

    }

    break;

  case 'D':
  case 'd':

    if (running_mode == mode_none) {
      editing_mode = mode_none;
      editing_phase = 0;
      running_mode = mode_ramp;
      running_phase = 0;
      ramp_start_temp = max(MINIMUM_TEMP, current_temp);
      run_phase_start = current_time;
      
      write_ee_prom();
    
    } else {
      editing_mode = mode_navigate;
      editing_phase = 0;
      running_mode = mode_none;
      running_phase = 0;
    }


    break;

  case '*':

    editing_phase = (editing_phase + MAX_PHASE - 1) % MAX_PHASE;
    break;

  case '#':

    editing_phase = (editing_phase + 1) % MAX_PHASE;
    break;
  }

  update_display();
}

bool phase_hit_target(int i, phase *p, int current_temp) {

  if (phase_is_cool(i, p)) {
    return current_temp <= p->target || current_temp <= MINIMUM_TEMP;
  }
  
  return current_temp >= p->target;
}

bool phase_hit_time(phase *p, unsigned long time_difference) {
  return time_difference >= phase_get_soak_time(p);
}

void update_state() {
  
  if (running_mode == mode_none) {
    digitalWrite(RAMP_PIN, 0);
    digitalWrite(COOL_PIN, 0);
    digitalWrite(SOAK_PIN, 0);
    digitalWrite(RELAY_PIN, 0);
    return;
  }

  if (running_mode == mode_ramp) {
    if (phase_hit_target(running_phase, &phases[running_phase], current_temp)) {
      running_mode = mode_soak;
      run_phase_start = current_time;
      update_state();
      return;
    }

    int rate = phases[running_phase].rate;
    
    if (phase_is_cool(running_phase, &phases[running_phase]) && (rate >= 0 || rate == -999)) {
      ideal_temp = phases[running_phase].target;
      
    } else if (!phase_is_cool(running_phase, &phases[running_phase]) && (rate <= 0 || rate == 999)) {
      ideal_temp = phases[running_phase].target;
    } else {
      ideal_temp = ramp_start_temp + (float)phases[running_phase].rate * (float)(current_time - run_phase_start) / (float)HOUR;
    }


  } else if (running_mode == mode_soak) {
    if (phase_hit_time(&phases[running_phase], current_time - run_phase_start)) {
      if (running_phase + 1 >= MAX_PHASE || 
           (phases[running_phase].target == 0 && phases[running_phase].rate == 0 && phases[running_phase].soak_input == 0)) {
        running_mode = mode_none;
        running_phase = 0;
        editing_mode = mode_navigate;
        editing_phase = 0;
        return;
      }

      run_phase_start = millis();
      running_phase += 1;
      running_mode = mode_ramp;
      ramp_start_temp = max(MINIMUM_TEMP, current_temp);
      run_phase_start = current_time;

      update_state();
      return;
    }

    ideal_temp = phases[running_phase].target;
  }

  if (ideal_temp < MINIMUM_TEMP) {
    ideal_temp = MINIMUM_TEMP;
  }

  digitalWrite(RAMP_PIN, running_mode == mode_ramp && !phase_is_cool(running_phase, &phases[running_phase]));
  digitalWrite(COOL_PIN, running_mode == mode_ramp && phase_is_cool(running_phase, &phases[running_phase]));
  digitalWrite(SOAK_PIN, running_mode == mode_soak);

  Serial.print("current: ");
  Serial.print(current_temp);
  Serial.print(" ideal: ");
  Serial.print(ideal_temp);
  Serial.print("\n");

  if (current_time - relay_last_toggled_at > 10000) {
    if (ideal_temp > current_temp && !relay_on) {
      relay_on = true;
      relay_last_toggled_at = current_time;
      digitalWrite(RELAY_PIN, 1);
    }
    if (ideal_temp < current_temp && relay_on) {
      relay_on = false;
      digitalWrite(RELAY_PIN, 0);
      relay_last_toggled_at = current_time;
    }
  }
}

void read_ee_prom() {
  int check = 0;
  int eeAddress = 0;

  EEPROM.get(eeAddress, check);
  eeAddress += sizeof(check);
  if (check != EEPROM_CHECK) {
    return;
  }

  for (int i = 0; i < MAX_PHASE; i++) {
    EEPROM.get(eeAddress, phases[i]);
    eeAddress += sizeof(phases[i]);
  }
}

void write_ee_prom() {
  int eeAddress = 0;
  
  eeAddress += sizeof(EEPROM_CHECK);

  for (int i = 0; i < MAX_PHASE; i++) {
    EEPROM.put(eeAddress, phases[i]);
    eeAddress += sizeof(phases[i]);
  }

  EEPROM.put(0, EEPROM_CHECK);
}

void setup() {
  // put your setup code here, to run once:

  lcd->begin(20, 4);
  lcd->setCursor(0,0); // first character - 1st line
  lcd->print("Trippnwyk Kiln!");
  lcd->setCursor(14, 3);
  lcd->print("(v0.2)");

  KP2.SetDebounceTime(30);
  KP2.SetFudgeFactor(Keyboard_Fudge);
  Serial.begin(9600);

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, 0);
  
  pinMode(SOAK_PIN, OUTPUT);
  pinMode(RAMP_PIN, OUTPUT);
  pinMode(COOL_PIN, OUTPUT);

  digitalWrite(SOAK_PIN, 1);
  digitalWrite(RAMP_PIN, 1);
  digitalWrite(COOL_PIN, 1);

  read_ee_prom();
  
  delay(1500); // Allow max to initialize? (and display to say hi!)

  digitalWrite(SOAK_PIN, 0);
  digitalWrite(RAMP_PIN, 0);
  digitalWrite(COOL_PIN, 0);
  
  current_temp = ktc.readFahrenheit();
  update_display();
  Serial.println("Hello! ");
}

void loop() {
  
  current_time = millis();
   
  if (KP2.Getkey() && KP2.Key_State() == PRESSED) {
     handle_key(KP2.Getkey());
  }

  if ((current_time - current_temp_updated_at) > 1000) {
    current_temp_updated_at = current_time;
    current_temp = ktc.readFahrenheit();
    update_state();
    update_display();
  }
}
