/*
 * Copyright (c) 2015 Qualcomm Atheros, Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <linux/hw_random.h>
#include <linux/kthread.h>

#include "ath9k.h"
#include "hw.h"
#include "ar9003_phy.h"

static DECLARE_WAIT_QUEUE_HEAD(rng_queue);

static int ath9k_rng_data_read(struct ath_softc *sc, u32 *buf, u32 buf_size)
{
	int i, j;
	u32  v1, v2, rng_last = sc->rng_last;
	struct ath_hw *ah = sc->sc_ah;

	ath9k_ps_wakeup(sc);

	REG_RMW_FIELD(ah, AR_PHY_TEST, AR_PHY_TEST_BBB_OBS_SEL, 1);
	REG_CLR_BIT(ah, AR_PHY_TEST, AR_PHY_TEST_RX_OBS_SEL_BIT5);
	REG_RMW_FIELD(ah, AR_PHY_TEST_CTL_STATUS, AR_PHY_TEST_CTL_RX_OBS_SEL, 0);

	for (i = 0, j = 0; i < buf_size; i++) {
		v1 = REG_READ(ah, AR_PHY_TST_ADC) & 0xffff;
		v2 = REG_READ(ah, AR_PHY_TST_ADC) & 0xffff;

		/* wait for data ready */
		if (v1 && v2 && rng_last != v1 && v1 != v2 && v1 != 0xffff &&
		    v2 != 0xffff)
			buf[j++] = (v1 << 16) | v2;

		rng_last = v2;
	}

	ath9k_ps_restore(sc);

	sc->rng_last = rng_last;

	return j << 2;
}

static u32 ath9k_rng_delay_get(u32 fail_stats)
{
	u32 delay;

	if (fail_stats < 100)
		delay = 10;
	else if (fail_stats < 105)
		delay = 1000;
	else
		delay = 10000;

	return delay;
}

static int ath9k_rng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct ath_softc *sc = container_of(rng, struct ath_softc, rng_ops);
	int bytes_read;
	u32 fail_stats = 0;

retry:
	bytes_read = ath9k_rng_data_read(sc, buf, max);
	if (unlikely(!bytes_read) && wait) {
		msleep(ath9k_rng_delay_get(++fail_stats));
		goto retry;
	}

	return bytes_read;
}

void ath9k_rng_start(struct ath_softc *sc)
{
	struct ath_hw *ah = sc->sc_ah;
	int ret;

	if (sc->rng_ops.read)
		return;

	if (!AR_SREV_9300_20_OR_LATER(ah))
		return;

	sc->rng_ops.name = "ath9k";
	sc->rng_ops.read = ath9k_rng_read;
	sc->rng_ops.quality = 320;

	ret = devm_hwrng_register(sc->dev, &sc->rng_ops);
	if (ret)
		sc->rng_ops.read = NULL;
}

void ath9k_rng_stop(struct ath_softc *sc)
{
	if (sc->rng_ops.read) {
		devm_hwrng_unregister(sc->dev, &sc->rng_ops);
		sc->rng_ops.read = NULL;
	}
}
