/* MPFR 4.x functions that MPC 1.4 uses, for linking it against MPFR 3.1.3 (the newest MPFR with a configure script
 * reachable from this build environment). Same semantics: the products are computed exactly (precision = sum of the
 * operands' precisions), then one correctly rounded addition or subtraction, so the result is correctly rounded as in
 * MPFR 4. (MPFR 4's versions also avoid overflow in the intermediate products; with operands near the exponent limits
 * these could return an infinity where MPFR 4 returns a finite value. GCC only uses MPC to fold complex constants.) */
#include <gmp.h>
#include <mpfr.h>

int mpfr_cmpabs_ui(mpfr_srcptr x, unsigned long u)
{
    mpfr_t t;
    int r;
    mpfr_init2(t, mpfr_get_prec(x));
    mpfr_abs(t, x, MPFR_RNDN);                /* exact: same precision */
    r = mpfr_cmp_ui(t, u);
    mpfr_clear(t);
    return r;
}

static int fmm(mpfr_ptr z, mpfr_srcptr a, mpfr_srcptr b, mpfr_srcptr c, mpfr_srcptr d, mpfr_rnd_t rnd, int sub)
{
    mpfr_t p, q;
    int r;
    mpfr_init2(p, mpfr_get_prec(a) + mpfr_get_prec(b));
    mpfr_init2(q, mpfr_get_prec(c) + mpfr_get_prec(d));
    mpfr_mul(p, a, b, MPFR_RNDN);            /* exact */
    mpfr_mul(q, c, d, MPFR_RNDN);            /* exact */
    r = sub ? mpfr_sub(z, p, q, rnd) : mpfr_add(z, p, q, rnd);
    mpfr_clear(p);
    mpfr_clear(q);
    return r;
}

int mpfr_fmma(mpfr_ptr z, mpfr_srcptr a, mpfr_srcptr b, mpfr_srcptr c, mpfr_srcptr d, mpfr_rnd_t rnd)
{
    return fmm(z, a, b, c, d, rnd, 0);
}

int mpfr_fmms(mpfr_ptr z, mpfr_srcptr a, mpfr_srcptr b, mpfr_srcptr c, mpfr_srcptr d, mpfr_rnd_t rnd)
{
    return fmm(z, a, b, c, d, rnd, 1);
}
