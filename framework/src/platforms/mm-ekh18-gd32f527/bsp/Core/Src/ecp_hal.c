/*
 * ecp_hal.c - GD32F527 PKCAU-accelerated ECC scalar multiplication and point check.
 *
 * Copyright 2026 Morse Micro
 * SPDX-License-Identifier: Apache-2.0
 *
 * Implements the two functions forward-declared in ecp_alt.c and called from the
 * mbedTLS ECP alternative implementation:
 *   - ecp_mul_comb()        -- ECC scalar multiplication R = m * P  (PKCAU mode 32)
 *   - ecp_check_pubkey_sw() -- point-on-curve check (SEC1 3.2.3.1)  (PKCAU mode 40)
 *
 * The grp->st_* fields (st_p, st_a_sign, st_a_abs, st_b, st_modulus_size) must be
 * populated before calling these functions; this happens automatically when
 * mbedtls_ecp_group_load() is called (implemented in ecp_curves_alt.c).
 *
 * pkcau_point_check_operation() returns 0 if the point is on the curve, non-zero
 * otherwise (hardware writes 0 to PKCAU RAM[0x400] on success).
 */

#if !defined(MBEDTLS_CONFIG_FILE)
#include "mbedtls/config.h"
#else
#include MBEDTLS_CONFIG_FILE
#endif

#if defined(MBEDTLS_ECP_C)

#define MBEDTLS_ALLOW_PRIVATE_ACCESS 1

#include "mbedtls/ecp.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/error.h"

#if defined(MBEDTLS_ECP_ALT)

#include "gd32f5xx_pkcau.h"

#if defined(MBEDTLS_ECP_SHORT_WEIERSTRASS_ENABLED)

/*
 * ECC scalar multiplication R = m * P using the GD32F527 PKCAU.
 *
 * PKCAU mode PKCAU_MODE_ECC_SCALAR_MUL (mode 32) computes the Montgomery parameter
 * internally before performing the multiplication, so no pre-computation is needed.
 * The scalar m is rounded up to a 4-byte boundary because PKCAU processes whole words.
 */
int ecp_mul_comb(mbedtls_ecp_group *grp,
                 mbedtls_ecp_point *R,
                 const mbedtls_mpi *m,
                 const mbedtls_ecp_point *P,
                 int (*f_rng)(void *, unsigned char *, size_t),
                 void *p_rng,
                 mbedtls_ecp_restart_ctx *rs_ctx)
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    int pkcau_clk_enabled = 0;
    size_t olen;
    size_t scalarMulSize;
    uint8_t *P_binary = NULL;
    uint8_t *m_binary = NULL;
    uint8_t *R_binary = NULL;
    pkcau_ec_group_parameter_struct curve_group_para;
    pkcau_point_parameter_struct point_para;
    pkcau_ecc_out_struct result_out;

    (void)f_rng;
    (void)p_rng;
    (void)rs_ctx;

    /* Round scalar size up to 4-byte word boundary (PKCAU requirement). */
    scalarMulSize = mbedtls_mpi_size(m);
    scalarMulSize = ((scalarMulSize + 3U) >> 2) << 2;

    pkcau_ec_group_struct_para_init(&curve_group_para);
    pkcau_point_struct_para_init(&point_para);
    pkcau_ecc_out_struct_para_init(&result_out);

    /* Map grp->st_* fields to PKCAU curve parameters. coff_b is not needed for
     * scalar multiplication (only for point-on-curve check). */
    curve_group_para.modulus_p     = grp->st_p;
    curve_group_para.modulus_p_len = grp->st_modulus_size;
    curve_group_para.coff_a        = grp->st_a_abs;
    curve_group_para.coff_a_len    = grp->st_modulus_size;
    curve_group_para.a_sign        = grp->st_a_sign;

    /* Serialize input point P as uncompressed: 0x04 || X || Y (big-endian). */
    P_binary = (uint8_t *)mbedtls_calloc(2U * grp->st_modulus_size + 1U, 1U);
    MBEDTLS_MPI_CHK((P_binary == NULL) ? MBEDTLS_ERR_ECP_ALLOC_FAILED : 0);

    MBEDTLS_MPI_CHK(mbedtls_ecp_point_write_binary(grp, P, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                                    &olen, P_binary,
                                                    2U * grp->st_modulus_size + 1U));
    point_para.point_x     = P_binary + 1U;
    point_para.point_x_len = grp->st_modulus_size;
    point_para.point_y     = P_binary + grp->st_modulus_size + 1U;
    point_para.point_y_len = grp->st_modulus_size;

    /* Encode scalar m as a big-endian byte array padded to scalarMulSize. */
    m_binary = (uint8_t *)mbedtls_calloc(scalarMulSize, 1U);
    MBEDTLS_MPI_CHK((m_binary == NULL) ? MBEDTLS_ERR_ECP_ALLOC_FAILED : 0);
    MBEDTLS_MPI_CHK(mbedtls_mpi_write_binary(m, m_binary, scalarMulSize));
    curve_group_para.multi_k     = m_binary;
    curve_group_para.multi_k_len = scalarMulSize;

    /* Allocate output buffer: 0x04 || kP.X || kP.Y. PKCAU writes directly into
     * result_out.point_x / result_out.point_y, so those must be non-NULL. */
    R_binary = (uint8_t *)mbedtls_calloc(2U * grp->st_modulus_size + 1U, 1U);
    MBEDTLS_MPI_CHK((R_binary == NULL) ? MBEDTLS_ERR_ECP_ALLOC_FAILED : 0);
    R_binary[0]        = 0x04U;
    result_out.point_x = R_binary + 1U;
    result_out.point_y = R_binary + grp->st_modulus_size + 1U;

    /* Enable PKCAU clock, run hardware scalar multiplication. pkcau_point_mul_operation()
     * internally resets, enables, polls until done, and disables the PKCAU peripheral. */
    rcu_periph_clock_enable(RCU_PKCAU);
    pkcau_clk_enabled = 1;
    pkcau_point_mul_operation(&point_para, &curve_group_para,
                              PKCAU_MODE_ECC_SCALAR_MUL, &result_out);

    /* Decode result back into the mbedtls_ecp_point R. */
    MBEDTLS_MPI_CHK(mbedtls_ecp_point_read_binary(grp, R, R_binary,
                                                   2U * grp->st_modulus_size + 1U));

cleanup:
    if (pkcau_clk_enabled)
    {
        rcu_periph_clock_disable(RCU_PKCAU);
    }

    if (P_binary != NULL)
    {
        mbedtls_platform_zeroize(P_binary, 2U * grp->st_modulus_size + 1U);
        mbedtls_free(P_binary);
    }
    if (m_binary != NULL)
    {
        mbedtls_platform_zeroize(m_binary, scalarMulSize);
        mbedtls_free(m_binary);
    }
    if (R_binary != NULL)
    {
        mbedtls_platform_zeroize(R_binary, 2U * grp->st_modulus_size + 1U);
        mbedtls_free(R_binary);
    }

    return ret;
}

/*
 * Check that an affine point is valid as a public key, short Weierstrass curves
 * (SEC1 3.2.3.1), using the GD32F527 PKCAU mode 40 (point-on-curve check).
 *
 * pkcau_point_check_operation() reads a result byte from PKCAU RAM: 0 = on curve,
 * non-zero = not on curve.
 */
int ecp_check_pubkey_sw(const mbedtls_ecp_group *grp, const mbedtls_ecp_point *pt)
{
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    int pkcau_clk_enabled = 0;
    size_t olen;
    uint8_t *pt_binary = NULL;
    pkcau_ec_group_parameter_struct curve_group_para;
    pkcau_point_parameter_struct point_para;

    /* Affine coordinates must be in [0, P). */
    if (mbedtls_mpi_cmp_int(&pt->X, 0) < 0 ||
        mbedtls_mpi_cmp_int(&pt->Y, 0) < 0 ||
        mbedtls_mpi_cmp_mpi(&pt->X, &grp->P) >= 0 ||
        mbedtls_mpi_cmp_mpi(&pt->Y, &grp->P) >= 0)
    {
        return MBEDTLS_ERR_ECP_INVALID_KEY;
    }

    pkcau_ec_group_struct_para_init(&curve_group_para);
    pkcau_point_struct_para_init(&point_para);

    /* Point check (mode 40) uses p, a, b, and the point (x, y). */
    curve_group_para.modulus_p     = grp->st_p;
    curve_group_para.modulus_p_len = grp->st_modulus_size;
    curve_group_para.coff_a        = grp->st_a_abs;
    curve_group_para.coff_a_len    = grp->st_modulus_size;
    curve_group_para.coff_b        = grp->st_b;
    curve_group_para.coff_b_len    = grp->st_modulus_size;
    curve_group_para.a_sign        = grp->st_a_sign;

    pt_binary = (uint8_t *)mbedtls_calloc(2U * grp->st_modulus_size + 1U, 1U);
    MBEDTLS_MPI_CHK((pt_binary == NULL) ? MBEDTLS_ERR_ECP_ALLOC_FAILED : 0);

    MBEDTLS_MPI_CHK(mbedtls_ecp_point_write_binary(grp, pt, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                                    &olen, pt_binary,
                                                    2U * grp->st_modulus_size + 1U));

    point_para.point_x     = pt_binary + 1U;
    point_para.point_x_len = grp->st_modulus_size;
    point_para.point_y     = pt_binary + grp->st_modulus_size + 1U;
    point_para.point_y_len = grp->st_modulus_size;

    rcu_periph_clock_enable(RCU_PKCAU);
    pkcau_clk_enabled = 1;
    /* pkcau_point_check_operation returns 0 if point is on the curve. */
    ret = (pkcau_point_check_operation(&point_para, &curve_group_para) == 0U)
              ? 0
              : MBEDTLS_ERR_ECP_INVALID_KEY;

cleanup:
    if (pkcau_clk_enabled)
    {
        rcu_periph_clock_disable(RCU_PKCAU);
    }

    if (pt_binary != NULL)
    {
        mbedtls_platform_zeroize(pt_binary, 2U * grp->st_modulus_size + 1U);
        mbedtls_free(pt_binary);
    }

    return ret;
}
#endif /* MBEDTLS_ECP_SHORT_WEIERSTRASS_ENABLED */

#endif /* MBEDTLS_ECP_ALT */

#endif /* MBEDTLS_ECP_C */
