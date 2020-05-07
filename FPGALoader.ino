#include <SPI.h>
#include "string.h"

#define INCREMENT_ADDRESS      1
#define NON_INCREMENT_ADDRESS   0
#define UPLOADTIMEOUT 5000

#define DEBUG;
//Since Bistream Loader and NIOS Loader use the same SPI, define a diferent CS Signal
#define avalonCS 31 //PB12//was 8
//HIGH->LOW->HIGH puts FPGA On Reset+Programming Mode
#define nCONFIG 2 //PB2 [IS BOOT1]//was 7
//resetReq=HIGH sends reset request to NIOS [Keep HIGH for Programing of NIOS]
#define resetReq 27 //PA8//was 9
//resetDone = HIGH[1 cycle only] : NIOS is in RESET Mode, you may upload code
#define resetDone 3 //PB0 NOT ON FPGA
//Bitream size for EP4CE6
#define BITSTREAMSIZE  368011
//ON-CHIP mem Size
#define RAMSIZE  20480
//ON-CHIP Avalon Base Address
#define RAMbase  0X00000000 
//NIOS MULTIPLE BYTES UPLOAD     
#define byteChain 1      

//flag to assert nConfig ONCE [akso used on NIOS upload to control CS signal]
uint8_t initiateflag = 0;

//Counter to end the BitStream/Code Load [will i use it on NIOS Loader aswell????  -> YEP!]
uint32_t byteCount = 0;

//for debug purposes, mode select 0=idle/check for command, 1=BitStream Loader, 2=Serial Passthrough, 3=NIOS Loader
uint8_t loaderMode = 0;

String mess1;

uint32_t timeCount1 = 0;

void setup() {
Serial3.begin(115200);
pinMode(PB1, INPUT);       //Led PIN TO FLOATING INPUT SO IT CAN BE USED BY FPGA
pinMode(avalonCS,OUTPUT_OPEN_DRAIN);   //SPI CS 
digitalWrite(avalonCS,HIGH);
pinMode(resetReq, OUTPUT); //on board pull this LOW!!!!!!!!
digitalWrite(resetReq,LOW);
pinMode(resetDone, INPUT_PULLDOWN);
pinMode(nCONFIG,OUTPUT);
Serial.begin(921600);
//Serial.begin(115200);

//In final design this should change to SPI 2  
SPI_2.begin(); //Initialize the SPI_1 port.
SPI_2.setBitOrder(LSBFIRST); // Set the SPI_1 bit order
SPI_2.setDataMode(SPI_MODE0);
SPI_2.setClockDivider(SPI_CLOCK_DIV8);      // Slow speed (72 / 16 = 4.5 MHz SPI_1 speed) this was 16 for SPI1, now for SPI2 goes to 8 to initiate the same Freq.

  //gpio_write_bit(GPIOB,9,1);
  //delay(500);
  //gpio_write_bit(GPIOB,9,0);  

//Interrupt for resetDone NIOS Loader
//attachInterrupt(resetDone, resetDoneISR,RISING);
}

void loop() {
//if waiting
if(loaderMode==0){
  if(Serial.available()>8){
    mess1 = Serial.readString();  
    if(mess1.equals("SETMODE0\r\n")){
      loaderMode=0;
      Serial.println("Mode Set to 0");  
      }  
    if(mess1.equals("SETMODE1\r\n")){
      loaderMode=1;  
      Serial.println("Mode Set to 1"); 
      }   
    if(mess1.equals("SETMODE2\r\n")){
      loaderMode=2;  
      Serial.println("Mode Set to 2"); 
      }
    if(mess1.equals("SETMODE3\r\n")){
      loaderMode=3;  
      Serial.println("Mode Set to 3"); 
      }
    if(mess1.equals("CONNOK?\r\n")){
      clearSerialBuffer();
      Serial.println("OK"); 
      }       
   }
}  

//If Loading BitStream to FPGA
if(loaderMode==1){
  if(timeCount1==0){
    timeCount1 = millis();
  }
  pinMode(PB1, OUTPUT);
  if(Serial.available()){
    if(!initiateflag){
    digitalWrite(nCONFIG,LOW);
    delay(2);
    digitalWrite(nCONFIG,HIGH);
    delay(2);
    initiateflag = 1; 
    }
  uint8_t tempBuff = Serial.read();
  digitalWrite(PB1,HIGH);
  SPI_2.transfer(tempBuff);
  digitalWrite(PB1,LOW);
  byteCount++;
  timeCount1 = 0;
  }
  if(byteCount==BITSTREAMSIZE){
    Serial.print("Transfer of ");
    Serial.print(byteCount);
    Serial.println(" Bytes to FPGA Fabric Completed, Have a Nice day... :D");
    initiateflag = 0;
    byteCount = 0;
    loaderMode = 0;
  }
pinMode(PB1, INPUT);
if(millis() >= (timeCount1 + UPLOADTIMEOUT) && timeCount1!=0){
  Serial.print("FAILED!");
  initiateflag = 0;
  byteCount = 0;
  loaderMode = 0;
  timeCount1 = 0;
  clearSerialBuffer();
  }
}

//if using As Console Passthrough
if(loaderMode==2){
    uint8_t header = 0;
    uint8_t payload = 0;
  if(Serial3.available()){
    Serial.write(Serial3.read());  
  }  
  if(Serial.available()>1){    
  header = Serial.read();
  payload = Serial.read();
  serialInterpret(header,payload);
  }
}

if(loaderMode==3){
  pinMode(PB1, OUTPUT);
  if(Serial.available()){
    if(!initiateflag){
      SPI_2.setClockDivider(SPI_CLOCK_DIV32);
      //digitalWrite(avalonCS,LOW); 
      digitalWrite(PB1,HIGH);
      SPI_2.setBitOrder(MSBFIRST);
      digitalWrite(resetReq,HIGH);
      delay(10);
      digitalWrite(avalonCS,LOW);
      delay(1);  
      initiateflag=1;
    }
    unsigned char tempBuff[1];
    int FAILCOUNT = 0;
    int FAILPACKET = 0;
    tempBuff[0] = Serial.read();
    
//    if(byteCount%1000 == 0){
//    Serial.print("Prepariing to Send Byte ");
//    Serial.print(byteCount);
//    Serial.print(" With Value ");
//    Serial.print(tempBuff[0]);
//    Serial.print(" To Address ");
//    Serial.println(RAMbase+byteCount,HEX);
//    } 

    if(!transaction_channel_write(RAMbase+byteCount,1,tempBuff,NON_INCREMENT_ADDRESS)){
    FAILCOUNT++;
    FAILPACKET = byteCount;  
    }
   
    byteCount++;
    if(byteCount==RAMSIZE){
      Serial.print("Upload of ");
      Serial.print(byteCount);
      Serial.print(" Bytes with ");
      Serial.print(FAILCOUNT);
      Serial.print(" Failed Responses, (LAST ONE)At Packet ");
      Serial.print(FAILPACKET);
      Serial.println(" , Completed");
      digitalWrite(avalonCS,HIGH);
      digitalWrite(resetReq,LOW);
      initiateflag = 0;
      byteCount = 0;
      loaderMode = 0;
      FAILCOUNT = 0;
      SPI_2.transfer(0x00);
      SPI_2.setBitOrder(LSBFIRST);
      SPI_2.setClockDivider(SPI_CLOCK_DIV16);
      digitalWrite(PB1,LOW); 
    }    
  }
pinMode(PB1, INPUT);
}
}

void resetDoneISR(){
Serial.println("resetDone Signal detected");  
}

