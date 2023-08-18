# Javascript toolchain for IRVisualizer: the single page web application

## Features:

* To trigger Github's online Dependabot alerts for security vulnerabilities.

* To enable static analyzer for the Javascript/ES6 language.

* To visualize the Halide IR and the assembly code interactively
with a web browser.

## Usage:

* Execute `yarn install` to scan for broken Javascript dependencies.

* (To be completed) execute `yarn run lint` to scan the main script for potential syntax errors and
  bugs.

## Recommended Makefile build rules

```make
ESBUILD=node_modules/.bin/esbuild
ESLINT=node_modules/.bin/eslint

help:  ## Display this help
	@awk 'BEGIN {FS = ":.*##"; printf "\nUsage:\n  make \033[36m<target>\033[0m\n\nTargets:\n"} /^[a-zA-Z_-]+:.*?##/ { printf "  \033[36m%-10s\033[0m %s\n", $$1, $$2 }' $(MAKEFILE_LIST)

all: depend build    ## Download dependencies and build everything

depend:    ## Download Nodejs dependencies
	yarn install

watch:    ## Debug mode, monitor file changes and then compile automatically
	yarn run watch

build:   ## Release mode, optimize and minimize the Javascript file
	yarn run build

lint:    ## Run static analyzer on the main javascript
	yarn run lint

.PHONY: depend watch build lint
```

## Development roadmap

* [ ] Rename the extensions from `*.template.html` to `template/*.[js|css|html]`.

* [ ] Modernize the main script to ES6.

* [ ] Import all dependencies with the `import` statement, not via the `<script>` tag.

* [ ] Resolve all static analyzer warnings, e.g. "buttonHide" is already defined, "btns.size()" is undefined, etc.

* [ ] Invoke `make build` to bundle all dependencies, stylesheets, and
  javascripts from the `template/` folder to the output folder
  `distribution/bundle.[js|css]`; to embed all bundled code into the single HTML
  file, so that users can browse the IRVisualizer output without Internet access.
