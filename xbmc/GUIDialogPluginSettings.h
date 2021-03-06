#ifndef GUIDIALOG_PLUGIN_SETTINGS_
#define GUIDIALOG_PLUGIN_SETTINGS_

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

#include "GUIDialogBoxBase.h"
#include "PluginSettings.h"

struct SScraperInfo;

class CGUIDialogPluginSettings : public CGUIDialogBoxBase
{
public:
  CGUIDialogPluginSettings(void);
  virtual ~CGUIDialogPluginSettings(void);
  virtual bool OnMessage(CGUIMessage& message);
  virtual bool OnAction(const CAction &action);

  void SetSettings(const CBasicSettings &settings);
  CStdString GetValue(const CStdString &strKey);

  static void ShowAndGetInput(CURI& url);
  static void ShowAndGetInput(SScraperInfo& info);
  static void ShowAndGetInput(CStdString& path);

  // currently generate string at the format:
  // <settings>
  // <setting> id="" type="text" label="" />  - for strings
  // <setting> id="" type="bool" value="" />  - for numric
  // we can extend this to dynamic args list if it will be necessary
  CStdString GenerateXmlSettingsStr(void);


protected:
  virtual void OnInitWindow();

private:
  void CreateControls();
  void FreeControls();
  void EnableControls();
  void SetDefaults();
  bool GetCondition(const CStdString &condition, const int controlId);
  
  bool SaveSettings(void);
  bool ShowVirtualKeyboard(int iControl);
  static CURI m_url;
  bool TranslateSingleString(const CStdString &strCondition, std::vector<CStdString> &enableVec);
  CBasicSettings m_settings;
  CStdString m_strHeading;
  
  std::map<CStdString,CStdString> dialogValuesMap;
  std::map<CStdString,CStdString> m_buttonValues;
};

#endif

