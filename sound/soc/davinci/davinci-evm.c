/*
 * ASoC driver for TI DAVINCI EVM platform
 *
 * Author:      Vladimir Barinov, <vbarinov@embeddedalley.com>
 * Copyright:   (C) 2007 MontaVista Software, Inc., <source@mvista.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/timer.h>
#include <linux/interrupt.h>
#include <linux/platform_device.h>
#include <linux/i2c.h>
#include <linux/of_platform.h>
#include <linux/clk.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/soc.h>

#include <asm/dma.h>
#include <asm/mach-types.h>
#include "../codecs/tlv320aic3x.h"

#define MASTER_DAI_FMT (SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBM_CFM | SND_SOC_DAIFMT_IB_NF)
#define SLAVE_DAI_FMT  (SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBS_CFS | SND_SOC_DAIFMT_IB_NF)
int aic3x_set_dai_fmt(struct snd_soc_dai *codec_dai, unsigned int fmt);

struct snd_soc_card_drvdata_davinci {
	struct clk *mclk;
	unsigned sysclk;
        int controls_added_already;
};

static int evm_startup(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *soc_card = rtd->card;
	struct snd_soc_card_drvdata_davinci *drvdata =
		snd_soc_card_get_drvdata(soc_card);

	if (drvdata->mclk)
		return clk_prepare_enable(drvdata->mclk);

	return 0;
}

static void evm_shutdown(struct snd_pcm_substream *substream)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	struct snd_soc_card *soc_card = rtd->card;
	struct snd_soc_card_drvdata_davinci *drvdata =
		snd_soc_card_get_drvdata(soc_card);

	if (drvdata->mclk)
		clk_disable_unprepare(drvdata->mclk);
}


static int setup_puppy_codecs(struct snd_soc_pcm_runtime *rtd)
{
	unsigned int tdm_mask;
	struct snd_soc_card *soc_card = rtd->card;
	struct snd_soc_card_drvdata_davinci *drvdata = snd_soc_card_get_drvdata(soc_card);
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	unsigned sysclk = drvdata->sysclk;
	int i;
	int ret = 0;
	printk(KERN_INFO "*** %s setup\n", __func__);
	if (drvdata->mclk)
		return clk_prepare_enable(drvdata->mclk);

	{
		struct snd_soc_codec *codec0 = rtd->codec_dais[0]->codec;
		u8 iface_areg;
		printk(KERN_INFO "--- %s sysclk\n", __func__);
		// Step 5 -- set blkc to output on codec 0
		iface_areg = snd_soc_read(codec0, AIC3X_ASD_INTF_CTRLA) & 0x3f;
		iface_areg |= (1<<7) | (1<<6);
		snd_soc_write(codec0, AIC3X_ASD_INTF_CTRLA, iface_areg  );// set mclk & bclk as outputs for codec 0
		
		// step 6 -- switch clkdiv_clkin from bclk to mclk on codec 0
		snd_soc_write(codec0, AIC3X_CLKGEN_CTRL_REG, 0); // and finally, start the actual clocks.
	}

	/* set the CPU system clock */
	ret = snd_soc_dai_set_sysclk(cpu_dai, 0, sysclk, SND_SOC_CLOCK_OUT);
	//regcache_sync(codec0->regmap);
	if (ret < 0)
		return ret;
	printk(KERN_INFO "--- %s setup\n", __func__);
	return ret;
}
static int evm_hw_params(struct snd_pcm_substream *substream,
			 struct snd_pcm_hw_params *params)
{
	struct snd_soc_pcm_runtime *rtd = substream->private_data;
	int ret = 0;

	ret = setup_puppy_codecs(rtd);

	return ret;
}

static struct snd_soc_ops evm_ops = {
	// .startup = evm_startup,
	// .shutdown = evm_shutdown,
	// .hw_params = evm_hw_params,
};

/* davinci-evm machine dapm widgets */
static const struct snd_soc_dapm_widget aic3x_dapm_widgets[] = {
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_LINE("Line Out", NULL),
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_LINE("Line In", NULL),
};

/* davinci-evm machine audio_mapnections to the codec pins */
static const struct snd_soc_dapm_route audio_map[] = {
	/* Headphone connected to HPLOUT, HPROUT */
	{"Headphone Jack", NULL, "a HPLOUT"},
	{"Headphone Jack", NULL, "a HPROUT"},

	/* Line Out connected to LLOUT, RLOUT */
	{"Line Out", NULL, "a LLOUT"},
	{"Line Out", NULL, "a RLOUT"},

	/* Mic connected to (MIC3L | MIC3R) */
	{"a MIC3L", NULL, "Mic Bias"},
	{"a MIC3R", NULL, "Mic Bias"},
	{"a Mic Bias", NULL, "Mic Jack"},

	/* Line In connected to (LINE1L | LINE2L), (LINE1R | LINE2R) */
	{"a LINE1L", NULL, "Line In"},
	{"a LINE2L", NULL, "Line In"},
	{"a LINE1R", NULL, "Line In"},
	{"a LINE2R", NULL, "Line In"},
};

/* Logic for a aic3x as connected on a davinci-evm */
static int evm_aic3x_init(struct snd_soc_pcm_runtime *rtd)
{
	struct snd_soc_card *card = rtd->card;
	struct device_node *np = card->dev->of_node;
	
	struct snd_soc_card_drvdata_davinci *drvdata =
		snd_soc_card_get_drvdata(card);
	int ret;

	printk(KERN_INFO "*** %s\n", __func__);
	/* Add davinci-evm specific widgets */
	if (!drvdata->controls_added_already) {
	    snd_soc_dapm_new_controls(&card->dapm, aic3x_dapm_widgets,
				      ARRAY_SIZE(aic3x_dapm_widgets));
	    drvdata->controls_added_already = 1;
	}

	if (np) {
		ret = snd_soc_of_parse_audio_routing(card, "ti,audio-routing");
		if (ret)
			return ret;
	} else {
		/* Set up davinci-evm specific audio path audio_map */
		snd_soc_dapm_add_routes(&card->dapm, audio_map,
					ARRAY_SIZE(audio_map));
	}

	/* not connected */
	//snd_soc_dapm_nc_pin(&card->dapm, "a MONO_LOUT");
	//snd_soc_dapm_nc_pin(&card->dapm, "a HPLCOM");
	//snd_soc_dapm_nc_pin(&card->dapm, "a HPRCOM");
	setup_puppy_codecs(rtd);
	printk(KERN_INFO "--- %s setup\n", __func__);
	return 0;
}


/* davinci-evm digital audio interface glue - connects codec <--> CPU */
static struct snd_soc_dai_link dm6446_evm_dai = {
	.name = "TLV320AIC3X",
	.stream_name = "AIC3X",
	.cpu_dai_name = "davinci-mcbsp",
	.codec_dai_name = "tlv320aic3x-hifi",
	.codec_name = "tlv320aic3x-codec.1-001b",
	.platform_name = "davinci-mcbsp",
	.init = evm_aic3x_init,
	.ops = &evm_ops,
	.dai_fmt = SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBM_CFM |
		   SND_SOC_DAIFMT_IB_NF,
};

static struct snd_soc_dai_link dm355_evm_dai = {
	.name = "TLV320AIC3X",
	.stream_name = "AIC3X",
	.cpu_dai_name = "davinci-mcbsp.1",
	.codec_dai_name = "tlv320aic3x-hifi",
	.codec_name = "tlv320aic3x-codec.1-001b",
	.platform_name = "davinci-mcbsp.1",
	.init = evm_aic3x_init,
	.ops = &evm_ops,
	.dai_fmt = SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBM_CFM |
		   SND_SOC_DAIFMT_IB_NF,
};

static struct snd_soc_dai_link dm365_evm_dai = {
#ifdef CONFIG_SND_DM365_AIC3X_CODEC
	.name = "TLV320AIC3X",
	.stream_name = "AIC3X",
	.cpu_dai_name = "davinci-mcbsp",
	.codec_dai_name = "tlv320aic3x-hifi",
	.codec_name = "tlv320aic3x-codec.1-0018",
	.platform_name = "davinci-mcbsp",
	.init = evm_aic3x_init,
	.ops = &evm_ops,
	.dai_fmt = SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBM_CFM |
		   SND_SOC_DAIFMT_IB_NF,
#elif defined(CONFIG_SND_DM365_VOICE_CODEC)
	.name = "Voice Codec - CQ93VC",
	.stream_name = "CQ93",
	.cpu_dai_name = "davinci-vcif",
	.codec_dai_name = "cq93vc-hifi",
	.codec_name = "cq93vc-codec",
	.platform_name = "davinci-vcif",
#endif
};

static struct snd_soc_dai_link dm6467_evm_dai[] = {
	{
		.name = "TLV320AIC3X",
		.stream_name = "AIC3X",
		.cpu_dai_name= "davinci-mcasp.0",
		.codec_dai_name = "tlv320aic3x-hifi",
		.platform_name = "davinci-mcasp.0",
		.codec_name = "tlv320aic3x-codec.0-001a",
		.init = evm_aic3x_init,
		.ops = &evm_ops,
		.dai_fmt = SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBM_CFM |
			   SND_SOC_DAIFMT_IB_NF,
	},
	{
		.name = "McASP",
		.stream_name = "spdif",
		.cpu_dai_name= "davinci-mcasp.1",
		.codec_dai_name = "dit-hifi",
		.codec_name = "spdif_dit",
		.platform_name = "davinci-mcasp.1",
		.dai_fmt = SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBM_CFM |
			   SND_SOC_DAIFMT_IB_NF,
	},
};

static struct snd_soc_dai_link da830_evm_dai = {
	.name = "TLV320AIC3X",
	.stream_name = "AIC3X",
	.cpu_dai_name = "davinci-mcasp.1",
	.codec_dai_name = "tlv320aic3x-hifi",
	.codec_name = "tlv320aic3x-codec.1-0018",
	.platform_name = "davinci-mcasp.1",
	.init = evm_aic3x_init,
	.ops = &evm_ops,
	.dai_fmt = SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBM_CFM |
		   SND_SOC_DAIFMT_IB_NF,
};

static struct snd_soc_dai_link da850_evm_dai = {
	.name = "TLV320AIC3X",
	.stream_name = "AIC3X",
	.cpu_dai_name= "davinci-mcasp.0",
	.codec_dai_name = "tlv320aic3x-hifi",
	.codec_name = "tlv320aic3x-codec.1-0018",
	.platform_name = "davinci-mcasp.0",
	.init = evm_aic3x_init,
	.ops = &evm_ops,
	.dai_fmt = SND_SOC_DAIFMT_DSP_B | SND_SOC_DAIFMT_CBM_CFM |
		   SND_SOC_DAIFMT_IB_NF,
};

/* davinci dm6446 evm audio machine driver */
/*
 * ASP0 in DM6446 EVM is clocked by U55, as configured by
 * board-dm644x-evm.c using GPIOs from U18.  There are six
 * options; here we "know" we use a 48 KHz sample rate.
 */
static struct snd_soc_card_drvdata_davinci dm6446_snd_soc_card_drvdata = {
	.sysclk = 12288000,
};

static struct snd_soc_card dm6446_snd_soc_card_evm = {
	.name = "DaVinci DM6446 EVM",
	.owner = THIS_MODULE,
	.dai_link = &dm6446_evm_dai,
	.num_links = 1,
	.drvdata = &dm6446_snd_soc_card_drvdata,
};

/* davinci dm355 evm audio machine driver */
/* ASP1 on DM355 EVM is clocked by an external oscillator */
static struct snd_soc_card_drvdata_davinci dm355_snd_soc_card_drvdata = {
	.sysclk = 27000000,
};

static struct snd_soc_card dm355_snd_soc_card_evm = {
	.name = "DaVinci DM355 EVM",
	.owner = THIS_MODULE,
	.dai_link = &dm355_evm_dai,
	.num_links = 1,
	.drvdata = &dm355_snd_soc_card_drvdata,
};

/* davinci dm365 evm audio machine driver */
static struct snd_soc_card_drvdata_davinci dm365_snd_soc_card_drvdata = {
	.sysclk = 27000000,
};

static struct snd_soc_card dm365_snd_soc_card_evm = {
	.name = "DaVinci DM365 EVM",
	.owner = THIS_MODULE,
	.dai_link = &dm365_evm_dai,
	.num_links = 1,
	.drvdata = &dm365_snd_soc_card_drvdata,
};

/* davinci dm6467 evm audio machine driver */
static struct snd_soc_card_drvdata_davinci dm6467_snd_soc_card_drvdata = {
	.sysclk = 27000000,
};

static struct snd_soc_card dm6467_snd_soc_card_evm = {
	.name = "DaVinci DM6467 EVM",
	.owner = THIS_MODULE,
	.dai_link = dm6467_evm_dai,
	.num_links = ARRAY_SIZE(dm6467_evm_dai),
	.drvdata = &dm6467_snd_soc_card_drvdata,
};

static struct snd_soc_card_drvdata_davinci da830_snd_soc_card_drvdata = {
	.sysclk = 24576000,
};

static struct snd_soc_card da830_snd_soc_card = {
	.name = "DA830/OMAP-L137 EVM",
	.owner = THIS_MODULE,
	.dai_link = &da830_evm_dai,
	.num_links = 1,
	.drvdata = &da830_snd_soc_card_drvdata,
};

static struct snd_soc_card_drvdata_davinci da850_snd_soc_card_drvdata = {
	.sysclk = 24576000,
};

static struct snd_soc_card da850_snd_soc_card = {
	.name = "DA850/OMAP-L138 EVM",
	.owner = THIS_MODULE,
	.dai_link = &da850_evm_dai,
	.num_links = 1,
	.drvdata = &da850_snd_soc_card_drvdata,
};

#if defined(CONFIG_OF)

/*
 * The struct is used as place holder. It will be completely
 * filled with data from dt node.
 */
static struct snd_soc_dai_link evm_dai_tlv320aic3x = {
	.name		= "TLV320AIC3X",
	.stream_name	= "AIC3X",
	.ops            = &evm_ops,
	.init           = evm_aic3x_init,
	.dai_fmt        = MASTER_DAI_FMT,
};

static const struct of_device_id davinci_evm_dt_ids[] = {
	{
		.compatible = "ti,da830-evm-audio",
		.data = (void *) &evm_dai_tlv320aic3x,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, davinci_evm_dt_ids);

/* davinci evm audio machine driver */
static struct snd_soc_card evm_soc_card = {
	.owner = THIS_MODULE,
	.num_links = 1,
};

#define SE_MAX_NUM_CODECS 16
static struct snd_soc_codec_conf evm_codec_confs[SE_MAX_NUM_CODECS];

static int davinci_evm_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	const struct of_device_id *match =
		of_match_device(of_match_ptr(davinci_evm_dt_ids), &pdev->dev);
	struct snd_soc_dai_link *dai = (struct snd_soc_dai_link *) match->data;
	struct snd_soc_card_drvdata_davinci *drvdata = NULL;
	struct clk *mclk;
	int ret = 0;
	int i;

	evm_soc_card.dai_link = dai;
	
	evm_soc_card.codec_conf = evm_codec_confs;
	
	dai->codecs = kzalloc(sizeof(dai->codecs)*SE_MAX_NUM_CODECS, GFP_KERNEL);

	for (i = 0;
	     (of_parse_phandle(np, "ti,audio-codec", i) != NULL) && (i < SE_MAX_NUM_CODECS); 
	     i++) {
	    char *name_prefix = kzalloc(4, GFP_KERNEL);
	    dai->codecs[i].of_node = of_parse_phandle(np, "ti,audio-codec", i);
	    dai->codecs[i].dai_name	= "tlv320aic3x-hifi";
	    
	    if (!dai->codecs[i].of_node)
		return -EINVAL;

	    evm_codec_confs[i].of_node = dai->codecs[i].of_node;
	    snprintf(name_prefix, 4, "%c", 'a'+i);
	    evm_codec_confs[i].name_prefix = name_prefix;

	}
	evm_soc_card.num_configs=i;
	dai->num_codecs = i;
	if (dai->num_codecs == 0)  {
	    printk(KERN_INFO "%s:  no codecs specified.  you must specify a list of ti,audio-codec in the devicetree\n", __func__);
	    return -EINVAL;
	}

	dai->cpu_of_node = of_parse_phandle(np, "ti,mcasp-controller", 0);
	if (!dai->cpu_of_node)
	    return -EINVAL;

	dai->platform_of_node = dai->cpu_of_node;

	evm_soc_card.dev = &pdev->dev;
	ret = snd_soc_of_parse_card_name(&evm_soc_card, "ti,model");
	if (ret)
		return ret;

	mclk = devm_clk_get(&pdev->dev, "mclk");
	if (PTR_ERR(mclk) == -EPROBE_DEFER) {
		return -EPROBE_DEFER;
	} else if (IS_ERR(mclk)) {
		dev_dbg(&pdev->dev, "mclk not found.\n");
		mclk = NULL;
	}

	drvdata = devm_kzalloc(&pdev->dev, sizeof(*drvdata), GFP_KERNEL);
	if (!drvdata)
		return -ENOMEM;

	drvdata->mclk = mclk;

	ret = of_property_read_u32(np, "ti,codec-clock-rate", &drvdata->sysclk);

	if (ret < 0) {
		if (!drvdata->mclk) {
			dev_err(&pdev->dev,
				"No clock or clock rate defined.\n");
			return -EINVAL;
		}
		drvdata->sysclk = clk_get_rate(drvdata->mclk);
	} else if (drvdata->mclk) {
		unsigned int requestd_rate = drvdata->sysclk;
		clk_set_rate(drvdata->mclk, drvdata->sysclk);
		drvdata->sysclk = clk_get_rate(drvdata->mclk);
		if (drvdata->sysclk != requestd_rate)
			dev_warn(&pdev->dev,
				 "Could not get requested rate %u using %u.\n",
				 requestd_rate, drvdata->sysclk);
	}

	snd_soc_card_set_drvdata(&evm_soc_card, drvdata);
	ret = devm_snd_soc_register_card(&pdev->dev, &evm_soc_card);

	if (drvdata->mclk)
		return clk_prepare_enable(drvdata->mclk);

	if (ret)
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);

	return ret;
}

static struct platform_driver davinci_evm_driver = {
	.probe		= davinci_evm_probe,
	.driver		= {
		.name	= "davinci_evm",
		.pm	= &snd_soc_pm_ops,
		.of_match_table = of_match_ptr(davinci_evm_dt_ids),
	},
};
#endif

static struct platform_device *evm_snd_device;

static int __init evm_init(void)
{
	struct snd_soc_card *evm_snd_dev_data;
	int index;
	int ret;

	/*
	 * If dtb is there, the devices will be created dynamically.
	 * Only register platfrom driver structure.
	 */
#if defined(CONFIG_OF)
	if (of_have_populated_dt())
		return platform_driver_register(&davinci_evm_driver);
#endif

	if (machine_is_davinci_evm()) {
		evm_snd_dev_data = &dm6446_snd_soc_card_evm;
		index = 0;
	} else if (machine_is_davinci_dm355_evm()) {
		evm_snd_dev_data = &dm355_snd_soc_card_evm;
		index = 1;
	} else if (machine_is_davinci_dm365_evm()) {
		evm_snd_dev_data = &dm365_snd_soc_card_evm;
		index = 0;
	} else if (machine_is_davinci_dm6467_evm()) {
		evm_snd_dev_data = &dm6467_snd_soc_card_evm;
		index = 0;
	} else if (machine_is_davinci_da830_evm()) {
		evm_snd_dev_data = &da830_snd_soc_card;
		index = 1;
	} else if (machine_is_davinci_da850_evm()) {
		evm_snd_dev_data = &da850_snd_soc_card;
		index = 0;
	} else
		return -EINVAL;

	evm_snd_device = platform_device_alloc("soc-audio", index);
	if (!evm_snd_device)
		return -ENOMEM;

	platform_set_drvdata(evm_snd_device, evm_snd_dev_data);
	ret = platform_device_add(evm_snd_device);
	if (ret)
		platform_device_put(evm_snd_device);

	return ret;
}

static void __exit evm_exit(void)
{
#if defined(CONFIG_OF)
	if (of_have_populated_dt()) {
		platform_driver_unregister(&davinci_evm_driver);
		return;
	}
#endif

	platform_device_unregister(evm_snd_device);
}

module_init(evm_init);
module_exit(evm_exit);

MODULE_AUTHOR("Vladimir Barinov");
MODULE_DESCRIPTION("TI DAVINCI EVM ASoC driver");
MODULE_LICENSE("GPL");
