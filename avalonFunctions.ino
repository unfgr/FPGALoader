#include <stdlib.h>
#include <stdio.h>
#include <string.h>

// ----------------------------------------
// Define to use static or dynamic memory 
// ----------------------------------------
//#define DYNAMIC_MEMORY_ALLOC
#define STATIC_MEMORY_ALLOC

// ------------------------------------
// Transaction opcodes
// ------------------------------------
#define SEQUENTIAL_WRITE 0x04
#define SEQUENTIAL_READ  0x14
#define NON_SEQUENTIAL_WRITE 0x00
#define NON_SEQUENTIAL_READ  0x10

#define HEADER_LEN 8
#define RESPONSE_LEN 4

//------------------------------------
// Special packet characters
//------------------------------------
#define SOP     0x7a
#define EOP     0x7b
#define CHANNEL 0x7c
#define ESC     0x7d

//------------------------------------
// Special packet characters
//------------------------------------
#define BYTESIDLECHAR 0x4a
#define BYTESESCCHAR  0x4d


#ifdef STATIC_MEMORY_ALLOC  /* Buffer datasize allocated is sufficient for up to 1K data transaction only */
#define TRANSACTION_BUFFER_LENGTH   128  /* 1K data + Header length 8 */
unsigned char transaction_buffer[TRANSACTION_BUFFER_LENGTH]; 
unsigned char response_buffer[TRANSACTION_BUFFER_LENGTH];
#define PACKET_BUFFER_LENGTH    256  /* TRANSACTION_BUFFER_LENGTH * 2 (for special characters) + 4 (EOP/SOP/CHAN/ESC) */
unsigned char send_packet_buffer[PACKET_BUFFER_LENGTH];
unsigned char response_packet_buffer[PACKET_BUFFER_LENGTH];
#define BYTE_BUFFER_LENGTH    512  /* PACKET_BUFFER_LENGTH * 2 (for special characters) */
unsigned char send_byte_buffer[BYTE_BUFFER_LENGTH];
unsigned char response_byte_buffer[BYTE_BUFFER_LENGTH];
#endif

// ------------------------------------
// Function prototypes
// ------------------------------------
static unsigned char do_transaction(unsigned char trans_type, unsigned int datasize,  unsigned int address, unsigned char* data);
static unsigned char xor_20(unsigned char val);

unsigned char transaction_channel_write (unsigned int address,
                  unsigned int burst_length,
                  unsigned char* data_buffer,
                  unsigned char sequential)
{
    return sequential?do_transaction(SEQUENTIAL_WRITE, burst_length, address, data_buffer):do_transaction(NON_SEQUENTIAL_WRITE, burst_length, address, data_buffer);
}

unsigned char transaction_channel_read  (unsigned int address,
                  unsigned int burst_length,
                  unsigned char* data_buffer,
                  unsigned char sequential)
{
    return sequential?do_transaction(SEQUENTIAL_READ, burst_length, address, data_buffer):do_transaction(NON_SEQUENTIAL_READ, burst_length, address, data_buffer);
}

static unsigned char do_transaction(unsigned char trans_type,
                unsigned int datasize,
                unsigned int address,
                unsigned char* data)
{
    unsigned int i;
    unsigned char result = 0;
    unsigned char header[8];
    unsigned char* transaction;
    unsigned char* response;
    unsigned char* p;


    //-------------------------
    // Make transaction header
    //-------------------------
    header[0] = trans_type;
    header[1] = 0;
    header[2] = (datasize >> 8) & 0xff;
    header[3] = (datasize & 0xff);
    header[4] = (address >> 24) & 0xff;
    header[5] = (address >> 16) & 0xff;
    header[6] = (address >> 8)  & 0xff;
    header[7] = (address & 0xff);

    switch(trans_type)
    {
        case NON_SEQUENTIAL_WRITE:
        case SEQUENTIAL_WRITE:
      //--------------------------------
      // Build up the write transaction
      //--------------------------------
#ifdef DYNAMIC_MEMORY_ALLOC
      transaction = (unsigned char *) malloc ((datasize + HEADER_LEN) * sizeof(unsigned char));
      if(transaction == NULL)   Serial.print("Allocating heap memory failed\n");

      response = (unsigned char *) malloc (RESPONSE_LEN * sizeof(unsigned char));
      if(response == NULL)  Serial.print("Allocating heap memory failed\n");
#endif
#ifdef STATIC_MEMORY_ALLOC
      transaction = &transaction_buffer[0];

      response = &response_buffer[0];
#endif
      p = transaction;

      for (i = 0; i < HEADER_LEN; i++)
        *p++ = header[i];

      for (i = 0; i < datasize; i++)
        *p++ = *data++;

      //-----------------------------------------------
      // Send the header and data, get 4 byte response
      //-----------------------------------------------
      packet_to_byte_convert (datasize + HEADER_LEN, transaction, RESPONSE_LEN, response);

      //------------------------------------------------------------------
      // Check return number of bytes in the 3rd and 4th byte of response
      //------------------------------------------------------------------
      if (datasize == (((unsigned int)(response[2]& 0xff)<<8)|((unsigned int)(response[3]&0xff))))
        result = 1;
#ifdef DYNAMIC_MEMORY_ALLOC
      free(transaction);
      free(response);
#endif
      break;

        case NON_SEQUENTIAL_READ:
        case SEQUENTIAL_READ:
#ifdef DYNAMIC_MEMORY_ALLOC
          response = (unsigned char *) malloc (datasize * sizeof(unsigned char));
          if(response == NULL)
            Serial.print("Allocating heap memory failed\n");
#endif
#ifdef STATIC_MEMORY_ALLOC
      response = &response_buffer[0];
#endif
          //--------------------------------------------
          // Send the header, get n datasize byte response
          //--------------------------------------------
          packet_to_byte_convert (HEADER_LEN, header, datasize, response);

      for (i = 0; i < datasize; i++)
        *data++ = *response++;

      //-------------------------------------------------------------------
      // Read do not return number of bytes , assume result always set to 1
      //-------------------------------------------------------------------
      result = 1;
#ifdef DYNAMIC_MEMORY_ALLOC
      free(response);
#endif
      break;

        default:
      break;
    }

    if(result)return 1;
    else return 0;
}

unsigned char packet_to_byte_convert (unsigned int send_length, unsigned char* send_data,
                unsigned int response_length, unsigned char* response_data)
{
    unsigned int i;
    unsigned int packet_length = 0;
    unsigned char *send_packet;
    unsigned char *response_packet;
    unsigned char *p;
    unsigned char current_byte;

    //--------------------------------------------------------------
    //To figure out what the maximum length of the packet is going
    // to be so we can allocate a chunk of memory for it.
    //
    // All packets start with an SOP byte, followed by a channel
    // id (2 bytes) and end with an EOP. That's 4 bytes.
    //
    // However, we have to escape characters that are the same
    // as any of the SOP/EOP/channel bytes. Worst case scenario
    // is that each data byte is escaped, which leads us to the
    // algorithm below.
    //---------------------------------------------------------------
    
#ifdef DYNAMIC_MEMORY_ALLOC
    unsigned int send_max_len = 2 * send_length + 4;
    unsigned int response_max_len = 2 * response_length + 4;
  
  send_packet = (unsigned char *) malloc (send_max_len * sizeof(unsigned char));
    if(send_packet == NULL) Serial.print("Allocating heap memory failed\n");

    response_packet = (unsigned char *) malloc (response_max_len * sizeof(unsigned char));
    if(response_packet == NULL) Serial.print("Allocating heap memory failed\n");
#endif
#ifdef STATIC_MEMORY_ALLOC
  unsigned int response_max_len = 2 * response_length + 4;
  
  send_packet = &send_packet_buffer[0];

  response_packet = &response_packet_buffer[0];
#endif
    p = send_packet;

    //------------------------------------
    // SOP
    //------------------------------------
    *p++ = SOP;

    //------------------------------------
    // Channel information. Only channel 0 is defined.
     //------------------------------------
    *p++ = CHANNEL;
    *p++ = 0x0;

    //------------------------------------
    // Append the data to the packet
    //------------------------------------
    for (i = 0; i < send_length; i++)
    {
        current_byte = send_data[i];
        //------------------------------------
        // EOP must be added before the last byte
        //------------------------------------
        if (i == send_length-1)
        {
            *p++ = EOP;
        }

        //------------------------------------
        // Escape data bytes which collide with our
        // special characters.
        //------------------------------------
        switch(current_byte)
        {
            case SOP:
                        *p++ = ESC;
                        *p++ = xor_20(current_byte);
                        break;
            case EOP:
                        *p++ = ESC;
                        *p++ = xor_20(current_byte);
                        break;
            case CHANNEL:
                        *p++ = ESC;
                        *p++ = xor_20(current_byte);
                        break;
            case ESC:
                        *p++ = ESC;
                        *p++ = xor_20(current_byte);
                        break;

            default:
                        *p++ = current_byte;
                        break;
        }

    }
    packet_length=p-send_packet;

  byte_to_core_convert(packet_length,send_packet,response_max_len,response_packet);
  //-----------------------------------------------------------------
  //Analyze response packet , reset pointer to start of response data
  //-----------------------------------------------------------------
  p = response_data;
  //-------------
  //Look for SOP
  //-------------
  for(i=0;i<response_max_len;i++){
    if(response_packet[i] == SOP) {
      i++;
      break;
    }
  }

  //-------------------------------
  //Continue parsing data after SOP
  //-------------------------------
  while(i < response_max_len)
  {
    current_byte = response_packet[i];

    switch(current_byte)
    {
      case ESC:
      case CHANNEL:
      case SOP:
        i++;
        current_byte = response_packet[i];
        *p++ = xor_20(current_byte);
        i++;
        break;

      //------------------------------------
      // Get a EOP, get the next last byte
      // and exit while loop
      //------------------------------------
      case EOP:
        i++;
        current_byte = response_packet[i];

        if((current_byte == ESC)||(current_byte == CHANNEL)||(current_byte == SOP)){
          i++;
          current_byte = response_packet[i];
          *p++ = xor_20(current_byte);
        }

        else *p++ = current_byte;

        i = response_max_len;
        break;

      default:
        *p++ = current_byte;
        i++;
        break;
    }
  }
#ifdef DYNAMIC_MEMORY_ALLOC
  free(send_packet);
    free(response_packet);
#endif
    return 0;
}

unsigned char byte_to_core_convert (unsigned int send_length, unsigned char* send_data,
                unsigned int response_length, unsigned char* response_data)
{
    unsigned int i;
    unsigned int packet_length = 0;
    unsigned char *send_packet;
    unsigned char *response_packet;
    unsigned char *p;
    unsigned char current_byte;

    //---------------------------------------------------------------------
    // The maximum length of the packet is going to be so we can allocate
    // a chunk of memory for it. Assuming worst case scenario is that each
    // data byte is escaped, so we double the memory allocation.
    //---------------------------------------------------------------------
    
#ifdef DYNAMIC_MEMORY_ALLOC
    unsigned int send_max_len = 2 * send_length;
    unsigned int response_max_len = 2 * response_length;
  
  send_packet = (unsigned char*) malloc (send_max_len * sizeof(unsigned char));
    if(send_packet == NULL) Serial.print("Allocating heap memory failed\n");

    response_packet = (unsigned char*) malloc (response_max_len * sizeof(unsigned char));
    if(response_packet == NULL) Serial.print("Allocating heap memory failed\n");
#endif
#ifdef STATIC_MEMORY_ALLOC
  unsigned int response_max_len = 2 * response_length;
  
  send_packet = &send_byte_buffer[0];

  response_packet = &response_byte_buffer[0];
#endif
    p = send_packet;

    for (i = 0; i < send_length; i++)
    {
        current_byte = send_data[i];
        //-----------------------------------------------
        // Check for Escape and Idle special characters.
        // If exists, insert Escape and XOR the next byte
        //-----------------------------------------------
        switch(current_byte)
        {
            case BYTESIDLECHAR:
                        *p++ = BYTESESCCHAR;
                        *p++ = xor_20(current_byte);
                        break;
            case BYTESESCCHAR:
                        *p++ = BYTESESCCHAR;
                        *p++ = xor_20(current_byte);
                        break;
            default:
                        *p++ = current_byte;
                        break;
        }

    }
    packet_length=p-send_packet;

    //---------------------------------------------------------
    // Use the SPI core access routine to transmit and receive
    //---------------------------------------------------------
    //spi_command(SPI_BASE,0,packet_length,send_packet,response_max_len,response_packet,0);
    spi_command(packet_length,send_packet,response_max_len, response_packet);

    //-----------------------------------------------------------------
    //Analyze response packet , reset pointer to start of response data
    //-----------------------------------------------------------------
  i=0;
  p = response_data;
  while(i < response_max_len)
  {
    current_byte = response_packet[i];
    //-----------------------------------------------
    // Check for Escape and Idle special characters.
    // If exists, ignore and XOR the next byte
    //-----------------------------------------------
    switch(current_byte)
    {
      case BYTESIDLECHAR:
        i++;
        break;

      case BYTESESCCHAR:
        i++;
        current_byte = response_packet[i];
        *p++ = xor_20(current_byte);
        i++;
        break;

      default:
        *p++ = current_byte;
        i++;
        break;
    }
  }
#ifdef DYNAMIC_MEMORY_ALLOC
  free(send_packet);
    free(response_packet);
#endif
    return 0;
}

static unsigned char xor_20(unsigned char val)
{
    return val^0x20;
}

void spi_command(unsigned int packet_length, unsigned char* send_packet, unsigned int response_max_len, unsigned char* response_packet){
//Serial.print("Sending: ");
	int i = 0;
	for(i=0;i<packet_length;i++){
		//Serial.print(*(send_packet+i),HEX);
		//Serial.print("|");   
	}
	//Serial.println("");
	SPI_2.write(send_packet,packet_length);   //<---- Using SPI2, may need to be instantiated!
	//Serial.println("Packet Send, Trying to read Response...");
	SPI_2.read(response_packet, response_max_len);      
	//Serial.print("Response Received: ");
	for(i=0;i<response_max_len;i++){ 
		//Serial.print(*(response_packet+i),HEX);
		//Serial.print("|");   
	}
	//Serial.println("-");  
}




