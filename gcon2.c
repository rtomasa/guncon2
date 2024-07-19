// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for Namco GunCon 2 USB light gun
 * Copyright (C) 2019-2021 beardypig <beardypig@protonmail.com>
 *
 * Based largely on the PXRC driver by Marcus Folkesson <marcus.folkesson@gmail.com>
 *
 * Modified by (RTA) <ruben.tomas.alonso@gmail.com>
 */
#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/usb.h>
#include <linux/usb/input.h>

#define NAMCO_VENDOR_ID 0x0b9a
#define GUNCON2_PRODUCT_ID 0x016a

#define GUNCON2_DPAD_LEFT BIT(15)
#define GUNCON2_DPAD_RIGHT BIT(13)
#define GUNCON2_DPAD_UP BIT(12)
#define GUNCON2_DPAD_DOWN BIT(14)
#define GUNCON2_TRIGGER BIT(5)
#define GUNCON2_BTN_A BIT(11)
#define GUNCON2_BTN_B BIT(10)
#define GUNCON2_BTN_C BIT(9)
#define GUNCON2_BTN_START BIT(7)
#define GUNCON2_BTN_SELECT BIT(6)

// default calibration, can be updated with evdev-joystick
#define ABS_X_MIN 155
#define ABS_X_MAX 725
#define ABS_Y_MIN 5
#define ABS_Y_MAX 239
#define ABS_FUZZ 1
#define ABS_X_FLAT 0
#define ABS_Y_FLAT 5

struct guncon2
{
    struct input_dev *input_device;
    struct usb_interface *intf;
    struct urb *urb;
    struct mutex pm_mutex;
    bool is_open;
    char phys[64];
};

struct gc_mode
{
    unsigned short a;
    unsigned char b;
    unsigned char c;
    unsigned char d;
    unsigned char mode;
};

static void guncon2_usb_irq(struct urb *urb)
{
    struct guncon2 *guncon2 = urb->context;
    unsigned char *data = urb->transfer_buffer;
    int error, buttons;
    unsigned short x, y;

    switch (urb->status)
    {
    case 0:
        /* success */
        break;
    case -ETIME:
        /* this urb is timing out */
        dev_dbg(&guncon2->intf->dev,
                "%s - urb timed out - was the device unplugged?\n",
                __func__);
        return;
    case -ECONNRESET:
    case -ENOENT:
    case -ESHUTDOWN:
    case -EPIPE:
        /* this urb is terminated, clean up */
        dev_dbg(&guncon2->intf->dev, "%s - urb shutting down with status: %d\n",
                __func__, urb->status);
        return;
    default:
        dev_dbg(&guncon2->intf->dev, "%s - nonzero urb status received: %d\n",
                __func__, urb->status);
        goto exit;
    }

    if (urb->actual_length == 6)
    {
        /* Aiming */
        x = (data[3] << 8) | data[2];
        y = data[4];

        input_report_abs(guncon2->input_device, ABS_X, x);
        input_report_abs(guncon2->input_device, ABS_Y, y);

        /* Buttons */
        buttons = ((data[0] << 8) | data[1]) ^ 0xffff;        

        /* Joy */
        input_report_key(guncon2->input_device, BTN_DPAD_UP, buttons & GUNCON2_DPAD_UP);
        input_report_key(guncon2->input_device, BTN_DPAD_DOWN, buttons & GUNCON2_DPAD_DOWN);
        input_report_key(guncon2->input_device, BTN_DPAD_LEFT, buttons & GUNCON2_DPAD_LEFT);
        input_report_key(guncon2->input_device, BTN_DPAD_RIGHT, buttons & GUNCON2_DPAD_RIGHT);
        input_report_key(guncon2->input_device, BTN_A, buttons & GUNCON2_BTN_A);
        input_report_key(guncon2->input_device, BTN_B, buttons & GUNCON2_BTN_B);
        input_report_key(guncon2->input_device, BTN_C, buttons & GUNCON2_BTN_C);
        input_report_key(guncon2->input_device, BTN_START, buttons & GUNCON2_BTN_START);
        input_report_key(guncon2->input_device, BTN_SELECT, buttons & GUNCON2_BTN_SELECT);
        /* Mouse */
        input_report_key(guncon2->input_device, BTN_LEFT, buttons & GUNCON2_TRIGGER);
        input_report_key(guncon2->input_device, BTN_RIGHT, buttons & GUNCON2_BTN_A || buttons & GUNCON2_BTN_C);
        input_report_key(guncon2->input_device, BTN_MIDDLE, buttons & GUNCON2_BTN_B);

        input_sync(guncon2->input_device);
    }

exit:
    /* Resubmit to fetch new fresh URBs */
    error = usb_submit_urb(urb, GFP_ATOMIC);
    if (error && error != -EPERM)
        dev_err(&guncon2->intf->dev,
                "%s - usb_submit_urb failed with result: %d",
                __func__, error);
}

static int guncon2_open(struct input_dev *input)
{
    unsigned char *gmode;
    struct guncon2 *guncon2 = input_get_drvdata(input);
    struct usb_device *usb_dev = interface_to_usbdev(guncon2->intf);
    int retval;
    mutex_lock(&guncon2->pm_mutex);

    gmode = kzalloc(6, GFP_KERNEL);
    if (!gmode)
        return -ENOMEM;

    /* set the mode to normal 50Hz mode */
    gmode[5] = 1;
    usb_control_msg(usb_dev, usb_sndctrlpipe(usb_dev, 0),
                    0x09, 0x21, 0x200, 0, gmode, 6, 100000);

    kfree(gmode);

    retval = usb_submit_urb(guncon2->urb, GFP_KERNEL);
    if (retval)
    {
        dev_err(&guncon2->intf->dev,
                "%s - usb_submit_urb failed, error: %d\n",
                __func__, retval);
        retval = -EIO;
        goto out;
    }

    guncon2->is_open = true;

out:
    mutex_unlock(&guncon2->pm_mutex);
    return retval;
}

static void guncon2_close(struct input_dev *input)
{
    struct guncon2 *guncon2 = input_get_drvdata(input);
    mutex_lock(&guncon2->pm_mutex);
    usb_kill_urb(guncon2->urb);
    guncon2->is_open = false;
    mutex_unlock(&guncon2->pm_mutex);
}

static void guncon2_free_urb(void *context)
{
    struct guncon2 *guncon2 = context;

    usb_free_urb(guncon2->urb);
}

static int guncon2_probe(struct usb_interface *intf,
                         const struct usb_device_id *id)
{
    struct usb_device *udev = interface_to_usbdev(intf);
    struct guncon2 *gcon2_joy;
    struct guncon2 *gcon2_aim;
    struct usb_endpoint_descriptor *epirq;
    size_t xfer_size;
    void *xfer_buf;
    int error;

    /*
     * Locate the endpoint information. This device only has an
     * interrupt endpoint.
     */
    error = usb_find_common_endpoints(intf->cur_altsetting,
                                      NULL, NULL, &epirq, NULL);
    if (error)
    {
        dev_err(&intf->dev, "Could not find endpoint\n");
        return error;
    }

    /* Allocate memory for the guncon2 struct using devm */
    gcon2_joy = devm_kzalloc(&intf->dev, sizeof(*gcon2_joy), GFP_KERNEL);
    if (!gcon2_joy)
        return -ENOMEM;
    gcon2_aim = devm_kzalloc(&intf->dev, sizeof(*gcon2_aim), GFP_KERNEL);
    if (!gcon2_aim)
        return -ENOMEM;

    mutex_init(&gcon2_joy->pm_mutex);
    gcon2_joy->intf = intf;

    usb_set_intfdata(gcon2_joy->intf, gcon2_joy);

    xfer_size = usb_endpoint_maxp(epirq);
    xfer_buf = devm_kmalloc(&intf->dev, xfer_size, GFP_KERNEL);
    if (!xfer_buf)
        return -ENOMEM;

    mutex_init(&gcon2_aim->pm_mutex);
    gcon2_aim->intf = intf;

    usb_set_intfdata(gcon2_aim->intf, gcon2_aim);

    xfer_size = usb_endpoint_maxp(epirq);
    xfer_buf = devm_kmalloc(&intf->dev, xfer_size, GFP_KERNEL);
    if (!xfer_buf)
        return -ENOMEM;

    gcon2_joy->urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!gcon2_joy->urb)
        return -ENOMEM;

    error = devm_add_action_or_reset(&intf->dev, guncon2_free_urb, gcon2_aim);
    if (error)
        return error;

    error = devm_add_action_or_reset(&intf->dev, guncon2_free_urb, gcon2_joy);
    if (error)
        return error;

    gcon2_aim->urb = usb_alloc_urb(0, GFP_KERNEL);
    if (!gcon2_aim->urb)
        return -ENOMEM;

    error = devm_add_action_or_reset(&intf->dev, guncon2_free_urb, gcon2_aim);
    if (error)
        return error;

    /* set to URB for the interrupt interface  */
    usb_fill_int_urb(gcon2_joy->urb, udev,
                     usb_rcvintpipe(udev, epirq->bEndpointAddress),
                     xfer_buf, xfer_size, guncon2_usb_irq, gcon2_joy, 1);

    usb_fill_int_urb(gcon2_aim->urb, udev,
                     usb_rcvintpipe(udev, epirq->bEndpointAddress),
                     xfer_buf, xfer_size, guncon2_usb_irq, gcon2_aim, 1);

    /* get path tree for the usb device */
    usb_make_path(udev, gcon2_joy->phys, sizeof(gcon2_joy->phys));
    strlcat(gcon2_joy->phys, "/gcon2/usb/joy", sizeof(gcon2_joy->phys));

    usb_make_path(udev, gcon2_aim->phys, sizeof(gcon2_aim->phys));
    strlcat(gcon2_aim->phys, "/gcon2/usb/aim", sizeof(gcon2_aim->phys));

    /* Button related */
    gcon2_joy->input_device = devm_input_allocate_device(&intf->dev);
    if (!gcon2_joy->input_device)
    {
        dev_err(&intf->dev, "couldn't allocate input_device input device\n");
        return -ENOMEM;
    }

    gcon2_aim->input_device = devm_input_allocate_device(&intf->dev);
    if (!gcon2_aim->input_device)
    {
        dev_err(&intf->dev, "couldn't allocate input_device input device\n");
        return -ENOMEM;
    }

    /* Aiming */
    gcon2_aim->input_device->name = "Namco GunCon 2 (Aim)";
    gcon2_aim->input_device->phys = gcon2_aim->phys;
    gcon2_aim->input_device->open = guncon2_open;
    gcon2_aim->input_device->close = guncon2_close;
    usb_to_input_id(udev, &gcon2_aim->input_device->id);

    input_set_capability(gcon2_aim->input_device, EV_KEY, BTN_LEFT);
    input_set_capability(gcon2_aim->input_device, EV_KEY, BTN_RIGHT);
    input_set_capability(gcon2_aim->input_device, EV_KEY, BTN_MIDDLE);
    input_set_capability(gcon2_aim->input_device, EV_ABS, ABS_X);
    input_set_capability(gcon2_aim->input_device, EV_ABS, ABS_Y);

    input_set_abs_params(gcon2_aim->input_device, ABS_X, ABS_X_MIN, ABS_X_MAX, ABS_FUZZ, ABS_X_FLAT);
    input_set_abs_params(gcon2_aim->input_device, ABS_Y, ABS_Y_MIN, ABS_Y_MAX, ABS_FUZZ, ABS_Y_FLAT);

    input_set_drvdata(gcon2_aim->input_device, gcon2_aim);

    error = input_register_device(gcon2_aim->input_device);
    if (error)
        return error;

    /* Joystick */
    gcon2_joy->input_device->name = "Namco GunCon 2 (Joy)";
    gcon2_joy->input_device->phys = gcon2_joy->phys;
    gcon2_joy->input_device->open = guncon2_open;
    gcon2_joy->input_device->close = guncon2_close;
    usb_to_input_id(udev, &gcon2_joy->input_device->id);

    input_set_capability(gcon2_joy->input_device, EV_KEY, BTN_DPAD_UP);
    input_set_capability(gcon2_joy->input_device, EV_KEY, BTN_DPAD_DOWN);
    input_set_capability(gcon2_joy->input_device, EV_KEY, BTN_DPAD_LEFT);
    input_set_capability(gcon2_joy->input_device, EV_KEY, BTN_DPAD_RIGHT);
    input_set_capability(gcon2_joy->input_device, EV_KEY, BTN_A);
    input_set_capability(gcon2_joy->input_device, EV_KEY, BTN_B);
    input_set_capability(gcon2_joy->input_device, EV_KEY, BTN_C);
    input_set_capability(gcon2_joy->input_device, EV_KEY, BTN_START);
    input_set_capability(gcon2_joy->input_device, EV_KEY, BTN_SELECT);


    input_set_drvdata(gcon2_joy->input_device, gcon2_joy);

    error = input_register_device(gcon2_joy->input_device);
    if (error)
        return error;

    return 0;
}

static void guncon2_disconnect(struct usb_interface *intf)
{
    /* All driver resources are devm-managed. */
}

static int guncon2_suspend(struct usb_interface *intf, pm_message_t message)
{
    struct guncon2 *guncon2 = usb_get_intfdata(intf);

    mutex_lock(&guncon2->pm_mutex);
    if (guncon2->is_open)
    {
        usb_kill_urb(guncon2->urb);
    }
    mutex_unlock(&guncon2->pm_mutex);

    return 0;
}

static int guncon2_resume(struct usb_interface *intf)
{
    struct guncon2 *guncon2 = usb_get_intfdata(intf);
    int retval = 0;

    mutex_lock(&guncon2->pm_mutex);
    if (guncon2->is_open && usb_submit_urb(guncon2->urb, GFP_KERNEL) < 0)
    {
        retval = -EIO;
    }

    mutex_unlock(&guncon2->pm_mutex);
    return retval;
}

static int guncon2_pre_reset(struct usb_interface *intf)
{
    struct guncon2 *guncon2 = usb_get_intfdata(intf);

    mutex_lock(&guncon2->pm_mutex);
    usb_kill_urb(guncon2->urb);
    return 0;
}

static int guncon2_post_reset(struct usb_interface *intf)
{
    struct guncon2 *guncon2 = usb_get_intfdata(intf);
    int retval = 0;

    if (guncon2->is_open && usb_submit_urb(guncon2->urb, GFP_KERNEL) < 0)
    {
        retval = -EIO;
    }

    mutex_unlock(&guncon2->pm_mutex);

    return retval;
}

static int guncon2_reset_resume(struct usb_interface *intf)
{
    return guncon2_resume(intf);
}

static const struct usb_device_id guncon2_table[] = {
    {USB_DEVICE(NAMCO_VENDOR_ID, GUNCON2_PRODUCT_ID)},
    {}};

MODULE_DEVICE_TABLE(usb, guncon2_table);

static struct usb_driver guncon2_driver = {
    .name = "guncon2",
    .probe = guncon2_probe,
    .disconnect = guncon2_disconnect,
    .id_table = guncon2_table,
    .suspend = guncon2_suspend,
    .resume = guncon2_resume,
    .pre_reset = guncon2_pre_reset,
    .post_reset = guncon2_post_reset,
    .reset_resume = guncon2_reset_resume,
};

module_usb_driver(guncon2_driver);

MODULE_AUTHOR("beardypig <beardypig@protonmail.com>, (RTA) <ruben.tomas.alonso@gmail.com>");
MODULE_DESCRIPTION("Namco GunCon 2");
MODULE_LICENSE("GPL v2");
