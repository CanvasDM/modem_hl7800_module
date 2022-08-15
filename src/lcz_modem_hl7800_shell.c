/**
 * @file lcz_modem_hl7800.c
 *
 * Copyright (c) 2022 Laird Connectivity
 *
 * SPDX-License-Identifier: LicenseRef-LairdConnectivity-Clause
 */

/**************************************************************************************************/
/* Includes                                                                                       */
/**************************************************************************************************/
#include <logging/log.h>
LOG_MODULE_REGISTER(lcz_hl7800_shell, CONFIG_LCZ_MODEM_HL7800_LOG_LEVEL);

#include <zephyr.h>
#include <zephyr/drivers/modem/hl7800.h>
#include <shell/shell.h>
#include <stdlib.h>
#include "attr.h"

/**************************************************************************************************/
/* Local Constant, Macro and Type Definitions                                                     */
/**************************************************************************************************/
#define ARG_STR(x) x ? x : "null"
#define AT_CMD_LOG_DBG_MIN_SECONDS 1
#define CMD_OK_MSG "Ok"

/**************************************************************************************************/
/* Local Function Prototypes                                                                      */
/**************************************************************************************************/
static void log_restore_handler(struct k_work *work);

/**************************************************************************************************/
/* Local Data Definitions                                                                         */
/**************************************************************************************************/
static K_WORK_DELAYABLE_DEFINE(log_work, log_restore_handler);

/**************************************************************************************************/
/* Local Function Definitions                                                                     */
/**************************************************************************************************/
static void log_restore_handler(struct k_work *work)
{
	ARG_UNUSED(work);

	(void)mdm_hl7800_log_filter_set(attr_get_uint32(ATTR_ID_lte_log_lvl, LOG_LEVEL_DBG));
}

/**
 * @note The log level cannot be set higher than its compiled level.
 * Viewing the response to many AT commands requires that the log level
 * is DEBUG.
 */
static int shell_send_at_cmd(const struct shell *shell, size_t argc, char **argv)
{
	int ret = 0;
	long log_restore_delay;

	if ((argc == 3) && (argv[1] != NULL) && (argv[2] != NULL)) {
		mdm_hl7800_log_filter_set(LOG_LEVEL_DBG);
		ret = mdm_hl7800_send_at_cmd(argv[2]);
		if (ret < 0) {
			shell_error(shell, "Command not accepted");
		}
		log_restore_delay = strtol(argv[1], NULL, 0);
		log_restore_delay = MAX(AT_CMD_LOG_DBG_MIN_SECONDS, log_restore_delay);
		k_work_schedule(&log_work, K_SECONDS(log_restore_delay));
	} else {
		shell_error(shell,
			    "Invalid parameter"
			    "argc: %d argv[0]: %s argv[1]: %s argv[2]: %s",
			    argc, ARG_STR(argv[0]), ARG_STR(argv[1]), ARG_STR(argv[2]));

		ret = -EINVAL;
	}

	return ret;
}

#ifdef CONFIG_MODEM_HL7800_FW_UPDATE
static int shell_hl_fup_cmd(const struct shell *shell, size_t argc, char **argv)
{
	int ret = 0;

	if ((argc == 2) && (argv[1] != NULL)) {
		ret = mdm_hl7800_update_fw(argv[1]);
		if (ret < 0) {
			shell_error(shell, "Command error");
		}
	} else {
		shell_error(shell, "Invalid parameter");
		ret = -EINVAL;
	}

	return ret;
}
#endif /* CONFIG_MODEM_HL7800_FW_UPDATE */

static int shell_hl_off_cmd(const struct shell *shell, size_t argc, char **argv)
{
	int ret = 0;

	ret = mdm_hl7800_power_off();
	if (ret != 0) {
		shell_error(shell, "Error [%d]", ret);
	} else {
		shell_print(shell, CMD_OK_MSG);
	}

	return ret;
}

static int shell_hl_reset_cmd(const struct shell *shell, size_t argc, char **argv)
{
	int ret = 0;

	ret = mdm_hl7800_reset();
	if (ret != 0) {
		shell_error(shell, "Error [%d]", ret);
	} else {
		shell_print(shell, CMD_OK_MSG);
	}

	return ret;
}

static int shell_hl_site_survey_cmd(const struct shell *shell, size_t argc, char **argv)
{
	int ret = 0;

	shell_print(shell, "survey status: %d", mdm_hl7800_perform_site_survey());

	/* Results are printed to shell by lcz_modem_hl7800.site_survey_handler() */

	return ret;
}

/**************************************************************************************************/
/* Global Function Definitions                                                                    */
/**************************************************************************************************/

SHELL_STATIC_SUBCMD_SET_CREATE(
	hl_cmds,
	SHELL_CMD(cmd, NULL,
		  "CAUTION: Only use this for advanced debug!\r\n"
		  "Send AT command\r\n"
		  "hl cmd <return to normal log level delay seconds> <at command>",
		  shell_send_at_cmd),
#ifdef CONFIG_MODEM_HL7800_FW_UPDATE
	SHELL_CMD(fup, NULL, "Update firmware", shell_hl_fup_cmd),
#endif
	SHELL_CMD(off, NULL, "Power off the HL7800. To turn the it back on, reset it.",
		  shell_hl_off_cmd),
	SHELL_CMD(reset, NULL, "Reset and reconfigure the HL7800", shell_hl_reset_cmd),
	SHELL_CMD(survey, NULL, "Site survey", shell_hl_site_survey_cmd),
	SHELL_SUBCMD_SET_END /* Array terminated. */
);

SHELL_CMD_REGISTER(hl, &hl_cmds, "HL7800 commands", NULL);
