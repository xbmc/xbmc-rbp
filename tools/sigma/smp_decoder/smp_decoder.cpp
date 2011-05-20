/*
 *      Copyright (C) 2011 Team XBMC
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

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <string>
#include <fcntl.h>

#include <pthread.h>

#include <directfb.h>
#include <advancedmediaprovider.h>
#include <cdefs_lpb.h>

#include "CFileIDataSource.h"

union UMSStatus
{
  struct SStatus      generic;
  struct SLPBStatus   lpb;
};
union UMSCommand
{
  struct SCommand     generic;
  struct SLPBCommand  lpb;
};
union UMSResult
{
  struct SResult      generic;
  struct SLPBResult   lpb;
};

struct SIdsData
{
	CFileIDataSource *src;
	void *ch;
};

static const char *videoMediaTypes[] =
{
  "",
  "MPEG1",
  "MPEG2",
  "MPEG4",
  "AVC",
  "VC1",
  "DIVX3",
  "DIVX4",
  "WMV",
};
// AppContext - Application state
typedef struct
{
  char                      url[2048];
  char                      curl[2048];
  IDirectFBDataBuffer       *cDataBuffer;
  struct SIdsData           ids;
  struct SMediaFormat       format;
} AppContext;

void dump_stream_info(IAdvancedMediaProvider *pAmp, UMSStatus *status)
{
/*
  SLPBCommand cmd;
  cmd.cmd = LPBCmd_GET_STREAM_SET_INFO;
  cmd.param1.streamSetIndex = 0;
  cmd.dataSize = sizeof(cmd);
  cmd.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;
  
  SLPBResult  res;
  res.dataSize = sizeof(res);
  res.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;
  if (pAmp->ExecutePresentationCmd(pAmp, (SCommand*)&cmd, (SResult*)&res) == DFB_OK)
    fprintf(stderr, "CSMPPlayer::Pause:AMP command succeeded\n");
  else
    fprintf(stderr, "CSMPPlayer::Pause:AMP command failed!\n");

  fprintf(stderr,
    "Type: 0x%x \n"
    "Duration: %d \n"
    "Rate: %ld/%ld \n"
    "Streams: %d \n"
    "Videos: %d \n"
    "Audios: %d \n"
    "Subtitles: %d \n",
    res.value.media.format.mediaType,
    res.value.media.duration,
    res.value.media.clockTickM,
    res.value.media.clockTickN,
    res.value.media.streams,
    res.value.media.video_streams,
    res.value.media.audio_streams,
    res.value.media.subtitle_streams);
*/
  for (size_t i=0; i<sizeof(status->generic.mediaInfo)/sizeof(status->generic.mediaInfo[0]); i++)
  {
    fprintf(stderr, "dump, i(%d), status->mediaInfo[i].mediaType(0x%08lx)\n",
      i, (long unsigned int)status->generic.mediaInfo[i].mediaType);
    //if (MTYPE_ELEM_IS_VIDEO(status->mediaInfo[i].mediaType))
    {
      fprintf(stderr,
          "Video: %s %dx%d @ %2.2f%c AR: %d:%d ",
          GET_VIDEO_MINDEX(status->generic.mediaInfo[i].mediaType) < sizeof(videoMediaTypes)/sizeof(videoMediaTypes[0])
              ? videoMediaTypes[GET_VIDEO_MINDEX(status->generic.mediaInfo[i].mediaType)]
              : "unknown",
          status->generic.mediaInfo[i].format.image.width,
          status->generic.mediaInfo[i].format.image.height,
          (float)status->generic.mediaInfo[i].format.image.rateM/status->generic.mediaInfo[i].format.image.rateN,
          status->generic.mediaInfo[i].format.image.interlaced ? 'i' : 'p',
          status->generic.mediaInfo[i].format.image.aspectX, status->generic.mediaInfo[i].format.image.aspectY);
    }
  }
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
  // initialize our app contex.
  AppContext ctx;
  memset(&ctx, 0, sizeof(ctx));

	IDirectFB *dfb = NULL;
	IAdvancedMediaProvider *pAmp = NULL;

  strcpy(ctx.url, input_filename.c_str());

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

    // default to autodetection
    ctx.format.mediaType = MTYPE_APP_UNKNOWN;
    // setup IDataSource cookie
    ctx.ids.src = new CFileIDataSource(input_filename.c_str());
		fprintf(stderr, "Using IDataSource: 0x%08lx\n", (long unsigned int)ctx.ids.src);
    snprintf(ctx.url, sizeof(ctx.url)/sizeof(char), "ids://0x%08lx", (long unsigned int)&ctx.ids);

    // open the media
    if (pAmp->OpenMedia(pAmp, ctx.url, &ctx.format, NULL) == DFB_OK)
    {
      UMSStatus   status;

      memset(&status, 0, sizeof(status));
      status.generic.size = sizeof(status);
      status.generic.mediaSpace = MEDIA_SPACE_UNKNOWN;

      // wait and check the confirmation event
      if ((pAmpEvent->WaitForEventWithTimeout(pAmpEvent, 1000, 0) == DFB_OK) &&
          (pAmp->UploadStatusChanges(pAmp, (SStatus*)&status, DFB_TRUE) == DFB_OK) &&
          (status.generic.flags & SSTATUS_COMMAND) && IS_SUCCESS(status.generic.lastCmd.result)) // succeeded
      {
				DFBEvent event;

				pAmpEvent->GetEvent(pAmpEvent, &event);
        
        
        memset(&status, 0, sizeof(status));
        status.generic.size = sizeof(status);
        status.generic.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;
        pAmp->UploadStatusChanges(pAmp, (SStatus*)&status, DFB_TRUE);
        dump_stream_info(pAmp, &status);

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
				res = pAmp->StartPresentation(pAmp, DFB_TRUE);
        if (res == DFB_OK)
				{
          bool paused = false;
          memset(&status, 0, sizeof(status));
          status.generic.size = sizeof(status);
          status.generic.mediaSpace = MEDIA_SPACE_UNKNOWN;

          sleep(2);
					// wait and check the confirmation event
					//if ((pAmpEvent->WaitForEventWithTimeout(pAmpEvent, 1000, 0) == DFB_OK) &&
					if ((pAmpEvent->WaitForEvent(pAmpEvent) == DFB_OK) &&
						(pAmp->UploadStatusChanges(pAmp, (SStatus*)&status, DFB_TRUE) == DFB_OK) &&
						(status.generic.flags & SSTATUS_MODE) && (status.generic.mode.flags & SSTATUS_MODE_PLAYING))
					{
						DFBEvent keyEvent;
						struct SLPBCommand cmd;
						bool bExit = false;
            
            // consume the event
						pAmpEvent->GetEvent(pAmpEvent, &event);

            memset(&status, 0, sizeof(status));
            status.generic.size = sizeof(status);
            status.generic.mediaSpace = MEDIA_SPACE_LINEAR_MEDIA;
            pAmp->UploadStatusChanges(pAmp, (SStatus*)&status, DFB_TRUE);
            dump_stream_info(pAmp, &status);

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

                memset(&status, 0, sizeof(status));
                status.generic.size = sizeof(status);
                status.generic.mediaSpace = MEDIA_SPACE_UNKNOWN;

								if (pAmp->UploadStatusChanges(pAmp, (SStatus*)&status, DFB_TRUE) == DFB_OK)
								{
                  dump_stream_info(pAmp, &status);

									if ((status.generic.flags & SSTATUS_MODE) &&
										(status.generic.mode.flags & SSTATUS_MODE_STOPPED) &&
										(cmd.cmd != LPBCmd_STOP))
									{
                    fprintf(stderr, "presentation has stopped, leaving runloop\n");
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
												cmd.param2.speed = 2*1024;	// 2 x normal
												break;

											case DIKS_REWIND:
												printf("Issuing FAST REWIND command...\n");
												cmd.cmd = LPBCmd_SCAN_BACKWARD;
												cmd.param2.speed = 2*1024;	// 2 x normal
												break;

											case DIKS_SLOW:
												printf("Issuing SLOW FORWARD command...\n");
												cmd.cmd = LPBCmd_SCAN_FORWARD;
												cmd.param2.speed = 1024/2;	// 1/2 x normal
												break;

											case DIKS_PLAY:
												printf("Issuing PLAY command to resume normal playback\n");
                        if (paused)
                          cmd.cmd = LPBCmd_PAUSE_OFF;
                        else
                          cmd.cmd = LPBCmd_PLAY;
												break;

											case DIKS_STOP:
												printf("Issuing STOP command...\n");
												cmd.cmd = LPBCmd_STOP; // back to regular playback
												cmd.param1.stopMode = SM_LAST_FRAME;
												break;

											case DIKS_PAUSE:
												printf("Issuing Pause command...\n");
                        paused = true;
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
    dfb->Release(dfb);
	}

  return 0;
}
