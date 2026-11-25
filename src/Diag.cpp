#include "Diag.hpp"

#include <IOKit/IOLib.h>
#include <IOKit/IOPlatformExpert.h>
#include <IOKit/IOService.h>
#include <stdarg.h>

namespace {
constexpr size_t LogSize = 1800;
char logBuf[LogSize];
size_t logLen = 0;
uint32_t seq = 0;
volatile char busy = 0;
}

void acerDiag(const char *fmt, ...) {
	if (__atomic_test_and_set(&busy, __ATOMIC_ACQUIRE)) return;   // never block, drop on contention

	uint64_t ns = 0;
	clock_interval_to_absolutetime_interval(1, kNanosecondScale, &ns);   // ns per tick unit (usually 1)
	uint64_t now = mach_absolute_time();
	absolutetime_to_nanoseconds(now, &now);

	char line[200];
	int n = snprintf(line, sizeof(line), "#%u t=%llus ", ++seq, now / 1000000000ULL);
	va_list ap;
	va_start(ap, fmt);
	if (n > 0 && static_cast<size_t>(n) < sizeof(line))
		vsnprintf(line + n, sizeof(line) - n, fmt, ap);
	va_end(ap);

	size_t len = strlen(line);
	if (len + 2 > sizeof(line)) len = sizeof(line) - 2;
	line[len++] = ';';
	line[len] = 0;

	// Keep the newest entries: drop the oldest half when full.
	if (logLen + len >= LogSize) {
		size_t keep = LogSize / 2;
		memmove(logBuf, logBuf + (logLen - keep), keep);
		logLen = keep;
	}
	memcpy(logBuf + logLen, line, len + 1);
	logLen += len;

	auto platform = IOService::getPlatform();
	if (platform) platform->setProperty("SMCAcerFan-Log", logBuf);

	__atomic_clear(&busy, __ATOMIC_RELEASE);
}
