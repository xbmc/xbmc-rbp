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

#include "Texture.h"
#include "windowing/WindowingFactory.h"
#include "utils/log.h"
#include "utils/URIUtils.h"
#include "pictures/DllImageLib.h"
#include "DDSImage.h"
#include "filesystem/SpecialProtocol.h"
#if defined(__APPLE__) && defined(__arm__)
#include <ImageIO/ImageIO.h>
#include "filesystem/File.h"
#include "osx/DarwinUtils.h"
#endif
#if defined(HAS_DIRECTFB)
#include "filesystem/File.h"
#include "threads/SingleLock.h"
#include "threads/CriticalSection.h"
#include <directfb.h>
#endif

#if defined(HAS_DIRECTFB)
// we need this to serialize access to hw image decoder.
static CCriticalSection gHWLoaderSection;
#endif

/************************************************************************/
/*                                                                      */
/************************************************************************/
CBaseTexture::CBaseTexture(unsigned int width, unsigned int height, unsigned int format)
 : m_hasAlpha( true )
{
#ifndef HAS_DX 
  m_texture = 0; 
#endif
  m_pixels = NULL;
#if defined(HAS_DIRECTFB)
  m_dfbSurface = NULL;
#endif
  m_loadedToGPU = false;
  Allocate(width, height, format);
}

CBaseTexture::~CBaseTexture()
{
#if defined(HAS_DIRECTFB)
  if (m_dfbSurface && (m_format & XB_FMT_DFBSURFACE))
  {
    m_dfbSurface->Release(m_dfbSurface);
    m_dfbSurface = NULL;
  }
  else
#endif
  {
    delete[] m_pixels;
    m_pixels = NULL;
  }
}

void CBaseTexture::Allocate(unsigned int width, unsigned int height, unsigned int format)
{
  m_imageWidth = width;
  m_imageHeight = height;
  m_format = format;
  m_orientation = 0;

  m_textureWidth = m_imageWidth;
  m_textureHeight = m_imageHeight;

  if (m_format & XB_FMT_DXT_MASK)
    while (GetPitch() < g_Windowing.GetMinDXTPitch())
      m_textureWidth += GetBlockSize();

  if (!g_Windowing.SupportsNPOT((m_format & XB_FMT_DXT_MASK) != 0))
  {
    m_textureWidth = PadPow2(m_textureWidth);
    m_textureHeight = PadPow2(m_textureHeight);
  }
  if (m_format & XB_FMT_DXT_MASK)
  { // DXT textures must be a multiple of 4 in width and height
    m_textureWidth = ((m_textureWidth + 3) / 4) * 4;
    m_textureHeight = ((m_textureHeight + 3) / 4) * 4;
  }

  // check for max texture size
  #define CLAMP(x, y) { if (x > y) x = y; }
  CLAMP(m_textureWidth, g_Windowing.GetMaxTextureSize());
  CLAMP(m_textureHeight, g_Windowing.GetMaxTextureSize());
  CLAMP(m_imageWidth, m_textureWidth);
  CLAMP(m_imageHeight, m_textureHeight);

#if defined(HAS_DIRECTFB)
  if (!(m_format & XB_FMT_DFBSURFACE))
#endif
  {
    delete[] m_pixels;
    m_pixels = new unsigned char[GetPitch() * GetRows()];
  }
}

void CBaseTexture::Update(unsigned int width, unsigned int height, unsigned int pitch, unsigned int format, const unsigned char *pixels, bool loadToGPU)
{
  if (pixels == NULL)
    return;

  if (format & XB_FMT_DXT_MASK && !g_Windowing.SupportsDXT())
  { // compressed format that we don't support
    Allocate(width, height, XB_FMT_A8R8G8B8);
    CDDSImage::Decompress(m_pixels, std::min(width, m_textureWidth), std::min(height, m_textureHeight), GetPitch(m_textureWidth), pixels, format);
  }
#if defined(HAS_DIRECTFB)
  else if (m_format & XB_FMT_DFBSURFACE)
  {
    CLog::Log(LOGERROR, "CBaseTexture::Update, should not be here, format is XB_FMT_DFBSURFACE");
    Allocate(width, height, format);
  }
#endif
  else
  {
    Allocate(width, height, format);

    unsigned int srcPitch = pitch ? pitch : GetPitch(width);
    unsigned int srcRows = GetRows(height);
    unsigned int dstPitch = GetPitch(m_textureWidth);
    unsigned int dstRows = GetRows(m_textureHeight);

    if (srcPitch == dstPitch)
      memcpy(m_pixels, pixels, srcPitch * std::min(srcRows, dstRows));
    else
    {
      const unsigned char *src = pixels;
      unsigned char* dst = m_pixels;
      for (unsigned int y = 0; y < srcRows && y < dstRows; y++)
      {
        memcpy(dst, src, std::min(srcPitch, dstPitch));
        src += srcPitch;
        dst += dstPitch;
      }
    }
  }
  ClampToEdge();

  if (loadToGPU)
    LoadToGPU();
}

void CBaseTexture::ClampToEdge()
{
  if (!(m_format & XB_FMT_DFBSURFACE))
  {
    unsigned int imagePitch = GetPitch(m_imageWidth);
    unsigned int imageRows = GetRows(m_imageHeight);
    unsigned int texturePitch = GetPitch(m_textureWidth);
    unsigned int textureRows = GetRows(m_textureHeight);
    if (imagePitch < texturePitch)
    {
      unsigned int blockSize = GetBlockSize();
      unsigned char *src = m_pixels + imagePitch - blockSize;
      unsigned char *dst = m_pixels;
      for (unsigned int y = 0; y < imageRows; y++)
      {
        for (unsigned int x = imagePitch; x < texturePitch; x += blockSize)
          memcpy(dst + x, src, blockSize);
        dst += texturePitch;
      }
    }

    if (imageRows < textureRows)
    {
      unsigned char *dst = m_pixels + imageRows * texturePitch;
      for (unsigned int y = imageRows; y < textureRows; y++)
      {
        memcpy(dst, dst - texturePitch, texturePitch);
        dst += texturePitch;
      }
    }
  }
}

bool CBaseTexture::LoadFromFile(const CStdString& texturePath, unsigned int maxWidth, unsigned int maxHeight,
                                bool autoRotate, unsigned int *originalWidth, unsigned int *originalHeight)
{
  if (URIUtils::GetExtension(texturePath).Equals(".dds"))
  { // special case for DDS images
    CDDSImage image;
    if (image.ReadFile(texturePath))
    {
      Update(image.GetWidth(), image.GetHeight(), 0, image.GetFormat(), image.GetData(), false);
      return true;
    }
    return false;
  }

  if (URIUtils::GetExtension(texturePath).Equals(".jpg") || URIUtils::GetExtension(texturePath).Equals(".tbn"))
  {
    if (LoadHWAccelerated(texturePath))
      return true;
  }

#if defined(__APPLE__) && defined(__arm__)
  XFILE::CFile file;
  UInt8 *imageBuff      = NULL;
  int64_t imageBuffSize = 0;

  //open path and read data to buffer
  //this handles advancedsettings.xml pathsubstitution
  //and resulting networking
  if (file.Open(texturePath, 0))
  {
    imageBuffSize =file.GetLength();
    imageBuff = new UInt8[imageBuffSize];
    imageBuffSize = file.Read(imageBuff, imageBuffSize);
    file.Close();
  }
  else
  {
    CLog::Log(LOGERROR, "Texture manager unable to open file %s", texturePath.c_str());
    return false;
  }

  if (imageBuffSize <= 0)
  {
    CLog::Log(LOGERROR, "Texture manager read texture file failed.");
    delete [] imageBuff;
    return false;
  }

  // create the image from buffer;
  CGImageSourceRef imageSource;
  // create a CFDataRef using CFDataCreateWithBytesNoCopy and kCFAllocatorNull for deallocator.
  // this allows us to do a nocopy reference and we handle the free of imageBuff
  CFDataRef cfdata = CFDataCreateWithBytesNoCopy(NULL, imageBuff, imageBuffSize, kCFAllocatorNull);
  imageSource = CGImageSourceCreateWithData(cfdata, NULL);   
    
  if (imageSource == nil)
  {
    CLog::Log(LOGERROR, "Texture manager unable to load file: %s", CSpecialProtocol::TranslatePath(texturePath).c_str());
    CFRelease(cfdata);
    delete [] imageBuff;
    return false;
  }

  CGImageRef image = CGImageSourceCreateImageAtIndex(imageSource, 0, NULL);

  int rotate = 0;
  if (autoRotate)
  { // get the orientation of the image for displaying it correctly
    CFDictionaryRef imagePropertiesDictionary = CGImageSourceCopyPropertiesAtIndex(imageSource,0, NULL);
    if (imagePropertiesDictionary != nil)
    {
      CFNumberRef orientation = (CFNumberRef)CFDictionaryGetValue(imagePropertiesDictionary, kCGImagePropertyOrientation);
      if (orientation != nil)
      {
        int value = 0;
        CFNumberGetValue(orientation, kCFNumberIntType, &value);
        if (value)
          rotate = value - 1;
      }
      CFRelease(imagePropertiesDictionary);
    }
  }

  CFRelease(imageSource);

  unsigned int width  = CGImageGetWidth(image);
  unsigned int height = CGImageGetHeight(image);

  m_hasAlpha = (CGImageGetAlphaInfo(image) != kCGImageAlphaNone);

  if (originalWidth)
    *originalWidth = width;
  if (originalHeight)
    *originalHeight = height;

  // check texture size limits and limit to screen size - preserving aspectratio of image  
  if ( width > g_Windowing.GetMaxTextureSize() || height > g_Windowing.GetMaxTextureSize() )
  {
    float aspect;

    if ( width > height )
    {
      aspect = (float)width / (float)height;
      width  = g_Windowing.GetWidth();
      height = (float)width / (float)aspect;
    }
    else
    {
      aspect = (float)height / (float)width;
      height = g_Windowing.GetHeight();
      width  = (float)height / (float)aspect;
    }
    CLog::Log(LOGDEBUG, "Texture manager texture clamp:new texture size: %i x %i", width, height);
  }

  Allocate(width, height, XB_FMT_A8R8G8B8);
  m_orientation = rotate;
    
  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();

  CGContextRef context = CGBitmapContextCreate(m_pixels,
    width, height, 8, GetPitch(), colorSpace,
    kCGImageAlphaPremultipliedFirst | kCGBitmapByteOrder32Host);

  CGColorSpaceRelease(colorSpace);

  // Flip so that it isn't upside-down
  //CGContextTranslateCTM(context, 0, height);
  //CGContextScaleCTM(context, 1.0f, -1.0f);
  #if MAC_OS_X_VERSION_MAX_ALLOWED < MAC_OS_X_VERSION_10_5
    CGContextClearRect(context, CGRectMake(0, 0, width, height));
  #else
    #if MAC_OS_X_VERSION_MIN_REQUIRED < MAC_OS_X_VERSION_10_5
    // (just a way of checking whether we're running in 10.5 or later)
    if (CGContextDrawLinearGradient == 0)
      CGContextClearRect(context, CGRectMake(0, 0, width, height));
    else
    #endif
      CGContextSetBlendMode(context, kCGBlendModeCopy);
  #endif
  //CGContextSetBlendMode(context, kCGBlendModeCopy);
  CGContextDrawImage(context, CGRectMake(0, 0, width, height), image);
  CGContextRelease(context);
  CGImageRelease(image);
  CFRelease(cfdata);
  delete [] imageBuff;

#else
  DllImageLib dll;
  if (!dll.Load())
    return false;

  ImageInfo image;
  memset(&image, 0, sizeof(image));

  unsigned int width = maxWidth ? std::min(maxWidth, g_Windowing.GetMaxTextureSize()) : g_Windowing.GetMaxTextureSize();
  unsigned int height = maxHeight ? std::min(maxHeight, g_Windowing.GetMaxTextureSize()) : g_Windowing.GetMaxTextureSize();

  if(!dll.LoadImage(texturePath.c_str(), width, height, &image))
  {
    CLog::Log(LOGERROR, "Texture manager unable to load file: %s", texturePath.c_str());
    return false;
  }

  m_hasAlpha = NULL != image.alpha;

  Allocate(image.width, image.height, XB_FMT_A8R8G8B8);
  if (autoRotate && image.exifInfo.Orientation)
    m_orientation = image.exifInfo.Orientation - 1;
  if (originalWidth)
    *originalWidth = image.originalwidth;
  if (originalHeight)
    *originalHeight = image.originalheight;

  unsigned int dstPitch = GetPitch();
  unsigned int srcPitch = ((image.width + 1)* 3 / 4) * 4; // bitmap row length is aligned to 4 bytes

  unsigned char *dst = m_pixels;
  unsigned char *src = image.texture + (m_imageHeight - 1) * srcPitch;

  for (unsigned int y = 0; y < m_imageHeight; y++)
  {
    unsigned char *dst2 = dst;
    unsigned char *src2 = src;
    for (unsigned int x = 0; x < m_imageWidth; x++, dst2 += 4, src2 += 3)
    {
      dst2[0] = src2[0];
      dst2[1] = src2[1];
      dst2[2] = src2[2];
      dst2[3] = 0xff;
    }
    src -= srcPitch;
    dst += dstPitch;
  }

  if(image.alpha)
  {
    dst = m_pixels + 3;
    src = image.alpha + (m_imageHeight - 1) * m_imageWidth;

    for (unsigned int y = 0; y < m_imageHeight; y++)
    {
      unsigned char *dst2 = dst;
      unsigned char *src2 = src;

      for (unsigned int x = 0; x < m_imageWidth; x++,  dst2+=4, src2++)
        *dst2 = *src2;
      src -= m_imageWidth;
      dst += dstPitch;
    }
  }
  dll.ReleaseImage(&image);
#endif

  ClampToEdge();

  return true;
}

bool CBaseTexture::LoadFromMemory(unsigned int width, unsigned int height, unsigned int pitch, unsigned int format, unsigned char* pixels)
{
  m_imageWidth = width;
  m_imageHeight = height;
  m_format = format;
  Update(width, height, pitch, format, pixels, false);
  return true;
}

bool CBaseTexture::LoadPaletted(unsigned int width, unsigned int height, unsigned int pitch, unsigned int format, const unsigned char *pixels, const COLOR *palette)
{
  if (pixels == NULL || palette == NULL)
    return false;

  Allocate(width, height, format);

  for (unsigned int y = 0; y < m_imageHeight; y++)
  {
    unsigned char *dest = m_pixels + y * GetPitch();
    const unsigned char *src = pixels + y * pitch;
    for (unsigned int x = 0; x < m_imageWidth; x++)
    {
      COLOR col = palette[*src++];
      *dest++ = col.b;
      *dest++ = col.g;
      *dest++ = col.r;
      *dest++ = col.x;
    }
  }
  ClampToEdge();
  return true;
}

unsigned int CBaseTexture::PadPow2(unsigned int x)
{
  --x;
  x |= x >> 1;
  x |= x >> 2;
  x |= x >> 4;
  x |= x >> 8;
  x |= x >> 16;
  return ++x;
}

bool CBaseTexture::SwapBlueRed(unsigned char *pixels, unsigned int height, unsigned int pitch, unsigned int elements, unsigned int offset)
{
  if (!pixels) return false;
  unsigned char *dst = pixels;
  for (unsigned int y = 0; y < height; y++)
  {
    dst = pixels + (y * pitch);
    for (unsigned int x = 0; x < pitch; x+=elements)
      std::swap(dst[x+offset], dst[x+2+offset]);
  }
  return true;
}

unsigned int CBaseTexture::GetPitch(unsigned int width) const
{
  switch (m_format)
  {
  case XB_FMT_DXT1:
    return ((width + 3) / 4) * 8;
  case XB_FMT_DXT3:
  case XB_FMT_DXT5:
  case XB_FMT_DXT5_YCoCg:
    return ((width + 3) / 4) * 16;
  case XB_FMT_A8:
    return width;
  case XB_FMT_DFBSURFACE:
    return width*4;
  case XB_FMT_A8R8G8B8:
  default:
    return width*4;
  }
}

unsigned int CBaseTexture::GetRows(unsigned int height) const
{
  switch (m_format)
  {
  case XB_FMT_DXT1:
    return (height + 3) / 4;
  case XB_FMT_DXT3:
  case XB_FMT_DXT5:
  case XB_FMT_DXT5_YCoCg:
    return (height + 3) / 4;
  case XB_FMT_DFBSURFACE:
    return height;
  default:
    return height;
  }
}

unsigned int CBaseTexture::GetBlockSize() const
{
  switch (m_format)
  {
  case XB_FMT_DXT1:
    return 8;
  case XB_FMT_DXT3:
  case XB_FMT_DXT5:
  case XB_FMT_DXT5_YCoCg:
    return 16;
  case XB_FMT_A8:
    return 1;
  case XB_FMT_DFBSURFACE:
    return 4;
  default:
    return 4;
  }
}

bool CBaseTexture::HasAlpha() const
{
  return m_hasAlpha;
}

bool CBaseTexture::LoadHWAccelerated(const CStdString& texturePath)
{
#if defined (HAS_DIRECTFB)
  CSingleLock lock(gHWLoaderSection);

  if (!g_Windowing.IsCreated())
    return false;

  XFILE::CFile file;
  uint8_t *imageBuff = NULL;
  int64_t imageBuffSize = 0;

#if 1
  //open path and read data to buffer
  //this handles advancedsettings.xml pathsubstitution
  //and resulting networking
  if (file.Open(texturePath, 0))
  {
    int64_t imgsize;
    imgsize = file.GetLength();
    imageBuff = new uint8_t[imgsize];
    imageBuffSize = file.Read(imageBuff, imgsize);                                                           
    file.Close();

    if (imgsize != imageBuffSize)
      CLog::Log(LOGERROR,"CBaseTexture::LoadHWAccelerated:imgsize(%llu) != imageBuffSize(%llu)",
        imgsize, imageBuffSize);

    if (imageBuffSize <= 0)
    {
      delete [] imageBuff;
      return false;
    }
  }
  else
  {
    return false;
  }

  DFBResult err;
  IDirectFB *dfb = g_Windowing.GetIDirectFB();
  if (!dfb)
  {
    delete [] imageBuff;
    return false;
  }

  // create a directfb data buffer for memory
  DFBDataBufferDescription dbd ={ (DFBDataBufferDescriptionFlags)0, 0 };
  dbd.flags         = DBDESC_MEMORY;
  dbd.memory.data   = imageBuff;
  dbd.memory.length = imageBuffSize;
#else
  DFBResult err;
  IDirectFB *dfb = g_Windowing.GetIDirectFB();
  if (!dfb)
  {
    delete [] imageBuff;
    return false;
  }

  char image_filename[1024];
  DFBDataBufferDescription dbd = {DBDESC_FILE, image_filename};
  strcpy(image_filename, CSpecialProtocol::TranslatePath(texturePath).c_str());
#endif

  IDirectFBDataBuffer *buffer = NULL;
  err = dfb->CreateDataBuffer(dfb, &dbd, &buffer);
  if (err != DFB_OK)
  {
    CLog::Log(LOGERROR,"CBaseTexture::LoadHWAccelerated:dfb->CreateDataBuffer failed");
    delete [] imageBuff;
    return false;
  }

  IDirectFBImageProvider *provider = NULL;
  // use g_Windowing to create the image provider and retain the 1st
  // so we avoid recurring setup overhead. 
  if (!g_Windowing.CreateImageProvider(buffer, &provider, true))
  {
    CLog::Log(LOGERROR,"CBaseTexture::LoadHWAccelerated:buffer->CreateImageProvider failed");
    buffer->Release(buffer);
    delete [] imageBuff;
    return false;
  }

  // get the surface description, from the incoming compressed image.
  DFBSurfaceDescription dsc;
  memset(&dsc, 0, sizeof(dsc));

  // important to set what we want to get back in flags
  // before calling GetSurfaceDescription.
  dsc.flags = (DFBSurfaceDescriptionFlags)(DSDESC_CAPS|DSDESC_WIDTH|DSDESC_HEIGHT|DSDESC_PIXELFORMAT);
  provider->GetSurfaceDescription(provider, &dsc);

  // remember original jpeg/png width, height.
  unsigned int img_width  = dsc.width;
  unsigned int img_height = dsc.height;
  // check size limits and limit to screen size - preserving aspectratio of image  
  unsigned int maxSize = g_Windowing.GetMaxTextureSize();
  if ( img_width  > maxSize || img_height > maxSize )
  {
    float aspect;
    if (img_width > img_height)
    {
      aspect = (float)img_width / (float)img_height;
      img_width  = g_Windowing.GetWidth();
      img_height = (float)img_width / (float)aspect;
    }
    else
    {
      aspect = (float)img_height / (float)img_width;
      img_height = g_Windowing.GetHeight();
      img_width  = (float)img_height / (float)aspect;
    }
    CLog::Log(LOGDEBUG, "CBaseTexture::LoadHWAccelerated:clamping image size from %i x %i to %i x %i",
      dsc.width, dsc.height, img_height, img_height);
    dsc.width  = img_width;
    dsc.height = img_height;
  }
  if (!g_Windowing.SupportsNPOT(false))
  {
    dsc.width  = PadPow2(img_width);
    dsc.height = PadPow2(img_height);
  }
 
  // set caps to DSCAPS_VIDEOONLY so we get hw decode.
  dsc.caps  = (DFBSurfaceCapabilities)(dsc.caps | DSCAPS_VIDEOONLY);
  dsc.caps  = (DFBSurfaceCapabilities)(dsc.caps &~DSCAPS_SYSTEMONLY);
  dsc.pixelformat = DSPF_ARGB;

  // create the surface and render the compressed image to it.
  // once we render to it, we can release/delete most dfb objects.
  IDirectFBSurface *imagesurface = NULL;
  dfb->CreateSurface(dfb, &dsc, &imagesurface);
  if (!imagesurface)
  {
    // sometimes, during startup, we get a null surface back.
    CLog::Log(LOGERROR, "CBaseTexture::LoadHWAccelerated:dfb->CreateSurface failed");
    g_Windowing.ReleaseImageProvider(provider);
    buffer->Release(buffer);
    delete [] imageBuff;
    return false;
  }
  const DFBRectangle dstRect = {0, 0, img_width, img_height};
  provider->RenderTo(provider, imagesurface, &dstRect);
  // use g_Windowing to manage release
  g_Windowing.ReleaseImageProvider(provider);
  buffer->Release(buffer);
  delete [] imageBuff;

#if 1
  m_dfbSurface = imagesurface;
  Allocate(img_width, img_height, XB_FMT_DFBSURFACE);
  // correct our texture width to match the IDirectFBSurface pitch
  int imgpitch = 0;
  void *src = NULL;
  imagesurface->Lock(imagesurface, DSLF_READ , &src, &imgpitch);
  m_textureWidth = imgpitch / 4;
  imagesurface->Unlock(imagesurface);
#else
  Allocate(img_width, img_height, XB_FMT_A8R8G8B8);
  // lock the rendered surface, get a read pointer to it
  // and memcpy the contents into our m_pixels.
  // TODO: fixup memcpy to pay attention to imgpitch
  int imgpitch = 0;
  void *src = NULL;
  imagesurface->Lock(imagesurface, DSLF_READ , &src, &imgpitch);
  memcpy(m_pixels, src, GetPitch() * dsc.height);
  imagesurface->Unlock(imagesurface);
  imagesurface->Release(imagesurface);

  ClampToEdge();
#endif

  return true;
#else
  return false;
#endif
}


