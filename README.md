# Slightly More Usable Calendar (SMUC)
SMUC is a GUI calendar application, capable of handling calendar events and
todos. It still is in early development stage, and probably remain for the
foreseeable future.

## Features
- Uses the [iCalendar](https://tools.ietf.org/html/rfc5545) format for storage.
- Fully keyboard driven.
- Fast, does not impede your workflow.
- An optimized DSL is used for editing your events and todos, with your favorite
  text editor.
- Can handle many calendars at once. (Simply specify them on the command line.)
- Automatically suggests doing todos in the free time between your confirmed
  events.

### Supported iCalendar features
- SUMMARY, DESCRIPTION, LOCATION, DTSTART, DTEND, DUE, CATEGORIES, RELATED-TO,
  LAST-MODIFIED
- CLASS, STATUS
- [COLOR](https://tools.ietf.org/html/rfc7986#section-5.9)
- PERCENT-COMPLETE
- [ESTIMATED-DURATION](https://tools.ietf.org/html/draft-apthorp-ical-tasks-01#section-6.1)
- [DEPENDS-ON](https://tools.ietf.org/html/draft-ietf-calext-ical-relations-05#section-10.4)
  RELTYPE value
- partial support for viewing recurring events (RRULE, but no RDATE or EXDATE)

## Dependencies
- `ds`
- `libtouch`
- `mgu`
- `platform_utils`

## Compilation
- Place dependencies into the `subprojects` directory.
- `meson build`
- `ninja -C build`

## Usage
You can get help with the command line options with the `-h` switch.
The keybindings are listed in the sidebar when you launch the application.

## Contributing
[Send me a patch.](mailto:kuruczgyurci@hotmail.com)

## License
GPL
