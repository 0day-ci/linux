// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright (c) 2021 Realtek Semiconductor Corp. All rights reserved.
 */

#include <linux/netdevice.h>
#include <linux/usb.h>
#include <linux/mii.h>
#include <linux/ethtool.h>
#include <linux/firmware.h>
#include <linux/usb/r8152.h>
#include <crypto/hash.h>
#include "r8152_basic.h"

/**
 * struct fw_block - block type and total length
 * @type: type of the current block, such as RTL_FW_END, RTL_FW_PLA,
 *	RTL_FW_USB and so on.
 * @length: total length of the current block.
 */
struct fw_block {
	__le32 type;
	__le32 length;
} __packed;

/**
 * struct fw_header - header of the firmware file
 * @checksum: checksum of sha256 which is calculated from the whole file
 *	except the checksum field of the file. That is, calculate sha256
 *	from the version field to the end of the file.
 * @version: version of this firmware.
 * @blocks: the first firmware block of the file
 */
struct fw_header {
	u8 checksum[32];
	char version[RTL_VER_SIZE];
	struct fw_block blocks[];
} __packed;

enum rtl8152_fw_flags {
	FW_FLAGS_USB = 0,
	FW_FLAGS_PLA,
	FW_FLAGS_START,
	FW_FLAGS_STOP,
	FW_FLAGS_NC,
	FW_FLAGS_NC1,
	FW_FLAGS_NC2,
	FW_FLAGS_UC2,
	FW_FLAGS_UC,
	FW_FLAGS_SPEED_UP,
	FW_FLAGS_VER,
};

enum rtl8152_fw_fixup_cmd {
	FW_FIXUP_AND = 0,
	FW_FIXUP_OR,
	FW_FIXUP_NOT,
	FW_FIXUP_XOR,
};

struct fw_phy_set {
	__le16 addr;
	__le16 data;
} __packed;

struct fw_phy_speed_up {
	struct fw_block blk_hdr;
	__le16 fw_offset;
	__le16 version;
	__le16 fw_reg;
	__le16 reserved;
	char info[];
} __packed;

struct fw_phy_ver {
	struct fw_block blk_hdr;
	struct fw_phy_set ver;
	__le32 reserved;
} __packed;

struct fw_phy_fixup {
	struct fw_block blk_hdr;
	struct fw_phy_set setting;
	__le16 bit_cmd;
	__le16 reserved;
} __packed;

struct fw_phy_union {
	struct fw_block blk_hdr;
	__le16 fw_offset;
	__le16 fw_reg;
	struct fw_phy_set pre_set[2];
	struct fw_phy_set bp[8];
	struct fw_phy_set bp_en;
	u8 pre_num;
	u8 bp_num;
	char info[];
} __packed;

/**
 * struct fw_mac - a firmware block used by RTL_FW_PLA and RTL_FW_USB.
 *	The layout of the firmware block is:
 *	<struct fw_mac> + <info> + <firmware data>.
 * @blk_hdr: firmware descriptor (type, length)
 * @fw_offset: offset of the firmware binary data. The start address of
 *	the data would be the address of struct fw_mac + @fw_offset.
 * @fw_reg: the register to load the firmware. Depends on chip.
 * @bp_ba_addr: the register to write break point base address. Depends on
 *	chip.
 * @bp_ba_value: break point base address. Depends on chip.
 * @bp_en_addr: the register to write break point enabled mask. Depends
 *	on chip.
 * @bp_en_value: break point enabled mask. Depends on the firmware.
 * @bp_start: the start register of break points. Depends on chip.
 * @bp_num: the break point number which needs to be set for this firmware.
 *	Depends on the firmware.
 * @bp: break points. Depends on firmware.
 * @reserved: reserved space (unused)
 * @fw_ver_reg: the register to store the fw version.
 * @fw_ver_data: the firmware version of the current type.
 * @info: additional information for debugging, and is followed by the
 *	binary data of firmware.
 */
struct fw_mac {
	struct fw_block blk_hdr;
	__le16 fw_offset;
	__le16 fw_reg;
	__le16 bp_ba_addr;
	__le16 bp_ba_value;
	__le16 bp_en_addr;
	__le16 bp_en_value;
	__le16 bp_start;
	__le16 bp_num;
	__le16 bp[16]; /* any value determined by firmware */
	__le32 reserved;
	__le16 fw_ver_reg;
	u8 fw_ver_data;
	char info[];
} __packed;

/**
 * struct fw_phy_patch_key - a firmware block used by RTL_FW_PHY_START.
 *	This is used to set patch key when loading the firmware of PHY.
 * @blk_hdr: firmware descriptor (type, length)
 * @key_reg: the register to write the patch key.
 * @key_data: patch key.
 * @reserved: reserved space (unused)
 */
struct fw_phy_patch_key {
	struct fw_block blk_hdr;
	__le16 key_reg;
	__le16 key_data;
	__le32 reserved;
} __packed;

/**
 * struct fw_phy_nc - a firmware block used by RTL_FW_PHY_NC.
 *	The layout of the firmware block is:
 *	<struct fw_phy_nc> + <info> + <firmware data>.
 * @blk_hdr: firmware descriptor (type, length)
 * @fw_offset: offset of the firmware binary data. The start address of
 *	the data would be the address of struct fw_phy_nc + @fw_offset.
 * @fw_reg: the register to load the firmware. Depends on chip.
 * @ba_reg: the register to write the base address. Depends on chip.
 * @ba_data: base address. Depends on chip.
 * @patch_en_addr: the register of enabling patch mode. Depends on chip.
 * @patch_en_value: patch mode enabled mask. Depends on the firmware.
 * @mode_reg: the regitster of switching the mode.
 * @mode_pre: the mode needing to be set before loading the firmware.
 * @mode_post: the mode to be set when finishing to load the firmware.
 * @reserved: reserved space (unused)
 * @bp_start: the start register of break points. Depends on chip.
 * @bp_num: the break point number which needs to be set for this firmware.
 *	Depends on the firmware.
 * @bp: break points. Depends on firmware.
 * @info: additional information for debugging, and is followed by the
 *	binary data of firmware.
 */
struct fw_phy_nc {
	struct fw_block blk_hdr;
	__le16 fw_offset;
	__le16 fw_reg;
	__le16 ba_reg;
	__le16 ba_data;
	__le16 patch_en_addr;
	__le16 patch_en_value;
	__le16 mode_reg;
	__le16 mode_pre;
	__le16 mode_post;
	__le16 reserved;
	__le16 bp_start;
	__le16 bp_num;
	__le16 bp[4];
	char info[];
} __packed;

enum rtl_fw_type {
	RTL_FW_END = 0,
	RTL_FW_PLA,
	RTL_FW_USB,
	RTL_FW_PHY_START,
	RTL_FW_PHY_STOP,
	RTL_FW_PHY_NC,
	RTL_FW_PHY_FIXUP,
	RTL_FW_PHY_UNION_NC,
	RTL_FW_PHY_UNION_NC1,
	RTL_FW_PHY_UNION_NC2,
	RTL_FW_PHY_UNION_UC2,
	RTL_FW_PHY_UNION_UC,
	RTL_FW_PHY_UNION_MISC,
	RTL_FW_PHY_SPEED_UP,
	RTL_FW_PHY_VER,
};

static void rtl_patch_key_set(struct r8152 *tp, u16 key_addr, u16 patch_key)
{
	if (patch_key && key_addr) {
		sram_write(tp, key_addr, patch_key);
		sram_write(tp, SRAM_PHY_LOCK, PHY_PATCH_LOCK);
	} else if (key_addr) {
		u16 data;

		sram_write(tp, 0x0000, 0x0000);

		data = ocp_reg_read(tp, OCP_PHY_LOCK);
		data &= ~PATCH_LOCK;
		ocp_reg_write(tp, OCP_PHY_LOCK, data);

		sram_write(tp, key_addr, 0x0000);
	} else {
		WARN_ON_ONCE(1);
	}
}

static int rtl_pre_ram_code(struct r8152 *tp, u16 key_addr, u16 patch_key, bool wait)
{
	if (rtl_phy_patch_request(tp, true, wait))
		return -ETIME;

	rtl_patch_key_set(tp, key_addr, patch_key);

	return 0;
}

static int rtl_post_ram_code(struct r8152 *tp, u16 key_addr, bool wait)
{
	rtl_patch_key_set(tp, key_addr, 0);

	rtl_phy_patch_request(tp, false, wait);

	ocp_write_word(tp, MCU_TYPE_PLA, PLA_OCP_GPHY_BASE, tp->ocp_base);

	return 0;
}

/* Clear the bp to stop the firmware before loading a new one */
static void rtl_clear_bp(struct r8152 *tp, u16 type)
{
	switch (tp->version) {
	case RTL_VER_01:
	case RTL_VER_02:
	case RTL_VER_07:
		break;
	case RTL_VER_03:
	case RTL_VER_04:
	case RTL_VER_05:
	case RTL_VER_06:
		ocp_write_byte(tp, type, PLA_BP_EN, 0);
		break;
	case RTL_VER_08:
	case RTL_VER_09:
	case RTL_VER_10:
	case RTL_VER_11:
	case RTL_VER_12:
	case RTL_VER_13:
	case RTL_VER_14:
	case RTL_VER_15:
	default:
		if (type == MCU_TYPE_USB) {
			ocp_write_byte(tp, MCU_TYPE_USB, USB_BP2_EN, 0);

			ocp_write_word(tp, MCU_TYPE_USB, USB_BP_8, 0);
			ocp_write_word(tp, MCU_TYPE_USB, USB_BP_9, 0);
			ocp_write_word(tp, MCU_TYPE_USB, USB_BP_10, 0);
			ocp_write_word(tp, MCU_TYPE_USB, USB_BP_11, 0);
			ocp_write_word(tp, MCU_TYPE_USB, USB_BP_12, 0);
			ocp_write_word(tp, MCU_TYPE_USB, USB_BP_13, 0);
			ocp_write_word(tp, MCU_TYPE_USB, USB_BP_14, 0);
			ocp_write_word(tp, MCU_TYPE_USB, USB_BP_15, 0);
		} else {
			ocp_write_byte(tp, MCU_TYPE_PLA, PLA_BP_EN, 0);
		}
		break;
	}

	ocp_write_word(tp, type, PLA_BP_0, 0);
	ocp_write_word(tp, type, PLA_BP_1, 0);
	ocp_write_word(tp, type, PLA_BP_2, 0);
	ocp_write_word(tp, type, PLA_BP_3, 0);
	ocp_write_word(tp, type, PLA_BP_4, 0);
	ocp_write_word(tp, type, PLA_BP_5, 0);
	ocp_write_word(tp, type, PLA_BP_6, 0);
	ocp_write_word(tp, type, PLA_BP_7, 0);

	/* wait 3 ms to make sure the firmware is stopped */
	usleep_range(3000, 6000);
	ocp_write_word(tp, type, PLA_BP_BA, 0);
}

static bool rtl8152_is_fw_phy_speed_up_ok(struct r8152 *tp, struct fw_phy_speed_up *phy)
{
	u16 fw_offset;
	u32 length;
	bool rc = false;

	switch (tp->version) {
	case RTL_VER_01:
	case RTL_VER_02:
	case RTL_VER_03:
	case RTL_VER_04:
	case RTL_VER_05:
	case RTL_VER_06:
	case RTL_VER_07:
	case RTL_VER_08:
	case RTL_VER_09:
	case RTL_VER_10:
	case RTL_VER_11:
	case RTL_VER_12:
	case RTL_VER_14:
		goto out;
	case RTL_VER_13:
	case RTL_VER_15:
	default:
		break;
	}

	fw_offset = __le16_to_cpu(phy->fw_offset);
	length = __le32_to_cpu(phy->blk_hdr.length);
	if (fw_offset < sizeof(*phy) || length <= fw_offset) {
		dev_err(&tp->intf->dev, "invalid fw_offset\n");
		goto out;
	}

	length -= fw_offset;
	if (length & 3) {
		dev_err(&tp->intf->dev, "invalid block length\n");
		goto out;
	}

	if (__le16_to_cpu(phy->fw_reg) != 0x9A00) {
		dev_err(&tp->intf->dev, "invalid register to load firmware\n");
		goto out;
	}

	rc = true;
out:
	return rc;
}

static bool rtl8152_is_fw_phy_ver_ok(struct r8152 *tp, struct fw_phy_ver *ver)
{
	bool rc = false;

	switch (tp->version) {
	case RTL_VER_10:
	case RTL_VER_11:
	case RTL_VER_12:
	case RTL_VER_13:
	case RTL_VER_15:
		break;
	default:
		goto out;
	}

	if (__le32_to_cpu(ver->blk_hdr.length) != sizeof(*ver)) {
		dev_err(&tp->intf->dev, "invalid block length\n");
		goto out;
	}

	if (__le16_to_cpu(ver->ver.addr) != SRAM_GPHY_FW_VER) {
		dev_err(&tp->intf->dev, "invalid phy ver addr\n");
		goto out;
	}

	rc = true;
out:
	return rc;
}

static bool rtl8152_is_fw_phy_fixup_ok(struct r8152 *tp, struct fw_phy_fixup *fix)
{
	bool rc = false;

	switch (tp->version) {
	case RTL_VER_10:
	case RTL_VER_11:
	case RTL_VER_12:
	case RTL_VER_13:
	case RTL_VER_15:
		break;
	default:
		goto out;
	}

	if (__le32_to_cpu(fix->blk_hdr.length) != sizeof(*fix)) {
		dev_err(&tp->intf->dev, "invalid block length\n");
		goto out;
	}

	if (__le16_to_cpu(fix->setting.addr) != OCP_PHY_PATCH_CMD ||
	    __le16_to_cpu(fix->setting.data) != BIT(7)) {
		dev_err(&tp->intf->dev, "invalid phy fixup\n");
		goto out;
	}

	rc = true;
out:
	return rc;
}

static bool rtl8152_is_fw_phy_union_ok(struct r8152 *tp, struct fw_phy_union *phy)
{
	u16 fw_offset;
	u32 length;
	bool rc = false;

	switch (tp->version) {
	case RTL_VER_10:
	case RTL_VER_11:
	case RTL_VER_12:
	case RTL_VER_13:
	case RTL_VER_15:
		break;
	default:
		goto out;
	}

	fw_offset = __le16_to_cpu(phy->fw_offset);
	length = __le32_to_cpu(phy->blk_hdr.length);
	if (fw_offset < sizeof(*phy) || length <= fw_offset) {
		dev_err(&tp->intf->dev, "invalid fw_offset\n");
		goto out;
	}

	length -= fw_offset;
	if (length & 1) {
		dev_err(&tp->intf->dev, "invalid block length\n");
		goto out;
	}

	if (phy->pre_num > 2) {
		dev_err(&tp->intf->dev, "invalid pre_num %d\n", phy->pre_num);
		goto out;
	}

	if (phy->bp_num > 8) {
		dev_err(&tp->intf->dev, "invalid bp_num %d\n", phy->bp_num);
		goto out;
	}

	rc = true;
out:
	return rc;
}

static bool rtl8152_is_fw_phy_nc_ok(struct r8152 *tp, struct fw_phy_nc *phy)
{
	u32 length;
	u16 fw_offset, fw_reg, ba_reg, patch_en_addr, mode_reg, bp_start;
	bool rc = false;

	switch (tp->version) {
	case RTL_VER_04:
	case RTL_VER_05:
	case RTL_VER_06:
		fw_reg = 0xa014;
		ba_reg = 0xa012;
		patch_en_addr = 0xa01a;
		mode_reg = 0xb820;
		bp_start = 0xa000;
		break;
	default:
		goto out;
	}

	fw_offset = __le16_to_cpu(phy->fw_offset);
	if (fw_offset < sizeof(*phy)) {
		dev_err(&tp->intf->dev, "fw_offset too small\n");
		goto out;
	}

	length = __le32_to_cpu(phy->blk_hdr.length);
	if (length < fw_offset) {
		dev_err(&tp->intf->dev, "invalid fw_offset\n");
		goto out;
	}

	length -= __le16_to_cpu(phy->fw_offset);
	if (!length || (length & 1)) {
		dev_err(&tp->intf->dev, "invalid block length\n");
		goto out;
	}

	if (__le16_to_cpu(phy->fw_reg) != fw_reg) {
		dev_err(&tp->intf->dev, "invalid register to load firmware\n");
		goto out;
	}

	if (__le16_to_cpu(phy->ba_reg) != ba_reg) {
		dev_err(&tp->intf->dev, "invalid base address register\n");
		goto out;
	}

	if (__le16_to_cpu(phy->patch_en_addr) != patch_en_addr) {
		dev_err(&tp->intf->dev,
			"invalid patch mode enabled register\n");
		goto out;
	}

	if (__le16_to_cpu(phy->mode_reg) != mode_reg) {
		dev_err(&tp->intf->dev,
			"invalid register to switch the mode\n");
		goto out;
	}

	if (__le16_to_cpu(phy->bp_start) != bp_start) {
		dev_err(&tp->intf->dev,
			"invalid start register of break point\n");
		goto out;
	}

	if (__le16_to_cpu(phy->bp_num) > 4) {
		dev_err(&tp->intf->dev, "invalid break point number\n");
		goto out;
	}

	rc = true;
out:
	return rc;
}

static bool rtl8152_is_fw_mac_ok(struct r8152 *tp, struct fw_mac *mac)
{
	u16 fw_reg, bp_ba_addr, bp_en_addr, bp_start, fw_offset;
	bool rc = false;
	u32 length, type;
	int i, max_bp;

	type = __le32_to_cpu(mac->blk_hdr.type);
	if (type == RTL_FW_PLA) {
		switch (tp->version) {
		case RTL_VER_01:
		case RTL_VER_02:
		case RTL_VER_07:
			fw_reg = 0xf800;
			bp_ba_addr = PLA_BP_BA;
			bp_en_addr = 0;
			bp_start = PLA_BP_0;
			max_bp = 8;
			break;
		case RTL_VER_03:
		case RTL_VER_04:
		case RTL_VER_05:
		case RTL_VER_06:
		case RTL_VER_08:
		case RTL_VER_09:
		case RTL_VER_11:
		case RTL_VER_12:
		case RTL_VER_13:
		case RTL_VER_14:
		case RTL_VER_15:
			fw_reg = 0xf800;
			bp_ba_addr = PLA_BP_BA;
			bp_en_addr = PLA_BP_EN;
			bp_start = PLA_BP_0;
			max_bp = 8;
			break;
		default:
			goto out;
		}
	} else if (type == RTL_FW_USB) {
		switch (tp->version) {
		case RTL_VER_03:
		case RTL_VER_04:
		case RTL_VER_05:
		case RTL_VER_06:
			fw_reg = 0xf800;
			bp_ba_addr = USB_BP_BA;
			bp_en_addr = USB_BP_EN;
			bp_start = USB_BP_0;
			max_bp = 8;
			break;
		case RTL_VER_08:
		case RTL_VER_09:
		case RTL_VER_11:
		case RTL_VER_12:
		case RTL_VER_13:
		case RTL_VER_14:
		case RTL_VER_15:
			fw_reg = 0xe600;
			bp_ba_addr = USB_BP_BA;
			bp_en_addr = USB_BP2_EN;
			bp_start = USB_BP_0;
			max_bp = 16;
			break;
		case RTL_VER_01:
		case RTL_VER_02:
		case RTL_VER_07:
		default:
			goto out;
		}
	} else {
		goto out;
	}

	fw_offset = __le16_to_cpu(mac->fw_offset);
	if (fw_offset < sizeof(*mac)) {
		dev_err(&tp->intf->dev, "fw_offset too small\n");
		goto out;
	}

	length = __le32_to_cpu(mac->blk_hdr.length);
	if (length < fw_offset) {
		dev_err(&tp->intf->dev, "invalid fw_offset\n");
		goto out;
	}

	length -= fw_offset;
	if (length < 4 || (length & 3)) {
		dev_err(&tp->intf->dev, "invalid block length\n");
		goto out;
	}

	if (__le16_to_cpu(mac->fw_reg) != fw_reg) {
		dev_err(&tp->intf->dev, "invalid register to load firmware\n");
		goto out;
	}

	if (__le16_to_cpu(mac->bp_ba_addr) != bp_ba_addr) {
		dev_err(&tp->intf->dev, "invalid base address register\n");
		goto out;
	}

	if (__le16_to_cpu(mac->bp_en_addr) != bp_en_addr) {
		dev_err(&tp->intf->dev, "invalid enabled mask register\n");
		goto out;
	}

	if (__le16_to_cpu(mac->bp_start) != bp_start) {
		dev_err(&tp->intf->dev,
			"invalid start register of break point\n");
		goto out;
	}

	if (__le16_to_cpu(mac->bp_num) > max_bp) {
		dev_err(&tp->intf->dev, "invalid break point number\n");
		goto out;
	}

	for (i = __le16_to_cpu(mac->bp_num); i < max_bp; i++) {
		if (mac->bp[i]) {
			dev_err(&tp->intf->dev, "unused bp%u is not zero\n", i);
			goto out;
		}
	}

	rc = true;
out:
	return rc;
}

/* Verify the checksum for the firmware file. It is calculated from the version
 * field to the end of the file. Compare the result with the checksum field to
 * make sure the file is correct.
 */
static long rtl8152_fw_verify_checksum(struct r8152 *tp,
				       struct fw_header *fw_hdr, size_t size)
{
	unsigned char checksum[sizeof(fw_hdr->checksum)];
	struct crypto_shash *alg;
	struct shash_desc *sdesc;
	size_t len;
	long rc;

	alg = crypto_alloc_shash("sha256", 0, 0);
	if (IS_ERR(alg)) {
		rc = PTR_ERR(alg);
		goto out;
	}

	if (crypto_shash_digestsize(alg) != sizeof(fw_hdr->checksum)) {
		rc = -EFAULT;
		dev_err(&tp->intf->dev, "digestsize incorrect (%u)\n",
			crypto_shash_digestsize(alg));
		goto free_shash;
	}

	len = sizeof(*sdesc) + crypto_shash_descsize(alg);
	sdesc = kmalloc(len, GFP_KERNEL);
	if (!sdesc) {
		rc = -ENOMEM;
		goto free_shash;
	}
	sdesc->tfm = alg;

	len = size - sizeof(fw_hdr->checksum);
	rc = crypto_shash_digest(sdesc, fw_hdr->version, len, checksum);
	kfree(sdesc);
	if (rc)
		goto free_shash;

	if (memcmp(fw_hdr->checksum, checksum, sizeof(fw_hdr->checksum))) {
		dev_err(&tp->intf->dev, "checksum fail\n");
		rc = -EFAULT;
	}

free_shash:
	crypto_free_shash(alg);
out:
	return rc;
}

static long rtl8152_check_firmware(struct r8152 *tp, struct rtl_fw *rtl_fw)
{
	const struct firmware *fw = rtl_fw->fw;
	struct fw_header *fw_hdr = (struct fw_header *)fw->data;
	unsigned long fw_flags = 0;
	long ret = -EFAULT;
	int i;

	if (fw->size < sizeof(*fw_hdr)) {
		dev_err(&tp->intf->dev, "file too small\n");
		goto fail;
	}

	ret = rtl8152_fw_verify_checksum(tp, fw_hdr, fw->size);
	if (ret)
		goto fail;

	ret = -EFAULT;

	for (i = sizeof(*fw_hdr); i < fw->size;) {
		struct fw_block *block = (struct fw_block *)&fw->data[i];
		u32 type;

		if ((i + sizeof(*block)) > fw->size)
			goto fail;

		type = __le32_to_cpu(block->type);
		switch (type) {
		case RTL_FW_END:
			if (__le32_to_cpu(block->length) != sizeof(*block))
				goto fail;
			goto fw_end;
		case RTL_FW_PLA:
			if (test_bit(FW_FLAGS_PLA, &fw_flags)) {
				dev_err(&tp->intf->dev,
					"multiple PLA firmware encountered");
				goto fail;
			}

			if (!rtl8152_is_fw_mac_ok(tp, (struct fw_mac *)block)) {
				dev_err(&tp->intf->dev,
					"check PLA firmware failed\n");
				goto fail;
			}
			__set_bit(FW_FLAGS_PLA, &fw_flags);
			break;
		case RTL_FW_USB:
			if (test_bit(FW_FLAGS_USB, &fw_flags)) {
				dev_err(&tp->intf->dev,
					"multiple USB firmware encountered");
				goto fail;
			}

			if (!rtl8152_is_fw_mac_ok(tp, (struct fw_mac *)block)) {
				dev_err(&tp->intf->dev,
					"check USB firmware failed\n");
				goto fail;
			}
			__set_bit(FW_FLAGS_USB, &fw_flags);
			break;
		case RTL_FW_PHY_START:
			if (test_bit(FW_FLAGS_START, &fw_flags) ||
			    test_bit(FW_FLAGS_NC, &fw_flags) ||
			    test_bit(FW_FLAGS_NC1, &fw_flags) ||
			    test_bit(FW_FLAGS_NC2, &fw_flags) ||
			    test_bit(FW_FLAGS_UC2, &fw_flags) ||
			    test_bit(FW_FLAGS_UC, &fw_flags) ||
			    test_bit(FW_FLAGS_STOP, &fw_flags)) {
				dev_err(&tp->intf->dev,
					"check PHY_START fail\n");
				goto fail;
			}

			if (__le32_to_cpu(block->length) != sizeof(struct fw_phy_patch_key)) {
				dev_err(&tp->intf->dev,
					"Invalid length for PHY_START\n");
				goto fail;
			}
			__set_bit(FW_FLAGS_START, &fw_flags);
			break;
		case RTL_FW_PHY_STOP:
			if (test_bit(FW_FLAGS_STOP, &fw_flags) ||
			    !test_bit(FW_FLAGS_START, &fw_flags)) {
				dev_err(&tp->intf->dev,
					"Check PHY_STOP fail\n");
				goto fail;
			}

			if (__le32_to_cpu(block->length) != sizeof(*block)) {
				dev_err(&tp->intf->dev,
					"Invalid length for PHY_STOP\n");
				goto fail;
			}
			__set_bit(FW_FLAGS_STOP, &fw_flags);
			break;
		case RTL_FW_PHY_NC:
			if (!test_bit(FW_FLAGS_START, &fw_flags) ||
			    test_bit(FW_FLAGS_STOP, &fw_flags)) {
				dev_err(&tp->intf->dev,
					"check PHY_NC fail\n");
				goto fail;
			}

			if (test_bit(FW_FLAGS_NC, &fw_flags)) {
				dev_err(&tp->intf->dev,
					"multiple PHY NC encountered\n");
				goto fail;
			}

			if (!rtl8152_is_fw_phy_nc_ok(tp, (struct fw_phy_nc *)block)) {
				dev_err(&tp->intf->dev,
					"check PHY NC firmware failed\n");
				goto fail;
			}
			__set_bit(FW_FLAGS_NC, &fw_flags);
			break;
		case RTL_FW_PHY_UNION_NC:
			if (!test_bit(FW_FLAGS_START, &fw_flags) ||
			    test_bit(FW_FLAGS_NC1, &fw_flags) ||
			    test_bit(FW_FLAGS_NC2, &fw_flags) ||
			    test_bit(FW_FLAGS_UC2, &fw_flags) ||
			    test_bit(FW_FLAGS_UC, &fw_flags) ||
			    test_bit(FW_FLAGS_STOP, &fw_flags)) {
				dev_err(&tp->intf->dev, "PHY_UNION_NC out of order\n");
				goto fail;
			}

			if (test_bit(FW_FLAGS_NC, &fw_flags)) {
				dev_err(&tp->intf->dev, "multiple PHY_UNION_NC encountered\n");
				goto fail;
			}

			if (!rtl8152_is_fw_phy_union_ok(tp, (struct fw_phy_union *)block)) {
				dev_err(&tp->intf->dev, "check PHY_UNION_NC failed\n");
				goto fail;
			}
			__set_bit(FW_FLAGS_NC, &fw_flags);
			break;
		case RTL_FW_PHY_UNION_NC1:
			if (!test_bit(FW_FLAGS_START, &fw_flags) ||
			    test_bit(FW_FLAGS_NC2, &fw_flags) ||
			    test_bit(FW_FLAGS_UC2, &fw_flags) ||
			    test_bit(FW_FLAGS_UC, &fw_flags) ||
			    test_bit(FW_FLAGS_STOP, &fw_flags)) {
				dev_err(&tp->intf->dev, "PHY_UNION_NC1 out of order\n");
				goto fail;
			}

			if (test_bit(FW_FLAGS_NC1, &fw_flags)) {
				dev_err(&tp->intf->dev, "multiple PHY NC1 encountered\n");
				goto fail;
			}

			if (!rtl8152_is_fw_phy_union_ok(tp, (struct fw_phy_union *)block)) {
				dev_err(&tp->intf->dev, "check PHY_UNION_NC1 failed\n");
				goto fail;
			}
			__set_bit(FW_FLAGS_NC1, &fw_flags);
			break;
		case RTL_FW_PHY_UNION_NC2:
			if (!test_bit(FW_FLAGS_START, &fw_flags) ||
			    test_bit(FW_FLAGS_UC2, &fw_flags) ||
			    test_bit(FW_FLAGS_UC, &fw_flags) ||
			    test_bit(FW_FLAGS_STOP, &fw_flags)) {
				dev_err(&tp->intf->dev, "PHY_UNION_NC2 out of order\n");
				goto fail;
			}

			if (test_bit(FW_FLAGS_NC2, &fw_flags)) {
				dev_err(&tp->intf->dev, "multiple PHY NC2 encountered\n");
				goto fail;
			}

			if (!rtl8152_is_fw_phy_union_ok(tp, (struct fw_phy_union *)block)) {
				dev_err(&tp->intf->dev, "check PHY_UNION_NC2 failed\n");
				goto fail;
			}
			__set_bit(FW_FLAGS_NC2, &fw_flags);
			break;
		case RTL_FW_PHY_UNION_UC2:
			if (!test_bit(FW_FLAGS_START, &fw_flags) ||
			    test_bit(FW_FLAGS_UC, &fw_flags) ||
			    test_bit(FW_FLAGS_STOP, &fw_flags)) {
				dev_err(&tp->intf->dev, "PHY_UNION_UC2 out of order\n");
				goto fail;
			}

			if (test_bit(FW_FLAGS_UC2, &fw_flags)) {
				dev_err(&tp->intf->dev, "multiple PHY UC2 encountered\n");
				goto fail;
			}

			if (!rtl8152_is_fw_phy_union_ok(tp, (struct fw_phy_union *)block)) {
				dev_err(&tp->intf->dev, "check PHY_UNION_UC2 failed\n");
				goto fail;
			}
			__set_bit(FW_FLAGS_UC2, &fw_flags);
			break;
		case RTL_FW_PHY_UNION_UC:
			if (!test_bit(FW_FLAGS_START, &fw_flags) ||
			    test_bit(FW_FLAGS_STOP, &fw_flags)) {
				dev_err(&tp->intf->dev, "PHY_UNION_UC out of order\n");
				goto fail;
			}

			if (test_bit(FW_FLAGS_UC, &fw_flags)) {
				dev_err(&tp->intf->dev, "multiple PHY UC encountered\n");
				goto fail;
			}

			if (!rtl8152_is_fw_phy_union_ok(tp, (struct fw_phy_union *)block)) {
				dev_err(&tp->intf->dev, "check PHY_UNION_UC failed\n");
				goto fail;
			}
			__set_bit(FW_FLAGS_UC, &fw_flags);
			break;
		case RTL_FW_PHY_UNION_MISC:
			if (!rtl8152_is_fw_phy_union_ok(tp, (struct fw_phy_union *)block)) {
				dev_err(&tp->intf->dev, "check RTL_FW_PHY_UNION_MISC failed\n");
				goto fail;
			}
			break;
		case RTL_FW_PHY_FIXUP:
			if (!rtl8152_is_fw_phy_fixup_ok(tp, (struct fw_phy_fixup *)block)) {
				dev_err(&tp->intf->dev, "check PHY fixup failed\n");
				goto fail;
			}
			break;
		case RTL_FW_PHY_SPEED_UP:
			if (test_bit(FW_FLAGS_SPEED_UP, &fw_flags)) {
				dev_err(&tp->intf->dev, "multiple PHY firmware encountered");
				goto fail;
			}

			if (!rtl8152_is_fw_phy_speed_up_ok(tp, (struct fw_phy_speed_up *)block)) {
				dev_err(&tp->intf->dev, "check PHY speed up failed\n");
				goto fail;
			}
			__set_bit(FW_FLAGS_SPEED_UP, &fw_flags);
			break;
		case RTL_FW_PHY_VER:
			if (test_bit(FW_FLAGS_START, &fw_flags) ||
			    test_bit(FW_FLAGS_NC, &fw_flags) ||
			    test_bit(FW_FLAGS_NC1, &fw_flags) ||
			    test_bit(FW_FLAGS_NC2, &fw_flags) ||
			    test_bit(FW_FLAGS_UC2, &fw_flags) ||
			    test_bit(FW_FLAGS_UC, &fw_flags) ||
			    test_bit(FW_FLAGS_STOP, &fw_flags)) {
				dev_err(&tp->intf->dev, "Invalid order to set PHY version\n");
				goto fail;
			}

			if (test_bit(FW_FLAGS_VER, &fw_flags)) {
				dev_err(&tp->intf->dev, "multiple PHY version encountered");
				goto fail;
			}

			if (!rtl8152_is_fw_phy_ver_ok(tp, (struct fw_phy_ver *)block)) {
				dev_err(&tp->intf->dev, "check PHY version failed\n");
				goto fail;
			}
			__set_bit(FW_FLAGS_VER, &fw_flags);
			break;
		default:
			dev_warn(&tp->intf->dev, "Unknown type %u is found\n",
				 type);
			break;
		}

		/* next block */
		i += ALIGN(__le32_to_cpu(block->length), 8);
	}

fw_end:
	if (test_bit(FW_FLAGS_START, &fw_flags) && !test_bit(FW_FLAGS_STOP, &fw_flags)) {
		dev_err(&tp->intf->dev, "without PHY_STOP\n");
		goto fail;
	}

	return 0;
fail:
	return ret;
}

static void rtl_ram_code_speed_up(struct r8152 *tp, struct fw_phy_speed_up *phy, bool wait)
{
	u32 len;
	u8 *data;

	if (sram_read(tp, SRAM_GPHY_FW_VER) >= __le16_to_cpu(phy->version)) {
		dev_dbg(&tp->intf->dev, "PHY firmware has been the newest\n");
		return;
	}

	len = __le32_to_cpu(phy->blk_hdr.length);
	len -= __le16_to_cpu(phy->fw_offset);
	data = (u8 *)phy + __le16_to_cpu(phy->fw_offset);

	if (rtl_phy_patch_request(tp, true, wait))
		return;

	while (len) {
		u32 ocp_data, size;
		int i;

		if (len < 2048)
			size = len;
		else
			size = 2048;

		ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_GPHY_CTRL);
		ocp_data |= GPHY_PATCH_DONE | BACKUP_RESTRORE;
		ocp_write_word(tp, MCU_TYPE_USB, USB_GPHY_CTRL, ocp_data);

		generic_ocp_write(tp, __le16_to_cpu(phy->fw_reg), 0xff, size, data, MCU_TYPE_USB);

		data += size;
		len -= size;

		ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_POL_GPIO_CTRL);
		ocp_data |= POL_GPHY_PATCH;
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_POL_GPIO_CTRL, ocp_data);

		for (i = 0; i < 1000; i++) {
			if (!(ocp_read_word(tp, MCU_TYPE_PLA, PLA_POL_GPIO_CTRL) & POL_GPHY_PATCH))
				break;
		}

		if (i == 1000) {
			dev_err(&tp->intf->dev, "ram code speedup mode timeout\n");
			break;
		}
	}

	ocp_write_word(tp, MCU_TYPE_PLA, PLA_OCP_GPHY_BASE, tp->ocp_base);
	rtl_phy_patch_request(tp, false, wait);

	if (sram_read(tp, SRAM_GPHY_FW_VER) == __le16_to_cpu(phy->version))
		dev_dbg(&tp->intf->dev, "successfully applied %s\n", phy->info);
	else
		dev_err(&tp->intf->dev, "ram code speedup mode fail\n");
}

static int rtl8152_fw_phy_ver(struct r8152 *tp, struct fw_phy_ver *phy_ver)
{
	u16 ver_addr, ver;

	ver_addr = __le16_to_cpu(phy_ver->ver.addr);
	ver = __le16_to_cpu(phy_ver->ver.data);

	if (sram_read(tp, ver_addr) >= ver) {
		dev_dbg(&tp->intf->dev, "PHY firmware has been the newest\n");
		return 0;
	}

	sram_write(tp, ver_addr, ver);

	dev_dbg(&tp->intf->dev, "PHY firmware version %x\n", ver);

	return ver;
}

static void rtl8152_fw_phy_fixup(struct r8152 *tp, struct fw_phy_fixup *fix)
{
	u16 addr, data;

	addr = __le16_to_cpu(fix->setting.addr);
	data = ocp_reg_read(tp, addr);

	switch (__le16_to_cpu(fix->bit_cmd)) {
	case FW_FIXUP_AND:
		data &= __le16_to_cpu(fix->setting.data);
		break;
	case FW_FIXUP_OR:
		data |= __le16_to_cpu(fix->setting.data);
		break;
	case FW_FIXUP_NOT:
		data &= ~__le16_to_cpu(fix->setting.data);
		break;
	case FW_FIXUP_XOR:
		data ^= __le16_to_cpu(fix->setting.data);
		break;
	default:
		return;
	}

	ocp_reg_write(tp, addr, data);

	dev_dbg(&tp->intf->dev, "applied ocp %x %x\n", addr, data);
}

static void rtl8152_fw_phy_union_apply(struct r8152 *tp, struct fw_phy_union *phy)
{
	__le16 *data;
	u32 length;
	int i, num;

	num = phy->pre_num;
	for (i = 0; i < num; i++)
		sram_write(tp, __le16_to_cpu(phy->pre_set[i].addr),
			   __le16_to_cpu(phy->pre_set[i].data));

	length = __le32_to_cpu(phy->blk_hdr.length);
	length -= __le16_to_cpu(phy->fw_offset);
	num = length / 2;
	data = (__le16 *)((u8 *)phy + __le16_to_cpu(phy->fw_offset));

	ocp_reg_write(tp, OCP_SRAM_ADDR, __le16_to_cpu(phy->fw_reg));
	for (i = 0; i < num; i++)
		ocp_reg_write(tp, OCP_SRAM_DATA, __le16_to_cpu(data[i]));

	num = phy->bp_num;
	for (i = 0; i < num; i++)
		sram_write(tp, __le16_to_cpu(phy->bp[i].addr), __le16_to_cpu(phy->bp[i].data));

	if (phy->bp_num && phy->bp_en.addr)
		sram_write(tp, __le16_to_cpu(phy->bp_en.addr), __le16_to_cpu(phy->bp_en.data));

	dev_dbg(&tp->intf->dev, "successfully applied %s\n", phy->info);
}

static void rtl8152_fw_phy_nc_apply(struct r8152 *tp, struct fw_phy_nc *phy)
{
	u16 mode_reg, bp_index;
	u32 length, i, num;
	__le16 *data;

	mode_reg = __le16_to_cpu(phy->mode_reg);
	sram_write(tp, mode_reg, __le16_to_cpu(phy->mode_pre));
	sram_write(tp, __le16_to_cpu(phy->ba_reg),
		   __le16_to_cpu(phy->ba_data));

	length = __le32_to_cpu(phy->blk_hdr.length);
	length -= __le16_to_cpu(phy->fw_offset);
	num = length / 2;
	data = (__le16 *)((u8 *)phy + __le16_to_cpu(phy->fw_offset));

	ocp_reg_write(tp, OCP_SRAM_ADDR, __le16_to_cpu(phy->fw_reg));
	for (i = 0; i < num; i++)
		ocp_reg_write(tp, OCP_SRAM_DATA, __le16_to_cpu(data[i]));

	sram_write(tp, __le16_to_cpu(phy->patch_en_addr),
		   __le16_to_cpu(phy->patch_en_value));

	bp_index = __le16_to_cpu(phy->bp_start);
	num = __le16_to_cpu(phy->bp_num);
	for (i = 0; i < num; i++) {
		sram_write(tp, bp_index, __le16_to_cpu(phy->bp[i]));
		bp_index += 2;
	}

	sram_write(tp, mode_reg, __le16_to_cpu(phy->mode_post));

	dev_dbg(&tp->intf->dev, "successfully applied %s\n", phy->info);
}

static void rtl8152_fw_mac_apply(struct r8152 *tp, struct fw_mac *mac)
{
	u16 bp_en_addr, bp_index, type, bp_num, fw_ver_reg;
	u32 length;
	u8 *data;
	int i;

	switch (__le32_to_cpu(mac->blk_hdr.type)) {
	case RTL_FW_PLA:
		type = MCU_TYPE_PLA;
		break;
	case RTL_FW_USB:
		type = MCU_TYPE_USB;
		break;
	default:
		return;
	}

	fw_ver_reg = __le16_to_cpu(mac->fw_ver_reg);
	if (fw_ver_reg && ocp_read_byte(tp, MCU_TYPE_USB, fw_ver_reg) >= mac->fw_ver_data) {
		dev_dbg(&tp->intf->dev, "%s firmware has been the newest\n", type ? "PLA" : "USB");
		return;
	}

	rtl_clear_bp(tp, type);

	/* Enable backup/restore of MACDBG. This is required after clearing PLA
	 * break points and before applying the PLA firmware.
	 */
	if (tp->version == RTL_VER_04 && type == MCU_TYPE_PLA &&
	    !(ocp_read_word(tp, MCU_TYPE_PLA, PLA_MACDBG_POST) & DEBUG_OE)) {
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_MACDBG_PRE, DEBUG_LTSSM);
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_MACDBG_POST, DEBUG_LTSSM);
	}

	length = __le32_to_cpu(mac->blk_hdr.length);
	length -= __le16_to_cpu(mac->fw_offset);

	data = (u8 *)mac;
	data += __le16_to_cpu(mac->fw_offset);

	generic_ocp_write(tp, __le16_to_cpu(mac->fw_reg), 0xff, length, data,
			  type);

	ocp_write_word(tp, type, __le16_to_cpu(mac->bp_ba_addr),
		       __le16_to_cpu(mac->bp_ba_value));

	bp_index = __le16_to_cpu(mac->bp_start);
	bp_num = __le16_to_cpu(mac->bp_num);
	for (i = 0; i < bp_num; i++) {
		ocp_write_word(tp, type, bp_index, __le16_to_cpu(mac->bp[i]));
		bp_index += 2;
	}

	bp_en_addr = __le16_to_cpu(mac->bp_en_addr);
	if (bp_en_addr)
		ocp_write_word(tp, type, bp_en_addr,
			       __le16_to_cpu(mac->bp_en_value));

	if (fw_ver_reg)
		ocp_write_byte(tp, MCU_TYPE_USB, fw_ver_reg,
			       mac->fw_ver_data);

	dev_dbg(&tp->intf->dev, "successfully applied %s\n", mac->info);
}

void rtl8152_apply_firmware(struct r8152 *tp, bool power_cut)
{
	struct rtl_fw *rtl_fw = &tp->rtl_fw;
	const struct firmware *fw;
	struct fw_header *fw_hdr;
	struct fw_phy_patch_key *key;
	u16 key_addr = 0;
	int i, patch_phy = 1;

	if (IS_ERR_OR_NULL(rtl_fw->fw))
		return;

	fw = rtl_fw->fw;
	fw_hdr = (struct fw_header *)fw->data;

	if (rtl_fw->pre_fw)
		rtl_fw->pre_fw(tp);

	for (i = offsetof(struct fw_header, blocks); i < fw->size;) {
		struct fw_block *block = (struct fw_block *)&fw->data[i];

		switch (__le32_to_cpu(block->type)) {
		case RTL_FW_END:
			goto post_fw;
		case RTL_FW_PLA:
		case RTL_FW_USB:
			rtl8152_fw_mac_apply(tp, (struct fw_mac *)block);
			break;
		case RTL_FW_PHY_START:
			if (!patch_phy)
				break;
			key = (struct fw_phy_patch_key *)block;
			key_addr = __le16_to_cpu(key->key_reg);
			rtl_pre_ram_code(tp, key_addr, __le16_to_cpu(key->key_data), !power_cut);
			break;
		case RTL_FW_PHY_STOP:
			if (!patch_phy)
				break;
			WARN_ON(!key_addr);
			rtl_post_ram_code(tp, key_addr, !power_cut);
			break;
		case RTL_FW_PHY_NC:
			rtl8152_fw_phy_nc_apply(tp, (struct fw_phy_nc *)block);
			break;
		case RTL_FW_PHY_VER:
			patch_phy = rtl8152_fw_phy_ver(tp, (struct fw_phy_ver *)block);
			break;
		case RTL_FW_PHY_UNION_NC:
		case RTL_FW_PHY_UNION_NC1:
		case RTL_FW_PHY_UNION_NC2:
		case RTL_FW_PHY_UNION_UC2:
		case RTL_FW_PHY_UNION_UC:
		case RTL_FW_PHY_UNION_MISC:
			if (patch_phy)
				rtl8152_fw_phy_union_apply(tp, (struct fw_phy_union *)block);
			break;
		case RTL_FW_PHY_FIXUP:
			if (patch_phy)
				rtl8152_fw_phy_fixup(tp, (struct fw_phy_fixup *)block);
			break;
		case RTL_FW_PHY_SPEED_UP:
			rtl_ram_code_speed_up(tp, (struct fw_phy_speed_up *)block, !power_cut);
			break;
		default:
			break;
		}

		i += ALIGN(__le32_to_cpu(block->length), 8);
	}

post_fw:
	if (rtl_fw->post_fw)
		rtl_fw->post_fw(tp);

	strscpy(rtl_fw->version, fw_hdr->version, RTL_VER_SIZE);
	dev_info(&tp->intf->dev, "load %s successfully\n", rtl_fw->version);
}

void rtl8152_release_firmware(struct r8152 *tp)
{
	struct rtl_fw *rtl_fw = &tp->rtl_fw;

	if (!IS_ERR_OR_NULL(rtl_fw->fw)) {
		release_firmware(rtl_fw->fw);
		rtl_fw->fw = NULL;
	}
}

int rtl8152_request_firmware(struct r8152 *tp)
{
	struct rtl_fw *rtl_fw = &tp->rtl_fw;
	long rc;

	if (rtl_fw->fw || !rtl_fw->fw_name) {
		dev_info(&tp->intf->dev, "skip request firmware\n");
		rc = 0;
		goto result;
	}

	rc = request_firmware(&rtl_fw->fw, rtl_fw->fw_name, &tp->intf->dev);
	if (rc < 0)
		goto result;

	rc = rtl8152_check_firmware(tp, rtl_fw);
	if (rc < 0)
		release_firmware(rtl_fw->fw);

result:
	if (rc) {
		rtl_fw->fw = ERR_PTR(rc);

		dev_warn(&tp->intf->dev,
			 "unable to load firmware patch %s (%ld)\n",
			 rtl_fw->fw_name, rc);
	}

	return rc;
}

static int r8153_pre_firmware_1(struct r8152 *tp)
{
	int i;

	/* Wait till the WTD timer is ready. It would take at most 104 ms. */
	for (i = 0; i < 104; i++) {
		u32 ocp_data = ocp_read_byte(tp, MCU_TYPE_USB, USB_WDT1_CTRL);

		if (!(ocp_data & WTD1_EN))
			break;
		usleep_range(1000, 2000);
	}

	return 0;
}

static int r8153_post_firmware_1(struct r8152 *tp)
{
	/* reset UPHY timer to 36 ms */
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_UPHY_TIMER, 36000 / 16);

	return 0;
}

static int r8153_pre_firmware_2(struct r8152 *tp)
{
	u32 ocp_data;

	r8153_pre_firmware_1(tp);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_FW_FIX_EN0);
	ocp_data &= ~FW_FIX_SUSPEND;
	ocp_write_word(tp, MCU_TYPE_USB, USB_FW_FIX_EN0, ocp_data);

	return 0;
}

#define BP4_SUPER_ONLY		0x1578	/* RTL_VER_04 only */

static int r8153_post_firmware_2(struct r8152 *tp)
{
	u32 ocp_data;

	/* set USB_BP_4 to support USB_SPEED_SUPER only */
	if (ocp_read_byte(tp, MCU_TYPE_USB, USB_CSTMR) & FORCE_SUPER)
		ocp_write_word(tp, MCU_TYPE_USB, USB_BP_4, BP4_SUPER_ONLY);

	r8153_post_firmware_1(tp);

	/* enable U3P3 check, set the counter to 4 */
	ocp_write_word(tp, MCU_TYPE_PLA, PLA_EXTRA_STATUS, U3P3_CHECK_EN | 4);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_FW_FIX_EN0);
	ocp_data |= FW_FIX_SUSPEND;
	ocp_write_word(tp, MCU_TYPE_USB, USB_FW_FIX_EN0, ocp_data);

	ocp_data = ocp_read_byte(tp, MCU_TYPE_USB, USB_USB2PHY);
	ocp_data |= USB2PHY_L1 | USB2PHY_SUSPEND;
	ocp_write_byte(tp, MCU_TYPE_USB, USB_USB2PHY, ocp_data);

	return 0;
}

static int r8153_post_firmware_3(struct r8152 *tp)
{
	u32 ocp_data;

	/* enable bp0 if support USB_SPEED_SUPER only */
	if (ocp_read_byte(tp, MCU_TYPE_USB, USB_CSTMR) & FORCE_SUPER) {
		ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_BP_EN);
		ocp_data |= BIT(0);
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_BP_EN, ocp_data);
	}

	ocp_data = ocp_read_byte(tp, MCU_TYPE_USB, USB_USB2PHY);
	ocp_data |= USB2PHY_L1 | USB2PHY_SUSPEND;
	ocp_write_byte(tp, MCU_TYPE_USB, USB_USB2PHY, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_FW_FIX_EN1);
	ocp_data |= FW_IP_RESET_EN;
	ocp_write_word(tp, MCU_TYPE_USB, USB_FW_FIX_EN1, ocp_data);

	return 0;
}

static int r8153b_pre_firmware_1(struct r8152 *tp)
{
	/* enable fc timer and set timer to 1 second. */
	ocp_write_word(tp, MCU_TYPE_USB, USB_FC_TIMER,
		       CTRL_TIMER_EN | (1000 / 8));

	return 0;
}

static int r8153b_post_firmware_1(struct r8152 *tp)
{
	u32 ocp_data;

	/* enable bp0 for RTL8153-BND */
	ocp_data = ocp_read_byte(tp, MCU_TYPE_USB, USB_MISC_1);
	if (ocp_data & BND_MASK) {
		ocp_data = ocp_read_word(tp, MCU_TYPE_PLA, PLA_BP_EN);
		ocp_data |= BIT(0);
		ocp_write_word(tp, MCU_TYPE_PLA, PLA_BP_EN, ocp_data);
	}

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_FW_CTRL);
	ocp_data |= FLOW_CTRL_PATCH_OPT;
	ocp_write_word(tp, MCU_TYPE_USB, USB_FW_CTRL, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_FW_TASK);
	ocp_data |= FC_PATCH_TASK;
	ocp_write_word(tp, MCU_TYPE_USB, USB_FW_TASK, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_FW_FIX_EN1);
	ocp_data |= FW_IP_RESET_EN;
	ocp_write_word(tp, MCU_TYPE_USB, USB_FW_FIX_EN1, ocp_data);

	return 0;
}

static int r8153c_post_firmware_1(struct r8152 *tp)
{
	u32 ocp_data;

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_FW_CTRL);
	ocp_data |= FLOW_CTRL_PATCH_2;
	ocp_write_word(tp, MCU_TYPE_USB, USB_FW_CTRL, ocp_data);

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_FW_TASK);
	ocp_data |= FC_PATCH_TASK;
	ocp_write_word(tp, MCU_TYPE_USB, USB_FW_TASK, ocp_data);

	return 0;
}

static int r8156a_post_firmware_1(struct r8152 *tp)
{
	u32 ocp_data;

	ocp_data = ocp_read_word(tp, MCU_TYPE_USB, USB_FW_FIX_EN1);
	ocp_data |= FW_IP_RESET_EN;
	ocp_write_word(tp, MCU_TYPE_USB, USB_FW_FIX_EN1, ocp_data);

	/* Modify U3PHY parameter for compatibility issue */
	ocp_write_dword(tp, MCU_TYPE_USB, USB_UPHY3_MDCMDIO, 0x4026840e);
	ocp_write_dword(tp, MCU_TYPE_USB, USB_UPHY3_MDCMDIO, 0x4001acc9);

	return 0;
}

int rtl_fw_init(struct r8152 *tp)
{
	struct rtl_fw *rtl_fw = &tp->rtl_fw;

	switch (tp->version) {
	case RTL_VER_04:
		rtl_fw->fw_name		= FIRMWARE_8153A_2;
		rtl_fw->pre_fw		= r8153_pre_firmware_1;
		rtl_fw->post_fw		= r8153_post_firmware_1;
		break;
	case RTL_VER_05:
		rtl_fw->fw_name		= FIRMWARE_8153A_3;
		rtl_fw->pre_fw		= r8153_pre_firmware_2;
		rtl_fw->post_fw		= r8153_post_firmware_2;
		break;
	case RTL_VER_06:
		rtl_fw->fw_name		= FIRMWARE_8153A_4;
		rtl_fw->post_fw		= r8153_post_firmware_3;
		break;
	case RTL_VER_09:
		rtl_fw->fw_name		= FIRMWARE_8153B_2;
		rtl_fw->pre_fw		= r8153b_pre_firmware_1;
		rtl_fw->post_fw		= r8153b_post_firmware_1;
		break;
	case RTL_VER_11:
		rtl_fw->fw_name		= FIRMWARE_8156A_2;
		rtl_fw->post_fw		= r8156a_post_firmware_1;
		break;
	case RTL_VER_13:
	case RTL_VER_15:
		rtl_fw->fw_name		= FIRMWARE_8156B_2;
		break;
	case RTL_VER_14:
		rtl_fw->fw_name		= FIRMWARE_8153C_1;
		rtl_fw->pre_fw		= r8153b_pre_firmware_1;
		rtl_fw->post_fw		= r8153c_post_firmware_1;
		break;
	default:
		break;
	}

	return 0;
}
