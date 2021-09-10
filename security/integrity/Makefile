# SPDX-License-Identifier: GPL-2.0
#
# Makefile for caching inode integrity data (iint)
#

quiet_cmd_extract_certs  = EXTRACT_CERTS   $(patsubst "%",%,$(2))
      cmd_extract_certs  = scripts/extract-cert $(2) $@
$(eval $(call config_filename,INTEGRITY_PLATFORM_BUILTIN_KEYS))

obj-$(CONFIG_INTEGRITY) += integrity.o

integrity-y := iint.o
integrity-$(CONFIG_INTEGRITY_AUDIT) += integrity_audit.o
integrity-$(CONFIG_INTEGRITY_SIGNATURE) += digsig.o
integrity-$(CONFIG_INTEGRITY_ASYMMETRIC_KEYS) += digsig_asymmetric.o
integrity-$(CONFIG_INTEGRITY_PLATFORM_KEYRING) += platform_certs/platform_keyring.o \
						  platform_certs/platform_cert.o
integrity-$(CONFIG_LOAD_UEFI_KEYS) += platform_certs/efi_parser.o \
				      platform_certs/load_uefi.o \
				      platform_certs/keyring_handler.o
integrity-$(CONFIG_LOAD_IPL_KEYS) += platform_certs/load_ipl_s390.o
integrity-$(CONFIG_LOAD_PPC_KEYS) += platform_certs/efi_parser.o \
                                     platform_certs/load_powerpc.o \
                                     platform_certs/keyring_handler.o
obj-$(CONFIG_IMA)			+= ima/
obj-$(CONFIG_EVM)			+= evm/


$(obj)/platform_certs/platform_cert.o: $(obj)/platform_certs/platform_certificate_list

targets += platform_certificate_list

$(obj)/platform_certs/platform_certificate_list: scripts/extract-cert $(INTEGRITY_PLATFORM_BUILTIN_KEYS_FILENAME) FORCE
	$(call if_changed,extract_certs,$(CONFIG_INTEGRITY_PLATFORM_BUILTIN_KEYS))

clean-files := platform_certs/platform_certificate_list
