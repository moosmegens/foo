// embed.js: turns <div data-foo-editor></div> into a small editor with buttons, output and problems.
//
//   import { mountEmbed } from "./embed.js";
//   mountEmbed(document.querySelector("[data-foo-editor]"));
//
// optional attributes on the div:
//   data-height="280px"        height of the editor (default 320px)
//   data-src="path/to/x.foo"   load the starting code from a file
//   data-code="..."            starting code inline (use &#10; for a newline)

import { createEditor, loadCss, DEFAULT_CODE } from "./editor.js";

// no whitespace between tags, so it can't show up inside a white-space: pre parent
const TEMPLATE = [
    '<fieldset class="foo-box"><div data-foo="editor"></div></fieldset>',
    '<div class="foo-embed-bar">',
    '<button type="button" class="foo-btn foo-btn-primary" data-foo="run">[ run ]</button>',
    '<button type="button" class="foo-btn" data-foo="check">[ check ]</button>',
    '<span data-foo="status"></span>',
    '<span class="foo-embed-right">' +
    // '<span data-foo="cursor"></span>' +
    '<span class="foo-embed-hint">ctrl+enter runs</span></span>',
    '</div>',
    '<div class="foo-embed-panels">',
    '<fieldset class="foo-box"><div data-foo="output"></div></fieldset>',
    '<fieldset class="foo-box"><div data-foo="problems"></div></fieldset>',
    '</div>',
].join("");

export function mountEmbed(container) {
    if ("fooMounted" in container.dataset) return null;
    container.dataset.fooMounted = "";

    loadCss("embed.css");
    container.classList.add("foo-embed");
    if (container.dataset.height) container.style.setProperty("--foo-embed-height", container.dataset.height);
    container.innerHTML = TEMPLATE;

    const part = name => container.querySelector(`[data-foo="${name}"]`);
    const editor = createEditor({
        editor: part("editor"),
        output: part("output"),
        problems: part("problems"),
        status: part("status"),
        cursor: part("cursor"),
        runButton: part("run"),
        checkButton: part("check"),
        code: container.dataset.code ?? DEFAULT_CODE,
        autoClose: container.dataset.autoClose !== "false",
    });

    if (container.dataset.src) {
        fetch(container.dataset.src)
            .then(r => (r.ok ? r.text() : Promise.reject(new Error(`${r.status} ${r.statusText}`))))
            .then(text => editor.setCode(text))
            .catch(error => console.warn("foo: could not load", container.dataset.src, error));
    }

    return editor;
}