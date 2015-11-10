#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <sound/soc.h>

#include "imx-audmux.h"

struct se_puck_data {
	struct snd_soc_dai_link dai;
	struct snd_soc_card card;
};

static int se_puck_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	//struct se_puck_data *data = snd_soc_card_get_drvdata(rtd->card);
	//struct device *dev = rtd->card->dev;
	//int ret;

	return 0;
}

static const struct snd_soc_dapm_widget se_puck_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_LINE("Line In Jack", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Line Out Jack", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};

#define NUM_CODECS 3
static struct snd_soc_codec_conf codec_confs[NUM_CODECS] = {
	{
		.name_prefix = "a",
	},
	{
		.name_prefix = "b",
	},
	{
		.name_prefix = "c",
	}
};
static int se_puck_probe(struct platform_device *pdev)
{
	struct device_node *np = pdev->dev.of_node;
	struct device_node *ssi_np, *adc_0_np, *adc_1_np, *dac_np;
	struct platform_device *ssi_pdev;
	struct i2c_client *adc_0_dev, *adc_1_dev, *dac_dev;
	struct se_puck_data *data = NULL;
	int int_port, ext_port;
	int ret;
	ret = of_property_read_u32(np, "mux-int-port", &int_port);
	if (ret) {
		dev_err(&pdev->dev, "mux-int-port missing or invalid\n");
		return ret;
	}
	ret = of_property_read_u32(np, "mux-ext-port", &ext_port);
	if (ret) {
		dev_err(&pdev->dev, "mux-ext-port missing or invalid\n");
		return ret;
	}

	/*
	 * The port numbering in the hardware manual starts at 1, while
	 * the audmux API expects it starts at 0.
	 */
	int_port--;
	ext_port--;
	ret = imx_audmux_v2_configure_port(int_port,
			IMX_AUDMUX_V2_PTCR_SYN |
			IMX_AUDMUX_V2_PTCR_TFSEL(ext_port) |
			IMX_AUDMUX_V2_PTCR_TCSEL(ext_port) |
			IMX_AUDMUX_V2_PTCR_TFSDIR |
			IMX_AUDMUX_V2_PTCR_TCLKDIR,
			IMX_AUDMUX_V2_PDCR_RXDSEL(ext_port));
	if (ret) {
		dev_err(&pdev->dev, "audmux internal port setup failed\n");
		return ret;
	}
	printk(KERN_INFO "*** %s 2\n", __func__);
	ret = imx_audmux_v2_configure_port(ext_port,
			IMX_AUDMUX_V2_PTCR_SYN,
			IMX_AUDMUX_V2_PDCR_RXDSEL(int_port));
	if (ret) {
		dev_err(&pdev->dev, "audmux external port setup failed\n");
		return ret;
	}

	ssi_np = of_parse_phandle(pdev->dev.of_node, "ssi-controller", 0);
	adc_0_np = of_parse_phandle(pdev->dev.of_node, "audio-codec", 0);
	adc_1_np = of_parse_phandle(pdev->dev.of_node, "audio-codec", 1);
	dac_np = of_parse_phandle(pdev->dev.of_node, "audio-codec", 2);
	if (!ssi_np || !adc_0_np || !adc_1_np || !dac_np) {
		dev_err(&pdev->dev, "phandle missing or invalid.  must include 2 ADCs and 1 DAC, in that order\n");
		ret = -EINVAL;
		goto fail;
	}
	printk(KERN_INFO "*** %s 3\n", __func__);
	ssi_pdev = of_find_device_by_node(ssi_np);
	if (!ssi_pdev) {
		dev_err(&pdev->dev, "failed to find SSI platform device\n");
		ret = -EPROBE_DEFER;
		goto fail;
	}

	adc_0_dev = of_find_i2c_device_by_node(adc_0_np);
	adc_1_dev = of_find_i2c_device_by_node(adc_1_np);
	dac_dev = of_find_i2c_device_by_node(dac_np);
	if (!adc_0_dev || !adc_1_dev || !dac_dev) {
		dev_err(&pdev->dev, "failed to find codec platform device for adc 0, 1, or dac: %d, %d, %d\n",
			(unsigned int)adc_0_dev,
			(unsigned int)adc_1_dev,
			(unsigned int)dac_dev
			);
		return -EPROBE_DEFER;
	}

	codec_confs[0].of_node = adc_0_np;
	codec_confs[1].of_node = adc_1_np;
	codec_confs[2].of_node = dac_np;
	
	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		ret = -ENOMEM;
		goto fail;
	}

	printk(KERN_INFO "*** %s 5\n", __func__);
	data->dai.name = "HiFi";
	data->dai.stream_name = "HiFi";
	data->dai.cpu_of_node = ssi_np;
	data->dai.platform_of_node = ssi_np;
	data->dai.init = &se_puck_dai_init;
	data->dai.dai_fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_NB_NF |
			    SND_SOC_DAIFMT_CBM_CFM;

	data->card.dev = &pdev->dev;
	
	ret = snd_soc_of_parse_card_name(&data->card, "model");
	if (ret)
		goto fail;
	printk(KERN_INFO "*** %s 6\n", __func__);
	ret = snd_soc_of_parse_audio_routing(&data->card, "audio-routing");
	if (ret)
		goto fail;
	printk(KERN_INFO "*** %s 7\n", __func__);

	data->card.num_configs = NUM_CODECS;
	data->card.codec_conf = codec_confs;

	data->dai.codecs = kzalloc(sizeof(data->dai.codecs)*NUM_CODECS, GFP_KERNEL);
	data->dai.num_codecs = NUM_CODECS;
	data->dai.codecs[0].of_node = adc_0_np;
	data->dai.codecs[1].of_node = adc_1_np;
	data->dai.codecs[2].of_node = dac_np;
	data->dai.codecs[0].dai_name = "cs53l30-hifi";
	data->dai.codecs[1].dai_name = "cs53l30-hifi";
	data->dai.codecs[2].dai_name = "tlv320dac3100-hifi";
	
	codec_confs[0].of_node = data->dai.codecs[0].of_node;
	codec_confs[1].of_node = data->dai.codecs[1].of_node;
	codec_confs[2].of_node = data->dai.codecs[2].of_node;
	
	data->card.num_links = 1;
	data->card.owner = THIS_MODULE;
	data->card.dai_link = &data->dai;
	data->card.dapm_widgets = se_puck_dapm_widgets;
	data->card.num_dapm_widgets = ARRAY_SIZE(se_puck_dapm_widgets);

	platform_set_drvdata(pdev, &data->card);
	snd_soc_card_set_drvdata(&data->card, data);
	printk(KERN_INFO "*** %s 8\n", __func__);

	ret = devm_snd_soc_register_card(&pdev->dev, &data->card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
		goto fail;
	}

	of_node_put(ssi_np);
	of_node_put(adc_0_np);
	of_node_put(adc_1_np);
	of_node_put(dac_np);

	printk(KERN_INFO "*** %s 9\n", __func__);
	return 0;

fail:
	of_node_put(ssi_np);
	of_node_put(adc_0_np);
	of_node_put(adc_1_np);
	of_node_put(dac_np);

	return ret;
}

static int se_puck_remove(struct platform_device *pdev)
{
	//struct snd_soc_card *card = platform_get_drvdata(pdev);
	//struct se_puck_data *data = snd_soc_card_get_drvdata(card);
	return 0;
}

static const struct of_device_id se_puck_dt_ids[] = {
	{ .compatible = "se,puck", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, se_puck_dt_ids);

static struct platform_driver se_puck_driver = {
	.driver = {
		.name = "se-puck",
		.pm = &snd_soc_pm_ops,
		.of_match_table = se_puck_dt_ids,
	},
	.probe = se_puck_probe,
	.remove = se_puck_remove,
};
module_platform_driver(se_puck_driver);

MODULE_AUTHOR("Caleb Crome <caleb@crome.org>");
MODULE_DESCRIPTION("Signal Essence puck ASoC machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:se-puck");
