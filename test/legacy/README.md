Legacy tests
-------------
_There is some older support for tests outside `cpp`. You probably don't want to use this unless you're working on new testing infrastructure in the core compiler that can't be tested through the C++ front end for some reason._

The iterface to tests is defined by a few conventions. So far:

- `<test>/<test>.json` describes the essential mechanisms for running and validating the test. Cf. brightness/brightness.json for reference. @TODO: document these here

Output is stored in:

- `<test>.log` - stdout from the run
- `<test>.png` - the resulting image
- `<test>.time` - the time to run, in fractional seconds (plaintext)
