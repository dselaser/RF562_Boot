/*===========================================================================*\
 *  boot_rs485.c - RS485 Communication & CNNX Protocol for Bootloader
 *
 *  Phase 1: ASCII command detection  @B*42\n  @X*58\n
 *  Phase 2: CNNX binary firmware transfer protocol
 *
 *  All communication is polling-based (no interrupts, no DMA).
 *  This is appropriate for a single-threaded bootloader.
\*===========================================================================*/

#include <string.h>
#include <stdio.h>
#include "boot_rs485.h"
#include "boot_config.h"
#include "boot_flash.h"
#include "boot_util.h"

/* UART handle (defined in main.c) */
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart1;  /* Debug UART */

/* Minimal debug print via USART1 */
static void rs485_dbg(const char *msg)
{
    HAL_UART_Transmit(&huart1, (const uint8_t *)msg, (uint16_t)strlen(msg), 100);
}

/* Packet counter for outgoing packets */
static uint16_t s_pkt_counter = 0;

/*===========================================================================*\
 *  Low-Level UART Polling Functions
\*===========================================================================*/

void
RS485_Init(void)
{
    while (USART2->ISR & USART_ISR_RXNE_RXFNE)
        (void)USART2->RDR;
    USART2->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF | USART_ICR_PECF;
}

void
RS485_DeInit(void)
{
    HAL_UART_DeInit(&huart2);
}

int
RS485_RecvByte(uint8_t *byte, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    while ((HAL_GetTick() - start) < timeout_ms) {
        if (USART2->ISR & USART_ISR_RXNE_RXFNE) {
            *byte = (uint8_t)(USART2->RDR & 0xFF);
            return 1;
        }
        if (USART2->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE))
            USART2->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;
    }
    return 0;
}

void
RS485_Send(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        while (!(USART2->ISR & USART_ISR_TXE_TXFNF));
        USART2->TDR = data[i];
    }
    while (!(USART2->ISR & USART_ISR_TC));
}


/*===========================================================================*\
 *  ASCII Protocol  -  @<payload>*XX\n
 *
 *  XOR parity of payload bytes between '@' and '*'
 *  XX = uppercase hex of parity byte
\*===========================================================================*/

static const char HEX_CHARS[] = "0123456789ABCDEF";

static uint8_t
ascii_xor_parity(const uint8_t *data, uint16_t len)
{
    uint8_t x = 0;
    for (uint16_t i = 0; i < len; i++) {
        x ^= data[i];
    }
    return x;
}

static int8_t
hex_val(uint8_t ch)
{
    if (ch >= '0' && ch <= '9') return (int8_t)(ch - '0');
    if (ch >= 'A' && ch <= 'F') return (int8_t)(ch - 'A' + 10);
    if (ch >= 'a' && ch <= 'f') return (int8_t)(ch - 'a' + 10);
    return -1;
}

void
RS485_SendAsciiResp(const char *payload)
{
    uint8_t buf[32];
    uint16_t i = 0;
    uint16_t plen = (uint16_t)strlen(payload);

    buf[i++] = '@';
    memcpy(&buf[i], payload, plen);
    i += plen;

    uint8_t par = ascii_xor_parity((const uint8_t *)payload, plen);
    buf[i++] = '*';
    buf[i++] = (uint8_t)HEX_CHARS[(par >> 4) & 0x0F];
    buf[i++] = (uint8_t)HEX_CHARS[par & 0x0F];
    buf[i++] = '\n';

    RS485_Send(buf, i);
}


int
RS485_WaitForAsciiCmd(uint32_t timeout_ms)
{
    USART_TypeDef *uart = USART2;  /* Direct register access */
    uint32_t start = HAL_GetTick();
    uint32_t led_tick = 0;
    uint8_t  prev = 0;

    while ((HAL_GetTick() - start) < timeout_ms) {
        /* LED 1s toggle while waiting */
        if ((HAL_GetTick() - led_tick) >= 1000) {
            led_tick = HAL_GetTick();
            LED_TOGGLE();
        }

        /* Clear errors */
        if (uart->ISR & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE))
            uart->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NECF;

        /* Check for received byte */
        if (!(uart->ISR & USART_ISR_RXNE_RXFNE))
            continue;

        uint8_t ch = (uint8_t)(uart->RDR & 0xFF);

        /* Simple 2-byte detection: '@' followed by command */
        if (prev == '@') {
            if (ch == 'U') {
                HAL_Delay(5);
                RS485_SendAsciiResp("U");
                return 'U';
            }
            if (ch == 'A') {
                HAL_Delay(5);
                RS485_SendAsciiResp("A");
                return 'A';
            }
        }
        prev = ch;
    }

    return 0;  /* Timeout */
}


/*===========================================================================*\
 *  CNNX Binary Packet Protocol
 *
 *  Packet format:
 *    HDR[2]  0xAA 0x55
 *    DST[1]  Destination ID
 *    SRC[1]  Source ID
 *    PID[2]  Packet ID (counter, big-endian)
 *    CMD[1]  Command
 *    NDATA[1] Data length (0~240)
 *    DATA[N]  Payload
 *    CRC16[2] (reserved, currently 0)
 *    TRL[2]  0x55 0xAA
\*===========================================================================*/

static uint8_t s_rxpkt[PKT_MAX_SIZE];   /* Receive packet buffer */
static uint8_t s_txpkt[PKT_MAX_SIZE];   /* Transmit packet buffer */

/* Data buffer for flash writes - must be 4-byte aligned */
static uint8_t s_flash_buf[PKT_MAX_DATA] __attribute__((aligned(4)));


/**
 * @brief  Receive a complete CNNX binary packet (polling)
 * @param  buf         Buffer to store packet (must be >= PKT_MAX_SIZE)
 * @param  timeout_ms  Timeout in milliseconds
 * @return  Packet length (> 0), or -1 on timeout
 */
static int
pkt_receive(uint8_t *buf, uint32_t timeout_ms)
{
    uint32_t start = HAL_GetTick();
    int state = 0;
    int idx = 0;
    int ndata = 0;
    int expected_len = 0;

    while ((HAL_GetTick() - start) < timeout_ms) {
        uint8_t ch;
        if (!RS485_RecvByte(&ch, 1)) {
            continue;
        }

        switch (state) {
        case 0:  /* Waiting for HDR[0] = 0xAA */
            if (ch == PKT_HDR0) {
                buf[0] = ch;
                idx = 1;
                state = 1;
            }
            break;

        case 1:  /* Waiting for HDR[1] = 0x55 */
            if (ch == PKT_HDR1) {
                buf[1] = ch;
                idx = 2;
                state = 2;
            } else {
                state = 0;  /* Not a valid header, restart */
            }
            break;

        case 2:  /* Receiving DST, SRC, PID[2], CMD, NDATA, DATA, CRC16, TRL */
            buf[idx++] = ch;

            /* After NDATA byte (offset 7), we know the total packet length */
            if (idx == (PKT_OFF_NDATA + 1)) {
                ndata = ch;
                expected_len = ndata + PKT_OVERHEAD;

                /* Sanity check */
                if (expected_len > PKT_MAX_SIZE) {
                    state = 0;  /* Packet too large, restart */
                    idx = 0;
                    break;
                }
            }

            /* Check if packet is complete */
            if (idx >= PKT_OVERHEAD && idx >= expected_len) {
                /* Verify trailer */
                if (buf[idx - 2] == PKT_TRL0 && buf[idx - 1] == PKT_TRL1) {
                    /* Verify destination */
                    if (buf[PKT_OFF_DST] == RS485_ID_HP ||
                        buf[PKT_OFF_DST] == 0xFF) {
                        return idx;  /* Valid packet */
                    }
                }
                /* Invalid trailer or not for us, restart */
                state = 0;
                idx = 0;
            }
            break;
        }
    }

    return -1;  /* Timeout */
}


/**
 * @brief  Build and send a CNNX binary packet
 * @param  dst   Destination ID
 * @param  cmd   Command byte
 * @param  data  Payload data (can be NULL if ndata == 0)
 * @param  ndata Payload length
 */
static void
pkt_send(uint8_t dst, uint8_t cmd, const uint8_t *data, uint8_t ndata)
{
    uint16_t crc16 = 0;  /* CRC16: reserved, currently 0 */
    uint16_t i = 0;

    s_txpkt[i++] = PKT_HDR0;
    s_txpkt[i++] = PKT_HDR1;
    s_txpkt[i++] = dst;
    s_txpkt[i++] = RS485_ID_HP;          /* SRC = this device */
    s_pkt_counter++;
    s_txpkt[i++] = (uint8_t)(s_pkt_counter >> 8);
    s_txpkt[i++] = (uint8_t)(s_pkt_counter & 0xFF);
    s_txpkt[i++] = cmd;
    s_txpkt[i++] = ndata;

    if (data && ndata > 0) {
        memcpy(&s_txpkt[i], data, ndata);
        i += ndata;
    }

    s_txpkt[i++] = (uint8_t)(crc16 >> 8);
    s_txpkt[i++] = (uint8_t)(crc16 & 0xFF);
    s_txpkt[i++] = PKT_TRL0;
    s_txpkt[i++] = PKT_TRL1;

    RS485_Send(s_txpkt, i);
}


/**
 * @brief  Extract 4-byte big-endian uint32 from buffer
 */
static uint32_t
buf_to_u32(const uint8_t *p)
{
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) |
           ((uint32_t)p[3]);
}


/**
 * @brief  Extract 2-byte big-endian uint16 from buffer
 */
static uint16_t
buf_to_u16(const uint8_t *p)
{
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}


/*===========================================================================*\
 *  Firmware Receive Protocol (Sub-Device / HP Side)
 *
 *  Sequence:
 *    Master → HP:  FWPREPARE (codesize, blockcnt, fwver)
 *    HP → Master:  FWREADY   (flash erased, ready)
 *    HP → Master:  FWSTART   (request first code block)
 *
 *    [repeat]
 *    Master → HP:  FWCODE    (240 bytes of firmware)
 *    HP → Master:  FWNEXT    (block written, send next)
 *
 *    Master → HP:  FWEND     (no more data)
 *    HP → Master:  FWEND     (echo, confirm complete)
 *    HP → Master:  FWSUCCESS (verified OK)
 *    HP:           NVIC_SystemReset()
\*===========================================================================*/

int
RS485_FirmwareReceive(void)
{
    uint8_t  master_id = RS485_ID_MASTER;
    uint32_t codesize = 0;
    uint32_t blockcnt = 0;
    uint16_t fwver = 0;
    uint32_t received = 0;
    uint32_t flash_addr = APP_FLASH_BASE;
    int      pkt_len;
    uint8_t  cmd;

    LED_ON();
    rs485_dbg("[B] FW recv start\r\n");

    /*===================================================================
     *  Step 1: Wait for FWPREPARE from Master
     *          May also receive FWWAIT (extend timeout)
     *===================================================================*/
    {
        uint32_t wait_start = HAL_GetTick();
        uint32_t wait_limit = ERASE_WAIT_MS;
        int got_prepare = 0;

        while ((HAL_GetTick() - wait_start) < wait_limit) {
            LED_TOGGLE();

            pkt_len = pkt_receive(s_rxpkt, 1000);
            if (pkt_len < 0) continue;  /* Timeout on this attempt */

            cmd = s_rxpkt[PKT_OFF_CMD];
            master_id = s_rxpkt[PKT_OFF_SRC];

            {
                char dbg[32];
                snprintf(dbg, sizeof(dbg), "[B] pkt cmd=%d\r\n", cmd);
                rs485_dbg(dbg);
            }

            if (cmd == CMD_FWWAIT) {
                /* Master says "wait, not ready yet" - extend timeout */
                wait_limit = (HAL_GetTick() - wait_start) + PACKET_TIMEOUT_MS;
                continue;
            }

            if (cmd == CMD_FWPREPARE) {
                uint8_t ndata = s_rxpkt[PKT_OFF_NDATA];
                if (ndata < 10) continue;  /* Need at least 10 bytes */

                const uint8_t *pdata = &s_rxpkt[PKT_OFF_DATA];
                codesize = buf_to_u32(pdata + FWPREP_OFF_CODESIZE);
                blockcnt = buf_to_u32(pdata + FWPREP_OFF_BLOCKCNT);
                fwver    = buf_to_u16(pdata + FWPREP_OFF_FWVER);

                /* Validate firmware size */
                if (codesize == 0 || codesize > APP_FLASH_SIZE) {
                    rs485_dbg("[B] bad size\r\n");
                    pkt_send(master_id, CMD_FWFAILURE, NULL, 0);
                    return -4;  /* Invalid size */
                }
                {
                    char dbg[48];
                    snprintf(dbg, sizeof(dbg), "[B] PREP sz=%lu blk=%lu\r\n",
                             (unsigned long)codesize, (unsigned long)blockcnt);
                    rs485_dbg(dbg);
                }
                got_prepare = 1;
                break;
            }
        }

        if (!got_prepare) {
            rs485_dbg("[B] PREP timeout\r\n");
            return -1;  /* Timeout waiting for FWPREPARE */
        }
    }

    /*===================================================================
     *  Step 2: Erase Application Flash
     *===================================================================*/
    rs485_dbg("[B] Erasing...\r\n");
    {
        HAL_StatusTypeDef ret = Boot_Flash_EraseApp(codesize);
        if (ret != HAL_OK) {
            rs485_dbg("[B] Erase FAIL\r\n");
            pkt_send(master_id, CMD_FWWRERR, NULL, 0);
            return -2;  /* Flash erase failed */
        }
    }
    rs485_dbg("[B] Erase OK\r\n");

    /* Send FWREADY to master (erase complete) */
    pkt_send(master_id, CMD_FWREADY, NULL, 0);
    HAL_Delay(5);  /* Small gap for RS485 turnaround */

    /* Send FWSTART to request first code block */
    pkt_send(master_id, CMD_FWSTART, NULL, 0);
    rs485_dbg("[B] FWREADY+START sent\r\n");

    /*===================================================================
     *  Step 3: Receive firmware blocks and write to flash
     *===================================================================*/
    {
        uint32_t wait_start = HAL_GetTick();
        uint32_t wait_limit = PACKET_TIMEOUT_MS;
        uint32_t blocks_written = 0;
        int      finished = 0;
        int      error = 0;

        while (!finished && !error) {
            /* Check overall timeout */
            if ((HAL_GetTick() - wait_start) > wait_limit) {
                error = -1;  /* Timeout */
                break;
            }

            /* Toggle LED for activity indication */
            if ((blocks_written & 0x07) == 0) LED_TOGGLE();

            /* Receive next packet */
            pkt_len = pkt_receive(s_rxpkt, PACKET_TIMEOUT_MS);
            if (pkt_len < 0) {
                error = -1;  /* Timeout */
                break;
            }

            cmd = s_rxpkt[PKT_OFF_CMD];

            switch (cmd) {
            case CMD_FWCODE: {
                /* Firmware data block */
                uint8_t ndata = s_rxpkt[PKT_OFF_NDATA];

                /* Copy data to aligned buffer */
                memcpy(s_flash_buf, &s_rxpkt[PKT_OFF_DATA], ndata);

                /* Write to flash */
                HAL_StatusTypeDef ret;
                ret = Boot_Flash_WriteBlock(flash_addr, s_flash_buf, ndata);
                if (ret != HAL_OK) {
                    pkt_send(master_id, CMD_FWWRERR, NULL, 0);
                    error = -2;
                    break;
                }

                /* Verify written data */
                uint32_t match = Boot_Flash_Verify(flash_addr, s_flash_buf, ndata);
                if (match != ndata) {
                    pkt_send(master_id, CMD_FWWRERR, NULL, 0);
                    error = -3;
                    break;
                }

                /* Advance pointers */
                flash_addr += ndata;
                received   += ndata;
                blocks_written++;

                /* Request next block */
                pkt_send(master_id, CMD_FWNEXT, NULL, 0);

                /* Extend timeout for next packet */
                wait_limit = (HAL_GetTick() - wait_start) + PACKET_TIMEOUT_MS;
                break;
            }

            case CMD_FWEND:
                /* Master signals end of transmission */
                finished = 1;
                /* Echo FWEND back to master */
                pkt_send(master_id, CMD_FWEND, NULL, 0);
                break;

            case CMD_FWWAIT:
                /* Master asks us to wait (e.g., USB read delay) */
                wait_limit = (HAL_GetTick() - wait_start) + PACKET_TIMEOUT_MS;
                break;

            case CMD_FWPREPARE:
                /* Master re-sent FWPREPARE (missed our FWREADY?) */
                /* Re-send FWSTART */
                pkt_send(master_id, CMD_FWSTART, NULL, 0);
                wait_limit = (HAL_GetTick() - wait_start) + PACKET_TIMEOUT_MS;
                break;

            default:
                /* Unknown command, ignore */
                break;
            }
        }

        if (error) {
            pkt_send(master_id, CMD_FWFAILURE, NULL, 0);
            return error;
        }
    }

    /*===================================================================
     *  Step 4: Verify and finalize
     *===================================================================*/

    /* Check that the written app looks valid */
    if (!Boot_Flash_IsAppValid()) {
        pkt_send(master_id, CMD_FWFAILURE, NULL, 0);
        return -3;
    }

    /* Send success to master */
    pkt_send(master_id, CMD_FWSUCCESS, NULL, 0);

    LED_ON();
    HAL_Delay(500);  /* Brief pause before reboot */

    return 0;  /* Success - caller should reset */
}
