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

typedef struct {
   OMX_PARAM_CONTENTURITYPE uri;
   OMX_U8 uri_data[CONTENTURI_MAXLEN];
} OMX_CONTENTURI_TYPE_T;

COMXTexture::COMXTexture()
{
  m_is_open       = false;
}

COMXTexture::~COMXTexture()
{
  if (m_is_open)
    Close();
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
}

int COMXTexture::Decode(COMXImage *omx_image, void *egl_image, void *egl_display, unsigned width, unsigned height)
{
  bool m_firstFrame = true;
  OMX_BUFFERHEADERTYPE *omx_buffer = NULL;
  unsigned int demuxer_bytes = 0;
  const uint8_t *demuxer_content = NULL;
  OMX_ERRORTYPE omx_err = OMX_ErrorNone;
  OMX_BUFFERHEADERTYPE* m_output_buffer;
  int nTimeOut = 0;

  if(!m_is_open || !omx_image)
    return false;

  if(omx_image->GetCompressionFormat() == OMX_IMAGE_CodingMax)
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

  port_def.format.image.eCompressionFormat = omx_image->GetCompressionFormat() ;
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
  port_image.eCompressionFormat = omx_image->GetCompressionFormat();
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

  demuxer_bytes   = omx_image->GetImageSize();
  demuxer_content = omx_image->GetImageBuffer();
  if(!demuxer_bytes || !demuxer_content)
    goto do_exit;

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

  // 2000ms - this has to wait for jpeg decode, so could take a while
  while(nTimeOut < 2000)
  {
    m_output_buffer=m_omx_egl_render.GetOutputBuffer(2000);
    if (!m_output_buffer) {
      OMXClock::OMXSleep(50);
      nTimeOut += 50;
    }
    else
    {
      break;
    }
  }

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


