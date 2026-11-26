/*
 * ojay runtime support library.
 *
 * Compiled once per platform (see the platform layer below):
 *   linux hosted        - uses the C library (write, malloc, exit)
 *   linux freestanding  - talks to the Linux kernel directly, provides _start
 *   cortex-m            - bare metal: vector table, reset handler, a heap
 *                         (see "Heap" below) in the RAM the linker script
 *                         leaves free, console I/O via semihosting or the
 *                         board's UART (see "Console" below)
 *   esp32-c6            - bare metal RISC-V: watchdogs off, trap handler,
 *                         console via semihosting or the USB Serial/JTAG port
 *
 * This file must not include any libc headers; only compiler-provided
 * freestanding headers are used so it can be cross-compiled without a sysroot.
 */
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------------ */
/* Layouts shared with the compiler (src/codegen.cpp).                       */

typedef struct {
  int64_t count;
  uint8_t *data;
} OString;

/* Allocator_Proc and Allocator (src/preload.jai). Jai procedures use the C
 * calling convention for these argument types, so C can call them. */
typedef void *(*OAllocatorProc)(int64_t mode, int64_t size, int64_t old_size, void *old_memory, void *data);
typedef struct {
  OAllocatorProc proc;
  void *data;
} OAllocator;
enum { MODE_ALLOCATE = 0, MODE_RESIZE = 1, MODE_FREE = 2 };

/* __allocate (preload.jai): an allocation through `allocator`, or through
 * context.allocator when it (or its proc) is null. The compiler passes it to
 * the runtime functions that allocate when the program uses the context;
 * otherwise they get null and use the runtime's heap. */
typedef void *(*OAllocateFn)(int64_t mode, int64_t size, int64_t old_size, void *old_memory,
                             const OAllocator *allocator);

typedef struct {
  int64_t count;
  uint8_t *data;
  int64_t allocated;
  OAllocator allocator; /* set when the array first grows (to context.allocator) */
} ODynArray;

/* Runtime type information: the layout of Type_Info and the Type_Info_*
   structs in src/core.jai (the compiler emits them; keep in sync). */
enum {
  TI_INTEGER = 0, TI_FLOAT = 1, TI_BOOL = 2, TI_STRING = 3, TI_POINTER = 4, TI_PROCEDURE = 5, TI_VOID = 6,
  TI_STRUCT = 7, TI_ARRAY = 8, TI_OVERLOAD_SET = 9, TI_ANY = 10, TI_ENUM = 11, TI_POLYMORPHIC_VARIABLE = 12,
  TI_TYPE = 13, TI_CODE = 14, TI_VARIANT = 15,
};

typedef struct OTypeInfo {
  uint32_t type; /* TI_* */
  int64_t runtime_size;
} OTypeInfo;

typedef struct {
  OTypeInfo info;
  uint8_t is_signed;
} OTypeInfoInteger;

typedef struct {
  OTypeInfo info;
  const OTypeInfo *pointer_to;
} OTypeInfoPointer;

enum { TI_ARRAY_FIXED = 0, TI_ARRAY_VIEW = 1, TI_ARRAY_RESIZABLE = 2 };

typedef struct {
  OTypeInfo info;
  const OTypeInfo *element_type;
  uint32_t array_type;
  int64_t array_count;
} OTypeInfoArray;

typedef struct {
  OString name;
  const OTypeInfo *type;
  int64_t offset_in_bytes;
  uint32_t flags;
} OTypeInfoMember;

enum { TI_STRUCT_UNION = 0x2 };
enum { TI_MEMBER_CONSTANT = 0x1 };

/* Type_Info_Variant: a distinct type. */
typedef struct {
  OTypeInfo info;
  OString name;
  const OTypeInfo *variant_of;
} OTypeInfoVariant;

typedef struct {
  OTypeInfo info;
  OString name;
  struct {
    int64_t count;
    const OTypeInfoMember *data;
  } members;
  uint32_t textual_flags;
} OTypeInfoStruct;

enum { TI_ENUM_FLAGS = 0x1 };

typedef struct {
  OTypeInfo info;
  OString name;
  const OTypeInfoInteger *internal_type;
  uint32_t enum_type_flags;
  struct {
    int64_t count;
    const OString *data;
  } names;
  struct {
    int64_t count;
    const int64_t *data;
  } values;
} OTypeInfoEnum;

enum { TI_PROC_C_CALL = 0x1, TI_PROC_VARARGS = 0x2 };

typedef struct {
  int64_t count;
  const OTypeInfo *const *data;
} OTypeInfoList;

typedef struct {
  OTypeInfo info;
  OTypeInfoList argument_types;
  OTypeInfoList return_types;
  uint32_t procedure_flags;
} OTypeInfoProcedure;

typedef struct {
  const OTypeInfo *type;
  const void *value;
} OAny;

/* ------------------------------------------------------------------------ */
/* Platform layer                                                            */
/*                                                                           */
/*   OJAY_PLATFORM_LINUX_HOSTED        libc (write, malloc, exit)            */
/*   OJAY_PLATFORM_LINUX_FREESTANDING  raw Linux syscalls, own _start        */
/*   OJAY_PLATFORM_CORTEXM             bare metal: vector table, semihosting */
/*   OJAY_PLATFORM_ESP32C6             bare metal RISC-V: semihosting or     */
/*                                     USB Serial/JTAG console               */

#if defined(OJAY_PLATFORM_LINUX_FREESTANDING)

#if !defined(__x86_64__)
#error "freestanding Linux runtime is only implemented for x86_64"
#endif
#define SYS_WRITE 1
#define SYS_MMAP 9
#define SYS_MUNMAP 11
#define SYS_EXIT_GROUP 231
static long sys3(long n, long a, long b, long c) {
  long r;
  __asm__ volatile("syscall" : "=a"(r) : "a"(n), "D"(a), "S"(b), "d"(c) : "rcx", "r11", "memory");
  return r;
}
static long sys6(long n, long a, long b, long c, long d, long e, long f) {
  long r;
  register long r10 __asm__("r10") = d;
  register long r8 __asm__("r8") = e;
  register long r9 __asm__("r9") = f;
  __asm__ volatile("syscall"
                   : "=a"(r)
                   : "a"(n), "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9)
                   : "rcx", "r11", "memory");
  return r;
}

static void plat_write(int fd, const void *buf, int64_t n) {
  const uint8_t *p = (const uint8_t *)buf;
  while (n > 0) {
    long r = sys3(SYS_WRITE, fd, (long)p, (long)n);
    if (r <= 0) return;
    p += r;
    n -= r;
  }
}

__attribute__((noreturn)) static void plat_exit(int code) {
  for (;;) sys3(SYS_EXIT_GROUP, code, 0, 0);
}

/* More memory for the heap: at least `min` bytes at [*start, *end). */
static int plat_more_heap(size_t min, uint8_t **start, uint8_t **end) {
  size_t size = min > (1u << 20) ? (min + 4095) & ~(size_t)4095 : (1u << 20);
  long r = sys6(SYS_MMAP, 0, (long)size, 3 /*RW*/, 0x22 /*PRIVATE|ANON*/, -1, 0);
  if (r < 0 && r > -4096) return 0;
  *start = (uint8_t *)r;
  *end = (uint8_t *)r + size;
  return 1;
}

#elif defined(OJAY_PLATFORM_CORTEXM)

/* Symbols provided by the board linker script. */
extern uint8_t __heap_start[], __heap_end[];
/* Defined (to any address) by linker scripts for simulators such as QEMU,
 * where semihosting works without a debugger attached. */
extern uint8_t __ojay_semihost_always __attribute__((weak));

#define SCB_DHCSR (*(volatile uint32_t *)0xE000EDF0u)

/* Console.
 *
 * Output goes to semihosting when a debugger handles it, otherwise to the
 * board's console: a program-provided `__ojay_console_write` (for example a
 * Jai `#c_call #program_export "__ojay_console_write"` procedure), else the
 * built-in UART driver the board script selects (`__ojay_console_mcxn947`:
 * LPUART4 on the FRDM-MCXN947, the MCU-Link's virtual COM port).
 *
 * A semihosting call (bkpt 0xAB) halts the core whenever halting debug is
 * enabled (DHCSR.C_DEBUGEN), whether or not a debugger is still there to
 * resume it, and that bit survives resets until a power cycle. So on boards
 * with a console, semihosting is only used when the debugger announced
 * itself: it starts the program at Reset_Handler with r0 = OJAY_DEBUGGER_MAGIC
 * and r1 = ~OJAY_DEBUGGER_MAGIC (tools/run_mcxn947.py does). Boards without a
 * console keep using semihosting whenever C_DEBUGEN is set, as nothing else
 * could show the output. */
#define OJAY_DEBUGGER_MAGIC 0x5E41D0B6u
extern uint8_t __ojay_console_mcxn947 __attribute__((weak));
extern void __ojay_console_write(const uint8_t *data, int64_t len) __attribute__((weak));

static int debugger_announced; /* set by Reset_Handler */

static int have_console(void) {
  return __ojay_console_write != 0 || (uintptr_t)&__ojay_console_mcxn947 != 0;
}

static int semihost_available(void) {
  if ((uintptr_t)&__ojay_semihost_always != 0) return 1;
  if ((SCB_DHCSR & 1u) == 0) return 0; /* C_DEBUGEN clear: bkpt would fault */
  return debugger_announced || !have_console();
}

/* ---- FRDM-MCXN947: LP_FLEXCOMM4 as LPUART, TX on P1_9, RX on P1_8 (ALT2),
 * clocked from FRO 12 MHz, 115200 8N1. Register layout from NXP's
 * MCXN947 device headers (SYSCON, SCG, PORT, LP_FLEXCOMM, LPUART). */
#define MMIO(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define MCX_SYSCON 0x40000000u
#define MCX_SCG0 0x40044000u
#define MCX_PORT1 0x40117000u
#define MCX_FC4 0x400B4000u
#define LPUART_BAUD 0x10u
#define LPUART_STAT 0x14u
#define LPUART_CTRL 0x18u
#define LPUART_DATA 0x1Cu
#define LPUART_FIFO 0x28u
#define LPUART_STAT_TDRE (1u << 23)
#define LPUART_STAT_TC (1u << 22)

static int mcx_uart_ready;

static void mcx_uart_init(void) {
  MMIO(MCX_SCG0 + 0x200) |= 1u << 5;          /* SIRCCSR.SIRC_CLK_PERIPH_EN: FRO 12 MHz to peripherals */
  MMIO(MCX_SYSCON + 0x220) = 1u << 14;        /* AHBCLKCTRLSET0: PORT1 */
  MMIO(MCX_SYSCON + 0x224) = 1u << 15;        /* AHBCLKCTRLSET1: LP_FLEXCOMM4 */
  MMIO(MCX_SYSCON + 0x140) = 1u << 14;        /* PRESETCTRLCLR0: PORT1 out of reset */
  MMIO(MCX_SYSCON + 0x144) = 1u << 15;        /* PRESETCTRLCLR1: LP_FLEXCOMM4 out of reset */
  MMIO(MCX_SYSCON + 0x2B0 + 4 * 4) = 2;       /* FCCLKSEL4: FRO 12 MHz */
  MMIO(MCX_SYSCON + 0x850 + 4 * 4) = 1u << 30; /* FLEXCOMM4CLKDIV: halt, then divide by 1 */
  MMIO(MCX_SYSCON + 0x850 + 4 * 4) = 0;
  for (int i = 0; i < 100000 && (MMIO(MCX_SYSCON + 0x850 + 4 * 4) & (1u << 31)); i++) {}
  MMIO(MCX_FC4 + 0xFF8) = (MMIO(MCX_FC4 + 0xFF8) & ~7u) | 1u; /* PSELID: LPUART */
  MMIO(MCX_FC4 + LPUART_CTRL) = 0;
  /* 12 MHz / (26 * 4) = 115385 baud (+0.16%): OSR = 26 - 1, SBR = 4. */
  MMIO(MCX_FC4 + LPUART_BAUD) = (25u << 24) | 4u;
  MMIO(MCX_FC4 + LPUART_FIFO) |= 1u << 7;     /* TXFE: transmit FIFO */
  MMIO(MCX_FC4 + LPUART_CTRL) = 1u << 19;     /* TE: the transmitter idles high */
  /* Route the pins only now, with a pull-up on TX, so the line never goes
   * low (which the receiver would take for a NUL byte or a break). */
  MMIO(MCX_PORT1 + 0x80 + 4 * 9) = (2u << 8) | 3u;          /* PCR9: TX, ALT2, pull-up */
  MMIO(MCX_PORT1 + 0x80 + 4 * 8) = (2u << 8) | (1u << 12) | 3u; /* PCR8: RX, ALT2, input buffer, pull-up */
  mcx_uart_ready = 1;
}

static void mcx_uart_write(const uint8_t *p, int64_t n) {
  if (!mcx_uart_ready) mcx_uart_init();
  for (int64_t i = 0; i < n; i++) {
    if (p[i] == '\n') {
      while (!(MMIO(MCX_FC4 + LPUART_STAT) & LPUART_STAT_TDRE)) {}
      MMIO(MCX_FC4 + LPUART_DATA) = '\r';
    }
    while (!(MMIO(MCX_FC4 + LPUART_STAT) & LPUART_STAT_TDRE)) {}
    MMIO(MCX_FC4 + LPUART_DATA) = p[i];
  }
}

static void console_write(const void *buf, int64_t n) {
  if (__ojay_console_write) __ojay_console_write((const uint8_t *)buf, n);
  else if ((uintptr_t)&__ojay_console_mcxn947 != 0) mcx_uart_write((const uint8_t *)buf, n);
}

static void console_drain(void) {
  if (!__ojay_console_write && mcx_uart_ready)
    while (!(MMIO(MCX_FC4 + LPUART_STAT) & LPUART_STAT_TC)) {}
}

static int semihost(int op, void *arg) {
  register int r0 __asm__("r0") = op;
  register void *r1 __asm__("r1") = arg;
  __asm__ volatile("bkpt 0xAB" : "+r"(r0) : "r"(r1) : "memory");
  return r0;
}

enum { SH_OPEN = 0x01, SH_WRITE = 0x05, SH_EXIT = 0x18, SH_EXIT_EXTENDED = 0x20 };

static int sh_handles[3] = {-1, -1, -1};

static int sh_handle(int fd) {
  if (fd != 1 && fd != 2) fd = 1;
  if (sh_handles[fd] < 0) {
    /* ":tt" is the console; mode 4 = "w" (stdout), 8 = "a" (stderr). */
    static const char tt[] = ":tt";
    uintptr_t args[3] = {(uintptr_t)tt, fd == 1 ? 4u : 8u, 3};
    sh_handles[fd] = semihost(SH_OPEN, args);
  }
  return sh_handles[fd];
}

static void plat_write(int fd, const void *buf, int64_t n) {
  if (n <= 0) return;
  if (!semihost_available()) {
    console_write(buf, n);
    return;
  }
  uintptr_t args[3] = {(uintptr_t)sh_handle(fd), (uintptr_t)buf, (uintptr_t)n};
  semihost(SH_WRITE, args);
}

/* Without semihosting the program ends in a wfi loop with r0 = OJAY_EXITED_MAGIC
 * and r1 = the exit status, where a debugger can read them. */
#define OJAY_EXITED_MAGIC 0xE417C0DEu

__attribute__((noreturn)) static void plat_exit(int code) {
  if (!semihost_available()) {
    console_drain();
    register uint32_t r0 __asm__("r0") = OJAY_EXITED_MAGIC;
    register uint32_t r1 __asm__("r1") = (uint32_t)code;
    for (;;) __asm__ volatile("wfi" : : "r"(r0), "r"(r1));
  }
  {
    /* ADP_Stopped_ApplicationExit with an exit code (SYS_EXIT_EXTENDED),
     * falling back to plain SYS_EXIT for hosts that lack it. */
    uintptr_t args[2] = {0x20026, (uintptr_t)code};
    semihost(SH_EXIT_EXTENDED, args);
    semihost(SH_EXIT, (void *)(uintptr_t)(code == 0 ? 0x20026 : 0x20023 /* RunTimeErrorUnknown */));
  }
  for (;;) __asm__ volatile("wfi");
}

/* The heap is the RAM between .bss and the stack, given out once. */
static int plat_more_heap(size_t min, uint8_t **start, uint8_t **end) {
  static int given;
  (void)min;
  if (given) return 0;
  given = 1;
  *start = __heap_start;
  *end = __heap_end;
  return 1;
}

#elif defined(OJAY_PLATFORM_ESP32C6)

/* ESP32-C6 high-performance core (RV32IMAC). Register addresses and write
 * protection keys are from ESP-IDF's public headers (soc/esp32c6). */

/* Symbols provided by the board linker script. */
extern uint8_t __heap_start[], __heap_end[];

/* Set by the trap handler when a semihosting call traps because no debugger
 * is attached; console output then goes to the USB Serial/JTAG port. */
volatile int __ojay_semihost_dead;

static int semihost(int op, void *arg) {
  register int a0 __asm__("a0") = op;
  register void *a1 __asm__("a1") = arg;
  /* The RISC-V semihosting sequence: uncompressed, in this exact order. */
  __asm__ volatile(
      ".balign 4\n"
      ".option push\n"
      ".option norvc\n"
      "slli x0, x0, 0x1f\n"
      "ebreak\n"
      "srai x0, x0, 7\n"
      ".option pop\n"
      : "+r"(a0)
      : "r"(a1)
      : "memory");
  return a0;
}

enum { SH_WRITE = 0x05, SH_EXIT = 0x18, SH_EXIT_EXTENDED = 0x20 };

/* Espressif's OpenOCD does not map the ":tt" console name (it would create a
 * file of that name), but passes write handles through as host descriptors:
 * handle 1 is the debugger's stdout and 2 its stderr. */
static int sh_handle(int fd) { return fd == 2 ? 2 : 1; }

/* USB Serial/JTAG controller: CDC-ACM console (e.g. /dev/ttyACM0). */
#define USJ_EP1 (*(volatile uint32_t *)0x6000F000u)
#define USJ_EP1_CONF (*(volatile uint32_t *)0x6000F004u)
#define USJ_WR_DONE (1u << 0)
#define USJ_IN_EP_DATA_FREE (1u << 1)

static int usj_dead;

static void usj_write(const uint8_t *p, int64_t n) {
  for (int64_t i = 0; i < n && !usj_dead; ++i) {
    uint32_t spins = 0;
    while (!(USJ_EP1_CONF & USJ_IN_EP_DATA_FREE)) {
      if (++spins > 500000) {  /* nobody is reading: stop trying */
        usj_dead = 1;
        return;
      }
    }
    USJ_EP1 = p[i];
  }
  USJ_EP1_CONF = USJ_WR_DONE;
}

static void plat_write(int fd, const void *buf, int64_t n) {
  if (n <= 0) return;
  if (!__ojay_semihost_dead) {
    uintptr_t args[3] = {(uintptr_t)sh_handle(fd), (uintptr_t)buf, (uintptr_t)n};
    semihost(SH_WRITE, args);
    if (!__ojay_semihost_dead) return;
  }
  usj_write((const uint8_t *)buf, n);
}

__attribute__((noreturn)) static void plat_exit(int code) {
  if (!__ojay_semihost_dead) {
    uintptr_t args[2] = {0x20026, (uintptr_t)code};
    semihost(SH_EXIT_EXTENDED, args);
    semihost(SH_EXIT, (void *)(uintptr_t)(code == 0 ? 0x20026 : 0x20023));
  }
  for (;;) __asm__ volatile("wfi");
}

/* The heap is the RAM between .bss and the stack, given out once. */
static int plat_more_heap(size_t min, uint8_t **start, uint8_t **end) {
  static int given;
  (void)min;
  if (given) return 0;
  given = 1;
  *start = __heap_start;
  *end = __heap_end;
  return 1;
}

#elif defined(OJAY_PLATFORM_LINUX_HOSTED)

long write(int fd, const void *buf, size_t n);
void *malloc(size_t n);
void *realloc(void *p, size_t n);
void free(void *p);
__attribute__((noreturn)) void exit(int code);

static void plat_write(int fd, const void *buf, int64_t n) {
  const uint8_t *p = (const uint8_t *)buf;
  while (n > 0) {
    long r = write(fd, p, (size_t)n);
    if (r <= 0) return;
    p += r;
    n -= r;
  }
}
__attribute__((noreturn)) static void plat_exit(int code) { exit(code); }
__attribute__((noreturn)) static void heap_out_of_memory(int64_t n);
static void *plat_alloc(int64_t size) {
  void *p = size < 0 ? 0 : malloc(size > 0 ? (size_t)size : 1);
  if (!p) heap_out_of_memory(size);
  return p;
}
static void plat_free(void *p) { free(p); }
int posix_memalign(void **p, size_t align, size_t n);
static void *plat_alloc_aligned(int64_t size, int64_t align) {
  if (align <= 16) return plat_alloc(size);
  void *p = 0;
  if (size < 0 || posix_memalign(&p, (size_t)align, size > 0 ? (size_t)size : 1) != 0) heap_out_of_memory(size);
  return p;
}
static void *plat_realloc(void *p, int64_t size) {
  void *q = size < 0 ? 0 : realloc(p, size > 0 ? (size_t)size : 1);
  if (!q) heap_out_of_memory(size);
  return q;
}

#else
#error "define one of the OJAY_PLATFORM_* macros"
#endif

#if !defined(OJAY_PLATFORM_LINUX_HOSTED)
/* ------------------------------------------------------------------------ */
/* Heap (platforms without libc)                                             */
/*                                                                           */
/* A first-fit allocator with boundary tags over the memory that             */
/* plat_more_heap() gives (bare metal: all RAM between .bss and the stack;   */
/* freestanding Linux: mmap'ed chunks, each an arena of its own).            */
/*                                                                           */
/* Every block starts with a two-word header: the size of the previous block */
/* (valid only while that one is free) and this block's size with two flag   */
/* bits, INUSE and PREV_INUSE. Sizes include the header and are multiples of */
/* HEAP_ALIGN (8 bytes on 32-bit targets, 16 on 64-bit), so payloads are     */
/* HEAP_ALIGN-aligned. Free blocks are on one doubly linked list (links in   */
/* the payload) and are merged with free neighbours as soon as they are      */
/* freed, so no two free blocks are ever adjacent. Each arena ends with a    */
/* zero-size INUSE fence header, so merging never runs off its end.          */
/* Allocation takes the first free block that fits and splits off the rest  */
/* when that is big enough to be a block. realloc grows in place into a     */
/* free successor when it can, and shrinks in place.                        */
/*                                                                           */
/* free() and realloc() check the block they are given (in a heap arena,     */
/* aligned, in use, consistent with its neighbour) and stop the program on a */
/* bad or double free. Running out of memory stops the program too, with a  */
/* message giving the request and the heap's state.                          */

typedef struct HeapBlock {
  size_t prev_size;
  size_t head; /* size | HEAP_INUSE | HEAP_PREV_INUSE */
  struct HeapBlock *next, *prev; /* free blocks only */
} HeapBlock;

#define HEAP_ALIGN (2 * sizeof(size_t))
#define HEAP_HDR (2 * sizeof(size_t))
#define HEAP_MIN_BLOCK ((sizeof(HeapBlock) + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1))
#define HEAP_INUSE ((size_t)1)
#define HEAP_PREV_INUSE ((size_t)2)
#define HEAP_FLAGS (HEAP_INUSE | HEAP_PREV_INUSE)
#define HEAP_MAX_ARENAS 64

static HeapBlock *heap_free_list;
static struct {
  uint8_t *start, *end; /* end: the fence header */
} heap_arenas[HEAP_MAX_ARENAS];
static int heap_narenas;
static size_t heap_total, heap_free_bytes;

static size_t blk_size(const HeapBlock *b) { return b->head & ~HEAP_FLAGS; }
static HeapBlock *blk_next(HeapBlock *b) { return (HeapBlock *)((uint8_t *)b + blk_size(b)); }
static void *blk_payload(HeapBlock *b) { return (uint8_t *)b + HEAP_HDR; }

static void heap_link(HeapBlock *b) {
  b->prev = 0;
  b->next = heap_free_list;
  if (heap_free_list) heap_free_list->prev = b;
  heap_free_list = b;
  heap_free_bytes += blk_size(b);
}

static void heap_unlink(HeapBlock *b) {
  if (b->prev) b->prev->next = b->next;
  else heap_free_list = b->next;
  if (b->next) b->next->prev = b->prev;
  heap_free_bytes -= blk_size(b);
}

/* Makes `b` (not on the free list, flags of its own as given) free and puts
 * it on the list; its neighbours must not be free. */
static void heap_make_free(HeapBlock *b, size_t size) {
  b->head = size | HEAP_PREV_INUSE;
  HeapBlock *n = blk_next(b);
  n->prev_size = size;
  n->head &= ~HEAP_PREV_INUSE;
  heap_link(b);
}

/* Frees the block at `b` of `size` bytes (its own flags are ignored; its
 * predecessor is in use), merging it with a free successor. */
static void heap_release_tail(HeapBlock *b, size_t size) {
  HeapBlock *n = (HeapBlock *)((uint8_t *)b + size);
  if (!(n->head & HEAP_INUSE)) {
    heap_unlink(n);
    size += blk_size(n);
  }
  heap_make_free(b, size);
}

/* Shrinks in-use block `b` to `need` bytes if the rest can be a block. */
static void heap_trim(HeapBlock *b, size_t need) {
  size_t size = blk_size(b);
  if (size - need < HEAP_MIN_BLOCK) return;
  b->head = need | (b->head & HEAP_FLAGS);
  heap_release_tail((HeapBlock *)((uint8_t *)b + need), size - need);
}

static int heap_add_arena(size_t min) {
  uint8_t *start, *end;
  if (heap_narenas == HEAP_MAX_ARENAS || !plat_more_heap(min + 2 * HEAP_HDR + HEAP_ALIGN, &start, &end)) return 0;
  start = (uint8_t *)(((uintptr_t)start + HEAP_ALIGN - 1) & ~(uintptr_t)(HEAP_ALIGN - 1));
  end = (uint8_t *)((uintptr_t)end & ~(uintptr_t)(HEAP_ALIGN - 1)) - HEAP_HDR;
  if (end <= start || (size_t)(end - start) < HEAP_MIN_BLOCK) return 0;
  HeapBlock *fence = (HeapBlock *)end;
  fence->head = HEAP_INUSE;
  heap_arenas[heap_narenas].start = start;
  heap_arenas[heap_narenas].end = end;
  ++heap_narenas;
  HeapBlock *b = (HeapBlock *)start;
  heap_total += (size_t)(end - start);
  heap_make_free(b, (size_t)(end - start));
  return 1;
}

static size_t heap_largest_free(void) {
  size_t m = 0;
  for (HeapBlock *b = heap_free_list; b; b = b->next)
    if (blk_size(b) > m) m = blk_size(b);
  return m > HEAP_HDR ? m - HEAP_HDR : 0;
}

/* The block size for a request of `n` bytes, or 0 if it is too big. */
static size_t heap_need(int64_t n) {
  if (n < 0 || (uint64_t)n > (uint64_t)(SIZE_MAX / 2)) return 0;
  size_t need = ((size_t)n + HEAP_HDR + HEAP_ALIGN - 1) & ~(HEAP_ALIGN - 1);
  return need < HEAP_MIN_BLOCK ? HEAP_MIN_BLOCK : need;
}

__attribute__((noreturn)) static void heap_out_of_memory(int64_t n);
__attribute__((noreturn)) static void heap_bad_pointer(const char *what, void *p);

static void *plat_alloc(int64_t n) {
  size_t need = heap_need(n);
  if (!need) heap_out_of_memory(n);
  for (;;) {
    for (HeapBlock *b = heap_free_list; b; b = b->next) {
      if (blk_size(b) < need) continue;
      heap_unlink(b);
      b->head |= HEAP_INUSE;
      blk_next(b)->head |= HEAP_PREV_INUSE;
      heap_trim(b, need);
      return blk_payload(b);
    }
    if (!heap_add_arena(need)) heap_out_of_memory(n);
  }
}

/* A block whose payload is `align`-aligned (a power of two above HEAP_ALIGN):
 * the first free block with room for it after a gap that is big enough to be
 * a free block of its own (or no gap at all). free() takes it as usual. */
static void *plat_alloc_aligned(int64_t n, int64_t align) {
  if (align <= (int64_t)HEAP_ALIGN) return plat_alloc(n);
  size_t need = heap_need(n);
  if (!need || (uint64_t)align > (uint64_t)(SIZE_MAX / 4)) heap_out_of_memory(n);
  uintptr_t a = (uintptr_t)align;
  for (;;) {
    for (HeapBlock *b = heap_free_list; b; b = b->next) {
      uintptr_t start = (uintptr_t)b, end = start + blk_size(b);
      uintptr_t p = (start + HEAP_HDR + a - 1) & ~(a - 1);
      while (p - HEAP_HDR != start && p - HEAP_HDR - start < HEAP_MIN_BLOCK) p += a;
      if (p - HEAP_HDR + need > end) continue;
      heap_unlink(b);
      HeapBlock *nb = (HeapBlock *)(p - HEAP_HDR);
      size_t gap = (size_t)((uintptr_t)nb - start);
      nb->head = (blk_size(b) - gap) | HEAP_INUSE | HEAP_PREV_INUSE;
      if (gap) heap_make_free(b, gap); /* the gap stays free; nb's PREV_INUSE is cleared */
      blk_next(nb)->head |= HEAP_PREV_INUSE;
      heap_trim(nb, need);
      return blk_payload(nb);
    }
    if (!heap_add_arena(need + (size_t)align + HEAP_MIN_BLOCK)) heap_out_of_memory(n);
  }
}

/* The block of payload `p`, checked to be an in-use heap block. */
static HeapBlock *heap_block_of(void *p, const char *what) {
  uint8_t *q = (uint8_t *)p;
  int i = 0;
  while (i < heap_narenas && !(q >= heap_arenas[i].start + HEAP_HDR && q < heap_arenas[i].end)) ++i;
  if (i == heap_narenas || ((uintptr_t)q & (HEAP_ALIGN - 1))) heap_bad_pointer(what, p);
  HeapBlock *b = (HeapBlock *)(q - HEAP_HDR);
  size_t size = blk_size(b);
  if (!(b->head & HEAP_INUSE)) heap_bad_pointer(what, p);
  if (size < HEAP_MIN_BLOCK || size > (size_t)(heap_arenas[i].end - (uint8_t *)b) ||
      !(blk_next(b)->head & HEAP_PREV_INUSE))
    heap_bad_pointer(what, p);
  return b;
}

static void plat_free(void *p) {
  if (!p) return;
  HeapBlock *b = heap_block_of(p, "free");
  size_t size = blk_size(b);
  if (!(b->head & HEAP_PREV_INUSE)) { /* merge with the free predecessor */
    HeapBlock *prev = (HeapBlock *)((uint8_t *)b - b->prev_size);
    heap_unlink(prev);
    size += blk_size(prev);
    b = prev;
  }
  heap_release_tail(b, size);
}

void *memcpy(void *dst, const void *src, size_t n);

static void *plat_realloc(void *p, int64_t n) {
  if (!p) return plat_alloc(n);
  HeapBlock *b = heap_block_of(p, "realloc");
  size_t need = heap_need(n);
  if (!need) heap_out_of_memory(n);
  size_t size = blk_size(b);
  if (need > size) { /* grow into a free successor */
    HeapBlock *next = blk_next(b);
    if (!(next->head & HEAP_INUSE) && size + blk_size(next) >= need) {
      heap_unlink(next);
      size += blk_size(next);
      b->head = size | (b->head & HEAP_FLAGS);
      blk_next(b)->head |= HEAP_PREV_INUSE;
    }
  }
  if (need <= size) {
    heap_trim(b, need);
    return p;
  }
  void *q = plat_alloc(n);
  memcpy(q, p, size - HEAP_HDR);
  plat_free(p);
  return q;
}
#endif

/* ------------------------------------------------------------------------ */
/* Output buffers                                                            */

typedef struct {
  int fd;       /* >= 0: flush to this fd; < 0: grow in heap */
  uint8_t *buf;
  int64_t len, cap;
  int64_t total;
  uint8_t local[1024];
} Out;

static void out_init_fd(Out *o, int fd) {
  o->fd = fd;
  o->buf = o->local;
  o->len = 0;
  o->cap = (int64_t)sizeof(o->local);
  o->total = 0;
}

static void out_init_heap(Out *o) {
  o->fd = -1;
  o->buf = o->local;
  o->len = 0;
  o->cap = (int64_t)sizeof(o->local);
  o->total = 0;
}

static void out_flush(Out *o) {
  if (o->fd >= 0 && o->len) {
    plat_write(o->fd, o->buf, o->len);
    o->len = 0;
  }
}

static void out_bytes(Out *o, const void *data, int64_t n) {
  const uint8_t *p = (const uint8_t *)data;
  o->total += n;
  while (n > 0) {
    if (o->len == o->cap) {
      if (o->fd >= 0) {
        out_flush(o);
      } else {
        int64_t ncap = o->cap * 2;
        uint8_t *nb = (uint8_t *)plat_alloc(ncap);
        for (int64_t i = 0; i < o->len; ++i) nb[i] = o->buf[i];
        if (o->buf != o->local) plat_free(o->buf);
        o->buf = nb;
        o->cap = ncap;
      }
    }
    int64_t room = o->cap - o->len;
    int64_t k = n < room ? n : room;
    for (int64_t i = 0; i < k; ++i) o->buf[o->len + i] = p[i];
    o->len += k;
    p += k;
    n -= k;
  }
}

static void out_cstr(Out *o, const char *s) {
  int64_t n = 0;
  while (s[n]) ++n;
  out_bytes(o, s, n);
}

static void out_u64(Out *o, uint64_t v) {
  char tmp[24];
  int i = 24;
  do {
    uint64_t q = v / 10;
    tmp[--i] = (char)('0' + (v - q * 10));
    v = q;
  } while (v);
  out_bytes(o, tmp + i, 24 - i);
}

static void out_s64(Out *o, int64_t v) {
  if (v < 0) {
    out_bytes(o, "-", 1);
    out_u64(o, (uint64_t)0 - (uint64_t)v);
  } else {
    out_u64(o, (uint64_t)v);
  }
}

static void out_hex(Out *o, uint64_t v) {
  char tmp[16];
  int i = 16;
  do {
    tmp[--i] = "0123456789abcdef"[v & 15];
    v >>= 4;
  } while (v);
  out_bytes(o, "0x", 2);
  out_bytes(o, tmp + i, 16 - i);
}

static void out_f64(Out *o, double x) {
  if (x != x) {
    out_cstr(o, "NaN");
    return;
  }
  if (x < 0 || (x == 0 && 1.0 / x < 0)) {
    out_bytes(o, "-", 1);
    x = -x;
  }
  if (x > 1.7976931348623157e308) {
    out_cstr(o, "inf");
    return;
  }
  int exp10 = 0;
  if (x >= 1e15) {
    while (x >= 10.0) {
      x /= 10.0;
      ++exp10;
    }
  }
  /* 6 digits after the decimal point, then trailing zeros are removed. */
  uint64_t ip = (uint64_t)x;
  double frac = x - (double)ip;
  uint64_t fp = (uint64_t)(frac * 1000000.0 + 0.5);
  if (fp >= 1000000) {
    ip += 1;
    fp -= 1000000;
  }
  out_u64(o, ip);
  if (fp) {
    char digits[6];
    for (int i = 5; i >= 0; --i) {
      uint64_t q = fp / 10;
      digits[i] = (char)('0' + (fp - q * 10));
      fp = q;
    }
    int n = 6;
    while (n > 0 && digits[n - 1] == '0') --n;
    out_bytes(o, ".", 1);
    out_bytes(o, digits, n);
  }
  if (exp10) {
    out_cstr(o, "e+");
    out_u64(o, (uint64_t)exp10);
  }
}

static int64_t read_int(const void *p, int64_t size, int is_signed) {
  switch (size) {
    case 1: return is_signed ? (int64_t) * (const int8_t *)p : (int64_t) * (const uint8_t *)p;
    case 2: return is_signed ? (int64_t) * (const int16_t *)p : (int64_t) * (const uint16_t *)p;
    case 4: return is_signed ? (int64_t) * (const int32_t *)p : (int64_t) * (const uint32_t *)p;
    default: return *(const int64_t *)p;
  }
}

static void print_value(Out *o, const OTypeInfo *t, const void *v, int depth);

static void print_enum(Out *o, const OTypeInfo *ti, const void *v) {
  const OTypeInfoEnum *t = (const OTypeInfoEnum *)ti;
  int is_signed = t->internal_type->is_signed;
  int64_t val = read_int(v, ti->runtime_size, is_signed);
  int64_t n = t->values.count;
  if (!(t->enum_type_flags & TI_ENUM_FLAGS)) {
    for (int64_t i = 0; i < n; ++i) {
      if (t->values.data[i] == val) {
        out_bytes(o, t->names.data[i].data, t->names.data[i].count);
        return;
      }
    }
    out_s64(o, val);
    return;
  }
  /* enum_flags */
  if (val == 0) {
    for (int64_t i = 0; i < n; ++i)
      if (t->values.data[i] == 0) {
        out_bytes(o, t->names.data[i].data, t->names.data[i].count);
        return;
      }
    out_cstr(o, "0");
    return;
  }
  uint64_t rest = (uint64_t)val;
  int first = 1;
  for (int64_t i = 0; i < n; ++i) {
    uint64_t m = (uint64_t)t->values.data[i];
    if (m && (rest & m) == m) {
      if (!first) out_cstr(o, " | ");
      out_bytes(o, t->names.data[i].data, t->names.data[i].count);
      rest &= ~m;
      first = 0;
    }
  }
  if (rest) {
    if (!first) out_cstr(o, " | ");
    out_u64(o, rest);
  }
}

/* A type's name, as the compiler writes it (s64, *Node, [..]u8, (s32) -> bool, ...). */
static void print_type_name(Out *o, const OTypeInfo *t, int depth) {
  if (!t) {
    out_cstr(o, "(null type)");
    return;
  }
  if (depth > 16) {
    out_cstr(o, "...");
    return;
  }
  switch (t->type) {
    case TI_INTEGER:
      out_cstr(o, ((const OTypeInfoInteger *)t)->is_signed ? "s" : "u");
      out_u64(o, (uint64_t)t->runtime_size * 8);
      return;
    case TI_FLOAT: out_cstr(o, t->runtime_size == 4 ? "float32" : "float64"); return;
    case TI_BOOL: out_cstr(o, "bool"); return;
    case TI_STRING: out_cstr(o, "string"); return;
    case TI_VOID: out_cstr(o, "void"); return;
    case TI_ANY: out_cstr(o, "Any"); return;
    case TI_TYPE: out_cstr(o, "Type"); return;
    case TI_CODE: out_cstr(o, "Code"); return;
    case TI_POINTER:
      out_cstr(o, "*");
      print_type_name(o, ((const OTypeInfoPointer *)t)->pointer_to, depth + 1);
      return;
    case TI_ARRAY: {
      const OTypeInfoArray *a = (const OTypeInfoArray *)t;
      if (a->array_type == TI_ARRAY_FIXED) {
        out_cstr(o, "[");
        out_s64(o, a->array_count);
        out_cstr(o, "]");
      } else {
        out_cstr(o, a->array_type == TI_ARRAY_VIEW ? "[]" : "[..]");
      }
      print_type_name(o, a->element_type, depth + 1);
      return;
    }
    case TI_STRUCT: {
      const OTypeInfoStruct *st = (const OTypeInfoStruct *)t;
      out_bytes(o, st->name.data, st->name.count);
      return;
    }
    case TI_ENUM: {
      const OTypeInfoEnum *e = (const OTypeInfoEnum *)t;
      out_bytes(o, e->name.data, e->name.count);
      return;
    }
    case TI_VARIANT: {
      const OTypeInfoVariant *d = (const OTypeInfoVariant *)t;
      out_bytes(o, d->name.data, d->name.count);
      return;
    }
    case TI_PROCEDURE: {
      const OTypeInfoProcedure *p = (const OTypeInfoProcedure *)t;
      out_cstr(o, "(");
      for (int64_t i = 0; i < p->argument_types.count; ++i) {
        const OTypeInfo *a = p->argument_types.data[i];
        if (i) out_cstr(o, ", ");
        if ((p->procedure_flags & TI_PROC_VARARGS) && i + 1 == p->argument_types.count && a->type == TI_ARRAY) {
          out_cstr(o, "..");
          a = ((const OTypeInfoArray *)a)->element_type;
        }
        print_type_name(o, a, depth + 1);
      }
      out_cstr(o, ")");
      for (int64_t i = 0; i < p->return_types.count; ++i) {
        out_cstr(o, i ? ", " : " -> ");
        print_type_name(o, p->return_types.data[i], depth + 1);
      }
      if (p->procedure_flags & TI_PROC_C_CALL) out_cstr(o, " #c_call");
      return;
    }
    default: out_cstr(o, "(unknown type)"); return;
  }
}

/* The members of a struct, or the first of a union. Anonymous struct and
   union members (no name) are shown inline, as members of the enclosing one.
   Constants are skipped; members that overlap (#place) are all shown, each
   read from its own offset. */
static void print_members(Out *o, const OTypeInfo *ti, const void *v, int depth, int *first) {
  const OTypeInfoStruct *t = (const OTypeInfoStruct *)ti;
  int64_t n = t->members.count;
  int union_ = (t->textual_flags & TI_STRUCT_UNION) != 0; /* unions: first member */
  for (int64_t i = 0; i < n; ++i) {
    const OTypeInfoMember *m = &t->members.data[i];
    if ((m->flags & TI_MEMBER_CONSTANT) || m->type->type == TI_VOID) continue; /* constants, void markers */
    const uint8_t *p = (const uint8_t *)v + m->offset_in_bytes;
    if (m->name.count == 0 && m->type->type == TI_STRUCT) {
      print_members(o, m->type, p, depth, first);
      if (union_) return;
      continue;
    }
    if (!*first) out_cstr(o, ", ");
    *first = 0;
    print_value(o, m->type, p, depth + 1);
    if (union_) return;
  }
}

static void print_value(Out *o, const OTypeInfo *t, const void *v, int depth) {
  if (depth > 32) {
    out_cstr(o, "...");
    return;
  }
  switch (t->type) {
    case TI_VOID: out_cstr(o, "void"); return;
    case TI_BOOL: out_cstr(o, *(const uint8_t *)v ? "true" : "false"); return;
    case TI_INTEGER:
      if (((const OTypeInfoInteger *)t)->is_signed) out_s64(o, read_int(v, t->runtime_size, 1));
      else if (t->runtime_size == 8) out_u64(o, *(const uint64_t *)v);
      else out_u64(o, (uint64_t)read_int(v, t->runtime_size, 0));
      return;
    case TI_FLOAT:
      out_f64(o, t->runtime_size == 4 ? (double)*(const float *)v : *(const double *)v);
      return;
    case TI_STRING: {
      const OString *s = (const OString *)v;
      out_bytes(o, s->data, s->count);
      return;
    }
    case TI_POINTER:
    case TI_PROCEDURE: {
      uintptr_t p = *(const uintptr_t *)v;
      if (!p) out_cstr(o, "null");
      else out_hex(o, (uint64_t)p);
      return;
    }
    case TI_ARRAY: {
      const OTypeInfoArray *at = (const OTypeInfoArray *)t;
      int64_t count;
      const uint8_t *data;
      if (at->array_type == TI_ARRAY_FIXED) {
        count = at->array_count;
        data = (const uint8_t *)v;
      } else {
        const OString *view = (const OString *)v; /* same layout as a view */
        count = view->count;
        data = view->data;
      }
      out_bytes(o, "[", 1);
      for (int64_t i = 0; i < count; ++i) {
        if (i) out_cstr(o, ", ");
        print_value(o, at->element_type, data + i * at->element_type->runtime_size, depth + 1);
      }
      out_bytes(o, "]", 1);
      return;
    }
    case TI_STRUCT: {
      out_bytes(o, "{", 1);
      int first = 1;
      print_members(o, t, v, depth, &first);
      out_bytes(o, "}", 1);
      return;
    }
    case TI_ENUM: print_enum(o, t, v); return;
    case TI_VARIANT: print_value(o, ((const OTypeInfoVariant *)t)->variant_of, v, depth); return;
    case TI_ANY: {
      const OAny *a = (const OAny *)v;
      print_value(o, a->type, a->value, depth + 1);
      return;
    }
    case TI_TYPE: print_type_name(o, *(const OTypeInfo *const *)v, 0); return;
    default: out_cstr(o, "(unknown)"); return;
  }
}

static void format(Out *o, const uint8_t *fmt, int64_t len, const OAny *args, int64_t n) {
  int64_t next = 0;
  int64_t start = 0;
  for (int64_t i = 0; i < len; ++i) {
    if (fmt[i] != '%') continue;
    out_bytes(o, fmt + start, i - start);
    if (i + 1 < len && fmt[i + 1] == '%') {
      out_bytes(o, "%", 1);
      ++i;
    } else if (next < n) {
      print_value(o, args[next].type, args[next].value, 0);
      ++next;
    } else {
      out_bytes(o, "%", 1);
    }
    start = i + 1;
  }
  out_bytes(o, fmt + start, len - start);
}

/* ------------------------------------------------------------------------ */
/* Entry points called by generated code                                     */

/* ------------------------------------------------------------------------ */
/* Heap errors                                                               */

__attribute__((noreturn)) static void heap_out_of_memory(int64_t n) {
  Out o;
  out_init_fd(&o, 2);
  out_cstr(&o, "Out of memory: cannot allocate ");
  out_s64(&o, n);
  out_cstr(&o, " bytes");
#if !defined(OJAY_PLATFORM_LINUX_HOSTED)
  out_cstr(&o, " (heap: ");
  out_u64(&o, heap_total);
  out_cstr(&o, " bytes, ");
  out_u64(&o, heap_free_bytes);
  out_cstr(&o, " free, largest free block ");
  out_u64(&o, heap_largest_free());
  out_cstr(&o, ")");
#endif
  out_cstr(&o, ".\n");
  out_flush(&o);
  plat_exit(1);
}

#if !defined(OJAY_PLATFORM_LINUX_HOSTED)
__attribute__((noreturn)) static void heap_bad_pointer(const char *what, void *p) {
  Out o;
  out_init_fd(&o, 2);
  out_cstr(&o, what);
  out_cstr(&o, "(): ");
  out_hex(&o, (uint64_t)(uintptr_t)p);
  out_cstr(&o, " is not an allocated block (already freed, or not from the heap).\n");
  out_flush(&o);
  plat_exit(1);
}
#endif

/* Saved for get_command_line_arguments(). */
int __ojay_argc;
char **__ojay_argv;

/* get_command_line_arguments(): the arguments as Jai strings ({count, data}),
   built on the first call; the array and the strings belong to the runtime.
   Bare-metal programs have none. */
const OString *__ojay_command_line_arguments(int64_t *count) {
  static OString *args;
  static int64_t n;
  if (!args && __ojay_argc > 0 && __ojay_argv) {
    OString *a = (OString *)plat_alloc((int64_t)sizeof(OString) * __ojay_argc);
    if (a) {
      for (int i = 0; i < __ojay_argc; i++) {
        const char *s = __ojay_argv[i] ? __ojay_argv[i] : "";
        int64_t len = 0;
        while (s[len]) len++;
        a[i].count = len;
        a[i].data = (uint8_t *)s;
      }
      n = __ojay_argc;
      args = a;
    }
  }
  *count = n;
  return args;
}

void __ojay_runtime_init(int argc, char **argv) {
  __ojay_argc = argc;
  __ojay_argv = argv;
}

int64_t __ojay_print(const uint8_t *fmt, int64_t len, const OAny *args, int64_t n, int fd) {
  Out o;
  out_init_fd(&o, fd);
  format(&o, fmt, len, args, n);
  out_flush(&o);
  return o.total;
}

/* Allocates `size` bytes with `allocate` and `allocator`, or from the heap. */
static void *alloc_with(OAllocateFn allocate, const OAllocator *allocator, int64_t size) {
  return allocate ? allocate(MODE_ALLOCATE, size, 0, 0, allocator) : plat_alloc(size);
}

void __ojay_sprint(OString *result, const uint8_t *fmt, int64_t len, const OAny *args, int64_t n,
                   OAllocateFn allocate, const OAllocator *allocator) {
  Out o;
  out_init_heap(&o);
  format(&o, fmt, len, args, n);
  uint8_t *data = (uint8_t *)alloc_with(allocate, allocator, o.len + 1);
  for (int64_t i = 0; i < o.len; ++i) data[i] = o.buf[i];
  data[o.len] = 0;
  if (o.buf != o.local) plat_free(o.buf);
  result->count = o.len;
  result->data = data;
}

void __ojay_write_string(const uint8_t *data, int64_t len, int fd) { plat_write(fd, data, len); }

void *__ojay_alloc(int64_t size) { return plat_alloc(size); }
/* New / NewArray of a type aligned more than the heap guarantees (#align). */
void *__ojay_alloc_aligned(int64_t size, int64_t align) { return plat_alloc_aligned(size, align); }
void __ojay_free(void *p) { plat_free(p); }
void *__ojay_realloc(void *p, int64_t new_size, int64_t old_size) {
  (void)old_size;
  return plat_realloc(p, new_size);
}

__attribute__((noreturn)) void __ojay_exit(int code) { plat_exit(code); }

int __ojay_string_equal(const uint8_t *a, int64_t alen, const uint8_t *b, int64_t blen) {
  if (alen != blen) return 0;
  for (int64_t i = 0; i < alen; ++i)
    if (a[i] != b[i]) return 0;
  return 1;
}

/* `context_allocator` is the context's Allocator (with `allocate`), which the
 * array adopts when it first grows. */
// Makes room for `needed` items: exactly that many (array_reserve) or, when
// growing for array_add (`exact` = 0), at least twice as many as before.
/* `align`: 0, or the elements' alignment when it is more than the heap's
 * (#align); the array then moves to a new aligned block each time it grows,
 * from the heap or (with a context) from __allocate_aligned, the compiler's
 * aligned allocation through the array's allocator. */
typedef void *(*OAllocateAlignedFn)(int64_t size, int64_t align, const OAllocator *allocator);

void __ojay_array_reserve(ODynArray *a, int64_t elem_size, int64_t needed, int32_t exact, OAllocateFn allocate,
                          const OAllocator *context_allocator, int64_t align, OAllocateAlignedFn allocate_aligned) {
  if (a->allocated >= needed) return;
  int64_t cap = needed;
  if (!exact) {
    cap = a->allocated * 2;
    if (cap < 8) cap = 8;
    if (cap < needed) cap = needed;
  }
  uint8_t *data;
  if (allocate && !a->allocator.proc) a->allocator = *context_allocator;
  if (align > (int64_t)(2 * sizeof(void *))) {
    data = (uint8_t *)(allocate_aligned ? allocate_aligned(cap * elem_size, align, &a->allocator)
                                        : plat_alloc_aligned(cap * elem_size, align));
    if (data && a->data) {
      for (int64_t i = 0; i < a->count * elem_size; ++i) data[i] = a->data[i];
      if (allocate) allocate(MODE_FREE, 0, 0, a->data, &a->allocator);
      else plat_free(a->data);
    }
  } else if (allocate) {
    data = (uint8_t *)allocate(MODE_RESIZE, cap * elem_size, a->allocated * elem_size, a->data, &a->allocator);
  } else {
    data = (uint8_t *)plat_realloc(a->data, cap * elem_size);
  }
  if (!data) {
    static const char msg[] = "array_add / array_reserve: out of memory\n";
    plat_write(2, msg, sizeof(msg) - 1);
    plat_exit(1);
  }
  a->data = data;
  a->allocated = cap;
}

uint8_t *__ojay_to_c_string(const uint8_t *data, int64_t len, OAllocateFn allocate, const OAllocator *allocator) {
  uint8_t *r = (uint8_t *)alloc_with(allocate, allocator, len + 1);
  for (int64_t i = 0; i < len; ++i) r[i] = data[i];
  r[len] = 0;
  return r;
}

__attribute__((noreturn)) void __ojay_bounds_fail(int64_t index, int64_t count, const uint8_t *file,
                                                  int64_t file_len, int line) {
  Out o;
  out_init_fd(&o, 2);
  out_bytes(&o, file, file_len);
  out_cstr(&o, ":");
  out_s64(&o, line);
  out_cstr(&o, ": Array bounds check failed. Index ");
  out_s64(&o, index);
  out_cstr(&o, ", count ");
  out_s64(&o, count);
  out_cstr(&o, ".\n");
  out_flush(&o);
  plat_exit(1);
}

__attribute__((noreturn)) void __ojay_assert_fail(const uint8_t *file, int64_t file_len, int line,
                                                  const uint8_t *msg, int64_t msg_len) {
  Out o;
  out_init_fd(&o, 2);
  out_bytes(&o, file, file_len);
  out_cstr(&o, ":");
  out_s64(&o, line);
  out_cstr(&o, ": Assertion failed");
  if (msg_len) {
    out_cstr(&o, ": ");
    out_bytes(&o, msg, msg_len);
  }
  out_cstr(&o, "\n");
  out_flush(&o);
  plat_exit(1);
}

/* ------------------------------------------------------------------------ */
/* Program startup and compiler support routines (no-libc platforms)        */

#if !defined(OJAY_PLATFORM_LINUX_HOSTED)

int main(int argc, char **argv);

void *memcpy(void *dst, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  for (size_t i = 0; i < n; ++i) d[i] = s[i];
  return dst;
}

void *memmove(void *dst, const void *src, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  const uint8_t *s = (const uint8_t *)src;
  if (d < s) {
    for (size_t i = 0; i < n; ++i) d[i] = s[i];
  } else {
    for (size_t i = n; i > 0; --i) d[i - 1] = s[i - 1];
  }
  return dst;
}

void *memset(void *dst, int c, size_t n) {
  uint8_t *d = (uint8_t *)dst;
  for (size_t i = 0; i < n; ++i) d[i] = (uint8_t)c;
  return dst;
}

/* LLVM may turn a loop that looks for a zero byte (e.g. Basic's
   c_style_strlen) into a call of strlen. */
size_t strlen(const char *s) {
  size_t n = 0;
  while (s[n]) ++n;
  return n;
}

int memcmp(const void *a, const void *b, size_t n) {
  const uint8_t *x = (const uint8_t *)a, *y = (const uint8_t *)b;
  for (size_t i = 0; i < n; ++i)
    if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
  return 0;
}

#endif

#if defined(OJAY_PLATFORM_LINUX_FREESTANDING)

__attribute__((noreturn, used)) void __ojay_start_c(long *sp) {
  int argc = (int)sp[0];
  char **argv = (char **)(sp + 1);
  plat_exit(main(argc, argv));
}

__asm__(
    ".text\n"
    ".global _start\n"
    ".type _start,@function\n"
    "_start:\n"
    "  xor %rbp, %rbp\n"
    "  mov %rsp, %rdi\n"
    "  and $-16, %rsp\n"
    "  call __ojay_start_c\n"
    "  hlt\n");

#elif defined(OJAY_PLATFORM_CORTEXM)

/* The ARM EABI memory helpers LLVM uses on bare-metal targets. */
void __aeabi_memcpy(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memcpy4(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memcpy8(void *d, const void *s, size_t n) { memcpy(d, s, n); }
void __aeabi_memmove(void *d, const void *s, size_t n) { memmove(d, s, n); }
void __aeabi_memmove4(void *d, const void *s, size_t n) { memmove(d, s, n); }
void __aeabi_memmove8(void *d, const void *s, size_t n) { memmove(d, s, n); }
void __aeabi_memset(void *d, size_t n, int c) { memset(d, c, n); }
void __aeabi_memset4(void *d, size_t n, int c) { memset(d, c, n); }
void __aeabi_memset8(void *d, size_t n, int c) { memset(d, c, n); }
void __aeabi_memclr(void *d, size_t n) { memset(d, 0, n); }
void __aeabi_memclr4(void *d, size_t n) { memset(d, 0, n); }
void __aeabi_memclr8(void *d, size_t n) { memset(d, 0, n); }

/* Symbols provided by the board linker script. */
extern uint8_t __stack_top[];
extern uint8_t __data_load[], __data_start[], __data_end[];
extern uint8_t __bss_start[], __bss_end[];

#define SCB_CPACR (*(volatile uint32_t *)0xE000ED88u)
#define SCB_CFSR (*(volatile uint32_t *)0xE000ED28u)
#define SCB_HFSR (*(volatile uint32_t *)0xE000ED2Cu)

/* r0 / r1 hold whatever the core had at reset, or the values a debugger set
 * to announce that it handles semihosting (see "Console"). */
__attribute__((noreturn, used)) void Reset_Handler(uint32_t r0, uint32_t r1) {
  int announced = r0 == OJAY_DEBUGGER_MAGIC && r1 == ~OJAY_DEBUGGER_MAGIC;
  /* Enable the FPU (CP10/CP11 full access) before any floating-point code. */
  SCB_CPACR |= 0xFu << 20;
  __asm__ volatile("dsb\n isb" ::: "memory");
  if ((uintptr_t)__data_start != (uintptr_t)__data_load) {
    for (uint8_t *d = __data_start, *s = __data_load; d < __data_end;) *d++ = *s++;
  }
  for (uint8_t *d = __bss_start; d < __bss_end;) *d++ = 0;
  debugger_announced = announced;
  /* Drive the console's TX line from the start when it will carry the output. */
  if (!semihost_available() && !__ojay_console_write && (uintptr_t)&__ojay_console_mcxn947 != 0)
    mcx_uart_init();
  plat_exit(main(0, 0));
}

static void fault(const char *what) {
  Out o;
  out_init_fd(&o, 2);
  out_cstr(&o, "\n*** ");
  out_cstr(&o, what);
  out_cstr(&o, " (CFSR=");
  out_hex(&o, SCB_CFSR);
  out_cstr(&o, " HFSR=");
  out_hex(&o, SCB_HFSR);
  out_cstr(&o, ")\n");
  out_flush(&o);
  plat_exit(134);
}

__attribute__((noreturn)) void NMI_Handler(void) { fault("NMI"); for (;;) {} }
__attribute__((noreturn)) void HardFault_Handler(void) { fault("HardFault"); for (;;) {} }
__attribute__((noreturn)) void MemManage_Handler(void) { fault("MemManage fault"); for (;;) {} }
__attribute__((noreturn)) void BusFault_Handler(void) { fault("BusFault"); for (;;) {} }
__attribute__((noreturn)) void UsageFault_Handler(void) { fault("UsageFault"); for (;;) {} }
__attribute__((noreturn)) void SecureFault_Handler(void) { fault("SecureFault"); for (;;) {} }
__attribute__((noreturn)) void Default_Handler(void) { fault("unexpected interrupt"); for (;;) {} }

typedef void (*Vector)(void);

/* Core exception vectors. Device interrupts are never enabled by the
 * runtime, so the device-specific part of the table is omitted. */
__attribute__((section(".vectors"), used)) const Vector __ojay_vectors[16] = {
    (Vector)(uintptr_t)__stack_top,
    (Vector)Reset_Handler,
    (Vector)NMI_Handler,
    (Vector)HardFault_Handler,
    (Vector)MemManage_Handler,
    (Vector)BusFault_Handler,
    (Vector)UsageFault_Handler,
    (Vector)SecureFault_Handler,
    0, 0, 0,                 /* reserved */
    (Vector)Default_Handler, /* SVCall */
    (Vector)Default_Handler, /* DebugMonitor */
    0,                       /* reserved */
    (Vector)Default_Handler, /* PendSV */
    (Vector)Default_Handler, /* SysTick */
};

#elif defined(OJAY_PLATFORM_ESP32C6)

extern uint8_t __data_load[], __data_start[], __data_end[];
extern uint8_t __bss_start[], __bss_end[];

/* Application descriptor (ESP-IDF's esp_app_desc_t, 256 bytes). The ESP-IDF
 * second-stage bootloader refuses to boot an application image whose first
 * segment does not start with it; the linker script places it first. Zero
 * eFuse revision limits mean "no limit"; an MMU page size of 0 means the
 * default. */
typedef struct {
  uint32_t magic_word;
  uint32_t secure_version;
  uint32_t reserv1[2];
  char version[32];
  char project_name[32];
  char time[16];
  char date[16];
  char idf_ver[32];
  uint8_t app_elf_sha256[32];
  uint16_t min_efuse_blk_rev_full;
  uint16_t max_efuse_blk_rev_full;
  uint8_t mmu_page_size;
  uint8_t spi_flash_mode;
  uint8_t reserv3[2];
  uint32_t reserv2[18];
} EspAppDesc;
_Static_assert(sizeof(EspAppDesc) == 256, "esp_app_desc_t must be 256 bytes");

__attribute__((section(".ojay.image_header"), used)) const EspAppDesc __ojay_esp_app_desc = {
    .magic_word = 0xABCD5432u,
    .version = "1",
    .project_name = "ojay",
    .idf_ver = "ojay runtime",
};

/* Espressif's newlib reaches its per-thread state (errno, used by libm)
 * through __getreent(); a single-threaded program needs only one. errno is
 * the first member, and the area is generously sized. */
static uint64_t g_reent[128];
void *__getreent(void) { return g_reent; }

#define REG(addr) (*(volatile uint32_t *)(addr))
#define WDT_WKEY 0x50D83AA1u

/* The ROM and the second-stage bootloader leave watchdogs running; a bare
 * program has to stop them (or it is reset after a few seconds). */
static void disable_watchdogs(void) {
  static const uint32_t timg[2] = {0x60008000u, 0x60009000u};
  for (int i = 0; i < 2; ++i) {
    REG(timg[i] + 0x64) = WDT_WKEY;   /* TIMG_WDTWPROTECT */
    REG(timg[i] + 0x48) = 1u << 22;   /* TIMG_WDTCONFIG0: disabled, WDT_CONF_UPDATE_EN */
    REG(timg[i] + 0x64) = 0;
  }
  REG(0x600B1C18u) = WDT_WKEY;        /* LP_WDT_WPROTECT */
  REG(0x600B1C00u) = 0;               /* LP_WDT_CONFIG0: RTC watchdog off */
  REG(0x600B1C18u) = 0;
  REG(0x600B1C20u) = WDT_WKEY;        /* LP_WDT_SWD_WPROTECT */
  REG(0x600B1C1Cu) |= 1u << 18;       /* LP_WDT_SWD_CONFIG: super watchdog auto-feed */
  REG(0x600B1C20u) = 0;
}

__attribute__((noreturn, used)) void __ojay_reset(void) {
  disable_watchdogs();
  if ((uintptr_t)__data_start != (uintptr_t)__data_load) {
    for (uint8_t *d = __data_start, *s = __data_load; d < __data_end;) *d++ = *s++;
  }
  for (uint8_t *d = __bss_start; d < __bss_end;) *d++ = 0;
  plat_exit(main(0, 0));
}

static uint32_t read_csr_mcause(void) { uint32_t v; __asm__ volatile("csrr %0, mcause" : "=r"(v)); return v; }
static uint32_t read_csr_mepc(void) { uint32_t v; __asm__ volatile("csrr %0, mepc" : "=r"(v)); return v; }
static uint32_t read_csr_mtval(void) { uint32_t v; __asm__ volatile("csrr %0, mtval" : "=r"(v)); return v; }

__attribute__((noreturn, used)) void __ojay_fault(void) {
  uint32_t cause = read_csr_mcause(), epc = read_csr_mepc(), tval = read_csr_mtval();
  Out o;
  out_init_fd(&o, 2);
  out_cstr(&o, "\n*** exception (mcause=");
  out_hex(&o, cause);
  out_cstr(&o, " mepc=");
  out_hex(&o, epc);
  out_cstr(&o, " mtval=");
  out_hex(&o, tval);
  out_cstr(&o, ")\n");
  out_flush(&o);
  plat_exit(134);
}

/* Entry point (from a debugger, or from the ESP-IDF second-stage bootloader).
 * The trap vector is used in vectored mode; interrupts stay disabled, so only
 * its base (exceptions) is ever entered. A breakpoint exception there means a
 * semihosting call was made with no debugger attached: it is skipped and
 * console output switches to the USB Serial/JTAG port. */
__asm__(
    ".section .text._start, \"ax\", @progbits\n"
    ".global _start\n"
    ".type _start, @function\n"
    "_start:\n"
    "  csrci mstatus, 8\n"
    "  la sp, __stack_top\n"
    "  la t0, __ojay_trap_vector\n"
    "  ori t0, t0, 1\n"
    "  csrw mtvec, t0\n"
    "  j __ojay_reset\n"
    "\n"
    ".section .text.__ojay_trap_vector, \"ax\", @progbits\n"
    ".balign 256\n"
    ".global __ojay_trap_vector\n"
    "__ojay_trap_vector:\n"
    "  csrw mscratch, t0\n"
    "  csrr t0, mcause\n"
    "  addi t0, t0, -3\n"            /* breakpoint? */
    "  bnez t0, 1f\n"
    "  csrr t0, mepc\n"
    "  addi t0, t0, 4\n"             /* skip the (uncompressed) ebreak */
    "  csrw mepc, t0\n"
    "  la t0, __ojay_semihost_dead\n"
    "  sw sp, 0(t0)\n"               /* any non-zero value */
    "  csrr t0, mscratch\n"
    "  mret\n"
    "1:\n"
    "  j __ojay_fault\n");

#endif
