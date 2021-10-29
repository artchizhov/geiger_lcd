#include <LiquidCrystal.h>


#define LOG_PERIOD        3000  // mSec
#define LCD_UPD_PERIOD    1000  // mSec
#define ACCUM_PERIOD      600   // sec

#define TUBE_K_R          0.81            // CPM to uR/h
#define WARN_THRESHOLD    50              // uR/h
//#define TUBE_K_SV        TUBE_K_R / 100  // CPM to uSv/h

#define EXPO_FILTER_K     0.1   // exponential smoothing k

#define LCD_BACKLIGHT_PIN 3     // pin for control lcd backlight
#define LCD_BL_PERIOD     500   // mSec


// initialize the library by associating any needed LCD interface pin
// with the arduino pin number it is connected to
const int rs = 8;
const int en = 9;
const int d4 = 4;
const int d5 = 5;
const int d6 = 6;
const int d7 = 7;

LiquidCrystal lcd(rs, en, d4, d5, d6, d7);


// variable for GM Tube events INT - use in interrupt, not for calculation!
unsigned long INT_counts_live;    // for live calc (reset to zero every period of measure)
unsigned long INT_counts_accum;   // accumulate all counts from start

// variable for CPM
unsigned long cpm;

unsigned long runTime;
unsigned long nextPeriodTime;
unsigned long nextLCDUpdTime;
unsigned long nextLCDBlTime;    // LCD Backlight Time

unsigned long startMillis;      // variable for time measurement

float cpm_multiplier;   // multiplier for calc counts per minute (from counts per period)

bool f_trig;    // trigger at geiger tube count
bool f_warn;    // for warning if radiation so high
bool f_finish;  // run or finish (at run - accumulate count at ACCUM_PERIOD)
bool f_lcdbl;   // lcd backlight state


// INTerrupt at pin 2 falling
// subprocedure for capturing events from Geiger Kit
void tube_impulse() {
  INT_counts_accum++;
  INT_counts_live++;
  
  f_trig = true;
}


void setup() {
  // set default vars
  INT_counts_accum = 0;
  INT_counts_live = 0;

  cpm = 0;

  runTime         = 0;
  nextPeriodTime  = 0;
  nextLCDUpdTime  = 0;
  nextLCDBlTime   = 0;

  // set flags
  f_trig    = false;    // no inpulses at start
  f_warn    = false;    // no warning at start
  f_finish  = false;    // no finish at start
  
  f_lcdbl   = true;     // lcd backlight is ON at start

  // multiplier for calculate: counts per period -> counts per minute
  // (1000 ms in 1 second; 60 second in 1 minute)
  cpm_multiplier = (float)60000 / (float)LOG_PERIOD;

  // configure pin for LCD Backlight control
  setLcdBl(f_lcdbl);                    // at start is ON
  pinMode(LCD_BACKLIGHT_PIN, OUTPUT);   // pin is OUT

  // init serial connection
  Serial.begin(57600);
  Serial.println("Time;Counts;CPM");

  // init lcd
  lcd.begin(16, 2);   // set up the LCD's number of columns and rows
  printFrame();

  // get "start" millis before run
  startMillis = millis();
  
  attachInterrupt(0, tube_impulse, FALLING); //define external interrupts
}

void setLcdBl(bool state) {
  if (state) {
    digitalWrite(LCD_BACKLIGHT_PIN, LOW); // backlight is ON
  } else {
    digitalWrite(LCD_BACKLIGHT_PIN, HIGH); // backlight is OFF
  }
}

void printFrame() {
  // time
  lcd.setCursor(0, 0);
  lcd.print("t");

  // counts
  lcd.setCursor(6, 0);
  lcd.print("c");

  // cpm
  lcd.setCursor(4, 1);
  lcd.print("cpm");

  // uR/h
  lcd.setCursor(12, 1);
  lcd.print("uR/h");
}

void printTime(unsigned int t) {
  lcd.setCursor(1, 0);
  lcd.print(t);
}

void printCounts(unsigned long c) {
  lcd.setCursor(7, 0);
  lcd.print(c);
}

// print c/m (cpm)
void printCpm(unsigned long val) {
  lcd.setCursor(0, 1);

  // print first spaces, example:
  // val = 15, will print: "  15"
  if (val < 10) {
    lcd.print("   ");
  } else if (val < 100) {
    lcd.print("  ");
  } else if (val < 1000) {
    lcd.print(" ");
  }
  
  lcd.print(val);
}

// print uR/h
void printRph(unsigned long val) {
  lcd.setCursor(8, 1);

  // print first spaces, example:
  // val = 15, will print: "  15"
  if (val < 10) {
    lcd.print("   ");
  } else if (val < 100) {
    lcd.print("  ");
  } else if (val < 1000) {
    lcd.print(" ");
  }

  lcd.print(val);
}

void printWarn(bool w) {
  lcd.setCursor(15, 0);
  
  if (w) {
    lcd.print("!");   // WARNING!
  } else {
    lcd.print(" ");   // normal
  }
}

void loop() {
  // copy counts to local variable for calc
  unsigned long cnts_lv = INT_counts_live;
  unsigned long cnts_acm = INT_counts_accum;

  // get current time
  unsigned long currentMillis = millis();
  
  // calc running time (from start)
  runTime = currentMillis - startMillis;  // in mSec
  
  unsigned int runTimeSec = round(runTime / 1000);     // in seconds

  if (!f_finish and f_trig) {
    f_trig = false;
    // Сначала сбрасываем триггер, обозначая что событие принято и будет выведено
    // потом отсылаем на экран.
    // Если во время вывода на экран снова будет зафиксировано срабатывание, триггер снова будет установлен.
    // Если сделать наоборот (сначала вывод а потом сброс триггера) то происходит ситуация:
    // когда новые срабатывания зафиксированы во время вывода на экран прошлого значения,
    // но триггер будет сброшен принудительно после окончания вывода старых значений,
    // и новые значения не будут отображаться до следующего срабатывания и установки триггера.
    printCounts(cnts_acm);
  }

  // if running time equal or more than expected time for new calculation
  if (runTime >= nextPeriodTime) {
    // recalculate data and show

    // Exponential smoothing:
    // S_t = a*p + (1-a)*S_(t-1)
    // a    - EXPO_FILTER_K
    // p    - cnts_lv * cpm_multiplier
    // S_t  - cpm (counts per minute, c/m)
    cpm = round(EXPO_FILTER_K * (cnts_lv * cpm_multiplier) + (1 - EXPO_FILTER_K) * (float)cpm);

    // micro roentgen per hour (uR/h)
    unsigned long rph = round(cpm * TUBE_K_R);

    // print data to display
    printCpm(cpm);
    printRph(rph);

    // check threshold value of radiation
    // if high then turn on "Warning"
    if (rph > WARN_THRESHOLD) {
      f_warn = true;
    } else {
      f_warn = false;
    }
    printWarn(f_warn);

    // reset for new period.
    INT_counts_live = 0;

    // set new time for wait it
    nextPeriodTime = runTime + LOG_PERIOD;
  }

  // if running time equal or more than expected time for update LCD (every 1 seconds)
  if (runTime >= nextLCDUpdTime) {
    // update lcd
    
    if (!f_finish) {
      printTime(runTimeSec);
    }

    // send "Time;CPM" by serial port
    Serial.print(runTimeSec);
    Serial.print(";");
    Serial.print(cnts_acm);
    Serial.print(";");
    Serial.println(cpm);
    
    // set new time for wait it
    nextLCDUpdTime = runTime + LCD_UPD_PERIOD;
  }

  // enable blinking lcd backlight when warning
  if (f_warn) {
    if (runTime >= nextLCDBlTime) {
      // change state flag of LCD Backlight
      f_lcdbl = !f_lcdbl;

      // do change
      setLcdBl(f_lcdbl);

      // set new time for wait it
      nextLCDBlTime = runTime + LCD_BL_PERIOD;
    }
  } else {
    // FIXME: run ones
    f_lcdbl = true;
    
    setLcdBl(f_lcdbl);
  }

  if (!f_finish and runTimeSec >= ACCUM_PERIOD) {
    f_finish = true;
  }
}
