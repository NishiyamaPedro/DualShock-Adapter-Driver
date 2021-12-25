// SPDX-License-Identifier: GPL-3.0-or-later
/*
 * Copyright (c) 2021 Pedro Nishiyama <nishiyama.v3@gmail.com>
 * Based on hid-playstation driver
 * 
 * TO-DO
 * - Implement Force Feedback
 * 
 */

#include <linux/input.h>
#include <linux/hid.h>
#include <linux/module.h>

#define BUTTONS0_HAT_SWITCH	GENMASK(3, 0)
#define BUTTONS0_CROSS		BIT(4)
#define BUTTONS0_CIRCLE		BIT(5)
#define BUTTONS0_SQUARE		BIT(6)
#define BUTTONS0_TRIANGLE	BIT(7)
#define BUTTONS1_L1			BIT(0)
#define BUTTONS1_L2			BIT(1)
#define BUTTONS1_R1			BIT(2)
#define BUTTONS1_R2			BIT(3)
#define BUTTONS1_SELECT		BIT(4)
#define BUTTONS1_START		BIT(5)
#define BUTTONS1_L3			BIT(6)
#define BUTTONS1_R3			BIT(7)

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

struct sanwa_adapter {
	struct input_dev *port_one;
	struct input_dev *port_two;
};

struct sanwa_input_report {
	uint8_t rz, z; // Right Y and X axis
	uint8_t x, y; // Left X and Y axis
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
	//	TO-DO
	return 0;
}

static void sanwa_set_capabilities(struct input_dev *port)
{
	unsigned int i;

	input_set_abs_params(port, ABS_X, 0, 255, 0, 0);
	input_set_abs_params(port, ABS_Y, 0, 255, 0, 0);
	input_set_abs_params(port, ABS_Z, 0, 255, 0, 0);
	input_set_abs_params(port, ABS_RZ, 0, 255, 0, 0);

	input_set_abs_params(port, ABS_HAT0X, -1, 1, 0, 0);
	input_set_abs_params(port, ABS_HAT0Y, -1, 1, 0, 0);

	for (i = 0; i < ARRAY_SIZE(gamepad_buttons); i++)
		input_set_capability(port, EV_KEY, gamepad_buttons[i]);

	input_set_capability(port, EV_FF, FF_RUMBLE);
	input_ff_create_memless(port, NULL, sanwa_ff_play);
}

static int sanwa_create_inputs(struct hid_device *hdev)
{
	struct sanwa_adapter *sa;
	int ret;

	sa = devm_kzalloc(&hdev->dev, sizeof(*sa), GFP_KERNEL);
	if (!sa)
		return 1;

	hid_set_drvdata(hdev, sa);

	hid_err(hdev, "creating port 1\n");
	sa->port_one = sanwa_allocate_input_dev(hdev, "Port 1");
	sanwa_set_capabilities(sa->port_one);
	ret = input_register_device(sa->port_one);
	if (ret)
		return ret;

	hid_err(hdev, "creating port 2\n");
	sa->port_two = sanwa_allocate_input_dev(hdev, "Port 2");
	sanwa_set_capabilities(sa->port_two);
	ret = input_register_device(sa->port_two);
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
	struct sanwa_adapter *sa = hid_get_drvdata(hdev);
	struct sanwa_input_report *sa_report = (struct sanwa_input_report *)&data[1];
	struct input_dev *gamepad;
	uint8_t value;
	
	if (report->id == 1)
		gamepad = sa->port_one;
	else if (report->id == 2)
		gamepad = sa->port_two;

	if (gamepad) {
		input_report_abs(gamepad, ABS_X,  sa_report->x);
		input_report_abs(gamepad, ABS_Y,  sa_report->y);
		input_report_abs(gamepad, ABS_Z,  sa_report->z);
		input_report_abs(gamepad, ABS_RZ, sa_report->rz);
		
		value = sa_report->buttons[0] & BUTTONS0_HAT_SWITCH;
		if (value >= ARRAY_SIZE(sanwa_gamepad_hat_mapping))
			value = 8;
		input_report_abs(gamepad, ABS_HAT0X, sanwa_gamepad_hat_mapping[value].x);
		input_report_abs(gamepad, ABS_HAT0Y, sanwa_gamepad_hat_mapping[value].y);

		// *************************
		//  BTN_WEST and BTN_NORTH may appear swapped in some applications due to confusion with the button codes.
		//  In input-event-codes.h they appear as the layout for Nintendo controllers. 
		//  Strangely, the A and B buttons appear on the xbox layout.
		// 
		//  #define BTN_NORTH		0x133
		//	#define BTN_X			BTN_NORTH
		//	#define BTN_WEST		0x134
		//	#define BTN_Y			BTN_WEST
		//
		//  I decide to use them swapped since in most cases they are treated as the xbox layout.
		input_report_key(gamepad, BTN_X,   	  sa_report->buttons[0] & BUTTONS0_SQUARE);
		input_report_key(gamepad, BTN_Y,  	  sa_report->buttons[0] & BUTTONS0_TRIANGLE);
		// *************************

		input_report_key(gamepad, BTN_A,	  sa_report->buttons[0] & BUTTONS0_CROSS);
		input_report_key(gamepad, BTN_B,	  sa_report->buttons[0] & BUTTONS0_CIRCLE);
		
		input_report_key(gamepad, BTN_TL,     sa_report->buttons[1] & BUTTONS1_L1);
		input_report_key(gamepad, BTN_TR,     sa_report->buttons[1] & BUTTONS1_R1);
		input_report_key(gamepad, BTN_TL2,    sa_report->buttons[1] & BUTTONS1_L2);
		input_report_key(gamepad, BTN_TR2,    sa_report->buttons[1] & BUTTONS1_R2);
		input_report_key(gamepad, BTN_THUMBL, sa_report->buttons[1] & BUTTONS1_L3);
		input_report_key(gamepad, BTN_THUMBR, sa_report->buttons[1] & BUTTONS1_R3);	
		input_report_key(gamepad, BTN_SELECT, sa_report->buttons[1] & BUTTONS1_SELECT);
		input_report_key(gamepad, BTN_START,  sa_report->buttons[1] & BUTTONS1_START);
	
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
	.raw_event = sanwa_raw_event,
};
module_hid_driver(sanwa_driver);

MODULE_AUTHOR("Pedro Nishiyama <nishiyama.v3@gmail.com>");
MODULE_DESCRIPTION("HID Driver for sanwa dualshock adapter.");
MODULE_LICENSE("GPL");
