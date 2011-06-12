#include <stdio.h>
#include <string.h>
#include <unistd.h>
   
#include <directfb.h>

#define DFBCHECK(x...)                                         \
  {                                                            \
    DFBResult err = x;                                         \
                                                               \
    if (err != DFB_OK)                                         \
      {                                                        \
        fprintf( stderr, "%s <%d>:\n\t", __FILE__, __LINE__ ); \
        DirectFBErrorFatal( #x, err );                         \
      }                                                        \
  }

int main (int argc, char **argv)
{
  IDirectFB *dfb;
  IDirectFBDisplayLayer *dfb_layer;
  DFBDisplayLayerConfig dlcfg;

  // setup directfb like we do in xbmc
  DFBCHECK(DirectFBInit(&argc, &argv));
  DFBCHECK(DirectFBCreate(&dfb));

  DFBCHECK(dfb->GetDisplayLayer(dfb, DLID_PRIMARY, &dfb_layer));
  DFBCHECK(dfb_layer->SetCooperativeLevel(dfb_layer, DLSCL_ADMINISTRATIVE));
  DFBCHECK(dfb_layer->GetConfiguration(dfb_layer, &dlcfg));
  fprintf( stderr, "DisplayLayer width(%d), height(%d)\n",
    dlcfg.width, dlcfg.height);

  dlcfg.flags       = (DFBDisplayLayerConfigFlags)(DLCONF_BUFFERMODE | DLCONF_PIXELFORMAT);
  dlcfg.options     = (DFBDisplayLayerOptions)(DLOP_OPACITY);
  dlcfg.buffermode  = (DFBDisplayLayerBufferMode)DLBM_FRONTONLY;     
  dlcfg.pixelformat = DSPF_ARGB;
  DFBCHECK(dfb_layer->SetConfiguration(dfb_layer, &dlcfg));


  // now render an jpeg image
  char image_filename[1024];
  DFBDataBufferDescription dbd = {DBDESC_FILE, image_filename};
  strcpy(image_filename, "/usr/share/xbmc/addons/skin.confluence/backgrounds/music.jpg");

  IDirectFBDataBuffer *buffer = NULL;
  DFBCHECK(dfb->CreateDataBuffer(dfb, &dbd, &buffer));

  IDirectFBImageProvider *provider = NULL;
  DFBCHECK(buffer->CreateImageProvider(buffer, &provider));

  // get the surface description, from the incoming compressed image.
  DFBSurfaceDescription dsc;
  memset(&dsc, 0, sizeof(dsc));
  DFBCHECK(provider->GetSurfaceDescription(provider, &dsc));

  // set caps to DSCAPS_VIDEOONLY so we get hw decode.
  dsc.flags = (DFBSurfaceDescriptionFlags)(DSDESC_CAPS|DSDESC_WIDTH|DSDESC_HEIGHT|DSDESC_PIXELFORMAT);
  dsc.caps  = (DFBSurfaceCapabilities)(dsc.caps | DSCAPS_VIDEOONLY);
  dsc.caps  = (DFBSurfaceCapabilities)(dsc.caps &~DSCAPS_SYSTEMONLY);
  dsc.pixelformat = DSPF_ARGB;

  // create the surface and render the compressed image to it.
  // once we render to it, we can release/delete most dfb objects.
  IDirectFBSurface *imagesurface = NULL;
  DFBCHECK(dfb->CreateSurface(dfb, &dsc, &imagesurface));
  if (!imagesurface)
  {
    // sometimes, during startup, we get a null surface back.
    fprintf( stderr, "CBaseTexture::LoadHWAccelerated:dfb->CreateSurface failed\n");
    provider->Release(provider);
    buffer->Release(buffer);
    return false;
  }
  DFBCHECK(provider->RenderTo(provider, imagesurface, NULL));
  // fails in provider->Release with *** glibc detected *** imagetest: double free or corruption (!prev): 0x0043dfb0 ***
  /* gdb bt
  #0  0x2adb88f4 in raise () from /home/davilla/xbmc-sigma/trunk/build/tmp/sysroots/mips-linux-gnu/lib/libc.so.6
  #1  0x2adbd824 in abort () from /home/davilla/xbmc-sigma/trunk/build/tmp/sysroots/mips-linux-gnu/lib/libc.so.6
  #2  0x2adf057c in __libc_message () from /home/davilla/xbmc-sigma/trunk/build/tmp/sysroots/mips-linux-gnu/lib/libc.so.6
  #3  0x2adfd1fc in malloc_printerr () from /home/davilla/xbmc-sigma/trunk/build/tmp/sysroots/mips-linux-gnu/lib/libc.so.6
  #4  0x2adff1a8 in free () from /home/davilla/xbmc-sigma/trunk/build/tmp/sysroots/mips-linux-gnu/lib/libc.so.6
  #5  0x2b0d9fa8 in ?? () from /home/davilla/xbmc-sigma/trunk/build/tmp/sysroots/mips-linux-gnu/usr/lib/libdirectfb-smp86xx.so
  #6  0x2b0da16c in ?? () from /home/davilla/xbmc-sigma/trunk/build/tmp/sysroots/mips-linux-gnu/usr/lib/libdirectfb-smp86xx.so
  #7  0x2b0da274 in ?? () from /home/davilla/xbmc-sigma/trunk/build/tmp/sysroots/mips-linux-gnu/usr/lib/libdirectfb-smp86xx.so
  #8  0x004011b0 in main (argc=1, argv=0x7ff40044) at imagetest.c:76
  */
  DFBCHECK(provider->Release(provider));
  DFBCHECK(buffer->Release(buffer));

  DFBCHECK(dfb->Release(dfb));

  return 0;
}
