#include <Arduino.h>
#include <U8g2lib.h>
#include <bitset>
#include <STM32FreeRTOS.h>

//Execution time measurement defines
//Uncomment to enable testing modes
//#define TEST_SCANKEYS      //Test scanKeysTask execution time
//#define DISABLE_THREADS    //Disable normal thread creation during testing
//#define DISABLE_ISRS       //Disable ISRs during task testing

//Constants
  const uint32_t interval = 100; //Display update interval

//Pin definitions
  //Row select and enable
  const int RA0_PIN = D3;
  const int RA1_PIN = D6;
  const int RA2_PIN = D12;
  const int REN_PIN = A5;

  //Matrix input and output
  const int C0_PIN = A2;
  const int C1_PIN = D9;
  const int C2_PIN = A6;
  const int C3_PIN = D1;
  const int OUT_PIN = D11;

  //Audio analogue out
  const int OUTL_PIN = A4;
  const int OUTR_PIN = A3;

  //Joystick analogue in
  const int JOYY_PIN = A0;
  const int JOYX_PIN = A1;

  //Output multiplexer bits
  const int KNOB_MODE = 2;
  const int DEN_BIT = 3;
  const int DRST_BIT = 4;
  const int HKOW_BIT = 5;
  const int HKOE_BIT = 6;

//Display driver object
U8G2_SSD1305_128X32_ADAFRUIT_F_HW_I2C u8g2(U8G2_R0);

//Step sizes for notes C, C#, D, D#, E, F, F#, G, G#, A, A#, B
//Calculated using S = 2^32 * f / 22000 where f is note frequency
//A4 = 440Hz at index 9, equal temperament (semitone ratio = 2^(1/12) ≈ 1.059463)
const uint32_t stepSizes[] = {51076057, 54113183, 57330935, 60740010, 64351798,
                              68178355, 72232452, 76527617, 81078186, 85899345,
                              91007186, 96418755};

//Global variable for current step size - accessed by main loop and ISR
volatile uint32_t currentStepSize = 0;

//System state struct shared between threads
struct {
  std::bitset<32> inputs;
  SemaphoreHandle_t mutex;
  uint8_t knobRotation;  //Volume level 0-8 for log taper control
} sysState;

//Execution time measurement storage
uint32_t executionTimeUs = 0;  //Stored execution time in microseconds
bool executionTimeValid = false;  //Flag indicating valid measurement

//Hardware timer for audio sample generation (22kHz)
HardwareTimer sampleTimer(TIM1);



//Function to set outputs using key matrix
void setOutMuxBit(const uint8_t bitIdx, const bool value) {
      digitalWrite(REN_PIN,LOW);
      digitalWrite(RA0_PIN, bitIdx & 0x01);
      digitalWrite(RA1_PIN, bitIdx & 0x02);
      digitalWrite(RA2_PIN, bitIdx & 0x04);
      digitalWrite(OUT_PIN,value);
      digitalWrite(REN_PIN,HIGH);
      delayMicroseconds(2);
      digitalWrite(REN_PIN,LOW);
}

void setRow(uint8_t rowIdx){
  digitalWrite(REN_PIN, LOW);
  digitalWrite(RA0_PIN, rowIdx & 0x01);
  digitalWrite(RA1_PIN, (rowIdx >> 1) & 0x01);
  digitalWrite(RA2_PIN, (rowIdx >> 2) & 0x01);
  digitalWrite(REN_PIN, HIGH);
}

std::bitset<4> readCols(){
  std::bitset<4> result;
  result[0] = digitalRead(C0_PIN);
  result[1] = digitalRead(C1_PIN);
  result[2] = digitalRead(C2_PIN);
  result[3] = digitalRead(C3_PIN);
  return result;
}

void sampleISR() {
  static uint32_t phaseAcc = 0;
  phaseAcc += currentStepSize;
  int32_t Vout = (phaseAcc >> 24) - 128;
  
  // Apply log taper volume control using atomic access
  // Volume range 0-8: Vout >> (8 - rotation)
  // Rotation 8 = no shift (full volume)
  // Rotation 0 = shift by 8 (minimum volume)
  uint8_t rotation = __atomic_load_n(&sysState.knobRotation, __ATOMIC_RELAXED);
  if(rotation > 0 && rotation <= 8) {
    Vout = Vout >> (8 - rotation);
  } else if(rotation == 0) {
    Vout = Vout >> 8;  //Minimum volume
  }
  //If rotation > 8, use full volume (no shift)
  
  analogWrite(OUTR_PIN, Vout + 128);
}



void scanKeysTask(void * pvParameters) {
  const TickType_t xFrequency = pdMS_TO_TICKS(20); //20ms interval for better knob response
  TickType_t xLastWakeTime = xTaskGetTickCount();
  static const char* notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  
  //Knob 3 state machine variables (row 3, columns 0-1)
  static uint8_t knob3PrevState = 0;
  static int8_t knob3LastDirection = 0;  //+1 for CW, -1 for CCW, 0 for unknown
  
  //Test mode iteration counter
  static uint32_t testIteration = 0;
  
  while(1) {
    //In test mode, skip delay and run only once per call
    #ifndef TEST_SCANKEYS
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
    #endif
    
    std::bitset<32> localInputs;
    uint8_t knob3CurrentState = 0;
    
    //Scan key matrix rows 0-3 (extended to include knob row)
    for(int i = 0; i < 4; i++){
      setRow(i);
      delayMicroseconds(3);
      std::bitset<4> cols = readCols();
      localInputs[i*4] = cols[0];
      localInputs[i*4+1] = cols[1];
      localInputs[i*4+2] = cols[2];
      localInputs[i*4+3] = cols[3];
      
      //Capture Knob 3 state from row 3, columns 0-1
      if(i == 3){
        uint8_t knobA = cols[0];  //Column 0
        uint8_t knobB = cols[1];  //Column 1
        knob3CurrentState = (knobA << 1) | knobB;
      }
    }
    
    //Update currentStepSize based on pressed keys
    uint32_t localStepSize = 0;
    int pressedKey = -1;
    for(int i = 0; i < 12; i++){
      if(!localInputs[i]){  //Keys are active low (0 when pressed)
        localStepSize = stepSizes[i];
        pressedKey = i;
        break;  //Use first pressed key
      }
    }
    
    //Atomic store for ISR synchronization
    __atomic_store_n(&currentStepSize, localStepSize, __ATOMIC_RELAXED);
    
    //Knob 3 decoding with state machine
    uint8_t currentRotation = 4;  //Default middle position
    if(sysState.mutex != NULL){
      if(xSemaphoreTake(sysState.mutex, pdMS_TO_TICKS(5)) == pdTRUE){
        currentRotation = sysState.knobRotation;
        xSemaphoreGive(sysState.mutex);
      }
    }
    
    if(knob3CurrentState != knob3PrevState){
      //State transition table from LabPart2.md
      //Only change rotation when input A toggles (valid detent positions)
      int8_t direction = 0;
      
      //Valid transitions (when A toggles)
      uint8_t transition = (knob3PrevState << 2) | knob3CurrentState;
      switch(transition){
        case 0b0001:  //00 -> 01: CW, A toggled
        case 0b1110:  //11 -> 10: CW, A toggled
          direction = 1;
          break;
        case 0b0100:  //01 -> 00: CCW, A toggled
        case 0b1011:  //10 -> 11: CCW, A toggled
          direction = -1;
          break;
        case 0b0011:  //00 -> 11: impossible
        case 0b1100:  //11 -> 00: impossible
        case 0b0101:  //01 -> 01: no change
        case 0b1010:  //10 -> 10: no change
          //Use last known direction for impossible transitions
          direction = knob3LastDirection;
          break;
        default:
          //Intermediate states - no rotation
          direction = 0;
          break;
      }
      
      //Update rotation with limits 0-8
      if(direction != 0){
        int16_t newRotation = (int16_t)currentRotation + direction;
        if(newRotation < 0) newRotation = 0;
        if(newRotation > 8) newRotation = 8;
        currentRotation = (uint8_t)newRotation;
        knob3LastDirection = direction;
      }
      
      knob3PrevState = knob3CurrentState;
    }
    
    //Update shared sysState with mutex protection
    if(sysState.mutex != NULL){
      if(xSemaphoreTake(sysState.mutex, pdMS_TO_TICKS(5)) == pdTRUE){
        sysState.inputs = localInputs;
        sysState.knobRotation = currentRotation;
        xSemaphoreGive(sysState.mutex);
      }
    }
    
    //Debug output (rate limited to reduce overhead)
    #ifndef TEST_SCANKEYS
    static uint32_t debugCount = 0;
    if(++debugCount >= 25){  //Print every 500ms (25 * 20ms)
      Serial.print("[SCAN] Keys:");
      for(int i = 0; i < 12; i++){
        Serial.print(!localInputs[i] ? "1" : "0");
      }
      Serial.print(" Step:");
      Serial.print(localStepSize);
      Serial.print(" Vol:");
      Serial.print(currentRotation);
      if(pressedKey >= 0){
        Serial.print(" Note:");
        Serial.print(notes[pressedKey]);
      }
      Serial.println();
      debugCount = 0;
    }
    #endif
    
    //In test mode, exit after single iteration
    #ifdef TEST_SCANKEYS
    testIteration++;
    break;
    #endif
  }
}

void displayUpdateTask(void * pvParameters) {
  const TickType_t xFrequency = pdMS_TO_TICKS(100); //100ms interval
  TickType_t xLastWakeTime = xTaskGetTickCount();
  static const char* notes[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
  
  while(1) {
    vTaskDelayUntil(&xLastWakeTime, xFrequency);
    
    //Read shared sysState with mutex protection
    std::bitset<32> localInputs;
    uint8_t rotation = 4;  //Default middle position
    if(sysState.mutex != NULL){
      if(xSemaphoreTake(sysState.mutex, portMAX_DELAY) == pdTRUE){
        localInputs = sysState.inputs;
        rotation = sysState.knobRotation;
        xSemaphoreGive(sysState.mutex);
      }
    }
    
    //Update display
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.setCursor(2, 10);
    
    bool keyPressed = false;
    for(int i = 0; i < 12; i++){
      if(!localInputs[i]){  //Keys are active low (0 when pressed)
        u8g2.print(notes[i]);
        keyPressed = true;
        break;  //Show first pressed key
      }
    }
    if(!keyPressed){
      u8g2.print("-");
    }
    
    //Display rotation/volume level (0-8)
    u8g2.setCursor(2, 25);
    u8g2.print("Vol:");
    u8g2.print(rotation);
    
    //Display execution time if valid (measured during testing)
    if(executionTimeValid){
      u8g2.setCursor(66, 25);
      u8g2.print("T:");
      u8g2.print(executionTimeUs);
      u8g2.print("us");
    }
    
    u8g2.sendBuffer();
    
    //Toggle LED
    digitalToggle(LED_BUILTIN);
  }
}

void setup() {
  // put your setup code here, to run once:

  //Set pin directions
  pinMode(RA0_PIN, OUTPUT);
  pinMode(RA1_PIN, OUTPUT);
  pinMode(RA2_PIN, OUTPUT);
  pinMode(REN_PIN, OUTPUT);
  pinMode(OUT_PIN, OUTPUT);
  pinMode(OUTL_PIN, OUTPUT);
  pinMode(OUTR_PIN, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);

  pinMode(C0_PIN, INPUT);
  pinMode(C1_PIN, INPUT);
  pinMode(C2_PIN, INPUT);
  pinMode(C3_PIN, INPUT);
  pinMode(JOYX_PIN, INPUT);
  pinMode(JOYY_PIN, INPUT);

  //Initialise display
  setOutMuxBit(DRST_BIT, LOW);  //Assert display logic reset
  delayMicroseconds(2);
  setOutMuxBit(DRST_BIT, HIGH);  //Release display logic reset
  u8g2.begin();
  setOutMuxBit(DEN_BIT, HIGH);  //Enable display power supply
  setOutMuxBit(KNOB_MODE, HIGH);  //Read knobs through key matrix

  //Initialise UART
  Serial.begin(9600);
  Serial.println("Hello World");
  
  //Configure sample timer for 22kHz audio generation
  sampleTimer.setOverflow(22000, HERTZ_FORMAT);
  #ifndef DISABLE_ISRS
  sampleTimer.attachInterrupt(sampleISR);
  #endif
  sampleTimer.resume();

  //Create mutex for sysState protection
  sysState.mutex = xSemaphoreCreateMutex();
  if(sysState.mutex == NULL){
    Serial.println("Failed to create mutex!");
    while(1); //Halt if mutex creation fails
  }
  
  //Initialize knob rotation to middle position (4 out of 0-8)
  sysState.knobRotation = 4;

  #ifdef TEST_SCANKEYS
  //Execution time measurement for scanKeysTask
  Serial.println("\n=== Testing scanKeysTask Execution Time ===");
  Serial.println("Running 32 iterations for averaging...");
  
  uint32_t startTime = micros();
  for(int iter = 0; iter < 32; iter++){
    scanKeysTask(NULL);
  }
  uint32_t endTime = micros();
  uint32_t totalTime = endTime - startTime;
  uint32_t avgTime = totalTime / 32;
  
  //Store execution time for later use
  executionTimeUs = avgTime;
  executionTimeValid = true;
  
  Serial.print("Total time for 32 iterations: ");
  Serial.print(totalTime);
  Serial.println(" us");
  Serial.print("Average time per iteration: ");
  Serial.print(avgTime);
  Serial.println(" us");
  Serial.print("Stored executionTimeUs: ");
  Serial.print(executionTimeUs);
  Serial.println(" us");
  Serial.println("=== Test Complete ===\n");
  Serial.println("Execution time stored in executionTimeUs variable");
  
  //Halt after testing
  while(1){
    digitalToggle(LED_BUILTIN);
    delay(500);
  }
  #endif

  #ifndef DISABLE_THREADS
  //Create threads with optimized stack sizes
  //scanKeysTask: 128 words (512 bytes) - includes knob decoding
  //displayUpdateTask: 128 words (512 bytes) - display library is efficient
  TaskHandle_t scanKeysHandle = NULL;
  xTaskCreate(
    scanKeysTask,
    "scanKeys",
    128,
    NULL,
    2,    //Higher priority
    &scanKeysHandle
  );

  TaskHandle_t displayUpdateHandle = NULL;
  xTaskCreate(
    displayUpdateTask,
    "displayUpdate",
    128,
    NULL,
    1,    //Lower priority
    &displayUpdateHandle
  );
  #endif

  //Start scheduler
  vTaskStartScheduler();
}

void loop() {
  //Empty - tasks run via FreeRTOS scheduler
}