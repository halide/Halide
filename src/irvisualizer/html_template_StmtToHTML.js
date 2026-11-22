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

/* The shown lines of the host assembly, as text. For each of them,
 * asmLineNumbers holds its line number in the assembly file, asmIRLines the
 * line of IR it was generated for (or 0), and asmIRNodes the node of IR it
 * was generated for (or 0): the data-g of the expression and of the
 * punctuation of that node. */
const asmLines = document.getElementById('asm-text').textContent.split('\n');
const asmPane = document.getElementById('host-assembly-pane');
const asmCode = document.getElementById('asm-code');
const asmHighlights = document.getElementById('asm-highlights');
const asmOfIRNode = new Map();
asmIRNodes.forEach((node, i) => {
    if (node) {
        if (!asmOfIRNode.has(node)) {
            asmOfIRNode.set(node, []);
        }
        asmOfIRNode.get(node).push(i);
    }
});
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

// The nesting depth that the punctuation of each node of IR is colored by
// with rainbow parentheses, which the selected code that was generated for
// the node is colored by too.
const depthOfIRNode = new Map();
irPane.querySelectorAll('span.op[data-g], span.kw[data-g]').forEach(el => {
    const depth = /\bd(\d)\b/.exec(el.className);
    if (depth && !depthOfIRNode.has(el.dataset.g)) {
        depthOfIRNode.set(el.dataset.g, depth[1]);
    }
});
function depthClassOfIRNode(node) {
    const depth = depthOfIRNode.get(String(node));
    return depth === undefined ? 'dn' : 'd' + depth;
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
// Marks lines of the assembly. With rainbow set, each also gets the class of
// the nesting depth of the node of IR it was generated for, and those of the
// node with the number own are marked as that node's own.
function highlightAsm(lines, cls, rainbow, own) {
    asmHighlights.querySelectorAll('div.' + cls).forEach(el => el.remove());
    const lineHeight = asmRowHeight;
    for (const i of lines) {
        const el = document.createElement('div');
        el.className = 'asm-hl ' + cls + (rainbow ? ' ' + depthClassOfIRNode(asmIRNodes[i]) : '') +
                       (own && asmIRNodes[i] === own ? ' own' : '');
        el.style.top = (i * lineHeight) + 'px';
        el.style.height = lineHeight + 'px';
        asmHighlights.appendChild(el);
    }
}
// How to scroll when going to code or to the IR it was generated for.
function scrollBehavior() {
    return document.getElementsByName('checkbox-smooth-scroll')[0].checked ? 'smooth' : 'instant';
}
function scrollToHostAsm(i) {
    asmPane.scrollTo({
        behavior : scrollBehavior(),
        top : i * asmRowHeight - asmPane.clientHeight / 3
    });
}

/* The device code. Its lines have their line number in data-n, and the line
 * and the node of IR they were generated for in data-ir and data-node, if
 * any. */
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
        const attributes = ' data-n="' + el.dataset.n + '"' + (el.dataset.ir ? ' data-ir="' + el.dataset.ir + '"' : '') +
                           (el.dataset.node ? ' data-node="' + el.dataset.node + '"' : '');
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
// The lines of device code generated for each line, and for each node, of IR.
const deviceOfIRLine = new Map();
const deviceOfIRNode = new Map();
if (devicePane) {
    const add = function(map, key, el) {
        if (!map.has(key)) {
            map.set(key, []);
        }
        map.get(key).push(el);
    };
    devicePane.querySelectorAll('span.line[data-ir]').forEach(el => {
        add(deviceOfIRLine, parseInt(el.dataset.ir), el);
        if (el.dataset.node) {
            add(deviceOfIRNode, parseInt(el.dataset.node), el);
        }
    });
}
function deviceOfIRNodes(nodes) {
    const lines = [];
    for (const n of nodes) {
        for (const el of (deviceOfIRNode.get(n) || [])) {
            lines.push(el);
        }
    }
    return lines;
}
// Marks lines of the device code. With rainbow set, each also gets the class
// of the nesting depth of the node of IR it was generated for, and those of
// the node with the number own are marked as that node's own.
const depthClasses = ['d0', 'd1', 'd2', 'd3', 'd4', 'd5', 'dn', 'own'];
function highlightDevice(lines, cls, rainbow, own) {
    if (!devicePane) {
        return;
    }
    devicePane.querySelectorAll('span.line.' + cls).forEach(el => {
        el.classList.remove(cls);
        if (cls.endsWith('-selected')) {
            el.classList.remove(...depthClasses);
        }
    });
    for (const el of lines) {
        el.classList.add(cls);
        if (rainbow) {
            el.classList.add(depthClassOfIRNode(el.dataset.node || 0));
            el.classList.toggle('own', Boolean(own) && parseInt(el.dataset.node || '0') === own);
        }
    }
}
function scrollToDevice(lines) {
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
        behavior : scrollBehavior(),
        top : lines[0].offsetTop - devicePane.clientHeight / 3
    });
}
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

/* Linking the IR to the code generated for it. What is linked is a line of
 * IR, and within it possibly one node: an expression with all that is nested
 * in it, or the node that one instruction was generated for. Device code is
 * linked to lines of IR only. Hovering shows
 * the link, double-clicking keeps it shown, and double-clicking it again or
 * pressing Escape lets go of it. The line is marked faintly, the node
 * strongly. With rainbow parentheses, the code that is selected (of the
 * node, or else of the line) is colored by the nesting depth of the node
 * each line of it was generated for, like the punctuation of that node. */

// The nodes of IR an element stands for: the node it is of, and for
// punctuation and expressions, the nodes nested in that one.
function irNodesOf(el) {
    const node = el.dataset.g;
    const nodes = [parseInt(node)];
    // The punctuation of an expression is in the span of the expression.
    const e = el.classList.contains('e') ? el : el.closest('span.e[data-g="' + node + '"]');
    if (e) {
        e.querySelectorAll('span.e[data-g]').forEach(inner => nodes.push(parseInt(inner.dataset.g)));
    }
    return nodes;
}
function asmOfIRNodes(nodes) {
    const rows = new Set();
    for (const n of nodes) {
        for (const row of (asmOfIRNode.get(n) || [])) {
            rows.add(row);
        }
    }
    return Array.from(rows).sort((a, b) => a - b);
}
// What is linked at an element of the IR, or null if no code was generated
// for it: its line with the lines of assembly of it, and its node (or 0)
// with the lines of assembly of that.
function linkAtIR(target) {
    const l = target.closest('div.l');
    if (!l) {
        return null;
    }
    const line = parseInt(l.dataset.line);
    const lineRows = asmOfIRLine.get(line) || [];
    const g = target.closest('[data-g]');
    const nodes = g ? irNodesOf(g) : [];
    const rows = asmOfIRNodes(nodes);
    const device = deviceOfIRNodes(nodes);
    if (!rows.length && !device.length && !lineRows.length && !deviceOfIRLine.has(line)) {
        return null;
    }
    return {
        line : line,
        node : rows.length || device.length ? parseInt(g.dataset.g) : 0,
        rows : rows,
        lineRows : lineRows,
        device : device,
        nested : true
    };
}
// What is linked at a line of the device code: its line of IR, and the node
// on it that the line was generated for, if that is known.
function linkAtDeviceLine(el) {
    const line = parseInt(el.dataset.ir);
    const node = el.dataset.node ? parseInt(el.dataset.node) : 0;
    return {
        line : line,
        node : node,
        rows : asmOfIRNode.get(node) || [],
        lineRows : asmOfIRLine.get(line) || [],
        device : deviceOfIRNode.get(node) || [],
        nested : false
    };
}
// What is linked at a line of the assembly, or null if it is not known which
// IR it was generated for.
function linkAtAsm(i) {
    const line = asmIRLines[i];
    if (!line) {
        return null;
    }
    const node = asmIRNodes[i];
    return {
        line : line,
        node : node,
        rows : asmOfIRNode.get(node) || [],
        lineRows : asmOfIRLine.get(line) || [],
        device : deviceOfIRNode.get(node) || [],
        nested : false
    };
}
function sameLink(a, b) {
    return a && b && a.line === b.line && a.node === b.node && a.nested === b.nested;
}
// Shows a link (or none) as hovered or as selected, in the IR and in the
// assembly.
function showLink(link, kind) {
    irPane.querySelectorAll('.line-' + kind + ', .node-' + kind)
        .forEach(el => el.classList.remove('line-' + kind, 'node-' + kind));
    if (link && link.line > 0 && link.line <= irLines.length) {
        irLines[link.line - 1].classList.add('line-' + kind);
        if (link.node) {
            irPane.querySelectorAll('[data-g="' + link.node + '"]').forEach(el => el.classList.add('node-' + kind));
        }
    }
    if (kind === 'selected') {
        // What is selected is the node, or else all of the line: that is
        // what rainbow parentheses color.
        if (link && link.node) {
            highlightAsm(link.lineRows, 'line-selected');
            highlightAsm(link.rows, 'node-selected', true, link.node);
        } else {
            highlightAsm(link ? link.lineRows : [], 'line-selected', true);
            highlightAsm([], 'node-selected');
        }
    } else {
        highlightAsm(link ? link.lineRows : [], 'line-' + kind);
        highlightAsm(link ? link.rows : [], 'node-' + kind);
    }
    const deviceOfLine = link ? (deviceOfIRLine.get(link.line) || []) : [];
    if (kind === 'selected') {
        if (link && link.node) {
            highlightDevice([], 'node-selected');
            highlightDevice(deviceOfLine, 'line-selected');
            highlightDevice(link.device, 'node-selected', true, link.node);
        } else {
            highlightDevice([], 'node-selected');
            highlightDevice(deviceOfLine, 'line-selected', true);
        }
    } else {
        highlightDevice(deviceOfLine, 'line-' + kind);
        highlightDevice(link ? link.device : [], 'node-' + kind);
    }
}
// A few words about a node of IR: its punctuation.
function describeIRNode(node) {
    const el = irPane.querySelector('[data-g="' + node + '"]:not(.e)') || irPane.querySelector('[data-g="' + node + '"]');
    const text = el ? el.textContent.trim() : '';
    return text.length > 24 ? text.slice(0, 24) + '…' : text;
}
let selectedLink = null;
const linkStatus = document.getElementById('link-status');
const linkClear = document.getElementById('link-clear');
function selectLink(link) {
    selectedLink = sameLink(link, selectedLink) ? null : link;
    showLink(selectedLink, 'selected');
    if (selectedLink) {
        const s = selectedLink;
        const count = n => n + (n === 1 ? ' instruction' : ' instructions');
        const device = (deviceOfIRLine.get(s.line) || []).length;
        const parts = [];
        if (s.lineRows.length || !device) {
            parts.push(s.node ? count(s.rows.length) + ' of ' + s.lineRows.length + ' on the line' : count(s.lineRows.length));
        }
        if (device) {
            parts.push((s.node ? s.device.length + ' of ' : '') + device + (device === 1 ? ' line' : ' lines') + ' of device code');
        }
        linkStatus.textContent = 'line ' + s.line + (s.node ? ', ' + describeIRNode(s.node) : '') + ' : ' + parts.join(', ');
    } else {
        linkStatus.textContent = 'double-click IR' + (devicePane ? ', assembly or device code' : ' or assembly');
    }
    linkClear.disabled = !selectedLink;
    return selectedLink;
}
linkClear.addEventListener('click', () => selectLink(null));
document.addEventListener('keydown', function(event) {
    if (event.key === 'Escape') {
        selectLink(null);
    }
});
selectLink(null);

// Marks (or unmarks) the punctuation of the same node as an element.
function markPunctuation(target, mark) {
    const g = target.closest('[data-g]:not(.e)');
    if (g) {
        irPane.querySelectorAll('[data-g="' + g.dataset.g + '"]:not(.e)').forEach(el => el.classList.toggle('Highlight', mark));
    }
}
irPane.addEventListener('mouseover', function(event) {
    markPunctuation(event.target, true);
    if (event.target.classList.contains('t')) {
        const header = event.target.closest('div.l');
        if (header && header.dataset.blockEnd) {
            markFold(header, true);
        }
    }
    const link = linkAtIR(event.target);
    showLink(link, 'hover');
    // The pointer says whether there is code to link to.
    irPane.classList.toggle('linkable', link !== null);
});
irPane.addEventListener('mouseout', function(event) {
    markPunctuation(event.target, false);
    if (event.target.classList.contains('t')) {
        const header = event.target.closest('div.l');
        if (header && header.dataset.blockEnd) {
            markFold(header, false);
        }
    }
});
irPane.addEventListener('mouseleave', function() {
    showLink(null, 'hover');
});
// Double-clicking is not for selecting text here.
function noTextSelection(event) {
    if (event.detail > 1) {
        event.preventDefault();
    }
}
irPane.addEventListener('mousedown', noTextSelection);
asmCode.addEventListener('mousedown', noTextSelection);

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
    } else if (event.target.classList.contains('var')) {
        toggleVariableHighlight(event.target.textContent);
    }
});
irPane.addEventListener('dblclick', function(event) {
    if (event.target.classList.contains('t')) {
        return;
    }
    const link = linkAtIR(event.target);
    if (link && selectLink(link)) {
        if (link.rows.length || link.lineRows.length) {
            scrollToHostAsm(link.rows.length ? link.rows[0] : link.lineRows[0]);
        }
        scrollToDevice(link.device.length ? link.device : (deviceOfIRLine.get(link.line) || []));
    }
});

// Goes to a line of IR.
function goToIRLine(line) {
    revealIRLine(line);
    irLines[line - 1].scrollIntoView({
        behavior : scrollBehavior(),
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
    const i = Math.floor((event.clientY - asmCode.getBoundingClientRect().top) / asmRowHeight);
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
    }
});
// Hovering assembly shows the IR it was generated for, double-clicking it
// goes there.
function asmRowAt(event) {
    return Math.floor((event.clientY - asmCode.getBoundingClientRect().top) / asmRowHeight);
}
let asmHoverRow = -1;
asmCode.addEventListener('mousemove', function(event) {
    const i = asmRowAt(event);
    if (i !== asmHoverRow) {
        asmHoverRow = i;
        const link = linkAtAsm(i);
        showLink(link, 'hover');
        asmCode.classList.toggle('linkable', link !== null);
    }
});
asmCode.addEventListener('mouseleave', function() {
    asmHoverRow = -1;
    showLink(null, 'hover');
});
asmCode.addEventListener('dblclick', function(event) {
    if (event.target.closest('span.asm-label')) {
        return;
    }
    const link = linkAtAsm(asmRowAt(event));
    if (link && selectLink(link)) {
        goToIRLine(link.line);
    }
});
// The same for device code.
if (devicePane) {
    const linkAtDevice = function(event) {
        const el = event.target.closest('span.line[data-ir]');
        return el ? linkAtDeviceLine(el) : null;
    };
    devicePane.addEventListener('mouseover', function(event) {
        const link = linkAtDevice(event);
        showLink(link, 'hover');
        devicePane.classList.toggle('linkable', link !== null);
    });
    devicePane.addEventListener('mouseleave', function() {
        showLink(null, 'hover');
    });
    devicePane.addEventListener('mousedown', noTextSelection);
    devicePane.addEventListener('dblclick', function(event) {
        const link = linkAtDevice(event);
        if (link && selectLink(link)) {
            goToIRLine(link.line);
            if (link.lineRows.length) {
                scrollToHostAsm(link.lineRows[0]);
            }
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
