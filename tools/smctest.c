// Minimal AppleSMC key reader/writer for testing SMCAcerFan.
//   smctest read            -> dump all fan keys
//   smctest write <key> <hex bytes...>   (e.g. smctest write F0Md 01)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <IOKit/IOKitLib.h>

typedef struct { char major, minor, build, reserved[1]; uint16_t release; } Vers;
typedef struct { uint16_t version, length; uint32_t cpuP, gpuP, memP; } PLimit;
typedef struct { uint32_t dataSize, dataType; char dataAttributes; } KeyInfo;
typedef struct {
	uint32_t key; Vers vers; PLimit pLimit; KeyInfo keyInfo;
	char result, status, data8; uint32_t data32; uint8_t bytes[32];
} SMCKeyData;

enum { kSMCHandleYPCEvent = 2, kRead = 5, kWrite = 6, kInfo = 9 };
static io_connect_t conn;

static uint32_t fourcc(const char *s) { return (uint32_t)s[0]<<24 | (uint32_t)s[1]<<16 | (uint32_t)s[2]<<8 | (uint32_t)s[3]; }
static void unfourcc(uint32_t v, char *o) { o[0]=v>>24; o[1]=v>>16; o[2]=v>>8; o[3]=v; o[4]=0; }

static kern_return_t call(SMCKeyData *in, SMCKeyData *out) {
	size_t outSize = sizeof(SMCKeyData);
	return IOConnectCallStructMethod(conn, kSMCHandleYPCEvent, in, sizeof(SMCKeyData), out, &outSize);
}

static int readKey(const char *k, uint32_t *size, char *type, uint8_t *bytes) {
	SMCKeyData in = {0}, out = {0};
	in.key = fourcc(k); in.data8 = kInfo;
	if (call(&in, &out) != KERN_SUCCESS || out.result != 0) return -1;
	*size = out.keyInfo.dataSize; unfourcc(out.keyInfo.dataType, type);
	in.keyInfo.dataSize = out.keyInfo.dataSize; in.data8 = kRead;
	memset(&out, 0, sizeof out);
	if (call(&in, &out) != KERN_SUCCESS || out.result != 0) return -2;
	memcpy(bytes, out.bytes, 32);
	return 0;
}

static void show(const char *k) {
	uint32_t size; char type[5]; uint8_t b[32];
	int r = readKey(k, &size, type, b);
	if (r) { printf("%-5s  not available (err %d)\n", k, r); return; }
	printf("%-5s  type=%-4s size=%u  bytes=", k, type, size);
	for (uint32_t i = 0; i < size && i < 16; i++) printf("%02X ", b[i]);
	if (!strncmp(type, "fpe2", 4)) printf(" -> %u rpm", ((b[0] << 8) | b[1]) >> 2);
	else if (!strncmp(type, "ui8", 3)) printf(" -> %u", b[0]);
	else if (!strncmp(type, "ui16", 4)) printf(" -> %u", (b[0] << 8) | b[1]);
	else if (!strncmp(type, "{fds", 4)) printf(" -> \"%.12s\"", (char *)b + 4);
	printf("\n");
}

int main(int argc, char **argv) {
	io_service_t svc = IOServiceGetMatchingService(kIOMainPortDefault, IOServiceMatching("AppleSMC"));
	if (!svc) { fprintf(stderr, "AppleSMC not found\n"); return 1; }
	if (IOServiceOpen(svc, mach_task_self(), 0, &conn) != KERN_SUCCESS) { fprintf(stderr, "open failed\n"); return 1; }
	if (argc >= 2 && !strcmp(argv[1], "write") && argc >= 4) {
		SMCKeyData in = {0}, out = {0}; uint32_t size; char type[5]; uint8_t b[32];
		if (readKey(argv[2], &size, type, b)) { fprintf(stderr, "key %s not found\n", argv[2]); return 1; }
		in.key = fourcc(argv[2]); in.data8 = kWrite; in.keyInfo.dataSize = size;
		for (int i = 3; i < argc && (i - 3) < 32; i++) in.bytes[i - 3] = (uint8_t)strtol(argv[i], NULL, 16);
		kern_return_t kr = call(&in, &out);
		printf("write %s: kr=0x%x result=0x%x\n", argv[2], kr, (unsigned)(uint8_t)out.result);
		if (kr != KERN_SUCCESS || out.result != 0) {
			fprintf(stderr, "write failed%s\n", kr == kIOReturnNotPrivileged ? " (not privileged - run with sudo)" : "");
			IOServiceClose(conn);
			return 1;
		}
		show(argv[2]);
	} else {
		const char *keys[] = {"FNum","F0ID","F0Ac","F0Mn","F0Mx","F0Tg","F0Md","FS! "};
		for (size_t i = 0; i < sizeof keys / sizeof *keys; i++) show(keys[i]);
	}
	IOServiceClose(conn);
	return 0;
}
