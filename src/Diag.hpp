//
//  Diag.hpp
//  SMCAcerFan
//
//  Lifecycle diary published to the IORegistry (property "SMCAcerFan-Log" on the platform
//  expert). Kernel log output from Lilu plugins is not retrievable on this machine, and the
//  plugin's own registry entry may disappear, so this is the only reliable way to see what
//  happened. Read with: ioreg -l -w0 | grep SMCAcerFan-Log
//
#ifndef Diag_hpp
#define Diag_hpp

void acerDiag(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

#endif
