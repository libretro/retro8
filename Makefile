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

GIT_VERSION := " $(shell git rev-parse --short HEAD || echo unknown)"
ifneq ($(GIT_VERSION)," unknown")
	CXXFLAGS += -DGIT_VERSION=\"$(GIT_VERSION)\"
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
# Nintendo Game Cube
else ifeq ($(platform), ngc)
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	CC = $(DEVKITPPC)/bin/powerpc-eabi-gcc$(EXE_EXT)
	CXX = $(DEVKITPPC)/bin/powerpc-eabi-g++$(EXE_EXT)
	AR = $(DEVKITPPC)/bin/powerpc-eabi-ar$(EXE_EXT)
	CFLAGS += -DGEKKO -DHW_DOL -mrvl -mcpu=750 -meabi -mhard-float
	CXXFLAGS += -DGEKKO -DHW_DOL -mrvl -mcpu=750 -meabi -mhard-float
	HAVE_RZLIB := 1
	STATIC_LINKING=1
# Nintendo Wii
else ifeq ($(platform), wii)
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	CC = $(DEVKITPPC)/bin/powerpc-eabi-gcc$(EXE_EXT)
	CXX = $(DEVKITPPC)/bin/powerpc-eabi-g++$(EXE_EXT)
	AR = $(DEVKITPPC)/bin/powerpc-eabi-ar$(EXE_EXT)
	CFLAGS += -DGEKKO -DHW_RVL -mrvl -mcpu=750 -meabi -mhard-float
	CXXFLAGS += -DGEKKO -DHW_RVL -mrvl -mcpu=750 -meabi -mhard-float
	HAVE_RZLIB := 1
	STATIC_LINKING=1
# Nintendo WiiU
else ifeq ($(platform), wiiu)
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	CC = $(DEVKITPPC)/bin/powerpc-eabi-gcc$(EXE_EXT)
	CXX = $(DEVKITPPC)/bin/powerpc-eabi-g++$(EXE_EXT)
	AR = $(DEVKITPPC)/bin/powerpc-eabi-ar$(EXE_EXT)
	CFLAGS += -DGEKKO -DWIIU -DHW_RVL -mwup -mcpu=750 -meabi -mhard-float
	CXXFLAGS += -DGEKKO -DWIIU -DHW_RVL -mcpu=750 -meabi -mhard-float
	CXXFLAGS += -ffunction-sections -fdata-sections -D__wiiu__ -D__wut__
	HAVE_RZLIB := 1
	STATIC_LINKING=1
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
   TARGET := $(TARGET_NAME)_libretro_vita.a
   CC = arm-vita-eabi-gcc
   CXX = arm-vita-eabi-g++
   AR = arm-vita-eabi-ar
   CXXFLAGS += -Wl,-q -Wall -O3
	STATIC_LINKING = 1
# PSP
else ifeq ($(platform), psp1)
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	CC = psp-gcc
	CXX = psp-g++
	AR = psp-ar
	CFLAGS += -G0 -DPSP -DUSE_RGB565
	CXXFLAGS += -G0 -DPSP -DUSE_RGB565
	STATIC_LINKING=1
# CTR/3DS
else ifeq ($(platform), ctr)
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	CC = $(DEVKITARM)/bin/arm-none-eabi-gcc$(EXE_EXT)
	CXX = $(DEVKITARM)/bin/arm-none-eabi-g++$(EXE_EXT)
	AR = $(DEVKITARM)/bin/arm-none-eabi-ar$(EXE_EXT)
	CFLAGS += -DARM11 -D_3DS
	CFLAGS += -march=armv6k -mtune=mpcore -mfloat-abi=hard
	CFLAGS += -Wall -mword-relocations
	CFLAGS += -fomit-frame-pointer -ffast-math
	CXXFLAGS += -DARM11 -D_3DS
	CXXFLAGS += -march=armv6k -mtune=mpcore -mfloat-abi=hard
	CXXFLAGS += -Wall -mword-relocations
	CXXFLAGS += -fomit-frame-pointer -ffast-math
	HAVE_RZLIB := 1
	DISABLE_ERROR_LOGGING := 1
	ARM = 1
	STATIC_LINKING=1
# GCW0
else ifeq ($(platform), gcw0)
	TARGET := $(TARGET_NAME)_libretro.so
	CC = /opt/gcw0-toolchain/usr/bin/mipsel-linux-gcc
	AR = /opt/gcw0-toolchain/usr/bin/mipsel-linux-ar
	fpic := -fPIC
	SHARED := -shared -Wl,--version-script=link.T -Wl,-no-undefined
	DISABLE_ERROR_LOGGING := 1
	CFLAGS += -march=mips32 -mtune=mips32r2 -mhard-float
	LIBS = -lm
# RS90
else ifeq ($(platform), rs90)
   TARGET := $(TARGET_NAME)_libretro.so
   CC = /opt/rs90-toolchain/usr/bin/mipsel-linux-gcc
   CXX = /opt/rs90-toolchain/usr/bin/mipsel-linux-g++
   AR = /opt/rs90-toolchain/usr/bin/mipsel-linux-ar
   fpic := -fPIC
   SHARED := -shared -Wl,-version-script=$(CORE_DIR)/link.T
   CFLAGS += -fomit-frame-pointer -ffast-math -march=mips32 -mtune=mips32
   CXXFLAGS += $(CFLAGS)
# PS2
else ifeq ($(platform), ps2)
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	CC = mips64r5900el-ps2-elf-gcc
	CXX = mips64r5900el-ps2-elf-g++
	AR = mips64r5900el-ps2-elf-ar
	CFLAGS += -G0 -DPS2 -DUSE_RGB565 -DABGR1555
	CXXFLAGS += -G0 -DPS2 -DUSE_RGB565 -DABGR1555
	STATIC_LINKING=1
# DOS
else ifeq ($(platform), dos)
	TARGET := $(TARGET_NAME)_libretro_$(platform).a
	CC = i586-pc-msdosdjgpp-gcc
	AR = i586-pc-msdosdjgpp-ar
	CXX = i586-pc-msdosdjgpp-g++
	PLATFORM_DEFINES += -march=i386
	STATIC_LINKING=1
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
