// Deterministic 16.16 fixed-point math.
//
// This file is a PORT of 0 A.D. code, licensed GPL-2.0-or-later
// (Copyright (C) Wildfire Games). Vendor reference, read-only, in src/0ad/maths/:
//   Fixed.h:  round_away_from_zero            lines 109-119
//             CFixed storage / Zero / Epsilon  lines 135-140
//             FromInt / FromFraction           lines 148-157
//             FromFloat / FromDouble           lines 159-173
//             ToFloat / ToDouble               lines 179-188
//             ToInt_Round*                     lines 190-211
//             comparisons / IsZero             lines 217-235
//             + - += -= unary -                lines 240-264
//             / (fixed) / * (int) / MultiplyClamp / / (int) / %  lines 280-321
//             Absolute                         line 323
//             Multiply / Square / MulDiv       lines 329-354
//             Sqrt                             lines 356-362
//   Fixed.cpp: FromString                      lines 27-88
//             atan2_approx                     lines 148-181
//             Pi                               lines 183-187
//             sincos_approx                    lines 189-238
//   Sqrt.cpp:  isqrt64                         lines 51-72
//
// Semantics match the vendor release build (NDEBUG): integer arithmetic wraps
// two's-complement, Multiply/MulDiv truncate the 64-bit result to 32 bits, and
// no debug_warn overflow checks are performed. Division by zero is undefined,
// as in the vendor code. Only standard headers are used.

#ifndef RTSO_CORE_MATH_FIXED_H
#define RTSO_CORE_MATH_FIXED_H

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace rtso {

namespace detail {

// Wrapping 32-bit arithmetic (vendor relies on two's-complement wraparound).
constexpr int32_t wrap_add(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b)); }
constexpr int32_t wrap_sub(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b)); }
constexpr int32_t wrap_mul(int32_t a, int32_t b) { return static_cast<int32_t>(static_cast<uint32_t>(a) * static_cast<uint32_t>(b)); }
constexpr int32_t wrap_neg(int32_t a) { return static_cast<int32_t>(0u - static_cast<uint32_t>(a)); }

// Round half away from zero, then truncate (vendor round_away_from_zero<T>).
inline int32_t round_away_from_zero(float value) { return static_cast<int32_t>(value >= 0 ? value + 0.5f : value - 0.5f); }
inline int32_t round_away_from_zero(double value) { return static_cast<int32_t>(value >= 0 ? value + 0.5 : value - 0.5); }

// 64-bit integer square root: returns r with r^2 <= n < (r+1)^2 (vendor isqrt64, Sqrt.cpp:52-72).
constexpr uint32_t isqrt64(uint64_t n)
{
	uint64_t op = n;
	uint64_t res = 0;
	uint64_t one = static_cast<uint64_t>(1) << 62; // highest power of four <= the argument

	while (one > op)
		one >>= 2;

	while (one != 0)
	{
		if (op >= res + one)
		{
			op -= (res + one);
			res += (one << 1);
		}
		res >>= 1;
		one >>= 2;
	}
	return static_cast<uint32_t>(res);
}

} // namespace detail

/**
 * Signed 16.16 fixed-point number (1 sign bit, 15 integer bits, 16 fraction bits).
 * Port of 0 A.D. CFixed_15_16 / fixed.
 */
class Fixed
{
public:
	static constexpr int fract_bits = 16;
	static constexpr int32_t fract_pow2 = 65536;

	constexpr Fixed() : value_(0) { }

	static constexpr Fixed Zero() { return Fixed(0); }
	static constexpr Fixed Epsilon() { return Fixed(1); }
	static constexpr Fixed Pi() { return Fixed(205887); } // pi << 16

	constexpr int32_t GetInternalValue() const { return value_; }
	static constexpr Fixed FromInternalValue(int32_t v) { return Fixed(v); }

	// Conversion to/from primitive types.

	static constexpr Fixed FromInt(int n)
	{
		return Fixed(static_cast<int32_t>(static_cast<uint32_t>(n) << fract_bits));
	}

	// Vendor: (int)((unsigned)n << 16) / d.
	static constexpr Fixed FromFraction(int n, int d)
	{
		return Fixed(static_cast<int32_t>(static_cast<uint32_t>(n) << fract_bits) / d);
	}

	static Fixed FromFloat(float n)
	{
		if (!std::isfinite(n))
			return Fixed(0);
		float scaled = n * static_cast<float>(fract_pow2);
		return Fixed(detail::round_away_from_zero(scaled));
	}

	static Fixed FromDouble(double n)
	{
		if (!std::isfinite(n))
			return Fixed(0);
		double scaled = n * static_cast<double>(fract_pow2);
		return Fixed(detail::round_away_from_zero(scaled));
	}

	/// Parses [-+]?\d*(\.\d*)?  (superset of xsd:decimal). Invalid trailing input is ignored.
	static Fixed FromString(std::string_view s)
	{
		if (s.empty())
			return Zero();

		// Reads a char, returning NUL past the end, as the vendor C-string parser does.
		auto at = [&s](size_t i) -> char { return i < s.size() ? s[i] : '\0'; };

		bool neg = false;
		Fixed r;
		size_t c = 0;

		if (at(c) == '+')
		{
			++c;
		}
		else if (at(c) == '-')
		{
			++c;
			neg = true;
		}

		while (true)
		{
			// Integer part:
			if (at(c) >= '0' && at(c) <= '9')
			{
				r = Fixed(detail::wrap_mul(r.value_, 10));
				r += FromInt(at(c) - '0');
				++c;
			}
			else if (at(c) == '.')
			{
				++c;
				uint32_t frac = 0;
				uint32_t div = 1;
				// Fractional part:
				while (at(c) >= '0' && at(c) <= '9')
				{
					frac *= 10;
					frac += static_cast<uint32_t>(at(c) - '0');
					div *= 10;
					++c;
					if (div >= 100000)
						break; // further digits are too small to matter
				}
				r += Fixed(static_cast<int32_t>((static_cast<uint64_t>(frac) << fract_bits) / div));
				break;
			}
			else
			{
				// Invalid character or end of string.
				break;
			}
		}

		return (neg ? -r : r);
	}

	/// Convert to float. May be lossy.
	float ToFloat() const
	{
		return static_cast<float>(value_) / static_cast<float>(fract_pow2);
	}

	/// Convert to double. Exact.
	double ToDouble() const
	{
		return value_ / static_cast<double>(fract_pow2);
	}

	constexpr int ToInt_RoundToZero() const
	{
		if (value_ > 0)
			return value_ >> fract_bits;
		return detail::wrap_add(value_, fract_pow2 - 1) >> fract_bits;
	}

	constexpr int ToInt_RoundToInfinity() const
	{
		return detail::wrap_add(value_, fract_pow2 - 1) >> fract_bits;
	}

	constexpr int ToInt_RoundToNegInfinity() const
	{
		return value_ >> fract_bits;
	}

	constexpr int ToInt_RoundToNearest() const // ties to infinity
	{
		return detail::wrap_add(value_, fract_pow2 / 2) >> fract_bits;
	}

	/// True if the number is exactly 0.
	constexpr bool IsZero() const { return value_ == 0; }

	// Comparisons.

	constexpr bool operator==(Fixed n) const { return value_ == n.value_; }
	constexpr bool operator!=(Fixed n) const { return value_ != n.value_; }
	constexpr bool operator<=(Fixed n) const { return value_ <= n.value_; }
	constexpr bool operator<(Fixed n) const { return value_ < n.value_; }
	constexpr bool operator>=(Fixed n) const { return value_ >= n.value_; }
	constexpr bool operator>(Fixed n) const { return value_ > n.value_; }

	// Basic arithmetic (wraps on overflow, as the vendor release build does).

	constexpr Fixed operator+(Fixed n) const { return Fixed(detail::wrap_add(value_, n.value_)); }
	constexpr Fixed operator-(Fixed n) const { return Fixed(detail::wrap_sub(value_, n.value_)); }
	constexpr Fixed& operator+=(Fixed n) { *this = *this + n; return *this; }
	constexpr Fixed& operator-=(Fixed n) { *this = *this - n; return *this; }
	constexpr Fixed operator-() const { return Fixed(detail::wrap_neg(value_)); }

	constexpr Fixed operator>>(int n) const { return Fixed(value_ >> n); }
	constexpr Fixed operator<<(int n) const { return Fixed(static_cast<int32_t>(static_cast<uint32_t>(value_) << n)); }

	/// Divide by a Fixed. n must not be zero.
	constexpr Fixed operator/(Fixed n) const
	{
		int64_t t = static_cast<int64_t>(value_) << fract_bits;
		int64_t result = t / static_cast<int64_t>(n.value_);
		return Fixed(static_cast<int32_t>(result));
	}

	/// Multiply by an integer.
	constexpr Fixed operator*(int n) const { return Fixed(detail::wrap_mul(value_, n)); }

	/// Multiply by an integer, clamping to the representable range instead of wrapping.
	constexpr Fixed MultiplyClamp(int n) const
	{
		int64_t t = static_cast<int64_t>(value_) * n;
		t = std::max(static_cast<int64_t>(INT32_MIN), std::min(static_cast<int64_t>(INT32_MAX), t));
		return Fixed(static_cast<int32_t>(t));
	}

	/// Divide by an integer. n must not be zero.
	constexpr Fixed operator/(int n) const
	{
		if (n == -1)
			return -(*this); // avoids INT32_MIN / -1
		return Fixed(value_ / n);
	}

	/// Mod by a Fixed. n must not be zero. Result has the same sign as n.
	constexpr Fixed operator%(Fixed n) const
	{
		int32_t t = (n.value_ == -1) ? 0 : value_ % n.value_;
		if (n.value_ > 0 && t < 0)
			t += n.value_;
		else if (n.value_ < 0 && t > 0)
			t += n.value_;
		return Fixed(t);
	}

	constexpr Fixed Absolute() const { return Fixed(value_ < 0 ? detail::wrap_neg(value_) : value_); }

	/// Multiply by a Fixed. The 64-bit product is shifted right by 16 and truncated to 32 bits.
	constexpr Fixed Multiply(Fixed n) const
	{
		int64_t t = static_cast<int64_t>(value_) * static_cast<int64_t>(n.value_);
		t >>= fract_bits;
		return Fixed(static_cast<int32_t>(t));
	}

	/// Square of this value.
	constexpr Fixed Square() const { return Multiply(*this); }

	/// Compute this * m / d. d must not be zero.
	constexpr Fixed MulDiv(Fixed m, Fixed d) const
	{
		int64_t t = (static_cast<int64_t>(value_) * static_cast<int64_t>(m.value_)) / static_cast<int64_t>(d.value_);
		return Fixed(static_cast<int32_t>(t));
	}

	/// Square root. Result is floor(sqrt(this) * 2^16) in internal units; <= 0 gives 0.
	constexpr Fixed Sqrt() const
	{
		if (value_ <= 0)
			return Fixed(0);
		uint32_t s = detail::isqrt64(static_cast<uint64_t>(value_) << fract_bits);
		return Fixed(static_cast<int32_t>(s));
	}

	// Prevent accidental implicit float/double operands (the vendor leaves these undefined).
	Fixed operator*(float) const = delete;
	Fixed operator*(double) const = delete;
	Fixed operator/(float) const = delete;
	Fixed operator/(double) const = delete;

private:
	constexpr explicit Fixed(int32_t v) : value_(v) { }

	int32_t value_;
};

namespace fixed {

/**
 * Approximate atan2 over Fixed. Max error is about 0.08 rad (4.5 degrees).
 * Vendor: Fixed.cpp:148-181 (after dspguru.com fixed-point atan2).
 */
inline Fixed atan2_approx(Fixed y, Fixed x)
{
	Fixed zero;

	// Avoid division by zero.
	if (x.IsZero() && y.IsZero())
		return zero;

	Fixed c1 = Fixed::FromInternalValue(51472); // pi/4 << 16
	Fixed c2 = Fixed::FromInternalValue(154415); // 3*pi/4 << 16

	Fixed abs_y = y.Absolute();

	Fixed angle;
	if (x >= zero)
	{
		Fixed r = (x - abs_y) / (x + abs_y);
		angle = c1 - c1.Multiply(r);
	}
	else
	{
		Fixed r = (x + abs_y) / (abs_y - x);
		angle = c2 - c1.Multiply(r);
	}

	if (y < zero)
		return -angle;
	return angle;
}

/**
 * Compute sin(a) and cos(a). Max error for -2pi < a < 2pi is about 0.0005.
 * Vendor: Fixed.cpp:189-238 (after coranac.com sines, fifth-order variant).
 */
inline void sincos_approx(Fixed a, Fixed& sin_out, Fixed& cos_out)
{
	Fixed c2_pi = Fixed::FromInternalValue(41721); // 2/pi << 16

	// Map radians onto [0, 4).
	Fixed z = a.Multiply(c2_pi) % Fixed::FromInt(4);

	// Map z onto [-1, +1] for sin; z+1 gives cos.
	Fixed sz, cz;
	if (z >= Fixed::FromInt(3)) // [3, 4)
	{
		sz = z - Fixed::FromInt(4);
		cz = z - Fixed::FromInt(3);
	}
	else if (z >= Fixed::FromInt(2)) // [2, 3)
	{
		sz = Fixed::FromInt(2) - z;
		cz = z - Fixed::FromInt(3);
	}
	else if (z >= Fixed::FromInt(1)) // [1, 2)
	{
		sz = Fixed::FromInt(2) - z;
		cz = Fixed::FromInt(1) - z;
	}
	else // [0, 1)
	{
		sz = z;
		cz = Fixed::FromInt(1) - z;
	}

	// Fifth-order polynomial (max abs error ~0.0005).
	Fixed sz2 = sz.Multiply(sz);
	sin_out = sz.Multiply(Fixed::Pi() - sz2.Multiply(Fixed::Pi() * 2 - Fixed::FromInt(5) - sz2.Multiply(Fixed::Pi() - Fixed::FromInt(3)))) / 2;

	Fixed cz2 = cz.Multiply(cz);
	cos_out = cz.Multiply(Fixed::Pi() - cz2.Multiply(Fixed::Pi() * 2 - Fixed::FromInt(5) - cz2.Multiply(Fixed::Pi() - Fixed::FromInt(3)))) / 2;
}

} // namespace fixed
using CFixed_15_16 = Fixed;
inline CFixed_15_16 ToCFixed1516(int32_t value) { return Fixed::FromInt(value); }
inline CFixed_15_16 ToCFixed1516(double value) { return Fixed::FromDouble(value); }
inline int32_t CFixed1516ToRaw(CFixed_15_16 v) { return v.GetInternalValue(); }
inline CFixed_15_16 CFixed1516FromRaw(int32_t raw) { return Fixed::FromInternalValue(raw); }


} // namespace rtso

#endif // RTSO_CORE_MATH_FIXED_H
