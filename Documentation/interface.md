# The graphical backends
The graphical calendar interface supports multiple backends (availability
configurable at compile time). At runtime, one is selected automatically
(interactive backends are preferred), or you can specify it on the command
line.

## Interactive backends
With these backends, the view is continuously rendered to an output. The user
can potentially interact with the application. Currently, only keyboard input
is supported.

### Wayland backend
Wayland is natively supported, but note, that there is no graphical acceleration
available.

### Framebuffer backend
Renders directly to `/dev/fb0`. No input mechanism is supported.

## Image backends
With these backends, the application renders a single view into a file, then
exits. The output file name can be specified on the command line. Some graphical
elements are missing compared to the interactive backends, if they would only be
useful in an interactive scenario.

### Svg backend
Renders a vector graphic output. Note, that this might be removed in the future,
if we switch to a raster-only rendering solution.

### Image backend
Renders a raster output.

### Dummy backend
Renders the output, and discards it. Useful for testing.

# The keyboard interface
The goal of the keyboard interface is to make user interaction with the
application as efficient as possible. (As such, ease of use is not of major
concern.) The current keybindings are far from perfect, there is room for
improvement. Configurability is also planned, to make experimentation easier.

## Keybindings
- `a`: Switch to the calendar view.
- `s`: Switch to the todo view.
- `h`/`l`: Go backward/forward in time in the calendar view.
- `t`: Switch the calendar view to a view that includes the current time.
- `n`: Launch an editor, to create a new calendar component. The template
  defaults to an event or todo based on the current view.
- `e`: Edit a calendar component. A unique key combination is generated for
  each visible component, and displayed. (The combination is generated from the
  `ASDFQWER` set of keys.) The user must enter the key combination sequentially
  to select a component. If the selection is successful, an editor is launched
  for the selected component. If any key is pressed that is not part of the
  generation set, or if a nonexistent combination is entered, editing is
  cancelled, and the application returns to the default state.
- `c`: Reset the visibility of each calendar to the default state.
- `r`: Reload all the calendars from disk.
- `p`: Toggle whether private components are hidden.
- `ih`/`ij`/`ik`/`il`: Switch the calendar view mode to months/weeks/days/day.
  Note, that if the second key of the combination is incorrect, it is ignored,
  and the key combination is cancelled.
- `1`-`9`: Switch to the corresponding filter view.
- `F1`-`F9`: Toggle the visibility of the corresponding calendar. If the `Shift`
  key is being held, 9 is added to the number. (So for example, while `Shift` is
  being held, `F1` corresponds to calendar 10.)

# The graphical interface
The application attempts to fill the available rectangular area as efficiently
as possible, but of course there is much room for improvement here. It
dynamically responds to the view being resized, on backends where resizing is
supported.

## Layout
There is a fixed 120px wide sidebar on the left, listing the calendars and
their visibility status. Below the calendars, the filters are listed, with the
active one being highlighted. Below is a small summary of the keybindings.

The rest of the space is filled by the active view: either the calendar, or the
todo view.

## Calendar view
The calendar view represents an interval of time. It is displayed as vertical
slices, with headers at the top describing each slice. (The description is
usually the month, the week, or the day.)

The same amount for each slice may be chopped off from the bottom or the top.
This is dynamically changed, so that the chopping is the largest possible
without covering any object in any of the slices.

Object are displayed as rectangles in the slices. The top and the bottom of the
rectangle represent the interval. The width of the slice is divided between
overlapping objects.

Objects have the following attributes:
- interval
- fill color
- fading: Whether the object is drawn as a transparent box.
- hiding: Whether to NOT draw text on the box.
- visibility: Whether to NOT draw the object at all.

There are currently two types of objects displayed: events, and todo
suggestions. Events are displayed with their specified color, or if none
specified, blue. Todo suggestions are faded, and their text is prefixed with
`TODO:`. Currently, there is no way to disable todo suggestions.

If an object spans more than a whole slice, it is displayed horizontally in a
separate area between the other slices, and the header.

The current time is marked with a red line.

## Todo view
The todos are listed line by line. They are sorted by the priority sorting
algorithm. Each one is 40px high, but this is expanded, if the text would not
fit otherwise. From left to right, the summary, the description, and the timing
information is displayed. (The description section is omitted if there is no
description.) The timing information includes the due date and the estimated
duration. The whole todo item is grayed out if the current time is before the
start date of the todo. The background of the timing information section is
filled with red if the todo is overdue. If the percent complete property is
set, a corresponding area of the summary and description sections is filled
with a solid color.
