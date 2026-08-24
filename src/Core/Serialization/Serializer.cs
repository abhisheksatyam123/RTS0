/* Ported from source/simulation2/serialization/ISerializer.h + IDeserializer.h + StdSerializer.h/.cpp — 0 A.D. GPL-2.0 */
/// <summary>Serialization interfaces + StdSerializer — mirrors ISerializer / IDeserializer / CStdSerializer (source/simulation2/serialization/ISerializer.h, IDeserializer.h, StdSerializer.h/.cpp).</summary>
/// <remarks>Reference: ISerializer.h, IDeserializer.h, StdSerializer.h/.cpp, BinarySerializer.h</remarks>
using System;
using System.IO;
using System.Text;

namespace Rts0.Core;

public interface ISerializer
{
    void NumberU8(string name, byte value, byte lower, byte upper);
    void NumberI8(string name, sbyte value, sbyte lower, sbyte upper);
    void NumberU16(string name, ushort value, ushort lower, ushort upper);
    void NumberI16(string name, short value, short lower, short upper);
    void NumberU32(string name, uint value, uint lower, uint upper);
    void NumberI32(string name, int value, int lower, int upper);
    void NumberU8_Unbounded(string name, byte value);
    void NumberI8_Unbounded(string name, sbyte value);
    void NumberU16_Unbounded(string name, ushort value);
    void NumberI16_Unbounded(string name, short value);
    void NumberU32_Unbounded(string name, uint value);
    void NumberI32_Unbounded(string name, int value);
    void NumberFloat_Unbounded(string name, float value);
    void NumberDouble_Unbounded(string name, double value);
    void NumberFixed_Unbounded(string name, Fixed value);
    void Bool(string name, bool value);
    void StringASCII(string name, string value, uint minLen, uint maxLen);
    void String(string name, string value, uint minLen, uint maxLen);
    void RawBytes(string name, byte[] data, int len);
    bool IsDebug { get; }
    Stream GetStream();
}

public interface IDeserializer
{
    void NumberU8(string name, out byte value, byte lower, byte upper);
    void NumberI8(string name, out sbyte value, sbyte lower, sbyte upper);
    void NumberU16(string name, out ushort value, ushort lower, ushort upper);
    void NumberI16(string name, out short value, short lower, short upper);
    void NumberU32(string name, out uint value, uint lower, uint upper);
    void NumberI32(string name, out int value, int lower, int upper);
    void NumberU8_Unbounded(string name, out byte value);
    void NumberI8_Unbounded(string name, out sbyte value);
    void NumberU16_Unbounded(string name, out ushort value);
    void NumberI16_Unbounded(string name, out short value);
    void NumberU32_Unbounded(string name, out uint value);
    void NumberI32_Unbounded(string name, out int value);
    void NumberFloat_Unbounded(string name, out float value);
    void NumberDouble_Unbounded(string name, out double value);
    void NumberFixed_Unbounded(string name, out Fixed value);
    void Bool(string name, out bool value);
    void StringASCII(string name, out string value, uint minLen, uint maxLen);
    void String(string name, out string value, uint minLen, uint maxLen);
    void RawBytes(string name, byte[] data, int len);
    Stream GetStream();
    void RequireBytesInStream(int n);
}

/// <summary>Binary serializer — mirrors CStdSerializer / CBinarySerializer (little-endian).</summary>
public sealed class StdSerializer : ISerializer
{
    private readonly BinaryWriter _w;
    public bool IsDebug => false;
    public StdSerializer(Stream s) => _w = new BinaryWriter(s, Encoding.UTF8, true);
    public Stream GetStream() => _w.BaseStream;

    private static void Check<T>(T v, T lo, T hi) where T : IComparable<T>
    { if (v.CompareTo(lo) < 0 || v.CompareTo(hi) > 0) throw new InvalidDataException($"OutOfBounds {v} not in [{lo},{hi}]"); }

    public void NumberU8(string n, byte v, byte lo, byte hi) { Check(v, lo, hi); _w.Write(v); }
    public void NumberI8(string n, sbyte v, sbyte lo, sbyte hi) { Check(v, lo, hi); _w.Write(v); }
    public void NumberU16(string n, ushort v, ushort lo, ushort hi) { Check(v, lo, hi); _w.Write(v); }
    public void NumberI16(string n, short v, short lo, short hi) { Check(v, lo, hi); _w.Write(v); }
    public void NumberU32(string n, uint v, uint lo, uint hi) { Check(v, lo, hi); _w.Write(v); }
    public void NumberI32(string n, int v, int lo, int hi) { Check(v, lo, hi); _w.Write(v); }

    public void NumberU8_Unbounded(string n, byte v) => _w.Write(v);
    public void NumberI8_Unbounded(string n, sbyte v) => _w.Write(v);
    public void NumberU16_Unbounded(string n, ushort v) => _w.Write(v);
    public void NumberI16_Unbounded(string n, short v) => _w.Write(v);
    public void NumberU32_Unbounded(string n, uint v) => _w.Write(v);
    public void NumberI32_Unbounded(string n, int v) => _w.Write(v);
    public void NumberFloat_Unbounded(string n, float v) => _w.Write(v);
    public void NumberDouble_Unbounded(string n, double v) => _w.Write(v);
    public void NumberFixed_Unbounded(string n, Fixed v) => _w.Write(v.RawValue);
    public void Bool(string n, bool v) => _w.Write(v ? (byte)1 : (byte)0);
    public void StringASCII(string n, string v, uint min, uint max)
    { if (v.Length < min || v.Length > max) throw new InvalidDataException("StringASCII bounds"); _w.Write((uint)v.Length); _w.Write(Encoding.ASCII.GetBytes(v)); }
    public void String(string n, string v, uint min, uint max)
    { if (v.Length < min || v.Length > max) throw new InvalidDataException("String bounds"); var b = Encoding.UTF8.GetBytes(v); _w.Write((uint)b.Length); _w.Write(b); }
    public void RawBytes(string n, byte[] data, int len) => _w.Write(data, 0, len);
}

/// <summary>Binary deserializer — mirrors CStdDeserializer.</summary>
public sealed class StdDeserializer : IDeserializer
{
    private readonly BinaryReader _r;
    public StdDeserializer(Stream s) => _r = new BinaryReader(s, Encoding.UTF8, true);
    public Stream GetStream() => _r.BaseStream;
    public void RequireBytesInStream(int n) { if (_r.BaseStream.Length - _r.BaseStream.Position < n) throw new EndOfStreamException(); }

    private static void Check<T>(T v, T lo, T hi) where T : IComparable<T>
    { if (v.CompareTo(lo) < 0 || v.CompareTo(hi) > 0) throw new InvalidDataException($"OutOfBounds {v}"); }

    public void NumberU8(string n, out byte v, byte lo, byte hi) { v = _r.ReadByte(); Check(v, lo, hi); }
    public void NumberI8(string n, out sbyte v, sbyte lo, sbyte hi) { v = _r.ReadSByte(); Check(v, lo, hi); }
    public void NumberU16(string n, out ushort v, ushort lo, ushort hi) { v = _r.ReadUInt16(); Check(v, lo, hi); }
    public void NumberI16(string n, out short v, short lo, short hi) { v = _r.ReadInt16(); Check(v, lo, hi); }
    public void NumberU32(string n, out uint v, uint lo, uint hi) { v = _r.ReadUInt32(); Check(v, lo, hi); }
    public void NumberI32(string n, out int v, int lo, int hi) { v = _r.ReadInt32(); Check(v, lo, hi); }
    public void NumberU8_Unbounded(string n, out byte v) => v = _r.ReadByte();
    public void NumberI8_Unbounded(string n, out sbyte v) => v = _r.ReadSByte();
    public void NumberU16_Unbounded(string n, out ushort v) => v = _r.ReadUInt16();
    public void NumberI16_Unbounded(string n, out short v) => v = _r.ReadInt16();
    public void NumberU32_Unbounded(string n, out uint v) => v = _r.ReadUInt32();
    public void NumberI32_Unbounded(string n, out int v) => v = _r.ReadInt32();
    public void NumberFloat_Unbounded(string n, out float v) => v = _r.ReadSingle();
    public void NumberDouble_Unbounded(string n, out double v) => v = _r.ReadDouble();
    public void NumberFixed_Unbounded(string n, out Fixed v) => v = Fixed.FromRaw(_r.ReadInt32());
    public void Bool(string n, out bool v) => v = _r.ReadByte() != 0;
    public void StringASCII(string n, out string v, uint min, uint max)
    { uint len = _r.ReadUInt32(); if (len < min || len > max) throw new InvalidDataException(); v = Encoding.ASCII.GetString(_r.ReadBytes((int)len)); }
    public void String(string n, out string v, uint min, uint max)
    { uint len = _r.ReadUInt32(); if (len < min || len > max) throw new InvalidDataException(); v = Encoding.UTF8.GetString(_r.ReadBytes((int)len)); }
    public void RawBytes(string n, byte[] data, int len) { var b = _r.ReadBytes(len); if (b.Length != len) throw new EndOfStreamException(); Array.Copy(b, data, len); }
}
