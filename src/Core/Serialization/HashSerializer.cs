/* Ported from source/simulation2/serialization/HashSerializer.h/.cpp — 0 A.D. GPL-2.0 */
/// <summary>Hash serializer — mirrors CHashSerializer (source/simulation2/serialization/HashSerializer.h/.cpp).</summary>
/// <remarks>Reference: HashSerializer.h, HashSerializer.cpp, BinarySerializer.h — computes MD5 sync hash</remarks>
using System;
using System.IO;
using System.Security.Cryptography;
using System.Text;

namespace Rts0.Core;

public sealed class HashSerializer : ISerializer, IDisposable
{
    private readonly IncrementalHash _hash = IncrementalHash.CreateHash(HashAlgorithmName.MD5);
    private byte[]? _digest;
    public bool IsDebug => false;
    public Stream GetStream() => Stream.Null;

    private void Put(ReadOnlySpan<byte> data) => _hash.AppendData(data);
    private void PutLE(ushort v) => Put(BitConverter.GetBytes(v));
    private void PutLE(uint v) => Put(BitConverter.GetBytes(v));
    private void PutLE(int v) => Put(BitConverter.GetBytes(v));
    private void PutLE(short v) => Put(BitConverter.GetBytes(v));

    private static void Check<T>(T v, T lo, T hi) where T : IComparable<T>
    { if (v.CompareTo(lo) < 0 || v.CompareTo(hi) > 0) throw new InvalidDataException($"OOB {v}"); }

    public void NumberU8(string n, byte v, byte lo, byte hi) { Check(v, lo, hi); Put(new[] { v }); }
    public void NumberI8(string n, sbyte v, sbyte lo, sbyte hi) { Check(v, lo, hi); Put(new[] { (byte)v }); }
    public void NumberU16(string n, ushort v, ushort lo, ushort hi) { Check(v, lo, hi); PutLE(v); }
    public void NumberI16(string n, short v, short lo, short hi) { Check(v, lo, hi); PutLE(v); }
    public void NumberU32(string n, uint v, uint lo, uint hi) { Check(v, lo, hi); PutLE(v); }
    public void NumberI32(string n, int v, int lo, int hi) { Check(v, lo, hi); PutLE(v); }

    public void NumberU8_Unbounded(string n, byte v) => Put(new[] { v });
    public void NumberI8_Unbounded(string n, sbyte v) => Put(new[] { (byte)v });
    public void NumberU16_Unbounded(string n, ushort v) => PutLE(v);
    public void NumberI16_Unbounded(string n, short v) => PutLE(v);
    public void NumberU32_Unbounded(string n, uint v) => PutLE(v);
    public void NumberI32_Unbounded(string n, int v) => PutLE(v);
    public void NumberFloat_Unbounded(string n, float v) => Put(BitConverter.GetBytes(v));
    public void NumberDouble_Unbounded(string n, double v) => Put(BitConverter.GetBytes(v));
    public void NumberFixed_Unbounded(string n, Fixed v) => PutLE(v.RawValue);
    public void Bool(string n, bool v) => Put(new[] { v ? (byte)1 : (byte)0 });
    public void StringASCII(string n, string v, uint min, uint max)
    { if (v.Length < min || v.Length > max) throw new InvalidDataException(); PutLE((uint)v.Length); Put(Encoding.ASCII.GetBytes(v)); }
    public void String(string n, string v, uint min, uint max)
    { var b = Encoding.UTF8.GetBytes(v); if (b.Length < min || b.Length > max) throw new InvalidDataException(); PutLE((uint)b.Length); Put(b); }
    public void RawBytes(string n, byte[] data, int len) => Put(data.AsSpan(0, len));

    public int GetHashLength() => 16;
    public byte[] ComputeHash()
    {
        _digest ??= _hash.GetHashAndReset();
        return (byte[])_digest.Clone();
    }
    public string ComputeHashHex() => BitConverter.ToString(ComputeHash()).Replace("-", "").ToLowerInvariant();
    public void Dispose() => _hash.Dispose();
}
