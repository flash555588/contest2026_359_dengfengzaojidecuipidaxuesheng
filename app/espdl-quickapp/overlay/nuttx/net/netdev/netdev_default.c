/****************************************************************************
 * net/netdev/netdev_default.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>
#include <netinet/in.h>

#include <nuttx/net/netdev.h>

#include "utils/utils.h"
#include "netdev/netdev.h"

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: netdev_default
 *
 * Description:
 *   Return the default network device.  REVISIT:  At present this function
 *   arbitrarily returns the first UP device at the head of the device
 *   list.  Perhaps the default device should be a device name
 *   configuration option?
 *
 *   So why is this here:  It represents my current though for what to do
 *   if a socket is connected with INADDY_ANY.  In this case, I suppose we
 *   should use the IP address associated with some default device???
 *
 * Input Parameters:
 *   NULL
 *
 * Returned Value:
 *  Pointer to default network driver on success; null on failure
 *
 ****************************************************************************/

FAR struct net_driver_s *netdev_default(void)
{
  FAR struct net_driver_s *ret = NULL;
  FAR struct net_driver_s *dev;
#if defined(CONFIG_NET_IPv4) && !defined(CONFIG_NET_IPv6)
  FAR struct net_driver_s *addressed = NULL;
#endif

  /* Examine each registered network device */

  netdev_list_lock();
  for (dev = g_netdevices; dev; dev = dev->flink)
    {
      /* Is the interface in the "up" state? */

      if (IFF_IS_RUNNING(dev->d_flags) != 0)
        {
          /* Return a reference to the first device that we find in the UP
           * state (but not the loopback device unless it is the only
           * device).
           */

          if (dev->d_lltype != NET_LL_LOOPBACK &&
              dev->d_lltype != NET_LL_CAN)
            {
#if defined(CONFIG_NET_IPv4) && !defined(CONFIG_NET_IPv6)
              /* Carrier alone is insufficient for IPv4 traffic: an Ethernet
               * port awaiting DHCP must not steal the default from Wi-Fi.
               * Keep registration order among usable gateways (eth0 first),
               * then addressed interfaces, then the original carrier-only
               * fallback so initial DHCP broadcasts can still be sent.
               */

              if (ret == NULL)
                {
                  ret = dev;
                }

              if (dev->d_ipaddr != INADDR_ANY)
                {
                  if (addressed == NULL)
                    {
                      addressed = dev;
                    }

                  if (dev->d_draddr != INADDR_ANY)
                    {
                      addressed = dev;
                      break;
                    }
                }
#else
              ret = dev;
              break;
#endif
            }
        }
    }

  netdev_list_unlock();
#if defined(CONFIG_NET_IPv4) && !defined(CONFIG_NET_IPv6)
  if (addressed != NULL)
    {
      ret = addressed;
    }
#endif
  return ret;
}
