
###############################################################################
# Basic Makefile
###############################################################################

BUILDROOT_PREFIX=/home/niklas/buildroot/buildroot-2018.08.4

cc.SC1000 := LD_LIBRARY_PATH=$(BUILDROOT_PREFIX)/output/target/usr/lib $(BUILDROOT_PREFIX)/output/host/usr/bin/arm-linux-gcc
cc.NATIVE := $(CC)

cxx.SC1000 := LD_LIBRARY_PATH=$(BUILDROOT_PREFIX)/output/target/usr/lib $(BUILDROOT_PREFIX)/output/host/usr/bin/arm-linux-g++
cxx.NATIVE := $(CXX)

opt_flags.SC1000 := -mcpu=cortex-a8 -mfpu=neon -flto -ftree-vectorize
opt_flags.NATIVE := -flto -ftree-vectorize

CC := $(cc.$(ARCH))
CXX := $(cxx.$(ARCH))
OPT_FLAGS := $(opt_flags.$(ARCH))

ALSA_LIBS ?= -lasound
DEVICE_CFLAGS = -DWITH_ALSA
DEVICE_LIBS = $(ALSA_LIBS)

# Use Bash
SHELL = /bin/sh

# Functions
find_includes_in_dir = $(shell find $(1) -name "*.h" | sed 's|/[^/]*$$||' | sort -u)

# ---------------------------------------------------------------------
# Toolchain Configuration
# ---------------------------------------------------------------------
C_STANDARD              := -std=gnu11
CXX_STANDARD            := -std=gnu++17

# -----------------------------------------------------------------------------------------------------------------
# Defined Symbols
# -----------------------------------------------------------------------------------------------------------------
DEFS                    :=

# ---------------------------------------------------------------------------------------------------------------------------------------
# Compiler & Linker Flags
# ---------------------------------------------------------------------------------------------------------------------------------------
# Flags sent to all tools in the Toolchain
TOOLCHAIN_SETTINGS      := -fmessage-length=0

# C Compiler -- Warnings
CFLAGS                  += $(TOOLCHAIN_SETTINGS) $(DEFS) $(addprefix -I, $(INC_DIRS))
CFLAGS                  += -Ideps
CFLAGS                  += -Wall
CFLAGS                  += -Wextra
CFLAGS                  += -Wfatal-errors
CFLAGS                  += -Wpacked
CFLAGS                  += -Winline
CFLAGS                  += -Wfloat-equal
CFLAGS                  += -Wconversion
CFLAGS                  += -Wpointer-arith
CFLAGS                  += -Wdisabled-optimization
CFLAGS                  += -Wno-unused-parameter
CFLAGS                  += $(OPT_FLAGS)
CFLAGS                  += $(DEVICE_CFLAGS)

# C++ Compiler -- Required & Optimization Flags
CXXFLAGS                += $(CFLAGS)

# C++ -- Warnings
CXXFLAGS                += -Weffc++
CXXFLAGS                += -Wfloat-equal
CXXFLAGS                += -Wsign-promo
CXXFLAGS                += -Wmissing-declarations
CXXFLAGS                += -Woverloaded-virtual
CXXFLAGS                += -Wmissing-format-attribute
CXXFLAGS                += -Wold-style-cast
CXXFLAGS                += -Wshadow
CXXFLAGS                += -Wctor-dtor-privacy
CXXFLAGS                += -static-libstdc++ -static-libgcc

# Linker
LDFLAGS                 += $(TOOLCHAIN_SETTINGS) $(DEFS)
LDFLAGS                 += $(OPT_FLAGS)
LDFLAGS                 += $(DEVICE_LIBS) -lm -static-libstdc++ -static-libgcc

# -------------------------------------------------------------
# Build Type Modifiers
# -------------------------------------------------------------
# Debug
DEFS_DEBUG              := -DDEBUG
CFLAGS_DEBUG            := -ggdb -g3 -Og

# Release
CFLAGS_RELEASE          := -O3

#########################################################################################################################################
# RULE DEFINITIONS -- This section is generic
#########################################################################################################################################

# =======================================================================================================================================
# Build Configuration Rule
# - Generate build config using Product Root Directory ($1), Build Type ("Debug" or "Release") ($2)
# =======================================================================================================================================
define CONFIG_RULE
BUILD_DIR               := $1/Build/$2
OBJ_DIR                 := $$(BUILD_DIR)/obj
INC_DIRS                := $$(call find_includes_in_dir, $$(SRC_DIRS))
HEADERS                 := $$(foreach dir, $$(SRC_DIRS), $$(shell find $$(dir) -name "*.h"))
ASM_SRC                 := $$(foreach dir, $$(SRC_DIRS), $$(shell find $$(dir) -name "*.s"))
C_SRC                   := $$(foreach dir, $$(SRC_DIRS), $$(shell find $$(dir) -name "*.c"))
CXX_SRC                 := $$(foreach dir, $$(SRC_DIRS), $$(shell find $$(dir) -name "*.cpp"))
OBJECTS                 := $$(addprefix $$(OBJ_DIR)/, $$(C_SRC:.c=.o) $$(CXX_SRC:.cpp=.o) $$(ASM_SRC:.s=.o))
LDSCRIPTS               := $$(addprefix -T, $$(foreach dir, $$(SRC_DIRS), $$(shell find $$(dir) -name "*.ld")))
DIRS                    := $$(BUILD_DIR) $$(sort $$(dir $$(OBJECTS)))
AUTODEPS                := $$(OBJECTS:.o=.d)


ifeq ($2, Release)
    DEFS    += $$(DEFS_RELEASE)
    CFLAGS  += $$(CFLAGS_RELEASE)
    LDFLAGS += $$(LDFLAGS_RELEASE)
else
    DEFS    += $$(DEFS_DEBUG)
    CFLAGS  += $$(CFLAGS_DEBUG)
    LDFLAGS += $$(LDFLAGS_DEBUG)
endif

endef
# =======================================================================================================================================
# End CONFIG_RULE
# =======================================================================================================================================


# =======================================================================================================================================
# Build Target Rule
# - Generate build config using Product Name ($1), Product Root Directory ($2), Build Type ("Debug" or "Release") ($3)
# =======================================================================================================================================
define BUILD_TARGET_RULE
$(eval $(call CONFIG_RULE,$2,$3))

all : $$(BUILD_DIR)/$1

# Tool Invocations
$$(BUILD_DIR)/$1 : $$(OBJECTS) | $$(BUILD_DIR)
	@echo ' '
	@echo 'Building $$(@)'
	@echo 'Invoking: C++ Linker'
	$$(CXX) $$(LDSCRIPTS) -o $$(@) $$(OBJECTS) $$(LDFLAGS)
	@echo 'Finished building: $$@'
	@echo ' '

$$(OBJECTS) : | $$(DIRS)

$$(DIRS) :
	@echo Creating $$(@)
	@mkdir -p $$(@)

$$(OBJ_DIR)/%.o : %.c
	@echo Compiling $$(<F)
	@$$(CC) $$(C_STANDARD) $$(CFLAGS) -c -MMD -MP $$< -o $$(@)

$$(OBJ_DIR)/%.o : %.cpp
	@echo Compiling $$(<F)
	@$$(CXX) $$(CXX_STANDARD) $$(CXXFLAGS) -c -MMD -MP $$< -o $$(@)

$$(OBJ_DIR)/%.o : %.s
	@echo Assembling $$(<F)
	@$$(AS) $$(ASFLAGS) $$< -o $$(@)

clean :
	@rm -rf $$(PRODUCT_DIR)/Build

.PHONY : clean all

# include by auto dependencies
-include $$(AUTODEPS)

endef
# =======================================================================================================================================
# End BUILD_TARGET_RULE
# =======================================================================================================================================
#########################################################################################################################################
#########################################################################################################################################

# Build Type
ifeq ($(build), Debug)
	BUILD_TYPE := Debug
else
	BUILD_TYPE := Release
endif


# Defaults
PRODUCT ?= sc1000
PRODUCT_DIR ?= src
BUILD_TYPE ?= Debug
SRC_DIRS ?= $(PRODUCT_DIR)

# Evaluate Rules Defined Above
$(eval $(call BUILD_TARGET_RULE,$(PRODUCT),$(PRODUCT_DIR),$(BUILD_TYPE)))



