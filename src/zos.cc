///////////////////////////////////////////////////////////////////////////////
// Licensed Materials - Property of IBM
// ZOSLIB
// (C) Copyright IBM Corp. 2020. All Rights Reserved.
// US Government Users Restricted Rights - Use, duplication
// or disclosure restricted by GSA ADP Schedule Contract with IBM Corp.
///////////////////////////////////////////////////////////////////////////////

#define _AE_BIMODAL 1
#undef _ENHANCED_ASCII_EXT
#define _ENHANCED_ASCII_EXT 0xFFFFFFFF
#define _XOPEN_SOURCE 600
#define _OPEN_SYS_FILE_EXT 1
#define _OPEN_MSGQ_EXT 1
#define __ZOS_CC
#include "edcwccwi.h"
#include "zos-base.h"

#include <_Ccsid.h>
#include <_Nascii.h>
#include <__le_api.h>
#include <assert.h>
#include <builtins.h>
#include <ctest.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <iconv.h>
#include <libgen.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/msg.h>
#include <sys/ps.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <exception>
#include <mutex>
#include <sstream>
#include <unordered_map>
#include <vector>

#pragma linkage(_gtca, builtin)
#pragma linkage(_gdsa, builtin)

#ifndef dsa
#define dsa() ((unsigned long *)_gdsa())
#endif

#define MIN(a, b) ((a) < (b) ? (a) : (b))

static int __debug_mode = 0;
static char **__argv = nullptr;
static int __argc = -1;
static pthread_t _timer_tid;
static int *__main_thread_stack_top_address = 0;
static bool __is_backtrace_on_abort = true;

#if defined(BUILD_VERSION)
const char *__zoslib_version = BUILD_VERSION;
#else
#define XSTR(a) STRINGIFY(a)
#define STRINGIFY(a) #a
#define DEFAULT_BUILD_STRING                                                   \
  "v" XSTR(MAJOR_VERSION) "." XSTR(MINOR_VERSION) "." XSTR(PATCH_VERSION)
const char *__zoslib_version = DEFAULT_BUILD_STRING;
#endif

extern "C" void __set_ccsid_guess_buf_size(int nbytes);
static int shmid_value(void);

#ifndef max
#define max(a, b) (((a) > (b)) ? (a) : (b))
#endif

// Return the smallest multiple of m which is >= x.
static inline size_t __round_up(size_t x, size_t m) {
  assert(m > 0);
  size_t mod = x % m;
  if (mod == 0) return x;
  if (x < m) return m;
  return x + m - mod;
}

extern char **environ; // this would be the ebcdic one

extern "C" char **__get_environ_np(void) {
  static char **__environ = 0;
  static long __environ_size = 0;
  char **start = environ;
  int cnt = 0;
  int size = 0;
  int len = 0;
  int arysize = 0;
  while (*start) {
    size += (strlen(*start) + 1);
    ++start;
    ++cnt;
  }
  arysize = (cnt + 1) * sizeof(void *);
  size += arysize;
  if (__environ) {
    if (__environ_size < size) {
      free(__environ);
      __environ_size = size;
      __environ = (char **)malloc(__environ_size);
    }
  } else {
    __environ_size = size;
    __environ = (char **)malloc(__environ_size);
  }
  char *p = (char *)__environ;
  p += arysize;
  int i;
  start = environ;
  for (i = 0; i < cnt; ++i) {
    __environ[i] = p;
    len = strlen(*start) + 1;
    __convert_one_to_one(__ibm1047_iso88591, p, len, *start);
    p += len;
    ++start;
  }
  __environ[i] = 0;
  return __environ;
}

unsigned int __zsync_val_compare_and_swap32(volatile unsigned int *__p,
                                            unsigned int __compVal,
                                            unsigned int __exchVal) {
  unsigned int initv;
  __asm(" cs  %1,%3,%2 \n "
        " lgr %0,%1 \n"
        : "=r"(initv), "+r"(__compVal), "+m"(*__p)
        : "r"(__exchVal)
        :);
  return initv;
}

int __setenv_a(const char *, const char *, int);
#pragma map(__setenv_a, "\174\174A00188")
extern "C" void __xfer_env(void) {
  char **start = __get_environ_np();
  int i;
  int len;
  char *str;
  char *a_str;
  while (*start) {
    str = *start;
    len = strlen(str);
    a_str = (char *)alloca(len + 1);
    memcpy(a_str, str, len);
    a_str[len] = 0;
    for (i = 0; i < len; ++i) {
      if (a_str[i] == u'=') {
        a_str[i] = 0;
        break;
      }
    }
    if (i < len) {
      int rc = __setenv_a(a_str, a_str + i + 1, 1);
      if (rc != 0) {
        __auto_ascii _a;
        __printf_a("__setenv_a %s=%s failed rc=%d\n", a_str, a_str + i + 1, rc);
      }
    }
    ++start;
  }
}

static void ledump(const char *title) {
  __auto_ascii _a;
  __cdump_a((char *)title);
}

extern "C" int gettid() { return (int)(pthread_self().__ & 0x7fffffff); }

static void init_tf_parms_t(__tf_parms_t *parm, char *pu_name_buf, size_t len1,
                            char *entry_name_buf, size_t len2,
                            char *stmt_id_buf, size_t len3) {
  _FEEDBACK fc;
  parm->__tf_pu_name.__tf_buff = pu_name_buf;
  parm->__tf_pu_name.__tf_bufflen = len1;
  parm->__tf_entry_name.__tf_buff = entry_name_buf;
  parm->__tf_entry_name.__tf_bufflen = len2;
  parm->__tf_statement_id.__tf_buff = stmt_id_buf;
  parm->__tf_statement_id.__tf_bufflen = len3;
  parm->__tf_dsa_addr = 0;
  parm->__tf_caa_addr = 0;
  parm->__tf_call_instruction = 0;
  int skip = 2;
  while (skip > 0 && !parm->__tf_is_main) {
    ____le_traceback_a(__TRACEBACK_FIELDS, parm, &fc);
    parm->__tf_dsa_addr = parm->__tf_caller_dsa_addr;
    parm->__tf_call_instruction = parm->__tf_caller_call_instruction;
    --skip;
  }
}

static int backtrace_w(void **buffer, int size);

extern "C" int backtrace(void **buffer, int size) {
  int mode;
  int result;
  mode = __ae_thread_swapmode(__AE_ASCII_MODE);
  result = backtrace_w(buffer, size);
  __ae_thread_swapmode(mode);
  return result;
}

int backtrace_w(void **input_buffer, int size) {
  void **buffer = input_buffer;
  __tf_parms_t tbck_parms;
  _FEEDBACK fc;
  int rc = 0;
  init_tf_parms_t(&tbck_parms, 0, 0, 0, 0, 0, 0);
  while (size > 0 && !tbck_parms.__tf_is_main) {
    ____le_traceback_a(__TRACEBACK_FIELDS, &tbck_parms, &fc);
    if (fc.tok_sev >= 2) {
      dprintf(2, "____le_traceback_a() service failed\n");
      return 0;
    }
    *buffer = tbck_parms.__tf_dsa_addr;
    tbck_parms.__tf_dsa_addr = tbck_parms.__tf_caller_dsa_addr;
    tbck_parms.__tf_call_instruction = tbck_parms.__tf_caller_call_instruction;
    --size;
    if (rc > 0) {
      if (input_buffer[rc - 1] >= input_buffer[rc]) {
        // xplink stack address is not increasing as we go up, could be
        // stack corruption
        input_buffer[rc] = 0;
        --rc;
        return rc;
      }
    }
    ++buffer;
    ++rc;
  }
  return rc;
}

static void backtrace_symbols_w(void *const *buffer, int size, int fd,
                                char ***return_string);

extern "C" char **backtrace_symbols(void *const *buffer, int size) {
  int mode;
  char **result;
  mode = __ae_thread_swapmode(__AE_ASCII_MODE);
  backtrace_symbols_w(buffer, size, -1, &result);
  __ae_thread_swapmode(mode);
  return result;
}

static void rbracket_entry_name(char *entry_name, int size) {
  // if entry_name has a different number of ( and ), then add a ) (r=right)
  // at the end; used for the backtrace
  if (!strchr(entry_name, '('))
    return;
  int len = strlen(entry_name);
  if (len < 4)
    return;
  int lcnt = 0, rcnt = 0;
  for (int i = 0; i < len; ++i) {
    if (entry_name[i] == '(')
      ++lcnt;
    else if (entry_name[i] == ')')
      ++rcnt;
  }
  if (lcnt == rcnt)
    return;
  if (entry_name[len - 1] == ')')
    return;
  if (!strcmp(entry_name + len - 3, "...")) {
    assert(len <= size);
    if (len == size)
      entry_name[len - 1] = ')';
    else {
      entry_name[len++] = ')';
      entry_name[len] = 0;
    }
  }
}

void backtrace_symbols_w(void *const *buffer, int size, int fd,
                         char ***return_string) {
  int sz;
  char *return_buff = 0;
  char **table;
  char *stringpool;
  char *buff_end;
  __tf_parms_t tbck_parms;
  char pu_name[256];
  char entry_name[256];
  char stmt_id[256];
  char *return_addr;
  _FEEDBACK fc;
  int i;
  int cnt;
  int inst;
  void *caller_dsa = 0;
  void *caller_inst = 0;

  sz = ((size + 1) * 300); // estimate
  if (fd == -1) {
    return_buff = (char *)malloc(sz);
  }
  while (return_buff != 0 || (return_buff == 0 && fd != -1)) {
    if (fd == -1) {
      table = (char **)return_buff;
      stringpool = return_buff + ((size + 1) * sizeof(void *));
      buff_end = return_buff + sz;
    }
    init_tf_parms_t(&tbck_parms, pu_name, 256, entry_name, 256, stmt_id, 256);
    for (i = 0; i < size; ++i) {
      if (i > 0) {
        tbck_parms.__tf_dsa_addr = buffer[i];
      }
      if (tbck_parms.__tf_dsa_addr == caller_dsa) {
        tbck_parms.__tf_call_instruction = caller_inst;
      } else {
        tbck_parms.__tf_call_instruction = 0;
      }
      ____le_traceback_a(__TRACEBACK_FIELDS, &tbck_parms, &fc);
      if (fc.tok_sev >= 2) {
        dprintf(2, "____le_traceback_a() service failed\n");
        free(return_buff);
        *return_string = 0;
        return;
      }
      caller_dsa = tbck_parms.__tf_caller_dsa_addr;
      caller_inst = tbck_parms.__tf_caller_call_instruction;
      inst = *(char *)(tbck_parms.__tf_caller_call_instruction);
      if (inst == 0xa7) {
        // BRAS
        return_addr = 6 + (char *)tbck_parms.__tf_caller_call_instruction;
      } else {
        // BASR
        return_addr = 4 + (char *)tbck_parms.__tf_caller_call_instruction;
      }
      rbracket_entry_name(entry_name, sizeof(entry_name));
      if (tbck_parms.__tf_call_instruction) {
        if (pu_name[0]) {
          if (fd == -1)
            cnt = __snprintf_a(stringpool, buff_end - stringpool,
                               " %d: 0x%p %s+0x%lx [%s:%s]", i + 1, return_addr,
                               entry_name,
                               (char *)tbck_parms.__tf_call_instruction -
                                   (char *)tbck_parms.__tf_entry_addr,
                               pu_name, stmt_id);
          else
            dprintf(fd, " %d: 0x%p %s+0x%lx [%s:%s]\n", i + 1, return_addr,
                    entry_name,
                    (char *)tbck_parms.__tf_call_instruction -
                        (char *)tbck_parms.__tf_entry_addr,
                    pu_name, stmt_id);

        } else {
          if (fd == -1)
            cnt = __snprintf_a(stringpool, buff_end - stringpool,
                               " %d: 0x%p %s+0x%lx", i + 1, return_addr,
                               entry_name,
                               (char *)tbck_parms.__tf_call_instruction -
                                   (char *)tbck_parms.__tf_entry_addr);
          else
            dprintf(fd, " %d: 0x%p %s+0x%lx\n", i + 1, return_addr, entry_name,
                    (char *)tbck_parms.__tf_call_instruction -
                        (char *)tbck_parms.__tf_entry_addr);
        }
      } else {
        if (pu_name[0]) {
          if (fd == -1)
            cnt = __snprintf_a(stringpool, buff_end - stringpool,
                               " %d: 0x%p %s [%s:%s]", i + 1, return_addr,
                               entry_name, pu_name, stmt_id);
          else
            dprintf(fd, " %d 0x%p %s [%s:%s]\n", i + 1, return_addr, entry_name,
                    pu_name, stmt_id);
        } else {
          if (fd == -1)
            cnt = __snprintf_a(stringpool, buff_end - stringpool,
                               " %d: 0x%p %s", i + 1, return_addr, entry_name);
          else
            dprintf(fd, " %d: 0x%p %s\n", i + 1, return_addr, entry_name);
        }
      }
      if (fd == -1) {
        if (cnt < 0 || cnt >= (buff_end - stringpool)) {
          // out of space
          break;
        }
        table[i] = stringpool;
        stringpool += (cnt + 1);
      }
    }
    if (fd == -1) {
      if (i == size) {
        // return &table[0];
        table[i] = 0;
        *return_string = &table[0];
        return;
      }
      free(return_buff);
      sz += (size * 300);
      return_buff = (char *)malloc(sz);
    } else
      return;
  }
}

extern "C" void backtrace_symbols_fd(void *const *buffer, int size, int fd) {
  int mode;
  mode = __ae_thread_swapmode(__AE_ASCII_MODE);
  backtrace_symbols_w(buffer, size, fd, 0);
  __ae_thread_swapmode(mode);
}

extern "C" void __display_backtrace(int fd) {
  void *buffer[4096];
  int nptrs = backtrace(buffer, 4096);
  backtrace_symbols_fd(buffer, nptrs, fd);
}

void __abend(int comp_code, unsigned reason_code, int flat_byte, void *plist) {
  unsigned long r15 = reason_code;
  unsigned long r1;
  void *__ptr32 r0 = plist;
  if (flat_byte == -1)
    flat_byte = 0x84;
  r1 = (flat_byte << 24) + (0x00ffffff & comp_code);
  __asm(" SVC 13\n" : : "NR:r0"(r0), "NR:r1"(r1), "NR:r15"(r15) :);
}

static const unsigned char ascii_to_lower[256] __attribute__((aligned(8))) = {
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b,
    0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23,
    0x24, 0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
    0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b,
    0x3c, 0x3d, 0x3e, 0x3f, 0x40, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
    0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73,
    0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
    0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x6b,
    0x6c, 0x6d, 0x6e, 0x6f, 0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
    0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f, 0x80, 0x81, 0x82, 0x83,
    0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
    0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0x9b,
    0x9c, 0x9d, 0x9e, 0x9f, 0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb0, 0xb1, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
    0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xcb,
    0xcc, 0xcd, 0xce, 0xcf, 0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
    0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe0, 0xe1, 0xe2, 0xe3,
    0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
    0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0xfb,
    0xfc, 0xfd, 0xfe, 0xff};

int strcasecmp_ignorecp(const char *a, const char *b) {
  int len_a = strlen(a);
  int len_b = strlen(b);

  if (len_a != len_b)
    return len_a - len_b;
  if (!memcmp(a, b, len_a))
    return 0;
  char *a_new = (char *)_convert_e2a(alloca(len_a + 1), a, len_a + 1);
  char *b_new = (char *)_convert_e2a(alloca(len_b + 1), b, len_b + 1);
  __convert_one_to_one(ascii_to_lower, a_new, len_a, a_new);
  __convert_one_to_one(ascii_to_lower, b_new, len_b, a_new);
  return strcmp(a_new, b_new);
}

int strncasecmp_ignorecp(const char *a, const char *b, size_t n) {
  int ccsid_a, ccsid_b;
  int am_a, am_b;
  unsigned len_a = strlen_ae((unsigned char *)a, &ccsid_a, n, &am_a);
  unsigned len_b = strlen_ae((unsigned char *)b, &ccsid_b, n, &am_b);
  char *a_new;
  char *b_new;
  if (len_a != len_b)
    return len_a - len_b;

  if (ccsid_a != 819) {
    a_new = (char *)__convert_one_to_one(__ibm1047_iso88591, alloca(len_a + 1),
                                         len_a, a);
    a_new[len_a] = 0;
    a_new = (char *)__convert_one_to_one(ascii_to_lower, a_new, len_a, a_new);
  } else {
    a_new = (char *)__convert_one_to_one(ascii_to_lower, alloca(len_a + 1),
                                         len_a, a);
    a_new[len_a] = 0;
  }

  if (ccsid_b != 819) {
    b_new = (char *)__convert_one_to_one(__ibm1047_iso88591, alloca(len_b + 1),
                                         len_b, b);
    b_new[len_b] = 0;
    b_new = (char *)__convert_one_to_one(ascii_to_lower, b_new, len_b, b_new);
  } else {
    b_new = (char *)__convert_one_to_one(ascii_to_lower, alloca(len_b + 1),
                                         len_b, b);
    b_new[len_b] = 0;
  }

  return strcmp(a_new, b_new);
}

int get_ipcs_overview(IPCQPROC *info) {
  return __getipc(0, info, sizeof(IPCQPROC), IPCQOVER);
}

void __cleanupipc(int others) {
  IPCQPROC buf;
  int rc;
  int uid = getuid();
  int pid = getpid();
  int stop = -1;

  // Get the number of message queues and shared memory to
  // prevent infinite loop
  if (get_ipcs_overview(&buf) == -1)
    return;

  int count_message_queue = buf.overview.ipcqomsgprivate;
  int count_shared_memory = buf.overview.ipcqoshmkeyed;

  rc = __getipc(0, &buf, sizeof(IPCQPROC), IPCQMSG);

  // Since __getipc has undocumented behaviour, add several checks
  // to prevent infinite looping
  // Experimentation shows that return val of 0 denotes that there
  // are no further IPCS records
  // Check both rc != 0 (undocumented) and rc != -1 (documented)
  // and check if count <= number of message queues
  int count = 0;
  while (rc != 0 && rc != -1 && stop != buf.msg.ipcqmid &&
         count <= count_message_queue) {
    if (stop == -1)
      stop = buf.msg.ipcqmid;
    if (buf.msg.ipcqpcp.uid == uid) {
      if (buf.msg.ipcqkey == 0) {
        if (buf.msg.ipcqlrpid == pid) {
          msgctl(buf.msg.ipcqmid, IPC_RMID, 0);
        } else if (others && kill(buf.msg.ipcqlrpid, 0) == -1 &&
                   kill(buf.msg.ipcqlspid, 0) == -1) {
          msgctl(buf.msg.ipcqmid, IPC_RMID, 0);
        }
      }
    }
    rc = __getipc(rc, &buf, sizeof(buf), IPCQMSG);
  }
  if (others) {
    stop = -1;
    // Since __getipc has undocumented behaviour, add several checks
    // to prevent infinite looping
    // Experimentation shows that return val of 0 denotes that there
    // are no further IPCS records
    // Check both rc != 0 (undocumented) and rc != -1 (documented)
    // and check if count <= number of shared memory
    rc = __getipc(0, &buf, sizeof(buf), IPCQSHM);
    count = 0;
    while (rc != 0 && rc != -1 && stop != buf.shm.ipcqmid &&
           count <= count_shared_memory) {
      if (stop == -1) {
        stop = buf.shm.ipcqmid;
      }
      if (buf.shm.ipcqpcp.uid == uid) {
        if (buf.shm.ipcqcpid == pid) {
          shmctl(buf.shm.ipcqmid, IPC_RMID, 0);
        } else if (kill(buf.shm.ipcqcpid, 0) == -1) {
          shmctl(buf.shm.ipcqmid, IPC_RMID, 0);
        }
      }
      rc = __getipc(rc, &buf, sizeof(buf), IPCQSHM);
    }
  }
}

typedef struct timer_parm {
  int secs;
  pthread_t tid;
} timer_parm_t;

unsigned long __clock(void) {
  unsigned long long value;
  __stckf(&value);
  return ((value / 512UL) * 125UL) - 2208988800000000000UL;
}

static void *_timer(void *parm) {
  timer_parm_t *tp = (timer_parm_t *)parm;
  unsigned long t0 = __clock();
  unsigned long t1 = t0;
  while ((t1 - t0) < ((tp->secs) * 1000000000UL)) {
    sleep(tp->secs);
    t1 = __clock();
  }
  if (__debug_mode) {
    dprintf(2, "Sent abort: __NODERUNTIMELIMIT was set to %d\n", tp->secs);
    raise(SIGABRT);
  }
  return 0;
}

extern void __settimelimit(int secs) {
  pthread_attr_t attr;
  int rc;
  timer_parm_t *tp = (timer_parm_t *)malloc(sizeof(timer_parm_t));
  tp->secs = secs;
  tp->tid = pthread_self();
  rc = pthread_attr_init(&attr);
  if (rc) {
    perror("timer:pthread_attr_init");
    return;
  }
  rc = pthread_create(&_timer_tid, &attr, _timer, tp);
  if (rc) {
    perror("timer:pthread_create");
    return;
  }
  pthread_attr_destroy(&attr);
}
extern "C" void __setdebug(int v) { __debug_mode = v; }
extern "C" int __indebug(void) { return __debug_mode; }

extern "C" void *__dlcb_next(void *last) {
  if (last == 0) {
    return ((char *****__ptr32 *)1208)[0][11][1][113][193];
  }
  return ((char **)last)[0];
}
extern "C" int __dlcb_entry_name(char *buf, int size, void *dlcb) {
  unsigned short n;
  char *name;
  if (dlcb == 0)
    return 0;
  n = ((unsigned short *)dlcb)[44];
  name = ((char **)dlcb)[12];
  return __snprintf_a(
      buf, size, "%-.*s", n,
      __convert_one_to_one(__ibm1047_iso88591, alloca(n + 1), n, name));
}
extern "C" void *__dlcb_entry_addr(void *dlcb) {
  if (dlcb == 0)
    return 0;
  char *addr = ((char **)dlcb)[2];
  return addr;
}

extern "C" void abort(void) {
  if (__is_backtrace_on_abort) {
    __display_backtrace(STDERR_FILENO);
  }
  __zinit::getInstance()->__abort();
  exit(-1); // never reach here, suppress clang warning
}

extern "C" void __set_backtrace_on_abort(bool flag) {
  __is_backtrace_on_abort = flag;
}

int __cond_timed_wait(unsigned int secs, unsigned int nsecs,
                      unsigned int event_list, unsigned int *secs_rem,
                      unsigned int *nsecs_rem) {
  int rv, rc, rn;
  __bpx4ctw(&secs, &nsecs, &event_list, secs_rem, nsecs_rem, &rv, &rc, &rn);
  if (rv != 0)
    errno = rc;
  return rv;
}

// overriding LE's kill when linked statically
extern "C" int kill(int pid, int sig) {
  int rv, rc, rn;
  __bpx4kil(pid, sig, 0, &rv, &rc, &rn);
  if (rv != 0)
    errno = rc;
  return rv;
}

// overriding LE's fork when linked statically
extern "C" int __fork(void) {
  int cnt = __zinit::getInstance()->inc_forkcount();
  int max = __zinit::getInstance()->get_forkmax();
  if (cnt > max) {
    dprintf(2,
            "fork(): current count %d is greater than "
            "__NODEFORKMAX value %d, fork failed\n",
            cnt, max);
    errno = EPROCLIM;
    return -1;
  }
#if 0
  int rc, rn, pid;
  __bpx4frk(&pid, &rc, &rn);
  if (-1 == pid) {
    errno = rc;
  }
  return pid;
#else
  int pid = fork();
  if (pid == 0) {
    __zinit::getInstance()->forked(1);
  }
  return pid;
#endif
}

// Thread entry constants for __getthent():
#define PGTH_CURRENT 1
#define PGTHACOMMANDLONG 1

extern "C" int __getargcv(int *argc, char ***argv, pid_t pid) {
  // Gets the argv/argc using the thread entry information in the address space.
  // See https://www.ibm.com/docs/en/zos/2.4.0?
  //             topic=31-bpxypgth-map-getthent-inputoutput-structure
  // for the structs mapping below.
#pragma pack(1)
  struct {
    int pid;
    unsigned long thid;
    char accesspid;
    char accessthid;
    char asid[2];
    char loginname[8];
    char flag;
    char len;
  } input_data;

  union {
    struct {
      char gthb[4];
      int pid;
      unsigned long thid;
      char accesspid;
      char accessthid[3];
      int lenused;
      int offsetProcess;
      int offsetConTTY;
      int offsetPath;
      int offsetCommand;
      int offsetFileData;
      int offsetThread;
    } output_data;
    char buf[2048];
  } output_buf;

  struct output_cmd_type {
    char gthe[4];
    short int len;
    char cmd[2048];
  };
#pragma pack()

  int input_length;
  int output_length;
  void *input_address;
  void *output_address;
  struct output_cmd_type *output_cmd;
  int rv;
  int rc;
  int rsn;

  input_length = sizeof(input_data);
  output_length = sizeof(output_buf);
  output_address = &output_buf;
  input_address = &input_data;
  memset(&input_data, 0, sizeof(input_data));
  input_data.flag = PGTHACOMMANDLONG;
  input_data.pid = pid;
  input_data.accesspid = PGTH_CURRENT;

  __bpx4gth(&input_length, &input_address, &output_length, &output_address, &rv,
            &rc, &rsn);

  if (rv == -1) {
    errno = rc;
    return -1;
  }

  // Check first byte (PGTHBLIMITE): A = the section was completely filled in
  __e2a_l((char *)&output_buf.output_data.offsetCommand, 1);
  assert(((output_buf.output_data.offsetCommand >> 24) & 0xFF) == 'A');

  // Command offset is in the lowest 3 bytes (PGTHACOMMANDLONG):
  output_cmd =
      (struct output_cmd_type *)((char *)(&output_buf) +
                                 (output_buf.output_data.offsetCommand &
                                  0x00FFFFFF));

  if (output_cmd->len >= sizeof(output_cmd->cmd)) {
    errno = EBUFLEN;
    return -1;
  }

  __e2a_l(output_cmd->cmd, output_cmd->len);

  // allocate argv and fill it first with pointers to each arg's address
  // in the same block:
  int i, args_offset, base_args_offset, argi;
  const int ptr_size = sizeof(char *);
  char *argvbuf;

  for (i = 0, *argc = 0; i < output_cmd->len; i++) {
    if (!output_cmd->cmd[i]) {
      *argc += 1;
    }
  }

  // + 1 is for *argv[*argc] to store the pointer to nullptr
  args_offset = (*argc + 1) * ptr_size;
  argvbuf = (char *)malloc(args_offset + output_cmd->len);
  assert(argvbuf != nullptr);
  *argv = (char **)argvbuf;
  memcpy(argvbuf + args_offset, output_cmd->cmd, output_cmd->len);

  base_args_offset = args_offset;

  for (i = 0, argi = 0; i < output_cmd->len; i++) {
    if (!output_cmd->cmd[i]) {
      (*argv)[argi++] = (char *)argvbuf + args_offset;
      // each arg is null-terminated, hence the + 1:
      args_offset = base_args_offset + i + 1;
    }
  }
  (*argv)[argi] = nullptr;
  return 0;
}

extern "C" char **__getargv(void) {
  if (__argv == nullptr) {
    static std::mutex access_lock;
    std::lock_guard<std::mutex> lock(access_lock);
    if (__argv != nullptr)
      return __argv;
    char **tmpargv;
    if (__getargcv(&__argc, &tmpargv, getpid())) {
      perror("__getargcv");
      return nullptr;
    }
    __argv = tmpargv;
  }
  return __argv;
}

extern "C" char **__getargv_a(void) { return __getargv(); }

extern "C" int __getargc(void) {
  if (__getargv())
    return __argc;
  return -1;
}

extern "C" int __getexepath(char *path, int pathlen, pid_t pid) {
  int argc;
  char **argv = nullptr;
  char *stptr;

  if (pid == getpid())
    stptr = __getargv()[0]; // a static, so don't free
  else {
    if (int rc = __getargcv(&argc, &argv, pid))
      return rc;
    stptr = argv[0];
  }
  assert(stptr);
  int len = strlen(stptr);
  if (len >= pathlen) {
    if (argv)
      free(argv);
    errno = EBUFLEN;
    return -1;
  }
  memcpy(path, stptr, len);
  path[len] = 0;
  if (argv)
    free(argv);
  return 0;
}

struct IntHash {
  size_t operator()(const int &n) const { return n * 0x54edcfac64d7d667L; }
};

typedef unsigned long fd_attribute;

typedef std::unordered_map<int, fd_attribute, IntHash>::const_iterator cursor_t;

class fdAttributeCache {
  std::unordered_map<int, fd_attribute, IntHash> cache;
  std::mutex access_lock;

public:
  fd_attribute get_attribute(int fd) {
    std::lock_guard<std::mutex> guard(access_lock);
    cursor_t c = cache.find(fd);
    if (c != cache.end()) {
      return c->second;
    }
    return 0;
  }
  void set_attribute(int fd, fd_attribute attr) {
    std::lock_guard<std::mutex> guard(access_lock);
    cache[fd] = attr;
  }
  void unset_attribute(int fd) {
    std::lock_guard<std::mutex> guard(access_lock);
    cache.erase(fd);
  }
  void clear(void) {
    std::lock_guard<std::mutex> guard(access_lock);
    cache.clear();
  }
};

static notagread_t no_tag_read_behaviour;
static int no_tag_ignore_ccsid1047;

static notagread_t get_no_tag_read_behaviour(const char *envar) {
  char *ntr = __getenv_a(envar);
  if (ntr && !strcmp(ntr, "AUTO")) {
    return __NO_TAG_READ_DEFAULT;
  } else if (ntr && !strcmp(ntr, "WARN")) {
    return __NO_TAG_READ_DEFAULT_WITHWARNING;
  } else if (ntr && !strcmp(ntr, "V6")) {
    return __NO_TAG_READ_V6;
  } else if (ntr && !strcmp(ntr, "STRICT")) {
    return __NO_TAG_READ_STRICT;
  }
  return __NO_TAG_READ_DEFAULT; // default
}

extern "C" notagread_t __get_no_tag_read_behaviour() {
  return no_tag_read_behaviour;
}

static int get_no_tag_ignore_ccsid1047(const char *envar) {
  char *ntr = __getenv_a(envar);
  if (ntr && !strcmp(ntr, "NO")) {
    return 1;
  }
  return 0; // default, consider conversion for txtflag 0 && ccsid 1047
}

extern "C" int __get_no_tag_ignore_ccsid1047() {
  return no_tag_ignore_ccsid1047;
}

extern "C" unsigned long __mach_absolute_time(void) {
  unsigned long long value;
  __stckf(&value);
  return ((value / 512UL) * 125UL) - 2208988800000000000UL;
}

//------------------------------------------accounting for memory allocation
// begin

static const int kMegaByte = 1024 * 1024;
static const int kGigaByte = 1024 * 1024 * 1024;

static int mem_account(void) {
  static int res = -1;
  if (-1 == res) {
    res = 0;
    char *ma = getenv("__MEM_ACCOUNT");
    if (ma && 0 == strcmp("1", ma)) {
      res = 1;
    }
  }
  return res;
}

static void getxttoken(char *out) {
  void *p;
  asm("  l %0,1208 \n"
      "  llgtr %0,%0 \n"
      "  lg %0,x'130'(%0)\n"
      : "+r"(p)::);
  memcpy(out, (char *)p + 0x14, 16);
}

struct iarv64parm {
  unsigned char xversion __attribute__((__aligned__(16))); //    0
  unsigned char xrequest;                                  //    1
  unsigned xmotknsource_system : 1;                        //    2
  unsigned xmotkncreator_system : 1;                       //    2(1)
  unsigned xmatch_motoken : 1;                             //    2(2)
  unsigned xflags0_rsvd1 : 5;                              //    2(3)
  unsigned char xkey;                                      //    3
  unsigned keyused_key : 1;                                //    4
  unsigned keyused_usertkn : 1;                            //    4(1)
  unsigned keyused_ttoken : 1;                             //    4(2)
  unsigned keyused_convertstart : 1;                       //    4(3)
  unsigned keyused_guardsize64 : 1;                        //    4(4)
  unsigned keyused_convertsize64 : 1;                      //    4(5)
  unsigned keyused_motkn : 1;                              //    4(6)
  unsigned keyused_ownerjobname : 1;                       //    4(7)
  unsigned xcond_yes : 1;                                  //    5
  unsigned xfprot_no : 1;                                  //    5(1)
  unsigned xcontrol_auth : 1;                              //    5(2)
  unsigned xguardloc_high : 1;                             //    5(3)
  unsigned xchangeaccess_global : 1;                       //    5(4)
  unsigned xpageframesize_1meg : 1;                        //    5(5)
  unsigned xpageframesize_max : 1;                         //    5(6)
  unsigned xpageframesize_all : 1;                         //    5(7)
  unsigned xmatch_usertoken : 1;                           //    6
  unsigned xaffinity_system : 1;                           //    6(1)
  unsigned xuse2gto32g_yes : 1;                            //    6(2)
  unsigned xowner_no : 1;                                  //    6(3)
  unsigned xv64select_no : 1;                              //    6(4)
  unsigned xsvcdumprgn_no : 1;                             //    6(5)
  unsigned xv64shared_no : 1;                              //    6(6)
  unsigned xsvcdumprgn_all : 1;                            //    6(7)
  unsigned xlong_no : 1;                                   //    7
  unsigned xclear_no : 1;                                  //    7(1)
  unsigned xview_readonly : 1;                             //    7(2)
  unsigned xview_sharedwrite : 1;                          //    7(3)
  unsigned xview_hidden : 1;                               //    7(4)
  unsigned xconvert_toguard : 1;                           //    7(5)
  unsigned xconvert_fromguard : 1;                         //    7(6)
  unsigned xkeepreal_no : 1;                               //    7(7)
  unsigned long long xsegments;                            //    8
  unsigned char xttoken[16];                               //   16
  unsigned long long xusertkn;                             //   32
  void *xorigin;                                           //   40
  void *xranglist;                                         //   48
  void *xmemobjstart;                                      //   56
  unsigned xguardsize;                                     //   64
  unsigned xconvertsize;                                   //   68
  unsigned xaletvalue;                                     //   72
  int xnumrange;                                           //   76
  void *__ptr32 xv64listptr;                               //   80
  unsigned xv64listlength;                                 //   84
  unsigned long long xconvertstart;                        //   88
  unsigned long long xconvertsize64;                       //   96
  unsigned long long xguardsize64;                         //  104
  char xusertoken[8];                                      //  112
  unsigned char xdumppriority;                             //  120
  unsigned xdumpprotocol_yes : 1;                          //  121
  unsigned xorder_dumppriority : 1;                        //  121(1)
  unsigned xtype_pageable : 1;                             //  121(2)
  unsigned xtype_dref : 1;                                 //  121(3)
  unsigned xownercom_home : 1;                             //  121(4)
  unsigned xownercom_primary : 1;                          //  121(5)
  unsigned xownercom_system : 1;                           //  121(6)
  unsigned xownercom_byasid : 1;                           //  121(7)
  unsigned xv64common_no : 1;                              //  122
  unsigned xmemlimit_no : 1;                               //  122(1)
  unsigned xdetachfixed_yes : 1;                           //  122(2)
  unsigned xdoauthchecks_yes : 1;                          //  122(3)
  unsigned xlocalsysarea_yes : 1;                          //  122(4)
  unsigned xamountsize_4k : 1;                             //  122(5)
  unsigned xamountsize_1meg : 1;                           //  122(6)
  unsigned xmemlimit_cond : 1;                             //  122(7)
  unsigned keyused_dump : 1;                               //  123
  unsigned keyused_optionvalue : 1;                        //  123(1)
  unsigned keyused_svcdumprgn : 1;                         //  123(2)
  unsigned xattribute_defs : 1;                            //  123(3)
  unsigned xattribute_ownergone : 1;                       //  123(4)
  unsigned xattribute_notownergone : 1;                    //  123(5)
  unsigned xtrackinfo_yes : 1;                             //  123(6)
  unsigned xunlocked_yes : 1;                              //  123(7)
  unsigned char xdump;                                     //  124
  unsigned xpageframesize_pageable1meg : 1;                //  125
  unsigned xpageframesize_dref1meg : 1;                    //  125(1)
  unsigned xsadmp_yes : 1;                                 //  125(2)
  unsigned xsadmp_no : 1;                                  //  125(3)
  unsigned xuse2gto64g_yes : 1;                            //  125(4)
  unsigned xdiscardpages_yes : 1;                          //  125(5)
  unsigned xexecutable_yes : 1;                            //  125(6)
  unsigned xexecutable_no : 1;                             //  125(7)
  unsigned short xownerasid;                               //  126
  unsigned char xoptionvalue;                              //  128
  unsigned char xrsv0001[8];                               //  129
  unsigned char xownerjobname[8];                          //  137
  unsigned char xrsv0004[7];                               //  145
  void *xdmapagetable;                                     //  152
  unsigned long long xunits;                               //  160
  unsigned keyused_units : 1;                              //  168
  unsigned xunitsize_1m : 1;                               //  168(1)
  unsigned xunitsize_2g : 1;                               //  168(2)
  unsigned xpageframesize_1m : 1;                          //  168(3)
  unsigned xpageframesize_2g : 1;                          //  168(4)
  unsigned xtype_fixed : 1;                                //  168(5)
  unsigned xflags9_rsvd1 : 2;                              //  168(6)
  unsigned xkeyused_inorigin : 1;                          //  169
  unsigned x_rsv0005 : 7;                                  //  169(1)
  unsigned char xrsv0006[6];                               //  170
};

static long long __iarv64(void *parm, long long *reason_code_ptr) {
  long long rc;
  long long reason;
  char *code = ((char *__ptr32 *__ptr32 *__ptr32 *)0)[4][193][52];
  code = (char *)(((unsigned long long)code) | 14); // offset to the entry
  asm(" pc 0(%3)"
      : "=NR:r0"(reason), "+NR:r1"(parm), "=NR:r15"(rc)
      : "r"(code)
      :);
  rc = (rc & 0x0ffff);
  if (rc != 0 && reason_code_ptr != 0) {
    *reason_code_ptr = (0x0ffff & reason);
  }
  return rc;
}

// getipttoken returns the address of the initial process thread library
// anchor area. This is used to create a user token that associates
// memory allocations with the LE enclave.
// See
// https://www.ibm.com/support/knowledgecenter/en/SSLTBW_2.1.0/com.ibm.zos.v2r1.ceev100/mout.htm
unsigned long getipttoken(void) {
  return ((unsigned long)((char *__ptr32 *__ptr32 *__ptr32)(1208))[0][82])
         << 32;
}

static void *__iarv64_alloc(int segs, const char *token) {
  if (segs == 0) {
    // process gets killed if __iarv64(&parm,..) is called with parm.xsegments=0
    if (mem_account())
      dprintf(2, "WARNING: ignoring request to allocate 0 segments, errno set "
                 "to ENOMEM\n");
    errno = ENOMEM; // mimic behaviour of malloc(0)
    return 0;
  }
  long long rc, reason;
  struct iarv64parm parm __attribute__((__aligned__(16)));
  memset(&parm, 0, sizeof(parm));
  parm.xversion = 5;
  parm.xrequest = 1;
  parm.xcond_yes = 1;
  parm.xsegments = segs;
  parm.xorigin = 0;
  parm.xdumppriority = 99;
  parm.xtype_pageable = 1;
  parm.xdump = 32;
  parm.xusertkn = getipttoken();
  parm.xsadmp_no = 1;
  parm.xpageframesize_pageable1meg = 1;
  parm.xuse2gto64g_yes = 1;
  parm.xexecutable_yes = 1;
  parm.keyused_ttoken = 1;
  memcpy(&parm.xttoken, token, 16);
  rc = __iarv64(&parm, &reason);
  if (mem_account())
    dprintf(2,
            "__iarv64_alloc: pid %d tid %d ptr=%p size=%lu(0x%lx) rc=%llx, "
            "reason=%llx\n",
            getpid(), gettid(), parm.xorigin, (unsigned long)segs * kMegaByte,
            (unsigned long)segs * kMegaByte, rc, reason);
  if (rc == 0) {
    return parm.xorigin;
  }
  return 0;
}

static void *__iarv64_alloc_inorigin(int segs, const char *token,
                                     void *inorigin) {
  if (segs == 0) {
    // process gets killed if __iarv64(&parm,..) is called with parm.xsegments=0
    if (mem_account())
      dprintf(2, "WARNING: ignoring request to allocate 0 segments, errno set "
                 "to ENOMEM\n");
    errno = ENOMEM; // mimic behaviour of malloc(0)
    return 0;
  }
  long long rc, reason;
  struct iarv64parm parm __attribute__((__aligned__(16)));
  memset(&parm, 0, sizeof(parm));
  parm.xversion = 6;
  parm.xrequest = 1;
  parm.xcond_yes = 1;
  parm.xsegments = segs;
  parm.xorigin = 0;
  parm.xdumppriority = 99;
  parm.xtype_pageable = 1;
  parm.xdump = 32;
  parm.xusertkn = getipttoken();
  parm.xsadmp_no = 1;
  parm.xpageframesize_pageable1meg = 1;
  parm.xuse2gto64g_yes = 0;
  parm.xexecutable_yes = 1;
  parm.keyused_ttoken = 1;
  parm.xkeyused_inorigin = 1;
  parm.xmemobjstart = inorigin;
  memcpy(&parm.xttoken, token, 16);
  rc = __iarv64(&parm, &reason);
  if (mem_account())
    dprintf(2,
            "__iarv64_alloc: pid %d tid %d ptr=%p size=%lu(0x%lx) rc=%llx, "
            "reason=%llx\n",
            getpid(), gettid(), parm.xorigin, (unsigned long)segs * kMegaByte,
            (unsigned long)segs * kMegaByte, rc, reason);
  if (rc == 0) {
    return parm.xorigin;
  }
  return 0;
}

#define __USE_IARV64 1 // 0=moservices, 1=iarv64
static int __iarv64_free(void *ptr, const char *token) {
  long long rc, reason;
  void *org = ptr;
  struct iarv64parm parm __attribute__((__aligned__(16)));
  memset(&parm, 0, sizeof(parm));
  parm.xversion = 5;
  parm.xrequest = 3;
  parm.xcond_yes = 1;
  parm.xsadmp_no = 1;
  parm.xmemobjstart = ptr;
  parm.keyused_ttoken = 1;
  memcpy(&parm.xttoken, token, 16);
  rc = __iarv64(&parm, &reason);
  if (mem_account())
    dprintf(2, "__iarv64_free pid %d tid %d ptr=%p rc=%lld\n", getpid(),
            gettid(), org, rc);
  return rc;
}

static void *__mo_alloc(int segs) {
  __mopl_t moparm;
  void *p = 0;
  memset(&moparm, 0, sizeof(moparm));
  moparm.__mopldumppriority = __MO_DUMP_PRIORITY_STACK + 5;
  moparm.__moplrequestsize = segs;
  moparm.__moplgetstorflags = __MOPL_PAGEFRAMESIZE_PAGEABLE1MEG;
  int rc = __moservices(__MO_GETSTOR, sizeof(moparm), &moparm, &p);
  if (mem_account()) {
    fprintf(stderr,
            "__moservices-alloc: pid %d tid %d ptr=%p size=%lu(0x%lx) rc=%d, "
            "iarv64_rc=%d\n",
            getpid(), gettid(), p, (unsigned long)segs * kMegaByte,
            (unsigned long)segs * kMegaByte, rc, moparm.__mopl_iarv64_rc);
  }
  if (rc == 0 && moparm.__mopl_iarv64_rc == 0) {
    return p;
  }
  perror("__moservices GETSTOR");
  return 0;
}

static int __mo_free(void *ptr) {
  int rc = __moservices(__MO_DETACH, 0, NULL, &ptr);
  if (mem_account()) {
    fprintf(stderr, "__moservices-free: pid %d tid %d ptr=%p rc=%d\n", getpid(),
            gettid(), ptr, rc);
  }
  if (rc) {
    perror("__moservices DETACH");
  }
  return rc;
}

typedef unsigned long value_type;
typedef unsigned long key_type;

struct __hash_func {
  size_t operator()(const key_type &k) const {
    int s = 0;
    key_type n = k;
    while (0 == (n & 1) && s < (sizeof(key_type) - 1)) {
      n = n >> 1;
      ++s;
    }
    return s + (n * 0x744dcf5364d7d667UL);
  }
};

static int anon_munmap_inner(void *addr, size_t len, bool is_above_bar);

typedef std::unordered_map<key_type, value_type, __hash_func>::const_iterator
    mem_cursor_t;

class __Cache {
  std::unordered_map<key_type, value_type, __hash_func> cache;
  std::mutex access_lock;
  char xttoken[16];
  unsigned short asid;
  int oktouse;

public:
  __Cache() {
#if __USE_IARV64
    getxttoken(xttoken);
    asid = ((unsigned short *)(*(char *__ptr32 *)(0x224)))[18];
#endif
    oktouse =
        (*(int *)(80 + ((char ****__ptr32 *)1208)[0][11][1][123]) > 0x040202FF);
    // LE level is 220 or above
  }
  void addptr(const void *ptr, size_t v) {
    unsigned long k = (unsigned long)ptr;
    std::lock_guard<std::mutex> guard(access_lock);
    cache[k] = (unsigned long)v;
    if (mem_account())
      dprintf(2, "ADDED: @%lx size %lu\n", k, cache[k]);
  }
  // normal case:  bool elligible() { return oktouse; }
  bool elligible() { return true; } // always true for now
#if __USE_IARV64
  void *alloc_seg(int segs) {
    std::lock_guard<std::mutex> guard(access_lock);
    void *p = __iarv64_alloc(segs, xttoken);
    if (p) {
      unsigned long k = (unsigned long)p;
      cache[k] = (unsigned long)segs * kMegaByte;
      if (mem_account())
        dprintf(2, "MEM_CACHE INSERTED: @%lx size %lu RMODE64\n", k, cache[k]);
    }
    return p;
  }
  int free_seg(void *ptr) {
    unsigned long k = (unsigned long)ptr;
    std::lock_guard<std::mutex> guard(access_lock);
    int rc = __iarv64_free(ptr, xttoken);
    if (rc == 0) {
      mem_cursor_t c = cache.find(k);
      if (c != cache.end()) {
        unsigned long s = c->second;
        cache.erase(c);
        if (mem_account()) {
          dprintf(2, "MEM_CACHE DELETED: @%lx size %lu RMODE64\n", k, s);
        }
      }
    }
    return rc;
  }
#else
  void *alloc_seg(int segs) {
    void *p = __mo_alloc(segs);
    std::lock_guard<std::mutex> guard(access_lock);
    if (p) {
      unsigned long k = (unsigned long)p;
      cache[k] = (unsigned long)segs * kMegaByte;
      if (mem_account())
        dprintf(2, "MEM_CACHE INSERTED: @%lx size %lu RMODE64\n", k,
                (unsigned long)segs * kMegaByte);
    }
    return p;
  }
  int free_seg(void *ptr) {
    unsigned long k = (unsigned long)ptr;
    int rc = __mo_free(ptr);
    std::lock_guard<std::mutex> guard(access_lock);
    if (rc == 0) {
      mem_cursor_t c = cache.find(k);
      if (c != cache.end()) {
        unsigned long s = c->second;
        cache.erase(c);
        if (mem_account()) {
          dprintf(2, "MEM_CACHE DELETED: @%lx size %lu RMODE64\n", k, s);
        }
      }
    }
    return rc;
  }
#endif
  int is_exist_ptr(const void *ptr) {
    unsigned long k = (unsigned long)ptr;
    std::lock_guard<std::mutex> guard(access_lock);
    mem_cursor_t c = cache.find(k);
    if (c != cache.end()) {
      return 1;
    }
    return 0;
  }
  int is_rmode64(const void *ptr) {
    unsigned long k = (unsigned long)ptr;
    std::lock_guard<std::mutex> guard(access_lock);
    mem_cursor_t c = cache.find(k);
    if (c != cache.end()) {
      if (0 != (k & 0xffffffff80000000UL))
        return 1;
      else
        return 0;
    }
    return 0;
  }
  void show(void) {
    std::lock_guard<std::mutex> guard(access_lock);
    if (mem_account()) {
      for (mem_cursor_t it = cache.begin(); it != cache.end(); ++it) {
        dprintf(2, "LIST: @%lx size %lu\n", it->first, it->second);
      }
    }
  }
  void freeptr(const void *ptr) {
    unsigned long k = (unsigned long)ptr;
    std::lock_guard<std::mutex> guard(access_lock);
    mem_cursor_t c = cache.find(k);
    if (c != cache.end()) {
      cache.erase(c);
    }
  }
  ~__Cache() {
    std::lock_guard<std::mutex> guard(access_lock);
    if (mem_account()) {
      for (mem_cursor_t it = cache.begin(); it != cache.end(); ++it) {
        dprintf(2,
                "Error: DEBRIS (allocated but never free'd): @%lx size %lu\n",
                it->first, it->second);
      }
    }
  }
};

static __Cache alloc_info;

static void *anon_mmap_inner(void *addr, size_t len) {
  int retcode;
  if (alloc_info.elligible() && len % kMegaByte == 0) {
    size_t request_size = len / kMegaByte;
    void *p = alloc_info.alloc_seg(request_size);
    if (p)
      return p;
    else
      return MAP_FAILED;
  } else if (alloc_info.elligible() && len > 2UL * kGigaByte) {
    size_t request_size = __round_up(len, kMegaByte) / kMegaByte;
    void *p = alloc_info.alloc_seg(request_size);
    if (p)
      return p;
    else
      return MAP_FAILED;
  } else {
    char *p;
#if defined(__64BIT__)
    __asm(" SYSSTATE ARCHLVL=2,AMODE64=YES\n"
          " STORAGE "
          "OBTAIN,LENGTH=(%2),BNDRY=PAGE,COND=YES,ADDR=(%0),RTCD=(%1),"
          "LOC=(31,64)\n"
#if defined(__clang__)
          : "=NR:r1"(p), "=NR:r15"(retcode)
          : "NR:r0"(len)
          : "r0", "r1", "r14", "r15");
#else
          : "=r"(p), "=r"(retcode)
          : "r"(len)
          : "r0", "r1", "r14", "r15");
#endif
#else
    __asm(" SYSSTATE ARCHLVL=2\n"
          " STORAGE "
          "OBTAIN,LENGTH=(%2),BNDRY=PAGE,COND=YES,ADDR=(%0),RTCD=(%1)\n"
#if defined(__clang__)
          : "=NR:r1"(p), "=NR:r15"(retcode)
          : "NR:r0"(len)
          : "r0", "r1", "r14", "r15");
#else
          : "=r"(p), "=r"(retcode)
          : "r"(len)
          : "r0", "r1", "r14", "r15");
#endif

#endif
    if (retcode == 0) {
      alloc_info.addptr(p, len);
      return p;
    }
    return MAP_FAILED;
  }
}

static int anon_munmap_inner(void *addr, size_t len, bool is_above_bar) {
  int retcode;
  if (is_above_bar) {
    return alloc_info.free_seg(addr);
  } else {
#if defined(__64BIT__)
    __asm(" SYSSTATE ARCHLVL=2,AMODE64=YES\n"
          " STORAGE RELEASE,LENGTH=(%2),ADDR=(%1),RTCD=(%0),COND=YES\n"
#if defined(__clang__)
          : "=NR:r15"(retcode)
          : "NR:r1"(addr), "NR:r0"(len)
          : "r0", "r1", "r14", "r15");
#else
          : "=r"(retcode)
          : "r"(addr), "r"(len)
          : "r0", "r1", "r14", "r15");
#endif
#else
    __asm("SYSSTATE ARCHLVL=2"
          "STORAGE RELEASE,LENGTH=(%2),ADDR=(%1),RTCD=(%0),COND=YES"
#if defined(__clang__)
          : "=NR:r15"(retcode)
          : "NR:r1"(addr), "NR:r0"(len)
          : "r0", "r1", "r14", "r15");
#else
          : "=r"(retcode)
          : "r"(addr), "r"(len)
          : "r0", "r1", "r14", "r15");
#endif

#endif
    if (0 == retcode)
      alloc_info.freeptr(addr);
  }
  return retcode;
}

extern "C" void *anon_mmap(void *_, size_t len) {
  void *ret = anon_mmap_inner(_, len);
  if (ret == MAP_FAILED) {
    if (mem_account()) {
      dprintf(2, "Error: anon_mmap request size %zu failed\n", len);
    }
    return ret;
  }
  return ret;
}

extern "C" int anon_munmap(void *addr, size_t len) {
  if (alloc_info.is_exist_ptr(addr)) {
    if (mem_account())
      dprintf(2, "Address found, attempt to free @%p size %zu\n", addr, len);
    int rc = anon_munmap_inner(addr, len, alloc_info.is_rmode64(addr));
    if (rc != 0) {
      if (mem_account()) {
        dprintf(2, "Error: anon_munmap @%p size %zu failed\n", addr, len);
      }
      return rc;
    }
    return 0;
  } else {
    if (mem_account())
      dprintf(2, "Error: attempt to free %p size %zu (not allocated)\n", addr,
              len);
    return 0;
  }
}

extern "C" int execvpe(const char *name, char *const argv[],
                       char *const envp[]) {
  int lp, ln;
  const char *p;

  int eacces = 0, etxtbsy = 0;
  char *bp, *cur, *path, *buf = 0;

  // Absolute or Relative Path Name
  if (strchr(name, '/')) {
    return execve(name, argv, envp);
  }

  // Get the path we're searching
  if (!(path = getenv("PATH"))) {
    if ((cur = path = (char *)alloca(2)) != NULL) {
      path[0] = ':';
      path[1] = '\0';
    }
  } else {
    char *n = (char *)alloca(strlen(path) + 1);
    strcpy(n, path);
    cur = path = n;
  }

  if (path == NULL ||
      (bp = buf = (char *)alloca(strlen(path) + strlen(name) + 2)) == NULL)
    goto done;

  while (cur != NULL) {
    p = cur;
    if ((cur = strchr(cur, ':')) != NULL)
      *cur++ = '\0';

    if (!*p) {
      p = ".";
      lp = 1;
    } else
      lp = strlen(p);
    ln = strlen(name);

    memcpy(buf, p, lp);
    buf[lp] = '/';
    memcpy(buf + lp + 1, name, ln);
    buf[lp + ln + 1] = '\0';

  retry:
    (void)execve(bp, argv, envp);
    switch (errno) {
    case EACCES:
      eacces = 1;
      break;
    case ENOTDIR:
    case ENOENT:
      break;
    case ENOEXEC: {
      size_t cnt;
      char **ap;

      for (cnt = 0, ap = (char **)argv; *ap; ++ap, ++cnt)
        ;
      if ((ap = (char **)alloca((cnt + 2) * sizeof(char *))) != NULL) {
        memcpy(ap + 2, argv + 1, cnt * sizeof(char *));

        ap[0] = (char *)"sh";
        ap[1] = bp;
        (void)execve("/bin/sh", ap, envp);
      }
      goto done;
    }
    case ETXTBSY:
      if (etxtbsy < 3)
        (void)sleep(++etxtbsy);
      goto retry;
    default:
      goto done;
    }
  }
  if (eacces)
    errno = EACCES;
  else if (!errno)
    errno = ENOENT;
done:
  return (-1);
}
//------------------------------------------accounting for memory allocation end

// --- start __atomic_store
#define CSG(_op1, _op2, _op3)                                                  \
  __asm(" csg %0,%2,%1 \n " : "+r"(_op1), "+m"(_op2) : "r"(_op3) :)

#define CS(_op1, _op2, _op3)                                                   \
  __asm(" cs %0,%2,%1 \n " : "+r"(_op1), "+m"(_op2) : "r"(_op3) :)

extern "C" void __atomic_store_real(int size, void *ptr, void *val,
                                    int memorder) asm("__atomic_store");
void __atomic_store_real(int size, void *ptr, void *val, int memorder) {
  if (size == 4) {
    unsigned int new_val = *(unsigned int *)val;
    unsigned int *stor = (unsigned int *)ptr;
    unsigned int org;
    unsigned int old_val;
    do {
      org = *(unsigned int *)ptr;
      old_val = org;
      CS(old_val, *stor, new_val);
    } while (old_val != org);
  } else if (size == 8) {
    unsigned long new_val = *(unsigned long *)val;
    unsigned long *stor = (unsigned long *)ptr;
    unsigned long org;
    unsigned long old_val;
    do {
      org = *(unsigned long *)ptr;
      old_val = org;
      CSG(old_val, *stor, new_val);
    } while (old_val != org);
  } else if (0x40 & *(const char *)209) {
    long cc;
    int retry = 10000;
    while (retry--) {
      __asm(" TBEGIN 0,X'FF00'\n"
            " IPM      %0\n"
            " LLGTR    %0,%0\n"
            " SRLG     %0,%0,28\n"
            : "=r"(cc)::);
      if (0 == cc) {
        memcpy(ptr, val, size);
        __asm(" TEND\n"
              " IPM      %0\n"
              " LLGTR    %0,%0\n"
              " SRLG     %0,%0,28\n"
              : "=r"(cc)::);
        if (0 == cc)
          break;
      }
    }
    if (retry < 1) {
      dprintf(2, "%s:%s:%d size=%d target=%p source=%p store failed\n",
              __FILE__, __FUNCTION__, __LINE__, size, ptr, val);
      abort();
    }
  } else {
    dprintf(2, "%s:%s:%d size=%d target=%p source=%p not implimented\n",
            __FILE__, __FUNCTION__, __LINE__, size, ptr, val);
    abort();
  }
}
// --- end __atomic_store

struct espiearg {
  void *__ptr32 exitproc;
  void *__ptr32 exitargs;
  int flags;
  void *__ptr32 reserved;
};

extern "C" int __testread(const void *location) {
  struct espiearg *r1 = (struct espiearg *)__malloc31(sizeof(struct espiearg));
  long token = 0;
  volatile int state = 0;
  volatile int word;
  r1->flags = 0x08000000;
  r1->reserved = 0;
  __asm volatile("&suffix SETA &suffix+1\n"
                 " lg 1,%1\n"
                 " larl 0,exit&suffix \n"
                 " st 0,0(1)\n"
                 " larl 0,back&suffix \n"
                 " st 0,4(1)\n"
                 " la 15,28\n"
                 " la 0,4\n"
                 " svc 109\n"
                 " stg 1,%0\n"
                 " brc 15,back&suffix \n"
                 "exit&suffix  l 2,4(1)\n"
                 " la 3,1\n"
                 " sll 3,31\n"
                 " or 2,3\n"
                 " st 2,76(1)\n"
                 " br 14\n"
                 "back&suffix  ds 0d\n"
                 : "=m"(token)
                 : "m"(r1)
                 : "r0", "r1", "r15");

  if (state == 1) {
    state = 2;
  } else {
    state = 1;
    word = *(int *)location;
  }
  __asm volatile(" lg 1,%0\n"
                 " la 0,8\n"
                 " la 15,28\n"
                 " svc 109\n"
                 :
                 : "m"(token)
                 : "r0", "r1", "r15");
  free(r1);
  if (state != 1)
    return -1;
  return 0;
}

static int returnStatus(int error, const char *msg) {
  if (0 != error) {
    if (msg)
      perror(msg);
    errno = error;
    return -1;
  }
  return 0;
}
void Usleep(unsigned int msec) {
  int sec = msec / 1000000;
  if (sec == 0) {
    usleep(msec);
  } else {
    sleep(sec);
    usleep(msec - (sec * 1000000));
  }
}

static unsigned int atomic_helper(volatile unsigned int *loc, int val) {
  volatile unsigned int tmp = *loc;
  volatile unsigned int org;
  org = __zsync_val_compare_and_swap32(loc, tmp, tmp + val);
  while (org != tmp) {
    tmp = *loc;
    org = __zsync_val_compare_and_swap32(loc, tmp, tmp + val);
  }
  return org;
}

unsigned int atomic_dec(volatile unsigned int *loc) {
  return atomic_helper(loc, -1);
}

unsigned int atomic_inc(volatile unsigned int *loc) {
  return atomic_helper(loc, 1);
}

static int we_expired(const struct timespec *t0) {
  unsigned long long value, sec, nsec;
  __stckf(&value);
  sec = (value / 4096000000UL) - 2208988800UL;
  if (sec > t0->tv_sec) {
    return 1;
  }

  if (sec == t0->tv_sec) {
    nsec = (value % 4096000000UL) * 1000 / 4096;
    if (nsec > t0->tv_nsec) {
      return 1;
    }
  }
  return 0;
}

extern int __sem_init(__sem_t *s0, int shared, unsigned int val) { // no lock
  ____sem_t *s = (____sem_t *)calloc(1, sizeof(____sem_t));
  int err;
  s0->_s = s;
  s->value = val;
  if (shared) {
    s->id = getpid();
  } else {
    s->id = 0;
  }
  if (shared == 0) {
    int rc;
    s->waitcnt = 0;
    rc = pthread_mutex_init(&s->mutex, 0);
    if (rc)
      err = errno;
    if (0 == rc) {
      rc = pthread_cond_init(&s->cond, 0);
      if (rc)
        err = errno;
      if (0 == rc)
        return 0;
      pthread_mutex_destroy(&s->mutex);
      free(s);
      s0->_s = 0;
      return returnStatus(err, "pthread_cond_init");
    } else {
      free(s);
      s0->_s = 0;
      return returnStatus(err, "pthread_mutex_init");
    }
  }
  return 0;
}
static int __sem_post_thread_w(____sem_t *s) {
  ++s->value;
  if (s->waitcnt > 0) {
    int rc = pthread_cond_signal(&s->cond);
    if (rc) {
      // unexpected error
      perror("pthread_cond_signal");
      errno = EINVAL;
      --s->value;
      return -1;
    }
  }
  return 0;
}
static int __sem_post_thread(____sem_t *s) {
  int rc, err;
  rc = pthread_mutex_lock(&s->mutex);
  if (rc == 0) {
    rc = __sem_post_thread_w(s);
    if (rc == 0) {
      pthread_mutex_unlock(&s->mutex);
      return 0;
    } else {
      err = errno;
      pthread_mutex_unlock(&s->mutex);
      return returnStatus(err, 0);
    }
  }
  return returnStatus(errno, "pthread_mutex_lock");
}
extern int __sem_post(__sem_t *s0) {
  ____sem_t *s = (____sem_t *)s0->_s;
  volatile unsigned int o;
  if (s->id != 0 && s->id != getpid()) {
    return returnStatus(EINVAL, 0);
  }
  if (s->id == 0) {
    int rc = __sem_post_thread(s);
    return rc;
  } else {
    atomic_inc(&s->value);
  }
  return 0;
}
static int __sem_trywait_thread_w(____sem_t *s) {
  if (s->value > 0) {
    --s->value;
    return 0;
  }
  errno = EAGAIN;
  return -1;
}
static int __sem_trywait_thread(____sem_t *s) {
  int rc, err;
  rc = pthread_mutex_lock(&s->mutex);
  if (rc == 0) {
    rc = __sem_trywait_thread_w(s);
    if (rc == 0) {
      pthread_mutex_unlock(&s->mutex);
      return 0;
    } else {
      err = errno;
      pthread_mutex_unlock(&s->mutex);
      return returnStatus(err, 0);
    }
  }
  return returnStatus(errno, "pthread_mutex_lock");
}
extern int __sem_trywait(__sem_t *s0) {
  ____sem_t *s = (____sem_t *)s0->_s;
  volatile unsigned int v;
  volatile unsigned int o;
  if (s->id != 0 && s->id != getpid()) {
    return returnStatus(EINVAL, 0);
  }
  if (s->id == 0) {
    int rc = __sem_trywait_thread(s);
    return rc;
  }
  v = s->value;
  if (v > 0) {
    o = __zsync_val_compare_and_swap32(&s->value, v, v - 1);
    if (o == v) {
      return 0;
    }
  }
  return returnStatus(EAGAIN, 0);
}
static int __sem_timedwait_share(____sem_t *s,
                                 const struct timespec *abs_timeout) {
  volatile unsigned int v;
  volatile unsigned int o;
  unsigned int cnt = 0;
  if (s->id != getpid()) {
    return returnStatus(EINVAL, 0);
  }
  cnt = 0;
  while (1) {
    v = s->value;
    if (v == 0) {
      if (abs_timeout && we_expired(abs_timeout)) {
        return returnStatus(ETIMEDOUT, 0);
      }
      if (cnt <= 10000000) {
        cnt += 500;
      }
      Usleep(cnt);
    } else {
      // v > 0
      o = __zsync_val_compare_and_swap32(&s->value, v, v - 1);
      if (o == v) {
        return 0;
      }
    }
  }
  return 0;
}
static int __sem_timedwait_thread_w(____sem_t *s,
                                    const struct timespec *abs_timeout) {
  int rc = 0;
  while (s->value == 0 && rc == 0) {
    ++s->waitcnt;
    if (abs_timeout)
      rc = pthread_cond_timedwait(&s->cond, &s->mutex, abs_timeout);
    else
      rc = pthread_cond_wait(&s->cond, &s->mutex);
    --s->waitcnt;
  }
  if (rc == 0 && s->value > 0)
    --s->value;
  return rc;
}
static int __sem_timedwait_thread(____sem_t *s,
                                  const struct timespec *abs_timeout) {
  int rc, err;
  rc = pthread_mutex_lock(&s->mutex);
  if (rc == 0) {
    rc = __sem_timedwait_thread_w(s, abs_timeout);
    if (rc == 0) {
      pthread_mutex_unlock(&s->mutex);
      return 0;
    } else {
      err = errno;
      pthread_mutex_unlock(&s->mutex);
      return returnStatus(err, 0);
    }
  }
  return returnStatus(errno, "pthread_mutex_lock");
}
extern int __sem_timedwait(__sem_t *s0, const struct timespec *abs_timeout) {
  ____sem_t *s = (____sem_t *)s0->_s;
  int rc;
  if (s->id) {
    rc = __sem_timedwait_share(s, abs_timeout);
  } else {
    rc = __sem_timedwait_thread(s, abs_timeout);
  }
  return rc;
}

extern int __sem_wait(__sem_t *s0) {
  ____sem_t *s = (____sem_t *)s0->_s;
  int rc = __sem_timedwait(s0, 0);
  return rc;
}

extern int __sem_destroy(__sem_t *s0) {
  ____sem_t *s = (____sem_t *)s0->_s;
  s->id = 0;
  s->value = 0;
  pthread_mutex_destroy(&s->mutex);
  pthread_cond_destroy(&s->cond);
  free(s);
  s0->_s = 0;
  return 0;
}
static int __sem_getvalue_thread_w(____sem_t *s, int *sval) {
  *sval = s->value;
  return 0;
}
static int __sem_getvalue_thread(____sem_t *s, int *sval) {
  int rc, err;
  rc = pthread_mutex_lock(&s->mutex);
  if (rc == 0) {
    rc = __sem_getvalue_thread_w(s, sval);
    if (rc == 0) {
      pthread_mutex_unlock(&s->mutex);
      return 0;
    } else {
      err = errno;
      pthread_mutex_unlock(&s->mutex);
      return returnStatus(err, 0);
    }
  }
  return returnStatus(errno, "pthread_mutex_lock");
}
extern int __sem_getvalue(__sem_t *s0, int *sval) {
  ____sem_t *s = (____sem_t *)s0->_s;
  if (s->id) {
    *sval = s->value;
  } else {
    int rc = __sem_getvalue_thread(s, sval);
    return rc;
  }
  return 0;
}
extern "C" void __tb(void) {
  void *buffer[100];
  int nptrs = backtrace(buffer, 100);
  char **str = backtrace_symbols(buffer, nptrs);
  if (str) {
    int pid = getpid();
    for (int i = 0; i < nptrs; ++i)
      __console_printf("pid %d ->%s\n", pid, str[i]);
    free(str);
  }
}

extern "C" int clock_gettime(clockid_t clk_id, struct timespec *tp) {
  unsigned long long value;
  __stckf(&value);
  tp->tv_sec = (value / 4096000000UL) - 2208988800UL;
  tp->tv_nsec = (value % 4096000000UL) * 1000 / 4096;
  return 0;
}
static unsigned char _value(int bit) {
  unsigned long long t0, t1, start;
  int i;
  asm(" la 15,0 \n svc 137\n" ::: "r15", "r6");
  asm(" stckf %0 " : "=m"(start)::);
  start = start >> bit;
  for (i = 0; i < 400; ++i) {
    asm(" la 15,0 \n svc 137\n" ::: "r15", "r6");
    asm(" stckf %0 " : "=m"(t0)::);
    t0 = t0 >> bit;
    if ((t0 - start) > 0xfffff) {
      break;
    }
    t1 ^= t0;
  }
  return (unsigned char)t1;
}

static void _slow(int size, void *output) {
  char *out = (char *)output;
  int i;
#ifdef _LP64
  static int bits = 0;
#else
  int bits = 0;
#endif
  unsigned long long t0, t1, r, m = 0xffff;
  unsigned int zbitcnt[] = {0xffffffff, 0,  1,  26, 2,  23, 27, 0, 3,  16,
                            24,         30, 28, 11, 0,  13, 4,  7, 17, 0,
                            25,         22, 31, 15, 29, 19, 12, 6, 0,  21,
                            14,         9,  5,  20, 8,  19, 18};
  unsigned int t = 0xaa;

  while (bits == 0 || bits > 11) {
    for (i = 0; i < 10; ++i) {
      asm(" stckf %0 " : "=m"(t0)::);
      asm(" stckf %0 " : "=m"(t1)::);
      r = t0 ^ t1;
      if (r < m)
        m = r;
    }
    bits = zbitcnt[(-m & m) % 37];
  }
  for (i = 0; i < size; ++i) {
    t ^= _value(bits);
    out[i] = t;
  }
}

extern "C" int getentropy(void *output, size_t size) {
  char *out = (char *)output;
#ifdef _LP64
  static int feature = -1;
#else
  int feature = -1;
#endif
  typedef struct parm {
    unsigned long long a;
    unsigned long long b;
  } parm_t;

  if (feature == -1) {
    if (0x40 & *(char *)(207)) {
      volatile parm_t value = {0, 0};
      asm(" dc x'b93c008a' \n"
          " jo *-4\n"
          :
          : "NR:r0"(0), "NR:r1"(&value)
          :);
      if (0x2000 & value.b) {
        feature = 1;
        // parm bit 114 is on
      } else {
        feature = 0;
      }
    } else {
      feature = 0;
    }
  }
  if (feature == 0) {
    _slow(size, out);
    return 0;
  }
#ifdef __XPLINK__
  asm(" dc x'b93c00a2' \n"
      " jo *-4\n"
      : "+NR:r2"(out), "+NR:r3"(size)
      : "NR:r0"(114), "NR:r11"(0)
      : "r0");
#else
  asm(" dc x'b93c008a' \n"
      " jo *-4\n"
      : "+NR:r10"(out), "+NR:r11"(size)
      : "NR:r0"(114), "NR:r9"(0)
      : "r0");
#endif
  return 0;
}

extern "C" void __build_version(void) {
  char *V = __getenv_a("V");
  if (V && !memcmp(V, "1", 2)) {
    printf("%s\n", __zoslib_version);
  }
}
extern "C" size_t strnlen(const char *str, size_t maxlen) {
  char *op1 = (char *)str + maxlen;
  asm(" SRST %0,%1\n"
      " jo *-4"
      : "+r"(op1)
      : "r"(str), "NR:r0"(0)
      :);
  return op1 - str;
}
extern "C" void __cpu_relax(__crwa_t *p) {
  // heuristics to avoid excessive CPU spin
  void *r4;
  sched_yield();
  asm(" lgr %0,4" : "=r"(r4)::);
  if (p->sfaddr != r4) {
    p->sfaddr = r4;
    asm(" stckf %0 " : "=m"(p->t0)::);
  } else {
    unsigned long now;
    asm(" stckf %0 " : "=m"(now)::);
    unsigned long ticks = now - p->t0;

    if (ticks < 12288000000UL) {
      if (ticks < 4096)
        ticks = 4096;
      int sec = ticks / 4096000000;
      int msec = (ticks - (sec * 4096000000)) / 4096;
      if (sec)
        sleep(sec);
      if (msec)
        usleep(msec);
    } else {
      sleep(3);
    }
  }
}

extern "C" void __tcp_clear_to_close(int socket, unsigned int secs) {
  struct linger lg;
  lg.l_onoff = 1;
  lg.l_linger = secs;
  shutdown(socket, SHUT_WR);
  int rc =
      setsockopt(socket, SOL_SOCKET, SO_LINGER, (const char *)&lg, sizeof lg);
  if (rc != 0) {
    perror("setsockopt");
    return;
  }
  char buffer[4096];
  int s;
  s = read(socket, buffer, 4096);
  while (s > 1) {
    s = read(socket, buffer, 4096);
  }
}

// Interfaces to call 24/32-bit services
typedef struct thunk24 {
  unsigned short _force_address_align;
  unsigned short sam24;
  unsigned short basr;
  unsigned short sam64;
  unsigned short loadr14[3];
  unsigned short br14;
  void *braddr;
  unsigned int dsa[18];
  unsigned int upperhalf[15];
} thunk24_t;

typedef struct loadmod {
  char modname[8];
  unsigned load_r1;
  unsigned load_r15;
  thunk24_t *thptr;
  void *reg15;
  void *reg1;
  void *reg13;
  unsigned int dsa[18];
  unsigned int upperhalf[15];
  char *_1[32];
} loadmod_t;

extern void __unloadmod(void *mod) {
  loadmod_t *m = (loadmod_t *)mod;
  if (!m)
    return;
  __asm(" lgr 0,%1\n"
        " svc 9\n"
        " st 15,%0\n"
        : "=m"(m->load_r15)
        : "r"(m->modname)
        : "r0", "r1", "r15");
  if (m->thptr)
    free(m->thptr);
  free(m);
}

extern void *__loadmod(const char *name) {
  loadmod_t *m = (loadmod_t *)__malloc31(sizeof(loadmod_t));
  if (!m)
    return 0; // fail to allocate
  memset(m, 0, sizeof(loadmod_t));
  size_t len = strlen(name);
  if (len > 8)
    len = 8;
  memcpy(m->modname, name, len);
  if (len < 8) {
    memset(len + m->modname, 0x40, 8 - len);
  }
  m->load_r1 = 0x80000000;
  __asm(" lgr 0,%3\n"
        " l  1,%1\n"
        " svc 8\n"
        " stg 0,%0\n"
        " st 1,%1\n"
        " st 15,%2\n"
        : "=m"(m->reg15), "+m"(m->load_r1), "=m"(m->load_r15)
        : "r"(m->modname)
        : "r0", "r1", "r15");
  if (m->load_r15) {
    free(m);
    return 0;
  }
  if ((unsigned long)m->reg15 & 0x0000000080000000UL) {
    // module is amode-31
    m->reg13 = m->dsa;
  } else {
    // module is amode-24
    thunk24_t *t24 = (thunk24_t *)__malloc24(sizeof(thunk24_t));
    if (!t24) {
      // fail to allocate
      __unloadmod(m);
      return 0;
    }
    m->thptr = t24;
    t24->sam24 = 0x010c;      // sam 24
    t24->basr = 0x0def;       // basr 14,15
    t24->sam64 = 0x010e;      // sam64
    t24->loadr14[0] = 0xc4e8; // lgrl 14,+8  64 bit branch back
    t24->loadr14[1] = 0x0000;
    t24->loadr14[2] =
        (offsetof(thunk24_t, braddr) - offsetof(thunk24_t, loadr14)) / 2;
    t24->br14 = 0x07fe;
    m->reg13 = t24->dsa;
  }
  return m;
}

// FIXME: noinline is specified, otherwise we get an error: No active USING for
// operand H3090
__attribute__((noinline)) extern long __callmod(void *mod, void *plist) {
  loadmod_t *m = (loadmod_t *)mod;
  long rc;
  if (!mod)
    return -1;
  m->reg1 = plist;
  if (m->thptr) {
    // amode 24 call
    __asm(" BASR 14,0 \n"
          " USING *,14 \n"
          " LG 1,%3 \n"
          " LG 13,%4 \n"
          " LG 15,%5 \n"
          " LA 14,H3090 \n" // get the branch back address
          " DROP 14 \n"
          " STG 14,%1 \n" // store in braddr
          " LGR 14,%2 \n" // load r4 to point to SAM24 instruction
          " STMH 14,12,72(13)\n"
          " BR 14 \n"                 // branch to thunk
          "H3090 LMH  14,12,72(13)\n" // just in case program mess with the
                                      // upper half of registers.
          " LGR %0,15 \n"
          : "=r"(rc), "=m"(m->thptr->braddr)
          : "r"(&(m->thptr->sam24)), "m"(m->reg1), "m"(m->reg13), "m"(m->reg15)
          : "r1", "r13", "r14", "r15");
  } else {
    // amode 31 call
    __asm(" LG 1,%1 \n"
          " LG 13,%2 \n"
          " LG 15,%3 \n"
          " SAM31 \n"
          " STMH 14,12,72(13)\n"
          " BASR 14,15 \n"
          " LMH  14,12,72(13)\n" // just in case program mess with the upper
                                 // half of registers
          " SAM64 \n"
          " LGR %0,15 \n"
          : "=r"(rc)
          : "m"(m->reg1), "m"(m->reg13), "m"(m->reg15)
          : "r1", "r13", "r14", "r15");
  }
  return rc;
}

// IFAED Interfaces
/*  Type for TYPE operand of IFAEDREG                                */
typedef int IfaedType;

/*  Type for Product Owner                                           */
typedef char IfaedProdOwner[16];

/*  Type for Product Name                                            */
typedef char IfaedProdName[16];

/*  Type for Feature Name                                            */
typedef char IfaedFeatureName[16];

/*  Type for Product Version                                         */
typedef char IfaedProdVers[2];

/*  Type for Product Release                                         */
typedef char IfaedProdRel[2];

/*  Type for Product Modification level                              */
typedef char IfaedProdMod[2];

/*  Type for Product ID                                              */
typedef char IfaedProdID[8];

/*  Type for Product Token                                           */
typedef char IfaedProdToken[8];

/*  Type for Features Length                                         */
typedef int IfaedFeaturesLen;

/*  Type for Return Code                                             */
typedef int IfaedReturnCode;

/*  Type for user supplied EDOI                                      */
typedef struct {
  struct {
    int EdoiRegistered : 1;             /* The product is registered    */
    int EdoiStatusNotDefined : 1;       /* The product is not known to
                             be enabled or disabled                     */
    int EdoiStatusEnabled : 1;          /* The product is enabled       */
    int EdoiNotAllFeaturesReturned : 1; /* The featureslen
                       area was too small to hold the features
                       provided at registration time. Field
                       EdoiNeededFeaturesLen contains the size
                       provided at registration time.              */
    int Rsvd0 : 4;                      /* Reserved                     */
  } EdoiFlags;
  char Rsvd1[3];             /* Reserved                            */
  int EdoiNeededFeaturesLen; /* The featureslen size provided at
                                 registration time                  */
  struct {
    IfaedProdVers EdoiProdVers; /* The version information
                  provided at registration time                 */
    IfaedProdRel EdoiProdRel;   /* The release information
                  provided at registration time                 */
    IfaedProdMod EdoiProdMod;   /* The mod level information
                  provided at registration time                 */
  } EdoiProdVersRelMod;
  char Rsvd[2]; /* Reserved                            */
} EDOI;

typedef struct IFAEDSTA_parms {
  void *__ptr32 args[8];
  char cpo[16];    // PRODUCT OWNER
  char cpnpp[16];  // PRODUCT NAME
  char cfnpp[16];  // FEATURE NAME
  char cpidpp[16]; // PID
  int coinfo[4];
  int cflpp;
  int cfspp[256];
  int crcpp;
} IFAEDSTA_parms_t;

typedef struct IFAARGS {
  int prefix;
  char id[8];
  short listlen;
  char version;
  char request;
  char prodowner[16];
  char prodname[16];
  char prodvers[8];
  char prodqual[8];
  char prodid[8];
  char domain;
  char scope;
  char rsv0001;
  char flags;
  char *__ptr32 prtoken_addr;
  char *__ptr32 begtime_addr;
  char *__ptr32 data_addr;
  char xformat;
  char rsv0002[3];
  char *__ptr32 currentdata_addr;
  char *__ptr32 enddata_addr;
  char *__ptr32 endtime_addr;
} IFAARGS_t;

#pragma convert("IBM-1047")
const char *MODULE_REGISTER_USAGE = "IFAUSAGE";
#pragma convert(pop)

const char *IFAUsageErrorStrings[] = {
    /*RC=0*/
    NULL,
    /*RC=1*/
    "SYSTEM MANAGEMENT FACILITIES (SMF) is not present on the system."
    /*RC=2*/
    "SYSTEM MANAGEMENT FACILITIES (SMF) Usage Collection "
    "Services is not active.",
    /*RC=3*/
    NULL,
    /*RC=4*/
    "Another product has already registered under the TASK domain."
    " IFAUSAGE will record the data for each product.",
    /*RC=5, RC=6, RC=7*/
    NULL, NULL, NULL,
    /*RC=8*/
    "IFAUSAGE could not process more than two problem state program"
    " invocations of REQUEST=REGISTER for the TASK domain.",
    /*RC=9, RC=10, RC=11*/
    NULL, NULL, NULL,
    /*RC=12*/
    "You specified a token on the PRTOKEN parameter that the system"
    " cannot identify.",
    /*RC=13, RC=14, RC=15*/
    NULL, NULL, NULL,
    /*RC=16*/
    "IFAUSAGE cannot complete processing because SMF usage processing"
    " is not available on the system."};

const char *getIFAUsageErrorString(unsigned long rc) {
  if (rc >= (sizeof(IFAUsageErrorStrings) / sizeof(IFAUsageErrorStrings[0])))
    return NULL;
  return IFAUsageErrorStrings[rc];
}

unsigned long long __registerProduct(const char *major_version,
                                     const char *product_owner,
                                     const char *feature_name,
                                     const char *product_name,
                                     const char *pid) {
  // Check if SMF is Active first
  char *xx = ((char *__ptr32 *__ptr32 *)0)[4][49];
  if (0 == xx) {
    return 1;
  }
  if (0 == (*xx & 0x04)) {
    return 2;
  }

  // Creates buffers for registration product info
  char str_product_owner[17];
  char str_feature_name[17];
  char str_product_name[17];
  char str_pid[9];
  char version[9];

  // Left justify with space padding and convert to ebcdic
  snprintf(str_product_owner, sizeof(str_product_owner), "%-16s",
           product_owner);
  __a2e_s(str_product_owner);
  snprintf(str_feature_name, sizeof(str_feature_name), "%-16s", feature_name);
  __a2e_s(str_feature_name);
  snprintf(str_product_name, sizeof(str_product_name), "%-16s", product_name);
  __a2e_s(str_product_name);
  snprintf(str_pid, sizeof(str_pid), "%-8s", pid);
  __a2e_s(str_pid);
  snprintf(version, sizeof(version), "%-8s", major_version);
  __a2e_s(version);

  // Register Product with IFAUSAGE
  IFAARGS_t *arg = (IFAARGS_t *)__malloc31(sizeof(IFAARGS_t));
  assert(arg);
  memset(arg, 0, sizeof(IFAARGS_t));
  memcpy(arg->id, MODULE_REGISTER_USAGE, 8);
  arg->listlen = sizeof(IFAARGS_t);
  arg->version = 1;
  arg->request = 1; // 1=REGISTER

  // Insert properties
  memcpy(arg->prodowner, str_product_owner, 16);
  memcpy(arg->prodname, str_product_name, 16);
  memcpy(arg->prodvers, version, 8);
#pragma convert("IBM-1047")
  memcpy(arg->prodqual, "NONE    ", 8);
#pragma convert(pop)
  memcpy(arg->prodid, str_pid, 8);
  arg->domain = 1;
  arg->scope = 1;
  unsigned long long ifausage_rc = 0xFFFFFFFFFFFFFFFF;

  arg->prtoken_addr = (char *__ptr32)__malloc31(sizeof(char *__ptr32));
  arg->begtime_addr = (char *__ptr32)__malloc31(sizeof(char *__ptr32));

  // Load 25 (IFAUSAGE) into reg15 and call via SVC
  asm(" svc 109\n" : "=NR:r15"(ifausage_rc) : "NR:r1"(arg), "NR:r15"(25) :);

  free(arg);

  return ifausage_rc;
}

extern "C" void *roanon_mmap(void *_, size_t len, int prot, int flags,
                             const char *filename, int fildes, off_t off) {
  // TODO(gabylb): read-only anonymous mmap: the anon_mmap() call below is used
  // rather than the OS's mmap() because mmap() doesn't convert .js with EBCDIC
  // content to ASCII (in both tagged and untagged files).
  // This function is currently called only by d8, and has been tested only
  // when d8 processes its .js arg (from OS::MemoryMappedFile::open()), hence
  // the first check below)
  // With this, .js with either EBCDIC or ASCII, tagged or untagged can be
  // processed; without it, d8 could not process .js with EBCDIC content
  // (tagged and untagged).

  if (prot != PROT_READ || flags == MAP_SHARED) {
    return mmap(_, len, prot, flags, fildes, off);
  }
  struct stat st;
  if (fstat(fildes, &st)) {
    perror("fstat");
    return nullptr;
  }
  static const int pgsize = sysconf(_SC_PAGESIZE);
  size_t size = __round_up(len, pgsize);

  void *memory = anon_mmap(_, size);
  if (memory == MAP_FAILED) {
    return memory;
  }
  if (lseek(fildes, 0, SEEK_SET) != 0) {
    perror("lseek");
    return nullptr;
  }
  size_t nread = read(fildes, memory, len);
  if (nread != len) {
    perror("read");
    return nullptr;
  }
  if (st.st_tag.ft_txtflag == 0 && st.st_tag.ft_ccsid == 0) {
    __file_needs_conversion_init(filename, fildes);
    if (__file_needs_conversion(fildes)) {
      __e2a_l((char *)memory, len);
    }
  }
  return memory;
}

int __print_zoslib_help(FILE *fp, const char *title) {
  __zinit *zinit_ptr = __zinit::getInstance();

  if (!zinit_ptr)
    return -1;

  if (fp == NULL)
    return -2;

  if (!title)
    fprintf(fp, "%s\n", "ZOSLIB Environment Variables");
  else
    fprintf(fp, "%s\n", title);

  for (auto envarMap : zinit_ptr->envarHelpMap) {
    std::stringstream ss;
    ss << envarMap.first.envarName;

    if (envarMap.first.envarValue != "")
      ss << "=" << envarMap.first.envarValue;

    fprintf(fp, "%-34s %s\n", ss.str().c_str(), envarMap.second.c_str());
  }

  return 0;
}

int __update_envar_settings(const char *envar) {
  __zinit *zinit_ptr = __zinit::getInstance();
  if (!zinit_ptr)
    return -1;

  // Exit early if envar is not a ZOSLIB envar
  if (envar && !zinit_ptr->isValidZOSLIBEnvar(envar)) {
    if (__debug_mode)
      dprintf(2,
              "__update_envar_settings(): \"%s\" is not a valid zoslib envar\n",
              envar);
    return -1;
  }

  bool force_update_all = envar == NULL;

  zoslib_config_t &config = zinit_ptr->config;

  if (force_update_all || strcmp(envar, config.IPC_CLEANUP_ENVAR) == 0) {
    char *cu = __getenv_a(config.IPC_CLEANUP_ENVAR);
    if (cu && !memcmp(cu, "1", 2)) {
      __cleanupipc(1);
    }
  }

  if (force_update_all || strcmp(envar, config.DEBUG_ENVAR) == 0) {
    char *dbg = __getenv_a(config.DEBUG_ENVAR);
    if (!dbg) {
      __debug_mode = 0;
    } else if (!memcmp(dbg, "1", 2)) {
      __debug_mode = 1;
    }
  }

  if (force_update_all || strcmp(envar, config.RUNTIME_LIMIT_ENVAR) == 0) {
    char *tl = __getenv_a(config.RUNTIME_LIMIT_ENVAR);
    if (!tl) {
      // Kill timer thread
      pthread_join(_timer_tid, NULL);
    } else {
      int sec = __atoi_a(tl);
      if (sec > 0) {
        __settimelimit(sec);
      }
    }
  }

  if (force_update_all ||
      strcmp(envar, config.CCSID_GUESS_BUF_SIZE_ENVAR) == 0) {
    char *cgbs = __getenv_a(config.CCSID_GUESS_BUF_SIZE_ENVAR);
    if (!cgbs) {
      __set_ccsid_guess_buf_size(4096);
    } else {
      int gs = __atoi_a(cgbs);
      if (gs > 0)
        __set_ccsid_guess_buf_size(gs);
    }
  }

  if (force_update_all || strcmp(envar, config.FORKMAX_ENVAR) == 0) {
    char *fm = __getenv_a(config.FORKMAX_ENVAR);
    if (!fm) {
      if (0 != zinit_ptr->forkmax && 0 != zinit_ptr->shmid) {
        zinit_ptr->forkmax = 0;
        shmdt(zinit_ptr->forkcurr);
        shmctl(zinit_ptr->shmid, IPC_RMID, 0);
      }
    } else {
      int v = __atoi_a(fm);
      if (v > 0) {
        zinit_ptr->forkmax = v;
        char path[1024];
        if (0 == getcwd(path, sizeof(path)))
          strcpy(path, "./");
        key_t key = ftok(path, 9021);
        zinit_ptr->shmid = shmget(key, 1024, 0666 | IPC_CREAT);
        zinit_ptr->forkcurr = (int *)shmat(zinit_ptr->shmid, (void *)0, 0);
        *(zinit_ptr->forkcurr) = 0;
      }
    }
  }

  if (force_update_all || strcmp(envar, config.UNTAGGED_READ_MODE_ENVAR) == 0) {
    no_tag_read_behaviour =
        get_no_tag_read_behaviour(config.UNTAGGED_READ_MODE_ENVAR);
  }

  if (force_update_all ||
      strcmp(envar, config.UNTAGGED_READ_MODE_CCSID1047_ENVAR) == 0) {
    no_tag_ignore_ccsid1047 =
        get_no_tag_ignore_ccsid1047(config.UNTAGGED_READ_MODE_CCSID1047_ENVAR);
  }
  return 0;
}

extern "C" int __update_envar_names(zoslib_config_t *const config) {
  __zinit *zinit_ptr = __zinit::getInstance();
  if (!zinit_ptr)
    return -1;

  zoslib_config_t &cur_config = zinit_ptr->config;
  memcpy(&cur_config, config, sizeof(*config));

  return zinit_ptr->setEnvarHelpMap();
}

void *__iterate_stack_and_get(void *dsaptr, __stack_info *si) {
  /* Eyecatcher .C.E.E.1 */
  static const char XPLINK_EYECATCHER[] = {0x00, 0xC3, 0x00, 0xC5,
                                           0x00, 0xC5, 0x00, 0xF1};

  // Level 4 (version 3) layout
  static const char PPA1_EYECATCHER[] = {0x02, 0xCE};

  typedef struct routine_layout {
    const char XPLINK_LAYOUT_EYECATCHER[8];
    int ppa1_offset;
    int layout_flags;
  } routine_layout;

  typedef struct ppa1_layout {
    const char PPA1_LAYOUT_EYECATCHER[2];
    short int gpr_mask;
    int ppa2_offset;
    char flag1;
    char flag2;
    char flag3;
    char flag4;
    int misc_fields;
    int code_lth;
  } ppa1_layout;

  typedef struct dsa_layout {
    char misc[2048];
    long int savearea_backchain;
    long int savearea_environment;
    long int savearea_entry;
    long int savearea_return;
  } dsa_layout;

  int dsa_format = __EDCWCCWI_DOWN;
  int prev_fmt;
  int *prev_fmt_p = &prev_fmt;
  void **ph_callee_dsa_p = NULL;
  int ph_callee_dsa_fmt;
  void *new_dsaptr = NULL;
  short int epname_length;
  void *entry_ptr;
  int *ppa1off_ptr;
  int ppa1_offset;
  char FLAG3;
  char FLAG4;
  int offset_lth;

  ppa1_layout *ppa1_ptr;
  routine_layout *layout_ptr;

  {
    std::mutex access_lock;
    std::lock_guard<std::mutex> lock(access_lock);
    errno = 0;
    new_dsaptr = __dsa_prev(dsaptr, __EDCWCCWI_PHYSICAL, dsa_format, NULL, NULL,
                            prev_fmt_p, ph_callee_dsa_p, &ph_callee_dsa_fmt);

    if (errno == EINVAL || errno == ESRCH || errno == EACCES ||
        new_dsaptr == NULL)
      return 0;
  }

  dsa_format = prev_fmt;
  entry_ptr = __ep_find(new_dsaptr, dsa_format, NULL);

  if (entry_ptr == 0)
    return 0;

  si->prev_dsa = new_dsaptr;
  si->entry_point = (void *)entry_ptr;
  si->return_addr = (int *)((dsa_layout *)new_dsaptr)->savearea_return;
  si->entry_addr = (int *)((dsa_layout *)new_dsaptr)->savearea_entry;
  si->stack_addr = (int *)((dsa_layout *)new_dsaptr)->savearea_backchain - 2048;
  layout_ptr =
      (routine_layout *)((unsigned long)entry_ptr - sizeof(routine_layout));
  ppa1_ptr =
      (ppa1_layout *)((unsigned long)layout_ptr + layout_ptr->ppa1_offset);

  if (memcmp(&XPLINK_EYECATCHER, &layout_ptr->XPLINK_LAYOUT_EYECATCHER, 8) !=
      0) {
    strcpy(si->entry_name,
           "**Module not traced, not a XPLINK routine layout**");
    return new_dsaptr;
  }
  /* PPA1: Entry Point Block for XPLINK (Version 3)
      is the only block we are currently checking.
  */
  if (memcmp(&PPA1_EYECATCHER, &ppa1_ptr->PPA1_LAYOUT_EYECATCHER, 2) != 0) {
    strcpy(si->entry_name,
           "**Module not traced, PPA1 format not currently traced**");
    return new_dsaptr;
  }

  /* Calculate the offset from the beginning of PPA1 to the name length
   * field */
  if (!(ppa1_ptr->flag4 & 0x01)) {
    strcpy(si->entry_name,
           "**Module not traced, name field not included in PPA1**");
    return new_dsaptr;
  }
  memcpy(&FLAG3, &ppa1_ptr->flag3, 1);
  offset_lth = sizeof(ppa1_layout); /* Length of fixed mandatory fields */
  if (FLAG3 & 0x80) {
    offset_lth = offset_lth + 4;
  }
  if (FLAG3 & 0x40) {
    offset_lth = offset_lth + 4;
  }
  if (FLAG3 & 0x20) {
    offset_lth = offset_lth + 4;
  }
  if (FLAG3 & 0x10) {
    offset_lth = offset_lth + 4;
  }
  if (FLAG3 & 0x08) {
    offset_lth = offset_lth + 4;
  }
  if (FLAG3 & 0x04) {
    offset_lth = offset_lth + 4;
  }
  if (FLAG3 & 0x02) {
    offset_lth = offset_lth + 4;
  }
  if (FLAG3 & 0x01) {
    offset_lth = offset_lth + 8;
  }

  memcpy(&epname_length, (void *)((long)ppa1_ptr + offset_lth), 2);
  epname_length = MIN(epname_length, sizeof(si->entry_name));
  si->entry_name[epname_length] = 0;
  strncpy((char *)si->entry_name,
          (char *)((long)ppa1_ptr + offset_lth + sizeof(epname_length)),
          epname_length);
  __e2a_l(si->entry_name, epname_length);
  return new_dsaptr;
}

int *__get_stack_start() {
  if (gettid() == 1 && __main_thread_stack_top_address != 0)
    return __main_thread_stack_top_address;

  __stack_info si;
  void *cur_dsa = dsa();

  while (__iterate_stack_and_get(cur_dsa, &si) != 0) {
    cur_dsa = si.prev_dsa;

    if ((gettid() == 1 && strcmp(si.entry_name, "CELQINIT") == 0) ||
        strcmp(si.entry_name, "CELQPCMM") == 0)
      return si.stack_addr;
  }
  return nullptr;
}

bool __zinit::isValidZOSLIBEnvar(std::string envar) {
  return std::find_if(
             envarHelpMap.begin(), envarHelpMap.end(),
             [&envar](std::pair<zoslibEnvar, std::string> const &item) {
               return (envar == item.first.envarName);
             }) != envarHelpMap.end();
}

__zinit::__zinit(const zoslib_config_t &config)
    : forkmax(0), shmid(0), __forked(0) {
  memcpy(&this->config, &config, sizeof(config));
}

int __zinit::initialize() {
  mode = __ae_thread_swapmode(__AE_ASCII_MODE);
  cvstate = __ae_autoconvert_state(_CVTSTATE_QUERY);
  if (_CVTSTATE_OFF == cvstate) {
    __ae_autoconvert_state(_CVTSTATE_ON);
  }

  __main_thread_stack_top_address = __get_stack_start();

  if (setEnvarHelpMap() != 0)
    return -1;

  char *tenv = getenv("_EDC_SIG_DFLT");
  if (!tenv || !*tenv) {
    setenv("_EDC_SIG_DFLT", "1", 1);
  }

  tenv = getenv("_EDC_SUSV3");
  if (!tenv || !*tenv) {
    setenv("_EDC_SUSV3", "1", 1);
  }

  _th = std::get_terminate();
  std::set_terminate(abort);
  return 0;
}

int __zinit::setEnvarHelpMap() {
  // Populate ZOSLIB Envars and help text

  envarHelpMap.clear();

  envarHelpMap.insert(std::make_pair(
      zoslibEnvar(config.UNTAGGED_READ_MODE_ENVAR, std::string("NO")),
      "changes the __UNTAGGED_READ_MODE behavior to ignore files tagged with "
      "CCSID 1047 and txtflag turned off"));

  envarHelpMap.insert(std::make_pair(
      zoslibEnvar(config.UNTAGGED_READ_MODE_ENVAR, std::string("AUTO")),
      "(default) for handling of reading untagged files or files tagged with "
      "CCSID 1047 and txtflag turned off, up to 4k of data"
      "will be read and checked, if it is found to be in CCSID 1047, data is "
      "converted from CCSID 1047 to CCSID 819"));

  envarHelpMap.insert(std::make_pair(
      zoslibEnvar(config.UNTAGGED_READ_MODE_ENVAR, std::string("STRICT")),
      "for no explicit conversion of data"));

  envarHelpMap.insert(std::make_pair(
      zoslibEnvar(config.UNTAGGED_READ_MODE_ENVAR, std::string("V6")),
      "for same behavior as Node.js for z/OS V6/V8, i.e. always convert data "
      "from CCSID 1047 to CCSID 819"));

  envarHelpMap.insert(std::make_pair(
      zoslibEnvar(config.UNTAGGED_READ_MODE_ENVAR, std::string("WARN")),
      "for same behavior as \"AUTO\" but issue a warning if conversion "
      "occurs"));

  envarHelpMap.insert(std::make_pair(
      zoslibEnvar(config.CCSID_GUESS_BUF_SIZE_ENVAR, std::string("")),
      "number of bytes to scan for CCSID guess heuristics (default: 4096)"));

  envarHelpMap.insert(
      std::make_pair(zoslibEnvar(config.FORKMAX_ENVAR, std::string("")),
                     "set to indicate max number of forks"));

  envarHelpMap.insert(
      std::make_pair(zoslibEnvar(config.IPC_CLEANUP_ENVAR, std::string("")),
                     "set to toggle IPC cleanup"));

  envarHelpMap.insert(
      std::make_pair(zoslibEnvar(config.DEBUG_ENVAR, std::string("")),
                     "set to toggle debug ZOSLIB mode"));

  envarHelpMap.insert(
      std::make_pair(zoslibEnvar(config.RUNTIME_LIMIT_ENVAR, std::string("")),
                     "number of seconds to run before zoslib raises a SIGABRT "
                     "signal to terminate"));

  return __update_envar_settings(NULL);
}

__zinit *__zinit::instance = 0;

void init_zoslib_config(zoslib_config_t &config) {
  init_zoslib_config(&config);
}

extern "C" void init_zoslib_config(zoslib_config_t *const config) {
  config->IPC_CLEANUP_ENVAR = IPC_CLEANUP_ENVAR_DEFAULT;
  config->DEBUG_ENVAR = DEBUG_ENVAR_DEFAULT;
  config->RUNTIME_LIMIT_ENVAR = RUNTIME_LIMIT_ENVAR_DEFAULT;
  config->FORKMAX_ENVAR = FORKMAX_ENVAR_DEFAULT;
  config->CCSID_GUESS_BUF_SIZE_ENVAR = CCSID_GUESS_BUF_SIZE_DEFAULT;
  config->UNTAGGED_READ_MODE_ENVAR = UNTAGGED_READ_MODE_DEFAULT;
  config->UNTAGGED_READ_MODE_CCSID1047_ENVAR =
      UNTAGGED_READ_MODE_CCSID1047_DEFAULT;
}

extern "C" void init_zoslib(const zoslib_config_t config) {
  __init_zoslib __nodezoslib(config);
}

extern "C" int nanosleep(const struct timespec *req, struct timespec *rem) {
  unsigned secrem;
  unsigned nanorem;
  int rv;
  int err;

  rv = __cond_timed_wait((unsigned int)req->tv_sec, (unsigned int)req->tv_nsec,
                         (unsigned int)(CW_CONDVAR | CW_INTRPT), &secrem,
                         &nanorem);
  err = errno;

  if (rem != NULL && (rv == 0 || err == EINTR)) {
    rem->tv_nsec = nanorem;
    rem->tv_sec = secrem;
  }

  /* Don't clobber errno unless __cond_timed_wait() errored.
   * Don't leak EAGAIN, that just means the timeout expired.
   */
  if (rv == -1 && err == EAGAIN) {
    errno = 0;
    rv = 0;
  }

  return rv;
}

extern "C" int __lutimes(const char *filename, const struct timeval tv[2]) {
  int return_value;
  int return_code;
  int reason_code;

  __bpxyatt_t attributes;
  memset(&attributes, 0, sizeof(attributes));
  memcpy(attributes.att_id, "\xC1\xE3\xE3\x40", 4); // "ATT ".
  attributes.att_version = 3;
  attributes.att_atimechg = 1;
  attributes.att_atime = tv[0].tv_sec;
  attributes.att_mtimechg = 1;
  attributes.att_mtime = tv[1].tv_sec;

  // ZOSLIB is built in ASCII, but BPX4LCR wants EBCDIC.
  char *pathname = _str_a2e(filename);

  __bpx4lcr(strlen(pathname), pathname, sizeof(attributes), &attributes,
            &return_value, &return_code, &reason_code);

  if (return_value != 0) {
    errno = return_code;
    return -1;
  }

  return 0;
}
