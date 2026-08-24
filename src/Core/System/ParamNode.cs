/* Ported from source/simulation2/system/ParamNode.h/.cpp — 0 A.D. GPL-2.0 */
/// <summary>XML param tree — mirrors CParamNode (source/simulation2/system/ParamNode.h/.cpp).</summary>
/// <remarks>Reference: source/simulation2/system/ParamNode.h, ParamNode.cpp</remarks>
using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Xml.Linq;

namespace Rts0.Core;

/// <summary>Immutable-style XML param node. Mirrors CParamNode API: GetChild/HasChild, ToFixed etc.</summary>
public sealed class ParamNode
{
    private static readonly ParamNode _null = new(false);
    private readonly SortedDictionary<string, ParamNode> _children = new();
    private string _value = "";
    private readonly bool _isOk;

    public string Name { get; set; } = "";
    public uint Index { get; set; }
    public bool IsOk => _isOk;
    public string Value => _value;
    public IReadOnlyDictionary<string, ParamNode> Children => _children;

    public ParamNode(bool isOk = true) => _isOk = isOk;

    public ParamNode GetChild(string name) => _children.TryGetValue(name, out var c) ? c : _null;
    public bool HasChild(string name) => _children.ContainsKey(name);
    public ParamNode GetOnlyChild() => _children.Count == 1 ? _children.Values.First() : _null;
    public IReadOnlyDictionary<string, ParamNode> GetChildren() => _children;

    // --- conversions (mirrors ToFixed/ToInt/ToFloat/ToBool) ---
    public string ToStringValue() => _value;
    public string ToWString() => _value;
    public int ToInt() => int.TryParse(_value, NumberStyles.Integer, CultureInfo.InvariantCulture, out var v) ? v : 0;
    public float ToFloat() => float.TryParse(_value, NumberStyles.Float, CultureInfo.InvariantCulture, out var v) ? v : 0f;
    public bool ToBool() => _value == "true";
    public Fixed ToFixed()
    {
        if (double.TryParse(_value, NumberStyles.Float, CultureInfo.InvariantCulture, out var d))
            return Fixed.FromDouble(d);
        return Fixed.Zero;
    }
    public override string ToString() => _value;

    // --- XML loading (mirrors ApplyLayer: element name becomes child key) ---
    public static bool LoadXMLString(ParamNode ret, string xml, string? sourceId = null)
    {
        try
        {
            var doc = XDocument.Parse(xml, LoadOptions.PreserveWhitespace);
            if (doc.Root == null) return false;
            // Original stores root name as child of ret (e.g. "Entity")
            string rootName = doc.Root.Name.LocalName;
            if (!ret._children.TryGetValue(rootName, out var rootNode))
            {
                rootNode = new ParamNode(true) { Name = rootName };
                ret._children[rootName] = rootNode;
            }
            rootNode.ApplyElement(doc.Root);
            return true;
        }
        catch { return false; }
    }

    public void ApplyElement(XElement el)
    {
        _value = el.HasElements ? "" : (el.Value ?? "").Trim();
        foreach (var attr in el.Attributes())
            _children["@" + attr.Name.LocalName] = new ParamNode(true) { _value = attr.Value, Name = "@" + attr.Name.LocalName };
        foreach (var child in el.Elements())
        {
            string name = child.Name.LocalName;
            if (!_children.TryGetValue(name, out var node))
            {
                node = new ParamNode(true) { Name = name };
                _children[name] = node;
            }
            node.ApplyElement(child);
        }
    }

    // --- serialization helpers ---
    public static string EscapeXMLString(string s) => s
        .Replace("&", "&amp;").Replace("<", "&lt;").Replace(">", "&gt;")
        .Replace("\"", "&quot;").Replace("\t", "&#9;").Replace("\n", "&#10;").Replace("\r", "&#13;");

    public string ToXMLString()
    {
        using var sw = new StringWriter();
        ToXMLString(sw);
        return sw.ToString();
    }
    public void ToXMLString(TextWriter w)
    {
        if (_value.Length > 0) w.Write(EscapeXMLString(_value));
        foreach (var kv in _children)
        {
            if (kv.Key.StartsWith("@")) continue;
            w.Write($"<{kv.Key}");
            foreach (var a in kv.Value._children.Where(c => c.Key.StartsWith("@")))
                w.Write($" {a.Key.Substring(1)}=\"{EscapeXMLString(a.Value._value)}\"");
            w.Write(">");
            kv.Value.ToXMLString(w);
            w.Write($"</{kv.Key}>");
        }
    }
}
