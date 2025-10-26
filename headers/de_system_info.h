#ifndef DE_SYSTEM_INFO_HEADER
#define DE_SYSTEM_INFO_HEADER
#ifdef __cplusplus
extern "C" {
#endif

/*
to get function definitions #define DE_SYSTEM_INFO_IMPLEMENTATION before the #include
*/

/* possible options to set before 'first' include */
#ifndef DE_SYSTEM_INFO_OPTIONS
#ifdef DE_SYSTEM_INFO_OPTIONS
/* no options */
#endif
#endif

#ifdef DE_SYSTEM_INFO_IMPLEMENTATION
#define DE_SYSTEM_INFO_API
#else
#define DE_SYSTEM_INFO_API extern
#endif

#define DE_SYSTEM_INFO_INTERNAL

/* Tunable sizes for fixed buffers inside the struct */
#define SI_STRLEN 128
#define SI_HOSTLEN 64
/* clang-format off */
/* declarations */

typedef struct SystemInfo {
  char os_name[SI_STRLEN];        /* OS kernel/name (e.g., "Linux", "Windows") */
  char os_version[SI_STRLEN];     /* Version string if available */
  char architecture[SI_STRLEN];   /* e.g., "x86_64", "arm64" */
  char cpu_model[SI_STRLEN];      /* CPU model string (best-effort) */
  char hostname[SI_HOSTLEN];      /* Hostname */

  unsigned long logical_cpus;     /* number of logical processors */
  unsigned long physical_cpus;    /* best-effort - 0 if unknown */

  unsigned long long total_ram;   /* bytes (0 if unknown) */
  unsigned long long avail_ram;   /* bytes (0 if unknown) */
  unsigned long page_size;        /* bytes */

  double loadavg_1;               /* POSIX load averages (or -1 if not available) */
  double loadavg_5;
  double loadavg_15;

  unsigned long long uptime_seconds; /* best-effort (0 if unknown) */

  /* Flags / metadata */
  int is_windows;                 /* 1 for Windows, 0 for POSIX */
} SystemInfo;

/* Forward declaration */
int get_system_information(SystemInfo *out);

/* clang-format on */
#ifdef __cplusplus
} // extern "C"
#endif

#endif

// #define DE_SYSTEM_INFO_IMPLEMENTATION_DEVELOPMENT
#if defined(DE_SYSTEM_INFO_IMPLEMENTATION) || defined(DE_SYSTEM_INFO_IMPLEMENTATION_DEVELOPMENT)
#ifndef DE_SYSTEM_INFO_IMPLEMENTATION_INTERNAL
#define DE_SYSTEM_INFO_IMPLEMENTATION_INTERNAL
#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <sysinfoapi.h> /* for GetTickCount64 on older headers */
#else
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef __linux__
#include <sys/sysinfo.h>
#endif
#if defined(__APPLE__) || defined(__MACH__)
#include <sys/sysctl.h>
#endif
#endif

/* implementations */
#ifdef _WIN32

static void safe_strncpy(char *dst, const char *src, size_t dstlen) {
  if (dstlen == 0) return;
  strncpy(dst, src ? src : "", dstlen - 1);
  dst[dstlen - 1] = '\0';
}

int get_system_information(SystemInfo *out) {
  if (!out) return -1;
  memset(out, 0, sizeof(*out));
  out->is_windows = 1;

  /* OS name */
  safe_strncpy(out->os_name, "Windows", SI_STRLEN);

  /* Architecture */
  SYSTEM_INFO si;
  ZeroMemory(&si, sizeof(si));
  GetNativeSystemInfo(&si);
  switch (si.wProcessorArchitecture) {
  case PROCESSOR_ARCHITECTURE_AMD64: safe_strncpy(out->architecture, "x86_64", SI_STRLEN); break;
  case PROCESSOR_ARCHITECTURE_INTEL: safe_strncpy(out->architecture, "x86", SI_STRLEN); break;
  case PROCESSOR_ARCHITECTURE_ARM64: safe_strncpy(out->architecture, "arm64", SI_STRLEN); break;
  case PROCESSOR_ARCHITECTURE_ARM: safe_strncpy(out->architecture, "arm", SI_STRLEN); break;
  default: safe_strncpy(out->architecture, "unknown", SI_STRLEN); break;
  }

  /* Logical CPUs */
  DWORD_PTR processMask = 0, systemMask = 0;
  out->logical_cpus = (unsigned long)GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
  out->physical_cpus = 0; /* retrieving physical count cross-Windows-version safe is complex; leave 0 */

  /* Memory */
  MEMORYSTATUSEX ms;
  ms.dwLength = sizeof(ms);
  if (GlobalMemoryStatusEx(&ms)) {
    out->total_ram = ms.ullTotalPhys;
    out->avail_ram = ms.ullAvailPhys;
  }

  /* Page size from SYSTEM_INFO */
  out->page_size = si.dwPageSize ? si.dwPageSize : 0;

  /* Hostname */
  char hostbuf[SI_HOSTLEN];
  DWORD hostlen = SI_HOSTLEN;
  if (GetComputerNameA(hostbuf, &hostlen)) { safe_strncpy(out->hostname, hostbuf, SI_HOSTLEN); }

  /* Uptime - GetTickCount64 returns milliseconds since boot */
  ULONGLONG ms_since_boot = GetTickCount64();
  out->uptime_seconds = (unsigned long long)(ms_since_boot / 1000ULL);

  /* OS version (best-effort). GetVersionEx is deprecated but usable for simple info.
     For production code you might query the registry or use RtlGetVersion via ntdll. */
  {
    OSVERSIONINFOEXA osvi;
    ZeroMemory(&osvi, sizeof(osvi));
    osvi.dwOSVersionInfoSize = sizeof(osvi);
    if (GetVersionExA((LPOSVERSIONINFOA)&osvi)) {
      snprintf(out->os_version, SI_STRLEN, "%lu.%lu (build %lu)", (unsigned long)osvi.dwMajorVersion,
               (unsigned long)osvi.dwMinorVersion, (unsigned long)osvi.dwBuildNumber);
    } else {
      safe_strncpy(out->os_version, "unknown", SI_STRLEN);
    }
  }

  /* CPU model string: use registry query as a fallback - keep it simple and use environment info */
  {
    HKEY hKey;
    LONG res =
        RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0", 0, KEY_READ, &hKey);
    if (res == ERROR_SUCCESS) {
      char buf[SI_STRLEN];
      DWORD buflen = sizeof(buf);
      DWORD type = 0;
      if (RegQueryValueExA(hKey, "ProcessorNameString", NULL, &type, (LPBYTE)buf, &buflen) == ERROR_SUCCESS) {
        safe_strncpy(out->cpu_model, buf, SI_STRLEN);
      }
      RegCloseKey(hKey);
    }
  }

  /* Load averages not available on Windows; set to -1 */
  out->loadavg_1 = out->loadavg_5 = out->loadavg_15 = -1.0;

  return 0;
}

#else /* POSIX implementation */

static void safe_strncpy(char *dst, const char *src, size_t dstlen) {
  if (dstlen == 0) return;
  strncpy(dst, src ? src : "", dstlen - 1);
  dst[dstlen - 1] = '\0';
}

static unsigned long get_logical_cpus_posix(void) {
  long n = sysconf(_SC_NPROCESSORS_ONLN);
  if (n < 1) return 1;
  return (unsigned long)n;
}

/* Try to get physical CPU count on Linux from /proc/cpuinfo (best-effort) */
static unsigned long get_physical_cpus_linux(void) {
#ifdef __linux__
  FILE *f = fopen("/proc/cpuinfo", "r");
  if (!f) return 0;
  char line[512];
  unsigned long physical_ids[256];
  size_t phys_count = 0;
  int current_physical = -1;
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "physical id", 11) == 0) {
      int id;
      if (sscanf(line, "physical id\t: %d", &id) == 1) {
        int found = 0;
        for (size_t i = 0; i < phys_count; ++i) {
          if (physical_ids[i] == (unsigned long)id) {
            found = 1;
            break;
          }
        }
        if (!found && phys_count < sizeof(physical_ids) / sizeof(physical_ids[0])) {
          physical_ids[phys_count++] = (unsigned long)id;
        }
      }
    }
  }
  fclose(f);
  if (phys_count > 0) return phys_count;
#endif
  return 0;
}

/* Try to read a short CPU model string from /proc/cpuinfo on Linux (best-effort) */
static void get_cpu_model_linux(char *outbuf, size_t outlen) {
  if (!outbuf || outlen == 0) return;
#ifdef __linux__
  FILE *f = fopen("/proc/cpuinfo", "r");
  if (!f) {
    safe_strncpy(outbuf, "unknown", outlen);
    return;
  }
  char line[512];
  while (fgets(line, sizeof(line), f)) {
    if (strncmp(line, "model name", 10) == 0) {
      /* line like: model name  : Intel(R) ... */
      char *p = strchr(line, ':');
      if (p) {
        ++p;
        while (*p == ' ' || *p == '\t')
          ++p;
        size_t L = strlen(p);
        while (L && (p[L - 1] == '\n' || p[L - 1] == '\r'))
          --L;
        if (L >= outlen) L = outlen - 1;
        memcpy(outbuf, p, L);
        outbuf[L] = '\0';
        fclose(f);
        return;
      }
    }
  }
  fclose(f);
#endif
  safe_strncpy(outbuf, "unknown", outlen);
}

int get_system_information(SystemInfo *out) {
  if (!out) return -1;
  memset(out, 0, sizeof(*out));
  out->is_windows = 0;

  /* Hostname */
  if (gethostname(out->hostname, SI_HOSTLEN) != 0) { safe_strncpy(out->hostname, "unknown", SI_HOSTLEN); }

  /* uname for os name and architecture */
  struct utsname u;
  if (uname(&u) == 0) {
    safe_strncpy(out->os_name, u.sysname, SI_STRLEN);
    safe_strncpy(out->os_version, u.release, SI_STRLEN);
    safe_strncpy(out->architecture, u.machine, SI_STRLEN);
  } else {
    safe_strncpy(out->os_name, "unknown", SI_STRLEN);
    safe_strncpy(out->os_version, "unknown", SI_STRLEN);
    safe_strncpy(out->architecture, "unknown", SI_STRLEN);
  }

  /* Logical CPUs */
  out->logical_cpus = get_logical_cpus_posix();
  out->physical_cpus = 0;
#ifdef __linux__
  out->physical_cpus = get_physical_cpus_linux();
#endif

  /* Page size */
  long pgsz = sysconf(_SC_PAGESIZE);
  out->page_size = (unsigned long)(pgsz > 0 ? pgsz : 0);

  /* Memory: best-effort using sysconf for pages */
  long phys_pages = sysconf(_SC_PHYS_PAGES);
  long avail_pages = -1;
#ifdef _SC_AVPHYS_PAGES
  avail_pages = sysconf(_SC_AVPHYS_PAGES);
#endif
  if (phys_pages > 0 && out->page_size > 0) {
    out->total_ram = (unsigned long long)phys_pages * (unsigned long long)out->page_size;
  }
  if (avail_pages > 0 && out->page_size > 0) {
    out->avail_ram = (unsigned long long)avail_pages * (unsigned long long)out->page_size;
  }

/* On Linux, try sysinfo for more precise uptime and memory if available */
#ifdef __linux__
  struct sysinfo sinfo;
  if (sysinfo(&sinfo) == 0) {
    out->uptime_seconds = (unsigned long long)sinfo.uptime;
    if (sinfo.totalram && sinfo.freeram) {
      /* sysinfo reports in bytes already */
      out->total_ram = (unsigned long long)sinfo.totalram;
      out->avail_ram = (unsigned long long)sinfo.freeram;
    }
  }
#endif

/* On macOS, we can use sysctl to get memory / cpu model if needed */
#if defined(__APPLE__) && (defined(__MACH__) || 1)
  /* Try to get CPU model name via sysctlbyname("machdep.cpu.brand_string") */
  char cpubuf[SI_STRLEN];
  size_t cpubuflen = sizeof(cpubuf);
  if (sysctlbyname("machdep.cpu.brand_string", cpubuf, &cpubuflen, NULL, 0) == 0) {
    safe_strncpy(out->cpu_model, cpubuf, SI_STRLEN);
  }
  /* total RAM via hw.memsize */
  unsigned long long memsize = 0;
  size_t memszlen = sizeof(memsize);
  if (sysctlbyname("hw.memsize", &memsize, &memszlen, NULL, 0) == 0) { out->total_ram = memsize; }
#endif

/* Generic attempt to get CPU model on Linux */
#ifdef __linux__
  get_cpu_model_linux(out->cpu_model, SI_STRLEN);
#endif

  /* Load averages (POSIX) */
  double loads[3];
  if (getloadavg(loads, 3) == 3) {
    out->loadavg_1 = loads[0];
    out->loadavg_5 = loads[1];
    out->loadavg_15 = loads[2];
  } else {
    out->loadavg_1 = out->loadavg_5 = out->loadavg_15 = -1.0;
  }

  /* Uptime fallback: if not filled by platform-specific call, try /proc/uptime on Linux */
  if (out->uptime_seconds == 0) {
#ifdef __linux__
    FILE *f = fopen("/proc/uptime", "r");
    if (f) {
      double up = 0.0;
      if (fscanf(f, "%lf", &up) == 1) { out->uptime_seconds = (unsigned long long)up; }
      fclose(f);
    }
#endif
  }

  return 0;
}

#endif /* end POSIX */

#ifdef __cplusplus
} // extern "C"
#endif
#endif
#endif