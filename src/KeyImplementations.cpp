//
//  KeyImplementations.cpp
//  SMCAcerFan
//

#include "KeyImplementations.hpp"
#include "AcerFanController.hpp"
#include <Headers/kern_util.hpp>

SMC_RESULT F0Ac::readAccess() {
	*reinterpret_cast<uint16_t *>(data) = VirtualSMCAPI::encodeIntFp(SmcKeyTypeFpe2, AcerFanController::shared().actualRpm());
	return SmcSuccess;
}

SMC_RESULT F0Mn::readAccess() {
	*reinterpret_cast<uint16_t *>(data) = VirtualSMCAPI::encodeIntFp(SmcKeyTypeFpe2, AcerFanController::shared().minRpm());
	return SmcSuccess;
}

SMC_RESULT F0Mn::update(const SMC_DATA *src) {
	VirtualSMCValue::update(src);
	uint16_t rpm = VirtualSMCAPI::decodeIntFp(SmcKeyTypeFpe2, *reinterpret_cast<const uint16_t *>(src));
	AcerFanController::shared().setMinRpm(rpm);
	DBGLOG("acerfan", "F0Mn set to %u rpm", rpm);
	return SmcSuccess;
}

SMC_RESULT F0Mx::readAccess() {
	*reinterpret_cast<uint16_t *>(data) = VirtualSMCAPI::encodeIntFp(SmcKeyTypeFpe2, AcerFanController::shared().maxRpm());
	return SmcSuccess;
}

SMC_RESULT F0Tg::readAccess() {
	*reinterpret_cast<uint16_t *>(data) = VirtualSMCAPI::encodeIntFp(SmcKeyTypeFpe2, AcerFanController::shared().targetRpm());
	return SmcSuccess;
}

SMC_RESULT F0Tg::update(const SMC_DATA *src) {
	VirtualSMCValue::update(src);
	uint16_t rpm = VirtualSMCAPI::decodeIntFp(SmcKeyTypeFpe2, *reinterpret_cast<const uint16_t *>(src));
	AcerFanController::shared().setTargetRpm(rpm);
	DBGLOG("acerfan", "F0Tg set to %u rpm", rpm);
	return SmcSuccess;
}

SMC_RESULT F0Md::readAccess() {
	*reinterpret_cast<uint8_t *>(data) = AcerFanController::shared().manual() ? 1 : 0;
	return SmcSuccess;
}

SMC_RESULT F0Md::update(const SMC_DATA *src) {
	VirtualSMCValue::update(src);
	AcerFanController::shared().setManual(src[0] != 0);
	DBGLOG("acerfan", "F0Md set to %u", src[0]);
	return SmcSuccess;
}

SMC_RESULT FS__::readAccess() {
	uint16_t value = AcerFanController::shared().manual() ? 1 : 0;
	auto bytes = reinterpret_cast<uint8_t *>(data);
	bytes[0] = value >> 8;
	bytes[1] = value & 0xFF;
	return SmcSuccess;
}

SMC_RESULT FS__::update(const SMC_DATA *src) {
	VirtualSMCValue::update(src);
	uint16_t mask = (static_cast<uint16_t>(src[0]) << 8) | src[1];
	AcerFanController::shared().setManual((mask & 1U) != 0);
	DBGLOG("acerfan", "FS! set to 0x%04X", mask);
	return SmcSuccess;
}
