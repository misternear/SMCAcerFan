//
//  SMCAcerFan.hpp
//  SMCAcerFan
//
//  VirtualSMC plugin exposing the Acer Aspire E5-575 EC fan as SMC fan keys.
//

#ifndef SMCAcerFan_hpp
#define SMCAcerFan_hpp

#include <IOKit/IOService.h>
#include <IOKit/IOWorkLoop.h>
#include <IOKit/IOTimerEventSource.h>
#include <IOKit/pwr_mgt/RootDomain.h>
#include <VirtualSMCSDK/kern_vsmcapi.hpp>
#include <Headers/kern_util.hpp>

class EXPORT SMCAcerFan : public IOService {
	OSDeclareDefaultStructors(SMCAcerFan)

	static constexpr SMC_KEY KeyFNum = SMC_MAKE_IDENTIFIER('F','N','u','m');
	static constexpr SMC_KEY KeyF0ID = SMC_MAKE_IDENTIFIER('F','0','I','D');
	static constexpr SMC_KEY KeyF0Ac = SMC_MAKE_IDENTIFIER('F','0','A','c');
	static constexpr SMC_KEY KeyF0Mn = SMC_MAKE_IDENTIFIER('F','0','M','n');
	static constexpr SMC_KEY KeyF0Mx = SMC_MAKE_IDENTIFIER('F','0','M','x');
	static constexpr SMC_KEY KeyF0Md = SMC_MAKE_IDENTIFIER('F','0','M','d');
	static constexpr SMC_KEY KeyF0Tg = SMC_MAKE_IDENTIFIER('F','0','T','g');
	static constexpr SMC_KEY KeyFS__ = SMC_MAKE_IDENTIFIER('F','S','!',' ');

	static constexpr uint32_t PollIntervalMS = 500;
	static constexpr uint32_t WakeDelayMS    = 2000;
	static constexpr uint32_t SubmitDelayMS  = 5000;

	IONotifier *vsmcNotifier {nullptr};
	IONotifier *sleepNotifier {nullptr};
	IOWorkLoop *workLoop {nullptr};
	IOTimerEventSource *timer {nullptr};
	IOTimerEventSource *submitTimer {nullptr};
	uint32_t heartbeat {0};

	VirtualSMCAPI::Plugin vsmcPlugin {
		xStringify(PRODUCT_NAME),
		parseModuleVersion(xStringify(MODULE_VERSION)),
		VirtualSMCAPI::Version,
	};

	static IOReturn sleepWakeHandler(void *target, void *, UInt32 messageType, IOService *provider, void *messageArgument, vm_size_t);

public:
	bool init(OSDictionary *dict) override;
	IOService *probe(IOService *provider, SInt32 *score) override;
	bool start(IOService *provider) override;
	void stop(IOService *provider) override;
	void free() override;
	bool terminate(IOOptionBits options = 0) override;

	static bool vsmcNotificationHandler(void *sensors, void *refCon, IOService *vsmc, IONotifier *notifier);
};

#endif /* SMCAcerFan_hpp */
