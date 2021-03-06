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

#include "FileCurl.h"
#include "Util.h"
#include "URL.h"
#include "AdvancedSettings.h"
#include "GUISettings.h"
#include "utils/log.h"
#include "Application.h"
#include "SystemInfo.h"
#include "File.h"

#include <vector>
#include <climits>

#ifdef _LINUX
#include <errno.h>
#include <inttypes.h>
#include "../linux/XFileUtils.h"
#endif

#include "DllLibCurl.h"
#include "FileShoutcast.h"
#include "bxcurl.h"
#include "utils/CharsetConverter.h"
#include "utils/TimeUtils.h"

using namespace XFILE;

#define XMIN(a,b) ((a)<(b)?(a):(b))
#define FITS_INT(a) (((a) <= INT_MAX) && ((a) >= INT_MIN))

CStdString CFileCurl::m_strCookieFileName;

#ifdef __APPLE__
extern "C" int __stdcall dllselect(int ntfs, fd_set *readfds, fd_set *writefds, fd_set *errorfds, const timeval *timeout);
#define dllselect select
#else
#define dllselect select
#endif

// curl calls this routine to debug
extern "C" int debug_callback(CURL_HANDLE *handle, curl_infotype info, char *output, size_t size, void *data)
{
  if (info == CURLINFO_DATA_IN || info == CURLINFO_DATA_OUT)
    return 0;

  CStdString strLine;
  strLine.append(output, size);
  std::vector<CStdString> vecLines;
  CUtil::Tokenize(strLine, vecLines, "\r\n");
  std::vector<CStdString>::const_iterator it = vecLines.begin();

  while (it != vecLines.end()) {
    CLog::Log(LOGDEBUG, "Curl::Debug %s", (*it).c_str());
    it++;
  }
  return 0;
}

/* curl calls this routine to get more data */
extern "C" size_t dummy_callback(char *buffer,
                                 size_t size,
                                 size_t nitems,
                                 void *userp)
{
  return 0;  
}

extern "C" size_t write_callback(char *buffer,
               size_t size,
               size_t nitems,
               void *userp)
{
  if(userp == NULL || g_application.m_bStop) return 0;
 
  CFileCurl::CReadState *state = (CFileCurl::CReadState *)userp;
  return state->WriteCallback(buffer, size, nitems);
}

extern "C" size_t header_callback(void *ptr, size_t size, size_t nmemb, void *stream)
{
  CFileCurl::CReadState *state = (CFileCurl::CReadState *)stream;
  return state->HeaderCallback(ptr, size, nmemb);
}

/* fix for silly behavior of realloc */
static inline void* realloc_simple(void *ptr, size_t size)
{
  void *ptr2 = realloc(ptr, size);
  if(ptr && !ptr2 && size > 0)
  {
    free(ptr);
    return NULL;
  }
  else
    return ptr2;
}

size_t CFileCurl::CReadState::HeaderCallback(void *ptr, size_t size, size_t nmemb)
{
  // libcurl doc says that this info is not always \0 terminated
  char* strData = (char*)ptr;
  int iSize = size * nmemb;
  
  if (strData[iSize] != 0)
  {
    strData = (char*)malloc(iSize + 1);
    strncpy(strData, (char*)ptr, iSize);
    strData[iSize] = 0;
  }
  else strData = strdup((char*)ptr);
  
  m_httpheader.Parse(strData);

  free(strData);
  
  return iSize;
}

size_t CFileCurl::CReadState::WriteCallback(char *buffer, size_t size, size_t nitems)
{
  unsigned int amount = size * nitems;
//  CLog::Log(LOGDEBUG, "CFileCurl::WriteCallback (%p) with %i bytes, readsize = %i, writesize = %i", this, amount, m_buffer.GetMaxReadSize(), m_buffer.GetMaxWriteSize() - m_overflowSize);
  if (m_overflowSize)
  {
    // we have our overflow buffer - first get rid of as much as we can
    unsigned int maxWriteable = XMIN((unsigned int)m_buffer.GetMaxWriteSize(), m_overflowSize);
    if (maxWriteable)
    {
      if (!m_buffer.WriteData(m_overflowBuffer, maxWriteable))
        CLog::Log(LOGERROR, "Unable to write to buffer - what's up?");
      if (m_overflowSize > maxWriteable)
      { // still have some more - copy it down
        memmove(m_overflowBuffer, m_overflowBuffer + maxWriteable, m_overflowSize - maxWriteable);
      }
      m_overflowSize -= maxWriteable;
    }
  }
  // ok, now copy the data into our ring buffer
  unsigned int maxWriteable = XMIN((unsigned int)m_buffer.GetMaxWriteSize(), amount);
  if (maxWriteable)
  {
    if (!m_buffer.WriteData(buffer, maxWriteable))
      CLog::Log(LOGERROR, "Unable to write to buffer - what's up?");
    amount -= maxWriteable;
    buffer += maxWriteable;
  }
  if (amount)
  {
    CLog::Log(LOGDEBUG, "CFileCurl::WriteCallback(%p) not enough free space for %i bytes", (void*)this,  amount); 
    
    m_overflowBuffer = (char*)realloc_simple(m_overflowBuffer, amount + m_overflowSize);
    if(m_overflowBuffer == NULL)
    {
      CLog::Log(LOGDEBUG, "%s - Failed to grow overflow buffer", __FUNCTION__);
      return 0;
    }
    memcpy(m_overflowBuffer + m_overflowSize, buffer, amount);
    m_overflowSize += amount;
  }
  m_bDirty = true;
  return size * nitems;
}

CFileCurl::CReadState::CReadState()
{
  m_easyHandle = NULL;
  m_multiHandle = NULL;
  m_overflowBuffer = NULL;
  m_overflowSize = 0;
	m_filePos = 0;
	m_fileSize = 0;
  m_bufferSize = 0;
  m_cancelled = false;
  m_bFirstLoop = true;
  m_bDirty     = false;
}

CFileCurl::CReadState::~CReadState()
{
  Disconnect();

  if(m_easyHandle)
    g_curlInterface.easy_release(&m_easyHandle, &m_multiHandle);
}

void CFileCurl::CReadState::Init()
{
  m_easyHandle = g_curlInterface.easy_init();
  m_multiHandle = g_curlInterface.multi_init();;
  CLog::Log(LOGDEBUG, "FileCurl::%s (%p - %p)", __FUNCTION__, m_easyHandle, m_multiHandle);
}

bool CFileCurl::CReadState::Seek(int64_t pos)
{
  if(pos == m_filePos)
    return true;

  if(FITS_INT(pos - m_filePos) && m_buffer.SkipBytes((int)(pos - m_filePos)))
  {
    m_filePos = pos;
    return true;
  }
  
  if(pos > m_filePos && pos < m_filePos + m_bufferSize)
  {
    int len = m_buffer.GetMaxReadSize();
    m_filePos += len;
    m_buffer.SkipBytes(len);
    if(!FillBuffer(m_bufferSize))
    {
      if(!m_buffer.SkipBytes(-len))
        CLog::Log(LOGERROR, "%s - Failed to restore position after failed fill", __FUNCTION__);
      else
        m_filePos -= len;
      return false;
    }

    if(!FITS_INT(pos - m_filePos) || !m_buffer.SkipBytes((int)(pos - m_filePos)))
    {
      CLog::Log(LOGERROR, "%s - Failed to skip to position after having filled buffer", __FUNCTION__);
      if(!m_buffer.SkipBytes(-len))
        CLog::Log(LOGERROR, "%s - Failed to restore position after failed seek", __FUNCTION__);
      else
        m_filePos -= len;
      return false;
    }
    m_filePos = pos;
    return true;
  }
  return false;
}

long CFileCurl::CReadState::Connect(unsigned int size)
{
  g_curlInterface.easy_setopt(m_easyHandle, CURLOPT_RESUME_FROM_LARGE, m_filePos);
  g_curlInterface.multi_add_handle(m_multiHandle, m_easyHandle);

  m_bufferSize = size;
  m_buffer.Destroy();
  m_buffer.Create(size * 3);

  // read some data in to try and obtain the length
  // maybe there's a better way to get this info??
  m_stillRunning = 1;
  if (!FillBuffer(1))
  {
    long response = -1;
    g_curlInterface.easy_getinfo(m_easyHandle, CURLINFO_RESPONSE_CODE, &response);
    CLog::Log(LOGDEBUG, "CFileCurl::CReadState::Open, didn't get any data from stream. HTTP CODE: %lu", response);
    return response;
  }

  double length;
  if (CURLE_OK == g_curlInterface.easy_getinfo(m_easyHandle, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &length))
  {
    if (length < 0)
      length = 0.0;
    m_fileSize = m_filePos + (int64_t)length;
  }

  char *url = NULL; 
  if (CURLE_OK == g_curlInterface.easy_getinfo(m_easyHandle, CURLINFO_EFFECTIVE_URL, (char*)&url))
    m_strEffectiveUrl = url;
    
  long response;
  if (CURLE_OK == g_curlInterface.easy_getinfo(m_easyHandle, CURLINFO_RESPONSE_CODE, &response))
    return response;

  return -1;
}

long CFileCurl::CReadState::Reconnect()
{
  m_filePos=0;
  m_fileSize=0;
  m_bDirty=true;
  m_buffer.Destroy();
  m_buffer.Create(m_bufferSize * 3);
  
  if(m_multiHandle && m_easyHandle)
  {
    g_curlInterface.multi_remove_handle(m_multiHandle, m_easyHandle);
    g_curlInterface.multi_add_handle(m_multiHandle, m_easyHandle);
  }
  
  m_stillRunning = 1;
  if (!FillBuffer(1))
  {
    long response = -1;
    g_curlInterface.easy_getinfo(m_easyHandle, CURLINFO_RESPONSE_CODE, &response);
    CLog::Log(LOGERROR, "CFileCurl::CReadState::Open, didn't get any data from stream. HTTP CODE: %lu", response);    
    return response;
  }
  
  double length;
  if (CURLE_OK == g_curlInterface.easy_getinfo(m_easyHandle, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &length))
  {
    if (length < 0)
      length = 0.0;
    m_fileSize = m_filePos + (int64_t)length;
  }
  
  char *url = NULL; 
  if (CURLE_OK == g_curlInterface.easy_getinfo(m_easyHandle, CURLINFO_EFFECTIVE_URL, (char*)&url))
    m_strEffectiveUrl = url;
  
  long response;
  if (CURLE_OK == g_curlInterface.easy_getinfo(m_easyHandle, CURLINFO_RESPONSE_CODE, &response))
    return response;
  
  return -1;
}

void CFileCurl::CReadState::Disconnect()
{
  if(m_multiHandle && m_easyHandle)
      g_curlInterface.multi_remove_handle(m_multiHandle, m_easyHandle);
  
  m_buffer.Clear();
  free(m_overflowBuffer);
  m_overflowBuffer = NULL;
  m_overflowSize = 0;
  m_filePos = 0;
  m_fileSize = 0;
  m_bufferSize = 0;
}

CFileCurl::~CFileCurl()
{ 
  if (m_opened)
  Close();
  delete m_state;
  g_curlInterface.Unload();
}

CFileCurl::CFileCurl()
{
  g_curlInterface.Load(); // loads the curl dll and resolves exports etc.
  m_curlAliasList = NULL;
  m_curlHeaderList = NULL;
  m_opened = false;
  m_multisession  = true;
  m_seekable = true;
  m_useOldHttpVersion = false;
  m_connecttimeout = 0;
  m_lowspeedtime = 0;
  m_ftpauth = "";
  m_ftpport = "";
  m_ftppasvip = false;
  m_bufferSize = 32768;
  m_binary = true;
  m_postdata = "";
  m_httpTimeModified = 0;
  m_state = new CReadState();
  m_lastRetCode = 0;
}

//Has to be called before Open()
void CFileCurl::SetBufferSize(unsigned int size)
{
  m_bufferSize = size;
}

void CFileCurl::Close()
{
  CLog::Log(LOGDEBUG, "FileCurl::Close(%p) %s", (void*)this, m_url.c_str());

  if (m_state->m_easyHandle)
    g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_COOKIELIST, "FLUSH");

  m_state->Disconnect();

  CURI url(m_url);
  double totalTime = 0.0, dnsTime = 0.0, connectTime = 0.0, pretransfer = 0.0, speed = 0.0, sizeDownloaded = 0.0;
  g_curlInterface.easy_getinfo(m_state->m_easyHandle, CURLINFO_TOTAL_TIME, &totalTime);
  g_curlInterface.easy_getinfo(m_state->m_easyHandle, CURLINFO_NAMELOOKUP_TIME, &dnsTime);
  g_curlInterface.easy_getinfo(m_state->m_easyHandle, CURLINFO_CONNECT_TIME, &connectTime);
  g_curlInterface.easy_getinfo(m_state->m_easyHandle, CURLINFO_PRETRANSFER_TIME, &pretransfer);
  g_curlInterface.easy_getinfo(m_state->m_easyHandle, CURLINFO_SPEED_DOWNLOAD, &speed);
  g_curlInterface.easy_getinfo(m_state->m_easyHandle, CURLINFO_SIZE_DOWNLOAD, &sizeDownloaded);
  CLog::Log(LOGDEBUG,"CURL STATS: done with <%s>. %4.2fk in: %4.2f sec. dns: %4.2f sec. connect: %4.2f s. pretransfer: %4.2f s. speed: %4.2f kbytes/sec.\n",
         url.GetHostName().c_str(), 
         sizeDownloaded / 1024.0, totalTime, dnsTime, connectTime, pretransfer, speed / 1024.0); 
  
  m_url.Empty();
  m_referer.Empty();
  
  /* cleanup */
  if( m_curlAliasList )
    g_curlInterface.slist_free_all(m_curlAliasList);
  if( m_curlHeaderList )
    g_curlInterface.slist_free_all(m_curlHeaderList);
  
  m_curlAliasList = NULL;
  m_curlHeaderList = NULL;
  m_opened = false;
}

void CFileCurl::SetCommonOptions(CReadState* state)
{
  CURL_HANDLE* h = state->m_easyHandle;

  g_curlInterface.easy_reset(h);

  g_curlInterface.easy_setopt(h, CURLOPT_DEBUGFUNCTION, (void *)debug_callback);

  g_curlInterface.easy_setopt(h, CURLOPT_VERBOSE, 0);
  
  g_curlInterface.easy_setopt(h, CURLOPT_WRITEDATA, state);
  g_curlInterface.easy_setopt(h, CURLOPT_WRITEFUNCTION, (void *)write_callback);
  
  // make sure headers are seperated from the data stream
  g_curlInterface.easy_setopt(h, CURLOPT_WRITEHEADER, state);
  g_curlInterface.easy_setopt(h, CURLOPT_HEADERFUNCTION, (void *)header_callback);
  g_curlInterface.easy_setopt(h, CURLOPT_HEADER, FALSE);
  
  g_curlInterface.easy_setopt(h, CURLOPT_FTP_USE_EPSV, 0); // turn off epsv
  
  // Allow us to follow two redirects
  g_curlInterface.easy_setopt(h, CURLOPT_FOLLOWLOCATION, TRUE);
  g_curlInterface.easy_setopt(h, CURLOPT_MAXREDIRS, 8);
//  g_curlInterface.easy_setopt(h, CURLOPT_TCP_NODELAY, 1);

  // When using multiple threads you should set the CURLOPT_NOSIGNAL option to
  // TRUE for all handles. Everything will work fine except that timeouts are not
  // honored during the DNS lookup - which you can work around by building libcurl
  // with c-ares support. c-ares is a library that provides asynchronous name
  // resolves. Unfortunately, c-ares does not yet support IPv6.
  g_curlInterface.easy_setopt(h, CURLOPT_NOSIGNAL, TRUE);

  // not interested in failed requests
  g_curlInterface.easy_setopt(h, CURLOPT_FAILONERROR, 1);

  // enable support for icecast / shoutcast streams
  m_curlAliasList = g_curlInterface.slist_append(m_curlAliasList, "ICY 200 OK"); 
  g_curlInterface.easy_setopt(h, CURLOPT_HTTP200ALIASES, m_curlAliasList); 

  // never verify peer, we don't have any certificates to do this
  g_curlInterface.easy_setopt(h, CURLOPT_SSL_VERIFYPEER, 0);
  g_curlInterface.easy_setopt(h, CURLOPT_SSL_VERIFYHOST, 0);

  g_curlInterface.easy_setopt(h, CURLOPT_URL, m_url.c_str());
  g_curlInterface.easy_setopt(h, CURLOPT_TRANSFERTEXT, FALSE);

  // setup POST data if it exists
  if (!m_postdata.IsEmpty())
  {
    g_curlInterface.easy_setopt(h, CURLOPT_POST, 1 );
    g_curlInterface.easy_setopt(h, CURLOPT_POSTFIELDSIZE, (int) m_postdata.length());
    g_curlInterface.easy_setopt(h, CURLOPT_POSTFIELDS, m_postdata.c_str());
  }

  // setup Referer header if needed
  if (!m_referer.IsEmpty())
    g_curlInterface.easy_setopt(h, CURLOPT_REFERER, m_referer.c_str());
  else
    g_curlInterface.easy_setopt(h, CURLOPT_AUTOREFERER, TRUE);
    
  // setup any requested authentication
  if( m_ftpauth.length() > 0 )
  {
    g_curlInterface.easy_setopt(h, CURLOPT_FTP_SSL, CURLFTPSSL_TRY);
    if( m_ftpauth.Equals("any") )
      g_curlInterface.easy_setopt(h, CURLOPT_FTPSSLAUTH, CURLFTPAUTH_DEFAULT);
    else if( m_ftpauth.Equals("ssl") )
      g_curlInterface.easy_setopt(h, CURLOPT_FTPSSLAUTH, CURLFTPAUTH_SSL);
    else if( m_ftpauth.Equals("tls") )
      g_curlInterface.easy_setopt(h, CURLOPT_FTPSSLAUTH, CURLFTPAUTH_TLS);
  }

  // allow passive mode for ftp
  if( m_ftpport.length() > 0 )
    g_curlInterface.easy_setopt(h, CURLOPT_FTPPORT, m_ftpport.c_str());
  else
    g_curlInterface.easy_setopt(h, CURLOPT_FTPPORT, (void *)NULL);

  // allow curl to not use the ip address in the returned pasv response
  if( m_ftppasvip )
    g_curlInterface.easy_setopt(h, CURLOPT_FTP_SKIP_PASV_IP, 0);
  else
    g_curlInterface.easy_setopt(h, CURLOPT_FTP_SKIP_PASV_IP, 1);

  // setup Content-Encoding if requested
  if( m_contentencoding.length() > 0 )
    g_curlInterface.easy_setopt(h, CURLOPT_ENCODING, m_contentencoding.c_str());
  
  if (m_userAgent.length() > 0)
    g_curlInterface.easy_setopt(h, CURLOPT_USERAGENT, m_userAgent.c_str());
  else /* set some default agent as shoutcast doesn't return proper stuff otherwise */
    g_curlInterface.easy_setopt(h, CURLOPT_USERAGENT, BOXEE::BXCurl::GetGlobalUserAgent().c_str());
  
  if (m_useOldHttpVersion)
    g_curlInterface.easy_setopt(h, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_0);
  else
    SetRequestHeader("Connection", "keep-alive");

  if (m_proxy.length() > 0)
  {
    g_curlInterface.easy_setopt(h, CURLOPT_PROXY, m_proxy.c_str());
    if (m_proxyuserpass.length() > 0)
      g_curlInterface.easy_setopt(h, CURLOPT_PROXYUSERPWD, m_proxyuserpass.c_str());

  }
  if (m_customrequest.length() > 0)
    g_curlInterface.easy_setopt(h, CURLOPT_CUSTOMREQUEST, m_customrequest.c_str());

  if (m_connecttimeout == 0)
    m_connecttimeout = g_advancedSettings.m_curlconnecttimeout;

  /*
  curl_easy_setopt(h, CURLOPT_DNS_CACHE_TIMEOUT , 0);
  g_curlInterface.easy_setopt(h, CURLOPT_CONNECTTIMEOUT, m_timeout);

  // set our timeouts, we abort connection after m_timeout, and reads after no data for m_timeout seconds
  g_curlInterface.easy_setopt(h, CURLOPT_CONNECTTIMEOUT, m_connecttimeout);

  // We abort in case we transfer less than 1byte/second
  g_curlInterface.easy_setopt(h, CURLOPT_LOW_SPEED_LIMIT, 1);

  if (m_lowspeedtime == 0)
    m_lowspeedtime = g_advancedSettings.m_curllowspeedtime;

  // Set the lowspeed time very low as it seems Curl takes much longer to detect a lowspeed condition
  g_curlInterface.easy_setopt(h, CURLOPT_LOW_SPEED_TIME, m_lowspeedtime);
  */
  
// boxee  
  g_curlInterface.easy_setopt(h, CURLOPT_COOKIEFILE, m_strCookieFileName.c_str());
  g_curlInterface.easy_setopt(h, CURLOPT_COOKIEJAR, m_strCookieFileName.c_str());
  g_curlInterface.easy_setopt(h, CURLOPT_HTTPAUTH, CURLAUTH_ANYSAFE);
  g_curlInterface.easy_setopt(h, CURLOPT_COOKIELIST, "FLUSH");
  SetRequestHeader("Icy-MetaData", "1");

  if (!m_etag.IsEmpty())
  {
    SetRequestHeader("If-None-Match",m_etag);
  }
  else if (m_httpTimeModified > 0)
  {
    g_curlInterface.easy_setopt(h, CURLOPT_TIMEVALUE, (int)m_httpTimeModified);
    g_curlInterface.easy_setopt(h, CURLOPT_TIMECONDITION, CURL_TIMECOND_IFMODSINCE);
  }

// end boxee section

}

void CFileCurl::SetRequestHeaders(CReadState* state)
{
  if(m_curlHeaderList) 
  {
    g_curlInterface.slist_free_all(m_curlHeaderList);
    m_curlHeaderList = NULL;
  }

  MAPHTTPHEADERS::iterator it;
  for(it = m_requestheaders.begin(); it != m_requestheaders.end(); it++)
  {
    CStdString buffer = it->first + ": " + it->second;
    m_curlHeaderList = g_curlInterface.slist_append(m_curlHeaderList, buffer.c_str()); 
  }

  // add user defined headers
  if (m_curlHeaderList && state->m_easyHandle)
    g_curlInterface.easy_setopt(state->m_easyHandle, CURLOPT_HTTPHEADER, m_curlHeaderList); 

}

void CFileCurl::SetCorrectHeaders(CReadState* state)
{
  CHttpHeader& h = state->m_httpheader;
  /* workaround for shoutcast server wich doesn't set content type on standard mp3 */
  if( h.GetContentType().IsEmpty() )
  {
    if( !h.GetValue("icy-notice1").IsEmpty()
    || !h.GetValue("icy-name").IsEmpty()
    || !h.GetValue("icy-br").IsEmpty() )
    h.Parse("Content-Type: audio/mpeg\r\n");
  }

  /* hack for google video */
  if ( h.GetContentType().Equals("text/html") 
  &&  !h.GetValue("Content-Disposition").IsEmpty() )
  {
    CStdString strValue = h.GetValue("Content-Disposition");
    if (strValue.Find("filename=") > -1 && strValue.Find(".flv") > -1)
      h.Parse("Content-Type: video/flv\r\n");
  }
}

void CFileCurl::ParseAndCorrectUrl(CURI &url2)
{
  if( url2.GetProtocol().Equals("ftpx") )
    url2.SetProtocol("ftp");
  else if( url2.GetProtocol().Equals("shout") 
       ||  url2.GetProtocol().Equals("daap") 
       ||  url2.GetProtocol().Equals("tuxbox") 
       ||  url2.GetProtocol().Equals("lastfm")
       ||  url2.GetProtocol().Equals("mms"))
    url2.SetProtocol("http");    

  if( url2.GetProtocol().Equals("ftp")
  ||  url2.GetProtocol().Equals("ftps") )
  {
    /* this is uggly, depending on from where   */
    /* we get the link it may or may not be     */
    /* url encoded. if handed from ftpdirectory */
    /* it won't be so let's handle that case    */
    
    CStdString partial, filename(url2.GetFileName());
    CStdStringArray array;

    /* our current client doesn't support utf8 */
    g_charsetConverter.utf8ToStringCharset(filename);

    /* TODO: create a tokenizer that doesn't skip empty's */
    CUtil::Tokenize(filename, array, "/");
    filename.Empty();
    for(CStdStringArray::iterator it = array.begin(); it != array.end(); it++)
    {
      if(it != array.begin())
        filename += "/";

      partial = *it;      
      CUtil::URLEncode(partial);      
      filename += partial;
    }

    /* make sure we keep slashes */    
    if(url2.GetFileName().Right(1) == "/")
      filename += "/";

    url2.SetFileName(filename);

    CStdString options = url2.GetOptions().Mid(1);
    options.TrimRight('/'); // hack for trailing slashes being added from source

    m_ftpauth = "";
    m_ftpport = "";
    m_ftppasvip = false;

    /* parse options given */
    CUtil::Tokenize(options, array, "&");
    for(CStdStringArray::iterator it = array.begin(); it != array.end(); it++)
    {
      CStdString name, value;
      int pos = it->Find('=');
      if(pos >= 0)
      {
        name = it->Left(pos);
        value = it->Mid(pos+1, it->size());
      }
      else
      {
        name = (*it);
        value = "";
      }      

      if(name.Equals("auth"))
      {
        m_ftpauth = value;
        if(m_ftpauth.IsEmpty())
          m_ftpauth = "any";
      }
      else if(name.Equals("active"))
      {
        m_ftpport = value;
        if(value.IsEmpty())
          m_ftpport = "-";
      }
      else if(name.Equals("pasvip"))
      {        
        if(value == "0")
          m_ftppasvip = false;
        else
          m_ftppasvip = true;
      }
    }

    /* ftp has no options */
    url2.SetOptions("");
  }
  else if( url2.GetProtocol().Equals("http")
       ||  url2.GetProtocol().Equals("https"))
  {
    if (g_guiSettings.GetBool("network.usehttpproxy") && m_proxy.IsEmpty() && !g_guiSettings.GetString("network.httpproxyserver").IsEmpty())
    {
      m_proxy = "http://" + g_guiSettings.GetString("network.httpproxyserver");
      m_proxy += ":" + g_guiSettings.GetString("network.httpproxyport");
      if (g_guiSettings.GetString("network.httpproxyusername").length() > 0 && m_proxyuserpass.IsEmpty())
      {
        m_proxyuserpass = g_guiSettings.GetString("network.httpproxyusername");
        m_proxyuserpass += ":" + g_guiSettings.GetString("network.httpproxypassword");
      }
      CLog::Log(LOGDEBUG, "Using proxy %s", m_proxy.c_str());
    }
    // handle any protocol options
    CStdString options = url2.GetProtocolOptions();
    options.TrimRight('/'); // hack for trailing slashes being added from source
    if (options.length() > 0)
    {
      // clear protocol options
      url2.SetProtocolOptions("");
      // set xbmc headers
      CStdStringArray array;
      CUtil::Tokenize(options, array, "&");
      for(CStdStringArray::iterator it = array.begin(); it != array.end(); it++)
      {
        // parse name, value
        CStdString name, value;
        int pos = it->Find('=');
        if(pos >= 0)
        {
          name = it->Left(pos);
          value = it->Mid(pos+1, it->size());
  }
        else
        {
          name = (*it);
          value = "";
}
        // url decode value
        CUtil::UrlDecode(value);
        // set headers
        if (name.Equals("User-Agent"))
          SetUserAgent(value);
        else
          SetRequestHeader(name, value);
      }
    }
  }
}

bool CFileCurl::Post(const CStdString& strURL, const CStdString& strPostData, CStdString& strHTML, bool bCloseConnection)
{
  return Service(strURL, strPostData, strHTML, bCloseConnection);
}

bool CFileCurl::Get(const CStdString& strURL, CStdString& strHTML, bool bCloseConnection)
{
  return Service(strURL, "", strHTML, bCloseConnection);
}

bool CFileCurl::Service(const CStdString& strURL, const CStdString& strPostData, CStdString& strHTML, bool bCloseConnection)
{
  XBMC::HttpCacheHandle cacheHandle = g_application.GetHttpCacheManager().Open();
  XBMC::CHttpCacheHandleGuard guard(cacheHandle);

  m_postdata = strPostData;
  
  std::string strLocalCacheName;

  m_etag.clear();
  m_httpTimeModified = 0;

  if (m_postdata.empty())
  {
    XBMC::HttpCacheReturnCode rc = g_application.GetHttpCacheManager().StartCachingURL(cacheHandle, strURL.c_str(), strLocalCacheName, m_etag, m_httpTimeModified);
    if (rc == XBMC::HTTP_CACHE_ALREADY_IN_PROGRESS)
    {
      if ( g_application.GetHttpCacheManager().WaitForURL(cacheHandle, strURL.c_str(), strLocalCacheName, 5000) == XBMC::HTTP_CACHE_OK )
        rc = XBMC::HTTP_CACHE_ALREADY_EXISTS;
      else
        rc = g_application.GetHttpCacheManager().StartCachingURL(cacheHandle, strURL.c_str(), strLocalCacheName, m_etag, m_httpTimeModified);
    }
    
    if (rc == XBMC::HTTP_CACHE_ALREADY_EXISTS)
    {
      if (ReadFile(strLocalCacheName, strHTML))
        return true;
      strLocalCacheName.clear();
    }
    else if (rc != XBMC::HTTP_CACHE_OK)
    {
      strLocalCacheName.clear();
      cacheHandle = NULL;
      m_etag.clear();
      m_httpTimeModified = 0;
    }
  }
  
  if (Open(strURL))
  {
    if (ReadData(strHTML))
    {
      long nLastCode=200;
      g_curlInterface.easy_getinfo(m_state->m_easyHandle, CURLINFO_RESPONSE_CODE, &nLastCode);

      if (bCloseConnection)
        Close();
      
      if (nLastCode == 304)
      {
        ReadFile(strLocalCacheName, strHTML);
        strLocalCacheName.clear();
      }
      
      if (!strLocalCacheName.empty() && strHTML.size() && cacheHandle)
      {
        XFILE::CFile file;
        if (file.OpenForWrite(strLocalCacheName, true))
        {
          file.Write(strHTML.data(), strHTML.size());
          file.Close();
          XBMC::HttpCacheHeaders headers;
          const char **ptr = g_application.GetHttpCacheManager().GetUsedReponseHeaders();
          while (ptr && *ptr)
          {
            std::string strHeader = m_state->m_httpheader.GetValue(*ptr).c_str();
            if (!strHeader.empty())
              headers[*ptr] = strHeader;
            ptr++;
          }
          g_application.GetHttpCacheManager().DoneCachingURL(cacheHandle, strURL.c_str(), nLastCode, headers);
        }
      }
      return true;
    }
  }
  Close();
  return false;
}

bool CFileCurl::ReadFile(const CStdString &fileName, CStdString &out)
{
  XFILE::CFile file;
  if (file.Open(fileName))
  {
    char buffer[16384];
    unsigned int size_read; 
    while( (size_read = file.Read(buffer, sizeof(buffer)) ) > 0 )
      out.append(buffer, size_read);
    file.Close();
    return true;
  }
  return false;
}

bool CFileCurl::ReadData(CStdString& strHTML)
{
  int size_read = 0;
  int data_size = 0;
  strHTML = "";
  char buffer[16384];
  while( (size_read = Read(buffer, sizeof(buffer)) ) > 0 )
  {
    strHTML.append(buffer, size_read);
    data_size += size_read;
  }
  if (m_state->m_fileSize > 0 && m_state->m_fileSize != data_size)
  {
    CLog::Log(LOGDEBUG,"%s - not all data retrieved! (%d from %"PRId64"). aborting.", __FUNCTION__, data_size, m_state->m_fileSize);
    return false;
  }

  if (m_state->m_cancelled)
    return false;
  return true;
}

bool CFileCurl::Download(const CStdString& strURL, const CStdString& strFileName, LPDWORD pdwSize)
{
  CLog::Log(LOGINFO, "Download: %s->%s", strURL.c_str(), strFileName.c_str());

  CStdString strData;
  if (!Get(strURL, strData))
    return false;

  XFILE::CFile file;
  if (!file.OpenForWrite(strFileName, true))
  {
    CLog::Log(LOGERROR, "Unable to open file %s: %u",
    strFileName.c_str(), GetLastError());
    return false;
  }
  if (strData.size())
    file.Write(strData.data(), strData.size());
  file.Close();

  if (pdwSize != NULL)
  {
    *pdwSize = strData.size();
  }

  return true;
}

// Detect whether we are "online" or not! Very simple and dirty!
bool CFileCurl::IsInternet(bool checkDNS /* = true */)
{
  CStdString strURL = "http://www.google.com";
  if (!checkDNS)
    strURL = "http://74.125.19.103"; // www.google.com ip

  int result = Stat(strURL, NULL);
  Close();

  if (result)
    return false;

  return true;
}

void CFileCurl::Cancel()
{
  m_state->m_cancelled = true;
  while (m_opened)
    Sleep(1);
}

void CFileCurl::Reset()
{
  m_state->m_cancelled = false;
}


bool CFileCurl::Open(const CURI& url)
{
  CURI url2(url);
  ParseAndCorrectUrl(url2);

  m_url = url2.Get();

  CLog::Log(LOGDEBUG, "FileCurl::Open(%p) %s", (void*)this, m_url.c_str());  

  ASSERT(!(!m_state->m_easyHandle ^ !m_state->m_multiHandle));
  m_state->m_strEffectiveUrl = m_url;
  if( m_state->m_easyHandle == NULL )
    g_curlInterface.easy_aquire(url2.GetProtocol(), url2.GetHostName(), &m_state->m_easyHandle, &m_state->m_multiHandle );
  
  // reset the handle state before reusing it. it has to happen before setting the options.
  // later we will re-use this handle to utilize its "keep-alive" feature.
  if (m_opened)
    g_curlInterface.easy_reset(m_state->m_easyHandle);

  // setup common curl options
  SetCommonOptions(m_state);
  SetRequestHeaders(m_state);

  if (!m_opened)
  {
    m_lastRetCode = m_state->Connect(m_bufferSize);
    if( m_lastRetCode < 0 || m_lastRetCode >= 400)
    return false;
  }
  else
  {
    if (m_state->Reconnect() < 0)
      return false;
  }
  
  m_opened = true;
  
  SetCorrectHeaders(m_state);

  // check if this stream is a shoutcast stream. sometimes checking the protocol line is not enough so examine other headers as well.
  // shoutcast streams should be handled by FileShoutcast.
  if (m_state->m_httpheader.GetProtoLine().Left(3) == "ICY" || !m_state->m_httpheader.GetValue("icy-notice1").IsEmpty()
     || !m_state->m_httpheader.GetValue("icy-name").IsEmpty()
     || !m_state->m_httpheader.GetValue("icy-br").IsEmpty() )
  {
    CLog::Log(LOGDEBUG,"FileCurl - file <%s> is a shoutcast stream. re-opening", m_url.c_str());
    throw new CRedirectException(new CFileShoutcast); 
  }

  m_url = m_state->m_strEffectiveUrl;
  
  m_multisession = false;
  if(m_url.Left(5).Equals("http:") || m_url.Left(6).Equals("https:"))
  {
    m_multisession = true;
    if(m_state->m_httpheader.GetValue("Server").Find("Portable SDK for UPnP devices") >= 0)
    {
      CLog::Log(LOGWARNING, "FileCurl - disabling multi session due to broken libupnp server");
      m_multisession = false;
    }
  }

  CLog::Log(LOGDEBUG,"%s - filesize: %llu", __FUNCTION__, m_state->m_fileSize);
  
  if(m_state->m_httpheader.GetValue("Transfer-Encoding").Equals("chunked"))
    m_state->m_fileSize = 0;

  m_seekable = false;
  if(m_state->m_fileSize > 0)
  {
    m_seekable = true;

    if(url2.GetProtocol().Equals("http")
    || url2.GetProtocol().Equals("https"))
    {
      // if server says explicitly it can't seek, respect that
      if(m_state->m_httpheader.GetValue("Accept-Ranges").Equals("none"))
        m_seekable = false;
    }
  }

  return true;
}

bool CFileCurl::CReadState::ReadString(char *szLine, int iLineLength)
{
  unsigned int want = (unsigned int)iLineLength;

  if((m_fileSize == 0 || m_filePos < m_fileSize) && !FillBuffer(want))
    return false;

  // ensure only available data is considered 
  want = XMIN((unsigned int)m_buffer.GetMaxReadSize(), want);

  /* check if we finished prematurely */
  if (!m_stillRunning && (m_fileSize == 0 || m_filePos != m_fileSize) && !want)
  {
    if (m_fileSize != 0)
      CLog::Log(LOGWARNING, "%s - Transfer ended before entire file was retrieved pos %"PRId64", size %"PRId64, __FUNCTION__, m_filePos, m_fileSize);
      
    return false;
  }

  char* pLine = szLine;
  do
  {
    if (!m_buffer.ReadData(pLine, 1))
      break;

    pLine++;
  } while (((pLine - 1)[0] != '\n') && ((unsigned int)(pLine - szLine) < want));
  pLine[0] = 0;
  m_filePos += (pLine - szLine);
  return (bool)((pLine - szLine) > 0);
}

bool CFileCurl::Exists(const CURI& url)
{
  return Stat(url, NULL) == 0;
}


int64_t CFileCurl::Seek(int64_t iFilePosition, int iWhence)
{
  int64_t nextPos = m_state->m_filePos;
	switch(iWhence) 
	{
		case SEEK_SET:
			nextPos = iFilePosition;
			break;
		case SEEK_CUR:
			nextPos += iFilePosition;
			break;
		case SEEK_END:
			if (m_state->m_fileSize)
        nextPos = m_state->m_fileSize + iFilePosition;
      else
        return -1;
			break;
    case SEEK_POSSIBLE:
      return m_seekable ? 1 : 0;
    default:
      return -1;
	}

  // We can't seek beyond EOF
  if (m_state->m_fileSize && nextPos > m_state->m_fileSize) return -1;

  if(m_state->Seek(nextPos))
    return nextPos;

  if(!m_seekable)
    return -1;

  CReadState* oldstate = NULL;
  if(m_multisession)
  {
    CURI url(m_url);
    oldstate = m_state;
    m_state = new CReadState();

    g_curlInterface.easy_aquire(url.GetProtocol(), url.GetHostName(), &m_state->m_easyHandle, &m_state->m_multiHandle );
    
    // setup common curl options
    SetCommonOptions(m_state);
  }
  else
    m_state->Disconnect();

  /* caller might have changed some headers (needed for daap)*/
  SetRequestHeaders(m_state);

  m_state->m_filePos = nextPos;
  long response = m_state->Connect(m_bufferSize);
  if(response < 0 && (m_state->m_fileSize == 0 || m_state->m_fileSize != m_state->m_filePos))
  {
    m_seekable = false;
    if(oldstate)
    {
      delete m_state;
      m_state = oldstate;
    }
    return -1;
  }

  SetCorrectHeaders(m_state);

  if(oldstate)
    delete oldstate;

  return m_state->m_filePos;
}

int64_t CFileCurl::GetLength()
{
	if (!m_opened) return 0;
	return m_state->m_fileSize;
}

int64_t CFileCurl::GetPosition()
{
	if (!m_opened) return 0;
	return m_state->m_filePos;
}

int CFileCurl::Stat(struct __stat64* buffer)
{
  buffer->st_size = GetLength();
	buffer->st_mode = _S_IFREG;
  return 0;
}

int CFileCurl::Stat(const CURI& url, struct __stat64* buffer)
{ 

  // if file is already running, get info from it
  if( m_opened )
  {
    CLog::Log(LOGWARNING, "%s - Stat called on open file", __FUNCTION__);
    buffer->st_size = GetLength();
		buffer->st_mode = _S_IFREG;
    return 0;
  }

  CURI url2(url);
  ParseAndCorrectUrl(url2);

  m_url = url2.Get();

  ASSERT(m_state->m_easyHandle == NULL);
  g_curlInterface.easy_aquire(url2.GetProtocol(), url2.GetHostName(), &m_state->m_easyHandle, NULL);

  SetCommonOptions(m_state); 
  SetRequestHeaders(m_state);
  g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_TIMEOUT, 5);
  g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_NOBODY, 0);
  g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_WRITEFUNCTION, (void*)dummy_callback);
  g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_FOLLOWLOCATION, 1);
  g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_WRITEDATA, (void *)NULL); /* will cause write failure*/
  
  char err[4096];
  memset(err,0,4096);
  g_curlInterface.easy_setopt (m_state->m_easyHandle, CURLOPT_ERRORBUFFER, err);
  
  if(url2.GetProtocol() == "ftp")
  {
    g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_FILETIME, 1);
    g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_FTP_FILEMETHOD, CURLFTPMETHOD_NOCWD);
  }

  CURLcode result = g_curlInterface.easy_perform(m_state->m_easyHandle);

  if(result == CURLE_GOT_NOTHING || result == CURLE_HTTP_RETURNED_ERROR )
  {
    /* some http servers and shoutcast servers don't give us any data on a head request */
    /* request normal and just fail out, it's their loss */
    /* somehow curl doesn't reset CURLOPT_NOBODY properly so reset everything */
    SetCommonOptions(m_state);
    SetRequestHeaders(m_state);
    g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_TIMEOUT, 5);
    g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_RANGE, "0-0");
    g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_WRITEDATA, (void *)NULL); /* will cause write failure*/
    result = g_curlInterface.easy_perform(m_state->m_easyHandle);
  }

  if( result == CURLE_HTTP_RANGE_ERROR )
  {
    /* crap can't use the range option, disable it and try again */
    g_curlInterface.easy_setopt(m_state->m_easyHandle, CURLOPT_RANGE, (void *)NULL);
    result = g_curlInterface.easy_perform(m_state->m_easyHandle);
  }

  m_lastRetCode = 0;
  g_curlInterface.easy_getinfo(m_state->m_easyHandle, CURLINFO_RESPONSE_CODE, &m_lastRetCode);

  if( result != CURLE_WRITE_ERROR && result != CURLE_OK )
  {
    g_curlInterface.easy_release(&m_state->m_easyHandle, NULL);
    errno = ENOENT;
    return -1;
  }

  double length;
  if (CURLE_OK != g_curlInterface.easy_getinfo(m_state->m_easyHandle, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &length) || length < 0.0)
  {
    if (url.GetProtocol() == "ftp")
    {
      g_curlInterface.easy_release(&m_state->m_easyHandle, NULL);
      errno = ENOENT;
      return -1;
    }
    else
      length = 0.0;
  }

  SetCorrectHeaders(m_state);

  if(buffer)
  {

    double length;
    char content[255];
    if (CURLE_OK != g_curlInterface.easy_getinfo(m_state->m_easyHandle, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &length)
     || CURLE_OK != g_curlInterface.easy_getinfo(m_state->m_easyHandle, CURLINFO_CONTENT_TYPE, content))
    {
      g_curlInterface.easy_release(&m_state->m_easyHandle, NULL); 
      errno = ENOENT;
      return -1;
    }
    else
    {
      buffer->st_size = (int64_t)length;
      if(strstr(content, "text/html")) //consider html files directories
        buffer->st_mode = _S_IFDIR;
      else
        buffer->st_mode = _S_IFREG;      
    }
  }

  g_curlInterface.easy_release(&m_state->m_easyHandle, NULL);
  return 0;
}

unsigned int CFileCurl::CReadState::Read(void* lpBuf, int64_t uiBufSize)
{
  /* only request 1 byte, for truncated reads (only if not eof) */
  if((m_fileSize == 0 || m_filePos < m_fileSize) && !FillBuffer(1))
    return 0;

  /* ensure only available data is considered */
  unsigned int want = (unsigned int)XMIN(m_buffer.GetMaxReadSize(), uiBufSize);

  /* xfer data to caller */
  if (m_buffer.ReadData((char *)lpBuf, want))
  {
    m_filePos += want;
    return want;
  }  

  /* check if we finished prematurely */
  if (!m_stillRunning && (m_fileSize == 0 || m_filePos != m_fileSize))
  {
    CLog::Log(LOGWARNING, "%s - Transfer ended before entire file was retrieved pos %"PRId64", size %"PRId64, __FUNCTION__, m_filePos, m_fileSize);
    return 0;
  }

  return 0;
}

/* use to attempt to fill the read buffer up to requested number of bytes */
bool CFileCurl::CReadState::FillBuffer(unsigned int want)
{  
  int retry=0;
  int maxfd;  
  fd_set fdread;
  fd_set fdwrite;
  fd_set fdexcep;

  DWORD tmLastDirty = CTimeUtils::GetTimeMS();
  
  // only attempt to fill buffer if transactions still running and buffer
  // doesnt exceed required size already
  while ((unsigned int)m_buffer.GetMaxReadSize() < want && m_buffer.GetMaxWriteSize() > 0 && !g_application.m_bStop)
  {
    if (m_cancelled)
      return false;

    /* if there is data in overflow buffer, try to use that first */
    if(m_overflowSize)
    {
      unsigned amount = XMIN((unsigned int)m_buffer.GetMaxWriteSize(), m_overflowSize);
      m_buffer.WriteData(m_overflowBuffer, amount);

      if(amount < m_overflowSize)
        memcpy(m_overflowBuffer, m_overflowBuffer+amount,m_overflowSize-amount);

      m_overflowSize -= amount;
      m_overflowBuffer = (char*)realloc_simple(m_overflowBuffer, m_overflowSize);
      continue;
    }

    m_bDirty = false;
    CURLMcode result = g_curlInterface.multi_perform(m_multiHandle, &m_stillRunning);
    
    DWORD now = CTimeUtils::GetTimeMS();
    if (m_bDirty)
      tmLastDirty = now;
    else if (now - tmLastDirty > 12000)
    {
      CLog::Log(LOGWARNING, "%s - no data on connection for too long. aborting operation.", __FUNCTION__);
      return false;
    }
    
    if( !m_stillRunning )
    {
      if( result == CURLM_OK )
      {
        /* if we still have stuff in buffer, we are fine */
        if( m_buffer.GetMaxReadSize() )
          return true;

        /* verify that we are actually okey */
        int msgs;
        CURLcode CURLresult = CURLE_OK;
        CURLMsg* msg;
        while((msg = g_curlInterface.multi_info_read(m_multiHandle, &msgs)))
        {
          long nRet=200;
	        g_curlInterface.easy_getinfo(msg->easy_handle, CURLINFO_RESPONSE_CODE, &nRet);
          if (nRet == 416) 
          {
            //m_seekable = false;
            return false;
          }
          
          if (msg->msg == CURLMSG_DONE)
          {
            if (msg->data.result == CURLE_OK)
              return true;

            CLog::Log(LOGDEBUG, "%s: curl failed with code %i", __FUNCTION__, msg->data.result);

            // We need to check the data.result here as we don't want to retry on every error
            if ( (msg->data.result == CURLE_OPERATION_TIMEDOUT ||
                  msg->data.result == CURLE_PARTIAL_FILE       ||
                  msg->data.result == CURLE_RECV_ERROR)        &&
                  !m_bFirstLoop)
              CURLresult=msg->data.result;
            else
              return false;
          }
        }

        // Don't retry, when we didn't "see" any error
        if (CURLresult == CURLE_OK)
          return false;

        // Close handle
        if (m_multiHandle && m_easyHandle)
          g_curlInterface.multi_remove_handle(m_multiHandle, m_easyHandle);

        // Reset all the stuff like we would in Disconnect()
        m_buffer.Clear();
          free(m_overflowBuffer);
        m_overflowBuffer = NULL;
        m_overflowSize = 0;

        // If we got here something is wrong
        if (++retry > g_advancedSettings.m_curlretries)
        {
          CLog::Log(LOGDEBUG, "%s: Reconnect failed!", __FUNCTION__);
          // Reset the rest of the variables like we would in Disconnect()
          m_filePos = 0;
          m_fileSize = 0;
          m_bufferSize = 0;

          return false;
        }

        CLog::Log(LOGDEBUG, "%s: Reconnect, (re)try %i", __FUNCTION__, retry);
        
        // On timeout, when we have to retry more than 2 times in a row
        // we increase the Curl low speed timeout, but only at start
        if (m_bFirstLoop && retry > 1 && CURLresult == CURLE_OPERATION_TIMEDOUT && m_fileSize != 0)
        {
          int newlowspeedtime;

          if (g_advancedSettings.m_curllowspeedtime<5)
            newlowspeedtime = 5;
          else
            newlowspeedtime = g_advancedSettings.m_curllowspeedtime;

          newlowspeedtime += (5*(retry-1));
          
          CLog::Log(LOGDEBUG, "%s: Setting low-speed-time to %i seconds", __FUNCTION__, newlowspeedtime);
          g_curlInterface.easy_setopt(m_easyHandle, CURLOPT_LOW_SPEED_TIME, newlowspeedtime);
        }

        // Connect + seek to current position (again)
        g_curlInterface.easy_setopt(m_easyHandle, CURLOPT_RESUME_FROM_LARGE, m_filePos);
        g_curlInterface.multi_add_handle(m_multiHandle, m_easyHandle);

        // Return to the beginning of the loop:
        continue;
      }
      return false;
    }

    // We've finished out first loop
    if(m_bFirstLoop && m_buffer.GetMaxReadSize() > 0)
    m_bFirstLoop=false;

    switch(result)
    {
      case CURLM_OK:
      {
        // hack for broken curl, that thinks there is data all the time
        // happens especially on ftp during initial connection
#ifndef _LINUX
        SwitchToThread();
#elif __APPLE__
        sched_yield();
#else
        pthread_yield();
#endif

        FD_ZERO(&fdread);
        FD_ZERO(&fdwrite);
        FD_ZERO(&fdexcep);

        // get file descriptors from the transfers
        if( CURLM_OK != g_curlInterface.multi_fdset(m_multiHandle, &fdread, &fdwrite, &fdexcep, &maxfd) || maxfd == -1 )
          return false;

        long timeout = 0;
        if(CURLM_OK != g_curlInterface.multi_timeout(m_multiHandle, &timeout) || timeout < 0 || timeout > 500)
          timeout = 200;

        if( maxfd >= 0  )
        {
          struct timeval t = { timeout / 1000, (timeout % 1000) * 1000 };          
          
          // wait until data is avialable or a timeout occours
          if( SOCKET_ERROR == dllselect(maxfd + 1, &fdread, &fdwrite, &fdexcep, &t) )
            return false;
        }
        else
          SleepEx(timeout, true);

      }
      break;
      case CURLM_CALL_MULTI_PERFORM:
      {
        // we don't keep calling here as that can easily overwrite our buffer wich we want to avoid
        // docs says we should call it soon after, but aslong as we are reading data somewhere
        // this aught to be soon enough. should stay in socket otherwise
        continue;
      }
      break;
      default:
      {
        CLog::Log(LOGERROR, "%s - curl multi perform failed with code %d, aborting", __FUNCTION__, result);
        return false;
      }
      break;
    }
  }
  return true;
}

void CFileCurl::ClearRequestHeaders()
{
  m_requestheaders.clear();
}

void CFileCurl::SetRequestHeader(CStdString header, CStdString value)
{
  m_requestheaders[header] = value;
}

void CFileCurl::SetRequestHeader(CStdString header, long value)
{
  CStdString buffer;
  buffer.Format("%ld", value);
  m_requestheaders[header] = buffer;
}

/* STATIC FUNCTIONS */
bool CFileCurl::GetHttpHeader(const CURI &url, CHttpHeader &headers)
{
  try
  {
    CFileCurl file;
    if(file.Stat(url, NULL) == 0)
    {
      headers = file.GetHttpHeader();
      return true;
    }
    return false;
  }
  catch(...)
  {
    CStdString path;
    path = url.Get();
    CLog::Log(LOGERROR, "%s - Exception thrown while trying to retrieve header url: %s", __FUNCTION__, path.c_str());
    return false;
  }
}

bool CFileCurl::GetContent(const CURI &url, CStdString &content, CStdString useragent)
{
   CFileCurl file;
   if (!useragent.IsEmpty())
     file.SetUserAgent(useragent);

   if( file.Stat(url, NULL) == 0 )
   {
     content = file.GetContent();
     return true;
   }
   
   if (file.GetLastRetCode() > 400 )
   {
	 content = "text/html";
   }
   else
   {
   content = "";
   }

   return false;
}

void CFileCurl::SetCookieJar(const CStdString &strCookieFile)
{
  m_strCookieFileName = strCookieFile;
}

