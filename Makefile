# Manual kext build using only Command Line Tools + MacKernelSDK (no Xcode needed).
# Build only. This never loads the kext.
NAME     := SMCAcerFan
VERSION  := 1.0.0
DEPS     := deps

# make            -> release build in build/
# make DEBUG=1    -> debug build in build-debug/ (enables DBGLOG output; also pass -acerfandbg at boot)
DEBUG ?= 0
ifeq ($(DEBUG),1)
BUILD   := build-debug
DBGFLAG := -DDEBUG=1
else
BUILD   := build
DBGFLAG :=
endif
SDK      := $(shell xcrun --show-sdk-path)
KEXT     := $(BUILD)/$(NAME).kext
BIN      := $(KEXT)/Contents/MacOS/$(NAME)

CXX := xcrun clang++
CC  := xcrun clang

INCLUDES := -I$(DEPS)/MacKernelSDK/Headers -I$(DEPS)/Lilu/Lilu -I$(DEPS)/VirtualSMC -I$(DEPS)/VirtualSMC/VirtualSMCSDK

COMMON := -arch x86_64 -mmacosx-version-min=10.15 -isysroot $(SDK) \
	-nostdinc -nostdlib -ffreestanding -fno-builtin -fno-stack-protector \
	-mkernel -fapple-kext -fno-exceptions -fno-rtti -fno-asynchronous-unwind-tables \
	-mmmx -msse -msse2 -msse3 -mssse3 -mfpmath=sse \
	-D_FORTIFY_SOURCE=0 -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
	-DPRODUCT_NAME=$(NAME) -DMODULE_VERSION=$(VERSION) -DLILU_CUSTOM_IOKIT_INIT=1 \
	-Wno-unknown-warning-option -Wno-ossharedptr-misuse -Wno-vla -Wno-stdlibcxx-not-found \
	-Wno-deprecated-declarations -Wno-nullability-completeness -Wno-expansion-to-defined \
	-Os $(DBGFLAG)

CXXFLAGS := $(COMMON) -std=gnu++17 $(INCLUDES) \
	-isystem $(shell xcrun clang -print-resource-dir)/include
CFLAGS   := $(COMMON) -std=gnu11 $(INCLUDES) \
	-isystem $(shell xcrun clang -print-resource-dir)/include

SRCS := src/AcerFanController.cpp src/Diag.cpp src/KeyImplementations.cpp src/SMCAcerFan.cpp src/kern_start.cpp \
        $(DEPS)/Lilu/Lilu/Library/plugin_start.cpp
OBJS := $(patsubst %.cpp,$(BUILD)/obj/%.o,$(SRCS)) $(BUILD)/obj/kmod_info.o

all: $(BIN) $(KEXT)/Contents/Info.plist

$(BUILD)/obj/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/obj/kmod_info.o: kmod_info.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BIN): $(OBJS)
	@mkdir -p $(dir $@)
	$(CXX) -arch x86_64 -mmacosx-version-min=10.15 -isysroot $(SDK) -nostdlib -static \
		-Xlinker -kext -Xlinker -no_data_const \
		-L$(DEPS)/MacKernelSDK/Library/x86_64 -L$(DEPS)/MacKernelSDK/Library/universal \
		$(filter-out $(BUILD)/obj/kmod_info.o,$(OBJS)) -lkmodc++ $(BUILD)/obj/kmod_info.o -lkmod \
		-o $@

$(KEXT)/Contents/Info.plist: Info.plist
	@mkdir -p $(dir $@)
	cp $< $@

clean:
	rm -rf build build-debug

.PHONY: all clean
