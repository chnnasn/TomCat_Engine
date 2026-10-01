namespace TomCat;

public sealed partial class UIScrollView
{
    /// <summary>Logical index represented by child pool slot zero after layout clamps Offset.</summary>
    public uint FirstVisibleIndex
    {
        get
        {
            if (!Virtualized) return 0;
            double top = Math.Floor(Math.Max(0, Offset.Y) / Math.Max(1, VirtualItemHeight));
            uint index = (uint)Math.Min(VirtualItemCount, top);
            return index - Math.Min(index, Math.Min(VirtualOverscan, 1024u));
        }
    }
    /// <summary>Bind this logical item to an existing pooled child; do not create one child per data item.</summary>
    public bool TryGetItemIndex(uint poolSlot, out uint itemIndex)
    {
        ulong index = (ulong)FirstVisibleIndex + poolSlot;
        itemIndex = index <= uint.MaxValue ? (uint)index : 0;
        return Virtualized && index < VirtualItemCount;
    }
}
