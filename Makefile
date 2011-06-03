# This makefile simply reflects the build to the $(BUILD_DIR) directory so that
# we can make out of directory.  The slightly naughty trick here is that make
# will first look for makefile (rather than Makefile) and we can then redirect
# to the $(BUILD_DIR) directory.

TOP = $(CURDIR)

DEVICE_DIR = $(TOP)/device
BUILD_DIR = $(CURDIR)/build


# Any special installation instructions are picked up from Makefile.private
# which is created by the dls-release process.
#    The following symbols are used by this make file to configure the
# installation and should be defined in this file:
#
#    PREFIX         Root of installation
#    SCRIPT_DIR     Where the executables should be installed
#    PYTHON         Path to Python executable
-include $(TOP)/Makefile.private
PYTHON ?= python
SCRIPT_DIR ?= $(PREFIX)/bin
export PYTHON
export PREFIX
export SCRIPT_DIR


BIN_BUILD_DIR = $(BUILD_DIR)/$(shell uname -m)
DOCS_BUILD_DIR = $(BUILD_DIR)/docs

BIN_BUILD = \
    VPATH=$(CURDIR)/src $(MAKE) TOP=$(TOP) DEVICE_DIR=$(DEVICE_DIR) \
        -C $(BIN_BUILD_DIR) -f $(CURDIR)/src/Makefile
DOCS_BUILD = \
    VPATH=$(CURDIR)/docs $(MAKE) TOP=$(TOP) \
        -C $(DOCS_BUILD_DIR) -f $(CURDIR)/docs/Makefile

# Targets other than these are redirected to building the binary tools
NON_BIN_TARGETS = default clean install docs
BIN_TARGETS = $(filter-out $(NON_BIN_TARGETS) $(BIN_BUILD_DIR),$(MAKECMDGOALS))


default $(BIN_TARGETS): $(BIN_BUILD_DIR)
	$(BIN_BUILD) $@

docs: $(DOCS_BUILD_DIR)
	$(DOCS_BUILD)

$(BUILD_DIR)/%: $(BUILD_DIR)
	mkdir -p $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)

install: $(BIN_BUILD_DIR) $(DOCS_BUILD_DIR)
ifndef PREFIX
	@echo >&2 Must define PREFIX; false
endif
	$(BIN_BUILD) install
	$(DOCS_BUILD) install
	make -C $(TOP)/python install
	make -C $(TOP)/matlab install

.PHONY: default docs clean install
