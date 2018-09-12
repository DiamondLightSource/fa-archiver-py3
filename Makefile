# Top level make file.

TOP = $(CURDIR)

DEVICE_DIR = $(TOP)/device
BUILD_DIR = $(CURDIR)/build

# Unfortunately, due in part I think to confused documentation from I-Tech,
# there appear be two different implementations of Fast Acquisition data over
# gigabit ethernet, otherwise known as Libera Grouping.  For details look at
# src/libera-grouping.h, but in brief, set LIBERA_GROUPING to 0 for the older
# compatibility, to 1 for newer systems (including Brilliance+).
LIBERA_GROUPING = 1

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
export LIBERA_GROUPING


BIN_BUILD_DIR = $(BUILD_DIR)/$(shell uname -m)
DOCS_BUILD_DIR = $(BUILD_DIR)/docs
MATLAB_BUILD_DIR = $(BUILD_DIR)/matlab
PYTHON_BUILD_DIR = $(BUILD_DIR)/python
COVERITY_BUILD_DIR = $(BUILD_DIR)/coverity

VPATH_BUILD = \
    $(MAKE) VPATH=$(CURDIR)/$1 TOP=$(TOP) srcdir=$(CURDIR)/$1 \
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
NON_BIN_TARGETS = default clean install docs matlab python coverity
BIN_TARGETS = $(filter-out $(NON_BIN_TARGETS) $(BIN_BUILD_DIR),$(MAKECMDGOALS))


default $(BIN_TARGETS): $(BIN_BUILD_DIR)
	$(BIN_BUILD) $@

docs: $(DOCS_BUILD_DIR)
	$(DOCS_BUILD)

matlab: $(MATLAB_BUILD_DIR)
	$(MATLAB_BUILD)

python: $(PYTHON_BUILD_DIR)
	$(PYTHON_BUILD)

coverity:
	rm -rf $(COVERITY_BUILD_DIR)
	mkdir -p $(COVERITY_BUILD_DIR)
	$(COVERITY_BUILD)
	cd $(COVERITY_BUILD_DIR)  &&  cov-build --dir cov-int \
            $(call VPATH_BUILD,src,.) DEVICE_DIR=$(DEVICE_DIR)
	tar czf fa-archiver.coverity.tgz -C $(COVERITY_BUILD_DIR) cov-int
	echo 'Upload to https://scan.coverity.com/projects/2415/builds/new'

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
