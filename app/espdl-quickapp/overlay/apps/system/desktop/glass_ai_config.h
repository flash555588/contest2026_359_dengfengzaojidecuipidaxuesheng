/* SPDX-License-Identifier: Apache-2.0 */
#pragma once
#include <stdbool.h>
/* Standalone service fixtures have no desktop lifecycle. The enabled lifecycle
 * is exercised separately with the same build define as the firmware. */
#ifdef GLASS_AI_CONFIG_ENABLED
bool glass_ai_config_active(void);
bool glass_ai_start_enter(void);
void glass_ai_start_leave(void);
int glass_ai_config_begin(unsigned timeout_ms);
void glass_ai_config_end(void);
bool glass_ai_config_ui_pending(void);
void glass_ai_config_ui_ack(void);
#else
static inline bool glass_ai_config_active(void){return false;}
static inline bool glass_ai_start_enter(void){return true;}
static inline void glass_ai_start_leave(void){}
static inline int glass_ai_config_begin(unsigned ms){(void)ms;return 0;}
static inline void glass_ai_config_end(void){}
static inline bool glass_ai_config_ui_pending(void){return false;}
static inline void glass_ai_config_ui_ack(void){}
#endif
