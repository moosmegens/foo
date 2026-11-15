// editing.js: code-editor behaviour for a <textarea>.
//   auto-indent on Enter, auto-close brackets and quotes, smart backspace,
//   Tab / Shift+Tab (also over several lines), Ctrl+/ to toggle comments.
//
//   const detach = attachEditing(textarea, { autoIndent: true, autoClose: true, onMove });
//
// every change goes through the browser's own editing commands, so undo / redo keep working.
// press Esc, then Tab, to move focus out of the editor instead of indenting.

const PAIRS = new Map([["(", ")"], ["[", "]"], ["{", "}"], ['"', '"']]);
const CLOSERS = new Set(PAIRS.values());
const INDENT_SIZE = 4;
const INDENT = " ".repeat(INDENT_SIZE);
const OUTDENT = new RegExp(`^(?: {1,${INDENT_SIZE}}|\\t)`);

const lineStartOf = (text, pos) => (pos === 0 ? 0 : text.lastIndexOf("\n", pos - 1) + 1);
const lineEndOf = (text, pos) => {
    const i = text.indexOf("\n", pos);
    return i === -1 ? text.length : i;
};

// how many spaces back to the previous tab stop
const toTabStop = col => Math.min(col, col % INDENT_SIZE || INDENT_SIZE);

// ---------- line transforms (take all selected lines, return the new lines) ----------

const indentLines  = lines => lines.map(l => (l.trim() === "" ? l : INDENT + l));
const outdentLines = lines => lines.map(l => l.replace(OUTDENT, ""));

function toggleComments(lines) {
    const body = lines.filter(l => l.trim() !== "");
    if (body.length === 0) return lines;

    if (body.every(l => /^\s*\/\//.test(l)))
        return lines.map(l => l.replace(/^(\s*)\/\/ ?/, "$1"));

    const margin = Math.min(...body.map(l => l.match(/^ */)[0].length));
    return lines.map(l => (l.trim() === "" ? l : l.slice(0, margin) + "// " + l.slice(margin)));
}

// ---------- attach ----------

export function attachEditing(input, { autoIndent = true, autoClose = true, onMove = () => {} } = {}) {
    let editing = false;       // our own edits fire beforeinput again, ignore those
    let releaseTab = false;    // Esc was pressed: the next Tab leaves the editor

    // replaces [start, end) and puts the caret / selection where asked
    function replace(start, end, text, caretStart = start + text.length, caretEnd = caretStart) {
        if (start === end && text === "") return;

        editing = true;
        try {
            input.focus();
            input.setSelectionRange(start, end);

            const ok = text === ""
                ? document.execCommand("delete")
                : document.execCommand("insertText", false, text);

            if (!ok) {   // fallback: works everywhere, but loses undo for this edit
                input.setRangeText(text, start, end, "end");
                input.dispatchEvent(new Event("input", { bubbles: true }));
            }

            input.setSelectionRange(caretStart, caretEnd);
        } finally {
            editing = false;
        }
        onMove();
    }

    // runs `transform(lines)` over every line the selection touches
    function mapLines(transform) {
        const { value, selectionStart: s, selectionEnd: end } = input;

        const from = lineStartOf(value, s);
        // a selection that ends at the start of a line doesn't include that line
        const last = end > s && value[end - 1] === "\n" ? end - 1 : end;
        const to = lineEndOf(value, last);

        const before = value.slice(from, to).split("\n");
        const after = transform(before);
        const text = after.join("\n");
        if (text === value.slice(from, to)) return;

        const firstDelta = after[0].length - before[0].length;
        const totalDelta = text.length - (to - from);

        replace(from, to, text, Math.max(from, s + firstDelta), Math.max(from, end + totalDelta));
    }

    // ---------- typing ----------

    function typed(e, ch) {
        const { value, selectionStart: s, selectionEnd: end } = input;
        const collapsed = s === end;
        const next = value[end] ?? "";

        // typing a closer that is already there steps over it
        if (autoClose && collapsed && CLOSERS.has(ch) && next === ch) {
            e.preventDefault();
            input.setSelectionRange(s + 1, s + 1);
            onMove();
            return;
        }

        // "}" on a line that is only indentation: step back one level first
        if (autoIndent && ch === "}" && collapsed) {
            const col = s - lineStartOf(value, s);
            if (col > 0 && /^ +$/.test(value.slice(s - col, s))) {
                e.preventDefault();
                replace(s - toTabStop(col), s, "}");
                return;
            }
        }

        if (!autoClose || !PAIRS.has(ch)) return;
        const close = PAIRS.get(ch);

        // an opener over a selection wraps it
        if (!collapsed) {
            e.preventDefault();
            replace(s, end, ch + value.slice(s, end) + close, s + 1, end + 1);
            return;
        }

        // no pairing inside strings or comments, or right before a word
        const before = value.slice(lineStartOf(value, s), s);
        const inComment = before.includes("//");
        const inString = (before.match(/"/g)?.length ?? 0) % 2 === 1;
        const prev = value[s - 1] ?? "";
        const nextOk = next === "" || /[\s)\]},;]/.test(next);
        const prevOk = ch !== '"' || !/[\w"\\]/.test(prev);

        if (!inComment && !inString && nextOk && prevOk) {
            e.preventDefault();
            replace(s, s, ch + close, s + 1);
        }
    }

    function enter(e) {
        const { value, selectionStart: s, selectionEnd: end } = input;
        const lineStart = lineStartOf(value, s);
        const before = value.slice(lineStart, s);
        const base = before.match(/^[ \t]*/)[0];

        const last = before.trimEnd().slice(-1);
        const opens = last !== "" && "([{".includes(last);

        const open = value[s - 1];
        const splits = s === end && open !== '"' && PAIRS.has(open) && PAIRS.get(open) === value[end];

        e.preventDefault();

        if (splits) {                       // {|}  ->  {  /  |  /  }
            const middle = "\n" + base + INDENT;
            replace(s, end, middle + "\n" + base, s + middle.length);
        } else if (before.trim() === "") {  // blank indented line: move the indentation down
            replace(lineStart, end, "\n" + base);
        } else {
            replace(s, end, "\n" + base + (opens ? INDENT : ""));
        }
    }

    function backspace(e) {
        const { value, selectionStart: s, selectionEnd: end } = input;
        if (s !== end || s === 0) return;

        // between a pair: delete both
        if (autoClose && PAIRS.has(value[s - 1]) && PAIRS.get(value[s - 1]) === value[s]) {
            e.preventDefault();
            replace(s - 1, s + 1, "", s - 1);
            return;
        }

        // inside indentation: back to the previous tab stop
        if (autoIndent) {
            const col = s - lineStartOf(value, s);
            if (col > 1 && /^ +$/.test(value.slice(s - col, s))) {
                const n = toTabStop(col);
                if (n > 1) {
                    e.preventDefault();
                    replace(s - n, s, "", s - n);
                }
            }
        }
    }

    function onBeforeInput(e) {
        if (editing || e.isComposing) return;

        switch (e.inputType) {
            case "insertText":
                if (typeof e.data === "string" && e.data.length === 1) typed(e, e.data);
                break;
            case "insertLineBreak":
                if (autoIndent) enter(e);
                break;
            case "deleteContentBackward":
                backspace(e);
                break;
        }
    }

    // ---------- keys that aren't text ----------

    function onKeyDown(e) {
        if (e.isComposing) return;

        if (e.key === "Escape") {
            releaseTab = true;
            return;
        }

        const mod = e.ctrlKey || e.metaKey;

        if (e.key === "Tab" && !mod && !e.altKey) {
            if (releaseTab) {            // let the browser move focus
                releaseTab = false;
                return;
            }
            e.preventDefault();

            const { value, selectionStart: s, selectionEnd: end } = input;
            if (e.shiftKey) {
                mapLines(outdentLines);
            } else if (s !== end) {
                mapLines(indentLines);
            } else {
                const col = s - lineStartOf(value, s);
                replace(s, s, " ".repeat(INDENT_SIZE - (col % INDENT_SIZE)));
            }
            return;
        }

        if (!["Shift", "Control", "Alt", "Meta"].includes(e.key)) releaseTab = false;

        if (mod && (e.key === "/" || e.code === "Slash")) {
            e.preventDefault();
            mapLines(toggleComments);
        }
    }

    const onBlur = () => { releaseTab = false; };

    input.addEventListener("beforeinput", onBeforeInput);
    input.addEventListener("keydown", onKeyDown);
    input.addEventListener("blur", onBlur);

    return function detach() {
        input.removeEventListener("beforeinput", onBeforeInput);
        input.removeEventListener("keydown", onKeyDown);
        input.removeEventListener("blur", onBlur);
    };
}