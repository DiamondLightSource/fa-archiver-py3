# Top level make file.

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
#    MODULEVER      Module version for versioned Python install
PREFIX = $(BUILD_DIR)/prefix
PYTHON = dls-python
MODULEVER = 0.0
-include $(TOP)/Makefile.private
SCRIPT_DIR ?= $(PREFIX)/bin

export PYTHON
export PREFIX
export SCRIPT_DIR
export MODULEVER


BIN_BUILD_DIR = $(BUILD_DIR)/$(shell uname -m)
DOCS_BUILD_DIR = $(BUILD_DIR)/docs
MATLAB_BUILD_DIR = $(BUILD_DIR)/matlab
PYTHON_BUILD_DIR = $(BUILD_DIR)/python

VPATH_BUILD = \
    VPATH=$(CURDIR)/$1 $(MAKE) TOP=$(TOP) srcdir=$(CURDIR)/$1 \
        -C $2 -f $(CURDIR)/$1/Makefile
BIN_BUILD = \
    $(call VPATH_BUILD,src,$(BIN_BUILD_DIR)) DEVICE_DIR=$(DEVICE_DIR)
DOCS_BUILD = \
    $(call VPATH_BUILD,docs,$(DOCS_BUILD_DIR))
MATLAB_BUILD = \
    $(call VPATH_BUILD,matlab,$(MATLAB_BUILD_DIR))
PYTHON_BUILD = \
    make -C python PYTHON_BUILD_DIR=$(PYTHON_BUILD_DIR)

# Targets other than these are redirected to building the binary tools
NON_BIN_TARGETS = default clean install docs matlab python
BIN_TARGETS = $(filter-out $(NON_BIN_TARGETS) $(BIN_BUILD_DIR),$(MAKECMDGOALS))


default $(BIN_TARGETS): $(BIN_BUILD_DIR)
	$(BIN_BUILD) $@

docs: $(DOCS_BUILD_DIR)
	$(DOCS_BUILD)

matlab: $(MATLAB_BUILD_DIR)
	$(MATLAB_BUILD)

python: $(PYTHON_BUILD_DIR)
	$(PYTHON_BUILD)

$(BUILD_DIR)/%:
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR)
	$(PYTHON_BUILD) clean

install: $(BIN_BUILD_DIR) $(DOCS_BUILD_DIR) $(MATLAB_BUILD_DIR) python
	$(BIN_BUILD) install
	$(DOCS_BUILD) install
	$(PYTHON_BUILD) install
	$(MATLAB_BUILD) install

.PHONY: $(NON_BIN_TARGETS)
