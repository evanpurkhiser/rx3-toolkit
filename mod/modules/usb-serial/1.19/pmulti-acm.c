/*
 * multi.c -- Multifunction Composite driver
 *
 * Copyright (C) 2008 David Brownell
 * Copyright (C) 2008 Nokia Corporation
 * Copyright (C) 2009 Samsung Electronics
 * Author: Michal Nazarewicz (m.nazarewicz@samsung.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#if 0
#define DEBUG
#define VERBOSE_DEBUG
#endif
#undef CONFIG_TRACING


#include <linux/kernel.h>
#include <linux/utsname.h>
#include <linux/module.h>


#define DRIVER_DESC		"Multifunction Composite Gadget"

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Michal Nazarewicz");
MODULE_LICENSE("GPL");

#define MULTI_VENDOR_NUM	0x2b73	/* AlphaTheta */
#define MULTI_PRODUCT_NUM	0x003d	/* XDJ-RX3 */
#define CONFIG_XDJ		1

/***************************** All the files... *****************************/

/*
 * kbuild is not very cooperative with respect to linking separately
 * compiled library objects into one module.  So for now we won't use
 * separate compilation ... ensuring init/exit sections work to shrink
 * the runtime footprint, and giving us at least some parts of what
 * a "gcc --combine ... part1.c part2.c part3.c ... " build would.
 */

#ifdef CONFIG_PDJ
#include "p_descrip.c"
#endif
#include "composite.c"
#include "usbstring.c"
#include "config.c"
#include "epautoconf.c"
#include "f_acm.c"
#include "u_serial.c"


#if defined(CONFIG_USB_G_MULTI_PDJ_AUDIO) || defined(CONFIG_USB_G_MULTI_PDJ_MIDI)
/* Declare ac_header_descriptor here,
 * because of conflicting between f_pmidi.c and f_audio.c
 */
#include <linux/usb/audio.h>
DECLARE_UAC_AC_HEADER_DESCRIPTOR(1);

#endif

#if defined(CONFIG_USB_G_MULTI_PDJ_MIDI)
#include "f_xmidi.c"
#endif


#if defined(CONFIG_USB_G_MULTI_PDJ_AUDIO)
#include "f_paudio.c"
#endif

#if defined(CONFIG_USB_G_MULTI_PDJ_HID)
#include <linux/platform_device.h>
#include <linux/list.h>

#include "f_hid.c"

struct hidg_func_node {
	struct list_head node;
	struct hidg_func_descriptor *func;
};
static LIST_HEAD(hidg_func_list);
#endif

/***************************** Device Descriptor ****************************/
#ifdef CONFIG_PDJ_VENDOR_SPEC
static struct usb_device_descriptor device_desc = {
	.bLength =		0x12,
	.bDescriptorType =	USB_DT_DEVICE,
	.bcdUSB =		cpu_to_le16(0x0200),
	.bDeviceClass =		0,
	.bDeviceSubClass =	0,
	.bDeviceProtocol =	0,
	.bMaxPacketSize0 =	0x40,
	.idVendor =		cpu_to_le16(MULTI_VENDOR_NUM),
	.idProduct =		cpu_to_le16(MULTI_PRODUCT_NUM),
	.bcdDevice = 		cpu_to_le16(0x0200),
	.iManufacturer = 	0x01,
	.iProduct = 		0x02,
	.iSerialNumber = 	0,
	.bNumConfigurations =	1,
};
#else

static struct usb_device_descriptor device_desc = {
	.bLength =		sizeof device_desc,
	.bDescriptorType =	USB_DT_DEVICE,

	.bcdUSB =		cpu_to_le16(0x0200),

	.bDeviceClass =		USB_CLASS_MISC /* 0xEF */,
	.bDeviceSubClass =	2,
	.bDeviceProtocol =	1,

	/* Vendor and product id can be overridden by module parameters.  */
	.idVendor =		cpu_to_le16(MULTI_VENDOR_NUM),
	.idProduct =		cpu_to_le16(MULTI_PRODUCT_NUM),
};
#endif


static const struct usb_descriptor_header *otg_desc[] = {
	(struct usb_descriptor_header *) &(struct usb_otg_descriptor){
		.bLength =		sizeof(struct usb_otg_descriptor),
		.bDescriptorType =	USB_DT_OTG,

		/*
		 * REVISIT SRP-only hardware is possible, although
		 * it would not be called "OTG" ...
		 */
		.bmAttributes =		USB_OTG_SRP | USB_OTG_HNP,
	},
	NULL,
};


/* string IDs are assigned dynamically */

#define STRING_MANUFACTURER_IDX		0
#define STRING_PRODUCT_IDX		1

static char manufacturer[50];

static struct usb_string strings_dev[] = {
	[STRING_MANUFACTURER_IDX].s = manufacturer,
	[STRING_PRODUCT_IDX].s = DRIVER_DESC,
	{  } /* end of list */
};

static struct usb_gadget_strings *dev_strings[] = {
	&(struct usb_gadget_strings){
		.language	= 0x0409,	/* en-us */
		.strings	= strings_dev,
	},
	NULL,
};




/****************************** Configurations ******************************/
static __init int djctrl_do_config(struct usb_configuration *c)
{
	int ret;

	if (gadget_is_otg(c->cdev->gadget)) {
		c->descriptors = otg_desc;
		c->bmAttributes |= USB_CONFIG_ATT_WAKEUP;
	}
#ifdef CONFIG_USB_G_MULTI_PDJ_AUDIO
	ret = paudio_bind_config(c);
	if (ret < 0)
		return ret;
#endif

#ifdef CONFIG_USB_G_MULTI_PDJ_MIDI
	ret = midi_bind_config(c);
	if (ret < 0)
		return ret;
#endif

#ifdef CONFIG_USB_G_MULTI_PDJ_HID
	{
	struct hidg_func_node *e;
	int func = 0;

	list_for_each_entry(e, &hidg_func_list, node) {
		ret = hidg_bind_config(c, e->func, func++);
		if (ret)
			break;
	}
	if (ret < 0)
		return ret;
	}
#endif


	c->next_interface_id = USBF_IFNUM_AUDIO_STRM_IN + 1;
	ret = acm_bind_config(c, 0);
	if (ret < 0)
		return ret;

	return 0;
}

static int djctrl_config_register(struct usb_composite_dev *cdev)
{
	static struct usb_configuration config = {
#ifdef CONFIG_PDJ_VENDOR_SPEC
		.bConfigurationValue	= 1,
#else
		.bConfigurationValue	= 2,
#endif
		.bmAttributes		= USB_CONFIG_ATT_SELFPOWER,
	};

	config.label = "Multifunction Composite";

	return usb_add_config(cdev, &config, djctrl_do_config);
}


/****************************** Gadget Bind ******************************/


static int __ref multi_bind(struct usb_composite_dev *cdev)
{
	struct usb_gadget *gadget = cdev->gadget;
	int status;
#ifdef CONFIG_USB_G_MULTI_PDJ_HID
	struct list_head *tmp;
	int funcs = 0;
#endif

	status = gserial_setup(gadget, 1);
	if (status < 0)
		return status;

#ifdef CONFIG_USB_G_MULTI_PDJ_AUDIO
	// Setup has already finished by config.
#endif

#ifdef CONFIG_USB_G_MULTI_PDJ_MIDI
	// Setup has already finished by config.
#endif

#ifdef CONFIG_USB_G_MULTI_PDJ_HID
	list_for_each(tmp, &hidg_func_list)
		funcs++;

	if (!funcs) {
		status = -ENODEV;
		goto fail_serial;
	}

	/* set up HID */
	status = ghid_setup(cdev->gadget, funcs);
	if (status < 0)
		//return status;
		goto fail0;

#else
	int gcnum;

	gcnum = usb_gadget_controller_number(gadget);
	if (gcnum >= 0)
		device_desc.bcdDevice = cpu_to_le16(0x0300 | gcnum);
	else {
		/* We assume that can_support_ecm() tells the truth;
		 * but if the controller isn't recognized at all then
		 * that assumption is a bit more likely to be wrong.
		 */
		WARNING(cdev, "controller '%s' not recognized\n",
		        gadget->name);
		device_desc.bcdDevice = cpu_to_le16(0x0300 | 0x0099);
	}
#endif


	/* Allocate string descriptor numbers ... note that string
	 * contents can be overridden by the composite_dev glue.
	 */

	/* device descriptor strings: manufacturer, product */
	snprintf(manufacturer, sizeof manufacturer, "%s %s with %s",
	         init_utsname()->sysname, init_utsname()->release,
	         gadget->name);
	status = usb_string_id(cdev);
	if (status < 0)
		goto fail1;
	strings_dev[STRING_MANUFACTURER_IDX].id = status;
#ifndef CONFIG_PDJ_VENDOR_SPEC
	device_desc.iManufacturer = status;
#endif

	status = usb_string_id(cdev);
	if (status < 0)
		goto fail1;
	strings_dev[STRING_PRODUCT_IDX].id = status;
#ifndef CONFIG_PDJ_VENDOR_SPEC
	device_desc.iProduct = status;
#endif

	/* register our first configuration */
	status = djctrl_config_register(cdev);
	if (unlikely(status < 0))
		goto fail1;

	/* we're done */
	dev_info(&gadget->dev, DRIVER_DESC "\n");
	return 0;

	/* error recovery */
fail1:
#ifdef CONFIG_USB_G_MULTI_PDJ_HID
	ghid_cleanup();
fail0:
#endif
#ifdef CONFIG_USB_G_MULTI_PDJ_AUDIO
	gaudio_cleanup();
#endif
fail_serial:
	gserial_cleanup();

	return status;
}

static int __exit multi_unbind(struct usb_composite_dev *cdev)
{
	gserial_cleanup();
#ifdef CONFIG_USB_G_MULTI_PDJ_AUDIO
	gaudio_cleanup();
#endif

#ifdef CONFIG_USB_G_MULTI_PDJ_MIDI
	//gmidi_cleanup();
#endif

#ifdef CONFIG_USB_G_MULTI_PDJ_HID
	ghid_cleanup();
#endif

	return 0;
}

#ifdef CONFIG_USB_G_MULTI_PDJ_HID
static int __init hidg_plat_driver_probe(struct platform_device *pdev)
{
	struct hidg_func_descriptor *func = pdev->dev.platform_data;
	struct hidg_func_node *entry;

	if (!func) {
		dev_err(&pdev->dev, "Platform data missing\n");
		return -ENODEV;
	}

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->func = func;
	list_add_tail(&entry->node, &hidg_func_list);

	return 0;
}

static int __devexit hidg_plat_driver_remove(struct platform_device *pdev)
{
	struct hidg_func_node *e, *n;

	list_for_each_entry_safe(e, n, &hidg_func_list, node) {
		list_del(&e->node);
		kfree(e);
	}

	return 0;
}
#endif

/****************************** Some noise ******************************/


static struct usb_composite_driver multi_driver = {
	.name		= "g_multi_pdj",
	.dev		= &device_desc,
	.strings	= dev_strings,
	.unbind		= __exit_p(multi_unbind),
};

#ifdef CONFIG_USB_G_MULTI_PDJ_HID
static struct platform_driver hidg_plat_driver = {
	.remove		= __devexit_p(hidg_plat_driver_remove),
	.driver		= {
		.owner	= THIS_MODULE,
		.name	= "hidg",
	},
};
#endif


static int __init multi_init(void)
{
	int status;

#ifdef CONFIG_USB_G_MULTI_PDJ_HID
	status = platform_driver_probe(&hidg_plat_driver,
				hidg_plat_driver_probe);
	if (status < 0)
		return status;
#endif

	status = usb_composite_probe(&multi_driver, multi_bind);

#ifdef CONFIG_USB_G_MULTI_PDJ_HID
	if (status < 0)
		platform_driver_unregister(&hidg_plat_driver);
#endif
	return status;
}
module_init(multi_init);

static void __exit multi_exit(void)
{
#ifdef CONFIG_USB_G_MULTI_PDJ_HID
	platform_driver_unregister(&hidg_plat_driver);
#endif
	usb_composite_unregister(&multi_driver);
}
module_exit(multi_exit);
