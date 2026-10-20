// Deterministic maths for the lockstep simulation: the same bits on every compiler, CPU and C library.
// Every function computes in double with + - * / and sqrt only (all IEEE-754 correctly rounded,
// so constant folding and runtime agree); the float overloads then round once to float. The kernels
// are fdlibm's (Sun Microsystems, freely redistributable): each double result is within about 1.5 ulp
// of the exact value (acos 2.1), so the float result is the correctly rounded one except when the
// true value lies within ~2^-51 of a float rounding boundary (pow: |b ln a| times that). Double,
// integer and mixed arguments get double results, as <cmath> gives them, so replacing std:: with
// det:: never narrows a computation to float. Special values (zeros, infinities) follow C99 Annex F;
// every NaN result is the one canonical quiet NaN, since hardware NaN bits differ between CPUs.
// Requires no FP contraction (FMA fusion): SConstruct passes -ffp-contract=off (GCC/Clang),
// /fp:precise (MSVC) and /clang:-ffp-contract=off (clang-cl) to every source, and
// tools/test_cross_compiler.py lints it. The clang and MSVC pragmas below also keep it off from here
// to the end of any file that forgets the flag. GCC has no such guard (its optimize pragma would stop
// these functions inlining), so a GCC build relies on SConstruct's -ffp-contract=off alone.
// Simulation code (src/simulation, src/core, turn_manager) calls det::sin, det::cos, det::atan2,
// det::acos, det::hypot, det::pow, det::log and det::exp, never <cmath>'s; tools/check_det_math.py
// enforces it. sqrt, floor, fmod and fabs are exact everywhere and stay std::.
// Include this before anything else in a file: sim_types.h includes formation.h inside
// namespace rtso, so a first include there would declare rtso::rtso::det.
//
// The sin, cos, atan, log and exp kernels and their coefficients come from fdlibm (k_sin.c, k_cos.c,
// s_atan.c, e_log.c, e_exp.c), whose notice is:
//   Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
//   Developed at SunSoft, a Sun Microsystems, Inc. business.
//   Permission to use, copy, modify, and distribute this software is freely granted,
//   provided that this notice is preserved.
#ifndef RTSO_CORE_MATH_DET_MATH_H
#define RTSO_CORE_MATH_DET_MATH_H

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#endif

namespace rtso::det {
namespace detail {

inline uint64_t bits(double x) { uint64_t u; std::memcpy(&u, &x, 8); return u; }
inline double from_bits(uint64_t u) { double x; std::memcpy(&x, &u, 8); return x; }
// The canonical quiet NaN: computed NaNs (0/0, inf - inf) carry the sign and payload of the CPU
// that made them (x86 SSE sets the sign bit, ARM and clang's constant folder don't).
inline double nan_d() { return std::numeric_limits<double>::quiet_NaN(); }
// 2^k for -1022 <= k <= 1023, exactly.
inline double pow2i(int k) { return from_bits((uint64_t)(k + 1023) << 52); }
// Nearest integer, ties away from zero, |x| < 2^52 (floor only).
inline double nearest(double x) { return x >= 0.0 ? std::floor(x + 0.5) : -std::floor(-x + 0.5); }

// fdlibm k_sin.c / k_cos.c on |x| <= pi/4.
inline double ksin(double x) {
	const double S1 = -1.66666666666666324348e-01, S2 = 8.33333333332248946124e-03, S3 = -1.98412698298579493134e-04,
				 S4 = 2.75573137070700676789e-06, S5 = -2.50507602534068634195e-08, S6 = 1.58969099521155010221e-10;
	const double z = x * x, v = z * x, r = S2 + z * (S3 + z * (S4 + z * (S5 + z * S6)));
	return x + v * (S1 + z * r);
}
inline double kcos(double x) {
	const double C1 = 4.16666666666666019037e-02, C2 = -1.38888888888741095749e-03, C3 = 2.48015872894767294178e-05,
				 C4 = -2.75573143513906633035e-07, C5 = 2.08757232129817482790e-09, C6 = -1.13596475577881948265e-11;
	const double z = x * x, r = z * (C1 + z * (C2 + z * (C3 + z * (C4 + z * (C5 + z * C6)))));
	const double hz = 0.5 * z, w = 1.0 - hz;
	return w + (((1.0 - w) - hz) + z * r);
}
// x = n * pi/2 + r, |r| <= ~pi/4 (Cody-Waite, fdlibm's three-part pi/2), for finite x. The products
// are exact for |x| < 2^20 pi/2 (1.6e6; the game's angles stay below 1e4); from there to 2^51 fn * P1
// rounds and r is off by up to ulp(x)/2 radians. Past 2^51 x * invpio2 can round to a half and fn to
// the wrong quadrant: |r| nears pi/2, where the polynomials exceed 1, and larger x gives inf and at
// last a hardware NaN from kcos's inf - inf. So such x is first taken modulo 4 P1 (2 pi to 33 bits;
// 4 P1 and fmod are exact): that drifts from the true angle by about 4e-11 |x| radians, more than
// 2 pi there, but the result stays in [-1, 1] and the same bits everywhere. n is the quadrant, fn mod 4
// (a float-to-integer conversion of a huge fn would be undefined).
inline double reduce_pio2(double x, int &n) {
	const double invpio2 = 6.36619772367581382433e-01, P1 = 1.57079632673412561417e+00,
				 P2 = 6.07710050630396597660e-11, P3 = 2.02226624879595063154e-21;
	if (std::fabs(x) >= 0x1p51) x = std::fmod(x, 4.0 * P1);
	const double fn = nearest(x * invpio2);
	n = (int)(fn - 4.0 * std::floor(fn * 0.25));
	return ((x - fn * P1) - fn * P2) - fn * P3;
}
inline double sin_d(double x) {
	if (std::fabs(x) < 0x1p-27) return x; // fdlibm: sin(x) rounds to x, and keeps -0
	if (std::fabs(x) <= 0.78539816339744828) return ksin(x);
	if (!std::isfinite(x)) return nan_d();
	int n;
	const double r = reduce_pio2(x, n);
	switch (n & 3) {
		case 0: return ksin(r);
		case 1: return kcos(r);
		case 2: return -ksin(r);
		default: return -kcos(r);
	}
}
inline double cos_d(double x) {
	if (std::fabs(x) <= 0.78539816339744828) return kcos(x);
	if (!std::isfinite(x)) return nan_d();
	int n;
	const double r = reduce_pio2(x, n);
	switch (n & 3) {
		case 0: return kcos(r);
		case 1: return -ksin(r);
		case 2: return -kcos(r);
		default: return ksin(r);
	}
}
// fdlibm s_atan.c.
inline double atan_d(double x) {
	static const double hi[] = {4.63647609000806093515e-01, 7.85398163397448278999e-01, 9.82793723247329054082e-01, 1.57079632679489655800e+00};
	static const double lo[] = {2.26987774529616870924e-17, 3.06161699786838301793e-17, 1.39033110312309984516e-17, 6.12323399573676603587e-17};
	static const double aT[] = {3.33333333333329318027e-01, -1.99999999998764832476e-01, 1.42857142725034663711e-01,
		-1.11111104054623557880e-01, 9.09088713343650656196e-02, -7.69187620504482999495e-02, 6.66107313738753120669e-02,
		-5.83357013379057348645e-02, 4.97687799461593236017e-02, -3.65315727442169155270e-02, 1.62858201153657823623e-02};
	if (x != x) return nan_d();
	const bool neg = x < 0.0;
	double ax = neg ? -x : x;
	if (ax >= 1.8014398509481984e16) return neg ? -(hi[3] + lo[3]) : hi[3] + lo[3]; // 2^54
	int id = -1;
	if (ax >= 0.4375) {
		if (ax < 1.1875) {
			if (ax < 0.6875) { id = 0; ax = (2.0 * ax - 1.0) / (2.0 + ax); }
			else { id = 1; ax = (ax - 1.0) / (ax + 1.0); }
		} else if (ax < 2.4375) { id = 2; ax = (ax - 1.5) / (1.0 + 1.5 * ax); }
		else { id = 3; ax = -1.0 / ax; }
	}
	const double z = ax * ax, w = z * z;
	const double s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
	const double s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
	double r;
	if (id < 0) r = ax - ax * (s1 + s2);
	else r = hi[id] - ((ax * (s1 + s2) - lo[id]) - ax);
	return neg ? -r : r;
}
// IEEE atan2 special cases as C99 Annex F (what glibc, Apple, bionic and the MSVC CRT return).
inline double atan2_d(double y, double x) {
	const double pi = 3.1415926535897931160e+00, pi_lo = 1.2246467991473531772e-16, pio2 = 1.5707963267948965580e+00;
	if (x != x || y != y) return nan_d();
	const bool yneg = std::signbit(y), xneg = std::signbit(x);
	if (y == 0.0) return xneg ? (yneg ? -pi : pi) : y;
	if (x == 0.0) return yneg ? -pio2 : pio2;
	if (std::isinf(x)) { // C99 F.9.1.4: +-pi/4, +-3pi/4, +-0, +-pi
		const double r = std::isinf(y) ? (xneg ? 3.0 * 0.78539816339744828 : 0.78539816339744828) : (xneg ? pi : 0.0);
		return yneg ? -r : r;
	}
	if (std::isinf(y)) return yneg ? -pio2 : pio2;
	const double a = atan_d(std::fabs(y / x));
	const double r = xneg ? pi - (a - pi_lo) : a;
	return yneg ? -r : r;
}
// fdlibm e_log.c (x > 0, finite).
inline double log_d(double x) {
	const double ln2_hi = 6.93147180369123816490e-01, ln2_lo = 1.90821492927058770002e-10;
	const double Lg1 = 6.666666666666735130e-01, Lg2 = 3.999999999940941908e-01, Lg3 = 2.857142874366239149e-01,
				 Lg4 = 2.222219843214978396e-01, Lg5 = 1.818357216161805012e-01, Lg6 = 1.531383769920937332e-01,
				 Lg7 = 1.479819860511658591e-01;
	if (!(x > 0.0)) return x == 0.0 ? -HUGE_VAL : nan_d(); // log(-0) = -inf; negatives and NaN
	if (std::isinf(x)) return x;
	int k = 0;
	if (x < 2.2250738585072014e-308) { x *= 18014398509481984.0; k -= 54; } // subnormal
	uint64_t u = bits(x);
	k += (int)(u >> 52) - 1023;
	u &= 0x000fffffffffffffull;
	// m in [sqrt(2)/2, sqrt(2))
	double m = from_bits(u | 0x3ff0000000000000ull);
	if (m > 1.4142135623730951) { m *= 0.5; ++k; }
	const double f = m - 1.0, s = f / (2.0 + f), z = s * s, w = z * z;
	const double t1 = w * (Lg2 + w * (Lg4 + w * Lg6)), t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
	const double R = t2 + t1, hfsq = 0.5 * f * f, dk = (double)k;
	return dk * ln2_hi - ((hfsq - (s * (hfsq + R) + dk * ln2_lo)) - f);
}
// fdlibm e_exp.c.
inline double exp_d(double x) {
	const double ln2HI = 6.93147180369123816490e-01, ln2LO = 1.90821492927058770002e-10, invln2 = 1.44269504088896338700e+00;
	const double P1 = 1.66666666666666019037e-01, P2 = -2.77777777770155933842e-03, P3 = 6.61375632143793436117e-05,
				 P4 = -1.65339022054652515390e-06, P5 = 4.13813679705723846039e-08;
	if (x != x) return nan_d();
	if (x > 709.782712893384) return HUGE_VAL;
	if (x < -745.13321910194110842) return 0.0;
	const double fk = nearest(x * invln2);
	const int k = (int)fk;
	const double hi = x - fk * ln2HI, lo = fk * ln2LO, r = hi - lo, t = r * r;
	const double c = r - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
	const double y = 1.0 - ((lo - (r * c) / (2.0 - c)) - hi);
	if (k >= -1021) return k <= 1023 ? y * pow2i(k) : y * 2.0 * pow2i(k - 1);
	return y * pow2i(k + 1000) * pow2i(-1000);
}

// acos(d) = atan2(sqrt(1 - d^2), d); (1 - d)(1 + d) is exact for a float d.
inline double acos_d(double d) {
	if (!(d >= -1.0 && d <= 1.0)) return nan_d();
	return atan2_d(std::sqrt((1.0 - d) * (1.0 + d)), d);
}
// glibc's hypotf algorithm: exact squares of floats, one double sum, one sqrt. For doubles it is
// within 1.2 ulp but overflows past about 1.3e154 (and underflows below 1.5e-154): the game's
// doubles are wall geometry in metres.
inline double hypot_d(double x, double y) {
	if (std::isinf(x) || std::isinf(y)) return HUGE_VAL; // even beside a NaN (Annex F)
	if (x != x || y != y) return nan_d();
	return std::sqrt(x * x + y * y);
}
// a^k for an integer 0 <= k <= 64 by repeated squaring: exact whenever the result fits a double
// (pow(3, 3) is 27, where exp(3 log 3) is 26.999999999999996). Otherwise each squaring doubles the
// relative error, so a double result can be off by about 3k/4 ulp (49 measured for k = 64); float
// results stayed correctly rounded on all 2^25 samples of a^k and a^-k tried.
inline double ipow(double a, int k) {
	double r = 1.0;
	for (; k; k >>= 1, a *= a)
		if (k & 1) r *= a;
	return r;
}
// pow with C99 Annex F's special cases: integer exponents up to 64 by ipow, others as exp(b log a).
inline double pow_d(double a, double b) {
	if (b == 0.0 || a == 1.0) return 1.0; // even for a NaN on the other side
	if (a != a || b != b) return nan_d();
	const double aa = std::fabs(a);
	if (std::isinf(b)) return aa == 1.0 ? 1.0 : (aa > 1.0) == (b > 0.0) ? HUGE_VAL : 0.0;
	const bool integer = std::floor(b) == b, odd = integer && std::fmod(std::fabs(b), 2.0) == 1.0;
	const bool neg = odd && std::signbit(a); // the result takes a's sign only for an odd integer b
	if (a == 0.0 || std::isinf(a)) { // pow(+-0, b) and pow(+-inf, b)
		const double r = (a == 0.0) == (b > 0.0) ? 0.0 : HUGE_VAL;
		return neg ? -r : r;
	}
	if (a < 0.0 && !integer) return nan_d();
	double m;
	if (integer && std::fabs(b) <= 64.0) {
		const int k = (int)std::fabs(b);
		m = ipow(aa, k);
		// 1 / a^k is 0 where a^k overflows but a^-k is a subnormal. Where a^k is subnormal, 1 / a^k
		// only fits when a^k >= 2^-1024 (51 bits) and is about twice as accurate as ipow(1 / a, k).
		if (b < 0.0) m = std::isinf(m) ? ipow(1.0 / aa, k) : 1.0 / m;
	} else
		m = exp_d(b * log_d(aa));
	return neg ? -m : m;
}

// <cmath>'s rule for other arithmetic arguments: an integer, or a float beside a double, is
// computed in double. long double is refused, not narrowed: its width differs between platforms.
template <class... T> using Arithmetic = std::enable_if_t<(std::is_arithmetic_v<T> && ...), int>;
template <class T> inline double promote(T x) {
	static_assert(!std::is_same_v<T, long double>, "rtso::det has no long double overloads");
	return (double)x;
}

} // namespace detail

// Float in, float out: the simulation's angles, distances and damage.
inline float sin(float x) { return (float)detail::sin_d(x); }
inline float cos(float x) { return (float)detail::cos_d(x); }
inline float atan2(float y, float x) { return (float)detail::atan2_d(y, x); }
inline float acos(float x) { return (float)detail::acos_d(x); }
inline float hypot(float x, float y) { return (float)detail::hypot_d(x, y); }
inline float log(float x) { return (float)detail::log_d(x); }
inline float exp(float x) { return (float)detail::exp_d(x); }
inline float pow(float a, float b) { return (float)detail::pow_d(a, b); }

// Double in, double out, for the wall builder's double geometry. sin and cos (for |x| < 2^20 pi/2),
// atan2, hypot, log and exp are within 1.5 ulp of the exact value and acos within 2.1; a non-integer
// pow is exp(b log a), whose error grows to about 3 |b ln a| ulp (still the same bits everywhere;
// `test_det_math libm` measures all of them).
inline double sin(double x) { return detail::sin_d(x); }
inline double cos(double x) { return detail::cos_d(x); }
inline double atan2(double y, double x) { return detail::atan2_d(y, x); }
inline double acos(double x) { return detail::acos_d(x); }
inline double hypot(double x, double y) { return detail::hypot_d(x, y); }
inline double log(double x) { return detail::log_d(x); }
inline double exp(double x) { return detail::exp_d(x); }
inline double pow(double a, double b) { return detail::pow_d(a, b); }

// Integer or mixed arguments, as <cmath>: det::pow(0.9f, 2) is pow(0.9, 2.0) in double, like
// std::pow(0.9f, 2). All-float and all-double calls take the exact overloads above instead.
template <class A, detail::Arithmetic<A> = 0> inline double sin(A x) { return detail::sin_d(detail::promote(x)); }
template <class A, detail::Arithmetic<A> = 0> inline double cos(A x) { return detail::cos_d(detail::promote(x)); }
template <class A, detail::Arithmetic<A> = 0> inline double acos(A x) { return detail::acos_d(detail::promote(x)); }
template <class A, detail::Arithmetic<A> = 0> inline double log(A x) { return detail::log_d(detail::promote(x)); }
template <class A, detail::Arithmetic<A> = 0> inline double exp(A x) { return detail::exp_d(detail::promote(x)); }
template <class A, class B, detail::Arithmetic<A, B> = 0> inline double atan2(A y, B x) {
	return detail::atan2_d(detail::promote(y), detail::promote(x));
}
template <class A, class B, detail::Arithmetic<A, B> = 0> inline double hypot(A x, B y) {
	return detail::hypot_d(detail::promote(x), detail::promote(y));
}
template <class A, class B, detail::Arithmetic<A, B> = 0> inline double pow(A a, B b) {
	return detail::pow_d(detail::promote(a), detail::promote(b));
}

} // namespace rtso::det

#endif
