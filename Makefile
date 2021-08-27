STATIC_LINKING := 0
AR             := ar

ifneq ($(SANITIZER),)
   CFLAGS   := -fsanitize=$(SANITIZER) $(CFLAGS)
   CXXFLAGS := -fsanitize=$(SANITIZER) $(CXXFLAGS)
   LDFLAGS  := -fsanitize=$(SANITIZER) $(LDFLAGS)
endif

ifeq ($(platform),)
platform = unix
ifeq ($(shell uname -a),)
   platform = win
else ifneq ($(findstring MINGW,$(shell uname -a)),)
   platform = win
else ifneq ($(findstring Darwin,$(shell uname -a)),)
   platform = osx
else ifneq ($(findstring win,$(shell uname -a)),)
   platform = win
endif
endif

# system platform
system_platform = unix
ifeq ($(shell uname -a),)
	EXE_EXT = .exe
	system_platform = win
else ifneq ($(findstring Darwin,$(shell uname -a)),)
	system_platform = osx
	arch = intel
        ifeq ($(shell uname -p),powerpc)
		arch = ppc
        endif
	ifeq ($(shell uname -p),arm)
		arch = arm
	endif
ifeq ($(shell uname -p),powerpc)
	arch = ppc
endif
else ifneq ($(findstring MINGW,$(shell uname -a)),)
	system_platform = win
endif

CORE_DIR    += .
TARGET_NAME := retro8
LIBM		    = -lm

ifeq ($(STATIC_LINKING), 1)
EXT := a
endif

ifneq (,$(findstring unix,$(platform)))
	EXT ?= so
   TARGET := $(TARGET_NAME)_libretro.$(EXT)
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
   LIBS += -lpthread
else ifeq ($(platform), linux-portable)
   TARGET := $(TARGET_NAME)_libretro.$(EXT)
   fpic := -fPIC -nostdlib
   SHARED := -shared -Wl,--version-script=$(CORE_DIR)/link.T
	LIBM :=
else ifneq (,$(findstring osx,$(platform)))
   TARGET := $(TARGET_NAME)_libretro.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   MACSOSVER = `sw_vers -productVersion | cut -d. -f 1`
   OSXVER = `sw_vers -productVersion | cut -d. -f 2`
   OSX_LT_MAVERICKS = `(( $(OSXVER) <= 9)) && echo "YES"`

ifeq ($(UNIVERSAL),1)
ifeq ($(archs),ppc)
   ARCHFLAGS = -arch ppc -arch ppc64
else ifeq ($(archs),arm64)
   ARCHFLAGS = -arch arm64 -arch i386 -arch x86_64
else
   ARCHFLAGS = -arch i386 -arch x86_64
endif
endif

ifeq ($(OSX_LT_MAVERICKS),"NO")
   CXXFLAGS  += -stdlib=libc++
   CPPFLAGS  += -stdlib=libc++
   LDFLAGS   += -stdlib=libc++
endif


   ifeq ($(CROSS_COMPILE),1)
		TARGET_RULE   = -target $(LIBRETRO_APPLE_PLATFORM) -isysroot $(LIBRETRO_APPLE_ISYSROOT)
		CFLAGS   += $(TARGET_RULE)
		CPPFLAGS += $(TARGET_RULE)
		CXXFLAGS += $(TARGET_RULE)
		LDFLAGS  += $(TARGET_RULE)
   endif

	CFLAGS  += $(ARCHFLAGS)
	CXXFLAGS  += $(ARCHFLAGS)
	LDFLAGS += $(ARCHFLAGS)

else ifneq (,$(findstring ios,$(platform)))
   TARGET := $(TARGET_NAME)_libretro_ios.dylib
   fpic := -fPIC
   SHARED := -dynamiclib
   MINVERSION :=

ifeq ($(IOSSDK),)
   IOSSDK := $(shell xcodebuild -version -sdk iphoneos Path)
endif

   DEFINES := -DIOS

ifeq ($(platform),ios-arm64)
   CC = cc -arch arm64 -isysroot $(IOSSDK)
   CXX = c++ -arch arm64 -isysroot $(IOSSDK)
else
   CC = cc -arch armv7 -isysroot $(IOSSDK)
   CXX = c++ -arch armv7 -isysroot $(IOSSDK)
endif
ifeq ($(platform),$(filter $(platform),ios9 ios-arm64))
   MINVERSION = -miphoneos-version-min=8.0
   CXXFLAGS  += -stdlib=libc++
   CPPFLAGS  += -stdlib=libc++
   LDFLAGS   += -stdlib=libc++
else
   MINVERSION = -miphoneos-version-min=5.0
endif
   CFLAGS += $(MINVERSION)

else ifeq ($(platform), tvos-arm64)
   EXT?=dylib
   TARGET := $(TARGET_NAME)_libretro_tvos.$(EXT)
   fpic := -fPIC
   SHARED := -dynamiclib
   DEFINES := -DIOS
ifeq ($(IOSSDK),)
   IOSSDK := $(shell xcodebuild -version -sdk appletvos Path)
endif
   CC = cc -arch arm64 -isysroot $(IOSSDK)
   CXX = c++ -arch arm64 -isysroot $(IOSSDK)

else ifneq (,$(findstring qnx,$(platform)))
	TARGET := $(TARGET_NAME)_libretro_qnx.so
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
else ifeq ($(platform), emscripten)
   TARGET := $(TARGET_NAME)_libretro_emscripten.bc
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
else ifeq ($(platform), libnx)
   include $(DEVKITPRO)/libnx/switch_rules
   TARGET := $(TARGET_NAME)_libretro_$(platform).a
   DEFINES := -DSWITCH=1 -D__SWITCH__ -DARM
   CFLAGS := $(DEFINES) -fPIE -I$(LIBNX)/include/ -ffunction-sections -fdata-sections -ftls-model=local-exec
   CFLAGS += -march=armv8-a -mtune=cortex-a57 -mtp=soft -mcpu=cortex-a57+crc+fp+simd -ffast-math
   CXXFLAGS := $(ASFLAGS) $(CFLAGS)
   STATIC_LINKING = 1
else ifeq ($(platform), retrofw)
   EXT ?= so
   TARGET := $(TARGET_NAME)_libretro.$(EXT)
   CC = /opt/retrofw-toolchain/usr/bin/mipsel-linux-gcc
   CXX = /opt/retrofw-toolchain/usr/bin/mipsel-linux-g++
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
   LIBS += -lpthread
   CFLAGS += -fomit-frame-pointer -ffast-math -march=mips32 -mtune=mips32 -mhard-float
   CFLAGS += -ffunction-sections -fdata-sections -flto -mplt 
   CFLAGS += -falign-functions=1 -falign-jumps=1 -falign-loops=1
   CFLAGS += -fomit-frame-pointer -ffast-math -fmerge-all-constants 
   CFLAGS += -funsafe-math-optimizations -fsingle-precision-constant -fexpensive-optimizations
   CFLAGS += -fno-unwind-tables -fno-asynchronous-unwind-tables 
   CFLAGS += -fmerge-all-constants -fno-math-errno -fno-stack-protector -fno-ident    
   CXXFLAGS := $(ASFLAGS) $(CFLAGS)
else ifeq ($(platform), miyoo)
   EXT ?= so
   TARGET := $(TARGET_NAME)_libretro.$(EXT)
   CC = /opt/miyoo/usr/bin/arm-linux-gcc
   CXX = /opt/miyoo/usr/bin/arm-linux-g++
   fpic := -fPIC
   SHARED := -shared -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
   LIBS += -lpthread
   CFLAGS += -fomit-frame-pointer -ffast-math -march=armv5te -mtune=arm926ej-s
   CFLAGS += -ffunction-sections -fdata-sections -flto 
   CFLAGS += -falign-functions=1 -falign-jumps=1 -falign-loops=1
   CFLAGS += -fomit-frame-pointer -ffast-math -fmerge-all-constants 
   CFLAGS += -funsafe-math-optimizations -fsingle-precision-constant -fexpensive-optimizations
   CFLAGS += -fno-unwind-tables -fno-asynchronous-unwind-tables 
   CFLAGS += -fmerge-all-constants -fno-math-errno -fno-stack-protector -fno-ident    
   CXXFLAGS := $(ASFLAGS) $(CFLAGS)
else ifeq ($(platform), vita)
   TARGET := $(TARGET_NAME)_vita.a
   CC = arm-vita-eabi-gcc
   AR = arm-vita-eabi-ar
   CXXFLAGS += -Wl,-q -Wall -O3
	STATIC_LINKING = 1
else
   CC ?= gcc
   TARGET := $(TARGET_NAME)_libretro.dll
   SHARED := -shared -static-libgcc -static-libstdc++ -s -Wl,--version-script=$(CORE_DIR)/link.T -Wl,--no-undefined
endif

LDFLAGS += $(LIBM)

ifeq ($(DEBUG), 1)
   CFLAGS += -O0 -g -DDEBUG
   CXXFLAGS += -O0 -g -DDEBUG
else ifeq ($(platform), retrofw)
	CFLAGS += -Ofast
	CXXFLAGS += -Ofast
else ifeq ($(platform), miyoo)
   CFLAGS += -Ofast
   CXXFLAGS += -Ofast
else
   CFLAGS += -O3
   CXXFLAGS += -O3
endif

include Makefile.common

OBJECTS := $(SOURCES_C:.c=.o) $(SOURCES_CXX:.cpp=.o)

CFLAGS   += -Wall -D__LIBRETRO__ $(fpic) $(INCFLAGS) 
CXXFLAGS += -Wall -D__LIBRETRO__ $(fpic) $(INCFLAGS) -std=c++14

all: $(TARGET)

$(TARGET): $(OBJECTS)
ifeq ($(STATIC_LINKING), 1)
	$(AR) rcs $@ $(OBJECTS)
else
	$(CXX) $(fpic) $(SHARED) -o $@ $(OBJECTS) $(LIBS) $(LDFLAGS)
endif


%.o: %.c
	$(CC) $(CFLAGS) $(fpic) -c -o $@ $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(fpic) -c -o $@ $<

clean:
	rm -f $(OBJECTS) $(TARGET)

.PHONY: clean

print-%:
	@echo '$*=$($*)'
