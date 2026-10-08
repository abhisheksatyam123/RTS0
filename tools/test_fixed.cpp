// Headless test for src/core/math/fixed.h (rtso::Fixed, 16.16 fixed-point).
// Build and run:
//   g++ -std=c++20 -O2 -Isrc tools/test_fixed.cpp -o /tmp/test_fixed && /tmp/test_fixed

#include "core/math/fixed.h"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <random>

using rtso::Fixed;

static const double kPi = 3.14159265358979323846;

static int g_checks = 0;
static int g_failed = 0;

static void check(bool ok, const char *name)
{
	++g_checks;
	if (!ok)
		++g_failed;
	std::printf("%s  %s\n", ok ? "PASS" : "FAIL", name);
}

static bool near(double got, double want, double tol)
{
	return std::fabs(got - want) <= tol;
}

// Exact check that r = isqrt64(n): r*r <= n < (r+1)^2.
static bool isqrt_exact(uint64_t n, uint32_t r)
{
	if (static_cast<uint64_t>(r) * r > n)
		return false;
	if (r == 0xFFFFFFFFu)
		return true; // (r+1)^2 = 2^64 > any u64
	uint64_t r1 = static_cast<uint64_t>(r) + 1;
	return r1 * r1 > n;
}

int main()
{
	// Storage and constants.
	check(Fixed::FromInt(1).GetInternalValue() == 65536, "FromInt(1) raw == 65536");
	check(Fixed::FromInt(-3).GetInternalValue() == -196608, "FromInt(-3) raw == -196608");
	check(Fixed::Zero().IsZero() && Fixed::Epsilon().GetInternalValue() == 1, "Zero / Epsilon");
	check(Fixed::Pi().GetInternalValue() == 205887, "Pi raw == 205887 (pi << 16)");
	check(near(Fixed::Pi().ToDouble(), 3.14159265358979, 1.0 / 65536), "Pi ~= 3.14159");

	// FromString.
	check(Fixed::FromString("1.5").GetInternalValue() == 98304, "FromString(\"1.5\") == 1.5");
	check(Fixed::FromString("-2.25").GetInternalValue() == -147456, "FromString(\"-2.25\") == -2.25");
	check(Fixed::FromString("+3").GetInternalValue() == 3 * 65536, "FromString(\"+3\") == 3");
	check(Fixed::FromString(".5").GetInternalValue() == 32768, "FromString(\".5\") == 0.5");
	check(Fixed::FromString("12.").GetInternalValue() == 12 * 65536, "FromString(\"12.\") == 12");
	check(Fixed::FromString("").IsZero(), "FromString(\"\") == 0");
	check(Fixed::FromString("1.99999").GetInternalValue() == 65536 + 65535, "FromString(\"1.99999\") truncates at 5 digits");
	check(Fixed::FromString("0.00001").IsZero(), "FromString(\"0.00001\") rounds down to 0");
	check(Fixed::FromString("12abc").GetInternalValue() == 12 * 65536, "FromString stops at invalid char");

	// FromFloat / FromDouble / FromFraction.
	check(Fixed::FromFloat(1.5f).GetInternalValue() == 98304, "FromFloat(1.5) == 1.5");
	check(Fixed::FromFloat(-1.5f).GetInternalValue() == -98304, "FromFloat(-1.5) == -1.5");
	check(Fixed::FromDouble(0.5 / 65536).GetInternalValue() == 1, "FromDouble rounds half away from zero (+)");
	check(Fixed::FromDouble(-0.5 / 65536).GetInternalValue() == -1, "FromDouble rounds half away from zero (-)");
	check(Fixed::FromDouble(0.4 / 65536).GetInternalValue() == 0, "FromDouble rounds 0.4 ulp to 0");
	check(Fixed::FromFloat(INFINITY).IsZero() && Fixed::FromDouble(NAN).IsZero(), "non-finite input gives 0");
	check(Fixed::FromFraction(1, 3).GetInternalValue() == 21845, "FromFraction(1,3) == 21845 raw");
	check(near(Fixed::FromDouble(2.7).ToDouble(), 2.7, 1.0 / 65536), "FromDouble/ToDouble round trip");
	check(near(Fixed::FromInt(5).ToFloat(), 5.0, 0.0), "ToFloat(5) == 5");

	// ToInt rounding modes on negatives and ties.
	Fixed m15 = Fixed::FromString("-1.5");
	Fixed p25 = Fixed::FromString("2.5");
	Fixed m25 = Fixed::FromString("-2.5");
	check(m15.ToInt_RoundToZero() == -1, "ToInt_RoundToZero(-1.5) == -1");
	check(m15.ToInt_RoundToInfinity() == -1, "ToInt_RoundToInfinity(-1.5) == -1");
	check(m15.ToInt_RoundToNegInfinity() == -2, "ToInt_RoundToNegInfinity(-1.5) == -2");
	check(m15.ToInt_RoundToNearest() == -1, "ToInt_RoundToNearest(-1.5) == -1 (ties to +inf)");
	check(p25.ToInt_RoundToNearest() == 3, "ToInt_RoundToNearest(2.5) == 3 (ties to +inf)");
	check(m25.ToInt_RoundToNearest() == -2, "ToInt_RoundToNearest(-2.5) == -2 (ties to +inf)");
	check(Fixed::FromString("1.7").ToInt_RoundToZero() == 1, "ToInt_RoundToZero(1.7) == 1");
	check(Fixed::FromString("1.2").ToInt_RoundToInfinity() == 2, "ToInt_RoundToInfinity(1.2) == 2");

	// Arithmetic and comparison.
	Fixed a = Fixed::FromInt(7), b = Fixed::FromInt(3);
	check((a + b) == Fixed::FromInt(10) && (a - b) == Fixed::FromInt(4), "add / sub");
	check((-a).GetInternalValue() == -7 * 65536 && (-a).Absolute() == a, "unary minus / Absolute");
	check(Fixed::FromInt(1) / Fixed::FromInt(2) == Fixed::FromString("0.5"), "fixed / fixed");
	check((Fixed::FromInt(1) / -1) == Fixed::FromInt(-1), "fixed / int(-1)");
	check((Fixed::FromInt(7) / 2) == Fixed::FromString("3.5"), "fixed / int");
	check(Fixed::FromInt(-7) % Fixed::FromInt(3) == Fixed::FromInt(2), "mod: sign follows divisor (+)");
	check(Fixed::FromInt(7) % Fixed::FromInt(-3) == Fixed::FromInt(-2), "mod: sign follows divisor (-)");
	check(a > b && b < a && a >= a && b <= b && a != b && !(a == b), "comparisons");
	check(Fixed::FromInt(30000).MultiplyClamp(100000).GetInternalValue() == INT32_MAX, "MultiplyClamp saturates high");
	check(Fixed::FromInt(-30000).MultiplyClamp(100000).GetInternalValue() == INT32_MIN, "MultiplyClamp saturates low");
	check((Fixed::FromInt(32767) + Fixed::FromInt(1)).GetInternalValue() == INT32_MIN, "add wraps on overflow (two's complement)");

	// Multiply rounding: 64-bit product, arithmetic shift (floor), truncation.
	check(Fixed::FromString("1.5").Multiply(Fixed::FromString("1.5")) == Fixed::FromString("2.25"), "Multiply(1.5, 1.5) == 2.25");
	check(Fixed::FromString("0.5").Square() == Fixed::FromString("0.25"), "Square(0.5) == 0.25");
	check(Fixed::Epsilon().Multiply(Fixed::Epsilon()).IsZero(), "Multiply(eps, eps) rounds to 0");
	check(Fixed(Fixed::Epsilon()).Multiply(Fixed::Zero() - Fixed::Epsilon()).GetInternalValue() == -1, "Multiply(eps, -eps) floors to -1 eps");
	check((Fixed::Zero() - Fixed::Epsilon()).Multiply(Fixed::Zero() - Fixed::Epsilon()).IsZero(), "Multiply(-eps, -eps) == 0");
	check(Fixed::FromInt(3).MulDiv(Fixed::FromInt(2), Fixed::FromInt(4)) == Fixed::FromString("1.5"), "MulDiv(3, 2, 4) == 1.5");
	check(Fixed::FromInt(-3).MulDiv(Fixed::FromInt(2), Fixed::FromInt(4)) == Fixed::FromString("-1.5"), "MulDiv negative");

	// Sqrt: known values and exact integer check.
	check(Fixed::FromInt(2).Sqrt().GetInternalValue() == 92681, "Sqrt(2) raw == 92681");
	check(Fixed::FromInt(4).Sqrt() == Fixed::FromInt(2), "Sqrt(4) == 2");
	check(Fixed::Zero().Sqrt().IsZero() && (Fixed::FromInt(-4)).Sqrt().IsZero(), "Sqrt(<=0) == 0");
	{
		uint32_t big = Fixed::FromInt(32767).Sqrt().GetInternalValue();
		// Exact floor(sqrt(32767 * 2^32)) via isqrt_exact.
		check(isqrt_exact(static_cast<uint64_t>(32767) << 32, big), "Sqrt(32767) is exact floor in internal units");
		check(near(big / 65536.0, std::sqrt(32767.0), 1.0 / 65536), "Sqrt(32767) ~= 181.019");
	}
	{
		Fixed mx = Fixed::FromInternalValue(INT32_MAX);
		uint32_t r = mx.Sqrt().GetInternalValue();
		check(isqrt_exact(static_cast<uint64_t>(INT32_MAX) << 16, r), "Sqrt(max fixed) is exact floor");
	}
	check(rtso::detail::isqrt64(0) == 0 && rtso::detail::isqrt64(1) == 1 && rtso::detail::isqrt64(UINT64_MAX) == 0xFFFFFFFFu,
			"isqrt64 edge values (0, 1, u64 max)");

	std::mt19937_64 rng(0xC0FFEEu);
	{
		bool ok = true;
		for (int i = 0; i < 100000 && ok; ++i)
		{
			uint64_t n = rng();
			ok = isqrt_exact(n, rtso::detail::isqrt64(n));
		}
		check(ok, "isqrt64 exact over 100k random u64");
	}
	{
		// Sqrt vs std::sqrt over 100k random positive fixed values: within 1 ulp (1/65536).
		std::mt19937 gen(12345);
		std::uniform_int_distribution<int32_t> dist(1, INT32_MAX);
		double max_ulp = 0.0;
		for (int i = 0; i < 100000; ++i)
		{
			Fixed x = Fixed::FromInternalValue(dist(gen));
			double got = x.Sqrt().ToDouble() * 65536.0;
			double want = std::sqrt(x.ToDouble()) * 65536.0;
			double d = std::fabs(got - want);
			if (d > max_ulp)
				max_ulp = d;
		}
		std::printf("      (max Sqrt error over 100k samples: %.6f ulp)\n", max_ulp);
		check(max_ulp <= 1.0, "Sqrt matches std::sqrt within 1 ulp over 100k random inputs");
	}

	// sincos_approx.
	{
		Fixed s, c;
		rtso::fixed::sincos_approx(Fixed::FromInt(0), s, c);
		check(near(s.ToDouble(), 0.0, 0.001) && near(c.ToDouble(), 1.0, 0.001), "sincos(0) == (0, 1)");
		rtso::fixed::sincos_approx(Fixed::Pi(), s, c);
		check(near(s.ToDouble(), 0.0, 0.001) && near(c.ToDouble(), -1.0, 0.001), "sincos(pi) == (0, -1)");
		Fixed half_pi = Fixed::Pi().MulDiv(Fixed::FromInt(1), Fixed::FromInt(2));
		rtso::fixed::sincos_approx(half_pi, s, c);
		check(near(s.ToDouble(), 1.0, 0.001) && near(c.ToDouble(), 0.0, 0.001), "sincos(pi/2) == (1, 0)");
		rtso::fixed::sincos_approx(-half_pi, s, c);
		check(near(s.ToDouble(), -1.0, 0.001) && near(c.ToDouble(), 0.0, 0.001), "sincos(-pi/2) == (-1, 0)");

		double max_err = 0.0;
		std::uniform_real_distribution<double> ang(-2.0 * kPi, 2.0 * kPi);
		for (int i = 0; i < 20000; ++i)
		{
			double t = ang(rng);
			rtso::fixed::sincos_approx(Fixed::FromDouble(t), s, c);
			double es = std::fabs(s.ToDouble() - std::sin(t));
			double ec = std::fabs(c.ToDouble() - std::cos(t));
			if (es > max_err) max_err = es;
			if (ec > max_err) max_err = ec;
		}
		std::printf("      (max sin/cos error over [-2pi, 2pi]: %.6f)\n", max_err);
		check(max_err < 0.001, "sincos max error < 0.001 over [-2pi, 2pi]");
	}

	// atan2_approx quadrants (vendor max error ~0.08 rad).
	{
		const double tol = 0.08;
		check(near(rtso::fixed::atan2_approx(Fixed::FromInt(1), Fixed::FromInt(1)).ToDouble(), kPi / 4, tol), "atan2(1,1) ~= pi/4");
		check(near(rtso::fixed::atan2_approx(Fixed::FromInt(1), Fixed::FromInt(-1)).ToDouble(), 3 * kPi / 4, tol), "atan2(1,-1) ~= 3pi/4");
		check(near(rtso::fixed::atan2_approx(Fixed::FromInt(-1), Fixed::FromInt(-1)).ToDouble(), -3 * kPi / 4, tol), "atan2(-1,-1) ~= -3pi/4");
		check(near(rtso::fixed::atan2_approx(Fixed::FromInt(-1), Fixed::FromInt(1)).ToDouble(), -kPi / 4, tol), "atan2(-1,1) ~= -pi/4");
		check(rtso::fixed::atan2_approx(Fixed::Zero(), Fixed::FromInt(1)).IsZero(), "atan2(0,1) == 0");
		check(near(rtso::fixed::atan2_approx(Fixed::FromInt(1), Fixed::Zero()).ToDouble(), kPi / 2, tol), "atan2(1,0) ~= pi/2");
		check(near(rtso::fixed::atan2_approx(Fixed::FromInt(-1), Fixed::Zero()).ToDouble(), -kPi / 2, tol), "atan2(-1,0) ~= -pi/2");
		check(near(rtso::fixed::atan2_approx(Fixed::Zero(), Fixed::FromInt(-1)).ToDouble(), kPi, tol), "atan2(0,-1) ~= pi");
		check(rtso::fixed::atan2_approx(Fixed::Zero(), Fixed::Zero()).IsZero(), "atan2(0,0) == 0 (no division by zero)");
	}

	std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
	std::printf("RESULT: %s\n", g_failed == 0 ? "PASS" : "FAIL");
	return g_failed == 0 ? 0 : 1;
}
