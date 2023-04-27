//------------Copyright (C) 2008 Maxim Integrated Products --------------
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY,  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL MAXIM INTEGRATED PRODCUTS BE LIABLE FOR ANY CLAIM, DAMAGES
// OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//
// Except as contained in this notice, the name of Maxim Integrated Products
// shall not be used except as stated in the Maxim Integrated Products
// Branding Policy.
// ---------------------------------------------------------------------------
//
// ds28ec20.c - 1-Wire functions to read and write to the 
//              DS28EC20.
//

#include "sha.h"
#include "1wire.h"

#define DS28EC20
#include "ds28ec20.h"

#include <memory.h>

// DS28EC20 function declarations
int ReadPage(int pg, uchar *page_data, int cont);
int WritePage(int pg, uchar *page_data);
static int DeviceSelect();
static unsigned short docrc16(unsigned short data);
static int WriteScratchpad(int address, uchar *data);
static int CopyScratchpad(int address);

// DS28EC20 state
unsigned short CRC16;

//-----------------------------------------------------------------------------
// ------ DS28EC20 Functions
//-----------------------------------------------------------------------------

//----------------------------------------------------------------------
// Read Page for DS28EC20.
//
// 'pagenum'     - page number to do a read authenticate
// 'data'        - buffer to read into from page
//
// Return: TRUE if data read with CRC16 verification
//         FALSE if failed to read device or CRC16 incorrect
//
int ReadPage(int pagenum, uchar* data, int cont)
{
   short send_cnt=0;
   uchar send_block[55];
   int i;
   int address = pagenum << 5;

   //seed the crc
   CRC16 = 0;

   // Select the device 
   if (!cont)
   {
      if (!DeviceSelect())
         return FALSE;
   
      // create the send block
      send_block[send_cnt] = CMD_EX_READ;
   
      docrc16(send_block[send_cnt++]);

      // TA1
      send_block[send_cnt] = (uchar)(address & 0xFF);
      docrc16(send_block[send_cnt++]);

      // TA2
      send_block[send_cnt] = (uchar)((address >> 8) & 0xFF);
      docrc16(send_block[send_cnt++]);
   }

   // now add the read bytes for data bytes, and crc16
   memset(&send_block[send_cnt], 0x0FF, 34);
   send_cnt += 34;

   // now send the block (except for last byte)
   OWBlock(send_block,send_cnt);

   // check the CRC
   for (i = send_cnt - 34; i < send_cnt; i++)
      docrc16(send_block[i]);

   // verify CRC16 is correct
   if (CRC16!=0xB001)
      return FALSE;
   
   // transfer results
   memcpy(data, &send_block[send_cnt-34], 32);

   return TRUE;
}


//--------------------------------------------------------------------------
//  Write an 8-byte block of data using MAC generated from secret
//
//  Parameters
//     address - memory address to write 8 byte block (must be on 8 byte boundary)
//     block - 8 byte block of data to write
//     page_data - 32 byte containing the current page value (from previous read)
// 
//  Globals used : ROM_NO (used to select the device)
//                 SECRET (used to calculate MAC)
//
//
int WritePage(int pg, uchar *page_data)
{
   if (!WriteScratchpad(pg << 5, page_data))
      return FALSE;

   if (!CopyScratchpad(pg << 5))
      return FALSE;

   return TRUE;
}

//----------------------------------------------------------------------
// Write the scratchpad with CRC16 verification for DS28E01-100.  
// There must be eight bytes worth of data in the buffer.
//
// 'address'   - address to write data to
// 'data'      - data to write
//
// Return: TRUE - write to scratch verified
//         FALSE - error writing scratch
//
static int WriteScratchpad(int address, uchar *data)
{
   uchar send_block[50];
   short send_cnt=0,i;

   // Select the device 
   if (!DeviceSelect())
      return FALSE;

   CRC16 = 0;
   // write scratchpad command
   send_block[send_cnt] = CMD_WRITE_SCRATCHPAD;
   docrc16(send_block[send_cnt++]);
   // address 1
   send_block[send_cnt] = (uchar)(address & 0xFF);
   docrc16(send_block[send_cnt++]);
   // address 2
   send_block[send_cnt] = (uchar)((address >> 8) & 0xFF);
   docrc16(send_block[send_cnt++]);
   // data
   for (i = 0; i < 32; i++)
   {
      send_block[send_cnt] = data[i];
      docrc16(send_block[send_cnt++]);
   }
   // CRC16
   send_block[send_cnt++] = 0xFF;
   send_block[send_cnt++] = 0xFF;

   // now send the block
   OWBlock(send_block,send_cnt);

   // perform CRC16 of last 2 byte in packet
   for (i = send_cnt - 2; i < send_cnt; i++)
      docrc16(send_block[i]);

   // verify CRC16 is correct
   return (CRC16==0xB001);   
}

//----------------------------------------------------------------------
// Copy the scratchpad with verification for DS28E01
//
// 'address'     - address of destination
// 'MAC'         - authentication MAC, required to execute copy scratchpad.
//
// Return: TRUE - copy scratch verified
//         FALSE - error during copy scratch, device not present, or HIDE
//                 flag is in incorrect state for address being written.
//
static int CopyScratchpad(int address)
{
   short send_cnt=0;
   uchar send_block[10],confirm;

   // Select the device 
   if (!DeviceSelect())
      return FALSE;

   // copy scratchpad command
   send_block[send_cnt++] = CMD_COPY_SCRATCHPAD;
   // address 1
   send_block[send_cnt++] = (uchar)(address & 0xFF);
   // address 2
   send_block[send_cnt++] = (uchar)((address >> 8) & 0xFF);

   // now send the block (without last byte)
   OWBlock(send_block,send_cnt);

   // Send ES and enable strong pull-up
   OWWriteBytePower(0x1F);

   // now wait for EEPROM write.
   // should do strong pullup here.
   msDelay(EEPROM_WRITE_DELAY_DS28EC20);

   // disable strong pullup
   OWLevel(MODE_STANDARD);

   // read the confirmaiton byte
   confirm = OWReadByte();

   // return TRUE if confirmation byte shows a toggle
   return ((confirm == 0x55) || (confirm == 0xAA));
}


//--------------------------------------------------------------------------
//  Select the DS28E01 in the global ROM_NO
//
static int DeviceSelect()
{
   uchar buf[9];

   // NOTE: Could use Skip ROM for single drop environment

   // use MatchROM 
   if (OWReset())
   {
      buf[0] = 0x55;
      memcpy(&buf[1], &ROM_NO[0], 8);
      OWBlock(buf,9);
      return TRUE;
   }
   else
      return FALSE;
}

//--------------------------------------------------------------------------
// Calculate a new CRC16 from the input data shorteger.  Return the current
// CRC16 and also update the global variable CRC16.
//
static short oddparity[16] = { 0, 1, 1, 0, 1, 0, 0, 1, 1, 0, 0, 1, 0, 1, 1, 0 };

static unsigned short docrc16(unsigned short data)
{
   data = (data ^ (CRC16 & 0xff)) & 0xff;
   CRC16 >>= 8;

   if (oddparity[data & 0xf] ^ oddparity[data >> 4])
     CRC16 ^= 0xc001;

   data <<= 6;
   CRC16  ^= data;
   data <<= 1;
   CRC16   ^= data;

   return CRC16;
}
