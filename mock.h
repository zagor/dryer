// mock the system so the code can be tested on command-line

#include <stdio.h>

#define DHT22 1
#define HIGH 1
#define LOW  0
#define INPUT 0
#define OUTPUT 1
#define WDTO_4S 4

int mock_time = 0;
int mock_heater = 0;
int mock_fan = 0;
int mock_switch = 1;

class DHT {
 public:
  enum {
    inside,
    outside
  } sensor;
  double temp;
  double hum;

  DHT(int pin, int type) {
    (void)type;
    temp = 22;
    hum = 60;
    if (pin == 7)
      sensor = inside;
    else
      sensor = outside;
  }
  float readHumidity() { mock_time += 250; return hum; }
  float readTemperature() { return temp; }
  void begin() {}
};

class LiquidCrystal_I2C {
 public:
  LiquidCrystal_I2C(int, int, int) {}
  void begin() {}
  void home() {}
  void backlight() {}
  void print(const char* line) {
    printf("LCD: %s\n", line);
  }
  void setCursor(int, int) {}
};

int  millis(void) { return mock_time; }
void delay(int time) { mock_time += time; }
void wdt_enable(int time) {(void)time;}
void wdt_reset() {}
void digitalWrite(int pin, int on) {
  if (pin == 3) {
    mock_heater = on;
    printf("time: %02d:%02d Heat: %s\n",
           mock_time / 1000 / 60,
           mock_time / 1000 % 60,
           on ? "ON" : "OFF");
  }
  else if (pin == 4) {
    mock_fan = on;
    printf("time: %02d:%02d Fan: %s\n",
           mock_time / 1000 / 60,
           mock_time / 1000 % 60,
           on ? "ON" : "OFF");
  }
}

int digitalRead(int pin) {
  (void)pin;
  /* power switch */
  return mock_switch;
}

void pinMode(int pin, int mode) {
  (void)pin;
  (void)mode;
}

