export module skiff.widgets;

// The whole widget set, for a screen that wants all of it. Each widget is its
// own module and can be imported on its own -- a screen that needs a text box
// and nothing else says `import skiff.widgets.textbox;` and does not compile
// the rest.
//
// A widget draws itself from a Theme rather than from constants baked into
// it, so a screen restyles by handing over a different Theme and not by
// subclassing. The default Theme is a dark neutral one; the client overwrites
// theme() at startup with its own.

export import skiff.widgets.theme;
export import skiff.widgets.textbox;
export import skiff.widgets.button;
export import skiff.widgets.tabbar;
export import skiff.widgets.dropdown;
