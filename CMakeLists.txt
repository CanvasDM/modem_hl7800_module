#
# Copyright (c) 2022 Laird Connectivity LLC
#
# SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
#
if(CONFIG_LCZ_MODEM_HL7800)
    zephyr_sources(src/lcz_modem_hl7800.c)
    zephyr_sources_ifdef(CONFIG_LCZ_MODEM_HL7800_SHELL src/lcz_modem_hl7800_shell.c)
endif()
