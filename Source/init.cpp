#include "diablo.h"
#include "../3rdParty/Storm/Source/storm.h"
#include "../DiabloUI/diabloui.h"

DEVILUTION_BEGIN_NAMESPACE

_SNETVERSIONDATA fileinfo;
int gbActive;
char diablo_exe_path[MAX_PATH];
HANDLE hellfire_mpq;
char patch_rt_mpq_path[MAX_PATH];
WNDPROC CurrentProc;
HANDLE diabdat_mpq;
char diabdat_mpq_path[MAX_PATH];
HANDLE patch_rt_mpq;
BOOLEAN screensaver_enabled_prev;

/* data */

char gszVersionNumber[MAX_PATH] = "internal version unknown";
char gszProductName[MAX_PATH] = "Diablo v1.09";

void init_cleanup(BOOL show_cursor)
{
	pfile_flush_W();
	init_disable_screensaver(0);

	if (diabdat_mpq) {
		SFileCloseArchive(diabdat_mpq);
		diabdat_mpq = NULL;
	}
	if (patch_rt_mpq) {
		SFileCloseArchive(patch_rt_mpq);
		patch_rt_mpq = NULL;
	}
	if (hellfire_mpq) {
		SFileCloseArchive(hellfire_mpq);
		hellfire_mpq = NULL;
	}

	UiDestroy();
	effects_cleanup_sfx();
	sound_cleanup();
	NetClose();
	dx_cleanup();
	engine_debug_trap(show_cursor);

	if (show_cursor)
		ShowCursor(TRUE);
}

void init_disable_screensaver(BOOLEAN disable)
{
	BOOLEAN enabled;
	char Data[16];
	DWORD Type, cbData;
	HKEY phkResult;
	LRESULT success;

	// BUGFIX: this is probably the worst possible way to do this. Alternatives: ExtEscape() with SETPOWERMANAGEMENT,
	// SystemParametersInfo() with SPI_SETSCREENSAVEACTIVE/SPI_SETPOWEROFFACTIVE/SPI_SETLOWPOWERACTIVE

	success = RegOpenKeyEx(HKEY_CURRENT_USER, "Control Panel\\Desktop", 0, KEY_READ | KEY_WRITE, (PHKEY)&phkResult);
	if (success != ERROR_SUCCESS) {
		return;
	}

	if (disable) {
		cbData = 16;
		success = RegQueryValueEx(phkResult, "ScreenSaveActive", 0, &Type, (LPBYTE)Data, &cbData);
		if (success == ERROR_SUCCESS)
			screensaver_enabled_prev = Data[0] != '0';
		enabled = FALSE;
	} else {
		enabled = screensaver_enabled_prev;
	}

	Data[1] = 0;
	Data[0] = enabled ? '1' : '0';
	RegSetValueEx(phkResult, "ScreenSaveActive", 0, REG_SZ, (const BYTE *)Data, 2);
	RegCloseKey(phkResult);
}

void init_create_window(int nCmdShow)
{
	int nWidth, nHeight;
	HWND hWnd;
	WNDCLASSEXA wcex;

	pfile_init_save_directory();
	memset(&wcex, 0, sizeof(wcex));
	wcex.cbSize = sizeof(wcex);
	wcex.style = CS_HREDRAW | CS_VREDRAW;
	wcex.lpfnWndProc = WindowProc;
	wcex.hInstance = ghInst;
	wcex.hIcon = LoadIcon(ghInst, MAKEINTRESOURCE(IDI_ICON1));
	wcex.hCursor = LoadCursor(0, IDC_ARROW);
	wcex.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
	wcex.lpszMenuName = "DIABLO";
	wcex.lpszClassName = "DIABLO";
	wcex.hIconSm = (HICON)LoadImage(ghInst, MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
	if (!RegisterClassEx(&wcex))
		app_fatal("Unable to register window class");
	if (GetSystemMetrics(SM_CXSCREEN) < SCREEN_WIDTH)
		nWidth = SCREEN_WIDTH;
	else
		nWidth = GetSystemMetrics(SM_CXSCREEN);
	if (GetSystemMetrics(SM_CYSCREEN) < SCREEN_HEIGHT)
		nHeight = SCREEN_HEIGHT;
	else
		nHeight = GetSystemMetrics(SM_CYSCREEN);
	hWnd = CreateWindowEx(0, "DIABLO", "DIABLO", WS_POPUP, 0, 0, nWidth, nHeight, NULL, NULL, ghInst, NULL);
	if (!hWnd)
		app_fatal("Unable to create main window");
	ShowWindow(hWnd, SW_SHOWNORMAL); // nCmdShow used only in beta: ShowWindow(hWnd, nCmdShow)
	UpdateWindow(hWnd);
	dx_init(hWnd);
	BlackPalette();
	snd_init(hWnd);
	init_archives();
	init_disable_screensaver(1);
}

void init_archives()
{
	HANDLE fh;
#ifdef COPYPROT
	int result;
#endif
	memset(&fileinfo, 0, sizeof(fileinfo));
	fileinfo.size = sizeof(fileinfo);
	fileinfo.versionstring = gszVersionNumber;
	fileinfo.executablefile = diablo_exe_path;
	fileinfo.originalarchivefile = diabdat_mpq_path;
	fileinfo.patcharchivefile = patch_rt_mpq_path;
	init_get_file_info();
#ifdef COPYPROT
	while (1) {
#endif
#ifdef SPAWN
		diabdat_mpq = init_test_access(diabdat_mpq_path, "\\spawn.mpq", "DiabloSpawn", MPQ_FLAG_READ_ONLY, FS_PC);
#else
#ifdef COPYPROT
		diabdat_mpq = init_test_access(diabdat_mpq_path, "\\diabdat.mpq", "DiabloCD", MPQ_FLAG_READ_ONLY, FS_CD);
#else
		diabdat_mpq = init_test_access(diabdat_mpq_path, "\\diabdat.mpq", "DiabloCD", MPQ_FLAG_READ_ONLY, FS_PC);
#endif
#endif
#ifdef COPYPROT
		if (diabdat_mpq != NULL)
			break;
		UiCopyProtError(&result);
		if (result == COPYPROT_CANCEL)
			FileErrDlg("diabdat.mpq");
	}
#endif
	if (!WOpenFile("ui_art\\title.pcx", &fh, TRUE))
#ifdef SPAWN
		FileErrDlg("Main program archive: spawn.mpq");
#else
		FileErrDlg("Main program archive: diabdat.mpq");
#endif
	WCloseFile(fh);
#ifdef SPAWN
	patch_rt_mpq = init_test_access(patch_rt_mpq_path, "\\patch_sh.mpq", "DiabloSpawn", MPQ_FLAG_READ_ONLY, FS_PC);
#else
	patch_rt_mpq = init_test_access(patch_rt_mpq_path, "\\patch_rt.mpq", "DiabloInstall", MPQ_FLAG_READ_ONLY, FS_PC);
#endif
}

HANDLE init_test_access(char *mpq_path, char *mpq_name, char *reg_loc, int flags, int fs)
{
	char *last_slash_pos;
	char Filename[MAX_PATH];
	char Buffer[MAX_PATH];
	char archive_path[MAX_PATH];
	HANDLE archive;

	if (!GetCurrentDirectory(sizeof(Buffer), Buffer))
		app_fatal("Can't get program path");
	init_strip_trailing_slash(Buffer);
	if (!SFileSetBasePath(Buffer))
		app_fatal("SFileSetBasePath");
	if (!GetModuleFileName(ghInst, Filename, sizeof(Filename)))
		app_fatal("Can't get program name");
	last_slash_pos = strrchr(Filename, '\\');
	if (last_slash_pos)
		*last_slash_pos = '\0';
	init_strip_trailing_slash(Filename);
	strcpy(mpq_path, Buffer);
	strcat(mpq_path, mpq_name);
	if (SFileOpenArchive(mpq_path, 0, flags, &archive))
		return archive;
	if (strcmp(Filename, Buffer)) {
		strcpy(mpq_path, Filename);
		strcat(mpq_path, mpq_name);
		if (SFileOpenArchive(mpq_path, 0, flags, &archive))
			return archive;
	}
	archive_path[0] = '\0';
	if (reg_loc) {
		if (SRegLoadString("Archives", reg_loc, 0, archive_path, sizeof(archive_path))) {
			init_strip_trailing_slash(archive_path);
			strcpy(mpq_path, archive_path);
			strcat(mpq_path, mpq_name);
			if (SFileOpenArchive(mpq_path, 0, flags, &archive))
				return archive;
		}
	}
	if (fs != FS_PC && init_read_test_file(archive_path, mpq_name, flags, &archive)) {
		strcpy(mpq_path, archive_path);
		return archive;
	}
	return NULL;
}

char *init_strip_trailing_slash(char *path)
{
	char *result;

	result = strrchr(path, '\\');
	if (result) {
		if (!result[1])
			*result = 0;
	}
	return result;
}

BOOL init_read_test_file(char *pszPath, char *pszArchive, int flags, HANDLE *phArchive)
{
	DWORD dwSize;
	char *pszDrive, *pszRoot;
	char szDrive[MAX_PATH];

	dwSize = GetLogicalDriveStrings(sizeof(szDrive), szDrive);
	if (dwSize == 0 || dwSize > sizeof(szDrive)) {
		return FALSE;
	}

	while (*pszArchive == '\\') {
		pszArchive++;
	}

	pszDrive = szDrive;
	if (*pszDrive == '\0') {
		return FALSE;
	}
	while (1) {
		pszRoot = pszDrive;
		while (*pszDrive++ != '\0')
			;
		if (GetDriveType(pszRoot) == DRIVE_CDROM) {
			strcpy(pszPath, pszRoot);
			strcat(pszPath, pszArchive);
			if (SFileOpenArchive(pszPath, 0, flags, phArchive)) {
				return TRUE;
			}
		}
		if (*pszDrive == '\0') {
			return FALSE;
		}
	}

	return TRUE;
}

void init_get_file_info()
{
	DWORD dwLen;
	void *pBlock;
	unsigned int uBytes;
	DWORD dwHandle;
	VS_FIXEDFILEINFO *lpBuffer;

	if (GetModuleFileName(ghInst, diablo_exe_path, sizeof(diablo_exe_path))) {
		dwLen = GetFileVersionInfoSize(diablo_exe_path, &dwHandle);
		if (dwLen) {
			pBlock = DiabloAllocPtr(dwLen);
			if (GetFileVersionInfo(diablo_exe_path, 0, dwLen, pBlock)) {
				if (VerQueryValue(pBlock, "\\", (LPVOID *)&lpBuffer, &uBytes))
					sprintf(
					    gszVersionNumber,
					    "version %d.%d.%d.%d",
					    lpBuffer->dwProductVersionMS >> 16,
					    lpBuffer->dwProductVersionMS & 0xFFFF,
					    lpBuffer->dwProductVersionLS >> 16,
					    lpBuffer->dwProductVersionLS & 0xFFFF);
			}
			mem_free_dbg(pBlock);
		}
	}
}

LRESULT __stdcall MainWndProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	switch (Msg) {
	case WM_ERASEBKGND:
		return 0;
	case WM_CREATE:
		ghMainWnd = hWnd;
		break;
	case WM_DESTROY:
		init_cleanup(1);
		ghMainWnd = 0;
		PostQuitMessage(0);
		break;
	case WM_PAINT:
		drawpanflag = 255;
		break;
	case WM_CLOSE:
		return 0;
	case WM_ACTIVATEAPP:
		init_activate_window(hWnd, wParam);
		break;
#ifdef _DEBUG
	case WM_SYSKEYUP:
		if (wParam == VK_RETURN) {
			fullscreen = !fullscreen;
			dx_reinit();
			return 0;
		}
		break;
#endif
	case WM_QUERYNEWPALETTE:
		return 1;
	}

	return DefWindowProc(hWnd, Msg, wParam, lParam);
}

void init_activate_window(HWND hWnd, BOOL bActive)
{
	LONG dwNewLong;

	gbActive = bActive;
	UiAppActivate(bActive);
	dwNewLong = GetWindowLong(hWnd, GWL_STYLE);

	if (gbActive && fullscreen)
		dwNewLong &= ~WS_SYSMENU;
	else
		dwNewLong |= WS_SYSMENU;

	SetWindowLong(hWnd, GWL_STYLE, dwNewLong);

	if (gbActive) {
		drawpanflag = 255;
		ResetPal();
	}
}

LRESULT __stdcall WindowProc(HWND hWnd, UINT Msg, WPARAM wParam, LPARAM lParam)
{
	if (CurrentProc)
		return CurrentProc(hWnd, Msg, wParam, lParam);

	return MainWndProc(hWnd, Msg, wParam, lParam);
}

WNDPROC SetWindowProc(WNDPROC NewProc)
{
	WNDPROC OldProc;

	OldProc = CurrentProc;
	CurrentProc = NewProc;
	return OldProc;
}

DEVILUTION_END_NAMESPACE
