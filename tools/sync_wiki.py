#!/usr/bin/env python3
"""Копирует рецепты (recipes/*/README.md) в вики wiki.kitslab.ru (Starlight).

Единый источник — README в этом репозитории: страницы вики
src/content/docs/boards/bh723/recipes/ перезаписываются целиком, руками их не правят.

    python tools/sync_wiki.py [путь к вики]   # по умолчанию ../../сайты/wiki от репозитория

Скрипт берёт заголовок «# ...» в title, первый абзац — в description, переписывает
ссылки между рецептами на адреса вики, а остальные относительные — на GitHub.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
RECIPES = ROOT / "recipes"
GITHUB = "https://github.com/Kits-Lab-Dev/Board-H723"
WIKI_DIR = "boards/bh723/recipes"
DEFAULT_WIKI = ROOT.parent.parent / "сайты" / "wiki"

HEADER = "<!-- Сгенерировано tools/sync_wiki.py из {src} (репозиторий Board-H723) — не править здесь -->\n\n"
LINK = re.compile(r"\]\(([^)\s]+)\)")


def yaml_str(s):
    return '"' + s.replace("\\", "\\\\").replace('"', '\\"') + '"'


def plain(md):
    """Текст абзаца без разметки — для description."""
    md = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", md)
    md = md.replace("`", "").replace("**", "")
    return " ".join(md.split())


def convert(readme, name, order):
    """README.md рецепта → страница вики (frontmatter + тело)."""
    text = readme.read_text(encoding="utf-8").replace("\r\n", "\n")
    lines = text.split("\n")
    assert lines[0].startswith("# "), f"{readme}: первая строка должна быть «# Заголовок»"
    title = lines[0][2:].strip()
    body = "\n".join(lines[1:]).strip("\n")

    first_par = body.split("\n\n")[0]
    description = plain(first_par)
    if len(description) > 200:
        description = description[:197].rsplit(" ", 1)[0] + "…"

    src_dir = readme.parent.relative_to(ROOT).as_posix()

    def fix_link(m):
        url = m.group(1)
        if re.match(r"^[a-z]+:|^#|^/", url):
            return m.group(0)
        anchor = ""
        if "#" in url:
            url, anchor = url.split("#", 1)
            anchor = "#" + anchor
        target = (readme.parent / url).resolve()
        # README другого рецепта → страница вики
        if target.name == "README.md" and target.parent.parent == RECIPES:
            return f"](/{WIKI_DIR}/{target.parent.name}/{anchor})"
        if target == RECIPES / "README.md":
            return f"](/{WIKI_DIR}/{anchor})"
        rel = target.relative_to(ROOT).as_posix()
        kind = "tree" if target.is_dir() else "blob"
        return f"]({GITHUB}/{kind}/main/{rel}{anchor})"

    body = LINK.sub(fix_link, body)
    src_link = f"{GITHUB}/tree/main/{src_dir}"
    if name:
        body += f"\n\n---\n\nИсходники рецепта: [{src_dir}]({src_link})\n"
    else:
        body += f"\n\n---\n\nРецепты в репозитории: [{src_dir}]({src_link})\n"

    front = (
        "---\n"
        f"title: {yaml_str(title)}\n"
        f"description: {yaml_str(description)}\n"
        f"sidebar:\n  order: {order}\n"
    )
    if not name:
        front += "  label: Все рецепты\n"
    front += "---\n\n"
    return front + HEADER.format(src=readme.relative_to(ROOT).as_posix()) + body


def main():
    wiki = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT_WIKI
    docs = wiki / "src" / "content" / "docs"
    if not docs.is_dir():
        sys.exit(f"Не найдена вики: {docs}")
    out = docs / WIKI_DIR
    out.mkdir(parents=True, exist_ok=True)

    # порядок — как в таблице recipes/README.md
    index_text = (RECIPES / "README.md").read_text(encoding="utf-8")
    names = re.findall(r"\]\(([a-z0-9_]+)/README\.md\)", index_text)
    names += sorted(p.parent.name for p in RECIPES.glob("*/README.md") if p.parent.name not in names)

    written = {"index.md"}
    (out / "index.md").write_text(convert(RECIPES / "README.md", None, 0), encoding="utf-8")
    for i, name in enumerate(names, 1):
        page = f"{name}.md"
        (out / page).write_text(convert(RECIPES / name / "README.md", name, i), encoding="utf-8")
        written.add(page)
    for old in out.glob("*.md*"):
        if old.name not in written:
            old.unlink()
            print("удалено", old.name)
    print(f"{out}: {len(written)} страниц ({', '.join(['index'] + names)})")


if __name__ == "__main__":
    main()
