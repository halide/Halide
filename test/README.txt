The iterface to tests is defined by a few conventions. So far:

- <test>/<test>.json describes the essential mechanisms for running and validating the test. Cf. brightness/brightness.json for reference. @TODO: document these here

Output is stored in:

- <test>.log - stdout from the run
- <test>.png - the resulting image
- <test>.time - the time to run, in fractional seconds (plaintext)
