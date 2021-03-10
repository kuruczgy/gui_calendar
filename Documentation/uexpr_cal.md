# Introduction
This document describes the various extensions for the core `uexpr` language
for interaction with the calendar application.

# Environment
Each evaluation of an expression is associated with a combination of contexts.
Each context defines the different variables and functions it makes available.

# Enumerations
Enumerations are represented by values of type `string`. When we say that
something must evaluate to an enumeration, we mean that it must evaluate to a
value of type `string`, that is exactly one of the elements of the enumeration.

## `enum view`
Represents a graphical view in the application.

values: "cal", "todo"
## `enum comp_type`
Represents a component type.

values: "event", "todo"

# Config context
This context is used when evaluating a top-level config file.

The following functions are available:
## `add_cal`
Expects 2 arguments. The first must be a variable, the second must evaluate to
a `string`. The calendar is loaded from the implementation defined path
specified by the second argument, and the variable given as the first argument
is *set* to a `nativeobj` representing the calendar.
## `add_filter`
Expects 3 arguments.

The first argument must evaluate to a `string`. It represents a name given to
the filter, and may be displayed to the user.

The second argument must evaluate to a `nativeobj`, obtained from some previous
call to `add_cal`.

The third argument represents the body of the filter, and will be evaluated (in
a filter context) each time the filter is executed.
## `add_action`
Expects 2 or 3 arguments.

The first argument must evaluate to a `list`, describing parameters for the
action. The list must have at least one item, but the rest of the items are
optional. It's first item must be a `string` representing a key symbol. The
second item may be a `string` representing a descriptive label for the action.

The second argument represents the body of the action, and will be evaluated
(in an action context) each time the action is executed.

The third argument, if present, must evaluate to `enum view`. It represents the
view where this application will be available to the user.
## `include`
Expects 1 argument. It must evaluate to a `string`. A `uexpr` file is then
loaded from the implementation defined path specified by the argument, and is
executed as a top-level config expression.
## `set_alarm`
Expects 2 arguments. The first argument must evaluate to `string`, and
represents an implementation defined command to execute when an alarm is
triggered. The second argument is evaluated (in filter context) for each
component to determine whether they should trigger an alarm.
## `set_timezone`
Expects one argument. It must evaluate to a `string` that represents a timezone
in an implementation defined manner.

# Filter context
Each evaluation in a filter context is associated with a calendar component.

## Variables available for *getting*
- `$ev`: A `boolean` specifying whether the component is an event.
- `$sum`: A `string` containing the summary property of the component.
- `$color`: A `string` containing the color property of the component.
- `$loc`: A `string` containing the location property of the component.
- `$desc`: A `string` containing the description property of the component.
- `$st`: A `string` representing the status property of the component.
- `$clas`: A `string` representing the class property of the component.
- `$cats`: A `list` representing the categories property of the component.
- `$cal`: A `nativeobj` representing the calendar that contains the component.
- `$last_mod_today`: A `boolean` representing whether the component was last
  modified on the current day.

## Variables available for *getting* and *setting*
- `$fade`: A `boolean` representing whether to display this component with
  reduced opacity to the user.
- `$hide`: A `boolean` representing whether to hide any text information on
  this component from the user.
- `$vis`: A `boolean` representing whether to display the componant at all to
  the user.

Setting any of the  variables below signals a request to edit the component.
Whether the component will actually be edited is up to whoever requested the
evaluation.
- `$st`: A `string` representing the status property of the component.

# Action context

The following functions are available:
## `switch_view`
Expects 1 argument that must evaluate to `enum view`. It represents the view
that will be switched to.
## `move_view_discrete`
Expects 1 argument that must evaluate to a `string`. This string will be parsed
as signed a decimal number. The calendar view is moved in a number of discrete
steps specified by the number.
## `view_today`
Expects no arguments. Moves the calendar view so that it includes the current
point in time.
## `launch_editor`
If called with no arguments, it must be called in a filter context. In this
case, it requests the user to edit the component associated with the filter
context.

If given 1 argument, it must evaluate to `enum comp_type`. In this case, it
requests the user to create a new component of the specified kind.
## `select_comp`
Expects 3 arguments.

The first argument must evaluate to `enum comp_type`. The user will be
requested to select a component of this kind.

The second argument must evaluate to a `string`. It represents a message that
may be displayed to the user as part of the selection request.

The third argument will be evaluated (in both an action and filter context) if,
and when the user has selected a component.
