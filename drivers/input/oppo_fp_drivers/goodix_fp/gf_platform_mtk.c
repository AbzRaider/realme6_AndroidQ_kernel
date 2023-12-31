/*
 * platform indepent driver interface
 *
 * Coypritht (c) 2017 Goodix
 */
#include <linux/delay.h>
#include <linux/workqueue.h>
#include <linux/of_gpio.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/timer.h>
#include <linux/err.h>
#include <soc/oppo/oppo_project.h>

#include "gf_spi_tee.h"

#if defined(USE_SPI_BUS)
#include <linux/spi/spi.h>
#include <linux/spi/spidev.h>
#elif defined(USE_PLATFORM_BUS)
#include <linux/platform_device.h>
#endif

#include <linux/of.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>

static struct pinctrl *gf_irq_pinctrl = NULL;
static struct pinctrl_state *gf_irq_no_pull = NULL;
extern struct regulator *gf_regulator;

int gf_parse_dts(struct gf_dev* gf_dev)
{
	int rc = 0;


	struct device_node *node = NULL;
	struct platform_device *pdev = NULL;
        gf_dev->cs_gpio_set = false;
        gf_dev->pinctrl = NULL;
        gf_dev->pstate_cs_func = NULL;


	node = of_find_compatible_node(NULL, NULL, "goodix,goodix-fp");
	if (node) {
		pdev = of_find_device_by_node(node);
		if (pdev) {
			gf_irq_pinctrl = devm_pinctrl_get(&pdev->dev);
			if (IS_ERR(gf_irq_pinctrl)) {
				rc = PTR_ERR(gf_irq_pinctrl);
				pr_err("%s can't find goodix fingerprint pinctrl\n", __func__);
				return rc;
			}
		} else {
			pr_err("%s platform device is null\n", __func__);
			return -1;
		}
	} else {
		pr_err("%s device node is null\n", __func__);
		return -1;
	}

        if (get_project() != 19661) {
                /*get ldo resource*/

                gf_dev->ldo_gpio = of_get_named_gpio(pdev->dev.of_node, "goodix,gpio_ldo", 0);
                if (!gpio_is_valid(gf_dev->ldo_gpio)) {
                        pr_info("LDO GPIO is invalid.\n");
                        return -1;
                }

                rc = gpio_request(gf_dev->ldo_gpio, "gpio_ldo");
                if (rc) {
                        dev_err(&gf_dev->spi->dev, "Failed to request LDO GPIO. rc = %d\n", rc);
                        return -1;
                }

                msleep(20);
                gpio_direction_output(gf_dev->ldo_gpio, 1);
                msleep(20);
                gpio_set_value(gf_dev->ldo_gpio, 1);

        gf_dev->pinctrl = devm_pinctrl_get(&pdev->dev);
        if (IS_ERR(gf_dev->pinctrl)) {
                dev_err(&pdev->dev, "can not get the gf pinctrl");
                return PTR_ERR(gf_dev->pinctrl);
        }
        gf_dev->pstate_cs_func = pinctrl_lookup_state(gf_dev->pinctrl, "gf_cs_func");
        if (IS_ERR(gf_dev->pstate_cs_func)) {
                dev_err(&pdev->dev, "Can't find gf_cs_func pinctrl state\n");
                return PTR_ERR(gf_dev->pstate_cs_func);
        }
     }

	/*get reset resource*/
	gf_dev->reset_gpio = of_get_named_gpio(pdev->dev.of_node, "goodix,gpio_reset", 0);
	if (!gpio_is_valid(gf_dev->reset_gpio)) {
		pr_info("RESET GPIO is invalid.\n");
		return -1;
	}

	rc = gpio_request(gf_dev->reset_gpio, "goodix_reset");
	if (rc) {
		dev_err(&gf_dev->spi->dev, "Failed to request RESET GPIO. rc = %d\n", rc);
		return -1;
	}

	gpio_direction_output(gf_dev->reset_gpio, 0);
	msleep(3);

    if (get_project() != 19661) {

        /*get cs resource*/
        gf_dev->cs_gpio = of_get_named_gpio(pdev->dev.of_node, "goodix,gpio_cs", 0);
        if (!gpio_is_valid(gf_dev->cs_gpio)) {
                pr_info("CS GPIO is invalid.\n");
                return -1;
        }
        rc = gpio_request(gf_dev->cs_gpio, "goodix_cs");
        if (rc) {
                dev_err(&gf_dev->spi->dev, "Failed to request CS GPIO. rc = %d\n", rc);
                return -1;
        }
        gpio_direction_output(gf_dev->cs_gpio, 0);
        gf_dev->cs_gpio_set = true;
    }

	/*get irq resourece*/
	gf_dev->irq_gpio = of_get_named_gpio(pdev->dev.of_node, "goodix,gpio_irq", 0);
	if (!gpio_is_valid(gf_dev->irq_gpio)) {
		pr_info("IRQ GPIO is invalid.\n");
		return -1;
	}

	rc = gpio_request(gf_dev->irq_gpio, "goodix_irq");
	if (rc) {
		dev_err(&gf_dev->spi->dev, "Failed to request IRQ GPIO. rc = %d\n", rc);
		return -1;
	}

	/*set irq gpio no pull*/
	gf_irq_no_pull = pinctrl_lookup_state(gf_irq_pinctrl, "goodix_irq_no_pull");
	if (IS_ERR(gf_irq_no_pull)) {
		rc = PTR_ERR(gf_irq_no_pull);
		pr_err("Cannot find goodix pinctrl gf_irq_no_pull!\n");
		return -1;
	}
	pinctrl_select_state(gf_irq_pinctrl, gf_irq_no_pull);

	gpio_direction_input(gf_dev->irq_gpio);

	return 0;
}

void gf_cleanup(struct gf_dev* gf_dev)
{
	pr_info("[info] %s\n",__func__);
	if (gpio_is_valid(gf_dev->irq_gpio))
	{
		gpio_free(gf_dev->irq_gpio);
		pr_info("remove irq_gpio success\n");
	}
        if (gpio_is_valid(gf_dev->cs_gpio)) {
                gpio_free(gf_dev->cs_gpio);
                pr_info("remove cs_gpio success\n");
        }
	if (gpio_is_valid(gf_dev->reset_gpio))
	{
		gpio_free(gf_dev->reset_gpio);
		pr_info("remove reset_gpio success\n");
	}
}

int gf_power_on(struct gf_dev* gf_dev)
{
	int rc = 0;

	if (get_project() != 19661) {
		gpio_set_value(gf_dev->ldo_gpio, 1);
		msleep(10);
	}

	pr_info("---- power on ok ----\n");

	return rc;
}

int gf_power_off(struct gf_dev* gf_dev)
{
	int rc = 0;
	pr_err("%s liangliang power off enter  \n",__func__);
	rc = regulator_disable(gf_regulator);
	pr_err("%s regulator_disable value = %d \n",__func__, rc);

	return rc;
}

int gf_hw_reset(struct gf_dev *gf_dev, unsigned int delay_ms)
{
	if(gf_dev == NULL) {
		pr_info("Input buff is NULL.\n");
		return -1;
	}

	gpio_set_value(gf_dev->reset_gpio, 0);
	mdelay(20);
	gpio_set_value(gf_dev->reset_gpio, 1);
        if (gf_dev->cs_gpio_set) {
                pr_info("---- pull CS up and set CS from gpio to func ----");
                gpio_set_value(gf_dev->cs_gpio, 1);
                pinctrl_select_state(gf_dev->pinctrl, gf_dev->pstate_cs_func);
                gf_dev->cs_gpio_set = false;
        }
	mdelay(delay_ms);
	return 0;
}

int gf_irq_num(struct gf_dev *gf_dev)
{
	if(gf_dev == NULL) {
		pr_info("Input buff is NULL.\n");
		return -1;
	} else {
		return gpio_to_irq(gf_dev->irq_gpio);
	}
}

