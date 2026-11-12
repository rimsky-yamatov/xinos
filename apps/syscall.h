#ifdef __cplusplus
#include <cstddef>
#include <cstdint>

extern "C" {
#else
#include <stddef.h>
#include <stdint.h>
#endif

#include "../kernel/logger.hpp"
#include "../kernel/app_event.hpp"

struct SyscallResult {
  uint64_t value;
  int error;
};

struct SyscallResult SyscallLogString(enum LogLevel level, const char* message);
struct SyscallResult SyscallPutString(int fd, const char* s, size_t len);
void SyscallExit(int exit_code) __attribute__((noreturn));
struct SyscallResult SyscallOpenWindow(int w, int h, int x, int y, const char* title);

#define LAYER_NO_REDRAW (0x00000001ull << 32)
struct SyscallResult SyscallWinWriteString(
    uint64_t layer_id_flags, int x, int y, uint32_t color, const char* s);
struct SyscallResult SyscallWinFillRectangle(
    uint64_t layer_id_flags, int x, int y, int w, int h, uint32_t color);
struct SyscallResult SyscallGetCurrentTick();
struct SyscallResult SyscallWinRedraw(uint64_t layer_id_flags);
struct SyscallResult SyscallWinDrawLine(
    uint64_t layer_id_flags, int x0, int y0, int x1, int y1, uint32_t color);

struct SyscallResult SyscallCloseWindow(uint64_t layer_id_flags);
struct SyscallResult SyscallReadEvent(struct AppEvent* events, size_t len);

#define TIMER_ONESHOT_REL 1
#define TIMER_ONESHOT_ABS 0
struct SyscallResult SyscallCreateTimer(
    unsigned int type, int timer_value, unsigned long timeout_ms);

struct SyscallResult SyscallOpenFile(const char* path, int flags);
struct SyscallResult SyscallReadFile(int fd, void* buf, size_t count);
struct SyscallResult SyscallDemandPages(size_t num_pages, int flags);
struct SyscallResult SyscallMapFile(int fd, size_t* file_size, int flags);
struct SyscallResult SyscallIsTerminal(int fd);

/* ---- ファイルマネージャ等向け(XinOS 拡張) ---- */
struct DirInfo {
  char name[16];       /* "NAME.EXT" (大文字) */
  unsigned char is_dir;
  unsigned char pad[3];
  unsigned int size;
};
/* path の index 番目の項目を取得。成功で value=1、終端で error=ENOENT */
struct SyscallResult SyscallReadDir(const char* path, int index, struct DirInfo* out);

#define FSOP_MKDIR 1
#define FSOP_REMOVE 2
#define FSOP_MOVE 3
#define FSOP_TRASH 4
#define FSOP_EMPTY_TRASH 5
#define FSOP_RESTORE 6
#define FSOP_REMOVE_ALL 7
/* 失敗時は error に errno 相当の値が入る */
struct SyscallResult SyscallFsOp(int op, const char* path1, const char* path2);

#define LAUNCH_WITH_TERMINAL 1
/* command を新しいタスクで実行する。cwd は NULL か空なら "/" */
struct SyscallResult SyscallLaunch(const char* command, int flags, const char* cwd);

/* 32bit ARGB(A=0 は透過)の w x h 画像をウィンドウへ描く */
struct SyscallResult SyscallWinBlit(uint64_t layer_id_flags, int x, int y, int w, int h,
                                    const unsigned int* argb);

#ifdef __cplusplus
} // extern "C"
#endif
