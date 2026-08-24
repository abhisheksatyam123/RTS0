/// Ported from source/maths/MD5.h/.cpp — 0 A.D. GPL-2.0 — MD5 hash wrapper.
using System;
using System.Security.Cryptography;
using System.Text;

namespace Rts0.Core;

/// <summary>MD5 hash. Port of 0 A.D. <c>MD5</c> (source/maths/MD5.h/.cpp). Uses <see cref="System.Security.Cryptography.MD5"/> internally; keeps Update/Final/GetDigest API.</summary>
public sealed class MD5 : IDisposable
{
    public const int DigestSize = 16;
    private readonly IncrementalHash _hash = IncrementalHash.CreateHash(HashAlgorithmName.MD5);
    private byte[]? _digest;
    private bool _finalized;

    public void Update(byte[] data, int len) => Update(data.AsSpan(0, len));
    public void Update(ReadOnlySpan<byte> data) { if (_finalized) throw new InvalidOperationException("Already finalized"); _hash.AppendData(data); }
    public void Update(string text) => Update(Encoding.UTF8.GetBytes(text));
    public void Update(byte b) => Update(new ReadOnlySpan<byte>(new[] { b }));

    /// <summary>Finalize and return 16-byte digest. See MD5.cpp Final.</summary>
    public byte[] Final()
    {
        _digest = _hash.GetHashAndReset();
        _finalized = true;
        return (byte[])_digest.Clone();
    }

    public void Final(Span<byte> digest)
    {
        var d = Final();
        d.CopyTo(digest);
    }

    public byte[] GetDigest() => _digest != null ? (byte[])_digest.Clone() : _hash.GetCurrentHash();
    public string GetDigestHex() => BitConverter.ToString(GetDigest()).Replace("-", "").ToLowerInvariant();

    public static byte[] Compute(byte[] data) => global::System.Security.Cryptography.MD5.HashData(data);
    public static byte[] Compute(string text) => Compute(Encoding.UTF8.GetBytes(text));
    public static string ComputeHex(byte[] data) => BitConverter.ToString(Compute(data)).Replace("-", "").ToLowerInvariant();

    public void Reset() { _hash.TryGetHashAndReset(Span<byte>.Empty, out _); _digest = null; _finalized = false; }
    public void Dispose() => _hash.Dispose();
}
