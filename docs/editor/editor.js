// editor.js: a foo code editor for any page.
// no page layout in here: the page decides where the editor, output, problems and status go.
//
//   import { createEditor } from "./editor.js";
//
//   const editor = createEditor({
//     editor:   document.querySelector("#editor"),     // required, give it a height
//     output:   document.querySelector("#output"),     // everything below is optional
//     problems: document.querySelector("#problems"),
//     status:   document.querySelector("#status"),
//     cursor:   document.querySelector("#cursor"),     // shows "Ln 3, Col 5"
//     runButton:   document.querySelector("#run"),
//     checkButton: document.querySelector("#check"),
//   });
//
//   editor.run()  editor.check()  editor.getCode()  editor.setCode(text)
//   editor.focus()  editor.destroy()
//   editor.ready   resolves to true once foo is loaded, false when loading failed
//
// other options: code, checkDelay (ms, default 300), module (url of foo_web.js),
//   autoIndent and autoClose (default true), css (false to skip editor.css)
//
// hovering an underlined range shows the problem in a tooltip.

import { loadFoo } from "../foo.js";
import { highlight } from "./highlight.js";
import { attachEditing } from "./editing.js";

const HERE = import.meta.url;
const TOOLTIP_DELAY_MS = 150;

export const DEFAULT_CODE = `fn main() -> int
{
    println("Hello from foo!")
    return 42
}`;

// adds a stylesheet that sits next to this file, once
const loadedCss = new Set();
export function loadCss(file) {
    const href = new URL(file, HERE).href;
    if (loadedCss.has(href)) return;
    loadedCss.add(href);

    const link = document.createElement("link");
    link.rel = "stylesheet";
    link.href = href;
    document.head.append(link);
}

// ---------- helpers ----------

const el = (tag, cls) => {
    const e = document.createElement(tag);
    if (cls) e.className = cls;
    return e;
};

const lineOf = (text, index) => text.slice(0, index).split("\n").length;
const lineStartOf = (text, pos) => (pos === 0 ? 0 : text.lastIndexOf("\n", pos - 1) + 1);

// diagnostics -> non-overlapping character ranges to underline.
// every range keeps the diagnostics it stands for, so hovering can show them
function toMarks(text, diags) {
    const raw = diags
        .filter(d => d.start !== null)
        .map(d => {
            const start = Math.min(d.start, text.length);
            return { start, end: Math.max(d.end, start), isError: d.severity === "error", diag: d };
        })
        .sort((a, b) => a.start - b.start);

    const marks = [];
    let last = 0;

    const addTo = (mark, m) => {
        mark.diags.push(m.diag);
        mark.isError ||= m.isError;
    };

    for (const m of raw) {
        // fully inside a range that is already underlined
        if (m.start < last && m.end <= last) {
            addTo(marks.at(-1), m);
            continue;
        }

        const start = Math.max(m.start, last);
        let end = m.end;

        // on a newline or at the very end there is no character to underline
        const atEol = start >= text.length || text[start] === "\n";
        if (!atEol && end <= start) end = start + 1;

        const sameEol = atEol && marks.find(x => x.pad && x.start === start);
        if (sameEol) {
            addTo(sameEol, m);
            continue;
        }

        end = Math.max(end, start);
        marks.push({ start, end, isError: m.isError, pad: atEol, diags: [m.diag] });
        last = end;
    }
    return marks;
}

// draws the underline layer, returns [{ el, diags }] so the editor can hit-test it
function renderBackdrop(target, text, marks) {
    target.textContent = "";
    const spans = [];
    let pos = 0;

    for (const m of marks) {
        if (m.start > pos) target.append(text.slice(pos, m.start));
        const span = el("span", m.isError ? "foo-err-mark" : "foo-warn-mark");
        span.textContent = (m.pad ? "\u00A0" : "") + text.slice(m.start, m.end);
        target.append(span);
        spans.push({ el: span, diags: m.diags });
        pos = Math.max(pos, m.end);
    }

    target.append(text.slice(pos) + "\n");
    return spans;
}

function renderGutter(target, text, marks) {
    const level = new Map();     // line number -> "err" | "warn"
    for (const m of marks) {
        const line = lineOf(text, m.start);
        if (level.get(line) !== "err") level.set(line, m.isError ? "err" : "warn");
    }

    const count = text.split("\n").length;
    target.textContent = "";
    for (let i = 1; i <= count; i++) {
        const span = el("span", level.has(i) ? "foo-ln-" + level.get(i) : "");
        span.textContent = i + "\n";
        target.append(span);
    }
}

// ---------- the editor ----------

export function createEditor(options) {
    const {
        editor, output = null, problems = null, status = null, cursor = null,
        runButton = null, checkButton = null,
        code = DEFAULT_CODE, checkDelay = 220, css = true, module,
        autoIndent = true, autoClose = true,
    } = options;

    if (!editor) throw new Error("createEditor: the 'editor' element is required");
    if (css) loadCss("editor.css");

    let foo = null;
    let destroyed = false;
    let timer = 0;
    let caretFrame = 0;
    let caretLine = 1;
    let activeLine = null;
    let markEls = [];          // [{ el, diags }] from the last render, for hover
    let tipTimer = 0;
    let tipMark = null;        // the range the tooltip is showing (or about to show) for

    // build the editor
    editor.classList.add("foo-editor");
    editor.replaceChildren();

    const gutter         = el("div", "foo-gutter");
    const codeBox        = el("div", "foo-code");
    const lineBand       = el("div", "foo-current");
    const highlightLayer = el("div", "foo-highlight");
    const backdrop       = el("div", "foo-backdrop");
    const input          = el("textarea", "foo-input");

    for (const layer of [gutter, lineBand, highlightLayer, backdrop]) layer.setAttribute("aria-hidden", "true");
    input.spellcheck = false;
    input.wrap = "off";
    input.autocomplete = "off";
    input.autocapitalize = "off";
    input.setAttribute("aria-label", "foo source code");
    input.value = code;

    codeBox.append(lineBand, highlightLayer, backdrop, input);
    editor.append(gutter, codeBox);

    output?.classList.add("foo-output");
    problems?.classList.add("foo-problems");
    status?.classList.add("foo-status");
    cursor?.classList.add("foo-cursor");

    // the tooltip lives on <body>, so no overflow: hidden around the editor can clip it
    const tip = el("div", "foo-tooltip");
    tip.hidden = true;
    tip.setAttribute("role", "tooltip");
    document.body.append(tip);

    // ---------- caret: current line band, active line number, "Ln 3, Col 5" ----------

    function metrics() {
        const style = getComputedStyle(input);
        return {
            lineHeight: parseFloat(style.lineHeight) || 21,
            paddingTop: parseFloat(style.paddingTop) || 0,
        };
    }

    function placeBand() {
        const { lineHeight, paddingTop } = metrics();
        lineBand.style.top = `${paddingTop + (caretLine - 1) * lineHeight - input.scrollTop}px`;
        lineBand.style.height = `${lineHeight}px`;
    }

    function updateCaret() {
        if (destroyed) return;
        const text = input.value;
        const pos = input.selectionStart;
        const selected = input.selectionEnd - pos;

        caretLine = lineOf(text, pos);
        lineBand.hidden = selected > 0;
        placeBand();

        activeLine?.classList.remove("foo-ln-active");
        activeLine = gutter.children[caretLine - 1] ?? null;
        activeLine?.classList.add("foo-ln-active");

        if (cursor) {
            const col = pos - lineStartOf(text, pos) + 1;
            cursor.textContent = `Ln ${caretLine}, Col ${col}` + (selected ? ` · ${selected} selected` : "");
        }
    }

    function scheduleCaret() {
        cancelAnimationFrame(caretFrame);
        caretFrame = requestAnimationFrame(updateCaret);
    }

    // ---------- tooltip on hover ----------

    // which underlined range is under the mouse? the textarea covers the underline layer,
    // so ask the layer where its ranges are on screen instead
    function findMark(x, y) {
        if (markEls.length === 0) return null;
        const { lineHeight } = metrics();

        for (const mark of markEls) {
            for (const rect of mark.el.getClientRects()) {
                // the inline box is shorter than the line, widen it so there are no dead gaps between lines
                const pad = Math.max(0, (lineHeight - rect.height) / 2);
                if (x >= rect.left && x < rect.right && y >= rect.top - pad && y < rect.bottom + pad)
                    return { mark, rect };
            }
        }
        return null;
    }

    function hideTip() {
        clearTimeout(tipTimer);
        tipMark = null;
        tip.hidden = true;
    }

    function showTip(hit, x) {
        if (tipMark !== hit.mark) return;

        tip.textContent = "";
        for (const d of hit.mark.diags) {
            const isError = d.severity === "error";
            const tag = el("span", "foo-tip-tag " + (isError ? "foo-tip-err" : "foo-tip-warn"));
            tag.textContent = isError ? "error" : "warning";

            const row = el("div", "foo-tip-row");
            row.append(tag, d.message);
            tip.append(row);
        }

        tip.hidden = false;

        // just below the range, or above it when there is no room
        const gap = 6, margin = 8;
        const { offsetWidth: w, offsetHeight: h } = tip;
        const left = Math.max(margin, Math.min(x, window.innerWidth - w - margin));
        let top = hit.rect.bottom + gap;
        if (top + h > window.innerHeight - margin) top = Math.max(margin, hit.rect.top - h - gap);

        tip.style.left = `${left}px`;
        tip.style.top = `${top}px`;
    }

    function onHover(e) {
        if (e.buttons !== 0) { hideTip(); return; }     // selecting text

        const hit = findMark(e.clientX, e.clientY);
        if (!hit) { hideTip(); return; }
        if (hit.mark === tipMark) return;                // already showing, or about to

        tipMark = hit.mark;
        clearTimeout(tipTimer);

        if (tip.hidden) tipTimer = setTimeout(() => showTip(hit, e.clientX), TOOLTIP_DELAY_MS);
        else showTip(hit, e.clientX);                    // moving between errors: no delay
    }

    // ---------- rendering ----------

    function setStatus(state, text) {
        if (!status) return;
        status.dataset.state = state;
        status.textContent = text;
    }

    function paintText(text) {
        highlight(highlightLayer, text, { trailingNewline: true });
        input.classList.add("foo-hl");     // only hide the textarea's own text once the colors exist
    }

    function clearMarks(text) {
        markEls = renderBackdrop(backdrop, text, []);
        renderGutter(gutter, text, []);
    }

    function jumpTo(start, end, line) {
        input.focus();
        input.setSelectionRange(start, end);
        input.scrollTop = Math.max(0, (line - 3) * metrics().lineHeight);
        updateCaret();
    }

    function renderProblems(diags) {
        if (!problems) return;
        problems.textContent = "";

        if (diags.length === 0) {
            const empty = el("div", "foo-problem-empty");
            empty.textContent = "no problems";
            problems.append(empty);
            return;
        }

        for (const d of diags) {
            const isError = d.severity === "error";
            const row = el("div", "foo-problem " + (isError ? "foo-problem-err" : "foo-problem-warn"));

            const tag = el("span", "foo-problem-tag");
            tag.textContent = isError ? "error" : "warn";
            row.append(tag);

            if (d.start !== null) {
                const loc = el("span", "foo-problem-loc");
                loc.textContent = `${d.line}:${d.column}`;
                row.append(loc);
                row.onclick = () => jumpTo(d.start, d.end, d.line);
            }

            row.append(d.message);
            problems.append(row);
        }
    }

    function renderStatus(diags) {
        const errors = diags.filter(d => d.severity === "error").length;
        const warnings = diags.length - errors;
        const plural = (n, word) => `${n} ${word}${n === 1 ? "" : "s"}`;

        if (errors > 0)        setStatus("err", plural(errors, "error") + (warnings ? ", " + plural(warnings, "warning") : ""));
        else if (warnings > 0) setStatus("warn", plural(warnings, "warning"));
        else                   setStatus("ok", "no problems");
    }

    function show(text, diags) {
        hideTip();
        const marks = toMarks(text, diags);
        markEls = renderBackdrop(backdrop, text, marks);
        renderGutter(gutter, text, marks);
        renderProblems(diags);
        renderStatus(diags);
        updateCaret();
    }

    // ---------- actions ----------

    function check() {
        clearTimeout(timer);
        if (!foo) return;
        const text = input.value;
        show(text, foo.check(text));
    }

    function run() {
        clearTimeout(timer);
        if (!foo) {
            setStatus("", "loading…");
            return;
        }

        const text = input.value;
        const result = foo.run(text);

        if (output) {
            output.textContent = result.output;
            if (result.ok) {
                const line = el("div", "foo-returned");
                line.textContent = `main returned: ${result.returned}`;
                output.append(line);
            }
        }
        show(text, result.diagnostics);
    }

    function scheduleCheck() {
        clearTimeout(timer);
        timer = setTimeout(() => {
            if (!destroyed && editor.isConnected) check();
        }, checkDelay);
    }

    // text changed: colors follow immediately, old underlines are stale, so clear and re-check
    function changed() {
        hideTip();
        const text = input.value;
        paintText(text);
        clearMarks(text);
        setStatus("", foo ? "checking..." : "loading...");
        scheduleCheck();
        updateCaret();
    }

    // ---------- events ----------

    const detachEditing = attachEditing(input, { autoIndent, autoClose, onMove: updateCaret });

    input.addEventListener("keydown", e => {
        if ((e.ctrlKey || e.metaKey) && e.key === "Enter") {
            e.preventDefault();
            run();
        }
        hideTip();
        scheduleCaret();
    });

    input.addEventListener("input", changed);
    input.addEventListener("keyup", scheduleCaret);
    input.addEventListener("mouseup", scheduleCaret);

    input.addEventListener("mousemove", onHover);
    input.addEventListener("mousedown", hideTip);
    input.addEventListener("mouseleave", hideTip);
    input.addEventListener("blur", hideTip);

    input.addEventListener("focus", () => {
        editor.classList.add("foo-focused");
        updateCaret();
    });
    input.addEventListener("blur", () => editor.classList.remove("foo-focused"));

    input.addEventListener("scroll", () => {
        highlightLayer.scrollTop = backdrop.scrollTop = gutter.scrollTop = input.scrollTop;
        highlightLayer.scrollLeft = backdrop.scrollLeft = input.scrollLeft;
        placeBand();
        hideTip();
    });

    // arrow keys, shift-selection, dragging: keep the caret readout in sync
    const onSelectionChange = () => {
        if (!editor.isConnected) {            // removed from the page without destroy()
            document.removeEventListener("selectionchange", onSelectionChange);
            tip.remove();
            return;
        }
        if (document.activeElement === input) scheduleCaret();
    };
    document.addEventListener("selectionchange", onSelectionChange);

    runButton?.addEventListener("click", run);
    checkButton?.addEventListener("click", check);

    // ---------- start ----------

    paintText(code);
    clearMarks(code);
    setStatus("", "loading...");
    updateCaret();

    const ready = loadFoo(module)
        .then(loaded => {
            if (destroyed) return false;
            foo = loaded;
            check();
            return true;
        })
        .catch(error => {
            console.error(error);
            setStatus("err", "could not load foo");
            if (output) output.textContent = "could not load the foo module:\n" + (error?.message ?? error);
            return false;
        });

    return {
        ready,
        run,
        check,
        getCode: () => input.value,
        setCode(text) { input.value = text; changed(); },
        focus: () => input.focus(),
        destroy() {
            destroyed = true;
            clearTimeout(timer);
            clearTimeout(tipTimer);
            cancelAnimationFrame(caretFrame);
            document.removeEventListener("selectionchange", onSelectionChange);
            detachEditing();
            tip.remove();
            editor.replaceChildren();
            editor.classList.remove("foo-editor", "foo-focused");
        },
    };
}