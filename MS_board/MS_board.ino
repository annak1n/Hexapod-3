#ifndef VarSpeedServo_h
#define VarSpeedServo_h

#include <inttypes.h>

// Say which 16 bit timers can be used and in what order
#if defined(__AVR_ATmega1280__)  || defined(__AVR_ATmega2560__)
#define _useTimer5
#define _useTimer1 
#define _useTimer3
#define _useTimer4 
typedef enum { _timer5, _timer1, _timer3, _timer4, _Nbr_16timers } timer16_Sequence_t ;

#elif defined(__AVR_ATmega32U4__)  
#define _useTimer3
#define _useTimer1 
typedef enum { _timer3, _timer1, _Nbr_16timers } timer16_Sequence_t ;

#elif defined(__AVR_AT90USB646__) || defined(__AVR_AT90USB1286__)
#define _useTimer3
#define _useTimer1
typedef enum { _timer3, _timer1, _Nbr_16timers } timer16_Sequence_t ;

#elif defined(__AVR_ATmega128__) ||defined(__AVR_ATmega1281__)||defined(__AVR_ATmega2561__)
#define _useTimer3
#define _useTimer1
typedef enum { _timer3, _timer1, _Nbr_16timers } timer16_Sequence_t ;

#else  // everything else
#define _useTimer1
typedef enum { _timer1, _Nbr_16timers } timer16_Sequence_t ;                  
#endif

#define VarSpeedServo_VERSION           2      // software version of this library

#define MIN_PULSE_WIDTH       544     // the shortest pulse sent to a servo  
#define MAX_PULSE_WIDTH      2400     // the longest pulse sent to a servo 
#define DEFAULT_PULSE_WIDTH  1500     // default pulse width when servo is attached
#define REFRESH_INTERVAL    20000     // minumim time to refresh servos in microseconds 

#define SERVOS_PER_TIMER       12     // the maximum number of servos controlled by one timer 
#define MAX_SERVOS   (_Nbr_16timers  * SERVOS_PER_TIMER)

#define INVALID_SERVO         255     // flag indicating an invalid servo index

#define CURRENT_SEQUENCE_STOP   255    // used to indicate the current sequence is not used and sequence should stop


typedef struct  {
  uint8_t nbr        :6 ;             // a pin number from 0 to 63
  uint8_t isActive   :1 ;             // true if this channel is enabled, pin not pulsed if false 
} ServoPin_t   ;  

typedef struct {
  ServoPin_t Pin;
  unsigned int ticks;
  unsigned int value;     // Extension for external wait (Gill)
  unsigned int target;      // Extension for slowmove
  uint8_t speed;          // Extension for slowmove
} servo_t;

typedef struct {
  uint8_t position;
  uint8_t speed;
} servoSequencePoint;

class VarSpeedServo
{
public:
  VarSpeedServo();
  uint8_t attach(int pin);           // attach the given pin to the next free channel, sets pinMode, returns channel number or 0 if failure
  uint8_t attach(int pin, int min, int max); // as above but also sets min and max values for writes. 
  void detach();
  void write(int value);             // if value is < 200 its treated as an angle, otherwise as pulse width in microseconds
  void write(int value, uint8_t speed); // Move to given position at reduced speed.
          // speed=0 is identical to write, speed=1 slowest and speed=255 fastest.
          // On the RC-Servos tested, speeds differences above 127 can't be noticed,
          // because of the mechanical limits of the servo.
  void write(int value, uint8_t speed, bool wait); // wait parameter causes call to block until move completes
  void writeMicroseconds(int value); // Write pulse width in microseconds 
  void writeMicroseconds(int value, uint8_t speed); // Write pulse width in microseconds 
  void slowmove(int value, uint8_t speed);
  void stop(); // stop the servo where it is
  
  int read();                        // returns current pulse width as an angle between 0 and 180 degrees
  int readMicroseconds();            // returns current pulse width in microseconds for this servo (was read_us() in first release)
  bool attached();                   // return true if this servo is attached, otherwise false 

  uint8_t sequencePlay(servoSequencePoint sequenceIn[], uint8_t numPositions, bool loop, uint8_t startPos);
  uint8_t sequencePlay(servoSequencePoint sequenceIn[], uint8_t numPositions); // play a looping sequence starting at position 0
  void sequenceStop(); // stop movement
  void wait(); // wait for movement to finish  
  bool isMoving(); // return true if servo is still moving
private:
   uint8_t servoIndex;               // index into the channel data for this servo
   int8_t min;                       // minimum is this value times 4 added to MIN_PULSE_WIDTH    
   int8_t max;                       // maximum is this value times 4 added to MAX_PULSE_WIDTH
   servoSequencePoint * curSequence; // for sequences
   uint8_t curSeqPosition; // for sequences

};

#endif
#include <avr/interrupt.h>
#include <Arduino.h> // updated from WProgram.h to Arduino.h for Arduino 1.0+, pva

//#include "./SpeedServo.h"

#define usToTicks(_us)    (( clockCyclesPerMicrosecond()* _us) / 8)     // converts microseconds to tick (assumes prescale of 8)  // 12 Aug 2009
#define ticksToUs(_ticks) (( (unsigned)_ticks * 8)/ clockCyclesPerMicrosecond() ) // converts from ticks back to microseconds


#define TRIM_DURATION       2                               // compensation ticks to trim adjust for digitalWrite delays // 12 August 2009

//#define NBR_TIMERS        (MAX_SERVOS / SERVOS_PER_TIMER)

static servo_t servos[MAX_SERVOS];                          // static array of servo structures
static volatile int8_t Channel[_Nbr_16timers ];             // counter for the servo being pulsed for each timer (or -1 if refresh interval)

uint8_t ServoCount = 0;                                     // the total number of attached servos

// sequence vars

servoSequencePoint initSeq[] = {{0,100},{45,100}};

//sequence_t sequences[MAX_SEQUENCE];

// convenience macros
#define SERVO_INDEX_TO_TIMER(_servo_nbr) ((timer16_Sequence_t)(_servo_nbr / SERVOS_PER_TIMER)) // returns the timer controlling this servo
#define SERVO_INDEX_TO_CHANNEL(_servo_nbr) (_servo_nbr % SERVOS_PER_TIMER)       // returns the index of the servo on this timer
#define SERVO_INDEX(_timer,_channel)  ((_timer*SERVOS_PER_TIMER) + _channel)     // macro to access servo index by timer and channel
#define SERVO(_timer,_channel)  (servos[SERVO_INDEX(_timer,_channel)])            // macro to access servo class by timer and channel

#define SERVO_MIN() (MIN_PULSE_WIDTH - this->min * 4)  // minimum value in uS for this servo
#define SERVO_MAX() (MAX_PULSE_WIDTH - this->max * 4)  // maximum value in uS for this servo

/************ static functions common to all instances ***********************/

static inline void handle_interrupts(timer16_Sequence_t timer, volatile uint16_t *TCNTn, volatile uint16_t* OCRnA)
{
  if( Channel[timer] < 0 )
    *TCNTn = 0; // channel set to -1 indicated that refresh interval completed so reset the timer
  else{
    if( SERVO_INDEX(timer,Channel[timer]) < ServoCount && SERVO(timer,Channel[timer]).Pin.isActive == true )
      digitalWrite( SERVO(timer,Channel[timer]).Pin.nbr,LOW); // pulse this channel low if activated
  }

  Channel[timer]++;    // increment to the next channel
  if( SERVO_INDEX(timer,Channel[timer]) < ServoCount && Channel[timer] < SERVOS_PER_TIMER) {

  // Extension for slowmove
  if (SERVO(timer,Channel[timer]).speed) {
    // Increment ticks by speed until we reach the target.
    // When the target is reached, speed is set to 0 to disable that code.
    if (SERVO(timer,Channel[timer]).target > SERVO(timer,Channel[timer]).ticks) {
      SERVO(timer,Channel[timer]).ticks += SERVO(timer,Channel[timer]).speed;
      if (SERVO(timer,Channel[timer]).target <= SERVO(timer,Channel[timer]).ticks) {
        SERVO(timer,Channel[timer]).ticks = SERVO(timer,Channel[timer]).target;
        SERVO(timer,Channel[timer]).speed = 0;
      }
    }
    else {
      SERVO(timer,Channel[timer]).ticks -= SERVO(timer,Channel[timer]).speed;
      if (SERVO(timer,Channel[timer]).target >= SERVO(timer,Channel[timer]).ticks) {
        SERVO(timer,Channel[timer]).ticks = SERVO(timer,Channel[timer]).target;
        SERVO(timer,Channel[timer]).speed = 0;
      }
    }
  }
  // End of Extension for slowmove

  // Todo

    *OCRnA = *TCNTn + SERVO(timer,Channel[timer]).ticks;
    if(SERVO(timer,Channel[timer]).Pin.isActive == true)     // check if activated
      digitalWrite( SERVO(timer,Channel[timer]).Pin.nbr,HIGH); // its an active channel so pulse it high
  }
  else {
    // finished all channels so wait for the refresh period to expire before starting over
    if( (unsigned)*TCNTn <  (usToTicks(REFRESH_INTERVAL) + 4) )  // allow a few ticks to ensure the next OCR1A not missed
      *OCRnA = (unsigned int)usToTicks(REFRESH_INTERVAL);
    else
      *OCRnA = *TCNTn + 4;  // at least REFRESH_INTERVAL has elapsed
    Channel[timer] = -1; // this will get incremented at the end of the refresh period to start again at the first channel
  }
}

#ifndef WIRING // Wiring pre-defines signal handlers so don't define any if compiling for the Wiring platform
// Interrupt handlers for Arduino
#if defined(_useTimer1)
SIGNAL (TIMER1_COMPA_vect)
{
  handle_interrupts(_timer1, &TCNT1, &OCR1A);
}
#endif

#if defined(_useTimer3)
SIGNAL (TIMER3_COMPA_vect)
{
  handle_interrupts(_timer3, &TCNT3, &OCR3A);
}
#endif

#if defined(_useTimer4)
SIGNAL (TIMER4_COMPA_vect)
{
  handle_interrupts(_timer4, &TCNT4, &OCR4A);
}
#endif

#if defined(_useTimer5)
SIGNAL (TIMER5_COMPA_vect)
{
  handle_interrupts(_timer5, &TCNT5, &OCR5A);
}
#endif

#elif defined WIRING
// Interrupt handlers for Wiring
#if defined(_useTimer1)
void Timer1Service()
{
  handle_interrupts(_timer1, &TCNT1, &OCR1A);
}
#endif
#if defined(_useTimer3)
void Timer3Service()
{
  handle_interrupts(_timer3, &TCNT3, &OCR3A);
}
#endif
#endif


static void initISR(timer16_Sequence_t timer)
{
#if defined (_useTimer1)
  if(timer == _timer1) {
    TCCR1A = 0;             // normal counting mode
    TCCR1B = _BV(CS11);     // set prescaler of 8
    TCNT1 = 0;              // clear the timer count
#if defined(__AVR_ATmega8__)|| defined(__AVR_ATmega128__)
    TIFR |= _BV(OCF1A);      // clear any pending interrupts;
    TIMSK |=  _BV(OCIE1A) ;  // enable the output compare interrupt
#else
    // here if not ATmega8 or ATmega128
    TIFR1 |= _BV(OCF1A);     // clear any pending interrupts;
    TIMSK1 |=  _BV(OCIE1A) ; // enable the output compare interrupt
#endif
#if defined(WIRING)
    timerAttach(TIMER1OUTCOMPAREA_INT, Timer1Service);
#endif
  }
#endif

#if defined (_useTimer3)
  if(timer == _timer3) {
    TCCR3A = 0;             // normal counting mode
    TCCR3B = _BV(CS31);     // set prescaler of 8
    TCNT3 = 0;              // clear the timer count
#if defined(__AVR_ATmega128__)
    TIFR |= _BV(OCF3A);     // clear any pending interrupts;
  ETIMSK |= _BV(OCIE3A);  // enable the output compare interrupt
#else
    TIFR3 = _BV(OCF3A);     // clear any pending interrupts;
    TIMSK3 =  _BV(OCIE3A) ; // enable the output compare interrupt
#endif
#if defined(WIRING)
    timerAttach(TIMER3OUTCOMPAREA_INT, Timer3Service);  // for Wiring platform only
#endif
  }
#endif

#if defined (_useTimer4)
  if(timer == _timer4) {
    TCCR4A = 0;             // normal counting mode
    TCCR4B = _BV(CS41);     // set prescaler of 8
    TCNT4 = 0;              // clear the timer count
    TIFR4 = _BV(OCF4A);     // clear any pending interrupts;
    TIMSK4 =  _BV(OCIE4A) ; // enable the output compare interrupt
  }
#endif

#if defined (_useTimer5)
  if(timer == _timer5) {
    TCCR5A = 0;             // normal counting mode
    TCCR5B = _BV(CS51);     // set prescaler of 8
    TCNT5 = 0;              // clear the timer count
    TIFR5 = _BV(OCF5A);     // clear any pending interrupts;
    TIMSK5 =  _BV(OCIE5A) ; // enable the output compare interrupt
  }
#endif
}

static void finISR(timer16_Sequence_t timer)
{
    //disable use of the given timer
#if defined WIRING   // Wiring
  if(timer == _timer1) {
    #if defined(__AVR_ATmega1281__)||defined(__AVR_ATmega2561__)
    TIMSK1 &=  ~_BV(OCIE1A) ;  // disable timer 1 output compare interrupt
    #else
    TIMSK &=  ~_BV(OCIE1A) ;  // disable timer 1 output compare interrupt
    #endif
    timerDetach(TIMER1OUTCOMPAREA_INT);
  }
  else if(timer == _timer3) {
    #if defined(__AVR_ATmega1281__)||defined(__AVR_ATmega2561__)
    TIMSK3 &= ~_BV(OCIE3A);    // disable the timer3 output compare A interrupt
    #else
    ETIMSK &= ~_BV(OCIE3A);    // disable the timer3 output compare A interrupt
    #endif
    timerDetach(TIMER3OUTCOMPAREA_INT);
  }
#else
    //For arduino - in future: call here to a currently undefined function to reset the timer
#endif
}

static boolean isTimerActive(timer16_Sequence_t timer)
{
  // returns true if any servo is active on this timer
  for(uint8_t channel=0; channel < SERVOS_PER_TIMER; channel++) {
    if(SERVO(timer,channel).Pin.isActive == true)
      return true;
  }
  return false;
}


/****************** end of static functions ******************************/

VarSpeedServo::VarSpeedServo()
{
  if( ServoCount < MAX_SERVOS) {
    this->servoIndex = ServoCount++;                    // assign a servo index to this instance
    servos[this->servoIndex].ticks = usToTicks(DEFAULT_PULSE_WIDTH);   // store default values  - 12 Aug 2009
    this->curSeqPosition = 0;
    this->curSequence = initSeq;
  }
  else
    this->servoIndex = INVALID_SERVO ;  // too many servos
}

uint8_t VarSpeedServo::attach(int pin)
{
  return this->attach(pin, MIN_PULSE_WIDTH, MAX_PULSE_WIDTH);
}

uint8_t VarSpeedServo::attach(int pin, int min, int max)
{
  if(this->servoIndex < MAX_SERVOS ) {
    pinMode( pin, OUTPUT) ;                                   // set servo pin to output
    servos[this->servoIndex].Pin.nbr = pin;
    // todo min/max check: abs(min - MIN_PULSE_WIDTH) /4 < 128
    this->min  = (MIN_PULSE_WIDTH - min)/4; //resolution of min/max is 4 uS
    this->max  = (MAX_PULSE_WIDTH - max)/4;
    // initialize the timer if it has not already been initialized
    timer16_Sequence_t timer = SERVO_INDEX_TO_TIMER(servoIndex);
    if(isTimerActive(timer) == false)
      initISR(timer);
    servos[this->servoIndex].Pin.isActive = true;  // this must be set after the check for isTimerActive
  }
  return this->servoIndex ;
}

void VarSpeedServo::detach()
{
  servos[this->servoIndex].Pin.isActive = false;
  timer16_Sequence_t timer = SERVO_INDEX_TO_TIMER(servoIndex);
  if(isTimerActive(timer) == false) {
    finISR(timer);
  }
}

void VarSpeedServo::write(int value)
{

  byte channel = this->servoIndex;
  servos[channel].value = value;

  if(value < MIN_PULSE_WIDTH)
  {  // treat values less than 544 as angles in degrees (valid values in microseconds are handled as microseconds)
    // updated to use constrain() instead of if(), pva
    value = constrain(value, 0, 180);
    value = map(value, 0, 180, SERVO_MIN(),  SERVO_MAX());
  }
  this->writeMicroseconds(value);
}

void VarSpeedServo::writeMicroseconds(int value)
{
  // calculate and store the values for the given channel
  byte channel = this->servoIndex;
  servos[channel].value = value;

  if( (channel >= 0) && (channel < MAX_SERVOS) )   // ensure channel is valid
  {
    if( value < SERVO_MIN() )          // ensure pulse width is valid
      value = SERVO_MIN();
    else if( value > SERVO_MAX() )
      value = SERVO_MAX();

    value -= TRIM_DURATION;
    value = usToTicks(value);  // convert to ticks after compensating for interrupt overhead - 12 Aug 2009

    uint8_t oldSREG = SREG;
    cli();
    servos[channel].ticks = value;
    SREG = oldSREG;

  // Extension for slowmove
  // Disable slowmove logic.
  servos[channel].speed = 0;
  // End of Extension for slowmove
  }
}

// Extension for slowmove
/*
  write(value, speed) - Just like write but at reduced speed.

  value - Target position for the servo. Identical use as value of the function write.
  speed - Speed at which to move the servo.
          speed=0 - Full speed, identical to write
          speed=1 - Minimum speed
          speed=255 - Maximum speed
*/
void VarSpeedServo::write(int value, uint8_t speed) {
  // This fuction is a copy of write and writeMicroseconds but value will be saved
  // in target instead of in ticks in the servo structure and speed will be save
  // there too.

  byte channel = this->servoIndex;
  servos[channel].value = value;

  if (speed) {

    if (value < MIN_PULSE_WIDTH) {
      // treat values less than 544 as angles in degrees (valid values in microseconds are handled as microseconds)
          // updated to use constrain instead of if, pva
          value = constrain(value, 0, 180);
          value = map(value, 0, 180, SERVO_MIN(),  SERVO_MAX());
    }

    // calculate and store the values for the given channel
    if( (channel >= 0) && (channel < MAX_SERVOS) ) {   // ensure channel is valid
          // updated to use constrain instead of if, pva
          value = constrain(value, SERVO_MIN(), SERVO_MAX());

      value = value - TRIM_DURATION;
      value = usToTicks(value);  // convert to ticks after compensating for interrupt overhead - 12 Aug 2009

      // Set speed and direction
      uint8_t oldSREG = SREG;
      cli();
      servos[channel].target = value;
      servos[channel].speed = speed;
      SREG = oldSREG;
    }
  }
  else {
    write (value);
  }
}
void VarSpeedServo::writeMicroseconds(int value, uint8_t speed) {
  // This fuction is a copy of write and writeMicroseconds but value will be saved
  // in target instead of in ticks in the servo structure and speed will be save
  // there too.

  byte channel = this->servoIndex;
  servos[channel].value = value;

  if (speed) {

    // calculate and store the values for the given channel
    if( (channel >= 0) && (channel < MAX_SERVOS) ) {   // ensure channel is valid
          // updated to use constrain instead of if, pva

      value = value - TRIM_DURATION;
      value = usToTicks(value);  // convert to ticks after compensating for interrupt overhead - 12 Aug 2009

      // Set speed and direction
      uint8_t oldSREG = SREG;
      cli();
      servos[channel].target = value;
      servos[channel].speed = speed;
      SREG = oldSREG;
    }
  }
  else {
    write (value);
  }
}

void VarSpeedServo::write(int value, uint8_t speed, bool wait) {
  write(value, speed);

  if (wait) { // block until the servo is at its new position
    if (value < MIN_PULSE_WIDTH) {
      while (read() != value) {
        delay(5);
      }
    } else {
      while (readMicroseconds() != value) {
        delay(5);
      }
    }
  }
}

void VarSpeedServo::stop() {
  write(read());
}

void VarSpeedServo::slowmove(int value, uint8_t speed) {
  // legacy function to support original version of VarSpeedServo
  write(value, speed);
}

// End of Extension for slowmove


int VarSpeedServo::read() // return the value as degrees
{
  return  map( this->readMicroseconds()+1, SERVO_MIN(), SERVO_MAX(), 0, 180);
}

int VarSpeedServo::readMicroseconds()
{
  unsigned int pulsewidth;
  if( this->servoIndex != INVALID_SERVO )
    pulsewidth = ticksToUs(servos[this->servoIndex].ticks)  + TRIM_DURATION ;   // 12 aug 2009
  else
    pulsewidth  = 0;

  return pulsewidth;
}

bool VarSpeedServo::attached()
{
  return servos[this->servoIndex].Pin.isActive ;
}

uint8_t VarSpeedServo::sequencePlay(servoSequencePoint sequenceIn[], uint8_t numPositions, bool loop, uint8_t startPos) {
  uint8_t oldSeqPosition = this->curSeqPosition;

  if( this->curSequence != sequenceIn) {
    //Serial.println("newSeq");
    this->curSequence = sequenceIn;
    this->curSeqPosition = startPos;
    oldSeqPosition = 255;
  }

  if (read() == sequenceIn[this->curSeqPosition].position && this->curSeqPosition != CURRENT_SEQUENCE_STOP) {
    this->curSeqPosition++;

    if (this->curSeqPosition >= numPositions) { // at the end of the loop
      if (loop) { // reset to the beginning of the loop
        this->curSeqPosition = 0;
      } else { // stop the loop
        this->curSeqPosition = CURRENT_SEQUENCE_STOP;
      }
    }
  }

  if (this->curSeqPosition != oldSeqPosition && this->curSeqPosition != CURRENT_SEQUENCE_STOP) {
    // CURRENT_SEQUENCE_STOP position means the animation has ended, and should no longer be played
    // otherwise move to the next position
    write(sequenceIn[this->curSeqPosition].position, sequenceIn[this->curSeqPosition].speed);
    //Serial.println(this->seqCurPosition);
  }

  return this->curSeqPosition;
}

uint8_t VarSpeedServo::sequencePlay(servoSequencePoint sequenceIn[], uint8_t numPositions) {
  return sequencePlay(sequenceIn, numPositions, true, 0);
}

void VarSpeedServo::sequenceStop() {
  write(read());
  this->curSeqPosition = CURRENT_SEQUENCE_STOP;
}

// to be used only with "write(value, speed)"
void VarSpeedServo::wait() {
  byte channel = this->servoIndex;
  int value = servos[channel].value;

  // wait until is done
  if (value < MIN_PULSE_WIDTH) {
    while (read() != value) {
      delay(5);
    }
  } else {
    while (readMicroseconds() != value) {
      delay(5);
    }
  }
}

bool VarSpeedServo::isMoving() {
  byte channel = this->servoIndex;
  int value = servos[channel].value;
  if (!servos[this->servoIndex].Pin.isActive)
    return false;
  if (value < MIN_PULSE_WIDTH) {
    if (read() != value) {
      return true;
    }
  } else {
    if (readMicroseconds() != value) {
      return true;
    }
  }
  return false;
}
















volatile uint8_t data;
volatile uint16_t t_data;
VarSpeedServo servo[18];
//                0,   1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15, 16, 17 
//byte s_pin[18] = {22, 23, 24, 25, 26, 27, 28, 29, 39, 41, 40, 37, 36, 35, 34, 33, 32, 31};
byte s_pin[18] = {41, 40, 37, 36, 35, 34, 33, 32, 31, 22, 23, 24, 25, 26, 27, 28, 29, 39};
int pulse[18];
int speed[18];
unsigned long lastS;
bool first = 1;
void recv(byte num)
{
  for(int i = 0; i < num; i++)
  {
    uint8_t received_data = Serial3.read();
    static byte msg = 0;
    static byte data[5];
    if(millis() - lastS >= 800)
      msg = 0;
    data[msg] = received_data;
    if(msg == 4)
    {
      int pulseWidth = (data[1] << 8) | data[2];
      
      if((data[3] << 8) | data[4] == 0)
      {
        if(pulseWidth == 10)
        {
          t_data = int(servo[data[0]].isMoving());
          callback();
        }
        else if(pulseWidth == 15)
        {
          t_data = servo[data[0]].read();
          callback();
        }
        else if(pulseWidth == 20)
        {
          t_data = servo[data[0]].readMicroseconds();
          callback();
        }/*
        else if(pulseWidth == 30)
        {
          digitalWrite(23, LOW);
        }
        else if(pulseWidth == 31)
        {
          digitalWrite(23, HIGH);
        }*/
      }
      else
      {
        pulse[data[0]] = pulseWidth;
        speed[data[0]] = (data[3] << 8) | data[4];
      }
      /*
      Serial.print("Pin: ");
      Serial.print(data[0]);
      Serial.print("    Width: ");
      Serial.print(pulseWidth);
      Serial.print("    Speed: ");
      Serial.println((data[3] << 8) | data[4]);
      */
      msg = 0;
    }
    else
      msg++;
  }
  lastS = millis();
}

void callback()
{
  Serial3.write(t_data >> 8);
  Serial3.write(t_data & 0xFF);
}

void setup()
{
  //Serial.begin(250000);
  Serial3.begin(57600);
  //pinMode(23, OUTPUT);
  /*
  for(int i = 0; i < 18; i++)
  {
      servo[i].attach(s_pin[i]);
  }*/
}

void loop()
{/*
  for(int j = 0; j < 180; j++)
  {
  for(int i = 0; i < 18; i++)
  {
      servo[i].write(j);
  }
  delay(5);
  }*/
   if (Serial3.available())
   {
      recv(Serial3.available());
   }
   /*
   if (Serial3.available())
   {
      Serial.write(Serial3.read());
   }*/
   for(int i = 0; i < 18; i++)
  {
    if(pulse[i] != 0)
    {
      
      if(!servo[i].attached())
        servo[i].attach(s_pin[i]);
      servo[i].writeMicroseconds(pulse[i], speed[i]);
    }
    else
    {
      if(servo[i].attached())
        servo[i].detach();
    }
  }
}
