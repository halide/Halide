/* Highlighting 'matched' elements in IR code */
$('#ir-code-tab .matched').each(function () {
    this.onmouseover = function () {
        $('#ir-code-tab .matched[id^=' + this.id.split('-')[0] + '-]').addClass('Highlight');
    }
    this.onmouseout = function () {
        $('#ir-code-tab .matched[id^=' + this.id.split('-')[0] + '-]').removeClass('Highlight');
    }
});

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

/* Resizing visualization tabs */
var codeDiv = document.getElementById('ir-code-tab');
var resizeBar = document.getElementById('resize-bar-1');
var irVizDiv = document.getElementById('ir-visualization-tab');
var resizeBarAssembly = document.getElementById('resize-bar-2');
var assemblyCodeDiv = document.getElementById('assembly-tab');

codeDiv.style.flexGrow = '0';
resizeBar.style.flexGrow = '0';
irVizDiv.style.flexGrow = '0';
resizeBarAssembly.style.flexGrow = '0';
assemblyCodeDiv.style.flexGrow = '0';

codeDiv.style.flexBasis = 'calc(50% - 6px)';
resizeBar.style.flexBasis = '6px';
irVizDiv.style.flexBasis = 'calc(50% - 3px)';
resizeBarAssembly.style.flexBasis = '6px';

var currentResizer;
var mousedown = false;
resizeBar.addEventListener('mousedown', (event) => {
    currentResizer = resizeBar;
    mousedown = true;
});
resizeBarAssembly.addEventListener('mousedown', (event) => {
    currentResizer = resizeBarAssembly;
    mousedown = true;
});
document.addEventListener('mouseup', (event) => {
    currentResizer = null;
    mousedown = false;
});

document.addEventListener('mousemove', (event) => {
    if (mousedown) {
        if (currentResizer == resizeBar) {
            resize(event);
        } else if (currentResizer == resizeBarAssembly) {
            resizeAssembly(event);
        }
    }
});


function resize(e) {
    if (e.x < 25) {
        collapse_code_tab();
        return;
    }

    const size = `${e.x}px`;
    var rect = resizeBarAssembly.getBoundingClientRect();

    if (e.x > rect.left) {
        collapseR_visualization_tab();
        return;
    }

    codeDiv.style.display = 'block';
    irVizDiv.style.display = 'block';
    codeDiv.style.flexBasis = size;
    irVizDiv.style.flexBasis = `calc(${rect.left}px - ${size})`;
}

function resizeAssembly(e) {
    if (e.x > screen.width - 25) {
        collapse_assembly_tab();
        return;
    }

    var rect = resizeBar.getBoundingClientRect();

    if (e.x < rect.right) {
        collapseL_visualization_tab();
        return;
    }

    const size = `${e.x}px`;
    irVizDiv.style.display = 'block';
    assemblyCodeDiv.style.display = 'block';
    irVizDiv.style.flexBasis = `calc(${size} - ${rect.right}px)`;
    assemblyCodeDiv.style.flexBasis = `calc(100% - ${size})`;

}

function collapse_tab(index) {
    var tabs = document.getElementById("visualization-tabs");
    var tab = tabs.firstElementChild;
    for (var i = 0; i < index; ++i) {
        tab = tab.nextElementSibling.nextElementSibling;
    }

    tab.style.display = 'none';
}

function collapse_code_tab() {
    irVizDiv.style.display = 'block';
    var rect = resizeBarAssembly.getBoundingClientRect();
    irVizDiv.style.flexBasis = `${rect.left}px`;
    codeDiv.style.display = 'none';
}

function collapse_assembly_tab() {
    irVizDiv.style.display = 'block';
    var rect = resizeBar.getBoundingClientRect();
    irVizDiv.style.flexBasis = `calc(100% - ${rect.right}px)`;
    assemblyCodeDiv.style.display = 'none';
}

function scrollToAsm(lno) {
    var asmContent = document.getElementById("assemblyContent");
    var line_height = window.getComputedStyle(asmContent).getPropertyValue("line-height");
    line_height = parseInt(line_height, 10);
    document.getElementById("assembly-tab").scrollTo({
        behavior: "smooth",
        top: lno * line_height
    });
}

// Instead of calling 'collapse_assembly_tab();' we use the
// last bit of it, which skips a whole Layout Recalculation.
assemblyCodeDiv.style.display = 'none';

// Cost model js
var re = /(?:\-([^-]+))?$/;
var cost_btns = $('div[id^="cc-"], div[id^="dc-"]');
for (var i = 0; i < cost_btns.size(); i++) {
    const button = cost_btns[i];
    const highlight_span = document.getElementById("cost-bg-" + re.exec(button.id)[1]); // span#
    $(button).mouseover(() => {
        $(highlight_span).css("background", "#e5e3e3");
    });
    $(button).mouseout(() => {
        $(highlight_span).css("background", "none");
    });
}
