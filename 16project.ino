#include <Arduino.h>
#include <TM1637Display.h>
#include <pitches.h>
#include <LedControl.h>

// ------------------------[ 타이머/디스플레이/키패드 영역 ]--------------------------

// TM1637 핀 정의
#define CLK 34  
#define DIO 35
TM1637Display display(CLK, DIO);

// 키패드 핀 정의
#define SCL_PIN 36
#define SDO_PIN 37

int oldKey       = -1;       
byte Key;
int inputNumber  = 0;   // 4자리 숫자 저장 (시간:분)
bool isStart     = false;  
bool colonState  = true; 
bool isFinished  = false; 
bool isPaused    = false;  
bool speakerOn   = true;  

// 스피커 핀
int speakerPin = 57;

// 타이머 종료 시 재생할 멜로디
int melody[] = {
  NOTE_AS4, 0, NOTE_C5, NOTE_F5, 0, NOTE_A4, 0, NOTE_C5, NOTE_F5, 0, 0, 0,
  NOTE_C4, 0, NOTE_G4, NOTE_E5, 0, NOTE_D4, 0, NOTE_A4, NOTE_F5, 0, 0, 0,
};
int noteDuration = 6;
int noteLength   = 1000 / noteDuration;

unsigned long lastColonUpdate = 0; 
unsigned long lastTimerUpdate = 0; 

// ------------------------[ MUX + 압력센서 32채널 영역 ]--------------------------
int S0  = 5;  
int S1  = 4;  
int S2  = 3;  
int S3  = 2;  
int En0 = 7;  // Low 시 활성화
int En1 = 6;  // Low 시 활성화

int controlPin[] = {S0, S1, S2, S3, En0, En1}; 
int ADC_pin = A0;  

const int NUM_OF_CH = 32;
int sensor_data[NUM_OF_CH];

// ------------------------[ 초음파 센서 영역 ]--------------------------
int trigPin = 31; 
int echoPin = 30; 

// ------------------------[ MAX7219 (LedControl) 영역 ]--------------------------
// MAX7219 핀 정의 
#define DATA_IN  51   // MOSI (DIN)
#define LOAD     10   // CS
#define CLOCK    52   // SCK (CLK)

// LedControl 객체
LedControl lc = LedControl(DATA_IN, CLOCK, LOAD, 1);

// LED 타이밍 관련 변수
unsigned long totalTimeInMillis = 0;  // 타이머의 전체 시간 (ms)
unsigned long stepTimeInMillis  = 0;  // 1칸(1 LED) 꺼지는 데 필요한 시간 (ms)
unsigned long startTime         = 0;  // 타이머 시작 시점 (millis)
byte         lastFraction       = 0;  // 직전까지 몇 칸 꺼졌는지
int          currentLED         = 0;  // 0 ~ 63 (총 64칸)

// ------------------------[ setup() ]--------------------------
void setup() {
  // 타이머 디스플레이 초기화
  display.setBrightness(0);
  display.showNumberDecEx(0, 0b01000000, true);

  // 키패드 핀
  pinMode(SCL_PIN, OUTPUT);  
  pinMode(SDO_PIN, INPUT);  

  // 시리얼 통신
  Serial.begin(115200);

  // MUX 핀
  pinMode(En0, OUTPUT);
  pinMode(En1, OUTPUT);
  pinMode(S0,  OUTPUT);
  pinMode(S1,  OUTPUT);
  pinMode(S2,  OUTPUT);
  pinMode(S3,  OUTPUT);

  // 스피커 핀
  pinMode(speakerPin, OUTPUT);

  // 초음파 센서 핀
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  // ------------------------[ MAX7219 초기화 ]--------------------------
  lc.shutdown(0, false);    // 전원 On
  lc.setIntensity(0, 8);    // 밝기 (0~15)
  lc.clearDisplay(0);       // 전체 LED 끄기

  // 시작 시엔 일단 전부 꺼놓거나, 전부 켜놓고 싶다면 켜둠
  for(int row=0; row<8; row++){
    lc.setRow(0, row, B11111111); // 전부 켜기
  }
}

// ------------------------[ loop() ]--------------------------
void loop() {
  // ---------- (1) 초음파 센서로 거리 측정 ----------
  digitalWrite(trigPin, HIGH);
  delay(10);
  digitalWrite(trigPin, LOW);
  float duration = pulseIn(echoPin, HIGH);
  float distance = duration * 340.0 / 10000.0 / 2.0;  // 단위: cm
  
  Serial.print("Distance(cm): ");
  Serial.println(distance);

  // 20cm 미만이면 스피커 경고
  if (distance < 20) {
    tone(speakerPin, 1500, 150);
    delay(200);
    noTone(speakerPin);
  }

  // ---------- (2) MUX로 압력센서 32채널 읽기 ----------
  for(int ch = 0; ch < NUM_OF_CH; ch++){
    sensor_data[ch] = readMux(ch);
  }

  // 센서값 시리얼 출력
  for(int ch = 0; ch < NUM_OF_CH; ch++){
    Serial.print(sensor_data[ch]);
    Serial.print(",");
  }
  Serial.println();

  // 스피커 경고 (30으로 설정함)
  for(int ch = 0; ch < NUM_OF_CH; ch++){
    if(speakerOn && sensor_data[ch] >= 30) {
      tone(speakerPin, 1000, 100);
      delay(150);
      noTone(speakerPin);
      // break; // 한 번만 울리고 싶으면 break
    }
  }

  // ---------- (3) 타이머/키패드 기능 ----------
  if (isStart && !isPaused) {
    Key = Read_Keypad(); 
    if (Key >= 1 && Key <= 10) {
      Key = 0; 
    }
  } else {
    Key = Read_Keypad(); 
  }

  // 키패드 입력 처리
  if (Key != 0) {
    Serial.print("Key pressed: "); 
    Serial.println(Key);

    if (Key >= 1 && Key <= 9) { 
      if (isFinished) { 
        inputNumber = 0;
        isFinished = false;
        colonState = true;
      }
      inputNumber = ((inputNumber % 1000) * 10) + Key; 
      showTime(inputNumber, colonState);

    } else if (Key == 10) { // 10 -> 0
      if (isFinished) {
        inputNumber = 0;
        isFinished = false;
        colonState = true;
      }
      inputNumber = ((inputNumber % 1000) * 10);
      showTime(inputNumber, colonState);

    } else if (Key == 11) { // 스피커 On
      tone(speakerPin, NOTE_A4, noteLength);
      delay(noteLength);
      noTone(speakerPin);
      speakerOn = true;

    } else if (Key == 12) { // 스피커 Off
      tone(speakerPin, NOTE_G4, noteLength);
      delay(noteLength);
      noTone(speakerPin);
      speakerOn = false;

    } else if (Key == 13) { // Start / Pause
      toggleTimer();

    } else if (Key == 14) { // Reset
      resetTimer();

    } else if (Key == 15) { 
      Serial.println("Key 15 pressed.");

    } else if (Key == 16) { 
      Serial.println("Key 16 pressed.");
    }

    delay(200); // 디바운싱
  }

  // 타이머 동작(카운트다운)
  if (isStart && !isPaused) {
    countdownTimer();
  }

  // ---------- (4) LED매트릭스: "총 시간의 1/64 지날 때마다 한 칸씩 꺼지기" ----------
  // 타이머가 진행중(isStart && !isPaused && 아직 안 끝남)이고,
  // totalTimeInMillis가 0이 아니라면, 경과시간에 따라 LED를 끔.
  if (isStart && !isPaused && !isFinished && (totalTimeInMillis > 0)) {
    unsigned long elapsed = millis() - startTime;
    // 1칸씩 꺼져야 할 총 분기(0 ~ 63) 중 몇 번째 분기까지 왔는지 계산
    // (fraction=5라면 현재 5칸 꺼져야 함)
    byte fraction = elapsed / stepTimeInMillis;  
    if (fraction > 64) fraction = 64;  // 최대 64칸

    // fraction이 증가했다면, 증가한 수만큼 LED를 끔
    while (fraction > lastFraction && currentLED < 64) {
      int row = currentLED / 8;
      int col = currentLED % 8;
      lc.setLed(0, row, col, false);

      currentLED++;
      lastFraction++;
    }
  }

  // 전체 속도 조절
  delay(100);
}

// ------------------------[ 함수 정의들 ]--------------------------

// 키패드 값 읽기
byte Read_Keypad(void) {
  byte Count;
  byte Key_State = 0;

  for (Count = 1; Count <= 16; Count++) {
    digitalWrite(SCL_PIN, LOW);
    if (!digitalRead(SDO_PIN))
      Key_State = Count;
    digitalWrite(SCL_PIN, HIGH);
  }
  return Key_State;
}

// 시간을 디스플레이에 출력
void showTime(int number, bool colon) {
  int hours = number / 100;
  int minutes = number % 100;
  display.showNumberDecEx(hours * 100 + minutes, colon ? 0b01000000 : 0, true);
}

// 타이머 카운트다운 (1분 단위)
void countdownTimer() {
  if (isFinished) {
    colonState = false;
    showTime(inputNumber, colonState);
    Serial.println("Time finished.");
    return;
  }

  // 1초마다 콜론 깜빡임
  if (millis() - lastColonUpdate >= 1000 && !isFinished && !isPaused) {
    colonState = !colonState;
    showTime(inputNumber, colonState);
    lastColonUpdate = millis();
  }

  // 00:00이면 타이머 종료
  int hours = inputNumber / 100;
  int minutes = inputNumber % 100;
  if (hours == 0 && minutes == 0) {
    isStart = false;
    isFinished = true;
    colonState = false;
    showTime(0, colonState);
    Serial.println("Timer finished.");

    // 스피커 On 상태일 때 멜로디 재생
    if (speakerOn) {
      for (int thisNote = 0; thisNote < (int)(sizeof(melody) / sizeof(int)); thisNote++) {
        tone(speakerPin, melody[thisNote], noteLength);
        delay(noteLength);
        noTone(speakerPin);
      }
    }
    return;
  }

  // 1분마다 감소
  if (millis() - lastTimerUpdate >= 60000) {
    if (minutes == 0) {
      hours--;
      minutes = 59;
    } else {
      minutes--;
    }
    inputNumber = hours * 100 + minutes;
    showTime(inputNumber, colonState);
    lastTimerUpdate = millis();
  }
}

// 타이머 Start / Pause
void toggleTimer() {
  // 타이머 시간(= 입력값)이 0이면 무시
  if (inputNumber == 0) {
    return;
  }

  if (isStart) {
    // 이미 동작 중이면 Pause/Resume
    isPaused = !isPaused;
    if (isPaused) {
      Serial.println("Timer paused.");
    } else {
      Serial.println("Timer resumed.");
      // 일시정지 해제 시, startTime 갱신(누적 시간 보정)을 하려면 별도 로직 필요 
      // (본 예제는 간단히 그대로 진행)
    }
  } else {
    // 타이머가 처음 시작될 때 => 총 시간 계산 후, LED 관련 변수 초기화
    int hours = inputNumber / 100;
    int minutes = inputNumber % 100;
    unsigned long totalTimeInMinutes = (hours * 60UL) + (minutes);
    totalTimeInMillis = totalTimeInMinutes * 60UL * 1000UL;

    // 혹시 전체 타임이 너무 짧아 64등분이 불가능하면(=stepTimeInMillis=0이 되면)
    // 64등분이 조금이라도 되도록 최소값 지정
    if (totalTimeInMillis < 64) {
      totalTimeInMillis = 64;
    }

    stepTimeInMillis = totalTimeInMillis / 64;
    startTime        = millis();
    lastFraction     = 0;
    currentLED       = 0;

    // LED 전체 켜기(매트릭스 초기화)
    for(int row=0; row<8; row++){
      lc.setRow(0, row, B11111111); 
    }

    // 타이머 플래그 세팅
    isStart          = true;
    isPaused         = false;
    lastTimerUpdate  = millis();

    Serial.println("Timer started.");
  }
}

// 타이머 Reset
void resetTimer() {
  inputNumber  = 0;
  isStart      = false;
  isPaused     = false;
  isFinished   = false;
  colonState   = true;
  showTime(inputNumber, colonState);
  Serial.println("Timer reset.");

  // LED도 다시 전부 켬
  for(int row=0; row<8; row++){
    lc.setRow(0, row, B11111111); 
  }
  currentLED    = 0;
  lastFraction  = 0;
}

// ------------------------[ MUX 읽기 함수 ]--------------------------
int readMux(int channel){
  // {S0, S1, S2, S3, En0, En1}
  static int muxChannel[NUM_OF_CH][6] = {
    {0,0,0,0,0,1}, // 0
    {0,0,0,1,0,1}, // 1
    {0,0,1,0,0,1}, // 2
    {0,0,1,1,0,1}, // 3
    {0,1,0,0,0,1}, // 4
    {0,1,0,1,0,1}, // 5
    {0,1,1,0,0,1}, // 6
    {0,1,1,1,0,1}, // 7
    {1,0,0,0,0,1}, // 8
    {1,0,0,1,0,1}, // 9
    {1,0,1,0,0,1}, // 10
    {1,0,1,1,0,1}, // 11
    {1,1,0,0,0,1}, // 12
    {1,1,0,1,0,1}, // 13
    {1,1,1,0,0,1}, // 14
    {1,1,1,1,0,1}, // 15
    {0,0,0,0,1,0}, // 16
    {0,0,0,1,1,0}, // 17
    {0,0,1,0,1,0}, // 18
    {0,0,1,1,1,0}, // 19
    {0,1,0,0,1,0}, // 20
    {0,1,0,1,1,0}, // 21
    {0,1,1,0,1,0}, // 22
    {0,1,1,1,1,0}, // 23
    {1,0,0,0,1,0}, // 24
    {1,0,0,1,1,0}, // 25
    {1,0,1,0,1,0}, // 26
    {1,0,1,1,1,0}, // 27
    {1,1,0,0,1,0}, // 28
    {1,1,0,1,1,0}, // 29
    {1,1,1,0,1,0}, // 30
    {1,1,1,1,1,0}  // 31
  };

  // 핀 세팅
  for(int i = 0; i < 6; i++){
    digitalWrite(controlPin[i], muxChannel[channel][i]);
  }
  // 아날로그 읽기
  int adc_value = analogRead(ADC_pin);
  return adc_value;
}