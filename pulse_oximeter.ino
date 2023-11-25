// Author: Rylin Soto
// Date: November 8th 2023
// Version: V0.1.0

// LCD
//  #include <LiquidCrystal.h>
//  LiquidCrystal lcd(8, 7, 5, 4, 3, 2);
//  #define SCREEN_WIDTH 16
//  #define SCREEN_HEIGHT 2

// Definitions
#define NUM_HB_AVERAGES 10 // Number of heart beat periods stored
// #define NUM_R_CONST 10          //Number of R constants stored
#define MAX_R_PERIOD_SIZE 80 // Number of absorbance averages to store
#define RISE_THRESHOLD 3     // Number of averages that are increasing to be considered rising edge

// Global variables
int T = 20; // ms to read values from sensor, take average over 20ms to avoid 50hz noise caused by electric light
float SpO2;
float HR;

// Setup hardware
const int redLED = 3;
const int irLED = 4;
const int photoDiode = A0;

void setup()
{

  // Initialize LEDs
  pinMode(redLED, OUTPUT);
  pinMode(irLED, OUTPUT);

  // Default LEDs to off
  digitalWrite(redLED, LOW);
  digitalWrite(irLED, LOW);

  // Initialize photo diode
  pinMode(photoDiode, INPUT);

  // Initialize serial monitor
  Serial.begin(9600);
  Serial.println("Beginning Oxygen Saturation and heart rate measurments");

  // Initialize LCD
  //  lcd.begin(SCREEN_WIDTH, SCREEN_HEIGHT);
  //  lcd.setCursor(0, 0);
  //  lcd.println("Insert Finger");
}

void error()
{
  Serial.println("Could not get SpO2 and/or hr, trying again...");
}

// void display_LCD(float SpO2, float HR)
// {
//   lcd.setCursor(1,0);
//   lcd.print("BPM: %.0f%" + String(HR));
//   lcd.setCursor(1,8);
//   lcd.print("SpO2: %.2f%", String(SpO2));
// }

void loop()
{
  // *************************** LED pattern variables ***************************
  unsigned long startTime;
  float absorbance;
  float irSum, irBuffer[MAX_R_PERIOD_SIZE], irLast;
  float redSum, redBuffer[MAX_R_PERIOD_SIZE], redLast;

  irSum = 0;
  redSum = 0;

  for (int i = 0; i < MAX_R_PERIOD_SIZE; i++)
  {
    irBuffer[i] = 0;
    redBuffer[i] = 0;
  }

  int n = 0;

  //*************************** SpO2 calc variables ***************************
  int ptr = 0;

  float IRmax = 0;
  float IRmin = 0;
  float REDmax = 0;
  float REDmin = 0;
  float R = 0;
  int sampleCounter = 0;

  //*************************** Heart rate variables ***************************
  float irBefore = 0;
  bool rising = false;
  int rise_count = 0;
  unsigned long last_beat_ms;

  int hbPeriods[NUM_HB_AVERAGES];
  int m = 0;
  for (int i = 0; i < NUM_HB_AVERAGES; i++)
  {
    hbPeriods[i] = 0;
  }

  while (1)
  {
    //*************************** LED Pattern ***************************
    // ir on, red off
    digitalWrite(redLED, LOW);
    digitalWrite(irLED, HIGH);

    // Take average over period (T=20ms) to avoid 50hz noise caused by electric light
    n = 0;
    absorbance = 0.;
    startTime = millis();
    do
    {
      absorbance += analogRead(photoDiode);
      n++;
    } while (millis() < startTime + T);
    absorbance /= n; // Take average
    irLast = absorbance;

    // Repeat with red LED
    digitalWrite(irLED, LOW);
    digitalWrite(redLED, HIGH);

    // Take average over 20ms to avoid 50hz noise caused by electric light
    n = 0;
    startTime = millis();
    absorbance = 0.;
    do
    {
      absorbance += analogRead(photoDiode);
      n++;
    } while (millis() < startTime + T);
    absorbance /= n; // Take average
    redLast = absorbance;

    //*************************** R Parameter Calculation ***************************

    irBuffer[ptr] = irLast;
    redBuffer[ptr] = redLast;
    ptr++;
    ptr %= MAX_R_PERIOD_SIZE;
    sampleCounter++;

    // if saved all the samples of a period, find max and min values and calculate R parameter
    if (sampleCounter >= MAX_R_PERIOD_SIZE)
    {
      sampleCounter = 0;
      IRmax = 0;
      IRmin = 1023;
      REDmax = 0;
      REDmin = 1023; // 10 bit ADC resolution

      for (int i = 0; i < MAX_R_PERIOD_SIZE; i++)
      {
        if (irBuffer[i] > IRmax)
          IRmax = irBuffer[i];
        if (irBuffer[i] > 0 && irBuffer[i] < IRmin)
          IRmin = irBuffer[i];
        irBuffer[i] = 0;

        if (redBuffer[i] > REDmax)
          REDmax = redBuffer[i];
        if (redBuffer[i] > 0 && redBuffer[i] < REDmin)
          REDmin = redBuffer[i];
        redBuffer[i] = 0;
      }

      // R = (ac absorbance red /dc absorbance red) / (ac absorbance IR /dc absorbance IR)
      // DC absorbance cancles out (i.e. non pulsating arterial absorbance)
      R = ((REDmax - REDmin) / REDmin) / ((IRmax - IRmin) / IRmin);
      Serial.println(String(R));
    }

    // *************************** Heart Rate Calculation ***************************
    // check for a rising curve (i.e. heart beat) (only need to check IR light)
    if (irLast > irBefore)
    {
      rise_count++;
      if (!rising && rise_count > RISE_THRESHOLD)
      {
        // detected a rising curve which implies a heartbeat
        // record the time since last beat,
        // keep track of the 10 previous peaks to get an average value

        rising = true;

        hbPeriods[m] = millis() - last_beat_ms;
        last_beat_ms = millis();

        long unsigned avgPeriod = 0;

        // store the number of good measures (not floating more than 10%) in the last 10 peaks
        int numGoodReadings = 0;
        for (int i = 1; i < NUM_HB_AVERAGES; i++)
        {
          if ((hbPeriods[i] < hbPeriods[i - 1] * 1.1) && (hbPeriods[i] > hbPeriods[i - 1] / 1.1))
          {
            numGoodReadings++;
            avgPeriod += hbPeriods[i];
          }
        }

        m++;
        m %= NUM_HB_AVERAGES;

        // *************************** Validating and Sending Values ***************************
        // Record HR if there are at least 5 good measures
        if (numGoodReadings > 4)
        {
          HR = (avgPeriod / numGoodReadings) * 1000; // BPMS to BPM
          SpO2 = 22.6 * R + 95.842;                  // Oxygen saturation curve

          // if((HR > 40 && HR <220) && (SpO2 > 70 && SpO2 <110))
          // {
          Serial.println(String(HR) + "bpm  " + String(SpO2) + "%");

          // display_LCD();
          // }
          // else error();
        }
        else if (numGoodReadings <= 3)
        {
          Serial.println("Insert Finger");
          //  lcd.setCursor(0, 0);
          //  lcd.println("Insert Finger");
        }
      }
    }
    else // Curve is falling
    {
      rising = false;
      rise_count = 0;
    }

    // To compare absorbance level with the current absorbance to determine if rising or falling edge
    irBefore = irLast;
  }
}
