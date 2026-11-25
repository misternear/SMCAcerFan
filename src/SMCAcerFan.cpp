//
//  SMCAcerFan.cpp
//  SMCAcerFan
//

#include "SMCAcerFan.hpp"
#include "KeyImplementations.hpp"
#include "AcerFanController.hpp"
#include "Diag.hpp"

#include <Headers/plugin_start.hpp>
#include <Headers/kern_version.hpp>

OSDefineMetaClassAndStructors(SMCAcerFan, IOService)

// IOKit probes personalities repeatedly during boot. Only one instance may ever own the EC and be
// submitted to VirtualSMC, otherwise keys get submitted twice and the EC is polled needlessly.
static bool claimed = false;

namespace {
// Layout copied from SMCDellSensors' FanTypeDescStruct (16 bytes).
struct FanDesc {
	uint8_t type {0};      // FAN_PWM_TACH
	uint8_t zone {1};
	uint8_t location {0};
	uint8_t rsvd {0};
	char    name[12] {};
};
}

bool SMCAcerFan::init(OSDictionary *dict) {
	return IOService::init(dict);
}

IOService *SMCAcerFan::probe(IOService *provider, SInt32 *score) {
	if (__atomic_load_n(&claimed, __ATOMIC_ACQUIRE)) return nullptr;   // already owned by another instance
	acerDiag("probe() enter this=%p", this);
	if (!IOService::probe(provider, score)) {
		acerDiag("probe: parent probe failed");
		SYSLOG("acerfan", "failed to probe the parent");
		return nullptr;
	}

	// Verifies the EC looks right. Nothing is written to it here.
	if (!AcerFanController::shared().probe()) {
		acerDiag("probe: EC check FAILED, returning null");
		return nullptr;
	}
	acerDiag("probe: EC ok");

	// VirtualSMC lets a plugin loaded later override a same-named key (kern_keystore.cpp loadPlugin).
	// SMCSuperIO's F0Ac is a stub that always reads 0, its F0Mx is 3315 (caps tools at ~level 6),
	// and it reports 2 fans on IT8987. SMCAcerFan registers all 8 fan keys and delays VirtualSMC
	// registration by ~5 s so it always submits after SMCSuperIO.
	VirtualSMCAPI::addKey(KeyFNum, vsmcPlugin.data,
		VirtualSMCAPI::valueWithUint8(1, nullptr, SMC_KEY_ATTRIBUTE_CONST | SMC_KEY_ATTRIBUTE_READ));
	VirtualSMCAPI::addKey(KeyF0Ac, vsmcPlugin.data,
		VirtualSMCAPI::valueWithFp(0, SmcKeyTypeFpe2, new F0Ac, SMC_KEY_ATTRIBUTE_READ));
	VirtualSMCAPI::addKey(KeyF0Mn, vsmcPlugin.data,
		VirtualSMCAPI::valueWithFp(0, SmcKeyTypeFpe2, new F0Mn, SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_READ));
	VirtualSMCAPI::addKey(KeyF0Mx, vsmcPlugin.data,
		VirtualSMCAPI::valueWithFp(AcerFanController::MaxRpm, SmcKeyTypeFpe2, new F0Mx, SMC_KEY_ATTRIBUTE_CONST | SMC_KEY_ATTRIBUTE_READ));
	VirtualSMCAPI::addKey(KeyF0Tg, vsmcPlugin.data,
		VirtualSMCAPI::valueWithFp(0, SmcKeyTypeFpe2, new F0Tg, SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_READ));
	VirtualSMCAPI::addKey(KeyF0Md, vsmcPlugin.data,
		VirtualSMCAPI::valueWithUint8(0, new F0Md, SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_READ));
	VirtualSMCAPI::addKey(KeyFS__, vsmcPlugin.data,
		VirtualSMCAPI::valueWithUint16(0, new FS__, SMC_KEY_ATTRIBUTE_WRITE | SMC_KEY_ATTRIBUTE_READ));

	FanDesc desc;
	lilu_os_strncpy(desc.name, "CPU Fan", sizeof(desc.name));
	VirtualSMCAPI::addKey(KeyF0ID, vsmcPlugin.data, VirtualSMCAPI::valueWithData(
		reinterpret_cast<const SMC_DATA *>(&desc), sizeof(desc), SmcKeyTypeFds, nullptr, SMC_KEY_ATTRIBUTE_CONST | SMC_KEY_ATTRIBUTE_READ));

	qsort(const_cast<VirtualSMCKeyValue *>(vsmcPlugin.data.data()), vsmcPlugin.data.size(), sizeof(VirtualSMCKeyValue), VirtualSMCKeyValue::compare);

	bool expected = false;
	if (!__atomic_compare_exchange_n(&claimed, &expected, true, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
		acerDiag("probe: lost race, another instance claimed it");
		return nullptr;
	}
	acerDiag("probe: keys built (%u), CLAIMED, returning this", static_cast<unsigned>(vsmcPlugin.data.size()));
	return this;
}

bool SMCAcerFan::start(IOService *provider) {
	acerDiag("start() enter");
	if (!IOService::start(provider)) {
		acerDiag("start: parent start FAILED"); __atomic_store_n(&claimed, false, __ATOMIC_RELEASE);
		SYSLOG("acerfan", "failed to start the parent");
		return false;
	}

	setProperty("VersionInfo", kextVersion);

	// Private workloop: the only context that ever touches the EC.
	workLoop = IOWorkLoop::workLoop();
	if (!workLoop) {
		acerDiag("start: workloop FAILED"); __atomic_store_n(&claimed, false, __ATOMIC_RELEASE);
		SYSLOG("acerfan", "failed to create workloop");
		return false;
	}

	timer = IOTimerEventSource::timerEventSource(this, [](OSObject *owner, IOTimerEventSource *sender) {
		AcerFanController::shared().tick();
		auto self = static_cast<SMCAcerFan *>(owner);
		++self->heartbeat;
		if (self->heartbeat % 120 == 1)
			acerDiag("alive tick=%u retain=%d usable=%d manual=%d failsafe=%d", self->heartbeat, self->getRetainCount(), AcerFanController::shared().usable(), AcerFanController::shared().manual(), AcerFanController::shared().failsafeActive());
		if (self->heartbeat % 240 == 1)
			SYSLOG("acerfan", "alive: tick %u usable=%d manual=%d failsafe=%d", self->heartbeat, AcerFanController::shared().usable(), AcerFanController::shared().manual(), AcerFanController::shared().failsafeActive());
		sender->setTimeoutMS(PollIntervalMS);
	});
	if (!timer || workLoop->addEventSource(timer) != kIOReturnSuccess) {
		acerDiag("start: timer FAILED"); __atomic_store_n(&claimed, false, __ATOMIC_RELEASE);
		SYSLOG("acerfan", "failed to create timer");
		OSSafeReleaseNULL(timer);
		return false;
	}

	sleepNotifier = registerSleepWakeInterest(sleepWakeHandler, this);
	if (!sleepNotifier) {
		acerDiag("start: sleep notifier FAILED"); __atomic_store_n(&claimed, false, __ATOMIC_RELEASE);
		SYSLOG("acerfan", "failed to register sleep/wake interest");
		return false;
	}

	vsmcNotifier = VirtualSMCAPI::registerHandler(vsmcNotificationHandler, this);
	if (!vsmcNotifier) {
		acerDiag("start: registerHandler FAILED");
		SYSLOG("acerfan", "registerHandler failed");
	}

	timer->setTimeoutMS(PollIntervalMS);

	// Pin ourselves: VirtualSMC keeps a raw pointer into this object, so it must never be freed.
	retain();
	acerDiag("start: OK, pinned retain=%d (deferring VSMC submit 5s)", getRetainCount());
	SYSLOG("acerfan", "started, pinned (retain=%d)", getRetainCount());
	return true;
}

bool SMCAcerFan::vsmcNotificationHandler(void *sensors, void *refCon, IOService *vsmc, IONotifier *notifier) {
	if (sensors && vsmc) {
		DBGLOG("acerfan", "got vsmc notification");
		auto &plugin = static_cast<SMCAcerFan *>(sensors)->vsmcPlugin;
		auto ret = vsmc->callPlatformFunction(VirtualSMCAPI::SubmitPlugin, true, sensors, &plugin, nullptr, nullptr);
		if (ret == kIOReturnSuccess) {
			acerDiag("plugin submitted OK retain=%d", static_cast<SMCAcerFan *>(sensors)->getRetainCount());
			SYSLOG("acerfan", "plugin submitted to VirtualSMC");
			return true;
		} else if (ret != kIOReturnUnsupported) {
			acerDiag("plugin submit FAILED ret=0x%X", ret);
			SYSLOG("acerfan", "plugin submission failure %X", ret);
		} else {
			DBGLOG("acerfan", "plugin submission to non vsmc");
		}
	} else {
		SYSLOG("acerfan", "got null vsmc notification");
	}
	return false;
}

IOReturn SMCAcerFan::sleepWakeHandler(void *target, void *, UInt32 messageType, IOService *, void *messageArgument, vm_size_t) {
	if (messageType != kIOMessageSystemWillSleep &&
		messageType != kIOMessageSystemHasPoweredOn &&
		messageType != kIOMessageSystemWillNotSleep &&
		messageType != kIOMessageSystemWillPowerOff &&
		messageType != kIOMessageSystemWillRestart) {
		return kIOReturnUnsupported;
	}

	acerDiag("sleepwake msg=0x%X", messageType);
	auto note = static_cast<sleepWakeNote *>(messageArgument);
	auto self = OSDynamicCast(SMCAcerFan, static_cast<OSObject *>(target));

	if (messageType == kIOMessageSystemWillSleep ||
		messageType == kIOMessageSystemWillPowerOff ||
		messageType == kIOMessageSystemWillRestart) {
		// Give the fan back to the BIOS before the machine goes down. Run it ON the
		// workloop so it can never interleave with a tick().
		if (self && self->workLoop && self->timer) {
			self->workLoop->runAction([](OSObject *owner, void *, void *, void *, void *) -> IOReturn {
				auto me = static_cast<SMCAcerFan *>(owner);
				me->timer->cancelTimeout();
				AcerFanController::shared().restoreAuto();
				return kIOReturnSuccess;
			}, self);
		}
	} else if (self && self->timer) {
		// Woke up (or sleep was aborted): resume polling; tick() re-asserts manual mode if requested.
		self->timer->setTimeoutMS(WakeDelayMS);
	}

	note->returnValue = 0;
	acknowledgeSleepWakeNotification(note->powerRef);
	return kIOReturnSuccess;
}

void SMCAcerFan::stop(IOService *provider) {
	acerDiag("stop() CALLED");
	PANIC("acerfan", "called stop!!!");
}

bool SMCAcerFan::terminate(IOOptionBits options) {
	// VirtualSMC holds pointers into this object; tearing it down would leave them dangling.
	acerDiag("terminate(0x%X) REFUSED", options);
	SYSLOG("acerfan", "terminate(0x%X) requested, refusing", options);
	return false;
}

void SMCAcerFan::free() {
	acerDiag("free() CALLED - object destroyed");
	SYSLOG("acerfan", "free() called, object is being destroyed");
	IOService::free();
}
