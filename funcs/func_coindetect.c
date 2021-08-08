/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2021, Brady O'Brien
 *
 * Brady O'Brien <baobrien@baobrien.org>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief Function to count coins inserted into payphone
 *
 * \author Brady O'Brien <baobrien@baobrien.org>
 *
 * \ingroup functions
 */


#include "asterisk.h"

#include "asterisk/module.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"
#include "asterisk/audiohook.h"
#include "asterisk/dsp.h"
#include <math.h>

/*** DOCUMENTATION
	<function name="COIN_DETECT" language="en_US">
		<synopsis>
			Function to count coins inserted into payphone
		</synopsis>
	</function>
 ***/

struct coindetect_data {
    struct ast_audiohook audiohook;
};

static void destroy_callback(void *data)
{
	struct coindetect_data *coindetect = data;

	ast_audiohook_destroy(&coindetect->audiohook);
	ast_free(coindetect);
};

static const struct ast_datastore_info coindetect_datastore = {
    .type = "coindetect",
    .destroy = destroy_callback,
};

static int coindetect_helper(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_datastore *datastore = NULL;
	struct coindetect_data *coindetect = NULL;
	int new = 0;
	float amount = 0;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	ast_channel_lock(chan);
	if (!(datastore = ast_channel_datastore_find(chan, &coindetect_datastore, NULL))) {
		ast_channel_unlock(chan);

		if (!(datastore = ast_datastore_alloc(&coindetect_datastore, NULL))) {
			return 0;
		}
		if (!(coindetect = ast_calloc(1, sizeof(*coindetect)))) {
			ast_datastore_free(datastore);
			return 0;
		}

		ast_audiohook_init(&coindetect->audiohook, AST_AUDIOHOOK_TYPE_MANIPULATE, "coin_detect", AST_AUDIOHOOK_MANIPULATE_ALL_RATES);
		//shift->audiohook.manipulate_callback = pitchshift_cb;
		datastore->data = coindetect;
		new = 1;
	} else {
		ast_channel_unlock(chan);
		coindetect = datastore->data;
	}

    ast_log(LOG_NOTICE, "I can't really detect coins yet");


	// if (!strcasecmp(value, "highest")) {
	// 	amount = HIGHEST;
	// } else if (!strcasecmp(value, "higher")) {
	// 	amount = HIGHER;
	// } else if (!strcasecmp(value, "high")) {
	// 	amount = HIGH;
	// } else if (!strcasecmp(value, "lowest")) {
	// 	amount = LOWEST;
	// } else if (!strcasecmp(value, "lower")) {
	// 	amount = LOWER;
	// } else if (!strcasecmp(value, "low")) {
	// 	amount = LOW;
	// } else {
	// 	if (!sscanf(value, "%30f", &amount) || (amount <= 0) || (amount > 4)) {
	// 		goto cleanup_error;
	// 	}
	// }

	// if (!strcasecmp(data, "rx")) {
	// 	shift->rx.shift_amount = amount;
	// } else if (!strcasecmp(data, "tx")) {
	// 	shift->tx.shift_amount = amount;
	// } else if (!strcasecmp(data, "both")) {
	// 	shift->rx.shift_amount = amount;
	// 	shift->tx.shift_amount = amount;
	// } else {
	// 	goto cleanup_error;
	// }

	if (new) {
		ast_channel_lock(chan);
		ast_channel_datastore_add(chan, datastore);
		ast_channel_unlock(chan);
		// ast_audiohook_attach(chan, &coindetect->audiohook);
	}

	return 0;

cleanup_error:

	ast_log(LOG_ERROR, "Invalid argument provided to the %s function\n", cmd);
	if (new) {
		ast_datastore_free(datastore);
	}
	return -1;
}

static struct ast_custom_function coin_detect_function = {
	.name = "COIN_DETECT",
	.write = coindetect_helper,
};

static int unload_module(void)
{
	return ast_custom_function_unregister(&coin_detect_function);
}

static int load_module(void)
{
	int res = ast_custom_function_register(&coin_detect_function);
	return res ? AST_MODULE_LOAD_DECLINE : AST_MODULE_LOAD_SUCCESS;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "Coin Detection Functions");
