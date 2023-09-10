// Highlighting 'matched' elements in IR code
function addHighlight(selector) {
    var matchedElements = document.querySelectorAll(selector + ' .matched');

    matchedElements.forEach(function(element) {
        element.addEventListener('mouseover', function() {
            var idPrefix = this.id.split('-')[0];
            var matchingElements = document.querySelectorAll(selector + ' .matched[id^="' + idPrefix + '-"]');

            matchingElements.forEach(function(matchingElement) {
                matchingElement.classList.add('Highlight');
            });
        });

        element.addEventListener('mouseout', function() {
            var idPrefix = this.id.split('-')[0];
            var matchingElements = document.querySelectorAll(selector + ' .matched[id^="' + idPrefix + '-"]');

            matchingElements.forEach(function(matchingElement) {
                matchingElement.classList.remove('Highlight');
            });
        });
    });
}

// Example usage:
addHighlight('#ir-code-tab');
addHighlight('#device-code-tab');



/* Scroll to code from visualization */
function scrollToCode(id) {
    var container = document.getElementById('ir-code-tab');
    var scrollToObject = document.getElementById(id);
    makeCodeVisible(scrollToObject);
    container.scrollTo({
        top: scrollToObject.offsetTop,
        behavior: 'smooth'
    });
    scrollToObject.style.backgroundColor = 'lightgray';
    setTimeout(function () {
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
            console.log(event.target);

            currentResizer = event.target;
            currentResizerIndex = resizeBars.indexOf(currentResizer);
            var rect = currentResizer.getBoundingClientRect();
            mousedown = true;
            resizerInitCenter = event.x;
            resizerPreviewX = event.x;
            resizerPreview.style.left = resizerInitCenter + 'px';
            resizerPreview.style.display = 'block';
        }
    });
}

document.addEventListener('mouseup', (event) => {
    if (mousedown) {

        currentResizer = null;
        mousedown = false;
        resizerPreview.style.display = 'none';

        // Gather tab widths
        var tabs = Array.from(document.querySelectorAll('div.tab'));
        var widths = tabs.map(function(tab) {
            return tab.getBoundingClientRect().width;
        });


        // Adjust tab widths
        var diff = resizerPreviewX - resizerInitCenter;
        console.log(diff);
        var currentIndex = currentResizerIndex;
        while (currentIndex >= 0 && tabs[currentIndex].classList.contains('collapsed-tab')) {
            currentIndex--
        }
        widths[currentIndex] += diff;

        currentIndex = currentResizerIndex + 1;
        while (currentIndex < tabs.length && tabs[currentIndex].classList.contains('collapsed-tab')) {
            currentIndex++;
        }
        widths[currentIndex] -= diff;

        // Apply tab widths
        tabs.forEach(function(tab, index) {
            if (widths[index] >= 0) {
                tab.style.width = widths[index] + 'px';
            } else {
                tab.style.width = '0px'; // or any other default value you want to set
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


function collapse_tab(index) {
    var tabs = document.getElementById("visualization-tabs");
    var tab = tabs.firstElementChild.nextElementSibling;
    for (var i = 0; i < index; ++i) {
        tab = tab.nextElementSibling.nextElementSibling;
    }

    tab.classList.toggle('collapsed-tab');
}

function scrollToHostAsm(lno) {
    var asmContent = document.getElementById("assemblyContent");
    var line_height = window.getComputedStyle(asmContent).getPropertyValue("line-height");
    line_height = parseInt(line_height, 10);
    document.getElementById("host-assembly-tab").scrollTo({
        behavior: "smooth",
        top: lno * line_height
    });
}

function scrollToDeviceCode(lno) {
    var device_code_tab = document.getElementById("device-code-tab");
    const lineSpans = device_code_tab.querySelectorAll('span.line');
    line = lineSpans[lno - 1]
    line.scrollIntoView({
        behavior: "smooth",
    });
}
