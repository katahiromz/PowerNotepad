/*
 * PROJECT:    Power Notepad
 * LICENSE:    LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:    Providing a Windows-compatible simple text editor for ReactOS
 * COPYRIGHT:  Copyright 2000 Mike McCormack <Mike_McCormack@looksmart.com.au>
 *             Copyright 1997,98 Marcel Baur <mbaur@g26.ethz.ch>
 *             Copyright 2002 Sylvain Petreolle <spetreolle@yahoo.fr>
 *             Copyright 2002 Andriy Palamarchuk
 *             Copyright 2020-2023 Katayama Hirofumi MZ
 */

#include "notepad.h"

#include <shlobj.h>
#include <strsafe.h>
#include "regex_engine.h"

NOTEPAD_GLOBALS Globals;
static ATOM aFINDMSGSTRING;

VOID NOTEPAD_EnableSearchMenu()
{
    BOOL bEmpty = (GetWindowTextLengthW(Globals.hEdit) == 0);
    UINT uEnable = MF_BYCOMMAND | (bEmpty ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(Globals.hMenu, IDC_SEARCH, uEnable);
    EnableMenuItem(Globals.hMenu, IDC_SEARCH_NEXT, uEnable);
    EnableMenuItem(Globals.hMenu, IDC_SEARCH_PREV, uEnable);
}

/***********************************************************************
 *           SetFileName
 *
 *  Sets Global File Name.
 */
VOID SetFileName(LPCTSTR szFileName)
{
    StringCchCopy(Globals.szFileName, _countof(Globals.szFileName), szFileName);
    Globals.szFileTitle[0] = 0;
    GetFileTitle(szFileName, Globals.szFileTitle, _countof(Globals.szFileTitle));

    if (szFileName && szFileName[0])
        SHAddToRecentDocs(SHARD_PATHW, szFileName);
}

/***********************************************************************
 *           NOTEPAD_MenuCommand
 *
 *  All handling of main menu events
 */
static int NOTEPAD_MenuCommand(WPARAM wParam)
{
    switch (wParam)
    {
    case IDC_NEW:        DIALOG_FileNew(); break;
    case IDC_NEW_WINDOW: DIALOG_FileNewWindow(); break;
    case IDC_OPEN:       DIALOG_FileOpen(); break;
    case IDC_SAVE:       DIALOG_FileSave(); break;
    case IDC_SAVE_AS:    DIALOG_FileSaveAs(); break;
    case IDC_PRINT:      DIALOG_FilePrint(); break;
    case IDC_PAGE_SETUP: DIALOG_FilePageSetup(); break;
    case IDC_EXIT:       DIALOG_FileExit(); break;

    case IDC_UNDO:       DIALOG_EditUndo(); break;
    case IDC_CUT:        DIALOG_EditCut(); break;
    case IDC_COPY:       DIALOG_EditCopy(); break;
    case IDC_PASTE:      DIALOG_EditPaste(); break;
    case IDC_DELETE:     DIALOG_EditDelete(); break;
    case IDC_SELECT_ALL: DIALOG_EditSelectAll(); break;
    case IDC_TIME_DATE:  DIALOG_EditTimeDate(); break;

    case IDC_SEARCH:      DIALOG_Search(); break;
    case IDC_SEARCH_NEXT: DIALOG_SearchNext(TRUE); break;
    case IDC_REPLACE:     DIALOG_Replace(); break;
    case IDC_GOTO:        DIALOG_GoTo(); break;
    case IDC_SEARCH_PREV: DIALOG_SearchNext(FALSE); break;

    case IDC_WRAP: DIALOG_EditWrap(); break;
    case IDC_FONT: DIALOG_SelectFont(); break;

    case IDC_STATUSBAR: DIALOG_ViewStatusBar(); break;

    case IDC_HELP_CONTENTS: DIALOG_HelpContents(); break;
    case IDC_HELP_ABOUT_NOTEPAD: DIALOG_HelpAboutNotepad(); break;

    case IDC_CYCLIC_REPLACE: DIALOG_CyclicReplace(); break;

    default:
        break;
    }
    return 0;
}

/***********************************************************************
 *           NOTEPAD_FindTextAt
 */
static BOOL
NOTEPAD_FindTextAt(PFINDREPLACEDX pFindReplace, LPCTSTR pszText, INT iTextLength, DWORD dwPosition)
{
    BOOL bMatches;
    size_t iTargetLength;
    LPCTSTR pchPosition;

    if (!pFindReplace || !pszText)
        return FALSE;

    iTargetLength = _tcslen(pFindReplace->lpstrFindWhat);
    pchPosition = &pszText[dwPosition];

    /* Make proper comparison */
    if (pFindReplace->Flags & FR_MATCHCASE)
        bMatches = !_tcsncmp(pchPosition, pFindReplace->lpstrFindWhat, iTargetLength);
    else
        bMatches = !_tcsnicmp(pchPosition, pFindReplace->lpstrFindWhat, iTargetLength);

    if (bMatches && (pFindReplace->Flags & FR_WHOLEWORD))
    {
        if (dwPosition > 0)
        {
            if (_istalnum(*(pchPosition - 1)) || *(pchPosition - 1) == _T('_'))
                bMatches = FALSE;
        }
        if ((INT)dwPosition + iTargetLength < iTextLength)
        {
            if (_istalnum(pchPosition[iTargetLength]) || pchPosition[iTargetLength] == _T('_'))
                bMatches = FALSE;
        }
    }

    return bMatches;
}

static BOOL
NOTEPAD_IsWordBoundaryMatch(LPCTSTR pszText, INT iTextLength, DWORD dwStart, DWORD dwEnd)
{
    if (dwStart > 0)
    {
        TCHAR ch = pszText[dwStart - 1];
        if (_istalnum(ch) || ch == _T('_'))
            return FALSE;
    }
    if ((INT)dwEnd < iTextLength)
    {
        TCHAR ch = pszText[dwEnd];
        if (_istalnum(ch) || ch == _T('_'))
            return FALSE;
    }
    return TRUE;
}

static BOOL
NOTEPAD_FindRegexDown(PFINDREPLACEDX pFindReplace, const RegexEngine& regexFind,
                      LPCTSTR pszText, INT iTextLength, DWORD dwStartPos, DWORD *pdwPosition, DWORD *pdwEndPos)
{
    size_t matchStart = 0, matchEnd = 0;
    DWORD  offset     = dwStartPos;

    while (offset <= (DWORD)iTextLength)
    {
        if (!regexFind.SearchForward(pszText, (size_t)iTextLength, (size_t)offset,
                                     &matchStart, &matchEnd))
            return FALSE;

        if (!(pFindReplace->Flags & FR_WHOLEWORD) ||
            NOTEPAD_IsWordBoundaryMatch(pszText, iTextLength,
                                        (DWORD)matchStart, (DWORD)matchEnd))
        {
            *pdwPosition = (DWORD)matchStart;
            *pdwEndPos   = (DWORD)matchEnd;
            return TRUE;
        }

        /* Whole-word check failed: advance past this match start and retry */
        if (matchStart < (size_t)iTextLength)
            offset = (DWORD)matchStart + 1;
        else
            break;
    }

    return FALSE;
}

static BOOL
NOTEPAD_FindRegexUp(PFINDREPLACEDX pFindReplace, const RegexEngine& regexFind,
                    LPCTSTR pszText, INT iTextLength, DWORD dwStartPos, DWORD *pdwPosition, DWORD *pdwEndPos)
{
    /*
     * Backward search: scan from the beginning, collecting matches that start
     * before dwStartPos, then return the last qualifying one.
     */
    DWORD  offset    = 0;
    BOOL   bFound    = FALSE;
    DWORD  lastStart = 0, lastEnd = 0;

    while (offset <= (DWORD)iTextLength)
    {
        size_t matchStart = 0, matchEnd = 0;

        if (!regexFind.SearchForward(pszText, (size_t)iTextLength, (size_t)offset,
                                     &matchStart, &matchEnd))
            break;

        if (matchStart >= (size_t)dwStartPos)
            break;

        if (!(pFindReplace->Flags & FR_WHOLEWORD) ||
            NOTEPAD_IsWordBoundaryMatch(pszText, iTextLength,
                                        (DWORD)matchStart, (DWORD)matchEnd))
        {
            bFound    = TRUE;
            lastStart = (DWORD)matchStart;
            lastEnd   = (DWORD)matchEnd;
        }

        if (matchStart < (size_t)iTextLength)
            offset = (DWORD)matchStart + 1;
        else
            break;
    }

    if (!bFound)
        return FALSE;

    *pdwPosition = lastStart;
    *pdwEndPos   = lastEnd;
    return TRUE;
}

/***********************************************************************
 *           NOTEPAD_FindNext
 */
BOOL NOTEPAD_FindNext(PFINDREPLACEDX pFindReplace, BOOL bReplace, BOOL bShowAlert)
{
    int iTextLength, iTargetLength;
    DWORD dwMatchEnd;
    LPCTSTR pszText = NULL;
    HLOCAL hText = NULL;
    BOOL bTextLocked = FALSE;
    DWORD dwPosition, dwBegin, dwEnd;
    BOOL bMatches = FALSE;
    TCHAR szResource[128], szText[128];
    BOOL bSuccess;
    BOOL bUseRegex;
    RegexEngine regexFind;

    iTargetLength = (int) _tcslen(pFindReplace->lpstrFindWhat);
    bUseRegex = pFindReplace->bRegExp;

    if (bUseRegex)
    {
        std::wstring errMsg;
        bool caseless = !(pFindReplace->Flags & FR_MATCHCASE);
        if (!regexFind.Compile(pFindReplace->lpstrFindWhat, caseless, &errMsg))
        {
            if (bShowAlert)
            {
                /* Build a readable message: "Invalid regular expression 'pattern':\n<detail>"
                   so the user can diagnose their regex without needing to consult docs. */
                LoadString(Globals.hInstance, IDS_INVALID_REGEX, szResource, _countof(szResource));
                std::wstring fullMsg = szResource
                                       + std::wstring(L" '") + pFindReplace->lpstrFindWhat
                                       + L"':\n" + errMsg;
                LoadString(Globals.hInstance, IDS_NOTEPAD, szText, _countof(szText));
                MessageBox(Globals.hFindReplaceDlg, fullMsg.c_str(), szText, MB_OK | MB_ICONERROR);
            }
            return FALSE;
        }
    }

    auto acquireEditText = [&]() -> BOOL
    {
        if (bTextLocked)
        {
            LocalUnlock(hText);
            bTextLocked = FALSE;
        }

        pszText = NULL;
        hText = NULL;
        iTextLength = GetWindowTextLength(Globals.hEdit);
        if (iTextLength <= 0)
            return TRUE;

        hText = (HLOCAL)SendMessage(Globals.hEdit, EM_GETHANDLE, 0, 0);
        if (hText)
        {
            pszText = (LPCTSTR)LocalLock(hText);
            if (pszText)
                bTextLocked = TRUE;
        }

        if (!pszText)
            return FALSE;

        return TRUE;
    };

    if (!acquireEditText())
        return FALSE;

    SendMessage(Globals.hEdit, EM_GETSEL, (WPARAM) &dwBegin, (LPARAM) &dwEnd);
    if (bReplace && dwEnd > dwBegin)
    {
        BOOL bSelectionMatched = FALSE;

        if (bUseRegex)
        {
            if (pszText && dwEnd <= (DWORD)iTextLength)
            {
                bSelectionMatched = regexFind.IsFullMatch(pszText, dwBegin, dwEnd);
                if (bSelectionMatched && (pFindReplace->Flags & FR_WHOLEWORD))
                    bSelectionMatched = NOTEPAD_IsWordBoundaryMatch(pszText, iTextLength, dwBegin, dwEnd);
                if (bSelectionMatched)
                {
                    std::wstring replaced;
                    if (regexFind.ReplaceMatch(pszText, (size_t)iTextLength,
                                               (size_t)dwBegin, (size_t)dwEnd,
                                               pFindReplace->lpstrReplaceWith, replaced))
                    {
                        if (bTextLocked)
                        {
                            LocalUnlock(hText);
                            bTextLocked = FALSE;
                        }
                        SendMessage(Globals.hEdit, EM_REPLACESEL, TRUE, (LPARAM)replaced.c_str());
                        if (!acquireEditText())
                            return FALSE;
                    }
                    else
                    {
                        bSelectionMatched = FALSE;
                    }
                }
            }
        }
        else if (((dwEnd - dwBegin) == (DWORD)iTargetLength) &&
                 NOTEPAD_FindTextAt(pFindReplace, pszText, iTextLength, dwBegin))
        {
            bSelectionMatched = TRUE;
            if (bTextLocked)
            {
                LocalUnlock(hText);
                bTextLocked = FALSE;
            }
            SendMessage(Globals.hEdit, EM_REPLACESEL, TRUE, (LPARAM)pFindReplace->lpstrReplaceWith);
            if (!acquireEditText())
                return FALSE;
        }

        if (bSelectionMatched)
        {
            if (!acquireEditText())
                return FALSE;
            SendMessage(Globals.hEdit, EM_GETSEL, (WPARAM)&dwBegin, (LPARAM)&dwEnd);
        }
    }

    dwMatchEnd = dwEnd;
    if (pFindReplace->Flags & FR_DOWN)
    {
        /* Find Down */
        dwPosition = dwEnd;
        if (bUseRegex)
        {
            bMatches = (pszText &&
                        NOTEPAD_FindRegexDown(pFindReplace, regexFind, pszText, iTextLength,
                                              dwPosition, &dwPosition, &dwMatchEnd));
        }
        else while(dwPosition < (DWORD) iTextLength)
        {
            bMatches = NOTEPAD_FindTextAt(pFindReplace, pszText, iTextLength, dwPosition);
            if (bMatches)
            {
                dwMatchEnd = dwPosition + iTargetLength;
                break;
            }
            dwPosition++;
        }
    }
    else
    {
        /* Find Up */
        dwPosition = dwBegin;
        if (bUseRegex)
        {
            bMatches = (pszText &&
                        NOTEPAD_FindRegexUp(pFindReplace, regexFind, pszText, iTextLength,
                                            dwBegin, &dwPosition, &dwMatchEnd));
        }
        else while(dwPosition > 0)
        {
            dwPosition--;
            bMatches = NOTEPAD_FindTextAt(pFindReplace, pszText, iTextLength, dwPosition);
            if (bMatches)
            {
                dwMatchEnd = dwPosition + iTargetLength;
                break;
            }
        }
    }

    if (bMatches)
    {
        /* Found target */
        SendMessage(Globals.hEdit, EM_SETSEL, dwPosition, dwMatchEnd);
        SendMessage(Globals.hEdit, EM_SCROLLCARET, 0, 0);
        bSuccess = TRUE;
    }
    else
    {
        /* Can't find target */
        if (bShowAlert)
        {
            LoadString(Globals.hInstance, IDS_CANNOTFIND, szResource, _countof(szResource));
            StringCchPrintf(szText, _countof(szText), szResource, pFindReplace->lpstrFindWhat);
            LoadString(Globals.hInstance, IDS_NOTEPAD, szResource, _countof(szResource));
            MessageBox(Globals.hFindReplaceDlg, szText, szResource, MB_OK);
        }
        bSuccess = FALSE;
    }

    if (bTextLocked)
        LocalUnlock(hText);
    return bSuccess;
}

/***********************************************************************
 *           NOTEPAD_ReplaceAll
 */
static VOID NOTEPAD_ReplaceAll(PFINDREPLACEDX pFindReplace)
{
    BOOL bShowAlert = TRUE;

    SendMessage(Globals.hEdit, EM_SETSEL, 0, 0);

    while (NOTEPAD_FindNext(pFindReplace, TRUE, bShowAlert))
    {
        bShowAlert = FALSE;
    }
}

/***********************************************************************
 *           NOTEPAD_FindTerm
 */
static VOID NOTEPAD_FindTerm(VOID)
{
    Globals.hFindReplaceDlg = NULL;
}

/***********************************************************************
 * Data Initialization
 */
static VOID NOTEPAD_InitData(HINSTANCE hInstance)
{
    LPTSTR p;
    static const TCHAR txt_files[] = _T("*.txt");
    static const TCHAR all_files[] = _T("*.*");

    ZeroMemory(&Globals, sizeof(Globals));
    Globals.hInstance = hInstance;
    Globals.encFile = ENCODING_DEFAULT;

    p = Globals.szFilter;
    p += LoadString(Globals.hInstance, IDS_TEXT_FILES_TXT, p, MAX_STRING_LEN) + 1;
    _tcscpy(p, txt_files);
    p += _countof(txt_files);

    p += LoadString(Globals.hInstance, IDS_ALL_FILES, p, MAX_STRING_LEN) + 1;
    _tcscpy(p, all_files);
    p += _countof(all_files);
    *p = '\0';
    Globals.find.lpstrFindWhat = NULL;

    Globals.hDevMode = NULL;
    Globals.hDevNames = NULL;
}

/***********************************************************************
 * Enable/disable items on the menu based on control state
 */
static VOID NOTEPAD_InitMenuPopup(HMENU menu, LPARAM index)
{
    DWORD dwStart, dwEnd;
    int enable;

    UNREFERENCED_PARAMETER(index);

    CheckMenuItem(menu, IDC_WRAP, (Globals.bWrapLongLines ? MF_CHECKED : MF_UNCHECKED));
    CheckMenuItem(menu, IDC_STATUSBAR, (Globals.bShowStatusBar ? MF_CHECKED : MF_UNCHECKED));
    EnableMenuItem(menu, IDC_UNDO,
        SendMessage(Globals.hEdit, EM_CANUNDO, 0, 0) ? MF_ENABLED : MF_GRAYED);
    EnableMenuItem(menu, IDC_PASTE,
        IsClipboardFormatAvailable(CF_TEXT) ? MF_ENABLED : MF_GRAYED);
    SendMessage(Globals.hEdit, EM_GETSEL, (WPARAM)&dwStart, (LPARAM)&dwEnd);
    enable = ((dwStart == dwEnd) ? MF_GRAYED : MF_ENABLED);
    EnableMenuItem(menu, IDC_CUT, enable);
    EnableMenuItem(menu, IDC_COPY, enable);
    EnableMenuItem(menu, IDC_DELETE, enable);

    EnableMenuItem(menu, IDC_SELECT_ALL,
        GetWindowTextLength(Globals.hEdit) ? MF_ENABLED : MF_GRAYED);
}

LRESULT CALLBACK EDIT_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
        case WM_KEYDOWN:
        case WM_KEYUP:
        {
            switch (wParam)
            {
                case VK_UP:
                case VK_DOWN:
                case VK_LEFT:
                case VK_RIGHT:
                    DIALOG_StatusBarUpdateCaretPos();
                    break;
                default:
                {
                    UpdateWindowCaption(FALSE);
                    break;
                }
            }
        }
        case WM_LBUTTONUP:
        {
            DIALOG_StatusBarUpdateCaretPos();
            break;
        }
    }
    return CallWindowProc( Globals.EditProc, hWnd, msg, wParam, lParam);
}

/***********************************************************************
 *           NOTEPAD_WndProc
 */
static LRESULT
WINAPI
NOTEPAD_WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {

    case WM_CREATE:
        Globals.hMainWnd = hWnd;
        Globals.hMenu = GetMenu(hWnd);

        DragAcceptFiles(hWnd, TRUE); /* Accept Drag & Drop */

        /* Create controls */
        DoCreateEditWindow();
        DoShowHideStatusBar();

        DIALOG_FileNew(); /* Initialize file info */

        // For now, the "Help" dialog is disabled due to the lack of HTML Help support
        EnableMenuItem(Globals.hMenu, IDC_HELP_CONTENTS, MF_BYCOMMAND | MF_GRAYED);
        break;

    case WM_COMMAND:
        if (HIWORD(wParam) == EN_CHANGE || HIWORD(wParam) == EN_HSCROLL || HIWORD(wParam) == EN_VSCROLL)
            DIALOG_StatusBarUpdateCaretPos();
        if ((HIWORD(wParam) == EN_CHANGE))
            NOTEPAD_EnableSearchMenu();
        NOTEPAD_MenuCommand(LOWORD(wParam));
        break;

    case WM_CLOSE:
        if (DoCloseFile())
            DestroyWindow(hWnd);
        break;

    case WM_QUERYENDSESSION:
        if (DoCloseFile()) {
            return 1;
        }
        break;

    case WM_DESTROY:
        if (Globals.hFont)
            DeleteObject(Globals.hFont);
        if (Globals.hDevMode)
            GlobalFree(Globals.hDevMode);
        if (Globals.hDevNames)
            GlobalFree(Globals.hDevNames);
        SetWindowLongPtr(Globals.hEdit, GWLP_WNDPROC, (LONG_PTR)Globals.EditProc);
        NOTEPAD_SaveSettingsToRegistry();
        PostQuitMessage(0);
        break;

    case WM_SIZE:
    {
        RECT rc;
        GetClientRect(hWnd, &rc);

        if (Globals.bShowStatusBar)
        {
            RECT rcStatus;
            SendMessageW(Globals.hStatusBar, WM_SIZE, 0, 0);
            GetWindowRect(Globals.hStatusBar, &rcStatus);
            rc.bottom -= rcStatus.bottom - rcStatus.top;
        }

        MoveWindow(Globals.hEdit, 0, 0, rc.right, rc.bottom, TRUE);

        if (Globals.bShowStatusBar)
        {
            /* Align status bar parts, only if the status bar resize operation succeeds */
            DIALOG_StatusBarAlignParts();
        }
        break;
    }

    /* The entire client area is covered by edit control and by
     * the status bar. So there is no need to erase main background.
     * This resolves the horrible flicker effect during windows resizes. */
    case WM_ERASEBKGND:
        return 1;

    case WM_SETFOCUS:
        SetFocus(Globals.hEdit);
        break;

    case WM_DROPFILES:
    {
        TCHAR szFileName[MAX_PATH];
        HDROP hDrop = (HDROP) wParam;

        DragQueryFile(hDrop, 0, szFileName, _countof(szFileName));
        DragFinish(hDrop);
        DoOpenFile(szFileName);
        break;
    }

    case WM_INITMENUPOPUP:
        NOTEPAD_InitMenuPopup((HMENU)wParam, lParam);
        break;

    default:
        if (msg == DIALOG_GetFindReplaceMsg())
        {
            PFINDREPLACEDX pFindReplace = (PFINDREPLACEDX)lParam;
            Globals.find = *(PFINDREPLACEDX)lParam;

            WaitCursor(TRUE);

            if (pFindReplace->Flags & FR_FINDNEXT)
                NOTEPAD_FindNext(pFindReplace, FALSE, TRUE);
            else if (pFindReplace->Flags & FR_REPLACE)
                NOTEPAD_FindNext(pFindReplace, TRUE, TRUE);
            else if (pFindReplace->Flags & FR_REPLACEALL)
                NOTEPAD_ReplaceAll(pFindReplace);
            else if (pFindReplace->Flags & FR_DIALOGTERM)
                NOTEPAD_FindTerm();

            WaitCursor(FALSE);
            break;
        }

        return DefWindowProc(hWnd, msg, wParam, lParam);
    }
    return 0;
}

static int AlertFileDoesNotExist(LPCTSTR szFileName)
{
    return DIALOG_StringMsgBox(Globals.hMainWnd, IDS_DOESNOTEXIST,
                               szFileName,
                               MB_ICONEXCLAMATION | MB_YESNO);
}

static BOOL HandleCommandLine(LPTSTR cmdline)
{
    BOOL opt_print = FALSE;
    TCHAR szPath[MAX_PATH];

    while (*cmdline == _T(' ') || *cmdline == _T('-') || *cmdline == _T('/'))
    {
        TCHAR option;

        if (*cmdline++ == _T(' ')) continue;

        option = *cmdline;
        if (option) cmdline++;
        while (*cmdline == _T(' ')) cmdline++;

        switch(option)
        {
            case 'p':
            case 'P':
                opt_print = TRUE;
                break;
        }
    }

    if (*cmdline)
    {
        /* file name is passed in the command line */
        LPCTSTR file_name = NULL;
        BOOL file_exists = FALSE;
        TCHAR buf[MAX_PATH];

        if (cmdline[0] == _T('"'))
        {
            cmdline++;
            cmdline[lstrlen(cmdline) - 1] = 0;
        }

        file_name = cmdline;
        if (FileExists(file_name))
        {
            file_exists = TRUE;
        }
        else if (!HasFileExtension(cmdline))
        {
            static const TCHAR txt[] = _T(".txt");

            /* try to find file with ".txt" extension */
            if (!_tcscmp(txt, cmdline + _tcslen(cmdline) - _tcslen(txt)))
            {
                file_exists = FALSE;
            }
            else
            {
                _tcsncpy(buf, cmdline, MAX_PATH - _tcslen(txt) - 1);
                _tcscat(buf, txt);
                file_name = buf;
                file_exists = FileExists(file_name);
            }
        }

        GetFullPathName(file_name, _countof(szPath), szPath, NULL);

        if (file_exists)
        {
            DoOpenFile(szPath);
            InvalidateRect(Globals.hMainWnd, NULL, FALSE);
            if (opt_print)
            {
                DIALOG_FilePrint();
                return FALSE;
            }
        }
        else
        {
            switch (AlertFileDoesNotExist(file_name))
            {
            case IDYES:
                DoOpenFile(szPath);
                break;

            case IDNO:
                break;
            }
        }
    }

    return TRUE;
}

/***********************************************************************
 *           WinMain
 */
int WINAPI _tWinMain(HINSTANCE hInstance, HINSTANCE prev, LPTSTR cmdline, int show)
{
    MSG msg;
    HACCEL hAccel;
    WNDCLASSEX wndclass;
    WINDOWPLACEMENT wp;
    static const TCHAR className[] = _T("Notepad");
    static const TCHAR winName[] = _T("Notepad");

    switch (GetUserDefaultUILanguage())
    {
    case MAKELANGID(LANG_HEBREW, SUBLANG_DEFAULT):
        SetProcessDefaultLayout(LAYOUT_RTL);
        break;

    default:
        break;
    }

    UNREFERENCED_PARAMETER(prev);

    aFINDMSGSTRING = (ATOM)RegisterWindowMessage(FINDMSGSTRING);

    NOTEPAD_InitData(hInstance);
    NOTEPAD_LoadSettingsFromRegistry(&wp);

    ZeroMemory(&wndclass, sizeof(wndclass));
    wndclass.cbSize = sizeof(wndclass);
    wndclass.lpfnWndProc = NOTEPAD_WndProc;
    wndclass.hInstance = Globals.hInstance;
    wndclass.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_NPICON));
    wndclass.hCursor = LoadCursor(NULL, IDC_ARROW);
    wndclass.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wndclass.lpszMenuName = MAKEINTRESOURCE(IDR_MAIN_MENU);
    wndclass.lpszClassName = className;
    wndclass.hIconSm = (HICON)LoadImage(hInstance,
                                        MAKEINTRESOURCE(IDI_NPICON),
                                        IMAGE_ICON,
                                        GetSystemMetrics(SM_CXSMICON),
                                        GetSystemMetrics(SM_CYSMICON),
                                        0);
    if (!RegisterClassEx(&wndclass))
    {
        ShowLastError();
        return 1;
    }

    /* Globals.hMainWnd will be set in WM_CREATE handling */
    CreateWindow(className,
                 winName,
                 WS_OVERLAPPEDWINDOW,
                 CW_USEDEFAULT,
                 CW_USEDEFAULT,
                 CW_USEDEFAULT,
                 CW_USEDEFAULT,
                 NULL,
                 NULL,
                 Globals.hInstance,
                 NULL);
    if (!Globals.hMainWnd)
    {
        ShowLastError();
        return 1;
    }

    /* Use the result of CW_USEDEFAULT if the data in the registry is not valid */
    if (wp.rcNormalPosition.right == wp.rcNormalPosition.left)
    {
        GetWindowPlacement(Globals.hMainWnd, &wp);
    }
    /* Does the parent process want to force a show action? */
    if (show != SW_SHOWDEFAULT)
    {
        wp.showCmd = show;
    }
    SetWindowPlacement(Globals.hMainWnd, &wp);
    UpdateWindow(Globals.hMainWnd);

    if (!HandleCommandLine(cmdline))
        return 0;

    hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(ID_ACCEL));

    while (GetMessage(&msg, NULL, 0, 0))
    {
        if ((!Globals.hFindReplaceDlg || !IsDialogMessage(Globals.hFindReplaceDlg, &msg)) &&
            !TranslateAccelerator(Globals.hMainWnd, hAccel, &msg))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
    }

    DestroyAcceleratorTable(hAccel);
    Globals.CyclicReplaceItems.clear();

    return (int) msg.wParam;
}
