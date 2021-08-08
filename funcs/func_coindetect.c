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
	/*! The previous previous sample calculation (No binary point just plain int) */
	int v2;
	/*! The previous sample calculation (No binary point just plain int) */
	int v3;
	/*! v2 and v3 power of two exponent to keep value in int range */
	int chunky;
	/*! 15 bit fixed point goertzel coefficient = 2 * cos(2 * pi * freq / sample_rate) */
	int fac;
} goertzel_state_t;

typedef struct {
	int value;
	int power;
} goertzel_result_t;

#define COINDET_G_RATE 60
#define COINDET_THRESH 1e8

struct detector_state {
    int cd_current_sample;
    goertzel_state_t tone_a;
    goertzel_state_t tone_b;
};

struct coindetect_data {
    struct ast_audiohook audiohook;
    struct detector_state rx;
    struct detector_state tx; 
};

static void coindetect_process(struct ast_frame * f, struct detector_state * s);


static inline void goertzel_sample(goertzel_state_t *s, short sample)
{
	int v1;

	/*
	 * Shift previous values so
	 * v1 is previous previous value
	 * v2 is previous value
	 * until the new v3 is calculated.
	 */
	v1 = s->v2;
	s->v2 = s->v3;

	/* Discard the binary fraction introduced by s->fac */
	s->v3 = (s->fac * s->v2) >> 15;
	/* Scale sample to match previous values */
	s->v3 = s->v3 - v1 + (sample >> s->chunky);

	if (abs(s->v3) > (1 << 15)) {
		/* The result is now too large so increase the chunky power. */
		s->chunky++;
		s->v3 = s->v3 >> 1;
		s->v2 = s->v2 >> 1;
	}
}

static inline float goertzel_result(goertzel_state_t *s)
{
	goertzel_result_t r;

	r.value = (s->v3 * s->v3) + (s->v2 * s->v2);
	r.value -= ((s->v2 * s->v3) >> 15) * s->fac;
	/*
	 * We have to double the exponent because we multiplied the
	 * previous sample calculation values together.
	 */
	r.power = s->chunky * 2;
	return (float)r.value * (float)(1 << r.power);
}

static inline void goertzel_init(goertzel_state_t *s, float freq, unsigned int sample_rate)
{
	s->v2 = s->v3 = s->chunky = 0;
	s->fac = (int)(32768.0 * 2.0 * cos(2.0 * M_PI * freq / sample_rate));
}

static inline void goertzel_reset(goertzel_state_t *s)
{
	s->v2 = s->v3 = s->chunky = 0;
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
        coindetect->rx.cd_current_sample = 0;
        coindetect->tx.cd_current_sample = 0;
        goertzel_init(&(coindetect->rx.tone_a), 1700, 8000);
        goertzel_init(&(coindetect->rx.tone_b), 2200, 8000);
        goertzel_init(&(coindetect->tx.tone_a), 1700, 8000);
        goertzel_init(&(coindetect->tx.tone_b), 2200, 8000);

		ast_audiohook_init(&coindetect->audiohook, AST_AUDIOHOOK_TYPE_MANIPULATE, "coin_detect", 0);
		coindetect->audiohook.manipulate_callback = coindetect_cb;
		datastore->data = coindetect;
		new = 1;
	} else {
		ast_channel_unlock(chan);
		coindetect = datastore->data;
	}

    ast_log(LOG_NOTICE, "I can't really detect coins yet");

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
    for(int n = 0; n < samples; n++) {
        int16_t samp = data[n];
        goertzel_sample(&(s->tone_a), samp);
        goertzel_sample(&(s->tone_b), samp);
        s->cd_current_sample++;
        if(s->cd_current_sample < COINDET_G_RATE) {
            continue;
        }
        float e1 = goertzel_result(&(s->tone_a));
        float e2 = goertzel_result(&(s->tone_a));
        if (e1 > COINDET_THRESH && e2 > COINDET_THRESH) {
            ast_log(LOG_NOTICE,"e1: %f, e2: %f", e1, e2);
        }
        // ast_log(LOG_NOTICE,"e1: %f, e2: %f", e1, e2);

        goertzel_reset(&(s->tone_a));
        goertzel_reset(&(s->tone_b));
    }
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
