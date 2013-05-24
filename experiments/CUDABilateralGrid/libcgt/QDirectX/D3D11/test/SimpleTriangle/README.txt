SimpleTriangle is a test application for QD3D11, mainly for verifying that the
development environment has been set up correctly.  If it compiles and runs, two
Qt windows should appear.  One with a rotating red-green-blue triangle and a
second with a "rotate" button.  Clicking the button toggles the rotation on and
off.

Building:

- The environment variable QTDIR must be set.
- The DirectX 11 Effects framework must be compiled.  It can be found under
  $(DXSDK_DIR)/Samples.

Running:

- $(QTDIR)/bin must be in the current path.
- simple.fx must be in the working directory.
