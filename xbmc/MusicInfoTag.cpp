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

#include "MusicInfoTag.h"
#include "Album.h"
#include "utils/log.h"
#include "utils/Variant.h"

using namespace MUSIC_INFO;

CMusicInfoTag::CMusicInfoTag(void)
{
  Clear();
}

CMusicInfoTag::CMusicInfoTag(const CMusicInfoTag& tag) : IArchivable(tag), ISerializable(tag)
{
  *this = tag;
}

CMusicInfoTag::~CMusicInfoTag()
{}

const CMusicInfoTag& CMusicInfoTag::operator =(const CMusicInfoTag& tag)
{
  if (this == &tag) return * this;

  m_strURL = tag.m_strURL;
  m_strArtist = tag.m_strArtist;
  m_strAlbumArtist = tag.m_strAlbumArtist;
  m_strAlbum = tag.m_strAlbum;
  m_strGenre = tag.m_strGenre;
  m_strTitle = tag.m_strTitle;
  m_strMusicBrainzTrackID = tag.m_strMusicBrainzTrackID;
  m_strMusicBrainzArtistID = tag.m_strMusicBrainzArtistID;
  m_strMusicBrainzAlbumID = tag.m_strMusicBrainzAlbumID;
  m_strMusicBrainzAlbumArtistID = tag.m_strMusicBrainzAlbumArtistID;
  m_strMusicBrainzTRMID = tag.m_strMusicBrainzTRMID;
  m_strComment = tag.m_strComment;
  m_strLyrics = tag.m_strLyrics;
  m_iDuration = tag.m_iDuration;
  m_iTrack = tag.m_iTrack;
  m_bLoaded = tag.m_bLoaded;
  m_rating = tag.m_rating;
  m_iDbId = tag.m_iDbId;
  memcpy(&m_dwReleaseDate, &tag.m_dwReleaseDate, sizeof(m_dwReleaseDate) );
  return *this;
}

bool CMusicInfoTag::operator !=(const CMusicInfoTag& tag) const
{
  if (this == &tag) return false;
  if (m_strURL != tag.m_strURL) return true;
  if (m_strTitle != tag.m_strTitle) return true;
  if (m_strArtist != tag.m_strArtist) return true;
  if (m_strAlbumArtist != tag.m_strAlbumArtist) return true;
  if (m_strAlbum != tag.m_strAlbum) return true;
  if (m_iDuration != tag.m_iDuration) return true;
  if (m_iTrack != tag.m_iTrack) return true;
  return false;
}

int CMusicInfoTag::GetTrackNumber() const
{
  return (m_iTrack & 0xffff);
}

int CMusicInfoTag::GetDiscNumber() const
{
  return (m_iTrack >> 16);
}

int CMusicInfoTag::GetTrackAndDiskNumber() const
{
  return m_iTrack;
}

int CMusicInfoTag::GetDuration() const
{
  return m_iDuration;
}

const CStdString& CMusicInfoTag::GetTitle() const
{
  return m_strTitle;
}

const CStdString& CMusicInfoTag::GetURL() const
{
  return m_strURL;
}

const CStdString& CMusicInfoTag::GetArtist() const
{
  return m_strArtist;
}

const CStdString& CMusicInfoTag::GetAlbum() const
{
  return m_strAlbum;
}

const CStdString& CMusicInfoTag::GetAlbumArtist() const
{
  return m_strAlbumArtist;
}

const CStdString& CMusicInfoTag::GetGenre() const
{
  return m_strGenre;
}

void CMusicInfoTag::GetReleaseDate(SYSTEMTIME& dateTime) const
{
  memcpy(&dateTime, &m_dwReleaseDate, sizeof(m_dwReleaseDate) );
}

int CMusicInfoTag::GetYear() const
{
  return m_dwReleaseDate.wYear;
}

long CMusicInfoTag::GetDatabaseId() const
{
  return m_iDbId;
}

CStdString CMusicInfoTag::GetYearString() const
{
  CStdString strReturn;
  strReturn.Format("%i", m_dwReleaseDate.wYear);
  return m_dwReleaseDate.wYear ? strReturn : "";
}

const CStdString &CMusicInfoTag::GetComment() const
{
  return m_strComment;
}

const CStdString &CMusicInfoTag::GetLyrics() const
{
  return m_strLyrics;
}

char CMusicInfoTag::GetRating() const
{
  return m_rating;
}

void CMusicInfoTag::SetURL(const CStdString& strURL)
{
  m_strURL = strURL;
}

void CMusicInfoTag::SetTitle(const CStdString& strTitle)
{
  m_strTitle = strTitle;
  m_strTitle.TrimLeft(" ");
  m_strTitle.TrimRight(" ");
  m_strTitle.TrimRight("\n");
  m_strTitle.TrimRight("\r");
}

void CMusicInfoTag::SetArtist(const CStdString& strArtist)
{
  m_strArtist = strArtist;
  m_strArtist.TrimLeft(" ");
  m_strArtist.TrimRight(" ");
  m_strArtist.TrimRight("\n");
  m_strArtist.TrimRight("\r");
}

void CMusicInfoTag::SetAlbum(const CStdString& strAlbum)
{
  m_strAlbum = strAlbum;
  m_strAlbum.TrimLeft(" ");
  m_strAlbum.TrimRight(" ");
  m_strAlbum.TrimRight("\n");
  m_strAlbum.TrimRight("\r");
}

void CMusicInfoTag::SetAlbumArtist(const CStdString& strAlbumArtist)
{
  m_strAlbumArtist = strAlbumArtist;
  m_strAlbumArtist.TrimLeft(" ");
  m_strAlbumArtist.TrimRight(" ");
  m_strAlbumArtist.TrimRight("\n");
  m_strAlbumArtist.TrimRight("\r");
}

void CMusicInfoTag::SetGenre(const CStdString& strGenre)
{
  m_strGenre = strGenre;
  m_strGenre.TrimLeft(" ");
  m_strGenre.TrimRight(" ");
  m_strGenre.TrimRight("\n");
  m_strGenre.TrimRight("\r");
}

void CMusicInfoTag::SetYear(int year)
{
  memset(&m_dwReleaseDate, 0, sizeof(m_dwReleaseDate) );
  m_dwReleaseDate.wYear = year;
}

void CMusicInfoTag::SetDatabaseId(long id)
{
  m_iDbId = id;
}

void CMusicInfoTag::SetReleaseDate(SYSTEMTIME& dateTime)
{
  memcpy(&m_dwReleaseDate, &dateTime, sizeof(m_dwReleaseDate) );
}

void CMusicInfoTag::SetTrackNumber(int iTrack)
{
  m_iTrack = (m_iTrack & 0xffff0000) | (iTrack & 0xffff);
}

void CMusicInfoTag::SetPartOfSet(int iPartOfSet)
{
  m_iTrack = (m_iTrack & 0xffff) | (iPartOfSet << 16);
}

void CMusicInfoTag::SetTrackAndDiskNumber(int iTrackAndDisc)
{
  m_iTrack=iTrackAndDisc;
}

void CMusicInfoTag::SetDuration(int iSec)
{
  m_iDuration = iSec;
}

void CMusicInfoTag::SetComment(const CStdString& comment)
{
  m_strComment = comment;
}

void CMusicInfoTag::SetLyrics(const CStdString& lyrics)
{
  m_strLyrics = lyrics;
}

void CMusicInfoTag::SetRating(char rating)
{
  m_rating = rating;
}

void CMusicInfoTag::SetLoaded(bool bOnOff)
{
  m_bLoaded = bOnOff;
}

bool CMusicInfoTag::Loaded() const
{
  return m_bLoaded;
}

const CStdString& CMusicInfoTag::GetMusicBrainzTrackID() const
{
  return m_strMusicBrainzTrackID;
}

const CStdString& CMusicInfoTag::GetMusicBrainzArtistID() const
{
  return m_strMusicBrainzArtistID;
}

const CStdString& CMusicInfoTag::GetMusicBrainzAlbumID() const
{
  return m_strMusicBrainzAlbumID;
}

const CStdString& CMusicInfoTag::GetMusicBrainzAlbumArtistID() const
{
  return m_strMusicBrainzAlbumArtistID;
}

const CStdString& CMusicInfoTag::GetMusicBrainzTRMID() const
{
  return m_strMusicBrainzTRMID;
}

void CMusicInfoTag::SetMusicBrainzTrackID(const CStdString& strTrackID)
{
  m_strMusicBrainzTrackID=strTrackID;
}

void CMusicInfoTag::SetMusicBrainzArtistID(const CStdString& strArtistID)
{
  m_strMusicBrainzArtistID=strArtistID;
}

void CMusicInfoTag::SetMusicBrainzAlbumID(const CStdString& strAlbumID)
{
  m_strMusicBrainzAlbumID=strAlbumID;
}

void CMusicInfoTag::SetMusicBrainzAlbumArtistID(const CStdString& strAlbumArtistID)
{
  m_strMusicBrainzAlbumArtistID=strAlbumArtistID;
}

void CMusicInfoTag::SetMusicBrainzTRMID(const CStdString& strTRMID)
{
  m_strMusicBrainzTRMID=strTRMID;
}

void CMusicInfoTag::SetAlbum(const CAlbum& album)
{
  SetArtist(album.strArtist);
  SetAlbum(album.strAlbum);
  SetAlbumArtist(album.strArtist);
  SetGenre(album.strGenre);
  SetRating('0' + (album.iRating + 1) / 2);
  SYSTEMTIME stTime;
  stTime.wYear = album.iYear;
  SetReleaseDate(stTime);
  m_iDbId = album.idAlbum;
  m_bLoaded = true;
}

void CMusicInfoTag::SetSong(const CSong& song)
{
  SetTitle(song.strTitle);
  SetGenre(song.strGenre);
  SetArtist(song.strArtist);
  SetAlbum(song.strAlbum);
  SetAlbumArtist(song.strAlbumArtist);
  SetMusicBrainzTrackID(song.strMusicBrainzTrackID);
  SetMusicBrainzArtistID(song.strMusicBrainzArtistID);
  SetMusicBrainzAlbumID(song.strMusicBrainzAlbumID);
  SetMusicBrainzAlbumArtistID(song.strMusicBrainzAlbumArtistID);
  SetMusicBrainzTRMID(song.strMusicBrainzTRMID);
  SetComment(song.strComment);
  m_rating = song.rating;
  m_strURL = song.strFileName;
  SYSTEMTIME stTime;
  stTime.wYear = song.iYear;
  SetReleaseDate(stTime);
  m_iTrack = song.iTrack;
  m_iDuration = song.iDuration;
  m_iDbId = song.idSong;
  m_bLoaded = true;
}

void CMusicInfoTag::Dump() const
{
  CLog::Log(LOGDEBUG,"** Music Info Tag:");
  CLog::Log(LOGDEBUG,"Title: %s", m_strTitle.c_str());
  CLog::Log(LOGDEBUG,"Artist: %s", m_strArtist.c_str());
  CLog::Log(LOGDEBUG,"Album: %s", m_strAlbum.c_str());
  CLog::Log(LOGDEBUG,"Album Artist: %s", m_strAlbumArtist.c_str());
  CLog::Log(LOGDEBUG,"URL: %s", m_strURL.c_str());
  CLog::Log(LOGDEBUG,"Genre: %s", m_strGenre.c_str());
  CLog::Log(LOGDEBUG,"Comment: %s", m_strComment.c_str());
  CLog::Log(LOGDEBUG,"rating: %c", m_rating);
  CLog::Log(LOGDEBUG,"Duration: %d", m_iDuration);
  CLog::Log(LOGDEBUG,"Track: %d", m_iTrack);
  CLog::Log(LOGDEBUG,"Loaded: %s", m_bLoaded?"true":"false");
  CLog::Log(LOGDEBUG,"Release date: %d", m_dwReleaseDate.wYear);
  CLog::Log(LOGDEBUG,"Music Brainz TrackID: %s", m_strMusicBrainzTrackID.c_str());
  CLog::Log(LOGDEBUG,"Music Brainz m_strMusicBrainzArtistID: %s", m_strMusicBrainzArtistID.c_str());
  CLog::Log(LOGDEBUG,"Music Brainz m_strMusicBrainzAlbumID: %s", m_strMusicBrainzAlbumID.c_str());
  CLog::Log(LOGDEBUG,"Music Brainz m_strMusicBrainzAlbumArtistID: %s", m_strMusicBrainzAlbumArtistID.c_str());
  CLog::Log(LOGDEBUG,"Music Brainz m_strMusicBrainzTRMID: %s", m_strMusicBrainzTRMID.c_str());
}

void CMusicInfoTag::Archive(CArchive& ar)
{
  if (ar.IsStoring())
  {
    ar << m_strURL;
    ar << m_strTitle;
    ar << m_strArtist;
    ar << m_strAlbum;
    ar << m_strAlbumArtist;
    ar << m_strGenre;
    ar << m_iDuration;
    ar << m_iTrack;
    ar << m_bLoaded;
    ar << m_dwReleaseDate;
    ar << m_strMusicBrainzTrackID;
    ar << m_strMusicBrainzArtistID;
    ar << m_strMusicBrainzAlbumID;
    ar << m_strMusicBrainzAlbumArtistID;
    ar << m_strMusicBrainzTRMID;
    ar << m_strComment;
    ar << m_rating;
  }
  else
  {
    ar >> m_strURL;
    ar >> m_strTitle;
    ar >> m_strArtist;
    ar >> m_strAlbum;
    ar >> m_strAlbumArtist;
    ar >> m_strGenre;
    ar >> m_iDuration;
    ar >> m_iTrack;
    ar >> m_bLoaded;
    ar >> m_dwReleaseDate;
    ar >> m_strMusicBrainzTrackID;
    ar >> m_strMusicBrainzArtistID;
    ar >> m_strMusicBrainzAlbumID;
    ar >> m_strMusicBrainzAlbumArtistID;
    ar >> m_strMusicBrainzTRMID;
    ar >> m_strComment;
    ar >> m_rating;
 }
}

void CMusicInfoTag::Serialize(CVariant& value)
{
  value["url"] = m_strURL;
  value["title"] = m_strTitle;
  value["artist"] = m_strArtist;
  value["album"] = m_strAlbum;
  value["albumartist"] = m_strAlbumArtist;
  value["genre"] = m_strGenre;
  value["duration"] = m_iDuration;
  value["track"] = m_iTrack;
  value["loaded"] = m_bLoaded;
  value["year"] = m_dwReleaseDate.wYear;
  value["musicbrainztrackid"] = m_strMusicBrainzTrackID;
  value["musicbrainzartistid"] = m_strMusicBrainzArtistID;
  value["musicbrainzalbumid"] = m_strMusicBrainzAlbumID;
  value["musicbrainzalbumartistid"] = m_strMusicBrainzAlbumArtistID;
  value["musicbrainztrmid"] = m_strMusicBrainzTRMID;
  value["comment"] = m_strComment;
  value["rating"] = m_rating;
}

void CMusicInfoTag::Clear()
{
  m_strURL.Empty();
  m_strArtist.Empty();
  m_strAlbum.Empty();
  m_strAlbumArtist.Empty();
  m_strGenre.Empty();
  m_strTitle.Empty();
  m_strMusicBrainzTrackID.Empty();
  m_strMusicBrainzArtistID.Empty();
  m_strMusicBrainzAlbumID.Empty();
  m_strMusicBrainzAlbumArtistID.Empty();
  m_strMusicBrainzTRMID.Empty();
  m_iDuration = 0;
  m_iTrack = 0;
  m_bLoaded = false;
  m_strComment.Empty();
  m_rating = '0';
  m_iDbId = -1;
  memset(&m_dwReleaseDate, 0, sizeof(m_dwReleaseDate) );
}
