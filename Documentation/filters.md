# Filtering expressions
The filtering expression is evaluated for each active calendar component about
to be displayed to the user.

## API description

The following variables provide information about the current component, but
can not be modified.
- `$ev`: A boolean specifying whether the component is an event.
- `$sum`: A string containing the summary property of the component.
- `$color`: A string containing the color property of the component.
- `$loc`: A string containing the location property of the component.
- `$desc`: A string containing the description property of the component.
- `$st`: A string representing the status property of the component.
- `$clas`: A string representing the class property of the component.
- `$cats`: A list representing the categories property of the component.
- `$cal`: A string representing the (1-based) index of the calendar that
  contains the component.

The following variables provide information about global application state.
(They can not be modified.)
- `$show_priv`: Whether the user wants private components to be displayed.
- `$vis_cals`: A list representing the currently visible calendars.

The following properties can be set for each component, but they can not be read
from.
- `$fade`: A boolean representing whether to display this component with reduced
  opacity to the user.
- `$hide`: A boolean representing whether to hide any text information on this
  component from the user.
- `$vis`: A boolean representing whether to display the componant at all to the
  user.

## Config file
The config file is evaluated once upon application startup. Any functions
defined with `def` will be treated as filters selectable by the user. Note that
since you can use any string as function name, you should use it to provide an
appropriate description about the filter to the user.
