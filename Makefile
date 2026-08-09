# SPDX-License-Identifier: GPL-2.0

ifneq ($(KERNELRELEASE),)

obj-m += guncon2.o

else

KERNEL_RELEASE ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KERNEL_RELEASE)/build
MODULE_DIR := $(CURDIR)
INSTALL_MOD_DIR ?= extra

DEPMOD ?= depmod
MODULE_INSTALL_DIR ?= /lib/modules/$(KERNEL_RELEASE)/$(INSTALL_MOD_DIR)

.PHONY: all modules install modules_install install-module uninstall \
	uninstall-module clean help

all: modules

modules:
	$(MAKE) -C "$(KDIR)" M="$(MODULE_DIR)" modules

install modules_install: install-module

install-module: modules
	$(MAKE) -C "$(KDIR)" M="$(MODULE_DIR)" \
		INSTALL_MOD_PATH="$(DESTDIR)" INSTALL_MOD_DIR="$(INSTALL_MOD_DIR)" \
		modules_install

uninstall: uninstall-module

uninstall-module:
	$(RM) "$(DESTDIR)$(MODULE_INSTALL_DIR)/guncon2.ko" \
		"$(DESTDIR)$(MODULE_INSTALL_DIR)/guncon2.ko.gz" \
		"$(DESTDIR)$(MODULE_INSTALL_DIR)/guncon2.ko.xz" \
		"$(DESTDIR)$(MODULE_INSTALL_DIR)/guncon2.ko.zst"
	$(DEPMOD) $(if $(DESTDIR),-b "$(DESTDIR)") -a "$(KERNEL_RELEASE)"

clean:
	$(MAKE) -C "$(KDIR)" M="$(MODULE_DIR)" clean

help:
	@echo "Targets:"
	@echo "  all                Build the kernel module (default)"
	@echo "  modules            Build the kernel module"
	@echo "  install            Build and install the module"
	@echo "  modules_install    Alias for install"
	@echo "  install-module     Build and install the module"
	@echo "  uninstall          Uninstall the module"
	@echo "  uninstall-module   Uninstall the module"
	@echo "  clean              Remove generated files"
	@echo "  help               Show this help"
	@echo
	@echo "Overrides:"
	@echo "  KERNEL_RELEASE=<version>  Select a kernel (default: $(KERNEL_RELEASE))"
	@echo "  KDIR=<path>               Select its build directory"
	@echo "  INSTALL_MOD_DIR=<name>    Select the module subdirectory"
	@echo "  DESTDIR=<path>            Stage installation under this directory"

endif
