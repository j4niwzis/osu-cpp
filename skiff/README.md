# skiff

A small retained-mode UI framework on top of Skia, in the shape osu!framework
gives its drawables, sized for machines with no GPU acceleration.

It is four modules:

| module | what is in it |
| --- | --- |
| `skia` | a module wrapper over the Skia headers, so nothing downstream needs a global module fragment |
| `skiff.paint` | a font stack with fallbacks behind a primary face, a canvas painter, easing curves |
| `skiff.scene` | the graph: anchors and origins, relative and automatic sizing, padding and margins, transforms with easing, hit testing, and damage tracking that yields a dirty rectangle per frame |
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
