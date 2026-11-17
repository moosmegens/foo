// foo.js: run and check foo code from JavaScript. no DOM, no UI, nothing else gets loaded.
//
//   import { loadFoo } from "./foo.js";
//
//   const foo = await loadFoo();
//   foo.check(source);   // -> Diagnostic[]
//   foo.run(source);     // -> { ok, output, returned, diagnostics }
//
// Diagnostic: { message, severity, start, end, line, column }
//   severity    "error" | "warning"
//   start, end  indices into the source string, so source.slice(start, end) is the problem
//   line, column  start at 1
//   start, end, line and column are null for problems without a location (like a missing main())

const GLUE = new URL("./foo_web.js", import.meta.url).href;

let loading = null;

// loads the wasm once and shares it; the first call decides where it comes from
export function loadFoo(url = GLUE) {
    loading ??= import(url)
        .then(({ default: createModule }) => createModule())
        .then(module => new Foo(module))
        .catch(error => {
            loading = null;
            throw error;
        });
    return loading;
}

export class Foo {
    #module;

    constructor(module) {
        this.#module = module;
    }

    check(source) {
        try {
            return toDiagnostics(source, this.#module.checkSource(source));
        } catch (error) {
            return [internalError(error)];
        }
    }

    run(source) {
        try {
            const result = this.#module.runSource(source);
            return {
                ok: result.ok,
                output: result.output,
                returned: result.returned,
                diagnostics: toDiagnostics(source, result.diagnostics),
            };
        } catch (error) {
            return { ok: false, output: "", returned: "", diagnostics: [internalError(error)] };
        }
    }
}

// ---------- internals ----------

const ASCII = /^[\x00-\x7f]*$/;

// the compiler reports UTF-8 byte offsets, JS strings are UTF-16
function indexMapper(text) {
    if (ASCII.test(text)) return i => Math.min(i, text.length);

    const encoder = new TextEncoder();
    const map = [];
    let byte = 0;
    for (let i = 0; i < text.length; ) {
        const ch = String.fromCodePoint(text.codePointAt(i));
        const size = encoder.encode(ch).length;
        for (let b = 0; b < size; b++) map[byte + b] = i;
        byte += size;
        i += ch.length;
    }
    map[byte] = text.length;
    return i => map[i] ?? text.length;
}

function toDiagnostics(source, raw) {
    if (raw.length === 0) return [];
    const index = indexMapper(source);

    return raw.map(d => {
        if (d.start === null) {
            return { message: d.message, severity: d.severity, start: null, end: null, line: null, column: null };
        }

        const start = index(d.start);
        const end = Math.max(index(d.end), start);
        const lineStart = start === 0 ? -1 : source.lastIndexOf("\n", start - 1);

        return {
            message: d.message,
            severity: d.severity,
            start,
            end,
            line: source.slice(0, start).split("\n").length,
            column: start - lineStart,
        };
    });
}

function internalError(error) {
    return {
        message: "internal error: " + (error?.message ?? error),
        severity: "error",
        start: null, end: null, line: null, column: null,
    };
}