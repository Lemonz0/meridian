// Meridian Reader: finds the main article on the page and shows it in a
// clean overlay above the page. Original code (no third-party reader library).
//
// This file is one JavaScript *expression*: a function the browser calls as
//   (<this file>)("check" | "extract" | "show" | "hide" | "style", options)
// in the page's main frame. It returns a string of fields separated by U+001F:
//   available ("1"/"0"), title, excerpt, byline, siteName, shown ("1"/"0").
//
// The article is rebuilt from scratch out of an allow-list of elements and
// attributes (no scripts, no event handlers, no inline styles, no iframes),
// so Reader shows the page's text and images but none of its code.
(function (mode, options) {
    'use strict';
    var HOST_ATTR = 'data-meridian-reader';
    var SEP = '\u001f';
    var MIN_ARTICLE_CHARS = 500;
    var POSITIVE = /article|body|content|entry|main|page|post|text|blog|story|prose/i;
    var NEGATIVE = /comment|meta|footer|footnote|sidebar|sponsor|\bads?\b|\bads?-|advert|promo|related|share|social|nav|menu|masthead|header|widget|banner|cookie|consent|subscribe|newsletter|popup|modal|breadcrumb|pagination|toolbar|disqus|outbrain|taboola|recommend/i;
    var DROP_TAGS = { SCRIPT: 1, STYLE: 1, NOSCRIPT: 1, IFRAME: 1, FRAME: 1, OBJECT: 1, EMBED: 1, FORM: 1, INPUT: 1,
        BUTTON: 1, SELECT: 1, TEXTAREA: 1, NAV: 1, ASIDE: 1, FOOTER: 1, SVG: 1, CANVAS: 1, VIDEO: 1, AUDIO: 1,
        TEMPLATE: 1, DIALOG: 1, LINK: 1, META: 1, MAP: 1 };
    // Output element for each kept input element; anything else is unwrapped.
    var KEEP = { P: 'p', H1: 'h2', H2: 'h2', H3: 'h3', H4: 'h4', H5: 'h5', H6: 'h6', UL: 'ul', OL: 'ol', LI: 'li',
        BLOCKQUOTE: 'blockquote', PRE: 'pre', CODE: 'code', EM: 'em', I: 'em', STRONG: 'strong', B: 'strong',
        A: 'a', IMG: 'img', FIGURE: 'figure', FIGCAPTION: 'figcaption', BR: 'br', HR: 'hr', TABLE: 'table',
        THEAD: 'thead', TBODY: 'tbody', TR: 'tr', TD: 'td', TH: 'th', SUP: 'sup', SUB: 'sub', DL: 'dl', DT: 'dt',
        DD: 'dd', DIV: 'div', SECTION: 'div', ARTICLE: 'div', MAIN: 'div', HEADER: 'div', PICTURE: 'div',
        KBD: 'kbd', SAMP: 'samp', MARK: 'mark', SMALL: 'small', CITE: 'cite', Q: 'q', ABBR: 'abbr', TIME: 'span',
        SPAN: 'span', U: 'span', S: 's', DEL: 'del', INS: 'ins', CAPTION: 'caption' };

    function text(el) { return (el && el.textContent ? el.textContent : '').replace(/\s+/g, ' ').trim(); }
    function clean(s, max) { s = String(s || '').replace(/[\u0000-\u001f]+/g, ' ').replace(/\s+/g, ' ').trim(); return max && s.length > max ? s.slice(0, max - 1) + '…' : s; }
    function meta(names) {
        for (var i = 0; i < names.length; i++) {
            var m = document.querySelector('meta[property="' + names[i] + '"], meta[name="' + names[i] + '"]');
            if (m && m.getAttribute('content')) return clean(m.getAttribute('content'));
        }
        return '';
    }
    function classAndId(el) { return (el.className && typeof el.className === 'string' ? el.className : '') + ' ' + (el.id || ''); }
    function hidden(el) {
        if (el.hidden || el.getAttribute('aria-hidden') === 'true') return true;
        var style = el.getAttribute('style') || '';
        return /display\s*:\s*none|visibility\s*:\s*hidden/i.test(style);
    }
    function linkDensity(el) {
        var total = text(el).length;
        if (!total) return 0;
        var links = el.getElementsByTagName('a'), linked = 0;
        for (var i = 0; i < links.length; i++) linked += text(links[i]).length;
        return linked / total;
    }
    function result(available, article, shown) {
        var fields = [available ? '1' : '0', article ? article.title : '', article ? article.excerpt : '',
            article ? article.byline : '', article ? article.siteName : '', shown ? '1' : '0'];
        return fields.map(function (f) { return String(f).replace(/\u001f/g, ' '); }).join(SEP);
    }
    function existingHost() { return document.querySelector('[' + HOST_ATTR + ']'); }

    // ---- finding the article ------------------------------------------------
    function initialScore(el) {
        var score = 0;
        switch (el.tagName) {
        case 'ARTICLE': score += 15; break;
        case 'MAIN': score += 10; break;
        case 'DIV': case 'SECTION': score += 5; break;
        case 'PRE': case 'TD': case 'BLOCKQUOTE': score += 3; break;
        case 'FORM': case 'UL': case 'OL': case 'DL': case 'LI': score -= 3; break;
        case 'H1': case 'H2': case 'H3': case 'TH': score -= 5; break;
        }
        var names = classAndId(el);
        if (POSITIVE.test(names)) score += 25;
        if (NEGATIVE.test(names)) score -= 25;
        if (el.getAttribute('itemprop') === 'articleBody') score += 30;
        return score;
    }

    function findArticleRoot() {
        if (!document.body) return null;
        var scores = new Map();
        function add(el, points) {
            if (!el || el === document.documentElement) return;
            if (!scores.has(el)) scores.set(el, initialScore(el));
            scores.set(el, scores.get(el) + points);
        }
        var paragraphs = document.body.querySelectorAll('p, pre, td, blockquote, div > br + br');
        for (var i = 0; i < paragraphs.length; i++) {
            var p = paragraphs[i];
            if (p.tagName === 'BR') p = p.parentNode;
            var t = text(p);
            if (t.length < 25) continue;
            var ancestorHidden = false;
            for (var a = p; a && a !== document.body; a = a.parentElement)
                if (DROP_TAGS[a.tagName] || hidden(a) || (a !== p && NEGATIVE.test(classAndId(a)) && !POSITIVE.test(classAndId(a)))) { ancestorHidden = true; break; }
            if (ancestorHidden) continue;
            var points = 1 + (t.split(/[,，、]/).length - 1) + Math.min(Math.floor(t.length / 100), 3);
            add(p.parentElement, points);
            if (p.parentElement) add(p.parentElement.parentElement, points / 2);
            if (p.parentElement && p.parentElement.parentElement) add(p.parentElement.parentElement.parentElement, points / 3);
        }
        var best = null, bestScore = 0;
        scores.forEach(function (score, el) {
            var adjusted = score * (1 - linkDensity(el));
            if (adjusted > bestScore) { bestScore = adjusted; best = el; }
        });
        // A single <article> or itemprop=articleBody holding most of the text wins outright.
        // A single <article> (or itemprop=articleBody) with enough text wins,
        // unless the best-scoring block inside it already holds most of it.
        var marked = document.querySelectorAll('article, [itemprop="articleBody"]');
        if (marked.length === 1 && text(marked[0]).length >= MIN_ARTICLE_CHARS) {
            var m = marked[0];
            if (!(best && m.contains(best) && text(best).length >= text(m).length * 0.8)) {
                best = m;
                bestScore = Math.max(bestScore, scores.get(m) || 0);
            }
        }
        return best ? { root: best, score: bestScore, scores: scores } : null;
    }

    // ---- rebuilding it safely -----------------------------------------------
    function safeURL(value, allowData) {
        if (!value) return '';
        try {
            var u = new URL(value, document.baseURI);
            if (u.protocol === 'http:' || u.protocol === 'https:') return u.href;
            if (u.protocol === 'mailto:' && !allowData) return u.href;
            if (allowData && u.protocol === 'data:' && /^data:image\/(png|jpe?g|gif|webp|avif);/i.test(value)) return value;
        } catch (e) {}
        return '';
    }
    function imageSource(img) {
        var candidates = [img.currentSrc, img.getAttribute('src'), img.getAttribute('data-src'), img.getAttribute('data-lazy-src'), img.getAttribute('data-original')];
        var srcset = img.getAttribute('srcset') || img.getAttribute('data-srcset');
        if (srcset) {
            var parts = srcset.split(',').map(function (s) { return s.trim().split(/\s+/)[0]; }).filter(Boolean);
            if (parts.length) candidates.push(parts[parts.length - 1]);
        }
        for (var i = 0; i < candidates.length; i++) {
            var c = candidates[i];
            if (c && !/^data:image\/gif;base64,R0lGOD/i.test(c)) { // skip 1x1 placeholder GIFs
                var u = safeURL(c, true);
                if (u) return u;
            }
        }
        return '';
    }
    function copyInto(out, node, doc, depth) {
        if (depth > 60) return;
        for (var child = node.firstChild; child; child = child.nextSibling) {
            if (child.nodeType === 3) { out.appendChild(doc.createTextNode(child.nodeValue)); continue; }
            if (child.nodeType !== 1) continue;
            var tag = child.tagName.toUpperCase();
            if (DROP_TAGS[tag] || hidden(child)) continue;
            var names = classAndId(child);
            if (NEGATIVE.test(names) && !POSITIVE.test(names) && text(child).length < 600) continue;
            if ((tag === 'UL' || tag === 'OL' || tag === 'DIV') && linkDensity(child) > 0.5 && text(child).length < 400) continue;
            var outTag = KEEP[tag];
            if (!outTag) { copyInto(out, child, doc, depth + 1); continue; } // unwrap
            if (tag === 'IMG') {
                var src = imageSource(child);
                var w = parseInt(child.getAttribute('width') || '0', 10), h = parseInt(child.getAttribute('height') || '0', 10);
                if (!src || (w && w < 40) || (h && h < 40)) continue;
                var img = doc.createElement('img');
                img.setAttribute('src', src);
                img.setAttribute('alt', clean(child.getAttribute('alt'), 300));
                img.setAttribute('loading', 'lazy');
                out.appendChild(img);
                continue;
            }
            var el = doc.createElement(outTag);
            if (tag === 'A') {
                var href = safeURL(child.getAttribute('href'), false);
                if (href) el.setAttribute('href', href);
            }
            if ((tag === 'TD' || tag === 'TH')) {
                ['colspan', 'rowspan'].forEach(function (a) {
                    var v = parseInt(child.getAttribute(a) || '', 10);
                    if (v > 1 && v < 100) el.setAttribute(a, String(v));
                });
            }
            copyInto(el, child, doc, depth + 1);
            // Drop empty containers left behind by removed content.
            if (outTag !== 'br' && outTag !== 'hr' && !el.querySelector('img') && !text(el)) continue;
            out.appendChild(el);
        }
    }

    function extract() {
        var found = findArticleRoot();
        if (!found) return null;
        var root = found.root;
        var threshold = Math.max(10, found.score * 0.2);
        // The best container, plus siblings that look like more of the same article.
        var parts = [];
        var siblings = root.parentElement ? root.parentElement.children : [root];
        for (var i = 0; i < siblings.length; i++) {
            var s = siblings[i];
            if (s === root) { parts.push(s); continue; }
            if (DROP_TAGS[s.tagName] || hidden(s)) continue;
            var score = (found.scores.get(s) || 0) * (1 - linkDensity(s));
            var t = text(s);
            if (score >= threshold || (s.tagName === 'P' && t.length > 80 && linkDensity(s) < 0.25))
                parts.push(s);
        }
        var doc = document.implementation.createHTMLDocument('');
        var body = doc.createElement('div');
        parts.forEach(function (p) { copyInto(body, p, doc, 0); });
        var length = text(body).length;
        if (length < MIN_ARTICLE_CHARS) return null;

        var docTitle = clean(document.title);
        var heading = root.querySelector('h1') || document.querySelector('h1');
        var title = meta(['og:title', 'twitter:title']) || docTitle;
        if (heading && text(heading).length > 10 && (!title || title.indexOf(text(heading)) >= 0 || docTitle.indexOf(text(heading)) >= 0))
            title = text(heading);
        // The article repeats its own title: don't show it twice.
        var first = body.querySelector('h2');
        if (first && text(first) === title) first.parentNode.removeChild(first);
        var byline = meta(['author', 'article:author', 'byl']);
        if (!byline) {
            var b = document.querySelector('[rel="author"], [itemprop="author"], .byline, .author, .post-author');
            if (b && text(b).length < 100) byline = text(b);
        }
        var firstParagraph = body.querySelector('p');
        var excerpt = meta(['description', 'og:description']) || (firstParagraph ? text(firstParagraph) : '');
        return {
            title: clean(title, 300),
            byline: clean(byline, 100),
            siteName: clean(meta(['og:site_name']) || location.hostname.replace(/^www\./, ''), 100),
            excerpt: clean(excerpt, 300),
            content: body,
            length: length
        };
    }

    // ---- presentation ---------------------------------------------------------
    var THEMES = {
        light: { bg: '#ffffff', fg: '#1d1d1f', muted: '#6e6e73', link: '#0a6cff', rule: '#e5e5ea', code: '#f2f2f5' },
        sepia: { bg: '#f8f1e3', fg: '#4f3b26', muted: '#8a7356', link: '#8b5a1c', rule: '#e6d9bf', code: '#efe5cf' },
        dark: { bg: '#1e1e1f', fg: '#e8e8ea', muted: '#9a9aa0', link: '#5ea2ff', rule: '#38383a', code: '#2a2a2c' }
    };
    function css(opts) {
        var size = Math.max(60, Math.min(250, Number(opts.size) || 100)) / 100 * 20;
        var font = opts.font === 'serif' ? 'Georgia, Cambria, "Times New Roman", serif'
            : '"Segoe UI Variable Text", "Segoe UI", Inter, system-ui, sans-serif';
        function vars(t) {
            return '--bg:' + t.bg + ';--fg:' + t.fg + ';--muted:' + t.muted + ';--link:' + t.link + ';--rule:' + t.rule + ';--code:' + t.code + ';';
        }
        var theme = THEMES[opts.theme] ? ':host{' + vars(THEMES[opts.theme]) + '}'
            : ':host{' + vars(THEMES.light) + '}@media (prefers-color-scheme: dark){:host{' + vars(THEMES.dark) + '}}';
        return theme +
            ':host{all:initial;position:fixed;inset:0;z-index:2147483647;overflow:auto;background:var(--bg);color:var(--fg);outline:none}' +
            '.page{box-sizing:border-box;max-width:40em;margin:0 auto;padding:56px 28px 96px;font-family:' + font + ';font-size:' + size + 'px;line-height:1.6;color:var(--fg)}' +
            '.site{font-size:.8em;color:var(--muted);margin:0 0 .4em}' +
            'h1{font-size:1.75em;line-height:1.2;margin:0 0 .4em;font-weight:700}' +
            '.byline{color:var(--muted);font-size:.85em;margin:0 0 1.6em}' +
            'h2,h3,h4,h5,h6{line-height:1.25;margin:1.6em 0 .5em}h2{font-size:1.35em}h3{font-size:1.15em}h4,h5,h6{font-size:1em}' +
            'p{margin:0 0 1em}a{color:var(--link)}img{max-width:100%;height:auto;display:block;margin:1.2em auto;border-radius:6px}' +
            'figure{margin:1.4em 0}figcaption{font-size:.8em;color:var(--muted);text-align:center}' +
            'blockquote{margin:1.2em 0;padding:0 0 0 1em;border-left:3px solid var(--rule);color:var(--muted)}' +
            'pre,code,kbd,samp{font-family:Consolas,"Cascadia Mono",monospace;font-size:.85em;background:var(--code);border-radius:4px}' +
            'pre{padding:.8em 1em;overflow:auto;line-height:1.45}code,kbd,samp{padding:.1em .3em}pre code{padding:0;background:none}' +
            'table{border-collapse:collapse;margin:1.2em 0;font-size:.9em;display:block;overflow:auto}td,th{border:1px solid var(--rule);padding:.35em .6em;text-align:left}' +
            'hr{border:0;border-top:1px solid var(--rule);margin:2em 0}ul,ol{padding-left:1.4em}li{margin:.25em 0}';
    }
    function applyStyle(root, opts) {
        var sheetText = css(opts || {});
        if (root.adoptedStyleSheets !== undefined && typeof CSSStyleSheet === 'function') {
            try {
                var sheet = new CSSStyleSheet();
                sheet.replaceSync(sheetText);
                root.adoptedStyleSheets = [sheet];
                var old = root.querySelector('style');
                if (old) old.remove();
                return;
            } catch (e) {}
        }
        var style = root.querySelector('style') || root.insertBefore(document.createElement('style'), root.firstChild);
        style.textContent = sheetText;
    }
    function show(article, opts) {
        hide(); // a previous overlay: restore the page first, so its state is what gets saved
        var host = document.createElement('div');
        host.setAttribute(HOST_ATTR, '');
        host.tabIndex = -1;
        host.setAttribute('role', 'document');
        var root = host.attachShadow({ mode: 'open' });
        applyStyle(root, opts);
        var page = document.createElement('div');
        page.className = 'page';
        var site = document.createElement('p');
        site.className = 'site';
        site.textContent = article.siteName;
        var h1 = document.createElement('h1');
        h1.textContent = article.title;
        page.appendChild(site);
        page.appendChild(h1);
        if (article.byline) {
            var by = document.createElement('p');
            by.className = 'byline';
            by.textContent = article.byline;
            page.appendChild(by);
        }
        page.appendChild(document.importNode(article.content, true));
        root.appendChild(page);
        // The page underneath keeps its scroll position; only its scrolling is paused.
        var html = document.documentElement;
        host.setAttribute('data-overflow', html.style.overflow || '');
        html.style.overflow = 'hidden';
        html.appendChild(host);
        host.focus({ preventScroll: true });
    }
    function hide() {
        var host = existingHost();
        if (!host) return false;
        document.documentElement.style.overflow = host.getAttribute('data-overflow') || '';
        host.remove();
        return true;
    }

    try {
        options = options || {};
        if (mode === 'hide') { hide(); return result(false, null, false); }
        if (mode === 'style') {
            var h = existingHost();
            if (h && h.shadowRoot) applyStyle(h.shadowRoot, options);
            return result(!!h, null, !!h);
        }
        if (!document.body || !/^(https?|file):$/.test(location.protocol)) return result(false, null, false);
        var article = extract();
        if (mode === 'show' && article) { show(article, options); return result(true, article, true); }
        return result(!!article, article, !!existingHost());
    } catch (e) {
        return result(false, null, false);
    }
})
