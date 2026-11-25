//
//  AcerFanController.cpp
//  SMCAcerFan
//

#include "AcerFanController.hpp"
#include "Diag.hpp"

#include <IOKit/IOLib.h>
#include <i386/proc_reg.h>
#include <Headers/kern_util.hpp>
#include <Headers/kern_cpu.hpp>

namespace {
constexpr uint8_t EcCmdRead  = 0x80;
constexpr uint8_t EcCmdWrite = 0x81;
constexpr uint8_t EcStatusOBF = 0x01;
constexpr uint8_t EcStatusIBF = 0x02;

constexpr uint32_t MsrTemperatureTarget  = 0x1A2;
constexpr uint32_t MsrIa32ThermStatus    = 0x19C;
constexpr uint32_t MsrPackageThermStatus = 0x1B1;

inline uint8_t portIn(uint16_t port) {
	uint8_t v;
	__asm__ volatile("inb %1, %0" : "=a"(v) : "Nd"(port));
	return v;
}

inline void portOut(uint16_t port, uint8_t v) {
	__asm__ volatile("outb %0, %1" : : "a"(v), "Nd"(port));
}
}

AcerFanController &AcerFanController::shared() {
	static AcerFanController instance;
	return instance;
}

uint16_t AcerFanController::levelToRpm(uint8_t level) {
	if (level > MaxLevel) level = MaxLevel;
	return static_cast<uint16_t>((static_cast<uint32_t>(level) * MaxRpm) / MaxLevel);
}

uint8_t AcerFanController::rpmToLevel(uint16_t rpm) {
	if (rpm == 0) return 0;
	if (rpm >= MaxRpm) return MaxLevel;
	uint32_t lvl = (static_cast<uint32_t>(rpm) * MaxLevel + MaxRpm / 2) / MaxRpm;
	if (lvl == 0) lvl = 1;
	return lvl > MaxLevel ? MaxLevel : static_cast<uint8_t>(lvl);
}

void AcerFanController::setManual(bool m) {
	__atomic_store_n(&wantManual, m, __ATOMIC_RELEASE);
	if (!m) {
		__atomic_store_n(&targetLevel, 0, __ATOMIC_RELEASE);
	}
}

void AcerFanController::setTargetRpm(uint16_t rpm) {
	__atomic_store_n(&targetLevel, rpmToLevel(rpm), __ATOMIC_RELEASE);
}

void AcerFanController::setMinRpm(uint16_t rpm) {
	if (rpm > MaxRpm) rpm = MaxRpm;
	__atomic_store_n(&minRpmVal, rpm, __ATOMIC_RELEASE);
	__atomic_store_n(&minLevel, rpmToLevel(rpm), __ATOMIC_RELEASE);
}

uint8_t AcerFanController::effectiveLevel() const {
	bool expManual = __atomic_load_n(&wantManual, __ATOMIC_ACQUIRE);
	uint8_t tgt    = expManual ? __atomic_load_n(&targetLevel, __ATOMIC_ACQUIRE) : 0;
	uint8_t floor  = __atomic_load_n(&minLevel, __ATOMIC_ACQUIRE);
	uint8_t lvl    = tgt > floor ? tgt : floor;
	return lvl > MaxLevel ? MaxLevel : lvl;
}

uint16_t AcerFanController::targetRpm() const {
	return levelToRpm(effectiveLevel());
}

// ---------------------------------------------------------------- EC access

bool AcerFanController::waitIbfClear() {
	for (int i = 0; i < 200; i++) {
		if ((portIn(EcCmdPort) & EcStatusIBF) == 0) return true;
		IODelay(100);
	}
	return false;
}

bool AcerFanController::waitObfSet() {
	for (int i = 0; i < 200; i++) {
		if (portIn(EcCmdPort) & EcStatusOBF) return true;
		IODelay(100);
	}
	return false;
}

bool AcerFanController::ecRead(uint8_t reg, uint8_t &val) {
	if (!waitIbfClear()) return false;
	portOut(EcCmdPort, EcCmdRead);
	if (!waitIbfClear()) return false;
	portOut(EcDataPort, reg);
	if (!waitObfSet()) return false;
	val = portIn(EcDataPort);
	return true;
}

bool AcerFanController::ecWrite(uint8_t reg, uint8_t val) {
	if (!waitIbfClear()) return false;
	portOut(EcCmdPort, EcCmdWrite);
	if (!waitIbfClear()) return false;
	portOut(EcDataPort, reg);
	if (!waitIbfClear()) return false;
	portOut(EcDataPort, val);
	return true;
}

/// Track consecutive failures; stop touching the EC entirely after MaxFaults.
void AcerFanController::noteResult(bool ok) {
	if (ok) {
		faults = 0;
		return;
	}
	if (++faults >= MaxFaults && usable()) {
		SYSLOG("acerfan", "EC failed %u times in a row, disabling fan control", faults);
		__atomic_store_n(&ecUsable, false, __ATOMIC_RELEASE);
	}
}

// ---------------------------------------------------------------- temperature

int AcerFanController::readCpuTemp() {
	uint32_t lo = 0, hi = 0;
	int readout = -1;

	if (hasPkgTherm && rdmsr_carefully(MsrPackageThermStatus, &lo, &hi) == 0 && (lo & 0x80000000U))
		readout = static_cast<int>((lo >> 16) & 0x7F);
	else if (hasCoreTherm && rdmsr_carefully(MsrIa32ThermStatus, &lo, &hi) == 0 && (lo & 0x80000000U))
		readout = static_cast<int>((lo >> 16) & 0x7F);

	return readout < 0 ? -1 : tjmax - readout;
}

// ---------------------------------------------------------------- lifecycle

bool AcerFanController::probe() {
	// Refuse to drive an EC that doesn't look like the one the register map was verified on.
	uint8_t mode = 0, level = 0;
	if (!ecRead(RegFanMode, mode) || !ecRead(RegFanLevel, level)) {
		acerDiag("ecprobe: EC read failed");
		SYSLOG("acerfan", "EC not responding on ports 0x62/0x66, not loading");
		return false;
	}
	// 0x00 = BIOS untouched since cold boot, 0x04 = auto restored by a previous write, 0x14 = manual override.
	if ((mode != ModeBios && mode != ModeAuto && mode != ModeManual) || level > MaxLevel) {
		acerDiag("ecprobe: unexpected 0x93=0x%02X 0x55=%u", mode, level);
		SYSLOG("acerfan", "unexpected EC values (0x93=0x%02X, 0x55=%u), wrong machine? not loading", mode, level);
		return false;
	}

	uint32_t val = 0;
	hasCoreTherm = CPUInfo::getCpuid(6, 0, &val) && (val & 0x01U);
	hasPkgTherm  = CPUInfo::getCpuid(6, 0, &val) && (val & 0x40U);

	uint32_t lo = 0, hi = 0;
	if (rdmsr_carefully(MsrTemperatureTarget, &lo, &hi) == 0) {
		int t = static_cast<int>((lo >> 16) & 0xFF);
		if (t >= 70 && t <= 125) tjmax = t;
	}

	if (!hasCoreTherm && !hasPkgTherm)
		SYSLOG("acerfan", "no CPU thermal MSRs, 90C failsafe unavailable");

	__atomic_store_n(&actualLevel, level, __ATOMIC_RELEASE);
	__atomic_store_n(&ecUsable, true, __ATOMIC_RELEASE);
	DBGLOG("acerfan", "EC ok: mode=0x%02X level=%u tjmax=%d pkg=%d core=%d", mode, level, tjmax, hasPkgTherm, hasCoreTherm);
	return true;
}

void AcerFanController::tick() {
	tickCount++;
	if (!usable()) return;

	// 1. Failsafe, evaluated every tick regardless of what the OS asked for.
	int temp = readCpuTemp();
	bool tripped = failsafeActive();
	if (temp >= TripTempC && !tripped) {
		SYSLOG("acerfan", "CPU at %dC, handing fan back to BIOS", temp);
		__atomic_store_n(&failsafe, true, __ATOMIC_RELEASE);
		tripped = true;
	} else if (tripped && temp >= 0 && temp <= ClearTempC) {
		DBGLOG("acerfan", "CPU cooled to %dC, failsafe cleared", temp);
		__atomic_store_n(&failsafe, false, __ATOMIC_RELEASE);
		tripped = false;
	}

	// 2. Drive the EC.
	uint8_t level = 0;
	if (manual() && !tripped) {
		uint8_t want = effectiveLevel();
		uint8_t mode = 0;

		bool ok = ecRead(RegFanMode, mode);
		if (ok && mode != ModeManual) ok = ecWrite(RegFanMode, ModeManual);
		if (ok) ok = ecWrite(RegFanLevel, want);       // written every tick: keep-alive against the EC watchdog
		if (ok) ok = ecRead(RegFanLevel, level);
		noteResult(ok);
		if (ok) {
			ecInManual = true;
			__atomic_store_n(&actualLevel, level, __ATOMIC_RELEASE);
		}
	} else {
		if (ecInManual) {
			bool ok = ecWrite(RegFanMode, ModeAuto);
			if (ok) ok = ecWrite(RegFanLevel, 0);
			noteResult(ok);
			if (ok) ecInManual = false;
		} else if ((tickCount & 3U) == 0) {
			// Auto mode: only peek at the EC every 2s to keep F0Ac fresh and minimise bus contention.
			bool ok = ecRead(RegFanLevel, level);
			noteResult(ok);
			if (ok && level <= MaxLevel) __atomic_store_n(&actualLevel, level, __ATOMIC_RELEASE);
		}
	}
}

void AcerFanController::restoreAuto() {
	if (!usable() && !ecInManual) return;
	bool ok = ecWrite(RegFanMode, ModeAuto);
	if (ok) ok = ecWrite(RegFanLevel, 0);
	if (ok) ecInManual = false;
	DBGLOG("acerfan", "restoreAuto: %s", ok ? "ok" : "FAILED");
}
