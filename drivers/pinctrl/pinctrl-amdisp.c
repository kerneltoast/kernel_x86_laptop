/* SPDX-License-Identifier: MIT */
/*
 * Copyright (C) 2025 Advanced Micro Devices, Inc. All rights reserved.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */

#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/gpio/driver.h>
#include <linux/pinctrl/machine.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/interrupt.h>

#include "core.h"
#include "pinctrl-utils.h"
#include "pinctrl-amd.h"
#include "pinctrl-amdisp.h"

#define GPIO_CONTROL_PIN	4
#define GPIO_OFFSET_0		0x0
#define GPIO_OFFSET_1		0x4
#define GPIO_OFFSET_2		0x50

static const u32 gpio_offset[] = {
	GPIO_OFFSET_0,
	GPIO_OFFSET_1,
	GPIO_OFFSET_2
};

struct amdisp_pinctrl_data {
	const struct pinctrl_pin_desc *pins;
	unsigned int npins;
	const struct amdisp_function *functions;
	unsigned int nfunctions;
	const struct amdisp_pingroup *groups;
	unsigned int ngroups;
};

static const struct amdisp_pinctrl_data amdisp_pinctrl_data = {
	.pins = amdisp_pins,
	.npins = ARRAY_SIZE(amdisp_pins),
	.functions = amdisp_functions,
	.nfunctions = ARRAY_SIZE(amdisp_functions),
	.groups = amdisp_groups,
	.ngroups = ARRAY_SIZE(amdisp_groups),
};

struct amdisp_pinctrl {
	struct device *dev;
	struct pinctrl_dev *pctrl;
	struct pinctrl_desc desc;
	struct pinctrl_gpio_range gpio_range;
	struct gpio_chip gc;
	const struct amdisp_pinctrl_data *data;
	void __iomem *gpiobase;
	raw_spinlock_t lock;
};

static int amdisp_get_groups_count(struct pinctrl_dev *pctldev)
{
	struct amdisp_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->data->ngroups;
}

static const char *amdisp_get_group_name(struct pinctrl_dev *pctldev,
					 unsigned int group)
{
	struct amdisp_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	return pctrl->data->groups[group].name;
}

static int amdisp_get_group_pins(struct pinctrl_dev *pctldev,
				 unsigned int group,
				 const unsigned int **pins,
				 unsigned int *num_pins)
{
	struct amdisp_pinctrl *pctrl = pinctrl_dev_get_drvdata(pctldev);

	*pins = pctrl->data->groups[group].pins;
	*num_pins = pctrl->data->groups[group].npins;
	return 0;
}

const struct pinctrl_ops amdisp_pinctrl_ops = {
	.get_groups_count	= amdisp_get_groups_count,
	.get_group_name		= amdisp_get_group_name,
	.get_group_pins		= amdisp_get_group_pins,
};

#ifdef CONFIG_GPIOLIB
static int amdisp_gpio_get_direction(struct gpio_chip *gc, unsigned int gpio)
{
	/* amdisp gpio only has output mode */
	return GPIO_LINE_DIRECTION_OUT;
}

static int amdisp_gpio_direction_input(struct gpio_chip *gc, unsigned int gpio)
{
	return -EOPNOTSUPP;
}

static int amdisp_gpio_direction_output(struct gpio_chip *gc, unsigned int gpio,
					int value)
{
	/* Nothing to do, amdisp gpio only has output mode */
	return 0;
}

static int amdisp_gpio_get(struct gpio_chip *gc, unsigned int gpio)
{
	unsigned long flags;
	u32 pin_reg;
	struct amdisp_pinctrl *pctrl = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&pctrl->lock, flags);
	pin_reg = readl(pctrl->gpiobase + gpio_offset[gpio]);
	raw_spin_unlock_irqrestore(&pctrl->lock, flags);

	return !!(pin_reg & BIT(GPIO_CONTROL_PIN));
}

static void amdisp_gpio_set(struct gpio_chip *gc, unsigned int gpio, int value)
{
	unsigned long flags;
	u32 pin_reg;
	struct amdisp_pinctrl *pctrl = gpiochip_get_data(gc);

	raw_spin_lock_irqsave(&pctrl->lock, flags);
	pin_reg = readl(pctrl->gpiobase + gpio_offset[gpio]);
	if (value)
		pin_reg |= BIT(GPIO_CONTROL_PIN);
	else
		pin_reg &= ~BIT(GPIO_CONTROL_PIN);
	writel(pin_reg, pctrl->gpiobase + gpio_offset[gpio]);
	raw_spin_unlock_irqrestore(&pctrl->lock, flags);
}

static int amdisp_gpio_set_config(struct gpio_chip *gc, unsigned int gpio,
				  unsigned long config)
{
	return -EOPNOTSUPP;
}

static int amdisp_gpiochip_add(struct platform_device *pdev,
			       struct amdisp_pinctrl *pctrl)
{
	struct gpio_chip *gc = &pctrl->gc;
	struct pinctrl_gpio_range *grange = &pctrl->gpio_range;
	int ret;

	gc->label		= dev_name(pctrl->dev);
	gc->owner		= THIS_MODULE;
	gc->parent		= &pdev->dev;
	gc->names		= amdisp_range_pins_name;
	gc->request		= gpiochip_generic_request;
	gc->free		= gpiochip_generic_free;
	gc->get_direction	= amdisp_gpio_get_direction;
	gc->direction_input	= amdisp_gpio_direction_input;
	gc->direction_output	= amdisp_gpio_direction_output;
	gc->get			= amdisp_gpio_get;
	gc->set			= amdisp_gpio_set;
	gc->set_config		= amdisp_gpio_set_config;
	gc->base		= 0;
	gc->ngpio		= ARRAY_SIZE(amdisp_range_pins);
#if defined(CONFIG_OF_GPIO)
	gc->of_node		= pdev->dev.of_node;
	gc->of_gpio_n_cells	= 2;
#endif

	grange->id		= 0;
	grange->pin_base	= 0;
	grange->base		= 0;
	grange->pins		= amdisp_range_pins;
	grange->npins		= ARRAY_SIZE(amdisp_range_pins);
	grange->name		= gc->label;
	grange->gc		= gc;

	ret = devm_gpiochip_add_data(&pdev->dev, gc, pctrl);
	if (ret)
		return ret;

	pinctrl_add_gpio_range(pctrl->pctrl, grange);

	dev_info(&pdev->dev, "register amdisp gpio controller\n");
	return 0;
}
#endif

static int amdisp_pinctrl_probe(struct platform_device *pdev)
{
	struct amdisp_pinctrl *pctrl;
	struct resource *res;
	int ret;

	pctrl = devm_kzalloc(&pdev->dev, sizeof(*pctrl), GFP_KERNEL);
	if (!pctrl)
		return -ENOMEM;

#ifdef CONFIG_GPIOLIB
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (IS_ERR(res))
		return PTR_ERR(res);

	pctrl->gpiobase = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(pctrl->gpiobase))
		return PTR_ERR(pctrl->gpiobase);
#endif
	platform_set_drvdata(pdev, pctrl);

	pctrl->dev = &pdev->dev;
	pctrl->data = &amdisp_pinctrl_data;
	pctrl->desc.owner = THIS_MODULE;
	pctrl->desc.pctlops = &amdisp_pinctrl_ops;
	pctrl->desc.pmxops = NULL;
	pctrl->desc.name = dev_name(&pdev->dev);
	pctrl->desc.pins = pctrl->data->pins;
	pctrl->desc.npins = pctrl->data->npins;
	ret = devm_pinctrl_register_and_init(&pdev->dev, &pctrl->desc,
					     pctrl, &pctrl->pctrl);
	if (ret)
		return ret;

	ret = pinctrl_enable(pctrl->pctrl);
	if (ret)
		return ret;

#ifdef CONFIG_GPIOLIB
	ret = amdisp_gpiochip_add(pdev, pctrl);
	if (ret)
		return ret;
#endif
	dev_info(&pdev->dev, "amdisp pinctrl init successful\n");
	return 0;
}

static struct platform_driver amdisp_pinctrl_driver = {
	.driver = {
		.name = "amdisp-pinctrl",
	},
	.probe = amdisp_pinctrl_probe,
};

static int __init amdisp_pinctrl_init(void)
{
	return platform_driver_register(&amdisp_pinctrl_driver);
}
arch_initcall(amdisp_pinctrl_init);

static void __exit amdisp_pinctrl_exit(void)
{
	platform_driver_unregister(&amdisp_pinctrl_driver);
}
module_exit(amdisp_pinctrl_exit);

MODULE_DESCRIPTION("AMDISP pinctrl driver");
MODULE_AUTHOR("Benjamin Chan <benjamin.chan@amd.com>");
MODULE_LICENSE("GPL and additional rights");
