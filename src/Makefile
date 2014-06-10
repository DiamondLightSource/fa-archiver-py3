# This Makefile is invoked by makefile after setting the working directory to
# the $(BUILD_DIR) directory.
ifndef TOP
$(error Do not call this make file directly)
endif

CC_NAME = $(shell $(CC) --version | sed 's/ .*//;q')
CC_VERSION = $(shell $(CC) --version | sed -r 's/[^ ]* [^ ]* ([^ ]*) .*/\1/;q')


PROGRAM_PREFIX = fa-

SHELL = bash
INSTALL = install

default:    # Default target comes first!

CFLAGS += -std=gnu99
CFLAGS += -O3
CFLAGS += -g

CFLAGS += -Werror
CFLAGS += -Wall -Wextra
CFLAGS += -Wno-unused-parameter
CFLAGS += -Wno-missing-field-initializers
CFLAGS += -Wundef
CFLAGS += -Wshadow
CFLAGS += -Wcast-align
CFLAGS += -Wwrite-strings
CFLAGS += -Wredundant-decls
CFLAGS += -Wmissing-prototypes
CFLAGS += -Wmissing-declarations
CFLAGS += -Wstrict-prototypes
CFLAGS += -Wcast-qual
CFLAGS += -Woverflow
CFLAGS += -Wconversion

CFLAGS_clang += -Wstrict-overflow=5
CFLAGS_clang += -Wmost -Weverything
CFLAGS_clang += -Wno-variadic-macros

CFLAGS += -msse
CFLAGS += -msse2
CFLAGS_cc += -mfpmath=sse
CFLAGS += -ffast-math
CFLAGS += -funsigned-char
# This configures flags as necessary for large file support on 32-bit.
CFLAGS += $(shell getconf LFS_CFLAGS)

# Compiler specific flags.
CFLAGS += $(CFLAGS_$(CC_NAME))
CFLAGS += $(CFLAGS_$(CC_NAME)-$(CC_VERSION))


CPPFLAGS += -I$(DEVICE_DIR)
CPPFLAGS += -D_GNU_SOURCE
CPPFLAGS += -D_FORTIFY_SOURCE=2

LDLIBS += -lpthread -lrt -lm


BUILD = archiver prepare capture

COMMON_SRCS += error.c              # Core error handling framework
COMMON_SRCS += locking.c            # Simple abstraction of pthread locking
COMMON_SRCS += parse.c              # Parsing support
COMMON_SRCS += mask.c               # BPM capture mask support

# FA sniffer archiver
archiver_SRCS += archiver.c         # Command line interface to archiver
archiver_SRCS += buffer.c           # Ring buffer for data capture
archiver_SRCS += sniffer.c          # Interface to FA sniffer driver
archiver_SRCS += gigabit.c          # Gigabit Ethernet interface
archiver_SRCS += disk_writer.c      # Core disk writing access
archiver_SRCS += disk.c             # Disk header format definitions
archiver_SRCS += socket_server.c    # Socket server
archiver_SRCS += subscribe.c        # Subscription to current data
archiver_SRCS += transform.c        # Data transformation and access
archiver_SRCS += pool.c             # Shared buffer pool for readers
archiver_SRCS += reader.c           # Sniffer data readout
archiver_SRCS += decimate.c         # Continuous data reduction
archiver_SRCS += config_file.c      # Config file parsing
archiver_SRCS += replay.c           # Replay canned data for debug
archiver_SRCS += matlab.c           # For reading canned matlab data

# FA archive preparation
prepare_SRCS += prepare.c           # Command line interface
prepare_SRCS += disk.c

# FA data capture
capture_SRCS += capture.c           # Command line interface
capture_SRCS += matlab.c            # Matlab header support


BUILD_NAMES = $(patsubst %,$(PROGRAM_PREFIX)%,$(BUILD))
default: $(BUILD_NAMES) check_alignment

define expand_build
$(PROGRAM_PREFIX)$(target): $(COMMON_SRCS:.c=.o) $($(target)_SRCS:.c=.o)
	$$(LINK.o) $$^ $$(LOADLIBES) $$(LDLIBS) -o $$@
endef
$(foreach target,$(BUILD),$(eval $(expand_build)))

%.d: %.c
	set -o pipefail && $(CC) -M $(CPPFLAGS) $(CFLAGS) $< | \
            sed '1s/:/ $@:/' >$@
include $(patsubst %.c,%.d,$(foreach target,$(BUILD),$($(target)_SRCS)))

# Target for assembler build for code generation inspection.
%.s: %.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -S $<

# Installation: we install all the built executables in $(SCRIPT_DIR)
install: $(BUILD_NAMES)
	$(INSTALL) -d $(SCRIPT_DIR)
	$(INSTALL) -m 555 $^ $(SCRIPT_DIR)


# Check that the preserved layout definition hasn't changed
check_alignment: layout layout.new
	diff $^

# Computes structure layout for structures defined in layout-list.
LAYOUT_DEPENDS = $(shell sed '/^\#/d; s/^[^ ]*//' $(srcdir)/layout-list)
layout.new: make-layout layout-list show-layout.c $(LAYOUT_DEPENDS)
	CC="$(CC)" CPPFLAGS="$(CPPFLAGS)" CFLAGS="$(CFLAGS)" \
            srcdir="$(srcdir)" $< >$@

VPATH += $(DEVICE_DIR)

.PHONY: default install check_alignment
.DELETE_ON_ERROR:
