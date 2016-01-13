#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/i2c.h>
#include <linux/clk.h>
#include <sound/soc.h>

#include "imx-audmux.h"

#include "../codecs/tlv320aic3x.h"
#define NUM_CODECS 4

struct se_chessmen_data {
	struct snd_soc_dai_link dai;
	struct snd_soc_card card;
};

static int se_chessmen_dai_init(struct snd_soc_pcm_runtime *rtd)
{
	//struct se_chessmen_data *data = snd_soc_card_get_drvdata(rtd->card);
	struct device *dev = rtd->card->dev;
	struct snd_soc_dai **codec_dais = rtd->codec_dais;
	struct snd_soc_dai *cpu_dai = rtd->cpu_dai;
	int dai_fmt_master;
	int dai_fmt_slave;
	int dai_fmt;
	int i;
	int ret = 0;
	unsigned int txrx_mask;
	unsigned int slots = 16;
	unsigned int slot_width = 16;
	printk(KERN_INFO "*** %s\n", __func__);
	/* set codec 0 to 256-bit mode to generate 256 bits/frame */
	snd_soc_update_bits(codec_dais[0]->codec, AIC3X_ASD_INTF_CTRLB, 1<<3, 1<<3);

	/* Set dai formats.  codec 0 is different from the rest because it's the master. */
	dai_fmt_master = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_IB_NF | SND_SOC_DAIFMT_CBM_CFM;
	dai_fmt_slave = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_IB_NF | SND_SOC_DAIFMT_CBS_CFS;
	for (i = 0; i < NUM_CODECS; i++) {
		ret = snd_soc_dai_set_sysclk(rtd->codec_dais[i], CLKIN_MCLK,
					     24000000, SND_SOC_CLOCK_IN);
		if (ret) {
			dev_err(dev, "could not set codec driver clock params for codec %d\n",  i);
			return ret;
		}
		if (i == 0)
			dai_fmt = dai_fmt_master;
		else
			dai_fmt = dai_fmt_slave;
		ret = snd_soc_dai_set_fmt(codec_dais[i], dai_fmt);
		if (ret) {
			dev_err(dev, "could not set codec dai format for codec %d\n",  i);
			return ret;
		}

		txrx_mask = 3 << (i*2);
		ret = snd_soc_dai_set_tdm_slot(codec_dais[i], txrx_mask, txrx_mask, slots, slot_width);
		if (ret) {
			dev_err(dev, "could not set tdm slot for codec %d\n",  i);
			return ret;
		}
	}
	ret = snd_soc_dai_set_fmt(cpu_dai, dai_fmt_master); // that is... the CODEC is master
	if (ret) {
		dev_err(dev, "could not set codec dai format for the cpu\n");
		return ret;
	}

	
	txrx_mask = (1<<slots)-1;
	ret = snd_soc_dai_set_tdm_slot(cpu_dai, txrx_mask, txrx_mask, slots, slot_width);
	if (ret) {
		dev_err(dev, "could not set tdm slot for cpu, err= %d\n", ret);
		return ret;
	}
	return 0;
}

static const struct snd_soc_dapm_widget se_chessmen_dapm_widgets[] = {
	SND_SOC_DAPM_MIC("Mic Jack", NULL),
	SND_SOC_DAPM_LINE("Line In Jack", NULL),
	SND_SOC_DAPM_HP("Headphone Jack", NULL),
	SND_SOC_DAPM_SPK("Line Out Jack", NULL),
	SND_SOC_DAPM_SPK("Ext Spk", NULL),
};

static struct snd_soc_codec_conf codec_confs[NUM_CODECS] = {
	{ .name_prefix = "a", },
	{ .name_prefix = "b", },
	{ .name_prefix = "c", },
	{ .name_prefix = "d", },
};
static int se_chessmen_probe(struct platform_device *pdev)
{
	struct device_node *ssi_np;
	struct device_node *codecs_np[NUM_CODECS];
	struct platform_device *ssi_pdev;
	struct i2c_client *codec_dev[NUM_CODECS];
	struct se_chessmen_data *data = NULL;
	int i;
	int ret;
	printk(KERN_INFO "*** se_chessmen_probe!\n");
	ssi_np = of_parse_phandle(pdev->dev.of_node, "ssi-controller", 0);
	for (i = 0; i < NUM_CODECS; i++) {
		codecs_np[i] = of_parse_phandle(pdev->dev.of_node, "audio-codec", i); 
		if (!codecs_np[i]) {
			dev_err(&pdev->dev, "phandle missing or invalid.  you must include 4 AIC3x codecs in your 'audio-codec' spec.\n");
			ret = -EINVAL;
			goto fail;
		}
	}

	printk(KERN_INFO "*** %s 3\n", __func__);
	ssi_pdev = of_find_device_by_node(ssi_np);
	if (!ssi_pdev) {
		dev_err(&pdev->dev, "failed to find SSI platform device\n");
		ret = -EPROBE_DEFER;
		goto fail;
	}

	for (i = 0; i < NUM_CODECS;i++) {
		codec_dev[i] = of_find_i2c_device_by_node(codecs_np[i]);
		if (!codec_dev[i]) {
			dev_err(&pdev->dev, "failed to find codec platform device for codec %d\n", i);
			return -EPROBE_DEFER;
		}
	}
	for (i = 0; i < NUM_CODECS; i++) {
		codec_confs[i].of_node = codecs_np[i];
	}

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
	data->dai.init = &se_chessmen_dai_init;
	// Don't set dai_fmt because snd_soc_runtime_set_dai_fmt does the wrong thing
	// need to handle it in the dai.init function.
	// data->dai.dai_fmt = SND_SOC_DAIFMT_DSP_A | SND_SOC_DAIFMT_NB_NF | SND_SOC_DAIFMT_CBM_CFM;
	

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
	for (i = 0; i < NUM_CODECS; i++) {
		data->dai.codecs[i].of_node = codecs_np[i];
		data->dai.codecs[i].dai_name = "tlv320aic3x-hifi";
		codec_confs[i].of_node = data->dai.codecs[i].of_node;
	}
	
	data->card.num_links = 1;
	data->card.owner = THIS_MODULE;
	data->card.dai_link = &data->dai;
	data->card.dapm_widgets = se_chessmen_dapm_widgets;
	data->card.num_dapm_widgets = ARRAY_SIZE(se_chessmen_dapm_widgets);

	platform_set_drvdata(pdev, &data->card);
	snd_soc_card_set_drvdata(&data->card, data);
	printk(KERN_INFO "*** %s 8\n", __func__);

	ret = devm_snd_soc_register_card(&pdev->dev, &data->card);
	if (ret) {
		dev_err(&pdev->dev, "snd_soc_register_card failed (%d)\n", ret);
		goto fail;
	}

	of_node_put(ssi_np);
	for (i = 0; i < NUM_CODECS; i++)
		of_node_put(codecs_np[i]);

	printk(KERN_INFO "*** %s 9\n", __func__);
	return 0;

fail:
	of_node_put(ssi_np);
	for (i = 0; i < NUM_CODECS; i++)
		of_node_put(codecs_np[i]);

	return ret;
}

static int se_chessmen_remove(struct platform_device *pdev)
{
	//struct snd_soc_card *card = platform_get_drvdata(pdev);
	//struct se_chessmen_data *data = snd_soc_card_get_drvdata(card);
	return 0;
}

static const struct of_device_id se_chessmen_dt_ids[] = {
	{ .compatible = "se,chessmen", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, se_chessmen_dt_ids);

static struct platform_driver se_chessmen_driver = {
	.driver = {
		.name = "se-chessmen",
		.pm = &snd_soc_pm_ops,
		.of_match_table = se_chessmen_dt_ids,
	},
	.probe = se_chessmen_probe,
	.remove = se_chessmen_remove,
};
module_platform_driver(se_chessmen_driver);

MODULE_AUTHOR("Caleb Crome <caleb@crome.org>");
MODULE_DESCRIPTION("Signal Essence chessmen ASoC machine driver");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:se-chessmen");
