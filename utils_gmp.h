#pragma once
#ifdef AGISCORE_EXPORTS
#define AGIS_API __declspec(dllexport)
#else
#define AGIS_API __declspec(dllimport)
#endif

//#define AGIS_HIGH_PRECISION

#ifdef AGIS_HIGH_PRECISION
#include <gmp.h>
static int constexpr bits = 128;
#endif

AGIS_API inline void gmp_add_assign(double& x, double y)
{
#ifdef AGIS_HIGH_PRECISION
    mpf_t mpf_x, mpf_y;
    mpf_init2(mpf_x, bits);
    mpf_init2(mpf_y, bits);
    mpf_set_d(mpf_x, x);
    mpf_set_d(mpf_y, y);
    mpf_add(mpf_x, mpf_x, mpf_y);

    x = mpf_get_d(mpf_x);

    mpf_clear(mpf_x);
    mpf_clear(mpf_y);
#else 
    x += y;
#endif
}

inline void gmp_sub_assign(double& x, double y)
{
#ifdef AGIS_HIGH_PRECISION
    mpf_t mpf_x, mpf_y;
    mpf_init2(mpf_x, bits);
    mpf_init2(mpf_y, bits);
    mpf_set_d(mpf_x, x);
    mpf_set_d(mpf_y, y);
    mpf_sub(mpf_x, mpf_x, mpf_y);

    x = mpf_get_d(mpf_x);

    mpf_clear(mpf_x);
    mpf_clear(mpf_y);
#else
    x -= y;
#endif
}

inline void gmp_div_assign(double& x, double y)
{
#ifdef AGIS_HIGH_PRECISION
    mpf_t mpf_x, mpf_y;
    mpf_init2(mpf_x, bits);
    mpf_init2(mpf_y, bits);
    mpf_set_d(mpf_x, x);
    mpf_set_d(mpf_y, y);
    mpf_div(mpf_x, mpf_x, mpf_y);

    x = mpf_get_d(mpf_x);

    mpf_clear(mpf_x);
    mpf_clear(mpf_y);
#else
    x /= y;
#endif
}

inline double gmp_mult(double x, double y)
{
#ifdef AGIS_HIGH_PRECISION
    mpf_t mpf_x, mpf_y, mpf_res;
    mpf_init2(mpf_res, bits);
    mpf_init2(mpf_x, bits);
    mpf_init2(mpf_y, bits);
    mpf_set_d(mpf_x, x);
    mpf_set_d(mpf_y, y);
    mpf_mul(mpf_res, mpf_x, mpf_y);

    auto res_double = mpf_get_d(mpf_res);

    mpf_clear(mpf_x);
    mpf_clear(mpf_y);
    mpf_clear(mpf_res);

    return res_double;
#else
    return x * y;
#endif
}

inline double gmp_div(double x, double y)
{
#ifdef AGIS_HIGH_PRECISION
    mpf_t mpf_x, mpf_y, mpf_res;
    mpf_init2(mpf_res, bits);
    mpf_init2(mpf_x, bits);
    mpf_init2(mpf_y, bits);
    mpf_set_d(mpf_x, x);
    mpf_set_d(mpf_y, y);
    mpf_div(mpf_res, mpf_x, mpf_y);

    auto res_double = mpf_get_d(mpf_res);

    mpf_clear(mpf_x);
    mpf_clear(mpf_y);
    mpf_clear(mpf_res);

    return res_double;
#else
    return x / y;
#endif
}

inline double gmp_sub(double x, double y)
{
#ifdef AGIS_HIGH_PRECISION
    mpf_t mpf_x, mpf_y, mpf_res;
    mpf_init2(mpf_res, bits);
    mpf_init2(mpf_x, bits);
    mpf_init2(mpf_y, bits);
    mpf_set_d(mpf_x, x);
    mpf_set_d(mpf_y, y);
    mpf_sub(mpf_res, mpf_x, mpf_y);

    auto res_double = mpf_get_d(mpf_res);

    mpf_clear(mpf_x);
    mpf_clear(mpf_y);
    mpf_clear(mpf_res);
    return res_double;
#else
    return x - y;
#endif
}

inline double gmp_add(double x, double y)
{
#ifdef AGIS_HIGH_PRECISION
    mpf_t mpf_x, mpf_y, mpf_res;
    mpf_init2(mpf_res, bits);
    mpf_init2(mpf_x, bits);
    mpf_init2(mpf_y, bits);
    mpf_set_d(mpf_x, x);
    mpf_set_d(mpf_y, y);
    mpf_add(mpf_res, mpf_x, mpf_y);

    auto res_double = mpf_get_d(mpf_res);

    mpf_clear(mpf_x);
    mpf_clear(mpf_y);
    mpf_clear(mpf_res);
    return res_double;
#else
    return x + y;
#endif
}