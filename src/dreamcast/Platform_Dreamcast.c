#define CC_XTEA_ENCRYPTION
#define CC_NO_UPDATER
#define CC_NO_DYNLIB
#define DEFAULT_COMMANDLINE_FUNC

#include "../Stream.h"
#include "../ExtMath.h"
#include "../Funcs.h"
#include "../Window.h"
#include "../Utils.h"
#include "../Errors.h"
#include "../SystemFonts.h"
#include "../Constants.h"
#include "BootUX.h"

#include <errno.h>
#include <netdb.h>
#include <stdlib.h>
#include <string.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <poll.h>
#include <time.h>
#include <stdio.h>
#include <sys/stat.h>
#include <ppp/ppp.h>
#include <kos.h>
#include <dc/sq.h>
#include <dc/sd.h>
#include <fat/fs_fat.h>
#include <kos/dbgio.h>
#include <dc/net/w5500_adapter.h>

KOS_INIT_FLAGS(INIT_CONTROLLER | INIT_KEYBOARD | INIT_MOUSE |
               INIT_VMU        | INIT_CDROM    | INIT_NET   | INIT_FS_RAMDISK);

const cc_result ReturnCode_FileShareViolation = 1000000000; // not used
const cc_result ReturnCode_FileNotFound     = ENOENT;
const cc_result ReturnCode_PathNotFound     = 99999;
const cc_result ReturnCode_DirectoryExists  = EEXIST;
const cc_result ReturnCode_SocketInProgess  = EINPROGRESS;
const cc_result ReturnCode_SocketWouldBlock = EWOULDBLOCK;
const cc_result ReturnCode_SocketDropped    = EPIPE;
static cc_bool usingSD;
static cc_bool sd_fs_dirty;

static void MarkSDDirty(void) {
	if (usingSD) sd_fs_dirty = true;
}

static void SyncSDCard(void) {
	if (!usingSD || !sd_fs_dirty) return;

	fs_fat_sync("/sd");
	sd_fs_dirty = false;
}

const char* Platform_AppNameSuffix = " Dreamcast";
cc_bool Platform_ReadonlyFilesystem;
cc_uint8 Platform_Flags = PLAT_FLAG_SINGLE_PROCESS | PLAT_FLAG_APP_EXIT;
#include "../_PlatformBase.h"


/*########################################################################################################################*
*-----------------------------------------------------Main entrypoint-----------------------------------------------------*
*#########################################################################################################################*/
#include "../main_impl.h"

int main(int argc, char** argv) {
	// NOTE: Disable this if debugging via dcload
	arch_set_exit_path(ARCH_EXIT_MENU);

	SetupProgram(argc, argv);
	while (Window_Main.Exists) { 
		RunProgram(argc, argv);
	}
	
	Window_Free();
	return 0;
}


/*########################################################################################################################*
*------------------------------------------------------Logging/Time-------------------------------------------------------*
*#########################################################################################################################*/
cc_uint64 Stopwatch_ElapsedMicroseconds(cc_uint64 beg, cc_uint64 end) {
	if (end < beg) return 0;
	return end - beg;
}

cc_uint64 Stopwatch_Measure(void) {
	return timer_us_gettime64();
}

static cc_bool log_debugger  = true;
extern cc_bool window_inited;

static void BootUX_UpdateStorageLine(void) {
	if (usingSD)
		BootUX_SetStorage("Storage: SD card (/sd/ClassiCube/)");
	else if (!Platform_ReadonlyFilesystem)
		BootUX_SetStorage("Storage: writable");
	else
		BootUX_SetStorage("Storage: CD only - insert SD to save");
}

static void BootUX_UpdateNetworkLine(void) {
	if (net_default_dev)
		BootUX_SetNetwork("Network: Broadband adapter");
	else
		BootUX_SetNetwork("Network: Dreamcast modem");
}

static cc_bool log_timestamp = true;

void Platform_Log(const char* msg, int len) {
	if (log_debugger) {
		dbgio_write_buffer_xlat(msg,  len);
		dbgio_write_buffer_xlat("\n",   1);
	}
	
	if (window_inited) return;
	BootUX_Log(msg, len);
}

TimeMS DateTime_CurrentUTC(void) {
	uint32 secs, ms;
	timer_ms_gettime(&secs, &ms);
	
	time_t boot_time  = rtc_boot_time();
	cc_uint64 curSecs = boot_time + secs;
	return curSecs + UNIX_EPOCH_SECONDS;
}

void DateTime_CurrentLocal(struct cc_datetime* t) {
	uint32 secs, ms;
	time_t total_secs;
	struct tm loc_time;
	
	timer_ms_gettime(&secs, &ms);
	total_secs = rtc_boot_time() + secs;
	localtime_r(&total_secs, &loc_time);

	t->year   = loc_time.tm_year + 1900;
	t->month  = loc_time.tm_mon  + 1;
	t->day    = loc_time.tm_mday;
	t->hour   = loc_time.tm_hour;
	t->minute = loc_time.tm_min;
	t->second = loc_time.tm_sec;
}


/*########################################################################################################################*
*-------------------------------------------------------Crash handling----------------------------------------------------*
*#########################################################################################################################*/
static cc_string GetThreadName(void) {
	kthread_t* thd = thd_get_current();
	if (!thd) return String_FromReadonly("(unknown)");

	return String_FromRawArray(thd->label);
}

static void HandleCrash(irq_t evt, irq_context_t* ctx, void* data) {
	uint32_t code = evt;
	cc_bool logged = false;
	log_timestamp = false;
	window_inited = false;
	str_offset    = 0;

	for (;;)
	{
		if (!logged) {
			cc_string name = GetThreadName();
			Platform_LogConst("** CLASSICUBE FATALLY CRASHED **");
			Platform_Log3("PC: %h, error: %h, thd: %s",
							&ctx->pc, &code, &name);
			Platform_LogConst("");
		
			static const char* const regNames[] = {
				"R0 ", "R1 ", "R2 ", "R3 ", "R4 ", "R5 ", "R6 ", "R7 ",
				"R8 ", "R9 ", "R10", "R11", "R12", "R13", "R14", "R15"
			};
		
			for (int i = 0; i < 8; i++) {
				Platform_Log4("    %c: %h    %c: %h",
							regNames[i],     &ctx->r[i],
							regNames[i + 8], &ctx->r[i + 8]);
			}
		
			Platform_Log4("    %c : %h    %c : %h",
						"SR", &ctx->sr,
						"PR", &ctx->pr);
		
			Platform_LogConst("");
			Platform_LogConst("Please report on ClassiCube Discord or forums");
			Platform_LogConst("");
			Platform_LogConst("You will need to restart your Dreamcast");
			Platform_LogConst("");

			logged = true;
			log_debugger = false;
		}
		thd_sleep(1000);
	}
}

void CrashHandler_Install(void) {
	irq_set_handler(EXC_UNHANDLED_EXC, HandleCrash, NULL);
}

void Process_Abort2(cc_result result, const char* raw_msg) {
	Logger_DoAbort(result, raw_msg, NULL);
}


/*########################################################################################################################*
*----------------------------------------------------VMU options file-----------------------------------------------------*
*#########################################################################################################################*/
static const cc_uint8 icon_pal[] = {
0xff,0xff, 0xdd,0xfd, 0x00,0x50, 0x33,0xf3, 0xee,0xfe, 0xcc,0xfc, 0xbb,0xfb, 0x00,0x40, 
0x88,0xf8, 0x00,0xb0, 0x22,0xf2, 0x00,0xb0, 0x00,0xf0, 0x00,0x30, 0x00,0x00, 0x00,0xf0,
};
static const cc_uint8 icon_data[] = {
0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xd9, 0x97, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee,
0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xd9, 0xc3, 0x3c, 0x97, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee,
0xee, 0xee, 0xee, 0xee, 0xee, 0xd9, 0xc3, 0x60, 0x06, 0x3c, 0x97, 0xee, 0xee, 0xee, 0xee, 0xee,
0xee, 0xee, 0xee, 0xee, 0xd9, 0xc3, 0x60, 0x00, 0x00, 0x06, 0x3c, 0x97, 0xee, 0xee, 0xee, 0xee,
0xee, 0xee, 0xee, 0xd9, 0xc3, 0x60, 0x00, 0x00, 0x00, 0x00, 0x06, 0x3c, 0x97, 0xee, 0xee, 0xee,
0xee, 0xee, 0xd9, 0xc3, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x3c, 0x97, 0xee, 0xee,
0xee, 0xd9, 0xc3, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x3c, 0x97, 0xee,
0xe7, 0xc3, 0x60, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06, 0x3c, 0x7e,
0xe2, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1c, 0x2e,
0xe2, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x6c, 0x2e,
0xe2, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x66, 0x6c, 0x2e,
0xe2, 0xc0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x15, 0x11, 0x56, 0x6c, 0x2e,
0xe2, 0xc0, 0x00, 0x01, 0x11, 0x00, 0x00, 0x00, 0x00, 0x00, 0x16, 0x10, 0x00, 0x16, 0x6c, 0x2e,
0xe2, 0xc0, 0x00, 0x56, 0x66, 0x64, 0x00, 0x00, 0x00, 0x16, 0x64, 0x00, 0x00, 0x66, 0x6c, 0x2e,
0xe2, 0xc0, 0x04, 0x66, 0x66, 0x66, 0x40, 0x00, 0x16, 0x66, 0x40, 0x04, 0x15, 0x66, 0x6c, 0x2e,
0xe2, 0xc0, 0x01, 0x66, 0x14, 0x16, 0x60, 0x00, 0x66, 0x65, 0x00, 0x56, 0x66, 0x66, 0x6c, 0x2e,
0xe2, 0xc0, 0x05, 0x65, 0x00, 0x04, 0x64, 0x00, 0x66, 0x64, 0x04, 0x66, 0x66, 0x66, 0x6c, 0x2e,
0xe2, 0xc0, 0x05, 0x65, 0x00, 0x00, 0x00, 0x00, 0x66, 0x50, 0x05, 0x66, 0x66, 0x66, 0x6c, 0x2e,
0xe2, 0xc0, 0x05, 0x65, 0x00, 0x00, 0x00, 0x00, 0x66, 0x10, 0x06, 0x66, 0x66, 0x66, 0x6c, 0x2e,
0xe2, 0xc0, 0x01, 0x65, 0x00, 0x00, 0x00, 0x00, 0x66, 0x40, 0x46, 0x66, 0x66, 0x66, 0x6c, 0x2e,
0xe2, 0xc0, 0x04, 0x65, 0x00, 0x00, 0x00, 0x00, 0x66, 0x40, 0x46, 0x66, 0x65, 0x66, 0x6c, 0x2e,
0xe2, 0xc0, 0x00, 0x56, 0x40, 0x00, 0x00, 0x00, 0x66, 0x00, 0x06, 0x66, 0x50, 0x56, 0x6c, 0x2e,
0xe2, 0xc0, 0x00, 0x16, 0x50, 0x00, 0x00, 0x00, 0x66, 0x40, 0x05, 0x65, 0x40, 0x16, 0x6c, 0x2e,
0xe2, 0xc0, 0x00, 0x05, 0x61, 0x00, 0x00, 0x00, 0x66, 0x40, 0x00, 0x40, 0x00, 0x66, 0x6c, 0x2e,
0xe7, 0xc3, 0x60, 0x04, 0x66, 0x55, 0x50, 0x00, 0x66, 0x50, 0x00, 0x00, 0x05, 0x68, 0xac, 0x7e,
0xee, 0xd9, 0xc3, 0x60, 0x45, 0x66, 0x64, 0x00, 0x66, 0x64, 0x00, 0x04, 0x58, 0xac, 0x97, 0xee,
0xee, 0xee, 0xd9, 0xc3, 0x60, 0x15, 0x54, 0x00, 0x66, 0x66, 0x55, 0x58, 0xac, 0x97, 0xee, 0xee,
0xee, 0xee, 0xee, 0xd9, 0xc3, 0x60, 0x00, 0x00, 0x66, 0x66, 0x68, 0xac, 0x97, 0xee, 0xee, 0xee,
0xee, 0xee, 0xee, 0xee, 0xd9, 0xc3, 0x60, 0x00, 0x66, 0x68, 0xac, 0x97, 0xee, 0xee, 0xee, 0xee,
0xee, 0xee, 0xee, 0xee, 0xee, 0xd9, 0xc3, 0x60, 0x68, 0xac, 0x97, 0xee, 0xee, 0xee, 0xee, 0xee,
0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xd9, 0xc3, 0xac, 0x97, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee,
0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xd9, 0x97, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee,
};

static volatile int vmu_write_FD = -10000;
static char vmu_options_path[24];
static cc_bool vmu_path_inited;

static void VMU_SelectOptionsPath(void) {
	const char port_ids[] = { 'a', 'b', 'c', 'd' };
	struct stat sb;

	if (vmu_path_inited) return;
	vmu_path_inited = true;

	for (int p = 0; p < 4; p++)
	{
		if (!maple_enum_type(p, MAPLE_FUNC_MEMCARD)) continue;

		for (int s = 1; s <= 2; s++)
		{
			snprintf(vmu_options_path, sizeof(vmu_options_path),
				"/vmu/%c%d/CCOPT.txt", port_ids[p], s);
			if (fs_stat(vmu_options_path, &sb, 0) == 0) return;
		}
	}

	for (int p = 0; p < 4; p++)
	{
		if (!maple_enum_type(p, MAPLE_FUNC_MEMCARD)) continue;

		snprintf(vmu_options_path, sizeof(vmu_options_path),
			"/vmu/%c1/CCOPT.txt", port_ids[p]);
		return;
	}

	strcpy(vmu_options_path, "/vmu/a1/CCOPT.txt");
}

static int VMUFile_Do(cc_file* file, int mode) {
	void* data = NULL;
	int fd, err = -1, len;
	vmu_pkg_t pkg;

	VMU_SelectOptionsPath();
	errno = 0;
	fd    = fs_open(vmu_options_path, O_RDONLY | O_META);
	
	// Try to extract stored data from the VMU
	if (fd >= 0) {
		len  = fs_total(fd);
		data = Mem_Alloc(len, 1, "VMU data");
		if (fs_read(fd, data, len) != len) {
			Mem_Free(data);
			data = NULL;
			err  = -1;
		} else {
			err = vmu_pkg_parse(data, len, &pkg);
		}
		fs_close(fd);
	}
	
	// Copy VMU data into a RAM temp file
	errno = 0;
	fd    = fs_open("/ram/ccopt", O_RDWR | O_CREAT | O_TRUNC);
	if (fd < 0) return errno;
	
	if (err >= 0) {
		fs_write(fd, pkg.data, pkg.data_len);
		fs_seek(fd,  0, SEEK_SET);
	}
	Mem_Free(data);

	if (mode != O_RDONLY) vmu_write_FD = fd;
	*file = fd;
	return 0;
}

static cc_result VMUFile_Close(cc_file file) {
	void* data;
	uint8* pkg_data;
	int fd, err = -1, len, pkg_len;
	vmu_pkg_t pkg = { 0 };
	vmu_write_FD  = -10000;
	
	len  = fs_total(file);
	data = Mem_Alloc(len, 1, "VMU data");
	fs_seek(file, 0, SEEK_SET);
	fs_read(file, data, len);
	
	fs_close(file);
	fs_unlink("/ram/ccopt");
	
	strcpy(pkg.desc_short, "CC options file");
	strcpy(pkg.desc_long,  "ClassiCube config/settings");
	strcpy(pkg.app_id,     "ClassiCube");
	pkg.eyecatch_type = VMUPKG_EC_NONE;
	pkg.data_len      = len;
	pkg.data          = data;

	pkg.icon_cnt  = 1;
	pkg.icon_data = icon_data;
	Mem_Copy(pkg.icon_pal, icon_pal, sizeof(icon_pal));
		
	err = vmu_pkg_build(&pkg, &pkg_data, &pkg_len);
	if (err) { Mem_Free(data); return ERR_OUT_OF_MEMORY; }
	
	// Copy into VMU file
	errno = 0;
	fd    = fs_open(vmu_options_path, O_RDWR | O_CREAT | O_TRUNC | O_META);
	if (fd < 0) return errno;
	
	int wrote = fs_write(fd, pkg_data, pkg_len);
	fs_close(fd);
	free(pkg_data);
	Mem_Free(data);
	if (wrote != pkg_len) return errno ? errno : ERR_INVALID_ARGUMENT;

	Platform_Log1("VMU options saved (%i bytes)", &pkg_len);
	return 0;
}


/*########################################################################################################################*
*-----------------------------------------------------Directory/File------------------------------------------------------*
*#########################################################################################################################*/
static cc_string root_path = String_FromConst("/cd/");

void Platform_EncodePath(cc_filepath* dst, const cc_string* path) {
	char* str = dst->buffer;
	Mem_Copy(str, root_path.buffer, root_path.length);
	str += root_path.length;
	String_EncodeUtf8(str, path);
}

void Platform_DecodePath(cc_string* dst, const cc_filepath* path) {
	const char* str = path->buffer;
	String_AppendUtf8(dst, str, String_Length(str));
}

void Directory_GetCachePath(cc_string* path) { }

cc_result Directory_Create2(const cc_filepath* path) {
	int res = fs_mkdir(path->buffer);
	int err = res == -1 ? errno : 0;
	
	// Filesystem returns EINVAL when operation unsupported (e.g. CD system)
	//  so rather than logging an error, just pretend it already exists
	if (err == EINVAL) err = EEXIST;

	// Changes are cached in memory, defer sync to Platform_Free
	if (!err) MarkSDDirty();
	return err;
}

int File_Exists(const cc_filepath* path) {
	struct stat sb;
	return fs_stat(path->buffer, &sb, 0) == 0 && S_ISREG(sb.st_mode);
}

cc_result Directory_Enum(const cc_string* dirPath, void* obj, Directory_EnumCallback callback) {
	cc_string path; char pathBuffer[FILENAME_SIZE];
	cc_filepath str;
	// CD filesystem loader doesn't usually set errno
	//  when it can't find the requested file
	errno = 0;

	Platform_EncodePath(&str, dirPath);
	int fd = fs_open(str.buffer, O_DIR | O_RDONLY);
	if (fd < 0) return errno;

	String_InitArray(path, pathBuffer);
	const dirent_t* entry;
	errno = 0;
	
	while ((entry = fs_readdir(fd))) {
		path.length = 0;
		String_Format1(&path, "%s/", dirPath);

		// ignore . and .. entries if returned by the filesystem
		const char* src = entry->name;
		if (src[0] == '.' && src[1] == '\0')                  continue;
		if (src[0] == '.' && src[1] == '.' && src[2] == '\0') continue;
		
		int len = String_Length(src);
		String_AppendUtf8(&path, src, len);

		// negative size indicates a directory entry
		int is_dir = entry->size < 0;
		callback(&path, obj, is_dir);
	}

	int err = errno; // save error from fs_readdir
	fs_close(fd);
	return err;
}

static cc_bool IsOptionsFile(const cc_string* path) {
	int idx = String_LastIndexOf(path, '/');
	if (idx < 0) return false;

	cc_string name = String_UNSAFE_SubstringAt(path, idx + 1);
	return String_CaselessEqualsConst(&name, "options.txt");
}

static cc_result File_Do(cc_file* file, const char* path, int mode) {
	// CD filesystem loader doesn't usually set errno
	//  when it can't find the requested file
	errno = 0;

	int res = fs_open(path, mode);
	*file   = res;
	
	int err = res == -1 ? errno : 0;
	if (res == -1 && err == 0) err = ENOENT;

	// Read/Write VMU for options.txt if no SD card, since that file is critical
	cc_string raw = String_FromReadonly(path);
	if (err && !usingSD && IsOptionsFile(&raw)) {
		return VMUFile_Do(file, mode);
	}
	return err;
}

cc_result File_Open(cc_file* file, const cc_filepath* path) {
	return File_Do(file, path->buffer, O_RDONLY);
}
cc_result File_Create(cc_file* file, const cc_filepath* path) {
	return File_Do(file, path->buffer, O_RDWR | O_CREAT | O_TRUNC);
}
cc_result File_OpenOrCreate(cc_file* file, const cc_filepath* path) {
	return File_Do(file, path->buffer, O_RDWR | O_CREAT);
}

cc_result File_Read(cc_file file, void* data, cc_uint32 count, cc_uint32* bytesRead) {
	int res    = fs_read(file, data, count);
	*bytesRead = res;
	return res == -1 ? errno : 0;
}

cc_result File_Write(cc_file file, const void* data, cc_uint32 count, cc_uint32* bytesWrote) {
	int res     = fs_write(file, data, count);
	*bytesWrote = res;
	return res == -1 ? errno : 0;
}

cc_result File_Close(cc_file file) {
	if (file == vmu_write_FD) 
		return VMUFile_Close(file);
	
	// Sync written files immediately; directory ops are deferred to Platform_Free
	if (usingSD) MarkSDDirty();

	int res = fs_close(file);
	return res == -1 ? errno : 0;
}

cc_result File_Seek(cc_file file, int offset, int seekType) {
	static cc_uint8 modes[3] = { SEEK_SET, SEEK_CUR, SEEK_END };
	
	int res = fs_seek(file, offset, modes[seekType]);
	return res == -1 ? errno : 0;
}

cc_result File_Position(cc_file file, cc_uint32* pos) {
	int res = fs_seek(file, 0, SEEK_CUR);
	*pos    = res;
	return res == -1 ? errno : 0;
}

cc_result File_Length(cc_file file, cc_uint32* len) {
	int res = fs_total(file);
	*len    = res;
	return res == -1 ? errno : 0;
}


/*########################################################################################################################*
*--------------------------------------------------------Threading--------------------------------------------------------*
*#########################################################################################################################*/
// !!! NOTE: Dreamcast is configured to use preemptive multithreading !!!
void Thread_Sleep(cc_uint32 milliseconds) { 
	thd_sleep(milliseconds); 
}

static void* ExecThread(void* param) {
	Thread_StartFunc func = (Thread_StartFunc)param;
	(func)();
	return NULL;
}

void Thread_Run(void** handle, Thread_StartFunc func, int stackSize, const char* name) {
	kthread_attr_t attrs = { 0 };
	kthread_t* thread;

	attrs.stack_size = stackSize;
	attrs.label      = name;
	thread = thd_create_ex(&attrs, ExecThread, func);
	if (!thread) Process_Abort2(errno, "Creating thread");
	*handle = thread;
}

void Thread_Detach(void* handle) {
	thd_detach((kthread_t*)handle);
}

void Thread_Join(void* handle) {
	thd_join((kthread_t*)handle, NULL);
}


/*########################################################################################################################*
*-----------------------------------------------------Synchronisation-----------------------------------------------------*
*#########################################################################################################################*/
void* Mutex_Create(const char* name) {
	mutex_t* ptr = (mutex_t*)Mem_Alloc(1, sizeof(mutex_t), "mutex");
	int res = mutex_init(ptr, MUTEX_TYPE_NORMAL);
	if (res) Process_Abort2(errno, "Creating mutex");
	return ptr;
}

void Mutex_Free(void* handle) {
	int res = mutex_destroy((mutex_t*)handle);
	if (res) Process_Abort2(errno, "Destroying mutex");
	Mem_Free(handle);
}

void Mutex_Lock(void* handle) {
	int res = mutex_lock((mutex_t*)handle);
	if (res) Process_Abort2(errno, "Locking mutex");
}

void Mutex_Unlock(void* handle) {
	int res = mutex_unlock((mutex_t*)handle);
	if (res) Process_Abort2(errno, "Unlocking mutex");
}

void* Waitable_Create(const char* name) {
	semaphore_t* ptr = (semaphore_t*)Mem_Alloc(1, sizeof(semaphore_t), "waitable");
	int res = sem_init(ptr, 0);
	if (res) Process_Abort2(errno, "Creating waitable");
	return ptr;
}

void Waitable_Free(void* handle) {
	int res = sem_destroy((semaphore_t*)handle);
	if (res) Process_Abort2(errno, "Destroying waitable");
	Mem_Free(handle);
}

void Waitable_Signal(void* handle) {
	int res = sem_signal((semaphore_t*)handle);
	if (res < 0) Process_Abort2(errno, "Signalling event");
}

void Waitable_Wait(void* handle) {
	int res = sem_wait((semaphore_t*)handle);
	if (res < 0) Process_Abort2(errno, "Event wait");
}

void Waitable_WaitFor(void* handle, cc_uint32 milliseconds) {
	int res = sem_wait_timed((semaphore_t*)handle, milliseconds);
	if (res >= 0) return;
	
	int err = errno;
	if (err != ETIMEDOUT) Process_Abort2(err, "Event timed wait");
}


/*########################################################################################################################*
*---------------------------------------------------------Socket----------------------------------------------------------*
*#########################################################################################################################*/
cc_bool SockAddr_ToString(const cc_sockaddr* addr, cc_string* dst) {
	struct sockaddr_in* addr4 = (struct sockaddr_in*)addr->data;

	if (addr4->sin_family == AF_INET) 
		return IPv4_ToString(&addr4->sin_addr, &addr4->sin_port, dst);
	return false;
}

static cc_bool ParseIPv4(const cc_string* ip, int port, cc_sockaddr* dst) {
	struct sockaddr_in* addr4 = (struct sockaddr_in*)dst->data;
	cc_uint32 ip_addr = 0;
	if (!ParseIPv4Address(ip, &ip_addr)) return false;

	addr4->sin_addr.s_addr = ip_addr;
	addr4->sin_family      = AF_INET;
	addr4->sin_port        = SockAddr_EncodePort(port);
		
	dst->size = sizeof(*addr4);
	return true;
}

static cc_bool ParseIPv6(const char* ip, int port, cc_sockaddr* dst) {
	return false;
}

static cc_result ParseHost(const char* host, int port, cc_sockaddr* addrs, int* numValidAddrs) {
	char portRaw[32]; cc_string portStr;
	struct addrinfo hints = { 0 };
	struct addrinfo* result;
	struct addrinfo* cur;

	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	
	String_InitArray(portStr,  portRaw);
	String_AppendInt(&portStr, port);
	portRaw[portStr.length] = '\0';

	int res = getaddrinfo(host, portRaw, &hints, &result);
	if (res == EAI_NONAME) return SOCK_ERR_UNKNOWN_HOST;
	if (res == EAI_SYSTEM) return errno;
	if (res) return res;

	int i = 0;
	for (cur = result; cur && i < SOCKET_MAX_ADDRS; cur = cur->ai_next, i++) 
	{
		SocketAddr_Set(&addrs[i], cur->ai_addr, cur->ai_addrlen);
	}

	freeaddrinfo(result);
	*numValidAddrs = i;
	return i == 0 ? ERR_INVALID_ARGUMENT : 0;
}

cc_result Socket_Create(cc_socket* s, cc_sockaddr* addr) {
	struct sockaddr* raw = (struct sockaddr*)addr->data;

	*s = socket(raw->sa_family, SOCK_STREAM, IPPROTO_TCP);
	if (*s == -1) return errno;

	return 0;
}

cc_result Socket_SetNonBlocking(cc_socket s, cc_bool nonblocking) {
	int res = fcntl(s, F_GETFL, 0);
	if (res == -1) return errno;

	int flags = res & ~O_NONBLOCK;
	if (nonblocking) flags |= O_NONBLOCK;

	res = fcntl(s, F_SETFL, flags);
	return res == -1 ? errno : 0;
}

void Socket_Close(cc_socket s) {
	shutdown(s, SHUT_RDWR);
	close(s);
}

cc_result Socket_Connect(cc_socket s, cc_sockaddr* addr) {
	struct sockaddr* raw = (struct sockaddr*)addr->data;

	int res = connect(s, raw, addr->size);
	return res == -1 ? errno : 0;
}

cc_result Socket_Read(cc_socket s, cc_uint8* data, cc_uint32 count, cc_uint32* modified) {
	int recvCount = recv(s, data, count, 0);
	if (recvCount != -1) { *modified = recvCount; return 0; }
	*modified = 0; return errno;
}

cc_result Socket_Write(cc_socket s, const cc_uint8* data, cc_uint32 count, cc_uint32* modified) {
	int sentCount = send(s, data, count, 0);
	if (sentCount != -1) { *modified = sentCount; return 0; }
	*modified = 0; return errno;
}

cc_result Socket_Poll(cc_socket s, int timeoutMS, int mode, cc_bool* success) {
	struct pollfd pfd;
	int flags;

	pfd.fd     = s;
	pfd.events = mode == SOCKET_POLL_READ ? POLLIN : POLLOUT;
	if (poll(&pfd, 1, timeoutMS) == -1) { *success = false; return errno; }
	
	/* to match select, closed socket still counts as readable */
	flags    = mode == SOCKET_POLL_READ ? (POLLIN | POLLHUP) : POLLOUT;
	*success = (pfd.revents & flags) != 0;
	return 0;
}


/*########################################################################################################################*
*--------------------------------------------------------Platform---------------------------------------------------------*
*#########################################################################################################################*/
static kos_blockdev_t sd_dev;
static uint8 partition_type;

static void TryInitSDCard(void) {
	if (sd_init()) {
		// Both SD card and debug interface use the serial port
		// So if initing SD card fails, need to restore serial port state for debug logging
		scif_init();
		Platform_LogConst("Failed to init SD card"); return;
	}
	
	if (sd_blockdev_for_partition(0, &sd_dev, &partition_type)) {
		Platform_LogConst("Unable to find first partition on SD card"); return;
	}
	Platform_Log1("Found SD card (partitioned using: %b)", &partition_type);
	
	if (fs_fat_init()) {
		Platform_LogConst("Failed to init FAT filesystem"); return;
	}
	
	if (fs_fat_mount("/sd", &sd_dev, FS_FAT_MOUNT_READWRITE)) {
		Platform_LogConst("Failed to mount SD card"); return;
	}

	root_path = String_FromReadonly("/sd/ClassiCube/");
	Platform_ReadonlyFilesystem = false;

	usingSD      = true;
	log_debugger = false;

	cc_filepath* root = FILEPATH_RAW("/sd/ClassiCube");
	int res = Directory_Create2(root);
	Platform_Log1("ROOT DIRECTORY CREATE: %i", &res);
}

static void InitModem(void) {
	int err;
	BootUX_SetStatus("Initialising modem..");
	Platform_LogConst("Initialising modem..");
	
	if (!modem_init()) {
		BootUX_SetStatus("Modem unavailable - offline mode");
		Platform_LogConst("Modem init failed - continuing offline"); return;
	}
	ppp_init();
	
	BootUX_SetStatus("Dialling ISP (~20 seconds)..");
	Platform_LogConst("Dialling modem.. (can take ~20 seconds)");
	err = ppp_modem_init("111111111111", 1, NULL);
	if (err) {
		BootUX_SetStatus("Modem dial failed - offline mode");
		Platform_Log1("Modem link failed (%i) - continuing offline", &err); return;
	}

	ppp_set_login("dream", "dreamcast");

	BootUX_SetStatus("Connecting PPP (~20 seconds)..");
	Platform_LogConst("Connecting link.. (can take ~20 seconds)");
	err = ppp_connect();
	if (err) {
		BootUX_SetStatus("PPP failed - offline mode");
		Platform_Log1("PPP connect failed (%i) - continuing offline", &err); return;
 	}
	BootUX_SetStatus("Modem connected");
	BootUX_SetNetwork("Network: Modem (online)");
	Platform_LogConst("Modem connected");
}

static cc_bool StartHeldOnPorts(void) {
	for (int p = 0; p < 4; p++)
	{
		maple_device_t* cont = maple_enum_type(p, MAPLE_FUNC_CONTROLLER);
		cont_state_t* state;
		if (!cont) continue;

		state = (cont_state_t*)maple_dev_status(cont);
		if (state && (state->buttons & CONT_START)) return true;
	}
	return false;
}

static void WaitToStart(void) {
	BootUX_SetStatus("Press START to continue");

	for (int i = 0; i < 30; i++)
	{
		if (StartHeldOnPorts()) return;
		Thread_Sleep(100);
		BootUX_Tick();
	}
}

void Platform_Init(void) {
	Platform_ReadonlyFilesystem = true;

	// W5500 net adapter also uses the serial port
	if (w5500_adapter_init(NULL, true) == 0) {
		log_debugger = false;
		Platform_LogConst("Broadband adapter detected");
	}
	TryInitSDCard();
}

void Platform_NetworkInit(void) {
	BootUX_ShowSplash();
	BootUX_UpdateStorageLine();
	BootUX_UpdateNetworkLine();
	BootUX_SetStatus("Checking network..");

	if (net_default_dev) {
		BootUX_SetStatus("Broadband ready");
		BootUX_ShowLoading();
		return;
	}

	if (Options_GetBool("launcher-dc-skipmodem", false)) {
		BootUX_SetStatus("Modem skipped (option)");
		BootUX_SetNetwork("Network: Offline");
		Platform_LogConst("launcher-dc-skipmodem set - skipping modem init");
	} else if (usingSD) {
		BootUX_SetStatus("SD boot - modem skipped");
		BootUX_SetNetwork("Network: Offline");
		Platform_LogConst("SD card ready - skipping modem init");
	} else if (StartHeldOnPorts()) {
		BootUX_SetStatus("START held - modem skipped");
		BootUX_SetNetwork("Network: Offline");
		Platform_LogConst("START held - skipping modem init");
	} else {
		InitModem();
	}
	WaitToStart();
	BootUX_ShowLoading();
}
void Platform_GetLauncherStatus(cc_string* storage, cc_string* network) {
	if (usingSD) {
		String_AppendConst(storage, "SD card (/sd/ClassiCube/)");
	} else if (Platform_ReadonlyFilesystem) {
		String_AppendConst(storage, "CD (read only)");
	} else {
		String_AppendConst(storage, "Writable storage");
	}

	if (net_default_dev) {
		String_AppendConst(network, "Broadband adapter");
	} else {
		String_AppendConst(network, "Modem or offline");
	}
}
void Platform_Free(void) {
	SyncSDCard();
}

cc_bool Platform_DescribeError(cc_result res, cc_string* dst) {
	char chars[NATIVE_STR_LEN];
	int len;

	/* For unrecognised error codes, strerror_r might return messages */
	/*  such as 'No error information', which is not very useful */
	/* (could check errno here but quicker just to skip entirely) */
	if (res >= 1000) return false;

	len = strerror_r(res, chars, NATIVE_STR_LEN);
	if (len == -1) return false;

	len = String_CalcLen(chars, NATIVE_STR_LEN);
	String_AppendUtf8(dst, chars, len);
	return true;
}

cc_bool Process_OpenSupported = false;
cc_result Process_StartOpen(const cc_string* args) {
	return ERR_NOT_SUPPORTED;
}

void Process_Exit(cc_result code) { exit(code); }

cc_result Process_StartGame2(const cc_string* args, int numArgs) {
	Platform_LogConst("START CLASSICUBE");
	return SetGameArgs(args, numArgs);
}

cc_result Platform_SetDefaultCurrentDirectory(int argc, char **argv) {
	return 0;
}


/*########################################################################################################################*
*-------------------------------------------------------Encryption--------------------------------------------------------*
*#########################################################################################################################*/
#define MACHINE_KEY "DreamCastKOS_PVR"

static cc_result GetMachineID(cc_uint32* key) {
	Mem_Copy(key, MACHINE_KEY, sizeof(MACHINE_KEY) - 1);
	return 0;
}

cc_result Platform_GetEntropy(void* data, int len) {
	cc_uint8* dst = (cc_uint8*)data;
	cc_uint32 seed  = (cc_uint32)timer_us_gettime64();
	seed ^= (cc_uint32)rtc_boot_time();

	for (int i = 0; i < len; i++) {
		if ((i & 3) == 0) seed = seed * 1664525u + 1013904223u;
		dst[i] = (cc_uint8)(seed >> ((i & 3) * 8));
	}
	return 0;
}
