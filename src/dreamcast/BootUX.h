#ifndef CC_BOOTUX_H
#define CC_BOOTUX_H
#include "../Core.h"
CC_BEGIN_HEADER

/* Dreamcast boot splash and status screen (before Window_Init). */
void BootUX_ShowSplash(void);
void BootUX_SetStatus(const char* msg);
void BootUX_SetStorage(const char* msg);
void BootUX_SetNetwork(const char* msg);
void BootUX_Log(const char* msg, int len);
void BootUX_Tick(void);
void BootUX_ShowLoading(void);

CC_END_HEADER
#endif
