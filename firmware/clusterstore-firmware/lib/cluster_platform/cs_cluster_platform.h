#ifndef CS_CLUSTER_PLATFORM_H
#define CS_CLUSTER_PLATFORM_H

#include <stdbool.h>
#include <stdint.h>

#define CS_CAN_CLASSIC_MAX_DLC 8U

typedef enum {
    CS_STATUS_OK = 0,
    CS_STATUS_ERROR = 1,
    CS_STATUS_INVALID_ARGUMENT = 2,
    CS_STATUS_UNSUPPORTED = 3,
    CS_STATUS_TIMEOUT = 4,
    CS_STATUS_NOT_FOUND = 5
} cs_status_t;

typedef enum {
    CS_LOG_LEVEL_DEBUG = 0,
    CS_LOG_LEVEL_INFO = 1,
    CS_LOG_LEVEL_WARN = 2,
    CS_LOG_LEVEL_ERROR = 3
} cs_log_level_t;

typedef struct {
    uint32_t id;
    uint8_t dlc;
    uint8_t data[CS_CAN_CLASSIC_MAX_DLC];
} cs_can_frame_t;

typedef cs_status_t (*cs_flash_read_fn)(void *context,
                                        uint32_t address,
                                        void *buffer,
                                        uint32_t length);
typedef cs_status_t (*cs_flash_write_fn)(void *context,
                                         uint32_t address,
                                         const void *data,
                                         uint32_t length);
typedef cs_status_t (*cs_flash_erase_fn)(void *context,
                                         uint32_t address,
                                         uint32_t length);
typedef cs_status_t (*cs_can_send_fn)(void *context,
                                      const cs_can_frame_t *frame);
typedef bool (*cs_can_receive_fn)(void *context, cs_can_frame_t *frame);
typedef uint32_t (*cs_monotonic_ms_fn)(void *context);
typedef void (*cs_log_write_fn)(void *context,
                                cs_log_level_t level,
                                const char *message);

typedef struct {
    void *context;
    uint32_t page_size_bytes;
    cs_flash_read_fn read;
    cs_flash_write_fn write;
    cs_flash_erase_fn erase;
} cs_flash_ops_t;

typedef struct {
    void *context;
    cs_can_send_fn send;
    cs_can_receive_fn receive;
} cs_can_ops_t;

typedef struct {
    void *context;
    cs_monotonic_ms_fn monotonic_ms;
} cs_time_ops_t;

typedef struct {
    void *context;
    cs_log_write_fn write;
} cs_log_ops_t;

typedef struct {
    cs_flash_ops_t flash;
    cs_can_ops_t can;
    cs_time_ops_t time;
    cs_log_ops_t log;
} cs_platform_t;

void cs_platform_init(cs_platform_t *platform);
cs_status_t cs_platform_flash_read(const cs_platform_t *platform,
                                   uint32_t address,
                                   void *buffer,
                                   uint32_t length);
cs_status_t cs_platform_flash_write(const cs_platform_t *platform,
                                    uint32_t address,
                                    const void *data,
                                    uint32_t length);
cs_status_t cs_platform_flash_erase(const cs_platform_t *platform,
                                    uint32_t address,
                                    uint32_t length);
cs_status_t cs_platform_can_send(const cs_platform_t *platform,
                                 const cs_can_frame_t *frame);
bool cs_platform_can_receive(const cs_platform_t *platform,
                             cs_can_frame_t *frame);
uint32_t cs_platform_monotonic_ms(const cs_platform_t *platform);
void cs_platform_log(const cs_platform_t *platform,
                     cs_log_level_t level,
                     const char *message);

#endif
