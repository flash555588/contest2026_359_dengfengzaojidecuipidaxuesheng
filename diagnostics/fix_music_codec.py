from pathlib import Path
ws=Path(__file__).resolve().parent.parent
relative=Path('nuttx/drivers/audio/es8311.c')
target=ws/'04-v3-20260913/espdl-quickapp/overlay'/relative
target.parent.mkdir(parents=True,exist_ok=True)
if not target.exists(): target.write_bytes((Path('/tmp/v3-desktop-espdl-20260915')/relative).read_bytes())
s=target.read_text()
for call in ('es8311_setsamplerate', 'es8311_setbitspersample'):
    old='ret = '+call+'(priv) == -ENOTTY ? OK : ret;'
    s=s.replace(old,'ret = '+call+'(priv);\n        if (ret == -ENOTTY) ret = OK;')
# I2S clock divider is changed by the sample-rate setters; choose codec
# coefficients using that resulting MCLK, not the previous stream's clock.
old='''  priv->mclk = I2S_GETMCLKFREQUENCY(priv->i2s);
  coeff_index = es8311_getcoeff(priv, priv->samprate);

  if (coeff_index < 0)
    {
      auderr("Failed to set sample rate: %d\\n", -EINVAL);
      return -EINVAL;
    }

'''
if old in s:
    s=s.replace(old,'',1)
    needle='''  ret = 0;
  regconfig = es8311_readreg(priv, ES8311_CLK_MANAGER_REG02) & 0x07;'''
    assert needle in s
    s=s.replace(needle,old+needle,1)
target.write_text(s)
print('ES8311 configuration now propagates actual results and uses the new sample clock')
