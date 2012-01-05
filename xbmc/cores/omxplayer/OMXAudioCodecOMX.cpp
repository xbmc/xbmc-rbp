/*
 *      Copyright (C) 2005-2008 Team XBMC
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

#include "OMXAudioCodecOMX.h"
#ifdef _LINUX
#include "XMemUtils.h"
#endif
#include "utils/log.h"

#ifdef CLASSNAME
#undef CLASSNAME
#endif
#define CLASSNAME "COMXAudioCodecOMX"

COMXAudioCodecOMX::COMXAudioCodecOMX()
{
  m_iBufferSize1 = 0;
  m_pBuffer1     = (BYTE*)_aligned_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE + FF_INPUT_BUFFER_PADDING_SIZE, 16);
  memset(m_pBuffer1, 0, AVCODEC_MAX_AUDIO_FRAME_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);

  m_iBufferSize2 = 0;
  m_pBuffer2     = (BYTE*)_aligned_malloc(AVCODEC_MAX_AUDIO_FRAME_SIZE + FF_INPUT_BUFFER_PADDING_SIZE, 16);
  memset(m_pBuffer2, 0, AVCODEC_MAX_AUDIO_FRAME_SIZE + FF_INPUT_BUFFER_PADDING_SIZE);

  m_iBuffered = 0;
  m_pCodecContext = NULL;
  m_pConvert = NULL;
  m_bOpenedCodec = false;

  m_channelMap[0] = PCM_INVALID;
  m_channels = 0;
  m_layout = 0;
  m_exit    = false;
  m_firstFrame = false;
  m_omx_channels = 0;

  m_eEncoding = OMX_AUDIO_CodingUnused;

}

COMXAudioCodecOMX::~COMXAudioCodecOMX()
{
  m_exit = true;

  _aligned_free(m_pBuffer1);
  _aligned_free(m_pBuffer2);

  Dispose();
}

bool COMXAudioCodecOMX::Open(COMXStreamInfo &hints)
{
  m_codec = hints.codec;
  /* select omx decoder */
  switch(hints.codec)
  {
    /*
    case CODEC_ID_PCM_S16LE:
      CLog::Log(LOGDEBUG, "COMXAudioCodecOMX::Open OMX Codec OMX_AUDIO_CodingPCM\n");
      m_eEncoding = OMX_AUDIO_CodingPCM;
      break;
    */
    /*
    case CODEC_ID_AAC:
      CLog::Log(LOGDEBUG, "COMXAudioCodecOMX::Open OMX Codec OMX_AUDIO_CodingAAC\n");
      m_eEncoding = OMX_AUDIO_CodingAAC;
      break;
    */
    /*
    case CODEC_ID_MP3:
      CLog::Log(LOGDEBUG, "COMXAudioCodecOMX::Open OMX Codec OMX_AUDIO_CodingMP3\n");
      m_eEncoding = OMX_AUDIO_CodingMP3;
      break;
    case CODEC_ID_MP2:
      CLog::Log(LOGDEBUG, "COMXAudioCodecOMX::Open OMX Codec OMX_AUDIO_CodingMP3\n");
      m_eEncoding = OMX_AUDIO_CodingMP3;
      break;
    case CODEC_ID_AC3:
      CLog::Log(LOGDEBUG, "COMXAudioCodecOMX::Open OMX Codec OMX_AUDIO_CodingDDP\n");
      m_eEncoding = OMX_AUDIO_CodingDDP;
      break;
    case CODEC_ID_DTS:
      CLog::Log(LOGDEBUG, "COMXAudioCodecOMX::Open OMX Codec OMX_AUDIO_CodingDTS\n");
      m_eEncoding = OMX_AUDIO_CodingDTS;
      break;
    case CODEC_ID_EAC3:
      CLog::Log(LOGDEBUG, "COMXAudioCodecOMX::Open OMX Codec OMX_AUDIO_CodingDDP\n");
      m_eEncoding = OMX_AUDIO_CodingDDP;
      break;
    case CODEC_ID_VORBIS:
      CLog::Log(LOGDEBUG, "COMXAudioCodecOMX::Open OMX Codec OMX_AUDIO_CodingVORBIS\n");
      m_eEncoding = OMX_AUDIO_CodingVORBIS;
      break;
    */
    default:
      m_eEncoding = OMX_AUDIO_CodingUnused;
      break;
  }

  // set up the number/size of buffers
  if(m_eEncoding != OMX_AUDIO_CodingUnused)
  {
    OMX_ERRORTYPE omx_err = OMX_ErrorNone;
    CStdString componentName = "";

    componentName = "OMX.broadcom.audio_decode";
    if(!m_omx_decoder.Initialize((const CStdString)componentName, OMX_IndexParamAudioInit))
      return false;

    OMX_PARAM_PORTDEFINITIONTYPE port_param;
    OMX_INIT_STRUCTURE(port_param);
    port_param.nPortIndex = m_omx_decoder.GetInputPort();

    omx_err = m_omx_decoder.GetParameter(OMX_IndexParamPortDefinition, &port_param);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXAudioCodecOMX::Open error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", omx_err);
      return false;
    }

    port_param.format.audio.eEncoding = m_eEncoding;

    omx_err = m_omx_decoder.SetParameter(OMX_IndexParamPortDefinition, &port_param);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXAudioCodecOMX::Open error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", omx_err);
      return false;
    }

    /*
    OMX_INIT_STRUCTURE(port_param);
    port_param.nPortIndex = m_omx_decoder.GetOutputPort();
    omx_err = m_omx_decoder.GetParameter(OMX_IndexParamPortDefinition, &port_param);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXAudioCodecOMX::Open error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", omx_err);
      return false;
    }

    port_param.format.audio.eEncoding = OMX_AUDIO_CodingPCM;
    port_param.nBufferSize = 1024*32;
    port_param.nBufferCountActual = 10;

    omx_err = m_omx_decoder.SetParameter(OMX_IndexParamPortDefinition, &port_param);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXAudioCodecOMX::Open error OMX_IndexParamPortDefinition omx_err(0x%08x)\n", omx_err);
      return false;
    }
    */

    OMX_AUDIO_PARAM_PORTFORMATTYPE formatType;
    OMX_INIT_STRUCTURE(formatType);
    formatType.nPortIndex = m_omx_decoder.GetInputPort();

    formatType.eEncoding = m_eEncoding;
    omx_err = m_omx_decoder.SetParameter(OMX_IndexParamAudioPortFormat, &formatType);
    if(omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXAudioCodecOMX::Open error OMX_IndexParamAudioPortFormat omx_err(0x%08x)\n", omx_err);
      return false;
    }

    // Alloc buffers for the omx input port.
    omx_err = m_omx_decoder.AllocInputBuffers();
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXAudioCodecOMX::Open AllocOMXInputBuffers\n");
      return false;
    }

    // Alloc buffers for the omx output port.
    omx_err = m_omx_decoder.AllocOutputBuffers();
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXAudioCodecOMX::Open AllocOMXOutputBuffers\n");
      return false;
    }

    omx_err = m_omx_decoder.SetStateForComponent(OMX_StateExecuting);
    if (omx_err != OMX_ErrorNone)
    {
      CLog::Log(LOGERROR, "COMXAudioCodecOMX::Open error m_omx_decoder.SetStateForComponent\n");
      return false;
    }
    m_firstFrame = true;

    /* send decoder config */
    /*
    if(hints.extrasize > 0 && hints.extradata != NULL)
    {
      CLog::Log(LOGERROR, "COMXAudioCodecOMX::Open Audio Codec : send decoder config\n");
      OMX_BUFFERHEADERTYPE* omx_buffer = m_omx_decoder.GetInputBuffer();

      if(omx_buffer == NULL)
      {
        CLog::Log(LOGERROR, "%s::%s - buffer error\n", CLASSNAME, __func__);
        return false;
      }

      omx_buffer->nOffset = 0;
      omx_buffer->nFilledLen = hints.extrasize;
      if(omx_buffer->nFilledLen > omx_buffer->nAllocLen)
      {
        CLog::Log(LOGERROR, "COMXAudio::Initialize - omx_buffer->nFilledLen > omx_buffer->nAllocLen");
        return false;
      }

      memset((unsigned char *)omx_buffer->pBuffer, 0x0, omx_buffer->nAllocLen);
      memcpy((unsigned char *)omx_buffer->pBuffer, hints.extradata, omx_buffer->nFilledLen);
      omx_buffer->nFlags = OMX_BUFFERFLAG_CODECCONFIG | OMX_BUFFERFLAG_ENDOFFRAME;
  
      omx_err = m_omx_decoder.EmptyThisBuffer(omx_buffer);
      if (omx_err != OMX_ErrorNone)
      {
        CLog::Log(LOGERROR, "%s::%s - EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);
        return false;
      }
    }
    */
  }

  AVCodec* pCodec;
  m_bOpenedCodec = false;

  if (!m_dllAvCore.Load() || !m_dllAvUtil.Load() || !m_dllAvCodec.Load())
    return false;

  m_dllAvCodec.avcodec_register_all();
  m_pCodecContext = m_dllAvCodec.avcodec_alloc_context();
  m_dllAvCodec.avcodec_get_context_defaults(m_pCodecContext);

  pCodec = m_dllAvCodec.avcodec_find_decoder(hints.codec);
  if (!pCodec)
  {
    CLog::Log(LOGDEBUG,"COMXAudioCodecOMX::Open() Unable to find codec %d", hints.codec);
    return false;
  }

  m_pCodecContext->debug_mv = 0;
  m_pCodecContext->debug = 0;
  m_pCodecContext->workaround_bugs = 1;

  if (pCodec->capabilities & CODEC_CAP_TRUNCATED)
    m_pCodecContext->flags |= CODEC_FLAG_TRUNCATED;

  m_channels = 0;
  m_pCodecContext->channels = hints.channels;
  m_pCodecContext->sample_rate = hints.samplerate;
  m_pCodecContext->block_align = hints.blockalign;
  m_pCodecContext->bit_rate = hints.bitrate;
  m_pCodecContext->bits_per_coded_sample = hints.bitspersample;

  if(m_pCodecContext->bits_per_coded_sample == 0)
    m_pCodecContext->bits_per_coded_sample = 16;

  if( hints.extradata && hints.extrasize > 0 )
  {
    m_pCodecContext->extradata_size = hints.extrasize;
    m_pCodecContext->extradata = (uint8_t*)m_dllAvUtil.av_mallocz(hints.extrasize + FF_INPUT_BUFFER_PADDING_SIZE);
    memcpy(m_pCodecContext->extradata, hints.extradata, hints.extrasize);
  }

  if (m_dllAvCodec.avcodec_open(m_pCodecContext, pCodec) < 0)
  {
    CLog::Log(LOGDEBUG,"COMXAudioCodecOMX::Open() Unable to open codec");
    Dispose();
    return false;
  }

  m_bOpenedCodec = true;
  m_iSampleFormat = AV_SAMPLE_FMT_NONE;
  return true;
}

void COMXAudioCodecOMX::Dispose()
{
  if (m_pConvert)
  {
    m_dllAvCodec.av_audio_convert_free(m_pConvert);
    m_pConvert = NULL;

  }

  if(m_bOpenedCodec && m_eEncoding != OMX_AUDIO_CodingUnused)
  {
    m_omx_decoder.Deinitialize();

    m_firstFrame = false;
  }

  if (m_pCodecContext)
  {
    if (m_bOpenedCodec) m_dllAvCodec.avcodec_close(m_pCodecContext);
    m_bOpenedCodec = false;
    m_dllAvUtil.av_free(m_pCodecContext);
    m_pCodecContext = NULL;
  }

  m_dllAvCodec.Unload();
  m_dllAvUtil.Unload();

  m_iBufferSize1 = 0;
  m_iBufferSize2 = 0;
  m_iBuffered = 0;
}

int COMXAudioCodecOMX::Decode(BYTE* pData, int iSize)
{
  int iBytesUsed;
  if (!m_pCodecContext) return -1;

  /* omx decoding */
  if(m_eEncoding != OMX_AUDIO_CodingUnused)
  {
    OMX_ERRORTYPE omx_err;

    unsigned int demuxer_bytes = (unsigned int)iSize;
    uint8_t *demuxer_content = pData;

    while(demuxer_bytes)
    {
      // 200 ms timeout
      OMX_BUFFERHEADERTYPE *omx_buffer = m_omx_decoder.GetInputBuffer(200);
      if(omx_buffer == NULL)
      {
        CLog::Log(LOGERROR, "COMXAudioCodecOMX::Decode error\n");
        printf("COMXAudioCodecOMX::Decode error\n");
        return demuxer_bytes;
      }

      omx_buffer->nOffset = 0;
      omx_buffer->nFlags = 0;

      omx_buffer->nFilledLen = (demuxer_bytes > omx_buffer->nAllocLen) ? omx_buffer->nAllocLen : demuxer_bytes;
      memcpy(omx_buffer->pBuffer, demuxer_content, omx_buffer->nFilledLen);

      iBytesUsed = omx_buffer->nFilledLen;

      demuxer_bytes -= omx_buffer->nFilledLen;
      demuxer_content += omx_buffer->nFilledLen;

      if(demuxer_bytes == 0)
        omx_buffer->nFlags |= OMX_BUFFERFLAG_ENDOFFRAME;

      /*
      if((m_codec == CODEC_ID_MP3 || m_codec == CODEC_ID_AAC || m_codec == CODEC_ID_AC3 || m_codec == CODEC_ID_DTS) && demuxer_bytes == 0)
        omx_buffer->nFlags = OMX_BUFFERFLAG_ENDOFFRAME;
      */

#ifdef OMX_SKIP64BIT
      omx_buffer->nTimeStamp.nLowPart = 0;
      omx_buffer->nTimeStamp.nHighPart = 0;
#else
      omx_buffer->nTimeStamp = 0;
#endif

      //omx_buffer->pAppPrivate = omx_buffer;

      /*
      CLog::Log(LOGDEBUG, "%s::%s - feeding decoder, omx_buffer->pBuffer(%p), copy_len(%d)\n",
        CLASSNAME, __func__, omx_buffer->pBuffer, omx_buffer->nFilledLen);
      printf("%s::%s - feeding decoder, omx_buffer->pBuffer(%p), copy_len(%d)\n",
        CLASSNAME, __func__, omx_buffer->pBuffer, omx_buffer->nFilledLen);
      */
      
      omx_err = OMX_EmptyThisBuffer(m_omx_decoder.GetComponent(), omx_buffer);
      if (omx_err != OMX_ErrorNone)
      {
        CLog::Log(LOGERROR, "%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);

        printf("%s::%s - OMX_EmptyThisBuffer() failed with result(0x%x)\n", CLASSNAME, __func__, omx_err);
        
        return 0;
      }
      if(m_firstFrame)
      {
        m_firstFrame = false;
        m_omx_decoder.WaitForEvent(OMX_EventPortSettingsChanged);

        m_omx_decoder.SendCommand(OMX_CommandPortDisable, m_omx_decoder.GetOutputPort(), NULL);
        //m_omx_decoder.WaitForCommand(OMX_CommandPortDisable, m_omx_decoder.GetOutputPort());

        m_omx_decoder.SendCommand(OMX_CommandPortEnable, m_omx_decoder.GetOutputPort(), NULL);
        m_omx_decoder.WaitForCommand(OMX_CommandPortEnable, m_omx_decoder.GetOutputPort());

        OMX_INIT_STRUCTURE(m_pcm);
        m_pcm.nPortIndex         = m_omx_decoder.GetOutputPort();
        omx_err = m_omx_decoder.GetParameter(OMX_IndexParamAudioPcm, &m_pcm);
        if(omx_err != OMX_ErrorNone)
        {
          CLog::Log(LOGERROR, "%s::%s - OMX_IndexParamAudioPcm omx_err(0x%08x)\n", CLASSNAME, __func__, omx_err);
        }
        PrintPCM(&m_pcm);

        m_omx_channels = m_pcm.nChannels; 

      }
      return iBytesUsed;
    }
    return iSize;
  }

  m_iBufferSize1 = AVCODEC_MAX_AUDIO_FRAME_SIZE ;
  m_iBufferSize2 = 0;

  AVPacket avpkt;
  m_dllAvCodec.av_init_packet(&avpkt);
  avpkt.data = pData;
  avpkt.size = iSize;
  iBytesUsed = m_dllAvCodec.avcodec_decode_audio3( m_pCodecContext
                                                 , (int16_t*)m_pBuffer1
                                                 , &m_iBufferSize1
                                                 , &avpkt);

  /* some codecs will attempt to consume more data than what we gave */
  if (iBytesUsed > iSize)
  {
    CLog::Log(LOGWARNING, "COMXAudioCodecOMX::Decode - decoder attempted to consume more data than given");
    iBytesUsed = iSize;
  }

  if(m_iBufferSize1 == 0 && iBytesUsed >= 0)
    m_iBuffered += iBytesUsed;
  else
    m_iBuffered = 0;

  if(m_pCodecContext->sample_fmt != AV_SAMPLE_FMT_S16 && m_iBufferSize1 > 0)
  {
    if(m_pConvert && m_pCodecContext->sample_fmt != m_iSampleFormat)
    {
      m_dllAvCodec.av_audio_convert_free(m_pConvert);
      m_pConvert = NULL;
    }

    if(!m_pConvert)
    {
      m_iSampleFormat = m_pCodecContext->sample_fmt;
      m_pConvert = m_dllAvCodec.av_audio_convert_alloc(AV_SAMPLE_FMT_S16, 1, m_pCodecContext->sample_fmt, 1, NULL, 0);
    }

    if(!m_pConvert)
    {
      CLog::Log(LOGERROR, "COMXAudioCodecOMX::Decode - Unable to convert %d to AV_SAMPLE_FMT_S16", m_pCodecContext->sample_fmt);
      m_iBufferSize1 = 0;
      m_iBufferSize2 = 0;
      return iBytesUsed;
    }

    const void *ibuf[6] = { m_pBuffer1 };
    void       *obuf[6] = { m_pBuffer2 };
    int         istr[6] = { m_dllAvCore.av_get_bits_per_sample_fmt(m_pCodecContext->sample_fmt)/8 };
    int         ostr[6] = { 2 };
    int         len     = m_iBufferSize1 / istr[0];
    if(m_dllAvCodec.av_audio_convert(m_pConvert, obuf, ostr, ibuf, istr, len) < 0)
    {
      CLog::Log(LOGERROR, "COMXAudioCodecOMX::Decode - Unable to convert %d to AV_SAMPLE_FMT_S16", (int)m_pCodecContext->sample_fmt);
      m_iBufferSize1 = 0;
      m_iBufferSize2 = 0;
      return iBytesUsed;
    }

    m_iBufferSize1 = 0;
    m_iBufferSize2 = len * ostr[0];
  }

  return iBytesUsed;
}

int COMXAudioCodecOMX::GetData(BYTE** dst)
{
  if(m_eEncoding != OMX_AUDIO_CodingUnused)
  {
    OMX_ERRORTYPE omx_err;

    OMX_BUFFERHEADERTYPE *omx_buffer = m_omx_decoder.GetOutputBuffer();
    if(omx_buffer == NULL)
      return 0;

    omx_buffer->nOffset = 0;

    bool done = omx_buffer->nFlags & OMX_BUFFERFLAG_EOS;

    if(omx_buffer->nFilledLen > AVCODEC_MAX_AUDIO_FRAME_SIZE) 
    {
      *dst = NULL;
      m_iBufferSize1 = 0;
    }
    else
    {
      // omx decoed to 8 channels
      if(m_pCodecContext->channels == 6 && m_omx_channels == 8)
      {
        int copy_chunk_len = (m_pCodecContext->bits_per_coded_sample >> 3) * m_pCodecContext->channels;
        int omx_chunk_len = (m_pCodecContext->bits_per_coded_sample >> 3) * m_omx_channels;

        uint8_t *pDst = m_pBuffer1;
        uint8_t *pSrc =  (uint8_t *)omx_buffer->pBuffer;
        m_iBufferSize1 = 0;
        int omx_copied = 0;

        while(true)
        {
          if(omx_copied >= (int)omx_buffer->nFilledLen)
            break;

          memcpy(pDst, pSrc, copy_chunk_len);
          pSrc += omx_chunk_len;
          pDst += copy_chunk_len;
          m_iBufferSize1 += copy_chunk_len;
          omx_copied += omx_chunk_len;
        }
        *dst = m_pBuffer1;
      }
      else
      {
        m_iBufferSize1 = omx_buffer->nFilledLen;
        *dst = m_pBuffer1;
        memcpy(m_pBuffer1, (uint8_t *)omx_buffer->pBuffer, m_iBufferSize1);
      }
    }

    if(!done)
    {
      omx_err = m_omx_decoder.FillThisBuffer(omx_buffer);
      if (omx_err != OMX_ErrorNone)
      {
        CLog::Log(LOGERROR, "%s::%s - EmptyFillBuffer() failed with result(0x%x)\n",
          CLASSNAME, __func__, omx_err);
        return 0;
      }
    }

    return m_iBufferSize1;
  }

  if(m_iBufferSize1)
  {
    *dst = m_pBuffer1;
    return m_iBufferSize1;
  }
  if(m_iBufferSize2)
  {
    *dst = m_pBuffer2;
    return m_iBufferSize2;
  }
  return 0;
}

void COMXAudioCodecOMX::Reset()
{
  if(m_eEncoding != OMX_AUDIO_CodingUnused)
  {
    m_omx_decoder.FlushInput();
    m_omx_decoder.FlushOutput();
  }

  if (m_pCodecContext) m_dllAvCodec.avcodec_flush_buffers(m_pCodecContext);
  m_iBufferSize1 = 0;
  m_iBufferSize2 = 0;
  m_iBuffered = 0;
}

int COMXAudioCodecOMX::GetChannels()
{
  return m_pCodecContext->channels;
}

int COMXAudioCodecOMX::GetSampleRate()
{
  if (m_pCodecContext) return m_pCodecContext->sample_rate;
  return 0;
}

int COMXAudioCodecOMX::GetBitsPerSample()
{
  return 16;
}

int COMXAudioCodecOMX::GetBitRate()
{
  if (m_pCodecContext) return m_pCodecContext->bit_rate;
  return 0;
}

static unsigned count_bits(int64_t value)
{
  unsigned bits = 0;
  for(;value;++bits)
    value &= value - 1;
  return bits;
}

void COMXAudioCodecOMX::BuildChannelMap()
{
  if(m_eEncoding != OMX_AUDIO_CodingUnused)
  {
    for(int i = 0; i < OMX_AUDIO_MAXCHANNELS; i++)
    {
      switch(m_pcm.eChannelMapping[i])
      {
        case OMX_AUDIO_ChannelLF:
          m_channelMap[i] = PCM_FRONT_LEFT;
          break;
        case OMX_AUDIO_ChannelRF:
          m_channelMap[i] = PCM_FRONT_RIGHT;
          break;
        case OMX_AUDIO_ChannelLR:
          m_channelMap[i] = PCM_BACK_LEFT;
          break;
        case OMX_AUDIO_ChannelRR:
          m_channelMap[i] = PCM_BACK_RIGHT;
          break;
        case OMX_AUDIO_ChannelCF:
          m_channelMap[i] = PCM_FRONT_CENTER;
          break;
        case OMX_AUDIO_ChannelLFE:
          m_channelMap[i] = PCM_LOW_FREQUENCY;
          break;
        case OMX_AUDIO_ChannelLS:
          m_channelMap[i] = PCM_SIDE_LEFT;
          break;
        case OMX_AUDIO_ChannelRS:
          m_channelMap[i] = PCM_SIDE_RIGHT;
          break;
        case OMX_AUDIO_ChannelMax:
        default:
          m_channelMap[i] = PCM_INVALID;
          break;
      }
    }
  }
  else
  {
    if (m_channels == m_pCodecContext->channels && m_layout == m_pCodecContext->channel_layout)
      return; //nothing to do here

    m_channels = m_pCodecContext->channels;
    m_layout   = m_pCodecContext->channel_layout;

    int64_t layout;

    int bits = count_bits(m_pCodecContext->channel_layout);
    if (bits == m_pCodecContext->channels)
      layout = m_pCodecContext->channel_layout;
    else
    {
      CLog::Log(LOGINFO, "COMXAudioCodecOMX::GetChannelMap - FFmpeg reported %d channels, but the layout contains %d ignoring", m_pCodecContext->channels, bits);
      layout = m_dllAvCodec.avcodec_guess_channel_layout(m_pCodecContext->channels, m_pCodecContext->codec_id, NULL);
    }

    int index = 0;
    if (layout & AV_CH_FRONT_LEFT           ) m_channelMap[index++] = PCM_FRONT_LEFT           ;
    if (layout & AV_CH_FRONT_RIGHT          ) m_channelMap[index++] = PCM_FRONT_RIGHT          ;
    if (layout & AV_CH_FRONT_CENTER         ) m_channelMap[index++] = PCM_FRONT_CENTER         ;
    if (layout & AV_CH_LOW_FREQUENCY        ) m_channelMap[index++] = PCM_LOW_FREQUENCY        ;
    if (layout & AV_CH_BACK_LEFT            ) m_channelMap[index++] = PCM_BACK_LEFT            ;
    if (layout & AV_CH_BACK_RIGHT           ) m_channelMap[index++] = PCM_BACK_RIGHT           ;
    if (layout & AV_CH_FRONT_LEFT_OF_CENTER ) m_channelMap[index++] = PCM_FRONT_LEFT_OF_CENTER ;
    if (layout & AV_CH_FRONT_RIGHT_OF_CENTER) m_channelMap[index++] = PCM_FRONT_RIGHT_OF_CENTER;
    if (layout & AV_CH_BACK_CENTER          ) m_channelMap[index++] = PCM_BACK_CENTER          ;
    if (layout & AV_CH_SIDE_LEFT            ) m_channelMap[index++] = PCM_SIDE_LEFT            ;
    if (layout & AV_CH_SIDE_RIGHT           ) m_channelMap[index++] = PCM_SIDE_RIGHT           ;
    if (layout & AV_CH_TOP_CENTER           ) m_channelMap[index++] = PCM_TOP_CENTER           ;
    if (layout & AV_CH_TOP_FRONT_LEFT       ) m_channelMap[index++] = PCM_TOP_FRONT_LEFT       ;
    if (layout & AV_CH_TOP_FRONT_CENTER     ) m_channelMap[index++] = PCM_TOP_FRONT_CENTER     ;
    if (layout & AV_CH_TOP_FRONT_RIGHT      ) m_channelMap[index++] = PCM_TOP_FRONT_RIGHT      ;
    if (layout & AV_CH_TOP_BACK_LEFT        ) m_channelMap[index++] = PCM_TOP_BACK_LEFT        ;
    if (layout & AV_CH_TOP_BACK_CENTER      ) m_channelMap[index++] = PCM_TOP_BACK_CENTER      ;
    if (layout & AV_CH_TOP_BACK_RIGHT       ) m_channelMap[index++] = PCM_TOP_BACK_RIGHT       ;

    //terminate the channel map
    m_channelMap[index] = PCM_INVALID;
  }
}

enum PCMChannels* COMXAudioCodecOMX::GetChannelMap()
{
  BuildChannelMap();

  if (m_channelMap[0] == PCM_INVALID)
    return NULL;

  return m_channelMap;
}

void COMXAudioCodecOMX::PrintPCM(OMX_AUDIO_PARAM_PCMMODETYPE *pcm)
{
  CLog::Log(LOGDEBUG, "pcm->nPortIndex     : %d\n", (int)pcm->nPortIndex);
  CLog::Log(LOGDEBUG, "pcm->eNumData       : %d\n", pcm->eNumData);
  CLog::Log(LOGDEBUG, "pcm->eEndian        : %d\n", pcm->eEndian);
  CLog::Log(LOGDEBUG, "pcm->bInterleaved   : %d\n", (int)pcm->bInterleaved);
  CLog::Log(LOGDEBUG, "pcm->nBitPerSample  : %d\n", (int)pcm->nBitPerSample);
  CLog::Log(LOGDEBUG, "pcm->ePCMMode       : %d\n", pcm->ePCMMode);
  CLog::Log(LOGDEBUG, "pcm->nChannels      : %d\n", (int)pcm->nChannels);
  CLog::Log(LOGDEBUG, "pcm->nSamplingRate  : %d\n", (int)pcm->nSamplingRate);
}
