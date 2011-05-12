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

#include "CFileIDataSource.h"
#include <unistd.h>
#include <string.h>
#include <fcntl.h>

//========================================================================
CFileIDataSource::CFileIDataSource(const char *url)
{
  strncpy(m_url, url, sizeof(m_url));
};

//========================================================================
CFileIDataSource::~CFileIDataSource()
{
};

//========================================================================
void* CFileIDataSource::GetFormatSpecificCPInterface()
{
  fprintf(stderr, "CFileIDataSource::GetFormatSpecificCPInterface\n");

  void *test = NULL;
  return test;
}

//========================================================================
void* CFileIDataSource::Open(char* url, enum EDSResult *pRes)
{
  // open the file, always in read mode
  int fp = open(m_url, O_RDONLY | O_LARGEFILE);
  fprintf(stderr, "CFileIDataSource::Open url(%s), filename(%s), fp(%d)\n",
    url, m_url, fp);

  if (pRes)
  {
    if (fp)
      *pRes = DSRES_OK;
    else
      *pRes = DSRES_SOURCE_MISSING;
  }

  return (void*)fp;
}

//========================================================================
bool CFileIDataSource::GetChannelParams(struct SChannelParams *pParams)
{
  fprintf(stderr, "CFileIDataSource::GetChannelParams\n");
  // we do not know the channel parameters,
  /*
  pParams->blockSize;     // amount of data currently available in the buffer
  pParams->bitrate;
  pParams->delay;
  pParams->nonBlocking;   // whether the source is non-blocking or not
  */
return false;
}

//========================================================================
uint32_t CFileIDataSource::Read(void *ch, uint32_t size, unsigned char *buf, enum EDSResult *pRes)
{
  //fprintf(stderr, "CFileIDataSource::Read, ch(%d), size(%d)\n",
  //  (int)ch, size);
  uint32_t readSize;
  readSize = read((int)ch, buf, size);

  if (readSize > 0)
  {
    if (pRes)
      *pRes = DSRES_OK;
    return readSize;
  }
  else if (readSize < size)
  {
    if (pRes)
      *pRes = DSRES_EOF;
    return 0;
  }
  else if (pRes)
    *pRes = DSRES_SOURCE_ERROR;

  return -1;
}

//========================================================================
int64_t CFileIDataSource::Seek(void *ch, int64_t pos, bool isRel, enum EDSResult *pRes)
{
  //fprintf(stderr, "CFileIDataSource::Seek, ch(%d), pos(%lld), isRel(%d)\n",
  //  (int)ch, pos, isRel);
  if (pRes)
    *pRes = DSRES_SOURCE_ERROR;

  if (!isRel && (pos == -1))
  {
    // seek to the end of file
    pos = lseek((int)ch, 0, SEEK_END);
    if (pos == -1)
      return -1;
  }
  else
  {
    pos = lseek((int)ch, pos, isRel ? SEEK_CUR : SEEK_SET);
    if (pos == -1)
    return -1;
  }

  if (pRes)
    *pRes = DSRES_OK;

  return pos;
}

//========================================================================
void CFileIDataSource::Flush(void *ch, bool internal = false)
{
};


//========================================================================
void CFileIDataSource::Close(void* ch)
{
  fprintf(stderr, "CFileIDataSource::Close, ch(%d)\n",
    (int)ch);
  close((int)ch);
}
