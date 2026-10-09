/* Host faults around the actual extracted SDK s_i2c_send_commands function.
 * Only RTOS/HAL calls are doubles; no copied implementation of its wait loop.
 */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <string.h>

typedef uint32_t TickType_t;
enum { pdTRUE = 1, I2C_STATUS_IDLE, I2C_STATUS_ACK_ERROR, I2C_STATUS_TIMEOUT,
       I2C_STATUS_DONE, I2C_EVENT_ALIVE, I2C_EVENT_DONE, I2C_EVENT_NACK,
       I2C_LL_CMD_STOP, I2C_LL_CMD_WRITE, I2C_LL_CMD_READ };
typedef int i2c_master_event_t;
typedef struct { int op_code; } i2c_ll_hw_cmd_t;
typedef struct { i2c_ll_hw_cmd_t hw_cmd; } i2c_operation_t;
typedef struct { void *dev; } i2c_hal_context_t;
typedef struct { i2c_hal_context_t hal; int spinlock; } bus_base_t;
typedef struct {
    bus_base_t *base;
    int event_queue, cmd_semphr, event;
    atomic_int status;
    unsigned cmd_idx, trans_idx;
    struct { unsigned cmd_count; i2c_operation_t *ops; } i2c_trans;
    bool bypass_nack_log;
} bus_t;
typedef bus_t *i2c_master_bus_handle_t;

static struct {
    TickType_t start, last_tick;
    unsigned tick_calls, tick_divisor, busy_calls, busy_count;
    unsigned queue_resets, queue_receives, semaphore_gives, starts, resets;
    unsigned logs, event_logs, commands;
    bool forever, queue_ok, sem_ok, reset_clear;
    int event, reset_result;
} fake;
static unsigned checks;
#define CHECK(value) do { ++checks; assert(value); } while (0)
#define TAG "host-i2c"
#define ESP_LOGE(...) ((void)++fake.logs)
#define ESP_LOGD(...) ((void)0)
#define portENTER_CRITICAL_SAFE(...) ((void)0)
#define portEXIT_CRITICAL_SAFE(...) ((void)0)

static void xQueueReset(int queue) { (void)queue; ++fake.queue_resets; }
static int xSemaphoreTake(int semaphore, TickType_t ticks)
{ (void)semaphore; (void)ticks; return fake.sem_ok; }
static void xSemaphoreGive(int semaphore) { (void)semaphore; ++fake.semaphore_gives; }
static int xQueueReceive(int queue, int *event, TickType_t ticks)
{ (void)queue; (void)ticks; ++fake.queue_receives; *event = fake.event; return fake.queue_ok; }
static TickType_t xTaskGetTickCount(void)
{
    fake.last_tick = fake.start + fake.tick_calls++ / fake.tick_divisor;
    return fake.last_tick;
}
static bool i2c_ll_is_bus_busy(void *dev)
{
    (void)dev;
    /* Deterministic test guard: baseline's unbounded loop must hit this.
     * This is not a watchdog or timeout added to the production algorithm. */
    if (++fake.busy_calls > 100000) {
        fputs("UNBOUNDED_NACK_GUARD\n", stderr);
        exit(86);
    }
    return fake.forever || fake.busy_calls <= fake.busy_count;
}
static int s_i2c_hw_fsm_reset(i2c_master_bus_handle_t bus, bool clear_bus)
{ (void)bus; ++fake.resets; fake.reset_clear = clear_bus; return fake.reset_result; }
static void i2c_ll_master_write_cmd_reg(void *dev, i2c_ll_hw_cmd_t cmd, unsigned index)
{ (void)dev; (void)cmd; (void)index; ++fake.commands; }
static void i2c_hal_master_trans_start(i2c_hal_context_t *hal)
{ (void)hal; ++fake.starts; }
static void s_i2c_err_log_print(int event, bool bypass)
{ (void)event; (void)bypass; ++fake.event_logs; }
static void s_i2c_write_command(i2c_master_bus_handle_t bus, i2c_operation_t *operation,
                               uint8_t *fifo, uint8_t *address, void *yield)
{ (void)operation; (void)fifo; (void)address; (void)yield; --bus->i2c_trans.cmd_count; }
static void s_i2c_read_command(i2c_master_bus_handle_t bus, i2c_operation_t *operation,
                              uint8_t *fifo, void *yield)
{ (void)operation; (void)fifo; (void)yield; --bus->i2c_trans.cmd_count; }
static void s_i2c_start_end_command(i2c_master_bus_handle_t bus, i2c_operation_t *operation,
                                   uint8_t *address, void *yield)
{ (void)operation; (void)address; (void)yield; --bus->i2c_trans.cmd_count; }

#include "actual_i2c_send_commands.inc"

static bus_t fresh(bus_base_t *base)
{
    memset(&fake, 0, sizeof(fake));
    fake.queue_ok = true; fake.sem_ok = true; fake.tick_divisor = 1;
    fake.event = I2C_EVENT_NACK;
    bus_t bus = { .base = base };
    atomic_init(&bus.status, I2C_STATUS_ACK_ERROR);
    return bus;
}
static void completed(const bus_t *bus, int status)
{
    CHECK(atomic_load(&bus->status) == status);
    CHECK(fake.semaphore_gives == 1);
    CHECK(fake.queue_resets == 1);
    CHECK(fake.queue_receives == 1);
    CHECK(fake.event_logs == 1);
}

int main(int argc, char **argv)
{
    bus_base_t base = {0};
    bus_t bus = fresh(&base);
    if (argc == 2 && strcmp(argv[1], "baseline-stuck") == 0) {
        fake.forever = true;
        s_i2c_send_commands(&bus, 2);
        return 0;
    }

    /* NACK but already idle: retain the existing NACK status and cleanup. */
    s_i2c_send_commands(&bus, 2);
    completed(&bus, I2C_STATUS_ACK_ERROR);
    CHECK(fake.resets == 0); CHECK(fake.busy_calls == 1);

    /* Two ticks at 100 Hz represent the existing expander's 20 ms input.
     * Exact upstream comparison is >, so expiry occurs on elapsed tick 3. */
    bus = fresh(&base); fake.forever = true;
    s_i2c_send_commands(&bus, 2);
    completed(&bus, I2C_STATUS_TIMEOUT);
    CHECK(fake.last_tick == 3); CHECK(fake.busy_calls == 3);
    CHECK(fake.resets == 1); CHECK(fake.reset_clear); CHECK(fake.logs == 1);

    /* Bus clears at the deadline: no premature reset. */
    bus = fresh(&base); fake.busy_count = 2;
    s_i2c_send_commands(&bus, 2);
    completed(&bus, I2C_STATUS_ACK_ERROR);
    CHECK(fake.resets == 0); CHECK(fake.last_tick == 2);

    /* Scheduler tick can stay constant over many busy polls. */
    bus = fresh(&base); fake.forever = true; fake.tick_divisor = 4;
    s_i2c_send_commands(&bus, 2);
    completed(&bus, I2C_STATUS_TIMEOUT);
    CHECK(fake.busy_calls == 12); CHECK(fake.last_tick == 3);

    /* Unsigned subtraction must preserve expiry across tick wrap. */
    bus = fresh(&base); fake.forever = true; fake.start = UINT32_MAX - 1;
    s_i2c_send_commands(&bus, 2);
    completed(&bus, I2C_STATUS_TIMEOUT);
    CHECK(fake.last_tick == 1); CHECK(fake.busy_calls == 3); CHECK(fake.resets == 1);

    bus = fresh(&base); fake.forever = true;
    s_i2c_send_commands(&bus, 0);
    completed(&bus, I2C_STATUS_TIMEOUT);
    CHECK(fake.last_tick == 1); CHECK(fake.resets == 1);

    /* A reset error must not revive the wait or suppress semaphore cleanup. */
    bus = fresh(&base); fake.forever = true; fake.reset_result = -1;
    s_i2c_send_commands(&bus, 20);
    completed(&bus, I2C_STATUS_TIMEOUT);
    CHECK(fake.last_tick == 21); CHECK(fake.resets == 1);

    /* ACK_ERROR while there are pending commands uses the existing STOP path. */
    bus = fresh(&base); fake.forever = true; bus.i2c_trans.cmd_count = 1;
    s_i2c_send_commands(&bus, 2);
    completed(&bus, I2C_STATUS_TIMEOUT);
    CHECK(fake.commands == 1); CHECK(fake.starts == 2); CHECK(fake.resets == 1);

    /* Normal completion and missing-event behavior remain unchanged. */
    bus = fresh(&base); fake.event = I2C_EVENT_DONE;
    s_i2c_send_commands(&bus, 2);
    completed(&bus, I2C_STATUS_DONE);
    CHECK(fake.busy_calls == 0); CHECK(fake.resets == 0);

    bus = fresh(&base); fake.queue_ok = false; bus.cmd_idx = 4; bus.trans_idx = 3;
    s_i2c_send_commands(&bus, 2);
    CHECK(atomic_load(&bus.status) == I2C_STATUS_TIMEOUT);
    CHECK(bus.cmd_idx == 0); CHECK(bus.trans_idx == 0);
    CHECK(fake.semaphore_gives == 1); CHECK(fake.busy_calls == 0);

    /* A following healthy transaction can complete using the same bus object. */
    fake.queue_ok = true; fake.event = I2C_EVENT_DONE;
    s_i2c_send_commands(&bus, 2);
    CHECK(atomic_load(&bus.status) == I2C_STATUS_DONE);
    CHECK(fake.semaphore_gives == 2);

    printf("PASS: %u checks; actual pinned I2C function, bounded NACK faults, no device access\n", checks);
    return 0;
}
