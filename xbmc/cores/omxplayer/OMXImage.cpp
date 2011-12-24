/*
 *      Copyright (C) 2010 Team XBMC
 *      http://www.xbmc.org
 *
 *  This Program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This Program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with XBMC; see the file COPYING.  If not, write to
 *  the Free Software Foundation, 675 Mass Ave, Cambridge, MA 02139, USA.
 *  http://www.gnu.org/copyleft/gpl.html
 *
 */

#if (defined HAVE_CONFIG_H) && (!defined WIN32)
  #include "config.h"
#elif defined(_WIN32)
#include "system.h"
#endif

#include "OMXImage.h"

#include "OMXStreamInfo.h"
#include "utils/log.h"
#include "linux/XMemUtils.h"

#include "BitstreamConverter.h"

#include <sys/time.h>
#include <inttypes.h>

#ifdef CLASSNAME
#undef CLASSNAME
#endif
#define CLASSNAME "COMXImage"

#define CONTENTURI_MAXLEN 256

#define EXIF_TAG_ORIENTATION    0x0112

COMXImage::COMXImage()
{
  m_is_open       = false;
  m_image_size    = 0;
  m_image_buffer  = NULL;
  m_progressive   = false;
  m_orientation   = 0;
  m_width         = 0;
  m_height        = 0;

  memset(&m_omx_image, 0x0, sizeof(OMX_IMAGE_PORTDEFINITIONTYPE));
}

COMXImage::~COMXImage()
{
  if(m_image_buffer)
    free(m_image_buffer);
  m_image_buffer  = NULL;
  m_image_size    = 0;

  if (m_is_open)
    Close();
}

void COMXImage::Close()
{
  memset(&m_omx_image, 0x0, sizeof(OMX_IMAGE_PORTDEFINITIONTYPE));

  if(m_image_buffer)
    free(m_image_buffer);
  m_image_buffer  = NULL;
  m_image_size    = 0;
  m_width         = 0;
  m_height        = 0;
  m_is_open       = false;
  m_progressive   = false;
  m_orientation   = 0;

  m_pFile.Close();
}

typedef enum {      /* JPEG marker codes */
  M_SOF0  = 0xc0,
  M_SOF1  = 0xc1,
  M_SOF2  = 0xc2,
  M_SOF3  = 0xc3,
  M_SOF5  = 0xc5,
  M_SOF6  = 0xc6,
  M_SOF7  = 0xc7,
  M_JPG   = 0xc8,
  M_SOF9  = 0xc9,
  M_SOF10 = 0xca,
  M_SOF11 = 0xcb,
  M_SOF13 = 0xcd,
  M_SOF14 = 0xce,
  M_SOF15 = 0xcf,

  M_DHT   = 0xc4,

  M_RST0  = 0xd0,
  M_RST1  = 0xd1,
  M_RST2  = 0xd2,
  M_RST3  = 0xd3,
  M_RST4  = 0xd4,
  M_RST5  = 0xd5,
  M_RST6  = 0xd6,
  M_RST7  = 0xd7,

  M_SOI   = 0xd8,
  M_EOI   = 0xd9,
  M_SOS   = 0xda,
  M_DQT   = 0xdb,
  M_DNL   = 0xdc,
  M_DRI   = 0xdd,
  M_DHP   = 0xde,
  M_EXP   = 0xdf,

  M_APP0  = 0xe0,
  M_APP1  = 0xe1,
  M_APP2  = 0xe2,
  M_APP3  = 0xe3,
  M_APP4  = 0xe4,
  M_APP5  = 0xe5,
  M_APP6  = 0xe6,
  M_APP7  = 0xe7,
  M_APP8  = 0xe8,
  M_APP9  = 0xe9,
  M_APP10 = 0xea,
  M_APP11 = 0xeb,
  M_APP12 = 0xec,
  M_APP13 = 0xed,
  M_APP14 = 0xee,
  M_APP15 = 0xef,

  M_TEM   = 0x01,
} JPEG_MARKER;

OMX_IMAGE_CODINGTYPE COMXImage::GetCodingType()
{
  memset(&m_omx_image, 0x0, sizeof(OMX_IMAGE_PORTDEFINITIONTYPE));
  m_width         = 0;
  m_height        = 0;
  m_progressive   = false;
  m_orientation   = 0;

  m_omx_image.eCompressionFormat = OMX_IMAGE_CodingMax;

  if(!m_image_size)
    return OMX_IMAGE_CodingMax;

  bits_reader_t br;
  CBitstreamConverter::bits_reader_set( &br, m_image_buffer, m_image_size );

  /* JPEG Header */
  if(CBitstreamConverter::read_bits(&br, 16) == 0xFFD8)
  {
    m_omx_image.eCompressionFormat = OMX_IMAGE_CodingJPEG;

    unsigned char ff = CBitstreamConverter::read_bits(&br, 8);
    unsigned char marker = CBitstreamConverter::read_bits(&br, 8);
    unsigned short block_size = 0;
    bool nMarker = false;

    while(!br.oflow) {

      switch(marker)
      {
        case M_TEM:
        case M_DRI:
          CBitstreamConverter::skip_bits(&br, 16);
          continue;
        case M_SOI:
        case M_EOI:
          continue;
        
        case M_SOS:
        case M_DQT:
        case M_DNL:
        case M_DHP:
        case M_EXP:

        case M_DHT:

        case M_SOF0:
        case M_SOF1:
        case M_SOF2:
        case M_SOF3:

        case M_SOF5:
        case M_SOF6:
        case M_SOF7:

        case M_JPG:
        case M_SOF9:
        case M_SOF10:
        case M_SOF11:

        case M_SOF13:
        case M_SOF14:
        case M_SOF15:

        case M_APP0:
        case M_APP1:
        case M_APP2:
        case M_APP3:
        case M_APP4:
        case M_APP5:
        case M_APP6:
        case M_APP7:
        case M_APP8:
        case M_APP9:
        case M_APP10:
        case M_APP11:
        case M_APP12:
        case M_APP13:
        case M_APP14:
        case M_APP15:
          block_size = CBitstreamConverter::read_bits(&br, 16);
          nMarker = true;
          break;

        default:
          nMarker = false;
          break;
      }

      if(!nMarker)
      {
        break;
      }

      if(marker >= M_SOF0 && marker <= M_SOF15)
      {
        if(marker == M_SOF2 || marker == M_SOF6 || marker == M_SOF10 || marker == M_SOF14)
        {
          m_progressive = true;
        }
        CBitstreamConverter::skip_bits(&br, 8);
        m_omx_image.nFrameHeight = CBitstreamConverter::read_bits(&br, 16);
        m_omx_image.nFrameWidth = CBitstreamConverter::read_bits(&br, 16);

        CBitstreamConverter::skip_bits(&br, 8 * (block_size - 9));
      }
      else if(marker == M_APP1)
      {
        int readBits = 2;
        bool bMotorolla = false;
        bool bError = false;
        bool bOrientation = false;

        // Exif header
        if(CBitstreamConverter::read_bits(&br, 32) == 0x45786966)
        {
          CBitstreamConverter::skip_bits(&br, 8 * 2);
          readBits += 2;
        
          char o1 = CBitstreamConverter::read_bits(&br, 8);
          char o2 = CBitstreamConverter::read_bits(&br, 8);
          readBits += 2;

          /* Discover byte order */
          if(o1 == 'M' && o2 == 'M')
            bMotorolla = true;
          else if(o1 == 'I' && o2 == 'I')
            bMotorolla = false;
          else
            bError = true;
        
          CBitstreamConverter::skip_bits(&br, 8 * 2);
          readBits += 2;

          if(!bError)
          {
            unsigned int offset, a, b, numberOfTags, tagNumber;
  
            // Get first IFD offset (offset to IFD0)
            if(bMotorolla)
            {
              CBitstreamConverter::skip_bits(&br, 8 * 2);
              readBits += 2;

              a = CBitstreamConverter::read_bits(&br, 8);
              b = CBitstreamConverter::read_bits(&br, 8);
              readBits += 2;
              offset = (a << 8) + b;
            }
            else
            {
              a = CBitstreamConverter::read_bits(&br, 8);
              b = CBitstreamConverter::read_bits(&br, 8);
              readBits += 2;
              offset = (b << 8) + a;

              CBitstreamConverter::skip_bits(&br, 8 * 2);
              readBits += 2;
            }

            offset -= 8;
            if(offset > 0)
            {
              CBitstreamConverter::skip_bits(&br, 8 * offset);
              readBits += offset;
            } 

            // Get the number of directory entries contained in this IFD
            if(bMotorolla)
            {
              a = CBitstreamConverter::read_bits(&br, 8);
              b = CBitstreamConverter::read_bits(&br, 8);
              numberOfTags = (a << 8) + b;
            }
            else
            {
              a = CBitstreamConverter::read_bits(&br, 8);
              b = CBitstreamConverter::read_bits(&br, 8);
              numberOfTags = (b << 8) + a;
            }
            readBits += 2;

            while(numberOfTags && !br.oflow)
            {
              // Get Tag number
              if(bMotorolla)
              {
                a = CBitstreamConverter::read_bits(&br, 8);
                b = CBitstreamConverter::read_bits(&br, 8);
                tagNumber = (a << 8) + b;
                readBits += 2;
              }
              else
              {
                a = CBitstreamConverter::read_bits(&br, 8);
                b = CBitstreamConverter::read_bits(&br, 8);
                tagNumber = (b << 8) + a;
                readBits += 2;
              }

              //found orientation tag
              if(tagNumber == EXIF_TAG_ORIENTATION)
              {
                bOrientation = true;
                if(bMotorolla)
                {
                  CBitstreamConverter::skip_bits(&br, 8 * 7);
                  readBits += 7;
                  m_orientation = CBitstreamConverter::read_bits(&br, 8);
                  readBits += 1;
                  CBitstreamConverter::skip_bits(&br, 8 * 2);
                  readBits += 2;
                }
                else
                {
                  CBitstreamConverter::skip_bits(&br, 8 * 6);
                  readBits += 6;
                  m_orientation = CBitstreamConverter::read_bits(&br, 8);
                  readBits += 1;
                  CBitstreamConverter::skip_bits(&br, 8 * 3);
                  readBits += 3;
                }
                break;
              }
              else
              {
                CBitstreamConverter::skip_bits(&br, 8 * 10);
                readBits += 10;
              }
              numberOfTags--;
            }
          }
        }
        readBits += 4;
        CBitstreamConverter::skip_bits(&br, 8 * (block_size - readBits));
      }
      else
      {
        CBitstreamConverter::skip_bits(&br, 8 * (block_size - 2));
      }

      ff = CBitstreamConverter::read_bits(&br, 8);
      marker = CBitstreamConverter::read_bits(&br, 8);

    }

  }

  CBitstreamConverter::bits_reader_set( &br, m_image_buffer, m_image_size );

  /* PNG Header */
  if(CBitstreamConverter::read_bits(&br, 32) == 0x89504E47)
  {
    m_omx_image.eCompressionFormat = OMX_IMAGE_CodingPNG;
    CBitstreamConverter::skip_bits(&br, 32 * 2);
    if(CBitstreamConverter::read_bits(&br, 32) == 0x49484452)
    {
      m_omx_image.nFrameWidth = CBitstreamConverter::read_bits(&br, 32);
      m_omx_image.nFrameHeight = CBitstreamConverter::read_bits(&br, 32);
    }
  }

  m_width  = m_omx_image.nFrameWidth;
  m_height = m_omx_image.nFrameHeight;

  return m_omx_image.eCompressionFormat;
}

bool COMXImage::ReadFile(const std::string &inputFile)
{
  if(!m_pFile.Open(inputFile, 0))
    return false;

  if(m_image_buffer)
    free(m_image_buffer);
  m_image_buffer = NULL;

  m_image_size = m_pFile.GetLength();

  if(!m_image_size)
    return false;

  m_image_buffer = (uint8_t *)malloc(m_image_size);
  if(!m_image_buffer)
    return false;
  
  memset(m_image_buffer, 0x0, m_image_size);
  m_pFile.Read(m_image_buffer, m_image_size);

  GetCodingType();

  // ensure not too big for hardware
  while (m_width > 2048 || m_height > 2048)
    m_width >>= 1, m_height >>= 1;
  // ensure not too small
  while (m_width <= 32 || m_height <= 32)
    m_width <<= 1, m_height <<= 1;
  // surely not going to happen?
  if (m_width > 2048 || m_height > 2048)
    m_width = 256, m_height = 256;
  
  m_width  = (m_width + 15)  & ~15;
  m_height = (m_height + 15) & ~15;

  return true;
}
