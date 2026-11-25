//
//  kern_start.cpp
//  SMCAcerFan
//

#include <Headers/plugin_start.hpp>
#include <Headers/kern_api.hpp>

static const char *bootargOff[]   { "-acerfanoff" };
static const char *bootargDebug[] { "-acerfandbg" };
static const char *bootargBeta[]  { "-acerfanbeta" };

PluginConfiguration ADDPR(config) {
	xStringify(PRODUCT_NAME),
	parseModuleVersion(xStringify(MODULE_VERSION)),
	LiluAPI::AllowNormal,   // never act in installer / recovery / safe mode
	bootargOff,
	arrsize(bootargOff),
	bootargDebug,
	arrsize(bootargDebug),
	bootargBeta,
	arrsize(bootargBeta),
	KernelVersion::Catalina,
	KernelVersion::Tahoe,
	[]() {
		// All work happens in the IOService; nothing to hook.
	}
};
