//header = 0: goto Mode 0
//header = 1: transmit payload to Serial3 (to FPGA)

void serialInterpret(uint8_t header,uint8_t payload){
if(header == 0){
  while(Serial.available()){Serial.read();}
  loaderMode = 0;  
  }
if(header == 1){
Serial3.write(payload);  
  }
}

void clearSerialBuffer(){
while(Serial.available()){
  Serial.read();    
  }
}

