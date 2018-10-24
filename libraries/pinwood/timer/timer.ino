/*================================================================================*
   Pinewood Derby Timer                                Version 2.01 - 6 Jan 2013

   Flexible and affordable Pinewood Derby timer that interfaces with the 
   following software:
     - PD Test/Tune/Track Utility
     - Grand Prix Race Manager software

   Refer to the "PDT_MANUAL.PDF" file for setup and usage instructions.
   Website: www.miscjunk.org/mj/pg_pdt.html


   Copyright (C) 2011-2012 David Gadberry
   
   Ver 2.01 Fixes: Justin Bennett
   Cub Scout Pack 310, San Jacinto, CA, cajeeper@gmail.com

   This work is licensed under the Creative Commons Attribution-NonCommercial-
   ShareAlike 3.0 Unported License. To view a copy of this license, visit 
   http://creativecommons.org/licenses/by-nc-sa/3.0/ or send a letter to 
   Creative Commons, 444 Castro Street, Suite 900, Mountain View, California, 
   94041, USA.
   
   Version 2.01 Release Notes:
   
   Fixed bugs:
    -Resolved 1-3 msec delay between reading lanes resolved that prevented 2 or more lanes from a tie.
    -LED Display decimal point was stuck to < 10 seconds for read outs.
     Added routine to move decimal point over making up to 9999 seconds possible
     and displaying ---- for anything longer.
    -Added routine to sense tripped sensors during begining of race and auto mask the lanes - makes it
     possible to easly customize non-pc connected races by disconnecting phototransistors as needed.
     
 *================================================================================*/

#define LED_DISPLAY  1                 // Enable lane place/time displays
#define SHOW_PLACE   1                 // Show place mode
#define PLACE_DELAY  3                 // Delay (secs) when displaying time/place

#ifdef LED_DISPLAY                     // LED control libraries
#include "Wire.h"
#include "Adafruit_LEDBackpack.h"
#include "Adafruit_GFX.h"
#endif


#define PDT_VERSION  "2.01"            // software version

/*-----------------------------------------*
  - static definitions -
 *-----------------------------------------*/
#define NUM_LANES    3                 // number of lanes

#define MAX_LANES    6                 // maximum number of lanes (Uno)

#define mINIT       -1                 // program modes
#define mREADY       0
#define mRACING      1
#define mFINISH      2

#define START_TRIP   LOW               // start switch trip condition

#define NULL_TIME    9.999             // null (non-finish) time
#define NUM_DIGIT    3                 // timer resolution

#define char2int(c) (c - '0') 

#define DEF_BRIGHT   5                 // default LED display brightness
#define PWM_LED_ON   220
#define PWM_LED_OFF  255

//
// serial messages                        <- to timer
//                                        -> from timer
//
#define SMSG_ACKNW   '.'               // -> acknowledge message

#define SMSG_POWER   'P'               // -> start-up (power on or hard reset)

#define SMSG_CGATE   'G'               // <- check gate
#define SMSG_GOPEN   'O'               // -> gate open

#define SMSG_RESET   'R'               // <- reset
#define SMSG_READY   'K'               // -> ready

#define SMSG_SOLEN   'S'               // <- start solenoid
#define SMSG_START   'B'               // -> race started
#define SMSG_FORCE   'F'               // <- force end

#define SMSG_LMASK   'M'               // <- mask lane
#define SMSG_UMASK   'U'               // <- unmask all lanes

#define SMSG_GVERS   'V'               // <- request timer version
#define SMSG_DEBUG   'D'               // <- toggle debug on/off
#define SMSG_GNUML   'N'               // <- request number of lanes

/*-----------------------------------------*
  - pin assignments -
 *-----------------------------------------*/
byte BRIGHT_LEV   = A0;                // brightness level
byte RESET_SWITCH =  8;                // reset switch
byte STATUS_LED_R =  9;                // status LED (red)
byte STATUS_LED_B = 10;                // status LED (blue)
byte STATUS_LED_G = 11;                // status LED (green)
byte START_GATE   = 12;                // start gate switch
byte START_SOL    = 13;                // start solenoid

//
//                    Lane #    1     2     3     4    
//
int  DISP_ADD [MAX_LANES] = {0x70, 0x71, 0x72};    // display I2C addresses
byte LANE_DET [MAX_LANES] = {   2,    3,    4};    // finish detection pins

/*-----------------------------------------*
  - global variables -
 *-----------------------------------------*/
boolean       fDebug = false;          // debug flag
boolean       ready_first;             // first pass in ready mode flag

unsigned long start_time;              // race start time (milliseconds)
unsigned long lane_timer [MAX_LANES];  // lane timing data (milliseconds)
int           lane_place [MAX_LANES];  // lane finish place
boolean       lane_mask  [MAX_LANES];  // lane mask status
float         lane_time;               // calculated lane time (seconds)

int           lane;                    // lane number
int           finish_order;            // finish order
unsigned long last_finish_time;        // previous finish time
unsigned long last_display_update;     // display update time
int           serial_data;             // serial data
byte          mode;                    // current program mode
int           new_lev, cur_lev;        // LED brightness level

#ifdef LED_DISPLAY                     // LED display control
Adafruit_7segment  disp_mat[MAX_LANES];
#endif

void dbg(int, char * msg, int val=-999);
void smsg(char msg, boolean crlf=true);
void smsg_str(char * msg, boolean crlf=true);

/*================================================================================*
  SETUP
 *================================================================================*/
void setup()
{  
/*-----------------------------------------*
  - hardware setup -
 *-----------------------------------------*/
  pinMode(STATUS_LED_R, OUTPUT);
  pinMode(STATUS_LED_B, OUTPUT);
  pinMode(STATUS_LED_G, OUTPUT);
  pinMode(START_SOL,    OUTPUT);
  pinMode(RESET_SWITCH, INPUT);
  pinMode(START_GATE,   INPUT);
  pinMode(BRIGHT_LEV,   INPUT);
  
  digitalWrite(RESET_SWITCH, HIGH);    // enable pull-up resistor
  digitalWrite(START_GATE,   HIGH);    // enable pull-up resistor

  cur_lev = DEF_BRIGHT;

  for (int n=0; n<NUM_LANES; n++)
  {
#ifdef LED_DISPLAY
    disp_mat[n] = Adafruit_7segment();
    disp_mat[n].begin(DISP_ADD[n]);
    disp_mat[n].setBrightness(cur_lev);
    disp_mat[n].clear();
    disp_mat[n].drawColon(false);
    disp_mat[n].writeDisplay();
#endif

    pinMode(LANE_DET[n], INPUT);

    digitalWrite(LANE_DET[n], HIGH);   // enable pull-up resistor
  }

/*-----------------------------------------*
  - software setup -
 *-----------------------------------------*/
  Serial.begin(9600);
  smsg(SMSG_POWER);

  initialize();
  unmask_all_lanes();
}


/*================================================================================*
  MAIN LOOP
 *================================================================================*/
void loop()
{
    if (digitalRead(START_GATE) != START_TRIP)    // If gate closed, proceed to check reset switch
    {
      if (digitalRead(RESET_SWITCH) == LOW)           // timer reset
      {
        initialize();
      }
    }

    set_led_bright();

    read_serial();

    switch(mode)
    {
      case mREADY:
        mREADY_Funcion();
      case mRACING:
        mRACING_Function();
      case mFINISH:
        mFINISH_Function();
     }
}

/*-----------------------------------------*
  - READY -
 *-----------------------------------------*/
void mREADY_Funcion()
{
 if (mode == mREADY)
  {
    set_status_led(mode);
    clear_led_display(false);
        
    while(!digitalRead(START_GATE) == START_TRIP)   //wait until gate trip
    {    
      read_serial();                         // read serial while waiting  
      if (serial_data == int(SMSG_SOLEN))    // activate start solenoid
      {
        digitalWrite(START_SOL, HIGH);
        smsg(SMSG_ACKNW);
      }
    }
    
    start_time = millis();                 // timer start
    digitalWrite(START_SOL, LOW);
    smsg(SMSG_START);
    delay(100);
    mode = mRACING; 
    
  }
}

/*-----------------------------------------*
  - RACING -
 *-----------------------------------------*/
void mRACING_Function()
{
  {
    boolean finished = false;
	unsigned long tempMillis;              // temp milliseconds holder during loop
	int tempLaneRead[NUM_LANES];
	
	set_status_led(mode);  

        for (int n=0; n<NUM_LANES; n++)        // auto mask
        {
          if (digitalRead(LANE_DET[n]))
          {
            lane_mask[n] = true;
          }
        }
        
        clear_led_display(true);  
    
	while(!finished)
	{
	 
          for (int n=0; n<NUM_LANES; n++)
	  {
            tempLaneRead[n] = digitalRead(LANE_DET[n]);  // read all the lanes in one pass - cut delay
          }
          
	 tempMillis = millis();     // read the time once per loop
	 
         for (int n=0; n<NUM_LANES; n++)
         {
           if (lane_timer[n] == 0 && tempLaneRead[n] == HIGH && !lane_mask[n])    // cross finish line
           {
             lane_timer[n] = tempMillis - start_time; 

             if (lane_timer[n] > last_finish_time)
             {
               finish_order++;
               last_finish_time = lane_timer[n];
             }
             lane_place[n] = finish_order;
             set_led_display(n, lane_place[n], lane_timer[n], SHOW_PLACE);
           }
           
         }
         
         read_serial();                        // read serial data
         if (serial_data == int(SMSG_FORCE))    // check if told to end
         {
           finished = true;
           smsg(SMSG_ACKNW);
           break;
         }
         
	 for (int n=0; n<NUM_LANES; n++)    // check if all finished
         {
           if (lane_timer[n] == 0 && !lane_mask[n])    // not finished or masked
           {
             finished = false;
             break;
	   }
	   else if (digitalRead(RESET_SWITCH) == LOW)
           {
             finished = true;
             break;
           }
           else
	   {
	     finished = true; 
	   }
	 }	 
	 mode = mFINISH;
       }
     }
}

/*-----------------------------------------*
  - FINISHED -
 *-----------------------------------------*/
void mFINISH_Function()
{
    set_status_led(mode);

    for (int n=0; n<NUM_LANES; n++)    // send times
    {
       lane_time = (float)(lane_timer[n] / 1000.0);
         Serial.print(n+1);
         delay(20);
         Serial.print(" - ");
		 delay(20);

         if (lane_timer[n] > 0)           // finished
         {
           Serial.println(lane_time, 3);
		   delay(20);
         }
         else                             // did not finish
         {
           Serial.println(NULL_TIME, 3);
		   delay(20);
         }
    }
    
	boolean reset = false;
	unsigned long displaycounter = millis();
	
        while(!reset)                     // loop until reset switch
	{
          if (displaycounter > 500)
          {
            display_place_time();
          }
          else
          {
            displaycounter = millis();
          }
          
	  if (digitalRead(START_GATE) != START_TRIP) // if gate closed, read timer reset
          {
            if (digitalRead(RESET_SWITCH) == LOW)    // timer reset
            {
              unmask_all_lanes();
              initialize();
              reset = true;
            }
	  }
          read_serial();
          if (serial_data == int(SMSG_RESET))
          {
            initialize();
            reset = true;
          }
	}
} 



/*================================================================================*
  DISPLAY PLACE / TIME
 *================================================================================*/
void display_place_time()
{
  static boolean display_mode;
  unsigned long now;


  if (!SHOW_PLACE) return;

  now = millis();

  if (last_display_update == 0)  // first cycle
  {
    last_display_update = now;
    display_mode = false;
  }

  if ((now - last_display_update) > long(PLACE_DELAY * 1000))
  {
    dbg(fDebug, "display_place_time");

    for (int n=0; n<NUM_LANES; n++)
    {
      if (!lane_mask[n])
      {
        set_led_display(n, lane_place[n], lane_timer[n], display_mode);
      }
    }

    display_mode = !display_mode;
    last_display_update = now;
  }

  return;
}


/*================================================================================*
  SET LED PLACE/TIME DISPLAY
 *================================================================================*/
void set_led_display(int lane, int place, long time, int disp_mode)
{
  char cnum[10];

//  dbg(fDebug, "led: lane = ", lane);
//  dbg(fDebug, "led: plce = ", place);
//  dbg(fDebug, "led: time = ", time);

#ifdef LED_DISPLAY
  disp_mat[lane].clear();

  if (disp_mode)
  {
    if (place == 0)
    {
      disp_mat[lane].print(99999,DEC);
      disp_mat[lane].drawColon(false);
      disp_mat[lane].writeDisplay();
      return;
    }
    else
    {
      sprintf(cnum,"%1d", place);
      disp_mat[lane].writeDigitNum(3, char2int(cnum[0]), false);    // place
      disp_mat[lane].drawColon(false);
      disp_mat[lane].writeDisplay();
    }
  }
  else
  { 	
	if (time < 1)
        {
          disp_mat[lane].print(99999,DEC);
          disp_mat[lane].drawColon(false);
          disp_mat[lane].writeDisplay();
          return;
        }
        else if (time < 10000)                                         // time is less than 10 sec
        {
          sprintf(cnum,"%08d", time);
	  disp_mat[lane].writeDigitNum(0, char2int(cnum[4]), true);    
	  disp_mat[lane].writeDigitNum(1, char2int(cnum[5])); 	
	  disp_mat[lane].writeDigitNum(3, char2int(cnum[6]));    	
	  disp_mat[lane].writeDigitNum(4, char2int(cnum[7]));    
          disp_mat[lane].drawColon(false);
          disp_mat[lane].writeDisplay();
          return;	  
	}
	else if (time < 100000)                                   // time is less than 100 sec
        {
          sprintf(cnum,"%08d", round(time/10));
	  disp_mat[lane].writeDigitNum(0, char2int(cnum[4]));	
	  disp_mat[lane].writeDigitNum(1, char2int(cnum[5]), true);	
	  disp_mat[lane].writeDigitNum(3, char2int(cnum[6]));	
	  disp_mat[lane].writeDigitNum(4, char2int(cnum[7]));
          disp_mat[lane].drawColon(false);
          disp_mat[lane].writeDisplay();
          return;	  
	}
        else if (time < 1000000)                                  // time is less than 1000 sec
        {
          sprintf(cnum,"%08d", round(time/100));
	  disp_mat[lane].writeDigitNum(0, char2int(cnum[4]));	
	  disp_mat[lane].writeDigitNum(1, char2int(cnum[5]));	
	  disp_mat[lane].writeDigitNum(3, char2int(cnum[6]), true);	
	  disp_mat[lane].writeDigitNum(4, char2int(cnum[7]));
          disp_mat[lane].drawColon(false);
          disp_mat[lane].writeDisplay();
          return;	  
	}
        else if (time < 10000000)                                  // time is less than 10000 sec
        {
          sprintf(cnum,"%08d", round(time/1000));
	  disp_mat[lane].writeDigitNum(0, char2int(cnum[4]));	
	  disp_mat[lane].writeDigitNum(1, char2int(cnum[5]));	
	  disp_mat[lane].writeDigitNum(3, char2int(cnum[6]));	
	  disp_mat[lane].writeDigitNum(4, char2int(cnum[7]));
          disp_mat[lane].drawColon(false);
          disp_mat[lane].writeDisplay();
          return;	  
	}
        else                                                      // time is equal to or greater than 10000 sec, so mask the display
        {
          disp_mat[lane].print(99999,DEC);
          disp_mat[lane].drawColon(false);
          disp_mat[lane].writeDisplay();
          return;
        }
  }
#endif

  return;
}


/*================================================================================*
  CLEAR LED PLACE/TIME DISPLAY
 *================================================================================*/
void clear_led_display(boolean mode)
{

  dbg(fDebug, "led: CLEAR");

#ifdef LED_DISPLAY
  for (int n=0; n<NUM_LANES; n++)
  {
    if (!lane_mask[n])
    {
      disp_mat[n].clear();
    }
    
    if (!mode) //true is ready, false is racing
    {
      disp_mat[n].print(99999,DEC);
    }

    disp_mat[n].drawColon(false);
    disp_mat[n].writeDisplay();
  }
#endif

  return;
}


/*================================================================================*
  SET LED DISPLAY BRIGHTNESS
 *================================================================================*/
void set_led_bright()
{
  if (mode == mRACING) return;

#ifdef LED_DISPLAY
  new_lev = int(long(1023 - analogRead(BRIGHT_LEV)) / 1023.0F * 15.0F);

  if (new_lev != cur_lev)
  {
    dbg(fDebug, "led: BRIGHT");

    cur_lev = new_lev;

    for (int n=0; n<NUM_LANES; n++)
    {
      disp_mat[n].setBrightness(cur_lev);
    }
  }
#endif

  return;
}


/*================================================================================*
  SET STATUS LED
 *================================================================================*/
void set_status_led(int mode)
{
  int r_lev, b_lev, g_lev;

  dbg(fDebug, "status led");

  if (mode == mINIT)         // off
  {
    r_lev = PWM_LED_OFF;
    b_lev = PWM_LED_OFF;
    g_lev = PWM_LED_OFF;
  }
  else if (mode == mREADY)   // yellow
  {
    r_lev = PWM_LED_ON;
    b_lev = PWM_LED_OFF;
    g_lev = PWM_LED_ON;
  }
  else if (mode == mRACING)  // green
  {
    r_lev = PWM_LED_OFF;
    b_lev = PWM_LED_OFF;
    g_lev = PWM_LED_ON;
  }
  else if (mode == mFINISH)  // blue
  {
    r_lev = PWM_LED_OFF;
    b_lev = PWM_LED_ON;
    g_lev = PWM_LED_OFF;
  }

  analogWrite(STATUS_LED_R,  r_lev);
  analogWrite(STATUS_LED_B,  b_lev);
  analogWrite(STATUS_LED_G,  g_lev);

  return;
}


/*================================================================================*
  READ SERIAL DATA
 *================================================================================*/
int get_serial_data()
{  
  int data = 0;
  
  if (Serial.available() > 0)
  {
    data = Serial.read();
    dbg(fDebug, "ser rec = ", data);
  }

  return data;
}  

/*================================================================================*
  READ/PROCESS SERIAL DATA
 *================================================================================*/
 void read_serial()
{
  char          tmps[50];                // temp string
  serial_data = get_serial_data();

  if (serial_data == int(SMSG_GVERS))    // get software version
  {
      sprintf(tmps, "vert=%s", PDT_VERSION);
      smsg_str(tmps);
  } 

  if (serial_data == int(SMSG_GNUML))    // get number of lanes
  {
      sprintf(tmps, "numl=%d", NUM_LANES);
      smsg_str(tmps);
  } 

  if (serial_data == int(SMSG_DEBUG))    // toggle debug
  {
    fDebug = !fDebug;
    dbg(true, "toggle debug = ", fDebug);
  } 

  if (serial_data == int(SMSG_RESET))    // timer reset
  {
    if (digitalRead(START_GATE) != START_TRIP)    // only reset if gate closed
    {
      initialize();
    } 
    else
    {
      smsg(SMSG_GOPEN);
    } 
  } 

  if (serial_data == int(SMSG_CGATE))    // check start gate
  {
    if (digitalRead(START_GATE) == START_TRIP)    // gate open
    {
      smsg(SMSG_GOPEN);
    } 
    else
    {
      smsg(SMSG_ACKNW);
    } 
  } 

  if (serial_data == int(SMSG_LMASK))    // lane mask
  {
    delay(100);
    serial_data = get_serial_data();

    lane = serial_data - 48;
    if (lane >= 1 && lane <= NUM_LANES)
    {
      lane_mask[lane-1] = true;

      dbg(fDebug, "set mask on lane = ", lane);
    }
    smsg(SMSG_ACKNW);
  }

  if (serial_data == int(SMSG_UMASK))    // unmask all lanes
  {
    unmask_all_lanes();
    smsg(SMSG_ACKNW);
  }
  
    if (serial_data == int(SMSG_FORCE))    // force end
  {
    unmask_all_lanes();
    smsg(SMSG_ACKNW);
  }
  return;
  
}

/*================================================================================*
  INITIALIZE
 *================================================================================*/
void initialize()
{  
  for (int n=0; n<NUM_LANES; n++)
  {
    lane_timer[n] = 0;
    lane_place[n] = 0;
  }

  start_time = 0;
  finish_order = 0;
  last_finish_time = 0;
  last_display_update = 0;

  set_status_led(mINIT);

  digitalWrite(START_SOL,    LOW);

  smsg(SMSG_READY);
  delay(100);
  Serial.flush();

  ready_first  = true;

  mode = mREADY;

  return;
}


/*================================================================================*
  unmask_all_lanes
 *================================================================================*/
void unmask_all_lanes()
{  
  dbg(fDebug, "unmask all lanes");

  for (int n=0; n<NUM_LANES; n++)
  {
    lane_mask[n] = false;
  }  
}  


/*================================================================================*
  debug
 *================================================================================*/
void dbg(int flag, char * msg, int val)
{  
  char          tmps[50];                // temp string
  if (!flag) return;

  smsg_str("dbg: ", false);
  smsg_str(msg, false);

  if (val != -999)
  {
    sprintf(tmps, "%d", val);
    smsg_str(tmps);
  }
  else
  {
    smsg_str("");
  }

  return;
}


/*================================================================================*
  send serial message
 *================================================================================*/
void smsg(char msg, boolean crlf)
{  
  if (crlf)
  {
    Serial.println(msg);
  }
  else
  {
    Serial.print(msg);
  }

  return;
}


/*================================================================================*
  send serial message
 *================================================================================*/
void smsg_str(char * msg, boolean crlf)
{  
  if (crlf)
  {
    Serial.println(msg);
  }
  else
  {
    Serial.print(msg);
  }

  return;
}
