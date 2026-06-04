/*
html_motd.c - non-Android HTML MOTD platform fallback
Copyright (C) 2026

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.
*/

#include "platform/platform.h"

#if !XASH_ANDROID

/*
 * Android owns its HTML MOTD through Java WebView. Desktop targets need a
 * renderer-backed backend; for the NetSurf POC this file is the stable platform
 * entry point and deliberately falls back until that backend is compiled in.
 */
int Platform_ShowHtmlMotd( const char *html, const char *baseUrl, const char *serverName, int x, int y, int width, int height )
{
	(void)html;
	(void)baseUrl;
	(void)serverName;
	(void)x;
	(void)y;
	(void)width;
	(void)height;

	Con_DPrintf( "HTML MOTD: desktop backend is not available yet, falling back to HUD MOTD\n" );
	return 0;
}

void Platform_HideHtmlMotd( void )
{
}

#endif // !XASH_ANDROID
