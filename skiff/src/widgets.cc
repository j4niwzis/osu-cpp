export module skiff.widgets;

import std;
import skia;
import skiff.paint;
import skiff.scene;

// Widgets, as distinct from drawables: skiff.scene and skiff.nodes are the
// primitives -- a box, a string, a flow, a scroll -- and this is the layer of
// things a screen is actually made of, already knowing how they behave and
// what they look like.
//
// A widget draws itself from a Theme rather than from constants baked into
// it, so a screen restyles by handing over a different Theme and not by
// subclassing. The default Theme is a dark neutral one; the client overwrites
// theme() at startup with its own.

export import :theme;
export import :textbox;
export import :button;
export import :tabbar;
