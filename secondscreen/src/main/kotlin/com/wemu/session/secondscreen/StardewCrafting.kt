package com.wemu.session.secondscreen

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.alpha
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.FilterQuality
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.selected
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.thor.core.designsystem.theme.ThorTheme

/**
 * Reference-style crafting destination backed only by data and art sent by the game.
 *
 * The Stardew bridge supplies the game's recipe key, description, ingredient counts, output art,
 * and readiness. [onCraftRecipe] remains optional so previews can render without a connection.
 */
@Composable
fun StardewCraftingReferencePage(
    state: StardewState,
    modifier: Modifier = Modifier,
    onCraftRecipe: ((StardewRecipe) -> Unit)? = null,
) {
    var selectedCategoryName by rememberSaveable { mutableStateOf(CraftCategory.ALL.name) }
    var selectedRecipeKey by rememberSaveable { mutableStateOf("") }

    val recipesByCategory = remember(state.recipes) {
        state.recipes.groupBy(::categoryFor)
    }
    val categories = remember(recipesByCategory) {
        buildList {
            add(CraftCategory.ALL)
            CraftCategory.entries
                .filter { it != CraftCategory.ALL && !recipesByCategory[it].isNullOrEmpty() }
                .forEach(::add)
        }
    }
    val selectedCategory = categories.firstOrNull { it.name == selectedCategoryName }
        ?: CraftCategory.ALL
    val visibleRecipes = remember(state.recipes, selectedCategory) {
        if (selectedCategory == CraftCategory.ALL) state.recipes
        else state.recipes.filter { categoryFor(it) == selectedCategory }
    }
    val selectedRecipe = visibleRecipes.firstOrNull { it.selectionKey == selectedRecipeKey }
        ?: visibleRecipes.firstOrNull()

    LaunchedEffect(categories, selectedCategoryName) {
        if (categories.none { it.name == selectedCategoryName }) {
            selectedCategoryName = CraftCategory.ALL.name
        }
    }
    LaunchedEffect(visibleRecipes, selectedRecipeKey) {
        if (visibleRecipes.none { it.selectionKey == selectedRecipeKey }) {
            selectedRecipeKey = visibleRecipes.firstOrNull()?.selectionKey.orEmpty()
        }
    }

    BoxWithConstraints(modifier = modifier.fillMaxSize()) {
        val compact = maxWidth < 700.dp || maxHeight < 520.dp
        val categoryHeight = if (compact) 52.dp else 62.dp
        val carouselHeight = if (compact) 86.dp else 108.dp
        val listWidth = maxWidth * if (compact) 0.37f else 0.30f

        Column(
            modifier = Modifier
                .fillMaxSize()
                .background(CRAFT_PARCHMENT.copy(alpha = 0.30f)),
        ) {
            CraftCategoryStrip(
                state = state,
                categories = categories,
                selected = selectedCategory,
                recipesByCategory = recipesByCategory,
                modifier = Modifier.fillMaxWidth().height(categoryHeight),
                compact = compact,
                onSelect = { category ->
                    selectedCategoryName = category.name
                    selectedRecipeKey = ""
                },
            )
            CraftRule()

            if (state.recipes.isEmpty()) {
                EmptyCraftingPage(
                    state = state,
                    modifier = Modifier.fillMaxWidth().weight(1f),
                )
            } else {
                Row(modifier = Modifier.fillMaxWidth().weight(1f)) {
                    CraftRecipeList(
                        state = state,
                        recipes = visibleRecipes,
                        selectedKey = selectedRecipe?.selectionKey.orEmpty(),
                        modifier = Modifier.width(listWidth).fillMaxHeight(),
                        compact = compact,
                        onSelect = { selectedRecipeKey = it.selectionKey },
                    )
                    CraftVerticalRule()
                    selectedRecipe?.let { recipe ->
                        CraftRecipeDetail(
                            state = state,
                            recipe = recipe,
                            modifier = Modifier.weight(1f).fillMaxHeight(),
                            compact = compact,
                            onCraftRecipe = onCraftRecipe,
                        )
                    }
                }
            }

            CraftRule()
            CraftTimeCarousel(
                state = state,
                recipes = visibleRecipes,
                selectedKey = selectedRecipe?.selectionKey.orEmpty(),
                modifier = Modifier.fillMaxWidth().height(carouselHeight),
                compact = compact,
                onSelect = { selectedRecipeKey = it.selectionKey },
            )
        }
    }
}

@Composable
private fun CraftCategoryStrip(
    state: StardewState,
    categories: List<CraftCategory>,
    selected: CraftCategory,
    recipesByCategory: Map<CraftCategory, List<StardewRecipe>>,
    modifier: Modifier,
    compact: Boolean,
    onSelect: (CraftCategory) -> Unit,
) {
    LazyRow(
        modifier = modifier.padding(vertical = 4.dp),
        contentPadding = PaddingValues(horizontal = 5.dp),
        horizontalArrangement = Arrangement.spacedBy(4.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        items(categories, key = { it.name }) { category ->
            val categoryRecipes = recipesByCategory[category].orEmpty()
            val icon = when (category) {
                CraftCategory.ALL -> state.sprites[StardewUi.TAB_CRAFTING]
                else -> categoryRecipes.firstNotNullOfOrNull { state.sprites[it.id] }
            }
            CraftCategoryButton(
                state = state,
                category = category,
                count = if (category == CraftCategory.ALL) state.recipes.size else categoryRecipes.size,
                sprite = icon,
                selected = category == selected,
                compact = compact,
                onClick = { onSelect(category) },
            )
        }
    }
}

@Composable
private fun CraftCategoryButton(
    state: StardewState,
    category: CraftCategory,
    count: Int,
    sprite: UiSprite?,
    selected: Boolean,
    compact: Boolean,
    onClick: () -> Unit,
) {
    val frame = state.frame(if (selected) StardewUi.SLOT_SELECTED else StardewUi.BUTTON)
    val width = if (compact) 74.dp else 88.dp
    val base = Modifier.width(width).fillMaxHeight()
    val painted = if (frame != null) {
        base.drawBehind { drawNineSlice(frame, size) }
    } else {
        base
            .clip(ThorTheme.shapes.small)
            .background(if (selected) CRAFT_SELECTED else CRAFT_BUTTON)
            .border(
                width = if (selected) 2.dp else 1.dp,
                color = if (selected) CRAFT_GOLD else CRAFT_BORDER,
                shape = ThorTheme.shapes.small,
            )
    }

    Column(
        modifier = painted
            .semantics {
                this.selected = selected
                contentDescription = "${category.title}, $count recipes"
            }
            .clickable(role = Role.Tab, onClick = onClick)
            .padding(horizontal = 5.dp, vertical = 3.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        CraftSprite(
            sprite = sprite,
            description = null,
            modifier = Modifier.size(if (compact) 23.dp else 28.dp),
        )
        Text(
            text = category.title,
            style = MaterialTheme.typography.labelSmall,
            fontSize = if (compact) 9.sp else 10.sp,
            lineHeight = 10.sp,
            fontWeight = if (selected) FontWeight.Bold else FontWeight.Medium,
            color = CRAFT_INK,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}

@Composable
private fun CraftRecipeList(
    state: StardewState,
    recipes: List<StardewRecipe>,
    selectedKey: String,
    modifier: Modifier,
    compact: Boolean,
    onSelect: (StardewRecipe) -> Unit,
) {
    Column(modifier = modifier.padding(horizontal = 5.dp, vertical = 4.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 5.dp, vertical = 3.dp),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = "Recipes",
                style = MaterialTheme.typography.titleSmall,
                fontWeight = FontWeight.Bold,
                color = CRAFT_INK,
                modifier = Modifier.weight(1f),
            )
            Text(
                text = "${recipes.count { it.ready }}/${recipes.size} ready",
                style = MaterialTheme.typography.labelSmall,
                color = CRAFT_GREEN,
            )
        }
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(bottom = 5.dp),
            verticalArrangement = Arrangement.spacedBy(3.dp),
        ) {
            items(recipes, key = { it.selectionKey }) { recipe ->
                CraftRecipeRow(
                    state = state,
                    recipe = recipe,
                    selected = recipe.selectionKey == selectedKey,
                    compact = compact,
                    onClick = { onSelect(recipe) },
                )
            }
        }
    }
}

@Composable
private fun CraftRecipeRow(
    state: StardewState,
    recipe: StardewRecipe,
    selected: Boolean,
    compact: Boolean,
    onClick: () -> Unit,
) {
    val frame = state.frame(if (selected) StardewUi.SLOT_SELECTED else StardewUi.BUTTON)
    val base = Modifier.fillMaxWidth().heightIn(min = if (compact) 48.dp else 58.dp)
    val painted = if (frame != null) {
        base.drawBehind { drawNineSlice(frame, size) }
    } else {
        base
            .clip(ThorTheme.shapes.small)
            .background(if (selected) CRAFT_SELECTED else CRAFT_ROW)
            .border(
                if (selected) 2.dp else 1.dp,
                if (selected) CRAFT_GOLD else CRAFT_BORDER,
                ThorTheme.shapes.small,
            )
    }

    Row(
        modifier = painted
            .semantics { this.selected = selected }
            .clickable(role = Role.Button, onClick = onClick)
            .padding(horizontal = if (compact) 6.dp else 9.dp, vertical = 5.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        CraftSprite(
            sprite = state.sprites[recipe.id],
            description = null,
            modifier = Modifier.size(if (compact) 34.dp else 42.dp),
        )
        Column(modifier = Modifier.weight(1f)) {
            Text(
                text = recipe.name,
                style = MaterialTheme.typography.bodyMedium,
                fontWeight = if (selected) FontWeight.Bold else FontWeight.Medium,
                color = CRAFT_INK,
                maxLines = 2,
                overflow = TextOverflow.Ellipsis,
            )
            Text(
                text = if (recipe.ready) "Ready" else "Missing materials",
                style = MaterialTheme.typography.labelSmall,
                color = if (recipe.ready) CRAFT_GREEN else CRAFT_MISSING,
                maxLines = 1,
            )
        }
    }
}

@Composable
private fun CraftRecipeDetail(
    state: StardewState,
    recipe: StardewRecipe,
    modifier: Modifier,
    compact: Boolean,
    onCraftRecipe: ((StardewRecipe) -> Unit)?,
) {
    val category = categoryFor(recipe)
    Column(modifier = modifier.padding(if (compact) 6.dp else 14.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.Top,
            horizontalArrangement = Arrangement.spacedBy(if (compact) 8.dp else 16.dp),
        ) {
            CraftLargeIcon(
                state = state,
                recipe = recipe,
                size = if (compact) 56.dp else 94.dp,
            )
            Column(modifier = Modifier.weight(1f).padding(top = 2.dp)) {
                Text(
                    text = recipe.name,
                    style = if (compact) {
                        MaterialTheme.typography.titleMedium
                    } else {
                        MaterialTheme.typography.headlineSmall
                    },
                    fontWeight = FontWeight.Bold,
                    color = CRAFT_INK,
                    maxLines = if (compact) 1 else 2,
                    overflow = TextOverflow.Ellipsis,
                )
                Text(
                    text = recipe.description.ifEmpty { category.description },
                    style = if (compact) {
                        MaterialTheme.typography.bodySmall
                    } else {
                        MaterialTheme.typography.bodyMedium
                    },
                    color = CRAFT_INK,
                    maxLines = if (compact) 2 else 3,
                    overflow = TextOverflow.Ellipsis,
                    modifier = Modifier.padding(top = if (compact) 2.dp else 5.dp),
                )
                if (!compact) {
                    Text(
                        text = buildList {
                            add("${category.title} recipe")
                            if (recipe.outputCount > 1) add("Makes ${recipe.outputCount}")
                            if (recipe.crafted > 0) add("Crafted ${recipe.crafted}")
                        }.joinToString("  |  "),
                        style = MaterialTheme.typography.labelSmall,
                        fontWeight = FontWeight.SemiBold,
                        color = CRAFT_MUTED,
                        modifier = Modifier.padding(top = 4.dp),
                    )
                }
            }
        }

        Spacer(Modifier.height(if (compact) 3.dp else 12.dp))
        CraftIngredientsPanel(
            state = state,
            recipe = recipe,
            modifier = Modifier.fillMaxWidth().weight(1f),
            compact = compact,
        )
        Spacer(Modifier.height(if (compact) 3.dp else 10.dp))

        CraftActionButton(
            state = state,
            enabled = onCraftRecipe != null && recipe.ready && recipe.key.isNotEmpty(),
            modifier = Modifier.fillMaxWidth(),
            compact = compact,
            onClick = { onCraftRecipe?.invoke(recipe) },
        )
        if ((onCraftRecipe == null || recipe.key.isEmpty()) && !compact) {
            Text(
                text = "Craft action unavailable on the current connection",
                style = MaterialTheme.typography.labelSmall,
                color = CRAFT_MUTED,
                textAlign = TextAlign.Center,
                modifier = Modifier.fillMaxWidth().padding(top = 3.dp),
            )
        }
    }
}

@Composable
private fun CraftLargeIcon(
    state: StardewState,
    recipe: StardewRecipe,
    size: Dp,
) {
    val frame = state.frame(StardewUi.SLOT)
    val base = Modifier.size(size)
    Box(
        modifier = if (frame != null) {
            base.drawBehind { drawNineSlice(frame, this.size) }
        } else {
            base
                .clip(ThorTheme.shapes.small)
                .background(CRAFT_SLOT)
                .border(1.dp, CRAFT_BORDER, ThorTheme.shapes.small)
        },
        contentAlignment = Alignment.Center,
    ) {
        CraftSprite(
            sprite = state.sprites[recipe.id],
            description = recipe.name,
            modifier = Modifier.fillMaxSize().padding(11.dp),
            fallbackText = recipe.name.take(1),
        )
    }
}

@Composable
private fun CraftIngredientsPanel(
    state: StardewState,
    recipe: StardewRecipe,
    modifier: Modifier,
    compact: Boolean,
) {
    val base = modifier.heightIn(min = if (compact) 56.dp else 100.dp)
    Column(
        modifier = base.padding(
            horizontal = if (compact) 8.dp else 16.dp,
            vertical = if (compact) 5.dp else 9.dp,
        ),
    ) {
        Text(
            text = "Ingredients",
            style = if (compact) {
                MaterialTheme.typography.bodyMedium
            } else {
                MaterialTheme.typography.titleSmall
            },
            fontWeight = FontWeight.Bold,
            color = CRAFT_INK,
        )
        CraftRule(modifier = Modifier.padding(vertical = if (compact) 3.dp else 8.dp))
        if (recipe.ingredients.isEmpty()) {
            Text(
                text = "No ingredients required",
                style = MaterialTheme.typography.bodyMedium,
                color = CRAFT_MUTED,
            )
        } else {
            LazyColumn(
                modifier = Modifier.fillMaxWidth().weight(1f),
                verticalArrangement = Arrangement.spacedBy(if (compact) 3.dp else 5.dp),
            ) {
                items(
                    items = recipe.ingredients,
                    key = { "${it.id}\u0000${it.name}" },
                ) { ingredient ->
                    val enough = ingredient.have >= ingredient.required
                    Row(
                        modifier = Modifier.fillMaxWidth().heightIn(min = if (compact) 24.dp else 39.dp),
                        verticalAlignment = Alignment.CenterVertically,
                        horizontalArrangement = Arrangement.spacedBy(9.dp),
                    ) {
                        CraftSprite(
                            sprite = state.sprites[ingredient.id],
                            description = ingredient.name,
                            modifier = Modifier.size(if (compact) 20.dp else 34.dp),
                            fallbackText = ingredient.name.take(1),
                        )
                        Text(
                            text = ingredient.name,
                            style = if (compact) {
                                MaterialTheme.typography.bodySmall
                            } else {
                                MaterialTheme.typography.bodyMedium
                            },
                            color = CRAFT_INK,
                            modifier = Modifier.weight(1f),
                            maxLines = 1,
                            overflow = TextOverflow.Ellipsis,
                        )
                        Text(
                            text = "${ingredient.have} / ${ingredient.required}",
                            style = if (compact) {
                                MaterialTheme.typography.bodySmall
                            } else {
                                MaterialTheme.typography.bodyMedium
                            },
                            fontWeight = FontWeight.Bold,
                            color = if (enough) CRAFT_GREEN else CRAFT_MISSING,
                        )
                        CraftSprite(
                            sprite = state.sprites[
                                if (enough) StardewUi.CHECK_ON else StardewUi.CHECK_OFF
                            ],
                            description = null,
                            modifier = Modifier.size(if (compact) 14.dp else 23.dp),
                        )
                    }
                }
            }
        }
    }
}

@Composable
private fun CraftActionButton(
    state: StardewState,
    enabled: Boolean,
    modifier: Modifier,
    compact: Boolean,
    onClick: () -> Unit,
) {
    val frame = state.frame(StardewUi.BUTTON)
    val base = modifier.heightIn(min = if (compact) 36.dp else 44.dp)
    val painted = if (frame != null) {
        base.drawBehind { drawNineSlice(frame, size) }
    } else {
        base
            .clip(ThorTheme.shapes.small)
            .background(CRAFT_BUTTON)
            .border(1.dp, CRAFT_BORDER, ThorTheme.shapes.small)
    }
    Box(
        modifier = painted
            .alpha(if (enabled) 1f else 0.58f)
            .clickable(enabled = enabled, role = Role.Button, onClick = onClick),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text = "Craft",
            style = if (compact) {
                MaterialTheme.typography.bodyMedium
            } else {
                MaterialTheme.typography.titleSmall
            },
            fontWeight = FontWeight.Bold,
            color = CRAFT_INK,
            textAlign = TextAlign.Center,
            modifier = Modifier.padding(horizontal = 16.dp, vertical = 8.dp),
        )
    }
}

@Composable
private fun CraftTimeCarousel(
    state: StardewState,
    recipes: List<StardewRecipe>,
    selectedKey: String,
    modifier: Modifier,
    compact: Boolean,
    onSelect: (StardewRecipe) -> Unit,
) {
    if (recipes.isEmpty()) {
        Box(modifier = modifier, contentAlignment = Alignment.Center) {
            Text(
                text = "Ready recipes will appear here",
                style = MaterialTheme.typography.bodySmall,
                color = CRAFT_MUTED,
            )
        }
        return
    }

    LazyRow(
        modifier = modifier.padding(vertical = 5.dp),
        contentPadding = PaddingValues(horizontal = 8.dp),
        horizontalArrangement = Arrangement.spacedBy(5.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        items(recipes, key = { it.selectionKey }) { recipe ->
            val selected = recipe.selectionKey == selectedKey
            val frame = state.frame(if (selected) StardewUi.SLOT_SELECTED else StardewUi.SLOT)
            val base = Modifier
                .width(if (compact) 80.dp else 96.dp)
                .fillMaxHeight()
            val painted = if (frame != null) {
                base.drawBehind { drawNineSlice(frame, size) }
            } else {
                base
                    .clip(ThorTheme.shapes.small)
                    .background(if (selected) CRAFT_SELECTED else CRAFT_SLOT)
                    .border(
                        if (selected) 2.dp else 1.dp,
                        if (selected) CRAFT_GOLD else CRAFT_BORDER,
                        ThorTheme.shapes.small,
                    )
            }
            Column(
                modifier = painted
                    .semantics {
                        this.selected = selected
                        contentDescription = if (recipe.ready) {
                            "${recipe.name}, ready to craft"
                        } else {
                            "${recipe.name}, missing materials"
                        }
                    }
                    .clickable(role = Role.Button) { onSelect(recipe) }
                    .padding(horizontal = 5.dp, vertical = 4.dp),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) {
                CraftSprite(
                    sprite = state.sprites[recipe.id],
                    description = null,
                    modifier = Modifier.size(if (compact) 34.dp else 46.dp),
                    fallbackText = recipe.name.take(1),
                )
                Text(
                    text = recipe.name,
                    style = MaterialTheme.typography.labelSmall,
                    fontSize = if (compact) 8.sp else 9.sp,
                    lineHeight = 10.sp,
                    fontWeight = FontWeight.SemiBold,
                    color = CRAFT_INK,
                    maxLines = 1,
                    overflow = TextOverflow.Ellipsis,
                )
                Text(
                    text = if (recipe.ready) "Ready" else "Missing",
                    style = MaterialTheme.typography.labelSmall,
                    fontSize = if (compact) 8.sp else 9.sp,
                    lineHeight = 10.sp,
                    color = if (recipe.ready) CRAFT_GREEN else CRAFT_MISSING,
                    maxLines = 1,
                )
            }
        }
    }
}

@Composable
private fun EmptyCraftingPage(state: StardewState, modifier: Modifier) {
    Column(
        modifier = modifier.padding(24.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.Center,
    ) {
        CraftSprite(
            sprite = state.sprites[StardewUi.TAB_CRAFTING],
            description = null,
            modifier = Modifier.size(52.dp),
        )
        Text(
            text = "No recipes available",
            style = MaterialTheme.typography.titleMedium,
            fontWeight = FontWeight.Bold,
            color = CRAFT_INK,
            modifier = Modifier.padding(top = 9.dp),
        )
        Text(
            text = "Connect to a save with crafting enabled to load your farmer's known recipes.",
            style = MaterialTheme.typography.bodySmall,
            color = CRAFT_MUTED,
            textAlign = TextAlign.Center,
            modifier = Modifier.padding(top = 4.dp),
        )
    }
}

@Composable
private fun CraftSprite(
    sprite: UiSprite?,
    description: String?,
    modifier: Modifier,
    fallbackText: String = "",
) {
    if (sprite != null) {
        Image(
            bitmap = sprite.image,
            contentDescription = description,
            modifier = modifier,
            contentScale = ContentScale.Fit,
            filterQuality = FilterQuality.None,
        )
    } else {
        Box(modifier = modifier, contentAlignment = Alignment.Center) {
            if (fallbackText.isNotEmpty()) {
                Text(
                    text = fallbackText,
                    style = MaterialTheme.typography.titleMedium,
                    fontWeight = FontWeight.Bold,
                    color = CRAFT_MUTED,
                )
            }
        }
    }
}

@Composable
private fun CraftRule(modifier: Modifier = Modifier) {
    Box(modifier.fillMaxWidth().height(1.dp).background(CRAFT_LINE))
}

@Composable
private fun CraftVerticalRule() {
    Box(Modifier.fillMaxHeight().width(1.dp).background(CRAFT_LINE))
}

private enum class CraftCategory(
    val title: String,
    val description: String,
) {
    ALL("All", "Every crafting recipe your farmer currently knows."),
    STORAGE("Storage", "Stores supplies and helps keep the farm organized."),
    MACHINES("Machines", "Processes farm goods into useful or valuable products."),
    FARMING("Farming", "A useful tool or supply for tending the farm."),
    PATHS("Paths", "Builds fences, gates, floors, and paths around the farm."),
    FISHING("Fishing", "Helps catch fish or improves fishing equipment."),
    UTILITY("Utility", "A practical item for exploring and everyday farm work."),
    DECOR("Decor", "Adds a decorative finishing touch to the farm or farmhouse."),
    OTHER("Other", "Another useful item your farmer knows how to make."),
}

private fun categoryFor(recipe: StardewRecipe): CraftCategory {
    val name = recipe.name.lowercase()
    return when {
        name.containsAny("chest", "storage", "dresser") -> CraftCategory.STORAGE
        name.containsAny(
            "furnace", "kiln", "tapper", "keg", "preserves jar", "mayonnaise",
            "cheese press", "loom", "oil maker", "seed maker", "crystalarium",
            "recycling machine", "worm bin", "slime egg-press", "slime egg press",
            "incubator", "lightning rod", "bee house", "cask", "dehydrator",
            "bait maker", "fish smoker", "geode crusher", "bone mill",
        ) -> CraftCategory.MACHINES
        name.containsAny(
            "scarecrow", "sprinkler", "fertilizer", "speed-gro", "retaining soil",
            "grass starter", "seed", "tree fertilizer", "garden pot", "deluxe soil",
        ) -> CraftCategory.FARMING
        name.containsAny(
            "fence", "gate", "floor", "path", "stepping stone", "walkway",
        ) -> CraftCategory.PATHS
        name.containsAny(
            "bait", "spinner", "bobber", "tackle", "hook", "crab pot", "fishing",
        ) -> CraftCategory.FISHING
        name.containsAny(
            "torch", "campfire", "brazier", "bomb", "staircase", "warp totem",
            "tent kit", "farm computer", "mini-obelisk", "cookout kit", "ring",
        ) -> CraftCategory.UTILITY
        name.containsAny(
            "chair", "bench", "table", "sign", "statue", "flute block", "drum block",
            "flower", "seasonal plant", "jack-o-lantern", "stone cairn", "tub o' flowers",
        ) -> CraftCategory.DECOR
        else -> CraftCategory.OTHER
    }
}

private fun String.containsAny(vararg needles: String): Boolean =
    needles.any(::contains)

private val StardewRecipe.selectionKey: String
    get() = key.ifEmpty { "$id\u0000$name" }

private val CRAFT_INK = Color(0xFF321B0F)
private val CRAFT_MUTED = Color(0xFF70451F)
private val CRAFT_BORDER = Color(0xFFA95F16)
private val CRAFT_LINE = Color(0x55A95F16)
private val CRAFT_GOLD = Color(0xFFF2A900)
private val CRAFT_GREEN = Color(0xFF167A20)
private val CRAFT_MISSING = Color(0xFFB3261E)
private val CRAFT_PARCHMENT = Color(0xFFFFD991)
private val CRAFT_WASH = Color(0x33FFF0BD)
private val CRAFT_BUTTON = Color(0xFFE5A34D)
private val CRAFT_ROW = Color(0x22FFD789)
private val CRAFT_SLOT = Color(0x36FFD789)
private val CRAFT_SELECTED = Color(0x55FFD45C)
