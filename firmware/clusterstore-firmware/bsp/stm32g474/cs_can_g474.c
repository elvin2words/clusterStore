#include "cs_can_g474.h"
#include <stddef.h>
#include <string.h>

#ifdef CS_G474_USE_HAL
#include "stm32g4xx_hal.h"
#endif

static uint16_t cs_can_g474_ring_next(uint16_t index) {
    return (uint16_t)((index + 1U) % CS_G474_CAN_RX_RING_CAPACITY);
}

static void cs_can_g474_ring_push(cs_g474_can_t *can_bus,
                                  const cs_can_frame_t *frame) {
    uint16_t next_head;

    next_head = cs_can_g474_ring_next(can_bus->rx_head);
    if (next_head == can_bus->rx_tail) {
        can_bus->rx_tail = cs_can_g474_ring_next(can_bus->rx_tail);
    }

    can_bus->rx_ring[can_bus->rx_head] = *frame;
    can_bus->rx_head = next_head;
}

#ifdef CS_G474_USE_HAL
static uint32_t cs_can_g474_dlc_to_hal(uint8_t dlc) {
    switch (dlc) {
        case 0U:
            return FDCAN_DLC_BYTES_0;
        case 1U:
            return FDCAN_DLC_BYTES_1;
        case 2U:
            return FDCAN_DLC_BYTES_2;
        case 3U:
            return FDCAN_DLC_BYTES_3;
        case 4U:
            return FDCAN_DLC_BYTES_4;
        case 5U:
            return FDCAN_DLC_BYTES_5;
        case 6U:
            return FDCAN_DLC_BYTES_6;
        case 7U:
            return FDCAN_DLC_BYTES_7;
        case 8U:
        default:
            return FDCAN_DLC_BYTES_8;
    }
}

static uint8_t cs_can_g474_dlc_from_hal(uint32_t dlc) {
    switch (dlc) {
        case FDCAN_DLC_BYTES_0:
            return 0U;
        case FDCAN_DLC_BYTES_1:
            return 1U;
        case FDCAN_DLC_BYTES_2:
            return 2U;
        case FDCAN_DLC_BYTES_3:
            return 3U;
        case FDCAN_DLC_BYTES_4:
            return 4U;
        case FDCAN_DLC_BYTES_5:
            return 5U;
        case FDCAN_DLC_BYTES_6:
            return 6U;
        case FDCAN_DLC_BYTES_7:
            return 7U;
        case FDCAN_DLC_BYTES_8:
        default:
            return 8U;
    }
}

static cs_status_t cs_can_g474_apply_default_timing(cs_g474_can_t *can_bus) {
    FDCAN_HandleTypeDef *handle;

    handle = (FDCAN_HandleTypeDef *)can_bus->hfdcan;
    if (handle == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    handle->Init.FrameFormat = FDCAN_FRAME_CLASSIC;
    handle->Init.Mode = FDCAN_MODE_NORMAL;
    handle->Init.AutoRetransmission = ENABLE;
    handle->Init.TransmitPause = DISABLE;
    handle->Init.ProtocolException = DISABLE;
    handle->Init.NominalPrescaler = 10U;
    handle->Init.NominalSyncJumpWidth = 2U;
    handle->Init.NominalTimeSeg1 = 13U;
    handle->Init.NominalTimeSeg2 = 2U;
    handle->Init.DataPrescaler = 1U;
    handle->Init.DataSyncJumpWidth = 1U;
    handle->Init.DataTimeSeg1 = 1U;
    handle->Init.DataTimeSeg2 = 1U;
    handle->Init.StdFiltersNbr = 1U;
    handle->Init.ExtFiltersNbr = 1U;
    handle->Init.RxFifo0ElmtsNbr = 16U;
    handle->Init.RxFifo0ElmtSize = FDCAN_DATA_BYTES_8;
    handle->Init.RxBuffersNbr = 0U;
    handle->Init.TxEventsNbr = 0U;
    handle->Init.TxBuffersNbr = 0U;
    handle->Init.TxFifoQueueElmtsNbr = 16U;
    handle->Init.TxFifoQueueMode = FDCAN_TX_FIFO_OPERATION;
    handle->Init.TxElmtSize = FDCAN_DATA_BYTES_8;
    return HAL_FDCAN_Init(handle) == HAL_OK ? CS_STATUS_OK : CS_STATUS_ERROR;
}

static cs_status_t cs_can_g474_configure_accept_all(cs_g474_can_t *can_bus) {
    FDCAN_HandleTypeDef *handle;
    FDCAN_FilterTypeDef standard_filter;
    FDCAN_FilterTypeDef extended_filter;

    handle = (FDCAN_HandleTypeDef *)can_bus->hfdcan;
    if (handle == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    memset(&standard_filter, 0, sizeof(standard_filter));
    standard_filter.IdType = FDCAN_STANDARD_ID;
    standard_filter.FilterIndex = 0U;
    standard_filter.FilterType = FDCAN_FILTER_MASK;
    standard_filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    standard_filter.FilterID1 = 0U;
    standard_filter.FilterID2 = 0U;

    memset(&extended_filter, 0, sizeof(extended_filter));
    extended_filter.IdType = FDCAN_EXTENDED_ID;
    extended_filter.FilterIndex = 0U;
    extended_filter.FilterType = FDCAN_FILTER_MASK;
    extended_filter.FilterConfig = FDCAN_FILTER_TO_RXFIFO0;
    extended_filter.FilterID1 = 0U;
    extended_filter.FilterID2 = 0U;

    if (HAL_FDCAN_ConfigFilter(handle, &standard_filter) != HAL_OK ||
        HAL_FDCAN_ConfigFilter(handle, &extended_filter) != HAL_OK ||
        HAL_FDCAN_ConfigGlobalFilter(handle,
                                     FDCAN_ACCEPT_IN_RX_FIFO0,
                                     FDCAN_ACCEPT_IN_RX_FIFO0,
                                     FDCAN_REJECT_REMOTE,
                                     FDCAN_REJECT_REMOTE) != HAL_OK) {
        return CS_STATUS_ERROR;
    }

    return CS_STATUS_OK;
}
#endif

cs_status_t cs_can_g474_init(cs_g474_can_t *can_bus,
                             const cs_g474_can_config_t *config) {
    if (can_bus == NULL || config == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    memset(can_bus, 0, sizeof(*can_bus));
    can_bus->hfdcan = config->hfdcan;
    can_bus->kernel_clock_hz = config->kernel_clock_hz == 0U
                                   ? CS_G474_FDCAN_DEFAULT_KERNEL_CLOCK_HZ
                                   : config->kernel_clock_hz;
    can_bus->nominal_bitrate = config->nominal_bitrate == 0U
                                   ? CS_G474_FDCAN_DEFAULT_NOMINAL_BITRATE
                                   : config->nominal_bitrate;
    can_bus->enable_rx_fifo0_irq = config->enable_rx_fifo0_irq;
    return CS_STATUS_OK;
}

void cs_can_g474_push_rx_frame(cs_g474_can_t *can_bus, const cs_can_frame_t *frame) {
    if (can_bus == NULL || frame == NULL) {
        return;
    }

    cs_can_g474_ring_push(can_bus, frame);
}

cs_status_t cs_can_g474_start(cs_g474_can_t *can_bus) {
    if (can_bus == NULL || can_bus->hfdcan == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

    if (can_bus->kernel_clock_hz != CS_G474_FDCAN_DEFAULT_KERNEL_CLOCK_HZ ||
        can_bus->nominal_bitrate != CS_G474_FDCAN_DEFAULT_NOMINAL_BITRATE) {
        return CS_STATUS_UNSUPPORTED;
    }

#ifdef CS_G474_USE_HAL
    if (cs_can_g474_apply_default_timing(can_bus) != CS_STATUS_OK ||
        cs_can_g474_configure_accept_all(can_bus) != CS_STATUS_OK ||
        HAL_FDCAN_Start((FDCAN_HandleTypeDef *)can_bus->hfdcan) != HAL_OK) {
        return CS_STATUS_ERROR;
    }

    if (can_bus->enable_rx_fifo0_irq != 0U &&
        HAL_FDCAN_ActivateNotification((FDCAN_HandleTypeDef *)can_bus->hfdcan,
                                       FDCAN_IT_RX_FIFO0_NEW_MESSAGE,
                                       0U) != HAL_OK) {
        return CS_STATUS_ERROR;
    }

    return CS_STATUS_OK;
#else
    return CS_STATUS_UNSUPPORTED;
#endif
}

cs_status_t cs_can_g474_send(void *context, const cs_can_frame_t *frame) {
    cs_g474_can_t *can_bus;

    can_bus = (cs_g474_can_t *)context;
    if (can_bus == NULL || frame == NULL || frame->dlc > CS_CAN_CLASSIC_MAX_DLC) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

#ifdef CS_G474_USE_HAL
    {
        FDCAN_TxHeaderTypeDef header;

        memset(&header, 0, sizeof(header));
        header.Identifier = frame->id;
        header.IdType = frame->id > 0x7FFU ? FDCAN_EXTENDED_ID : FDCAN_STANDARD_ID;
        header.TxFrameType = FDCAN_DATA_FRAME;
        header.DataLength = cs_can_g474_dlc_to_hal(frame->dlc);
        header.ErrorStateIndicator = FDCAN_ESI_ACTIVE;
        header.BitRateSwitch = FDCAN_BRS_OFF;
        header.FDFormat = FDCAN_CLASSIC_CAN;
        header.TxEventFifoControl = FDCAN_NO_TX_EVENTS;
        header.MessageMarker = 0U;

        return HAL_FDCAN_AddMessageToTxFifoQ((FDCAN_HandleTypeDef *)can_bus->hfdcan,
                                             &header,
                                             (uint8_t *)frame->data) == HAL_OK
                   ? CS_STATUS_OK
                   : CS_STATUS_ERROR;
    }
#else
    (void)frame;
    return CS_STATUS_UNSUPPORTED;
#endif
}

bool cs_can_g474_receive(void *context, cs_can_frame_t *frame) {
    cs_g474_can_t *can_bus;

    can_bus = (cs_g474_can_t *)context;
    if (can_bus == NULL || frame == NULL || can_bus->rx_head == can_bus->rx_tail) {
        return false;
    }

    *frame = can_bus->rx_ring[can_bus->rx_tail];
    can_bus->rx_tail = cs_can_g474_ring_next(can_bus->rx_tail);
    return true;
}

cs_status_t cs_can_g474_drain_rx_fifo0(cs_g474_can_t *can_bus) {
    if (can_bus == NULL || can_bus->hfdcan == NULL) {
        return CS_STATUS_INVALID_ARGUMENT;
    }

#ifdef CS_G474_USE_HAL
    while (HAL_FDCAN_GetRxFifoFillLevel((FDCAN_HandleTypeDef *)can_bus->hfdcan,
                                        FDCAN_RX_FIFO0) > 0U) {
        FDCAN_RxHeaderTypeDef header;
        cs_can_frame_t frame;

        memset(&frame, 0, sizeof(frame));
        if (HAL_FDCAN_GetRxMessage((FDCAN_HandleTypeDef *)can_bus->hfdcan,
                                   FDCAN_RX_FIFO0,
                                   &header,
                                   frame.data) != HAL_OK) {
            return CS_STATUS_ERROR;
        }

        frame.id = header.Identifier;
        frame.dlc = cs_can_g474_dlc_from_hal(header.DataLength);
        cs_can_g474_ring_push(can_bus, &frame);
    }

    return CS_STATUS_OK;
#else
    return CS_STATUS_UNSUPPORTED;
#endif
}
