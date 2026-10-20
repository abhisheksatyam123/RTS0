// Platform-independent maths (src/core/math/det_math.h, REMAINING_GAPS G30): every rtso::det
// function on 2^21 inputs, float and double overloads, hashed into one 32-bit value that must be the
// same on every compiler, optimisation level, CPU and C library. It prints one line per function
// and the hash, and fails unless the hash is EXPECTED and every special value's result has the bits
// tools/test_det_math_special.inc stores for it. ci.yml runs it under g++ -O2, and
// tools/test_cross_compiler.py under g++ and clang++ -mfma.
// `special` prints that table instead (regenerate it, and EXPECTED, when det_math.h changes on purpose).
// With any other argument it also compares with this platform's C library (not part of the hash): how
// often det:: is not the correctly rounded result (float: double libm rounded to float; double:
// long double libm rounded to double), how often it differs from libm itself, and the largest error
// in ulps.
//
// g++ -std=c++20 -O2 -ffp-contract=off -Isrc tools/test_det_math.cpp -o /tmp/test_det_math && /tmp/test_det_math [libm]
#include "core/math/det_math.h"

#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

namespace det = rtso::det;

// det:: returns what <cmath> returns for the same argument types: all-float stays float, and
// double, integer or mixed arguments compute in double, so std:: -> det:: never narrows a call.
template <class A> constexpr bool same_as_cmath1 =
		std::is_same_v<decltype(det::sin(std::declval<A>())), decltype(std::sin(std::declval<A>()))> &&
		std::is_same_v<decltype(det::cos(std::declval<A>())), decltype(std::cos(std::declval<A>()))> &&
		std::is_same_v<decltype(det::acos(std::declval<A>())), decltype(std::acos(std::declval<A>()))> &&
		std::is_same_v<decltype(det::log(std::declval<A>())), decltype(std::log(std::declval<A>()))> &&
		std::is_same_v<decltype(det::exp(std::declval<A>())), decltype(std::exp(std::declval<A>()))>;
template <class A, class B> constexpr bool same_as_cmath2 =
		std::is_same_v<decltype(det::atan2(std::declval<A>(), std::declval<B>())), decltype(std::atan2(std::declval<A>(), std::declval<B>()))> &&
		std::is_same_v<decltype(det::hypot(std::declval<A>(), std::declval<B>())), decltype(std::hypot(std::declval<A>(), std::declval<B>()))> &&
		std::is_same_v<decltype(det::pow(std::declval<A>(), std::declval<B>())), decltype(std::pow(std::declval<A>(), std::declval<B>()))>;
static_assert(same_as_cmath1<float> && same_as_cmath1<double> && same_as_cmath1<int> && same_as_cmath1<uint8_t> &&
		same_as_cmath1<int64_t> && same_as_cmath1<const float &> && same_as_cmath1<double &>);
static_assert(same_as_cmath2<float, float> && same_as_cmath2<double, double> && same_as_cmath2<float, double> &&
		same_as_cmath2<double, float> && same_as_cmath2<float, int> && same_as_cmath2<int, float> &&
		same_as_cmath2<double, int> && same_as_cmath2<int, int> && same_as_cmath2<float, uint16_t>);

static constexpr uint32_t EXPECTED = 0x4c12b0bcu;

// The result bits, per function and over everything: each 32-bit word is xored in and avalanched
// with MurmurHash3's fmix32 (a bijection), so a difference in any bit reaches every hash bit and two
// differences cancel only by chance. (Word-wise FNV-1a let two sign flips cancel: a bit-31
// difference only ever reached bit 31.)
static uint32_t H = 2166136261u;
struct Hash {
	uint32_t h = 2166136261u;
	static uint32_t step(uint32_t h, uint32_t w) {
		h ^= w;
		h ^= h >> 16; h *= 0x85ebca6bu; h ^= h >> 13; h *= 0xc2b2ae35u; h ^= h >> 16;
		return h;
	}
	void word(uint32_t w) { h = step(h, w); H = step(H, w); }
	void mix(float f) { uint32_t b; std::memcpy(&b, &f, 4); word(b); }
	void mix(double d) { uint64_t b; std::memcpy(&b, &d, 8); word((uint32_t)b); word((uint32_t)(b >> 32)); }
};

// Deterministic inputs from an integer LCG, scaled only by exact operations, so neither rounding
// nor FP contraction can change them between builds.
static uint32_t s = 12345u;
static uint32_t next() { return s = s * 1664525u + 1013904223u; }
// Float in [lo, hi) as a power-of-two-wide window: lo = -2^e or 0, width 2^(e+1) or 2^e.
static float uf(float lo, float hi) {
	next();
	const float t = std::ldexp((float)(s >> 8), -24); // [0, 1) exactly
	int e; std::frexp(hi - lo, &e);
	const float w = std::ldexp(1.0f, e); // a power of two >= hi - lo
	if (lo < 0.0f && hi > 0.0f) { // symmetric: [-2^k, 2^k) with 2^k the smallest power of two >= hi
		int k; const float m = std::frexp(hi, &k); if (m == 0.5f) --k;
		return std::ldexp((float)((int32_t)(s >> 8) - (1 << 23)), k - 23);
	}
	return lo + std::ldexp(t, e) * (hi - lo) / w;
}
// Double with 53 random bits in [0, 2^e), or in [-2^e, 2^e) when sym.
static double ud(int e, bool sym) {
	const uint64_t hi = next() >> 6, lo = next() >> 5; // 26 + 27 bits
	const uint64_t m = hi << 27 | lo;
	return sym ? std::ldexp((double)((int64_t)m - (int64_t)(1ull << 52)), e - 52) : std::ldexp((double)m, e - 53);
}

// Distance in ulps (0 when both are the same NaN class or infinity).
static int64_t ord(double d) { int64_t i; std::memcpy(&i, &d, 8); return i < 0 ? INT64_MIN - i : i; }
static int32_t ordf(float f) { int32_t i; std::memcpy(&i, &f, 4); return i < 0 ? INT32_MIN - i : i; }
static double ulps(double a, double b) {
	if (a != a || b != b) return (a != a) == (b != b) ? 0.0 : 1e300;
	if (std::isinf(a) || std::isinf(b)) return a == b ? 0.0 : 1e300;
	return std::fabs((double)(int64_t)((uint64_t)ord(a) - (uint64_t)ord(b))); // exact below 2^53 ulps
}
static double ulpsf(float a, float b) {
	if (a != a || b != b) return (a != a) == (b != b) ? 0.0 : 1e300;
	if (std::isinf(a) || std::isinf(b)) return a == b ? 0.0 : 1e300;
	return std::fabs((double)ordf(a) - (double)ordf(b));
}

static bool libm = false, dump = false;
static int fails = 0;
static const int N = 1 << 21;

struct Stats {
	long notcr = 0, vs_libm = 0;
	double max_ulp = 0.0;
	template <class T> void add(T r, T cr, T l) {
		notcr += std::memcmp(&r, &cr, sizeof r) != 0;
		vs_libm += std::memcmp(&r, &l, sizeof r) != 0;
		const double u = sizeof(T) == 4 ? ulpsf((float)r, (float)cr) : ulps((double)r, (double)cr);
		if (u > max_ulp) max_ulp = u;
	}
};
static void report(const char *name, const Hash &h, const Stats &st, long n) {
	if (libm)
		std::printf("%-36s %08x  det!=correctly-rounded %7ld  det!=libm %8ld  max %.0f ulp  of %ld\n", name, h.h, st.notcr,
				st.vs_libm, st.max_ulp, n);
	else
		std::printf("%-36s %08x\n", name, h.h);
}

// Every special value's result, bit for bit, in the order special_values() computes them: the hash
// alone cannot say which input changed.
static const uint64_t SPECIAL[] = {
#include "test_det_math_special.inc"
};
// Hashes each special-value result and checks it against SPECIAL (or, with `special`, prints it).
// call is a printf format for the call, so a failure prints the input.
struct Special {
	Hash h;
	size_t i = 0;
	template <class T> void operator()(T r, const char *call, double x, double y = 0.0) {
		h.mix(r);
		uint64_t b = 0;
		std::memcpy(&b, &r, sizeof r);
		const size_t at = i++;
		if (dump) { std::printf(sizeof r == 8 ? " 0x%016llx," : " 0x%08llx,", (unsigned long long)b); return; }
		if (at < sizeof SPECIAL / sizeof *SPECIAL && SPECIAL[at] == b) return;
		std::printf("FAIL: det::");
		std::printf(call, x, y);
		if (at < sizeof SPECIAL / sizeof *SPECIAL)
			std::printf(" is %0*llx, test_det_math_special.inc says %0*llx\n", (int)sizeof r * 2, (unsigned long long)b,
					(int)sizeof r * 2, (unsigned long long)SPECIAL[at]);
		else
			std::printf(" is %0*llx, past the end of test_det_math_special.inc\n", (int)sizeof r * 2, (unsigned long long)b);
		fails++;
	}
	// Ends one line of the printed table.
	void eol(const char *comment, double x, double y = 0.0) {
		if (!dump) return;
		std::printf(" // ");
		std::printf(comment, x, y);
		std::printf("\n");
	}
};

// One float argument.
static void f1(const char *name, float lo, float hi, float (*det)(float), float (*lf)(float), double (*ref)(double)) {
	Hash h; Stats st;
	for (int i = 0; i < N; ++i) {
		const float x = uf(lo, hi), r = det(x);
		h.mix(r);
		if (libm) { volatile float vx = x; st.add(r, (float)ref(vx), lf(vx)); }
	}
	report(name, h, st, N);
}
// Two float arguments.
static void f2(const char *name, float lo, float hi, float lo2, float hi2, float (*det)(float, float),
		float (*lf)(float, float), double (*ref)(double, double)) {
	Hash h; Stats st;
	for (int i = 0; i < N; ++i) {
		const float x = uf(lo, hi), y = uf(lo2, hi2), r = det(x, y);
		h.mix(r);
		if (libm) { volatile float vx = x, vy = y; st.add(r, (float)ref(vx, vy), lf(vx, vy)); }
	}
	report(name, h, st, N);
}
// One double argument; the reference is long double libm rounded to double.
static void d1(const char *name, int e, bool sym, double (*det)(double), double (*lf)(double),
		long double (*ref)(long double)) {
	Hash h; Stats st;
	for (int i = 0; i < N; ++i) {
		const double x = ud(e, sym), r = det(x);
		h.mix(r);
		if (libm) { volatile double vx = x; st.add(r, (double)ref(vx), lf(vx)); }
	}
	report(name, h, st, N);
}
// Two double arguments.
static void d2(const char *name, int e, bool sym, int e2, bool sym2, double (*det)(double, double),
		double (*lf)(double, double), long double (*ref)(long double, long double)) {
	Hash h; Stats st;
	for (int i = 0; i < N; ++i) {
		const double x = ud(e, sym), y = ud(e2, sym2), r = det(x, y);
		h.mix(r);
		if (libm) { volatile double vx = x, vy = y; st.add(r, (double)ref(vx, vy), lf(vx, vy)); }
	}
	report(name, h, st, N);
}

// Special values: C99 Annex F results, the reduction of huge angles, exp's overflow and two-step
// underflow branches, and the one canonical NaN; each result is checked against SPECIAL.
static void special_values() {
	const double inf = HUGE_VAL, nan = std::numeric_limits<double>::quiet_NaN();
	// one argument, and both arguments of atan2, hypot and pow
	const double sp[] = {0.0, -0.0, 1.0, -1.0, 0.5, -0.5, 2.0, -2.0, 3.0, -3.0, 0x1p-30, -0x1p-30, 4.9e-324, 1e300,
			-1e300, 1.7e6, 1e18, -1e18, inf, -inf, nan};
	// one argument only: sin and cos past 2^51 (1e20f, 3.4e38f as floats), exp near and past its limits
	const double sp1[] = {DBL_MAX, -DBL_MAX, 1e20, 3.4e38, -720.0, -745.0, 709.5, -708.5};
	Special sv;
	if (dump)
		std::printf("// Generated by `test_det_math special` (tools/test_det_math.cpp): the bits of every special value's\n"
				"// result, in the order it computes them. Regenerate it, and EXPECTED, only when det_math.h changes on purpose.\n");
	auto one = [&](double x) {
		const float f = (float)x;
		sv(det::sin(x), "sin(%a)", x); sv(det::cos(x), "cos(%a)", x); sv(det::acos(x), "acos(%a)", x);
		sv(det::log(x), "log(%a)", x); sv(det::exp(x), "exp(%a)", x);
		sv(det::sin(f), "sin(%af)", f); sv(det::cos(f), "cos(%af)", f); sv(det::log(f), "log(%af)", f); sv(det::exp(f), "exp(%af)", f);
		sv.eol("x = %a: sin cos acos log exp, then float sin cos log exp", x);
		// the table pins whatever the bits are; this pins that they are in range (huge angles once gave inf and NaN)
		if (!dump && ((std::isfinite(x) && !(std::fabs(det::sin(x)) <= 1.0 && std::fabs(det::cos(x)) <= 1.0)) ||
				(std::isfinite(f) && !(std::fabs(det::sin(f)) <= 1.0f && std::fabs(det::cos(f)) <= 1.0f)))) {
			std::printf("FAIL: det::sin or det::cos of %a is outside [-1, 1]\n", x);
			fails++;
		}
	};
	for (double x : sp) {
		one(x);
		if (dump) std::printf("// x = %a, each y in sp: atan2(y, x) hypot(y, x) pow(x, y), then float atan2 and pow\n", x);
		for (double y : sp) {
			const float fx = (float)x, fy = (float)y;
			sv(det::atan2(y, x), "atan2(%a, %a)", y, x); sv(det::hypot(y, x), "hypot(%a, %a)", y, x);
			sv(det::pow(x, y), "pow(%a, %a)", x, y);
			sv(det::atan2(fy, fx), "atan2(%af, %af)", fy, fx); sv(det::pow(fx, fy), "pow(%af, %af)", fx, fy);
			sv.eol("y = %a", y);
		}
	}
	for (double x : sp1) one(x);
	// pow through exp's two-step underflow, and negative integer powers whose positive power overflows
	// (the result is a subnormal) or is subnormal (the result is near DBL_MAX)
	const double pw[][2] = {{2.0, -1070.5}, {0.5, 1073.5}, {1e5, -64.0}, {-1e5, -63.0}, {1.54e-5, -64.0}, {-1.29e-5, -63.0}};
	for (const auto &p : pw) {
		sv(det::pow(p[0], p[1]), "pow(%a, %a)", p[0], p[1]);
		sv(det::pow(p[0], (int)p[1]), "pow(%a, %.0f)", p[0], (double)(int)p[1]);
		sv(det::pow((float)p[0], (float)p[1]), "pow(%af, %af)", (float)p[0], (float)p[1]);
		sv.eol("pow(%a, %a), with b an int, then in float", p[0], p[1]);
	}
	if (dump) return;
	report("special values", sv.h, Stats(), 0);
	if (sv.i != sizeof SPECIAL / sizeof *SPECIAL) {
		std::printf("FAIL: %zu special values, test_det_math_special.inc has %zu\n", sv.i, sizeof SPECIAL / sizeof *SPECIAL);
		fails++;
	}
	struct Case { const char *what; double got, want; };
	const Case cases[] = {
		{"sin(-0)", det::sin(-0.0), -0.0}, {"sin(-0f)", det::sin(-0.0f), -0.0},
		{"pow(-0, 3)", det::pow(-0.0, 3.0), -0.0}, {"pow(-0, -3)", det::pow(-0.0, -3.0), -inf},
		{"pow(-0, 2)", det::pow(-0.0, 2.0), 0.0}, {"pow(-0, -0.5)", det::pow(-0.0, -0.5), inf},
		{"pow(-1, inf)", det::pow(-1.0, inf), 1.0}, {"pow(-1, -inf)", det::pow(-1.0, -inf), 1.0},
		{"pow(0.5, -inf)", det::pow(0.5, -inf), inf}, {"pow(2, -inf)", det::pow(2.0, -inf), 0.0},
		{"pow(-inf, 0.5)", det::pow(-inf, 0.5), inf}, {"pow(-inf, -0.5)", det::pow(-inf, -0.5), 0.0},
		{"pow(-inf, 3)", det::pow(-inf, 3.0), -inf}, {"pow(-inf, -3)", det::pow(-inf, -3.0), -0.0},
		{"pow(nan, 0)", det::pow(nan, 0.0), 1.0}, {"pow(1, nan)", det::pow(1.0, nan), 1.0},
		{"pow(2, 3)", det::pow(2.0, 3.0), 8.0}, {"pow(3, 3)", det::pow(3.0, 3.0), 27.0}, {"pow(10, -2)", det::pow(10.0, -2.0), 0.01},
		{"pow(-2, 3)", det::pow(-2.0, 3.0), -8.0}, {"pow(4097f, 2f)", det::pow(4097.0f, 2.0f), 4097.0f * 4097.0f},
		{"pow(2, -64)", det::pow(2.0, -64.0), 0x1p-64}, {"pow(0x1p-33, -31)", det::pow(0x1p-33, -31), 0x1p1023},
		{"pow(0x1p17, -63)", det::pow(0x1p17, -63), 0x1p-1071}, {"pow(-0x1p17, -63)", det::pow(-0x1p17, -63), -0x1p-1071},
		{"log(-0)", det::log(-0.0), -inf}, {"log(inf)", det::log(inf), inf}, {"exp(-inf)", det::exp(-inf), 0.0},
		{"atan2(-0, -1)", det::atan2(-0.0, -1.0), -3.1415926535897931}, {"atan2(inf, -inf)", det::atan2(inf, -inf), 3 * 0.78539816339744828},
		{"hypot(nan, inf)", det::hypot(nan, inf), inf}, {"hypot(-inf, nan)", det::hypot(-inf, nan), inf},
	};
	for (const Case &c : cases)
		if (std::memcmp(&c.got, &c.want, 8) != 0) {
			std::printf("FAIL: %s is %a, not %a\n", c.what, c.got, c.want);
			fails++;
		}
	// The table pins any bits and the range check passes a flipped sign, so angles past 2^20 pi/2, where
	// the reduction's products round, are also held near the true sin and cos.
	struct Near { double x, sin, cos; };
	const Near angles[] = {{2e6, -0.65571431556347, 0.75500909687574631},
			{2e6 + 3.141592653589793, 0.65571431547944607, -0.75500909694871986},
			{1e7 + 3.141592653589793, -0.42054779330105396, 0.90727038613062534}};
	for (const Near &c : angles)
		if (!(std::fabs(det::sin(c.x) - c.sin) < 1e-6 && std::fabs(det::cos(c.x) - c.cos) < 1e-6)) {
			std::printf("FAIL: det::sin(%a) is %a and det::cos %a, not near %a and %a\n", c.x, det::sin(c.x), det::cos(c.x),
					c.sin, c.cos);
			fails++;
		}
	// NaN results must be the canonical quiet NaN, never the CPU's (x86 sets the sign bit, ARM doesn't)
	struct NaN { const char *what; double got; };
	const NaN nans[] = {{"sin(inf)", det::sin(inf)}, {"cos(-inf)", det::cos(-inf)}, {"cos(inf)", det::cos(inf)},
			{"sin(-nan)", det::sin(-nan)}, {"acos(2)", det::acos(2.0)}, {"log(-1)", det::log(-1.0)},
			{"pow(-2, 0.5)", det::pow(-2.0, 0.5)}, {"atan2(nan, 1)", det::atan2(nan, 1.0)}, {"exp(nan)", det::exp(nan)},
			{"hypot(nan, 1)", det::hypot(nan, 1.0)}};
	struct NaNf { const char *what; float got; };
	const float inf_f = HUGE_VALF, nan_f = std::numeric_limits<float>::quiet_NaN();
	const NaNf nansf[] = {{"sin(inf_f)", det::sin(inf_f)}, {"cos(-inf_f)", det::cos(-inf_f)}, {"sin(-nan_f)", det::sin(-nan_f)},
			{"acos(2f)", det::acos(2.0f)}, {"log(-1f)", det::log(-1.0f)}, {"pow(-2f, 0.5f)", det::pow(-2.0f, 0.5f)},
			{"atan2(nan_f, 1f)", det::atan2(nan_f, 1.0f)}, {"exp(nan_f)", det::exp(nan_f)}, {"hypot(1f, nan_f)", det::hypot(1.0f, nan_f)}};
	const uint64_t qnan = 0x7ff8000000000000ull;
	const uint32_t qnanf = 0x7fc00000u;
	for (const NaN &n : nans)
		if (std::memcmp(&n.got, &qnan, 8) != 0) {
			uint64_t b;
			std::memcpy(&b, &n.got, 8);
			std::printf("FAIL: det::%s is %016llx, not the canonical quiet NaN %016llx\n", n.what, (unsigned long long)b,
					(unsigned long long)qnan);
			fails++;
		}
	for (const NaNf &n : nansf)
		if (std::memcmp(&n.got, &qnanf, 4) != 0) {
			uint32_t b;
			std::memcpy(&b, &n.got, 4);
			std::printf("FAIL: det::%s is %08x, not the canonical quiet NaN %08x\n", n.what, b, qnanf);
			fails++;
		}
}

int main(int argc, char **argv) {
	dump = argc > 1 && std::strcmp(argv[1], "special") == 0;
	libm = argc > 1 && !dump;
	if (dump) {
		special_values();
		return 0;
	}
	if (libm) std::printf("libm comparison (not hashed); long double has %d mantissa bits here\n", (int)LDBL_MANT_DIG);

	// float: what the simulation calls
	f1("sin(float) [-2pi,2pi]", -6.2831853f, 6.2831853f, [](float x) { return det::sin(x); }, [](float x) { return std::sin(x); }, [](double x) { return std::sin(x); });
	f1("sin(float) [-1000,1000]", -1000.0f, 1000.0f, [](float x) { return det::sin(x); }, [](float x) { return std::sin(x); }, [](double x) { return std::sin(x); });
	f1("cos(float) [-2pi,2pi]", -6.2831853f, 6.2831853f, [](float x) { return det::cos(x); }, [](float x) { return std::cos(x); }, [](double x) { return std::cos(x); });
	f2("atan2(float) [-300,300]^2", -300.0f, 300.0f, -300.0f, 300.0f, [](float y, float x) { return det::atan2(y, x); }, [](float y, float x) { return std::atan2(y, x); }, [](double y, double x) { return std::atan2(y, x); });
	f2("hypot(float) [-300,300]^2", -300.0f, 300.0f, -300.0f, 300.0f, [](float y, float x) { return det::hypot(y, x); }, [](float y, float x) { return std::hypot(y, x); }, [](double y, double x) { return std::hypot(y, x); });
	f1("pow(float) 0.9^a a in [0,40]", 0.0f, 40.0f, [](float a) { return det::pow(0.9f, a); }, [](float a) { return std::pow(0.9f, a); }, [](double a) { return std::pow((double)0.9f, a); });
	f1("pow(float) a^0.7 a in [1,100]", 1.0f, 100.0f, [](float a) { return det::pow(a, 0.7f); }, [](float a) { return std::pow(a, 0.7f); }, [](double a) { return std::pow(a, (double)0.7f); });
	f1("log(float) [1e-6,1]", 1e-6f, 1.0f, [](float x) { return det::log(x); }, [](float x) { return std::log(x); }, [](double x) { return std::log(x); });
	f1("exp(float) [-10,0]", -10.0f, 0.0f, [](float x) { return det::exp(x); }, [](float x) { return std::exp(x); }, [](double x) { return std::exp(x); });
	f1("acos(float) [-1,1]", -1.0f, 1.0f, [](float x) { return det::acos(x); }, [](float x) { return std::acos(x); }, [](double x) { return std::acos(x); });
	{ // the armour levels of RTS0's damage formula, powf(0.9f, armour) in 0.28's quarter steps
		Hash h; Stats st;
		for (int k = -10; k <= 60; ++k)
			for (int q = 0; q < 4; ++q) {
				const float a = (float)k + 0.25f * (float)q, r = det::pow(0.9f, a);
				h.mix(r);
				if (libm) { volatile float va = a; st.add(r, (float)std::pow((double)0.9f, (double)va), std::pow(0.9f, va)); }
			}
		report("pow(float) 0.9^(k/4) k=-40..243", h, st, 71 * 4);
	}

	// double: the wall builder's geometry, and anything <cmath> would compute in double
	d1("sin(double) [-8,8]", 3, true, [](double x) { return det::sin(x); }, [](double x) { return std::sin(x); }, [](long double x) { return std::sin(x); });
	d1("sin(double) [-1024,1024]", 10, true, [](double x) { return det::sin(x); }, [](double x) { return std::sin(x); }, [](long double x) { return std::sin(x); });
	d1("cos(double) [-8,8]", 3, true, [](double x) { return det::cos(x); }, [](double x) { return std::cos(x); }, [](long double x) { return std::cos(x); });
	d2("atan2(double) [-256,256]^2", 8, true, 8, true, [](double y, double x) { return det::atan2(y, x); }, [](double y, double x) { return std::atan2(y, x); }, [](long double y, long double x) { return std::atan2(y, x); });
	d2("hypot(double) [-256,256]^2", 8, true, 8, true, [](double y, double x) { return det::hypot(y, x); }, [](double y, double x) { return std::hypot(y, x); }, [](long double y, long double x) { return std::hypot(y, x); });
	d1("acos(double) [-1,1]", 0, true, [](double x) { return det::acos(x); }, [](double x) { return std::acos(x); }, [](long double x) { return std::acos(x); });
	d1("log(double) [0,1024]", 10, false, [](double x) { return det::log(x); }, [](double x) { return std::log(x); }, [](long double x) { return std::log(x); });
	d1("exp(double) [-16,16]", 4, true, [](double x) { return det::exp(x); }, [](double x) { return std::exp(x); }, [](long double x) { return std::exp(x); });
	d1("exp(double) [-512,512]", 9, true, [](double x) { return det::exp(x); }, [](double x) { return std::exp(x); }, [](long double x) { return std::exp(x); });
	d2("pow(double) [0,64]^[-8,8]", 6, false, 3, true, [](double a, double b) { return det::pow(a, b); }, [](double a, double b) { return std::pow(a, b); }, [](long double a, long double b) { return std::pow(a, b); });
	{ // negative bases with integer exponents, through the mixed (double, int) overload
		Hash h; Stats st;
		for (int i = 0; i < N; ++i) {
			const double a = ud(4, true);
			const int k = (int)(next() >> 27) - 16; // -16..15
			const double r = det::pow(a, k);
			h.mix(r);
			if (libm) { volatile double va = a; volatile int vk = k; st.add(r, (double)std::pow((long double)va, (long double)vk), std::pow(va, vk)); }
		}
		report("pow(double, int) [-16,16]^(-16..15)", h, st, N);
	}
	{ // integer and mixed arguments take the double kernels, as <cmath> would
		Hash h; Stats st;
		for (int i = 0; i < N; ++i) {
			const int n = (int)(next() >> 20) - 2048; // -2048..2047
			const float f = uf(-300.0f, 300.0f);
			const double r1 = det::sin(n), r2 = det::atan2(f, n), r3 = det::hypot(n, f), r4 = det::pow(0.9f, n >> 5);
			h.mix(r1); h.mix(r2); h.mix(r3); h.mix(r4);
			if (libm) {
				volatile int vn = n; volatile float vf = f;
				st.add(r1, (double)std::sin((long double)vn), std::sin(vn));
				st.add(r2, (double)std::atan2((long double)vf, (long double)vn), std::atan2(vf, vn));
				st.add(r3, (double)std::hypot((long double)vn, (long double)vf), std::hypot(vn, vf));
				st.add(r4, (double)std::pow((long double)0.9f, (long double)(vn >> 5)), std::pow(0.9f, vn >> 5));
			}
		}
		report("sin/atan2/hypot/pow(int, mixed)", h, st, 4l * N);
	}

	special_values();

	std::printf("det hash %08x\n", H);
	if (fails) {
		std::printf("FAIL: %d special value(s) wrong\n", fails);
		return 1;
	}
	if (H != EXPECTED) {
		std::printf("FAIL: expected det hash %08x; det_math.h no longer gives the same bits on this build\n", EXPECTED);
		return 1;
	}
	std::printf("ok: det_math.h gives the expected bits\n");
	return 0;
}
