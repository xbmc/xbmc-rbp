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

#include "OMXClock.h"
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

#define EXIF_TAG_ORIENTATION    0x0112

COMXTexture::COMXTexture()
{
  m_is_open       = false;
  m_output_buffer = NULL;

  OMX_INIT_STRUCTURE(m_output_format);
}

COMXTexture::~COMXTexture()
{
  if (m_is_open)
    Close();
}

bool COMXTexture::Open(void)
{
  std::string componentName = "";

  componentName = "OMX.broadcom.image_decode";
  if(!m_omx_decoder.Initialize((const std::string)componentName, OMX_IndexParamImageInit)) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_render.SetStateForComponent\n", CLASSNAME, __func__);
    return false;
  }
  componentName = "OMX.broadcom.resize";
  if(!m_omx_resize.Initialize((const std::string)componentName, OMX_IndexParamImageInit)) {
    CLog::Log(LOGERROR, "%s::%s error m_omx_render.SetStateForComponent\n", CLASSNAME, __func__);
    return false;
  }

  m_omx_tunnel_decode.Initialize(&m_omx_decoder, m_omx_decoder.GetOutputPort(), &m_omx_resize,       m_omx_resize.GetInputPort());

  m_is_open       = true;

  return true;
}

void COMXTexture::Close()
{
  m_output_buffer = NULL;

  m_omx_decoder.FlushInput();
  m_omx_decoder.FreeInputBuffers(true);
  m_omx_resize.FlushOutput();
  m_omx_resize.FreeOutputBuffers(true);

  Reset();

  m_omx_tunnel_decode.Deestablish();

  // delete components
  m_omx_decoder.Deinitialize();
  m_omx_resize.Deinitialize();

  OMX_INIT_STRUCTURE(m_output_format);

  m_is_open       = false;
}

int COMXTexture::Decode(COMXImage *omx_image, unsigned width, unsigned height)
{
  bool m_firstFrame = true;
  OMX_BUFFERHEADERTYPE *omx_buffer = NULL;
  unsigned int demuxer_bytes = 0;
  const uint8_t *demuxer_content = NULL;
  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  int nTimeOut = 0;
  OMX_INIT_STRUCTURE(m_output_format);

  if(!m_is_open || !omx_image)
    return false;

  if(omx_image->GetCompressionFormat() == OMX_IMAGE_CodingMax)
  {
    CLog::Log(LOGERROR, "%s::%s error unsupported image format\n", CLASSNAME, __func__);
    goto do_exit;
  }

  if (!m_is_open) {
    CLog::Log(LOGERROR, "%s::%s error m_is_open\n", CLASSNAME, __func__);
    goto do_exit;
  }

  omx_err = m_omx_tunnel_decode.Establish(false);
  if(omx_err != OMX_ErrorNone) {
    CLog::Log(LOGERROR, "%s::%s m_omx_tunnel_decode.Establish\n", CLASSNAME, __func__);
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
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "%s::%s m_omx_resize.SetStateForComponent result(0x%x)\n", CLASSNAME, __func__, omx_err);
    goto do_exit;
  }

  OMX_PARAM_PORTDEFINITIONTYPE port_def;
  OMX_INIT_STRUCTURE(port_def);
  port_def.nPortIndex = m_omx_decoder.GetInputPort();

  omx_err = m_omx_decoder.GetParameter(OMX_IndexParamPortDefinition, &port_def);
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "%s::%s m_omx_decoder.GetParameter result(0x%x)\n", CLASSNAME, __func__, omx_err);
    goto do_exit;
  }

  port_def.format.image.eCompressionFormat = omx_image->GetCompressionFormat();
  port_def.format.image.eColorFormat = OMX_COLOR_FormatUnused;
  port_def.format.image.nFrameWidth = 0;
  port_def.format.image.nFrameHeight = 0;
  port_def.format.image.nStride = 0;
  port_def.format.image.nSliceHeight = 0;
  port_def.format.image.bFlagErrorConcealment = OMX_FALSE;

  omx_err = m_omx_decoder.SetParameter(OMX_IndexParamPortDefinition, &port_def);
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "%s::%s m_omx_decoder.SetParameter result(0x%x)\n", CLASSNAME, __func__, omx_err);
    goto do_exit;
  }

  OMX_IMAGE_PARAM_PORTFORMATTYPE port_image;
  OMX_INIT_STRUCTURE(port_image);
  port_image.nPortIndex = m_omx_decoder.GetInputPort();
  port_image.eCompressionFormat = omx_image->GetCompressionFormat();
  port_image.eColorFormat = port_def.format.image.eColorFormat;

  omx_err = m_omx_decoder.SetParameter(OMX_IndexParamImagePortFormat, &port_image);
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "%s::%s m_omx_decoder.SetParameter result(0x%x)\n", CLASSNAME, __func__, omx_err);
    goto do_exit;
  }

  omx_err = m_omx_decoder.AllocInputBuffers();
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "%s::%s m_omx_decoder.AllocInputBuffers result(0x%x)", CLASSNAME, __func__, omx_err);
    goto do_exit;
  }

  omx_err = m_omx_decoder.SetStateForComponent(OMX_StateExecuting);
  if (omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "%s::%s m_omx_decoder.SetStateForComponent result(0x%x)\n", CLASSNAME, __func__, omx_err);
    goto do_exit;
  }

  OMX_INIT_STRUCTURE(port_def);
  port_def.nPortIndex = m_omx_resize.GetOutputPort();

  omx_err = m_omx_resize.GetParameter(OMX_IndexParamPortDefinition, &port_def);
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "%s::%s m_omx_resize.GetParameter result(0x%x)\n", CLASSNAME, __func__, omx_err);
    goto do_exit;
  }

  port_def.format.image.eCompressionFormat = OMX_IMAGE_CodingUnused;
  port_def.format.image.eColorFormat = OMX_COLOR_Format32bitABGR8888;
  port_def.format.image.nFrameWidth = width;
  port_def.format.image.nFrameHeight = height;
  port_def.format.image.nStride = 0;
  port_def.format.image.nSliceHeight = 0;
  port_def.format.image.bFlagErrorConcealment = OMX_FALSE;

  omx_err = m_omx_resize.SetParameter(OMX_IndexParamPortDefinition, &port_def);
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "%s::%s m_omx_resize.SetParameter result(0x%x)\n", CLASSNAME, __func__, omx_err);
    goto do_exit;
  }

  omx_err = m_omx_resize.AllocOutputBuffers();
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "%s::%s m_omx_resize.AllocOutputBuffers result(0x%x)\n", CLASSNAME, __func__, omx_err);
    goto do_exit;
  }

  omx_err = m_omx_resize.SetStateForComponent(OMX_StateExecuting);
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "%s::%s m_omx_resize.SetStateForComponent result(0x%x)\n", CLASSNAME, __func__, omx_err);
    goto do_exit;
  }

  demuxer_bytes   = omx_image->GetImageSize();
  demuxer_content = omx_image->GetImageBuffer();
  if(!demuxer_bytes || !demuxer_content)
    goto do_exit;

  m_firstFrame    = true;

  while(demuxer_bytes > 0)
  {   
    omx_buffer = m_omx_decoder.GetInputBuffer();
    if(omx_buffer == NULL)
      goto do_exit;
    
    omx_buffer->nOffset = omx_buffer->nFlags  = 0;

    omx_buffer->nFilledLen = (demuxer_bytes > omx_buffer->nAllocLen) ? omx_buffer->nAllocLen : demuxer_bytes;
    memcpy(omx_buffer->pBuffer, demuxer_content, omx_buffer->nFilledLen);

    demuxer_content += omx_buffer->nFilledLen;
    demuxer_bytes -= omx_buffer->nFilledLen;

    if(demuxer_bytes == 0)
      omx_buffer->nFlags |= OMX_BUFFERFLAG_EOS;

    omx_err = m_omx_decoder.EmptyThisBuffer(omx_buffer);
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "%s::%s OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);
      break;
    }
    if(m_firstFrame)
    {
      m_firstFrame = false;

      omx_err = m_omx_decoder.WaitForEvent(OMX_EventPortSettingsChanged);

      if(omx_err == OMX_ErrorStreamCorrupt)
      {
        CLog::Log(LOGERROR, "%s::%s image not unsupported\n", CLASSNAME, __func__);
        goto do_exit;
      }

      m_omx_decoder.SendCommand(OMX_CommandPortDisable, m_omx_decoder.GetOutputPort(), NULL);
 
      m_omx_resize.SendCommand(OMX_CommandPortDisable, m_omx_resize.GetInputPort(), NULL);
      m_omx_resize.WaitForCommand(OMX_CommandPortDisable, m_omx_resize.GetInputPort());
      m_omx_resize.WaitForEvent(OMX_EventPortSettingsChanged);

      m_omx_resize.SendCommand(OMX_CommandPortDisable, m_omx_resize.GetOutputPort(), NULL);

      OMX_PARAM_PORTDEFINITIONTYPE port_image;
      OMX_INIT_STRUCTURE(port_image);

      port_image.nPortIndex = m_omx_decoder.GetOutputPort();
      m_omx_decoder.GetParameter(OMX_IndexParamPortDefinition, &port_image);

      port_image.nPortIndex = m_omx_resize.GetInputPort();
      m_omx_resize.SetParameter(OMX_IndexParamPortDefinition, &port_image);

      m_omx_decoder.SendCommand(OMX_CommandPortEnable, m_omx_decoder.GetOutputPort(), NULL);
      m_omx_decoder.SendCommand(OMX_CommandPortEnable, m_omx_decoder.GetInputPort(), NULL);
      m_omx_decoder.WaitForCommand(OMX_CommandPortEnable, m_omx_decoder.GetOutputPort());

      m_omx_resize.SendCommand(OMX_CommandPortEnable, m_omx_resize.GetInputPort(), NULL);
      m_omx_resize.WaitForCommand(OMX_CommandPortEnable, m_omx_resize.GetInputPort());

    }
  }

  omx_err = m_omx_resize.SendCommand(OMX_CommandPortEnable, m_omx_resize.GetOutputPort(), NULL);
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "%s::%s m_omx_resize.SendCommand result(0x%x)\n", CLASSNAME, __func__, omx_err);
    goto do_exit;
  }

  omx_err = m_omx_resize.WaitForCommand(OMX_CommandPortEnable, m_omx_resize.GetOutputPort());
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "%s::%s m_omx_resize.WaitForCommand result(0x%x)\n", CLASSNAME, __func__, omx_err);
    goto do_exit;
  }

  omx_err = m_omx_resize.SetStateForComponent(OMX_StateExecuting);
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "%s::%s m_omx_resize.SetStateForComponent result(0x%x)\n", CLASSNAME, __func__, omx_err);
    goto do_exit;
  }

  omx_err = m_omx_decoder.WaitForEvent(OMX_EventBufferFlag, 1000);
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "%s::%s m_omx_decoder.WaitForEvent result(0x%x)\n", CLASSNAME, __func__, omx_err);
    goto do_exit;
  }

  m_output_buffer = m_omx_resize.GetOutputBuffer(1000);
    
  if(!m_output_buffer)
  {
    CLog::Log(LOGERROR, "%s::%s no output buffer\n", CLASSNAME, __func__);
    goto do_exit;
  }

  omx_err = m_omx_resize.FillThisBuffer(m_output_buffer);

  omx_err = m_omx_resize.WaitForEvent(OMX_EventBufferFlag, 1000);
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "%s::%s m_omx_resize WaitForEvent result(0x%x)\n", CLASSNAME, __func__, omx_err);
    goto do_exit;
  }

  m_output_format.nPortIndex = m_omx_resize.GetOutputPort();
  omx_err = m_omx_resize.GetParameter(OMX_IndexParamPortDefinition, &m_output_format);
  if(omx_err != OMX_ErrorNone) 
  {
    CLog::Log(LOGERROR, "%s::%s m_omx_resize.GetParameter result(0x%x)\n", CLASSNAME, __func__, omx_err);
    goto do_exit;
  }

  m_omx_tunnel_decode.Deestablish();

  return true;

do_exit:
  return false;
}

void COMXTexture::Reset(void)
{
  m_omx_decoder.FlushInput();
 
  m_omx_tunnel_decode.Flush();
  m_omx_tunnel_decode.Flush();

  m_output_buffer = NULL;
}

unsigned char *COMXTexture::GetData()
{
  if(!m_output_buffer)
    return NULL;

  return (unsigned char *)m_output_buffer->pBuffer;
}

unsigned int COMXTexture::GetSize()
{
  if(!m_output_buffer)
    return 0;
  return (unsigned int)m_output_buffer->nFilledLen;
}

