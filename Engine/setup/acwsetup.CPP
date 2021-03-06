//=============================================================================
//
// Adventure Game Studio (AGS)
//
// Copyright (C) 1999-2011 Chris Jones and 2011-20xx others
// The full list of copyright holders can be found in the Copyright.txt
// file, which is part of this source code distribution.
//
// The AGS source code is provided under the Artistic License 2.0.
// A copy of this license can be found in the file License.txt and at
// http://www.opensource.org/licenses/artistic-license-2.0.php
//
//=============================================================================

#include <windows.h>
#include <stdio.h>
#include <io.h>
#include <stdlib.h>
#include "resource/resource.h"
#include "ac/gamestructdefines.h"
#include "gfx/gfxfilter.h"
#include "util/filestream.h"

using AGS::Common::Stream;
using namespace AGS; // FIXME later

#define AL_ID(a,b,c,d)     (((a)<<24) | ((b)<<16) | ((c)<<8) | (d))

#define DIGI_DIRECTAMX(n)        AL_ID('A','X','A'+(n),' ')
// This DirectX hardware mixer is crap, it crashes the program
// when two sound effects are played at once
#define DIGI_DIRECTX(n)          AL_ID('D','X','A'+(n),' ')
#define DIGI_WAVOUTID(n)         AL_ID('W','O','A'+(n),' ')
#define DIGI_NONE  0
#define MIDI_AUTODETECT       -1 
#define MIDI_NONE             0 
#define MIDI_WIN32MAPPER         AL_ID('W','3','2','M')

extern "C" HWND allegro_wnd;
extern int  INIreadint(const char*, const char*, int=0);
extern void fgetstring_limit (char *, Stream *, int);
extern char* ac_config_file;
const char*setupstring, *enginever;
int curscrn=-1,csendto,curdigi=0,curmidi=0,windowed=0,useletterbox = 0;
GameResolutionType defaultRes = kGameResolution_Undefined;
int gameColDep = 0;
int refresh = 0, antialias = 0, reduce32to16 = 0;
int sideBorders = 1;
char curtranslation[200] = "";
char curdatadir[MAX_PATH] = ".";
char curFilter[100] = "";
char curGfxDriver[100] = "";
char gameNameForTitleBar[200] = "";
int curmaxcache = 1024 * 20, curusespeech = 1;
int wwidth, wheight;
int base_height;
GFXFilter **filterList = NULL;
const int specialFilterListIndex = 1;
const char *specialFilterName = "Max nearest-neighbour filter";
const char *specialFilterID = "max";

int get_filter_real_index(int list_index)
{
    if (list_index == specialFilterListIndex)
    {
        return -1;
    }

    return list_index > specialFilterListIndex ? list_index - 1 : list_index;    
}

int get_filter_list_index(int real_index)
{
    return real_index >= specialFilterListIndex ? real_index + 1 : real_index;
}

const char *get_filter_id(int list_index)
{
    if (list_index < 0)
    {
        return "";
    }

    int real_index = get_filter_real_index(list_index);
    return real_index >= 0 ? filterList[real_index]->GetFilterID() : specialFilterID;
}

void update_resolution_texts(HWND hDlg) {
  
  bool letterbox_by_design = INIreadint("misc", "letterbox", 0) > 0;
  Size res_size = ResolutionTypeToSize(defaultRes, letterbox_by_design);
  int resx = res_size.Width;
  int resy = res_size.Height;
  
  int selectedFilter = SendDlgItemMessage(hDlg, IDC_GFXFILTER, CB_GETCURSEL, 0, 0);

  char buffer[100];
  int thisx, thisy;

  thisx = resx;
  thisy = resy;
  selectedFilter = get_filter_real_index(selectedFilter);
  if (selectedFilter >= 0)
  {
    filterList[selectedFilter]->GetRealResolution(&thisx, &thisy);
  }
  sprintf(buffer, "Resolution: %d""x%d", thisx, thisy);
  SendDlgItemMessage (hDlg, IDC_RESOLUTION, WM_SETTEXT, 0, (LPARAM)buffer);
}

void retrieve_filter_list(bool isD3D)
{
  if (filterList != NULL)
  {
    for (int i = 0; filterList[i] != NULL; i++)
    {
      delete filterList[i];
    }
  }

  if (isD3D)
  {
    filterList = get_d3d_gfx_filter_list(true);
  }
  else
  {
    filterList = get_allegro_gfx_filter_list(true);
  }
}

void populate_drop_down_with_filters(HWND hDlg)
{
  SendDlgItemMessage(hDlg, IDC_GFXFILTER, CB_RESETCONTENT, 0, 0);

  int idx = 0;
  GFXFilter *thisFilter = filterList[idx];
  int selected_filter = 0;
  while (thisFilter != NULL) {

    if ((idx != 0) && (INIreadint("disabled", thisFilter->GetFilterID(), 0) == 1)) {
      // this filter is disabled
      delete thisFilter;
      // remove from list of filters
      for (int i = idx; filterList[i] != NULL; i++)
        filterList[i] = filterList[i + 1];

      thisFilter = filterList[idx];
      continue;
    }

    const char *filterName = thisFilter->GetVersionBoxText();
    char filterNameBuffer[100];

    if ((filterName == NULL) || (filterName[0] == 0)) {
      strcpy(filterNameBuffer, "None");
    }
    else {
      strcpy(filterNameBuffer, filterName);
      // strip trailing [
      if (filterNameBuffer[strlen(filterNameBuffer) - 1] == '[')
        filterNameBuffer[strlen(filterNameBuffer) - 1] = 0;
    }

    SendDlgItemMessage(hDlg, IDC_GFXFILTER, CB_ADDSTRING, 0, (LPARAM)filterNameBuffer);

    if (strcmp(curFilter, thisFilter->GetFilterID()) == 0)
    {
      selected_filter = idx;
    }

    idx++;
    thisFilter = filterList[idx];
  }

  SendDlgItemMessage(hDlg, IDC_GFXFILTER, CB_INSERTSTRING, specialFilterListIndex, (LPARAM)specialFilterName);
  if (stricmp(curFilter, specialFilterID) == 0)
  {
    selected_filter = specialFilterListIndex;
  }
  else
  {
    selected_filter = get_filter_list_index(selected_filter);
  }
  SendDlgItemMessage(hDlg, IDC_GFXFILTER, CB_SETCURSEL, selected_filter, 0);
}

void update_gfx_filter_box_enabled(HWND hDlg)
{
  int selDrv = SendDlgItemMessage(hDlg, IDC_GFXDRIVER, CB_GETCURSEL, 0,0);

  retrieve_filter_list(selDrv == 1);
  populate_drop_down_with_filters(hDlg);
/*
  if (selDrv == 1)
    SendDlgItemMessage(hDlg, IDC_GFXFILTER, CB_SETCURSEL, 0, 0);
  EnableWindow(GetDlgItem(hDlg, IDC_GFXFILTER), selDrv == 0);*/
  update_resolution_texts(hDlg);
}

void InitializeDialog(HWND hDlg) {
  struct _finddata_t c_file;
  long hFile;
  //FILE *fhan;
  int idx;
  retrieve_filter_list(false);

  SendDlgItemMessage(hDlg,IDC_VERSION, WM_SETTEXT, 0, (LPARAM)setupstring);

  SendDlgItemMessage(hDlg,IDC_GFXDRIVER,CB_ADDSTRING,0,(LPARAM)"DirectDraw 5");
  SendDlgItemMessage(hDlg,IDC_GFXDRIVER,CB_ADDSTRING,0,(LPARAM)"Direct3D 9");
  SendDlgItemMessage(hDlg,IDC_COMBO1,CB_RESETCONTENT,0,0);
  SendDlgItemMessage(hDlg,IDC_COMBO1,CB_ADDSTRING,0,(LPARAM)"Default DirectSound Device");
  SendDlgItemMessage(hDlg,IDC_COMBO1,CB_ADDSTRING,0,(LPARAM)"Default WaveOut Device");
  SendDlgItemMessage(hDlg,IDC_COMBO1,CB_ADDSTRING,0,(LPARAM)"No Digital Sound");
  SendDlgItemMessage(hDlg,IDC_COMBO1,CB_ADDSTRING,0,(LPARAM)"DirectSound (Hardware mixer)");
  SendDlgItemMessage(hDlg,IDC_COMBO1,CB_SETTOPINDEX,0,0);
  SendDlgItemMessage(hDlg,IDC_COMBO1,CB_SETCURSEL,curdigi,0);
  SendDlgItemMessage(hDlg,IDC_COMBO2,CB_ADDSTRING,0,(LPARAM)"Default MCI Music Device");
  SendDlgItemMessage(hDlg,IDC_COMBO2,CB_ADDSTRING,0,(LPARAM)"Disable music");
  SendDlgItemMessage(hDlg,IDC_COMBO2,CB_ADDSTRING,0,(LPARAM)"Win32 MIDI Mapper");
  SendDlgItemMessage(hDlg,IDC_COMBO2,CB_SETTOPINDEX,0,0);
  SendDlgItemMessage(hDlg,IDC_COMBO2,CB_SETCURSEL,curmidi,0);
  SendDlgItemMessage(hDlg,IDC_COMBO3,CB_ADDSTRING,0,(LPARAM)"Game Default");
  SendDlgItemMessage(hDlg,IDC_COMBO3,CB_SETCURSEL,0,0);
  idx = 1;

  char translationFindSpec[255];
  sprintf(translationFindSpec, "%s\\*.tra", curdatadir);
  // Find and add all translations in folder
  if ((hFile = _findfirst(translationFindSpec, &c_file )) != -1L ) {
    do {
      if (c_file.name[strlen(c_file.name)-4] == '.') {
        // it actually returns *.tra* so make sure it's a .TRA file,
        // then add to our list
        c_file.name[0] = toupper (c_file.name[0]);
        c_file.name[strlen(c_file.name)-4] = 0;
        int needToSet = 0;
        // if this is the translation we're using, set it
        if (stricmp (curtranslation, c_file.name) == 0)
          needToSet = 1;
        strcat (c_file.name, " translation");
        SendDlgItemMessage(hDlg,IDC_COMBO3,CB_ADDSTRING,0,(LPARAM)c_file.name);

        if (needToSet)
          SendDlgItemMessage (hDlg, IDC_COMBO3, CB_SETCURSEL, idx, 0);
        idx++;
      }
    } while( _findnext( hFile, &c_file ) == 0 );

    _findclose( hFile );
  }
  SendDlgItemMessage(hDlg,IDC_COMBO4,CB_ADDSTRING,0,(LPARAM)"10 MB");
  SendDlgItemMessage(hDlg,IDC_COMBO4,CB_ADDSTRING,0,(LPARAM)"20 MB (default)");
  SendDlgItemMessage(hDlg,IDC_COMBO4,CB_ADDSTRING,0,(LPARAM)"50 MB");
  SendDlgItemMessage(hDlg,IDC_COMBO4,CB_ADDSTRING,0,(LPARAM)"100 MB");
  idx = 0;
  if (curmaxcache >= 100*1024)
    idx = 3;
  else if (curmaxcache >= 50*1024)
    idx = 2;
  else if (curmaxcache >= 20*1024)
    idx = 1;
    
  SendDlgItemMessage(hDlg,IDC_COMBO4,CB_SETCURSEL,idx,0);

  populate_drop_down_with_filters(hDlg);

  if (stricmp(curGfxDriver, "D3D9") == 0)
    SendDlgItemMessage(hDlg, IDC_GFXDRIVER, CB_SETCURSEL, 1, 0);
  else
    SendDlgItemMessage(hDlg, IDC_GFXDRIVER, CB_SETCURSEL, 0, 0);
  update_gfx_filter_box_enabled(hDlg);

  if (windowed > 0)
    SendDlgItemMessage(hDlg,IDC_WINDOWED,BM_SETCHECK,BST_CHECKED,0);

  if (refresh > 0)
    SendDlgItemMessage(hDlg,IDC_REFRESH,BM_SETCHECK,BST_CHECKED,0);

  if (antialias > 0)
    SendDlgItemMessage(hDlg,IDC_ANTIALIAS,BM_SETCHECK,BST_CHECKED,0);

  if (curusespeech > 0)
    SendDlgItemMessage(hDlg,IDC_SPEECHPACK,BM_SETCHECK,BST_CHECKED,0);

  if (sideBorders > 0)
    SendDlgItemMessage(hDlg, IDC_SIDEBORDERS, BM_SETCHECK, BST_CHECKED, 0);

  if (useletterbox > 0)
    SendDlgItemMessage(hDlg, IDC_LETTERBOX,BM_SETCHECK,BST_CHECKED,0);

  // If the game isn't 32-bit, disable it
  if (gameColDep < 32)
    EnableWindow (GetDlgItem (hDlg, IDC_REDUCESPR), FALSE);
  else if (reduce32to16 > 0)
    SendDlgItemMessage(hDlg, IDC_REDUCESPR, BM_SETCHECK, BST_CHECKED,0);

  // if no speech pack, disable the checkbox
  if (!Common::File::TestReadFile("speech.vox"))
    EnableWindow (GetDlgItem (hDlg, IDC_SPEECHPACK), FALSE);

  if (INIreadint("disabled", "speechvox", 0) == 1)
    EnableWindow (GetDlgItem (hDlg, IDC_SPEECHPACK), FALSE);

  if (INIreadint("disabled", "16bit", 0) == 1)
    EnableWindow (GetDlgItem (hDlg, IDC_REDUCESPR), FALSE);

  if (INIreadint("disabled", "filters", 0) == 1)
    EnableWindow (GetDlgItem(hDlg, IDC_GFXFILTER), FALSE);

  RECT win_rect, client_rect;
  GetWindowRect (hDlg, &win_rect);
  wwidth = win_rect.right - win_rect.left;
  wheight = win_rect.bottom - win_rect.top;
  GetClientRect(hDlg, &client_rect);
  const int cl_height_diff = wheight - (client_rect.bottom - client_rect.top);

  RECT closed_dlg_unit_rect;
  ZeroMemory(&closed_dlg_unit_rect, sizeof(closed_dlg_unit_rect));
  closed_dlg_unit_rect.bottom = 121;
  MapDialogRect(hDlg, &closed_dlg_unit_rect);
  base_height = closed_dlg_unit_rect.bottom - closed_dlg_unit_rect.top + cl_height_diff;

  MoveWindow (hDlg, max(0, win_rect.left), max(0, win_rect.top + (wheight - base_height) / 2), wwidth, base_height, TRUE);
  update_resolution_texts (hDlg);

  SendMessage(hDlg, WM_SETTEXT, NULL, (LPARAM)gameNameForTitleBar);
  SendMessage(allegro_wnd, WM_SETTEXT, NULL, (LPARAM)gameNameForTitleBar);
}

// handler for Settings dialog
LRESULT CALLBACK callback_settings(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam) {
  char tbuf[200];
  int idx = 1;
  //sprintf(tbuf,"event %d wp %d lp %d",message,wParam,lParam);
  switch (message) {
    case WM_INITDIALOG:
      
      InitializeDialog(hDlg);
      return TRUE;

    case WM_COMMAND:

      if (LOWORD(wParam) == IDC_ADVANCED) {
        // Reveal the advanced bit of the window
        RECT winsize;
        GetWindowRect (hDlg, &winsize);

        ShowWindow (GetDlgItem(hDlg, IDC_ADVANCED), SW_HIDE);
        ShowWindow (GetDlgItem(hDlg, IDC_STATICADV), SW_SHOW);
        ShowWindow (GetDlgItem(hDlg, IDC_STATICADV2), SW_SHOW);
        ShowWindow (GetDlgItem(hDlg, IDC_STATICADV3), SW_SHOW);
        ShowWindow (GetDlgItem(hDlg, IDC_COMBO1), SW_SHOW);
        ShowWindow (GetDlgItem(hDlg, IDC_COMBO2), SW_SHOW);
        ShowWindow (GetDlgItem(hDlg, IDC_SPEECHPACK), SW_SHOW);
        ShowWindow (GetDlgItem(hDlg, IDC_REFRESH), SW_SHOW);
        ShowWindow (GetDlgItem(hDlg, IDC_SIDEBORDERS), SW_SHOW);
        ShowWindow (GetDlgItem(hDlg, IDC_ANTIALIAS), SW_SHOW);
        ShowWindow (GetDlgItem(hDlg, IDC_REDUCESPR), SW_SHOW);
        ShowWindow (GetDlgItem(hDlg, IDC_LETTERBOX), SW_SHOW);
        ShowWindow (GetDlgItem(hDlg, IDC_COMBO4), SW_SHOW);

        MoveWindow (hDlg, max(0, winsize.left), max(0, winsize.top - (wheight - base_height) / 2), wwidth, wheight, TRUE);
        //SetActiveWindow (GetDlgItem(hDlg, IDOK));
        SetFocus (GetDlgItem(hDlg, IDOK));
      }
      else if ((LOWORD(wParam) == IDC_LETTERBOX) || (LOWORD(wParam) == IDC_GFXFILTER)) {
        update_resolution_texts(hDlg);
      }

      if (LOWORD(wParam) == IDC_GFXDRIVER)
      {
        update_gfx_filter_box_enabled(hDlg);
      }
      else if (LOWORD(wParam) == IDC_GFXFILTER)
      {
        int curSel = SendDlgItemMessage(hDlg, IDC_GFXFILTER, CB_GETCURSEL, 0, 0);
        const char *filter_id = get_filter_id(curSel);
        strcpy(curFilter, filter_id);
      }

      if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL || LOWORD(wParam) == IDOKRUN) {
        if ((LOWORD(wParam) == IDOK) || (LOWORD(wParam) == IDOKRUN)) {

          csendto = 0;

          if (IsHiRes(defaultRes)) 
          {
            csendto = 1;
          }

          sprintf(tbuf,"%d",csendto);
          if (!WritePrivateProfileString("misc","screenres",tbuf,ac_config_file)) 
          {
            DWORD errCode = GetLastError();
            sprintf(tbuf, "Unable to write to the configuration file (error code 0x%08X). If you are using Windows Vista, you may need to right-click and Run as Administrator on the Setup application.", errCode);
            MessageBox(hDlg, tbuf, "Save error", MB_OK | MB_ICONEXCLAMATION);
            return TRUE;
          }
          sprintf(tbuf,"-1");
          WritePrivateProfileString("sound","digiid",tbuf,ac_config_file);
          WritePrivateProfileString("sound","midiid",tbuf,ac_config_file);
          sprintf(tbuf,"0");
          WritePrivateProfileString("sound","digiindx",tbuf,ac_config_file);
          WritePrivateProfileString("sound","midiindx",tbuf,ac_config_file);
          int idx=SendDlgItemMessage(hDlg,IDC_COMBO1,CB_GETCURSEL,0,0);
          sprintf(tbuf,"%d",idx);
          WritePrivateProfileString("sound","digiwinindx",tbuf,ac_config_file);
          if (idx == 0)
            idx = DIGI_DIRECTAMX(0);
          else if (idx == 1)
            idx = DIGI_WAVOUTID(0);
          else if (idx == 3) 
            idx = DIGI_DIRECTX(0);
          else idx = DIGI_NONE;
          sprintf(tbuf,"%d",idx);
          WritePrivateProfileString("sound","digiwin",tbuf,ac_config_file);

          idx=SendDlgItemMessage(hDlg,IDC_COMBO2,CB_GETCURSEL,0,0);
          sprintf(tbuf,"%d",idx);
          WritePrivateProfileString("sound","midiwinindx",tbuf,ac_config_file);
          if (idx == 0)
            idx = MIDI_AUTODETECT;
          else if (idx == 1)
            idx = MIDI_NONE;
          else idx = MIDI_WIN32MAPPER;
          sprintf(tbuf,"%d",idx);
          WritePrivateProfileString("sound","midiwin",tbuf,ac_config_file);

          idx = SendDlgItemMessage(hDlg,IDC_COMBO3,CB_GETCURSEL,0,0);
          if (idx > 0) {
            SendDlgItemMessage(hDlg,IDC_COMBO3,CB_GETLBTEXT,idx,(LPARAM)curtranslation);
            // cut the end off
            strstr (curtranslation, " translation")[0] = 0;
          }
          else
            curtranslation[0] = 0;

          WritePrivateProfileString ("language", "translation",
              (idx == 0) ? NULL : curtranslation, ac_config_file);

          idx = SendDlgItemMessage(hDlg,IDC_COMBO4,CB_GETCURSEL,0,0);
          SendDlgItemMessage(hDlg,IDC_COMBO4,CB_GETLBTEXT,idx,(LPARAM)tbuf);
          idx = atoi (tbuf);
          sprintf (tbuf, "%d", idx*1024);
          char *toWrite = &tbuf[0];
          // if it's the default, erase from config file
          if (idx == 20)
            toWrite = NULL;
          WritePrivateProfileString("misc","cachemax", toWrite,ac_config_file);

          if (SendDlgItemMessage(hDlg,IDC_SPEECHPACK,BM_GETCHECK,0,0) == BST_CHECKED)
            WritePrivateProfileString ("sound", "usespeech", "1", ac_config_file);
          else
            WritePrivateProfileString ("sound", "usespeech", "0", ac_config_file);

          if (SendDlgItemMessage(hDlg,IDC_WINDOWED,BM_GETCHECK,0,0) == BST_CHECKED)
            WritePrivateProfileString("misc","windowed","1",ac_config_file);
          else
            WritePrivateProfileString("misc","windowed","0",ac_config_file);

          if (SendDlgItemMessage(hDlg,IDC_ANTIALIAS,BM_GETCHECK,0,0) == BST_CHECKED)
            WritePrivateProfileString("misc","antialias","1",ac_config_file);
          else
            WritePrivateProfileString("misc","antialias",NULL,ac_config_file);

          if (SendDlgItemMessage(hDlg, IDC_SIDEBORDERS, BM_GETCHECK,0,0) == BST_CHECKED)
            WritePrivateProfileString("misc","prefer_sideborders","1",ac_config_file);
          else
            WritePrivateProfileString("misc","prefer_sideborders","0",ac_config_file);

          if (SendDlgItemMessage(hDlg,IDC_REFRESH,BM_GETCHECK,0,0) == BST_CHECKED)
            WritePrivateProfileString("misc","refresh","85",ac_config_file);
          else
            WritePrivateProfileString("misc","refresh","0",ac_config_file);

          if (SendDlgItemMessage(hDlg,IDC_LETTERBOX,BM_GETCHECK,0,0) == BST_CHECKED)
            WritePrivateProfileString ("misc", "prefer_letterbox", "1", ac_config_file);
          else
            WritePrivateProfileString ("misc", "prefer_letterbox", "0", ac_config_file);

          if (SendDlgItemMessage(hDlg, IDC_REDUCESPR, BM_GETCHECK,0,0) == BST_CHECKED)
            WritePrivateProfileString ("misc", "notruecolor", "1", ac_config_file);
          else
            WritePrivateProfileString ("misc", "notruecolor", NULL, ac_config_file);

          if (SendDlgItemMessage(hDlg, IDC_GFXDRIVER, CB_GETCURSEL,0,0) == 0)
            WritePrivateProfileString ("misc", "gfxdriver", "DX5", ac_config_file);
          else
            WritePrivateProfileString ("misc", "gfxdriver", "D3D9", ac_config_file);

          idx = SendDlgItemMessage(hDlg, IDC_GFXFILTER, CB_GETCURSEL, 0, 0);
          const char *filter_id = get_filter_id(idx);
          WritePrivateProfileString("misc", "gfxfilter", filter_id, ac_config_file);
        }
	EndDialog(hDlg, LOWORD(wParam));
 	return TRUE;
      }
      return FALSE;
    default: return FALSE;
    }
  return TRUE;
  }


int acwsetup(const char*vername, const char*enbuild) {
  setupstring = vername;
  enginever = enbuild;
  if (Common::File::TestReadFile(ac_config_file)) {
    curscrn=INIreadint("misc","screenres");
    if (curscrn > 1)
      curscrn = 1;
    defaultRes = (GameResolutionType)INIreadint ("misc", "defaultres", 0);
    if (defaultRes < 1)
      defaultRes = kGameResolution_Default;

    curdigi=INIreadint("sound","digiwinindx");
    curmidi=INIreadint("sound","midiwinindx");

    if (curdigi < 0) curdigi=0;
    if (curdigi > 3) curdigi = 0;
    if (curmidi < 0) curmidi=0;

    windowed = INIreadint("misc","windowed",0);
    useletterbox = INIreadint("misc","prefer_letterbox", 1) != 0;

    reduce32to16 = INIreadint("misc","notruecolor",0);
    if (reduce32to16 < 0)
      reduce32to16 = 0;

    gameColDep = INIreadint("misc", "gamecolordepth", 0);
    if (gameColDep < 0)
      gameColDep = 0;

    refresh = INIreadint("misc","refresh",0);
    if (refresh < 1)
      refresh = 0;
    antialias = INIreadint("misc","antialias",0);
    if (antialias < 1)
      antialias = 0;

    sideBorders = INIreadint("misc","prefer_sideborders", 1) != 0;

    int cacheval = INIreadint ("misc", "cachemax", 0);
    if (cacheval > 0)
      curmaxcache = cacheval;
    curusespeech = INIreadint ("sound", "usespeech", 0);
    if (curusespeech < 0)
      curusespeech = 1;

    GetPrivateProfileString("language", "translation", "", curtranslation, 200, ac_config_file);
    GetPrivateProfileString("misc", "datadir", ".", curdatadir, 255, ac_config_file);
    GetPrivateProfileString("misc", "gfxfilter", "None", curFilter, 100, ac_config_file);
    GetPrivateProfileString("misc", "gfxdriver", "D3D9", curGfxDriver, 100, ac_config_file);
    GetPrivateProfileString("misc", "titletext", "Game Setup", gameNameForTitleBar, 200, ac_config_file);
  }
  if (DialogBox(GetModuleHandle(NULL), (LPCTSTR)IDD_DIALOG1,allegro_wnd,(DLGPROC)callback_settings) == IDOKRUN)
    return 1;
  return 0;
}

void set_icon() {
  SetClassLong(allegro_wnd,GCL_HICON,
    (LONG) LoadIcon(GetModuleHandle(NULL), MAKEINTRESOURCE(IDI_ICON2)) ); 
}
