/*
 * PROJECT:    PowerNote
 * LICENSE:    LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:    Providing a Windows-compatible simple text editor for ReactOS
 * COPYRIGHT:  Copyright 1998,99 Marcel Baur <mbaur@g26.ethz.ch>
 *             Copyright 2002 Sylvain Petreolle <spetreolle@yahoo.fr>
 *             Copyright 2002 Andriy Palamarchuk
 *             Copyright 2023 Katayama Hirofumi MZ <katayama.hirofumi.mz@gmail.com>
 */

#include "notepad.h"

#include <assert.h>
#include <commctrl.h>
#include <windowsx.h>
#include <strsafe.h>
#include "regex_engine.h"

LRESULT CALLBACK EDIT_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

static const TCHAR helpfile[] = _T("notepad.hlp");
static const TCHAR empty_str[] = _T("");
static const TCHAR szDefaultExt[] = _T("txt");
static const TCHAR txt_files[] = _T("*.txt");

/* Status bar parts index */
#define SBPART_CURPOS   0
#define SBPART_EOLN     1
#define SBPART_ENCODING 2

/* Line endings - string resource ID mapping table */
static UINT EolnToStrId[] = {
    IDS_CRLF,
    IDS_LF,
    IDS_CR
};

/* Encoding - string resource ID mapping table */
static UINT EncToStrId[] = {
    IDS_ANSI,
    IDS_UNICODE,
    IDS_UNICODE_BE,
    IDS_UTF8,
    IDS_UTF8_BOM
};

VOID ShowLastError(VOID)
{
    DWORD error = GetLastError();
    if (error != NO_ERROR)
    {
        LPTSTR lpMsgBuf = NULL;
        TCHAR szTitle[MAX_STRING_LEN];
        TCHAR szFallback[42], *pszMessage = szFallback;

        LoadString(Globals.hInstance, IDS_ERROR, szTitle, _countof(szTitle));

        FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM,
                      NULL,
                      error,
                      0,
                      (LPTSTR) &lpMsgBuf,
                      0,
                      NULL);

        if (lpMsgBuf)
            pszMessage = lpMsgBuf;
        else
            wsprintfW(szFallback, L"%d", error);

        MessageBox(Globals.hMainWnd, pszMessage, szTitle, MB_OK | MB_ICONERROR);
        LocalFree(lpMsgBuf);
    }
}

/**
 * Sets the caption of the main window according to Globals.szFileTitle:
 *    (untitled) - Notepad      if no file is open
 *    [filename] - Notepad      if a file is given
 */
void UpdateWindowCaption(BOOL clearModifyAlert)
{
    TCHAR szCaption[MAX_STRING_LEN];
    TCHAR szNotepad[MAX_STRING_LEN];
    TCHAR szFilename[MAX_STRING_LEN];
    BOOL isModified;

    if (clearModifyAlert)
    {
        /* When a file is being opened or created, there is no need to have
         * the edited flag shown when the file has not been edited yet. */
        isModified = FALSE;
    }
    else
    {
        /* Check whether the user has modified the file or not. If we are
         * in the same state as before, don't change the caption. */
        isModified = !!SendMessage(Globals.hEdit, EM_GETMODIFY, 0, 0);
        if (isModified == Globals.bWasModified)
            return;
    }

    /* Remember the state for later calls */
    Globals.bWasModified = isModified;

    /* Load the name of the application */
    LoadString(Globals.hInstance, IDS_NOTEPAD, szNotepad, _countof(szNotepad));

    /* Determine if the file has been saved or if this is a new file */
    if (Globals.szFileTitle[0] != 0)
        StringCchCopy(szFilename, _countof(szFilename), Globals.szFileTitle);
    else
        LoadString(Globals.hInstance, IDS_UNTITLED, szFilename, _countof(szFilename));

    /* Update the window caption based upon whether the user has modified the file or not */
    StringCbPrintf(szCaption, sizeof(szCaption), _T("%s%s - %s"),
                   (isModified ? _T("*") : _T("")), szFilename, szNotepad);

    SetWindowText(Globals.hMainWnd, szCaption);
}

VOID WaitCursor(BOOL bBegin)
{
    static HCURSOR s_hWaitCursor = NULL;
    static HCURSOR s_hOldCursor = NULL;
    static INT s_nLock = 0;

    if (bBegin)
    {
        if (s_nLock++ == 0)
        {
            if (s_hWaitCursor == NULL)
                s_hWaitCursor = LoadCursor(NULL, IDC_WAIT);
            s_hOldCursor = SetCursor(s_hWaitCursor);
        }
        else
        {
            SetCursor(s_hWaitCursor);
        }
    }
    else
    {
        if (--s_nLock == 0)
            SetCursor(s_hOldCursor);
    }
}


VOID DIALOG_StatusBarAlignParts(VOID)
{
    static const int defaultWidths[] = {120, 120, 120};
    RECT rcStatusBar;
    int parts[3];

    GetClientRect(Globals.hStatusBar, &rcStatusBar);

    parts[0] = rcStatusBar.right - (defaultWidths[1] + defaultWidths[2]);
    parts[1] = rcStatusBar.right - defaultWidths[2];
    parts[2] = -1; // the right edge of the status bar

    parts[0] = max(parts[0], defaultWidths[0]);
    parts[1] = max(parts[1], defaultWidths[0] + defaultWidths[1]);

    SendMessageW(Globals.hStatusBar, SB_SETPARTS, _countof(parts), (LPARAM)parts);
}

static VOID DIALOG_StatusBarUpdateLineEndings(VOID)
{
    WCHAR szText[128];

    LoadStringW(Globals.hInstance, EolnToStrId[Globals.iEoln], szText, _countof(szText));

    SendMessageW(Globals.hStatusBar, SB_SETTEXTW, SBPART_EOLN, (LPARAM)szText);
}

static VOID DIALOG_StatusBarUpdateEncoding(VOID)
{
    WCHAR szText[128] = L"";

    if (Globals.encFile != ENCODING_AUTO)
    {
        LoadStringW(Globals.hInstance, EncToStrId[Globals.encFile], szText, _countof(szText));
    }

    SendMessageW(Globals.hStatusBar, SB_SETTEXTW, SBPART_ENCODING, (LPARAM)szText);
}

static VOID DIALOG_StatusBarUpdateAll(VOID)
{
    DIALOG_StatusBarUpdateCaretPos();
    DIALOG_StatusBarUpdateLineEndings();
    DIALOG_StatusBarUpdateEncoding();
}

int DIALOG_StringMsgBox(HWND hParent, int formatId, LPCTSTR szString, DWORD dwFlags)
{
    TCHAR szMessage[MAX_STRING_LEN];
    TCHAR szResource[MAX_STRING_LEN];

    /* Load and format szMessage */
    LoadString(Globals.hInstance, formatId, szResource, _countof(szResource));
    StringCchPrintf(szMessage, _countof(szMessage), szResource, szString);

    /* Load szCaption */
    if ((dwFlags & MB_ICONMASK) == MB_ICONEXCLAMATION)
        LoadString(Globals.hInstance, IDS_ERROR, szResource, _countof(szResource));
    else
        LoadString(Globals.hInstance, IDS_NOTEPAD, szResource, _countof(szResource));

    /* Display Modal Dialog */
    // if (hParent == NULL)
        // hParent = Globals.hMainWnd;
    return MessageBox(hParent, szMessage, szResource, dwFlags);
}

static void AlertFileNotFound(LPCTSTR szFileName)
{
    DIALOG_StringMsgBox(Globals.hMainWnd, IDS_NOTFOUND, szFileName, MB_ICONEXCLAMATION | MB_OK);
}

static int AlertFileNotSaved(LPCTSTR szFileName)
{
    TCHAR szUntitled[MAX_STRING_LEN];

    LoadString(Globals.hInstance, IDS_UNTITLED, szUntitled, _countof(szUntitled));

    return DIALOG_StringMsgBox(Globals.hMainWnd, IDS_NOTSAVED,
                               szFileName[0] ? szFileName : szUntitled,
                               MB_ICONQUESTION | MB_YESNOCANCEL);
}

/**
 * Returns:
 *   TRUE  - if file exists
 *   FALSE - if file does not exist
 */
BOOL FileExists(LPCTSTR szFilename)
{
    return GetFileAttributes(szFilename) != INVALID_FILE_ATTRIBUTES;
}

BOOL HasFileExtension(LPCTSTR szFilename)
{
    LPCTSTR s;

    s = _tcsrchr(szFilename, _T('\\'));
    if (s)
        szFilename = s;
    return _tcsrchr(szFilename, _T('.')) != NULL;
}

static BOOL DoSaveFile(VOID)
{
    BOOL bRet = FALSE;
    HANDLE hFile;
    DWORD cchText;

    WaitCursor(TRUE);

    /* Use OPEN_ALWAYS instead of CREATE_ALWAYS in order to succeed
     * even if the file has HIDDEN or SYSTEM attributes */
    hFile = CreateFileW(Globals.szFileName, GENERIC_WRITE, FILE_SHARE_READ,
                        NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        ShowLastError();
        WaitCursor(FALSE);
        return FALSE;
    }

    cchText = GetWindowTextLengthW(Globals.hEdit);
    if (cchText <= 0)
    {
        bRet = TRUE;
    }
    else
    {
        HLOCAL hLocal = (HLOCAL)SendMessageW(Globals.hEdit, EM_GETHANDLE, 0, 0);
        LPWSTR pszText = (LPWSTR)LocalLock(hLocal);
        if (pszText)
        {
            bRet = WriteText(hFile, pszText, cchText, Globals.encFile, Globals.iEoln);
            if (!bRet)
                ShowLastError();

            LocalUnlock(hLocal);
        }
        else
        {
            ShowLastError();
        }
    }

    /* Truncate the file and close it */
    SetEndOfFile(hFile);
    CloseHandle(hFile);

    if (bRet)
    {
        SendMessage(Globals.hEdit, EM_SETMODIFY, FALSE, 0);
        SetFileName(Globals.szFileName);
    }

    WaitCursor(FALSE);
    return bRet;
}

/**
 * Returns:
 *   TRUE  - User agreed to close (both save/don't save)
 *   FALSE - User cancelled close by selecting "Cancel"
 */
BOOL DoCloseFile(VOID)
{
    int nResult;

    if (SendMessage(Globals.hEdit, EM_GETMODIFY, 0, 0))
    {
        /* prompt user to save changes */
        nResult = AlertFileNotSaved(Globals.szFileName);
        switch (nResult)
        {
            case IDYES:
                if(!DIALOG_FileSave())
                    return FALSE;
                break;

            case IDNO:
                break;

            case IDCANCEL:
            default:
                return FALSE;
        }
    }

    SetFileName(empty_str);
    UpdateWindowCaption(TRUE);

    return TRUE;
}

VOID DoOpenFile(LPCTSTR szFileName)
{
    HANDLE hFile;
    TCHAR log[5];
    HLOCAL hOldLocal, hNewLocal;

    /* Close any files and prompt to save changes */
    if (!DoCloseFile())
        return;

    WaitCursor(TRUE);
    SetWindowText(Globals.hEdit, NULL);

    hFile = CreateFile(szFileName, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                       OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        ShowLastError();
        goto done;
    }

    /* To make loading file quicker, we use the internal handle of EDIT control */
    hOldLocal = (HLOCAL)SendMessageW(Globals.hEdit, EM_GETHANDLE, 0, 0);
    hNewLocal = ReadText(hFile, &Globals.encFile, &Globals.iEoln);
    if (!hNewLocal)
    {
        ShowLastError();
        goto done;
    }
    SendMessageW(Globals.hEdit, EM_SETHANDLE, (WPARAM)hNewLocal, 0);
    LocalFree(hOldLocal);
    /* No need of EM_SETMODIFY and EM_EMPTYUNDOBUFFER here. EM_SETHANDLE does instead. */

    SetFocus(Globals.hEdit);

    /*  If the file starts with .LOG, add a time/date at the end and set cursor after
     *  See http://web.archive.org/web/20090627165105/http://support.microsoft.com/kb/260563
     */
    if (GetWindowText(Globals.hEdit, log, _countof(log)) && !_tcscmp(log, _T(".LOG")))
    {
        static const TCHAR lf[] = _T("\r\n");
        SendMessage(Globals.hEdit, EM_SETSEL, GetWindowTextLength(Globals.hEdit), -1);
        SendMessage(Globals.hEdit, EM_REPLACESEL, TRUE, (LPARAM)lf);
        DIALOG_EditTimeDate();
        SendMessage(Globals.hEdit, EM_REPLACESEL, TRUE, (LPARAM)lf);
    }

    SetFileName(szFileName);
    UpdateWindowCaption(TRUE);
    NOTEPAD_EnableSearchMenu();
    DIALOG_StatusBarUpdateAll();

done:
    if (hFile != INVALID_HANDLE_VALUE)
        CloseHandle(hFile);
    WaitCursor(FALSE);
}

VOID DIALOG_FileNew(VOID)
{
    /* Close any files and prompt to save changes */
    if (!DoCloseFile())
        return;

    WaitCursor(TRUE);

    SetWindowText(Globals.hEdit, NULL);
    SendMessage(Globals.hEdit, EM_EMPTYUNDOBUFFER, 0, 0);
    Globals.iEoln = EOLN_CRLF;
    Globals.encFile = ENCODING_DEFAULT;

    NOTEPAD_EnableSearchMenu();
    DIALOG_StatusBarUpdateAll();

    WaitCursor(FALSE);
}

VOID DIALOG_FileNewWindow(VOID)
{
    TCHAR pszNotepadExe[MAX_PATH];

    WaitCursor(TRUE);

    GetModuleFileName(NULL, pszNotepadExe, _countof(pszNotepadExe));
    ShellExecute(NULL, NULL, pszNotepadExe, NULL, NULL, SW_SHOWNORMAL);

    WaitCursor(FALSE);
}

VOID DIALOG_FileOpen(VOID)
{
    OPENFILENAME openfilename;
    TCHAR szPath[MAX_PATH];

    ZeroMemory(&openfilename, sizeof(openfilename));

    if (Globals.szFileName[0] == 0)
        _tcscpy(szPath, txt_files);
    else
        _tcscpy(szPath, Globals.szFileName);

    openfilename.lStructSize = sizeof(openfilename);
    openfilename.hwndOwner = Globals.hMainWnd;
    openfilename.hInstance = Globals.hInstance;
    openfilename.lpstrFilter = Globals.szFilter;
    openfilename.lpstrFile = szPath;
    openfilename.nMaxFile = _countof(szPath);
    openfilename.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    openfilename.lpstrDefExt = szDefaultExt;

    if (GetOpenFileName(&openfilename)) {
        if (FileExists(openfilename.lpstrFile))
            DoOpenFile(openfilename.lpstrFile);
        else
            AlertFileNotFound(openfilename.lpstrFile);
    }
}

BOOL DIALOG_FileSave(VOID)
{
    if (Globals.szFileName[0] == 0)
    {
        return DIALOG_FileSaveAs();
    }
    else if (DoSaveFile())
    {
        UpdateWindowCaption(TRUE);
        return TRUE;
    }
    return FALSE;
}

static UINT_PTR
CALLBACK
DIALOG_FileSaveAs_Hook(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam)
{
    TCHAR szText[128];
    HWND hCombo;

    UNREFERENCED_PARAMETER(wParam);

    switch(msg)
    {
        case WM_INITDIALOG:
            hCombo = GetDlgItem(hDlg, ID_ENCODING);

            LoadString(Globals.hInstance, IDS_ANSI, szText, _countof(szText));
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            LoadString(Globals.hInstance, IDS_UNICODE, szText, _countof(szText));
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            LoadString(Globals.hInstance, IDS_UNICODE_BE, szText, _countof(szText));
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            LoadString(Globals.hInstance, IDS_UTF8, szText, _countof(szText));
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            LoadString(Globals.hInstance, IDS_UTF8_BOM, szText, _countof(szText));
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            SendMessage(hCombo, CB_SETCURSEL, Globals.encFile, 0);

            hCombo = GetDlgItem(hDlg, ID_EOLN);

            LoadString(Globals.hInstance, IDS_CRLF, szText, _countof(szText));
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            LoadString(Globals.hInstance, IDS_LF, szText, _countof(szText));
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            LoadString(Globals.hInstance, IDS_CR, szText, _countof(szText));
            SendMessage(hCombo, CB_ADDSTRING, 0, (LPARAM) szText);

            SendMessage(hCombo, CB_SETCURSEL, Globals.iEoln, 0);
            break;

        case WM_NOTIFY:
            if (((NMHDR *) lParam)->code == CDN_FILEOK)
            {
                hCombo = GetDlgItem(hDlg, ID_ENCODING);
                if (hCombo)
                    Globals.encFile = (ENCODING) SendMessage(hCombo, CB_GETCURSEL, 0, 0);

                hCombo = GetDlgItem(hDlg, ID_EOLN);
                if (hCombo)
                    Globals.iEoln = (EOLN)SendMessage(hCombo, CB_GETCURSEL, 0, 0);
            }
            break;
    }
    return 0;
}

BOOL DIALOG_FileSaveAs(VOID)
{
    OPENFILENAME saveas;
    TCHAR szPath[MAX_PATH];

    ZeroMemory(&saveas, sizeof(saveas));

    if (Globals.szFileName[0] == 0)
        _tcscpy(szPath, txt_files);
    else
        _tcscpy(szPath, Globals.szFileName);

    saveas.lStructSize = sizeof(OPENFILENAME);
    saveas.hwndOwner = Globals.hMainWnd;
    saveas.hInstance = Globals.hInstance;
    saveas.lpstrFilter = Globals.szFilter;
    saveas.lpstrFile = szPath;
    saveas.nMaxFile = _countof(szPath);
    saveas.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_HIDEREADONLY |
                   OFN_EXPLORER | OFN_ENABLETEMPLATE | OFN_ENABLEHOOK;
    saveas.lpstrDefExt = szDefaultExt;
    saveas.lpTemplateName = MAKEINTRESOURCE(IDD_ENCODING);
    saveas.lpfnHook = DIALOG_FileSaveAs_Hook;

    if (GetSaveFileName(&saveas))
    {
        /* HACK: Because in ROS, Save-As boxes don't check the validity
         * of file names and thus, here, szPath can be invalid !! We only
         * see its validity when we call DoSaveFile()... */
        SetFileName(szPath);
        if (DoSaveFile())
        {
            UpdateWindowCaption(TRUE);
            DIALOG_StatusBarUpdateAll();
            return TRUE;
        }
        else
        {
            SetFileName(_T(""));
            return FALSE;
        }
    }
    else
    {
        return FALSE;
    }
}

VOID DIALOG_FileExit(VOID)
{
    PostMessage(Globals.hMainWnd, WM_CLOSE, 0, 0);
}

VOID DIALOG_EditUndo(VOID)
{
    SendMessage(Globals.hEdit, EM_UNDO, 0, 0);
}

VOID DIALOG_EditCut(VOID)
{
    SendMessage(Globals.hEdit, WM_CUT, 0, 0);
}

VOID DIALOG_EditCopy(VOID)
{
    SendMessage(Globals.hEdit, WM_COPY, 0, 0);
}

VOID DIALOG_EditPaste(VOID)
{
    SendMessage(Globals.hEdit, WM_PASTE, 0, 0);
}

VOID DIALOG_EditDelete(VOID)
{
    SendMessage(Globals.hEdit, WM_CLEAR, 0, 0);
}

VOID DIALOG_EditSelectAll(VOID)
{
    SendMessage(Globals.hEdit, EM_SETSEL, 0, -1);
}

VOID DIALOG_EditTimeDate(VOID)
{
    SYSTEMTIME st;
    TCHAR szDate[MAX_STRING_LEN];
    TCHAR szText[MAX_STRING_LEN * 2 + 2];

    GetLocalTime(&st);

    GetTimeFormat(LOCALE_USER_DEFAULT, 0, &st, NULL, szDate, MAX_STRING_LEN);
    _tcscpy(szText, szDate);
    _tcscat(szText, _T(" "));
    GetDateFormat(LOCALE_USER_DEFAULT, DATE_LONGDATE, &st, NULL, szDate, MAX_STRING_LEN);
    _tcscat(szText, szDate);
    SendMessage(Globals.hEdit, EM_REPLACESEL, TRUE, (LPARAM)szText);
}

VOID DoShowHideStatusBar(VOID)
{
    /* Check if status bar object already exists. */
    if (Globals.bShowStatusBar && Globals.hStatusBar == NULL)
    {
        /* Try to create the status bar */
        Globals.hStatusBar = CreateStatusWindow(WS_CHILD | CCS_BOTTOM | SBARS_SIZEGRIP,
                                                NULL,
                                                Globals.hMainWnd,
                                                IDC_STATUSBAR_WND_ID);

        if (Globals.hStatusBar == NULL)
        {
            ShowLastError();
            return;
        }

        /* Load the string for formatting column/row text output */
        LoadString(Globals.hInstance, IDS_LINE_COLUMN, Globals.szStatusBarLineCol, MAX_PATH - 1);
    }

    /* Update layout of controls */
    SendMessageW(Globals.hMainWnd, WM_SIZE, 0, 0);

    if (Globals.hStatusBar == NULL)
        return;

    /* Update visibility of status bar */
    ShowWindow(Globals.hStatusBar, (Globals.bShowStatusBar ? SW_SHOWNOACTIVATE : SW_HIDE));

    /* Update status bar contents */
    DIALOG_StatusBarUpdateAll();
}

VOID DoCreateEditWindow(VOID)
{
    DWORD dwStyle;
    int iSize;
    LPTSTR pTemp = NULL;
    BOOL bModified = FALSE;

    iSize = 0;

    /* If the edit control already exists, try to save its content */
    if (Globals.hEdit != NULL)
    {
        /* number of chars currently written into the editor. */
        iSize = GetWindowTextLength(Globals.hEdit);
        if (iSize)
        {
            /* Allocates temporary buffer. */
            pTemp = (LPTSTR)HeapAlloc(GetProcessHeap(), 0, (iSize + 1) * sizeof(TCHAR));
            if (!pTemp)
            {
                ShowLastError();
                return;
            }

            /* Recover the text into the control. */
            GetWindowText(Globals.hEdit, pTemp, iSize + 1);

            if (SendMessage(Globals.hEdit, EM_GETMODIFY, 0, 0))
                bModified = TRUE;
        }

        /* Restore original window procedure */
        SetWindowLongPtr(Globals.hEdit, GWLP_WNDPROC, (LONG_PTR)Globals.EditProc);

        /* Destroy the edit control */
        DestroyWindow(Globals.hEdit);
    }

    /* Update wrap status into the main menu and recover style flags */
    dwStyle = (Globals.bWrapLongLines ? EDIT_STYLE_WRAP : EDIT_STYLE);

    /* Create the new edit control */
    Globals.hEdit = CreateWindowEx(WS_EX_CLIENTEDGE,
                                   EDIT_CLASS,
                                   NULL,
                                   dwStyle,
                                   CW_USEDEFAULT,
                                   CW_USEDEFAULT,
                                   CW_USEDEFAULT,
                                   CW_USEDEFAULT,
                                   Globals.hMainWnd,
                                   NULL,
                                   Globals.hInstance,
                                   NULL);
    if (Globals.hEdit == NULL)
    {
        if (pTemp)
        {
            HeapFree(GetProcessHeap(), 0, pTemp);
        }

        ShowLastError();
        return;
    }

    SendMessage(Globals.hEdit, WM_SETFONT, (WPARAM)Globals.hFont, FALSE);
    SendMessage(Globals.hEdit, EM_LIMITTEXT, 0, 0);

    /* If some text was previously saved, restore it. */
    if (iSize != 0)
    {
        SetWindowText(Globals.hEdit, pTemp);
        HeapFree(GetProcessHeap(), 0, pTemp);

        if (bModified)
            SendMessage(Globals.hEdit, EM_SETMODIFY, TRUE, 0);
    }

    /* Sub-class a new window callback for row/column detection. */
    Globals.EditProc = (WNDPROC)SetWindowLongPtr(Globals.hEdit,
                                                 GWLP_WNDPROC,
                                                 (LONG_PTR)EDIT_WndProc);

    /* Finally shows new edit control and set focus into it. */
    ShowWindow(Globals.hEdit, SW_SHOW);
    SetFocus(Globals.hEdit);

    /* Re-arrange controls */
    PostMessageW(Globals.hMainWnd, WM_SIZE, 0, 0);
}

VOID DIALOG_EditWrap(VOID)
{
    Globals.bWrapLongLines = !Globals.bWrapLongLines;

    EnableMenuItem(Globals.hMenu, IDC_GOTO, (Globals.bWrapLongLines ? MF_GRAYED : MF_ENABLED));

    DoCreateEditWindow();
    DoShowHideStatusBar();
}

VOID DIALOG_SelectFont(VOID)
{
    CHOOSEFONT cf;
    LOGFONT lf = Globals.lfFont;

    ZeroMemory( &cf, sizeof(cf) );
    cf.lStructSize = sizeof(cf);
    cf.hwndOwner = Globals.hMainWnd;
    cf.lpLogFont = &lf;
    cf.Flags = CF_SCREENFONTS | CF_INITTOLOGFONTSTRUCT | CF_NOVERTFONTS;

    if (ChooseFont(&cf))
    {
        HFONT currfont = Globals.hFont;

        Globals.hFont = CreateFontIndirect(&lf);
        Globals.lfFont = lf;
        SendMessage(Globals.hEdit, WM_SETFONT, (WPARAM)Globals.hFont, TRUE);
        if (currfont != NULL)
            DeleteObject(currfont);
    }
}

typedef HWND (WINAPI *FINDPROC)(LPFINDREPLACE lpfr);

static VOID DIALOG_SearchDialog(FINDPROC pfnProc)
{
    if (Globals.hFindReplaceDlg != NULL)
    {
        SetFocus(Globals.hFindReplaceDlg);
        return;
    }

    if (!Globals.find.lpstrFindWhat)
    {
        ZeroMemory(&Globals.find, sizeof(Globals.find));
        Globals.find.lStructSize = sizeof(FINDREPLACEW);
        Globals.find.hwndOwner = Globals.hMainWnd;
        Globals.find.lpstrFindWhat = Globals.szFindText;
        Globals.find.wFindWhatLen = _countof(Globals.szFindText);
        Globals.find.lpstrReplaceWith = Globals.szReplaceText;
        Globals.find.wReplaceWithLen = _countof(Globals.szReplaceText);
        Globals.find.Flags = FR_DOWN;
    }

    /* We only need to create the modal FindReplace dialog which will */
    /* notify us of incoming events using hMainWnd Window Messages    */

    Globals.hFindReplaceDlg = pfnProc(&Globals.find);
    assert(Globals.hFindReplaceDlg != NULL);
}

/* Read dialog controls into pFR; bReplace indicates the Replace dialog */
static VOID DIALOG_ReadFindReplace(HWND hDlg, PFINDREPLACEDX pFR, BOOL bReplace)
{
    GetDlgItemText(hDlg, edt1, pFR->lpstrFindWhat, pFR->wFindWhatLen);
    if (bReplace)
        GetDlgItemText(hDlg, edt2, pFR->lpstrReplaceWith, pFR->wReplaceWithLen);
    pFR->Flags &= ~(FR_MATCHCASE | FR_WHOLEWORD | FR_DOWN);
    if (IsDlgButtonChecked(hDlg, chx1) == BST_CHECKED) pFR->Flags |= FR_MATCHCASE;
    if (IsDlgButtonChecked(hDlg, chx2) == BST_CHECKED) pFR->Flags |= FR_WHOLEWORD;
    if (IsDlgButtonChecked(hDlg, rad2) == BST_CHECKED) pFR->Flags |= FR_DOWN;
    pFR->bRegExp = (IsDlgButtonChecked(hDlg, chx3) == BST_CHECKED);
    pFR->bCyclic = FALSE;
}

/* Registered message ID for find/replace notifications (cached on first use) */
static UINT s_uFindReplaceMsg = 0;

UINT DIALOG_GetFindReplaceMsg(VOID)
{
    if (s_uFindReplaceMsg == 0)
        s_uFindReplaceMsg = RegisterWindowMessage(FINDMSGSTRING);
    return s_uFindReplaceMsg;
}

static INT_PTR CALLBACK DIALOG_Find_DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    PFINDREPLACEDX pFR = (PFINDREPLACEDX)GetWindowLongPtr(hDlg, DWLP_USER);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            pFR = (PFINDREPLACEDX)lParam;
            SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)pFR);
            SendDlgItemMessage(hDlg, edt1, EM_LIMITTEXT, MAX_FINDREPLACE_LENGTH - 1, 0);
            SetDlgItemText(hDlg, edt1, pFR->lpstrFindWhat);
            CheckDlgButton(hDlg, chx1, (pFR->Flags & FR_MATCHCASE) ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, chx2, (pFR->Flags & FR_WHOLEWORD) ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, chx3, pFR->bRegExp ? BST_CHECKED : BST_UNCHECKED);
            CheckRadioButton(hDlg, rad1, rad2, (pFR->Flags & FR_DOWN) ? rad2 : rad1);
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDOK:
                    DIALOG_ReadFindReplace(hDlg, pFR, FALSE);
                    pFR->Flags = (pFR->Flags & ~FR_DIALOGTERM) | FR_FINDNEXT;
                    SendMessage(pFR->hwndOwner, DIALOG_GetFindReplaceMsg(), 0, (LPARAM)pFR);
                    break;

                case IDCANCEL:
                    pFR->Flags = (pFR->Flags & ~(FR_FINDNEXT | FR_REPLACE | FR_REPLACEALL)) | FR_DIALOGTERM;
                    SendMessage(pFR->hwndOwner, DIALOG_GetFindReplaceMsg(), 0, (LPARAM)pFR);
                    DestroyWindow(hDlg);
                    break;
            }
            break;
    }
    return 0;
}

static INT_PTR CALLBACK DIALOG_Replace_DialogProc(HWND hDlg, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    PFINDREPLACEDX pFR = (PFINDREPLACEDX)GetWindowLongPtr(hDlg, DWLP_USER);

    switch (uMsg)
    {
        case WM_INITDIALOG:
            pFR = (PFINDREPLACEDX)lParam;
            SetWindowLongPtr(hDlg, DWLP_USER, (LONG_PTR)pFR);
            SendDlgItemMessage(hDlg, edt1, EM_LIMITTEXT, MAX_FINDREPLACE_LENGTH - 1, 0);
            SendDlgItemMessage(hDlg, edt2, EM_LIMITTEXT, MAX_FINDREPLACE_LENGTH - 1, 0);
            SetDlgItemText(hDlg, edt1, pFR->lpstrFindWhat);
            SetDlgItemText(hDlg, edt2, pFR->lpstrReplaceWith);
            CheckDlgButton(hDlg, chx1, (pFR->Flags & FR_MATCHCASE) ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, chx2, (pFR->Flags & FR_WHOLEWORD) ? BST_CHECKED : BST_UNCHECKED);
            CheckDlgButton(hDlg, chx3, pFR->bRegExp ? BST_CHECKED : BST_UNCHECKED);
            CheckRadioButton(hDlg, rad1, rad2, (pFR->Flags & FR_DOWN) ? rad2 : rad1);
            return TRUE;

        case WM_COMMAND:
            switch (LOWORD(wParam))
            {
                case IDOK: /* Find Next */
                    DIALOG_ReadFindReplace(hDlg, pFR, TRUE);
                    pFR->Flags = (pFR->Flags & ~(FR_REPLACE | FR_REPLACEALL | FR_DIALOGTERM)) | FR_FINDNEXT;
                    SendMessage(pFR->hwndOwner, DIALOG_GetFindReplaceMsg(), 0, (LPARAM)pFR);
                    break;

                case psh1: /* Replace */
                    DIALOG_ReadFindReplace(hDlg, pFR, TRUE);
                    pFR->Flags = (pFR->Flags & ~(FR_FINDNEXT | FR_REPLACEALL | FR_DIALOGTERM)) | FR_REPLACE;
                    SendMessage(pFR->hwndOwner, DIALOG_GetFindReplaceMsg(), 0, (LPARAM)pFR);
                    break;

                case psh2: /* Replace All */
                    DIALOG_ReadFindReplace(hDlg, pFR, TRUE);
                    pFR->Flags = (pFR->Flags & ~(FR_FINDNEXT | FR_REPLACE | FR_DIALOGTERM)) | FR_REPLACEALL;
                    SendMessage(pFR->hwndOwner, DIALOG_GetFindReplaceMsg(), 0, (LPARAM)pFR);
                    break;

                case IDCANCEL: /* Cancel */
                    pFR->Flags = (pFR->Flags & ~(FR_FINDNEXT | FR_REPLACE | FR_REPLACEALL)) | FR_DIALOGTERM;
                    SendMessage(pFR->hwndOwner, DIALOG_GetFindReplaceMsg(), 0, (LPARAM)pFR);
                    DestroyWindow(hDlg);
                    break;
            }
            break;
    }
    return 0;
}

HWND CALLBACK DIALOG_FindText(LPFINDREPLACE lpfr)
{
    HWND hDlg = CreateDialogParam(Globals.hInstance,
                                   MAKEINTRESOURCE(IDD_FIND),
                                   lpfr->hwndOwner,
                                   DIALOG_Find_DialogProc,
                                   (LPARAM)lpfr);
    if (hDlg != NULL)
        ShowWindow(hDlg, SW_SHOW);
    return hDlg;
}

HWND CALLBACK DIALOG_ReplaceText(LPFINDREPLACE lpfr)
{
    HWND hDlg = CreateDialogParam(Globals.hInstance,
                                   MAKEINTRESOURCE(IDD_REPLACE),
                                   lpfr->hwndOwner,
                                   DIALOG_Replace_DialogProc,
                                   (LPARAM)lpfr);
    if (hDlg != NULL)
        ShowWindow(hDlg, SW_SHOW);
    return hDlg;
}

VOID DIALOG_Search(VOID)
{
    DIALOG_SearchDialog(DIALOG_FindText);
}

VOID DIALOG_SearchNext(BOOL bDown)
{
    if (bDown)
        Globals.find.Flags |= FR_DOWN;
    else
        Globals.find.Flags &= ~FR_DOWN;

    if (Globals.find.lpstrFindWhat != NULL && *Globals.find.lpstrFindWhat)
        NOTEPAD_FindNext(&Globals.find, FALSE, TRUE);
    else
        DIALOG_Search();
}

VOID DIALOG_Replace(VOID)
{
    DIALOG_SearchDialog(DIALOG_ReplaceText);
}

typedef struct tagGOTO_DATA
{
    UINT iLine;
    UINT cLines;
} GOTO_DATA, *PGOTO_DATA;

static INT_PTR
CALLBACK
DIALOG_GoTo_DialogProc(HWND hwndDialog, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static PGOTO_DATA s_pGotoData;

    switch (uMsg)
    {
        case WM_INITDIALOG:
            s_pGotoData = (PGOTO_DATA)lParam;
            SetDlgItemInt(hwndDialog, ID_LINENUMBER, s_pGotoData->iLine, FALSE);
            return TRUE; /* Set focus */

        case WM_COMMAND:
        {
            if (LOWORD(wParam) == IDOK)
            {
                UINT iLine = GetDlgItemInt(hwndDialog, ID_LINENUMBER, NULL, FALSE);
                if (iLine <= 0 || s_pGotoData->cLines < iLine) /* Out of range */
                {
                    /* Show error message */
                    WCHAR title[128], text[256];
                    LoadStringW(Globals.hInstance, IDS_NOTEPAD, title, _countof(title));
                    LoadStringW(Globals.hInstance, IDS_LINE_NUMBER_OUT_OF_RANGE, text, _countof(text));
                    MessageBoxW(hwndDialog, text, title, MB_OK);

                    SendDlgItemMessageW(hwndDialog, ID_LINENUMBER, EM_SETSEL, 0, -1);
                    SetFocus(GetDlgItem(hwndDialog, ID_LINENUMBER));
                    break;
                }
                s_pGotoData->iLine = iLine;
                EndDialog(hwndDialog, IDOK);
            }
            else if (LOWORD(wParam) == IDCANCEL)
            {
                EndDialog(hwndDialog, IDCANCEL);
            }
            break;
        }
    }

    return 0;
}

VOID DIALOG_GoTo(VOID)
{
    GOTO_DATA GotoData;
    DWORD dwStart = 0, dwEnd = 0;
    INT ich, cch = GetWindowTextLength(Globals.hEdit);

    /* Get the current line number and the total line number */
    SendMessage(Globals.hEdit, EM_GETSEL, (WPARAM) &dwStart, (LPARAM) &dwEnd);
    GotoData.iLine = (UINT)SendMessage(Globals.hEdit, EM_LINEFROMCHAR, dwStart, 0) + 1;
    GotoData.cLines = (UINT)SendMessage(Globals.hEdit, EM_GETLINECOUNT, 0, 0);

    /* Ask the user for line number */
    if (DialogBoxParam(Globals.hInstance,
                       MAKEINTRESOURCE(IDD_GOTO),
                       Globals.hMainWnd,
                       DIALOG_GoTo_DialogProc,
                       (LPARAM)&GotoData) != IDOK)
    {
        return; /* Canceled */
    }

    --GotoData.iLine; /* Make it zero-based */

    /* Get ich (the target character index) from line number */
    if (GotoData.iLine <= 0)
        ich = 0;
    else if (GotoData.iLine >= GotoData.cLines)
        ich = cch;
    else
        ich = (INT)SendMessage(Globals.hEdit, EM_LINEINDEX, GotoData.iLine, 0);

    /* EM_LINEINDEX can return -1 on failure */
    if (ich < 0)
        ich = 0;

    /* Move the caret */
    SendMessage(Globals.hEdit, EM_SETSEL, ich, ich);
    SendMessage(Globals.hEdit, EM_SCROLLCARET, 0, 0);
}

VOID DIALOG_StatusBarUpdateCaretPos(VOID)
{
    int line, ich, col;
    TCHAR buff[MAX_PATH];
    DWORD dwStart, dwSize;

    SendMessage(Globals.hEdit, EM_GETSEL, (WPARAM)&dwStart, (LPARAM)&dwSize);
    line = (int)SendMessage(Globals.hEdit, EM_LINEFROMCHAR, (WPARAM)dwStart, 0);
    ich = (int)SendMessage(Globals.hEdit, EM_LINEINDEX, (WPARAM)line, 0);

    /* EM_LINEINDEX can return -1 on failure */
    col = ((ich < 0) ? 0 : (dwStart - ich));

    StringCchPrintf(buff, _countof(buff), Globals.szStatusBarLineCol, line + 1, col + 1);
    SendMessage(Globals.hStatusBar, SB_SETTEXT, SBPART_CURPOS, (LPARAM)buff);
}

VOID DIALOG_ViewStatusBar(VOID)
{
    Globals.bShowStatusBar = !Globals.bShowStatusBar;
    DoShowHideStatusBar();
}

VOID DIALOG_HelpContents(VOID)
{
    WinHelp(Globals.hMainWnd, helpfile, HELP_INDEX, 0);
}

VOID DIALOG_HelpAboutNotepad(VOID)
{
    TCHAR szNotepad[MAX_STRING_LEN];
    TCHAR szNotepadAuthors[MAX_STRING_LEN];

    LoadString(Globals.hInstance, IDS_NOTEPAD, szNotepad, _countof(szNotepad));
    LoadString(Globals.hInstance, IDS_NOTEPAD_AUTHORS, szNotepadAuthors, _countof(szNotepadAuthors));

    ShellAbout(Globals.hMainWnd, szNotepad, szNotepadAuthors,
               LoadIcon(Globals.hInstance, MAKEINTRESOURCE(IDI_NPICON)));
}

typedef struct CYCLIC_REPLACE
{
    std::vector<std::wstring> items;
    BOOL bMatchCase;
    BOOL bWholeWord;
    std::wstring text;
    std::wstring strFind;
    std::wstring strReplace;
} CYCLIC_REPLACE, *PCYCLIC_REPLACE;

static INT_PTR CALLBACK
DIALOG_AddItem_DlgProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static PCYCLIC_REPLACE s_pThis = NULL;
    WCHAR text[MAX_FINDREPLACE_LENGTH];

    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            s_pThis = (PCYCLIC_REPLACE)lParam;
            SendDlgItemMessage(hwnd, edt1, EM_LIMITTEXT, MAX_FINDREPLACE_LENGTH - 1, 0);
            HWND hwndOK = GetDlgItem(hwnd, IDOK);
            EnableWindow(hwndOK, FALSE);
            return TRUE;
        }
        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
                case IDOK:
                    GetDlgItemTextW(hwnd, edt1, text, _countof(text));
                    if (text[0])
                    {
                        s_pThis->text = text;
                        EndDialog(hwnd, IDOK);
                    }
                    break;
                case IDCANCEL:
                    EndDialog(hwnd, IDCANCEL);
                    break;
                case edt1:
                {
                    if (HIWORD(wParam) == EN_CHANGE)
                    {
                        HWND hwndOK = GetDlgItem(hwnd, IDOK);
                        EnableWindow(hwndOK, GetWindowTextLength(GetDlgItem(hwnd, edt1)) > 0);
                        break;
                    }
                }
            }
        }
    }
    return 0;
}

static BOOL
DIALOG_CyclicReplace_OnUpdate(PCYCLIC_REPLACE pThis, HWND hwnd)
{
    HWND hLst1 = GetDlgItem(hwnd, lst1);
    size_t cItems = ListBox_GetCount(hLst1);

    // Get items
    WCHAR text[MAX_FINDREPLACE_LENGTH];
    pThis->items.clear();
    for (INT iItem = 0; iItem < cItems; ++iItem)
    {
        ListBox_GetText(hLst1, iItem, text);
        pThis->items.push_back(text);
    }

    // Set strFind
    std::wstring strFind = L"(";
    for (size_t iItem = 0; iItem < cItems; ++iItem)
    {
        if (iItem != 0)
            strFind += L")|(";
        if (pThis->bWholeWord)
        {
            strFind += L"\\b";
            strFind += RegexEngine::EscapeForRegex(pThis->items[iItem]);
            strFind += L"\\b";
        }
        else
        {
            strFind += RegexEngine::EscapeForRegex(pThis->items[iItem]);
        }
    }
    strFind += L")";
    pThis->strFind = std::move(strFind);

    // Set strReplace
    std::wstring strReplace;
    for (size_t iItem = 0; iItem < cItems; ++iItem)
    {
        strReplace += L"${";
        strReplace += std::to_wstring(iItem % cItems + 1);
        strReplace += L":+";
        strReplace += RegexEngine::EscapeForRegex(pThis->items[(iItem + 1) % cItems]);
        strReplace += L":}";
    }
    pThis->strReplace = std::move(strReplace);

    // Set Info
    if (pThis->items.size() < 2)
    {
        LoadStringW(Globals.hInstance, IDS_WANTTWOITEMS, text, _countof(text));
        SetDlgItemTextW(hwnd, edt1, text);
    }
    else
    {
        LoadStringW(Globals.hInstance, IDS_CYCLICREPLACEINFO, text, _countof(text));

        std::wstring str = text;
        for (size_t iItem = 0; iItem < pThis->items.size(); ++iItem)
        {
            if (iItem != 0)
                str += L" >> ";
            str += L"[";
            str += pThis->items[iItem];
            str += L"]";
        }
        str += L" >> [";
        str += pThis->items[0];
        str += L"]";
        SetDlgItemTextW(hwnd, edt1, str.c_str());
    }

    return TRUE;
}

static INT_PTR CALLBACK
DIALOG_CyclicReplace_DlgProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    static PCYCLIC_REPLACE s_pThis = NULL;

    switch (uMsg)
    {
        case WM_INITDIALOG:
        {
            s_pThis = (PCYCLIC_REPLACE)lParam;
            SendDlgItemMessage(hwnd, lst1, LB_SETITEMHEIGHT, 0, 24);

            CheckDlgButton(hwnd, chx1, (s_pThis->bMatchCase ? BST_CHECKED : BST_UNCHECKED));
            CheckDlgButton(hwnd, chx2, (s_pThis->bWholeWord ? BST_CHECKED : BST_UNCHECKED));

            HWND hLst1 = GetDlgItem(hwnd, lst1);
            for (size_t iItem = 0; iItem < s_pThis->items.size(); ++iItem)
            {
                ListBox_AddString(hLst1, s_pThis->items[iItem].c_str());
            }

            DIALOG_CyclicReplace_OnUpdate(s_pThis, hwnd);
            return TRUE;
        }
        case WM_COMMAND:
        {
            switch (LOWORD(wParam))
            {
                case IDOK: // Replace All
                {
                    s_pThis->bMatchCase = IsDlgButtonChecked(hwnd, chx1) == BST_CHECKED;
                    s_pThis->bWholeWord = IsDlgButtonChecked(hwnd, chx2) == BST_CHECKED;
                    DIALOG_CyclicReplace_OnUpdate(s_pThis, hwnd);

                    EndDialog(hwnd, IDOK);
                    break;
                }
                case IDCANCEL:
                    EndDialog(hwnd, IDCANCEL);
                    break;
                case psh2: // Add Item
                {
                    if (DialogBoxParam(Globals.hInstance, MAKEINTRESOURCE(IDD_ADDITEM),
                                       hwnd, DIALOG_AddItem_DlgProc, (LPARAM)s_pThis) == IDOK)
                    {
                        HWND hLst1 = GetDlgItem(hwnd, lst1);
                        INT iItem = ListBox_AddString(GetDlgItem(hwnd, lst1), s_pThis->text.c_str());
                        ListBox_SetCurSel(hLst1, iItem);
                        DIALOG_CyclicReplace_OnUpdate(s_pThis, hwnd);
                    }
                    break;
                }
                case psh3: // Up
                {
                    WCHAR text1[MAX_FINDREPLACE_LENGTH], text2[MAX_FINDREPLACE_LENGTH];
                    HWND hLst1 = GetDlgItem(hwnd, lst1);
                    INT iItem = ListBox_GetCurSel(hLst1);
                    if (iItem == LB_ERR || iItem == 0)
                        break;
                    ListBox_GetText(hLst1, iItem - 1, text1);
                    ListBox_GetText(hLst1, iItem, text2);
                    ListBox_DeleteString(hLst1, iItem - 1);
                    ListBox_DeleteString(hLst1, iItem - 1);
                    ListBox_InsertString(hLst1, iItem - 1, text1);
                    ListBox_InsertString(hLst1, iItem - 1, text2);
                    ListBox_SetCurSel(hLst1, iItem - 1);
                    DIALOG_CyclicReplace_OnUpdate(s_pThis, hwnd);
                    break;
                }
                case psh4: // Down
                {
                    WCHAR text1[MAX_FINDREPLACE_LENGTH], text2[MAX_FINDREPLACE_LENGTH];
                    HWND hLst1 = GetDlgItem(hwnd, lst1);
                    INT iItem = ListBox_GetCurSel(hLst1);
                    INT cItems = ListBox_GetCount(hLst1);
                    if (iItem == LB_ERR || iItem + 1 >= cItems)
                        break;
                    ListBox_GetText(hLst1, iItem, text1);
                    ListBox_GetText(hLst1, iItem + 1, text2);
                    ListBox_DeleteString(hLst1, iItem);
                    ListBox_DeleteString(hLst1, iItem);
                    ListBox_InsertString(hLst1, iItem, text1);
                    ListBox_InsertString(hLst1, iItem, text2);
                    ListBox_SetCurSel(hLst1, iItem + 1);
                    DIALOG_CyclicReplace_OnUpdate(s_pThis, hwnd);
                    break;
                }
                case psh5: // Remove
                {
                    HWND hLst1 = GetDlgItem(hwnd, lst1);
                    INT iItem = ListBox_GetCurSel(hLst1);
                    if (iItem == LB_ERR)
                        break;
                    ListBox_DeleteString(hLst1, iItem);
                    ListBox_SetCurSel(hLst1, iItem);
                    DIALOG_CyclicReplace_OnUpdate(s_pThis, hwnd);
                    break;
                }
                case psh6: // Remove All
                {
                    HWND hLst1 = GetDlgItem(hwnd, lst1);
                    ListBox_ResetContent(hLst1);
                    DIALOG_CyclicReplace_OnUpdate(s_pThis, hwnd);
                    break;
                }
            }
            break;
        }
    }
    return 0;
}

VOID DIALOG_CyclicReplace(VOID)
{
    CYCLIC_REPLACE data;
    data.bWholeWord = !!(Globals.find.Flags & FR_WHOLEWORD);
    data.bMatchCase = !!(Globals.find.Flags & FR_MATCHCASE);
    if (Globals.pCyclicReplaceItems)
        data.items = *Globals.pCyclicReplaceItems;
    INT_PTR id = DialogBoxParam(Globals.hInstance,
                                MAKEINTRESOURCE(IDD_CYCLICREPLACE), Globals.hMainWnd,
                                DIALOG_CyclicReplace_DlgProc, (LPARAM)&data);
    if (id == IDOK)
    {
        WaitCursor(TRUE);

        FINDREPLACEDX find = Globals.find;
        find.lStructSize = sizeof(FINDREPLACEW);
        find.hwndOwner = Globals.hMainWnd;
        find.lpstrFindWhat = &data.strFind[0];
        find.wFindWhatLen = (WORD)lstrlenW(find.lpstrFindWhat);
        find.lpstrReplaceWith = &data.strReplace[0];
        find.wReplaceWithLen = (WORD)lstrlenW(find.lpstrReplaceWith);
        find.bRegExp = TRUE;
        find.bCyclic = TRUE;
        find.Flags &= ~(FR_WHOLEWORD | FR_MATCHCASE);
        find.Flags |= FR_DOWN;
        if (data.bMatchCase)
            find.Flags |= FR_MATCHCASE;

        NOTEPAD_ReplaceAll(&find);

        Globals.find.Flags &= ~(FR_WHOLEWORD | FR_MATCHCASE);
        if (data.bWholeWord)
            Globals.find.Flags |= FR_WHOLEWORD;
        if (data.bMatchCase)
            Globals.find.Flags |= FR_MATCHCASE;
        Globals.pCyclicReplaceItems = new std::vector<std::wstring>(std::move(data.items));

        WaitCursor(FALSE);
    }
}
