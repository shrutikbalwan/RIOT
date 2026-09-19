/*
 * SPDX-License-Identifier: LGPL-2.1-only
 */

#include <string.h>

#include "embUnit.h"
#include "ethos.h"
#include "irq.h"
#include "tests-ethos.h"

static ethos_t _dev;
static uint8_t _buffer[16];
static uart_rx_cb_t _rx;
static void *_rx_arg;

int __wrap_uart_init(uart_t uart, uint32_t baud, uart_rx_cb_t callback, void *arg);
void __wrap_uart_write(uart_t uart, const uint8_t *data, size_t len);

int __wrap_uart_init(uart_t uart, uint32_t baud, uart_rx_cb_t callback, void *arg)
{
    (void)uart;
    (void)baud;
    _rx = callback;
    _rx_arg = arg;
    return 0;
}

void __wrap_uart_write(uart_t uart, const uint8_t *data, size_t len)
{
    (void)uart;
    (void)data;
    (void)len;
}

static void _setup(void)
{
    const ethos_params_t params = { .uart = UART_DEV(0), .baudrate = 115200 };

    memset(&_dev, 0, sizeof(_dev));
    ethos_setup(&_dev, &params, 0, _buffer, sizeof(_buffer));
}

static void _feed(const uint8_t *data, size_t len)
{
    unsigned state = irq_disable();

    for (size_t i = 0; i < len; i++) {
        _rx(_rx_arg, data[i]);
    }
    irq_restore(state);
}

static void _feed_text(void)
{
    const uint8_t text[] = { ETHOS_FRAME_DELIMITER, ETHOS_ESC_CHAR,
                             ETHOS_FRAME_TYPE_TEXT ^ 0x20, 'a', ETHOS_FRAME_DELIMITER };

    _feed(text, sizeof(text));
}

static void test_text_preserves_pending_packet(void)
{
    const uint8_t frame[] = { ETHOS_FRAME_DELIMITER, 0x11, 0x22, ETHOS_FRAME_DELIMITER };
    uint8_t received[3];

    _feed(frame, sizeof(frame));
    _feed_text();
    TEST_ASSERT_EQUAL_INT(3, tsrb_avail(&_dev.inbuf));
    TEST_ASSERT_EQUAL_INT(2, _dev.netdev.driver->recv(&_dev.netdev, received,
                                                      sizeof(received), NULL));
    TEST_ASSERT_EQUAL_INT(0x11, received[0]);
    TEST_ASSERT_EQUAL_INT(0x22, received[1]);
    TEST_ASSERT(tsrb_empty(&_dev.inbuf));
}

static void test_text_preserves_partly_consumed_packet(void)
{
    const uint8_t frame[] = { ETHOS_FRAME_DELIMITER, 0x11, 0x22, ETHOS_FRAME_DELIMITER };

    _feed(frame, sizeof(frame));
    TEST_ASSERT_EQUAL_INT(0x11, tsrb_get_one(&_dev.inbuf));
    _feed_text();
    TEST_ASSERT_EQUAL_INT(0x22, tsrb_get_one(&_dev.inbuf));
    TEST_ASSERT_EQUAL_INT(ETHOS_FRAME_DELIMITER, tsrb_get_one(&_dev.inbuf));
    TEST_ASSERT(tsrb_empty(&_dev.inbuf));
}

static void test_text_at_ring_wrap(void)
{
    for (unsigned i = 0; i < sizeof(_buffer) - 4; i++) {
        tsrb_add_one(&_dev.inbuf, 0);
        tsrb_get_one(&_dev.inbuf);
    }
    test_text_preserves_pending_packet();
}

static void test_text_before_data(void)
{
    _feed_text();
    TEST_ASSERT(tsrb_empty(&_dev.inbuf));
    test_text_preserves_pending_packet();
}

void tests_ethos(void)
{
    EMB_UNIT_TESTFIXTURES(fixtures) {
        new_TestFixture(test_text_preserves_pending_packet),
        new_TestFixture(test_text_preserves_partly_consumed_packet),
        new_TestFixture(test_text_at_ring_wrap),
        new_TestFixture(test_text_before_data),
    };
    EMB_UNIT_TESTCALLER(ethos_tests, _setup, NULL, fixtures);
    TESTS_RUN((Test *)&ethos_tests);
}
