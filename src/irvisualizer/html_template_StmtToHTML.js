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

// The row at which each label of the assembly is defined.
const asmLabelRows = new Map();
asmLines.forEach((line, i) => {
    const m = /^([\w.$]+):/.exec(line);
    if (m) {
        asmLabelRows.set(m[1], i);
    }
});

// The labels highlighted by clicking them, with the classes of their colors.
const asmLabelHighlights = new Map();

// A label that is defined in the assembly, which links to its definition.
function labelHTML(name) {
    const cls = asmLabelHighlights.get(name);
    return '<span class="shj-syn-label asm-label' + (cls ? ' ' + cls : '') + '" data-label="' + name + '">' +
           escapeHTML(name) + '</span>';
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
    if (head.endsWith(':') && asmLabelRows.has(head.slice(0, -1))) {
        out += labelHTML(head.slice(0, -1)) + ':';
    } else if (head) {
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
            } else if (asmLabelRows.has(w)) {
                out += labelHTML(w);
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

// Highlights CUDA device code (PTX): the kernels can be collapsed, the types
// of instructions are colored, and registers, predicates and labels can be
// highlighted.
function highlightPTX(pane) {
    const code = pane.querySelector('div.code.ptx');
    if (!code) {
        return;
    }
    let uid = 0;
    const ids = new Map();
    // The names come from the escaped text of the line.
    const variable = (name, tooltip) => {
        if (!ids.has(name)) {
            ids.set(name, uid++);
        }
        return '<b class="variable matched" id="' + ids.get(name) + '-' + (uid++) + '"' +
               (tooltip ? ' title="' + tooltip + '"' : '') + '>' + name + '</b>';
    };
    const typeSuffixes = [
        ['f32', 'OpF32'], ['f64', 'OpF64'],
        ['s8', 'OpI8'], ['s16', 'OpI16'], ['s32', 'OpI32'], ['s64', 'OpI64'],
        ['u8', 'OpI8'], ['u16', 'OpI16'], ['u32', 'OpI32'], ['u64', 'OpI64'],
        ['b8', 'OpB8'], ['b16', 'OpB16'], ['b32', 'OpB32'], ['b64', 'OpB64'],
        ['v2', 'OpVec2'], ['v4', 'OpVec4']];

    let html = '';
    let kernelId = -1, braceDepth = 0, inSignature = false;
    for (const el of Array.from(code.children)) {
        const attributes = ' data-n="' + el.dataset.n + '"' + (el.dataset.ir ? ' data-ir="' + el.dataset.ir + '"' : '');
        let text = el.textContent;
        if (text === '' || text === '\t') {
            html += '<span class="line"' + attributes + '></span>\n';
            continue;
        }
        // The first tab becomes four spaces.
        const indent = text[0] === '\t';
        if (indent) {
            text = text.slice(1);
        }
        let line = escapeHTML(text);
        let openIndent = false;
        if (text.startsWith('.visible .entry')) {
            const parts = text.split(' ');
            if (parts.length === 3) {
                inSignature = true;
                braceDepth = 0;
                kernelId = uid++;
                html += '<input type="checkbox" id="show-hide-btn-' + kernelId + '" class="show-hide-btn"/>' +
                        '<label for="show-hide-btn-' + kernelId + '">';
                line = '<span class="keyword">.visible</span> <span class="keyword">.entry</span> ' +
                       escapeHTML(parts[2].slice(0, -1)) + ' <span class="matched">(</span>';
            }
        } else if (text.startsWith(')') && inSignature) {
            inSignature = false;
            line = '<span class="matched">)</span>' + escapeHTML(text.slice(1));
        } else if (text.startsWith('{') && braceDepth <= 0) {
            html += '<span class="matched OpeningBrace">{</span>';
            braceDepth = 1;
            openIndent = true;
            line = escapeHTML(text.slice(1));
        } else if (text.startsWith('{') && braceDepth) {
            braceDepth++;
        } else if (text.startsWith('}') && --braceDepth <= 0) {
            html += '</div>';
            line = '<span class="matched">}</span>' + escapeHTML(text.slice(1));
            braceDepth = 0;
        }

        for (const [suffix, cls] of typeSuffixes) {
            line = line.split('.' + suffix).join('.<span class="' + cls + '">' + suffix + '</span>');
        }
        line = line.split('ld.').join('<span class="Memory">ld</span>.');
        line = line.split('st.').join('<span class="Memory">st</span>.');

        let idx = line.indexOf('//');
        if (idx >= 0) {
            line = line.slice(0, idx) + '<span class="Comment">' + line.slice(idx) + '</span>';
        }
        // Predicated instructions
        if (line[0] === '@' && indent) {
            idx = line.indexOf(' ');
            line = '<span class="Pred">@' + variable(line.slice(1, idx), 'bool') + '</span>' + line.slice(idx);
        }
        // Labels (depending on the LLVM version with or without a dollar)
        if (line.startsWith('$L_') && !indent && (idx = line.indexOf(':')) >= 0) {
            line = '<span class="Label">' + variable(line.slice(0, idx), 'Label') + '</span>:' + line.slice(idx + 1);
        }
        // Operands
        if ((idx = line.indexOf(' \t')) >= 0 && line.endsWith(';')) {
            const operands = line.slice(idx + 2, -1).split(', ').map(op => {
                if (op.endsWith('}')) {
                    return variable(op.slice(0, -1), 'reg') + '}';
                } else if (op[0] === '%') {
                    return variable(op, '');
                } else if (/^[-0-9]+$/.test(op)) {
                    return '<span class="IntImm Imm">' + op + '</span>';
                } else if (/^0f[0-9A-F]+$/.test(op)) {
                    return '<span class="FloatImm Imm">' + op + '</span>';
                } else if (op[0] === '[' && op.endsWith(']')) {
                    const plus = op.indexOf('+');
                    if (plus < 0) {
                        return '[' + variable(op.slice(1, -1), 'reg') + ']';
                    }
                    return '[' + variable(op.slice(1, plus), 'reg') + '+<span class="IntImm Imm">' +
                           op.slice(plus + 1, -1) + '</span>]';
                } else if (op[0] === '{') {
                    return '{' + variable(op.slice(1), 'reg');
                } else if (op.startsWith('$L_')) {
                    return '<span class="Label">' + variable(op, 'Label') + '</span>';
                }
                return op;
            });
            line = line.slice(0, idx + 2) + operands.join(', ') + ';';
        }

        html += '<span class="line"' + attributes + '>' + (indent ? '    ' : '') + line + '</span>\n';
        // The block of a kernel can only be opened after its first line.
        if (openIndent) {
            html += '</label><div class="op-btns"></div><div class="indent" id="' + kernelId + '">';
        }
    }
    code.innerHTML = html;
}
if (devicePane) {
    highlightPTX(devicePane);
    addHighlight('#device-code-pane');
}
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
// Marks (or unmarks) the lines that collapsing a block would hide, down to
// its closing brace, along the indentation of the block.
function markFold(header, mark) {
    const line = parseInt(header.dataset.line);
    const end = parseInt(header.dataset.blockEnd);
    const column = parseInt(header.style.getPropertyValue('--ind') || '0');
    for (let l = line + 1; l <= end; l++) {
        const el = irLines[l - 1];
        el.classList.toggle('fold', mark);
        el.classList.toggle('fold-end', mark && l === end);
        el.style.setProperty('--fold', column);
    }
}

irPane.addEventListener('mouseover', function(event) {
    const op = event.target.closest('span.op[data-g]');
    if (op) {
        irPane.querySelectorAll('span.op[data-g="' + op.dataset.g + '"]').forEach(el => el.classList.add('Highlight'));
    }
    if (event.target.classList.contains('t')) {
        const header = event.target.closest('div.l');
        if (header && header.dataset.blockEnd) {
            markFold(header, true);
        }
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
    if (event.target.classList.contains('t')) {
        const header = event.target.closest('div.l');
        if (header && header.dataset.blockEnd) {
            markFold(header, false);
        }
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
// Clicking a label where it is used goes to where it is defined. Clicking it
// there highlights it everywhere, in one of a few colors.
function toggleLabelHighlight(name) {
    if (asmLabelHighlights.has(name)) {
        asmLabelHighlights.delete(name);
    } else {
        const used = new Set(asmLabelHighlights.values());
        for (let i = 0; i < 7; i++) {
            if (!used.has('HighlightToggle' + i)) {
                asmLabelHighlights.set(name, 'HighlightToggle' + i);
                break;
            }
        }
    }
    asmRendered = [-1, -1];
    renderAsm();
}
asmContent.addEventListener('mouseover', function(event) {
    const label = event.target.closest('span.asm-label');
    if (label) {
        asmContent.querySelectorAll('span.asm-label[data-label="' + CSS.escape(label.dataset.label) + '"]')
            .forEach(el => el.classList.add('Highlight'));
    }
});
asmContent.addEventListener('mouseout', function(event) {
    const label = event.target.closest('span.asm-label');
    if (label) {
        asmContent.querySelectorAll('span.asm-label[data-label="' + CSS.escape(label.dataset.label) + '"]')
            .forEach(el => el.classList.remove('Highlight'));
    }
});

asmCode.addEventListener('click', function(event) {
    const i = Math.floor((event.clientY - asmCode.getBoundingClientRect().top) / asmLineHeight());
    const label = event.target.closest('span.asm-label');
    if (label) {
        const name = label.dataset.label;
        const row = asmLabelRows.get(name);
        if (row === i) {
            toggleLabelHighlight(name);
        } else {
            scrollToHostAsm(row);
            highlightAsm([row], 'label-target');
        }
        return;
    }
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


/* The device code of the APIs other than CUDA is C-like source (OpenCL C,
 * Metal, HLSL, WGSL, ...), highlighted here with the classes of the
 * speed-highlight library that the stylesheet colors. */
const deviceCodeKeywords = new Set([
    'auto', 'break', 'case', 'const', 'constant', 'constexpr', 'continue', 'default', 'device', 'do', 'else',
    'enum', 'extern', 'fn', 'for', 'global', 'groupshared', 'if', 'inline', 'kernel', 'let', 'local', 'namespace',
    'private', 'restrict', 'return', 'sizeof', 'static', 'struct', 'switch', 'template', 'thread', 'threadgroup',
    'typedef', 'typename', 'uniform', 'union', 'using', 'var', 'volatile', 'while', '__constant', '__global',
    '__kernel', '__local', '__private', '__attribute__', 'true', 'false']);
const deviceCodeTypes = /^(void|bool|char|uchar|short|ushort|int|uint|long|ulong|half|float|double|size_t|ptrdiff_t|u?int(8|16|32|64)_t|(bool|char|uchar|short|ushort|int|uint|long|ulong|half|float|double)(2|3|4|8|16)?|[iuf](8|16|32|64)|vec[234]\w*|mat\w+)$/;

function highlightDeviceCode(pane) {
    let inBlockComment = false;
    for (const el of pane.querySelectorAll('div.code:not(.ptx) span.line')) {
        const text = el.textContent;
        const span = (cls, t) => '<span class="shj-syn-' + cls + '">' + escapeHTML(t) + '</span>';
        let out = '';
        let i = 0;
        if (inBlockComment) {
            const end = text.indexOf('*/');
            i = end < 0 ? text.length : end + 2;
            inBlockComment = end < 0;
            out += span('cmnt', text.slice(0, i));
        } else {
            // A preprocessor directive.
            const m = /^\s*#\s*\w*/.exec(text);
            if (m) {
                out += span('kwd', m[0]);
                i = m[0].length;
            }
        }
        while (i < text.length) {
            const rest = text.slice(i);
            let m;
            if (rest.startsWith('//')) {
                out += span('cmnt', rest);
                break;
            } else if (rest.startsWith('/*')) {
                const end = rest.indexOf('*/', 2);
                const t = end < 0 ? rest : rest.slice(0, end + 2);
                inBlockComment = end < 0;
                out += span('cmnt', t);
                i += t.length;
            } else if ((m = /^"(?:[^"\\]|\\.)*"?|^'(?:[^'\\]|\\.)*'?/.exec(rest))) {
                out += span('str', m[0]);
                i += m[0].length;
            } else if ((m = /^(?:0x[0-9a-fA-F]+|\d+\.?\d*(?:[eE][+-]?\d+)?|\.\d+(?:[eE][+-]?\d+)?)[uUlLfFh]*/.exec(rest))) {
                out += span('num', m[0]);
                i += m[0].length;
            } else if ((m = /^[A-Za-z_]\w*/.exec(rest))) {
                const w = m[0];
                if (deviceCodeKeywords.has(w)) {
                    out += span('kwd', w);
                } else if (deviceCodeTypes.test(w)) {
                    out += span('type', w);
                } else if (/^\s*\(/.test(rest.slice(w.length))) {
                    out += span('func', w);
                } else {
                    out += escapeHTML(w);
                }
                i += w.length;
            } else {
                out += escapeHTML(rest[0]);
                i += 1;
            }
        }
        el.innerHTML = out;
    }
}
if (devicePane) {
    highlightDeviceCode(devicePane);
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
