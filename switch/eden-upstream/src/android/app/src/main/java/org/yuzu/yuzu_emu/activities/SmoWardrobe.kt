package org.yuzu.yuzu_emu.activities

/** UI eligibility only. The guest revalidates ownership, identity and player state. */
object SmoWardrobe {
    const val LEFT = 110f; const val TOP = 708f; const val RIGHT = 740f; const val BOTTOM = 776f
    fun canEquip(state: SmoSnapshot, kind: Int, collectionGeneration: Long?,
                 item: SmoCollectionItem, busy: Boolean): Boolean =
        !busy && item.owned && kind in 0..1 && state.active && state.health > 0 &&
        state.stage.isNotBlank() && collectionGeneration == state.generation &&
        item.name != (if (kind == 0) state.outfit else state.hat)

    fun equipTap(downX: Float, downY: Float, x: Float, y: Float): Boolean =
        downX >= LEFT && downX < RIGHT && downY >= TOP && downY < BOTTOM &&
        x >= LEFT && x < RIGHT && y >= TOP && y < BOTTOM
}
