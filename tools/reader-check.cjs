// Dev check for src/engine/reader.js against the fixtures in tests/reader/.
// Not part of the build. Needs Node 18+ and jsdom:
//   npm install --prefix <some temp dir> jsdom@24
//   set NODE_PATH=<that dir>\node_modules   (bash: NODE_PATH=<dir>/node_modules)
//   node tools/reader-check.cjs
'use strict';
const fs = require('fs');
const path = require('path');
const { JSDOM } = require('jsdom');

const root = path.join(__dirname, '..');
const script = fs.readFileSync(path.join(root, 'src/engine/reader.js'), 'utf8');
let failures = 0;
function check(name, ok, detail) {
    console.log(`[${ok ? 'PASS' : 'FAIL'}] ${name}${ok ? '' : '  ' + (detail || '')}`);
    if (!ok) failures++;
}
function page(fixture, url = 'https://journal.example/posts/paint') {
    const html = fs.readFileSync(path.join(root, 'tests/reader', fixture), 'utf8');
    const dom = new JSDOM(html, { url, runScripts: 'outside-only' });
    const run = (mode, options) => {
        const out = dom.window.eval(`(${script})(${JSON.stringify(mode)}, ${JSON.stringify(options || {})})`);
        const f = String(out).split('\u001f');
        return { available: f[0] === '1', title: f[1], excerpt: f[2], byline: f[3], site: f[4], shown: f[5] === '1', raw: out };
    };
    return { dom, run, doc: dom.window.document };
}

// Article page.
{
    const { run, doc, dom } = page('article.html');
    const r = run('check');
    check('article: available', r.available, r.raw);
    check('article: title from <h1>', r.title === 'How Engines Paint Text', r.title);
    check('article: byline from meta', r.byline === 'Ada Example', r.byline);
    check('article: site name', r.site === 'Example Journal', r.site);
    check('article: excerpt from description', r.excerpt.startsWith('A tour of how'), r.excerpt);
    check('article: check does not touch the page', !doc.querySelector('[data-meridian-reader]'));

    const s = run('show', { theme: 'sepia', size: 120, font: 'serif' });
    const host = doc.querySelector('[data-meridian-reader]');
    check('show: overlay added', s.shown && !!host && !!host.shadowRoot);
    const shadow = host.shadowRoot;
    const html = shadow.innerHTML;
    const content = shadow.textContent;
    check('show: all article paragraphs kept', ['Painting text', 'shapes the text', 'line breaking', 'rasterizes glyphs', 'compositor assembles'].every(t => content.includes(t)));
    check('show: title shown once', (content.match(/How Engines Paint Text/g) || []).length === 1, content.slice(0, 200));
    check('show: navigation dropped', !content.includes('Archive of everything'));
    check('show: sidebar dropped', !content.includes('Ten tricks'));
    check('show: comments dropped', !content.includes('First!'));
    check('show: footer dropped', !content.includes('Copyright'));
    check('show: share buttons dropped', !content.includes('Share'));
    check('show: hidden text dropped', !content.includes('Hidden paragraph'));
    check('show: no scripts', !shadow.querySelector('script') && !html.includes('injected'));
    check('show: no iframes or buttons', !shadow.querySelector('iframe, button, form, input'));
    check('show: no event handler attributes', !/\son[a-z]+=/i.test(html), html.match(/\son[a-z]+=[^>]*/i));
    check('show: no inline styles in content', !shadow.querySelector('.page [style]'));
    check('show: javascript: link has no href', [...shadow.querySelectorAll('a')].every(a => !a.getAttribute('href') || /^https?:/.test(a.getAttribute('href'))));
    check('show: relative link made absolute', [...shadow.querySelectorAll('a')].some(a => a.getAttribute('href') === 'https://journal.example/more'));
    const imgs = [...shadow.querySelectorAll('img')];
    check('show: real image kept, absolute', imgs.length === 1 && imgs[0].getAttribute('src') === 'https://journal.example/img/glyphs.png', imgs.map(i => i.getAttribute('src')).join(' '));
    check('show: tracking pixel dropped', !html.includes('pixel.gif'));
    check('show: code block kept', !!shadow.querySelector('pre code'));
    check('show: figure caption kept', content.includes('before rasterization'));
    check('show: page scrolling paused', doc.documentElement.style.overflow === 'hidden');
    const sheetText = shadow.adoptedStyleSheets && shadow.adoptedStyleSheets.length
        ? [...shadow.adoptedStyleSheets[0].cssRules].map(r => r.cssText).join('') : (shadow.querySelector('style') || {}).textContent || '';
    check('show: sepia theme and size applied', /f8f1e3/i.test(sheetText) && /24px/.test(sheetText), sheetText.slice(0, 120));
    check('show: page scripts never ran', dom.window.pageScriptRan === undefined);

    run('style', { theme: 'dark', size: 100 });
    const sheet2 = shadow.adoptedStyleSheets && shadow.adoptedStyleSheets.length
        ? [...shadow.adoptedStyleSheets[0].cssRules].map(r => r.cssText).join('') : (shadow.querySelector('style') || {}).textContent || '';
    check('style: theme changes in place', /1e1e1f/i.test(sheet2) && /20px/.test(sheet2));
    const again = run('show', {});
    check('show twice: one overlay', doc.querySelectorAll('[data-meridian-reader]').length === 1 && again.shown);
    const h = run('hide');
    check('hide: overlay removed, scrolling restored', !doc.querySelector('[data-meridian-reader]') && doc.documentElement.style.overflow === '' && !h.shown);
}

// Pages that are not articles.
{
    const r = page('short.html', 'https://example.com/').run('check');
    check('short page: not available', !r.available, r.raw);
    const s = page('short.html', 'https://example.com/').run('show');
    check('short page: show does nothing', !s.shown);
}
{
    const r = page('links.html', 'https://portal.example/').run('check');
    check('link portal: not available', !r.available, r.raw);
}
{
    const p = page('divs.html', 'https://divs.example/caching');
    const r = p.run('check');
    check('div/br article: available', r.available, r.raw);
    check('div/br article: title from document', r.title.startsWith('Notes on Caching'), r.title);
    p.run('show');
    const content = p.doc.querySelector('[data-meridian-reader]').shadowRoot.textContent;
    check('div/br article: <br><br> paragraphs kept', content.includes('art of remembering') && content.includes('policy for forgetting'));
    check('div/br article: newsletter banner dropped', !content.includes('newsletter'));
    check('div/br article: related posts dropped', !content.includes('Related:'));
}
{
    // Encyclopedia-style layout: nested content containers, infobox, edit links, citations.
    const p = page('wiki.html', 'https://encyclopedia.example/wiki/Layout_engine');
    const r = p.run('check');
    check('wiki: available', r.available, r.raw);
    check('wiki: title from <h1>', r.title === 'Layout engine', r.title);
    p.run('show');
    const content = p.doc.querySelector('[data-meridian-reader]').shadowRoot.textContent;
    check('wiki: all paragraphs kept', ['turns markup', 'share a pipeline', 'Early engines', 'Forks are common', 'conformance suites'].every(t => content.includes(t)));
    check('wiki: section headings kept', content.includes('History') && content.includes('Design'));
    check('wiki: [edit] links dropped', !/\bedit\b/.test(content), content.match(/.{20}\bedit\b.{20}/));
    check('wiki: citation markers dropped', !content.includes('[1]'));
    check('wiki: navigation, toc, navbox, footer dropped', !content.includes('Current events') && !content.includes('Jump to content') && !content.includes('Epsilon') && !content.includes('last edited'));
    check('wiki: hatnote and site subtitle dropped', !content.includes('disambiguation') && !content.includes('From the Encyclopedia'));
}
{
    const r = page('article.html', 'about:blank').run('check');
    check('about: pages are never available', !r.available);
}

console.log(failures ? `${failures} failed` : 'all passed');
process.exit(failures ? 1 : 0);
