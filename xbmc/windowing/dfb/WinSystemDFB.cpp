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
#include "system.h"

#ifdef HAS_DIRECTFB

#include "WinSystemDFB.h"
#include "utils/log.h"
#include "filesystem/SpecialProtocol.h"
#include "settings/Settings.h"
#include "guilib/Texture.h"
#include "windowing/dfb/WinBindingEGL.h"

#include <vector>
#include <directfb/directfb.h>

#define VERBOSE_LOGGING 1

CWinSystemDFB::CWinSystemDFB() : CWinSystemBase()
{
  m_dfb = NULL;
  m_dfb_screen  = NULL;
  m_dfb_layer   = NULL;
  m_dfb_surface = NULL;
  m_dfb_image_provider = NULL;

  m_buffermode  = DLBM_FRONTONLY;     // no backbuffer ( tearing unless we WaitForSync)
  //m_buffermode  = DLBM_BACKVIDEO;   // backbuffer in video memory (no tearing but gui fps is slower)
  m_eWindowSystem = WINDOW_SYSTEM_DFB;
  m_eglBinding = new CWinBindingEGL();
}

CWinSystemDFB::~CWinSystemDFB()
{
  DestroyWindowSystem();
  delete m_eglBinding;
}

bool CWinSystemDFB::InitWindowSystem()
{
  int width, height;
  DFBDisplayLayerConfig dlcfg;

  DirectFBInit(NULL, NULL);
  DirectFBCreate(&m_dfb);
  m_dfb->SetCooperativeLevel(m_dfb, DFSCL_NORMAL);

  m_dfb->GetScreen(m_dfb, DSCID_PRIMARY, &m_dfb_screen);

#if VERBOSE_LOGGING > 0
  // get and dump the screen descriptor
 	DFBScreenDescription screen_desc;
  screen_desc.caps = DSCCAPS_ALL;
  m_dfb_screen->GetDescription(m_dfb_screen, &screen_desc);
  CLog::Log(LOGDEBUG, "screen_desc caps(0x%x), name(%s), mixers(%d), encoders(%d), outputs(%d)",
    screen_desc.caps, screen_desc.name, screen_desc.mixers, screen_desc.encoders, screen_desc.outputs);

  // get and dump the mixer descriptors
  DFBScreenMixerDescription mixer_descs[screen_desc.mixers];
  m_dfb_screen->GetMixerDescriptions(m_dfb_screen, mixer_descs);
	for (int i = 0; i < screen_desc.mixers; ++i)
	{
    CLog::Log(LOGDEBUG, "i(%d), mixer_descs caps(0x%x), layers(0x%x), "
      "sub_num(0x%x), sub_layers(0x%x), name(%s)",
      i, mixer_descs[i].caps, mixer_descs[i].layers,
      mixer_descs[i].sub_num, mixer_descs[i].sub_layers, mixer_descs[i].name);
  }

  // get and dump the encoder descriptors
  DFBScreenEncoderConfig encoder_cfg;
  DFBScreenOutputSignals out_signal;
	for (int i = 0; i < screen_desc.encoders; ++i)
	{
    m_dfb_screen->GetEncoderConfiguration(m_dfb_screen, i, &encoder_cfg);
    CLog::Log(LOGDEBUG, "i(%d), enc_cfg flags(0x%x), tv_standard(0x%x), out_signals(0x%x)",
      i, encoder_cfg.flags, encoder_cfg.tv_standard, encoder_cfg.out_signals);
		if (encoder_cfg.out_signals & ~DSOS_EDID)
		{
			out_signal = (DFBScreenOutputSignals)(encoder_cfg.out_signals & ~DSOS_EDID);
			//break;
		}
	}

  // get and dump the screen descriptors
  DFBScreenOutputDescription screen_descs[screen_desc.outputs];
  m_dfb_screen->GetOutputDescriptions(m_dfb_screen, screen_descs);
	for (int i = 0; i < screen_desc.outputs; ++i)
	{
    CLog::Log(LOGDEBUG, "i(%d), screen_descs caps(0x%x), all_connectors(0x%x), all_signals(0x%x), name(%s)",
      i, screen_descs[i].caps, screen_descs[i].all_connectors, screen_descs[i].all_signals, screen_descs[i].name);
  }

  DFBScreenOutputConfig output_cfg;
  output_cfg.flags = DSOCONF_ALL;
  m_dfb_screen->GetOutputConfiguration(m_dfb_screen, 0, &output_cfg);
  CLog::Log(LOGDEBUG,
      "output_cfg flags(0x%x), encoder(0x%x), "
      "out_signals(0x%x), out_connectors(0x%x), slow_blanking(0x%x)",
      output_cfg.flags, output_cfg.encoder,
      output_cfg.out_signals, output_cfg.out_connectors, output_cfg.slow_blanking);

  m_dfb->GetDisplayLayer(m_dfb, DLID_PRIMARY, &m_dfb_layer);
  m_dfb_layer->SetCooperativeLevel(m_dfb_layer, DLSCL_ADMINISTRATIVE);
  m_dfb_layer->GetConfiguration(m_dfb_layer, &dlcfg);
  CLog::Log(LOGDEBUG, "CWinSystemDFB::InitWindowSystem: DisplayLayer width(%d), height(%d)",
    dlcfg.width, dlcfg.height);

  dlcfg.flags       = (DFBDisplayLayerConfigFlags)(DLCONF_BUFFERMODE | DLCONF_PIXELFORMAT);
  dlcfg.buffermode  = (DFBDisplayLayerBufferMode)m_buffermode;     
  dlcfg.pixelformat = DSPF_ARGB;
  m_dfb_layer->SetConfiguration(m_dfb_layer, &dlcfg);

  m_dfb_layer->GetSurface(m_dfb_layer, &m_dfb_surface);
  m_dfb_surface->GetSize(m_dfb_surface, &width, &height);
  CLog::Log(LOGDEBUG, "CWinSystemDFB::InitWindowSystem: Surface      width(%d), height(%d)",
    width, height);

  // based on sigma's directfb definitions. beware, others might be different.
  enum {
    XBMC_OSD = DSCID_PRIMARY,
    XBMC_BKGND,
    XBMC_MAINVIDEO,
    XBMC_SECVIDEO,
    XBMC_SECOSD
  };
  IDirectFBScreen *screen = NULL;
  if (m_dfb->GetScreen(m_dfb, DSCID_PRIMARY, &screen) == DFB_OK)
  {
    DFBScreenMixerConfig mixcfg;
    screen->GetMixerConfiguration(screen, 0, &mixcfg);
    // dump the mixer config
    for (int n = 0; n < DFB_DISPLAYLAYER_IDS_MAX; n++)
    {
      if (DFB_DISPLAYLAYER_IDS_HAVE( mixcfg.layers, n))
      {
        switch(n)
        {
          case XBMC_OSD:
            CLog::Log(LOGDEBUG, "CWinSystemDFB::InitWindowSystem: (%02x) OSD layer (graphic)", n);
          break;
          case XBMC_BKGND:
            CLog::Log(LOGDEBUG, "CWinSystemDFB::InitWindowSystem: (%02x) Background layer (graphic)", n);
            if (mixcfg.flags & DSMCONF_BACKGROUND)
            {
              CLog::Log(LOGDEBUG, "CWinSystemDFB::InitWindowSystem: (%02x) Background layer ARGB "
                "0x%02x, 0x%02x, 0x%02x, 0x%02x",
                n, mixcfg.background.a, mixcfg.background.r, mixcfg.background.g, mixcfg.background.b );
            }
          break;
          case XBMC_MAINVIDEO:
            CLog::Log(LOGDEBUG, "CWinSystemDFB::InitWindowSystem: (%02x) Main video layer", n);
          break;
          case XBMC_SECVIDEO:
            CLog::Log(LOGDEBUG, "CWinSystemDFB::InitWindowSystem: (%02x) Secondary video layer (video or graphic)", n);
          break;
          case XBMC_SECOSD:
            CLog::Log(LOGDEBUG, "CWinSystemDFB::InitWindowSystem: (%02x) Secondary OSD layer (graphic)", n);
          break;
        }
      }
    }
  }
#endif

  if (!CWinSystemBase::InitWindowSystem())
    return false;

  return true;
}

bool CWinSystemDFB::DestroyWindowSystem()
{
  if (m_dfb_image_provider)
    m_dfb_image_provider->Release(m_dfb_image_provider);
  m_dfb_image_provider = NULL;
    
  if (m_dfb_surface)
    m_dfb_surface->Release(m_dfb_surface);
  m_dfb_surface = NULL;
  if (m_dfb_layer)
    m_dfb_layer->Release(m_dfb_layer);
  m_dfb_layer  = NULL;

  if (m_dfb_screen)
    m_dfb_screen->Release(m_dfb_screen);
  m_dfb_screen = NULL;

  if (m_dfb)
    m_dfb->Release(m_dfb);
  m_dfb = NULL;

  return true;
}

bool CWinSystemDFB::CreateNewWindow(const CStdString& name, bool fullScreen, RESOLUTION_INFO& res, PHANDLE_EVENT_FUNC userFunction)
{
  m_nWidth  = res.iWidth;
  m_nHeight = res.iHeight;
  m_bFullScreen = fullScreen;

  if (!m_eglBinding->CreateWindow((EGLNativeDisplayType)m_dfb, (NativeWindowType)m_dfb_surface))
    return false;

  m_bWindowCreated = true;

  return true;
}

bool CWinSystemDFB::DestroyWindow()
{
  if (!m_eglBinding->DestroyWindow())
    return false;

  m_bWindowCreated = false;

  return true;
}

bool CWinSystemDFB::ResizeWindow(int newWidth, int newHeight, int newLeft, int newTop)
{
  CRenderSystemGLES::ResetRenderSystem(newWidth, newHeight, true, 0);
  return true;
}

bool CWinSystemDFB::SetFullScreen(bool fullScreen, RESOLUTION_INFO& res, bool blankOtherDisplays)
{
  CLog::Log(LOGDEBUG, "CWinSystemDFB::SetFullScreen");
  m_nWidth  = res.iWidth;
  m_nHeight = res.iHeight;
  m_bFullScreen = fullScreen;

  m_eglBinding->ReleaseSurface();
  CreateNewWindow("", fullScreen, res, NULL);

  CRenderSystemGLES::ResetRenderSystem(res.iWidth, res.iHeight, true, 0);

  return true;
}

void CWinSystemDFB::UpdateResolutions()
{
  CWinSystemBase::UpdateResolutions();

  int w = 1280;
  int h = 720;
  UpdateDesktopResolution(g_settings.m_ResInfo[RES_DESKTOP], 0, w, h, 0.0);
}

bool CWinSystemDFB::IsExtSupported(const char* extension)
{
  if(strncmp(extension, "EGL_", 4) != 0)
    return CRenderSystemGLES::IsExtSupported(extension);

  return m_eglBinding->IsExtSupported(extension);
}

bool CWinSystemDFB::PresentRenderImpl()
{
  // if we are not running a backbuffer,
  // then we have to handle the vsync ourselfs
  if (m_iVSyncMode && (m_buffermode == DLBM_FRONTONLY))
    m_dfb_layer->WaitForSync(m_dfb_layer);

  m_eglBinding->SwapBuffers();

  return true;
}

void CWinSystemDFB::SetVSyncImpl(bool enable)
{
  m_iVSyncMode = enable ? 10 : 0;
  if (m_eglBinding->SetVSync(enable) == DFB_FALSE)
  {
    // SetVSync will fail if we are DLBM_FRONTONLY
    if (m_buffermode != DLBM_FRONTONLY)
      CLog::Log(LOGERROR, "CWinSystemDFB::SetVSyncImpl: Could not set egl vsync");
  }
}

void CWinSystemDFB::ShowOSMouse(bool show)
{
  //m_dfb_layer->EnableCursor(m_dfb_layer, show ? 1 : 0);
}

void CWinSystemDFB::NotifyAppActiveChange(bool bActivated)
{
}

bool CWinSystemDFB::Minimize()
{
  Hide();
  return true;
}

bool CWinSystemDFB::Restore()
{
  Show(true);
  return false;
}

bool CWinSystemDFB::Hide()
{
  DFBScreenMixerConfig mixcfg;
  m_dfb_screen->GetMixerConfiguration(m_dfb_screen, 0, &mixcfg);
  mixcfg.flags = (DFBScreenMixerConfigFlags)(DSMCONF_LAYERS);

  if (DFB_DISPLAYLAYER_IDS_HAVE( mixcfg.layers, DLID_PRIMARY))
    DFB_DISPLAYLAYER_IDS_REMOVE(mixcfg.layers, DLID_PRIMARY);
  
  m_dfb_screen->SetMixerConfiguration(m_dfb_screen, 0, &mixcfg);

  return true;
}

bool CWinSystemDFB::Show(bool raise)
{
  DFBScreenMixerConfig mixcfg;
  m_dfb_screen->GetMixerConfiguration(m_dfb_screen, 0, &mixcfg);
  mixcfg.flags = (DFBScreenMixerConfigFlags)(DSMCONF_LAYERS);

  if (!DFB_DISPLAYLAYER_IDS_HAVE( mixcfg.layers, DLID_PRIMARY))
    DFB_DISPLAYLAYER_IDS_ADD(mixcfg.layers, DLID_PRIMARY);
  
  m_dfb_screen->SetMixerConfiguration(m_dfb_screen, 0, &mixcfg);

  return true;
}

IDirectFB* CWinSystemDFB::GetIDirectFB() const
{
  return m_dfb;
}

bool CWinSystemDFB::CreateImageProvider(IDirectFBDataBuffer *buffer, IDirectFBImageProvider **provider, bool retain1st)
{
  // some hardware accelerated dfb image provider implementations have
  // lengthy setup overhead and we can avoid this by retaining the 1st
  // created image provider.
  DFBResult err;
  err = buffer->CreateImageProvider(buffer, provider);
  if (retain1st && !m_dfb_image_provider)
  {
    // get the surface description, to check its size
    DFBSurfaceDescription dsc;
    memset(&dsc, 0, sizeof(dsc));

    // set the flags to indicate what we want to get back
    dsc.flags = (DFBSurfaceDescriptionFlags)(DSDESC_WIDTH|DSDESC_HEIGHT);
    (*provider)->GetSurfaceDescription(*provider, &dsc);
    if ((dsc.width >= 512) && (dsc.height >= 512))
      m_dfb_image_provider = *provider;
  }
    
  return(err == DFB_OK);
}

bool CWinSystemDFB::ReleaseImageProvider(IDirectFBImageProvider *provider)
{
  // if we have not cached the initial image provider, retain it now.
  // the initial image provider will get released when we shutdown.
  DFBResult err = DFB_OK;
  if (provider && (m_dfb_image_provider != provider))
    err = provider->Release(provider);

  return(err == DFB_OK);
}

#endif
