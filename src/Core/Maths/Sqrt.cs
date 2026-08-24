/* Ported from source/maths/Sqrt.cpp — isqrt64 deterministic. */

namespace Rts0.Core;

internal static class SqrtUtil
{
    public static uint Isqrt64(ulong n)
    {
        ulong op = n;
        ulong res = 0;
        ulong one = 1UL << 62;
        while (one > op) one >>= 2;
        while (one != 0)
        {
            if (op >= res + one)
            {
                op -= res + one;
                res += one << 1;
            }
            res >>= 1;
            one >>= 2;
        }
        return (uint)res;
    }
}
