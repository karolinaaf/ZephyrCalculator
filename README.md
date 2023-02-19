# a simple calculator running on Zephyr in simulated target

User input is a text describing the required operation e.g. “2 + 6 * 6 =”. The target parses this string, performs necessary mathematical operations and returns a text representation of the result. In case of the example above, “38” will be returned.

Invalid inputs are handled properly. E.g. “2 a 2” will return “invalid input” result.

The application runs in an infinite loop until stopped by the user using dedicated `exit` input;

Implemented calculator functionality is addition, subtraction, multiplication, integer division and parentheses.

Usage
---
To build the project, run:
```
west build -p always -b qemu_cortex_m3 ZephyrCalculator
```
To run it:
```
west build -t run
```

Example output:

![Example output](output.png?raw=true "Example output")
