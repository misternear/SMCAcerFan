//
//  AcerFanController.hpp
//  SMCAcerFan
//
//  EC fan control for the Acer Aspire E5-575 (registers verified with acer_fan_poc.c):
//    0x93: fan mode   (0x14 = manual override, 0x04 = BIOS/auto)
//    0x55: fan level  (0..8)
//  EC access is raw port I/O on 0x62/0x66 and is ONLY ever performed from tick() and
//  restoreAuto(), both of which must run on the plugin's private workloop. SMC key
//  handlers never touch the EC; they only exchange values through atomics.
//

#ifndef AcerFanController_hpp
#define AcerFanController_hpp

#include <stdint.h>

class AcerFanController {
public:
	static constexpr uint16_t EcDataPort   = 0x62;
	static constexpr uint16_t EcCmdPort    = 0x66;
	static constexpr uint8_t  RegFanLevel  = 0x55;
	static constexpr uint8_t  RegFanMode   = 0x93;
	static constexpr uint8_t  ModeManual   = 0x14;
	static constexpr uint8_t  ModeBios     = 0x00;   ///< value seen at cold boot, before anything wrote 0x93
	static constexpr uint8_t  ModeAuto     = 0x04;
	static constexpr uint8_t  MaxLevel     = 8;
	static constexpr uint16_t MaxRpm       = 4500;   ///< Nominal RPM at level 8 (spec value)

	static constexpr int      TripTempC    = 90;     ///< Force BIOS auto at/above this CPU temperature
	static constexpr int      ClearTempC   = 80;     ///< Allow manual control again at/below this
	static constexpr uint32_t MaxFaults    = 5;      ///< Consecutive EC failures before giving up

	static AcerFanController &shared();

	/// Sanity-check the EC (are registers 0x55/0x93 plausible?). Call once before registering keys.
	bool probe();

	/// Periodic work. Workloop only.
	void tick();

	/// Hand the fan back to the BIOS (0x93=0x04 then 0x55=0, same as the proven POC). Workloop only.
	void restoreAuto();

	// --- SMC side (any thread, atomics only) ---
	void     setManual(bool manual);
	bool     manual() const                { return __atomic_load_n(&wantManual, __ATOMIC_ACQUIRE) || __atomic_load_n(&minLevel, __ATOMIC_ACQUIRE) > 0; }
	void     setTargetRpm(uint16_t rpm);
	uint16_t targetRpm() const;
	void     setMinRpm(uint16_t rpm);
	uint16_t minRpm() const                { return __atomic_load_n(&minRpmVal, __ATOMIC_ACQUIRE); }
	uint16_t maxRpm() const                { return MaxRpm; }
	uint16_t actualRpm() const             { return levelToRpm(__atomic_load_n(&actualLevel, __ATOMIC_ACQUIRE)); }
	bool     usable() const                { return __atomic_load_n(&ecUsable, __ATOMIC_ACQUIRE); }
	bool     failsafeActive() const        { return __atomic_load_n(&failsafe, __ATOMIC_ACQUIRE); }

	static uint16_t levelToRpm(uint8_t level);
	static uint8_t  rpmToLevel(uint16_t rpm);

private:
	AcerFanController() = default;

	uint8_t effectiveLevel() const;

	// raw EC access
	bool waitIbfClear();
	bool waitObfSet();
	bool ecRead(uint8_t reg, uint8_t &val);
	bool ecWrite(uint8_t reg, uint8_t val);
	void noteResult(bool ok);

	// CPU temperature (degrees C) or -1 when unavailable
	int  readCpuTemp();

	bool     wantManual  {false};
	uint8_t  targetLevel {0};
	uint16_t minRpmVal   {0};
	uint8_t  minLevel    {0};
	uint8_t  actualLevel {0};
	bool     ecUsable    {false};
	bool     failsafe    {false};

	// workloop-private state
	bool     ecInManual {false};
	uint32_t faults     {0};
	uint32_t tickCount  {0};
	bool     hasPkgTherm {false};
	bool     hasCoreTherm {false};
	int      tjmax      {100};
};

#endif /* AcerFanController_hpp */
