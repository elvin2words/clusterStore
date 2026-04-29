#include "cs_iwdg_g474.h"
#include <stddef.h>
#include <string.h>

#ifdef CS_G474_USE_HAL
#include "stm32g4xx_hal.h"
#endif

cs_status_t cs_iwdg_g474_init(cs_g474_iwdg_t *watchdog,
                              const cs_g474_iwdg_config_t *config) {
    if (watchdog == NULL || config == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    memset(watchdog, 0, sizeof(*watchdog));
    watchdog->config = *config;
    if (config->hiwdg == NULL) {
#ifdef CS_G474_USE_HAL
        return CS_STATUS_INVALID_ARGUMENT;
#else
        return CS_STATUS_UNSUPPORTED;
#endif
    }

    if (watchdog->config.auto_start != 0U) {
        return cs_iwdg_g474_start(watchdog);
    }

    return CS_STATUS_OK;
}

cs_status_t cs_iwdg_g474_start(cs_g474_iwdg_t *watchdog) {
    if (watchdog == NULL || watchdog->config.hiwdg == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

#ifdef CS_G474_USE_HAL
    {
        IWDG_HandleTypeDef *handle;

        handle = (IWDG_HandleTypeDef *)watchdog->config.hiwdg;
        if (watchdog->config.prescaler != 0U) {
            handle->Init.Prescaler = watchdog->config.prescaler;
        }
        if (watchdog->config.reload != 0U) {
            handle->Init.Reload = watchdog->config.reload;
        }
        if (watchdog->config.window != 0U) {
            handle->Init.Window = watchdog->config.window;
        }

        if (HAL_IWDG_Init(handle) != HAL_OK) {
            return CS_STATUS_ERROR;
        }

        watchdog->started = 1U;
        return CS_STATUS_OK;
    }
#else
    return CS_STATUS_UNSUPPORTED;
#endif
}

cs_status_t cs_iwdg_g474_kick(cs_g474_iwdg_t *watchdog) {
    if (watchdog == NULL || watchdog->config.hiwdg == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

#ifdef CS_G474_USE_HAL
    return HAL_IWDG_Refresh((IWDG_HandleTypeDef *)watchdog->config.hiwdg) == HAL_OK
               ? CS_STATUS_OK
               : CS_STATUS_ERROR;
#else
    return CS_STATUS_UNSUPPORTED;
#endif
}
