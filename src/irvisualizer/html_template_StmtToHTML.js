// /* Highlighting 'matched' elements in IR code */
// $('#ir-code-tab .matched').each(function () {
//     this.onmouseover = function () {
//         $('#ir-code-tab .matched[id^=' + this.id.split('-')[0] + '-]').addClass('Highlight');
//     }
//     this.onmouseout = function () {
//         $('#ir-code-tab .matched[id^=' + this.id.split('-')[0] + '-]').removeClass('Highlight');
//     }
// });


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
addHighlight('#ptx-assembly-tab');



/* Expand/Collapse buttons in IR code */
function toggle(id) {
    e = document.getElementById(id);
    e_cb = document.getElementsByClassName("cb-" + id)[0];
    show = document.getElementById(id + '-show');
    hide = document.getElementById(id + '-hide');
    ccost_btn = document.getElementById("cc-" + id);
    dcost_btn = document.getElementById("dc-" + id);
    ccost_tt = document.getElementById("tooltip-cc-" + id);
    dcost_tt = document.getElementById("tooltip-dc-" + id);
    if (e.classList.contains("collapsed-block")) {
        e.classList.remove("collapsed-block");
        e_cb.classList.add("ClosingBrace");
        show.style.display = 'none';
        hide.style.display = 'block';
        if (ccost_btn && dcost_tt) {
            // Update cost indicators
            ccost_color = ccost_btn.getAttribute('line-cost-color');
            dcost_color = dcost_btn.getAttribute('line-cost-color');
            ccost_btn.className = ccost_btn.className.replace(/CostColor\d+/, 'CostColor' + ccost_color);
            dcost_btn.className = dcost_btn.className.replace(/CostColor\d+/, 'CostColor' + dcost_color);
            // Update cost tooltips
            ccost = ccost_btn.getAttribute('line-cost');
            dcost = dcost_btn.getAttribute('line-cost');
            ccost_tt.innerText = 'Op Count: ' + ccost;
            dcost_tt.innerText = 'Bits Moved: ' + dcost;
        }
    } else {
        e.classList.add("collapsed-block");
        e_cb.classList.remove("ClosingBrace");
        show.style.display = 'block';
        hide.style.display = 'none';
        if (ccost_btn && dcost_tt) {
            // Update cost indicators
            collapsed_ccost_color = ccost_btn.getAttribute('block-cost-color');
            collapsed_dcost_color = dcost_btn.getAttribute('block-cost-color');
            ccost_btn.className = ccost_btn.className.replace(/CostColor\d+/, 'CostColor' + collapsed_ccost_color);
            dcost_btn.className = dcost_btn.className.replace(/CostColor\d+/, 'CostColor' + collapsed_dcost_color);
            // Update cost tooltips
            collapsed_ccost = ccost_btn.getAttribute('block-cost');
            collapsed_dcost = dcost_btn.getAttribute('block-cost');
            ccost_tt.innerText = 'Op Count: ' + collapsed_ccost;
            dcost_tt.innerText = 'Bits Moved: ' + collapsed_dcost;
        }
    }
    return false;
}

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

// In case the code we are scrolling to code that sits within
// a collapsed code block, uncollapse it
function makeCodeVisible(element) {
    if (!element) return;
    if (element == document) return;
    if (element.classList.contains("collapsed-block")) {
        toggle(element.id);
    }
    makeCodeVisible(element.parentNode);
}


var currentResizer;
var mousedown = false;

var resizeBars = document.querySelectorAll('div.resize-bar');

for (var i = 0; i < resizeBars.length; i++) {
    resizeBars[i].addEventListener('mousedown', (event) => {
        currentResizer = event.target;
        mousedown = true;
    });
}

document.addEventListener('mouseup', (event) => {
    currentResizer = null;
    mousedown = false;
});

document.addEventListener('mousemove', (event) => {
    if (mousedown) {
        resize(event);
    }
});


function resize(e) {
    const size = `${e.x}px`;
    var rect = currentResizer.getBoundingClientRect();
    // TODO resize
}

function collapse_tab(index) {
    var tabs = document.getElementById("visualization-tabs");
    var tab = tabs.firstElementChild;
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
