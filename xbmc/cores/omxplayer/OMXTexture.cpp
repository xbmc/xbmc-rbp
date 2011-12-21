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

#include "OMXTexture.h"

#include "OMXStreamInfo.h"
#include "utils/log.h"
#include "linux/XMemUtils.h"

#include "BitstreamConverter.h"

#include <sys/time.h>
#include <inttypes.h>

#ifdef CLASSNAME
#undef CLASSNAME
#endif
#define CLASSNAME "COMXTexture"

#define CONTENTURI_MAXLEN 256

typedef struct {
   OMX_PARAM_CONTENTURITYPE uri;
   OMX_U8 uri_data[CONTENTURI_MAXLEN];
} OMX_CONTENTURI_TYPE_T;

COMXTexture::COMXTexture()
{
  m_OMX.Initialize();
  m_is_open       = false;
  m_image_size    = 0;
  m_image_buffer  = NULL;
  memset(&m_omx_image, 0x0, sizeof(OMX_IMAGE_PORTDEFINITIONTYPE));
  m_progressive   = false;
  m_width         = 0;
  m_height        = 0;
}

COMXTexture::~COMXTexture()
{
  if(m_image_buffer)
    free(m_image_buffer);
  m_image_buffer  = NULL;
  m_image_size    = 0;

  if (m_is_open)
    Close();

  m_OMX.Deinitialize();
}

bool COMXTexture::SetImageAutodetect(void)
{
  return true;
}

bool COMXTexture::Open(void)
{
  CStdString componentName = "";

  componentName = "OMX.broadcom.image_decode";
  if(!m_omx_image_decode.Initialize((const CStdString)componentName, OMX_IndexParamImageInit)) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_render.SetStateForComponent\n", CLASSNAME, __func__);
    return false;
  }
  componentName = "OMX.broadcom.resize";
  if(!m_omx_resize.Initialize((const CStdString)componentName, OMX_IndexParamImageInit)) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_render.SetStateForComponent\n", CLASSNAME, __func__);
    return false;
  }
  componentName = "OMX.broadcom.egl_render";
  if(!m_omx_egl_render.Initialize((const CStdString)componentName, OMX_IndexParamVideoInit)) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_render.SetStateForComponent\n", CLASSNAME, __func__);
    return false;
  }

  m_omx_tunnel_decode.Initialize(&m_omx_image_decode, m_omx_image_decode.GetOutputPort(), &m_omx_resize,       m_omx_resize.GetInputPort());
  m_omx_tunnel_egl.Initialize(&m_omx_resize,       m_omx_resize.GetOutputPort(),       &m_omx_egl_render,   m_omx_egl_render.GetInputPort());

  m_is_open       = true;

  return true;
}

void COMXTexture::Close()
{
  OMX_ERRORTYPE omx_err = OMX_ErrorNone;

  memset(&m_omx_image, 0x0, sizeof(OMX_IMAGE_PORTDEFINITIONTYPE));

  if(m_image_buffer)
    free(m_image_buffer);
  m_image_buffer  = NULL;
  m_image_size    = 0;
  m_width         = 0;
  m_height        = 0;

  m_omx_tunnel_decode.Flush();
  m_omx_tunnel_decode.Deestablish(false);
  m_omx_tunnel_egl.Deestablish(false);

  omx_err = m_omx_egl_render.FreeOutputBuffer(m_egl_buffer);
  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_egl_render.FreeBuffer\n", CLASSNAME, __func__);
  }

  // delete components
  m_omx_image_decode.Deinitialize();
  m_omx_resize.Deinitialize();
  m_omx_egl_render.Deinitialize();

  m_is_open       = false;
  m_progressive   = false;

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

OMX_IMAGE_CODINGTYPE COMXTexture::GetCodingType()
{
  memset(&m_omx_image, 0x0, sizeof(OMX_IMAGE_PORTDEFINITIONTYPE));
  m_width         = 0;
  m_height        = 0;

  m_progressive = false;

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

      if(nMarker)
      {
        //printf("0x%02X%02X %d\n", ff, marker, block_size);
      }
      else
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
      printf("jpeg %ld %ld\n", m_omx_image.nFrameWidth, m_omx_image.nFrameHeight);
      break;
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
      printf("png %ld %ld\n", m_omx_image.nFrameWidth, m_omx_image.nFrameHeight);
    }
  }

  m_width  = m_omx_image.nFrameWidth;
  m_height = m_omx_image.nFrameHeight;

  /*
  printf("0x%04X 0x%04X 0x%04X\n", CBitstreamConverter::read_bits(&br, 16),
      CBitstreamConverter::read_bits(&br, 16), CBitstreamConverter::read_bits(&br, 16));
  */

  return m_omx_image.eCompressionFormat;
}

bool COMXTexture::ReadFile(const std::string &inputFile)
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

int COMXTexture::Decode(void *egl_image, void *egl_display, unsigned width, unsigned height)
{
  bool m_firstFrame = true;
  OMX_BUFFERHEADERTYPE *omx_buffer = NULL;
  unsigned int demuxer_bytes = 0;
  uint8_t *demuxer_content = NULL;
  OMX_ERRORTYPE omx_err = OMX_ErrorNone;

  if(!m_is_open)
    return false;

  if(m_omx_image.eCompressionFormat == OMX_IMAGE_CodingMax)
  {
    CLog::Log(LOGERROR, "%s::%s error unsupported image format\n", CLASSNAME, __func__);
    goto do_exit;
  }

  omx_err = m_omx_tunnel_decode.Establish(false);
  if (!m_is_open) {
    CLog::Log(LOGERROR, "%s::%s error m_is_open\n", CLASSNAME, __func__);
    goto do_exit;
  }

  OMX_PARAM_RESIZETYPE resize_conf;
  OMX_INIT_STRUCTURE(resize_conf);
  resize_conf.nPortIndex = m_omx_resize.GetOutputPort();
  resize_conf.eMode = OMX_RESIZE_BOX;
  resize_conf.nMaxWidth = width;
  resize_conf.nMaxHeight = height;
  resize_conf.bPreserveAspectRatio = OMX_FALSE;
  resize_conf.bAllowUpscaling = OMX_TRUE;
      
  omx_err = m_omx_resize.SetParameter(OMX_IndexParamResize, &resize_conf);
  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_render.SetStateForComponent\n", CLASSNAME, __func__);
    goto do_exit;
  }

  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_INIT_STRUCTURE(port_def);
  port_def.nPortIndex = m_omx_image_decode.GetInputPort();

  omx_err = m_omx_image_decode.GetParameter(OMX_IndexParamPortDefinition, &port_def);
  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_image_decode.GetParameter\n", CLASSNAME, __func__);
    goto do_exit;
  }

  port_def.format.image.eCompressionFormat = m_omx_image.eCompressionFormat;
  port_def.format.image.eColorFormat = OMX_COLOR_FormatUnused;
  port_def.format.image.nFrameWidth = 0;
  port_def.format.image.nFrameHeight = 0;
  port_def.format.image.nStride = 0;
  port_def.format.image.nSliceHeight = 0;
  port_def.format.image.bFlagErrorConcealment = OMX_FALSE;

  omx_err = m_omx_image_decode.SetParameter(OMX_IndexParamPortDefinition, &port_def);
  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_image_decode.SetParameter\n", CLASSNAME, __func__);
    goto do_exit;
  }

  OMX_IMAGE_PARAM_PORTFORMATTYPE port_image;
  OMX_INIT_STRUCTURE(port_image);
  port_image.nPortIndex = m_omx_image_decode.GetInputPort();
  port_image.eCompressionFormat = m_omx_image.eCompressionFormat;
  port_image.eColorFormat = port_def.format.image.eColorFormat;

  omx_err = m_omx_image_decode.SetParameter(OMX_IndexParamImagePortFormat, &port_image);
  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_image_decode.SetParameter\n", CLASSNAME, __func__);
    goto do_exit;
  }

  omx_err = m_omx_image_decode.AllocInputBuffers();
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "%s::%s - Error alloc buffers 0x%08x", CLASSNAME, __func__, omx_err);
    goto do_exit;
  }

  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s m_omx_tunnel_decode.Establish\n", CLASSNAME, __func__);
    goto do_exit;
  }

  omx_err = m_omx_image_decode.SetStateForComponent(OMX_StateExecuting);
  if (omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_sched.SetStateForComponent\n", CLASSNAME, __func__);
    goto do_exit;
  }

  omx_err = m_omx_resize.SetStateForComponent(OMX_StateExecuting);
  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_egl_render.GetParameter\n", CLASSNAME, __func__);
    goto do_exit;
  }

  demuxer_bytes   = m_image_size;
  demuxer_content = m_image_buffer;
  m_firstFrame    = true;

  while(demuxer_bytes > 0)
  {   
    omx_buffer = m_omx_image_decode.GetInputBuffer();
    if(omx_buffer == NULL)
      assert(0);
    
    omx_buffer->nOffset = omx_buffer->nFlags  = 0;

    omx_buffer->nFilledLen = (demuxer_bytes > omx_buffer->nAllocLen) ? omx_buffer->nAllocLen : demuxer_bytes;
    memcpy(omx_buffer->pBuffer, demuxer_content, omx_buffer->nFilledLen);

    demuxer_content += omx_buffer->nFilledLen;
    demuxer_bytes -= omx_buffer->nFilledLen;

    if(demuxer_bytes == 0)
      omx_buffer->nFlags |= OMX_BUFFERFLAG_EOS;

    //printf("demuxer_bytes %ld %ld %ld \n", demuxer_bytes, omx_buffer->nFilledLen, omx_buffer->nAllocLen);

    omx_err = m_omx_image_decode.EmptyThisBuffer(omx_buffer);
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);
      break;
    }
    if(m_firstFrame)
    {
      m_firstFrame = false;

      omx_err = m_omx_image_decode.WaitForEvent(OMX_EventPortSettingsChanged);

      if(omx_err == OMX_ErrorStreamCorrupt)
      //if(m_omx_image_decode.GotError(OMX_ErrorStreamCorrupt))
      {
        CLog::Log(LOGERROR, "%s::%s - image not unsupported\n", CLASSNAME, __func__);
        return false;
      }

      m_omx_image_decode.SendCommand(OMX_CommandPortDisable, m_omx_image_decode.GetOutputPort(), NULL);
 
      m_omx_resize.SendCommand(OMX_CommandPortDisable, m_omx_resize.GetInputPort(), NULL);
      m_omx_resize.WaitForCommand(OMX_CommandPortDisable, m_omx_resize.GetInputPort());
      m_omx_resize.WaitForEvent(OMX_EventPortSettingsChanged);

      m_omx_resize.SendCommand(OMX_CommandPortDisable, m_omx_resize.GetOutputPort(), NULL);

      OMX_PARAM_PORTDEFINITIONTYPE port_image;
      OMX_INIT_STRUCTURE(port_image);

      port_image.nPortIndex = m_omx_image_decode.GetOutputPort();
      m_omx_image_decode.GetParameter(OMX_IndexParamPortDefinition, &port_image);

      port_image.nPortIndex = m_omx_resize.GetInputPort();
      m_omx_resize.SetParameter(OMX_IndexParamPortDefinition, &port_image);

      m_omx_image_decode.SendCommand(OMX_CommandPortEnable, m_omx_image_decode.GetOutputPort(), NULL);
      m_omx_image_decode.SendCommand(OMX_CommandPortEnable, m_omx_image_decode.GetInputPort(), NULL);
      m_omx_image_decode.WaitForCommand(OMX_CommandPortEnable, m_omx_image_decode.GetOutputPort());

      m_omx_resize.SendCommand(OMX_CommandPortEnable, m_omx_resize.GetInputPort(), NULL);
      m_omx_resize.WaitForCommand(OMX_CommandPortEnable, m_omx_resize.GetInputPort());

    }
  }

  omx_err = m_omx_tunnel_egl.Establish(false);
  if(omx_err != OMX_ErrorNone)
  {
    CLog::Log(LOGERROR, "%s::%s m_omx_tunnel_egl.Establish\n", CLASSNAME, __func__);
    goto do_exit;
  }

  OMX_PARAM_PORTDEFINITIONTYPE port_settings;
  OMX_INIT_STRUCTURE(port_settings);
  port_settings.nPortIndex = m_omx_egl_render.GetOutputPort();

  omx_err = m_omx_egl_render.GetParameter(OMX_IndexParamPortDefinition, &port_settings);
  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_egl_render.GetParameter\n", CLASSNAME, __func__);
    goto do_exit;
  } 

  port_settings.nBufferCountActual = 1;
  port_settings.format.video.pNativeWindow = egl_display;

  omx_err = m_omx_egl_render.SetParameter(OMX_IndexParamPortDefinition, &port_settings);
  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_egl_render.GetParameter\n", CLASSNAME, __func__);
    goto do_exit;
  } 


  omx_err = m_omx_egl_render.SendCommand(OMX_CommandPortEnable, m_omx_egl_render.GetOutputPort(), NULL);
  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_egl_render.GetParameter\n", CLASSNAME, __func__);
    goto do_exit;
  }

  omx_err = m_omx_egl_render.UseEGLImage(&m_egl_buffer, m_omx_egl_render.GetOutputPort(), NULL, egl_image);
  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_egl_render.GetParameter\n", CLASSNAME, __func__);
    goto do_exit;
  }

  omx_err = m_omx_egl_render.WaitForCommand(OMX_CommandPortEnable, m_omx_egl_render.GetOutputPort());
  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_egl_render.GetParameter\n", CLASSNAME, __func__);
    goto do_exit;
  }

  omx_err = m_omx_resize.SetStateForComponent(OMX_StateExecuting);
  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_egl_render.GetParameter\n", CLASSNAME, __func__);
    goto do_exit;
  }

  omx_err = m_omx_egl_render.SetStateForComponent(OMX_StateExecuting);
  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_egl_render.GetParameter\n", CLASSNAME, __func__);
    goto do_exit;
  }

  omx_err = m_omx_resize.WaitForEvent(OMX_EventBufferFlag, 1000);
  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_resize.WaitForEvent\n", CLASSNAME, __func__);
    goto do_exit;
  }

  omx_err = m_omx_egl_render.FillThisBuffer(m_egl_buffer);
  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_egl_render.FillThisBuffer\n", CLASSNAME, __func__);
    goto do_exit;
  }

  OMX_BUFFERHEADERTYPE* m_output_buffer;
  m_output_buffer=m_omx_egl_render.GetOutputBuffer();
  if (!m_output_buffer) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_egl_render.GetOutputBuffer\n", CLASSNAME, __func__);
    goto do_exit;
  }

  omx_err = m_omx_egl_render.SendCommand(OMX_CommandPortDisable, m_omx_egl_render.GetOutputPort(), NULL);
  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_egl_render.GetParameter\n", CLASSNAME, __func__);
  }

  return true;

do_exit:
  return false;
}

void COMXTexture::Reset(void)
{
  m_omx_image_decode.FlushInput();
 
  m_omx_tunnel_decode.Flush();
  m_omx_tunnel_decode.Flush();
  m_omx_tunnel_egl.Flush();
}


