#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/pm.h>
#include <linux/i2c.h>
#include <linux/gpio.h>
#include <linux/regulator/consumer.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/slab.h>
#include <sound/core.h>
#include <sound/pcm.h>
#include <sound/pcm_params.h>
#include <sound/soc.h>
#include <sound/initval.h>


/* codec private data */
struct cs53l30_priv {
	struct snd_soc_codec *codec;
	struct regmap *regmap;
	int tdm_offset;
};

static const struct reg_default cs53l30_reg[] = {
	{ 0x01, 0x93 }, { 0x02, 0xa3 }, { 0x03, 0x00 },	{ 0x05, 0x00 },
	{ 0x06, 0x10 }, { 0x07, 0x04 },	{ 0x08, 0x1c },	{ 0x0a, 0xf4 },
	{ 0x0c, 0x0c },	{ 0x0d, 0x80 },	{ 0x0e, 0x2f },	{ 0x0f, 0x2f },
	{ 0x10, 0x2f },	{ 0x11, 0x2f },	{ 0x12, 0x00 },	{ 0x13, 0x00 },
	{ 0x14, 0x00 },	{ 0x15, 0x00 },	{ 0x16, 0x00 },	{ 0x17, 0x00 },
	{ 0x18, 0x00 },	{ 0x19, 0x00 },	{ 0x1a, 0x00 },	{ 0x1b, 0x00 },
	{ 0x1c, 0x00 },	{ 0x1f, 0x00 },	{ 0x20, 0x80 },	{ 0x21, 0xaa },
	{ 0x22, 0xaa },	{ 0x23, 0xa8 },	{ 0x24, 0xec },	{ 0x25, 0x04 },
	{ 0x26, 0x00 },	{ 0x27, 0x04 },	{ 0x28, 0x00 },	{ 0x29, 0x00 },
	{ 0x2a, 0x00 },	{ 0x2b, 0x00 },	{ 0x2c, 0x00 },	{ 0x2d, 0x04 },
	{ 0x2e, 0x00 },	{ 0x2f, 0x08 },	{ 0x30, 0x00 },	{ 0x31, 0x00 },
	{ 0x32, 0x00 },	{ 0x33, 0x00 },	{ 0x34, 0x00 }, { 0x35, 0xff },
	{ 0x36, 0x00 },
};

static const struct regmap_config cs53l30_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x36,
	.reg_defaults = cs53l30_reg,
	.num_reg_defaults = ARRAY_SIZE(cs53l30_reg),
	.cache_type = REGCACHE_RBTREE,
};
static int cs53l30_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	/*struct cs53l30_priv *cs53l30 = snd_soc_codec_get_drvdata(codec);*/
	/*
	 * valid rates are 16k and 48k
	 */
	int fs = (params_rate(params));
	switch (fs) {
	case 48000:
		snd_soc_update_bits(codec, 0x0C, 0x0F, 0x0c);
		break;
	case 16000:
		snd_soc_update_bits(codec, 0x0C, 0x0F, 0x05);
		break;
	default:
		/* unsupported rate */
		return -EINVAL;
	}
	return 0;
}
static int cs53l30_init(struct snd_soc_codec *codec)
{
	int ret;
	/* 
	 * ext MCLK = 12.288 MHz
	 * 16kHz Sample rate: 16K    48K
	 *   MCLK_INT_SCALE   0      x
	 *   ASP_RATE         0101   1100
	 *   FSINT            48.000 48.000
	 *   LRCK             16.000 48.000
	 *   MCLK/LRCK RATIO  768    256
	 */
	struct cs53l30_priv *cs53l30 = snd_soc_codec_get_drvdata(codec);
	int i;
	unsigned long long int slots = 0xFF;
	slots = slots << (cs53l30->tdm_offset / 8);
	printk(KERN_INFO "*** %s\n", __func__);
	/* follow data sheet*/
	ret = snd_soc_write(codec, 0x06, 0x50); /* 5. power down the device.*/
	printk(KERN_INFO "*** %s: snd_soc_write returned %d\n", __func__, ret);
	snd_soc_write(codec, 0x07, 0x04); /* 6.*/
	/* if (cs53l30->tdm_offset < 6) {
	 * snd_soc_write(codec, 0x0C, (1<<7) | (0xc<<0)); // Configure as master
	 * } else { */
	snd_soc_write(codec, 0x0C, (0<<7) | (0xc<<0)); // Configure as slave
	snd_soc_write(codec, 0x0D, (0<<7) ); // Set TDM mode
	snd_soc_write(codec, 0x0E, 0x00+(cs53l30->tdm_offset/8)); // Channel 1 beginst at (8-bit) slot 0
	snd_soc_write(codec, 0x0F, 0x02+(cs53l30->tdm_offset/8)); // Channel 2 begins at slot 2
	snd_soc_write(codec, 0x10, 0x04+(cs53l30->tdm_offset/8)); // Channel 3 begins at slot 4
	snd_soc_write(codec, 0x11, 0x06+(cs53l30->tdm_offset/8)); // Channel 4 begins at slot 6

	snd_soc_write(codec, 0x12, (unsigned char)((slots & 0x00FF0000000000) >> 40)); //  enabled 8-bit slots 47:40
	snd_soc_write(codec, 0x13, (unsigned char)((slots & 0x0000FF00000000) >> 32)); //  enabled 8-bit slots 39:32
	snd_soc_write(codec, 0x14, (unsigned char)((slots & 0x000000FF000000) >> 24)); //  enabled 8-bit slots 31:24
	snd_soc_write(codec, 0x15, (unsigned char)((slots & 0x00000000FF0000) >> 16)); //  enabled 8-bit slots 23:16
	snd_soc_write(codec, 0x16, (unsigned char)((slots & 0x0000000000FF00) >>  8)); //  enabled 8-bit slots 15:08
	snd_soc_write(codec, 0x17, (unsigned char)((slots & 0x000000000000FF) >>  0)); //  enabled 8-bit slots 07:00  -- only 8 slots enabled (4 16-bit channels)


	for (i = 0; i < 4; i++) {
		/*
		 * set ADC premap and PGA gains...
		 *                             PREAMP   PGA_VOL
		 */
		snd_soc_write(codec, i + 0x29, (1<<6) | (0 << 0));
	}
	/* turn on mic bias */
	snd_soc_write(codec, 0x0a, (0x0 << 4) | (1<<2) | (2<<0));

	snd_soc_write(codec, 0x18, (1<<6)); // power down SDOUT2.  not needed
	snd_soc_write(codec, 0x1B, 0x00); // LRCK is 1 bit wide
	snd_soc_write(codec, 0x1C, 0x00); // LRCK is 1 bit wide
	snd_soc_write(codec, 0X06, 0X00); // power up the device.
	printk(KERN_INFO "***--- %s\n", __func__);
	return 0;
}

static int cs53l30_probe(struct snd_soc_codec *codec)
{
	struct cs53l30_priv *cs53l30 = snd_soc_codec_get_drvdata(codec);
	//int ret, i;
	printk(KERN_INFO "*** %s\n", __func__);

	cs53l30->codec = codec;

	cs53l30_init(codec);

	// cs53l30_add_widgets(codec);
	printk(KERN_INFO "***--- %s\n", __func__);
	
	return 0;
}

static int cs53l30_remove(struct snd_soc_codec *codec)
{
	//struct cs53l30_priv *cs53l30 = snd_soc_codec_get_drvdata(codec);
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_cs53l30 = {
	.probe = cs53l30_probe,
	.remove = cs53l30_remove,
//	.controls = cs53l30_snd_controls,
//	.num_controls = ARRAY_SIZE(cs53l30_snd_controls),
//	.dapm_widgets = cs53l30_dapm_widgets,
//	.num_dapm_widgets = ARRAY_SIZE(cs53l30_dapm_widgets),
//	.dapm_routes = intercon,
//	.num_dapm_routes = ARRAY_SIZE(intercon),
};

static const struct snd_soc_dai_ops cs53l30_dai_ops = {
	.hw_params	= cs53l30_hw_params,
	// .prepare	= cs53l30_prepare,
	// .digital_mute	= cs53l30_mute,
	// .set_sysclk	= cs53l30_set_dai_sysclk,
	// .set_fmt	= cs53l30_set_dai_fmt,
	// .set_tdm_slot	= cs53l30_set_dai_tdm_slot,
};

static struct snd_soc_dai_driver cs53l30_dai = {
	.name = "cs53l30-hifi",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 4,
		.channels_max = 16,
		.rates = SNDRV_PCM_RATE_16000,
		.formats =SNDRV_PCM_FMTBIT_S16_LE,
	},
	.playback = {
		.stream_name = "Playback",
		.channels_min = 0,
		.channels_max = 0,
		.rates = SNDRV_PCM_RATE_16000,
		.formats =SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &cs53l30_dai_ops,
	.symmetric_rates = 1,
};

static int cs53l30_i2c_probe(struct i2c_client *i2c,
			   const struct i2c_device_id *id)
{
	struct cs53l30_priv *cs53l30;
	int ret;
	struct device_node *np = i2c->dev.of_node;
	printk(KERN_INFO "*** %s\n", __func__);
	cs53l30 = devm_kzalloc(&i2c->dev, sizeof(struct cs53l30_priv), GFP_KERNEL);
	if (!cs53l30)
		return -ENOMEM;
	cs53l30->regmap = devm_regmap_init_i2c(i2c, &cs53l30_regmap);
	if (IS_ERR(cs53l30->regmap)) {
		ret = PTR_ERR(cs53l30->regmap);
		return ret;
	}
	regcache_cache_only(cs53l30->regmap, false);


// Get the gpios for clock enable & i2c enable
	ret = of_get_named_gpio(np, "clken-gpio", 0);
	if (ret >= 0) {
		printk(KERN_INFO "*** %s Yeah, got clken-gpio = %d\n", __func__, ret);
		gpio_direction_output(ret, 0);
		gpio_set_value(ret, 1);
	} else {
		printk(KERN_INFO "*** %s didn't get clken-gpio\n", __func__);
	}
	ret = of_get_named_gpio(np, "i2c-en-gpio", 0);
	if (ret >= 0) {
		printk(KERN_INFO "*** %s Yeah, got i2c-en-gpio = %d\n", __func__, ret);
		gpio_direction_output(ret, 0);
		gpio_set_value(ret, 1);
	} else {
		printk(KERN_INFO "*** %s didn't get i2c-en-gpio\n", __func__);
	}
	ret = of_get_named_gpio(np, "reset-gpio", 0);
	if (ret >= 0) {
		printk(KERN_INFO "*** %s Yeah, got reset-gpio = %d\n", __func__, ret);
		gpio_direction_output(ret, 0);
		gpio_set_value(ret, 0);
		udelay(1);
		gpio_set_value(ret, 1);
	} else {
		printk(KERN_INFO "*** %s didn't get reset-gpio\n", __func__);
	}
	if (of_property_read_u32(np, "tdm-offset", &cs53l30->tdm_offset)) {
		printk("You'll want to be settin' a tdm-offset parameter in your cs53l30 device node\n");
		return -EINVAL;
	}
	if ((cs53l30->tdm_offset & 0x7) != 0) {
		printk(KERN_INFO "Your tdm offset must be a multiple of 8");
		return -EINVAL;
	}

	i2c_set_clientdata(i2c, cs53l30);
	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_cs53l30, &cs53l30_dai, 1);
	if (ret != 0)
		goto err;

	printk(KERN_INFO "***--- %s\n", __func__);
	return 0;

err:
	return ret;
}


static int cs53l30_i2c_remove(struct i2c_client *client)
{
	// struct cs53l30_priv *cs53l30 = i2c_get_clientdata(client);

	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id cs53l30_id[] = {
	{"cs53l30", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, cs53l30_id);

static const struct of_device_id cs53l30_of_match[] = {
	{ .compatible = "ci,cs53l30", },
	{},
};
MODULE_DEVICE_TABLE(of, cs53l30_of_match);

static struct i2c_driver cs53l30_i2c_driver = {
	.driver = {
		.name = "cs53l30-codec",
		.of_match_table = of_match_ptr(cs53l30_of_match),
	},
	.probe	= cs53l30_i2c_probe,
	.remove = cs53l30_i2c_remove,
	.id_table = cs53l30_id,
};

module_i2c_driver(cs53l30_i2c_driver);

MODULE_DESCRIPTION("ASoC CS53L30 codec driver");
MODULE_AUTHOR("Caleb Crome <caleb@signalessence.com>");
MODULE_LICENSE("GPL");
