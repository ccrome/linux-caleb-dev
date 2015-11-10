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
struct tlv320dac3100_priv {
	struct snd_soc_codec *codec;
	struct regmap *regmap;
};

// read only registers are 0x01, 0x02, 0x05, 0x36
static const struct reg_default tlv320dac3100_reg[] = {
	{0x00, 0x00}, {0x01, 0x00},               {0x03, 0x66},
	{0x04, 0x00}, {0x05, 0x11}, {0x06, 0x04}, {0x07, 0x00},
	{0x08, 0x00},                             {0x0b, 0x01},
	{0x0c, 0x01}, {0x0d, 0x00}, {0x0e, 0x80}, 
	{0x18, 0x00}, {0x19, 0x00}, {0x1a, 0x01}, {0x1b, 0x00},
	{0x1c, 0x00}, {0x1d, 0x00}, {0x1e, 0x01}, {0x1f, 0x00},
	{0x20, 0x00}, {0x21, 0x00}, {0x22, 0x00}, 
	/*.......*/   {0x25, 0x00}, {0x26, 0x00}, {0x27, 0x00},
	{0x2c, 0x00},               {0x2e, 0x00}, 
	{0x30, 0x00}, {0x31, 0x00},               {0x33, 0x02},
	/*.....................*/   {0x36, 0x03}, 
	{0x3c, 0x01},                             {0x3f, 0x14},
	{0x40, 0x0c}, {0x41, 0x00}, {0x42, 0x00}, {0x43, 0x00},
	{0x44, 0x6f}, {0x45, 0x38}, {0x46, 0x00}, {0x47, 0x00},
	{0x48, 0x00}, {0x49, 0x00}, {0x4a, 0x00}, {0x4b, 0xee},
	{0x4c, 0x10}, {0x4d, 0xd8}, {0x4e, 0x7e}, {0x4f, 0xe3},
	/*.....................................*/ {0x73, 0x00},
	{0x74, 0x00}, {0x75, 0x7e}
};


static const struct regmap_config tlv320dac3100_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = 0x75,
	.reg_defaults = tlv320dac3100_reg,
	.num_reg_defaults = ARRAY_SIZE(tlv320dac3100_reg),
	.cache_type = REGCACHE_RBTREE,
};
static int tlv320dac3100_hw_params(struct snd_pcm_substream *substream,
			     struct snd_pcm_hw_params *params,
			     struct snd_soc_dai *dai)
{
	struct snd_soc_codec *codec = dai->codec;
	struct tlv320dac3100_priv *tlv320dac3100 = snd_soc_codec_get_drvdata(codec);
	/*
	 * valid rates are 16k and 48k
	 */
	int fs = (params_rate(params));
	switch (fs) {
	case 48000:
		break;
	case 16000:
		break;
	default:
		/* unsupported rate */
		return -EINVAL;
	}
	return 0;
}
static int tlv320dac3100_init(struct snd_soc_codec *codec)
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
	//struct tlv320dac31001_priv *tlv320dac3100 = snd_soc_codec_get_drvdata(codec);
	printk(KERN_INFO "*** %s\n", __func__);
	// follow data sheet
	ret = snd_soc_write(codec, 0x00, 0x00);
	if (ret) {
		printk("Couldn't write to DAC for some reason.  That's not good");
		return -EINVAL;
	}
	snd_soc_write(codec, 0x01, 0x01);
	
	//DAC_CLK
	//DAC_MOD_CLK == modulator clock.
	//DOSR = 256
	//MDAC = 1
	//DAC_MOD_CLK     = fs * DOSR   = 16k*256    48k*256   =  4.096,     12.288
	//DAC_CLK == BCLK = fs*DOSR*MDAC=                         4.096MHz   12.288MHz
	//NDAC = 1 for 48kHz
	//       3 for 16kHz
	snd_soc_write(codec, 0x0b, (1<<7) | 3  ); // set NDAC 
	snd_soc_write(codec, 0X0c, (1<<7) | 1  ); // set MDAC
	snd_soc_write(codec, 0x0d,  1);           // DOSR [9:8]
	snd_soc_write(codec, 0x0e,  0);           // DOSR [7:0]
	snd_soc_write(codec, 0x1b, (1<<6) | (0 << 4) | (1<<3) | (1<<2)); // DSP, 16-bits, master
	snd_soc_write(codec, 0x1e, (1<<7) | 1<<0); // bclk divider = 1
	snd_soc_write(codec, 0x3f, (1<<7) | (1<<6) | (1<<4) | (1<<2) );  // enable DAC.
	snd_soc_write(codec, 0x40, 0); // unmute dacs.

	// select page 1
	snd_soc_write(codec, 0, 1);

	snd_soc_write(codec, 0x1f, (1<<2) | (1<<7) | (1<<6) | (3<<3)); // power up headphones.
	snd_soc_write(codec, 0x20, (1<<7) | (3<<1));// power up class D
	snd_soc_write(codec, 0x23, (1<<6) | (1<<2)); // connect DAC_L and R to the output mixers.
	snd_soc_write(codec, 0x24, (1<<7) | (0<<0)); 
	snd_soc_write(codec, 0x25, (1<<7) | (0<<0));
	snd_soc_write(codec, 0x26, (1<<7) | (0<<0));
	snd_soc_write(codec, 0x28, (1<<2)); // hpl driver
	snd_soc_write(codec, 0x29, (1<<2)); // hpr driver
	snd_soc_write(codec, 0x2a, (1<<3) | (1<<2)); // enable class-d & set volume
	
	// select page 0
	snd_soc_write(codec, 0, 0);
	
	// DAC_FS = 16000
	// DAC_MOD_CLK = FS * 128
	// DAC_CLK
	// DOSR = 128
	// DAC_MOD_CLK =
	printk(KERN_INFO "***--- %s\n", __func__);
	return 0;
}

static int tlv320dac3100_probe(struct snd_soc_codec *codec)
{
	struct tlv320dac3100_priv *tlv320dac3100 = snd_soc_codec_get_drvdata(codec);
	//int ret, i;
	printk(KERN_INFO "*** %s\n", __func__);

	tlv320dac3100->codec = codec;

	tlv320dac3100_init(codec);

	// tlv320dac3100_add_widgets(codec);
	printk(KERN_INFO "***--- %s\n", __func__);
	
	return 0;
}

static int tlv320dac3100_remove(struct snd_soc_codec *codec)
{
	//struct tlv320dac3100_priv *tlv320dac3100 = snd_soc_codec_get_drvdata(codec);
	return 0;
}

static struct snd_soc_codec_driver soc_codec_dev_tlv320dac3100 = {
	.probe = tlv320dac3100_probe,
	.remove = tlv320dac3100_remove,
//	.controls = tlv320dac3100_snd_controls,
//	.num_controls = ARRAY_SIZE(tlv320dac3100_snd_controls),
//	.dapm_widgets = tlv320dac3100_dapm_widgets,
//	.num_dapm_widgets = ARRAY_SIZE(tlv320dac3100_dapm_widgets),
//	.dapm_routes = intercon,
//	.num_dapm_routes = ARRAY_SIZE(intercon),
};

static const struct snd_soc_dai_ops tlv320dac3100_dai_ops = {
	.hw_params	= tlv320dac3100_hw_params,
	// .prepare	= tlv320dac3100_prepare,
	// .digital_mute	= tlv320dac3100_mute,
	// .set_sysclk	= tlv320dac3100_set_dai_sysclk,
	// .set_fmt	= tlv320dac3100_set_dai_fmt,
	// .set_tdm_slot	= tlv320dac3100_set_dai_tdm_slot,
};

static struct snd_soc_dai_driver tlv320dac3100_dai = {
	.name = "tlv320dac3100-hifi",
	.capture = {
		.stream_name = "Capture",
		.channels_min = 0,
		.channels_max = 0,
		.rates = SNDRV_PCM_RATE_16000,
		.formats =SNDRV_PCM_FMTBIT_S16_LE,
	},
	.playback = {
		.stream_name = "Playback",
		.channels_min = 2,
		.channels_max = 16,
		.rates = SNDRV_PCM_RATE_16000,
		.formats =SNDRV_PCM_FMTBIT_S16_LE,
	},
	.ops = &tlv320dac3100_dai_ops,
	.symmetric_rates = 1,
};

static int tlv320dac3100_i2c_probe(struct i2c_client *i2c,
			   const struct i2c_device_id *id)
{
	struct tlv320dac3100_priv *tlv320dac3100;
	int ret;
	// struct device_node *np = i2c->dev.of_node;
	printk(KERN_INFO "*** %s\n", __func__);
	tlv320dac3100 = devm_kzalloc(&i2c->dev, sizeof(struct tlv320dac3100_priv), GFP_KERNEL);
	if (!tlv320dac3100)
		return -ENOMEM;
	tlv320dac3100->regmap = devm_regmap_init_i2c(i2c, &tlv320dac3100_regmap);
	if (IS_ERR(tlv320dac3100->regmap)) {
		ret = PTR_ERR(tlv320dac3100->regmap);
		return ret;
	}
	regcache_cache_only(tlv320dac3100->regmap, false);

	i2c_set_clientdata(i2c, tlv320dac3100);
	ret = snd_soc_register_codec(&i2c->dev,
			&soc_codec_dev_tlv320dac3100, &tlv320dac3100_dai, 1);
	if (ret != 0)
		goto err;

	printk(KERN_INFO "***--- %s\n", __func__);
	return 0;

err:
	return ret;
}


static int tlv320dac3100_i2c_remove(struct i2c_client *client)
{
	// struct tlv320dac3100_priv *tlv320dac3100 = i2c_get_clientdata(client);
	snd_soc_unregister_codec(&client->dev);
	return 0;
}

static const struct i2c_device_id tlv320dac3100_id[] = {
	{"tlv320dac3100", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, tlv320dac3100_id);

static const struct of_device_id tlv320dac3100_of_match[] = {
	{ .compatible = "ci,tlv320dac3100", },
	{},
};
MODULE_DEVICE_TABLE(of, tlv320dac3100_of_match);

static struct i2c_driver tlv320dac3100_i2c_driver = {
	.driver = {
		.name = "tlv320dac3100-codec",
		.of_match_table = of_match_ptr(tlv320dac3100_of_match),
	},
	.probe	= tlv320dac3100_i2c_probe,
	.remove = tlv320dac3100_i2c_remove,
	.id_table = tlv320dac3100_id,
};

module_i2c_driver(tlv320dac3100_i2c_driver);

MODULE_DESCRIPTION("ASoC TLV320DAC3100 codec driver");
MODULE_AUTHOR("Caleb Crome <caleb@signalessence.com>");
MODULE_LICENSE("GPL");
