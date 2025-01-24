# The Perception UI library.

For a background and history of the UI library, see [GUI Framework Design](../../Ramblings/GUI%20Framework%20Design.md).

## Introduction
Everything is a tree of [`Node`](public/perception/ui/node.h)s. The entire GUI that is shown on the screen is a hierarchy of nodes. Inheritence is not used, as composition is prefered.

Each node has a [`Layout`](public/perception/ui/layout.h). Nodes are layed out according to the [CSS Flexible Box Layout](https://en.wikipedia.org/wiki/CSS_Flexible_Box_Layout) model (which under the hood utilizes [Facebook Yoga](https://www.yogalayout.dev/)).

Composition is used as much as possible to create a variety of UI elements from a few basic building blocks, such as `Label` (for drawing text) and `Block` (a basic shape that may be filled, have a border, clip its children, etc.) There are also functions that allow you to compose the node hierarchy in a declarative style.

To build a GUI application, you create a window node that contains the children to draw, then you call `HandOverControl()`. Here is a complete example that creates a dialog that contains the text "Hello World":

``` C++
using ::perception::HandOverControl;
using ::perception::ui::components::Label;
using ::perception::ui::components::UiWindow;

int main(int argc, char *argv[]) {
  auto window = UiWindow::Dialog("Welcome",
    Label::BasicText("Hello World")); 
  HandOverControl();
  return 0;
}
```

Here's a more complicated example that forces the dialog to be 640x480 and centers the text:

``` C++
using ::perception::HandOverControl;
using ::perception::ui::Layout;
using ::perception::ui::TextAlignment;
using ::perception::ui::components::Label;
using ::perception::ui::components::UiWindow;

int main(int argc, char *argv[]) {
  auto window = UiWindow::Dialog("Welcome",
    Label::BasicText("Hello World",
      [](Layout &layout) {
        layout.SetWidth(640);
        layout.SetHeight(480);
        layout.SetJustifyContent(YGJustifyCenter);
        layout.SetAlignContent(YGAlignCenter);
      },
      [](Label &label) {
        label.SetAlignText(TextAlignment::MiddleCenter);
      }));
 
  HandOverControl();
  return 0;
}
```

Builder functions takes a variable number of parameters that can either be modifiers (functions that take a reference to the `Node`, `Layout`, or a component) that apply properties on the node being created, or other nodes which are added as children. The `Node::Empty()` builder (not shown in this example) returns a basic `std::shared_ptr<Node>` that does not have any components attached. Most builder functions start from another builder function (and `Node::Empty()` at the deepest level), and then optionally add components, adjust the node and its layout, and/or add children, and then return the created node.

In the above example `UiWindow::Dialog` is a builder function that returns a `std::shared_ptr<Node>` that has `UiWindow` component. `Label::BasicText` is another builder function that returns a `std::shared_ptr<Node>` that has a `Label` component. It takes one parameter that's unique to `Label::BasicText` which is a string, and a variable number of extra parameters.

If you want to update a node dynamically, there are a few ways you can do this. Here are different ways you can update the text inside of a `Label` component on a node.

Option 1 - Create the label node to its own variable, then add that variable as a child of `UiWindow::Dialog`:

``` C++
auto label_node = Label::BasicText("Hello World",
  [](Layout &layout) {
    layout.SetWidth(640);
    layout.SetHeight(480);
    layout.SetJustifyContent(YGJustifyCenter);
    layout.SetAlignContent(YGAlignCenter);
  },
  [](Label &label) {
    label.SetAlignText(TextAlignment::MiddleCenter);
  })

auto window = UiWindow::Dialog("Welcome",
  label_node);

label_node->Get<component::Label>()->SetText("Goodbye World");
```

Option 2 - Save the the node into a parameter. If you pass a `std::shared_ptr<Node> *` as a modifier, it gets assigned to point to the `Node`.

```C++
std::shared_ptr<Node> label_node;
auto window = UiWindow::Dialog("Welcome",
  Label::BasicText("Hello World",
    [](Layout &layout) {
      layout.SetWidth(640);
      layout.SetHeight(480);
      layout.SetJustifyContent(YGJustifyCenter);
      layout.SetAlignContent(YGAlignCenter);
    },
    [](Label &label) {
      label.SetAlignText(TextAlignment::MiddleCenter);
    },
    &label_node));
label_node->Get<component::Label>()->SetText("Goodbye World");
```

Option 3 - Save the component into a parameter. If you pass a `std::shared_ptr<Component Type> *` as a modifier, it gets a assigned to point to the instance of component inside of the node.

```C++
std::shared_ptr<Label> label;
auto window = UiWindow::Dialog("Welcome",
  Label::BasicText("Hello World",
    [](Layout &layout) {
      layout.SetWidth(640);
      layout.SetHeight(480);
      layout.SetJustifyContent(YGJustifyCenter);
      layout.SetAlignContent(YGAlignCenter);
    },
    [](Label &label) {
      label.SetAlignText(TextAlignment::MiddleCenter);
    },
    &label));
label->SetText("Goodbye World");
```

For convenience sake, you can still use the declarative syntax after a node is constructed:

```C++
label_node.Apply(
  [](Label& label) {
    label.SetText("Goodbye World");
  });
```

Nodes can be given OnDraw, OnMouseHover, etc. handler functions, as well as custom hit test functions, measure functions, etc. For example, here's a a custom drawing function, where `DrawContext` includes [Skia](https://skia.org/) canvas and the position and size of the node being drawn.

```C++
auto node = Node::Empty([&](Node &node) { 
    node.OnDraw([](const DrawContext& ctx) {
      // Draw something here.
    });
  });
```

Nodes can hold an arbitary number of custom components that can be used to hold state and perform logic. They are used when you want to have a data and/or a controller attached to a node. For example, the `Label` component holds a text string and a font, and it binds its `Draw` method as one of the node's `OnDraw` handlers to draw said text and font.

It is preferred for components to use composition as much as possible. For example, `Button` doesn't implement its own drawing method, but is made up of `Block` which does the drawing, and you can add a child `Label` to it. The `Button` component has logic for controlling the color of the `Block` when it is interacted with with the mouse.

## Custom builder functions
Composition is achieved by building your own functions that return the constructed `std::shared_ptr<Node>`. Here is an example of a function that builds a red square button:

```C++
template <typename... Modifiers>
std::shared_ptr<perception::ui::Node> RedSquareButton(
    std::string_view label, std::function<void()> on_click_handler,
    Modifiers... modifier) {
  return Label::TextButton(label, on_click_handler,
    [&](Layout& layout) {
      layout.SetWidth(100);
      layout.SetHeight(100);
    },
    [&](Block& block) {
      block.SetBorderRadius(0.0f);
    },
    [&](Button& button) {
      button.SetIdleColor(0xFF0000);
      button.SetHoverColor(0xFFAAAA);
      button.SetPushedColor(0xFF5555);
    },
    modifiers...);
}
```

Then you can create a node with the `RedSquareButton` function anywhere you want.

```C++
VerticalContainer(
  RedSquareButton("A", [] {}),
  RedSquareButton("B", [] {}));
```

## Custom components

You can attach state, data, and controllers to buttons using components. A component can be any class (it doesn't need to inherit anything), it just needs a public `void SetNode(std::weak_ptr<Node> node)` method that gets called the first time a component is added to a node. Nodes and components are owned by shared pointers, and nodes hold a shared pointer to their components. If a component wants to hold a reference to the node it's attached to, you must store it as a `std::weak_ptr<Node>`, not as a `std::shared_ptr<Node>` otherwise you'll create a retain cycle that will never get deallocated!

There are several ways to add a component to a node. The easiest is during construction by passing a function that takes a reference to the component. The component will be added before the function is called.

```C++
Node::Empty([](MyComponent& my_component) {
  // Adds a MyComponent to the node.
});
```

You can also call Add(), Get(), or GetOrAdd() on a node.

```C++
// Adds a component. Does nothing if it already exists.
node->Add<MyComponent>();

// Returns an existing component, or an empty shared_ptr if there is not one.
std::shared_ptr my_component = node->Get<MyComponent>();

// Returns an existing component if it exists, or creates and then returns the component.
std::shared_ptr my_component = node->GetOrAdd<MyComponent>();

// Adds an existing component to a node.
std::shared_ptr<MyComponent> component = std::make_shared<MyComponent>();
node->Add(component);
```

## Node functions
If you really need to do your own drawing, you can register drawing function on a node with `OnDraw` and `OnDrawPostChildren`. Likewise, there are many other functions you can register on a node, such as `OnGainFocus`, `OnMouseButtonDown`, etc. For most of these functions, a node can have multiple functions. This is so multiple components can be attached to a node (say react to mouse events) without conflicting with one another. Some functions, such as `SetHitTestFunction`, `SetMeasureFunction`, there can only be one, and subsequent calls to set a function will override the previously set function.

If you do your own drawing on a node and its state changes that requires redrawing the node, you can call `Invalidate()`. Likewise, if you have a custom measuring function and the size changes, you can call `Remeasure()`.

## Library of functions and components
Check out [`public/perception/ui/components/`](public/perception/ui/components/) for components and functions. Here are some common ones:

* `Block::SolidColor(color, ...)` - A solid color block.
* `Button:TextButton(text, on_push, ...)` - A text button.
* `Container::HorizontalContainer(...)` - A horizontal container.
* `Container::VerticalContainer(...)` - A vertical container.
* `ImageView::BasicImage(...)` - An image.
* `Label::BasicLabel(text, ...)` - A basic label.
* `UiWindow::ResizableWindow(title, ...)` - A resizable window.
* `UiWindow::Dialog(title, ...)` - A fixed size dialog window.
