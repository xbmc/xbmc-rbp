// Copyright (c) 2010 TeamXBMC. All rights reserved.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

#include <pthread.h>

#include <directfb.h>
#include <advancedmediaprovider.h>
#include <cdefs_lpb.h>

#include "ffmpeg_common.h"
#include "ffmpeg_file_protocol.h"
#include "file_reader_util.h"

// AppContext - Application state
typedef struct
{
  STimeStampTag             dmxstamp;
  struct SMediaFormat       dmxformat;
  IDirectFBDataBuffer       *dmxbuffer;
  struct SMediaFormat       ptsformat;
  IDirectFBDataBuffer       *ptsbuffer;
} DemuxerContext;

typedef struct
{
  int32_t                   sourceWidth;
  int32_t                   sourceHeight;
  FFmpegFileReader          *demuxer;
  int                       av_audio_stream;
  int                       av_video_stream;
  AVCodecContext            *av_audio_context;
  AVCodecContext            *av_video_context;

  DemuxerContext            audio_demuxer;
  DemuxerContext            video_demuxer;
  pthread_t                 demux_feeder;
} AppContext;


DFBResult FeedStreamedDataBuffer(AppContext* ctx)
{
  DFBResult     ret;
  unsigned int  audio_bytes_buffered, video_bytes_buffered;
  bool          status;

  // check buffer filled length and see if it needs filling
  // this limits the amount of data in buffer to 128k
  ret = ctx->audio_demuxer.dmxbuffer->GetLength(ctx->audio_demuxer.dmxbuffer, &audio_bytes_buffered);
  ret = ctx->video_demuxer.dmxbuffer->GetLength(ctx->video_demuxer.dmxbuffer, &video_bytes_buffered);
  //if (audio_bytes_buffered < 128*1024 && video_bytes_buffered < 128*1024)
  if (video_bytes_buffered < 64*1024)
  {
    int       stream_index, byte_count;
    uint8_t*  data;
    uint64_t  dts, pts;

    status = ctx->demuxer->Read(&data, &byte_count, &stream_index, &dts, &pts);
    printf("stream(%d), abuffered(%d), vbuffered(%d), byte_count(%d), dts(%llu), pts(%llu)\n",
      stream_index, audio_bytes_buffered, video_bytes_buffered, byte_count, dts, pts);
    if (!status && byte_count > 0)
    {
      if (stream_index == ctx->av_audio_stream) {
        //ctx->audio_demuxer.dmxstamp.time = dts;
        //ctx->audio_demuxer.dmxstamp.positionIncrement += byte_count;
        //ret = ctx->audio_demuxer.ptsbuffer->PutData(ctx->audio_demuxer.ptsbuffer, &ctx->audio_demuxer.dmxstamp, sizeof(STimeStampTag));
        ret = ctx->audio_demuxer.dmxbuffer->PutData(ctx->audio_demuxer.dmxbuffer, data, byte_count);
      }
      if (stream_index == ctx->av_video_stream) {
        //ctx->video_demuxer.dmxstamp.time = dts;
        //ctx->video_demuxer.dmxstamp.positionIncrement += byte_count;
        //ret = ctx->video_demuxer.ptsbuffer->PutData(ctx->video_demuxer.ptsbuffer, &ctx->video_demuxer.dmxstamp, sizeof(STimeStampTag));
        ret = ctx->video_demuxer.dmxbuffer->PutData(ctx->video_demuxer.dmxbuffer, data, byte_count);
      }
      free(data);
      return ret;
    }
    else
    {
      return DFB_EOF;
    }
  }

  usleep(10*1000);
  return DFB_OK;
}

static bool stopStreamingThread = false;
void* DemuxFeederThread(void *arg)
{
  AppContext* ctx = (AppContext*)arg;

  printf("DemuxFeederThread started\n");
  while (!stopStreamingThread)
  {
    pthread_testcancel();

    if (FeedStreamedDataBuffer(ctx) != DFB_OK)
      break;
  }

  ctx->audio_demuxer.dmxbuffer->Finish(ctx->video_demuxer.dmxbuffer);
  ctx->video_demuxer.dmxbuffer->Finish(ctx->video_demuxer.dmxbuffer);
  //ctx->audio_demuxer.ptsbuffer->Finish(ctx->video_demuxer.ptsbuffer);
  //ctx->video_demuxer.ptsbuffer->Finish(ctx->video_demuxer.ptsbuffer);
  ctx->demux_feeder = NULL;

  printf("DemuxFeederThread stopped\n");
  return NULL;
}

int main (int argc, char * const argv[])
{
  std::string input_filename;

  if (argc > 1) {
    for (int i = 1; i < argc; i++) {
      if (strncasecmp(argv[i], "--input", 7) == 0) {
        // check the next arg with the proper value.
        int next = i + 1;
        if (next < argc) {
          input_filename = argv[next];
          i++;
        }
      } else if (strncasecmp(argv[i], "-h", 2) == 0 || strncasecmp(argv[i], "--help", 6) == 0) {
        printf("Usage: %s [OPTIONS]...\n", argv[0]);
        printf("Arguments:\n");
        printf("  --input <filename> \tInput video filename\n");
        exit(0);
      }
    }
  }
  if (input_filename.empty()) {
    printf("no input file specified\n");
    exit(0);
  }

  // install signal handlers
  //signal(SIGINT, signal_handler);
  //signal(SIGTERM, signal_handler);

  // initialize our app contex.
  AppContext ctx;
  memset(&ctx, 0, sizeof(ctx));

  // create the ffmepg file reader/demuxer
  ctx.demuxer = new FFmpegFileReader(input_filename.c_str());
  if (!ctx.demuxer->Initialize()) {
    fprintf(stderr, "ERROR: Can't initialize FFmpegFileReader\n");
    exit(0);
  }
  
  ctx.av_audio_context = ctx.demuxer->GetACodecContext();
  if (!ctx.av_audio_context) {
    fprintf(stderr, "ERROR: Invalid FFmpegFileReader Audio Codec Context\n");
  }
  ctx.av_audio_stream = ctx.demuxer->GetAStreamIndex();

  ctx.av_video_context = ctx.demuxer->GetVCodecContext();
  if (!ctx.av_video_context) {
    fprintf(stderr, "ERROR: Invalid FFmpegFileReader Video Codec Context\n");
    exit(0);
  }
  ctx.av_video_stream = ctx.demuxer->GetVStreamIndex();

  ctx.sourceWidth  = ctx.av_video_context->width;
  ctx.sourceHeight = ctx.av_video_context->height;
  printf("video width(%d), height(%d), extradata_size(%d)\n",
    (int)ctx.sourceWidth, (int)ctx.sourceHeight, ctx.av_video_context->extradata_size);

  SMediaFormat amedia  = {0, };
  SMediaFormat atiming = {0, };
  SMediaFormat vmedia  = {0, };
  SMediaFormat vtiming = {0, };

  atiming.mediaType  |= MTYPE_ELEM_TIMESTAMPTAG;
  atiming.formatValid = 1;
  atiming.format.clock.rate = 90000;
  switch(ctx.av_audio_context->codec_id)
  {
    case CODEC_ID_AC3:
      printf("CODEC_ID_AC3\n");
      amedia.mediaType = MTYPE_ELEM_AC3;
      atiming.mediaType |= MTYPE_ELEM_AC3;
    break;
    case CODEC_ID_AAC:
      printf("CODEC_ID_AAC\n");
      amedia.mediaType = MTYPE_ELEM_AAC;
      amedia.format.sound.channels = (EChannelConfig)2;
      amedia.format.sound.samplingFreq = 48000;
      amedia.format.sound.lfe = 0;
      atiming.mediaType |= MTYPE_ELEM_AAC;
    break;
    default:
      fprintf(stderr, "ERROR: Invalid FFmpegFileReader Audio Codec Format (not ac3/aac) = %d\n",
      ctx.av_audio_context->codec_id);
      exit(0);
    break;
  }

  vtiming.mediaType  |= MTYPE_ELEM_TIMESTAMPTAG;
  vtiming.formatValid = 1;
  vtiming.format.clock.isMaster = 1;
  vtiming.format.clock.rate = 90000;
  switch(ctx.av_video_context->codec_id)
  {
    case CODEC_ID_VC1:
      printf("CODEC_ID_VC1\n");
      vmedia.mediaType   = MTYPE_ELEM_VC1;
      vmedia.bitrateType = SMediaFormat::HighBitrate; // assume HD
      vtiming.mediaType |= MTYPE_ELEM_VC1;
    break;
    case CODEC_ID_WMV3:
      printf("CODEC_ID_WMV3\n");
      vmedia.mediaType   = MTYPE_ELEM_WMV;
      vmedia.bitrateType = SMediaFormat::HighBitrate; // assume HD
      vtiming.mediaType |= MTYPE_ELEM_WMV;
    break;
    case CODEC_ID_H264:
      printf("CODEC_ID_H264\n");
      vmedia.mediaType   = MTYPE_ELEM_AVC;
      vmedia.bitrateType = SMediaFormat::HighBitrate; // assume HD
      vtiming.mediaType |= MTYPE_ELEM_AVC;
    break;
    case CODEC_ID_MPEG4:
      printf("CODEC_ID_MPEG4\n");
      vmedia.mediaType   = MTYPE_ELEM_MPEG4 ;
      vmedia.bitrateType = SMediaFormat::HighBitrate; // assume HD
      vtiming.mediaType |= MTYPE_ELEM_MPEG4 ;
    break;
    case CODEC_ID_MPEG2VIDEO:
      printf("CODEC_ID_MPEG2VIDEO\n");
      vmedia.mediaType   = MTYPE_ELEM_MPEG2;
      vmedia.bitrateType = SMediaFormat::HighBitrate; // assume HD
      vtiming.mediaType |= MTYPE_ELEM_MPEG2;
    break;
    default:
      fprintf(stderr, "ERROR: Invalid FFmpegFileReader Video Codec Format (not h264/mpeg4) = %d\n",
      ctx.av_video_context->codec_id);
      exit(0);
    break;
  }

  IDirectFB *dfb = NULL;
  IAdvancedMediaProvider *pAmp = NULL;

  // watch this if/then/else game to trickle down the function calls
  if (DirectFBInit(&argc, (char***)&argv) != DFB_OK)
    fprintf(stderr, "Could not initialize DirectFB!!!\n");
  else if (DirectFBCreate(&dfb) != DFB_OK)
    fprintf(stderr, "Could not instantiate the DirectFB object!!!\n");
  else if (dfb->GetInterface(dfb, "IAdvancedMediaProvider", "EM8630", (void*)0, (void **)&pAmp) != DFB_OK)
    fprintf(stderr, "Could not instantiate the AMP interface\n");
  else
  {
    DFBResult res;
    IDirectFBEventBuffer *keybuffer = NULL;
    IDirectFBEventBuffer *pAmpEvent = NULL;

    if (dfb->CreateInputEventBuffer(dfb, DICAPS_ALL, DFB_TRUE, &keybuffer) != DFB_OK)
    {
      fprintf(stderr, "Could not create input event buffer!!!\n");
      goto _exit;
    }
    else if (pAmp->GetEventBuffer(pAmp, &pAmpEvent) != DFB_OK)
    {
      fprintf(stderr, "Could not retrieve the AMP event buffer!!!\n");
      goto _exit;
    }

    ctx.audio_demuxer.dmxstamp.time = 0;
    ctx.audio_demuxer.dmxstamp.positionIncrement = 0;
    ctx.audio_demuxer.dmxformat = amedia;
    ctx.audio_demuxer.dmxbuffer = NULL;
    ctx.audio_demuxer.ptsformat = atiming;
    ctx.audio_demuxer.ptsbuffer = NULL;

    ctx.video_demuxer.dmxstamp.time = 0;
    ctx.video_demuxer.dmxstamp.positionIncrement = 0;
    ctx.video_demuxer.dmxformat = vmedia;
    ctx.video_demuxer.dmxbuffer = NULL;
    ctx.video_demuxer.ptsformat = vtiming;
    ctx.video_demuxer.ptsbuffer = NULL;

    // If no description is specified (NULL) a streamed data buffer is created
    if (dfb->CreateDataBuffer(dfb, NULL, &ctx.video_demuxer.dmxbuffer) != DFB_OK)
      goto _exit;
    if (dfb->CreateDataBuffer(dfb, NULL, &ctx.audio_demuxer.dmxbuffer) != DFB_OK)
      goto _exit;
    //if (dfb->CreateDataBuffer(dfb, NULL, &ctx.video_demuxer.ptsbuffer) != DFB_OK)
    //  goto _exit;
    //if (dfb->CreateDataBuffer(dfb, NULL, &ctx.audio_demuxer.ptsbuffer) != DFB_OK)
    //  goto _exit;
      
    pthread_create(&ctx.demux_feeder, NULL, DemuxFeederThread, &ctx );

    res = pAmp->SetDataBuffer(pAmp, ctx.video_demuxer.dmxbuffer, &ctx.video_demuxer.dmxformat);
    res = pAmp->SetDataBuffer(pAmp, ctx.audio_demuxer.dmxbuffer, &ctx.audio_demuxer.dmxformat);
    //res = pAmp->SetDataBuffer(pAmp, ctx.video_demuxer.ptsbuffer, &ctx.video_demuxer.ptsformat);
    //res = pAmp->SetDataBuffer(pAmp, ctx.audio_demuxer.ptsbuffer, &ctx.audio_demuxer.ptsformat);

    // open the media, using the video media as parameter
    // used only to select the correct playback control (CSimplePBC)
    if (pAmp->OpenMedia(pAmp, NULL, &ctx.video_demuxer.dmxformat, NULL) == DFB_OK)
    {
      char buffer[8192] = {0, };

      ((struct SStatus *)buffer)->size = sizeof(buffer);
      ((struct SStatus *)buffer)->mediaSpace = MEDIA_SPACE_UNKNOWN;

      // wait and check the confirmation event
      if ((pAmpEvent->WaitForEventWithTimeout(pAmpEvent, 1000, 0) == DFB_OK) &&
          (pAmp->UploadStatusChanges(pAmp, (struct SStatus *)buffer, DFB_TRUE) == DFB_OK) &&
          (((struct SStatus *)buffer)->flags & SSTATUS_COMMAND) &&
        IS_SUCCESS(((struct SStatus *)buffer)->lastCmd.result)) // succeeded
      {
        DFBEvent event;

        pAmpEvent->GetEvent(pAmpEvent, &event);

        // make the graphic layer transparent so that the video layer beneath can be seen
        {
          IDirectFBDisplayLayer *layer;
          IDirectFBSurface *primary;
          int screenW, screenH;
          bool bOK = false;

          if (dfb->GetDisplayLayer(dfb, DLID_PRIMARY, &layer) == DFB_OK)
          {
            if (layer->GetSurface(layer, &primary) == DFB_OK)
            {
              primary->SetDrawingFlags(primary, DSDRAW_NOFX);
              primary->GetSize(primary, &screenW, &screenH);
              primary->SetColor(primary, 0, 0, 0, 0);
              primary->FillRectangle(primary, 0, 0, screenW, screenH);
              primary->Flip(primary, NULL, DSFLIP_NONE);

              primary->Release(primary);

              bOK = true;
            }
            else
            {
              fprintf(stderr, "Could not retrieve the surface of the primary layer!!!\n");
            }

            layer->Release(layer);
          }
          else
          {
            fprintf(stderr, "Could not retrieve the primary layer!!!\n");
          }

          if (!bOK)
            goto _close_and_exit;
        }

        // start the playback
        if (pAmp->StartPresentation(pAmp, DFB_TRUE) == DFB_OK)
        {
          ((struct SStatus *)buffer)->size = sizeof(buffer);
          ((struct SStatus *)buffer)->mediaSpace = MEDIA_SPACE_UNKNOWN;

          // wait and check the confirmation event
          if ((pAmpEvent->WaitForEventWithTimeout(pAmpEvent, 1000, 0) == DFB_OK) &&
            (pAmp->UploadStatusChanges(pAmp, (struct SStatus *)buffer, DFB_TRUE) == DFB_OK) &&
            (((struct SStatus *)buffer)->flags & SSTATUS_MODE) &&
            (((struct SStatus *)buffer)->mode.flags & SSTATUS_MODE_PLAYING))
          {
            DFBEvent keyEvent;
            struct SLPBCommand cmd;
            bool bExit = false;

            pAmpEvent->GetEvent(pAmpEvent, &event);

            // enable the video layer
            {
              IDirectFBDisplayLayer *layer;
              bool bOK = false;

              if (dfb->GetDisplayLayer(dfb, EM86LAYER_MAINVIDEO, &layer) == DFB_OK)
              {
                if (layer->SetCooperativeLevel(layer, DLSCL_EXCLUSIVE) == DFB_OK)
                {
                  IDirectFBScreen *screen;

                  if (dfb->GetScreen(dfb, 0, &screen) == DFB_OK)
                  {
                    DFBScreenMixerConfig mixcfg;

                    screen->GetMixerConfiguration(screen, 0, &mixcfg);
                    mixcfg.flags = DSMCONF_LAYERS;
                    DFB_DISPLAYLAYER_IDS_ADD(mixcfg.layers, EM86LAYER_MAINVIDEO);
                    screen->SetMixerConfiguration(screen, 0, &mixcfg);

                    screen->Release(screen);

                    bOK = true;
                  }
                  else
                    fprintf(stderr, "Could not retrieve the screen interface\n");
                }
                else
                  fprintf(stderr, "Could not set the video layer in exclusive mode\n");

                layer->Release(layer);
              }
              else
                fprintf(stderr, "Could not retrieve the video layer\n");

              if (!bOK)
                goto _close_and_exit;
            }

            // at this point the program is successful, 
            // it's just a matter of letting it run and eventually terminate it
            //sdd nResult = 0;

            cmd.dataSize = sizeof(cmd);
            cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;
            cmd.cmd = (ELPBCmd)0;

            while (!bExit)
            {
              // AMP monitoring loop for automatic program termination
              while (pAmpEvent->WaitForEventWithTimeout(pAmpEvent, 0, 100) == DFB_OK)
              {
                pAmpEvent->GetEvent(pAmpEvent, &event);

                ((struct SStatus *)buffer)->size = sizeof(buffer);
                ((struct SStatus *)buffer)->mediaSpace = MEDIA_SPACE_UNKNOWN;

                if (pAmp->UploadStatusChanges(pAmp, (struct SStatus *)buffer, DFB_TRUE) == DFB_OK)
                {
                  if ((((struct SStatus *)buffer)->flags & SSTATUS_MODE) &&
                    (((struct SStatus *)buffer)->mode.flags & SSTATUS_MODE_STOPPED) &&
                    (cmd.cmd != LPBCmd_STOP))
                  {
                    // presentation has stopped spontaneously, exit the program
                    bExit = true;
                    break;
                  }
                }
              }

              while (keybuffer->WaitForEventWithTimeout(keybuffer, 0, 100) == DFB_OK)
              {
                keybuffer->GetEvent(keybuffer, &keyEvent);

                if ((keyEvent.clazz == DFEC_INPUT) && (keyEvent.input.type == DIET_KEYPRESS))
                {
                  if (keyEvent.input.key_symbol == DIKS_POWER)
                  {
                    // POWER key has been pressed, exit the program
                    bExit = true;
                    break;
                  }
                  else
                  {
                    switch (keyEvent.input.key_symbol)
                    {
                      case DIKS_FASTFORWARD:
                        printf("Issuing FAST FORWARD command...\n");
                        cmd.cmd = LPBCmd_FAST_FORWARD;
                        cmd.param2.speed = 5*1024;  // 5 x FFWD
                        break;

                      case DIKS_SLOW:
                        printf("Issuing SLOW FORWARD command...\n");
                        cmd.cmd = LPBCmd_SCAN_FORWARD;
                        cmd.param2.speed = 1024/2;  // 1/2 x SFWD
                        break;

                      case DIKS_PLAY:
                        printf("Issuing PLAY command to resume normal playback\n");
                        cmd.cmd = LPBCmd_PLAY; // back to regular playback
                        break;

                      case DIKS_STOP:
                        printf("Issuing STOP command...\n");
                        cmd.cmd = LPBCmd_STOP; // back to regular playback
                        cmd.param1.stopMode = SM_LAST_FRAME;
                        break;

                      case DIKS_PAUSE:
                        printf("Issuing Pause command...\n");
                        cmd.cmd = LPBCmd_PAUSE_ON; // back to regular playback
                        break;

                      default:
                        break;
                    }

                    if (cmd.cmd)
                    {
                      struct SLPBResult res;

                      res.dataSize = sizeof(res);
                      res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;
                      if (pAmp->ExecutePresentationCmd(pAmp, (struct SCommand *)&cmd, (struct SResult *)&res) == DFB_OK)
                        printf("AMP command succeeded\n");
                      else
                        printf("AMP command failed!\n");
                    }
                  }
                }
              }
            }
          }
          else
          {
            fprintf(stderr, "StartPresentation() failed\n");
          }
        }
        else
        {
          fprintf(stderr, "Could not issue StartPresentation()\n");
        }
      }
      else
      {
        fprintf(stderr, "OpenMedia() failed\n");
      }

_close_and_exit:
      pAmp->CloseMedia(pAmp);
    }
    else
    {
      fprintf(stderr, "Could not issue OpenMedia()\n");
    }

_exit:
    if (keybuffer) keybuffer->Release(keybuffer);
    if (pAmpEvent) pAmpEvent->Release(pAmpEvent);
    pAmp->Release(pAmp);
  }

  return 0;
}
