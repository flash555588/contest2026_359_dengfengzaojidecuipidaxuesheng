/****************************************************************************
 * boards/risc-v/esp32p4/esp32p4-function-ev-board/src/esp32p4_camera.c
 *
 * SPDX-License-Identifier: Apache-2.0
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <syslog.h>

#include <nuttx/i2c/i2c_master.h>
#include <nuttx/kmalloc.h>
#include <nuttx/video/imgdata.h>
#include <nuttx/video/imgsensor.h>
#include <nuttx/video/sc2336.h>
#include <nuttx/video/v4l2_cap.h>

#include "espressif/esp_i2c.h"
#include "espressif/esp_ldo.h"
#include "espressif/esp_mipi_csi.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define CAMERA_I2C_PORT       0
#define CAMERA_DPHY_LDO_CHAN  3
#define CAMERA_DPHY_LDO_MV    2500
#define CAMERA_WIDTH          1024
#define CAMERA_HEIGHT         600

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct esp_ldo_config_t g_camera_ldo =
{
  .chan_id = CAMERA_DPHY_LDO_CHAN,
  .voltage_mv = CAMERA_DPHY_LDO_MV,
};

static bool g_camera_registered;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int board_camera_initialize(void)
{
  FAR struct i2c_master_s *i2c;
  FAR struct imgsensor_s *sensor;
  FAR struct imgsensor_s *sensors[1];
  FAR struct imgdata_s *data;
  int ret;

  if (g_camera_registered)
    {
      return OK;
    }

  ret = esp_ldo_channel_acquire(&g_camera_ldo);
  if (ret < 0)
    {
      syslog(LOG_ERR, "Camera: MIPI D-PHY LDO failed: %d\n", ret);
      return ret;
    }

  i2c = esp_i2cbus_initialize(CAMERA_I2C_PORT);
  if (i2c == NULL)
    {
      syslog(LOG_ERR, "Camera: failed to acquire I2C%d\n",
             CAMERA_I2C_PORT);
      ret = -ENODEV;
      goto release_ldo;
    }

  sensor = sc2336_initialize(i2c, CAMERA_WIDTH, CAMERA_HEIGHT);
  if (sensor == NULL)
    {
      syslog(LOG_ERR, "Camera: failed to allocate SC2336 driver\n");
      ret = -ENOMEM;
      goto release_i2c;
    }

  if (!IMGSENSOR_IS_AVAILABLE(sensor))
    {
      syslog(LOG_ERR, "Camera: SC2336 not detected on SCCB 0x30\n");
      ret = -ENODEV;
      goto free_sensor;
    }

  data = esp_mipi_csi_initialize();
  if (data == NULL)
    {
      syslog(LOG_ERR, "Camera: failed to create MIPI-CSI data path\n");
      ret = -ENODEV;
      goto free_sensor;
    }

  sensors[0] = sensor;
  ret = capture_register("/dev/video0", data, sensors, 1);
  if (ret < 0)
    {
      syslog(LOG_ERR, "Camera: /dev/video0 registration failed: %d\n",
             ret);
      goto free_sensor;
    }

  g_camera_registered = true;
  syslog(LOG_INFO,
         "Camera: SC2336 1024x600 RGB565 registered at /dev/video0\n");
  return OK;

free_sensor:
  kmm_free(sensor);
release_i2c:
  esp_i2cbus_uninitialize(i2c);
release_ldo:
  esp_ldo_channel_release(&g_camera_ldo);
  return ret;
}
