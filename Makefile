# This makefile simply reflects the build to the $(BUILD_DIR) directory so that
# we can make out of directory.  The slightly naughty trick here is that make
# will first look for makefile (rather than Makefile) and we can then redirect
# to the $(BUILD_DIR) directory.

TOP = $(CURDIR)
BUILD_DIR = $(CURDIR)/build
SRCDIR = $(CURDIR)/src

MAKE_BUILD = \
    VPATH=$(SRCDIR) $(MAKE) TOP=$(TOP) -C $(BUILD_DIR) -f $(SRCDIR)/Makefile

all $(filter-out all clean install $(BUILD_DIR),$(MAKECMDGOALS)): $(BUILD_DIR)
	$(MAKE_BUILD) $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

install:
	$(MAKE_BUILD) install
	make -C $(TOP)/python install
	make -C $(TOP)/matlab install

.PHONY: all clean install
