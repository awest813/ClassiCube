#include "BootUX.h"
#include "../Constants.h"
#include "../Funcs.h"
#include "../SystemFonts.h"
#include <kos.h>
#include <dc/sq.h>

extern cc_bool window_inited;

#define BOOT_LOG_LINES      5
#define BOOT_LOG_Y_START  318
#define LINE_H           22
#define Onscreen_LineOffset(y) ((y) * vid_mode->width)

static cc_bool boot_active;
static cc_uint8 log_index;
static cc_uint8 spinner;
static char boot_status[64];
static char boot_storage[48];
static char boot_network[48];
static char boot_log[BOOT_LOG_LINES][50];

struct LogPosition { int x, y; };

static void PlotOnscreen(int x, int y, void* ctx) {
	struct LogPosition* pos = ctx;
	x += pos->x;
	y += pos->y;
	if (x >= vid_mode->width || y >= vid_mode->height) return;
	vram_s[Onscreen_LineOffset(y) + x] = 0xFFFF;
}

static void DrawTextAt(const char* text, int x, int y, int scale) {
	struct LogPosition pos;
	pos.x = x;
	pos.y = y;
	for (int i = 0; text[i]; i++)
		pos.x += FallbackFont_Plot((cc_uint8)text[i], PlotOnscreen, scale, &pos);
}

static void DrawCentered(const char* text, int y, int scale) {
	int len = 0;
	for (const char* p = text; *p; p++) len++;
	int x = (vid_mode->width - len * (6 * scale)) / 2;
	if (x < 0) x = 0;
	DrawTextAt(text, x, y, scale);
}

static void ClearLine(int y, int height) {
	uint16_t* dst = vram_s + Onscreen_LineOffset(y);
	sq_set16((uintptr_t)dst, 0x0000, height * vid_mode->width);
}

static void CopyBootStr(char* dst, int cap, const char* src) {
	int i = 0;
	for (; i < cap - 1 && src[i]; i++) dst[i] = src[i];
	dst[i] = '\0';
}

static void Redraw(void) {
	int y, i;
	if (!boot_active || window_inited || !vid_mode) return;

	sq_set16((uintptr_t)vram_s, 0x294A, vid_mode->width * vid_mode->height);

	DrawCentered(GAME_APP_TITLE, 100, 5);
	DrawCentered("Dreamcast Edition", 155, 3);

	if (boot_storage[0]) DrawCentered(boot_storage, 200, 2);
	if (boot_network[0]) DrawCentered(boot_network, 222, 2);

	if (boot_status[0]) {
		ClearLine(248, LINE_H + 4);
		DrawCentered(boot_status, 252, 3);
	}

	y = BOOT_LOG_Y_START;
	for (i = 0; i < BOOT_LOG_LINES; i++) {
		int idx = (log_index + i) % BOOT_LOG_LINES;
		if (!boot_log[idx][0]) continue;
		ClearLine(y, LINE_H);
		DrawTextAt(boot_log[idx], 16, y, 2);
		y += LINE_H;
	}

	DrawCentered("Hold START to skip", 430, 2);
	{
		static const char* const frames[] = { "", ".", "..", "..." };
		DrawTextAt(frames[spinner & 3], vid_mode->width / 2 + 112, 430, 2);
	}

	vid_flip(-1);
}

void BootUX_ShowSplash(void) {
	if (window_inited || !vid_mode) return;
	boot_active = true;
	log_index   = 0;
	spinner     = 0;
	boot_status[0]   = '\0';
	boot_storage[0]  = '\0';
	boot_network[0]  = '\0';
	for (int i = 0; i < BOOT_LOG_LINES; i++) boot_log[i][0] = '\0';
	Redraw();
}

void BootUX_SetStatus(const char* msg) {
	CopyBootStr(boot_status, sizeof(boot_status), msg);
	Redraw();
}

void BootUX_SetStorage(const char* msg) {
	CopyBootStr(boot_storage, sizeof(boot_storage), msg);
	Redraw();
}

void BootUX_SetNetwork(const char* msg) {
	CopyBootStr(boot_network, sizeof(boot_network), msg);
	Redraw();
}

void BootUX_Log(const char* msg, int len) {
	char line[50];
	int n = min(len, (int)sizeof(line) - 1);
	Mem_Copy(line, msg, n);
	line[n] = '\0';

	CopyBootStr(boot_log[log_index], sizeof(boot_log[0]), line);
	log_index = (log_index + 1) % BOOT_LOG_LINES;
	Redraw();
}

void BootUX_Tick(void) {
	if (!boot_active) return;
	spinner++;
	Redraw();
}

void BootUX_ShowLoading(void) {
	if (!boot_active || window_inited || !vid_mode) return;
	sq_set16((uintptr_t)vram_s, 0x0000, vid_mode->width * vid_mode->height);
	DrawCentered(GAME_APP_TITLE, 180, 4);
	DrawCentered("Loading menu..", 240, 3);
	vid_flip(-1);
	boot_active = false;
}
