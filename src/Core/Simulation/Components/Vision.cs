/* Ported from source/simulation2/components/CCmpVision.cpp + ICmpVision.h — 0 A.D. GPL-2.0 */
/// <summary>Vision range per entity — mirrors ICmpVision / CCmpVision.</summary>
using System;
using System.Collections.Generic;

namespace Rts0.Core;

public sealed class Vision : IComponent
{
    public int EntityId { get; private set; }
    private Fixed _range, _baseRange;
    private bool _revealShore;

    public event Action<int, Fixed, Fixed> VisionRangeChanged; // entity, old, new

    public Vision(int entityId, Fixed range, bool revealShore = false)
    {
        EntityId = entityId;
        _baseRange = _range = range;
        _revealShore = revealShore;
    }

    public void Init(Dictionary<string, object> p)
    {
        if (p != null && p.TryGetValue("Range", out var r))
            _baseRange = _range = Fixed.FromFloat(Convert.ToSingle(r));
        if (p != null && p.TryGetValue("RevealShore", out var rs))
            _revealShore = Convert.ToBoolean(rs);
    }
    public void Deinit() { }
    public void Serialize(System.IO.BinaryWriter w) { w.Write(_range.RawValue); w.Write(_baseRange.RawValue); w.Write(_revealShore); }
    public void Deserialize(System.IO.BinaryReader r) { _range = Fixed.FromRaw(r.ReadInt32()); _baseRange = Fixed.FromRaw(r.ReadInt32()); _revealShore = r.ReadBoolean(); }

    public Fixed GetRange() => _range;
    public bool GetRevealShore() => _revealShore;

    public void SetRange(Fixed range)
    {
        if (range == _range) return;
        Fixed old = _range;
        _range = range;
        VisionRangeChanged?.Invoke(EntityId, old, range);
    }

    public void ReloadRange(Func<string, Fixed, int, Fixed> applyMods)
    {
        if (applyMods == null) return;
        Fixed nv = applyMods("Vision/Range", _baseRange, EntityId);
        if (nv != _range) SetRange(nv);
    }

    // LOS helper — reveal in RangeManager/ LOS grid
    public void Reveal(Grid<uint> losGrid, FixedVector2D pos, int verticesPerSide, int player)
    {
        if (losGrid == null || _range.IsZero()) return;
        uint mask = LosQuerier.MaskForPlayer(player);
        int cx = (pos.X / Los.TileSize).ToInt_RoundToZero();
        int cy = (pos.Y / Los.TileSize).ToInt_RoundToZero();
        int radius = (_range / Los.TileSize).ToInt_RoundToZero();
        LosHelper.RevealCircle(losGrid, cx, cy, radius, mask, true);
    }

    public static Vision Default(float range = 12f) => new(0, Fixed.FromFloat(range));
}
