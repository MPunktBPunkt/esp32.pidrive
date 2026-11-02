#pragma once

#include <Arduino.h>

#ifndef FW_VERSION
#define FW_VERSION "0.4.61-dev"
#endif
/* Force a compile-time string even if -DFW_VERSION is stripped by env unflags. */
#ifndef FW_VERSION_STR
#define FW_VERSION_STR FW_VERSION
#endif
#ifndef HUB_HOST_DEFAULT
#define HUB_HOST_DEFAULT "192.168.178.113"
#endif
#ifndef HUB_PORT_DEFAULT
#define HUB_PORT_DEFAULT 8093
#endif
#ifndef DEVICE_NAME_DEFAULT
#define DEVICE_NAME_DEFAULT "PiDrive-USB"
#endif
#ifndef FW_TYPE
#define FW_TYPE "pidrive"
#endif

#define EVENT_RING_SIZE 128
#define MENU_MAX_ITEMS 32
