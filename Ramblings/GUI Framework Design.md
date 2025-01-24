This is a background on me exploring GUI framework design that lead to the creation of the Perception UI. See [Perception UI](../Libraries/Perception%20UI/) for the current framework.

Once Perception matured enough that I wanted to start to draw things on the screen, I needed to design a GUI framework. Most C++ GUI frameworks are cumbersome. What I don't want is something as verbose as Win32 which requires a lot of boilerplate to do anything.

Here's an example of an API I don't want (I copied this from [https://www.paulgriffiths.net/program/c/srcs/winhellosrc.html](https://www.paulgriffiths.net/program/c/srcs/winhellosrc.html) but removed the comments for brevity).

```C++
LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
		   LPSTR szCmdLine, int iCmdShow) {
    static char szAppName[] = "winhello";
    HWND        hwnd;
    MSG         msg;
    WNDCLASSEX  wndclass;

    wndclass.cbSize         = sizeof(wndclass);
    wndclass.style          = CS_HREDRAW | CS_VREDRAW;
    wndclass.lpfnWndProc    = WndProc;
    wndclass.cbClsExtra     = 0;
    wndclass.cbWndExtra     = 0;
    wndclass.hInstance      = hInstance;
    wndclass.hIcon          = LoadIcon(NULL, IDI_APPLICATION);
    wndclass.hIconSm        = LoadIcon(NULL, IDI_APPLICATION);
    wndclass.hCursor        = LoadCursor(NULL, IDC_ARROW);
    wndclass.hbrBackground  = (HBRUSH) GetStockObject(WHITE_BRUSH);
    wndclass.lpszClassName  = szAppName;
    wndclass.lpszMenuName   = NULL;

    RegisterClassEx(&wndclass);

    hwnd = CreateWindow(szAppName, "Hello, world!",
			WS_OVERLAPPEDWINDOW,
			CW_USEDEFAULT, CW_USEDEFAULT,
			CW_USEDEFAULT, CW_USEDEFAULT,
			NULL, NULL, hInstance, NULL);

    ShowWindow(hwnd, iCmdShow);
    UpdateWindow(hwnd);

    while (GetMessage(&msg, NULL, 0, 0)) {
    	TranslateMessage(&msg);
    	DispatchMessage(&msg);
    } 
    return msg.wParam;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT iMsg, WPARAM wParam, LPARAM lParam) {
    PAINTSTRUCT ps;
    HDC         hdc;

    switch ( iMsg ) {
    case WM_PAINT:
	    hdc = BeginPaint(hwnd, &ps);
	    TextOut(hdc, 100, 100, "Hello, world!", 13);
	    EndPaint(hwnd, &ps);
	    return 0;
    case WM_DESTROY:
	    PostQuitMessage(0);
	    return 0;
    }
    return DefWindowProc(hwnd, iMsg, wParam, lParam);
}

```

I really like declarative GUI frameworks. Declarative GUI frameworks feel more natural and easy to design user interfaces in. Here's an example from a [Jetpack Compose codelab](https://developer.android.com/codelabs/jetpack-compose-basics#7):

```Kotlin
fun OnboardingScreen(modifier: Modifier = Modifier) {
    // TODO: This state should be hoisted
    var shouldShowOnboarding by remember { mutableStateOf(true) }

    Column(
        modifier = modifier.fillMaxSize(),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text("Welcome to the Basics Codelab!")
        Button(
            modifier = Modifier.padding(vertical = 24.dp),
            onClick = { shouldShowOnboarding = false } 
        ) {
            Text("Continue")
        }
    }
}
```

There are 3 main differences between declarative and imperative GUI frameworks. The first difference is the programming style in which a GUI is laid out.

In a decalarative framework, construction often looks like a nested view:

```
Column(
    Button(
        "Button 1",
        { Some code to run when it's clicked. }
    )
    Button(
        "Button 2",
        { Some code to run when it's clicked. }
    )
)
```

In an imperative framework, construction often takes place by manually adding each child to the parent.

```
button_1 = new Button()
button_1.label = "Button 1"
button_1.click_handler = button_1_handler

button_2 = new Button()
button_2.label = "Button 2"
button_2.click_handler = button_2_handler

column = new Column()
column.AddChild(button_1);
column.AddChild(button_2);
```

The end result tends to be the same in memory (a hierarchy of nodes/widgets/etc).

The second main difference between declarative and imperative frameworks is how you update the view when something changes.

In a declarative framework, the data gets 'bounds' to the view. You say that text powers a label, and the label automatically updates when the label changes. You define your view as:

```
Label("You have " + $i + " apples.")
```

And when you change `$i`, the label automatically changes. Maybe it's automatic (an observer is added to the variable), or maybe you have to call `Refresh()`, but the system largely detects what has changed, but the burden typically isn't on the programmer to keep reference to everywhere `$i` is used and to update all of them when it changes.

In an imperative framework, the program has to let the model know something changed.

```
$i += 5
UpdateLabel()

function UpdateLabel():
    label.text = "You have " + $i + " apples."
```

The third main difference between declarative and imperative frameworks is how create new widgets.

In a declarative framework, new widgets are typically created by composing several other widgets together. For example:

```
function FancyButton(text, on_click_handler, args...):
    return Button(
        Text(text),
        OnClick(on_click_handler),
        Size(128x32),
        Background(Ripples),
        args...)
```

In an imperative framework, new widgest are typically created by extending an existing widget. For example:

```
class FancyButton extends Button:
    FancyButton():
        SetSize(128x32)
    
    override function Draw():
        DrawRipples()
        base.Draw()
```

Or, you can make sure widget classes generic, and then have builder functions that create what you need:

```
function BuildFancyButton(text, on_click_handler):
    button = new Button()
    button.SetText(text)
    button.SetOnClick(on_click_handler)
    button.SetSize(128x32)

    ripples = new Ripples()
    button.SetBackground(ripples)

    return button
```

What I like about using composition over inheritence is that often there's little to no distinction between widgets and builder functions, because basically everything is a function that composes together smaller functions. (Of course, at the very lowest level you do need some .fundamental elements such as text labels and drawing primitives.)

I went through several iterations of my C++ GUI API until I settled on one I really like.

Coming from a C++ background, I'm most used to imperative frameworks. There is a `Widget` base class. `Button` inherits from `Widget`. `Label` inherits from `Widget`. If you want `FancyButton` you create a new class and inherit from `Button` and override the neccessary methods to draw your fancy button.

So, my first attempt to build a C++ GUI API was taking what I liked most declarative frameworks (the programming style of how you can build a hierarchy of elements). I tried to build a chain syntax, so I could lay out my UI like a tree and avoid having to assign everything to a variable:

```C++
window->SetRoot(
    std::make_shared<VerticalContainer>()->
        AddChild(display)->
        AddChild(std::make_shared<FixedGrid>()->SetColumns(4)->SetRows(5)->
            AddChildren({
                std::make_shared<Button>()->SetLabel("C")->
                    OnClick(PressClear)->ToSharedPtr(),
                std::make_shared<Button>()->SetLabel("+-")->
                    OnClick(PressFlipSign)->ToSharedPtr(),
                std::make_shared<Button>()->SetLabel("/")->
                    OnClick(std::bind(PressOperator, DIVIDE))->ToSharedPtr(),
                std::make_shared<Button>()->SetLabel("x")->
                    OnClick(std::bind(PressOperator, MULTIPLY))->ToSharedPtr(),
                std::make_shared<Button>()->SetLabel("7")->
                    OnClick(std::bind(PressNumber, 7))->ToSharedPtr(),
                std::make_shared<Button>()->SetLabel("8")->
                    OnClick(std::bind(PressNumber, 8))->ToSharedPtr(),
                std::make_shared<Button>()->SetLabel("9")->
                    OnClick(std::bind(PressNumber, 9))->ToSharedPtr(),
                std::make_shared<Button>()->SetLabel("-")->
                    OnClick(std::bind(PressOperator, SUBTRACT))->ToSharedPtr(),
                std::make_shared<Button>()->SetLabel("4")->
                    OnClick(std::bind(PressNumber, 4))->ToSharedPtr(),
                std::make_shared<Button>()->SetLabel("5")->
                    OnClick(std::bind(PressNumber, 5))->ToSharedPtr(),
                std::make_shared<Button>()->SetLabel("6")->
                    OnClick(std::bind(PressNumber, 6))->ToSharedPtr(),
                std::make_shared<Button>()->SetLabel("+")->
                    OnClick(std::bind(PressOperator, ADD))->ToSharedPtr(),
                std::make_shared<Button>()->SetLabel("1")->
                    OnClick(std::bind(PressNumber, 1))->ToSharedPtr(),
                std::make_shared<Button>()->SetLabel("2")->
                    OnClick(std::bind(PressNumber, 2))->ToSharedPtr(),
                std::make_shared<Button>()->SetLabel("3")->
                    OnClick(std::bind(PressNumber, 3))->ToSharedPtr()})->
            AddChild(
                std::make_shared<Button>()->SetLabel("=")->
                    OnClick(PressEquals)->ToSharedPtr(),
                    /*x=*/3, /*y=*/3, /*columns=*/1, /*rows=*/2)->
            AddChild(std::make_shared<Button>()->SetLabel("0")->
                    OnClick(std::bind(PressNumber, 0))->ToSharedPtr(),
                    /*x=*/0, /*y=*/4, /*columns=*/2, /*rows=*/1)->
            AddChild(std::make_shared<Button>()->SetLabel(".")->
                    OnClick(PressDecimal)->ToSharedPtr())
            ->SetSize(kFillParent)->ToSharedPtr()
        )->SetMargin(8)->SetSize(kFillParent)->ToSharedPtr())->
    OnClose([]() { TerminateProcess(); });
```

The implementation to get the chaining working looked like this:

```C++
class Button : public Widget {
    // ...
    public Button* SetLabel(std::string_view label) {
        // ...
        return this;
    }
};

class Widget {
    // ...
    public Widget* SetMargin(int margin) {
        // ...
        return this;
    }
}
```

I wasn't happy with this approach. First of all, it's super verbose because there's a lot of `std::make_shared<type>` to build a component, and then the final `->ToSharedPtr()` to add it as a child. C++ lacks something similar to `instancetype` that you see in other languages such as Objective-C, so you had to call methods on the sub-most class first and the super-most class last. For example, you must do this:

```C++
std::make_shared<Button>()->SetLabel("hello")->SetMargin(8)->ToSharedPtr();
```

But because `SetMargin(int)` returns a `Widget*` and not a `Button*`, you can't do this:

```C++
std::make_shared<Button>()->SetMargin(8)->SetLabel("hello")->ToSharedPtr();
```

I wasn't happy with the syntax, but it did achieve my goal of letting me lay out my UI in an imperative-style, so I dealt with it. In the back of my mind though, I would wonder if there's something better out there.

I became curious about how languages such as Go and Rust built GUI libraries without inheritence. It led me down a path of learning about [composition over inheritance](https://en.wikipedia.org/wiki/Composition_over_inheritance). In my spare time, I have built games in Unity. I understood the principle of making small reusable parts. If you have a chair that the player can sit on when they walked up to it and pressed a key, instead of creating a unique `Chair` class that had bespoke code that controlled what to do if they player pressed a key while in range, it was better to create a generic `Useable` class and attach it to their chair, and a generic `SitTarget` node that was a child of the chair that you could visually place in the editor on the surface of the chair, and then visually in the editor I could wire up `Useable` to trigger the `SitTarget`. Now I have these generic components: `Useable` could be attached to doors and switches, `SitTarget` could be called triggered when entering a vehicle or activating a computer. I could spend less time making bespoke components, less code to maintain, and be more productive wiring together existing resusable compnoents.

The other fascinating thing about learning about composition, is that there tends to be a high emphasis on builder functions. For example, `Button` isn't a class, it's a function that builds a generic `Node`, all wired up with a colored background, a text label, and a click handler. Under the hood there can be a `Button` data structure containing state (e.g. something needs to keep track of if the button is being hovered over or held down), but it's attached by builder function.

Inspired by this, I rewrote my GUI library to get rid of inheritence. Instead of a `Widget` base class, I created a `Node` class. Nothing subclasses from `Node`, instead you can register functions that get called such as when the mouse hovers over it or it needs to draw. Some widgets, such as buttons, require holding state, so you can attach arbitary objects to Nodes. I adopted Unity terminology and called these "Components". Components don't inherit from anything and can be any class.

Under the hood I was able to get rid of inheritence, but I still needed to figure out what the API would look like to compose together nodes. If my builder functions returned a `Node` (technically an `std::shared_ptr<Node>`), I could no longer do:

```C++
Button("Hello", handler)->SetDisabled(true)
```

Because `disabled` might be a property unique to the button (to show it grayed out and not interactive) and not a property of `Node`. Perhaps it would be nice if C++ could allow us to do:

```
Button(
    Text: "Hello",
    OnClick: handler
    Disabled: true);
```

But it doesn't. Perhaps we could use a struct, but `Button(ButtonParams{.text =, ...}))` feels like it'd be super verbose and also limiting in its flexibility because what if I wanted to override the layout. My first attempt to solve this problem was to make the builder functions accept variable length arguments, and the functions could either be a modifier that overrode a property or a child node.

```C++
Button(
    Text("Hello"),
    OnClick(handler),
    Disabled(true)
)
```

For a more complicated example:
```C++
auto button_panel = Block(
    FillColor(kButtonPanelBackgroundColor), BorderWidth(0.0f),
    BorderRadius(0.0f), Width(194.0f), AlignSelf(YGAlignStretch),
    Margin(YGEdgeAll, 0.0f), AlignContent(YGAlignCenter),
    JustifyContent(YGJustifyCenter),
    Node(FlexDirection(YGFlexDirectionRow), Margin(YGEdgeVertical, 5.0f),
        JustifyContent(YGJustifyCenter), CalculatorButton("C", PressClear),
        CalculatorButton("+-", PressFlipSign),
        CalculatorButton("/", std::bind(PressOperator, DIVIDE)),
        CalculatorButton("x", std::bind(PressOperator, MULTIPLY))),
    Node(FlexDirection(YGFlexDirectionRow), Margin(YGEdgeVertical, 5.0f),
        JustifyContent(YGJustifyCenter),
        CalculatorButton("7", std::bind(PressNumber, 7)),
        CalculatorButton("8", std::bind(PressNumber, 8)),
        CalculatorButton("9", std::bind(PressNumber, 9)),
        CalculatorButton("-", std::bind(PressOperator, SUBTRACT))),
    Node(FlexDirection(YGFlexDirectionRow), Margin(YGEdgeVertical, 5.0f),
        JustifyContent(YGJustifyCenter),
        CalculatorButton("4", std::bind(PressNumber, 4)),
        CalculatorButton("5", std::bind(PressNumber, 5)),
        CalculatorButton("6", std::bind(PressNumber, 6)),
        CalculatorButton("+", std::bind(PressOperator, ADD))),
    Node(
        FlexDirection(YGFlexDirectionRow), JustifyContent(YGJustifyCenter),
        Node(FlexDirection(YGFlexDirectionColumn),
            Node(FlexDirection(YGFlexDirectionRow),
                Margin(YGEdgeVertical, 5.0f),
                JustifyContent(YGJustifyCenter),
                CalculatorButton("1", std::bind(PressNumber, 1)),
                CalculatorButton("2", std::bind(PressNumber, 2)),
                CalculatorButton("3", std::bind(PressNumber, 3))),
            Node(FlexDirection(YGFlexDirectionRow),
                JustifyContent(YGJustifyCenter),
                Margin(YGEdgeVertical, 5.0f),
                CalculatorButton("0", std::bind(PressNumber, 0), Width(58)),
                CalculatorButton(".", PressDecimal))),
        CalculatorButton("=", std::bind(PressEquals), Height(58),
                        Margin(YGEdgeVertical, 5.0f))));
```

This was a lot of work under the hood, because every property had to be exposed as a modifier. The modifers were kind of messy for a few reasons.

They were tedious because you would have to make one for every property. They were classes instead of functions with an `Apply(node)` method that applied the property on the node. For example, the implementation of the modifier `Text(text)` would have to look like this:

```C++
class Text {
public:
Text(std::string text) : text_(text) {}
void Apply(Node &node) {
    if (auto label_component = node.Get<Label>())
        label_component->SetText(text);
}
std::string text_;
};
```

I used macros to try minimize the repeated code to clean it up, but I wasn't in love with that approach because macros are just messy.

```C++
// Modifier to set the button's pushed color.
COMPONENT_MODIFIER_1(ButtonPushedColor, components::Button, SetPushedColor,
                     uint32);
```

It was also super messy because each one of these modifiers were in the `builders` namespace and so unless I want my code to be super verbose, I had to put a very loarge `using` block in my file.

```C++
using ::perception::ui::builders::AlignContent;
using ::perception::ui::builders::AlignSelf;
using ::perception::ui::builders::AlignText;
using ::perception::ui::builders::Block;
using ::perception::ui::builders::BorderRadius;
using ::perception::ui::builders::BorderWidth;
using ::perception::ui::builders::FillColor;
using ::perception::ui::builders::FlexDirection;
using ::perception::ui::builders::FlexGrow;
using ::perception::ui::builders::Height;
using ::perception::ui::builders::JustifyContent;
using ::perception::ui::builders::Label;
using ::perception::ui::builders::Margin;
using ::perception::ui::builders::Node;
using ::perception::ui::builders::OnPush;
using ::perception::ui::builders::OnWindowClose;
using ::perception::ui::builders::Padding;
using ::perception::ui::builders::StandardButton;
using ::perception::ui::builders::Text;
using ::perception::ui::builders::Width;
using ::perception::ui::builders::Window;
using ::perception::ui::builders::WindowTitle;
```

There were name conflicts. `Label` was both a builder function and a the name of a component that you might want to refer to so you could update the label's text after it was constructed. I worried about name conflicts between multiple components that could have the similar property names, thus why the modifier that called `UiWindow->SetTitle` was called `WindowTitle(title)` not just `Title(title)`.

I could have explored putting the modifiers inside the component's class, so instead of `Title(title)` or `WindowTitle(title)`, you could do `UiWindow::Title(title)`, and this would have been a better approach than putting them directly in a namespace because there's less `using ...` for each modifier, and we wouldn't have to worry conflicts if multiple components had a `Title` property. There is still some risk of conflict within the class, if you have both a class name `Disable` and a method named `Disable()`.

All in all, I wasn't perfectly happy with this approach beacuse of name conflicts, the cluttered `using` block, and having to expose every property on a component via a modifier.

The final API (as of writing) that I settled on gets rid of these modifier classes. Common parameters (a `SolidColor` will always need a color parameter, a `TextButton` will always need a string label and a function handler) will just be standard parameters to the builder function. The variable arguments that follow it will either be a function to modify a nod/layout/component, a node to add as a child, or a pointer to save the node/component

Here's what it looks like:

```C++
auto button_panel = Block::SolidColor(
    kButtonPanelBackgroundColor,
    [](Layout& layout) {
        layout.SetWidth(194.0f);
        layout.SetAlignSelf(YGAlignStretch);
        layout.SetAlignContent(YGAlignCenter);
        layout.SetJustifyContent(YGJustifyCenter);
    },
    Container::VerticalContainer(
        [](Layout& layout) {
            layout.SetAlignSelf(YGAlignCenter);
            layout.SetWidthAuto();
        },
        Container::HorizontalContainer(
            Button::TextButton("C", PressClear),
            Button::TextButton("+-", PressFlipSign),
            Button::TextButton("/", std::bind(PressOperator, DIVIDE)),
            Button::TextButton("x", std::bind(PressOperator, MULTIPLY))),
        Container::HorizontalContainer(
            Button::TextButton("7", std::bind(PressNumber, 7)),
            Button::TextButton("8", std::bind(PressNumber, 8)),
            Button::TextButton("9", std::bind(PressNumber, 9)),
            Button::TextButton("-", std::bind(PressOperator, SUBTRACT))),
        Container::HorizontalContainer(
            Button::TextButton("4", std::bind(PressNumber, 4)),
            Button::TextButton("5", std::bind(PressNumber, 5)),
            Button::TextButton("6", std::bind(PressNumber, 6)),
            Button::TextButton("+", std::bind(PressOperator, ADD))),
        Container::HorizontalContainer(
            Container::VerticalContainer(
                Container::HorizontalContainer(
                    Button::TextButton("1", std::bind(PressNumber, 1)),
                    Button::TextButton("2", std::bind(PressNumber, 2)),
                    Button::TextButton("3", std::bind(PressNumber, 3))),
                Container::HorizontalContainer(
                    Button::TextButton(
                        "0", std::bind(PressNumber, 0),
                        [](Layout& layout) { layout.SetWidth(58); }),
                    Button::TextButton(".", PressDecimal))),
            Button::TextButton("=", PressEquals))));
```

The earlier problem we had was: if a builder function returns a `std::shared_ptr<Node>`, and `Button::TextButton` takes a string and a handler functon, how do we set a custom property on the `Button` component?

```C++
Button::TextButton("Hello", handler,
    [](Button& button) {
        button->SetDisabled(true);
    })
```

Likewise, every node has a `Layout`, and `Container::VerticalContainer` is a builder function that returns a `Node` with the correct properties set so that the child objects will be laid out vertically from top to bottom, but if you want to set custom properties on the node's layout, you can do:
```C++
Container::VerticalContainer(
        [](Layout& layout) {
            layout.SetAlignSelf(YGAlignCenter);
            layout.SetWidthAuto();
        },
        // ...
    )
```

Instead of breaking the declarative-style tree, I thought it'd be helpful to be able to pass a `std::shared_ptr` pointer to a builder functoin, and it'll be populated to pointer to that node or component.

```C++
std::shared_ptr<Label> display;

auto terminal_container = Block::SolidColor(
    kTerminalBackgroundColor,
    [](Layout& layout) {
    layout.SetFlexGrow(1.0f);
    layout.SetAlignSelf(YGAlignStretch);
    layout.SetMargin(YGEdgeAll, 0.f);
    },
    Label::BasicLabel(
        "",
        [](Label& label) {
        label.SetTextAlignment(TextAlignment::MiddleCenter);
        },
        [](Layout& layout) {
        layout.SetAlignSelf(YGAlignStretch);
        layout.SetFlexGrow(1.0);
        },
        &display));

display->SetText("Hello!");
```

Also, to avoid the huge `using` block, I decided to group the builder functions together by making them static methods inside of a class (the class in question is usually the component they attach to a Node):

```C++
class Button {
    public:

    template <typename... Modifiers>
    static std::shared_ptr<Node> TextButton(std::string_view text,
                                            std::function<void()> on_push,
                                            Modifiers... modifiers) {
        return BasicButton(on_push, Label::BasicLabel(text), modifiers...);
  }

  // ...
};
```

So instead of:

```C++
using ::perception::ui::builders::BasicButton;
using ::perception::ui::builders::TextButton;
using ::perception::ui::components::Button;
```

You can just use:

```C++
using ::perception::ui::components::Button;
```

One caveat with this is that C++ won't allow us to create a static function named `Button` insisde of a `Button` class (because it'll conflict with the constructor), so in these cases, I prefix the builder with `Basic`: `Button::BasicButton`, `Label::BasicLabel`, etc.

Now we are 2/3rd of the way there to having a declarative GUI framework in C++. We can build our GUI hierarchy declarative style, we've eliminated inheritence and instead compose widgets together using functions.

I could stop now and I'd probably be happy with the result, but it's worth talking about the last difference - bound data. In a purely theoretical world, this means you store your data in some data structure, and when that data structure is updated, the view is redrawn. When researching this approach I can across the [Elm Architecture](https://guide.elm-lang.org/architecture/). In the Elm Architecture, a widget has 3 parts: the model, the view, and the update. The model contains the data you want to show. The view describes (in declarative fashion) how to draw the model. While composing together the view, actions will be tagged with an event ID/enum, when that action occurs, the update function will be called which mutates the model with the outcome of that action.

I liked the separation of data and view, and how you can update the data and not have to care about all the places in the view where it gets updated. What I don't love about the Elm architecture is now our widgets are no longer functions but actually classes (with view and update methods). I also like the idea of being able to put consequences near where you use them, for example:

```C++
Button::TextButton("Do Something", []() {
    // The thing to do when clicked.
    });
```

...rather than push a message, then handle the message inside of a huge switch statement somewhere else.

An alternative I thought about that would still be compatible with our existing UI library would be to make make all of our properties observable. For example:

```C++
auto text = std::make_shared<Observable<std::string>>("Hello!");

Button::TextButton(text, [&text]() {
    *text = "Goodbye!";
});
```

The type stored inside of `Button` would be `Observee<std::string>` that could hold a refernce to either a raw `std::string` (so you could still set it to a constant), a `std::shared_ptr<Observable<std::string>>`, or a `std::shared_ptr<ObservableFunction<std::string>>`.

The concept of an observable function would be to do some calculation to produce the result, because let's say you want to set `text` to `"I have $i apples."` and you want to update `$i`.

```C++
auto apple_count = std::make_shared<Observable<int>>(5);
auto text = std::make_shared<ObservableFunction<std::string>>(
    [apple_count]() -> std::string {
        return "I have " + std::string(apple_count) + " apples.";
    }, apple_count);

Button::TextButton(text, [&text]() {
    *apple_count = apple_count->value() + 1;
});
```

It's workable and only a little messy. We'd have to store every property in `Node`, `Layout`, and every component as an `Observee<T>`. I also worry about it becoming inefficient under the hood, as there would be many layers in indirection that get called when a variable changes, and many allocated objects (the observables, the lambdas inside the components to register to listen to changes)

Perhaps one day I will pursue property binding, but for now I'm happy with my UI library being 2/3rds declarative.