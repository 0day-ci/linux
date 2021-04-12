// SPDX-License-Identifier: GPL-2.0
/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
 *
 ******************************************************************************/

#include "Mp_Precomp.h"

/*  Global variables, these are static variables */
static struct coex_dm_8723b_1ant gl_coex_dm_8723b_1ant;
static struct coex_dm_8723b_1ant *p_coex_dm = &gl_coex_dm_8723b_1ant;
static struct coex_sta_8723b_1ant gl_coex_sta_8723b_1ant;
static struct coex_sta_8723b_1ant *p_coex_sta = &gl_coex_sta_8723b_1ant;

static const char *const gl_bt_info_src_8723b_1ant[] = {
	"BT Info[wifi fw]",
	"BT Info[bt rsp]",
	"BT Info[bt auto report]",
};

static u32 gl_coex_ver_date_8723b_1ant = 20140507;
static u32 gl_coex_ver_8723b_1ant = 0x4e;

/*  local function proto type if needed */
/*  local function start with halbtc8723b1ant_ */
static u8 hal_btc_8723b_1ant_btRssi_state(u8 levelNum, u8 rssi_thresh, u8 rssi_thresh1)
{
	s32 bt_rssi = 0;
	u8 bt_rssi_state = p_coex_sta->preBtRssiState;

	bt_rssi = p_coex_sta->btRssi;

	if (levelNum == 2) {
		if (
			(p_coex_sta->preBtRssiState == BTC_RSSI_STATE_LOW) ||
			(p_coex_sta->preBtRssiState == BTC_RSSI_STATE_STAY_LOW)
		) {
			if (bt_rssi >= (rssi_thresh + BTC_RSSI_COEX_THRESH_TOL_8723B_1ANT)) {

				bt_rssi_state = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state switch to High\n")
				);
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state stay at Low\n")
				);
			}
		} else {
			if (bt_rssi < rssi_thresh) {
				bt_rssi_state = BTC_RSSI_STATE_LOW;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state switch to Low\n")
				);
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state stay at High\n")
				);
			}
		}
	} else if (levelNum == 3) {
		if (rssi_thresh > rssi_thresh1) {
			BTC_PRINT(
				BTC_MSG_ALGORITHM,
				ALGO_BT_RSSI_STATE,
				("[BTCoex], BT Rssi thresh error!!\n")
			);
			return p_coex_sta->preBtRssiState;
		}

		if (
			(p_coex_sta->preBtRssiState == BTC_RSSI_STATE_LOW) ||
			(p_coex_sta->preBtRssiState == BTC_RSSI_STATE_STAY_LOW)
		) {
			if (bt_rssi >= (rssi_thresh + BTC_RSSI_COEX_THRESH_TOL_8723B_1ANT)) {
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state switch to Medium\n")
				);
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_LOW;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state stay at Low\n")
				);
			}
		} else if (
			(p_coex_sta->preBtRssiState == BTC_RSSI_STATE_MEDIUM) ||
			(p_coex_sta->preBtRssiState == BTC_RSSI_STATE_STAY_MEDIUM)
		) {
			if (bt_rssi >= (rssi_thresh1 + BTC_RSSI_COEX_THRESH_TOL_8723B_1ANT)) {
				bt_rssi_state = BTC_RSSI_STATE_HIGH;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state switch to High\n")
				);
			} else if (bt_rssi < rssi_thresh) {
				bt_rssi_state = BTC_RSSI_STATE_LOW;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state switch to Low\n")
				);
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_MEDIUM;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state stay at Medium\n")
				);
			}
		} else {
			if (bt_rssi < rssi_thresh1) {
				bt_rssi_state = BTC_RSSI_STATE_MEDIUM;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state switch to Medium\n")
				);
			} else {
				bt_rssi_state = BTC_RSSI_STATE_STAY_HIGH;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_BT_RSSI_STATE,
					("[BTCoex], BT Rssi state stay at High\n")
				);
			}
		}
	}

	p_coex_sta->preBtRssiState = bt_rssi_state;

	return bt_rssi_state;
}

static void halbtc8723b1ant_UpdateRaMask(
	struct btc_coexist *p_bt_coexist, bool force_exec, u32 dis_rate_mask
)
{
	p_coex_dm->curRaMask = dis_rate_mask;

	if (force_exec || (p_coex_dm->preRaMask != p_coex_dm->curRaMask))
		p_bt_coexist->fBtcSet(
			p_bt_coexist,
			BTC_SET_ACT_UPDATE_RAMASK,
			&p_coex_dm->curRaMask
		);
	p_coex_dm->preRaMask = p_coex_dm->curRaMask;
}

static void halbtc8723b1ant_AutoRateFallbackRetry(
	struct btc_coexist *p_bt_coexist, bool force_exec, u8 type
)
{
	bool wifi_under_bmode = false;

	p_coex_dm->curArfrType = type;

	if (force_exec || (p_coex_dm->preArfrType != p_coex_dm->curArfrType)) {
		switch (p_coex_dm->curArfrType) {
		case 0:	/*  normal mode */
			p_bt_coexist->fBtcWrite4Byte(
				p_bt_coexist, 0x430, p_coex_dm->backupArfrCnt1
			);
			p_bt_coexist->fBtcWrite4Byte(
				p_bt_coexist, 0x434, p_coex_dm->backupArfrCnt2
			);
			break;
		case 1:
			p_bt_coexist->fBtcGet(
				p_bt_coexist, BTC_GET_BL_WIFI_UNDER_B_MODE, &wifi_under_bmode
			);
			if (wifi_under_bmode) {
				p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x430, 0x0);
				p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x434, 0x01010101);
			} else {
				p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x430, 0x0);
				p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x434, 0x04030201);
			}
			break;
		default:
			break;
		}
	}

	p_coex_dm->preArfrType = p_coex_dm->curArfrType;
}

static void halbtc8723b1ant_RetryLimit(
	struct btc_coexist *p_bt_coexist, bool force_exec, u8 type
)
{
	p_coex_dm->curRetryLimitType = type;

	if (
		force_exec ||
		(p_coex_dm->preRetryLimitType != p_coex_dm->curRetryLimitType)
	) {
		switch (p_coex_dm->curRetryLimitType) {
		case 0:	/*  normal mode */
			p_bt_coexist->fBtcWrite2Byte(
				p_bt_coexist, 0x42a, p_coex_dm->backupRetryLimit
			);
			break;
		case 1:	/*  retry limit =8 */
			p_bt_coexist->fBtcWrite2Byte(p_bt_coexist, 0x42a, 0x0808);
			break;
		default:
			break;
		}
	}

	p_coex_dm->preRetryLimitType = p_coex_dm->curRetryLimitType;
}

static void halbtc8723b1ant_AmpduMaxTime(
	struct btc_coexist *p_bt_coexist, bool force_exec, u8 type
)
{
	p_coex_dm->curAmpduTimeType = type;

	if (
		force_exec || (p_coex_dm->preAmpduTimeType != p_coex_dm->curAmpduTimeType)
	) {
		switch (p_coex_dm->curAmpduTimeType) {
		case 0:	/*  normal mode */
			p_bt_coexist->fBtcWrite1Byte(
				p_bt_coexist, 0x456, p_coex_dm->backupAmpduMaxTime
			);
			break;
		case 1:	/*  AMPDU timw = 0x38 * 32us */
			p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x456, 0x38);
			break;
		default:
			break;
		}
	}

	p_coex_dm->preAmpduTimeType = p_coex_dm->curAmpduTimeType;
}

static void halbtc8723b1ant_LimitedTx(
	struct btc_coexist *p_bt_coexist,
	bool force_exec,
	u8 ra_mask_type,
	u8 arfr_type,
	u8 retry_limit_type,
	u8 ampdu_time_type
)
{
	switch (ra_mask_type) {
	case 0:	/*  normal mode */
		halbtc8723b1ant_UpdateRaMask(p_bt_coexist, force_exec, 0x0);
		break;
	case 1:	/*  disable cck 1/2 */
		halbtc8723b1ant_UpdateRaMask(p_bt_coexist, force_exec, 0x00000003);
		break;
	case 2:	/*  disable cck 1/2/5.5, ofdm 6/9/12/18/24, mcs 0/1/2/3/4 */
		halbtc8723b1ant_UpdateRaMask(p_bt_coexist, force_exec, 0x0001f1f7);
		break;
	default:
		break;
	}

	halbtc8723b1ant_AutoRateFallbackRetry(p_bt_coexist, force_exec, arfr_type);
	halbtc8723b1ant_RetryLimit(p_bt_coexist, force_exec, retry_limit_type);
	halbtc8723b1ant_AmpduMaxTime(p_bt_coexist, force_exec, ampdu_time_type);
}

static void halbtc8723b1ant_LimitedRx(
	struct btc_coexist *p_bt_coexist,
	bool force_exec,
	bool bRejApAggPkt,
	bool bBtCtrlAggBufSize,
	u8 aggBufSize
)
{
	bool bRejectRxAgg = bRejApAggPkt;
	bool bBtCtrlRxAggSize = bBtCtrlAggBufSize;
	u8 rxAggSize = aggBufSize;

	/*  */
	/* 	Rx Aggregation related setting */
	/*  */
	p_bt_coexist->fBtcSet(
		p_bt_coexist, BTC_SET_BL_TO_REJ_AP_AGG_PKT, &bRejectRxAgg
	);
	/*  decide BT control aggregation buf size or not */
	p_bt_coexist->fBtcSet(
		p_bt_coexist, BTC_SET_BL_BT_CTRL_AGG_SIZE, &bBtCtrlRxAggSize
	);
	/*  aggregation buf size, only work when BT control Rx aggregation size. */
	p_bt_coexist->fBtcSet(p_bt_coexist, BTC_SET_U1_AGG_BUF_SIZE, &rxAggSize);
	/*  real update aggregation setting */
	p_bt_coexist->fBtcSet(p_bt_coexist, BTC_SET_ACT_AGGREGATE_CTRL, NULL);


}

static void halbtc8723b1ant_QueryBtInfo(struct btc_coexist *p_bt_coexist)
{
	u8 	h2c_parameter[1] = {0};

	p_coex_sta->bC2hBtInfoReqSent = true;

	h2c_parameter[0] |= BIT0;	/*  trigger */

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW_EXEC,
		("[BTCoex], Query Bt Info, FW write 0x61 = 0x%x\n", h2c_parameter[0])
	);

	p_bt_coexist->fBtcFillH2c(p_bt_coexist, 0x61, 1, h2c_parameter);
}

static void halbtc8723b1ant_MonitorBtCtr(struct btc_coexist *p_bt_coexist)
{
	u32 regHPTxRx, regLPTxRx, u4Tmp;
	u32 regHPTx = 0, regHPRx = 0, regLPTx = 0, regLPRx = 0;
	static u8 NumOfBtCounterChk;

       /* to avoid 0x76e[3] = 1 (WLAN_Act control by PTA) during IPS */
	/* if (! (p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0x76e) & 0x8)) */

	if (p_coex_sta->bUnderIps) {
		p_coex_sta->highPriorityTx = 65535;
		p_coex_sta->highPriorityRx = 65535;
		p_coex_sta->lowPriorityTx = 65535;
		p_coex_sta->lowPriorityRx = 65535;
		return;
	}

	regHPTxRx = 0x770;
	regLPTxRx = 0x774;

	u4Tmp = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, regHPTxRx);
	regHPTx = u4Tmp & bMaskLWord;
	regHPRx = (u4Tmp & bMaskHWord) >> 16;

	u4Tmp = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, regLPTxRx);
	regLPTx = u4Tmp & bMaskLWord;
	regLPRx = (u4Tmp & bMaskHWord) >> 16;

	p_coex_sta->highPriorityTx = regHPTx;
	p_coex_sta->highPriorityRx = regHPRx;
	p_coex_sta->lowPriorityTx = regLPTx;
	p_coex_sta->lowPriorityRx = regLPRx;

	if ((p_coex_sta->lowPriorityTx >= 1050) && (!p_coex_sta->bC2hBtInquiryPage))
		p_coex_sta->popEventCnt++;

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE,
		(
			"[BTCoex], Hi-Pri Rx/Tx: %d/%d, Lo-Pri Rx/Tx: %d/%d\n",
			regHPRx,
			regHPTx,
			regLPRx,
			regLPTx
		)
	);

	/*  reset counter */
	p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x76e, 0xc);

	if ((regHPTx == 0) && (regHPRx == 0) && (regLPTx == 0) && (regLPRx == 0)) {
		NumOfBtCounterChk++;
		if (NumOfBtCounterChk >= 3) {
			halbtc8723b1ant_QueryBtInfo(p_bt_coexist);
			NumOfBtCounterChk = 0;
		}
	}
}


static void halbtc8723b1ant_MonitorWiFiCtr(struct btc_coexist *p_bt_coexist)
{
	s32	wifiRssi = 0;
	bool bWifiBusy = false, wifi_under_bmode = false;
	static u8 nCCKLockCounter;

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_S4_WIFI_RSSI, &wifiRssi);
	p_bt_coexist->fBtcGet(
		p_bt_coexist, BTC_GET_BL_WIFI_UNDER_B_MODE, &wifi_under_bmode
	);

	if (p_coex_sta->bUnderIps) {
		p_coex_sta->nCRCOK_CCK = 0;
		p_coex_sta->nCRCOK_11g = 0;
		p_coex_sta->nCRCOK_11n = 0;
		p_coex_sta->nCRCOK_11nAgg = 0;

		p_coex_sta->nCRCErr_CCK = 0;
		p_coex_sta->nCRCErr_11g = 0;
		p_coex_sta->nCRCErr_11n = 0;
		p_coex_sta->nCRCErr_11nAgg = 0;
	} else {
		p_coex_sta->nCRCOK_CCK	= p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0xf88);
		p_coex_sta->nCRCOK_11g	= p_bt_coexist->fBtcRead2Byte(p_bt_coexist, 0xf94);
		p_coex_sta->nCRCOK_11n	= p_bt_coexist->fBtcRead2Byte(p_bt_coexist, 0xf90);
		p_coex_sta->nCRCOK_11nAgg = p_bt_coexist->fBtcRead2Byte(p_bt_coexist, 0xfb8);

		p_coex_sta->nCRCErr_CCK	 = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0xf84);
		p_coex_sta->nCRCErr_11g	 = p_bt_coexist->fBtcRead2Byte(p_bt_coexist, 0xf96);
		p_coex_sta->nCRCErr_11n	 = p_bt_coexist->fBtcRead2Byte(p_bt_coexist, 0xf92);
		p_coex_sta->nCRCErr_11nAgg = p_bt_coexist->fBtcRead2Byte(p_bt_coexist, 0xfba);
	}


	/* reset counter */
	p_bt_coexist->fBtcWrite1ByteBitMask(p_bt_coexist, 0xf16, 0x1, 0x1);
	p_bt_coexist->fBtcWrite1ByteBitMask(p_bt_coexist, 0xf16, 0x1, 0x0);

	if (bWifiBusy && (wifiRssi >= 30) && !wifi_under_bmode) {
		if (
			(p_coex_dm->btStatus == BT_8723B_1ANT_BT_STATUS_ACL_BUSY) ||
			(p_coex_dm->btStatus == BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY) ||
			(p_coex_dm->btStatus == BT_8723B_1ANT_BT_STATUS_SCO_BUSY)
		) {
			if (
				p_coex_sta->nCRCOK_CCK > (
					p_coex_sta->nCRCOK_11g +
					p_coex_sta->nCRCOK_11n +
					p_coex_sta->nCRCOK_11nAgg
				)
			) {
				if (nCCKLockCounter < 5)
				 nCCKLockCounter++;
			} else {
				if (nCCKLockCounter > 0)
				 nCCKLockCounter--;
			}

		} else {
			if (nCCKLockCounter > 0)
			  nCCKLockCounter--;
		}
	} else {
		if (nCCKLockCounter > 0)
			nCCKLockCounter--;
	}

	if (!p_coex_sta->bPreCCKLock) {

		if (nCCKLockCounter >= 5)
		 p_coex_sta->bCCKLock = true;
		else
		 p_coex_sta->bCCKLock = false;
	} else {
		if (nCCKLockCounter == 0)
		 p_coex_sta->bCCKLock = false;
		else
		 p_coex_sta->bCCKLock = true;
	}

	p_coex_sta->bPreCCKLock =  p_coex_sta->bCCKLock;


}

static bool halbtc8723b1ant_IsWifiStatusChanged(struct btc_coexist *p_bt_coexist)
{
	static bool	bPreWifiBusy, bPreUnder4way, bPreBtHsOn;
	bool bWifiBusy = false, bUnder4way = false, bBtHsOn = false;
	bool bWifiConnected = false;

	p_bt_coexist->fBtcGet(
		p_bt_coexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected
	);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	p_bt_coexist->fBtcGet(
		p_bt_coexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS, &bUnder4way
	);

	if (bWifiConnected) {
		if (bWifiBusy != bPreWifiBusy) {
			bPreWifiBusy = bWifiBusy;
			return true;
		}

		if (bUnder4way != bPreUnder4way) {
			bPreUnder4way = bUnder4way;
			return true;
		}

		if (bBtHsOn != bPreBtHsOn) {
			bPreBtHsOn = bBtHsOn;
			return true;
		}
	}

	return false;
}

static void halbtc8723b1ant_UpdateBtLinkInfo(struct btc_coexist *p_bt_coexist)
{
	struct btc_bt_link_info *pBtLinkInfo = &p_bt_coexist->btLinkInfo;
	bool bBtHsOn = false;

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);

	pBtLinkInfo->bBtLinkExist = p_coex_sta->bBtLinkExist;
	pBtLinkInfo->bScoExist = p_coex_sta->bScoExist;
	pBtLinkInfo->bA2dpExist = p_coex_sta->bA2dpExist;
	pBtLinkInfo->bPanExist = p_coex_sta->bPanExist;
	pBtLinkInfo->bHidExist = p_coex_sta->bHidExist;

	/*  work around for HS mode. */
	if (bBtHsOn) {
		pBtLinkInfo->bPanExist = true;
		pBtLinkInfo->bBtLinkExist = true;
	}

	/*  check if Sco only */
	if (
		pBtLinkInfo->bScoExist &&
		!pBtLinkInfo->bA2dpExist &&
		!pBtLinkInfo->bPanExist &&
		!pBtLinkInfo->bHidExist
	)
		pBtLinkInfo->bScoOnly = true;
	else
		pBtLinkInfo->bScoOnly = false;

	/*  check if A2dp only */
	if (
		!pBtLinkInfo->bScoExist &&
		pBtLinkInfo->bA2dpExist &&
		!pBtLinkInfo->bPanExist &&
		!pBtLinkInfo->bHidExist
	)
		pBtLinkInfo->bA2dpOnly = true;
	else
		pBtLinkInfo->bA2dpOnly = false;

	/*  check if Pan only */
	if (
		!pBtLinkInfo->bScoExist &&
		!pBtLinkInfo->bA2dpExist &&
		pBtLinkInfo->bPanExist &&
		!pBtLinkInfo->bHidExist
	)
		pBtLinkInfo->bPanOnly = true;
	else
		pBtLinkInfo->bPanOnly = false;

	/*  check if Hid only */
	if (
		!pBtLinkInfo->bScoExist &&
		!pBtLinkInfo->bA2dpExist &&
		!pBtLinkInfo->bPanExist &&
		pBtLinkInfo->bHidExist
	)
		pBtLinkInfo->bHidOnly = true;
	else
		pBtLinkInfo->bHidOnly = false;
}

static u8 halbtc8723b1ant_ActionAlgorithm(struct btc_coexist *p_bt_coexist)
{
	struct btc_bt_link_info *pBtLinkInfo = &p_bt_coexist->btLinkInfo;
	bool bBtHsOn = false;
	u8 algorithm = BT_8723B_1ANT_COEX_ALGO_UNDEFINED;
	u8 numOfDiffProfile = 0;

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);

	if (!pBtLinkInfo->bBtLinkExist) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE,
			("[BTCoex], No BT link exists!!!\n")
		);
		return algorithm;
	}

	if (pBtLinkInfo->bScoExist)
		numOfDiffProfile++;
	if (pBtLinkInfo->bHidExist)
		numOfDiffProfile++;
	if (pBtLinkInfo->bPanExist)
		numOfDiffProfile++;
	if (pBtLinkInfo->bA2dpExist)
		numOfDiffProfile++;

	if (numOfDiffProfile == 1) {
		if (pBtLinkInfo->bScoExist) {
			BTC_PRINT(
				BTC_MSG_ALGORITHM,
				ALGO_TRACE,
				("[BTCoex], BT Profile = SCO only\n")
			);
			algorithm = BT_8723B_1ANT_COEX_ALGO_SCO;
		} else {
			if (pBtLinkInfo->bHidExist) {
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_TRACE,
					("[BTCoex], BT Profile = HID only\n")
				);
				algorithm = BT_8723B_1ANT_COEX_ALGO_HID;
			} else if (pBtLinkInfo->bA2dpExist) {
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_TRACE,
					("[BTCoex], BT Profile = A2DP only\n")
				);
				algorithm = BT_8723B_1ANT_COEX_ALGO_A2DP;
			} else if (pBtLinkInfo->bPanExist) {
				if (bBtHsOn) {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = PAN(HS) only\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_PANHS;
				} else {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = PAN(EDR) only\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_PANEDR;
				}
			}
		}
	} else if (numOfDiffProfile == 2) {
		if (pBtLinkInfo->bScoExist) {
			if (pBtLinkInfo->bHidExist) {
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_TRACE,
					("[BTCoex], BT Profile = SCO + HID\n")
				);
				algorithm = BT_8723B_1ANT_COEX_ALGO_HID;
			} else if (pBtLinkInfo->bA2dpExist) {
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_TRACE,
					("[BTCoex], BT Profile = SCO + A2DP ==> SCO\n")
				);
				algorithm = BT_8723B_1ANT_COEX_ALGO_SCO;
			} else if (pBtLinkInfo->bPanExist) {
				if (bBtHsOn) {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = SCO + PAN(HS)\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_SCO;
				} else {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = SCO + PAN(EDR)\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (pBtLinkInfo->bHidExist && pBtLinkInfo->bA2dpExist) {
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_TRACE,
					("[BTCoex], BT Profile = HID + A2DP\n")
				);
				algorithm = BT_8723B_1ANT_COEX_ALGO_HID_A2DP;
			} else if (pBtLinkInfo->bHidExist && pBtLinkInfo->bPanExist) {
				if (bBtHsOn) {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = HID + PAN(HS)\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = HID + PAN(EDR)\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (pBtLinkInfo->bPanExist && pBtLinkInfo->bA2dpExist) {
				if (bBtHsOn) {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = A2DP + PAN(HS)\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_A2DP_PANHS;
				} else {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = A2DP + PAN(EDR)\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_PANEDR_A2DP;
				}
			}
		}
	} else if (numOfDiffProfile == 3) {
		if (pBtLinkInfo->bScoExist) {
			if (pBtLinkInfo->bHidExist && pBtLinkInfo->bA2dpExist) {
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_TRACE,
					("[BTCoex], BT Profile = SCO + HID + A2DP ==> HID\n")
				);
				algorithm = BT_8723B_1ANT_COEX_ALGO_HID;
			} else if (
				pBtLinkInfo->bHidExist && pBtLinkInfo->bPanExist
			) {
				if (bBtHsOn) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = SCO + HID + PAN(HS)\n"));
					algorithm = BT_8723B_1ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = SCO + HID + PAN(EDR)\n"));
					algorithm = BT_8723B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			} else if (pBtLinkInfo->bPanExist && pBtLinkInfo->bA2dpExist) {
				if (bBtHsOn) {
					BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BT Profile = SCO + A2DP + PAN(HS)\n"));
					algorithm = BT_8723B_1ANT_COEX_ALGO_SCO;
				} else {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = SCO + A2DP + PAN(EDR) ==> HID\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		} else {
			if (
				pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bPanExist &&
				pBtLinkInfo->bA2dpExist
			) {
				if (bBtHsOn) {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = HID + A2DP + PAN(HS)\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_HID_A2DP;
				} else {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = HID + A2DP + PAN(EDR)\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_HID_A2DP_PANEDR;
				}
			}
		}
	} else if (numOfDiffProfile >= 3) {
		if (pBtLinkInfo->bScoExist) {
			if (
				pBtLinkInfo->bHidExist &&
				pBtLinkInfo->bPanExist &&
				pBtLinkInfo->bA2dpExist
			) {
				if (bBtHsOn) {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], Error!!! BT Profile = SCO + HID + A2DP + PAN(HS)\n")
					);

				} else {
					BTC_PRINT(
						BTC_MSG_ALGORITHM,
						ALGO_TRACE,
						("[BTCoex], BT Profile = SCO + HID + A2DP + PAN(EDR) ==>PAN(EDR)+HID\n")
					);
					algorithm = BT_8723B_1ANT_COEX_ALGO_PANEDR_HID;
				}
			}
		}
	}

	return algorithm;
}

static void halbtc8723b1ant_SetSwPenaltyTxRateAdaptive(
	struct btc_coexist *p_bt_coexist, bool bLowPenaltyRa
)
{
	u8 	h2c_parameter[6] = {0};

	h2c_parameter[0] = 0x6;	/*  opCode, 0x6 = Retry_Penalty */

	if (bLowPenaltyRa) {
		h2c_parameter[1] |= BIT0;
		h2c_parameter[2] = 0x00;  /* normal rate except MCS7/6/5, OFDM54/48/36 */
		h2c_parameter[3] = 0xf7;  /* MCS7 or OFDM54 */
		h2c_parameter[4] = 0xf8;  /* MCS6 or OFDM48 */
		h2c_parameter[5] = 0xf9;	/* MCS5 or OFDM36 */
	}

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW_EXEC,
		(
			"[BTCoex], set WiFi Low-Penalty Retry: %s",
			(bLowPenaltyRa ? "ON!!" : "OFF!!")
		)
	);

	p_bt_coexist->fBtcFillH2c(p_bt_coexist, 0x69, 6, h2c_parameter);
}

static void halbtc8723b1ant_LowPenaltyRa(
	struct btc_coexist *p_bt_coexist, bool force_exec, bool bLowPenaltyRa
)
{
	p_coex_dm->bCurLowPenaltyRa = bLowPenaltyRa;

	if (!force_exec) {
		if (p_coex_dm->bPreLowPenaltyRa == p_coex_dm->bCurLowPenaltyRa)
			return;
	}
	halbtc8723b1ant_SetSwPenaltyTxRateAdaptive(
		p_bt_coexist, p_coex_dm->bCurLowPenaltyRa
	);

	p_coex_dm->bPreLowPenaltyRa = p_coex_dm->bCurLowPenaltyRa;
}

static void halbtc8723b1ant_SetCoexTable(
	struct btc_coexist *p_bt_coexist,
	u32 val0x6c0,
	u32 val0x6c4,
	u32 val0x6c8,
	u8 val0x6cc
)
{
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_SW_EXEC,
		("[BTCoex], set coex table, set 0x6c0 = 0x%x\n", val0x6c0)
	);
	p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x6c0, val0x6c0);

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_SW_EXEC,
		("[BTCoex], set coex table, set 0x6c4 = 0x%x\n", val0x6c4)
	);
	p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x6c4, val0x6c4);

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_SW_EXEC,
		("[BTCoex], set coex table, set 0x6c8 = 0x%x\n", val0x6c8)
	);
	p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x6c8, val0x6c8);

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_SW_EXEC,
		("[BTCoex], set coex table, set 0x6cc = 0x%x\n", val0x6cc)
	);
	p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x6cc, val0x6cc);
}

static void halbtc8723b1ant_CoexTable(
	struct btc_coexist *p_bt_coexist,
	bool force_exec,
	u32 val0x6c0,
	u32 val0x6c4,
	u32 val0x6c8,
	u8 val0x6cc
)
{
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_SW,
		(
			"[BTCoex], %s write Coex Table 0x6c0 = 0x%x, 0x6c4 = 0x%x, 0x6cc = 0x%x\n",
			(force_exec ? "force to" : ""),
			val0x6c0, val0x6c4, val0x6cc
		)
	);
	p_coex_dm->curVal0x6c0 = val0x6c0;
	p_coex_dm->curVal0x6c4 = val0x6c4;
	p_coex_dm->curVal0x6c8 = val0x6c8;
	p_coex_dm->curVal0x6cc = val0x6cc;

	if (!force_exec) {
		if (
			(p_coex_dm->preVal0x6c0 == p_coex_dm->curVal0x6c0) &&
		    (p_coex_dm->preVal0x6c4 == p_coex_dm->curVal0x6c4) &&
		    (p_coex_dm->preVal0x6c8 == p_coex_dm->curVal0x6c8) &&
		    (p_coex_dm->preVal0x6cc == p_coex_dm->curVal0x6cc)
		)
			return;
	}

	halbtc8723b1ant_SetCoexTable(
		p_bt_coexist, val0x6c0, val0x6c4, val0x6c8, val0x6cc
	);

	p_coex_dm->preVal0x6c0 = p_coex_dm->curVal0x6c0;
	p_coex_dm->preVal0x6c4 = p_coex_dm->curVal0x6c4;
	p_coex_dm->preVal0x6c8 = p_coex_dm->curVal0x6c8;
	p_coex_dm->preVal0x6cc = p_coex_dm->curVal0x6cc;
}

static void halbtc8723b1ant_CoexTableWithType(
	struct btc_coexist *p_bt_coexist, bool force_exec, u8 type
)
{
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE,
		("[BTCoex], ********** CoexTable(%d) **********\n", type)
	);

	p_coex_sta->nCoexTableType = type;

	switch (type) {
	case 0:
		halbtc8723b1ant_CoexTable(
			p_bt_coexist, force_exec, 0x55555555, 0x55555555, 0xffffff, 0x3
		);
		break;
	case 1:
		halbtc8723b1ant_CoexTable(
			p_bt_coexist, force_exec, 0x55555555, 0x5a5a5a5a, 0xffffff, 0x3
		);
		break;
	case 2:
		halbtc8723b1ant_CoexTable(
			p_bt_coexist, force_exec, 0x5a5a5a5a, 0x5a5a5a5a, 0xffffff, 0x3
		);
		break;
	case 3:
		halbtc8723b1ant_CoexTable(
			p_bt_coexist, force_exec, 0xaaaa5555, 0xaaaa5a5a, 0xffffff, 0x3
		);
		break;
	case 4:
		halbtc8723b1ant_CoexTable(
			p_bt_coexist, force_exec, 0x55555555, 0xaaaa5a5a, 0xffffff, 0x3
		);
		break;
	case 5:
		halbtc8723b1ant_CoexTable(
			p_bt_coexist, force_exec, 0x5a5a5a5a, 0xaaaa5a5a, 0xffffff, 0x3
		);
		break;
	case 6:
		halbtc8723b1ant_CoexTable(
			p_bt_coexist, force_exec, 0x55555555, 0xaaaaaaaa, 0xffffff, 0x3
		);
		break;
	case 7:
		halbtc8723b1ant_CoexTable(
			p_bt_coexist, force_exec, 0xaaaaaaaa, 0xaaaaaaaa, 0xffffff, 0x3
		);
		break;
	default:
		break;
	}
}

static void halbtc8723b1ant_SetFwIgnoreWlanAct(
	struct btc_coexist *p_bt_coexist, bool bEnable
)
{
	u8 h2c_parameter[1] = {0};

	if (bEnable)
		h2c_parameter[0] |= BIT0; /* function enable */

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW_EXEC,
		(
			"[BTCoex], set FW for BT Ignore Wlan_Act, FW write 0x63 = 0x%x\n",
			h2c_parameter[0]
		)
	);

	p_bt_coexist->fBtcFillH2c(p_bt_coexist, 0x63, 1, h2c_parameter);
}

static void halbtc8723b1ant_IgnoreWlanAct(
	struct btc_coexist *p_bt_coexist, bool force_exec, bool bEnable
)
{
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW,
		(
			"[BTCoex], %s turn Ignore WlanAct %s\n",
			(force_exec ? "force to" : ""),
			(bEnable ? "ON" : "OFF")
		)
	);
	p_coex_dm->bCurIgnoreWlanAct = bEnable;

	if (!force_exec) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_FW_DETAIL,
			(
				"[BTCoex], bPreIgnoreWlanAct = %d, bCurIgnoreWlanAct = %d!!\n",
				p_coex_dm->bPreIgnoreWlanAct,
				p_coex_dm->bCurIgnoreWlanAct
			)
		);

		if (p_coex_dm->bPreIgnoreWlanAct == p_coex_dm->bCurIgnoreWlanAct)
			return;
	}
	halbtc8723b1ant_SetFwIgnoreWlanAct(p_bt_coexist, bEnable);

	p_coex_dm->bPreIgnoreWlanAct = p_coex_dm->bCurIgnoreWlanAct;
}

static void halbtc8723b1ant_SetLpsRpwm(
	struct btc_coexist *p_bt_coexist, u8 lpsVal, u8 rpwmVal
)
{
	u8 lps = lpsVal;
	u8 rpwm = rpwmVal;

	p_bt_coexist->fBtcSet(p_bt_coexist, BTC_SET_U1_LPS_VAL, &lps);
	p_bt_coexist->fBtcSet(p_bt_coexist, BTC_SET_U1_RPWM_VAL, &rpwm);
}

static void halbtc8723b1ant_LpsRpwm(
	struct btc_coexist *p_bt_coexist, bool force_exec, u8 lpsVal, u8 rpwmVal
)
{
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW,
		(
			"[BTCoex], %s set lps/rpwm = 0x%x/0x%x\n",
			(force_exec ? "force to" : ""),
			lpsVal,
			rpwmVal
		)
	);
	p_coex_dm->curLps = lpsVal;
	p_coex_dm->curRpwm = rpwmVal;

	if (!force_exec) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_FW_DETAIL,
			(
				"[BTCoex], LPS-RxBeaconMode = 0x%x , LPS-RPWM = 0x%x!!\n",
				p_coex_dm->curLps,
				p_coex_dm->curRpwm
			)
		);

		if (
			(p_coex_dm->preLps == p_coex_dm->curLps) &&
			(p_coex_dm->preRpwm == p_coex_dm->curRpwm)
		) {
			BTC_PRINT(
				BTC_MSG_ALGORITHM,
				ALGO_TRACE_FW_DETAIL,
				(
					"[BTCoex], LPS-RPWM_Last = 0x%x , LPS-RPWM_Now = 0x%x!!\n",
					p_coex_dm->preRpwm,
					p_coex_dm->curRpwm
				)
			);

			return;
		}
	}
	halbtc8723b1ant_SetLpsRpwm(p_bt_coexist, lpsVal, rpwmVal);

	p_coex_dm->preLps = p_coex_dm->curLps;
	p_coex_dm->preRpwm = p_coex_dm->curRpwm;
}

static void halbtc8723b1ant_SwMechanism(
	struct btc_coexist *p_bt_coexist, bool bLowPenaltyRA
)
{
	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_BT_MONITOR,
		("[BTCoex], SM[LpRA] = %d\n", bLowPenaltyRA)
	);

	halbtc8723b1ant_LowPenaltyRa(p_bt_coexist, NORMAL_EXEC, bLowPenaltyRA);
}

static void halbtc8723b1ant_SetAntPath(
	struct btc_coexist *p_bt_coexist, u8 antPosType, bool bInitHwCfg, bool bWifiOff
)
{
	struct btc_board_info *pBoardInfo = &p_bt_coexist->boardInfo;
	u32 fw_ver = 0, u4Tmp = 0, cntBtCalChk = 0;
	bool bPgExtSwitch = false;
	bool bUseExtSwitch = false;
	bool bIsInMpMode = false;
	u8 h2c_parameter[2] = {0}, u1Tmp = 0;

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_EXT_SWITCH, &bPgExtSwitch);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver); /*  [31:16]=fw ver, [15:0]=fw sub ver */

	if ((fw_ver > 0 && fw_ver < 0xc0000) || bPgExtSwitch)
		bUseExtSwitch = true;

	if (bInitHwCfg) {
		p_bt_coexist->fBtcSetRfReg(p_bt_coexist, BTC_RF_A, 0x1, 0xfffff, 0x780); /* WiFi TRx Mask on */
		p_bt_coexist->fBtcSetBtReg(p_bt_coexist, BTC_BT_REG_RF, 0x3c, 0x15); /* BT TRx Mask on */

		if (fw_ver >= 0x180000) {
			/* Use H2C to set GNT_BT to HIGH */
			h2c_parameter[0] = 1;
			p_bt_coexist->fBtcFillH2c(p_bt_coexist, 0x6E, 1, h2c_parameter);
		} else /*  set grant_bt to high */
			p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x765, 0x18);

		/* set wlan_act control by PTA */
		p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x76e, 0x4);

		p_bt_coexist->fBtcWrite1ByteBitMask(p_bt_coexist, 0x67, 0x20, 0x1); /* BT select s0/s1 is controlled by WiFi */

		p_bt_coexist->fBtcWrite1ByteBitMask(p_bt_coexist, 0x39, 0x8, 0x1);
		p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x974, 0xff);
		p_bt_coexist->fBtcWrite1ByteBitMask(p_bt_coexist, 0x944, 0x3, 0x3);
		p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x930, 0x77);
	} else if (bWifiOff) {
		if (fw_ver >= 0x180000) {
			/* Use H2C to set GNT_BT to HIGH */
			h2c_parameter[0] = 1;
			p_bt_coexist->fBtcFillH2c(p_bt_coexist, 0x6E, 1, h2c_parameter);
		} else /*  set grant_bt to high */
			p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x765, 0x18);

		/* set wlan_act to always low */
		p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x76e, 0x4);

		p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_IS_IN_MP_MODE, &bIsInMpMode);
		if (!bIsInMpMode)
			p_bt_coexist->fBtcWrite1ByteBitMask(p_bt_coexist, 0x67, 0x20, 0x0); /* BT select s0/s1 is controlled by BT */
		else
			p_bt_coexist->fBtcWrite1ByteBitMask(p_bt_coexist, 0x67, 0x20, 0x1); /* BT select s0/s1 is controlled by WiFi */

		/*  0x4c[24:23]= 00, Set Antenna control by BT_RFE_CTRL	BT Vendor 0xac = 0xf002 */
		u4Tmp = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x4c);
		u4Tmp &= ~BIT23;
		u4Tmp &= ~BIT24;
		p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x4c, u4Tmp);
	} else {
		/* Use H2C to set GNT_BT to LOW */
		if (fw_ver >= 0x180000) {
			if (p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0x765) != 0) {
				h2c_parameter[0] = 0;
				p_bt_coexist->fBtcFillH2c(p_bt_coexist, 0x6E, 1, h2c_parameter);
			}
		} else {
			/*  BT calibration check */
			while (cntBtCalChk <= 20) {
				u1Tmp = p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0x49d);
				cntBtCalChk++;

				if (u1Tmp & BIT0) {
					BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], ########### BT is calibrating (wait cnt =%d) ###########\n", cntBtCalChk));
					mdelay(50);
				} else {
					BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], ********** BT is NOT calibrating (wait cnt =%d)**********\n", cntBtCalChk));
					break;
				}
			}

			/*  set grant_bt to PTA */
			p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x765, 0x0);
		}

		if (p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0x76e) != 0xc)
			/* set wlan_act control by PTA */
			p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x76e, 0xc);
	}

	if (bUseExtSwitch) {
		if (bInitHwCfg) {
			/*  0x4c[23]= 0, 0x4c[24]= 1  Antenna control by WL/BT */
			u4Tmp = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x4c);
			u4Tmp &= ~BIT23;
			u4Tmp |= BIT24;
			p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x4c, u4Tmp);

			p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x948, 0x0); /*  fixed internal switch S1->WiFi, S0->BT */

			if (pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT) {
				/* tell firmware "no antenna inverse" */
				h2c_parameter[0] = 0;
				h2c_parameter[1] = 1;  /* ext switch type */
				p_bt_coexist->fBtcFillH2c(p_bt_coexist, 0x65, 2, h2c_parameter);
			} else {
				/* tell firmware "antenna inverse" */
				h2c_parameter[0] = 1;
				h2c_parameter[1] = 1;  /* ext switch type */
				p_bt_coexist->fBtcFillH2c(p_bt_coexist, 0x65, 2, h2c_parameter);
			}
		}


		/*  ext switch setting */
		switch (antPosType) {
		case BTC_ANT_PATH_WIFI:
			if (pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT)
				p_bt_coexist->fBtcWrite1ByteBitMask(p_bt_coexist, 0x92c, 0x3, 0x1);
			else
				p_bt_coexist->fBtcWrite1ByteBitMask(p_bt_coexist, 0x92c, 0x3, 0x2);
			break;
		case BTC_ANT_PATH_BT:
			if (pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT)
				p_bt_coexist->fBtcWrite1ByteBitMask(p_bt_coexist, 0x92c, 0x3, 0x2);
			else
				p_bt_coexist->fBtcWrite1ByteBitMask(p_bt_coexist, 0x92c, 0x3, 0x1);
			break;
		default:
		case BTC_ANT_PATH_PTA:
			if (pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT)
				p_bt_coexist->fBtcWrite1ByteBitMask(p_bt_coexist, 0x92c, 0x3, 0x1);
			else
				p_bt_coexist->fBtcWrite1ByteBitMask(p_bt_coexist, 0x92c, 0x3, 0x2);
			break;
		}

	} else {
		if (bInitHwCfg) {
			/*  0x4c[23]= 1, 0x4c[24]= 0  Antenna control by 0x64 */
			u4Tmp = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x4c);
			u4Tmp |= BIT23;
			u4Tmp &= ~BIT24;
			p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x4c, u4Tmp);

			/* Fix Ext switch Main->S1, Aux->S0 */
			p_bt_coexist->fBtcWrite1ByteBitMask(p_bt_coexist, 0x64, 0x1, 0x0);

			if (pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT) {

				/* tell firmware "no antenna inverse" */
				h2c_parameter[0] = 0;
				h2c_parameter[1] = 0;  /* internal switch type */
				p_bt_coexist->fBtcFillH2c(p_bt_coexist, 0x65, 2, h2c_parameter);
			} else {

				/* tell firmware "antenna inverse" */
				h2c_parameter[0] = 1;
				h2c_parameter[1] = 0;  /* internal switch type */
				p_bt_coexist->fBtcFillH2c(p_bt_coexist, 0x65, 2, h2c_parameter);
			}
		}


		/*  internal switch setting */
		switch (antPosType) {
		case BTC_ANT_PATH_WIFI:
			if (pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT)
				p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x948, 0x0);
			else
				p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x948, 0x280);
			break;
		case BTC_ANT_PATH_BT:
			if (pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT)
				p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x948, 0x280);
			else
				p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x948, 0x0);
			break;
		default:
		case BTC_ANT_PATH_PTA:
			if (pBoardInfo->btdmAntPos == BTC_ANTENNA_AT_MAIN_PORT)
				p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x948, 0x200);
			else
				p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x948, 0x80);
			break;
		}
	}
}

static void halbtc8723b1ant_SetFwPstdma(
	struct btc_coexist *p_bt_coexist, u8 byte1, u8 byte2, u8 byte3, u8 byte4, u8 byte5
)
{
	u8 h2c_parameter[5] = {0};
	u8 realByte1 = byte1, realByte5 = byte5;
	bool bApEnable = false;

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE, &bApEnable);

	if (bApEnable) {
		if (byte1 & BIT4 && !(byte1 & BIT5)) {
			BTC_PRINT(
				BTC_MSG_INTERFACE,
				INTF_NOTIFY,
				("[BTCoex], FW for 1Ant AP mode\n")
			);
			realByte1 &= ~BIT4;
			realByte1 |= BIT5;

			realByte5 |= BIT5;
			realByte5 &= ~BIT6;
		}
	}

	h2c_parameter[0] = realByte1;
	h2c_parameter[1] = byte2;
	h2c_parameter[2] = byte3;
	h2c_parameter[3] = byte4;
	h2c_parameter[4] = realByte5;

	p_coex_dm->psTdmaPara[0] = realByte1;
	p_coex_dm->psTdmaPara[1] = byte2;
	p_coex_dm->psTdmaPara[2] = byte3;
	p_coex_dm->psTdmaPara[3] = byte4;
	p_coex_dm->psTdmaPara[4] = realByte5;

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW_EXEC,
		(
			"[BTCoex], PS-TDMA H2C cmd = 0x%x%08x\n",
			h2c_parameter[0],
			h2c_parameter[1] << 24 |
			h2c_parameter[2] << 16 |
			h2c_parameter[3] << 8 |
			h2c_parameter[4]
		)
	);

	p_bt_coexist->fBtcFillH2c(p_bt_coexist, 0x60, 5, h2c_parameter);
}


static void halbtc8723b1ant_PsTdma(
	struct btc_coexist *p_bt_coexist, bool force_exec, bool bTurnOn, u8 type
)
{
	struct btc_bt_link_info *pBtLinkInfo = &p_bt_coexist->btLinkInfo;
	bool bWifiBusy = false;
	u8 rssiAdjustVal = 0;
	u8 psTdmaByte4Val = 0x50, psTdmaByte0Val = 0x51, psTdmaByte3Val =  0x10;
	s8 nWiFiDurationAdjust = 0x0;
	/* u32 		fw_ver = 0; */

	/* BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW, ("[BTCoex], %s turn %s PS TDMA, type =%d\n", */
	/* 	(force_exec? "force to":""), (bTurnOn? "ON":"OFF"), type)); */
	p_coex_dm->bCurPsTdmaOn = bTurnOn;
	p_coex_dm->curPsTdma = type;

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);

	if (p_coex_dm->bCurPsTdmaOn) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE,
			(
				"[BTCoex], ********** TDMA(on, %d) **********\n",
				p_coex_dm->curPsTdma
			)
		);
	} else {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE,
			(
				"[BTCoex], ********** TDMA(off, %d) **********\n",
				p_coex_dm->curPsTdma
			)
		);
	}

	if (!force_exec) {
		if (
			(p_coex_dm->bPrePsTdmaOn == p_coex_dm->bCurPsTdmaOn) &&
			(p_coex_dm->prePsTdma == p_coex_dm->curPsTdma)
		)
			return;
	}

	if (p_coex_sta->nScanAPNum <= 5)
		nWiFiDurationAdjust = 5;
	else if  (p_coex_sta->nScanAPNum >= 40)
		nWiFiDurationAdjust = -15;
	else if  (p_coex_sta->nScanAPNum >= 20)
		nWiFiDurationAdjust = -10;

	if (!p_coex_sta->bForceLpsOn) { /* only for A2DP-only case 1/2/9/11 */
		psTdmaByte0Val = 0x61;  /* no null-pkt */
		psTdmaByte3Val = 0x11; /*  no tx-pause at BT-slot */
		psTdmaByte4Val = 0x10; /*  0x778 = d/1 toggle */
	}


	if (bTurnOn) {
		if (pBtLinkInfo->bSlaveRole)
			psTdmaByte4Val = psTdmaByte4Val | 0x1;  /* 0x778 = 0x1 at wifi slot (no blocking BT Low-Pri pkts) */


		switch (type) {
		default:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x51, 0x1a, 0x1a, 0x0, psTdmaByte4Val
			);
			break;
		case 1:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist,
				psTdmaByte0Val,
				0x3a + nWiFiDurationAdjust,
				0x03,
				psTdmaByte3Val,
				psTdmaByte4Val
			);
			break;
		case 2:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist,
				psTdmaByte0Val,
				0x2d + nWiFiDurationAdjust,
				0x03,
				psTdmaByte3Val,
				psTdmaByte4Val
			);
			break;
		case 3:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x51, 0x1d, 0x1d, 0x0, 0x10
			);
			break;
		case 4:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x93, 0x15, 0x3, 0x14, 0x0
			);
			break;
		case 5:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x61, 0x15, 0x3, 0x11, 0x10
			);
			break;
		case 6:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x61, 0x20, 0x3, 0x11, 0x11
			);
			break;
		case 7:
			halbtc8723b1ant_SetFwPstdma(p_bt_coexist, 0x13, 0xc, 0x5, 0x0, 0x0);
			break;
		case 8:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x93, 0x25, 0x3, 0x10, 0x0
			);
			break;
		case 9:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist,
				psTdmaByte0Val,
				0x21,
				0x3,
				psTdmaByte3Val,
				psTdmaByte4Val
			);
			break;
		case 10:
			halbtc8723b1ant_SetFwPstdma(p_bt_coexist, 0x13, 0xa, 0xa, 0x0, 0x40);
			break;
		case 11:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist,
				psTdmaByte0Val,
				0x21,
				0x03,
				psTdmaByte3Val,
				psTdmaByte4Val
			);
			break;
		case 12:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x51, 0x0a, 0x0a, 0x0, 0x50
			);
			break;
		case 13:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x51, 0x12, 0x12, 0x0, 0x10
			);
			break;
		case 14:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x51, 0x21, 0x3, 0x10, psTdmaByte4Val
			);
			break;
		case 15:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x13, 0xa, 0x3, 0x8, 0x0
			);
			break;
		case 16:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x93, 0x15, 0x3, 0x10, 0x0
			);
			break;
		case 18:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x93, 0x25, 0x3, 0x10, 0x0
			);
			break;
		case 20:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x61, 0x3f, 0x03, 0x11, 0x10

			);
			break;
		case 21:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x61, 0x25, 0x03, 0x11, 0x11
			);
			break;
		case 22:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x61, 0x25, 0x03, 0x11, 0x10
			);
			break;
		case 23:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0xe3, 0x25, 0x3, 0x31, 0x18
			);
			break;
		case 24:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0xe3, 0x15, 0x3, 0x31, 0x18
			);
			break;
		case 25:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0xe3, 0xa, 0x3, 0x31, 0x18
			);
			break;
		case 26:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0xe3, 0xa, 0x3, 0x31, 0x18
			);
			break;
		case 27:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0xe3, 0x25, 0x3, 0x31, 0x98
			);
			break;
		case 28:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x69, 0x25, 0x3, 0x31, 0x0
			);
			break;
		case 29:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0xab, 0x1a, 0x1a, 0x1, 0x10
			);
			break;
		case 30:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x51, 0x30, 0x3, 0x10, 0x10
			);
			break;
		case 31:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0xd3, 0x1a, 0x1a, 0x0, 0x58
			);
			break;
		case 32:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x61, 0x35, 0x3, 0x11, 0x11
			);
			break;
		case 33:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0xa3, 0x25, 0x3, 0x30, 0x90
			);
			break;
		case 34:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x53, 0x1a, 0x1a, 0x0, 0x10
			);
			break;
		case 35:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x63, 0x1a, 0x1a, 0x0, 0x10
			);
			break;
		case 36:
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0xd3, 0x12, 0x3, 0x14, 0x50
			);
			break;
		case 40: /*  SoftAP only with no sta associated, BT disable , TDMA mode for power saving */
			/* here softap mode screen off will cost 70-80mA for phone */
			halbtc8723b1ant_SetFwPstdma(
				p_bt_coexist, 0x23, 0x18, 0x00, 0x10, 0x24
			);
			break;
		}
	} else {

		/*  disable PS tdma */
		switch (type) {
		case 8: /* PTA Control */
			halbtc8723b1ant_SetFwPstdma(p_bt_coexist, 0x8, 0x0, 0x0, 0x0, 0x0);
			halbtc8723b1ant_SetAntPath(
				p_bt_coexist, BTC_ANT_PATH_PTA, false, false
			);
			break;
		case 0:
		default:  /* Software control, Antenna at BT side */
			halbtc8723b1ant_SetFwPstdma(p_bt_coexist, 0x0, 0x0, 0x0, 0x0, 0x0);
			halbtc8723b1ant_SetAntPath(
				p_bt_coexist, BTC_ANT_PATH_BT, false, false
			);
			break;
		case 9:   /* Software control, Antenna at WiFi side */
			halbtc8723b1ant_SetFwPstdma(p_bt_coexist, 0x0, 0x0, 0x0, 0x0, 0x0);
			halbtc8723b1ant_SetAntPath(
				p_bt_coexist, BTC_ANT_PATH_WIFI, false, false
			);
			break;
		}
	}

	rssiAdjustVal = 0;
	p_bt_coexist->fBtcSet(
		p_bt_coexist, BTC_SET_U1_RSSI_ADJ_VAL_FOR_1ANT_COEX_TYPE, &rssiAdjustVal
	);

	/*  update pre state */
	p_coex_dm->bPrePsTdmaOn = p_coex_dm->bCurPsTdmaOn;
	p_coex_dm->prePsTdma = p_coex_dm->curPsTdma;
}

static bool halbtc8723b1ant_IsCommonAction(struct btc_coexist *p_bt_coexist)
{
	bool bCommon = false, bWifiConnected = false, bWifiBusy = false;

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);

	if (
		!bWifiConnected &&
		BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE == p_coex_dm->btStatus
	) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE,
			("[BTCoex], Wifi non connected-idle + BT non connected-idle!!\n")
		);

		/* halbtc8723b1ant_SwMechanism(p_bt_coexist, false); */

		bCommon = true;
	} else if (
		bWifiConnected &&
		(BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE == p_coex_dm->btStatus)
	) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE,
			("[BTCoex], Wifi connected + BT non connected-idle!!\n")
		);

		/* halbtc8723b1ant_SwMechanism(p_bt_coexist, false); */

		bCommon = true;
	} else if (
		!bWifiConnected &&
		(BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE == p_coex_dm->btStatus)
	) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE,
			("[BTCoex], Wifi non connected-idle + BT connected-idle!!\n")
		);

		/* halbtc8723b1ant_SwMechanism(p_bt_coexist, false); */

		bCommon = true;
	} else if (
		bWifiConnected &&
		(BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE == p_coex_dm->btStatus)
	) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Wifi connected + BT connected-idle!!\n"));

		/* halbtc8723b1ant_SwMechanism(p_bt_coexist, false); */

		bCommon = true;
	} else if (
		!bWifiConnected &&
		(BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE != p_coex_dm->btStatus)
	) {
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE,
			("[BTCoex], Wifi non connected-idle + BT Busy!!\n")
		);

		/* halbtc8723b1ant_SwMechanism(p_bt_coexist, false); */

		bCommon = true;
	} else {
		if (bWifiBusy) {
			BTC_PRINT(
				BTC_MSG_ALGORITHM,
				ALGO_TRACE,
				("[BTCoex], Wifi Connected-Busy + BT Busy!!\n")
			);
		} else {
			BTC_PRINT(
				BTC_MSG_ALGORITHM,
				ALGO_TRACE,
				("[BTCoex], Wifi Connected-Idle + BT Busy!!\n")
			);
		}

		bCommon = false;
	}

	return bCommon;
}


static void halbtc8723b1ant_TdmaDurationAdjustForAcl(
	struct btc_coexist *p_bt_coexist, u8 wifiStatus
)
{
	static s32 up, dn, m, n, WaitCount;
	s32 result;   /* 0: no change, +1: increase WiFi duration, -1: decrease WiFi duration */
	u8 retryCount = 0, btInfoExt;

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW,
		("[BTCoex], TdmaDurationAdjustForAcl()\n")
	);

	if (
		(BT_8723B_1ANT_WIFI_STATUS_NON_CONNECTED_ASSO_AUTH_SCAN == wifiStatus) ||
		(BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SCAN == wifiStatus) ||
		(BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SPECIAL_PKT == wifiStatus)
	) {
		if (
			p_coex_dm->curPsTdma != 1 &&
			p_coex_dm->curPsTdma != 2 &&
			p_coex_dm->curPsTdma != 3 &&
			p_coex_dm->curPsTdma != 9
		) {
			halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 9);
			p_coex_dm->psTdmaDuAdjType = 9;

			up = 0;
			dn = 0;
			m = 1;
			n = 3;
			result = 0;
			WaitCount = 0;
		}
		return;
	}

	if (!p_coex_dm->bAutoTdmaAdjust) {
		p_coex_dm->bAutoTdmaAdjust = true;
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE_FW_DETAIL,
			("[BTCoex], first run TdmaDurationAdjust()!!\n")
		);

		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 2);
		p_coex_dm->psTdmaDuAdjType = 2;
		/*  */
		up = 0;
		dn = 0;
		m = 1;
		n = 3;
		result = 0;
		WaitCount = 0;
	} else {
		/* acquire the BT TRx retry count from BT_Info byte2 */
		retryCount = p_coex_sta->btRetryCnt;
		btInfoExt = p_coex_sta->btInfoExt;
		/* BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], retryCount = %d\n", retryCount)); */
		/* BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], up =%d, dn =%d, m =%d, n =%d, WaitCount =%d\n", */
		/* 	up, dn, m, n, WaitCount)); */

		if (p_coex_sta->lowPriorityTx > 1050 || p_coex_sta->lowPriorityRx > 1250)
			retryCount++;

		result = 0;
		WaitCount++;

		if (retryCount == 0) { /*  no retry in the last 2-second duration */
			up++;
			dn--;

			if (dn <= 0)
				dn = 0;

			if (up >= n) { /*  if 連續 n 個2秒 retry count為0, 則調寬WiFi duration */
				WaitCount = 0;
				n = 3;
				up = 0;
				dn = 0;
				result = 1;
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_TRACE_FW_DETAIL,
					("[BTCoex], Increase wifi duration!!\n")
				);
			}
		} else if (retryCount <= 3) { /*  <=3 retry in the last 2-second duration */
			up--;
			dn++;

			if (up <= 0)
				up = 0;

			if (dn == 2) { /*  if 連續 2 個2秒 retry count< 3, 則調窄WiFi duration */
				if (WaitCount <= 2)
					m++; /*  避免一直在兩個level中來回 */
				else
					m = 1;

				if (m >= 20) /* m 最大值 = 20 ' 最大120秒 recheck是否調整 WiFi duration. */
					m = 20;

				n = 3 * m;
				up = 0;
				dn = 0;
				WaitCount = 0;
				result = -1;
				BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE_FW_DETAIL, ("[BTCoex], Decrease wifi duration for retryCounter<3!!\n"));
			}
		} else { /* retry count > 3, 只要1次 retry count > 3, 則調窄WiFi duration */
			if (WaitCount == 1)
				m++; /*  避免一直在兩個level中來回 */
			else
				m = 1;

			if (m >= 20) /* m 最大值 = 20 ' 最大120秒 recheck是否調整 WiFi duration. */
				m = 20;

			n = 3 * m;
			up = 0;
			dn = 0;
			WaitCount = 0;
			result = -1;
			BTC_PRINT(
				BTC_MSG_ALGORITHM,
				ALGO_TRACE_FW_DETAIL,
				("[BTCoex], Decrease wifi duration for retryCounter>3!!\n")
			);
		}

		if (result == -1) {
			if (
				BT_INFO_8723B_1ANT_A2DP_BASIC_RATE(btInfoExt) &&
				((p_coex_dm->curPsTdma == 1) || (p_coex_dm->curPsTdma == 2))
			) {
				halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 9);
				p_coex_dm->psTdmaDuAdjType = 9;
			} else if (p_coex_dm->curPsTdma == 1) {
				halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 2);
				p_coex_dm->psTdmaDuAdjType = 2;
			} else if (p_coex_dm->curPsTdma == 2) {
				halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 9);
				p_coex_dm->psTdmaDuAdjType = 9;
			} else if (p_coex_dm->curPsTdma == 9) {
				halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 11);
				p_coex_dm->psTdmaDuAdjType = 11;
			}
		} else if (result == 1) {
			if (
				BT_INFO_8723B_1ANT_A2DP_BASIC_RATE(btInfoExt) &&
				((p_coex_dm->curPsTdma == 1) || (p_coex_dm->curPsTdma == 2))
			) {
				halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 9);
				p_coex_dm->psTdmaDuAdjType = 9;
			} else if (p_coex_dm->curPsTdma == 11) {
				halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 9);
				p_coex_dm->psTdmaDuAdjType = 9;
			} else if (p_coex_dm->curPsTdma == 9) {
				halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 2);
				p_coex_dm->psTdmaDuAdjType = 2;
			} else if (p_coex_dm->curPsTdma == 2) {
				halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 1);
				p_coex_dm->psTdmaDuAdjType = 1;
			}
		} else {	  /* no change */
			BTC_PRINT(
				BTC_MSG_ALGORITHM,
				ALGO_TRACE_FW_DETAIL,
				(
					"[BTCoex], ********** TDMA(on, %d) **********\n",
					p_coex_dm->curPsTdma
				)
			);
		}

		if (
			p_coex_dm->curPsTdma != 1 &&
			p_coex_dm->curPsTdma != 2 &&
			p_coex_dm->curPsTdma != 9 &&
			p_coex_dm->curPsTdma != 11
		) /*  recover to previous adjust type */
			halbtc8723b1ant_PsTdma(
				p_bt_coexist, NORMAL_EXEC, true, p_coex_dm->psTdmaDuAdjType
			);
	}
}

static void halbtc8723b1ant_PsTdmaCheckForPowerSaveState(
	struct btc_coexist *p_bt_coexist, bool bNewPsState
)
{
	u8 lpsMode = 0x0;

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_U1_LPS_MODE, &lpsMode);

	if (lpsMode) {	/*  already under LPS state */
		if (bNewPsState) {
			/*  keep state under LPS, do nothing. */
		} else /*  will leave LPS state, turn off psTdma first */
			halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, false, 0);
	} else {						/*  NO PS state */
		if (bNewPsState) /*  will enter LPS state, turn off psTdma first */
			halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, false, 0);
		else {
			/*  keep state under NO PS state, do nothing. */
		}
	}
}

static void halbtc8723b1ant_PowerSaveState(
	struct btc_coexist *p_bt_coexist, u8 psType, u8 lpsVal, u8 rpwmVal
)
{
	bool bLowPwrDisable = false;

	switch (psType) {
	case BTC_PS_WIFI_NATIVE:
		/*  recover to original 32k low power setting */
		bLowPwrDisable = false;
		p_bt_coexist->fBtcSet(
			p_bt_coexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable
		);
		p_bt_coexist->fBtcSet(p_bt_coexist, BTC_SET_ACT_NORMAL_LPS, NULL);
		p_coex_sta->bForceLpsOn = false;
		break;
	case BTC_PS_LPS_ON:
		halbtc8723b1ant_PsTdmaCheckForPowerSaveState(p_bt_coexist, true);
		halbtc8723b1ant_LpsRpwm(p_bt_coexist, NORMAL_EXEC, lpsVal, rpwmVal);
		/*  when coex force to enter LPS, do not enter 32k low power. */
		bLowPwrDisable = true;
		p_bt_coexist->fBtcSet(
			p_bt_coexist, BTC_SET_ACT_DISABLE_LOW_POWER, &bLowPwrDisable
		);
		/*  power save must executed before psTdma. */
		p_bt_coexist->fBtcSet(p_bt_coexist, BTC_SET_ACT_ENTER_LPS, NULL);
		p_coex_sta->bForceLpsOn = true;
		break;
	case BTC_PS_LPS_OFF:
		halbtc8723b1ant_PsTdmaCheckForPowerSaveState(p_bt_coexist, false);
		p_bt_coexist->fBtcSet(p_bt_coexist, BTC_SET_ACT_LEAVE_LPS, NULL);
		p_coex_sta->bForceLpsOn = false;
		break;
	default:
		break;
	}
}

/*  */
/*  */
/* 	Software Coex Mechanism start */
/*  */
/*  */

/*  */
/*  */
/* 	Non-Software Coex Mechanism start */
/*  */
/*  */
static void halbtc8723b1ant_ActionWifiMultiPort(struct btc_coexist *p_bt_coexist)
{
	halbtc8723b1ant_PowerSaveState(p_bt_coexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, false, 8);
	halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 2);
}

static void halbtc8723b1ant_ActionHs(struct btc_coexist *p_bt_coexist)
{
	halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 5);
	halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 2);
}

static void halbtc8723b1ant_ActionBtInquiry(struct btc_coexist *p_bt_coexist)
{
	struct btc_bt_link_info *pBtLinkInfo = &p_bt_coexist->btLinkInfo;
	bool bWifiConnected = false;
	bool bApEnable = false;
	bool bWifiBusy = false;
	bool bBtBusy = false;

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE, &bApEnable);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	p_bt_coexist->fBtcSet(p_bt_coexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bBtBusy);

	if (!bWifiConnected && !p_coex_sta->bWiFiIsHighPriTask) {
		halbtc8723b1ant_PowerSaveState(p_bt_coexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, false, 8);

		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 0);
	} else if (
		pBtLinkInfo->bScoExist ||
		pBtLinkInfo->bHidExist ||
		pBtLinkInfo->bA2dpExist
	) {
		/*  SCO/HID/A2DP busy */
		halbtc8723b1ant_PowerSaveState(p_bt_coexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 32);

		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 4);
	} else if (pBtLinkInfo->bPanExist || bWifiBusy) {
		halbtc8723b1ant_PowerSaveState(p_bt_coexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 20);

		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 4);
	} else {
		halbtc8723b1ant_PowerSaveState(p_bt_coexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, false, 8);

		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 7);
	}
}

static void halbtc8723b1ant_ActionBtScoHidOnlyBusy(
	struct btc_coexist *p_bt_coexist, u8 wifiStatus
)
{
	struct btc_bt_link_info *pBtLinkInfo = &p_bt_coexist->btLinkInfo;
	bool bWifiConnected = false;

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);

	/*  tdma and coex table */

	if (pBtLinkInfo->bScoExist) {
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 5);
		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 5);
	} else { /* HID */
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 6);
		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 5);
	}
}

static void halbtc8723b1ant_ActionWifiConnectedBtAclBusy(
	struct btc_coexist *p_bt_coexist, u8 wifiStatus
)
{
	struct btc_bt_link_info *pBtLinkInfo = &p_bt_coexist->btLinkInfo;
	hal_btc_8723b_1ant_btRssi_state(2, 28, 0);

	if ((p_coex_sta->lowPriorityRx >= 1000) && (p_coex_sta->lowPriorityRx != 65535))
		pBtLinkInfo->bSlaveRole = true;
	else
		pBtLinkInfo->bSlaveRole = false;

	if (pBtLinkInfo->bHidOnly) { /* HID */
		halbtc8723b1ant_ActionBtScoHidOnlyBusy(p_bt_coexist, wifiStatus);
		p_coex_dm->bAutoTdmaAdjust = false;
		return;
	} else if (pBtLinkInfo->bA2dpOnly) { /* A2DP */
		if (BT_8723B_1ANT_WIFI_STATUS_CONNECTED_IDLE == wifiStatus) {
			halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 32);
			halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 4);
			p_coex_dm->bAutoTdmaAdjust = false;
		} else {
			halbtc8723b1ant_TdmaDurationAdjustForAcl(p_bt_coexist, wifiStatus);
			halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 4);
			p_coex_dm->bAutoTdmaAdjust = true;
		}
	} else if (pBtLinkInfo->bHidExist && pBtLinkInfo->bA2dpExist) { /* HID+A2DP */
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 14);
		p_coex_dm->bAutoTdmaAdjust = false;

		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 4);
	} else if (
		pBtLinkInfo->bPanOnly ||
		(pBtLinkInfo->bHidExist && pBtLinkInfo->bPanExist)
	) { /* PAN(OPP, FTP), HID+PAN(OPP, FTP) */
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 3);
		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 4);
		p_coex_dm->bAutoTdmaAdjust = false;
	} else if (
		(pBtLinkInfo->bA2dpExist && pBtLinkInfo->bPanExist) ||
		(pBtLinkInfo->bHidExist && pBtLinkInfo->bA2dpExist && pBtLinkInfo->bPanExist)
	) { /* A2DP+PAN(OPP, FTP), HID+A2DP+PAN(OPP, FTP) */
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 13);
		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 4);
		p_coex_dm->bAutoTdmaAdjust = false;
	} else {
		/* BT no-profile busy (0x9) */
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 32);
		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 4);
		p_coex_dm->bAutoTdmaAdjust = false;
	}
}

static void halbtc8723b1ant_ActionWifiNotConnected(struct btc_coexist *p_bt_coexist)
{
	/*  power save state */
	halbtc8723b1ant_PowerSaveState(p_bt_coexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	/*  tdma and coex table */
	halbtc8723b1ant_PsTdma(p_bt_coexist, FORCE_EXEC, false, 8);
	halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 0);
}

static void halbtc8723b1ant_ActionWifiNotConnectedScan(
	struct btc_coexist *p_bt_coexist
)
{
	struct btc_bt_link_info *pBtLinkInfo = &p_bt_coexist->btLinkInfo;

	halbtc8723b1ant_PowerSaveState(p_bt_coexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	/*  tdma and coex table */
	if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY == p_coex_dm->btStatus) {
		if (pBtLinkInfo->bA2dpExist) {
			halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 32);
			halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 4);
		} else if (pBtLinkInfo->bA2dpExist && pBtLinkInfo->bPanExist) {
			halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 22);
			halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 4);
		} else {
			halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 20);
			halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 4);
		}
	} else if (
		(BT_8723B_1ANT_BT_STATUS_SCO_BUSY == p_coex_dm->btStatus) ||
		(BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY == p_coex_dm->btStatus)
	) {
		halbtc8723b1ant_ActionBtScoHidOnlyBusy(
			p_bt_coexist, BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SCAN
		);
	} else {
		/* Bryant Add */
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, false, 8);
		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 2);
	}
}

static void halbtc8723b1ant_ActionWifiNotConnectedAssoAuth(
	struct btc_coexist *p_bt_coexist
)
{
	struct btc_bt_link_info *pBtLinkInfo = &p_bt_coexist->btLinkInfo;

	halbtc8723b1ant_PowerSaveState(p_bt_coexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	/*  tdma and coex table */
	if (
		(pBtLinkInfo->bScoExist) ||
		(pBtLinkInfo->bHidExist) ||
		(pBtLinkInfo->bA2dpExist)
	) {
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 32);
		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 4);
	} else if (pBtLinkInfo->bPanExist) {
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 20);
		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 4);
	} else {
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, false, 8);
		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 2);
	}
}

static void halbtc8723b1ant_ActionWifiConnectedScan(struct btc_coexist *p_bt_coexist)
{
	struct btc_bt_link_info *pBtLinkInfo = &p_bt_coexist->btLinkInfo;

	halbtc8723b1ant_PowerSaveState(p_bt_coexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	/*  tdma and coex table */
	if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY == p_coex_dm->btStatus) {
		if (pBtLinkInfo->bA2dpExist) {
			halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 32);
			halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 4);
		} else if (pBtLinkInfo->bA2dpExist && pBtLinkInfo->bPanExist) {
			halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 22);
			halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 4);
		} else {
			halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 20);
			halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 4);
		}
	} else if (
		(BT_8723B_1ANT_BT_STATUS_SCO_BUSY == p_coex_dm->btStatus) ||
		(BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY == p_coex_dm->btStatus)
	) {
		halbtc8723b1ant_ActionBtScoHidOnlyBusy(
			p_bt_coexist, BT_8723B_1ANT_WIFI_STATUS_CONNECTED_SCAN
		);
	} else {
		/* Bryant Add */
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, false, 8);
		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 2);
	}
}

static void halbtc8723b1ant_ActionWifiConnectedSpecialPacket(
	struct btc_coexist *p_bt_coexist
)
{
	struct btc_bt_link_info *pBtLinkInfo = &p_bt_coexist->btLinkInfo;

	halbtc8723b1ant_PowerSaveState(p_bt_coexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	/*  tdma and coex table */
	if (
		(pBtLinkInfo->bScoExist) ||
		(pBtLinkInfo->bHidExist) ||
		(pBtLinkInfo->bA2dpExist)
	) {
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 32);
		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 4);
	} else if (pBtLinkInfo->bPanExist) {
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, true, 20);
		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 4);
	} else {
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, false, 8);
		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 2);
	}
}

static void halbtc8723b1ant_ActionWifiConnected(struct btc_coexist *p_bt_coexist)
{
	bool bWifiBusy = false;
	bool bScan = false, bLink = false, bRoam = false;
	bool bUnder4way = false, bApEnable = false;

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE,
		("[BTCoex], CoexForWifiConnect() ===>\n")
	);

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_4_WAY_PROGRESS, &bUnder4way);
	if (bUnder4way) {
		halbtc8723b1ant_ActionWifiConnectedSpecialPacket(p_bt_coexist);
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE,
			("[BTCoex], CoexForWifiConnect(), return for wifi is under 4way<===\n")
		);
		return;
	}

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_SCAN, &bScan);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_LINK, &bLink);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_ROAM, &bRoam);
	if (bScan || bLink || bRoam) {
		if (bScan)
			halbtc8723b1ant_ActionWifiConnectedScan(p_bt_coexist);
		else
			halbtc8723b1ant_ActionWifiConnectedSpecialPacket(p_bt_coexist);
		BTC_PRINT(
			BTC_MSG_ALGORITHM,
			ALGO_TRACE,
			("[BTCoex], CoexForWifiConnect(), return for wifi is under scan<===\n")
		);
		return;
	}

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_AP_MODE_ENABLE, &bApEnable);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);

	/*  power save state */
	if (
		!bApEnable &&
		BT_8723B_1ANT_BT_STATUS_ACL_BUSY == p_coex_dm->btStatus &&
		!p_bt_coexist->btLinkInfo.bHidOnly
	) {
		if (p_bt_coexist->btLinkInfo.bA2dpOnly) { /* A2DP */
			if (!bWifiBusy)
				halbtc8723b1ant_PowerSaveState(
					p_bt_coexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0
				);
			else { /* busy */
				if  (p_coex_sta->nScanAPNum >= BT_8723B_1ANT_WIFI_NOISY_THRESH)  /* no force LPS, no PS-TDMA, use pure TDMA */
					halbtc8723b1ant_PowerSaveState(
						p_bt_coexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0
					);
				else
					halbtc8723b1ant_PowerSaveState(
						p_bt_coexist, BTC_PS_LPS_ON, 0x50, 0x4
					);
			}
		} else if (
			(!p_coex_sta->bPanExist) &&
			(!p_coex_sta->bA2dpExist) &&
			(!p_coex_sta->bHidExist)
		)
			halbtc8723b1ant_PowerSaveState(p_bt_coexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		else
			halbtc8723b1ant_PowerSaveState(p_bt_coexist, BTC_PS_LPS_ON, 0x50, 0x4);
	} else
		halbtc8723b1ant_PowerSaveState(p_bt_coexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);

	/*  tdma and coex table */
	if (!bWifiBusy) {
		if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY == p_coex_dm->btStatus) {
			halbtc8723b1ant_ActionWifiConnectedBtAclBusy(
				p_bt_coexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_IDLE
			);
		} else if (
			(BT_8723B_1ANT_BT_STATUS_SCO_BUSY == p_coex_dm->btStatus) ||
			(BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY == p_coex_dm->btStatus)
		) {
			halbtc8723b1ant_ActionBtScoHidOnlyBusy(p_bt_coexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_IDLE);
		} else {
			halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, false, 8);

			if ((p_coex_sta->highPriorityTx) + (p_coex_sta->highPriorityRx) <= 60)
				halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 2);
			else
				halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 7);
		}
	} else {
		if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY == p_coex_dm->btStatus) {
			halbtc8723b1ant_ActionWifiConnectedBtAclBusy(
				p_bt_coexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_BUSY
			);
		} else if (
			(BT_8723B_1ANT_BT_STATUS_SCO_BUSY == p_coex_dm->btStatus) ||
			(BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY == p_coex_dm->btStatus)
		) {
			halbtc8723b1ant_ActionBtScoHidOnlyBusy(
				p_bt_coexist,
				BT_8723B_1ANT_WIFI_STATUS_CONNECTED_BUSY
			);
		} else {
			halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, false, 8);

			if ((p_coex_sta->highPriorityTx) + (p_coex_sta->highPriorityRx) <= 60)
				halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 2);
			else
				halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 7);
		}
	}
}

static void halbtc8723b1ant_RunSwCoexistMechanism(struct btc_coexist *p_bt_coexist)
{
	u8 algorithm = 0;

	algorithm = halbtc8723b1ant_ActionAlgorithm(p_bt_coexist);
	p_coex_dm->curAlgorithm = algorithm;

	if (halbtc8723b1ant_IsCommonAction(p_bt_coexist)) {

	} else {
		switch (p_coex_dm->curAlgorithm) {
		case BT_8723B_1ANT_COEX_ALGO_SCO:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = SCO.\n"));
			/* halbtc8723b1ant_ActionSco(p_bt_coexist); */
			break;
		case BT_8723B_1ANT_COEX_ALGO_HID:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = HID.\n"));
			/* halbtc8723b1ant_ActionHid(p_bt_coexist); */
			break;
		case BT_8723B_1ANT_COEX_ALGO_A2DP:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = A2DP.\n"));
			/* halbtc8723b1ant_ActionA2dp(p_bt_coexist); */
			break;
		case BT_8723B_1ANT_COEX_ALGO_A2DP_PANHS:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = A2DP+PAN(HS).\n"));
			/* halbtc8723b1ant_ActionA2dpPanHs(p_bt_coexist); */
			break;
		case BT_8723B_1ANT_COEX_ALGO_PANEDR:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = PAN(EDR).\n"));
			/* halbtc8723b1ant_ActionPanEdr(p_bt_coexist); */
			break;
		case BT_8723B_1ANT_COEX_ALGO_PANHS:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = HS mode.\n"));
			/* halbtc8723b1ant_ActionPanHs(p_bt_coexist); */
			break;
		case BT_8723B_1ANT_COEX_ALGO_PANEDR_A2DP:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = PAN+A2DP.\n"));
			/* halbtc8723b1ant_ActionPanEdrA2dp(p_bt_coexist); */
			break;
		case BT_8723B_1ANT_COEX_ALGO_PANEDR_HID:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = PAN(EDR)+HID.\n"));
			/* halbtc8723b1ant_ActionPanEdrHid(p_bt_coexist); */
			break;
		case BT_8723B_1ANT_COEX_ALGO_HID_A2DP_PANEDR:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = HID+A2DP+PAN.\n"));
			/* halbtc8723b1ant_ActionHidA2dpPanEdr(p_bt_coexist); */
			break;
		case BT_8723B_1ANT_COEX_ALGO_HID_A2DP:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = HID+A2DP.\n"));
			/* halbtc8723b1ant_ActionHidA2dp(p_bt_coexist); */
			break;
		default:
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Action algorithm = coexist All Off!!\n"));
			break;
		}
		p_coex_dm->preAlgorithm = p_coex_dm->curAlgorithm;
	}
}

static void halbtc8723b1ant_RunCoexistMechanism(struct btc_coexist *p_bt_coexist)
{
	struct btc_bt_link_info *pBtLinkInfo = &p_bt_coexist->btLinkInfo;
	bool bWifiConnected = false, bBtHsOn = false;
	bool bIncreaseScanDevNum = false;
	bool bBtCtrlAggBufSize = false;
	u8 aggBufSize = 5;
	u32 wifiLinkStatus = 0;
	u32 numOfWifiLink = 0;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], RunCoexistMechanism() ===>\n"));

	if (p_bt_coexist->bManualControl) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], RunCoexistMechanism(), return for Manual CTRL <===\n"));
		return;
	}

	if (p_bt_coexist->bStopCoexDm) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], RunCoexistMechanism(), return for Stop Coex DM <===\n"));
		return;
	}

	if (p_coex_sta->bUnderIps) {
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], wifi is under IPS !!!\n"));
		return;
	}

	if (
		(BT_8723B_1ANT_BT_STATUS_ACL_BUSY == p_coex_dm->btStatus) ||
		(BT_8723B_1ANT_BT_STATUS_SCO_BUSY == p_coex_dm->btStatus) ||
		(BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY == p_coex_dm->btStatus)
	){
		bIncreaseScanDevNum = true;
	}

	p_bt_coexist->fBtcSet(
		p_bt_coexist,
		BTC_SET_BL_INC_SCAN_DEV_NUM,
		&bIncreaseScanDevNum
	);
	p_bt_coexist->fBtcGet(
		p_bt_coexist,
		BTC_GET_BL_WIFI_CONNECTED,
		&bWifiConnected
	);

	p_bt_coexist->fBtcGet(
		p_bt_coexist,
		BTC_GET_U4_WIFI_LINK_STATUS,
		&wifiLinkStatus
	);
	numOfWifiLink = wifiLinkStatus >> 16;

	if ((numOfWifiLink >= 2) || (wifiLinkStatus & WIFI_P2P_GO_CONNECTED)) {
		BTC_PRINT(
			BTC_MSG_INTERFACE,
			INTF_NOTIFY,
			(
				"############# [BTCoex],  Multi-Port numOfWifiLink = %d, wifiLinkStatus = 0x%x\n",
				numOfWifiLink,
				wifiLinkStatus
			)
		);
		halbtc8723b1ant_LimitedTx(p_bt_coexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8723b1ant_LimitedRx(p_bt_coexist, NORMAL_EXEC, false, bBtCtrlAggBufSize, aggBufSize);

		if ((pBtLinkInfo->bA2dpExist) && (p_coex_sta->bC2hBtInquiryPage)) {
			BTC_PRINT(
				BTC_MSG_INTERFACE,
				INTF_NOTIFY,
				("############# [BTCoex],  BT Is Inquirying\n")
			);
			halbtc8723b1ant_ActionBtInquiry(p_bt_coexist);
		} else
			halbtc8723b1ant_ActionWifiMultiPort(p_bt_coexist);

		return;
	}

	if ((pBtLinkInfo->bBtLinkExist) && (bWifiConnected)) {
		halbtc8723b1ant_LimitedTx(p_bt_coexist, NORMAL_EXEC, 1, 1, 0, 1);

		if (pBtLinkInfo->bScoExist)
			halbtc8723b1ant_LimitedRx(p_bt_coexist, NORMAL_EXEC, false, true, 0x5);
		else
			halbtc8723b1ant_LimitedRx(p_bt_coexist, NORMAL_EXEC, false, true, 0x8);

		halbtc8723b1ant_SwMechanism(p_bt_coexist, true);
		halbtc8723b1ant_RunSwCoexistMechanism(p_bt_coexist);  /* just print debug message */
	} else {
		halbtc8723b1ant_LimitedTx(p_bt_coexist, NORMAL_EXEC, 0, 0, 0, 0);

		halbtc8723b1ant_LimitedRx(p_bt_coexist, NORMAL_EXEC, false, false, 0x5);

		halbtc8723b1ant_SwMechanism(p_bt_coexist, false);
		halbtc8723b1ant_RunSwCoexistMechanism(p_bt_coexist); /* just print debug message */
	}

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	if (p_coex_sta->bC2hBtInquiryPage) {
		BTC_PRINT(
			BTC_MSG_INTERFACE,
			INTF_NOTIFY,
			("############# [BTCoex],  BT Is Inquirying\n")
		);
		halbtc8723b1ant_ActionBtInquiry(p_bt_coexist);
		return;
	} else if (bBtHsOn) {
		halbtc8723b1ant_ActionHs(p_bt_coexist);
		return;
	}


	if (!bWifiConnected) {
		bool bScan = false, bLink = false, bRoam = false;

		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], wifi is non connected-idle !!!\n"));

		p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_SCAN, &bScan);
		p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_LINK, &bLink);
		p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_ROAM, &bRoam);

		if (bScan || bLink || bRoam) {
			 if (bScan)
				halbtc8723b1ant_ActionWifiNotConnectedScan(p_bt_coexist);
			 else
				halbtc8723b1ant_ActionWifiNotConnectedAssoAuth(p_bt_coexist);
		} else
			halbtc8723b1ant_ActionWifiNotConnected(p_bt_coexist);
	} else /*  wifi LPS/Busy */
		halbtc8723b1ant_ActionWifiConnected(p_bt_coexist);
}

static void halbtc8723b1ant_InitCoexDm(struct btc_coexist *p_bt_coexist)
{
	/*  force to reset coex mechanism */

	/*  sw all off */
	halbtc8723b1ant_SwMechanism(p_bt_coexist, false);

	/* halbtc8723b1ant_PsTdma(p_bt_coexist, FORCE_EXEC, false, 8); */
	halbtc8723b1ant_CoexTableWithType(p_bt_coexist, FORCE_EXEC, 0);

	p_coex_sta->popEventCnt = 0;
}

static void halbtc8723b1ant_InitHwConfig(
	struct btc_coexist *p_bt_coexist,
	bool bBackUp,
	bool bWifiOnly
)
{
	u32 u4Tmp = 0;/*  fw_ver; */
	u8 u1Tmpa = 0, u1Tmpb = 0;

	BTC_PRINT(
		BTC_MSG_INTERFACE,
		INTF_INIT,
		("[BTCoex], 1Ant Init HW Config!!\n")
	);

	p_bt_coexist->fBtcWrite1ByteBitMask(p_bt_coexist, 0x550, 0x8, 0x1);  /* enable TBTT nterrupt */

	/*  0x790[5:0]= 0x5 */
	p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x790, 0x5);

	/*  Enable counter statistics */
	p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x778, 0x1);
	p_bt_coexist->fBtcWrite1ByteBitMask(p_bt_coexist, 0x40, 0x20, 0x1);

	/* Antenna config */
	if (bWifiOnly) {
		halbtc8723b1ant_SetAntPath(p_bt_coexist, BTC_ANT_PATH_WIFI, true, false);
		halbtc8723b1ant_PsTdma(p_bt_coexist, FORCE_EXEC, false, 9);
	} else
		halbtc8723b1ant_SetAntPath(p_bt_coexist, BTC_ANT_PATH_BT, true, false);

	/*  PTA parameter */
	halbtc8723b1ant_CoexTableWithType(p_bt_coexist, FORCE_EXEC, 0);

	u4Tmp = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x948);
	u1Tmpa = p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0x765);
	u1Tmpb = p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0x67);

	BTC_PRINT(
		BTC_MSG_INTERFACE,
		INTF_NOTIFY,
		(
			"############# [BTCoex], 0x948 = 0x%x, 0x765 = 0x%x, 0x67 = 0x%x\n",
			u4Tmp,
			u1Tmpa,
			u1Tmpb
		)
	);
}

/*  */
/*  work around function start with wa_halbtc8723b1ant_ */
/*  */
/*  */
/*  extern function start with EXhalbtc8723b1ant_ */
/*  */
void EXhalbtc8723b1ant_PowerOnSetting(struct btc_coexist *p_bt_coexist)
{
	struct btc_board_info *pBoardInfo = &p_bt_coexist->boardInfo;
	u8 u1Tmp = 0x0;
	u16 u2Tmp = 0x0;

	p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x67, 0x20);

	/*  enable BB, REG_SYS_FUNC_EN such that we can write 0x948 correctly. */
	u2Tmp = p_bt_coexist->fBtcRead2Byte(p_bt_coexist, 0x2);
	p_bt_coexist->fBtcWrite2Byte(p_bt_coexist, 0x2, u2Tmp | BIT0 | BIT1);

	/*  set GRAN_BT = 1 */
	p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x765, 0x18);
	/*  set WLAN_ACT = 0 */
	p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x76e, 0x4);

	/*  */
	/*  S0 or S1 setting and Local register setting(By the setting fw can get ant number, S0/S1, ... info) */
	/*  Local setting bit define */
	/* 	BIT0: "0" for no antenna inverse; "1" for antenna inverse */
	/* 	BIT1: "0" for internal switch; "1" for external switch */
	/* 	BIT2: "0" for one antenna; "1" for two antenna */
	/*  NOTE: here default all internal switch and 1-antenna ==> BIT1 = 0 and BIT2 = 0 */
	if (p_bt_coexist->chipInterface == BTC_INTF_USB) {
		/*  fixed at S0 for USB interface */
		p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x948, 0x0);

		u1Tmp |= 0x1;	/*  antenna inverse */
		p_bt_coexist->fBtcWriteLocalReg1Byte(p_bt_coexist, 0xfe08, u1Tmp);

		pBoardInfo->btdmAntPos = BTC_ANTENNA_AT_AUX_PORT;
	} else {
		/*  for PCIE and SDIO interface, we check efuse 0xc3[6] */
		if (pBoardInfo->singleAntPath == 0) {
			/*  set to S1 */
			p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x948, 0x280);
			pBoardInfo->btdmAntPos = BTC_ANTENNA_AT_MAIN_PORT;
		} else if (pBoardInfo->singleAntPath == 1) {
			/*  set to S0 */
			p_bt_coexist->fBtcWrite4Byte(p_bt_coexist, 0x948, 0x0);
			u1Tmp |= 0x1;	/*  antenna inverse */
			pBoardInfo->btdmAntPos = BTC_ANTENNA_AT_AUX_PORT;
		}

		if (p_bt_coexist->chipInterface == BTC_INTF_PCI)
			p_bt_coexist->fBtcWriteLocalReg1Byte(p_bt_coexist, 0x384, u1Tmp);
		else if (p_bt_coexist->chipInterface == BTC_INTF_SDIO)
			p_bt_coexist->fBtcWriteLocalReg1Byte(p_bt_coexist, 0x60, u1Tmp);
	}
}

void EXhalbtc8723b1ant_InitHwConfig(struct btc_coexist *p_bt_coexist, bool bWifiOnly)
{
	halbtc8723b1ant_InitHwConfig(p_bt_coexist, true, bWifiOnly);
}

void EXhalbtc8723b1ant_InitCoexDm(struct btc_coexist *p_bt_coexist)
{
	BTC_PRINT(
		BTC_MSG_INTERFACE,
		INTF_INIT,
		("[BTCoex], Coex Mechanism Init!!\n")
	);

	p_bt_coexist->bStopCoexDm = false;

	halbtc8723b1ant_InitCoexDm(p_bt_coexist);

	halbtc8723b1ant_QueryBtInfo(p_bt_coexist);
}

void EXhalbtc8723b1ant_DisplayCoexInfo(struct btc_coexist *p_bt_coexist)
{
	struct btc_board_info *pBoardInfo = &p_bt_coexist->boardInfo;
	struct btc_stack_info *pStackInfo = &p_bt_coexist->stackInfo;
	struct btc_bt_link_info *pBtLinkInfo = &p_bt_coexist->btLinkInfo;
	u8 *cliBuf = p_bt_coexist->cliBuf;
	u8 u1Tmp[4], i, btInfoExt, psTdmaCase = 0;
	u16 u2Tmp[4];
	u32 u4Tmp[4];
	bool bRoam = false;
	bool bScan = false;
	bool bLink = false;
	bool bWifiUnder5G = false;
	bool wifi_under_bmode = false;
	bool bBtHsOn = false;
	bool bWifiBusy = false;
	s32 wifiRssi = 0, btHsRssi = 0;
	u32 wifiBw, wifiTrafficDir, faOfdm, faCck, wifiLinkStatus;
	u8 wifiDot11Chnl, wifiHsChnl;
	u32 fw_ver = 0, bt_patch_ver = 0;
	static u8 PopReportIn10s;

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n ============[BT Coexist info]============"
	);
	CL_PRINTF(cliBuf);

	if (p_bt_coexist->bManualControl) {
		CL_SPRINTF(
			cliBuf,
			BT_TMP_BUF_SIZE,
			"\r\n ============[Under Manual Control]============"
		);
		CL_PRINTF(cliBuf);
		CL_SPRINTF(cliBuf,
			BT_TMP_BUF_SIZE,
			"\r\n =========================================="
		);
		CL_PRINTF(cliBuf);
	}
	if (p_bt_coexist->bStopCoexDm) {
		CL_SPRINTF(
			cliBuf,
			BT_TMP_BUF_SIZE,
			"\r\n ============[Coex is STOPPED]============"
		);
		CL_PRINTF(cliBuf);
		CL_SPRINTF(
			cliBuf,
			BT_TMP_BUF_SIZE,
			"\r\n =========================================="
		);
		CL_PRINTF(cliBuf);
	}

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d/ %d", "Ant PG Num/ Ant Mech/ Ant Pos:", \
		pBoardInfo->pgAntNum,
		pBoardInfo->btdmAntNum,
		pBoardInfo->btdmAntPos
	);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %s / %d", "BT stack/ hci ext ver", \
		((pStackInfo->bProfileNotified) ? "Yes" : "No"),
		pStackInfo->hciVersion
	);
	CL_PRINTF(cliBuf);

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_U4_BT_PATCH_VER, &bt_patch_ver);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d_%x/ 0x%x/ 0x%x(%d)", "CoexVer/ FwVer/ PatchVer", \
		gl_coex_ver_date_8723b_1ant,
		gl_coex_ver_8723b_1ant,
		fw_ver,
		bt_patch_ver,
		bt_patch_ver
	);
	CL_PRINTF(cliBuf);

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_U1_WIFI_DOT11_CHNL, &wifiDot11Chnl);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_U1_WIFI_HS_CHNL, &wifiHsChnl);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d / %d(%d)", "Dot11 channel / HsChnl(HsMode)", \
		wifiDot11Chnl,
		wifiHsChnl,
		bBtHsOn
	);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %02x %02x %02x ", "H2C Wifi inform bt chnl Info", \
		p_coex_dm->wifiChnlInfo[0],
		p_coex_dm->wifiChnlInfo[1],
		p_coex_dm->wifiChnlInfo[2]
	);
	CL_PRINTF(cliBuf);

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_S4_WIFI_RSSI, &wifiRssi);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_S4_HS_RSSI, &btHsRssi);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d", "Wifi rssi/ HS rssi", \
		wifiRssi - 100, btHsRssi - 100
	);
	CL_PRINTF(cliBuf);

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_SCAN, &bScan);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_LINK, &bLink);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_ROAM, &bRoam);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d/ %d/ %s", "Wifi bLink/ bRoam/ bScan/ bHi-Pri", \
		bLink, bRoam, bScan, ((p_coex_sta->bWiFiIsHighPriTask) ? "1" : "0")
	);
	CL_PRINTF(cliBuf);

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_UNDER_5G, &bWifiUnder5G);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_U4_WIFI_BW, &wifiBw);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_BUSY, &bWifiBusy);
	p_bt_coexist->fBtcGet(
		p_bt_coexist, BTC_GET_U4_WIFI_TRAFFIC_DIRECTION, &wifiTrafficDir
	);
	p_bt_coexist->fBtcGet(
		p_bt_coexist, BTC_GET_BL_WIFI_UNDER_B_MODE, &wifi_under_bmode
	);

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %s / %s/ %s/ AP =%d/ %s ", "Wifi status", \
		(bWifiUnder5G ? "5G" : "2.4G"),
		((wifi_under_bmode) ? "11b" : ((BTC_WIFI_BW_LEGACY == wifiBw) ? "11bg" : (((BTC_WIFI_BW_HT40 == wifiBw) ? "HT40" : "HT20")))),
		((!bWifiBusy) ? "idle" : ((BTC_WIFI_TRAFFIC_TX == wifiTrafficDir) ? "uplink" : "downlink")),
		p_coex_sta->nScanAPNum,
		(p_coex_sta->bCCKLock) ? "Lock" : "noLock"
	);
	CL_PRINTF(cliBuf);

	p_bt_coexist->fBtcGet(
		p_bt_coexist, BTC_GET_U4_WIFI_LINK_STATUS, &wifiLinkStatus
	);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d/ %d/ %d/ %d", "sta/vwifi/hs/p2pGo/p2pGc", \
		((wifiLinkStatus & WIFI_STA_CONNECTED) ? 1 : 0),
		((wifiLinkStatus & WIFI_AP_CONNECTED) ? 1 : 0),
		((wifiLinkStatus & WIFI_HS_CONNECTED) ? 1 : 0),
		((wifiLinkStatus & WIFI_P2P_GO_CONNECTED) ? 1 : 0),
		((wifiLinkStatus & WIFI_P2P_GC_CONNECTED) ? 1 : 0)
	);
	CL_PRINTF(cliBuf);


	PopReportIn10s++;
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = [%s/ %d/ %d/ %d] ", "BT [status/ rssi/ retryCnt/ popCnt]", \
		((p_bt_coexist->btInfo.bBtDisabled) ? ("disabled") : ((p_coex_sta->bC2hBtInquiryPage) ? ("inquiry/page scan") : ((BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE == p_coex_dm->btStatus) ? "non-connected idle" :
		((BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE == p_coex_dm->btStatus) ? "connected-idle" : "busy")))),
		p_coex_sta->btRssi, p_coex_sta->btRetryCnt, p_coex_sta->popEventCnt
	);
	CL_PRINTF(cliBuf);

	if (PopReportIn10s >= 5) {
		p_coex_sta->popEventCnt = 0;
		PopReportIn10s = 0;
	}


	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d / %d / %d / %d", "SCO/HID/PAN/A2DP", \
		pBtLinkInfo->bScoExist,
		pBtLinkInfo->bHidExist,
		pBtLinkInfo->bPanExist,
		pBtLinkInfo->bA2dpExist
	);
	CL_PRINTF(cliBuf);

	if (pStackInfo->bProfileNotified) {
		p_bt_coexist->fBtcDispDbgMsg(p_bt_coexist, BTC_DBG_DISP_BT_LINK_INFO);
	} else {
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %s", "BT Role", \
		(pBtLinkInfo->bSlaveRole) ? "Slave" : "Master");
		CL_PRINTF(cliBuf);
	}


	btInfoExt = p_coex_sta->btInfoExt;
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %s", "BT Info A2DP rate", \
		(btInfoExt & BIT0) ? "Basic rate" : "EDR rate"
	);
	CL_PRINTF(cliBuf);

	for (i = 0; i < BT_INFO_SRC_8723B_1ANT_MAX; i++) {
		if (p_coex_sta->btInfoC2hCnt[i]) {
			CL_SPRINTF(
				cliBuf,
				BT_TMP_BUF_SIZE,
				"\r\n %-35s = %02x %02x %02x %02x %02x %02x %02x(%d)", gl_bt_info_src_8723b_1ant[i], \
				p_coex_sta->btInfoC2h[i][0], p_coex_sta->btInfoC2h[i][1],
				p_coex_sta->btInfoC2h[i][2], p_coex_sta->btInfoC2h[i][3],
				p_coex_sta->btInfoC2h[i][4], p_coex_sta->btInfoC2h[i][5],
				p_coex_sta->btInfoC2h[i][6], p_coex_sta->btInfoC2hCnt[i]
			);
			CL_PRINTF(cliBuf);
		}
	}
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %s/%s, (0x%x/0x%x)", "PS state, IPS/LPS, (lps/rpwm)", \
		(p_coex_sta->bUnderIps ? "IPS ON" : "IPS OFF"),
		(p_coex_sta->bUnderLps ? "LPS ON" : "LPS OFF"),
		p_bt_coexist->btInfo.lpsVal,
		p_bt_coexist->btInfo.rpwmVal
	);
	CL_PRINTF(cliBuf);
	p_bt_coexist->fBtcDispDbgMsg(p_bt_coexist, BTC_DBG_DISP_FW_PWR_MODE_CMD);

	if (!p_bt_coexist->bManualControl) {
		/*  Sw mechanism */
		CL_SPRINTF(
			cliBuf,
			BT_TMP_BUF_SIZE,
			"\r\n %-35s", "============[Sw mechanism]============"
		);
		CL_PRINTF(cliBuf);

		CL_SPRINTF(
			cliBuf,
			BT_TMP_BUF_SIZE,
			"\r\n %-35s = %d", "SM[LowPenaltyRA]", \
			p_coex_dm->bCurLowPenaltyRa
		);
		CL_PRINTF(cliBuf);

		CL_SPRINTF(
			cliBuf,
			BT_TMP_BUF_SIZE,
			"\r\n %-35s = %s/ %s/ %d ", "DelBA/ BtCtrlAgg/ AggSize", \
			(p_bt_coexist->btInfo.bRejectAggPkt ? "Yes" : "No"),
			(p_bt_coexist->btInfo.bBtCtrlAggBufSize ? "Yes" : "No"),
			p_bt_coexist->btInfo.aggBufSize
		);
		CL_PRINTF(cliBuf);
		CL_SPRINTF(
			cliBuf,
			BT_TMP_BUF_SIZE,
			"\r\n %-35s = 0x%x ", "Rate Mask", \
			p_bt_coexist->btInfo.raMask
		);
		CL_PRINTF(cliBuf);

		/*  Fw mechanism */
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Fw mechanism]============");
		CL_PRINTF(cliBuf);

		psTdmaCase = p_coex_dm->curPsTdma;
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %02x %02x %02x %02x %02x case-%d (auto:%d)", "PS TDMA", \
			p_coex_dm->psTdmaPara[0], p_coex_dm->psTdmaPara[1],
			p_coex_dm->psTdmaPara[2], p_coex_dm->psTdmaPara[3],
			p_coex_dm->psTdmaPara[4], psTdmaCase, p_coex_dm->bAutoTdmaAdjust);
		CL_PRINTF(cliBuf);

		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "Coex Table Type", \
			p_coex_sta->nCoexTableType);
		CL_PRINTF(cliBuf);

		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = %d", "IgnWlanAct", \
			p_coex_dm->bCurIgnoreWlanAct);
		CL_PRINTF(cliBuf);

		/*
		CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x ", "Latest error condition(should be 0)", \
			p_coex_dm->errorCondition);
		CL_PRINTF(cliBuf);
		*/
	}

	/*  Hw setting */
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s", "============[Hw setting]============");
	CL_PRINTF(cliBuf);

	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/0x%x/0x%x/0x%x", "backup ARFR1/ARFR2/RL/AMaxTime", \
		p_coex_dm->backupArfrCnt1, p_coex_dm->backupArfrCnt2, p_coex_dm->backupRetryLimit, p_coex_dm->backupAmpduMaxTime);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x430);
	u4Tmp[1] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x434);
	u2Tmp[0] = p_bt_coexist->fBtcRead2Byte(p_bt_coexist, 0x42a);
	u1Tmp[0] = p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0x456);
	CL_SPRINTF(cliBuf, BT_TMP_BUF_SIZE, "\r\n %-35s = 0x%x/0x%x/0x%x/0x%x", "0x430/0x434/0x42a/0x456", \
		u4Tmp[0], u4Tmp[1], u2Tmp[0], u1Tmp[0]);
	CL_PRINTF(cliBuf);

	u1Tmp[0] = p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0x778);
	u4Tmp[0] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x6cc);
	u4Tmp[1] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x880);
	CL_SPRINTF(
		cliBuf, BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "0x778/0x6cc/0x880[29:25]", \
		u1Tmp[0], u4Tmp[0],  (u4Tmp[1] & 0x3e000000) >> 25
	);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x948);
	u1Tmp[0] = p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0x67);
	u4Tmp[1] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x764);
	u1Tmp[1] = p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0x76e);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x", "0x948/ 0x67[5] / 0x764 / 0x76e", \
		u4Tmp[0], ((u1Tmp[0] & 0x20) >> 5), (u4Tmp[1] & 0xffff), u1Tmp[1]
	);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x92c);
	u4Tmp[1] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x930);
	u4Tmp[2] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x944);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "0x92c[1:0]/ 0x930[7:0]/0x944[1:0]", \
		u4Tmp[0] & 0x3, u4Tmp[1] & 0xff, u4Tmp[2] & 0x3
	);
	CL_PRINTF(cliBuf);

	u1Tmp[0] = p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0x39);
	u1Tmp[1] = p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0x40);
	u4Tmp[0] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x4c);
	u1Tmp[2] = p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0x64);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x/ 0x%x/ 0x%x", "0x38[11]/0x40/0x4c[24:23]/0x64[0]", \
		((u1Tmp[0] & 0x8) >> 3),
		u1Tmp[1],
		((u4Tmp[0] & 0x01800000) >> 23),
		u1Tmp[2] & 0x1
	);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x550);
	u1Tmp[0] = p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0x522);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x", "0x550(bcn ctrl)/0x522", \
		u4Tmp[0], u1Tmp[0]
	);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0xc50);
	u1Tmp[0] = p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0x49c);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x", "0xc50(dig)/0x49c(null-drop)", \
		u4Tmp[0] & 0xff, u1Tmp[0]
	);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0xda0);
	u4Tmp[1] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0xda4);
	u4Tmp[2] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0xda8);
	u4Tmp[3] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0xcf0);

	u1Tmp[0] = p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0xa5b);
	u1Tmp[1] = p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0xa5c);

	faOfdm =
		((u4Tmp[0] & 0xffff0000) >> 16) +
		((u4Tmp[1] & 0xffff0000) >> 16) +
		(u4Tmp[1] & 0xffff) +  (u4Tmp[2] & 0xffff) + \
		((u4Tmp[3] & 0xffff0000) >> 16) + (u4Tmp[3] & 0xffff);
	faCck = (u1Tmp[0] << 8) + u1Tmp[1];

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "OFDM-CCA/OFDM-FA/CCK-FA", \
		u4Tmp[0] & 0xffff, faOfdm, faCck
	);
	CL_PRINTF(cliBuf);


	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d/ %d/ %d", "CRC_OK CCK/11g/11n/11n-Agg", \
		p_coex_sta->nCRCOK_CCK,
		p_coex_sta->nCRCOK_11g,
		p_coex_sta->nCRCOK_11n,
		p_coex_sta->nCRCOK_11nAgg
	);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d/ %d/ %d", "CRC_Err CCK/11g/11n/11n-Agg", \
		p_coex_sta->nCRCErr_CCK,
		p_coex_sta->nCRCErr_11g,
		p_coex_sta->nCRCErr_11n,
		p_coex_sta->nCRCErr_11nAgg
	);
	CL_PRINTF(cliBuf);

	u4Tmp[0] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x6c0);
	u4Tmp[1] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x6c4);
	u4Tmp[2] = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x6c8);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = 0x%x/ 0x%x/ 0x%x", "0x6c0/0x6c4/0x6c8(coexTable)", \
		u4Tmp[0], u4Tmp[1], u4Tmp[2]);
	CL_PRINTF(cliBuf);

	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d", "0x770(high-pri rx/tx)", \
		p_coex_sta->highPriorityRx, p_coex_sta->highPriorityTx
	);
	CL_PRINTF(cliBuf);
	CL_SPRINTF(
		cliBuf,
		BT_TMP_BUF_SIZE,
		"\r\n %-35s = %d/ %d", "0x774(low-pri rx/tx)", \
		p_coex_sta->lowPriorityRx, p_coex_sta->lowPriorityTx
	);
	CL_PRINTF(cliBuf);

	p_bt_coexist->fBtcDispDbgMsg(p_bt_coexist, BTC_DBG_DISP_COEX_STATISTICS);
}


void EXhalbtc8723b1ant_IpsNotify(struct btc_coexist *p_bt_coexist, u8 type)
{
	if (p_bt_coexist->bManualControl ||	p_bt_coexist->bStopCoexDm)
		return;

	if (BTC_IPS_ENTER == type) {
		BTC_PRINT(
			BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], IPS ENTER notify\n")
		);
		p_coex_sta->bUnderIps = true;

		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, false, 0);
		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 0);
		halbtc8723b1ant_SetAntPath(p_bt_coexist, BTC_ANT_PATH_BT, false, true);
	} else if (BTC_IPS_LEAVE == type) {
		BTC_PRINT(
			BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], IPS LEAVE notify\n")
		);
		p_coex_sta->bUnderIps = false;

		halbtc8723b1ant_InitHwConfig(p_bt_coexist, false, false);
		halbtc8723b1ant_InitCoexDm(p_bt_coexist);
		halbtc8723b1ant_QueryBtInfo(p_bt_coexist);
	}
}

void EXhalbtc8723b1ant_LpsNotify(struct btc_coexist *p_bt_coexist, u8 type)
{
	if (p_bt_coexist->bManualControl || p_bt_coexist->bStopCoexDm)
		return;

	if (BTC_LPS_ENABLE == type) {
		BTC_PRINT(
			BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], LPS ENABLE notify\n")
		);
		p_coex_sta->bUnderLps = true;
	} else if (BTC_LPS_DISABLE == type) {
		BTC_PRINT(
			BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], LPS DISABLE notify\n")
		);
		p_coex_sta->bUnderLps = false;
	}
}

void EXhalbtc8723b1ant_ScanNotify(struct btc_coexist *p_bt_coexist, u8 type)
{
	bool bWifiConnected = false, bBtHsOn = false;
	u32 wifiLinkStatus = 0;
	u32 numOfWifiLink = 0;
	bool bBtCtrlAggBufSize = false;
	u8 aggBufSize = 5;

	u8 u1Tmpa, u1Tmpb;
	u32 u4Tmp;

	if (p_bt_coexist->bManualControl || p_bt_coexist->bStopCoexDm)
		return;

	if (BTC_SCAN_START == type) {
		p_coex_sta->bWiFiIsHighPriTask = true;
		BTC_PRINT(
			BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], SCAN START notify\n")
		);

		halbtc8723b1ant_PsTdma(p_bt_coexist, FORCE_EXEC, false, 8);  /* Force antenna setup for no scan result issue */
		u4Tmp = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x948);
		u1Tmpa = p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0x765);
		u1Tmpb = p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0x67);


		BTC_PRINT(
			BTC_MSG_INTERFACE,
			INTF_NOTIFY,
			(
				"[BTCoex], 0x948 = 0x%x, 0x765 = 0x%x, 0x67 = 0x%x\n",
				u4Tmp,
				u1Tmpa,
				u1Tmpb
			)
		);
	} else {
		p_coex_sta->bWiFiIsHighPriTask = false;
		BTC_PRINT(
			BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], SCAN FINISH notify\n")
		);

		p_bt_coexist->fBtcGet(
			p_bt_coexist, BTC_GET_U1_AP_NUM, &p_coex_sta->nScanAPNum
		);
	}

	if (p_bt_coexist->btInfo.bBtDisabled)
		return;

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);

	halbtc8723b1ant_QueryBtInfo(p_bt_coexist);

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_U4_WIFI_LINK_STATUS, &wifiLinkStatus);
	numOfWifiLink = wifiLinkStatus >> 16;

	if (numOfWifiLink >= 2) {
		halbtc8723b1ant_LimitedTx(p_bt_coexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8723b1ant_LimitedRx(
			p_bt_coexist, NORMAL_EXEC, false, bBtCtrlAggBufSize, aggBufSize
		);
		halbtc8723b1ant_ActionWifiMultiPort(p_bt_coexist);
		return;
	}

	if (p_coex_sta->bC2hBtInquiryPage) {
		halbtc8723b1ant_ActionBtInquiry(p_bt_coexist);
		return;
	} else if (bBtHsOn) {
		halbtc8723b1ant_ActionHs(p_bt_coexist);
		return;
	}

	if (BTC_SCAN_START == type) {
		/* BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], SCAN START notify\n")); */
		if (!bWifiConnected)	/*  non-connected scan */
			halbtc8723b1ant_ActionWifiNotConnectedScan(p_bt_coexist);
		else	/*  wifi is connected */
			halbtc8723b1ant_ActionWifiConnectedScan(p_bt_coexist);
	} else if (BTC_SCAN_FINISH == type) {
		/* BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], SCAN FINISH notify\n")); */
		if (!bWifiConnected)	/*  non-connected scan */
			halbtc8723b1ant_ActionWifiNotConnected(p_bt_coexist);
		else
			halbtc8723b1ant_ActionWifiConnected(p_bt_coexist);
	}
}

void EXhalbtc8723b1ant_ConnectNotify(struct btc_coexist *p_bt_coexist, u8 type)
{
	bool bWifiConnected = false, bBtHsOn = false;
	u32 wifiLinkStatus = 0;
	u32 numOfWifiLink = 0;
	bool bBtCtrlAggBufSize = false;
	u8 aggBufSize = 5;

	if (
		p_bt_coexist->bManualControl ||
		p_bt_coexist->bStopCoexDm ||
		p_bt_coexist->btInfo.bBtDisabled
	)
		return;

	if (BTC_ASSOCIATE_START == type) {
		p_coex_sta->bWiFiIsHighPriTask = true;
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], CONNECT START notify\n"));
		 p_coex_dm->nArpCnt = 0;
	} else {
		p_coex_sta->bWiFiIsHighPriTask = false;
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], CONNECT FINISH notify\n"));
		/* p_coex_dm->nArpCnt = 0; */
	}

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_U4_WIFI_LINK_STATUS, &wifiLinkStatus);
	numOfWifiLink = wifiLinkStatus >> 16;
	if (numOfWifiLink >= 2) {
		halbtc8723b1ant_LimitedTx(p_bt_coexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8723b1ant_LimitedRx(p_bt_coexist, NORMAL_EXEC, false, bBtCtrlAggBufSize, aggBufSize);
		halbtc8723b1ant_ActionWifiMultiPort(p_bt_coexist);
		return;
	}

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	if (p_coex_sta->bC2hBtInquiryPage) {
		halbtc8723b1ant_ActionBtInquiry(p_bt_coexist);
		return;
	} else if (bBtHsOn) {
		halbtc8723b1ant_ActionHs(p_bt_coexist);
		return;
	}

	if (BTC_ASSOCIATE_START == type) {
		/* BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], CONNECT START notify\n")); */
		halbtc8723b1ant_ActionWifiNotConnectedAssoAuth(p_bt_coexist);
	} else if (BTC_ASSOCIATE_FINISH == type) {
		/* BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], CONNECT FINISH notify\n")); */

		p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
		if (!bWifiConnected) /*  non-connected scan */
			halbtc8723b1ant_ActionWifiNotConnected(p_bt_coexist);
		else
			halbtc8723b1ant_ActionWifiConnected(p_bt_coexist);
	}
}

void EXhalbtc8723b1ant_MediaStatusNotify(struct btc_coexist *p_bt_coexist, u8 type)
{
	u8 h2c_parameter[3] = {0};
	u32 wifiBw;
	u8 wifiCentralChnl;
	bool wifi_under_bmode = false;

	if (
		p_bt_coexist->bManualControl ||
		p_bt_coexist->bStopCoexDm ||
		p_bt_coexist->btInfo.bBtDisabled
	)
		return;

	if (BTC_MEDIA_CONNECT == type) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], MEDIA connect notify\n"));

		p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_UNDER_B_MODE, &wifi_under_bmode);

		/* Set CCK Tx/Rx high Pri except 11b mode */
		if (wifi_under_bmode) {
			p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x6cd, 0x00); /* CCK Tx */
			p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x6cf, 0x00); /* CCK Rx */
		} else {
			p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x6cd, 0x10); /* CCK Tx */
			p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x6cf, 0x10); /* CCK Rx */
		}

		p_coex_dm->backupArfrCnt1 = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x430);
		p_coex_dm->backupArfrCnt2 = p_bt_coexist->fBtcRead4Byte(p_bt_coexist, 0x434);
		p_coex_dm->backupRetryLimit = p_bt_coexist->fBtcRead2Byte(p_bt_coexist, 0x42a);
		p_coex_dm->backupAmpduMaxTime = p_bt_coexist->fBtcRead1Byte(p_bt_coexist, 0x456);
	} else {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], MEDIA disconnect notify\n"));
		p_coex_dm->nArpCnt = 0;

		p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x6cd, 0x0); /* CCK Tx */
		p_bt_coexist->fBtcWrite1Byte(p_bt_coexist, 0x6cf, 0x0); /* CCK Rx */
	}

	/*  only 2.4G we need to inform bt the chnl mask */
	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_U1_WIFI_CENTRAL_CHNL, &wifiCentralChnl);
	if ((BTC_MEDIA_CONNECT == type) && (wifiCentralChnl <= 14)) {
		/* h2c_parameter[0] = 0x1; */
		h2c_parameter[0] = 0x0;
		h2c_parameter[1] = wifiCentralChnl;
		p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_U4_WIFI_BW, &wifiBw);

		if (BTC_WIFI_BW_HT40 == wifiBw)
			h2c_parameter[2] = 0x30;
		else
			h2c_parameter[2] = 0x20;
	}

	p_coex_dm->wifiChnlInfo[0] = h2c_parameter[0];
	p_coex_dm->wifiChnlInfo[1] = h2c_parameter[1];
	p_coex_dm->wifiChnlInfo[2] = h2c_parameter[2];

	BTC_PRINT(
		BTC_MSG_ALGORITHM,
		ALGO_TRACE_FW_EXEC,
		(
			"[BTCoex], FW write 0x66 = 0x%x\n",
			h2c_parameter[0] << 16 | h2c_parameter[1] << 8 | h2c_parameter[2]
		)
	);

	p_bt_coexist->fBtcFillH2c(p_bt_coexist, 0x66, 3, h2c_parameter);
}

void EXhalbtc8723b1ant_SpecialPacketNotify(struct btc_coexist *p_bt_coexist, u8 type)
{
	bool bBtHsOn = false;
	u32 wifiLinkStatus = 0;
	u32 numOfWifiLink = 0;
	bool bBtCtrlAggBufSize = false;
	u8 aggBufSize = 5;

	if (
		p_bt_coexist->bManualControl ||
		p_bt_coexist->bStopCoexDm ||
		p_bt_coexist->btInfo.bBtDisabled
	)
		return;

	if (
		BTC_PACKET_DHCP == type ||
		BTC_PACKET_EAPOL == type ||
		BTC_PACKET_ARP == type
	) {
		if (BTC_PACKET_ARP == type) {
			BTC_PRINT(
				BTC_MSG_INTERFACE,
				INTF_NOTIFY,
				("[BTCoex], special Packet ARP notify\n")
			);

			p_coex_dm->nArpCnt++;
			BTC_PRINT(
				BTC_MSG_INTERFACE,
				INTF_NOTIFY,
				("[BTCoex], ARP Packet Count = %d\n", p_coex_dm->nArpCnt)
			);

			if (p_coex_dm->nArpCnt >= 10) /*  if APR PKT > 10 after connect, do not go to ActionWifiConnectedSpecialPacket(p_bt_coexist) */
				p_coex_sta->bWiFiIsHighPriTask = false;
			else
				p_coex_sta->bWiFiIsHighPriTask = true;
		} else {
			p_coex_sta->bWiFiIsHighPriTask = true;
			BTC_PRINT(
				BTC_MSG_INTERFACE,
				INTF_NOTIFY,
				("[BTCoex], special Packet DHCP or EAPOL notify\n")
			);
		}
	} else {
		p_coex_sta->bWiFiIsHighPriTask = false;
		BTC_PRINT(
			BTC_MSG_INTERFACE,
			INTF_NOTIFY,
			("[BTCoex], special Packet [Type = %d] notify\n", type)
		);
	}

	p_coex_sta->specialPktPeriodCnt = 0;

	p_bt_coexist->fBtcGet(
		p_bt_coexist, BTC_GET_U4_WIFI_LINK_STATUS, &wifiLinkStatus
	);
	numOfWifiLink = wifiLinkStatus >> 16;

	if (numOfWifiLink >= 2) {
		halbtc8723b1ant_LimitedTx(p_bt_coexist, NORMAL_EXEC, 0, 0, 0, 0);
		halbtc8723b1ant_LimitedRx(
			p_bt_coexist, NORMAL_EXEC, false, bBtCtrlAggBufSize, aggBufSize
		);
		halbtc8723b1ant_ActionWifiMultiPort(p_bt_coexist);
		return;
	}

	p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_HS_OPERATION, &bBtHsOn);
	if (p_coex_sta->bC2hBtInquiryPage) {
		halbtc8723b1ant_ActionBtInquiry(p_bt_coexist);
		return;
	} else if (bBtHsOn) {
		halbtc8723b1ant_ActionHs(p_bt_coexist);
		return;
	}

	if (
		BTC_PACKET_DHCP == type ||
		BTC_PACKET_EAPOL == type ||
		((BTC_PACKET_ARP == type) && (p_coex_sta->bWiFiIsHighPriTask))
	)
		halbtc8723b1ant_ActionWifiConnectedSpecialPacket(p_bt_coexist);
}

void EXhalbtc8723b1ant_BtInfoNotify(
	struct btc_coexist *p_bt_coexist, u8 *tmpBuf, u8 length
)
{
	u8 btInfo = 0;
	u8 i, rspSource = 0;
	bool bWifiConnected = false;
	bool bBtBusy = false;

	p_coex_sta->bC2hBtInfoReqSent = false;

	rspSource = tmpBuf[0] & 0xf;
	if (rspSource >= BT_INFO_SRC_8723B_1ANT_MAX)
		rspSource = BT_INFO_SRC_8723B_1ANT_WIFI_FW;
	p_coex_sta->btInfoC2hCnt[rspSource]++;

	BTC_PRINT(
		BTC_MSG_INTERFACE,
		INTF_NOTIFY,
		("[BTCoex], Bt info[%d], length =%d, hex data =[",
		rspSource,
		length)
	);
	for (i = 0; i < length; i++) {
		p_coex_sta->btInfoC2h[rspSource][i] = tmpBuf[i];
		if (i == 1)
			btInfo = tmpBuf[i];
		if (i == length - 1)
			BTC_PRINT(
				BTC_MSG_INTERFACE, INTF_NOTIFY, ("0x%02x]\n", tmpBuf[i])
			);
		else
			BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("0x%02x, ", tmpBuf[i]));
	}

	if (BT_INFO_SRC_8723B_1ANT_WIFI_FW != rspSource) {
		p_coex_sta->btRetryCnt = p_coex_sta->btInfoC2h[rspSource][2] & 0xf;

		if (p_coex_sta->btRetryCnt >= 1)
			p_coex_sta->popEventCnt++;

		if (p_coex_sta->btInfoC2h[rspSource][2] & 0x20)
			p_coex_sta->bC2hBtPage = true;
		else
			p_coex_sta->bC2hBtPage = false;

		p_coex_sta->btRssi = p_coex_sta->btInfoC2h[rspSource][3] * 2 - 90;
		/* p_coex_sta->btInfoC2h[rspSource][3]*2+10; */

		p_coex_sta->btInfoExt = p_coex_sta->btInfoC2h[rspSource][4];

		p_coex_sta->bBtTxRxMask = (p_coex_sta->btInfoC2h[rspSource][2] & 0x40);
		p_bt_coexist->fBtcSet(p_bt_coexist, BTC_SET_BL_BT_TX_RX_MASK, &p_coex_sta->bBtTxRxMask);

		if (!p_coex_sta->bBtTxRxMask) {
			/* BT into is responded by BT FW and BT RF REG 0x3C != 0x15 => Need to switch BT TRx Mask */
			BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], Switch BT TRx Mask since BT RF REG 0x3C != 0x15\n"));
			p_bt_coexist->fBtcSetBtReg(p_bt_coexist, BTC_BT_REG_RF, 0x3c, 0x15);
		}

		/*  Here we need to resend some wifi info to BT */
		/*  because bt is reset and loss of the info. */
		if (p_coex_sta->btInfoExt & BIT1) {
			BTC_PRINT(
				BTC_MSG_ALGORITHM,
				ALGO_TRACE,
				("[BTCoex], BT ext info bit1 check, send wifi BW&Chnl to BT!!\n")
			);
			p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_BL_WIFI_CONNECTED, &bWifiConnected);
			if (bWifiConnected)
				EXhalbtc8723b1ant_MediaStatusNotify(p_bt_coexist, BTC_MEDIA_CONNECT);
			else
				EXhalbtc8723b1ant_MediaStatusNotify(p_bt_coexist, BTC_MEDIA_DISCONNECT);
		}

		if (p_coex_sta->btInfoExt & BIT3) {
			if (!p_bt_coexist->bManualControl && !p_bt_coexist->bStopCoexDm) {
				BTC_PRINT(
					BTC_MSG_ALGORITHM,
					ALGO_TRACE,
					("[BTCoex], BT ext info bit3 check, set BT NOT to ignore Wlan active!!\n")
				);
				halbtc8723b1ant_IgnoreWlanAct(p_bt_coexist, FORCE_EXEC, false);
			}
		} else {
			/*  BT already NOT ignore Wlan active, do nothing here. */
		}
	}

	/*  check BIT2 first ==> check if bt is under inquiry or page scan */
	if (btInfo & BT_INFO_8723B_1ANT_B_INQ_PAGE)
		p_coex_sta->bC2hBtInquiryPage = true;
	else
		p_coex_sta->bC2hBtInquiryPage = false;

	/*  set link exist status */
	if (!(btInfo & BT_INFO_8723B_1ANT_B_CONNECTION)) {
		p_coex_sta->bBtLinkExist = false;
		p_coex_sta->bPanExist = false;
		p_coex_sta->bA2dpExist = false;
		p_coex_sta->bHidExist = false;
		p_coex_sta->bScoExist = false;
	} else {	/*  connection exists */
		p_coex_sta->bBtLinkExist = true;
		if (btInfo & BT_INFO_8723B_1ANT_B_FTP)
			p_coex_sta->bPanExist = true;
		else
			p_coex_sta->bPanExist = false;

		if (btInfo & BT_INFO_8723B_1ANT_B_A2DP)
			p_coex_sta->bA2dpExist = true;
		else
			p_coex_sta->bA2dpExist = false;

		if (btInfo & BT_INFO_8723B_1ANT_B_HID)
			p_coex_sta->bHidExist = true;
		else
			p_coex_sta->bHidExist = false;

		if (btInfo & BT_INFO_8723B_1ANT_B_SCO_ESCO)
			p_coex_sta->bScoExist = true;
		else
			p_coex_sta->bScoExist = false;
	}

	halbtc8723b1ant_UpdateBtLinkInfo(p_bt_coexist);

	btInfo = btInfo & 0x1f;  /* mask profile bit for connect-ilde identification (for CSR case: A2DP idle --> 0x41) */

	if (!(btInfo & BT_INFO_8723B_1ANT_B_CONNECTION)) {
		p_coex_dm->btStatus = BT_8723B_1ANT_BT_STATUS_NON_CONNECTED_IDLE;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT Non-Connected idle!!!\n"));
	} else if (btInfo == BT_INFO_8723B_1ANT_B_CONNECTION)	{
		/*  connection exists but no busy */
		p_coex_dm->btStatus = BT_8723B_1ANT_BT_STATUS_CONNECTED_IDLE;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT Connected-idle!!!\n"));
	} else if (
		(btInfo & BT_INFO_8723B_1ANT_B_SCO_ESCO) ||
		(btInfo & BT_INFO_8723B_1ANT_B_SCO_BUSY)
	) {
		p_coex_dm->btStatus = BT_8723B_1ANT_BT_STATUS_SCO_BUSY;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT SCO busy!!!\n"));
	} else if (btInfo & BT_INFO_8723B_1ANT_B_ACL_BUSY) {
		if (BT_8723B_1ANT_BT_STATUS_ACL_BUSY != p_coex_dm->btStatus)
			p_coex_dm->bAutoTdmaAdjust = false;

		p_coex_dm->btStatus = BT_8723B_1ANT_BT_STATUS_ACL_BUSY;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT ACL busy!!!\n"));
	} else {
		p_coex_dm->btStatus = BT_8723B_1ANT_BT_STATUS_MAX;
		BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], BtInfoNotify(), BT Non-Defined state!!!\n"));
	}

	if (
		(BT_8723B_1ANT_BT_STATUS_ACL_BUSY == p_coex_dm->btStatus) ||
		(BT_8723B_1ANT_BT_STATUS_SCO_BUSY == p_coex_dm->btStatus) ||
		(BT_8723B_1ANT_BT_STATUS_ACL_SCO_BUSY == p_coex_dm->btStatus)
	)
		bBtBusy = true;
	else
		bBtBusy = false;
	p_bt_coexist->fBtcSet(p_bt_coexist, BTC_SET_BL_BT_TRAFFIC_BUSY, &bBtBusy);

	halbtc8723b1ant_RunCoexistMechanism(p_bt_coexist);
}

void EXhalbtc8723b1ant_HaltNotify(struct btc_coexist *p_bt_coexist)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Halt notify\n"));

	halbtc8723b1ant_PowerSaveState(p_bt_coexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
	halbtc8723b1ant_PsTdma(p_bt_coexist, FORCE_EXEC, false, 0);
	halbtc8723b1ant_SetAntPath(p_bt_coexist, BTC_ANT_PATH_BT, false, true);

	halbtc8723b1ant_IgnoreWlanAct(p_bt_coexist, FORCE_EXEC, true);

	EXhalbtc8723b1ant_MediaStatusNotify(p_bt_coexist, BTC_MEDIA_DISCONNECT);

	p_bt_coexist->bStopCoexDm = true;
}

void EXhalbtc8723b1ant_PnpNotify(struct btc_coexist *p_bt_coexist, u8 pnpState)
{
	BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Pnp notify\n"));

	if (BTC_WIFI_PNP_SLEEP == pnpState) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Pnp notify to SLEEP\n"));

		halbtc8723b1ant_PowerSaveState(p_bt_coexist, BTC_PS_WIFI_NATIVE, 0x0, 0x0);
		halbtc8723b1ant_PsTdma(p_bt_coexist, NORMAL_EXEC, false, 0);
		halbtc8723b1ant_CoexTableWithType(p_bt_coexist, NORMAL_EXEC, 2);
		halbtc8723b1ant_SetAntPath(p_bt_coexist, BTC_ANT_PATH_BT, false, true);

		p_bt_coexist->bStopCoexDm = true;
	} else if (BTC_WIFI_PNP_WAKE_UP == pnpState) {
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_NOTIFY, ("[BTCoex], Pnp notify to WAKE UP\n"));
		p_bt_coexist->bStopCoexDm = false;
		halbtc8723b1ant_InitHwConfig(p_bt_coexist, false, false);
		halbtc8723b1ant_InitCoexDm(p_bt_coexist);
		halbtc8723b1ant_QueryBtInfo(p_bt_coexist);
	}
}

void EXhalbtc8723b1ant_Periodical(struct btc_coexist *p_bt_coexist)
{
	static u8 dis_ver_info_cnt;
	u32 fw_ver = 0, bt_patch_ver = 0;

	BTC_PRINT(BTC_MSG_ALGORITHM, ALGO_TRACE, ("[BTCoex], ==========================Periodical ===========================\n"));

	if (dis_ver_info_cnt <= 5) {
		dis_ver_info_cnt += 1;
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], ****************************************************************\n"));
		p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_U4_BT_PATCH_VER, &bt_patch_ver);
		p_bt_coexist->fBtcGet(p_bt_coexist, BTC_GET_U4_WIFI_FW_VER, &fw_ver);
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], CoexVer/ FwVer/ PatchVer = %d_%x/ 0x%x/ 0x%x(%d)\n", \
			gl_coex_ver_date_8723b_1ant, gl_coex_ver_8723b_1ant, fw_ver, bt_patch_ver, bt_patch_ver));
		BTC_PRINT(BTC_MSG_INTERFACE, INTF_INIT, ("[BTCoex], ****************************************************************\n"));
	}

	halbtc8723b1ant_MonitorBtCtr(p_bt_coexist);
	halbtc8723b1ant_MonitorWiFiCtr(p_bt_coexist);

	if (
		halbtc8723b1ant_IsWifiStatusChanged(p_bt_coexist) ||
		p_coex_dm->bAutoTdmaAdjust
	)
		halbtc8723b1ant_RunCoexistMechanism(p_bt_coexist);

	p_coex_sta->specialPktPeriodCnt++;
}
