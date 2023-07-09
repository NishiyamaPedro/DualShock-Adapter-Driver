// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (c) 2021 Pedro Nishiyama <nishiyama.v3@gmail.com>
 * Based on hid-playstation driver
 *
 */

#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>

#define BUTTONS0_HAT_SWITCH GENMASK(3, 0)
#define BUTTONS0_CROSS BIT(4)
#define BUTTONS0_CIRCLE BIT(5)
#define BUTTONS0_SQUARE BIT(6)
#define BUTTONS0_TRIANGLE BIT(7)
#define BUTTONS1_L1 BIT(0)
#define BUTTONS1_L2 BIT(1)
#define BUTTONS1_R1 BIT(2)
#define BUTTONS1_R2 BIT(3)
#define BUTTONS1_SELECT BIT(4)
#define BUTTONS1_START BIT(5)
#define BUTTONS1_L3 BIT(6)
#define BUTTONS1_R3 BIT(7)

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

struct sanwa_adapter
{
	struct input_dev *ports[2];
};

struct port_index
{
	unsigned int idx;
};

struct sanwa_input_report
{
	uint8_t ry, rx; // Right Y and X axis
	uint8_t x, y;	// Left X and Y axis
	uint8_t buttons[3];
};

static const struct {int x; int y; } sanwa_gamepad_hat_mapping[] = {
	{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1},
	{0, 0},
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

	input_dev->name = devm_kasprintf(&hdev->dev, GFP_KERNEL, "%s %s", hdev->name, sfx);
	if (!input_dev->name)
		return ERR_PTR(-ENOMEM);

	input_dev->phys = devm_kasprintf(&hdev->dev, GFP_KERNEL, "%s", hdev->phys);
	if (!input_dev->phys)
		return ERR_PTR(-ENOMEM);

	input_set_drvdata(input_dev, hdev);
	return input_dev;
}

static int sanwa_ff_play(struct input_dev *dev, void *data, struct ff_effect *effect)
{
	struct hid_device *hdev = input_get_drvdata(dev);
	struct port_index *portidx = data;
	struct hid_report *report = hdev->report_enum[HID_OUTPUT_REPORT].report_id_hash[portidx->idx];

	int strong, weak;

	strong = effect->u.rumble.strong_magnitude;
	weak = effect->u.rumble.weak_magnitude;

	report->field[0]->value[0] = 0x01;
	report->field[1]->value[0] = 0x00;

	if (strong || weak)
	{
		strong = strong * 0xff / 0xffff;
		weak = weak * 0xff / 0xffff;

		report->field[2]->value[0] = strong;
		report->field[3]->value[0] = weak;
	}
	else
	{
		report->field[2]->value[0] = 0x00;
		report->field[3]->value[0] = 0x00;
	}

	hid_hw_request(hdev, report, HID_REQ_SET_REPORT);

	return 0;
}

static int sanwa_set_capabilities(struct input_dev *port, int index)
{
	struct port_index *portidx;
	int ret;
	unsigned int i;

	input_set_abs_params(port, ABS_X, 0, 255, 0, 0);
	input_set_abs_params(port, ABS_Y, 0, 255, 0, 0);
	input_set_abs_params(port, ABS_RX, 0, 255, 0, 0);
	input_set_abs_params(port, ABS_RY, 0, 255, 0, 0);

	input_set_abs_params(port, ABS_HAT0X, -1, 1, 0, 0);
	input_set_abs_params(port, ABS_HAT0Y, -1, 1, 0, 0);

	for (i = 0; i < ARRAY_SIZE(gamepad_buttons); i++)
		input_set_capability(port, EV_KEY, gamepad_buttons[i]);

	input_set_capability(port, EV_FF, FF_RUMBLE);

	portidx = kzalloc(sizeof(*portidx), GFP_KERNEL);
	if (!portidx)
		return -ENOMEM;

	portidx->idx = index;

	ret = input_ff_create_memless(port, portidx, sanwa_ff_play);
	if (ret)
	{
		kfree(portidx);
		return ret;
	}

	return 0;
}

static int sanwa_create_inputs(struct hid_device *hdev)
{
	struct sanwa_adapter *sa;
	int ret;

	sa = devm_kzalloc(&hdev->dev, sizeof(*sa), GFP_KERNEL);
	if (!sa)
		return -ENOMEM;

	hid_info(hdev, "creating port 1\n");
	sa->ports[0] = sanwa_allocate_input_dev(hdev, "Port 1");
	ret = sanwa_set_capabilities(sa->ports[0], 1);
	if (ret)
		return ret;

	ret = input_register_device(sa->ports[0]);
	if (ret)
		return ret;

	hid_info(hdev, "creating port 2\n");
	sa->ports[1] = sanwa_allocate_input_dev(hdev, "Port 2");
	ret = sanwa_set_capabilities(sa->ports[1], 2);
	if (ret)
		return ret;

	ret = input_register_device(sa->ports[1]);
	if (ret)
		return ret;

	hid_set_drvdata(hdev, sa);

	return 0;
}

static int sanwa_probe(struct hid_device *hdev, const struct hid_device_id *id)
{
	int ret;

	ret = hid_parse(hdev);
	if (ret)
	{
		hid_err(hdev, "parse failed\n");
		return ret;
	}

	ret = hid_hw_start(hdev, HID_CONNECT_HIDRAW);
	if (ret)
	{
		hid_err(hdev, "hw start failed\n");
		return ret;
	}

	ret = hid_hw_open(hdev);
	if (ret)
	{
		hid_err(hdev, "hw open failed\n");
		goto err_stop;
	}

	ret = sanwa_create_inputs(hdev);
	if (ret)
	{
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

static void sanwa_remove(struct hid_device *hdev)
{
	hid_hw_close(hdev);
	hid_hw_stop(hdev);
}

static int sanwa_raw_event(struct hid_device *hdev, struct hid_report *report, u8 *data, int size)
{
	struct sanwa_adapter *sa = hid_get_drvdata(hdev);
	struct sanwa_input_report *sa_report = (struct sanwa_input_report *)&data[1];
	struct input_dev *gamepad;
	uint8_t value;

	gamepad = sa->ports[report->id - 1];

	if (gamepad)
	{
		input_report_abs(gamepad, ABS_X, sa_report->x);
		input_report_abs(gamepad, ABS_Y, sa_report->y);
		input_report_abs(gamepad, ABS_RX, sa_report->rx);
		input_report_abs(gamepad, ABS_RY, sa_report->ry);

		value = sa_report->buttons[0] & BUTTONS0_HAT_SWITCH;
		if (value >= ARRAY_SIZE(sanwa_gamepad_hat_mapping))
			value = 8;
		input_report_abs(gamepad, ABS_HAT0X, sanwa_gamepad_hat_mapping[value].x);
		input_report_abs(gamepad, ABS_HAT0Y, sanwa_gamepad_hat_mapping[value].y);

		input_report_key(gamepad, BTN_X, sa_report->buttons[0] & BUTTONS0_SQUARE);
		input_report_key(gamepad, BTN_Y, sa_report->buttons[0] & BUTTONS0_TRIANGLE);
		input_report_key(gamepad, BTN_A, sa_report->buttons[0] & BUTTONS0_CROSS);
		input_report_key(gamepad, BTN_B, sa_report->buttons[0] & BUTTONS0_CIRCLE);

		input_report_key(gamepad, BTN_TL, sa_report->buttons[1] & BUTTONS1_L1);
		input_report_key(gamepad, BTN_TR, sa_report->buttons[1] & BUTTONS1_R1);
		input_report_key(gamepad, BTN_TL2, sa_report->buttons[1] & BUTTONS1_L2);
		input_report_key(gamepad, BTN_TR2, sa_report->buttons[1] & BUTTONS1_R2);
		input_report_key(gamepad, BTN_THUMBL, sa_report->buttons[1] & BUTTONS1_L3);
		input_report_key(gamepad, BTN_THUMBR, sa_report->buttons[1] & BUTTONS1_R3);
		input_report_key(gamepad, BTN_SELECT, sa_report->buttons[1] & BUTTONS1_SELECT);
		input_report_key(gamepad, BTN_START, sa_report->buttons[1] & BUTTONS1_START);

		input_sync(gamepad);
	}

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
	.remove = sanwa_remove,
	.raw_event = sanwa_raw_event,
};
module_hid_driver(sanwa_driver);

MODULE_AUTHOR("Pedro Nishiyama <nishiyama.v3@gmail.com>");
MODULE_DESCRIPTION("HID Driver for sanwa dualshock adapter.");
MODULE_LICENSE("GPL");
