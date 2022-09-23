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
LOG_MODULE_REGISTER(lcz_hl7800, CONFIG_LCZ_MODEM_HL7800_LOG_LEVEL);

#include <zephyr.h>
#include <init.h>
#include <stdio.h>
#include <stdlib.h>
#include <zephyr/drivers/modem/hl7800.h>
#include <shell/shell_uart.h>
#include "attr.h"
#include "system_message_task.h"
#include "lcz_memfault.h"

/**************************************************************************************************/
/* Local Constant, Macro and Type Definitions                                                     */
/**************************************************************************************************/
#define CHECK_FOR_ERR_AND_PRINT(code, msg)                                                         \
	do {                                                                                       \
		if (code < 0) {                                                                    \
			LOG_ERR(msg " [%d]", code);                                                \
		}                                                                                  \
	} while (false);

/**************************************************************************************************/
/* Local Function Prototypes                                                                      */
/**************************************************************************************************/
static int lcz_modem_hl7800_init(const struct device *device);
static void hl7800_event_callback(enum mdm_hl7800_event event, void *event_data);
static void site_survey_handler(void *event_data);
static void attr_changed_callback(const attr_id_t *id_list, size_t list_count, void *context);

/**************************************************************************************************/
/* Local Data Definitions                                                                         */
/**************************************************************************************************/
static struct mdm_hl7800_callback_agent hl7800_evt_agent;
static bool log_lte_dropped = false;
static struct mdm_hl7800_apn *lte_apn_config;
static struct mdm_hl7800_event_socket_stats *socket_stats;
static struct smt_attr_changed_agent attr_event_agent;

/**************************************************************************************************/
/* Local Function Definitions                                                                     */
/**************************************************************************************************/
static void attr_changed_callback(const attr_id_t *id_list, size_t list_count, void *context)
{
	int i;
	int ret;

	for (i = 0; i < list_count; i++) {
		switch (id_list[i]) {
		case ATTR_ID_lte_apn:
			ret = mdm_hl7800_update_apn((char *)attr_get_quasi_static(ATTR_ID_lte_apn));
			CHECK_FOR_ERR_AND_PRINT(ret, "Set APN");
			break;
		case ATTR_ID_lte_bands:
			ret = mdm_hl7800_set_bands(
				(char *)attr_get_quasi_static(ATTR_ID_lte_bands));
			CHECK_FOR_ERR_AND_PRINT(ret, "Set Bands");
			break;
		case ATTR_ID_lte_log_lvl:
			ret = mdm_hl7800_log_filter_set(
				attr_get_uint32(ATTR_ID_lte_log_lvl, LOG_LEVEL_DBG));
			CHECK_FOR_ERR_AND_PRINT(ret, "Change log lvl");
			break;
		case ATTR_ID_lte_rat:
			ret = mdm_hl7800_update_rat((enum mdm_hl7800_radio_mode)attr_get_uint32(
				ATTR_ID_lte_rat, MDM_RAT_CAT_M1));
			CHECK_FOR_ERR_AND_PRINT(ret, "Change RAT");
			break;
		default:
			break;
		}
	}
}

/* Site survey is initiated by modem shell */
static void site_survey_handler(void *event_data)
{
#ifdef CONFIG_SHELL_BACKEND_SERIAL
	struct mdm_hl7800_site_survey *p = event_data;
	const struct shell *shell = shell_backend_uart_get_ptr();

	if (p != NULL) {
		shell_print(shell, "EARFCN: %u", p->earfcn);
		shell_print(shell, "Cell Id: %u", p->cell_id);
		shell_print(shell, "RSRP: %d", p->rsrp);
		shell_print(shell, "RSRQ: %d", p->rsrq);
	}
#else
	ARG_UNUSED(event_data);
#endif
}

static void hl7800_event_callback(enum mdm_hl7800_event event, void *event_data)
{
	uint8_t code = ((struct mdm_hl7800_compound_event *)event_data)->code;
	char *s = (char *)event_data;

	switch (event) {
	case HL7800_EVENT_NETWORK_STATE_CHANGE:
		attr_set_uint32(ATTR_ID_lte_network_state, code);
		switch (code) {
		case HL7800_HOME_NETWORK:
		case HL7800_ROAMING:
			log_lte_dropped = true;
			break;
		case HL7800_NOT_REGISTERED:
		case HL7800_REGISTRATION_DENIED:
		case HL7800_UNABLE_TO_CONFIGURE:
		case HL7800_OUT_OF_COVERAGE:
			if ((code == HL7800_OUT_OF_COVERAGE || code == HL7800_NOT_REGISTERED) &&
			    log_lte_dropped) {
				log_lte_dropped = false;
				MFLT_METRICS_ADD(lte_drop, 1);
			}
			break;
		case HL7800_SEARCHING:
		case HL7800_EMERGENCY:
		default:
			break;
		}
		break;
	case HL7800_EVENT_APN_UPDATE:
		lte_apn_config = (struct mdm_hl7800_apn *)event_data;
		attr_set_without_broadcast(ATTR_ID_lte_apn, ATTR_TYPE_STRING, lte_apn_config->value,
					   strlen(lte_apn_config->value), NULL);
		break;
	case HL7800_EVENT_RSSI:
		attr_set_signed32(ATTR_ID_lte_rsrp, *((int *)event_data));
		MFLT_METRICS_SET_SIGNED(lte_rsrp, *((int *)event_data));
		break;
	case HL7800_EVENT_SINR:
		attr_set_signed32(ATTR_ID_lte_sinr, *((int *)event_data));
		MFLT_METRICS_SET_SIGNED(lte_sinr, *((int *)event_data));
		break;
	case HL7800_EVENT_STARTUP_STATE_CHANGE:
		attr_set_uint32(ATTR_ID_lte_startup_state, code);
		break;
	case HL7800_EVENT_SLEEP_STATE_CHANGE:
		attr_set_uint32(ATTR_ID_lte_sleep_state, code);
		break;
	case HL7800_EVENT_RAT:
		attr_set_without_broadcast(ATTR_ID_lte_rat, ATTR_TYPE_U8, event_data,
					   sizeof(uint8_t), NULL);
		break;
	case HL7800_EVENT_BANDS:
		attr_set_without_broadcast(ATTR_ID_lte_bands, ATTR_TYPE_STRING, s, strlen(s), NULL);
		break;
	case HL7800_EVENT_ACTIVE_BANDS:
		attr_set_string(ATTR_ID_lte_active_bands, s, strlen(s));
		break;
	case HL7800_EVENT_REVISION:
		attr_set_string(ATTR_ID_lte_version, s, strlen(s));
		break;
	case HL7800_EVENT_SITE_SURVEY:
		site_survey_handler(event_data);
		break;
	case HL7800_EVENT_FOTA_STATE:
		attr_set_uint32(ATTR_ID_lte_fup_status, *((uint8_t *)event_data));
		break;
	case HL7800_EVENT_FOTA_COUNT:
		break;
	case HL7800_EVENT_SOCKET_STATS:
		socket_stats = (struct mdm_hl7800_event_socket_stats *)event_data;
		if (socket_stats->udp_tx != 0) {
			LOG_DBG("UDP TX bytes: %d", socket_stats->udp_tx);
		}
		if (socket_stats->udp_rx != 0) {
			LOG_DBG("UDP RX bytes: %d", socket_stats->udp_rx);
		}
		if (socket_stats->tcp_tx != 0) {
			LOG_DBG("TCP TX bytes: %d", socket_stats->tcp_tx);
		}
		if (socket_stats->tcp_rx != 0) {
			LOG_DBG("TCP RX bytes: %d", socket_stats->tcp_rx);
		}
		MFLT_METRICS_ADD(lte_udp_tx, socket_stats->udp_tx);
		MFLT_METRICS_ADD(lte_udp_rx, socket_stats->udp_rx);
		MFLT_METRICS_ADD(lte_tcp_tx, socket_stats->tcp_tx);
		MFLT_METRICS_ADD(lte_tcp_rx, socket_stats->tcp_rx);
		MFLT_METRICS_ADD(lte_data_total, socket_stats->udp_tx + socket_stats->udp_rx +
							 socket_stats->tcp_tx +
							 socket_stats->tcp_rx);
		(void)attr_add_uint32(ATTR_ID_lte_udp_tx, socket_stats->udp_tx);
		(void)attr_add_uint32(ATTR_ID_lte_udp_rx, socket_stats->udp_rx);
		(void)attr_add_uint32(ATTR_ID_lte_tcp_tx, socket_stats->tcp_tx);
		(void)attr_add_uint32(ATTR_ID_lte_tcp_rx, socket_stats->tcp_rx);
		(void)attr_add_uint32(ATTR_ID_lte_data_total,
				      socket_stats->udp_tx + socket_stats->udp_rx +
					      socket_stats->tcp_tx + socket_stats->tcp_rx);
		break;
	default:
		LOG_WRN("Unhandled event %d", event);
		break;
	}
}

static int lcz_modem_hl7800_init(const struct device *device)
{
	char *str;
	int ret = LTE_INIT_ERROR_NONE;
	int operator_index;

	ARG_UNUSED(device);

	(void)mdm_hl7800_log_filter_set(attr_get_uint32(ATTR_ID_lte_log_lvl, LOG_LEVEL_DBG));

	attr_event_agent.callback = attr_changed_callback;
	(void)smt_register_attr_changed_agent(&attr_event_agent);

#ifdef CONFIG_MODEM_HL7800_BOOT_DELAY
	ret = mdm_hl7800_reset();
	if (ret < 0) {
		ret = LTE_INIT_ERROR_MODEM;
		LOG_ERR("Unable to configure modem");
		attr_set_signed32(ATTR_ID_lte_init_error, ret);
		return ret;
	}
#endif

	str = mdm_hl7800_get_imei();
	attr_set_string(ATTR_ID_lte_imei, str, strlen(str));
	str = mdm_hl7800_get_fw_version();
	attr_set_string(ATTR_ID_lte_version, str, strlen(str));
	str = mdm_hl7800_get_iccid();
	attr_set_string(ATTR_ID_lte_iccid, str, strlen(str));
	str = mdm_hl7800_get_sn();
	attr_set_string(ATTR_ID_lte_sn, str, strlen(str));
	str = mdm_hl7800_get_imsi();
	attr_set_string(ATTR_ID_lte_imsi, str, strlen(str));

	operator_index = mdm_hl7800_get_operator_index();
	if (operator_index >= 0) {
		attr_set_uint32(ATTR_ID_lte_operator_index, operator_index);
	}

	hl7800_evt_agent.event_callback = hl7800_event_callback;
	mdm_hl7800_register_event_callback(&hl7800_evt_agent);
	mdm_hl7800_generate_status_events();

	attr_set_signed32(ATTR_ID_lte_init_error, ret);

	/* Don't log changes to these values */
	(void)attr_set_quiet(ATTR_ID_lte_udp_tx, true);
	(void)attr_set_quiet(ATTR_ID_lte_udp_rx, true);
	(void)attr_set_quiet(ATTR_ID_lte_tcp_tx, true);
	(void)attr_set_quiet(ATTR_ID_lte_tcp_rx, true);
	(void)attr_set_quiet(ATTR_ID_lte_data_total, true);

	return ret;
}

/**************************************************************************************************/
/* Global Function Definitions                                                                    */
/**************************************************************************************************/
SYS_INIT(lcz_modem_hl7800_init, APPLICATION, CONFIG_LCZ_MODEM_HL7800_INIT_PRIORITY);
