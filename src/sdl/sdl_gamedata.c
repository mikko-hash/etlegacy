/*
 * ET: Legacy
 * Copyright (C) 2012-2024 ET:Legacy team <mail@etlegacy.com>
 *
 * This file is part of ET: Legacy - http://www.etlegacy.com
 *
 * ET: Legacy is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ET: Legacy is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ET: Legacy. If not, see <http://www.gnu.org/licenses/>.
 */
/**
 * @file sdl_gamedata.c
 * @brief First launch download of the original W:ET data (pak0-2.pk3)
 *
 * Public builds can't ship the W:ET assets, so when pak0.pk3 is missing the client
 * fetches them from the ET: Legacy mirror into fs_homepath/etmain, the way the
 * Android app does, showing a small progress window while it runs.
 */

#include "../qcommon/q_shared.h"
#include "../qcommon/qcommon.h"

#ifdef ETL_GAMEDATA_DOWNLOAD

#include "sdl_defs.h"
#include <curl/curl.h>

#define GAMEDATA_MIRROR "https://mirror.etlegacy.com/etmain/"
#define GAMEDATA_MIN_PAK0_SIZE (200 * 1024 * 1024) // the real pak0.pk3 is about 217 MiB

typedef struct
{
	SDL_Window *window;
	SDL_Renderer *renderer;
	double done;        ///< bytes of the files already finished
	double expected;    ///< best guess of the total, refined once each size is known
	int lastPercent;
	qboolean quit;
} gameDataProgress_t;

static const char *gameDataPaks[] = { "pak0.pk3", "pak1.pk3", "pak2.pk3" };

/**
 * @brief Draw the progress bar and keep the window responsive
 */
static void GameData_Draw(gameDataProgress_t *p, double current)
{
	SDL_Event e;
	SDL_Rect  frame = { 20, 50, 440, 24 };
	SDL_Rect  bar   = frame;
	double    frac  = p->expected > 0 ? (p->done + current) / p->expected : 0;
	int       percent;

	while (SDL_PollEvent(&e))
	{
		if (e.type == SDL_QUIT)
		{
			p->quit = qtrue;
		}
	}

	if (!p->renderer)
	{
		return;
	}

	frac    = frac < 0 ? 0 : (frac > 1 ? 1 : frac);
	percent = (int)(frac * 100);
	bar.w   = (int)(frame.w * frac);

	SDL_SetRenderDrawColor(p->renderer, 24, 24, 24, 255);
	SDL_RenderClear(p->renderer);
	SDL_SetRenderDrawColor(p->renderer, 80, 80, 80, 255);
	SDL_RenderDrawRect(p->renderer, &frame);
	SDL_SetRenderDrawColor(p->renderer, 200, 160, 60, 255);
	SDL_RenderFillRect(p->renderer, &bar);
	SDL_RenderPresent(p->renderer);

	if (percent != p->lastPercent && p->window)
	{
		p->lastPercent = percent;
		SDL_SetWindowTitle(p->window, va("Downloading Enemy Territory game data... %d%%", percent));
	}
}

/**
 * @brief libcurl progress callback, returning non-zero aborts the transfer
 */
static int GameData_Progress(void *clientp, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
	gameDataProgress_t *p = (gameDataProgress_t *)clientp;

	(void)ultotal;
	(void)ulnow;

	if (dltotal > 0 && p->done + (double)dltotal > p->expected)
	{
		p->expected = p->done + (double)dltotal;
	}

	GameData_Draw(p, (double)dlnow);

	return p->quit ? 1 : 0;
}

/**
 * @brief Download one pak to a .part file and move it into place when complete
 */
static qboolean GameData_Fetch(gameDataProgress_t *p, const char *pak, const char *dir)
{
	char       url[MAX_OSPATH];
	char       part[MAX_OSPATH];
	char       dest[MAX_OSPATH];
	FILE       *f;
	CURL       *curl;
	CURLcode   res;
	long       code = 0;
	curl_off_t size = 0;

	Com_sprintf(url, sizeof(url), "%s%s", GAMEDATA_MIRROR, pak);
	Com_sprintf(dest, sizeof(dest), "%s%c%s", dir, PATH_SEP, pak);
	Com_sprintf(part, sizeof(part), "%s.part", dest);

	f = Sys_FOpen(part, "wb");
	if (!f)
	{
		Com_Printf(S_COLOR_RED "GameData: can't write %s\n", part);
		return qfalse;
	}

	curl = curl_easy_init();
	if (!curl)
	{
		fclose(f);
		return qfalse;
	}

	curl_easy_setopt(curl, CURLOPT_URL, url);
	curl_easy_setopt(curl, CURLOPT_USERAGENT, PRODUCT_LABEL);
	curl_easy_setopt(curl, CURLOPT_WRITEDATA, f);
	curl_easy_setopt(curl, CURLOPT_FAILONERROR, 1L);
	curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
	curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
	curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
#if defined(_WIN32)
	curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NATIVE_CA);
#elif defined(__linux__)
	if (FS_FileInPathExists("/etc/ssl/certs/ca-certificates.crt"))
	{
		curl_easy_setopt(curl, CURLOPT_CAINFO, "/etc/ssl/certs/ca-certificates.crt");
	}
#endif
	curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
	curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, GameData_Progress);
	curl_easy_setopt(curl, CURLOPT_XFERINFODATA, p);

	Com_Printf("GameData: downloading %s\n", url);
	res = curl_easy_perform(curl);
	curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &code);
	curl_easy_getinfo(curl, CURLINFO_SIZE_DOWNLOAD_T, &size);
	curl_easy_cleanup(curl);
	fclose(f);

	if (res != CURLE_OK || (!Q_stricmp(pak, "pak0.pk3") && size < GAMEDATA_MIN_PAK0_SIZE))
	{
		Com_Printf(S_COLOR_RED "GameData: %s failed (%s, HTTP %ld, %ld bytes)\n", pak, curl_easy_strerror(res), code, (long)size);
		Sys_Remove(part);
		return qfalse;
	}

	if (Sys_Rename(part, dest) != 0)
	{
		Com_Printf(S_COLOR_RED "GameData: can't move %s into place\n", pak);
		Sys_Remove(part);
		return qfalse;
	}

	p->done += (double)size;
	return qtrue;
}

/**
 * @brief Download whichever of pak0-2.pk3 are missing from etmainDir
 * @param[in] etmainPath OS path of fs_homepath/etmain
 * @return qtrue when all three are in place
 */
qboolean Sys_DownloadGameData(const char *etmainPath)
{
	gameDataProgress_t progress;
	char               etmainDir[MAX_OSPATH];
	char               path[MAX_OSPATH];
	qboolean           ok = qtrue;
	int                i;

	Q_strncpyz(etmainDir, etmainPath, sizeof(etmainDir)); // the caller's buffer is FS_BuildOSPath's static one
	Com_Memset(&progress, 0, sizeof(progress));
	progress.expected    = 228000000.0; // refined from the real sizes as they arrive
	progress.lastPercent = -1;

	Com_sprintf(path, sizeof(path), "%s%c", etmainDir, PATH_SEP);
	if (FS_CreatePath(path))
	{
		Com_Printf(S_COLOR_RED "GameData: can't create %s\n", etmainDir);
		return qfalse;
	}

	curl_global_init(CURL_GLOBAL_ALL);

	// a window without one is fine: the download still runs, only invisibly
#ifdef WIN32
	SDL_SetHint(SDL_HINT_WINDOWS_DPI_AWARENESS, "system");
#endif
	if (SDL_InitSubSystem(SDL_INIT_VIDEO) == 0)
	{
		progress.window = SDL_CreateWindow("Downloading Enemy Territory game data...", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 480, 124, SDL_WINDOW_SHOWN);
		if (progress.window)
		{
			progress.renderer = SDL_CreateRenderer(progress.window, -1, 0);
		}
	}

	for (i = 0; i < (int)ARRAY_LEN(gameDataPaks) && ok && !progress.quit; i++)
	{
		Com_sprintf(path, sizeof(path), "%s%c%s", etmainDir, PATH_SEP, gameDataPaks[i]);
		if (FS_FileInPathExists(path))
		{
			continue;
		}
		ok = GameData_Fetch(&progress, gameDataPaks[i], etmainDir);
	}

	if (progress.renderer)
	{
		SDL_DestroyRenderer(progress.renderer);
	}
	if (progress.window)
	{
		SDL_DestroyWindow(progress.window);
	}
	SDL_QuitSubSystem(SDL_INIT_VIDEO);

	return ok && !progress.quit;
}

#else

/**
 * @brief Builds without libcurl can't fetch the data
 */
qboolean Sys_DownloadGameData(const char *etmainDir)
{
	(void)etmainDir;
	return qfalse;
}

#endif // ETL_GAMEDATA_DOWNLOAD
