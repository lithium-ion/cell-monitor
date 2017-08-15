#include <Arduino.h>
#include <SoftwareSerial.h>
#include <avr/sleep.h>
#include <avr/wdt.h>

#define DO_EXPAND(VAL) VAL ## 1
#define EXPAND(VAL) DO_EXPAND(VAL)

// this address should be unique for each cell monitor on the serial bus
#if !defined(CELL_ADDRESS) || (EXPAND(CELL_ADDRESS) == 1)
#define CELL_ADDRESS 0b0001
#endif

// this value should be calibrated for more accurate voltage measurements
#if !defined(REF_VOLTAGE) || (EXPAND(REF_VOLTAGE) == 1)
#define REF_VOLTAGE 1100
#endif

#define CMD_SEND_VOLTAGE (1 << 0)
#define CMD_BALANCE (1 << 1)

#define adc_disable() (ADCSRA &= ~(1 << ADEN))
#define adc_enable() (ADCSRA |=  (1 << ADEN))

SoftwareSerial serial(3, 4); // RX, TX

uint16_t read_vcc(void) {
  // use VCC as reference
  // measure internal bandgap reference voltage
  ADMUX = _BV(MUX3) | _BV(MUX2);

  // wait for ADC to settle
  delay(2);

  // start conversion
  ADCSRA |= _BV(ADSC);

  // wait for conversion to complete
  while (bit_is_set(ADCSRA, ADSC));

  uint8_t low  = ADCL; // must read ADCL first
  uint8_t high = ADCH;
  uint16_t result = (high << 8) | low;

  return (REF_VOLTAGE * 1023L) / result;
}

uint16_t read_vcc_avg(void) {
  uint16_t sum = 0;

  for (int i = 0; i < 8; i++) {
    sum += read_vcc();
    delay(i);
  }

  return sum / 8;
}

void send_voltage(void) {
  uint16_t vcc = read_vcc_avg();
  serial.write(vcc >> 8); // MSB
  serial.write(vcc & 0xFF); // LSB
}

uint32_t enable_time;
uint8_t balancing = 0;

void enable_balancing(void) {
  enable_time = millis();
  digitalWrite(0, HIGH);
  balancing = 1;
}

void disable_balancing(void) {
  digitalWrite(0, LOW);
  balancing = 0;
}

void autodisable_balancing(void) {
  uint32_t now = millis();
  uint32_t balancing_time = 0;

  if (now < enable_time) {
    balancing_time = enable_time - now;
  } else {
    balancing_time = now - enable_time;
  }

  // 10 second auto-disable
  if (balancing_time >= 10000) {
    disable_balancing();
  }
}

void sleep(void) {
  adc_disable();
  wdt_reset();
  wdt_disable();
  sleep_enable();
  sleep_cpu();

  // wake up
  adc_enable();
  wdt_enable(WDTO_1S);
  wdt_reset();
}

void process_command(uint8_t command) {
  if (command & CMD_SEND_VOLTAGE) {
    send_voltage();
  }

  if (command & CMD_BALANCE) {
    enable_balancing();
  } else {
    disable_balancing();
  }
}

void process_input() {
  if (serial.available()) {
    uint8_t value = serial.read();

    if (value > 0) {
      uint8_t address = value >> 4;

      if (address == CELL_ADDRESS) {
        uint8_t command = value & 0xF;

        process_command(command);
      }
    }
  }
}

void setup() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);

  // 1 second watchdog
  wdt_enable(WDTO_1S);

  // serial pins
  pinMode(3, INPUT);
  pinMode(4, OUTPUT);

  // balancing pin
  pinMode(0, OUTPUT);
  digitalWrite(0, LOW);

  serial.begin(9600);
}

void loop() {
  process_input();
  wdt_reset();
  autodisable_balancing();

  if (!balancing) {
    sleep();
  }
}
