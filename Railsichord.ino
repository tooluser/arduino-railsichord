/*
Exploratorium Tinkering Snack version of Musical / Singing Bench.

Uses Arduino USB and a slightly modified SparkFun Musical Insturment Shield, see the
  website for the schematic and build instructions.

A simple resistive voltage divider sensor circuit is used for input.  The harder
  a person presses, or the more surface is in contact with the touch pads, or the
  moister a person's hands, the lower the touch resistence will be.  Low resistences are
  mapped to high notes, so the harder a person presses, the higher the note.  A light touch
  gets the low notes.

High sensor values are open circuit; touch resistence is the pull-down in the sensor circuit.
  This is so that the fixed pull-up resistor in the sensor circuit protects VCC from the
  cold, cruel outside world (GND is less vulnerable).  It's a bit counter-intuitive at first,
  but much more robust.

This is about as stripped down as it gets!  It arpeggiates the chromatic scale between limits
  set by the macros LOWEST_NOTE and HIGHEST_NOTE, with a fixed attack velocity
  and a single instrument voice.  The lowest and highest notes are not likely to be heard,
  the sensor range will be in practice restricted and stay near the low half of the range,
  but the mapping is defined for the full theoretical range to avoid burdensome error checking.
  Chromatics are not the most pleasing of scales, but it uses the simplest possible method
  of translating sensor values into note values: the map() function.  It sounds like a horror movie
  soundtrack, but that's kind of fun!

Two simple anti-noise strategies are in use.  First, sensor values are not accepted unless they
  differ from the last accepted value by a minimum amount: sensor hysteresis.  Second, there is
  a dead zone at the open-circuit end of the sensor range: the noise floor.  Without these, it
  would be pretty hard to have fun with this critter, it's a complication worth putting up with.

The main tweak opportunities are collected into the macros just below.  Have fun with them!

Extra credit:  can you spot why there is a single "glitch" note when this sketch starts?
*/

#include <SoftwareSerial.h>  // needed for MIDI communications with the MI shield

//  These are the most tweak-able macros, the ones that really define how it "plays"
#define SIXTEENTH_NOTE_MS 100  // Approx. loop rate in millis, all note durations will be integer multiples of this.  Tweak to speed up or slow down the arpeggio.
#define SENSOR_HYSTERESIS 15  // ignore incremental changes smaller than this value...  needed to kill noise, but also affects how busy and dense the arpeggio is
#define LOWEST_NOTE 48   // 48 = C3 in MIDI; the piano keyboard ranges from A0 = 21 through C8 = 108, the lowest octaves can be pretty muddy sounding
#define HIGHEST_NOTE 96   // 96 = C7. A smaller range will result in a less busy feel, and may allow more repeated strikes of the same note.  You probably won't hear half of this range with human touch, but short the touch pads with metal and you will

// these should need less tweaking, but go ahead and see what they do
#define MY_INSTRUMENT 1  // max 127.  GM1 Melodic bank instrument 0 is the Acoustic Grand Piano.  You could tweak this, but beware: since we are not using any note off method, if you pick one with long or infinite sustain, you'll get cacaphony in a hurry

// Felix's comments
// Fog: "Like foghorns" 76 Pan flute for foggy? 77, 102 goblins (night)
// Windy: 77 - blown bottle? 113 tinkle bell
// Sunny: "like vibration"
// Rain: "rain/water falling down waterfall"

#define NOISE_FLOOR 925 // out of 1023, higher than this is considered open circuit.
#define VELOCITY  100  //  note strike "force", max 127
#define VOLUME    110  //  max 127, 110 works well for most headphones and fine for most amplifiers as well

// From here on down, tweaking is probably not a great idea...

// pin assignments
#define MIDI_RX_PIN     2   // Soft serial Rx; M.I shield TxD
#define MIDI_TX_PIN     3   // shield RxD
#define MIDI_RESET_PIN  4   // low active shield reset

#define LED_PIN					13  // Arduino native LED
#define SENSOR_PIN  		A0  // also known as P14

// Standard MIDI, ADDRESS == CH1 which is all we propose to use
#define MIDI_BAUD     31250
#define PROG_CHANGE   0xC0  // program change message
#define CONT_CHANGE   0xB0  // controller change message
#define NOTE_ON       0x90  // sorry about the Hex, but MIDI is all about the nybbles!
#define ALL_OFF       0x7B  // Using "all notes off" rather than "note off" for single voice MIDI is a common tactic; MIDI devices are notorious for orphaning notes
#define BANK_SELECT   0x00
#define CHANNEL_VOL   0x07

// GM1 instrument bank mapping implemented on VS1053b chip
#define MELODIC_BANK  0x79  // there are other options, just not any that are better.

// Global variables and objects
SoftwareSerial MIDIserial(MIDI_RX_PIN, MIDI_TX_PIN);  // channel for MIDI comms with the MI shield
unsigned int 	touchSensor;  // holds the most recent accepted sensor value.
unsigned int 	sensorBuffer;  // holds a provisional sensor value while we look at it
//unsigned long lastNoteProcessed;

void setup() {
  Serial.begin(9600);  // USB is for debugging and tuning info, make sure your serial monitor is set to match this baud rate
  Serial.println("Starting...");

	Serial.print("MIDI initializing...");
	setup_midi();
	Serial.println("...done.");
  //lastNoteProcessed = 0;
}

void loop() {
  sensorBuffer = analogRead(SENSOR_PIN);  // get new sensor value to examine
  processSensorReading();
  delay (SIXTEENTH_NOTE_MS);  // tempo: do nothing for sixteenth note duration, the rest of the loop will execute so fast we'll never notice it
}

void setup_midi() {
  // reset pin set-up and hwr reset of the VS1053 on the MI shield.  It's probably going to make a fart-y sound if you have an external amplifier!
  pinMode(MIDI_RESET_PIN, OUTPUT);
  digitalWrite(MIDI_RESET_PIN, LOW);
  delay(100);
  digitalWrite(MIDI_RESET_PIN, HIGH);
  delay(1000);

  //Start soft serial channel for MIDI, and send one-time only messages
  MIDIserial.begin(MIDI_BAUD);
  talkMIDI(CONT_CHANGE , CHANNEL_VOL, VOLUME);   // set channel volume
  talkMIDI(CONT_CHANGE, BANK_SELECT, MELODIC_BANK);  // select instrument bank
  talkMIDI(PROG_CHANGE, MY_INSTRUMENT, 0);   // set specific instrument voice within bank.
}

void processSensorReading() {
  if (abs(sensorBuffer-touchSensor) >= SENSOR_HYSTERESIS)  {  // only accept a new sensor value if it differs from the last accepted value by the minimum amount
    touchSensor = sensorBuffer;  // accept the new sensor value, but don't play a note yet...

    Serial.print(touchSensor);  // report out values in case anyone is monitoring
    //    Serial.print(' ');
    //    Serial.print(" lastNoteAt: ");
    //    Serial.print(lastNoteProcessed);
    //    Serial.print(" now: ");
    //    Serial.print(millis());
    //    Serial.print(" delta: ");
    //    Serial.print(millis() - lastNoteProcessed);

    //// Stray readings can make random noises with no one touching. Require two solid signals in a row
    //if ((millis() - lastNoteProcessed) < 100) {
    //  Serial.println("ms - deferring");
    //  return;
    //}

    if (touchSensor <= NOISE_FLOOR) {  // only play a note if the accepted sensor value is below the noise floor
      // we've passed both the anti-noise tests; time to calculate and play a new note
      //      lastNoteProcessed = millis();
      unsigned int note = map(touchSensor, 0, 1023, HIGHEST_NOTE, LOWEST_NOTE);  // high sensor values (high resistence, light touch) map to low notes and V.V.
      playNote(note);
      Serial.print(" PLAY ");
      Serial.println(note);
    } else {
      Serial.println("ms - value above noise floor, is noise");
    }
  } else {
    //    Serial.print("- hysterisis value not exceeded (");
    //    Serial.print(abs(sensorBuffer-touchSensor));
    //    Serial.println(")");
  }
}

void playNote(unsigned int note) {
  talkMIDI(CONT_CHANGE , ALL_OFF, 0);  // turn off previous note(s); comment this out if you like the notes to ring over each other
  talkMIDI(NOTE_ON , note, VELOCITY);  // play new note!
}

//Sends all the MIDI messages we need for this sketch. Use with caution outside of this sketch: most of the MIDI protocol is not supported!
void talkMIDI(byte cmd, byte data1, byte data2) {
  digitalWrite(LED_PIN, HIGH);  // use Arduino LED to signal MIDI activity
  MIDIserial.write(cmd);
  MIDIserial.write(data1);
  if(cmd != PROG_CHANGE) MIDIserial.write(data2); //Send 2nd data byte if needed; all of our supported commands other than PROG_CHANGE have 2 data bytes
  digitalWrite(LED_PIN, LOW);
}
