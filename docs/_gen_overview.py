#!/usr/bin/env python3
"""Generate overview.html by embedding all docs/*.md. MD files are left intact."""
from __future__ import annotations

import html
import re
from pathlib import Path

DOCS = Path(__file__).resolve().parent
ROOT = DOCS.parent
OUT = DOCS / "overview.html"

# Logical reading order (docs/README.md). Remaining *.md appended alphabetically.
ORDER = [
    "usage.md",
    "getting_started.md",
    "faq.md",
    "architecture.md",
    "design_decisions.md",
    "references.md",
    "porting_guide.md",
    "esp_idf_cmake.md",
    "driver_guide.md",
    "peripherals.md",
    "usb_tusb_port.md",
    "amp.md",
    "osal_switching.md",
    "service_spec.md",
    "runtime_services.md",
    "can_hook.md",
    "fast_path.md",
    "api_compatibility.md",
    "debug_monitor.md",
    "keil_integration.md",
    "problem_summary.md",
    "file_index.md",
    "roadmap.md",
    "todolist.md",
    "README.md",
]

# Root markdown also embedded (linked from docs as ../README.md etc.)
ROOT_MD = [
    ("../README.md", ROOT / "README.md", "根 README"),
    ("../CHANGELOG.md", ROOT / "CHANGELOG.md", "变更"),
    ("../CONTRIBUTING.md", ROOT / "CONTRIBUTING.md", "贡献"),
]

SHORT = {
    "usage.md": "术语",
    "getting_started.md": "上手",
    "faq.md": "FAQ",
    "architecture.md": "架构",
    "design_decisions.md": "决策",
    "references.md": "对照",
    "porting_guide.md": "移植",
    "esp_idf_cmake.md": "ESP",
    "driver_guide.md": "驱动",
    "peripherals.md": "外设",
    "usb_tusb_port.md": "USB",
    "amp.md": "AMP",
    "osal_switching.md": "OSAL",
    "service_spec.md": "服务",
    "runtime_services.md": "运行时",
    "can_hook.md": "CAN",
    "fast_path.md": "热路径",
    "api_compatibility.md": "API",
    "debug_monitor.md": "调试",
    "keil_integration.md": "Keil",
    "problem_summary.md": "问题",
    "file_index.md": "索引",
    "roadmap.md": "路线",
    "todolist.md": "待办",
    "README.md": "目录",
    "../README.md": "根 README",
    "../CHANGELOG.md": "变更",
    "../CONTRIBUTING.md": "贡献",
}


def slug(name: str) -> str:
    if name.startswith("../"):
        return "doc-root-" + name[3:].replace(".md", "").lower().replace("_", "-")
    return "doc-" + name.replace(".md", "").replace("_", "-")


def heading_slug(title: str) -> str:
    """Match VS Code / github-slugger style used by docs TOC links.

    Punctuation is dropped (not turned into '-'); each space becomes '-'.
    So ``OSAL / 同步`` → ``osal--同步`` (slash removed → double space → ``--``).
    Underscores are kept (``DRIVER_REGISTER`` → ``driver_register``).
    """
    t = title.strip().lower()
    t = re.sub(r"[`*~]", "", t)
    # keep letters/digits/underscore/CJK/space/hyphen; drop . / （） etc.
    t = re.sub(r"[^\w\u4e00-\u9fff\- ]", "", t, flags=re.UNICODE)
    t = t.replace(" ", "-").strip("-")
    return t


def inline(
    text: str,
    doc_id: str,
    from_dir: Path,
    path_to_id: dict[Path, str],
) -> str:
    """Convert inline markdown; escape HTML first then restore markup."""
    codes: list[str] = []

    def save_code(m: re.Match[str]) -> str:
        codes.append(m.group(1))
        return f"\x00C{len(codes) - 1}\x00"

    text = re.sub(r"`([^`]+)`", save_code, text)
    text = html.escape(text)

    def link(m: re.Match[str]) -> str:
        label, url = m.group(1), m.group(2)
        href = url

        if url.startswith("#"):
            href = f"#{doc_id}-{url[1:]}"
        elif not url.startswith(("http://", "https://", "mailto:")):
            base = url.split("#", 1)
            path_part = base[0]
            frag = base[1] if len(base) > 1 else None
            if path_part.endswith(".md") or path_part.endswith(".html"):
                try:
                    target = (from_dir / path_part).resolve()
                except OSError:
                    target = None
                if target is not None and target in path_to_id:
                    href = "#" + path_to_id[target]
                    if frag:
                        href += "-" + frag
                elif path_part.endswith(".html") and target == OUT.resolve():
                    href = "#overview" if not frag else f"#{frag}"
        return f'<a href="{html.escape(href)}">{label}</a>'

    text = re.sub(r"\[([^\]]+)\]\(([^)]+)\)", link, text)
    text = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", text)
    text = re.sub(r"(?<!\*)\*([^*]+)\*(?!\*)", r"<em>\1</em>", text)

    for i, c in enumerate(codes):
        text = text.replace(f"\x00C{i}\x00", f"<code>{html.escape(c)}</code>")
    return text


def md_to_html(
    src: str,
    doc_id: str,
    from_dir: Path,
    path_to_id: dict[Path, str],
    skip_title: str | None = None,
) -> str:
    lines = src.replace("\r\n", "\n").split("\n")
    out: list[str] = []
    i = 0
    in_code = False
    code_lang = ""
    code_buf: list[str] = []
    in_ul = False
    in_ol = False
    in_table = False
    table_rows: list[list[str]] = []
    skipped_h1 = False

    def inl(s: str) -> str:
        return inline(s, doc_id, from_dir, path_to_id)

    def close_lists() -> None:
        nonlocal in_ul, in_ol
        if in_ul:
            out.append("</ul>")
            in_ul = False
        if in_ol:
            out.append("</ol>")
            in_ol = False

    def flush_table() -> None:
        nonlocal in_table, table_rows
        if not table_rows:
            return
        out.append('<div class="scroll"><table>')
        for ri, row in enumerate(table_rows):
            tag = "th" if ri == 0 else "td"
            # skip separator row |---|
            if ri == 1 and all(re.match(r"^:?-+:?$", c.strip()) for c in row):
                continue
            cells = "".join(f"<{tag}>{inl(c.strip())}</{tag}>" for c in row)
            out.append(f"<tr>{cells}</tr>")
        out.append("</table></div>")
        table_rows = []
        in_table = False

    while i < len(lines):
        line = lines[i]

        if in_code:
            if line.startswith("```"):
                lang = html.escape(code_lang) if code_lang else ""
                body = html.escape("\n".join(code_buf))
                out.append(f'<pre class="code"><code class="lang-{lang}">{body}</code></pre>')
                in_code = False
                code_buf = []
                code_lang = ""
            else:
                code_buf.append(line)
            i += 1
            continue

        if line.startswith("```"):
            close_lists()
            flush_table()
            in_code = True
            code_lang = line[3:].strip()
            code_buf = []
            i += 1
            continue

        # table
        if "|" in line and line.strip().startswith("|"):
            close_lists()
            cells = [c for c in line.strip().strip("|").split("|")]
            if not in_table:
                in_table = True
                table_rows = []
            table_rows.append(cells)
            i += 1
            continue
        else:
            flush_table()

        if not line.strip():
            close_lists()
            i += 1
            continue

        if line.strip() == "---":
            close_lists()
            out.append("<hr />")
            i += 1
            continue

        m = re.match(r"^(#{1,4})\s+(.*)$", line)
        if m:
            close_lists()
            level = len(m.group(1))
            title = m.group(2).strip()
            # Section panel already shows the MD H1 — skip once to avoid duplicate.
            if (
                not skipped_h1
                and level == 1
                and skip_title is not None
                and title == skip_title
            ):
                skipped_h1 = True
                i += 1
                continue
            # demote: page already has h1/h2 from shell; MD h1 -> h2, etc.
            h = min(level + 1, 5)
            hid = f"{doc_id}-{heading_slug(title)}"
            out.append(f'<h{h} id="{html.escape(hid)}">{inl(title)}</h{h}>')
            i += 1
            continue

        if line.lstrip().startswith("> "):
            close_lists()
            q = []
            while i < len(lines) and lines[i].lstrip().startswith(">"):
                q.append(re.sub(r"^>\s?", "", lines[i].lstrip()))
                i += 1
            out.append(f'<blockquote>{inl(" ".join(q))}</blockquote>')
            continue

        m = re.match(r"^(\s*)([-*]|\d+\.)\s+(.*)$", line)
        if m:
            ordered = m.group(2).endswith(".")
            content = m.group(3)
            if ordered:
                if in_ul:
                    out.append("</ul>")
                    in_ul = False
                if not in_ol:
                    out.append("<ol>")
                    in_ol = True
                out.append(f"<li>{inl(content)}</li>")
            else:
                if in_ol:
                    out.append("</ol>")
                    in_ol = False
                if not in_ul:
                    out.append("<ul>")
                    in_ul = True
                out.append(f"<li>{inl(content)}</li>")
            i += 1
            continue

        close_lists()
        # paragraph: merge consecutive non-blank non-special lines
        para = [line]
        i += 1
        while i < len(lines):
            nxt = lines[i]
            if (
                not nxt.strip()
                or nxt.startswith("#")
                or nxt.startswith("```")
                or nxt.strip() == "---"
                or nxt.lstrip().startswith("> ")
                or re.match(r"^(\s*)([-*]|\d+\.)\s+", nxt)
                or (nxt.strip().startswith("|") and "|" in nxt)
            ):
                break
            para.append(nxt)
            i += 1
        out.append(f"<p>{inl(' '.join(para))}</p>")

    close_lists()
    flush_table()
    if in_code:
        body = html.escape("\n".join(code_buf))
        out.append(f'<pre class="code"><code>{body}</code></pre>')
    return "\n".join(out)


CSS = r"""
    :root {
      --blue: #007aff;
      --blue-soft: rgba(0, 122, 255, 0.12);
      --indigo: #5856d6;
      --green: #34c759;
      --orange: #ff9500;
      --red: #ff3b30;
      --label: #1c1c1e;
      --sec: #3a3a3c;
      --ter: #8e8e93;
      --line: rgba(60, 60, 67, 0.12);
      --glass: rgba(255, 255, 255, 0.62);
      --glass2: rgba(255, 255, 255, 0.78);
      --border: rgba(255, 255, 255, 0.75);
      --shadow: 0 10px 40px rgba(15, 23, 42, 0.08);
      --font: -apple-system, BlinkMacSystemFont, "SF Pro Text", "PingFang SC",
        "Helvetica Neue", "Microsoft YaHei", sans-serif;
      --mono: ui-monospace, "SF Mono", Menlo, Consolas, monospace;
    }
    * { box-sizing: border-box; }
    html { scroll-behavior: smooth; scroll-padding-top: 76px; }
    body {
      margin: 0; font-family: var(--font); color: var(--label);
      background: #eef1f6; line-height: 1.55; -webkit-font-smoothing: antialiased;
    }
    code, .mono { font-family: var(--mono); font-size: 0.88em; }
    code {
      background: rgba(120,120,128,.14); padding: 1px 6px; border-radius: 6px;
    }
    a { color: var(--blue); text-decoration: none; }
    a:hover { text-decoration: underline; }
    .bg {
      position: fixed; inset: 0; z-index: -1;
      background:
        radial-gradient(900px 500px at 0% 0%, rgba(0,122,255,.22), transparent 60%),
        radial-gradient(700px 480px at 100% 10%, rgba(88,86,214,.18), transparent 55%),
        radial-gradient(600px 400px at 50% 100%, rgba(52,199,89,.1), transparent 50%),
        linear-gradient(180deg, #e7eef9, #eef1f6 45%, #e9edf4);
    }
    .top {
      position: sticky; top: 10px; z-index: 40; margin: 10px 14px 0;
      display: flex; align-items: center; gap: 6px; padding: 10px 12px;
      border-radius: 16px; background: var(--glass2);
      backdrop-filter: blur(28px) saturate(180%);
      -webkit-backdrop-filter: blur(28px) saturate(180%);
      border: 1px solid var(--border); box-shadow: var(--shadow);
      overflow-x: auto; scrollbar-width: none;
    }
    .top::-webkit-scrollbar { display: none; }
    .brand {
      font-weight: 750; font-size: 15px; letter-spacing: -.02em;
      padding: 0 8px; white-space: nowrap;
    }
    .top a.nav {
      white-space: nowrap; font-size: 12.5px; font-weight: 600; color: var(--blue);
      padding: 7px 10px; border-radius: 999px;
    }
    .top a.nav:hover, .top a.nav.on { background: var(--blue-soft); text-decoration: none; }
    .page { max-width: 920px; margin: 0 auto; padding: 18px 16px 80px; }
    .hero {
      margin-top: 16px; padding: 28px 26px 24px; border-radius: 24px;
      background: var(--glass);
      backdrop-filter: blur(30px) saturate(180%);
      -webkit-backdrop-filter: blur(30px) saturate(180%);
      border: 1px solid var(--border); box-shadow: var(--shadow);
    }
    .eyebrow {
      display: inline-block; font-size: 12px; font-weight: 700; color: var(--blue);
      background: var(--blue-soft); padding: 5px 11px; border-radius: 999px; margin-bottom: 12px;
    }
    .hero h1 {
      margin: 0 0 8px; font-size: clamp(28px, 5vw, 40px); font-weight: 780;
      letter-spacing: -.03em; line-height: 1.1;
    }
    .hero .lead { margin: 0; max-width: 46rem; color: var(--sec); font-size: 15.5px; }
    .hero-actions { display: flex; flex-wrap: wrap; gap: 8px; margin-top: 18px; }
    .btn {
      display: inline-flex; align-items: center; padding: 11px 16px; border-radius: 999px;
      font-size: 13.5px; font-weight: 650; border: none; cursor: pointer;
    }
    .btn.primary { background: var(--blue); color: #fff; }
    .btn.primary:hover { text-decoration: none; opacity: .92; }
    .btn.ghost { background: rgba(120,120,128,.14); color: var(--label); }
    .btn.ghost:hover { text-decoration: none; }
    .toc {
      margin-top: 18px; display: grid; grid-template-columns: repeat(3, 1fr); gap: 8px;
    }
    @media (max-width: 720px) { .toc { grid-template-columns: 1fr 1fr; } }
    .toc a {
      display: block; padding: 10px 12px; border-radius: 12px;
      background: rgba(255,255,255,.55); border: 1px solid rgba(255,255,255,.8);
      color: inherit; font-size: 13px; font-weight: 650;
    }
    .toc a:hover { text-decoration: none; background: rgba(255,255,255,.82); }
    .toc a span { display: block; font-size: 11px; color: var(--ter); font-weight: 600; margin-top: 2px; }
    .sec { margin-top: 28px; }
    .panel {
      padding: 22px 24px; border-radius: 22px; background: var(--glass);
      backdrop-filter: blur(26px) saturate(170%);
      -webkit-backdrop-filter: blur(26px) saturate(170%);
      border: 1px solid var(--border); box-shadow: var(--shadow);
    }
    .doc-head {
      display: flex; align-items: baseline; justify-content: space-between; gap: 12px;
      margin-bottom: 14px; padding-bottom: 12px; border-bottom: 1px solid var(--line);
    }
    .doc-head h2 { margin: 0; font-size: 22px; font-weight: 750; letter-spacing: -.02em; }
    .doc-head .src {
      font-size: 13px; font-weight: 650; white-space: nowrap; color: var(--ter);
    }
    .md-body h2 { font-size: 20px; margin: 1.4em 0 .5em; letter-spacing: -.02em; }
    .md-body h3 { font-size: 16.5px; margin: 1.25em 0 .45em; }
    .md-body h4 { font-size: 14.5px; margin: 1.1em 0 .4em; color: var(--sec); }
    .md-body h5 { font-size: 13.5px; margin: 1em 0 .35em; color: var(--sec); }
    .md-body p { margin: .65em 0; color: var(--sec); font-size: 14.5px; }
    .md-body ul, .md-body ol { margin: .5em 0 .8em; padding-left: 1.25em; color: var(--sec); font-size: 14.5px; }
    .md-body li { margin: .28em 0; }
    .md-body blockquote {
      margin: .8em 0; padding: 10px 14px; border-radius: 12px;
      background: var(--blue-soft); border-left: 3px solid var(--blue);
      color: var(--sec); font-size: 14px;
    }
    .md-body hr { border: 0; border-top: 1px solid var(--line); margin: 1.4em 0; }
    .md-body table { width: 100%; border-collapse: collapse; font-size: 13px; margin: .6em 0 1em; }
    .md-body th, .md-body td {
      text-align: left; padding: 9px 10px; border-bottom: 1px solid var(--line); vertical-align: top;
    }
    .md-body th {
      font-size: 11px; text-transform: uppercase; letter-spacing: .04em;
      color: var(--ter); font-weight: 750;
    }
    .md-body tr:last-child td { border-bottom: 0; }
    .scroll { overflow-x: auto; margin: .6em 0 1em; }
    pre.code {
      margin: .8em 0 1.1em; padding: 14px 16px; border-radius: 14px;
      background: #1c1c1e; color: #f2f2f7; overflow-x: auto;
      font-family: var(--mono); font-size: 12.5px; line-height: 1.5;
    }
    pre.code code { background: none; padding: 0; color: inherit; font-size: inherit; }
    .foot {
      margin-top: 28px; text-align: center; color: var(--ter); font-size: 12px;
    }
    .back-top {
      position: fixed; right: 18px; bottom: 18px; z-index: 30;
      width: 42px; height: 42px; border-radius: 50%;
      display: grid; place-items: center;
      background: var(--glass2); border: 1px solid var(--border); box-shadow: var(--shadow);
      backdrop-filter: blur(20px); color: var(--blue); font-weight: 750; text-decoration: none;
    }
    .back-top:hover { text-decoration: none; background: #fff; }
"""


def collect_docs() -> list[tuple[str, Path, str]]:
    """Return (logical_name, path, source_label)."""
    named = {p.name: p for p in DOCS.glob("*.md")}
    result: list[tuple[str, Path, str]] = []
    seen: set[str] = set()
    for name in ORDER:
        if name in named:
            result.append((name, named[name], name))
            seen.add(name)
    for name in sorted(named):
        if name not in seen:
            result.append((name, named[name], name))
    for logical, path, _short in ROOT_MD:
        if path.is_file():
            result.append((logical, path, logical))
    return result


def title_from_md(text: str, fallback: str) -> str:
    for line in text.splitlines():
        if line.startswith("# "):
            return line[2:].strip()
    return fallback


def main() -> None:
    docs = collect_docs()
    path_to_id: dict[Path, str] = {
        path.resolve(): slug(name) for name, path, _ in docs
    }

    nav_links = ['<div class="brand">mini_tree</div>', '<a class="nav" href="#overview">总览</a>']
    toc_cards: list[str] = []
    sections: list[str] = []

    for name, path, src_label in docs:
        sid = slug(name)
        text = path.read_text(encoding="utf-8")
        title = title_from_md(text, name)
        short = SHORT.get(name, name.replace(".md", "").replace("../", ""))
        nav_links.append(f'<a class="nav" href="#{sid}">{html.escape(short)}</a>')
        toc_cards.append(
            f'<a href="#{sid}">{html.escape(title)}'
            f'<span>{html.escape(src_label)}</span></a>'
        )
        body = md_to_html(
            text,
            sid,
            from_dir=path.parent,
            path_to_id=path_to_id,
            skip_title=title,
        )
        sections.append(
            f'''
    <section class="sec" id="{sid}">
      <div class="panel">
        <div class="doc-head">
          <h2>{html.escape(title)}</h2>
          <span class="src">来自 {html.escape(src_label)}</span>
        </div>
        <div class="md-body">
{body}
        </div>
      </div>
    </section>'''
        )

    html_out = f"""<!DOCTYPE html>
<html lang="zh-Hans">
<head>
  <meta charset="UTF-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1, viewport-fit=cover" />
  <title>mini_tree 文档全集</title>
  <style>
{CSS}
  </style>
</head>
<body>
  <div class="bg"></div>

  <nav class="top" id="top">
    {" ".join(nav_links)}
  </nav>

  <main class="page">
    <header class="hero" id="overview">
      <div class="eyebrow">docs/*.md + 根 README/CHANGELOG/CONTRIBUTING · 源文件仍保留</div>
      <h1>mini_tree 文档</h1>
      <p class="lead">
        本页把专题 Markdown 与仓库根 <code>README</code> / <code>CHANGELOG</code> / <code>CONTRIBUTING</code>
        <strong>原文内容</strong>嵌入同一磨玻璃页面。文内文档链接一律跳到本页对应章节。
      </p>
      <div class="hero-actions">
        <a class="btn primary" href="#{slug(docs[0][0])}">从第一篇开始</a>
        <a class="btn ghost" href="#{slug('README.md')}">文档目录</a>
        <a class="btn ghost" href="#{slug('../README.md')}">仓库 README</a>
      </div>
      <div class="toc">
        {"".join(toc_cards)}
      </div>
    </header>
{"".join(sections)}

    <p class="foot">
      由 <code>docs/_gen_overview.py</code> 从 Markdown 生成 · 修改文档后可再运行以刷新本页
    </p>
  </main>
  <a class="back-top" href="#overview" title="回顶">↑</a>
  <script>
    const links = [...document.querySelectorAll(".top a.nav")];
    const secs = links.map(a => document.querySelector(a.getAttribute("href"))).filter(Boolean);
    const io = new IntersectionObserver((entries) => {{
      entries.forEach(e => {{
        if (!e.isIntersecting) return;
        const id = "#" + e.target.id;
        links.forEach(a => a.classList.toggle("on", a.getAttribute("href") === id));
      }});
    }}, {{ rootMargin: "-20% 0px -70% 0px", threshold: 0 }});
    secs.forEach(s => io.observe(s));
  </script>
</body>
</html>
"""
    OUT.write_text(html_out, encoding="utf-8")
    print(f"Wrote {OUT} ({OUT.stat().st_size} bytes, {len(docs)} docs)")


if __name__ == "__main__":
    main()
