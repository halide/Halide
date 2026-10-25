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

addHighlight('#device-code-pane');

/* Scroll to code programmatically. Unused at the moment, but could be great for jumping back from assembly/device code to the stmt. */
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
            let colRightBtn = resizer
                .firstElementChild
                .firstElementChild
                .nextElementSibling
                .firstElementChild;
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

/* The IR code: one div.l per line of the stmt, with its line number in
 * data-line. Lines that open a block of code have the line that closes it in
 * data-block-end. */
const irPane = document.getElementById('ir-code-pane');
const irLines = Array.from(irPane.querySelectorAll('div.l'));

// Collapsing blocks of code. A line can be hidden by several collapsed
// blocks, data-hidden counts how many.
const collapsedBlocks = new Set();
function toggleBlock(header) {
    const line = parseInt(header.dataset.line);
    const end = parseInt(header.dataset.blockEnd);
    const collapse = !collapsedBlocks.has(line);
    if (collapse) {
        collapsedBlocks.add(line);
    } else {
        collapsedBlocks.delete(line);
    }
    header.classList.toggle('collapsed', collapse);
    for (let l = line + 1; l < end; l++) {
        const el = irLines[l - 1];
        el.dataset.hidden = parseInt(el.dataset.hidden || '0') + (collapse ? 1 : -1);
    }
}
function revealIRLine(line) {
    for (const h of Array.from(collapsedBlocks)) {
        const header = irLines[h - 1];
        if (h < line && line < parseInt(header.dataset.blockEnd)) {
            toggleBlock(header);
        }
    }
}
function selectIRLine(line) {
    irPane.querySelectorAll('div.l.selected').forEach(el => el.classList.remove('selected'));
    if (line > 0 && line <= irLines.length) {
        irLines[line - 1].classList.add('selected');
    }
}

/* The host assembly. asmIRLines holds, for each shown line of assembly, the
 * line of IR it was generated for (or 0). */
const asmPane = document.getElementById('host-assembly-pane');
const asmCode = document.getElementById('asm-code');
const asmHighlights = document.getElementById('asm-highlights');
const asmOfIRLine = new Map();
asmIRLines.forEach((line, i) => {
    if (line) {
        if (!asmOfIRLine.has(line)) {
            asmOfIRLine.set(line, []);
        }
        asmOfIRLine.get(line).push(i);
    }
});
// The assembly is rendered in rows of a fixed height, only those near the
// view, which keeps the page small and fast.
const asmContent = document.getElementById('assemblyContent');
const asmRowHeight = 14;
const asmLineNumbers = [];
{
    let n = 0;
    for (const step of asmLineSteps) {
        n += step;
        asmLineNumbers.push(n);
    }
}
function asmLineHeight() {
    return asmRowHeight;
}

function escapeHTML(text) {
    return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

// Syntax highlights a line of assembly, with the classes of the speed-highlight
// library that the stylesheet colors.
function asmToHTML(line) {
    const span = (cls, text) => '<span class="shj-syn-' + cls + '">' + escapeHTML(text) + '</span>';
    // Split off the comment, which is not inside a string.
    let code = line, comment = '';
    for (let i = 0; i < line.length; i++) {
        if (line[i] === '"') {
            const j = line.indexOf('"', i + 1);
            i = j < 0 ? line.length : j;
        } else if (line.startsWith(asmSyntax.comment, i)) {
            code = line.slice(0, i);
            comment = line.slice(i);
            break;
        }
    }
    const isWord = c => /[\w.$]/.test(c);
    const isRegister = w => {
        if (asmSyntax.x86) {
            return false;  // x86 registers start with %.
        }
        return /^(sp|wsp|xzr|wzr|lr|fp|pc)$/.test(w) || /^[a-z]{1,2}\d+(\..*)?$/.test(w);
    };
    const parts = /^(\s*)(\S*)(.*)$/s.exec(code);
    let out = parts[1];
    const head = parts[2], tail = parts[3];
    if (head) {
        out += span(head.endsWith(':') ? 'label' : head[0] === '.' ? 'asm-directive' : 'instruction', head);
    }
    // The operands.
    for (let i = 0; i < tail.length;) {
        const c = tail[i];
        let j = i + 1;
        if (c === '"') {
            const end = tail.indexOf('"', i + 1);
            j = end < 0 ? tail.length : end + 1;
            out += span('str', tail.slice(i, j));
        } else if (c === '%' || c === asmSyntax.immediate || isWord(c) || c === '-') {
            while (j < tail.length && (isWord(tail[j]) || (c !== '%' && j === i + 1 && tail[j] === '-'))) {
                j++;
            }
            const w = tail.slice(i, j);
            if (c === '%') {
                out += span('register', w);
            } else if (c === asmSyntax.immediate || /\d/.test(c) || (c === '-' && w.length > 1)) {
                out += span('num', w);
            } else if (/^(\.L|\$L|LBB)/.test(w)) {
                out += span('label', w);
            } else if (isRegister(w)) {
                out += span('register', w);
            } else {
                out += escapeHTML(w);
            }
        } else {
            out += escapeHTML(c);
        }
        i = j;
    }
    if (comment) {
        out += span('cmnt', comment);
    }
    return out;
}

let asmRendered = [-1, -1];
function renderAsm() {
    const top = asmPane.scrollTop - asmCode.offsetTop;
    const first = Math.max(0, Math.floor(top / asmRowHeight) - 100);
    const last = Math.min(asmLines.length, Math.ceil((top + asmPane.clientHeight) / asmRowHeight) + 100);
    if (first === asmRendered[0] && last === asmRendered[1]) {
        return;
    }
    asmRendered = [first, last];
    let html = '';
    for (let i = first; i < last; i++) {
        html += '<div style="top:' + (i * asmRowHeight) + 'px"><span class="n">' + asmLineNumbers[i] + '</span>' +
                asmToHTML(asmLines[i]) + '</div>';
    }
    asmContent.innerHTML = html;
}
asmContent.style.height = (asmLines.length * asmRowHeight) + 'px';
asmPane.addEventListener('scroll', renderAsm);
window.addEventListener('resize', renderAsm);
renderAsm();
function highlightAsm(lines, cls) {
    asmHighlights.querySelectorAll('div.' + cls).forEach(el => el.remove());
    const lineHeight = asmLineHeight();
    for (const i of lines) {
        const el = document.createElement('div');
        el.className = 'asm-hl ' + cls;
        el.style.top = (i * lineHeight) + 'px';
        el.style.height = lineHeight + 'px';
        asmHighlights.appendChild(el);
    }
}
function scrollToHostAsm(i) {
    asmPane.scrollTo({
        behavior : 'instant',
        top : i * asmLineHeight() - asmPane.clientHeight / 3
    });
}

/* The device code. Its lines have their line number in data-n, and the line
 * of IR they were generated for in data-ir, if any. */
const devicePane = document.getElementById('device-code-pane');
const deviceOfIRLine = new Map();
if (devicePane) {
    devicePane.querySelectorAll('span.line[data-ir]').forEach(el => {
        const line = parseInt(el.dataset.ir);
        if (!deviceOfIRLine.has(line)) {
            deviceOfIRLine.set(line, []);
        }
        deviceOfIRLine.get(line).push(el);
    });
}
function highlightDevice(line, cls) {
    if (!devicePane) {
        return;
    }
    devicePane.querySelectorAll('span.line.' + cls).forEach(el => el.classList.remove(cls));
    for (const el of (deviceOfIRLine.get(line) || [])) {
        el.classList.add(cls);
    }
}
function scrollToDevice(line) {
    const lines = deviceOfIRLine.get(line) || [];
    if (!lines.length) {
        return;
    }
    // Expand the kernel it is in.
    for (let el = lines[0].parentElement; el && el !== devicePane; el = el.parentElement) {
        if (el.classList.contains('indent') && el.id) {
            const btn = document.getElementById('show-hide-btn-' + el.id);
            if (btn) {
                btn.checked = false;
            }
        }
    }
    devicePane.scrollTo({
        behavior : 'instant',
        top : lines[0].offsetTop - devicePane.clientHeight / 3
    });
}

// Hovering a line of IR shows its code, clicking its number goes there.
irPane.addEventListener('mouseover', function(event) {
    const op = event.target.closest('span.op[data-g]');
    if (op) {
        irPane.querySelectorAll('span.op[data-g="' + op.dataset.g + '"]').forEach(el => el.classList.add('Highlight'));
    }
    const l = event.target.closest('div.l');
    highlightAsm(l ? (asmOfIRLine.get(parseInt(l.dataset.line)) || []) : [], 'hover');
    highlightDevice(l ? parseInt(l.dataset.line) : 0, 'device-hover');
});
irPane.addEventListener('mouseout', function(event) {
    const op = event.target.closest('span.op[data-g]');
    if (op) {
        irPane.querySelectorAll('span.op[data-g="' + op.dataset.g + '"]').forEach(el => el.classList.remove('Highlight'));
    }
});
irPane.addEventListener('mouseleave', function() {
    highlightAsm([], 'hover');
    highlightDevice(0, 'device-hover');
});

// Clicking a variable highlights it everywhere, in one of a few colors.
const variableHighlights = new Map();
function toggleVariableHighlight(name) {
    let cls = variableHighlights.get(name);
    if (cls === undefined) {
        const used = new Set(variableHighlights.values());
        for (let i = 0; i < 7; i++) {
            if (!used.has('HighlightToggle' + i)) {
                cls = 'HighlightToggle' + i;
                break;
            }
        }
        if (cls === undefined) {
            return;
        }
        variableHighlights.set(name, cls);
    } else {
        variableHighlights.delete(name);
    }
    irPane.querySelectorAll('span.var').forEach(el => {
        if (el.textContent === name) {
            el.classList.toggle(cls);
        }
    });
}

irPane.addEventListener('click', function(event) {
    const l = event.target.closest('div.l');
    if (!l) {
        return;
    }
    if (event.target.classList.contains('t') && l.dataset.blockEnd) {
        toggleBlock(l);
    } else if (event.target.classList.contains('n') && (l.dataset.asm || l.dataset.device)) {
        const line = parseInt(l.dataset.line);
        const asm = asmOfIRLine.get(line) || [];
        selectIRLine(line);
        highlightAsm(asm, 'selected');
        if (asm.length) {
            scrollToHostAsm(asm[0]);
        }
        highlightDevice(line, 'device-selected');
        scrollToDevice(line);
    } else if (event.target.classList.contains('var')) {
        toggleVariableHighlight(event.target.textContent);
    }
});

// Clicking code goes to the line of IR it was generated for.
function goToIRLine(line) {
    revealIRLine(line);
    selectIRLine(line);
    highlightAsm(asmOfIRLine.get(line) || [], 'selected');
    highlightDevice(line, 'device-selected');
    irLines[line - 1].scrollIntoView({
        behavior : 'instant',
        block : 'center'
    });
}
asmCode.addEventListener('click', function(event) {
    const i = Math.floor((event.clientY - asmCode.getBoundingClientRect().top) / asmLineHeight());
    const line = asmIRLines[i];
    if (line) {
        goToIRLine(line);
    }
});
if (devicePane) {
    devicePane.addEventListener('click', function(event) {
        const el = event.target.closest('span.line[data-ir]');
        if (el) {
            goToIRLine(parseInt(el.dataset.ir));
        }
    });
}


function initToolbar() {

    /* IR Settings */
    function make_toggler(ckbx, attr, inv) {
        if (ckbx === null || ckbx === undefined) return;
        ckbx.addEventListener('change', function() {
            if (ckbx.checked ^ inv) {
                document.body.setAttribute(attr, "true")
            } else {
                document.body.setAttribute(attr, "false")
            }
        });
        if (ckbx.checked ^ inv) {
            document.body.setAttribute(attr, "true")
        } else {
            document.body.setAttribute(attr, "false")
        }
    }
    make_toggler(document.getElementsByName("checkbox-show-ir-line-nums")[0], "data-show-line-nums", false);
    make_toggler(document.getElementsByName("checkbox-show-ir-costs")[0], "data-hide-cost", true);
    make_toggler(document.getElementsByName("checkbox-show-ir-wrap")[0], "data-wrap", false);
    make_toggler(document.getElementsByName("checkbox-rainbow-parens")[0], "data-rainbow-parens", false);

    /* Hiding panes */
    make_toggler(document.getElementsByName("checkbox-show-ir")[0], "data-show-ir", false);
    make_toggler(document.getElementsByName("checkbox-show-assembly")[0], "data-show-assembly", false);
    make_toggler(document.getElementsByName("checkbox-show-device-code")[0], "data-show-device-code", false);

    /* Theme toggling */
    var themeRadios = document.getElementById("form-theme").theme;

    var radioButtonAuto = themeRadios[0];
    const darkModePreference = window.matchMedia("(prefers-color-scheme: dark)");
    function updateAutoTheme() {
        console.log("update auto theme");
        if (radioButtonAuto.checked) {
            if(darkModePreference.matches) {
                document.body.setAttribute("data-theme", "gruvbox-dark");
            } else {
                document.body.setAttribute("data-theme", "classic-light");
            }
        }
    }
    radioButtonAuto.addEventListener('change', function() {
        updateAutoTheme();
    });
    updateAutoTheme();
    darkModePreference.addEventListener("change", e => updateAutoTheme());
    darkModePreference.addListener(e => updateAutoTheme());

    for (var i = 1; i < themeRadios.length; i++) {
        themeRadios[i].addEventListener('change', function() {
            document.body.setAttribute("data-theme", this.value);
        });
        if (themeRadios[i].checked) {
            document.body.setAttribute("data-theme", themeRadios[i].value);
        }
    }
}
initToolbar();
