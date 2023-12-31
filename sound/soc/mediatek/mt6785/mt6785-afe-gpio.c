// SPDX-License-Identifier: GPL-2.0
//
// mt6785-afe-gpio.c  --  Mediatek 6785 afe gpio ctrl
//
// Copyright (c) 2018 MediaTek Inc.
// Author: Eason Yen <eason.yen@mediatek.com>

#include <linux/gpio.h>
#include <linux/pinctrl/consumer.h>

#include "mt6785-afe-common.h"
#include "mt6785-afe-gpio.h"

struct pinctrl *aud_pinctrl;

enum mt6785_afe_gpio {
	MT6785_AFE_GPIO_DAT_MISO_OFF,
	MT6785_AFE_GPIO_DAT_MISO_ON,
	MT6785_AFE_GPIO_DAT_MOSI_OFF,
	MT6785_AFE_GPIO_DAT_MOSI_ON,
	MT6785_AFE_GPIO_DAT_MISO_CH34_OFF,
	MT6785_AFE_GPIO_DAT_MISO_CH34_ON,
	MT6785_AFE_GPIO_DAT_MOSI_CH34_OFF,
	MT6785_AFE_GPIO_DAT_MOSI_CH34_ON,
	MT6785_AFE_GPIO_I2S0_OFF,
	MT6785_AFE_GPIO_I2S0_ON,
	MT6785_AFE_GPIO_I2S1_OFF,
	MT6785_AFE_GPIO_I2S1_ON,
	MT6785_AFE_GPIO_I2S2_OFF,
	MT6785_AFE_GPIO_I2S2_ON,
	MT6785_AFE_GPIO_I2S3_OFF,
	MT6785_AFE_GPIO_I2S3_ON,
	MT6785_AFE_GPIO_I2S5_OFF,
	MT6785_AFE_GPIO_I2S5_ON,
	MT6785_AFE_GPIO_VOW_DAT_OFF,
	MT6785_AFE_GPIO_VOW_DAT_ON,
	MT6785_AFE_GPIO_VOW_CLK_OFF,
	MT6785_AFE_GPIO_VOW_CLK_ON,
	#ifdef ODM_HQ_EDIT
	MT6785_AFE_GPIO_EXTAMP_PULLHIGH,
	MT6785_AFE_GPIO_EXTAMP_PULLLOW,
	#endif /* ODM_HQ_EDIT */
	MT6785_AFE_GPIO_GPIO_NUM
};

struct audio_gpio_attr {
	const char *name;
	bool gpio_prepare;
	struct pinctrl_state *gpioctrl;
};

static struct audio_gpio_attr aud_gpios[MT6785_AFE_GPIO_GPIO_NUM] = {
	[MT6785_AFE_GPIO_DAT_MISO_OFF] = {"aud_dat_miso_off", false, NULL},
	[MT6785_AFE_GPIO_DAT_MISO_ON] = {"aud_dat_miso_on", false, NULL},
	[MT6785_AFE_GPIO_DAT_MOSI_OFF] = {"aud_dat_mosi_off", false, NULL},
	[MT6785_AFE_GPIO_DAT_MOSI_ON] = {"aud_dat_mosi_on", false, NULL},
	[MT6785_AFE_GPIO_I2S0_OFF] = {"aud_gpio_i2s0_off", false, NULL},
	[MT6785_AFE_GPIO_I2S0_ON] = {"aud_gpio_i2s0_on", false, NULL},
	[MT6785_AFE_GPIO_I2S1_OFF] = {"aud_gpio_i2s1_off", false, NULL},
	[MT6785_AFE_GPIO_I2S1_ON] = {"aud_gpio_i2s1_on", false, NULL},
	[MT6785_AFE_GPIO_I2S2_OFF] = {"aud_gpio_i2s2_off", false, NULL},
	[MT6785_AFE_GPIO_I2S2_ON] = {"aud_gpio_i2s2_on", false, NULL},
	[MT6785_AFE_GPIO_I2S3_OFF] = {"aud_gpio_i2s3_off", false, NULL},
	[MT6785_AFE_GPIO_I2S3_ON] = {"aud_gpio_i2s3_on", false, NULL},
	[MT6785_AFE_GPIO_I2S5_OFF] = {"aud_gpio_i2s5_off", false, NULL},
	[MT6785_AFE_GPIO_I2S5_ON] = {"aud_gpio_i2s5_on", false, NULL},
	[MT6785_AFE_GPIO_VOW_DAT_OFF] = {"vow_dat_miso_off", false, NULL},
	[MT6785_AFE_GPIO_VOW_DAT_ON] = {"vow_dat_miso_on", false, NULL},
	[MT6785_AFE_GPIO_VOW_CLK_OFF] = {"vow_clk_miso_off", false, NULL},
	[MT6785_AFE_GPIO_VOW_CLK_ON] = {"vow_clk_miso_on", false, NULL},
	[MT6785_AFE_GPIO_DAT_MISO_CH34_OFF] = {"aud_dat_miso_ch34_off",
					       false, NULL},
	[MT6785_AFE_GPIO_DAT_MISO_CH34_ON] = {"aud_dat_miso_ch34_on",
					      false, NULL},
	[MT6785_AFE_GPIO_DAT_MOSI_CH34_OFF] = {"aud_dat_mosi_ch34_off",
					       false, NULL},
	[MT6785_AFE_GPIO_DAT_MOSI_CH34_ON] = {"aud_dat_mosi_ch34_on",
					      false, NULL},
	#ifdef ODM_HQ_EDIT
	[MT6785_AFE_GPIO_EXTAMP_PULLHIGH] = {"aud_gpio_extamp_pullhigh", false, NULL},
	[MT6785_AFE_GPIO_EXTAMP_PULLLOW] = {"aud_gpio_extamp_pulllow", false, NULL},
	#endif /* ODM_HQ_EDIT */
};

static DEFINE_MUTEX(gpio_request_mutex);

int mt6785_afe_gpio_init(struct mtk_base_afe *afe)
{
	int ret;
	int i = 0;

	aud_pinctrl = devm_pinctrl_get(afe->dev);
	if (IS_ERR(aud_pinctrl)) {
		ret = PTR_ERR(aud_pinctrl);
		dev_err(afe->dev, "%s(), ret %d, cannot get aud_pinctrl!\n",
			__func__, ret);
		return -ENODEV;
	}

	for (i = 0; i < ARRAY_SIZE(aud_gpios); i++) {
		aud_gpios[i].gpioctrl = pinctrl_lookup_state(aud_pinctrl,
							     aud_gpios[i].name);
		if (IS_ERR(aud_gpios[i].gpioctrl)) {
			ret = PTR_ERR(aud_gpios[i].gpioctrl);
			dev_err(afe->dev, "%s(), pinctrl_lookup_state %s fail, ret %d\n",
				__func__, aud_gpios[i].name, ret);
		} else {
			aud_gpios[i].gpio_prepare = true;
		}
	}

	/* gpio status init */
	mt6785_afe_gpio_request(afe, false, MT6785_DAI_ADDA, 0);
	mt6785_afe_gpio_request(afe, false, MT6785_DAI_ADDA, 1);

	return 0;
}

static int mt6785_afe_gpio_select(struct mtk_base_afe *afe,
				  enum mt6785_afe_gpio type)
{
	int ret = 0;

	if (type < 0 || type >= MT6785_AFE_GPIO_GPIO_NUM) {
		dev_err(afe->dev, "%s(), error, invaild gpio type %d\n",
			__func__, type);
		return -EINVAL;
	}

	if (!aud_gpios[type].gpio_prepare) {
		dev_warn(afe->dev, "%s(), error, gpio type %d not prepared\n",
			 __func__, type);
		return -EIO;
	}

	ret = pinctrl_select_state(aud_pinctrl,
				   aud_gpios[type].gpioctrl);
	if (ret) {
		dev_err(afe->dev, "%s(), error, can not set gpio type %d\n",
		       __func__, type);
		AUDIO_AEE("can not set gpio type");
	}
	return ret;
}

static int mt6785_afe_gpio_adda_dl(struct mtk_base_afe *afe, bool enable)
{
	if (enable) {
		return mt6785_afe_gpio_select(afe,
					      MT6785_AFE_GPIO_DAT_MOSI_ON);
	} else {
		return mt6785_afe_gpio_select(afe,
					      MT6785_AFE_GPIO_DAT_MOSI_OFF);
	}
}

static int mt6785_afe_gpio_adda_ul(struct mtk_base_afe *afe, bool enable)
{
	if (enable) {
		return mt6785_afe_gpio_select(afe,
					      MT6785_AFE_GPIO_DAT_MISO_ON);
	} else {
		return mt6785_afe_gpio_select(afe,
					      MT6785_AFE_GPIO_DAT_MISO_OFF);
	}
}

static int mt6785_afe_gpio_adda_ch34_dl(struct mtk_base_afe *afe, bool enable)
{
	if (enable) {
		return mt6785_afe_gpio_select(afe,
			MT6785_AFE_GPIO_DAT_MOSI_CH34_ON);
	} else {
		return mt6785_afe_gpio_select(afe,
			MT6785_AFE_GPIO_DAT_MOSI_CH34_OFF);
	}
}

static int mt6785_afe_gpio_adda_ch34_ul(struct mtk_base_afe *afe, bool enable)
{
	if (enable) {
		return mt6785_afe_gpio_select(afe,
			MT6785_AFE_GPIO_DAT_MISO_CH34_ON);
	} else {
		return mt6785_afe_gpio_select(afe,
			MT6785_AFE_GPIO_DAT_MISO_CH34_OFF);
	}
}

#ifdef ODM_HQ_EDIT
static DEFINE_MUTEX(extamp_gpio_request_mutex);

int mt6785_afe_gpio_extamp_select(struct mtk_base_afe *afe, bool enable, int mode)
{
	int extamp_mode = 0;
	int retval = -1;
	int i = 0;
	mutex_lock(&extamp_gpio_request_mutex);

	//pr_info("[WENDELL] %s enter, enable = %d, mode = %d", __func__, enable, mode);

	if (enable) {
		if (mode == 1)
			extamp_mode = 1;
		else if (mode == 2)
			extamp_mode = 2;
		else
			extamp_mode = 3; /* default mode is 3 */

		for (i = 0; i < extamp_mode; i++) {
			retval = mt6785_afe_gpio_select(afe, MT6785_AFE_GPIO_EXTAMP_PULLLOW);
			if (retval)
				pr_err("could not set aud_gpios[MT6785_AFE_GPIO_EXTAMP_PULLLOW] pins\n");
			udelay(2);

			retval = mt6785_afe_gpio_select(afe, MT6785_AFE_GPIO_EXTAMP_PULLHIGH);
			if (retval)
				pr_err("could not set aud_gpios[MT6785_AFE_GPIO_EXTAMP_PULLHIGH] pins\n");
			udelay(2);
		}
	} else {
		retval = mt6785_afe_gpio_select(afe, MT6785_AFE_GPIO_EXTAMP_PULLLOW);
		if (retval)
			pr_err("could not set aud_gpios[MT6785_AFE_GPIO_EXTAMP_PULLLOW] pins\n");
		udelay(2);
	}
	//pr_info("[WENDELL] %s exit, enable = %d, mode = %d", __func__, enable, mode);
	mutex_unlock(&extamp_gpio_request_mutex);

	return retval;
}
#endif /* ODM_HQ_EDIT */

int mt6785_afe_gpio_request(struct mtk_base_afe *afe, bool enable,
			    int dai, int uplink)
{
	mutex_lock(&gpio_request_mutex);
	switch (dai) {
	case MT6785_DAI_ADDA:
		if (uplink)
			mt6785_afe_gpio_adda_ul(afe, enable);
		else
			mt6785_afe_gpio_adda_dl(afe, enable);
		break;
	case MT6785_DAI_ADDA_CH34:
		if (uplink)
			mt6785_afe_gpio_adda_ch34_ul(afe, enable);
		else
			mt6785_afe_gpio_adda_ch34_dl(afe, enable);
		break;
	case MT6785_DAI_I2S_0:
		if (enable)
			mt6785_afe_gpio_select(afe, MT6785_AFE_GPIO_I2S0_ON);
		else
			mt6785_afe_gpio_select(afe, MT6785_AFE_GPIO_I2S0_OFF);
		break;
	case MT6785_DAI_I2S_1:
		if (enable)
			mt6785_afe_gpio_select(afe, MT6785_AFE_GPIO_I2S1_ON);
		else
			mt6785_afe_gpio_select(afe, MT6785_AFE_GPIO_I2S1_OFF);
		break;
	case MT6785_DAI_I2S_2:
		if (enable)
			mt6785_afe_gpio_select(afe, MT6785_AFE_GPIO_I2S2_ON);
		else
			mt6785_afe_gpio_select(afe, MT6785_AFE_GPIO_I2S2_OFF);
		break;
	case MT6785_DAI_I2S_3:
		if (enable)
			mt6785_afe_gpio_select(afe, MT6785_AFE_GPIO_I2S3_ON);
		else
			mt6785_afe_gpio_select(afe, MT6785_AFE_GPIO_I2S3_OFF);
		break;
	case MT6785_DAI_I2S_5:
		if (enable)
			mt6785_afe_gpio_select(afe, MT6785_AFE_GPIO_I2S5_ON);
		else
			mt6785_afe_gpio_select(afe, MT6785_AFE_GPIO_I2S5_OFF);
		break;
	case MT6785_DAI_VOW:
		if (enable) {
			mt6785_afe_gpio_select(afe,
					       MT6785_AFE_GPIO_VOW_CLK_ON);
			mt6785_afe_gpio_select(afe,
					       MT6785_AFE_GPIO_VOW_DAT_ON);
		} else {
			mt6785_afe_gpio_select(afe,
					       MT6785_AFE_GPIO_VOW_CLK_OFF);
			mt6785_afe_gpio_select(afe,
					       MT6785_AFE_GPIO_VOW_DAT_OFF);
		}
		break;
	default:
		mutex_unlock(&gpio_request_mutex);
		dev_warn(afe->dev, "%s(), invalid dai %d\n", __func__, dai);
		AUDIO_AEE("invalid dai");
		return -EINVAL;
	}
	mutex_unlock(&gpio_request_mutex);
	return 0;
}

