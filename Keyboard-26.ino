/*

todo:
 - Init process !
 - Add TJs ASCII mapping
 - I2C protocol ?

*/

#include "PS2Keymaps.h"
#include "global.h"
#include "src/protocols/parallel_both_24char.h"

uint8_t dipConfig;
PS2Keymap_t *keymap;

enum InitState {
  INIT_IDLE,
  INIT_WAIT_FOR_FFFF,
  INIT_WAIT_FOR_ZERO_AGAIN,
  INIT_WAIT_FOR_PROTOCOL
};

void (*key_down[])(char) = {
  protocol_parallel_both_24char_keyDown, protocol_parallel_both_24char_keyDown, protocol_parallel_both_24char_keyDown, protocol_parallel_both_24char_keyDown,
  protocol_parallel_both_24char_keyDown, protocol_parallel_both_24char_keyDown, protocol_parallel_both_24char_keyDown, protocol_parallel_both_24char_keyDown,
  protocol_parallel_both_24char_keyDown, protocol_parallel_both_24char_keyDown, protocol_parallel_both_24char_keyDown, protocol_parallel_both_24char_keyDown,
  protocol_parallel_both_24char_keyDown, protocol_parallel_both_24char_keyDown, protocol_parallel_both_24char_keyDown, protocol_parallel_both_24char_keyDown
};

void (*key_up[])() = {
  protocol_parallel_both_24char_keyUp, protocol_parallel_both_24char_keyUp, protocol_parallel_both_24char_keyUp, protocol_parallel_both_24char_keyUp,
  protocol_parallel_both_24char_keyUp, protocol_parallel_both_24char_keyUp, protocol_parallel_both_24char_keyUp, protocol_parallel_both_24char_keyUp,
  protocol_parallel_both_24char_keyUp, protocol_parallel_both_24char_keyUp, protocol_parallel_both_24char_keyUp, protocol_parallel_both_24char_keyUp,
  protocol_parallel_both_24char_keyUp, protocol_parallel_both_24char_keyUp, protocol_parallel_both_24char_keyUp, protocol_parallel_both_24char_keyUp
};

void set_right_nibble(char value){
  // this might be moved to a protocol later
  // 1. set right SN74HC4066 to OE off !
  // 2. set right data port to output
  // 3. set right data port to keyvalue
  digitalWrite(OE_RIGHT_PORT, LOW);
  DDRD = 0b00001111;
  PORTD = value & 0x0F;
}

void setup() {
  #if DEBUG
  Serial.begin(115200);
  #endif

  pinMode(CLOCK, INPUT_PULLUP); // For most keyboards the builtin pullups are sufficient, so the 10k pullups can be omitted
  pinMode(DATA, INPUT_PULLUP);
  pinMode(OE_LEFT_PORT, OUTPUT);
  pinMode(OE_RIGHT_PORT, OUTPUT);
  digitalWrite(OE_LEFT_PORT, HIGH);  // Activate left joystick forwarding via first 74HC4066
  digitalWrite(OE_RIGHT_PORT, HIGH); // Activate right joystick forwarding via second 74HC4066

  bitSet(PCICR, PCIE0);   // Enable pin change interrupts on pin B0-B7
  bitSet(PCMSK0, PCINT1); // Pin change interrupt on Clock pin (PCINT1 = PB1 Arduino Pin 9)

  int adcValue = analogRead(DIP_A_PIN);      // Read voltage of DIP-Switch block resistor ladder (0-1023)
  dipConfig = map(adcValue, 0, 1023, 15, 0); // Get binary value of DIP-Switch Block (0-15) 
 
  switch (dipConfig & 0b11) { // lower 2 bits of dipConfig define keyboard layout
    case 0b01: keymap = &PS2Keymap_German;      break;
    case 0b10: keymap = &PS2Keymap_French;      break;
    default: keymap = &PS2Keymap_US;            break;
  }

  #if DEBUG
  Serial.println("Keyboard test");
  Serial.println(dipConfig);
  #endif
}

uint8_t curscan = 0;
bool key_action = false;

ISR(PCINT0_vect) {
  uint16_t newscan = 0;
  for(int i = 0; i<11; i++) {
    while(digitalRead(CLOCK));
    newscan |= digitalRead(DATA) << i;
    while(!digitalRead(CLOCK));
  }
  newscan >>= 1;
  newscan &= 0xFF;
  key_action = true;

  curscan = newscan;
  bitSet(PCIFR, PCIF0);
}

uint8_t lastscan = 0;

void loop() {
 	static uint8_t keyboard_state = 0;
  static InitState initState = INIT_IDLE;
  static uint8_t activeProtocol = 0;
  static unsigned long initTimer = 0;

  char c;

  if(key_action){
    key_action = false;
    if(curscan != 0xF0 && curscan != 0xE0){
      if (curscan < PS2_KEYMAP_SIZE){
        if (keyboard_state & SHIFT) {
          c = pgm_read_byte(keymap->shift + curscan);
        } else if ((keyboard_state & ALTGR) && keymap->uses_altgr) {
          c = pgm_read_byte(keymap->altgr + curscan);
        } else {
          c = pgm_read_byte(keymap->noshift + curscan);
        }
      }

      if(lastscan == 0xF0){
        if (curscan == 0x12 || curscan == 0x59) {
          keyboard_state &= ~SHIFT;
        } else if (curscan == 0x11 && (keyboard_state & MODIFIER)) {
          keyboard_state &= ~ALTGR;
        } else {
          // Key up
          #if DEBUG
          Serial.print(c);
          Serial.println(" up");
          #else
          key_up[activeProtocol]();
          #endif
        }
        // CTRL, ALT & WIN keys could be added
        // but is that really worth the overhead?
        keyboard_state &= ~MODIFIER;
      }else{
        if (curscan == 0x12 || curscan == 0x59) {
          keyboard_state |= SHIFT;
        } else if (curscan == 0x11 && (keyboard_state & MODIFIER)) {
          keyboard_state |= ALTGR;
        } else {
          if(lastscan == 0xE0){ // MODIFIER
            switch (curscan) {
              case 0x70: c = PS2_INSERT;      break;
              case 0x6C: c = PS2_HOME;        break;
              case 0x7D: c = PS2_PAGEUP;      break;
              case 0x71: c = PS2_DELETE;      break;
              case 0x69: c = PS2_END;         break;
              case 0x7A: c = PS2_PAGEDOWN;    break;
              case 0x75: c = PS2_UPARROW;     break;
              case 0x6B: c = PS2_LEFTARROW;   break;
              case 0x72: c = PS2_DOWNARROW;   break;
              case 0x74: c = PS2_RIGHTARROW;  break;
              case 0x4A: c = '/';             break;
              case 0x5A: c = PS2_ENTER;       break;
              default: break;
            }
          }
          key_down[activeProtocol](c);
        }
        keyboard_state &= ~MODIFIER;
      }
    }
    lastscan = curscan;
  }else{
    uint8_t busState = PIND & 0x0F;
    switch (initState) {
      case INIT_IDLE:
        if (busState == 0) {
          initState = initState + 1;
          initTimer = millis();
        }
        break;

      case INIT_WAIT_FOR_FFFF:
        if (busState == 0b1111) {
          initState = initState + 1;
        }
        break;

      case INIT_WAIT_FOR_ZERO_AGAIN:
        if (busState == 0) {
          initState = initState + 1;
        }
        break;

      case INIT_WAIT_FOR_PROTOCOL:
        if (busState != 0) {
          activeProtocol = busState;
          initState = INIT_IDLE;

          delay(17);                        // wait one 60Hz-Frame
          set_right_nibble(activeProtocol); // return activeProtocol for confirmation on the bus
          delay(34);                        // wait two more frames
          protocol_parallel_both_24char_keyUp();
        }
        break;
    }

    // Timeout protection
    if (initState != INIT_IDLE && (millis() - initTimer > 1000)) {
      initState = INIT_IDLE;
    }
  }
}
