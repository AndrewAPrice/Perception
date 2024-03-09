#pragma once

#include "../../third_party/Libraries/elf/public/elf.h"

typedef long clock_t;
typedef int pid_t;
typedef unsigned uid_t;

// Third party struct from ../../third_party/Libraries/musl/include/signal.h
union sigval {
  int sival_int;
  void *sival_ptr;
};

typedef struct {
  int si_signo, si_errno, si_code;
  union {
    char __pad[128 - 2 * sizeof(int) - sizeof(long)];
    struct {
      union {
        struct {
          pid_t si_pid;
          uid_t si_uid;
        } __piduid;
        struct {
          int si_timerid;
          int si_overrun;
        } __timer;
      } __first;
      union {
        union sigval si_value;
        struct {
          int si_status;
          clock_t si_utime, si_stime;
        } __sigchld;
      } __second;
    } __si_common;
    struct {
      void *si_addr;
      short si_addr_lsb;
      union {
        struct {
          void *si_lower;
          void *si_upper;
        } __addr_bnd;
        unsigned si_pkey;
      } __first;
    } __sigfault;
    struct {
      long si_band;
      int si_fd;
    } __sigpoll;
    struct {
      void *si_call_addr;
      int si_syscall;
      unsigned si_arch;
    } __sigsys;
  } __si_fields;
} siginfo_t;

struct elf_siginfo {
  int si_signo;
  int si_code;
  int si_errno;
};

typedef struct user_fpregs_struct {
  uint16 cwd, swd, ftw, fop;
  uint64 rip, rdp;
  uint32 mxcsr, mxcr_mask;
  uint32 st_space[32], xmm_space[64], padding[24];
} elf_fpregset_t;

struct user_regs_struct {
  unsigned long r15, r14, r13, r12, rbp, rbx, r11, r10, r9, r8;
  unsigned long rax, rcx, rdx, rsi, rdi, orig_rax, rip;
  unsigned long cs, eflags, rsp, ss, fs_base, gs_base, ds, es, fs, gs;
};
#define ELF_NGREG 27
typedef unsigned long long elf_greg_t, elf_gregset_t[ELF_NGREG];

struct elf_prstatus {
  struct elf_siginfo pr_info;
  short int pr_cursig;
  unsigned long int pr_sigpend;
  unsigned long int pr_sighold;
  int pr_pid;
  int pr_ppid;
  int pr_pgrp;
  int pr_sid;
  struct {
    long tv_sec, tv_usec;
  } pr_utime, pr_stime, pr_cutime, pr_cstime;
  elf_gregset_t pr_reg;
  int pr_fpvalid;
};

#define ELF_PRARGSZ 80

struct elf_prpsinfo {
	char pr_state;
	char pr_sname;
	char pr_zomb;
	char pr_nice;
	unsigned long int pr_flag;
	unsigned int pr_uid;
	unsigned int pr_gid;
	int pr_pid, pr_ppid, pr_pgrp, pr_sid;
	char pr_fname[16];
	char pr_psargs[ELF_PRARGSZ];
};

// Third party from lldb/source/Plugins/Process/Utility/RegisterContext_x86.h

# define LLVM_PACKED(d) d __attribute__((packed))
# define LLVM_PACKED_START _Pragma("pack(push, 1)")
# define LLVM_PACKED_END   _Pragma("pack(pop)")
#define LLVM_MARK_AS_BITMASK_ENUM(LargestValue)                                \
  LLVM_BITMASK_LARGEST_ENUMERATOR = LargestValue

// Generic floating-point registers

LLVM_PACKED_START
struct MMSRegComp {
  uint64 mantissa;
  uint16 sign_exp;
};

struct MMSReg {
  union {
    uint8 bytes[10];
    MMSRegComp comp;
  };
  uint8 pad[6];
};
LLVM_PACKED_END

static_assert(sizeof(MMSRegComp) == 10, "MMSRegComp is not 10 bytes of size");
static_assert(sizeof(MMSReg) == 16, "MMSReg is not 16 bytes of size");

struct XMMReg {
  uint8 bytes[16];  // 128-bits for each XMM register
};

// i387_fxsave_struct
struct FXSAVE {
  uint16 fctrl;  // FPU Control Word (fcw)
  uint16 fstat;  // FPU Status Word (fsw)
  uint16 ftag;   // FPU Tag Word (ftw)
  uint16 fop;    // Last Instruction Opcode (fop)
  union {
    struct {
      uint64 fip;  // Instruction Pointer
      uint64 fdp;  // Data Pointer
    } x86_64;
    struct {
      uint32 fioff;  // FPU IP Offset (fip)
      uint32 fiseg;  // FPU IP Selector (fcs)
      uint32 fooff;  // FPU Operand Pointer Offset (foo)
      uint32 foseg;  // FPU Operand Pointer Selector (fos)
    } i386_;  // Added _ in the end to avoid error with gcc defining i386 in
              // some cases
  } ptr;
  uint32 mxcsr;      // MXCSR Register State
  uint32 mxcsrmask;  // MXCSR Mask
  MMSReg stmm[8];      // 8*16 bytes for each FP-reg = 128 bytes
  XMMReg xmm[16];      // 16*16 bytes for each XMM-reg = 256 bytes
  uint8 padding1[48];
  uint64 xcr0;
  uint8 padding2[40];
};

// Extended floating-point registers

struct YMMHReg {
  uint8 bytes[16];  // 16 * 8 bits for the high bytes of each YMM register
};

struct YMMReg {
  uint8 bytes[32];  // 16 * 16 bits for each YMM register
};

struct YMM {
  YMMReg ymm[16];  // assembled from ymmh and xmm registers
};

struct MPXReg {
  uint8 bytes[16];  // MPX 128 bit bound registers
};

struct MPXCsr {
  uint8 bytes[8];  // MPX 64 bit bndcfgu and bndstatus registers (collectively
                     // BNDCSR state)
};

struct MPX {
  MPXReg mpxr[4];
  MPXCsr mpxc[2];
};

LLVM_PACKED_START
struct XSAVE_HDR {
  enum class XFeature : uint64 {
    FP = 1,
    SSE = FP << 1,
    YMM = SSE << 1,
    BNDREGS = YMM << 1,
    BNDCSR = BNDREGS << 1,
    OPMASK = BNDCSR << 1,
    ZMM_Hi256 = OPMASK << 1,
    Hi16_ZMM = ZMM_Hi256 << 1,
    PT = Hi16_ZMM << 1,
    PKRU = PT << 1,
    LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue*/ PKRU)
  };

  XFeature xstate_bv;  // OS enabled xstate mask to determine the extended
                       // states supported by the processor
  XFeature xcomp_bv;  // Mask to indicate the format of the XSAVE area and of
                      // the XRSTOR instruction
  uint64 reserved1[1];
  uint64 reserved2[5];
};
static_assert(sizeof(XSAVE_HDR) == 64, "XSAVE_HDR layout incorrect");
LLVM_PACKED_END

// x86 extensions to FXSAVE (i.e. for AVX and MPX processors)
LLVM_PACKED_START
struct XSAVE {
  FXSAVE i387;      // floating point registers typical in i387_fxsave_struct
  XSAVE_HDR header; // The xsave_hdr_struct can be used to determine if the
                    // following extensions are usable
  YMMHReg ymmh[16]; // High 16 bytes of each of 16 YMM registers (the low bytes
                    // are in FXSAVE.xmm for compatibility with SSE)
  uint64 reserved3[16];
  MPXReg mpxr[4];   // MPX BNDREG state, containing 128-bit bound registers
  MPXCsr mpxc[2];   // MPX BNDCSR state, containing 64-bit BNDCFGU and
                    // BNDSTATUS registers
};
LLVM_PACKED_END
