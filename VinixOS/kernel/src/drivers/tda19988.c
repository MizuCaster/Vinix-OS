/* ============================================================
 * tda19988.c
 * ------------------------------------------------------------
 * NXP TDA19988 HDMI Transmitter Driver
 * Target: BeagleBone Black (AM335x), 1280x720@60Hz
 * I2C interface via I2C0 at 0x44E0B000
 * ============================================================ */

#include "tda19988.h"
#include "i2c.h"
#include "uart.h"

/* ============================================================
 * 720p@60Hz timing (CEA-861 VIC=4)
 *
 * H: active=1280, FP=110, SW=40, BP=220, total=1650
 * V: active=720,  FP=5,   SW=5,  BP=20,  total=750
 * Pixel clock: 74.25 MHz (positive HS, positive VS)
 *
 * TDA19988 coordinate origin: start of HSYNC / start of VSYNC.
 * REFPIX = SW + BP = 260  (pixel where active video starts)
 * REFLINE = SW + BP = 25  (line where active video starts)
 * ============================================================ */
#define T720P_HTOTAL        1650
#define T720P_VTOTAL        750
#define T720P_HACTIVE       1280
#define T720P_VACTIVE       720
#define T720P_HSW           40
#define T720P_HBP           220
#define T720P_VSW           5
#define T720P_VBP           20

#define T720P_REFPIX        (T720P_HSW + T720P_HBP)        /* 260 */
#define T720P_REFLINE       (T720P_VSW + T720P_VBP)        /* 25  */

/* ============================================================
 * Internal state
 * ============================================================ */
static uint8_t g_current_page = 0xFF;   /* invalid sentinel */

/* ============================================================
 * Low-level I2C primitives
 * ============================================================ */

/* Switch TDA HDMI core to the given register page */
static void tda_set_page(uint8_t page)
{
    if (g_current_page == page)
        return;

    if (i2c_write_reg(TDA_HDMI_I2C_ADDR, TDA_CURPAGE_ADDR, page) != 0)
        uart_printf("[TDA] ERR: page switch to 0x%02x failed\n", page);
    else
        g_current_page = page;
}

/* Write one byte to a TDA HDMI core register (page:addr encoded) */
static int tda_write(uint16_t reg, uint8_t val)
{
    tda_set_page(TDA_PAGE(reg));
    return i2c_write_reg(TDA_HDMI_I2C_ADDR, TDA_ADDR(reg), val);
}

/* Read one byte from a TDA HDMI core register */
static int tda_read(uint16_t reg, uint8_t *val)
{
    tda_set_page(TDA_PAGE(reg));
    return i2c_read_reg(TDA_HDMI_I2C_ADDR, TDA_ADDR(reg), val);
}

/* Write one byte to TDA CEC core (no page mechanism) */
static int tda_cec_write(uint8_t reg, uint8_t val)
{
    return i2c_write_reg(TDA_CEC_I2C_ADDR, reg, val);
}

/* Spin-loop delay (calibrated for ~100ns per iteration at 500MHz) */
static void tda_udelay(volatile uint32_t us)
{
    /* ~500 iterations ≈ 1µs at Cortex-A8 500MHz; conservative factor */
    volatile uint32_t count = us * 100;
    while (count--);
}

/* Write 16-bit value to a pair of consecutive MSB/LSB registers */
static void tda_write16(uint16_t reg_msb, uint16_t val16)
{
    tda_write(reg_msb,     (uint8_t)((val16 >> 8) & 0xFF));
    tda_write(reg_msb + 1, (uint8_t)(val16 & 0xFF));
}

/* ============================================================
 * Init sequence steps
 * ============================================================ */

static void tda_cec_enable(void)
{
    /* Wake up CEC module and enable HDMI core via CEC address */
    tda_cec_write(TDA_CEC_ENAMODS, ENAMODS_RXSENS | ENAMODS_HDMI);
    tda_udelay(1000);   /* 1ms settle */
}

static void tda_soft_reset(void)
{
    uint8_t val = 0;

    /* Reset I2C and audio interfaces */
    tda_read(REG_SOFTRESET, &val);
    val |= SOFTRESET_I2C | SOFTRESET_AUDIO;
    tda_write(REG_SOFTRESET, val);
    tda_udelay(100);

    val &= ~(SOFTRESET_I2C | SOFTRESET_AUDIO);
    tda_write(REG_SOFTRESET, val);
    tda_udelay(10000);  /* 10ms after reset */
}

static uint16_t tda_read_version(void)
{
    uint8_t lo = 0, hi = 0;
    tda_read(REG_VERSION,     &lo);
    tda_read(REG_VERSION_MSB, &hi);
    return (uint16_t)(((uint16_t)hi << 8) | lo);
}

static void tda_powerup(void)
{
    /* Enable all sub-modules */
    tda_write(REG_CCLK_ON,        0x01);  /* CEC clock on */
    tda_write(REG_I2C_MASTER,     I2C_MASTER_DIS_MM | I2C_MASTER_DIS_FILT);
    tda_write(REG_FEAT_POWERDOWN, FEAT_POWERDOWN_SPDIF);  /* SPDIF off, video on */
}

static void tda_enable_video_ports(void)
{
    /* Enable all 24 video data bits (VP0-VP2 = 8 bits each) */
    tda_write(REG_ENA_VP_0, 0xFF);
    tda_write(REG_ENA_VP_1, 0xFF);
    tda_write(REG_ENA_VP_2, 0xFF);
    tda_write(REG_ENA_AP,   0x00);  /* Audio ports off */
}

static void tda_config_video_input(void)
{
    /*
     * RGB 4:4:4 external synchronization (rising edge)
     * From TDA19988 datasheet section 7.2.3.1:
     *   VPA = Blue (B[7:0]), VPB = Green (G[7:0]), VPC = Red (R[7:0])
     *   HSYNC/HREF, VSYNC/VREF, DE/FREF all used
     *
     * Register values from datasheet table:
     *   VIP_CNTRL_0 = 0x23: SWAP_A=3 (VPC→upper), SWAP_B=2 (VPB→mid)
     *   VIP_CNTRL_1 = 0x45: SWAP_C=5 (VPA→lower), SWAP_D=4
     *   VIP_CNTRL_2 = 0x01: DE enabled from external pin
     */
    tda_write(REG_VIP_CNTRL_0, 0x23);
    tda_write(REG_VIP_CNTRL_1, 0x45);
    tda_write(REG_VIP_CNTRL_2, 0x01);
    tda_write(REG_VIP_CNTRL_3, 0x00);  /* Rising edge, external sync */
    tda_write(REG_VIP_CNTRL_4, VIP_CNTRL_4_BLC(0) | VIP_CNTRL_4_BLANKIT(0));
    tda_write(REG_VIP_CNTRL_5, 0x00);

    /* Route VP output to HDMI encoder unchanged (no pixel remap) */
    tda_write(REG_MUX_VP_VIP_OUT, 0x24);
}

static void tda_config_color_matrix(void)
{
    /* Bypass color matrix: input RGB passes through unchanged to output RGB */
    tda_write(REG_MAT_CONTRL, MAT_CONTRL_MAT_BP | MAT_CONTRL_MAT_SC(3));
}

static void tda_config_720p_timing(void)
{
    /*
     * 720p@60Hz CEA timing parameters.
     * All pixel/line coordinates are relative to HSYNC/VSYNC start
     * (origin = blanking boundary, not active video start).
     *
     * HS occupies pixels [HBP : HBP+HSW] = [220 : 260] from pixel 0 of active
     * which in blanking-relative coords = pixels [0 : HSW] = [0 : 40].
     * But TDA counts from the start of the full line:
     *   hs_pix_strt = HTOTAL - (HSW+HBP) = HTOTAL - REFPIX = 1650-260 = 1390
     * ... or equivalently the complement:
     *   hs_pix_strt = HBP (= 220 from active start, wrapping = REFPIX - HSW = 220)
     *   hs_pix_stop = REFPIX (= 260)
     * (consistent with the Linux tda998x driver computation)
     */

    /* Disable output during timing setup to prevent garbage */
    tda_write(REG_TBG_CNTRL_0, TBG_CNTRL_0_FRAME_DIS | TBG_CNTRL_0_SYNC_ONCE);

    tda_write(REG_VIDFORMAT, 0x00);   /* Progressive, no repetition */

    /* Reference point: start of active video */
    tda_write16(REG_REFPIX_MSB,  T720P_REFPIX);    /* 260 */
    tda_write16(REG_REFLINE_MSB, T720P_REFLINE);   /* 25  */

    /* Total dimensions */
    tda_write16(REG_NPIX_MSB,  T720P_HTOTAL);      /* 1650 */
    tda_write16(REG_NLINE_MSB, T720P_VTOTAL);      /* 750  */

    /*
     * VSYNC timing (field 1 for progressive = entire frame)
     * VS starts at line 0, pixel REFPIX (= 260)
     * VS ends  at line VSW (= 5), pixel REFPIX (= 260)
     */
    tda_write16(REG_VS_LINE_STRT_1_MSB, 0);
    tda_write16(REG_VS_PIX_STRT_1_MSB,  T720P_REFPIX);
    tda_write16(REG_VS_LINE_END_1_MSB,  T720P_VSW);
    tda_write16(REG_VS_PIX_END_1_MSB,   T720P_REFPIX);

    /* Progressive: field 2 = unused (all zeros) */
    tda_write16(REG_VS_LINE_STRT_2_MSB, 0);
    tda_write16(REG_VS_PIX_STRT_2_MSB,  0);
    tda_write16(REG_VS_LINE_END_2_MSB,  0);
    tda_write16(REG_VS_PIX_END_2_MSB,   0);

    /*
     * HSYNC timing
     * hs_pix_strt = HBP = REFPIX - HSW = 220
     * hs_pix_stop = REFPIX = HSW + HBP = 260
     */
    tda_write16(REG_HS_PIX_START_MSB, T720P_HBP);          /* 220 */
    tda_write16(REG_HS_PIX_STOP_MSB,  T720P_REFPIX);       /* 260 */

    /*
     * Active video window
     * vwin starts at REFLINE (= 25), ends at REFLINE + Vactive (= 745)
     */
    tda_write16(REG_VWIN_START_1_MSB, T720P_REFLINE);                  /* 25  */
    tda_write16(REG_VWIN_END_1_MSB,   T720P_REFLINE + T720P_VACTIVE);  /* 745 */
    tda_write16(REG_VWIN_START_2_MSB, 0);
    tda_write16(REG_VWIN_END_2_MSB,   0);

    /*
     * Data Enable (DE) window
     * de_start = REFPIX = 260  (where active pixels begin)
     * de_stop  = REFPIX + Hactive = 1540
     */
    tda_write16(REG_DE_START_MSB, T720P_REFPIX);                       /* 260  */
    tda_write16(REG_DE_STOP_MSB,  T720P_REFPIX + T720P_HACTIVE);       /* 1540 */

    /*
     * Timing Bus Generator:
     * TBG_CNTRL_1: external sync mode, no polarity inversion
     *   (720p HSYNC and VSYNC are active-high → no toggle needed)
     * TBG_CNTRL_0: enable after all timing is written
     */
    tda_write(REG_TBG_CNTRL_1, TBG_CNTRL_1_X_EXT);   /* 0x08 */
    tda_write(REG_TBG_CNTRL_0, 0x00);                  /* Enable output */
}

static void tda_config_pll(void)
{
    /*
     * PLL configuration for 74.25 MHz pixel clock (720p@60Hz)
     *
     * PLL_SERIAL_2[1:0] = SRL_NOSC: VCO divider selection
     *   1 → divide-by-2: VCO runs at 2x pixel clock = 148.5 MHz
     *   Appropriate for 720p pixel clock range (50-100 MHz)
     *
     * SEL_CLK:
     *   bit 0 = 1: SEL_VRF_CLK (use REFCLK from pixel clock)
     *   bit 3 = 1: ENA_SC_CLK (enable serializer clock)
     *   → 0x09
     */
    tda_write(REG_PLL_SERIAL_1, 0x00);
    tda_write(REG_PLL_SERIAL_2, PLL_SERIAL_2_SRL_NOSC(1));   /* 0x01 */
    tda_write(REG_PLL_SERIAL_3, 0x00);

    tda_write(REG_PLL_SCGN1, 0xEA);    /* SCG N divider low */
    tda_write(REG_PLL_SCGN2, 0x00);    /* SCG N divider high */
    tda_write(REG_PLL_SCGR1, 0x2D);    /* SCG R divider low */
    tda_write(REG_PLL_SCGR2, 0x00);    /* SCG R divider high */
    tda_write(REG_PLL_SCG1,  0x00);
    tda_write(REG_PLL_SCG2,  0xB1);    /* SCG2 config for 74.25MHz */

    tda_write(REG_SEL_CLK,     0x09);  /* ENA_SC_CLK + SEL pixel refclk */
    tda_write(REG_ANA_GENERAL, 0x09);
}

static void tda_config_output(void)
{
    /* HVF (horizontal/vertical filter) — bypass for progressive output */
    tda_write(REG_HVF_CNTRL_0, HVF_CNTRL_0_INTPOL(0) | HVF_CNTRL_0_PREFIL(0));
    tda_write(REG_HVF_CNTRL_1, HVF_CNTRL_1_VQR(0));

    /* No pixel repetition for 720p */
    tda_write(REG_RPT_CNTRL, 0x00);

    /* Serializer and output buffer */
    tda_write(REG_SERIALIZER, 0x00);
    tda_write(REG_BUFFER_OUT, 0x00);

    tda_write(REG_PLL_SCG1, 0x00);
    tda_write(REG_PLL_SCG2, 0xB1);
}

/* ============================================================
 * Public API
 * ============================================================ */

void tda19988_init(void)
{
    uint16_t version;

    uart_printf("[TDA] Initializing TDA19988 HDMI transmitter\n");

    /* Step 1: Wake up CEC module, enable HDMI core */
    tda_cec_enable();
    uart_printf("[TDA] CEC modules enabled\n");

    /* Step 2: Soft reset HDMI core */
    tda_soft_reset();
    uart_printf("[TDA] HDMI core reset complete\n");

    /* Step 3: Verify chip identity */
    version = tda_read_version();
    uart_printf("[TDA] Chip version: 0x%04x (expected 0x%04x)\n",
                version, TDA19988_VERSION);

    if (version != TDA19988_VERSION) {
        uart_printf("[TDA] WARN: unexpected version, continuing anyway\n");
    }

    /* Step 4: Power up sub-modules */
    tda_powerup();

    /* Step 5: Enable all video data ports */
    tda_enable_video_ports();

    /* Step 6: Configure video input for RGB 4:4:4 external sync */
    tda_config_video_input();

    /* Step 7: Bypass color matrix (RGB in → RGB out) */
    tda_config_color_matrix();

    /* Step 8: Program 720p@60Hz timing */
    tda_config_720p_timing();

    /* Step 9: Configure PLL for 74.25 MHz pixel clock */
    tda_config_pll();

    /* Step 10: Configure output path */
    tda_config_output();

    uart_printf("[TDA] TDA19988 configured for 1280x720@60Hz RGB\n");
}
