#include <limits.h>
#ifdef MOCK
#include "mock.h"
#else
#include <avr/wdt.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h> // by Frank de Brabander
#include <DHT.h>
#endif

//#define EEPROM
//#define EEPROM_DUMP

#ifdef EEPROM
#include <EEPROM.h>
#endif


// i/o pins:
#define SWITCH_PIN 2
#define HEATER_PIN 3
#define FAN_PIN    4
#define INSENSOR_PIN  7
#define OUTSENSOR_PIN 8

#define MIN_VENT_TEMP  35
#define MIN_VENT_TIME  10
#define CYCLE_TIME      120
#define MIN_DRYING_TIME 600
#define OBSERVE_TIME   300

#define FULLVENT_MIN_TEMP 40
#define FULLVENT_HUM_OFFSET 15

enum {
   OFF,
   HEAT,
   VENT,
   OBSERVE,
   DONE, /* laundry is dry, wait for turn-off */
} state = OFF;

const char* statestring[] = {
    "Avst\xe1ngd",
    "V\xe1rmer",
    "Ventilerar",
    "Slutm\xe1ter",
    "Klar"
};

enum { IN=0, OUT };

static int countdown = 0;
static int humidity[2] = {0, 0};
static float temp[2] = {0, 0};
static int global_time = 0;
static int cold_start = 1;
static int fan_on = 0;
static int heat_on = 0;
static int restart_count = 0;
static int observe_end = 0;

const float kuniv   = 8.31447215;    // universal gas constant, J mol-1 K-1
const float MH2O    = 18.01534;      // molar mass of water, g mol-1
const float Mdry    = 28.9644;       // molar mass of dry air, g mol-1

// global device inits
DHT insensor(INSENSOR_PIN, DHT22);
DHT outsensor(OUTSENSOR_PIN, DHT22);
DHT* sensors[2] = { &insensor, &outsensor };
LiquidCrystal_I2C lcd(0x27,16,2);  // i2c address to 0x27, 16x2 chars display

// H2O saturation pressure from Lowe & Ficke, 1974
float h2opsat(float t)
{
    float pwat = 6.107799961 +
        t*(4.436518521e-1 +
            t*(1.428945805e-2 +
                t*(2.650648471e-4 +
                    t*(3.031240396e-6 +
                        t*(2.034080948e-8 +
                            t*6.136820929e-11)))));
    float pice = 6.109177956 +
        t*(5.034698970e-1 +
            t*(1.886013408e-2 +
                t*(4.176223716e-4 +
                    t*(5.824720280e-6 +
                        t*(4.838803174e-8 +
                            t*1.838826904e-10)))));
    return pwat < pice ? pwat : pice;
}

int rh2sh(float temp, float rh)
{
    float ap = 1013; // hPa air pressure

    float psat = h2opsat(temp);
    float ph2o = psat * rh / 100;
    float vmr = ph2o / ap;
    float spc = vmr * MH2O/(vmr*MH2O +(1.0-vmr) * Mdry);

    return spc * 1000;
}

static void reset_lcd()
{
   /* for display robustness, reset lcd after every relay throw */
   delay(200); /* wait briefly for power-on pulse to fade */
   wdt_reset(); /* pat watchdog */
   lcd.init();
   lcd.backlight();
}

static void heater(int on)
{
  heat_on = on;
  digitalWrite(HEATER_PIN, on ? HIGH : LOW);
}

static void fan(int on)
{
  fan_on = on;
  digitalWrite(FAN_PIN, on ? HIGH : LOW);
}

static int switched_on(void)
{
  return !digitalRead(SWITCH_PIN); /* active low */
}

#ifdef EEPROM

static void pwrite(int addr, int val)
{
  /* only write if value changed */
  if (EEPROM.read(addr) != val)
    EEPROM.write(addr, val);
}

static void eeprom_log(void)
{
   static int address = 2;
   if (state == OFF) {
     address = 2;
     return;
   }

   /* don't wrap */
   if (address > 1022)
     return;

   int val1 = temp[IN];
   if (heat_on)
      val1 |= 0x80;

   int val2 = humidity[IN];
   if (fan_on)
      val2 |= 0x80;

   pwrite(address++, val1);
   pwrite(address++, val2);
   pwrite(0, (address-2) >> 8);
   pwrite(1, (address-2) & 0xff);
}

static void eeprom_dump(void)
{
  int i;
  int count = (EEPROM.read(0) << 8) | EEPROM.read(1);
  if (count > 1022)
    count = 1022;
  char line[40];

  for (i=0; i < count; i+=2
  ) {
     int val1 = EEPROM.read(i + 2);
     int val2 = EEPROM.read(i + 3);
     sprintf(line, "%d,%d,%d,%d,%d\n", i, val1 & 0x7f, val1 >> 7, val2 & 0x7f
     , val2 >> 7);
     Serial.print(line);
  }
}
#endif

static void readsensor(void)
{
  int i;

  for (i=0; i<2; i++) {
    DHT* s = sensors[i];
    float rh = s->readHumidity();
    temp[i] = s->readTemperature();
    humidity[i] = rh2sh(temp[i], rh);
  }
}

static void display(void)
{
  char line[20];
  char buf[2][9];
  int i;

  for (i=0; i<2; i++) {
      snprintf(buf[i], sizeof(buf[i]), "%dg %d\337", humidity[i],
               (int)(temp[i] + 0.5));
  }
  sprintf(line, "%-8s%8s", buf[0], buf[1]);
  lcd.home();
  lcd.print(line);

  switch (state)
  {
      case OBSERVE: {
          int remain = observe_end - global_time;
          sprintf(line, "%-11s%dm%2ds", statestring[state],
                  remain / 60, remain % 60);
          break;
      }

      default:
          sprintf(line, "%-11s%dt%2dm", statestring[state],
                  global_time / 3600, (global_time % 3600) / 60);
          break;
  }

  lcd.setCursor(0,1);
  lcd.print(line);
}

void setup(void)
{
  pinMode(HEATER_PIN, OUTPUT);
  digitalWrite(HEATER_PIN, LOW);
  pinMode(FAN_PIN, OUTPUT);
  digitalWrite(FAN_PIN, LOW);

  pinMode(SWITCH_PIN, INPUT);
  digitalWrite(SWITCH_PIN, HIGH); /* enable pull-up */

  /* if the power switch is on, this was a watchdog reset.
     if so, run a parameter check rather than start heating */
  if (switched_on()) {
     cold_start = 0;
     state = HEAT;
     countdown = 0;
     global_time = MIN_DRYING_TIME;
  }

  lcd.init();
  lcd.backlight();
  lcd.print("Startar...");
  lcd.setCursor(0,1);
  lcd.print(__DATE__);

#ifdef EEPROM_DUMP // <-- set to 1 to dump eeprom log
  lcd.print("SERIAL");
  delay(2000);
  Serial.begin(115200);
  eeprom_dump();
  while(1);
#endif

  sensors[0]->begin();
  sensors[1]->begin();
  delay(2000); /* give the DHT22s time to warm up */

  wdt_enable(WDTO_4S); /* enable watchdog, 4s timeout */
}

int x = 0;

/*
void testdisplay(void)
{
   int i;
   lcd.home();
   char buf[24];
   sprintf(buf, "%02x", x);
   lcd.print(buf);
   for (int i=0; i<16; i++) {
       if (x+i);
         buf[i] = x + i;
   }
   lcd.setCursor(0,1);
   lcd.print(buf); 
   x += 16;
}
*/

void start(void)
{
    fan(0);
    heater(1);
    state = HEAT;
    countdown = CYCLE_TIME;
    reset_lcd();
    global_time = 0;
    restart_count = 0;
}

void loop(void)
{
    static int vent_time = 0;

#ifdef EEPROM
    testdisplay();
    delay(2000);
    wdt_reset(); /* pat watchdog */
    return;
#endif

    wdt_reset(); /* pat watchdog */

    if ((global_time % 3) == 0) {
        static int readcount = 0;
        readsensor();
        if (++readcount == 2) {
            // reading is slooow
            if (state > OFF && state < DONE)
                global_time++;
            readcount = 0;
        }

#ifdef EEPROM
        if ((global_time % 15) == 0)
            eeprom_log();
#endif
    }

    if (!countdown) {
        /* are we done? */
        if ((state && state < OBSERVE) &&
            (global_time >= MIN_DRYING_TIME) &&
            (humidity[IN] <= humidity[OUT]))
        {
            fan(0);
            heater(0);
            state = OBSERVE;
            countdown = 5;
            observe_end = global_time + OBSERVE_TIME;
        }

        /* change state? */
        switch (state) {

            case OFF:
                if (switched_on()) {
                    start();
                }
                break;

            case HEAT:
                /* don't vent until air is at least a little warm */
                if (temp[IN] < MIN_VENT_TEMP) {
                    countdown = CYCLE_TIME;
                    break;
                }

                /* go to next state: VENT */
                fan(1);
                //heater(0);
                state = VENT;
                vent_time = CYCLE_TIME * humidity[IN] / 200;
                if (vent_time < MIN_VENT_TIME)
                   vent_time = MIN_VENT_TIME;
                countdown = vent_time;
                reset_lcd();
                break;

            case VENT:
                if ((temp[IN] > FULLVENT_MIN_TEMP) &&
                    (humidity[IN] > humidity[OUT]+FULLVENT_HUM_OFFSET))
                {
                    /* in fullvent mode, keep ventilating until
                       limits are reached */
                    countdown = 5;
                    break;
                }

                /* go to next state: HEAT */
                fan(0);
                //heater(1);
                state = HEAT;
                countdown = CYCLE_TIME - vent_time;
                reset_lcd();
                break;

            case OBSERVE:
                /* After cycle is completed, observe humidity for X min
                 * to see if it rises again.
                 */
                if (global_time >= observe_end) {
                    state = DONE;
                    break;
                }

                if (humidity[IN] > humidity[OUT]) {
                    fan(0);
                    heater(1);
                    state = HEAT;
                    countdown = MIN_DRYING_TIME;
                    restart_count++;
                }
                break;

            case DONE:
                break;
        }
    }

    if (state && !switched_on()) {
        fan(0);
        heater(0);
        state = OFF;
        countdown = 0;
        reset_lcd();
    }

    display();

    delay(1000);

    if (state > OFF && state < DONE)
      global_time++;

    if (countdown)
        countdown--;
}

#ifdef MOCK
void mock_loop(void)
{
    static int hum_peak = 0;
    static int restart_state = 0;

    if (mock_heater) {
        if (insensor.temp < 60)
            insensor.temp += 0.07;
    }
    else
        insensor.temp -= 0.01;

    if (!hum_peak) {
        insensor.hum += 0.1;
        if (insensor.hum >= 100) {
            insensor.hum = 100;
            printf("hum peak!\n");
            hum_peak = 1;
        }
    }

    if (mock_fan) {
        insensor.temp -= insensor.temp / 300;
        insensor.hum -= 0.1;
    }

    if (state == 3 && restart_state < 2) {
        restart_state = 1;
        insensor.hum += 0.1;
    }
    if (state == 2 && restart_state == 1)
        restart_state = 3;

    if (mock_time / 1000 % 10 == 0)
        printf("time: %02d:%02d state %d, in %.1f° %dg (%.1f%%), out %.1f° %dg\n",
               mock_time / 1000 / 60,
               mock_time / 1000 % 60,
               state,
               temp[IN], humidity[IN], insensor.hum,
               temp[OUT], humidity[OUT]);
    mock_switch = 0;
}

int main(void) {
  setup();
  do {
    loop();
    mock_loop();
  } while (state != 4);

  for (int i=0; i<20; i++)
    loop();

  printf("%-11s%dt%2dm\n", statestring[state],
         global_time / 3600, (global_time % 3600) / 60);

  mock_switch = 1;
  loop();

  printf("%-11s%dt%2dm\n", statestring[state],
         global_time / 3600, (global_time % 3600) / 60);
}
#endif
