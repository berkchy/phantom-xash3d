/*
game.cpp -- executable to run Xash Engine
Copyright (C) 2011 Uncle Mike

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "port.h"
#include "build.h"
#include "xash3d_types.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#if XASH_WIN32
#include <sys/utime.h>
#ifndef S_ISREG
#define S_ISREG( m ) (((m) & _S_IFMT ) == _S_IFREG )
#endif
#ifndef S_ISDIR
#define S_ISDIR( m ) (((m) & _S_IFMT ) == _S_IFDIR )
#endif
#else
#include <utime.h>
#endif

#if XASH_POSIX
#include <dlfcn.h>
#include <dirent.h>
#include <unistd.h>
#define XASHLIB OS_LIB_PREFIX "xash." OS_LIB_EXT
#define FreeLibrary( x ) dlclose( x )
#elif XASH_WIN32
#include <shellapi.h> // CommandLineToArgvW
#include <windows.h>
#include <direct.h>
#define XASHLIB L"xash.dll"
#define SDL2LIB L"SDL2.dll"

extern "C"
{
// Enable NVIDIA High Performance Graphics while using Integrated Graphics.
__declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;

// Enable AMD High Performance Graphics while using Integrated Graphics.
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
#else
#error // port me!
#endif

#ifndef XASH_GAMEDIR
#define XASH_GAMEDIR "valve" // !!! Replace with your default (base) game directory !!!
#endif

typedef void (*pfnChangeGame)( const char *progname );
typedef int  (*pfnInit)( int argc, char **argv, const char *progname, int bChangeGame, pfnChangeGame func );
typedef void (*pfnShutdown)( void );

static pfnInit     Host_Main;
static pfnShutdown Host_Shutdown = NULL;
static int         szArgc;
static char        **szArgv;
static HINSTANCE   hEngine;

static inline size_t Game_Q_strncpy( char *dst, const char *src, size_t size )
{
	size_t len;

	if( !dst || !src || !size )
		return 0;

	len = strlen( src );
	if( len + 1 > size )
	{
		memcpy( dst, src, size - 1 );
		dst[size - 1] = 0;
	}
	else
		memcpy( dst, src, len + 1 );

	return len;
}

static inline int Game_Q_snprintf( char *dst, size_t size, const char *fmt, ... )
{
	va_list args;
	int ret;

	va_start( args, fmt );
	ret = vsnprintf( dst, size, fmt, args );
	va_end( args );

	return ret;
}

struct launch_config_t
{
	const char *gameDir;
	qboolean dedicated;
};

static void Launch_Error( const char *szFmt, ... )
{
	static char	buffer[16384];	// must support > 1k messages
	va_list		args;

	va_start( args, szFmt );
	vsnprintf( buffer, sizeof(buffer), szFmt, args );
	va_end( args );

#if XASH_WIN32
	MessageBoxA( NULL, buffer, "Xash Error", MB_OK );
#else
	fprintf( stderr, "Xash Error: %s\n", buffer );
#endif

	exit( 1 );
}

#if XASH_WIN32
static const char *GetStringLastError()
{
	static char buf[1024];

	FormatMessageA( FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		NULL, GetLastError(), MAKELANGID( LANG_ENGLISH, SUBLANG_DEFAULT ),
		buf, sizeof( buf ), NULL );

	return buf;
}
#endif

static void Sys_LoadEngine( void )
{
#if XASH_WIN32
	HMODULE hSDL = LoadLibraryExW( SDL2LIB, NULL, LOAD_LIBRARY_AS_DATAFILE );

	if( !hSDL )
	{
		Launch_Error("Unable to load %ls: %s", SDL2LIB, GetStringLastError( ));
		return;
	}

	FreeLibrary( hSDL );

	hEngine = LoadLibraryW( XASHLIB );
	if( !hEngine )
	{
		Launch_Error( "Unable to load %ls: %s", XASHLIB, GetStringLastError( ));
		return;
	}

	Host_Main = (pfnInit)GetProcAddress( hEngine, "Host_Main" );

	if( !Host_Main )
	{
		Launch_Error( "%ls missed 'Host_Main' export: %s", XASHLIB, GetStringLastError( ));
		return;
	}

	Host_Shutdown = (pfnShutdown)GetProcAddress( hEngine, "Host_Shutdown" );
#elif XASH_POSIX
	hEngine = dlopen( XASHLIB, RTLD_NOW );
	if( !hEngine )
	{
		Launch_Error( "Unable to load %s: %s", XASHLIB, dlerror( ));
		return;
	}

	Host_Main = (pfnInit)dlsym( hEngine, "Host_Main" );

	if( !Host_Main )
	{
		Launch_Error( "%s missed 'Host_Main' export: %s", XASHLIB, dlerror( ));
		return;
	}

	Host_Shutdown = (pfnShutdown)dlsym( hEngine, "Host_Shutdown" );
#else
#error "port me!"
#endif
}

static void Sys_UnloadEngine( void )
{
	if( Host_Shutdown )
		Host_Shutdown( );

	if( hEngine )
		FreeLibrary( hEngine );

	hEngine = NULL;
	Host_Main = NULL;
	Host_Shutdown = NULL;
}

static void Sys_ChangeGame( const char *progname )
{
	// presence of this function tells the engine to allow change game
	// but it's never called
	return;
}

static int Game_ArgCmp( const char *arg, const char *name )
{
	return arg && name ? strcmp( arg, name ) : 1;
}

static qboolean Game_HasArg( const char *name )
{
	for( int i = 1; i < szArgc; ++i )
	{
		if( !Game_ArgCmp( szArgv[i], name ))
			return true;
	}
	return false;
}

static const char *Game_GetValueArg( const char *name )
{
	for( int i = 1; i < szArgc; ++i )
	{
		if( !Game_ArgCmp( szArgv[i], name ) && i + 1 < szArgc )
			return szArgv[i + 1];

		if( szArgv[i] && !strncmp( szArgv[i], name, strlen( name ) ) && szArgv[i][strlen( name )] == '=' )
			return szArgv[i] + strlen( name ) + 1;
	}

	return NULL;
}

static qboolean Game_ShouldShowLauncher( void )
{
	if( szArgc <= 1 )
		return true;

	for( int i = 1; i < szArgc; ++i )
	{
		if( szArgv[i] && !strcmp( szArgv[i], "-launcher" ) )
			return true;
	}

	return false;
}

static const char *Game_GetDefaultDir( void )
{
	const char *gamedir = Game_GetValueArg( "-game" );
	if( gamedir && gamedir[0] )
		return gamedir;
	return XASH_GAMEDIR;
}

static qboolean Game_IsDedicatedRequested( void )
{
	return Game_HasArg( "-dedicated" );
}

static qboolean Game_IsDir( const char *path )
{
	struct stat st;
	return path && stat( path, &st ) == 0 && S_ISDIR( st.st_mode );
}

static qboolean Game_IsFile( const char *path )
{
	struct stat st;
	return path && stat( path, &st ) == 0 && S_ISREG( st.st_mode );
}

static void Game_MakeDirTree( const char *path )
{
	char buffer[1024];
	char *walk;

	if( !path || !path[0] )
		return;

	Game_Q_strncpy( buffer, path, sizeof( buffer ));

#if XASH_WIN32
	walk = buffer;
	if( isalpha( (unsigned char)walk[0] ) && walk[1] == ':' && ( walk[2] == '\\' || walk[2] == '/' ))
		walk += 3;
#else
	walk = buffer + 1;
#endif

	for( char *p = walk; *p; ++p )
	{
		if( *p == '/' || *p == '\\' )
		{
			char saved = *p;
			*p = 0;
			if( buffer[0] )
			{
#if XASH_WIN32
				_mkdir( buffer );
#else
				mkdir( buffer, 0755 );
#endif
			}
			*p = saved;
		}
	}

	if( buffer[0] )
	{
#if XASH_WIN32
		_mkdir( buffer );
#else
		mkdir( buffer, 0755 );
#endif
	}
}

static void Game_CopyFileIfMissing( const char *src, const char *dst )
{
	struct stat srcSt;
	struct stat dstSt;

	if( stat( src, &srcSt ) != 0 || !S_ISREG( srcSt.st_mode ))
		return;

	if( stat( dst, &dstSt ) == 0 && S_ISREG( dstSt.st_mode ) &&
		dstSt.st_size == srcSt.st_size && dstSt.st_mtime == srcSt.st_mtime )
		return;

	FILE *in = fopen( src, "rb" );
	if( !in )
		return;

	char dstPath[1024];
	char dstDir[1024];
	Game_Q_strncpy( dstPath, dst, sizeof( dstPath ));

	Game_Q_strncpy( dstDir, dstPath, sizeof( dstDir ));
	{
		char *slash = strrchr( dstDir, '\\' );
		char *slash2 = strrchr( dstDir, '/' );
		if( slash2 && ( !slash || slash2 > slash ))
			slash = slash2;
		if( slash )
		{
			*slash = 0;
			Game_MakeDirTree( dstDir );
		}
	}

	FILE *out = fopen( dst, "wb" );
	if( !out )
	{
		fclose( in );
		return;
	}

	char buf[8192];
	size_t readCount;
	while( ( readCount = fread( buf, 1, sizeof( buf ), in )) > 0 )
		fwrite( buf, 1, readCount, out );

	fclose( out );
	fclose( in );

#if XASH_WIN32
	struct _utimbuf times;
	times.actime = srcSt.st_atime;
	times.modtime = srcSt.st_mtime;
	_utime( dst, &times );
#else
	struct utimbuf times;
	times.actime = srcSt.st_atime;
	times.modtime = srcSt.st_mtime;
	utime( dst, &times );
#endif
}

static void Game_SyncDir( const char *srcDir, const char *dstDir )
{
	struct stat st;
	if( stat( srcDir, &st ) != 0 || !S_ISDIR( st.st_mode ))
		return;

#if XASH_WIN32
	char pattern[1024];
	Game_Q_snprintf( pattern, sizeof( pattern ), "%s\\*.*", srcDir );
	WIN32_FIND_DATAA data;
	HANDLE handle = FindFirstFileA( pattern, &data );
	if( handle == INVALID_HANDLE_VALUE )
		return;

	do
	{
		if( !strcmp( data.cFileName, "." ) || !strcmp( data.cFileName, ".." ))
			continue;
		if( data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY )
			continue;

		char src[1024];
		char dst[1024];
		Game_Q_snprintf( src, sizeof( src ), "%s\\%s", srcDir, data.cFileName );
		Game_Q_snprintf( dst, sizeof( dst ), "%s\\%s", dstDir, data.cFileName );
		Game_CopyFileIfMissing( src, dst );
	} while( FindNextFileA( handle, &data ) );

	FindClose( handle );
#else
	DIR *dir = opendir( srcDir );
	if( !dir )
		return;

	struct dirent *entry;
	while( ( entry = readdir( dir )) != NULL )
	{
		if( !strcmp( entry->d_name, "." ) || !strcmp( entry->d_name, ".." ))
			continue;

		char src[1024];
		char dst[1024];
		Game_Q_snprintf( src, sizeof( src ), "%s/%s", srcDir, entry->d_name );
		Game_Q_snprintf( dst, sizeof( dst ), "%s/%s", dstDir, entry->d_name );
		Game_CopyFileIfMissing( src, dst );
	}

	closedir( dir );
#endif
}

static qboolean Game_SelectAssetRoot( const char *gameDir, char *dst, size_t dstSize )
{
	const char *cwd = NULL;
	char candidate[1024];
	const char *envRoot = getenv( "XASH3D_ASSETDIR" );

	if( envRoot && envRoot[0] )
	{
		Game_Q_strncpy( candidate, envRoot, sizeof( candidate ));
		if( Game_IsDir( candidate ) )
		{
			Game_Q_strncpy( dst, candidate, dstSize );
			return true;
		}
	}

#if XASH_WIN32
	if( GetCurrentDirectoryA( sizeof( candidate ) - 1, candidate ) )
		cwd = candidate;
#else
	if( getcwd( candidate, sizeof( candidate ) - 1 ))
		cwd = candidate;
#endif

	if( cwd && cwd[0] )
	{
		const char *candidatesCS[] = {
			"%s%ccs16-client%c3rdparty%ccs16client-extras",
			"%s%cxash3d-fwgs%c3rdparty%cextras%cxash-extras",
			"%s%cassets",
			"%s%c3rdparty%ccs16client-extras",
			"%s%c3rdparty%cextras%cxash-extras"
		};
		const char *candidatesHL[] = {
			"%s%cxash3d-fwgs%c3rdparty%cextras%cxash-extras",
			"%s%ccs16-client%c3rdparty%ccs16client-extras",
			"%s%cassets",
			"%s%c3rdparty%cextras%cxash-extras",
			"%s%c3rdparty%ccs16client-extras"
		};
		const char **candidates = !gameDir || !stricmp( gameDir, "cstrike" ) ? candidatesCS : candidatesHL;
		const size_t numCandidates = !gameDir || !stricmp( gameDir, "cstrike" )
			? ( sizeof( candidatesCS ) / sizeof( candidatesCS[0] ))
			: ( sizeof( candidatesHL ) / sizeof( candidatesHL[0] ));
		const char sep =
#if XASH_WIN32
			'\\';
#else
			'/';
#endif

		for( size_t i = 0; i < numCandidates; ++i )
		{
			Game_Q_snprintf( candidate, sizeof( candidate ), candidates[i], cwd, sep, sep, sep, sep );
			if( Game_IsDir( candidate ) )
			{
				Game_Q_strncpy( dst, candidate, dstSize );
				return true;
			}
		}
	}

	return false;
}

static void Game_SyncAssets( const char *gameDir )
{
	char baseDir[1024] = {0};
	char assetRoot[1024] = {0};
	char srcRoot[1024] = {0};
	char dstRoot[1024] = {0};
	const char sep =
#if XASH_WIN32
		'\\';
#else
		'/';
#endif

#if XASH_WIN32
	char exePath[1024] = {0};
	if( !GetModuleFileNameA( NULL, exePath, sizeof( exePath ) - 1 ))
		return;
	char *slash = strrchr( exePath, '\\' );
	if( slash )
		*slash = 0;
#else
	char exePath[1024] = {0};
	ssize_t len = readlink( "/proc/self/exe", exePath, sizeof( exePath ) - 1 );
	if( len <= 0 )
		return;
	exePath[len] = 0;
	char *slash = strrchr( exePath, '/' );
	if( slash )
		*slash = 0;
#endif

	const char *envBase = getenv( "XASH3D_BASEDIR" );
	if( envBase && envBase[0] )
		Game_Q_strncpy( baseDir, envBase, sizeof( baseDir ));
	else
	{
#if XASH_WIN32
		if( !GetCurrentDirectoryA( sizeof( baseDir ) - 1, baseDir ))
			return;
#else
		if( !getcwd( baseDir, sizeof( baseDir ) - 1 ))
			return;
#endif
	}

	if( Game_SelectAssetRoot( gameDir, assetRoot, sizeof( assetRoot ) ) )
		Game_Q_strncpy( srcRoot, assetRoot, sizeof( srcRoot ));
	else
		Game_Q_snprintf( srcRoot, sizeof( srcRoot ), "%s%cassets", exePath, sep );
	Game_Q_snprintf( dstRoot, sizeof( dstRoot ), "%s%c%s", baseDir, sep, gameDir ? gameDir : XASH_GAMEDIR );

	char srcSprites[1024];
	char dstSprites[1024];
	char srcSounds[1024];
	char dstSounds[1024];
	char srcFonts[1024];
	char dstFonts[1024];
	char srcShell[1024];
	char dstShell[1024];
	char srcFontsWad[1024];
	char dstFontsWad[1024];
	char srcGfxWad[1024];
	char dstGfxWad[1024];
	char srcGameUi[1024];
	char dstGameUi[1024];
	char dstResource[1024];
	Game_Q_snprintf( srcSprites, sizeof( srcSprites ), "%s%csprites", srcRoot, sep );
	Game_Q_snprintf( dstSprites, sizeof( dstSprites ), "%s%csprites", dstRoot, sep );
	Game_Q_snprintf( srcSounds, sizeof( srcSounds ), "%s%sound%cvox", srcRoot, sep == '\\' ? "\\" : "/", sep );
	Game_Q_snprintf( dstSounds, sizeof( dstSounds ), "%s%sound%cvox", dstRoot, sep == '\\' ? "\\" : "/", sep );
	Game_Q_snprintf( srcFonts, sizeof( srcFonts ), "%s%cgfx%cfonts", srcRoot, sep, sep );
	Game_Q_snprintf( dstFonts, sizeof( dstFonts ), "%s%cgfx%cfonts", dstRoot, sep, sep );
	Game_Q_snprintf( srcShell, sizeof( srcShell ), "%s%cgfx%cshell", srcRoot, sep, sep );
	Game_Q_snprintf( dstShell, sizeof( dstShell ), "%s%cgfx%cshell", dstRoot, sep, sep );
	Game_Q_snprintf( srcFontsWad, sizeof( srcFontsWad ), "%s%cfonts.wad", srcRoot, sep );
	Game_Q_snprintf( dstFontsWad, sizeof( dstFontsWad ), "%s%cfonts.wad", dstRoot, sep );
	Game_Q_snprintf( srcGfxWad, sizeof( srcGfxWad ), "%s%cgfx.wad", srcRoot, sep );
	Game_Q_snprintf( dstGfxWad, sizeof( dstGfxWad ), "%s%cgfx.wad", dstRoot, sep );
	Game_Q_snprintf( srcGameUi, sizeof( srcGameUi ), "%s%cresource%cgameui_english.txt", srcRoot, sep, sep );
	Game_Q_snprintf( dstGameUi, sizeof( dstGameUi ), "%s%cresource%cgameui_english.txt", dstRoot, sep, sep );
	Game_Q_snprintf( dstResource, sizeof( dstResource ), "%s%cresource", dstRoot, sep );

	Game_MakeDirTree( dstRoot );
	Game_MakeDirTree( dstSprites );
	Game_MakeDirTree( dstSounds );
	Game_MakeDirTree( dstFonts );
	Game_MakeDirTree( dstShell );
	Game_MakeDirTree( dstResource );

	Game_SyncDir( srcSprites, dstSprites );
	Game_SyncDir( srcSounds, dstSounds );
	Game_SyncDir( srcFonts, dstFonts );
	Game_SyncDir( srcShell, dstShell );
	Game_CopyFileIfMissing( srcFontsWad, dstFontsWad );
	Game_CopyFileIfMissing( srcGfxWad, dstGfxWad );
	Game_CopyFileIfMissing( srcGameUi, dstGameUi );

	{
		char srcBrand[1024];
		char dstBrand[1024];
		const char *brandCandidates[] = {
			"cs_regular.ttf",
			"FiraSans-Regular.ttf",
			"tahoma.ttf"
		};
		const size_t numCandidates = sizeof( brandCandidates ) / sizeof( brandCandidates[0] );
		for( size_t i = 0; i < numCandidates; ++i )
		{
			Game_Q_snprintf( srcBrand, sizeof( srcBrand ), "%s%cgfx%cfonts%c%s", srcRoot, sep, sep, sep, brandCandidates[i] );
			if( Game_IsFile( srcBrand ) )
			{
				Game_Q_snprintf( dstBrand, sizeof( dstBrand ), "%s%cgfx%cfonts%ccs_regular.ttf", dstRoot, sep, sep, sep );
				Game_CopyFileIfMissing( srcBrand, dstBrand );
				break;
			}
		}
	}
}

#if XASH_WIN32
enum
{
	LAUNCHER_ID_GAME_GRP = 1001,
	LAUNCHER_ID_GAME_CS,
	LAUNCHER_ID_GAME_HL,
	LAUNCHER_ID_MODE_GRP,
	LAUNCHER_ID_MODE_CLIENT,
	LAUNCHER_ID_MODE_DEDICATED,
	LAUNCHER_ID_LAUNCH,
	LAUNCHER_ID_EXIT
};

struct game_launcher_ui_t
{
	HWND hwnd;
	HWND title;
	HWND gameGroup;
	HWND modeGroup;
	HWND csRadio;
	HWND hlRadio;
	HWND clientRadio;
	HWND dedicatedRadio;
	HWND launchBtn;
	HWND exitBtn;
	HFONT font;
	launch_config_t cfg;
	qboolean finished;
	qboolean canceled;
};

static void Game_LauncherApplyFont( HWND hwnd, HFONT font )
{
	if( hwnd && font )
		SendMessageA( hwnd, WM_SETFONT, (WPARAM)font, TRUE );
}

static qboolean Game_LauncherGetCheck( HWND hwnd, int id )
{
	return SendDlgItemMessageA( hwnd, id, BM_GETCHECK, 0, 0 ) == BST_CHECKED;
}

static void Game_LauncherReadConfig( game_launcher_ui_t *ui )
{
	if( !ui || !ui->hwnd )
		return;

	ui->cfg.gameDir = Game_LauncherGetCheck( ui->hwnd, LAUNCHER_ID_GAME_HL ) ? "valve" : "cstrike";
	ui->cfg.dedicated = Game_LauncherGetCheck( ui->hwnd, LAUNCHER_ID_MODE_DEDICATED );
}

static void Game_LauncherCreateControls( HWND hwnd, game_launcher_ui_t *ui )
{
	const HFONT font = (HFONT)GetStockObject( DEFAULT_GUI_FONT );
	const HINSTANCE hInst = GetModuleHandleA( NULL );
	const int leftX = 20;
	const int rightX = 228;
	const int topY = 50;
	const int groupW = 192;
	const int groupH = 120;
	const int margin = 16;

	ui->font = font;

	ui->title = CreateWindowA( "STATIC", "Choose game and launch mode",
		WS_CHILD | WS_VISIBLE,
		leftX, 16, 280, 20, hwnd, NULL, hInst, NULL );
	Game_LauncherApplyFont( ui->title, font );

	ui->gameGroup = CreateWindowA( "BUTTON", "Game",
		WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
		leftX, topY, groupW, groupH, hwnd, (HMENU)LAUNCHER_ID_GAME_GRP, hInst, NULL );
	Game_LauncherApplyFont( ui->gameGroup, font );

	ui->modeGroup = CreateWindowA( "BUTTON", "Mode",
		WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
		rightX, topY, groupW, groupH, hwnd, (HMENU)LAUNCHER_ID_MODE_GRP, hInst, NULL );
	Game_LauncherApplyFont( ui->modeGroup, font );

	ui->csRadio = CreateWindowA( "BUTTON", "Counter-Strike",
		WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
		leftX + margin, topY + 28, 150, 22, hwnd, (HMENU)LAUNCHER_ID_GAME_CS, hInst, NULL );
	Game_LauncherApplyFont( ui->csRadio, font );

	ui->hlRadio = CreateWindowA( "BUTTON", "Half-Life",
		WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
		leftX + margin, topY + 56, 150, 22, hwnd, (HMENU)LAUNCHER_ID_GAME_HL, hInst, NULL );
	Game_LauncherApplyFont( ui->hlRadio, font );

	ui->clientRadio = CreateWindowA( "BUTTON", "Client",
		WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
		rightX + margin, topY + 28, 150, 22, hwnd, (HMENU)LAUNCHER_ID_MODE_CLIENT, hInst, NULL );
	Game_LauncherApplyFont( ui->clientRadio, font );

	ui->dedicatedRadio = CreateWindowA( "BUTTON", "Dedicated server",
		WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
		rightX + margin, topY + 56, 150, 22, hwnd, (HMENU)LAUNCHER_ID_MODE_DEDICATED, hInst, NULL );
	Game_LauncherApplyFont( ui->dedicatedRadio, font );

	ui->launchBtn = CreateWindowA( "BUTTON", "Launch",
		WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON,
		220, 202, 92, 28, hwnd, (HMENU)LAUNCHER_ID_LAUNCH, hInst, NULL );
	Game_LauncherApplyFont( ui->launchBtn, font );

	ui->exitBtn = CreateWindowA( "BUTTON", "Exit",
		WS_CHILD | WS_VISIBLE,
		324, 202, 92, 28, hwnd, (HMENU)LAUNCHER_ID_EXIT, hInst, NULL );
	Game_LauncherApplyFont( ui->exitBtn, font );

	SendDlgItemMessageA( hwnd, ui->cfg.gameDir && !strcmp( ui->cfg.gameDir, "valve" ) ? LAUNCHER_ID_GAME_HL : LAUNCHER_ID_GAME_CS, BM_SETCHECK, BST_CHECKED, 0 );
	SendDlgItemMessageA( hwnd, ui->cfg.dedicated ? LAUNCHER_ID_MODE_DEDICATED : LAUNCHER_ID_MODE_CLIENT, BM_SETCHECK, BST_CHECKED, 0 );
}

static LRESULT CALLBACK Game_LauncherWndProc( HWND hwnd, UINT msg, WPARAM wp, LPARAM lp )
{
	game_launcher_ui_t *ui = (game_launcher_ui_t*)GetWindowLongPtrA( hwnd, GWLP_USERDATA );

	switch( msg )
	{
		case WM_CREATE:
		{
			LPCREATESTRUCTA cs = (LPCREATESTRUCTA)lp;
			ui = (game_launcher_ui_t*)cs->lpCreateParams;
			SetWindowLongPtrA( hwnd, GWLP_USERDATA, (LONG_PTR)ui );
			ui->hwnd = hwnd;
			Game_LauncherCreateControls( hwnd, ui );
			return 0;
		}
		case WM_COMMAND:
			switch( LOWORD( wp ) )
			{
				case LAUNCHER_ID_LAUNCH:
					Game_LauncherReadConfig( ui );
					ui->finished = true;
					DestroyWindow( hwnd );
					return 0;
				case LAUNCHER_ID_EXIT:
					ui->canceled = true;
					DestroyWindow( hwnd );
					return 0;
			}
			break;
		case WM_CLOSE:
			ui->canceled = true;
			DestroyWindow( hwnd );
			return 0;
		case WM_DESTROY:
			PostQuitMessage( 0 );
			return 0;
	}

	return DefWindowProcA( hwnd, msg, wp, lp );
}

static qboolean Game_ShowLauncherWindow( launch_config_t *cfg )
{
	WNDCLASSA wc;
	MSG msg;
	game_launcher_ui_t ui;
	RECT rc = { 0, 0, 440, 260 };
	HWND hwnd;
	HINSTANCE hInst = GetModuleHandleA( NULL );

	memset( &ui, 0, sizeof( ui ));
	ui.cfg = *cfg;

	memset( &wc, 0, sizeof( wc ));
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = Game_LauncherWndProc;
	wc.hInstance = hInst;
	wc.hCursor = LoadCursorA( NULL, IDC_ARROW );
	wc.hbrBackground = (HBRUSH)( COLOR_WINDOW + 1 );
	wc.lpszClassName = "Xash3DLauncherWindow";
	RegisterClassA( &wc );

	AdjustWindowRect( &rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE );
	hwnd = CreateWindowExA(
		WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW,
		wc.lpszClassName,
		"Xash3D Launcher",
		WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
		CW_USEDEFAULT, CW_USEDEFAULT,
		rc.right - rc.left,
		rc.bottom - rc.top,
		NULL, NULL, hInst, &ui );

	if( !hwnd )
		return false;

	ShowWindow( hwnd, SW_SHOW );
	UpdateWindow( hwnd );

	while( !ui.finished && !ui.canceled && GetMessageA( &msg, NULL, 0, 0 ) > 0 )
	{
		TranslateMessage( &msg );
		DispatchMessageA( &msg );
	}

	if( ui.canceled )
		return false;

	*cfg = ui.cfg;
	return true;
}
#endif

static void Game_PushArg( char ***argv, int *argc, const char *value )
{
	*argv = (char**)realloc( *argv, sizeof( char* ) * ( *argc + 2 ));
	(*argv)[*argc] = strdup( value );
	(*argc)++;
	(*argv)[*argc] = NULL;
}

static int Game_RunEngine( const launch_config_t &cfg )
{
	char **launchArgv = szArgv;
	int launchArgc = szArgc;
	qboolean ownsArgs = false;
	qboolean needCopy = cfg.dedicated && !Game_HasArg( "-dedicated" );
	if( Game_HasArg( "-launcher" ))
		needCopy = true;

	if( needCopy )
	{
		launchArgv = NULL;
		launchArgc = 0;
		for( int i = 0; i < szArgc; ++i )
		{
			if( szArgv[i] && !strcmp( szArgv[i], "-launcher" ) )
				continue;
			Game_PushArg( &launchArgv, &launchArgc, szArgv[i] );
		}
		if( cfg.dedicated && !Game_HasArg( "-dedicated" ))
			Game_PushArg( &launchArgv, &launchArgc, "-dedicated" );
		ownsArgs = true;
	}

	Game_SyncAssets( cfg.gameDir );
	Sys_LoadEngine();
	int ret = Host_Main( launchArgc, launchArgv, cfg.gameDir, 0, XASH_DISABLE_MENU_CHANGEGAME ? NULL : Sys_ChangeGame );
	Sys_UnloadEngine();

	if( ownsArgs )
	{
		for( int i = 0; i < launchArgc; ++i )
			free( launchArgv[i] );
		free( launchArgv );
	}

	return ret;
}

static launch_config_t Game_SelectLaunchConfig( void )
{
	launch_config_t cfg = { Game_GetDefaultDir(), Game_IsDedicatedRequested() };

#if XASH_WIN32
	if( !Game_ShowLauncherWindow( &cfg ))
		exit( 0 );
#else
	if( !isatty( fileno( stdin ) ))
	{
		fprintf( stderr, "Xash launcher: no interactive terminal, defaulting to Counter-Strike client.\n" );
		return cfg;
	}

	printf( "\nXash3D Launcher\n" );
	printf( "1) Counter-Strike client\n" );
	printf( "2) Half-Life client\n" );
	printf( "3) Counter-Strike dedicated server\n" );
	printf( "4) Half-Life dedicated server\n" );
	printf( "Selection: " );
	fflush( stdout );

	char line[32];
	if( !fgets( line, sizeof( line ), stdin ))
		return cfg;

	int choice = atoi( line );
	switch( choice )
	{
		case 2:
			cfg.gameDir = "valve";
			break;
		case 3:
			cfg.gameDir = "cstrike";
			cfg.dedicated = true;
			break;
		case 4:
			cfg.gameDir = "valve";
			cfg.dedicated = true;
			break;
		case 1:
		default:
			cfg.gameDir = "cstrike";
			break;
	}
#endif

	return cfg;
}

static int Sys_Start( void )
{
#if XASH_SAILFISH
	const char *home = getenv( "HOME" );
	char buf[1024];

	snprintf( buf, sizeof( buf ), "%s/xash", home );
	setenv( "XASH3D_BASEDIR", buf, true );
	setenv( "XASH3D_RODIR", "/usr/share/harbour-xash3d-fwgs/rodir", true );
#endif // XASH_SAILFISH

	if( Game_ShouldShowLauncher( ))
	{
		launch_config_t cfg = Game_SelectLaunchConfig( );
		return Game_RunEngine( cfg );
	}

	launch_config_t cfg = { Game_GetDefaultDir(), Game_IsDedicatedRequested() };
	return Game_RunEngine( cfg );
}

#if !XASH_WIN32
int main( int argc, char **argv )
{
	szArgc = argc;
	szArgv = argv;

	return Sys_Start();
}
#else
int __stdcall WinMain( HINSTANCE hInst, HINSTANCE hPrevInst, LPSTR cmdLine, int nShow )
{
	LPWSTR* lpArgv;
	int ret, i;

	lpArgv = CommandLineToArgvW( GetCommandLineW(), &szArgc );
	szArgv = ( char** )malloc( (szArgc + 1) * sizeof( char* ));

	for( i = 0; i < szArgc; ++i )
	{
		size_t size = wcslen(lpArgv[i]) + 1;

		// just in case, allocate some more memory
		szArgv[i] = ( char * )malloc( size * sizeof( wchar_t ));
		wcstombs( szArgv[i], lpArgv[i], size );
	}
	szArgv[szArgc] = 0;

	LocalFree( lpArgv );

	ret = Sys_Start();

	for( ; i < szArgc; ++i )
		free( szArgv[i] );
	free( szArgv );

	return ret;
}
#endif
