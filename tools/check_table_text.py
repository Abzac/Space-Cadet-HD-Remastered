"""Check the shipped EN/RU message catalog against the Space Cadet message windows."""
import ast
import re
import textwrap
from pathlib import Path

SOURCE = Path(__file__).resolve().parents[1] / "SpaceCadetPinball" / "translations.cpp"
SCORES = (1, 1000, 1000000, 2147483647)
COUNTS = (1, 8, 15, 75)
MISSIONS = tuple(f"STRING{n}" for n in range(161, 178))
RANKS = tuple(f"STRING{n}" for n in range(185, 194))
SYMBOLS = re.compile(r"%ld|%d|%s")


def catalog():
    text = SOURCE.read_text(encoding="utf-8")
    texts = re.split(r"\n\s*Msg::(\w+),\s*\n\s*\{", text)
    result = {}
    for name, block in zip(texts[1::2], texts[2::2]):
        result[name] = {
            lang: ast.literal_eval(value)
            for lang, value in re.findall(r'\{\s*Lang::(English|Russian),\s*("(?:\\.|[^"\\])*")\s*\}', block)
        }
    return result


def table_messages():
    cabinet_ids = [*range(101, 137), *range(145, 202), *range(205, 257),
                   *range(273, 278), *range(280, 289)]
    return [*(f"STRING{n}" for n in cabinet_ids), "ControlJackpotDoubled"]


def examples(name, language, strings):
    text = strings[name][language]
    markers = SYMBOLS.findall(text)
    if not markers:
        return [text]
    if markers == ["%s"]:
        values = {"STRING180": MISSIONS, "STRING207": MISSIONS,
                  "STRING184": RANKS, "STRING273": RANKS, "STRING274": RANKS}[name]
        return [text % strings[value][language] for value in values]
    assert markers in (["%d"], ["%ld"]), (name, language, markers)
    return [text % n for n in (SCORES if markers == ["%ld"] else COUNTS)]


def layout(text, columns):
    lines = []
    for part in text.splitlines():
        lines.extend(textwrap.wrap(part, width=columns, break_long_words=False, break_on_hyphens=False) or [""])
    return lines


def run():
    strings = catalog()
    keys = table_messages()
    for name in keys:
        for lang in ("English", "Russian"):
            assert strings[name].get(lang), (name, lang, "no text")
        assert SYMBOLS.findall(strings[name]["English"]) == SYMBOLS.findall(strings[name]["Russian"]), name
    for lang in ("Russian", "English"):
        tested = 0
        largest = (0, "", "")
        most_lines = (0, "")
        for name in keys:
            for text in examples(name, lang, strings):
                tested += 1
                assert text == text.strip() and all(part == part.strip() for part in text.splitlines()), (name, text)
                assert "%" not in text and "\ufffd" not in text, (name, text)
                lines = layout(text, 20)
                largest = max(largest, (len(text.encode("utf-8")), name, text))
                most_lines = max(most_lines, (len(lines), name))
                # INFO has 3 rows, MISSION has 7 at the normal matrix pitch.
                # Mission-only templates can use its extra rows.
                rows = 7 if name == "STRING207" or name.startswith("STRING") and 205 <= int(name[6:]) <= 256 else 3
                if lang == "Russian":
                    assert all(len(line) <= 20 for line in lines), (name, "unbreakable word", lines)
                    assert len(lines) <= rows, (name, rows, lines)
                    if "%d" in strings[name][lang]:
                        assert lines[-1] != lines[-1].split()[-1], (name, "orphaned mission counter", lines)
                else:
                    # The English catalog preserves period wording; the display
                    # also offers a denser layout when it needs a fourth row.
                    compact = layout(text, 26)
                    assert len(compact) <= (4 if rows == 3 else 9) and all(len(s) <= 26 for s in compact), (name, compact)
        print(f"{lang}: {len(keys)} catalog entries, {tested} expanded messages; "
              f"largest {largest[0]} UTF-8 bytes ({largest[1]}); up to {most_lines[0]} rows at 20 columns ({most_lines[1]}).")


if __name__ == "__main__":
    run()
