/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __IEEE802154_NL802154_H
#define __IEEE802154_NL802154_H

#include "core.h"

int nl802154_init(void);
void nl802154_exit(void);
int nl802154_send_scan_done(struct cfg802154_registered_device *rdev,
			    struct wpan_dev *wpan_dev);

#endif /* __IEEE802154_NL802154_H */
