# The Perception UI library.

Everything is `Node`. The entire GUI that is shown on the screen is a hierarchy of nodes. Inheritence is not used, as composition is prefered.

Each node has a layout (which can be accessed via `Node::GetLayout()`). Nodes are layed out according to the [CSS Flexible Box Layout](https://en.wikipedia.org/wiki/CSS_Flexible_Box_Layout) model (which under the hood utilizes [Facebook Yoga](https://www.yogalayout.dev/)).

Composition is used as much as possible to create a variety of UI elements from a few basic building blocks, such as `Label` (for drawing text) and `Block` (a basic shape that may be filled, have a border, clip its children, etc.) There are also builder classes and functions that allow you to compose the node hierarchy in a declarative style.

For example, here is Hello World example:

```
using ::perception::HandOverControl;
using ::perception::ui::TextAlignment;
using ::perception::ui::builders::AlignContent;
using ::perception::ui::builders::AlignText;
using ::perception::ui::builders::Height;
using ::perception::ui::builders::IsDialog;
using ::perception::ui::builders::JustifyContent;
using ::perception::ui::builders::Label;
using ::perception::ui::builders::Text;
using ::perception::ui::builders::Width;
using ::perception::ui::builders::Window;
using ::perception::ui::builders::WindowBackgroundColor;
using ::perception::ui::builders::WindowTitle;

int main(int argc, char *argv[]) {
  auto window = Window(WindowTitle("Welcome!"), IsDialog(),
    Label(
      JustifyContent(YGJustifyCenter),
      AlignContent(YGAlignCenter),
      Width(640),
      Height(480),
      Text("Hello World"),
      AlignText(TextAlignment::MiddleCenter)
    ));
 
  HandOverControl();
  return 0;
}
```

In this example `Window` is a builder function that returns a `std::shared_ptr<Node>` that has  `UiWindow` component. It takes a variable number of parameters that can either be modifiers (such as `WindowTitle` and `IsDialog`) that apply properties on the node being created, or other nodes which are added as children. The `Node` builder (not shown in this example) returns a basic `std::shared_ptr<Node>` that does not have a component attached.

`Label` is another builder function that returns a `std::shared_ptr<Node>` that has a `Label` component. If you want to change the text dynamically, you can do:

```
   auto label = 
    Label(
      JustifyContent(YGJustifyCenter),
      AlignContent(YGAlignCenter),
      Width(640),
      Height(480),
      Text("Hello World"),
      AlignText(TextAlignment::MiddleCenter)
    );

  auto window = Window(WindowTitle("Welcome!"), IsDialog(), label);
  label->Get<component::Label>()->SetText("Goodbye World);
```

For convenience sake, you can still use the declarative syntax after a node is constructed:

```
  ApplyModifiersToNode(*label, Text("Goodbye World", Height(200)));
```

Nodes can be given OnDraw, OnMouseHover, etc. handler functions, as well as custom hit test functions, measure functions, etc. For example, here's a a custom drawing function, where `DrawContext` includes [Skia](https://skia.org/) canvas and the position and size of the node being drawn.

```
  auto node =
     Node(OnDraw([](const DrawContext& ctx) {
        // Draw something here.
     }));
```

Components aren't actually required to get anything to draw. They are used when you want to have a data and/or a controller attached to a node. For example, the `Label` component holds a text string and a font, and it binds its `Draw` method as one of the node's `OnDraw` handlers to draw said text and font.

It is preferred for components to use composition as much as possible. For example, `Button` doesn't implement its own drawing method, but is made up of `Block` which does the drawing, and you can add a child `Label` to it. The `Button` component has logic for controlling the color of the `Block` when it is interacted with with the mouse.