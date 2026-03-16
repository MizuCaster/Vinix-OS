/* ============================================================
 * i2c.h
 * ------------------------------------------------------------
 * I2C Driver Interface
 * Target: AM335x I2C0 (for TDA19988 HDMI communication)
 * ============================================================ */

#ifndef I2C_H
#define I2C_H

#include "types.h"

/* ============================================================
 * I2C0 Configuration
 * ============================================================ */

/* I2C0 base address (L4_WKUP domain, already mapped) */
#define I2C0_BASE           0x44E0B000

/* ============================================================
 * Public API
 * ============================================================ */

/**
 * Initialize I2C0 module
 *
 * - Enables module clock (CM_WKUP)
 * - Configures prescaler for ~100kHz standard mode
 * - Sets master mode, polling
 *
 * CONTRACT:
 * - Must be called after mmu_init() (peripheral mapping required)
 * - Must be called after uart_init() (for debug logging)
 * - Polling mode only (no IRQ)
 */
void i2c_init(void);

/**
 * Read a single register from I2C slave
 *
 * @param slave_addr 7-bit slave address
 * @param reg        Register address to read
 * @param val        Pointer to store read value
 * @return 0 on success, -1 on error (NACK, timeout)
 */
int i2c_read_reg(uint8_t slave_addr, uint8_t reg, uint8_t *val);

/**
 * Write a single register to I2C slave
 *
 * @param slave_addr 7-bit slave address
 * @param reg        Register address to write
 * @param val        Value to write
 * @return 0 on success, -1 on error (NACK, timeout)
 */
int i2c_write_reg(uint8_t slave_addr, uint8_t reg, uint8_t val);

/**
 * Read a block of bytes from I2C slave
 *
 * Sends register address, then reads len bytes.
 * Used for EDID reading and multi-byte registers.
 *
 * @param slave_addr 7-bit slave address
 * @param reg        Starting register address
 * @param buf        Buffer to store read data
 * @param len        Number of bytes to read
 * @return 0 on success, -1 on error
 */
int i2c_read_block(uint8_t slave_addr, uint8_t reg, uint8_t *buf, int len);

#endif /* I2C_H */
