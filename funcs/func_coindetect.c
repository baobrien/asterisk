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
#include <stdbool.h>
#include <stdio.h>

#include <sys/stat.h>
#include <fcntl.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/*** DOCUMENTATION
	<function name="COIN_DETECT" language="en_US">
		<synopsis>
			Function to count coins inserted into payphone
		</synopsis>
	</function>
 ***/

typedef struct {
	float x1;
	float x2;
	float wr;
	float wi;
	float n;
} goertzel_state_t;

typedef struct {
	int value;
	int power;
} goertzel_result_t;

#define COINDET_G_RATE 60
#define COINDET_THRESH .05

struct detector_state {
    int cd_current_sample;
    goertzel_state_t tone_a;
    goertzel_state_t tone_b;
	int incoin;
	int hits;
	int misses;
	int coins;
	int detector_rate;
	int fd;
};

struct coindetect_data {
    struct ast_audiohook audiohook;
	bool en_rx, en_tx;
    struct detector_state rx;
    struct detector_state tx; 
};

static void coindetect_process(struct ast_frame * f, struct detector_state * s);


static inline void goertzel_sample(goertzel_state_t *s, int16_t sample)
{
	float x0 = (float)(sample) / 32786.0F;
	x0 = x0 + s->wr * s->x1 - s->x2;
	s->x2 = s->x1;
	s->x1 = x0;
}

static inline float goertzel_result(goertzel_state_t *s)
{
	double re = (0.5 * s->wr * s->x1 - s->x2) / s->n;
	double im = (s->wi * s->x1) / s->n;
	return sqrt(re*re+im*im);
}

static inline void goertzel_init(goertzel_state_t *s, float freq, unsigned int sample_rate, unsigned int n)
{
	float w = 2*M_PI*freq/(float)sample_rate;
	s->wr = 2 * cos(w);
	s->wi = sin(w);
	s->x1 = 0;
	s->x2 = 0;
	s->n = n;
}

static inline void goertzel_reset(goertzel_state_t *s)
{
	s->x1 = 0;
	s->x2 = 0;
}

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

static int coindetect_cb(struct ast_audiohook *audiohook, struct ast_channel *chan, struct ast_frame *f, enum ast_audiohook_direction direction)
{
	struct ast_datastore *datastore = NULL;
	struct coindetect_data *coindetect = NULL;


	if (!f) {
		return 0;
	}
	if (audiohook->status == AST_AUDIOHOOK_STATUS_DONE) {
		return -1;
	}

	if (!(datastore = ast_channel_datastore_find(chan, &coindetect_datastore, NULL))) {
		return -1;
	}

	coindetect = datastore->data;
    if (direction == AST_AUDIOHOOK_DIRECTION_WRITE) {
        coindetect_process(f, &coindetect->tx);
    } else {
        coindetect_process(f, &coindetect->rx);
    }
	return 0;
}

static int coindetect_read(struct ast_channel *chan, const char *cmd, char *data, char *buffer, size_t buflen)
{
	struct ast_datastore *datastore = NULL;
	struct coindetect_data *coindetect = NULL;
	
	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}	

	ast_channel_lock(chan);
	datastore = ast_channel_datastore_find(chan, &coindetect_datastore, NULL);
	ast_channel_unlock(chan);
	if (datastore == NULL) {
		return -1;
	}
	coindetect = datastore->data;

	if (!strcasecmp("tx_coins", data)) {
		snprintf(buffer, buflen, "%d", coindetect->tx.coins);
	} else if (!strcasecmp("rx_coins", data)) {
		snprintf(buffer, buflen, "%d", coindetect->rx.coins);
	}
	printf("cmd: %s data: %s buf: %s\n", cmd, data, buffer);
	return 0;

}

static int coindetect_write(struct ast_channel *chan, const char *cmd, char *data, const char *value)
{
	struct ast_datastore *datastore = NULL;
	struct coindetect_data *coindetect = NULL;
	int new = 0;

	if (!chan) {
		ast_log(LOG_WARNING, "No channel was provided to %s function.\n", cmd);
		return -1;
	}

	printf("cmd: %s data: %s val: %s\n", cmd, data, value);

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
        coindetect->rx.cd_current_sample = 0;
        coindetect->tx.cd_current_sample = 0;
		coindetect->rx.detector_rate = 0;
		coindetect->tx.detector_rate = 0;
		coindetect->en_rx = true;
		coindetect->en_tx = true;

		ast_audiohook_init(&coindetect->audiohook, AST_AUDIOHOOK_TYPE_MANIPULATE, "coin_detect", AST_AUDIOHOOK_MANIPULATE_ALL_RATES);
		coindetect->audiohook.manipulate_callback = coindetect_cb;
		datastore->data = coindetect;
		new = 1;
	} else {
		ast_channel_unlock(chan);
		coindetect = datastore->data;
	}


	if (new) {
		ast_channel_lock(chan);
		ast_channel_datastore_add(chan, datastore);
		ast_channel_unlock(chan);
		ast_audiohook_attach(chan, &coindetect->audiohook);
	}

	return 0;

cleanup_error:

	ast_log(LOG_ERROR, "Invalid argument provided to the %s function\n", cmd);
	if (new) {
		ast_datastore_free(datastore);
	}
	return -1;
}

static void coindetect_process(struct ast_frame * f, struct detector_state * s) {
	int16_t *data = (int16_t *) f->data.ptr;
    int samples = f->samples;
	int rate =  ast_format_get_sample_rate(f->subclass.format);
	char * name = ast_format_get_name(f->subclass.format);
	if (rate != s->detector_rate) {
		int adjusted_goertzel_rate = (int)(((float)rate / 8000.0f) * COINDET_G_RATE);
        goertzel_init(&(s->tone_a), 1700, rate, adjusted_goertzel_rate);
        goertzel_init(&(s->tone_b), 2200, rate, adjusted_goertzel_rate);
		s->detector_rate = rate;
	}
    for(int n = 0; n < samples; n++) {
        int16_t samp = data[n];
        goertzel_sample(&(s->tone_a), samp);
        goertzel_sample(&(s->tone_b), samp);
        s->cd_current_sample++;
        if(s->cd_current_sample < s->tone_a.n) {
            continue;
        }
		s->cd_current_sample = 0;
        float e1 = goertzel_result(&(s->tone_a));
        float e2 = goertzel_result(&(s->tone_b));
		int detect = ((e1 > COINDET_THRESH) && (e2 > COINDET_THRESH));

		if (s->incoin) {
			if (detect) {
				s->misses = 0;
            	//ast_log(LOG_NOTICE,"e1: %f, e2: %f, c: %f, %d, %s", e1, e2, e1*e2, rate, name);
			} else {
				s->misses++;
			}
			if (s->misses > 3) {
				s->incoin=0;
				s->hits=0;
				//ast_log(LOG_NOTICE,"Coins: %d h %d m %d ic %d %f", s->coins, s->hits, s->misses, s->incoin, e1 * e2);
			}
		} else {
			if (detect) {
				s->hits++;
            	//ast_log(LOG_NOTICE,"e1: %f, e2: %f, c: %f, %d, %s", e1, e2, e1*e2, rate, name);
			} else {
				s->hits=0;
			}
			if (s->hits > 3) {
				s->incoin=1;
				s->misses=0;
				s->coins+=1;
				printf("coin\n");
				//ast_log(LOG_NOTICE,"Coins: %d h %d m %d ic %d %f", s->coins, s->hits, s->misses, s->incoin, e1 * e2);
			}
		}

        goertzel_reset(&(s->tone_a));
        goertzel_reset(&(s->tone_b));
    }
}

static struct ast_custom_function coin_detect_function = {
	.name = "COIN_DETECT",
	.write = coindetect_write,
	.read = coindetect_read,
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
