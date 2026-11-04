"""Opt-in SD v2 CMD6 negotiation; raise read clock only, without read retries."""
import hashlib
from pathlib import Path

OPTION='ESPCLAW_SD_SPI_HIGHSPEED'
RELATIVE='drivers/mmcsd/mmcsd_spi.c'
BEGIN='/* ESPCLAW SD SPI high-speed negotiation */'
FUNCTION=r'''/* ESPCLAW SD SPI high-speed negotiation */
#ifdef CONFIG_ESPCLAW_SD_SPI_HIGHSPEED
static int mmcsd_fast_read_setup(FAR struct mmcsd_slot_s *slot)
{
  static const struct mmcsd_cmdinfo_s command = {0x46, MMCSD_CMDRESP_R1, 0xff};
  uint8_t status[64];
  slot->fast_read_clock = 0;
  if (!IS_SDV2(slot->type)) return OK;
  /* Optional check-mode rejection leaves the default-speed card untouched.
   * After an accepted command, missing status is a real transfer failure. */
  int response = mmcsd_sendcmd(slot, &command, 0x00fffff1);
  if (response != MMCSD_SPIR1_OK) return OK;
  if (mmcsd_recvblock(slot, status, sizeof(status)) != OK) return -EIO;
  if (!(status[13] & 2) || (status[29] & 2)) return OK;
  response = mmcsd_sendcmd(slot, &command, 0x80fffff1);
  if (response != MMCSD_SPIR1_OK) return -EIO;
  if (mmcsd_recvblock(slot, status, sizeof(status)) != OK) return -EIO;
  if ((status[16] & 15) != 1 || (status[29] & 2)) return -EIO;
  uint32_t actual = SPI_SETFREQUENCY(slot->spi, 40000000);
  SPI_SETFREQUENCY(slot->spi, slot->spispeed);
  if (actual > 40000000 || actual == 0) return -EIO;
  slot->fast_read_clock = actual;
  syslog(LOG_INFO, "SD_SPI high_speed=1 read_hz=%lu write_hz=%lu dma=0\n",
         (unsigned long)actual, (unsigned long)slot->spispeed);
  return OK;
}
#endif
/* ESPCLAW SD SPI high-speed negotiation end */

'''


def replace_once(s, before, after):
    if s.count(before)!=1:raise RuntimeError('Unrecognized SD SPI source: '+before[:70])
    return s.replace(before,after,1)


def transform(source):
    if BEGIN in source:
        if source.count(FUNCTION)!=1:raise RuntimeError('Unknown high-speed negotiation')
        return source
    s=replace_once(source,'  uint32_t spispeed;',
                  '#ifdef CONFIG_ESPCLAW_SD_SPI_HIGHSPEED\n  uint32_t fast_read_clock;\n#endif\n  uint32_t spispeed;')
    s=replace_once(s,'#include <debug.h>','#include <debug.h>\n#include <syslog.h>')
    # Function definitions are placed after receive/command helpers, before use.
    anchor='static int mmcsd_mediainitialize(FAR struct mmcsd_slot_s *slot)\n{'
    s=replace_once(s,anchor,FUNCTION+anchor)
    s=replace_once(s,'  mmcsd_decodecsd(slot, csd);\n  mmcsd_checkwrprotect(slot, csd);',
'''  mmcsd_decodecsd(slot, csd);
  mmcsd_checkwrprotect(slot, csd);
#ifdef CONFIG_ESPCLAW_SD_SPI_HIGHSPEED
  if (mmcsd_fast_read_setup(slot) != OK)
    {
      SPI_SELECT(spi, SPIDEV_MMCSD(0), false);
      return -EIO;
    }
#endif''')
    s=replace_once(s,'retry:\n  SPI_SELECT(spi, SPIDEV_MMCSD(0), true);\n\n  /* Single or multiple block read? */',
'''retry:
#ifdef CONFIG_ESPCLAW_SD_SPI_HIGHSPEED
  if (slot->fast_read_clock) SPI_SETFREQUENCY(spi, slot->fast_read_clock);
#endif
  SPI_SELECT(spi, SPIDEV_MMCSD(0), true);

  /* Single or multiple block read? */''')
    return s


def apply(nuttx):
    root=Path(nuttx);path=root/RELATIVE
    s=transform(path.read_text());path.write_text(s)
    k=root/'drivers/mmcsd/Kconfig';s=k.read_text()
    if 'config '+OPTION not in s:
        s+='\nconfig '+OPTION+'\n\tbool "SD v2 negotiated 40 MHz SPI reads"\n\tdepends on MMCSD_SPI\n\tdefault n\n'
        k.write_text(s)
    return {'source':RELATIVE,'sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
            'read_only_clock_boost':True,'requested_read_hz':40000000,
            'cmd6_required':True,'dma_enabled':False,'read_retry_added':False}
