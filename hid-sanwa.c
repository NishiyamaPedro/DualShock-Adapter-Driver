// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (c) 2021 Pedro Nishiyama <nishiyama.v3@gmail.com>
 * Based on hid-dualsense driver
 */

#include <linux/input.h>
#include <linux/slab.h>
#include <linux/hid.h>
#include <linux/module.h>

static const int gamepad_buttons[] = {
	BTN_WEST,
	BTN_NORTH,
	BTN_EAST,
	BTN_SOUTH,
	BTN_TL,
	BTN_TR,
	BTN_TL2,
	BTN_TR2,
	BTN_SELECT,
	BTN_START,
	BTN_THUMBL,
	BTN_THUMBR,
};

static struct input_dev *sanwa_allocate_input_dev(struct hid_device *hdev, const char *sfx)
{
	struct input_dev *input_dev;

	input_dev = devm_input_allocate_device(&hdev->dev);
	if (!input_dev)
		return ERR_PTR(-ENOMEM);

	input_dev->id.bustype = hdev->bus;
	input_dev->id.vendor = hdev->vendor;
	input_dev->id.product = hdev->product;
	input_dev->id.version = hdev->version;
	input_dev->uniq = hdev->uniq;
	
	input_dev->name = devm_kasprintf(&hdev->dev, GFP_KERNEL, "%s %s", hdev->name, sfx);
	if (!input_dev->name)
		return ERR_PTR(-ENOMEM);
	
	input_set_drvdata(input_dev, hdev);
	return input_dev;
}

static void sanwa_set_capabilities(struct input_dev *port)
{
	unsigned int i;

	input_set_abs_params(port, ABS_X, 0, 255, 0, 0);
	input_set_abs_params(port, ABS_Y, 0, 255, 0, 0);
	input_set_abs_params(port, ABS_Z, 0, 255, 0, 0);
	input_set_abs_params(port, ABS_RX, 0, 255, 0, 0);
	input_set_abs_params(port, ABS_RY, 0, 255, 0, 0);
	input_set_abs_params(port, ABS_RZ, 0, 255, 0, 0);

	input_set_abs_params(port, ABS_HAT0X, -1, 1, 0, 0);
	input_set_abs_params(port, ABS_HAT0Y, -1, 1, 0, 0);

	for (i = 0; i < ARRAY_SIZE(gamepad_buttons); i++)
		input_set_capability(port, EV_KEY, gamepad_buttons[i]);
}

static int sanwa_create_inputs(struct hid_device *hdev)
{
	struct input_dev *port_one;
	struct input_dev *port_two;
	int ret;

	hid_err(hdev, "creating port 1\n");
	port_one = sanwa_allocate_input_dev(hdev, "Port 1");
	sanwa_set_capabilities(port_one);
	ret = input_register_device(port_one);
	if (ret)
		return ret;

	hid_err(hdev, "creating port 2\n");
	port_two = sanwa_allocate_input_dev(hdev, "Port 2");
	sanwa_set_capabilities(port_two);
	ret = input_register_device(port_two);
	if (ret)
		return ret;
	
	return 0;
}

static int sanwa_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;

	ret = hid_parse(hdev);
	if (ret) {
		hid_err(hdev, "parse failed\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret) {
		hid_err(hdev, "hw start failed\n");
		return ret;
	}

	ret = hid_hw_open(hdev);
	if (ret) {
		hid_err(hdev, "hw open failed\n");
		goto err_stop;
	}

	ret = sanwa_create_inputs(hdev);
	if (ret) {
		hid_err(hdev, "failed to create inputs\n");
		goto err_close;
	}

	return ret;

err_close:
	hid_hw_close(hdev);
err_stop:
	hid_hw_stop(hdev);
	return ret;
}

static int sanwa_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data, int size)
{
	/* TO-DO */
	return 0;
}

static const struct hid_device_id sanwa_devices[] = {
	{ HID_USB_DEVICE(0x0d9d, 0x3012),  },
	{ }
};
MODULE_DEVICE_TABLE(hid, sanwa_devices);

static struct hid_driver sanwa_driver = {
	.name = "sanwa",
	.id_table = sanwa_devices,
	.probe = sanwa_probe,
	.raw_event = sanwa_raw_event,
};
module_hid_driver(sanwa_driver);

MODULE_AUTHOR("Pedro Nishiyama <nishiyama.v3@gmail.com>");
MODULE_DESCRIPTION("HID Driver for sanwa dualshock adapter.");
MODULE_LICENSE("GPL");
