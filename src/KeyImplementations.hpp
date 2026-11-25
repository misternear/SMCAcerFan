//
//  KeyImplementations.hpp
//  SMCAcerFan
//
//  SMC key handlers. None of these touch the EC: they read cached state from, and
//  write requests into, AcerFanController's atomics. The controller's timer applies them.
//

#ifndef KeyImplementations_hpp
#define KeyImplementations_hpp

#include <VirtualSMCSDK/kern_vsmcapi.hpp>

class F0Ac : public VirtualSMCValue { protected: SMC_RESULT readAccess() override; };
class F0Mn : public VirtualSMCValue { protected: SMC_RESULT readAccess() override; SMC_RESULT update(const SMC_DATA *src) override; };
class F0Mx : public VirtualSMCValue { protected: SMC_RESULT readAccess() override; };
class F0Tg : public VirtualSMCValue { protected: SMC_RESULT readAccess() override; SMC_RESULT update(const SMC_DATA *src) override; };
class F0Md : public VirtualSMCValue { protected: SMC_RESULT readAccess() override; SMC_RESULT update(const SMC_DATA *src) override; };
// 'FS! ' : fan force bitmask, bit 0 = fan 0 manual
class FS__ : public VirtualSMCValue { protected: SMC_RESULT readAccess() override; SMC_RESULT update(const SMC_DATA *src) override; };

#endif /* KeyImplementations_hpp */
