# skiff

A small retained-mode UI framework on top of Skia, in the shape osu!framework
gives its drawables, sized for machines with no GPU acceleration.

It is four modules:

| module | what is in it |
| --- | --- |
| `skia` | a module wrapper over the Skia headers, so nothing downstream needs a global module fragment |
| `skiff.paint` | a font stack with fallbacks behind a primary face, a canvas painter, easing curves |
| `skiff.scene` | the graph: anchors and origins, relative and automatic sizing, typed stylesheets, transforms with easing, hit testing, and damage tracking that yields a dirty rectangle per frame |
| `skiff.nodes` | the drawables: `Box`, `Text`, `Sprite`, `FillFlow`, `ScrollContainer`, `CachedContainer`, `Clickable` |

## Writing a tree

Every layout input a drawable has lives in one aggregate, so a node is written
down rather than assembled:

```cpp
auto root = scene::make<nodes::Box>({.fill = true}, kBackground);

auto *plate = root->add<nodes::Box>({.place = scene::Anchor::kTopCentre,
                                     .y = 24.0f,
                                     .autoSize = scene::Axes::kBoth,
                                     .padding = scene::Margin::all(8.0f),
                                     .cornerRadius = 6.0f},
                                    kPlateColour);
auto *label = plate->add<nodes::Text>({.place = scene::Anchor::kCentre},
                                      "0pp", 18.0f, kWhite);
```

`add<T>` builds the child in place, applies the spec and hands it back typed.
`place` sets anchor and origin together; `fill`, `fillX` and `fillY` are
relative sizing at 1.0 on one axis or both. Zero means unspecified, which is
what lets `{}` be a no-op and a custom node keep the size its constructor
chose.

## Styling

Reusable appearance and layout belongs in a typed stylesheet. Both node kinds
and application-defined roles are C++ types, so there are no selector strings,
runtime hashes or RTTI:

```cpp
struct Button;
struct ButtonLabel;
struct Sidebar;

struct LazerTheme {
  static constexpr auto styles =
      scene::makeStyleSheet()
          .rule(scene::select<nodes::Box, Button>(),
                {.height = 42.0f,
                 .padding = scene::Margin::horizontal(14.0f),
                 .cornerRadius = 8.0f,
                 .backgroundColour = kButton,
                 .transitionMs = 120.0})
          .rule(scene::select<nodes::Box, Button>().when(
                    scene::StyleState::kHover),
                {.scale = 1.03f, .backgroundColour = kButtonHover})
          .rule(scene::select<nodes::Text, ButtonLabel>(),
                {.colour = kWhite,
                 .fontSize = 16.0f,
                 .fontBold = true})
          .rule(scene::selectAny<Sidebar>().atMostWidth(900.0f),
                {.visible = false});
};

auto root = scene::make<nodes::Box>({.fill = true}, kBackground);
auto *button = root->add<nodes::Box>({.roles = {scene::role<Button>}}, kButton);
button->add<nodes::Text>({.roles = {scene::role<ButtonLabel>}}, "Play", 14.0f,
                         kWhite);
root->setStyleSheet<LazerTheme>();
```

Later matching rules override earlier ones. Foreground colour, font size and
font weight inherit through containers; backgrounds do not. State is
updated with `setSelected()` and `setDisabled()`, while hover is maintained by
`setHover()`. Width constraints on selectors are lightweight media queries.
Position, size, scale and alpha use the rule's transition when a selector
starts or stops matching. Removing the sheet restores each node's original
values.

The selector's node and role list are template parameters, and each `.rule()`
returns a new stylesheet type containing the previous rule tuple. Because the
theme owns that tuple as `static constexpr`, resolving a node is an unrolled,
allocation-free pass; each subtree stores only one generated resolver function
pointer. Custom node types that need type selectors derive from
`scene::TypedDrawable<MyNode>`. Nodes that only need role selectors may keep
deriving from `scene::Drawable`.

## Building and testing

`skiff` is the library project, `test` is a standalone test consumer, and
`all` is the aggregate project. The test project fetches the module build of
`j4niwzis/googletest-modules` through CPM and therefore requires CMake 4.3.4
or newer.

```sh
cmake -S all -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

Configure `test` directly to build only the library and its tests. Pass
`-DTEST_INSTALLED_VERSION=ON` to test a previously installed `skiff` package
instead of the adjacent source tree.

## Redrawing

Changing anything marks the node damaged, and the damage walks up to the root
through the parents' bounds and masking. A frame asks the root for one
rectangle:

```cpp
label->setText("734pp");           // no-op if the string did not change
root->updateTree(nowMs);
root->layoutIfNeeded(screen);
const skia::SkRect dirty = root->takeDamage();
```

That rectangle is the point of the whole thing: the framework it is shaped
after redraws everything every frame because it assumes a GPU. This one is
built to redraw the part that moved.

## Status

Extracted from a client that uses it for song select, settings, downloads,
the main menu and the pause overlay. It is the smaller half of what
osu!framework does, kept to what those screens actually needed.
