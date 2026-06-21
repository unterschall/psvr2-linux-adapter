// SPDX-License-Identifier: GPL-2.0
/*
 * PSVR2 Linux driver — headset controls via the input subsystem.
 *
 *   function button  -> KEY (BTN_MODE, the conventional "system" button)
 *   proximity sensor  -> SW_FRONT_PROXIMITY ("headset worn")
 *   IPD dial (59..72) -> ABS_MISC
 *
 * Reports are emitted only on change. Values come from the IF7 status header.
 *
 * Copyright (C) 2026 PSVR2 Linux project
 */
#include <linux/input.h>
#include <linux/slab.h>

#include "psvr2.h"

struct psvr2_input {
	struct input_dev	*dev;
	bool			have_state;
	bool			function_button;
	bool			proximity;
	u8			ipd_mm;
};

void psvr2_input_report(struct psvr2_device *psvr2, bool function_button,
			bool proximity, u8 ipd_mm)
{
	struct psvr2_input *in = psvr2->input;
	bool changed = false;

	if (!in)
		return;

	if (!in->have_state || in->function_button != function_button) {
		input_report_key(in->dev, BTN_MODE, function_button);
		in->function_button = function_button;
		changed = true;
	}
	if (!in->have_state || in->proximity != proximity) {
		input_report_switch(in->dev, SW_FRONT_PROXIMITY, proximity);
		in->proximity = proximity;
		changed = true;
	}
	if (!in->have_state || in->ipd_mm != ipd_mm) {
		input_report_abs(in->dev, ABS_MISC, ipd_mm);
		in->ipd_mm = ipd_mm;
		changed = true;
	}

	in->have_state = true;
	if (changed)
		input_sync(in->dev);
}

int psvr2_input_register(struct psvr2_device *psvr2, struct device *parent)
{
	struct psvr2_input *in;
	struct input_dev *dev;
	int ret;

	in = devm_kzalloc(parent, sizeof(*in), GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	dev = devm_input_allocate_device(parent);
	if (!dev)
		return -ENOMEM;

	in->dev = dev;
	dev->name = "PlayStation VR2 Headset Controls";
	dev->phys = "psvr2/input0";
	dev->id.bustype = BUS_USB;
	dev->id.vendor = PSVR2_VENDOR_ID;
	dev->id.product = PSVR2_PRODUCT_ID;

	input_set_capability(dev, EV_KEY, BTN_MODE);
	input_set_capability(dev, EV_SW, SW_FRONT_PROXIMITY);
	input_set_abs_params(dev, ABS_MISC, PSVR2_IPD_MIN_MM, PSVR2_IPD_MAX_MM,
			     0, 0);

	ret = input_register_device(dev);
	if (ret)
		return ret;

	psvr2->input = in;
	return 0;
}
