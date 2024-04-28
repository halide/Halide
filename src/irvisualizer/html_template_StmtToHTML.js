// Highlighting 'matched' elements in IR code
function addHighlight(selector) {
    let matchedElements = document.querySelectorAll(selector + ' .matched');

    matchedElements.forEach(function(element) {
        element.addEventListener('mouseover', function() {
            let idPrefix = this.id.split('-')[0];
            let matchingElements = document.querySelectorAll(selector + ' .matched[id^="' + idPrefix + '-"]');

            matchingElements.forEach(function(matchingElement) {
                matchingElement.classList.add('Highlight');
            });
        });

        element.addEventListener('mouseout', function() {
            let idPrefix = this.id.split('-')[0];
            let matchingElements = document.querySelectorAll(selector + ' .matched[id^="' + idPrefix + '-"]');

            matchingElements.forEach(function(matchingElement) {
                matchingElement.classList.remove('Highlight');
            });
        });
    });

    // The number of classes defined in the CSS:
    let highlightInUse = new Array(7).fill(false);

    // Variables are in <b> tags.
    matchedElements = document.querySelectorAll(selector + ' b.matched');
    matchedElements.forEach(function(element) {
        element.addEventListener('click', function(event) {
            let idPrefix = this.id.split('-')[0];
            let matchingElements = document.querySelectorAll(selector + ' b.matched[id^="' + idPrefix + '-"]');

            const classes = Array.from(element.classList).filter(className => className.startsWith("HighlightToggle"));
            if (classes.length === 1) {
                let className = classes[0];
                let highlightIdx = parseInt(className.substr("HighlightToggle".length));
                highlightInUse[highlightIdx] = false;
                matchingElements.forEach(function(matchingElement) {
                    matchingElement.classList.remove(className);
                });
            } else {
                let highlightIdx = highlightInUse.indexOf(false);
                if (highlightIdx != -1) {
                    highlightInUse[highlightIdx] = true;
                    matchingElements.forEach(function(matchingElement) {
                        matchingElement.classList.add('HighlightToggle' + highlightIdx);
                    });
                }
            }
            event.preventDefault();
        });
    });
}

// Example usage:
addHighlight('#ir-code-pane');
addHighlight('#device-code-pane');

/* Scroll to code programmatically. Unusued at the moment, but could be great for jumping back from assembly/device code to the stmt. */
function scrollToCode(id) {  // eslint-disable-line no-unused-vars
    let container = document.getElementById('ir-code-pane');
    let scrollToObject = document.getElementById(id);
    // makeCodeVisible(scrollToObject);
    container.scrollTo({
        top : scrollToObject.offsetTop,
        behavior : 'smooth'
    });
    scrollToObject.style.backgroundColor = 'lightgray';
    setTimeout(function() {
        scrollToObject.style.backgroundColor = 'transparent';
    }, 1000);
}

var currentResizer;
var currentResizerIndex;
var mousedown = false;

var resizeBars = Array.from(document.querySelectorAll('div.resize-bar'));
var resizerInitCenter = 0;
var resizerPreview = document.getElementById('resizer-preview');
var resizerPreviewX;

for (var i = 0; i < resizeBars.length; i++) {
    resizeBars[i].addEventListener('mousedown', (event) => {
        document.body.classList.add("no-select");
        if (event.target.classList.contains("resize-bar")) {
            currentResizer = event.target;
            currentResizerIndex = resizeBars.indexOf(currentResizer);
            mousedown = true;
            resizerInitCenter = event.x;
            resizerPreviewX = event.x;
            resizerPreview.style.left = resizerInitCenter + 'px';
            resizerPreview.style.display = 'block';
        }
    });
}

document.addEventListener('mouseup', () => {
    if (mousedown) {
        currentResizer = null;
        mousedown = false;
        resizerPreview.style.display = 'none';

        // Gather pane widths
        var panes = Array.from(document.querySelectorAll('div.pane'));
        var widths = panes.map(function(pane) {
            return pane.getBoundingClientRect().width;
        });

        // Adjust pane widths
        var diff = resizerPreviewX - resizerInitCenter;
        var currentIndex = currentResizerIndex;
        while (currentIndex >= 0 && panes[currentIndex].classList.contains('collapsed-pane')) {
            currentIndex--
        }
        widths[currentIndex] += diff;

        currentIndex = currentResizerIndex + 1;
        while (currentIndex < panes.length && panes[currentIndex].classList.contains('collapsed-pane')) {
            currentIndex++;
        }
        widths[currentIndex] -= diff;

        // Apply pane widths
        panes.forEach(function(pane, index) {
            if (widths[index] >= 0) {
                pane.style.width = widths[index] + 'px';
            } else {
                pane.style.width = '0px';  // or any other default value you want to set
            }
        });
    }

    document.body.classList.remove("no-select");
});

document.addEventListener('mousemove', (event) => {
    if (mousedown) {
        resizerPreview.style.left = event.x + 'px';
        resizerPreviewX = event.x;
    }
});

function collapseTab(index) {  // eslint-disable-line no-unused-vars
    var panes = document.getElementById("visualization-panes");
    var pane = panes.firstElementChild.nextElementSibling;
    for (var i = 0; i < index; ++i) {
        pane = pane.nextElementSibling.nextElementSibling;
    }

    pane.classList.toggle('collapsed-pane');
    if (index > 0) { // left resizer
        let resizer = panes.firstElementChild;
        for (let i = 0; i < index; ++i) {
            resizer = resizer.nextElementSibling.nextElementSibling;
        }
        if (resizer !== null) {
            let colRightBtn = resizer.firstElementChild.firstElementChild.nextElementSibling.firstElementChild;
            colRightBtn.classList.toggle('active');
        }
    }

    { // right resizer
        let resizer = panes.firstElementChild;
        for (let i = 0; i <= index; ++i) {
            if (resizer !== null) resizer = resizer.nextElementSibling;
            if (resizer !== null) resizer = resizer.nextElementSibling;
        }
        if (resizer !== null) {
            let colLeftBtn = resizer.firstElementChild.firstElementChild.firstElementChild;
            colLeftBtn.classList.toggle('active');
        }
    }
}

function scrollToHostAsm(lno) {  // eslint-disable-line no-unused-vars
    let asmContent = document.getElementById("assemblyContent");
    let line_height = window.getComputedStyle(asmContent).getPropertyValue("line-height");
    line_height = parseInt(line_height, 10);
    document.getElementById("host-assembly-pane").scrollTo({
        behavior : "smooth",
        top : lno * line_height
    });
}

function scrollToDeviceCode(lno) {  // eslint-disable-line no-unused-vars
    let device_code_pane = document.getElementById("device-code-pane");
    let lineSpans = device_code_pane.querySelectorAll('span.line');
    if (lno - 1 < lineSpans.length) {
        let line = lineSpans[lno - 1]
        line.scrollIntoView({
            behavior : "smooth"
        });

        line.style.backgroundColor = 'lightgray';
        setTimeout(function() {
            line.style.backgroundColor = 'transparent';
        }, 1000);
    } else {
        console.error("Jump to device code references line number " + lno + ", which is out or range of the " + lineSpans.length + " lines.");
    }
}
