using Godot;

public partial class SelectionRect : Control
{
    public bool Active { get; set; }
    public Rect2 Rect { get; set; }

    public override void _Draw()
    {
        if (!Active) return;
        DrawRect(Rect, new Color(0.3f, 0.9f, 1.0f, 0.15f), true);
        DrawRect(Rect, new Color(0.3f, 0.9f, 1.0f, 0.9f), false, 2.0f);
    }

    public void SetRect(Rect2 rect, bool active)
    {
        Rect = rect;
        Active = active;
        QueueRedraw();
    }
}
