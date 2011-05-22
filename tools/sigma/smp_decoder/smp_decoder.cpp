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

struct SIdsData
{
	CFileIDataSource *src;
	void *ch;
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
    //DFBDataBufferDescription desc = { DBDESC_FILE , ctx.url,{ NULL, 0}};


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
    //dfb->CreateDataBuffer(dfb, &desc, &ctx.cDataBuffer );

    ctx.ids.src = new CFileIDataSource();
		fprintf(stderr, "Using IDataSource: 0x%08lx\n", (long unsigned int)ctx.ids.src);
    snprintf(ctx.url, sizeof(ctx.url)/sizeof(char), "ids://0x%08lx", (long unsigned int)&ctx.ids);

    // open the media
    if (pAmp->OpenMedia(pAmp, ctx.url, &ctx.format, NULL) == DFB_OK)
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
				res = pAmp->StartPresentation(pAmp, DFB_TRUE);
        if (res == DFB_OK)
				{
					((struct SStatus *)buffer)->size = sizeof(buffer);
					((struct SStatus *)buffer)->mediaSpace = MEDIA_SPACE_UNKNOWN;

          sleep(2);
					// wait and check the confirmation event
					//if ((pAmpEvent->WaitForEventWithTimeout(pAmpEvent, 1000, 0) == DFB_OK) &&
					if ((pAmpEvent->WaitForEvent(pAmpEvent) == DFB_OK) &&
						(pAmp->UploadStatusChanges(pAmp, (struct SStatus *)buffer, DFB_TRUE) == DFB_OK) &&
						(((struct SStatus *)buffer)->flags & SSTATUS_MODE) &&
						(((struct SStatus *)buffer)->mode.flags & SSTATUS_MODE_PLAYING))
					{
						DFBEvent keyEvent;
						struct SLPBCommand cmd;
						bool bExit = false;
            
            // consume the event
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
                    fprintf(stderr, "presentation has stopped spontaneously, exit the program\n");
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
												cmd.param2.speed = 5*1024;	// 5 x FFWD
												break;

											case DIKS_SLOW:
												printf("Issuing SLOW FORWARD command...\n");
												cmd.cmd = LPBCmd_SCAN_FORWARD;
												cmd.param2.speed = 1024/2;	// 1/2 x SFWD
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
